/**
 * @file main.c
 * @brief ESP WiFi Config - Web UI Example
 *
 * This example demonstrates:
 * - WiFi Config with embedded Web UI
 * - Modern responsive interface accessible at device IP
 * - Captive portal for initial setup
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi_config.h"

static const char *TAG = "wifi_webui_example";

/**
 * @brief Callback for WiFi events
 */
static void on_wifi_event(wifi_cfg_event_t event, const void *data, size_t len, void *ctx)
{
    if (event == WIFI_CFG_EVENT_CONNECTED) {
        const wifi_connected_t *info = (const wifi_connected_t *)data;
        ESP_LOGI(TAG, "Connected to %s", info->ssid);
    } else if (event == WIFI_CFG_EVENT_DISCONNECTED) {
        const wifi_disconnected_t *info = (const wifi_disconnected_t *)data;
        ESP_LOGW(TAG, "Disconnected from %s (reason: %d)", info->ssid, info->reason);
    } else if (event == WIFI_CFG_EVENT_GOT_IP) {
        wifi_status_t status;
        if (wifi_cfg_get_status(&status) == ESP_OK) {
            ESP_LOGI(TAG, "Got IP: %s", status.ip);
            ESP_LOGI(TAG, "Web UI: http://%s/", status.ip);
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

    // Subscribe to WiFi events
    wifi_cfg_event_subscribe(WIFI_CFG_EVENT_CONNECTED, on_wifi_event, NULL, NULL);
    wifi_cfg_event_subscribe(WIFI_CFG_EVENT_DISCONNECTED, on_wifi_event, NULL, NULL);
    wifi_cfg_event_subscribe(WIFI_CFG_EVENT_GOT_IP, on_wifi_event, NULL, NULL);

    // Initialize WiFi Config with Web UI enabled
    wifi_cfg_config_t config = {
        WIFI_CFG_DEFAULTS,   // required: init no longer patches unset fields

        // provisioning_mode defaults to WIFI_PROV_ON_FAILURE: AP+HTTP start
        // when no networks are saved or every saved network fails.
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,

        // Web UI is auto-enabled via CONFIG_WIFI_CFG_ENABLE_WEBUI
    };

    // Nested sub-structs are set after the initialiser: a designated
    // initialiser for a nested struct replaces the whole sub-struct, blanking
    // what WIFI_CFG_DEFAULTS put there, and GCC warns (-Woverride-init).
    snprintf(config.default_ap.ssid, sizeof(config.default_ap.ssid),
             "ESP32-Setup-{id}");

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
