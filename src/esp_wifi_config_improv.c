/**
 * @file esp_wifi_config_improv.c
 * @brief Improv WiFi protocol core — state machine, RPC handlers, event listener
 *
 * Transport-agnostic. Serial and BLE transports feed raw bytes in;
 * this module processes RPC commands and invokes wifi_manager public API.
 */

#include "sdkconfig.h"

// Note: CONFIG_WIFI_CFG_ENABLE_IMPROV is derived inside esp_wifi_config_priv.h
// from the transport flags, but priv.h hasn't been included yet — gate on the
// transport flags directly.
#if defined(CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE) || defined(CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL)

#include "esp_wifi_config_improv.h"
#include "esp_wifi_config_priv.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_bus.h"
#include <string.h>

static const char *TAG = "wifi_cfg_improv";

// =============================================================================
// State
// =============================================================================

static improv_state_t s_state = IMPROV_STATE_AUTHORIZED;
static improv_error_t s_error = IMPROV_ERROR_NONE;

#define MAX_STATE_CBS 2
static struct {
    improv_state_change_cb_t cb;
    void *ctx;
} s_state_cbs[MAX_STATE_CBS];
static int s_state_cb_count = 0;

// esp_bus subscription IDs (for unsubscribe on deinit)
static int s_sub_connected = -1;
static int s_sub_disconnected = -1;

// =============================================================================
// Helpers: build Improv TLV strings
// =============================================================================

/**
 * Append a length-prefixed string to buf at *offset.
 * Returns false if it would overflow.
 */
static bool append_tlv_string(uint8_t *buf, size_t buf_size, size_t *offset,
                              const char *str)
{
    size_t slen = str ? strlen(str) : 0;
    if (*offset + 1 + slen > buf_size) return false;
    buf[(*offset)++] = (uint8_t)slen;
    if (slen > 0) {
        memcpy(buf + *offset, str, slen);
        *offset += slen;
    }
    return true;
}

// =============================================================================
// State Management
// =============================================================================

improv_state_t wifi_cfg_improv_get_state(void)
{
    return s_state;
}

improv_error_t wifi_cfg_improv_get_error(void)
{
    return s_error;
}

uint8_t wifi_cfg_improv_get_capabilities(void)
{
    uint8_t caps = IMPROV_CAPABILITY_DEVICE_INFO | IMPROV_CAPABILITY_WIFI_SCAN;

    if (g_wifi_cfg && g_wifi_cfg->config.improv.on_identify) {
        caps |= IMPROV_CAPABILITY_IDENTIFY;
    }

    return caps;
}

void wifi_cfg_improv_set_state(improv_state_t state)
{
    if (s_state == state) return;
    s_state = state;
    ESP_LOGI(TAG, "State -> %d", state);

    for (int i = 0; i < s_state_cb_count; i++) {
        if (s_state_cbs[i].cb) {
            s_state_cbs[i].cb(s_state, s_error, s_state_cbs[i].ctx);
        }
    }
}

void wifi_cfg_improv_set_error(improv_error_t error)
{
    s_error = error;
    for (int i = 0; i < s_state_cb_count; i++) {
        if (s_state_cbs[i].cb) {
            s_state_cbs[i].cb(s_state, s_error, s_state_cbs[i].ctx);
        }
    }
}

void wifi_cfg_improv_register_state_cb(improv_state_change_cb_t cb, void *ctx)
{
    if (s_state_cb_count < MAX_STATE_CBS) {
        s_state_cbs[s_state_cb_count].cb = cb;
        s_state_cbs[s_state_cb_count].ctx = ctx;
        s_state_cb_count++;
    }
}

// =============================================================================
// RPC Handlers
// =============================================================================

/**
 * Build an RPC result packet: [cmd_id, total_len, ...TLV strings...]
 */
static void send_rpc_result(uint8_t cmd_id, const uint8_t *payload, size_t payload_len,
                            improv_response_cb_t cb, void *ctx)
{
    uint8_t buf[256];
    if (2 + payload_len > sizeof(buf)) return;

    buf[0] = cmd_id;
    buf[1] = (uint8_t)payload_len;
    if (payload_len > 0) {
        memcpy(buf + 2, payload, payload_len);
    }
    cb(IMPROV_SERIAL_TYPE_RPC_RESULT, buf, 2 + payload_len, ctx);
}

// Pending WiFi settings response callback — used to send the RPC result
// asynchronously after the connection succeeds or fails.
static improv_response_cb_t s_pending_wifi_cb = NULL;
static void *s_pending_wifi_ctx = NULL;

static void handle_send_wifi_settings(const uint8_t *data, size_t len,
                                      improv_response_cb_t cb, void *ctx)
{
    // Parse: [ssid_len, ssid..., password_len, password...]
    if (len < 1) {
        wifi_cfg_improv_set_error(IMPROV_ERROR_INVALID_RPC);
        return;
    }

    size_t offset = 0;
    uint8_t ssid_len = data[offset++];
    if (offset + ssid_len > len) {
        wifi_cfg_improv_set_error(IMPROV_ERROR_INVALID_RPC);
        return;
    }
    char ssid[33] = {0};
    memcpy(ssid, data + offset, ssid_len > 32 ? 32 : ssid_len);
    offset += ssid_len;

    if (offset >= len) {
        wifi_cfg_improv_set_error(IMPROV_ERROR_INVALID_RPC);
        return;
    }
    uint8_t pass_len = data[offset++];
    if (offset + pass_len > len) {
        wifi_cfg_improv_set_error(IMPROV_ERROR_INVALID_RPC);
        return;
    }
    char password[64] = {0};
    memcpy(password, data + offset, pass_len > 63 ? 63 : pass_len);

    ESP_LOGI(TAG, "RPC: Send WiFi Settings, SSID=%s", ssid);

    // Transition to PROVISIONING
    wifi_cfg_improv_set_error(IMPROV_ERROR_NONE);
    wifi_cfg_improv_set_state(IMPROV_STATE_PROVISIONING);

    // Store the response callback for async delivery on connect/fail
    s_pending_wifi_cb = cb;
    s_pending_wifi_ctx = ctx;

    // Add network + connect via public API (non-blocking)
    wifi_network_t net = {0};
    strncpy(net.ssid, ssid, sizeof(net.ssid) - 1);
    strncpy(net.password, password, sizeof(net.password) - 1);
    net.priority = 10;

    esp_err_t ret = wifi_cfg_add_network(&net);
    if (ret == ESP_ERR_INVALID_STATE) {
        // Already exists — update
        wifi_cfg_update_network(&net);
    }
    wifi_cfg_connect(ssid);

    // Response will be sent from on_wifi_connected / on_wifi_disconnected callbacks
}

static void handle_identify(improv_response_cb_t cb, void *ctx)
{
    ESP_LOGI(TAG, "RPC: Identify");

    if (g_wifi_cfg && g_wifi_cfg->config.improv.on_identify) {
        g_wifi_cfg->config.improv.on_identify();
    }

    send_rpc_result(IMPROV_RPC_IDENTIFY, NULL, 0, cb, ctx);
}

static void handle_get_device_info(improv_response_cb_t cb, void *ctx)
{
    ESP_LOGI(TAG, "RPC: Get Device Info");

    const char *fw_name = "esp_wifi_config";
    const char *fw_version = "1.0.0";
    const char *chip_variant = "ESP32";
    const char *device_name = "";

    if (g_wifi_cfg) {
        if (g_wifi_cfg->config.improv.firmware_name) {
            fw_name = g_wifi_cfg->config.improv.firmware_name;
        }
        if (g_wifi_cfg->config.improv.firmware_version) {
            fw_version = g_wifi_cfg->config.improv.firmware_version;
        }
        if (g_wifi_cfg->config.improv.device_name) {
            device_name = g_wifi_cfg->config.improv.device_name;
        }
    }

    // Detect chip variant
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    switch (chip_info.model) {
        case CHIP_ESP32:   chip_variant = "ESP32";   break;
        case CHIP_ESP32S2: chip_variant = "ESP32-S2"; break;
        case CHIP_ESP32S3: chip_variant = "ESP32-S3"; break;
        case CHIP_ESP32C3: chip_variant = "ESP32-C3"; break;
        case CHIP_ESP32H2: chip_variant = "ESP32-H2"; break;
        case CHIP_ESP32C6: chip_variant = "ESP32-C6"; break;
        default:           chip_variant = "ESP32";    break;
    }

    // Build result: [fw_name, fw_version, chip_variant, device_name]
    uint8_t payload[200];
    size_t poff = 0;
    append_tlv_string(payload, sizeof(payload), &poff, fw_name);
    append_tlv_string(payload, sizeof(payload), &poff, fw_version);
    append_tlv_string(payload, sizeof(payload), &poff, chip_variant);
    append_tlv_string(payload, sizeof(payload), &poff, device_name);

    send_rpc_result(IMPROV_RPC_GET_DEVICE_INFO, payload, poff, cb, ctx);
}

static const char *auth_mode_str(wifi_auth_mode_t auth)
{
    switch (auth) {
        case WIFI_AUTH_OPEN:            return "NO";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2 EAP";
        case WIFI_AUTH_WAPI_PSK:        return "WAPI";
        default:                        return "WPA2";
    }
}

static void handle_get_wifi_networks(improv_response_cb_t cb, void *ctx)
{
    ESP_LOGI(TAG, "RPC: Get WiFi Networks (scan)");

    wifi_scan_result_t *results = malloc(WIFI_CFG_MAX_SCAN_RESULTS * sizeof(wifi_scan_result_t));
    if (!results) {
        wifi_cfg_improv_set_error(IMPROV_ERROR_UNKNOWN);
        return;
    }

    size_t count = 0;
    esp_err_t ret = wifi_cfg_scan(results, WIFI_CFG_MAX_SCAN_RESULTS, &count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        free(results);
        wifi_cfg_improv_set_error(IMPROV_ERROR_UNKNOWN);
        return;
    }
    ESP_LOGI(TAG, "Scan found %zu networks", count);

    // Per the Improv spec, scan results are ONE RPC response containing
    // a multiple of 3 strings: [SSID, RSSI, auth] for each network.
    // Max payload is 255 bytes (data_len is uint8_t).
    uint8_t payload[255];
    size_t poff = 0;

    for (size_t i = 0; i < count; i++) {
        char rssi_str[8];
        snprintf(rssi_str, sizeof(rssi_str), "%d", results[i].rssi);
        const char *auth_str = auth_mode_str(results[i].auth);

        // Check if this network fits in the remaining space
        size_t needed = 1 + strlen(results[i].ssid)
                      + 1 + strlen(rssi_str)
                      + 1 + strlen(auth_str);
        if (poff + needed > sizeof(payload)) break;

        append_tlv_string(payload, sizeof(payload), &poff, results[i].ssid);
        append_tlv_string(payload, sizeof(payload), &poff, rssi_str);
        append_tlv_string(payload, sizeof(payload), &poff, auth_str);
    }

    send_rpc_result(IMPROV_RPC_GET_WIFI_NETWORKS, payload, poff, cb, ctx);

    free(results);
}

// =============================================================================
// Main RPC Dispatcher
// =============================================================================

void wifi_cfg_improv_handle_rpc(const uint8_t *data, size_t len,
                                improv_response_cb_t response_cb, void *cb_ctx)
{
    if (!data || len < 2) {
        wifi_cfg_improv_set_error(IMPROV_ERROR_INVALID_RPC);
        return;
    }

    uint8_t cmd_id = data[0];
    uint8_t data_len = data[1];

    if (2 + data_len > len) {
        wifi_cfg_improv_set_error(IMPROV_ERROR_INVALID_RPC);
        return;
    }

    const uint8_t *cmd_data = data + 2;

    // Clear previous error
    wifi_cfg_improv_set_error(IMPROV_ERROR_NONE);

    switch (cmd_id) {
        case IMPROV_RPC_SEND_WIFI_SETTINGS:
            handle_send_wifi_settings(cmd_data, data_len, response_cb, cb_ctx);
            break;

        case IMPROV_RPC_IDENTIFY:
            handle_identify(response_cb, cb_ctx);
            break;

        case IMPROV_RPC_GET_DEVICE_INFO:
            handle_get_device_info(response_cb, cb_ctx);
            break;

        case IMPROV_RPC_GET_WIFI_NETWORKS:
            handle_get_wifi_networks(response_cb, cb_ctx);
            break;

        default:
            ESP_LOGW(TAG, "Unknown RPC command: 0x%02x", cmd_id);
            wifi_cfg_improv_set_error(IMPROV_ERROR_UNKNOWN_RPC);
            break;
    }
}

// =============================================================================
// esp_bus Event Listener
// =============================================================================

static void on_wifi_connected(const char *event, const void *data, size_t len, void *ctx)
{
    if (s_state == IMPROV_STATE_PROVISIONING) {
        wifi_cfg_improv_set_state(IMPROV_STATE_PROVISIONED);

        // Send deferred RPC result with redirect URL
        if (s_pending_wifi_cb) {
            wifi_status_t status;
            wifi_cfg_get_status(&status);

            char url[64];
            snprintf(url, sizeof(url), "http://%s", status.ip);

            uint8_t payload[128];
            size_t poff = 0;
            append_tlv_string(payload, sizeof(payload), &poff, url);

            send_rpc_result(IMPROV_RPC_SEND_WIFI_SETTINGS, payload, poff,
                            s_pending_wifi_cb, s_pending_wifi_ctx);
            s_pending_wifi_cb = NULL;
            s_pending_wifi_ctx = NULL;
        }
    }
}

static void on_wifi_disconnected(const char *event, const void *data, size_t len, void *ctx)
{
    if (s_state == IMPROV_STATE_PROVISIONING) {
        wifi_cfg_improv_set_error(IMPROV_ERROR_UNABLE_TO_CONNECT);
        wifi_cfg_improv_set_state(IMPROV_STATE_AUTHORIZED);

        // Clear pending callback — the client gets the error via state notifications
        s_pending_wifi_cb = NULL;
        s_pending_wifi_ctx = NULL;
    }
}

// =============================================================================
// Init / Deinit / Start / Stop
// =============================================================================

esp_err_t wifi_cfg_improv_init(void)
{
    ESP_LOGI(TAG, "Initializing Improv WiFi");

    s_state = IMPROV_STATE_AUTHORIZED;
    s_error = IMPROV_ERROR_NONE;
    s_state_cb_count = 0;

    // Subscribe to wifi events for state transitions
    s_sub_connected = esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_wifi_connected, NULL);
    s_sub_disconnected = esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_wifi_disconnected, NULL);

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL
    {
        esp_err_t ret = wifi_cfg_improv_serial_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Improv Serial init failed: %s", esp_err_to_name(ret));
        }
    }
#endif

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
    {
        esp_err_t ret = wifi_cfg_improv_ble_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Improv BLE init failed: %s", esp_err_to_name(ret));
        }
    }
#endif

    return ESP_OK;
}

esp_err_t wifi_cfg_improv_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing Improv WiFi");

    if (s_sub_connected >= 0) {
        esp_bus_unsub(s_sub_connected);
        s_sub_connected = -1;
    }
    if (s_sub_disconnected >= 0) {
        esp_bus_unsub(s_sub_disconnected);
        s_sub_disconnected = -1;
    }

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL
    wifi_cfg_improv_serial_deinit();
#endif

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
    wifi_cfg_improv_ble_deinit();
#endif

    s_state_cb_count = 0;
    return ESP_OK;
}

esp_err_t wifi_cfg_improv_start(void)
{
    ESP_LOGI(TAG, "Starting Improv provisioning");

    // Reset to authorized state when provisioning starts
    s_state = IMPROV_STATE_AUTHORIZED;
    s_error = IMPROV_ERROR_NONE;

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL
    wifi_cfg_improv_serial_start();
    g_wifi_cfg->improv_serial_active = true;
#endif

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
    wifi_cfg_improv_ble_start();
    g_wifi_cfg->improv_ble_active = true;
#endif

    return ESP_OK;
}

esp_err_t wifi_cfg_improv_stop(void)
{
    ESP_LOGI(TAG, "Stopping Improv provisioning");

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL
    if (g_wifi_cfg->improv_serial_active) {
        wifi_cfg_improv_serial_stop();
        g_wifi_cfg->improv_serial_active = false;
    }
#endif

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
    if (g_wifi_cfg->improv_ble_active) {
        wifi_cfg_improv_ble_stop();
        g_wifi_cfg->improv_ble_active = false;
    }
#endif

    return ESP_OK;
}

#else // no Improv transport enabled

// Stub implementations when Improv is disabled
#include <esp_err.h>
#include "esp_wifi_config_improv.h"

esp_err_t wifi_cfg_improv_init(void)  { return ESP_OK; }
esp_err_t wifi_cfg_improv_deinit(void) { return ESP_OK; }
esp_err_t wifi_cfg_improv_start(void)  { return ESP_OK; }
esp_err_t wifi_cfg_improv_stop(void)   { return ESP_OK; }
improv_state_t wifi_cfg_improv_get_state(void) { return IMPROV_STATE_AUTHORIZED; }
improv_error_t wifi_cfg_improv_get_error(void) { return IMPROV_ERROR_NONE; }
uint8_t wifi_cfg_improv_get_capabilities(void) { return 0; }
void wifi_cfg_improv_set_state(improv_state_t state) { (void)state; }
void wifi_cfg_improv_set_error(improv_error_t error) { (void)error; }
void wifi_cfg_improv_register_state_cb(improv_state_change_cb_t cb, void *ctx) { (void)cb; (void)ctx; }
void wifi_cfg_improv_handle_rpc(const uint8_t *data, size_t len,
                                improv_response_cb_t response_cb, void *cb_ctx) {
    (void)data; (void)len; (void)response_cb; (void)cb_ctx;
}

#endif // CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE || CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL
