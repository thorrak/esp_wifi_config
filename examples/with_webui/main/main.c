/**
 * @file main.c
 * @brief ESP WiFi Config - Web UI Example
 *
 * This example demonstrates:
 * - WiFi Config with embedded Web UI
 * - Modern responsive interface accessible at device IP
 * - Captive portal for initial setup
 * - mDNS for easy access (esp32-xxx.local)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi_config.h"
#include "esp_bus.h"

static const char *TAG = "wifi_webui_example";

/**
 * @brief Callback for WiFi events
 */
static void on_wifi_event(const char *event, const void *data, size_t len, void *ctx)
{
    if (strcmp(event, WIFI_EVT(WIFI_CFG_EVT_CONNECTED)) == 0) {
        const wifi_connected_t *info = (const wifi_connected_t *)data;
        ESP_LOGI(TAG, "Connected to %s", info->ssid);
    } else if (strcmp(event, WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED)) == 0) {
        const wifi_disconnected_t *info = (const wifi_disconnected_t *)data;
        ESP_LOGW(TAG, "Disconnected from %s (reason: %d)", info->ssid, info->reason);
    } else if (strcmp(event, WIFI_EVT(WIFI_CFG_EVT_GOT_IP)) == 0) {
        wifi_status_t status;
        if (wifi_cfg_get_status(&status) == ESP_OK) {
            ESP_LOGI(TAG, "Got IP: %s", status.ip);
            ESP_LOGI(TAG, "Web UI: http://%s/", status.ip);
            if (status.hostname[0]) {
                ESP_LOGI(TAG, "mDNS: http://%s.local/", status.hostname);
            }
        }
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

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  ESP WiFi Config - Web UI Example");
    ESP_LOGI(TAG, "==============================================");

    // Initialize esp_bus
    ESP_ERROR_CHECK(esp_bus_init());

    // Subscribe to WiFi events
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_wifi_event, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_wifi_event, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_wifi_event, NULL);

    // Initialize WiFi Config with Web UI enabled
    wifi_cfg_config_t config = {
        .max_retry_per_network = 3,
        .retry_interval_ms = 5000,
        .auto_reconnect = true,

        .default_ap = {
            .ssid = "ESP32-Setup-{id}",
            .password = "",
            .channel = 0,
            .max_connections = 4,
            .ip = "192.168.4.1",
            .netmask = "255.255.255.0",
            .gateway = "192.168.4.1",
            .dhcp_start = "192.168.4.2",
            .dhcp_end = "192.168.4.20",
        },
        // Provisioning: start AP+HTTP when no networks or all fail
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,

        .http = {
            .api_base_path = "/api/wifi",
        },

        // mDNS for easy access
        .mdns = {
            .enable = true,
            .hostname = "esp32-{id}",
            .instance_name = "ESP32 WiFi Config",
        },

        // Web UI is auto-enabled via CONFIG_WIFI_CFG_ENABLE_WEBUI
    };

    ret = wifi_cfg_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi Config: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "WiFi Config initialized with Web UI");
    ESP_LOGI(TAG, "");

    // Wait for connection
    ret = wifi_cfg_wait_connected(30000);

    if (ret == ESP_OK) {
        wifi_status_t status;
        wifi_cfg_get_status(&status);
        ESP_LOGI(TAG, "Connected! Access Web UI at:");
        ESP_LOGI(TAG, "  http://%s/", status.ip);
        if (status.hostname[0]) {
            ESP_LOGI(TAG, "  http://%s.local/", status.hostname);
        }
    } else {
        ESP_LOGW(TAG, "No saved networks. Starting AP mode...");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "Connect to WiFi: ESP32-Setup-XXXXXX");
        ESP_LOGI(TAG, "Open http://192.168.4.1/ to configure");
        ESP_LOGI(TAG, "");
    }

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
