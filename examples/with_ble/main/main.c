/**
 * @file main.c
 * @brief ESP WiFi Config - BLE Example
 *
 * This example demonstrates WiFi Config with BLE GATT interface:
 * - Configure WiFi networks via BLE from smartphone or Python CLI
 * - Enable HTTP REST API for web-based configuration
 * - Enable captive portal for initial setup
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi_config.h"
#include "esp_bus.h"

static const char *TAG = "wifi_ble_example";

static void on_wifi_connected(const char *event, const void *data, size_t len, void *ctx)
{
    const wifi_connected_t *info = (const wifi_connected_t *)data;
    ESP_LOGI(TAG, "WiFi connected to %s (RSSI: %d dBm)", info->ssid, info->rssi);
}

static void on_wifi_disconnected(const char *event, const void *data, size_t len, void *ctx)
{
    const wifi_disconnected_t *info = (const wifi_disconnected_t *)data;
    ESP_LOGW(TAG, "WiFi disconnected from %s (reason: %d)", info->ssid, info->reason);
}

static void on_wifi_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_status_t status;
    if (wifi_cfg_get_status(&status) == ESP_OK) {
        ESP_LOGI(TAG, "Got IP: %s", status.ip);
    }
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting WiFi Config with BLE example");

    // Initialize esp_bus
    ret = esp_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize esp_bus: %s", esp_err_to_name(ret));
        return;
    }

    // Subscribe to WiFi events
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_wifi_connected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_wifi_disconnected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_wifi_got_ip, NULL);

    // Initialize WiFi Config with BLE
    wifi_cfg_config_t config = {
        // Retry configuration
        .max_retry_per_network = 3,
        .retry_interval_ms = 5000,
        .auto_reconnect = true,

        // SoftAP configuration (for captive portal)
        .default_ap = {
            .ssid = "ESP_{id}",
            .password = "",
            .channel = 0,
            .max_connections = 4,
            .ip = "192.168.4.1",
            .netmask = "255.255.255.0",
            .gateway = "192.168.4.1",
            .dhcp_start = "192.168.4.2",
            .dhcp_end = "192.168.4.20",
        },
        // Provisioning: start AP+BLE+HTTP when no networks or all fail
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,

        // HTTP REST API configuration
        .http = {
            .httpd = NULL,
            .api_base_path = "/api/wifi",
            .enable_auth = false,
        },

        // BLE GATT configuration (enabled via CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=y in sdkconfig)
        .ble = {
            .device_name = NULL,  // Use Kconfig default: "ESP32-WiFi-{id}"
        },
    };

    ret = wifi_cfg_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi Config: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "WiFi Config initialized with BLE");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Configuration options:");
    ESP_LOGI(TAG, "  1. BLE: Use Python CLI or smartphone app");
    ESP_LOGI(TAG, "     python wifi_ble_cli.py scan");
    ESP_LOGI(TAG, "     python wifi_ble_cli.py add \"SSID\" \"password\"");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  2. Captive Portal: Connect to AP 'ESP_xxxx'");
    ESP_LOGI(TAG, "     Then visit http://192.168.4.1/api/wifi/");
    ESP_LOGI(TAG, "");

    // Wait for connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    ret = wifi_cfg_wait_connected(30000);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected successfully!");
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout - use BLE or captive portal to configure");
    }

    // Main loop
    while (1) {
        if (wifi_cfg_is_connected()) {
            wifi_status_t status;
            if (wifi_cfg_get_status(&status) == ESP_OK) {
                ESP_LOGI(TAG, "Connected: %s - Signal: %d%%", status.ssid, status.quality);
            }
        } else {
            ESP_LOGW(TAG, "WiFi not connected - configure via BLE or captive portal");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
