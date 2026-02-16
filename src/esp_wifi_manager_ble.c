/**
 * @file esp_wifi_manager_ble.c
 * @brief BLE GATT interface for WiFi Manager
 *
 * Provides BLE-based configuration interface with JSON commands.
 */

#include "esp_wifi_manager_priv.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

#ifdef CONFIG_WIFI_MGR_ENABLE_BLE

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

static const char *TAG = "wifi_mgr_ble";

// =============================================================================
// UUIDs
// =============================================================================

#define WIFI_SVC_UUID           0xFFE0
#define CHAR_STATUS_UUID        0xFFE1
#define CHAR_COMMAND_UUID       0xFFE2
#define CHAR_RESPONSE_UUID      0xFFE3

// GATT handles
#define WIFI_SVC_INST_ID        0

// Profile
#define PROFILE_NUM             1
#define PROFILE_APP_IDX         0

// Attribute indices
enum {
    IDX_SVC,
    IDX_CHAR_STATUS,
    IDX_CHAR_STATUS_VAL,
    IDX_CHAR_STATUS_CCC,
    IDX_CHAR_COMMAND,
    IDX_CHAR_COMMAND_VAL,
    IDX_CHAR_RESPONSE,
    IDX_CHAR_RESPONSE_VAL,
    IDX_CHAR_RESPONSE_CCC,
    WIFI_IDX_NB,
};

// =============================================================================
// GATT Profile
// =============================================================================

static uint16_t wifi_handle_table[WIFI_IDX_NB];

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE;

static const uint16_t wifi_svc_uuid = WIFI_SVC_UUID;
static const uint16_t char_status_uuid = CHAR_STATUS_UUID;
static const uint16_t char_command_uuid = CHAR_COMMAND_UUID;
static const uint16_t char_response_uuid = CHAR_RESPONSE_UUID;

static uint8_t status_ccc[2] = {0x00, 0x00};
static uint8_t response_ccc[2] = {0x00, 0x00};

// Service database
static const esp_gatts_attr_db_t wifi_gatt_db[WIFI_IDX_NB] = {
    // Service Declaration
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
         sizeof(uint16_t), sizeof(wifi_svc_uuid), (uint8_t *)&wifi_svc_uuid}
    },

    // Status Characteristic Declaration
    [IDX_CHAR_STATUS] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         1, 1, (uint8_t *)&char_prop_read_notify}
    },
    // Status Characteristic Value
    [IDX_CHAR_STATUS_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&char_status_uuid, ESP_GATT_PERM_READ,
         512, 0, NULL}
    },
    // Status CCCD
    [IDX_CHAR_STATUS_CCC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(status_ccc), sizeof(status_ccc), status_ccc}
    },

    // Command Characteristic Declaration
    [IDX_CHAR_COMMAND] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         1, 1, (uint8_t *)&char_prop_write}
    },
    // Command Characteristic Value
    [IDX_CHAR_COMMAND_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_16, (uint8_t *)&char_command_uuid, ESP_GATT_PERM_WRITE,
         512, 0, NULL}
    },

    // Response Characteristic Declaration
    [IDX_CHAR_RESPONSE] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         1, 1, (uint8_t *)&char_prop_read_notify}
    },
    // Response Characteristic Value
    [IDX_CHAR_RESPONSE_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&char_response_uuid, ESP_GATT_PERM_READ,
         512, 0, NULL}
    },
    // Response CCCD
    [IDX_CHAR_RESPONSE_CCC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(response_ccc), sizeof(response_ccc), response_ccc}
    },
};

// =============================================================================
// Profile State
// =============================================================================

typedef struct {
    esp_gatt_if_t gatts_if;
    uint16_t conn_id;
    bool connected;
    bool response_notify_enabled;
    bool status_notify_enabled;
    char device_name[32];
} ble_profile_t;

static ble_profile_t ble_profile = {
    .gatts_if = ESP_GATT_IF_NONE,
    .connected = false,
};

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
        cJSON_AddBoolToObject(data, "ap_active", status.ap_active);
    }

    return data;
}

static cJSON *handle_scan(void)
{
    wifi_scan_result_t results[WIFI_MGR_MAX_SCAN_RESULTS];
    size_t count = 0;

    esp_err_t ret = wifi_manager_scan(results, WIFI_MGR_MAX_SCAN_RESULTS, &count);
    if (ret != ESP_OK) {
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

static cJSON *handle_factory_reset(void)
{
    wifi_manager_factory_reset();
    return cJSON_CreateObject();
}

// =============================================================================
// Command Router
// =============================================================================

static void send_response(const char *json_str)
{
    if (!ble_profile.connected || !ble_profile.response_notify_enabled) {
        return;
    }

    size_t len = strlen(json_str);
    if (len > 500) {
        len = 500;  // Truncate for BLE MTU
    }

    esp_ble_gatts_send_indicate(ble_profile.gatts_if, ble_profile.conn_id,
                                 wifi_handle_table[IDX_CHAR_RESPONSE_VAL],
                                 len, (uint8_t *)json_str, false);
}

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
// GAP Event Handler
// =============================================================================

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Advertising start failed");
            } else {
                ESP_LOGI(TAG, "BLE advertising started: %s", ble_profile.device_name);
            }
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Advertising stop failed");
            }
            break;

        default:
            break;
    }
}

// =============================================================================
// GATTS Event Handler
// =============================================================================

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                 esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.status == ESP_GATT_OK) {
                ble_profile.gatts_if = gatts_if;

                // Set device name
                esp_ble_gap_set_device_name(ble_profile.device_name);

                // Config advertising data
                esp_ble_gap_config_adv_data(&adv_data);

                // Create attribute table
                esp_ble_gatts_create_attr_tab(wifi_gatt_db, gatts_if, WIFI_IDX_NB, WIFI_SVC_INST_ID);
            } else {
                ESP_LOGE(TAG, "GATT register failed, status %d", param->reg.status);
            }
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "Create attr table failed, status %d", param->add_attr_tab.status);
            } else if (param->add_attr_tab.num_handle != WIFI_IDX_NB) {
                ESP_LOGE(TAG, "Create attr table abnormally, num_handle (%d) != WIFI_IDX_NB (%d)",
                         param->add_attr_tab.num_handle, WIFI_IDX_NB);
            } else {
                memcpy(wifi_handle_table, param->add_attr_tab.handles, sizeof(wifi_handle_table));
                esp_ble_gatts_start_service(wifi_handle_table[IDX_SVC]);
                ESP_LOGI(TAG, "GATT service started");
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            ble_profile.conn_id = param->connect.conn_id;
            ble_profile.connected = true;
            ESP_LOGI(TAG, "BLE client connected, conn_id %d", param->connect.conn_id);

            // Update connection params for better throughput
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            conn_params.latency = 0;
            conn_params.max_int = 0x20;
            conn_params.min_int = 0x10;
            conn_params.timeout = 400;
            esp_ble_gap_update_conn_params(&conn_params);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ble_profile.connected = false;
            ble_profile.response_notify_enabled = false;
            ble_profile.status_notify_enabled = false;
            ESP_LOGI(TAG, "BLE client disconnected");

            // Restart advertising
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            if (!param->write.is_prep) {
                // Handle CCCD writes
                if (param->write.handle == wifi_handle_table[IDX_CHAR_RESPONSE_CCC]) {
                    if (param->write.len == 2) {
                        uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                        ble_profile.response_notify_enabled = (descr_value == 0x0001);
                        ESP_LOGI(TAG, "Response notify %s",
                                 ble_profile.response_notify_enabled ? "enabled" : "disabled");
                    }
                } else if (param->write.handle == wifi_handle_table[IDX_CHAR_STATUS_CCC]) {
                    if (param->write.len == 2) {
                        uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                        ble_profile.status_notify_enabled = (descr_value == 0x0001);
                        ESP_LOGI(TAG, "Status notify %s",
                                 ble_profile.status_notify_enabled ? "enabled" : "disabled");
                    }
                }
                // Handle command write
                else if (param->write.handle == wifi_handle_table[IDX_CHAR_COMMAND_VAL]) {
                    // Null-terminate and handle
                    char cmd_buf[512];
                    size_t len = param->write.len;
                    if (len > sizeof(cmd_buf) - 1) {
                        len = sizeof(cmd_buf) - 1;
                    }
                    memcpy(cmd_buf, param->write.value, len);
                    cmd_buf[len] = '\0';

                    handle_command(cmd_buf);
                }

                // Send response if needed
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                 param->write.trans_id, ESP_GATT_OK, NULL);
                }
            }
            break;

        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(TAG, "MTU changed to %d", param->mtu.mtu);
            break;

        default:
            break;
    }
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
    wifi_mgr_expand_template(name_template, ble_profile.device_name, sizeof(ble_profile.device_name));

    // Release classic BT memory
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register callbacks
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register application profile
    ret = esp_ble_gatts_app_register(PROFILE_APP_IDX);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS app register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set MTU
    esp_ble_gatt_set_local_mtu(517);

    ESP_LOGI(TAG, "BLE interface initialized");
    return ESP_OK;
}

esp_err_t wifi_mgr_ble_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE interface");

    esp_ble_gatts_app_unregister(ble_profile.gatts_if);
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    ble_profile.gatts_if = ESP_GATT_IF_NONE;
    ble_profile.connected = false;

    return ESP_OK;
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
