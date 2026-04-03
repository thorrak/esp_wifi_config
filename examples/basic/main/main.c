/**
 * @file main.c
 * @brief ESP WiFi Config - Basic Example
 *
 * This example demonstrates basic usage of the WiFi Config component:
 * - Initialize with default networks
 * - Enable HTTP REST API for configuration
 * - Provisioning mode: AP starts when no networks or all connections fail
 * - Subscribe to WiFi events
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi_config.h"
#include "esp_bus.h"

static const char *TAG = "wifi_example";

/**
 * @brief Callback for WiFi connected event
 */
static void on_wifi_connected(const char *event, const void *data, size_t len, void *ctx)
{
    const wifi_connected_t *info = (const wifi_connected_t *)data;
    ESP_LOGI(TAG, "WiFi connected to %s (RSSI: %d dBm, Channel: %d)",
             info->ssid, info->rssi, info->channel);
}

/**
 * @brief Callback for WiFi disconnected event
 */
static void on_wifi_disconnected(const char *event, const void *data, size_t len, void *ctx)
{
    const wifi_disconnected_t *info = (const wifi_disconnected_t *)data;
    ESP_LOGW(TAG, "WiFi disconnected from %s (reason: %d)", info->ssid, info->reason);
}

/**
 * @brief Callback for got IP event
 */
static void on_wifi_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    ESP_LOGI(TAG, "Got IP address");

    // Get full status
    wifi_status_t status;
    if (wifi_cfg_get_status(&status) == ESP_OK) {
        ESP_LOGI(TAG, "IP: %s", status.ip);
        ESP_LOGI(TAG, "Gateway: %s", status.gateway);
        ESP_LOGI(TAG, "Netmask: %s", status.netmask);
        ESP_LOGI(TAG, "DNS: %s", status.dns);
        ESP_LOGI(TAG, "MAC: %s", status.mac);
        ESP_LOGI(TAG, "Signal quality: %d%%", status.quality);
    }
}

/**
 * @brief Callback for variable changed event
 */
static void on_var_changed(const char *event, const void *data, size_t len, void *ctx)
{
    const wifi_var_t *var = (const wifi_var_t *)data;
    ESP_LOGI(TAG, "Variable changed: %s = %s", var->key, var->value);
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

    ESP_LOGI(TAG, "Starting WiFi Config example");

    // Initialize esp_bus first (required by wifi_cfg)
    ret = esp_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize esp_bus: %s", esp_err_to_name(ret));
        return;
    }

    // Subscribe to WiFi events (before wifi_cfg_init to catch early events)
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_wifi_connected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_wifi_disconnected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_wifi_got_ip, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_VAR_CHANGED), on_var_changed, NULL);

    // Initialize WiFi Config
    wifi_cfg_config_t config = {
        // Default networks (used if NVS is empty)
        // You can also configure networks via REST API or captive portal
        .default_networks = (wifi_network_t[]){
            {"YourWiFi", "YourPassword", 10},      // Priority 10 (highest)
            {"BackupWiFi", "BackupPassword", 5},   // Priority 5 (fallback)
        },
        .default_network_count = 2,

        // Default custom variables
        .default_vars = (wifi_var_t[]){
            {"device_name", "my-esp32"},
            {"server_url", "https://api.example.com"},
        },
        .default_var_count = 2,

        // Retry configuration
        .max_retry_per_network = 3,
        .retry_interval_ms = 5000,
        .auto_reconnect = true,

        // SoftAP configuration (for captive portal)
        .default_ap = {
            .ssid = "ESP_{id}",
            .password = "",           // Open network for easy setup
            .channel = 0,             // Auto channel selection
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
        .provisioning_teardown_delay_ms = 5000,  // 5s grace period before stopping AP
        .enable_ap = true,

        // HTTP REST API configuration
        .http = {
            .httpd = NULL,               // Create new HTTP server
            .api_base_path = "/api/wifi",
            .enable_auth = false,        // No authentication for this example
        },
    };

    ret = wifi_cfg_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi Config: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "WiFi Config initialized");
    ESP_LOGI(TAG, "HTTP API available at http://<device-ip>/api/wifi/");

    // Wait for connection with timeout
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    ret = wifi_cfg_wait_connected(30000);  // 30 second timeout

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected successfully!");

        // Get HTTP server handle to add custom endpoints
        httpd_handle_t httpd = wifi_cfg_get_httpd();
        if (httpd) {
            ESP_LOGI(TAG, "HTTP server handle available for custom endpoints");
            // You can register additional endpoints here:
            // httpd_uri_t my_uri = { ... };
            // httpd_register_uri_handler(httpd, &my_uri);
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout - captive portal should be active");
        ESP_LOGI(TAG, "Connect to AP '%s' and configure WiFi via http://192.168.4.1/api/wifi/",
                 config.default_ap.ssid);
    }

    // Main loop - your application code here
    while (1) {
        // Check connection status periodically
        if (wifi_cfg_is_connected()) {
            wifi_status_t status;
            if (wifi_cfg_get_status(&status) == ESP_OK) {
                ESP_LOGI(TAG, "Connected to %s - Signal: %d%% - Uptime: %lu ms",
                         status.ssid, status.quality, (unsigned long)status.uptime_ms);
            }
        } else {
            ESP_LOGW(TAG, "WiFi not connected");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));  // Check every 10 seconds
    }
}
