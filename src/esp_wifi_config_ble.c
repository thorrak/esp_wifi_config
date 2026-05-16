/**
 * @file esp_wifi_config_ble.c
 * @brief Improv-WiFi BLE host lifecycle shim
 *
 * Translates the library's `wifi_cfg_ble_*` lifecycle hooks into calls on
 * the stack-specific backend (NimBLE or Bluedroid). The custom JSON-over-GATT
 * 0xFFE0 service that previously lived here has been removed; ESP-IDF
 * Network Provisioning is now the recommended secure BLE provisioning path
 * (see esp_wifi_config_prov_ble.c).
 *
 * When CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE is OFF, this file compiles as
 * stubs so the lifecycle code in esp_wifi_config.c can call it
 * unconditionally.
 */

#include "esp_wifi_config_priv.h"
#include "esp_log.h"
#include <string.h>

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE

#include "esp_wifi_config_ble_int.h"

static const char *TAG = "wifi_cfg_ble";

esp_err_t wifi_cfg_ble_init(void)
{
    if (!g_wifi_cfg) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *name_template = g_wifi_cfg->config.improv.ble_device_name;
    if (!name_template || !name_template[0]) {
        name_template = WIFI_CFG_DEFAULT_BLE_DEVICE_NAME;
    }

    char device_name[32];
    wifi_cfg_expand_template(name_template, device_name, sizeof(device_name));

    ESP_LOGI(TAG, "Initializing BLE host for Improv (%s)", device_name);
    return wifi_cfg_ble_backend_init(device_name);
}

esp_err_t wifi_cfg_ble_start(void)
{
    return wifi_cfg_ble_backend_start();
}

esp_err_t wifi_cfg_ble_stop(void)
{
    return wifi_cfg_ble_backend_stop();
}

esp_err_t wifi_cfg_ble_deinit(void)
{
    return wifi_cfg_ble_backend_deinit();
}

#else // !CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE

esp_err_t wifi_cfg_ble_init(void)   { return ESP_OK; }
esp_err_t wifi_cfg_ble_start(void)  { return ESP_OK; }
esp_err_t wifi_cfg_ble_stop(void)   { return ESP_OK; }
esp_err_t wifi_cfg_ble_deinit(void) { return ESP_OK; }

#endif // CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
