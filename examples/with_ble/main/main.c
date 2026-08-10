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

/* ── Security 2 (SRP6a) test credentials ──────────────────────────────────
 * TEST ONLY — generated offline for username "wificfg" / password "abcd1234"
 * via: esp_prov.py --sec_ver 2 --sec2_gen_cred --sec2_username wificfg
 *                  --sec2_pwd abcd1234
 * The firmware stores the salt + verifier (NOT the raw password); the client
 * proves knowledge of the password "abcd1234" over SRP6a. Required because
 * wifi_cfg_prov_validate() refuses to start Security 2 without these.
 * Do not ship these specific bytes in a real product. */
static const uint8_t sec2_salt[] = {
    0x5a, 0xb8, 0x11, 0xb2, 0x9e, 0x3e, 0x86, 0x2d, 0x22, 0xc2, 0x58, 0xbb, 0x9e, 0xc6, 0xf0, 0x34
};
static const uint8_t sec2_verifier[] = {
    0x94, 0x4a, 0x5b, 0x57, 0xc5, 0x2c, 0x69, 0x15, 0x6e, 0x26, 0x61, 0xed, 0x3f, 0x4d, 0xf0, 0x96,
    0xef, 0x2a, 0x8c, 0x37, 0x36, 0x2e, 0x0f, 0x43, 0x07, 0xa4, 0x0f, 0x94, 0xd7, 0xea, 0x0d, 0x5d,
    0x35, 0xf5, 0x5a, 0xec, 0xcd, 0xb9, 0xfe, 0xca, 0xcc, 0xba, 0xa8, 0x7e, 0x00, 0xb2, 0x7d, 0xc2,
    0x8b, 0xc6, 0xd9, 0x99, 0xd7, 0x8e, 0xdf, 0x9b, 0xa0, 0x10, 0xd6, 0x6c, 0xc1, 0x29, 0x8e, 0x02,
    0xd2, 0x19, 0xca, 0x7e, 0x8d, 0xeb, 0xa3, 0x91, 0xe4, 0x42, 0xfd, 0xa7, 0xf6, 0x81, 0xb3, 0x16,
    0xe0, 0xfc, 0x07, 0x21, 0x39, 0xf7, 0x90, 0x62, 0x10, 0x41, 0x32, 0x19, 0x44, 0x24, 0x41, 0x25,
    0x3d, 0x07, 0x21, 0xfb, 0xe1, 0xd2, 0x78, 0x38, 0x3f, 0xd5, 0x98, 0x4c, 0xf7, 0x14, 0x26, 0xbb,
    0x79, 0x1b, 0x22, 0x22, 0xf9, 0x2e, 0xf5, 0xcb, 0x2e, 0xa3, 0xde, 0x9e, 0x51, 0xb2, 0xb1, 0x96,
    0x0d, 0x62, 0x9a, 0x33, 0x64, 0x44, 0x05, 0x35, 0x55, 0xf7, 0x92, 0xd6, 0x41, 0x20, 0x4e, 0xd5,
    0x6e, 0xd0, 0xbe, 0x20, 0x89, 0xac, 0x35, 0xc7, 0x67, 0xd7, 0x74, 0xeb, 0x33, 0xe1, 0xb4, 0x72,
    0xb5, 0x44, 0xc6, 0xb7, 0x0e, 0x18, 0x84, 0x1c, 0x44, 0xc1, 0xfa, 0xdb, 0x85, 0x1e, 0x09, 0x3a,
    0x9c, 0xe0, 0x4e, 0xc9, 0x0b, 0xbf, 0x7b, 0xe4, 0xaf, 0x2f, 0x35, 0x11, 0x95, 0x0a, 0x8d, 0xc2,
    0x99, 0xbd, 0xd7, 0x13, 0x8b, 0x09, 0x87, 0xe2, 0x74, 0xeb, 0x44, 0xd4, 0x7e, 0xc9, 0x93, 0x3c,
    0x4b, 0x01, 0x64, 0xd8, 0xd3, 0xb7, 0x79, 0x86, 0x4e, 0x26, 0x93, 0xf8, 0xe4, 0xb2, 0x18, 0x19,
    0x0e, 0xa2, 0x41, 0x05, 0xf8, 0x41, 0xc5, 0x59, 0x9a, 0xb5, 0x7b, 0x42, 0x08, 0x89, 0xf0, 0x91,
    0x82, 0xcc, 0x02, 0xd1, 0x71, 0x76, 0xd7, 0x49, 0x87, 0xc2, 0xd8, 0x7b, 0xbc, 0xb2, 0xfd, 0x47,
    0x24, 0xbb, 0xd2, 0x32, 0x4c, 0x69, 0x68, 0xa5, 0x80, 0x2a, 0xb5, 0x8a, 0x01, 0xdd, 0xa7, 0x02,
    0x29, 0x5b, 0x20, 0xb8, 0xd0, 0xe5, 0x62, 0x71, 0x90, 0x0f, 0xfc, 0x0f, 0x9b, 0xe6, 0xc4, 0x73,
    0x5f, 0x63, 0x99, 0x16, 0x79, 0xb3, 0x5a, 0x1d, 0xd8, 0xcb, 0x23, 0xb5, 0xa0, 0xb1, 0xeb, 0x65,
    0xb5, 0xdb, 0x99, 0x41, 0x88, 0x36, 0x8d, 0xd3, 0x1d, 0xa1, 0x1c, 0x93, 0x07, 0xba, 0xf1, 0x08,
    0xc2, 0xeb, 0x3f, 0x59, 0x4e, 0x02, 0x7a, 0x69, 0x1c, 0x0f, 0xd1, 0x9f, 0x99, 0x40, 0x48, 0xe5,
    0x80, 0x08, 0xf3, 0x29, 0xbf, 0x82, 0x20, 0xd7, 0x85, 0xc0, 0x62, 0xe4, 0xa4, 0x8f, 0xe0, 0x8a,
    0xe5, 0x68, 0x3f, 0x3f, 0xf5, 0xe6, 0xcf, 0x14, 0x06, 0x4c, 0xd7, 0x78, 0xfb, 0x92, 0x82, 0x65,
    0x3b, 0x93, 0xa1, 0x4c, 0x3a, 0x54, 0xfb, 0xaf, 0xe2, 0x8a, 0xbd, 0x69, 0x83, 0x27, 0x7c, 0x54
};

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
        WIFI_CFG_DEFAULTS,   // required: init no longer patches unset fields

        // SoftAP configuration (for captive portal). Only the SSID differs
        // from the defaults; wifi_cfg_init() backfills the rest.
        .default_ap = {
            .ssid = "ESP_{id}",
        },
        // provisioning_mode defaults to WIFI_PROV_ON_FAILURE: AP+BLE+HTTP
        // start when no networks are saved or every saved network fails.
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,

        // Network Provisioning is enabled via
        // CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y in sdkconfig.
        // All other parameters now live in this struct.
        //
        // device_name is left at its default, "PROV_{id}", where {id} expands
        // to the last three bytes of the STA MAC.
        .prov_ble = {
            .security = WIFI_CFG_PROV_SECURITY_2,
            .pop = "abcd1234",
            .security2_username = "wificfg",
            .security2_salt = sec2_salt,
            .security2_salt_len = sizeof(sec2_salt),
            .security2_verifier = sec2_verifier,
            .security2_verifier_len = sizeof(sec2_verifier),
            .firmware_version = "1.0.0",
            .reset_on_failure = true,
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
