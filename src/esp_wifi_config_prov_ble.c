/**
 * @file esp_wifi_config_prov_ble.c
 * @brief ESP-IDF Network/Wi-Fi Provisioning backend (BLE scheme)
 *
 * Wraps Espressif's wifi_provisioning manager so the library's higher-level
 * lifecycle (provisioning_mode, retry/backoff, multi-network store, custom
 * variables, post-provisioning HTTP behaviour) drives a stock, audited
 * provisioning protocol instead of a hand-rolled JSON-over-GATT service.
 *
 * Custom protocomm endpoints (registered automatically):
 *
 *   - "esp-wifi-config-version"       — JSON: library/IDF/firmware versions
 *   - "esp-wifi-config-capabilities"  — JSON: enabled features
 *   - "esp-wifi-config-vars"          — JSON: read/write the variable store
 *   - "esp-wifi-config-network-policy"— JSON: provisioning_mode, retries
 *
 * Additional endpoints may be supplied through
 * wifi_cfg_prov_config_t.custom_endpoints — they are created before
 * wifi_prov_mgr_start_provisioning() and their handlers are registered
 * afterwards, matching the ESP-IDF ordering requirement.
 *
 * Lifecycle
 * ---------
 * The library always calls wifi_prov_mgr_disable_auto_stop(), so the manager
 * never tears itself down implicitly. Stop is driven from
 * wifi_cfg_stop_provisioning() (library lifecycle) or from
 * wifi_cfg_prov_stop() directly. `cleanup_delay_ms` controls the grace
 * window the manager honours between a stop request and protocomm
 * shutdown — useful for letting custom-endpoint exchanges complete after
 * Wi-Fi credentials are accepted.
 *
 * IDF version notes
 * -----------------
 * On ESP-IDF 5.x the includes are the in-tree wifi_provisioning component
 * (`wifi_provisioning/manager.h`, `wifi_provisioning/scheme_ble.h`).
 *
 * On ESP-IDF 6.x the in-tree component is being retired in favour of the
 * external `espressif/network_provisioning` managed component, which has
 * the same shape but renamed types/functions (`network_prov_mgr_*`,
 * `wifi_prov_scheme_ble` -> `network_prov_scheme_ble`). The compatibility
 * macros below isolate the rename so the rest of the file is version-clean.
 * See MIGRATION.md for the recommended idf_component.yml change.
 */

#include "sdkconfig.h"

#if defined(CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING) && \
    defined(CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE)

#include "esp_wifi_config_priv.h"
#include "esp_bus.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_bt.h"
#include "esp_coexist.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "protocomm_ble.h"
#include "cJSON.h"
#include <string.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0) && __has_include("network_provisioning/manager.h")
// IDF 6.x with the migrated managed component.
#  include "network_provisioning/manager.h"
#  include "network_provisioning/scheme_ble.h"
#  define WIFI_PROV_MGR_INIT             network_prov_mgr_init
#  define WIFI_PROV_MGR_DEINIT           network_prov_mgr_deinit
#  define WIFI_PROV_MGR_CONFIG_T         network_prov_mgr_config_t
#  define WIFI_PROV_MGR_START            network_prov_mgr_start_provisioning
#  define WIFI_PROV_MGR_STOP             network_prov_mgr_stop_provisioning
#  define WIFI_PROV_MGR_IS_PROVISIONED   network_prov_mgr_is_wifi_provisioned
#  define WIFI_PROV_MGR_RESET_PROV       network_prov_mgr_reset_provisioning
#  define WIFI_PROV_MGR_RESET_SM         network_prov_mgr_reset_sm_state_for_reprovision
#  define WIFI_PROV_MGR_ENDPOINT_CREATE  network_prov_mgr_endpoint_create
#  define WIFI_PROV_MGR_ENDPOINT_REGISTER network_prov_mgr_endpoint_register
#  define WIFI_PROV_MGR_DISABLE_AUTO_STOP network_prov_mgr_disable_auto_stop
#  define WIFI_PROV_MGR_KEEP_BLE_ON      network_prov_mgr_keep_ble_on
#  define WIFI_PROV_MGR_SET_APP_INFO     network_prov_mgr_set_app_info
#  define WIFI_PROV_SCHEME_BLE           network_prov_scheme_ble
#  define WIFI_PROV_SCHEME_BLE_SET_SERVICE_UUID network_prov_scheme_ble_set_service_uuid
#  define WIFI_PROV_SCHEME_BLE_SET_MFG_DATA     network_prov_scheme_ble_set_mfg_data
#  define WIFI_PROV_SCHEME_BLE_SET_RANDOM_ADDR  network_prov_scheme_ble_set_random_addr
#  define WIFI_PROV_EVENT_BASE           NETWORK_PROV_EVENT
#  define WIFI_PROV_EVT_INIT             NETWORK_PROV_INIT
#  define WIFI_PROV_EVT_START            NETWORK_PROV_START
#  define WIFI_PROV_EVT_END              NETWORK_PROV_END
#  define WIFI_PROV_EVT_CRED_RECV        NETWORK_PROV_WIFI_CRED_RECV
#  define WIFI_PROV_EVT_CRED_FAIL        NETWORK_PROV_WIFI_CRED_FAIL
#  define WIFI_PROV_EVT_CRED_SUCCESS     NETWORK_PROV_WIFI_CRED_SUCCESS
#  define WIFI_PROV_EVT_DEINIT           NETWORK_PROV_DEINIT
#  define WIFI_PROV_SECURITY_T           network_prov_security_t
#  define WIFI_PROV_SECURITY_0           NETWORK_PROV_SECURITY_0
#  define WIFI_PROV_SECURITY_1           NETWORK_PROV_SECURITY_1
#  define WIFI_PROV_SECURITY_2           NETWORK_PROV_SECURITY_2
#  define WIFI_PROV_SCHEME_BLE_HANDLER_FREE_BTDM NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM
#  define WIFI_PROV_SCHEME_BLE_HANDLER_FREE_BLE  NETWORK_PROV_SCHEME_HANDLER_FREE_BLE
#  define WIFI_PROV_SCHEME_BLE_HANDLER_FREE_BT   NETWORK_PROV_SCHEME_HANDLER_FREE_BT
#  define WIFI_PROV_SCHEME_EVT_HANDLER_NONE      NETWORK_PROV_EVENT_HANDLER_NONE
#  define WIFI_PROV_FAIL_REASON_T        network_prov_wifi_sta_fail_reason_t
#  define WIFI_PROV_STA_AUTH_ERROR       NETWORK_PROV_WIFI_STA_AUTH_ERROR
#else
// IDF 5.x in-tree wifi_provisioning component.
#  include "wifi_provisioning/manager.h"
#  include "wifi_provisioning/scheme_ble.h"
#  define WIFI_PROV_MGR_INIT             wifi_prov_mgr_init
#  define WIFI_PROV_MGR_DEINIT           wifi_prov_mgr_deinit
#  define WIFI_PROV_MGR_CONFIG_T         wifi_prov_mgr_config_t
#  define WIFI_PROV_MGR_START            wifi_prov_mgr_start_provisioning
#  define WIFI_PROV_MGR_STOP             wifi_prov_mgr_stop_provisioning
#  define WIFI_PROV_MGR_IS_PROVISIONED   wifi_prov_mgr_is_provisioned
#  define WIFI_PROV_MGR_RESET_PROV       wifi_prov_mgr_reset_provisioning
#  define WIFI_PROV_MGR_RESET_SM         wifi_prov_mgr_reset_sm_state_for_reprovision
#  define WIFI_PROV_MGR_ENDPOINT_CREATE  wifi_prov_mgr_endpoint_create
#  define WIFI_PROV_MGR_ENDPOINT_REGISTER wifi_prov_mgr_endpoint_register
#  define WIFI_PROV_MGR_DISABLE_AUTO_STOP wifi_prov_mgr_disable_auto_stop
#  define WIFI_PROV_MGR_KEEP_BLE_ON      wifi_prov_mgr_keep_ble_on
#  define WIFI_PROV_MGR_SET_APP_INFO     wifi_prov_mgr_set_app_info
#  define WIFI_PROV_SCHEME_BLE           wifi_prov_scheme_ble
#  define WIFI_PROV_SCHEME_BLE_SET_SERVICE_UUID wifi_prov_scheme_ble_set_service_uuid
#  define WIFI_PROV_SCHEME_BLE_SET_MFG_DATA     wifi_prov_scheme_ble_set_mfg_data
#  define WIFI_PROV_SCHEME_BLE_SET_RANDOM_ADDR  wifi_prov_scheme_ble_set_random_addr
#  define WIFI_PROV_EVENT_BASE           WIFI_PROV_EVENT
#  define WIFI_PROV_EVT_INIT             WIFI_PROV_INIT
#  define WIFI_PROV_EVT_START            WIFI_PROV_START
#  define WIFI_PROV_EVT_END              WIFI_PROV_END
#  define WIFI_PROV_EVT_CRED_RECV        WIFI_PROV_CRED_RECV
#  define WIFI_PROV_EVT_CRED_FAIL        WIFI_PROV_CRED_FAIL
#  define WIFI_PROV_EVT_CRED_SUCCESS     WIFI_PROV_CRED_SUCCESS
#  define WIFI_PROV_EVT_DEINIT           WIFI_PROV_DEINIT
#  define WIFI_PROV_SECURITY_T           wifi_prov_security_t
#  define WIFI_PROV_SECURITY_0           WIFI_PROV_SECURITY_0
#  define WIFI_PROV_SECURITY_1           WIFI_PROV_SECURITY_1
#  define WIFI_PROV_SECURITY_2           WIFI_PROV_SECURITY_2
#  define WIFI_PROV_SCHEME_BLE_HANDLER_FREE_BTDM WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
#  define WIFI_PROV_SCHEME_BLE_HANDLER_FREE_BLE  WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BLE
#  define WIFI_PROV_SCHEME_BLE_HANDLER_FREE_BT   WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BT
#  define WIFI_PROV_SCHEME_EVT_HANDLER_NONE      WIFI_PROV_EVENT_HANDLER_NONE
#  define WIFI_PROV_FAIL_REASON_T        wifi_prov_sta_fail_reason_t
#  define WIFI_PROV_STA_AUTH_ERROR       WIFI_PROV_STA_AUTH_ERROR
#endif

static const char *TAG = "wifi_cfg_prov";

// =============================================================================
// State
// =============================================================================

static bool s_prov_initialized = false;     // wifi_prov_mgr_init has been called
static bool s_prov_active      = false;     // wifi_prov_mgr_start_provisioning succeeded
static int  s_failed_attempts  = 0;         // counted across CRED_FAIL events
static bool s_coex_pref_set    = false;     // tracks whether we biased coex toward BT

// IDF 5.5.3 NimBLE workaround state — see on_protocomm_ble_disconnect().
static bool     s_creds_received  = false;  // set in WIFI_PROV_EVT_CRED_RECV
static bool     s_restart_pending = false;  // restart queued, waiting for END
static bool     s_explicit_stop   = false;  // app-driven stop, not a bug recovery
static bool     s_disconnect_handler_registered = false;
static uint32_t s_restart_count   = 0;      // diagnostic: how many bug recoveries

// Reboot-on-success backstop. Started in WIFI_PROV_EVT_CRED_SUCCESS;
// fires esp_restart() if the BLE client never disconnects cleanly.
static TimerHandle_t s_reboot_timer = NULL;

// =============================================================================
// Resolve helpers (config → effective value with Kconfig fallback)
// =============================================================================

static const char *resolve_device_name_template(void)
{
    if (g_wifi_cfg && g_wifi_cfg->config.prov_ble.device_name && g_wifi_cfg->config.prov_ble.device_name[0]) {
        return g_wifi_cfg->config.prov_ble.device_name;
    }
    return "PROV_{id}";
}

static const char *resolve_pop(void)
{
    // NULL or empty string both mean "no PoP".
    if (g_wifi_cfg && g_wifi_cfg->config.prov_ble.pop && g_wifi_cfg->config.prov_ble.pop[0]) {
        return g_wifi_cfg->config.prov_ble.pop;
    }
    return NULL;
}

// Note: the Security 2 (SRP6a) username is supplied by the *client*
// during the handshake and never flows into wifi_prov_mgr from the
// device side. The salt and verifier baked into firmware were derived
// from a username+password pair offline; the client must use the same
// username when re-deriving its proof. The `security2_username` field
// on wifi_cfg_prov_config_t is kept as metadata so applications can
// surface the expected username through their own UI/endpoints.

static WIFI_PROV_SECURITY_T resolve_security(void)
{
    wifi_cfg_prov_security_t want = WIFI_CFG_PROV_SECURITY_DEFAULT;
    if (g_wifi_cfg) want = g_wifi_cfg->config.prov_ble.security;

    switch (want) {
        case WIFI_CFG_PROV_SECURITY_0: return WIFI_PROV_SECURITY_0;
        case WIFI_CFG_PROV_SECURITY_2: return WIFI_PROV_SECURITY_2;
        case WIFI_CFG_PROV_SECURITY_1:
        case WIFI_CFG_PROV_SECURITY_DEFAULT:
        default:                       return WIFI_PROV_SECURITY_1;
    }
}

// Map memory_policy enum to a wifi_prov_event_handler_t. If the BT
// controller is already enabled at start time we assume the application
// owns the stack and override to KEEP_ALL — freeing memory underneath an
// active host would fault.
static wifi_prov_event_handler_t resolve_scheme_event_handler(void)
{
    wifi_cfg_prov_memory_policy_t policy = WIFI_CFG_PROV_MEM_FREE_BTDM;
    if (g_wifi_cfg) policy = g_wifi_cfg->config.prov_ble.memory_policy;

    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED &&
        policy != WIFI_CFG_PROV_MEM_KEEP_ALL) {
        ESP_LOGW(TAG, "BT controller already enabled; forcing memory_policy=KEEP_ALL");
        policy = WIFI_CFG_PROV_MEM_KEEP_ALL;
    }

    switch (policy) {
        case WIFI_CFG_PROV_MEM_FREE_BLE: {
            wifi_prov_event_handler_t h = WIFI_PROV_SCHEME_BLE_HANDLER_FREE_BLE;
            return h;
        }
        case WIFI_CFG_PROV_MEM_FREE_BT: {
            wifi_prov_event_handler_t h = WIFI_PROV_SCHEME_BLE_HANDLER_FREE_BT;
            return h;
        }
        case WIFI_CFG_PROV_MEM_KEEP_ALL: {
            wifi_prov_event_handler_t h = WIFI_PROV_SCHEME_EVT_HANDLER_NONE;
            return h;
        }
        case WIFI_CFG_PROV_MEM_FREE_BTDM:
        default: {
            wifi_prov_event_handler_t h = WIFI_PROV_SCHEME_BLE_HANDLER_FREE_BTDM;
            return h;
        }
    }
}

static const char *chip_variant_str(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    switch (info.model) {
        case CHIP_ESP32:   return "esp32";
        case CHIP_ESP32S2: return "esp32s2";
        case CHIP_ESP32S3: return "esp32s3";
        case CHIP_ESP32C3: return "esp32c3";
        case CHIP_ESP32C6: return "esp32c6";
        case CHIP_ESP32H2: return "esp32h2";
        default:           return "unknown";
    }
}

// =============================================================================
// Init-time validation
// =============================================================================

esp_err_t wifi_cfg_prov_validate(const wifi_cfg_prov_config_t *prov)
{
    if (!prov) return ESP_OK;

    // Security 2 needs a salt + verifier; we don't silently fall back.
    // DEFAULT resolves to Security 1, so only an explicit Security 2 needs
    // these checks.
    if (prov->security == WIFI_CFG_PROV_SECURITY_2) {
        bool have_salt     = prov->security2_salt && prov->security2_salt_len > 0;
        bool have_verifier = prov->security2_verifier && prov->security2_verifier_len > 0;
        if (!have_salt || !have_verifier) {
            ESP_LOGE(TAG, "Security 2 selected but salt/verifier missing — "
                          "derive them offline with wifi_prov_sec2_get_salt_and_verifier() "
                          "and pass them through wifi_cfg_prov_config_t.");
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Manufacturer data must fit alongside the device name in the
    // 31-byte BLE scan response. The IDF rule is:
    //   (mfg_data_len + 2) < 31 - (name_len + 2)
    // We can't know the runtime-expanded name length cheaply here, but
    // we can reject obviously oversized payloads.
    if (prov->manufacturer_data_len > 25) {
        ESP_LOGE(TAG, "manufacturer_data_len=%zu exceeds BLE scan-response budget",
                 prov->manufacturer_data_len);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

// =============================================================================
// Custom protocomm endpoints
// =============================================================================
//
// Each endpoint receives a request buffer (raw bytes from the provisioning
// client) and must allocate the response buffer with malloc; protocomm
// frees it via free(). All four endpoints exchange small JSON payloads.

#define PROV_ENDPOINT_VERSION       "esp-wifi-config-version"
#define PROV_ENDPOINT_CAPABILITIES  "esp-wifi-config-capabilities"
#define PROV_ENDPOINT_VARS          "esp-wifi-config-vars"
#define PROV_ENDPOINT_NETWORK_POLICY "esp-wifi-config-network-policy"

#define PROV_LIB_VERSION_STRING     "esp_wifi_config 0.1.0"

static esp_err_t make_json_response(cJSON *doc, uint8_t **out, ssize_t *out_len)
{
    char *str = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!str) return ESP_ERR_NO_MEM;

    size_t len = strlen(str);
    *out = (uint8_t *)str;
    *out_len = (ssize_t)len;
    return ESP_OK;
}

static esp_err_t version_endpoint(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv)
{
    (void)session_id; (void)inbuf; (void)inlen; (void)priv;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "lib", PROV_LIB_VERSION_STRING);
    cJSON_AddStringToObject(root, "idf", IDF_VER);

    const esp_app_desc_t *app = esp_app_get_description();
    if (app) {
        cJSON_AddStringToObject(root, "app", app->project_name);
        cJSON_AddStringToObject(root, "fw_version", app->version);
        cJSON_AddStringToObject(root, "compile_time", app->time);
    }

    if (g_wifi_cfg && g_wifi_cfg->config.prov_ble.firmware_version) {
        cJSON_AddStringToObject(root, "firmware_version",
                                g_wifi_cfg->config.prov_ble.firmware_version);
    }

    cJSON_AddStringToObject(root, "chip", chip_variant_str());
    return make_json_response(root, outbuf, outlen);
}

static esp_err_t capabilities_endpoint(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                        uint8_t **outbuf, ssize_t *outlen, void *priv)
{
    (void)session_id; (void)inbuf; (void)inlen; (void)priv;

    cJSON *root = cJSON_CreateObject();
    cJSON *caps = cJSON_AddArrayToObject(root, "capabilities");
    cJSON_AddItemToArray(caps, cJSON_CreateString("multi-network"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("custom-vars"));
#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL
    cJSON_AddItemToArray(caps, cJSON_CreateString("improv-serial"));
#endif
#ifdef CONFIG_WIFI_CFG_ENABLE_WEBUI
    cJSON_AddItemToArray(caps, cJSON_CreateString("webui"));
#endif
#ifdef CONFIG_WIFI_CFG_ENABLE_CLI
    cJSON_AddItemToArray(caps, cJSON_CreateString("cli"));
#endif
    if (g_wifi_cfg && g_wifi_cfg->config.enable_ap) {
        cJSON_AddItemToArray(caps, cJSON_CreateString("softap"));
    }

    cJSON_AddNumberToObject(root, "max_networks", WIFI_CFG_MAX_NETWORKS);
    cJSON_AddNumberToObject(root, "max_vars",     WIFI_CFG_MAX_VARS);
    return make_json_response(root, outbuf, outlen);
}

static esp_err_t vars_endpoint(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                uint8_t **outbuf, ssize_t *outlen, void *priv)
{
    (void)session_id; (void)priv;

    if (!inbuf || inlen <= 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "error", "empty_request");
        return make_json_response(root, outbuf, outlen);
    }

    cJSON *req = cJSON_ParseWithLength((const char *)inbuf, inlen);
    if (!req) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "error", "bad_json");
        return make_json_response(root, outbuf, outlen);
    }

    const char *op = cJSON_GetStringValue(cJSON_GetObjectItem(req, "op"));
    cJSON *resp = cJSON_CreateObject();

    if (!op || strcmp(op, "list") == 0) {
        cJSON *arr = cJSON_AddArrayToObject(resp, "vars");
        wifi_cfg_lock();
        if (g_wifi_cfg) {
            for (size_t i = 0; i < g_wifi_cfg->var_count; i++) {
                cJSON *item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "k", g_wifi_cfg->vars[i].key);
                cJSON_AddStringToObject(item, "v", g_wifi_cfg->vars[i].value);
                cJSON_AddItemToArray(arr, item);
            }
        }
        wifi_cfg_unlock();
    } else if (strcmp(op, "get") == 0) {
        const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(req, "key"));
        if (!key) {
            cJSON_AddStringToObject(resp, "error", "missing_key");
        } else {
            char val[128] = {0};
            esp_err_t err = wifi_cfg_get_var(key, val, sizeof(val));
            if (err != ESP_OK) {
                cJSON_AddStringToObject(resp, "error", "not_found");
            } else {
                cJSON_AddStringToObject(resp, "key", key);
                cJSON_AddStringToObject(resp, "value", val);
            }
        }
    } else if (strcmp(op, "set") == 0) {
        const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(req, "key"));
        const char *value = cJSON_GetStringValue(cJSON_GetObjectItem(req, "value"));
        if (!key || !value) {
            cJSON_AddStringToObject(resp, "error", "missing_key_or_value");
        } else {
            esp_err_t err = wifi_cfg_set_var(key, value);
            if (err == ESP_OK) {
                cJSON_AddBoolToObject(resp, "ok", true);
            } else {
                cJSON_AddStringToObject(resp, "error",
                    (err == ESP_ERR_NO_MEM) ? "store_full" : "rejected");
            }
        }
    } else if (strcmp(op, "del") == 0) {
        const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(req, "key"));
        if (!key) {
            cJSON_AddStringToObject(resp, "error", "missing_key");
        } else {
            esp_err_t err = wifi_cfg_del_var(key);
            cJSON_AddBoolToObject(resp, "ok", err == ESP_OK);
            if (err != ESP_OK) {
                cJSON_AddStringToObject(resp, "error", "not_found");
            }
        }
    } else {
        cJSON_AddStringToObject(resp, "error", "unknown_op");
    }

    cJSON_Delete(req);
    return make_json_response(resp, outbuf, outlen);
}

static const char *prov_mode_str(wifi_provisioning_mode_t mode)
{
    switch (mode) {
        case WIFI_PROV_ALWAYS:             return "always";
        case WIFI_PROV_ON_FAILURE:         return "on_failure";
        case WIFI_PROV_WHEN_UNPROVISIONED: return "when_unprovisioned";
        case WIFI_PROV_MANUAL:             return "manual";
        default:                           return "unknown";
    }
}

static esp_err_t network_policy_endpoint(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                          uint8_t **outbuf, ssize_t *outlen, void *priv)
{
    (void)session_id; (void)inbuf; (void)inlen; (void)priv;

    cJSON *root = cJSON_CreateObject();
    if (g_wifi_cfg) {
        cJSON_AddStringToObject(root, "provisioning_mode",
                                prov_mode_str(g_wifi_cfg->config.provisioning_mode));
        cJSON_AddNumberToObject(root, "max_retry_per_network",
                                g_wifi_cfg->config.max_retry_per_network);
        cJSON_AddNumberToObject(root, "retry_interval_ms",
                                g_wifi_cfg->config.retry_interval_ms);
        cJSON_AddNumberToObject(root, "retry_max_interval_ms",
                                g_wifi_cfg->config.retry_max_interval_ms);
        cJSON_AddBoolToObject(root, "auto_reconnect",
                              g_wifi_cfg->config.auto_reconnect);
        cJSON_AddNumberToObject(root, "max_reconnect_attempts",
                                g_wifi_cfg->config.max_reconnect_attempts);
        cJSON_AddNumberToObject(root, "saved_networks", g_wifi_cfg->network_count);
    }
    return make_json_response(root, outbuf, outlen);
}

// =============================================================================
// BLE disconnect workaround (IDF 5.5.3 NimBLE reconnect bug)
// =============================================================================
//
// On IDF 5.5.3 NimBLE, only the first BLE client to connect after boot can
// complete a provisioning session. Subsequent reconnects accept at LL,
// optionally exchange MTU, then hit supervision timeout — and the broken
// state only clears on full reboot. The bug reproduces deterministically
// across sec0 and sec1 with both the Espressif iOS app and esp_prov.
//
// Workaround: tear down and re-init the provisioning manager whenever a BLE
// client disconnects before credentials have been received. The disconnect
// handler queues a restart (s_restart_pending), the existing WIFI_PROV_EVT_END
// path runs MGR_DEINIT(), then we re-enter wifi_cfg_prov_start(). Edge cases
// (creds-already-received, explicit stop, already-pending) are short-circuited
// by guards below.

// True when the reboot-on-success behavior is active (default-on
// unless the app explicitly opted out via the config flag).
static bool reboot_on_success_enabled(void)
{
    return g_wifi_cfg && !g_wifi_cfg->config.prov_ble.disable_reboot_on_provisioning_success;
}

static void reboot_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    ESP_LOGW(TAG, "Reboot backstop fired (BLE client never disconnected); restarting");
    esp_restart();
}

static void on_protocomm_ble_disconnect(void *arg, esp_event_base_t base,
                                        int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;

    if (!s_prov_active) {
        ESP_LOGD(TAG, "BLE disconnect: ignored (prov mgr not active)");
        return;
    }

    // Reboot-on-success path: if credentials were delivered before the
    // client disconnected, the provisioning flow is over and the
    // cleanest recovery is a reboot. esp_restart() doesn't return, so
    // we don't bother coordinating with the backstop timer.
    if (s_creds_received && reboot_on_success_enabled()) {
        ESP_LOGI(TAG, "Provisioning complete; client disconnected, rebooting");
        // Brief delay so the final log line drains and any in-flight
        // protocomm response finishes flushing before the reboot.
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
    }

    if (g_wifi_cfg && g_wifi_cfg->config.prov_ble.disable_disconnect_restart) {
        ESP_LOGD(TAG, "BLE disconnect: ignored (workaround disabled by config)");
        return;
    }
    if (s_explicit_stop) {
        ESP_LOGD(TAG, "BLE disconnect: ignored (explicit stop in progress)");
        return;
    }
    if (s_creds_received) {
        ESP_LOGD(TAG, "BLE disconnect: ignored (credentials already received)");
        return;
    }
    if (s_restart_pending) {
        ESP_LOGD(TAG, "BLE disconnect: ignored (restart already pending)");
        return;
    }

    s_restart_count++;
    ESP_LOGW(TAG, "BLE client disconnected mid-flow (restart #%lu); restarting "
                  "prov mgr (workaround for IDF NimBLE reconnect bug)",
             (unsigned long)s_restart_count);
    s_restart_pending = true;
    // Async: WIFI_PROV_EVT_END fires after cleanup_delay_ms; the restart
    // happens from prov_event_handler when END arrives.
    WIFI_PROV_MGR_STOP();
}

// =============================================================================
// Provisioning event handler
// =============================================================================

static void prov_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != WIFI_PROV_EVENT_BASE) return;

    const wifi_cfg_prov_config_t *prov_cfg = g_wifi_cfg ? &g_wifi_cfg->config.prov_ble : NULL;

    switch (id) {
        case WIFI_PROV_EVT_INIT:
            ESP_LOGI(TAG, "Provisioning initialised");
            break;

        case WIFI_PROV_EVT_START:
            ESP_LOGI(TAG, "Provisioning started");
            s_prov_active = true;
            s_failed_attempts = 0;
            break;

        case WIFI_PROV_EVT_CRED_RECV: {
            wifi_sta_config_t *cfg = (wifi_sta_config_t *)data;
            if (!cfg) break;

            // Mark creds received so the BLE disconnect workaround doesn't
            // restart the manager when the client drops after sending them.
            s_creds_received = true;

            wifi_cfg_prov_creds_t creds = {0};
            // wifi_sta_config_t.ssid is fixed-size and may not be NUL-terminated.
            memcpy(creds.ssid, cfg->ssid,
                   sizeof(cfg->ssid) > sizeof(creds.ssid) - 1
                       ? sizeof(creds.ssid) - 1 : sizeof(cfg->ssid));
            memcpy(creds.password, cfg->password,
                   sizeof(cfg->password) > sizeof(creds.password) - 1
                       ? sizeof(creds.password) - 1 : sizeof(cfg->password));
            ESP_LOGI(TAG, "Credentials received for %s", creds.ssid);

            // Persist into the multi-network store. Upsert on duplicate.
            wifi_network_t net = {0};
            strncpy(net.ssid, creds.ssid, sizeof(net.ssid) - 1);
            strncpy(net.password, creds.password, sizeof(net.password) - 1);
            net.priority = 10;
            esp_err_t err = wifi_cfg_add_network(&net);
            if (err == ESP_ERR_INVALID_STATE) {
                wifi_cfg_update_network(&net);
            }

            esp_bus_emit(WIFI_MODULE, WIFI_CFG_EVT_PROV_CRED_RECV, &creds, sizeof(creds));
            if (prov_cfg && prov_cfg->on_credentials_received) {
                prov_cfg->on_credentials_received(&creds, prov_cfg->event_ctx);
            }
            break;
        }

        case WIFI_PROV_EVT_CRED_FAIL: {
            WIFI_PROV_FAIL_REASON_T *reason = (WIFI_PROV_FAIL_REASON_T *)data;
            int reason_val = reason ? (int)*reason : -1;
            ESP_LOGW(TAG, "Provisioning failed, reason=%d", reason_val);
            if (reason && *reason == WIFI_PROV_STA_AUTH_ERROR) {
                ESP_LOGW(TAG, "Bad password — accepting another attempt");
            }
            s_failed_attempts++;
            if (prov_cfg && prov_cfg->reset_on_failure) {
                int limit = prov_cfg->max_failed_attempts ? prov_cfg->max_failed_attempts : 3;
                if (s_failed_attempts >= limit) {
                    ESP_LOGW(TAG, "Too many failed attempts, resetting state machine");
                    WIFI_PROV_MGR_RESET_SM();
                    s_failed_attempts = 0;
                }
            }
            esp_bus_emit(WIFI_MODULE, WIFI_CFG_EVT_PROV_CRED_FAIL, &reason_val, sizeof(reason_val));
            if (prov_cfg && prov_cfg->on_credentials_failed) {
                prov_cfg->on_credentials_failed(reason_val, prov_cfg->event_ctx);
            }
            break;
        }

        case WIFI_PROV_EVT_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning credentials accepted");
            s_failed_attempts = 0;
            esp_bus_emit(WIFI_MODULE, WIFI_CFG_EVT_PROV_CRED_SUCCESS, NULL, 0);
            if (prov_cfg && prov_cfg->on_credentials_success) {
                prov_cfg->on_credentials_success(prov_cfg->event_ctx);
            }
            if (reboot_on_success_enabled()) {
                // Backstop reboot: fires if the BLE client never
                // disconnects on its own. The disconnect handler
                // reboots first when the client behaves; this just
                // guarantees we don't get stuck if it doesn't.
                uint32_t wait = (prov_cfg && prov_cfg->reboot_max_wait_ms)
                                ? prov_cfg->reboot_max_wait_ms : 3000;
                if (s_reboot_timer) {
                    xTimerChangePeriod(s_reboot_timer, pdMS_TO_TICKS(wait), 0);
                    xTimerStart(s_reboot_timer, 0);
                    ESP_LOGI(TAG, "Reboot scheduled in %lu ms (or sooner on client disconnect)",
                             (unsigned long)wait);
                }
            } else if (prov_cfg && prov_cfg->stop_after_success) {
                WIFI_PROV_MGR_STOP();
            }
            break;

        case WIFI_PROV_EVT_END:
            ESP_LOGI(TAG, "Provisioning finished");
            s_prov_active = false;
            // Auto-stop is always disabled — END only fires after the
            // library-level lifecycle calls wifi_cfg_prov_stop(). Deinit
            // here finalises the protocomm teardown.
            if (s_prov_initialized) {
                WIFI_PROV_MGR_DEINIT();
                s_prov_initialized = false;
            }
            // Coex bias was raised in favour of BT during provisioning so
            // BLE GATT survives concurrent Wi-Fi scans (see wifi_cfg_prov_start).
            // The BLE link is no longer load-bearing here — restore balanced
            // arbitration so post-provisioning Wi-Fi throughput isn't penalised.
            if (s_coex_pref_set) {
                esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
                s_coex_pref_set = false;
            }
            // BLE disconnect workaround: if the manager was torn down because
            // a client dropped mid-flow, re-arm it now that protocomm is
            // fully shut down. Defer to wifi_cfg_task so the heavy MGR_INIT
            // (which pulls in NimBLE host init) runs with that task's stack
            // rather than the sys_evt task's — calling wifi_cfg_prov_start()
            // directly here overflows sys_evt's stack.
            if (s_restart_pending) {
                s_restart_pending = false;
                s_creds_received  = false;
                ESP_LOGI(TAG, "Queueing prov mgr restart after BLE disconnect");
                wifi_cfg_send_event(WM_INT_EVT_PROV_BLE_RESTART);
            }
            break;

        case WIFI_PROV_EVT_DEINIT:
            ESP_LOGI(TAG, "Provisioning deinitialised");
            s_prov_active = false;
            s_prov_initialized = false;
            break;

        default:
            break;
    }
}

// =============================================================================
// Public lifecycle
// =============================================================================

esp_err_t wifi_cfg_prov_init(void)
{
    if (s_prov_initialized) {
        return ESP_OK;
    }

    esp_err_t err = esp_event_handler_register(WIFI_PROV_EVENT_BASE,
                                               ESP_EVENT_ANY_ID,
                                               &prov_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "register prov event handler: %s", esp_err_to_name(err));
    }

    // Reboot-on-success backstop timer (one-shot, period set on start).
    if (!s_reboot_timer) {
        s_reboot_timer = xTimerCreate("prov_reboot", pdMS_TO_TICKS(3000),
                                      pdFALSE, NULL, reboot_timer_callback);
        if (!s_reboot_timer) {
            ESP_LOGW(TAG, "Failed to create reboot backstop timer");
        }
    }

    return ESP_OK;
}

esp_err_t wifi_cfg_prov_deinit(void)
{
    // Full library teardown — the app is shutting us down, so the BLE
    // disconnect workaround must not queue a restart from any in-flight
    // disconnect event, and the reboot backstop must not fire either.
    s_explicit_stop   = true;
    s_restart_pending = false;

    if (s_reboot_timer) {
        xTimerStop(s_reboot_timer, 0);
        xTimerDelete(s_reboot_timer, 0);
        s_reboot_timer = NULL;
    }

    if (s_prov_active) {
        WIFI_PROV_MGR_STOP();
        s_prov_active = false;
    }
    if (s_prov_initialized) {
        WIFI_PROV_MGR_DEINIT();
        s_prov_initialized = false;
    }
    // Force-teardown path: WIFI_PROV_EVT_END may not get a chance to run, so
    // restore coex here too. No-op if already restored by the event handler.
    if (s_coex_pref_set) {
        esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
        s_coex_pref_set = false;
    }
    if (s_disconnect_handler_registered) {
        esp_event_handler_unregister(PROTOCOMM_TRANSPORT_BLE_EVENT,
                                     PROTOCOMM_TRANSPORT_BLE_DISCONNECTED,
                                     &on_protocomm_ble_disconnect);
        s_disconnect_handler_registered = false;
    }
    esp_event_handler_unregister(WIFI_PROV_EVENT_BASE,
                                 ESP_EVENT_ANY_ID,
                                 &prov_event_handler);
    // Reset workaround state so a later init+start session begins clean.
    s_creds_received  = false;
    s_explicit_stop   = false;
    s_restart_count   = 0;
    return ESP_OK;
}

bool wifi_cfg_prov_is_active(void)
{
    return s_prov_active;
}

esp_err_t wifi_cfg_prov_start(void)
{
    if (s_prov_active) {
        return ESP_OK;
    }
    if (!g_wifi_cfg) {
        return ESP_ERR_INVALID_STATE;
    }

    // Fresh start: clear the workaround flags carried over from a prior
    // session. s_restart_pending is cleared by the END handler that
    // re-entered us; this just defends against stale state.
    s_explicit_stop  = false;
    s_creds_received = false;

    const wifi_cfg_prov_config_t *prov = &g_wifi_cfg->config.prov_ble;

    // Scheme-level customisations must be applied before wifi_prov_mgr_init,
    // since scheme_ble caches them at init time.
    if (prov->service_uuid128) {
        // The IDF prototype is non-const; the bytes are only read but we
        // cast through to satisfy the signature.
        WIFI_PROV_SCHEME_BLE_SET_SERVICE_UUID((uint8_t *)prov->service_uuid128);
    }
    if (prov->manufacturer_data && prov->manufacturer_data_len > 0) {
        WIFI_PROV_SCHEME_BLE_SET_MFG_DATA((uint8_t *)prov->manufacturer_data,
                                          (ssize_t)prov->manufacturer_data_len);
    }
    if (prov->random_addr) {
        WIFI_PROV_SCHEME_BLE_SET_RANDOM_ADDR(prov->random_addr);
    }

    WIFI_PROV_MGR_CONFIG_T cfg = {
        .scheme = WIFI_PROV_SCHEME_BLE,
        .scheme_event_handler = resolve_scheme_event_handler(),
        .wifi_prov_conn_cfg = {
            .wifi_conn_attempts = prov->wifi_conn_attempts,
        },
    };

    esp_err_t err = WIFI_PROV_MGR_INIT(cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_prov_initialized = true;

    // Always disable auto-stop. The library's higher-level lifecycle
    // (stop_provisioning_on_connect + provisioning_teardown_delay_ms)
    // drives the stop, and cleanup_delay_ms becomes the protocomm grace.
    uint32_t cleanup = prov->cleanup_delay_ms ? prov->cleanup_delay_ms : 1000;
    if (cleanup < 100) cleanup = 100;
    WIFI_PROV_MGR_DISABLE_AUTO_STOP(cleanup);

    if (prov->keep_ble_on_after_stop) {
        WIFI_PROV_MGR_KEEP_BLE_ON(1);
    }

    // App-info metadata exposed on the standard proto-ver endpoint.
    // wifi_prov_mgr_set_app_info() takes `const char **` (non-const inner
    // pointer); the cast is safe because the manager only reads the array.
    for (size_t i = 0; i < prov->app_info_count; i++) {
        const wifi_cfg_prov_app_info_t *info = &prov->app_infos[i];
        if (!info->label) continue;
        WIFI_PROV_MGR_SET_APP_INFO(info->label, info->version,
                                   (const char **)info->capabilities,
                                   info->capability_count);
    }

    // Built-in endpoints — create BEFORE start so the manager includes
    // them in the initial protocol set.
    WIFI_PROV_MGR_ENDPOINT_CREATE(PROV_ENDPOINT_VERSION);
    WIFI_PROV_MGR_ENDPOINT_CREATE(PROV_ENDPOINT_CAPABILITIES);
    WIFI_PROV_MGR_ENDPOINT_CREATE(PROV_ENDPOINT_VARS);
    WIFI_PROV_MGR_ENDPOINT_CREATE(PROV_ENDPOINT_NETWORK_POLICY);

    // User-supplied custom endpoints
    for (size_t i = 0; i < prov->custom_endpoint_count; i++) {
        if (!prov->custom_endpoints[i].name) continue;
        WIFI_PROV_MGR_ENDPOINT_CREATE(prov->custom_endpoints[i].name);
    }

    WIFI_PROV_SECURITY_T sec = resolve_security();
    const void *sec_params = NULL;
    const char *sec1_pop = NULL;
    static wifi_prov_security2_params_t sec2_params;

    if (sec == WIFI_PROV_SECURITY_2) {
        // Validation in wifi_cfg_prov_validate() guarantees these pointers
        // are non-NULL when Security 2 is selected. protocomm declares
        // salt/verifier as `const char *` even though they're opaque
        // bytes — the cast is purely cosmetic.
        sec2_params.salt         = (const char *)prov->security2_salt;
        sec2_params.salt_len     = (uint16_t)prov->security2_salt_len;
        sec2_params.verifier     = (const char *)prov->security2_verifier;
        sec2_params.verifier_len = (uint16_t)prov->security2_verifier_len;
        sec_params = (const void *)&sec2_params;
    } else if (sec == WIFI_PROV_SECURITY_1) {
        sec1_pop = resolve_pop();
        sec_params = (const void *)sec1_pop;
    }

    char device_name[32];
    wifi_cfg_expand_template(resolve_device_name_template(),
                             device_name, sizeof(device_name));

    // Bias Wi-Fi/BT coexistence toward BT for the duration of provisioning.
    // The iOS "ESP BLE Provisioning" app drives a full-channel active Wi-Fi
    // scan over the prov-scan endpoint; with balanced coex on the single
    // 2.4 GHz transceiver the scan starves BLE LL packets long enough to
    // trip the link supervision timeout (HCI 0x08), and the iOS app appears
    // to hang on the "Scanning for WiFi" screen. PREFER_BT keeps the GATT
    // link alive through the scan window. Restored to BALANCE in
    // WIFI_PROV_EVT_END once protocomm has fully torn down.
    {
        esp_err_t coex_err = esp_coex_preference_set(ESP_COEX_PREFER_BT);
        if (coex_err == ESP_OK) {
            s_coex_pref_set = true;
            ESP_LOGI(TAG, "coex preference set to PREFER_BT");
        } else {
            ESP_LOGW(TAG, "coex preference set returned %s (0x%x); BLE may starve during Wi-Fi scan",
                     esp_err_to_name(coex_err), coex_err);
        }
    }

    ESP_LOGI(TAG, "starting prov mgr: sec=%d device=%s", (int)sec, device_name);
    err = WIFI_PROV_MGR_START(sec, sec_params, device_name, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_start: %s", esp_err_to_name(err));
        WIFI_PROV_MGR_DEINIT();
        s_prov_initialized = false;
        if (s_coex_pref_set) {
            esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
            s_coex_pref_set = false;
        }
        return err;
    }

    // Register endpoint handlers AFTER start (per Espressif sample code).
    WIFI_PROV_MGR_ENDPOINT_REGISTER(PROV_ENDPOINT_VERSION,        version_endpoint,        NULL);
    WIFI_PROV_MGR_ENDPOINT_REGISTER(PROV_ENDPOINT_CAPABILITIES,   capabilities_endpoint,   NULL);
    WIFI_PROV_MGR_ENDPOINT_REGISTER(PROV_ENDPOINT_VARS,           vars_endpoint,           NULL);
    WIFI_PROV_MGR_ENDPOINT_REGISTER(PROV_ENDPOINT_NETWORK_POLICY, network_policy_endpoint, NULL);

    for (size_t i = 0; i < prov->custom_endpoint_count; i++) {
        const wifi_cfg_prov_custom_endpoint_t *ep = &prov->custom_endpoints[i];
        if (!ep->name || !ep->handler) continue;
        WIFI_PROV_MGR_ENDPOINT_REGISTER(ep->name, ep->handler, ep->user_ctx);
    }

    // Subscribe to BLE transport disconnects for the IDF NimBLE workaround.
    // Registered here (after MGR_START) so the protocomm BLE transport is
    // guaranteed up. Guarded by s_disconnect_handler_registered because
    // restart cycles call _start() repeatedly without _deinit() in between.
    if (!s_disconnect_handler_registered) {
        esp_err_t derr = esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT,
                                                    PROTOCOMM_TRANSPORT_BLE_DISCONNECTED,
                                                    &on_protocomm_ble_disconnect,
                                                    NULL);
        if (derr == ESP_OK) {
            s_disconnect_handler_registered = true;
        } else {
            ESP_LOGW(TAG, "register protocomm BLE disconnect handler: %s — "
                          "NimBLE reconnect workaround disabled this session",
                     esp_err_to_name(derr));
        }
    }

    s_prov_active = true;
    ESP_LOGI(TAG, "Provisioning advertising as %s", device_name);
    return ESP_OK;
}

esp_err_t wifi_cfg_prov_stop(void)
{
    if (!s_prov_active && !s_prov_initialized) {
        return ESP_OK;
    }
    // Signal the BLE disconnect handler not to queue a restart: this stop
    // was app-driven, not a bug recovery. Must be set before MGR_STOP()
    // because protocomm tears down the link synchronously and the
    // disconnect event may arrive before this function returns.
    s_explicit_stop = true;
    // Clear any restart that the disconnect handler already queued — the
    // app is taking over, so the workaround should yield.
    s_restart_pending = false;
    // wifi_prov_mgr_stop_provisioning() is asynchronous when auto-stop is
    // disabled: it schedules teardown after cleanup_delay_ms and fires
    // WIFI_PROV_EVT_END once protocomm is fully torn down. The event
    // handler calls wifi_prov_mgr_deinit() at that point.
    WIFI_PROV_MGR_STOP();
    s_prov_active = false;
    return ESP_OK;
}

#else // !(CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING && CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE)

#include "esp_wifi_config_priv.h"

esp_err_t wifi_cfg_prov_init(void)   { return ESP_OK; }
esp_err_t wifi_cfg_prov_deinit(void) { return ESP_OK; }
esp_err_t wifi_cfg_prov_start(void)  { return ESP_OK; }
esp_err_t wifi_cfg_prov_stop(void)   { return ESP_OK; }
bool      wifi_cfg_prov_is_active(void) { return false; }
esp_err_t wifi_cfg_prov_validate(const wifi_cfg_prov_config_t *prov) { (void)prov; return ESP_OK; }

#endif
