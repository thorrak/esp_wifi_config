/**
 * @file esp_wifi_config_ble_bluedroid.c
 * @brief Bluedroid host bootstrap for the Improv-WiFi BLE transport
 *
 * Brings up the BT controller and Bluedroid stack, registers the GATTS
 * dispatch callback used by the Improv profile, and drives Improv-compliant
 * advertising. The custom JSON-over-GATT 0xFFE0 service that previously
 * lived here has been removed in favour of ESP-IDF Network Provisioning
 * (see esp_wifi_config_prov_ble.c).
 *
 * This file is only compiled when CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE and
 * CONFIG_BT_BLUEDROID_ENABLED are both set.
 */

#include "sdkconfig.h"

#if defined(CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE) && defined(CONFIG_BT_BLUEDROID_ENABLED)

#include "esp_wifi_config_ble_int.h"
#include "esp_wifi_config_improv.h"
#include "esp_log.h"
#include <string.h>

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

static const char *TAG = "wifi_cfg_ble_bd";

static char s_device_name[32];
static bool s_ble_stack_owned = false;
static bool s_advertising_desired = false;
static bool s_connected = false;
static esp_bd_addr_t s_remote_bda = {0};

// =============================================================================
// Advertising — Improv layout
// =============================================================================
//
// Primary adv: Improv 128-bit UUID + service data (state + capabilities).
// Scan response: device name + nothing else (kept short for compatibility).

static uint8_t improv_adv_svc_uuid128[] = IMPROV_BLE_SVC_UUID_128;

static uint8_t improv_svc_data[] = {
    0x77, 0x46,                     // UUID16 0x4677 little-endian
    IMPROV_STATE_AUTHORIZED,        // Current state (refreshed at runtime)
    0x00,                           // Capabilities (refreshed at runtime)
    0x00, 0x00, 0x00, 0x00,        // Reserved
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = false,
    .include_txpower = false,
    .service_uuid_len = sizeof(improv_adv_svc_uuid128),
    .p_service_uuid = improv_adv_svc_uuid128,
    .service_data_len = sizeof(improv_svc_data),
    .p_service_data = improv_svc_data,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = false,
    .flag = 0,
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// =============================================================================
// GAP Event Handler
// =============================================================================

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_config_adv_data(&scan_rsp_data);
            break;

        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Advertising start failed");
            } else {
                ESP_LOGI(TAG, "BLE advertising started: %s", s_device_name);
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
// GATTS Dispatcher (forwards to Improv profile)
// =============================================================================

extern void improv_bd_gatts_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                     esp_ble_gatts_cb_param_t *param);

static void gatts_dispatch_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                    esp_ble_gatts_cb_param_t *param)
{
    // Track connection state for MTU / disconnect dispatch
    if (event == ESP_GATTS_CONNECT_EVT) {
        s_connected = true;
        memcpy(s_remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
    } else if (event == ESP_GATTS_DISCONNECT_EVT) {
        s_connected = false;
        if (s_advertising_desired) {
            esp_ble_gap_start_advertising(&adv_params);
        }
    }

    // Improv profile handles its own app registration / characteristic
    // table via esp_wifi_config_improv_ble.c. It filters by app_id so only
    // its own events are processed.
    improv_bd_gatts_handler(event, gatts_if, param);
}

// =============================================================================
// Backend Interface Implementation
// =============================================================================

uint16_t wifi_cfg_ble_backend_get_mtu(void)
{
    // Bluedroid Improv path uses the per-conn MTU tracked inside the Improv
    // profile. Returning 23 here is a safe lower bound; chunked Improv
    // responses query the actual MTU from their own state.
    return s_connected ? 23 : 0;
}

bool wifi_cfg_ble_backend_is_stack_running(void)
{
    return esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED;
}

esp_err_t wifi_cfg_ble_backend_init(const char *device_name)
{
    strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
    s_device_name[sizeof(s_device_name) - 1] = '\0';

    if (wifi_cfg_ble_backend_is_stack_running()) {
        s_ble_stack_owned = false;
        ESP_LOGI(TAG, "Bluedroid stack already running, registering service only");
    } else {
        s_ble_stack_owned = true;

        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

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
    }

    esp_err_t ret = esp_ble_gatts_register_callback(gatts_dispatch_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_ble_gap_set_device_name(s_device_name);
    esp_ble_gatt_set_local_mtu(517);

    // Push initial adv data — Improv adv start fires after scan-rsp set.
    improv_svc_data[2] = wifi_cfg_improv_get_state();
    improv_svc_data[3] = wifi_cfg_improv_get_capabilities();
    esp_ble_gap_config_adv_data(&adv_data);

    return ESP_OK;
}

esp_err_t wifi_cfg_ble_backend_start(void)
{
    s_advertising_desired = true;

    // Refresh adv state byte before re-arming
    improv_svc_data[2] = wifi_cfg_improv_get_state();
    improv_svc_data[3] = wifi_cfg_improv_get_capabilities();
    esp_ble_gap_config_adv_data(&adv_data);
    return ESP_OK;
}

esp_err_t wifi_cfg_ble_backend_stop(void)
{
    s_advertising_desired = false;

    if (s_connected) {
        esp_ble_gap_disconnect(s_remote_bda);
    }

    esp_ble_gap_stop_advertising();
    return ESP_OK;
}

esp_err_t wifi_cfg_ble_backend_deinit(void)
{
    s_advertising_desired = false;

    if (s_connected) {
        esp_ble_gap_disconnect(s_remote_bda);
    }

    esp_ble_gap_stop_advertising();

    if (s_ble_stack_owned) {
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
    }

    s_connected = false;
    s_ble_stack_owned = false;

    return ESP_OK;
}

#endif // CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE && CONFIG_BT_BLUEDROID_ENABLED
