/**
 * @file esp_wifi_manager_ble.c
 * @brief BLE shared layer — JSON command routing and protocol handling
 *
 * Stack-specific transport (Bluedroid or NimBLE) is in the backend files.
 */

#include "esp_wifi_manager_priv.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_WIFI_MGR_ENABLE_BLE

#include "esp_wifi_manager_ble_int.h"

static const char *TAG = "wifi_mgr_ble";

// =============================================================================
// Connection State (shared)
// =============================================================================

static bool s_connected = false;
static bool s_response_notify_enabled = false;

// =============================================================================
// Command Handlers
// =============================================================================

static cJSON *handle_get_status(void)
{
    cJSON *data = cJSON_CreateObject();
    wifi_status_t status;

    if (wifi_manager_get_status(&status) == ESP_OK) {
        cJSON_AddStringToObject(data, "state",
            status.state == WIFI_STATE_CONNECTED ? "connected" :
            status.state == WIFI_STATE_CONNECTING ? "connecting" : "disconnected");
        cJSON_AddStringToObject(data, "ssid", status.ssid);
        cJSON_AddNumberToObject(data, "rssi", status.rssi);
        cJSON_AddNumberToObject(data, "quality", status.quality);
        cJSON_AddStringToObject(data, "ip", status.ip);
        cJSON_AddNumberToObject(data, "channel", status.channel);
        cJSON_AddStringToObject(data, "netmask", status.netmask);
        cJSON_AddStringToObject(data, "gateway", status.gateway);
        cJSON_AddStringToObject(data, "dns", status.dns);
        cJSON_AddStringToObject(data, "mac", status.mac);
        cJSON_AddStringToObject(data, "hostname", status.hostname);
        cJSON_AddNumberToObject(data, "uptime_ms", status.uptime_ms);
        cJSON_AddBoolToObject(data, "ap_active", status.ap_active);
    }

    return data;
}

static cJSON *handle_scan(void)
{
    // If we merge the default configuration defines patch, the max count should be WIFI_MGR_MAX_SCAN_RESULTS rather than 20
    // To make it explicit, I'm defining a local constant here for the max scan results for now
    const size_t max_scan_results = 20;
    // malloc an array to hold the scan results (offloads from BLE host's stack)
    wifi_scan_result_t *results = malloc(max_scan_results * sizeof(wifi_scan_result_t));
    if (!results) {
        return NULL;
    }

    size_t count = 0;
    esp_err_t ret = wifi_manager_scan(results, max_scan_results, &count);
    if (ret != ESP_OK) {
        free(results);
        return NULL;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(data, "networks");

    for (size_t i = 0; i < count; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", results[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", results[i].rssi);

        const char *auth_str = "UNKNOWN";
        switch (results[i].auth) {
            case WIFI_AUTH_OPEN: auth_str = "OPEN"; break;
            case WIFI_AUTH_WEP: auth_str = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: auth_str = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: auth_str = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: auth_str = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK: auth_str = "WPA3"; break;
            default: break;
        }
        cJSON_AddStringToObject(net, "auth", auth_str);
        cJSON_AddItemToArray(arr, net);
    }

    free(results);
    return data;
}

static cJSON *handle_list_networks(void)
{
    wifi_network_t networks[WIFI_MGR_MAX_NETWORKS];
    size_t count = 0;

    wifi_manager_list_networks(networks, WIFI_MGR_MAX_NETWORKS, &count);

    cJSON *data = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(data, "networks");

    for (size_t i = 0; i < count; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", networks[i].ssid);
        cJSON_AddNumberToObject(net, "priority", networks[i].priority);
        cJSON_AddItemToArray(arr, net);
    }

    return data;
}

static cJSON *handle_add_network(cJSON *params)
{
    cJSON *ssid = cJSON_GetObjectItem(params, "ssid");
    cJSON *password = cJSON_GetObjectItem(params, "password");
    cJSON *priority = cJSON_GetObjectItem(params, "priority");

    if (!cJSON_IsString(ssid)) {
        return NULL;
    }

    wifi_network_t network = {0};
    strncpy(network.ssid, ssid->valuestring, sizeof(network.ssid) - 1);
    if (cJSON_IsString(password)) {
        strncpy(network.password, password->valuestring, sizeof(network.password) - 1);
    }
    if (cJSON_IsNumber(priority)) {
        network.priority = (uint8_t)priority->valueint;
    } else {
        network.priority = 10;
    }

    esp_err_t ret = wifi_manager_add_network(&network);
    if (ret != ESP_OK) {
        return NULL;
    }

    return cJSON_CreateObject();
}

static cJSON *handle_del_network(cJSON *params)
{
    cJSON *ssid = cJSON_GetObjectItem(params, "ssid");
    if (!cJSON_IsString(ssid)) {
        return NULL;
    }

    esp_err_t ret = wifi_manager_remove_network(ssid->valuestring);
    if (ret != ESP_OK) {
        return NULL;
    }

    return cJSON_CreateObject();
}

static cJSON *handle_connect(cJSON *params)
{
    const char *ssid = NULL;
    cJSON *ssid_item = cJSON_GetObjectItem(params, "ssid");
    if (cJSON_IsString(ssid_item)) {
        ssid = ssid_item->valuestring;
    }

    wifi_manager_connect(ssid);
    return cJSON_CreateObject();
}

static cJSON *handle_disconnect(void)
{
    wifi_manager_disconnect();
    return cJSON_CreateObject();
}

static cJSON *handle_ap_status(void)
{
    wifi_ap_status_t status;
    wifi_manager_get_ap_status(&status);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "active", status.active);
    cJSON_AddStringToObject(data, "ssid", status.ssid);
    cJSON_AddStringToObject(data, "ip", status.ip);
    cJSON_AddNumberToObject(data, "channel", status.channel);
    cJSON_AddNumberToObject(data, "sta_count", status.sta_count);

    return data;
}

static cJSON *handle_start_ap(cJSON *params)
{
    wifi_mgr_ap_config_t *config = NULL;
    wifi_mgr_ap_config_t temp_config;

    if (params) {
        wifi_manager_get_ap_config(&temp_config);

        cJSON *ssid = cJSON_GetObjectItem(params, "ssid");
        cJSON *password = cJSON_GetObjectItem(params, "password");

        if (cJSON_IsString(ssid)) {
            strncpy(temp_config.ssid, ssid->valuestring, sizeof(temp_config.ssid) - 1);
        }
        if (cJSON_IsString(password)) {
            strncpy(temp_config.password, password->valuestring, sizeof(temp_config.password) - 1);
        }
        config = &temp_config;
    }

    wifi_manager_start_ap(config);
    return cJSON_CreateObject();
}

static cJSON *handle_stop_ap(void)
{
    wifi_manager_stop_ap();
    return cJSON_CreateObject();
}

static cJSON *handle_get_var(cJSON *params)
{
    cJSON *key = cJSON_GetObjectItem(params, "key");
    if (!cJSON_IsString(key)) {
        return NULL;
    }

    char value[128];
    esp_err_t ret = wifi_manager_get_var(key->valuestring, value, sizeof(value));
    if (ret != ESP_OK) {
        return NULL;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "key", key->valuestring);
    cJSON_AddStringToObject(data, "value", value);
    return data;
}

static cJSON *handle_set_var(cJSON *params)
{
    cJSON *key = cJSON_GetObjectItem(params, "key");
    cJSON *value = cJSON_GetObjectItem(params, "value");
    if (!cJSON_IsString(key) || !cJSON_IsString(value)) {
        return NULL;
    }

    esp_err_t ret = wifi_manager_set_var(key->valuestring, value->valuestring);
    if (ret != ESP_OK) {
        return NULL;
    }

    return cJSON_CreateObject();
}

static cJSON *handle_update_network(cJSON *params)
{
    cJSON *ssid = cJSON_GetObjectItem(params, "ssid");
    if (!cJSON_IsString(ssid)) {
        return NULL;
    }

    wifi_network_t network = {0};
    strncpy(network.ssid, ssid->valuestring, sizeof(network.ssid) - 1);

    cJSON *password = cJSON_GetObjectItem(params, "password");
    cJSON *priority = cJSON_GetObjectItem(params, "priority");
    if (cJSON_IsString(password)) {
        strncpy(network.password, password->valuestring, sizeof(network.password) - 1);
    }
    if (cJSON_IsNumber(priority)) {
        network.priority = (uint8_t)priority->valueint;
    }

    esp_err_t ret = wifi_manager_update_network(&network);
    if (ret != ESP_OK) {
        return NULL;
    }

    return cJSON_CreateObject();
}

static cJSON *handle_list_vars(void)
{
    wifi_mgr_lock();

    cJSON *data = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(data, "vars");

    for (size_t i = 0; i < g_wifi_mgr->var_count; i++) {
        cJSON *var = cJSON_CreateObject();
        cJSON_AddStringToObject(var, "key", g_wifi_mgr->vars[i].key);
        cJSON_AddStringToObject(var, "value", g_wifi_mgr->vars[i].value);
        cJSON_AddItemToArray(arr, var);
    }

    wifi_mgr_unlock();
    return data;
}

static cJSON *handle_del_var(cJSON *params)
{
    cJSON *key = cJSON_GetObjectItem(params, "key");
    if (!cJSON_IsString(key)) {
        return NULL;
    }

    esp_err_t ret = wifi_manager_del_var(key->valuestring);
    if (ret != ESP_OK) {
        return NULL;
    }

    return cJSON_CreateObject();
}

static cJSON *handle_factory_reset(void)
{
    wifi_manager_factory_reset();
    return cJSON_CreateObject();
}

// =============================================================================
// Response Sending (via backend)
// =============================================================================

/** Minimum payload size if MTU query fails or returns something unusable. */
#define BLE_MIN_CHUNK_SIZE  20

/** Delay between chunks to avoid flooding the BLE controller's TX queue. */
#define BLE_CHUNK_DELAY_MS  20

static void send_response(const char *json_str)
{
    if (!s_connected || !s_response_notify_enabled) {
        return;
    }

    uint16_t mtu = wifi_mgr_ble_backend_get_mtu();
    size_t chunk_size = (mtu >= 3 + BLE_MIN_CHUNK_SIZE) ? (mtu - 3) : BLE_MIN_CHUNK_SIZE;

    const uint8_t *ptr = (const uint8_t *)json_str;
    size_t remaining = strlen(json_str);

    while (remaining > 0) {
        size_t send_len = (remaining > chunk_size) ? chunk_size : remaining;

        esp_err_t err = wifi_mgr_ble_backend_notify_response(ptr, send_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Notify failed during chunked send, %d bytes remaining",
                     (int)remaining);
            return;
        }

        ptr += send_len;
        remaining -= send_len;

        if (remaining > 0) {
            vTaskDelay(pdMS_TO_TICKS(BLE_CHUNK_DELAY_MS));
        }
    }
}

// =============================================================================
// Command Router
// =============================================================================

static void handle_command(const char *json_str)
{
    ESP_LOGD(TAG, "Command: %s", json_str);

    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        send_response("{\"status\":\"error\",\"error\":\"Invalid JSON\"}");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(json, "cmd");
    cJSON *params = cJSON_GetObjectItem(json, "params");

    if (!cJSON_IsString(cmd)) {
        cJSON_Delete(json);
        send_response("{\"status\":\"error\",\"error\":\"Missing cmd\"}");
        return;
    }

    const char *cmd_str = cmd->valuestring;
    cJSON *result = NULL;

    // Route command
    if (strcmp(cmd_str, "get_status") == 0) {
        result = handle_get_status();
    } else if (strcmp(cmd_str, "scan") == 0) {
        result = handle_scan();
    } else if (strcmp(cmd_str, "list_networks") == 0) {
        result = handle_list_networks();
    } else if (strcmp(cmd_str, "add_network") == 0) {
        result = handle_add_network(params);
    } else if (strcmp(cmd_str, "del_network") == 0) {
        result = handle_del_network(params);
    } else if (strcmp(cmd_str, "connect") == 0) {
        result = handle_connect(params);
    } else if (strcmp(cmd_str, "disconnect") == 0) {
        result = handle_disconnect();
    } else if (strcmp(cmd_str, "get_ap_status") == 0) {
        result = handle_ap_status();
    } else if (strcmp(cmd_str, "start_ap") == 0) {
        result = handle_start_ap(params);
    } else if (strcmp(cmd_str, "stop_ap") == 0) {
        result = handle_stop_ap();
    } else if (strcmp(cmd_str, "get_var") == 0) {
        result = handle_get_var(params);
    } else if (strcmp(cmd_str, "set_var") == 0) {
        result = handle_set_var(params);
    } else if (strcmp(cmd_str, "update_network") == 0) {
        result = handle_update_network(params);
    } else if (strcmp(cmd_str, "list_vars") == 0) {
        result = handle_list_vars();
    } else if (strcmp(cmd_str, "del_var") == 0) {
        result = handle_del_var(params);
    } else if (strcmp(cmd_str, "factory_reset") == 0) {
        result = handle_factory_reset();
    } else {
        cJSON_Delete(json);
        send_response("{\"status\":\"error\",\"error\":\"Unknown command\"}");
        return;
    }

    cJSON_Delete(json);

    // Build response
    cJSON *response = cJSON_CreateObject();
    if (result) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddItemToObject(response, "data", result);
    } else {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", "Command failed");
    }

    char *response_str = cJSON_PrintUnformatted(response);
    if (response_str) {
        send_response(response_str);
        free(response_str);
    }
    cJSON_Delete(response);
}

// =============================================================================
// Callbacks from stack backend
// =============================================================================

void wifi_mgr_ble_on_command(const uint8_t *data, size_t length)
{
    char cmd_buf[512];
    size_t len = length;
    if (len > sizeof(cmd_buf) - 1) {
        len = sizeof(cmd_buf) - 1;
    }
    memcpy(cmd_buf, data, len);
    cmd_buf[len] = '\0';

    handle_command(cmd_buf);
}

void wifi_mgr_ble_on_connect(void)
{
    s_connected = true;
    s_response_notify_enabled = false;
}

void wifi_mgr_ble_on_disconnect(void)
{
    s_connected = false;
    s_response_notify_enabled = false;
}

void wifi_mgr_ble_set_response_notify(bool enabled)
{
    s_response_notify_enabled = enabled;
}

// =============================================================================
// Public API
// =============================================================================

esp_err_t wifi_mgr_ble_init(void)
{
    if (!g_wifi_mgr) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing BLE interface");

    // Expand device name template
    const char *name_template = g_wifi_mgr->config.ble.device_name;
    if (!name_template || !name_template[0]) {
        name_template = CONFIG_WIFI_MGR_BLE_DEVICE_NAME;
    }

    char device_name[32];
    wifi_mgr_expand_template(name_template, device_name, sizeof(device_name));

    esp_err_t ret = wifi_mgr_ble_backend_init(device_name);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "BLE interface initialized");
    return ESP_OK;
}

esp_err_t wifi_mgr_ble_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE interface");
    s_connected = false;
    s_response_notify_enabled = false;
    return wifi_mgr_ble_backend_deinit();
}

#else // CONFIG_WIFI_MGR_ENABLE_BLE

esp_err_t wifi_mgr_ble_init(void)
{
    return ESP_OK;
}

esp_err_t wifi_mgr_ble_deinit(void)
{
    return ESP_OK;
}

#endif // CONFIG_WIFI_MGR_ENABLE_BLE
