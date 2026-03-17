/**
 * @file main.c
 * @brief ESP WiFi Manager - Improv WiFi Example
 *
 * This example demonstrates WiFi provisioning using the Improv WiFi standard
 * (https://www.improv-wifi.com/) alongside the existing custom BLE GATT interface.
 *
 * Provisioning methods available:
 *   - Improv BLE: Use Chrome/Edge Web Bluetooth (improv-wifi.com) or ESPHome app
 *   - Improv Serial: Use Chrome/Edge Web Serial (improv-wifi.com)
 *   - Custom BLE GATT (0xFFE0): JSON-based protocol via Python CLI or custom app
 *   - Captive Portal: Connect to the SoftAP and configure via browser
 *
 * The Improv BLE service (UUID 00467768-...) coexists with the custom BLE
 * service (UUID 0xFFE0) — both are advertised simultaneously.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi_manager.h"
#include "esp_bus.h"

static const char *TAG = "wifi_improv_example";

// LED GPIO for identify callback (adjust for your board)
// Set to -1 to disable
#define IDENTIFY_LED_GPIO  -1

#if IDENTIFY_LED_GPIO >= 0
#include "driver/gpio.h"
#endif

/**
 * Improv "Identify" callback — called when a client sends the Identify RPC.
 * Flash an LED or produce some visible/audible indication so the user
 * can confirm they're connecting to the right device.
 */
static void on_improv_identify(void)
{
    ESP_LOGI(TAG, "** IDENTIFY requested — flash LED or beep **");

#if IDENTIFY_LED_GPIO >= 0
    gpio_set_direction(IDENTIFY_LED_GPIO, GPIO_MODE_OUTPUT);
    for (int i = 0; i < 6; i++) {
        gpio_set_level(IDENTIFY_LED_GPIO, i % 2);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    gpio_set_level(IDENTIFY_LED_GPIO, 0);
#endif
}

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
    if (wifi_manager_get_status(&status) == ESP_OK) {
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

    ESP_LOGI(TAG, "Starting WiFi Manager with Improv WiFi example");

    // Initialize esp_bus
    ret = esp_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize esp_bus: %s", esp_err_to_name(ret));
        return;
    }

    // Subscribe to WiFi events
    esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_CONNECTED), on_wifi_connected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_DISCONNECTED), on_wifi_disconnected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_GOT_IP), on_wifi_got_ip, NULL);

    // Initialize WiFi Manager with Improv + BLE + AP
    wifi_manager_config_t config = {
        // Retry configuration
        .max_retry_per_network = 3,
        .retry_interval_ms = 5000,
        .auto_reconnect = true,

        // SoftAP configuration (captive portal)
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
        // Provisioning: start AP+BLE+Improv when no networks or all fail
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,

        // HTTP REST API
        .http = {
            .httpd = NULL,
            .api_base_path = "/api/wifi",
            .enable_auth = false,
        },

        // Custom BLE GATT (0xFFE0) — JSON-based protocol
        // Can be disabled if only Improv is needed; the BLE stack will still
        // be initialized automatically when .improv.enable_ble is true.
        .ble = {
            .enable = false,
            .device_name = NULL,  // Kconfig default: "ESP32-WiFi-{id}"
        },

        // Improv WiFi — open standard for browser/app provisioning
        .improv = {
            .enable_ble = true,               // Improv BLE (Web Bluetooth)
            .enable_serial = false,            // Set true for Improv Serial (Web Serial)
            .firmware_name = "wifi_improv_example",
            .firmware_version = "1.0.0",
            .device_name = "ESP32 Improv Demo",
            .on_identify = on_improv_identify, // LED flash on Identify RPC
        },
    };

    ret = wifi_manager_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi Manager: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "WiFi Manager initialized with Improv WiFi");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Provisioning options:");
    ESP_LOGI(TAG, "  1. Improv BLE: Open https://www.improv-wifi.com/ in Chrome/Edge");
    ESP_LOGI(TAG, "     Click 'Connect device via Bluetooth' and select this device");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  2. ESPHome Companion App (Android/iOS):");
    ESP_LOGI(TAG, "     The device will appear automatically for Improv provisioning");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  3. Custom BLE: Use Python CLI or custom app (UUID 0xFFE0)");
    ESP_LOGI(TAG, "     python wifi_ble_cli.py scan");
    ESP_LOGI(TAG, "     python wifi_ble_cli.py add \"SSID\" \"password\"");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  4. Captive Portal: Connect to AP 'ESP_xxxx'");
    ESP_LOGI(TAG, "     Then visit http://192.168.4.1");
    ESP_LOGI(TAG, "");

    // Wait for connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    ret = wifi_manager_wait_connected(30000);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected successfully!");
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout - use Improv, BLE, or captive portal");
    }

    // Main loop
    while (1) {
        if (wifi_manager_is_connected()) {
            wifi_status_t status;
            if (wifi_manager_get_status(&status) == ESP_OK) {
                ESP_LOGI(TAG, "Connected: %s - Signal: %d%%", status.ssid, status.quality);
            }
        } else {
            ESP_LOGW(TAG, "WiFi not connected - provision via Improv or other methods");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
