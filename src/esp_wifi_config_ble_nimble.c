/**
 * @file esp_wifi_config_ble_nimble.c
 * @brief NimBLE host bootstrap for the Improv-WiFi BLE transport
 *
 * Brings up the BLE controller and NimBLE host, registers the Improv GATT
 * service, and drives Improv-compliant advertising. The custom JSON-over-GATT
 * service that previously lived here has been removed in favour of ESP-IDF
 * Network Provisioning (see esp_wifi_config_prov_ble.c).
 *
 * This file is only compiled when CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE and
 * CONFIG_BT_NIMBLE_ENABLED are both set.
 */

#include "sdkconfig.h"

#if defined(CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE) && defined(CONFIG_BT_NIMBLE_ENABLED)

#include "esp_wifi_config_ble_int.h"
#include "esp_wifi_config_improv.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_bt.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "wifi_cfg_ble_nb";

// =============================================================================
// State
// =============================================================================

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static char s_device_name[32];

/** true if we initialized the NimBLE stack ourselves; false if it was already running. */
static bool s_ble_stack_owned = false;

/** Set true while advertising should be active. Gated against the GAP disconnect
 * handler so a graceful stop doesn't immediately re-arm advertising via the
 * auto-restart path. */
static bool s_advertising_desired = false;

/** Set true once the NimBLE host has synced with the controller. start_advertising()
 * is a no-op until this is true; on_sync drives the first start. */
static bool s_ble_synced = false;

// Forward declarations
static void nimble_host_task(void *param);
static void ble_on_sync(void);
static void ble_on_reset(int reason);
static int gap_event_handler(struct ble_gap_event *event, void *arg);
static void start_advertising(void);

// =============================================================================
// Advertising — Improv layout
// =============================================================================
//
// Improv spec requires the service UUID and service-data field in the
// primary advertisement. NimBLE's high-level ble_hs_adv_fields helper does
// not support service data, so the primary adv is built as raw AD bytes.
// The scan response carries the device name.

static void start_advertising(void)
{
    if (!s_ble_synced || !s_advertising_desired) {
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 0x20;
    adv_params.itvl_max = 0x40;

    static const uint8_t improv_uuid_le[] = IMPROV_BLE_SVC_UUID_128;
    uint8_t adv_buf[31];
    int pos = 0;

    // Flags (3 bytes)
    adv_buf[pos++] = 2;
    adv_buf[pos++] = 0x01;  // AD type: Flags
    adv_buf[pos++] = 0x06;  // General Discoverable + BR/EDR Not Supported

    // Complete List of 128-bit UUIDs (18 bytes)
    adv_buf[pos++] = 17;
    adv_buf[pos++] = 0x07;  // AD type: Complete List of 128-bit UUIDs
    memcpy(&adv_buf[pos], improv_uuid_le, 16);
    pos += 16;

    // Service Data - 16 bit UUID (10 bytes): UUID16 0x4677 + state + caps + 4 reserved
    adv_buf[pos++] = 9;
    adv_buf[pos++] = 0x16;  // AD type: Service Data - 16 bit UUID
    adv_buf[pos++] = 0x77;  // UUID16 0x4677 in little-endian
    adv_buf[pos++] = 0x46;
    adv_buf[pos++] = wifi_cfg_improv_get_state();
    adv_buf[pos++] = wifi_cfg_improv_get_capabilities();
    adv_buf[pos++] = 0x00;  // Reserved
    adv_buf[pos++] = 0x00;
    adv_buf[pos++] = 0x00;
    adv_buf[pos++] = 0x00;

    int rc = ble_gap_adv_set_data(adv_buf, pos);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set raw adv data, rc=%d", rc);
        return;
    }

    // Scan response: device name (Complete Local Name)
    size_t name_len = strlen(s_device_name);
    if (name_len > 28) name_len = 28; // 31 - 2 header - 1 type
    uint8_t rsp_buf[31];
    int rpos = 0;

    rsp_buf[rpos++] = (uint8_t)(name_len + 1);
    rsp_buf[rpos++] = 0x09;  // AD type: Complete Local Name
    memcpy(&rsp_buf[rpos], s_device_name, name_len);
    rpos += name_len;

    rc = ble_gap_adv_rsp_set_data(rsp_buf, rpos);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to set scan response data, rc=%d", rc);
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                            &adv_params, gap_event_handler, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Failed to start advertising, rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising started: %s", s_device_name);
    }
}

// =============================================================================
// GAP Event Handler
// =============================================================================

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "BLE client connected, conn_handle %d", s_conn_handle);

                struct ble_gap_upd_params conn_params = {
                    .itvl_min = 24,
                    .itvl_max = 48,
                    .latency = 0,
                    .supervision_timeout = 600,
                    .min_ce_len = 0,
                    .max_ce_len = 0,
                };
                ble_gap_update_params(s_conn_handle, &conn_params);

                extern void wifi_cfg_improv_ble_on_connect_nimble(uint16_t conn_handle);
                wifi_cfg_improv_ble_on_connect_nimble(s_conn_handle);
            } else {
                ESP_LOGE(TAG, "Connection failed, status %d", event->connect.status);
                start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT: {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGI(TAG, "BLE client disconnected, reason %d (0x%03x)",
                     event->disconnect.reason, event->disconnect.reason);
            extern void wifi_cfg_improv_ble_on_disconnect_nimble(void);
            wifi_cfg_improv_ble_on_disconnect_nimble();

            if (s_advertising_desired) {
                start_advertising();
            }
            break;
        }

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGD(TAG, "Advertising complete");
            if (s_advertising_desired) {
                start_advertising();
            }
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU changed to %d", event->mtu.value);
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(TAG, "Encryption change, status=%d", event->enc_change.status);
            if (event->enc_change.status != 0) {
                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                    ble_store_util_delete_peer(&desc.peer_id_addr);
                }
            }
            break;

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            struct ble_gap_conn_desc desc;
            ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        case BLE_GAP_EVENT_CONN_UPDATE:
            ESP_LOGD(TAG, "Conn params update, status=%d", event->conn_update.status);
            break;

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            if (event->passkey.params.action == BLE_SM_IOACT_NONE) {
                struct ble_sm_io pk = { .action = BLE_SM_IOACT_NONE };
                ble_sm_inject_io(event->passkey.conn_handle, &pk);
            }
            break;

        case BLE_GAP_EVENT_NOTIFY_TX:
            ESP_LOGD(TAG, "Notify TX: handle=%d status=%d",
                     event->notify_tx.attr_handle, event->notify_tx.status);
            break;

        case BLE_GAP_EVENT_DATA_LEN_CHG:
            ESP_LOGD(TAG, "Data length changed: tx=%d rx=%d",
                     event->data_len_chg.max_tx_octets,
                     event->data_len_chg.max_rx_octets);
            break;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
        case BLE_GAP_EVENT_LINK_ESTAB:
            ESP_LOGD(TAG, "Link established");
            break;

        case BLE_GAP_EVENT_PARING_COMPLETE:
            ESP_LOGI(TAG, "Pairing complete, status=%d",
                     event->pairing_complete.status);
            break;
#endif

        default:
            ESP_LOGD(TAG, "Unhandled GAP event: %d", event->type);
            break;
    }
    return 0;
}

// =============================================================================
// NimBLE Host Callbacks
// =============================================================================

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address, rc=%d", rc);
        return;
    }

    // Drop stale bonds left in NVS from a previous flash.
    ble_store_clear();

    ble_gap_write_sugg_def_data_len(27, 328);

    s_ble_synced = true;
    start_advertising();
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// =============================================================================
// Backend Interface Implementation
// =============================================================================

uint16_t wifi_cfg_ble_backend_get_mtu(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return 0;
    }
    return ble_att_mtu(s_conn_handle);
}

bool wifi_cfg_ble_backend_is_stack_running(void)
{
    return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
}

esp_err_t wifi_cfg_ble_backend_init(const char *device_name)
{
    strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
    s_device_name[sizeof(s_device_name) - 1] = '\0';

    if (wifi_cfg_ble_backend_is_stack_running()) {
        s_ble_stack_owned = false;
        ESP_LOGI(TAG, "NimBLE stack already running, registering service only");

        // Reset GATT db so we can re-register the GAP + Improv services.
        int reset_rc = ble_gatts_reset();
        if (reset_rc != 0) {
            ESP_LOGW(TAG, "ble_gatts_reset failed, rc=%d", reset_rc);
        }
    } else {
        s_ble_stack_owned = true;

        esp_err_t ret = nimble_port_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    int mtu_rc = ble_att_set_preferred_mtu(517);
    if (mtu_rc != 0) {
        ESP_LOGW(TAG, "Failed to set preferred MTU, rc=%d", mtu_rc);
    }

    ble_svc_gap_init();
    ble_svc_gap_device_name_set(s_device_name);

    // Improv GATT service definition lives in esp_wifi_config_improv_ble.c.
    extern const struct ble_gatt_svc_def wifi_cfg_improv_nimble_svcs[];
    int rc = ble_gatts_count_cfg(wifi_cfg_improv_nimble_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Improv ble_gatts_count_cfg failed, rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(wifi_cfg_improv_nimble_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Improv ble_gatts_add_svcs failed, rc=%d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Improv BLE GATT service registered");

    if (!s_ble_stack_owned) {
        int start_rc = ble_gatts_start();
        if (start_rc != 0) {
            ESP_LOGE(TAG, "ble_gatts_start (service-only commit) failed, rc=%d", start_rc);
            return ESP_FAIL;
        }
    }

    if (s_ble_stack_owned) {
        nimble_port_freertos_init(nimble_host_task);
    } else {
        s_ble_synced = true;
    }

    return ESP_OK;
}

esp_err_t wifi_cfg_ble_backend_start(void)
{
    s_advertising_desired = true;
    start_advertising();
    return ESP_OK;
}

esp_err_t wifi_cfg_ble_backend_stop(void)
{
    s_advertising_desired = false;

    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        for (int i = 0; i < 50 && s_conn_handle != BLE_HS_CONN_HANDLE_NONE; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    ble_gap_adv_stop();
    return ESP_OK;
}

esp_err_t wifi_cfg_ble_backend_deinit(void)
{
    s_advertising_desired = false;

    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        for (int i = 0; i < 50 && s_conn_handle != BLE_HS_CONN_HANDLE_NONE; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    ble_gap_adv_stop();

    int rc = ble_gatts_reset();
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gatts_reset failed, rc=%d", rc);
    }
    ble_svc_gap_init();
    ble_svc_gap_device_name_set(s_device_name);
    ble_gatts_start();

    if (s_ble_stack_owned) {
        rc = nimble_port_stop();
        if (rc != 0) {
            ESP_LOGE(TAG, "nimble_port_stop failed, rc=%d", rc);
        }

        rc = nimble_port_deinit();
        if (rc != 0) {
            ESP_LOGE(TAG, "nimble_port_deinit failed, rc=%d", rc);
        }
    }

    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_ble_stack_owned = false;
    s_ble_synced = false;

    return ESP_OK;
}

#endif // CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE && CONFIG_BT_NIMBLE_ENABLED
