/**
 * @file main.c
 * @brief ESP WiFi Config — Network Provisioning over BLE example
 *
 * Demonstrates WiFi Config with the official ESP-IDF wifi_provisioning
 * manager (BLE scheme):
 *
 *   - Provision the device using Espressif's "ESP BLE Provisioning"
 *     mobile app (iOS/Android) or `idf.py monitor` + a custom client
 *   - HTTP REST API stays available for management after provisioning
 *   - Captive-portal SoftAP runs as an additional provisioning fallback
 *
 * The legacy custom BLE GATT (UUID 0xFFE0) interface that this example
 * previously demonstrated has been removed in favour of the standardised
 * provisioning protocol. See MIGRATION.md for the protocol-level migration
 * notes if you have client tools that still talk the old JSON-over-GATT
 * format.
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

        // Network Provisioning is enabled via
        // CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y in sdkconfig.
        // All other parameters now live in this struct.
        .prov_ble = {
            .device_name = "PROV_{id}",
            .security = WIFI_CFG_PROV_SECURITY_1,
            .pop = "abcd1234",
            .firmware_version = "1.0.0",
            .reset_on_failure = true,
            .max_failed_attempts = 3,
        },
    };

    ret = wifi_cfg_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi Config: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "WiFi Config initialized with Network Provisioning over BLE");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Configuration options:");
    ESP_LOGI(TAG, "  1. BLE: Use the 'ESP BLE Provisioning' app");
    ESP_LOGI(TAG, "     - Scan for 'PROV_xxxxxx'");
    ESP_LOGI(TAG, "     - Use Proof-of-Possession 'abcd1234' (set in main.c)");
    ESP_LOGI(TAG, "     - Pick a Wi-Fi network and enter its password");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  2. Captive Portal: Connect to AP 'ESP_xxxx'");
    ESP_LOGI(TAG, "     Then visit http://192.168.4.1/api/wifi/");
    ESP_LOGI(TAG, "");

    // Wait for connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    ret = wifi_cfg_wait_connected(60000);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected successfully!");
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout - use BLE provisioning or captive portal");
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
