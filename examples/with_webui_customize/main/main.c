/**
 * @file main.c
 * @brief ESP WiFi Config - Customizable Web UI Example
 *
 * This example demonstrates:
 * - WiFi Config with customizable Web UI from LittleFS
 * - Custom frontend files served from /littlefs/
 * - Fallback to embedded Web UI if files not found
 * - Copy frontend/ folder and build your own UI
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"
#include "esp_wifi_config.h"

static const char *TAG = "wifi_webui_custom";

/**
 * @brief Initialize LittleFS filesystem
 */
static esp_err_t init_littlefs(void)
{
    ESP_LOGI(TAG, "Initializing LittleFS");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS: %zu/%zu bytes used", used, total);
    }

    return ESP_OK;
}

/**
 * @brief Callback for WiFi events
 */
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    if (event_id == WIFI_CFG_EVENT_CONNECTED) {
        const wifi_connected_t *info = (const wifi_connected_t *)data;
        ESP_LOGI(TAG, "Connected to %s", info->ssid);
    } else if (event_id == WIFI_CFG_EVENT_GOT_IP) {
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
    ESP_LOGI(TAG, "  ESP WiFi Config - Customizable Web UI");
    ESP_LOGI(TAG, "==============================================");

    // Initialize LittleFS for custom frontend
    init_littlefs();

    // The default event loop must exist before registering handlers.
    // wifi_cfg_init() creates it too, but registering first is what catches
    // the events emitted during startup.
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Subscribe to WiFi events
    esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_CONNECTED,
                               on_wifi_event, NULL);
    esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP,
                               on_wifi_event, NULL);

    // Initialize WiFi Config
    wifi_cfg_config_t config = {
        WIFI_CFG_DEFAULTS,   // required: init no longer patches unset fields

        // provisioning_mode defaults to WIFI_PROV_ON_FAILURE: AP+HTTP start
        // when no networks are saved or every saved network fails.
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,
    };

    // Nested sub-structs are set after the initialiser: a designated
    // initialiser for a nested struct replaces the whole sub-struct, blanking
    // what WIFI_CFG_DEFAULTS put there, and GCC warns (-Woverride-init).
    snprintf(config.default_ap.ssid, sizeof(config.default_ap.ssid),
             "ESP32-Custom-{id}");

    ret = wifi_cfg_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi Config: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "WiFi Config with customizable Web UI");
    ESP_LOGI(TAG, "Custom files from: /littlefs/");
    ESP_LOGI(TAG, "  - /littlefs/index.html");
    ESP_LOGI(TAG, "  - /littlefs/assets/app.js.gz");
    ESP_LOGI(TAG, "  - /littlefs/assets/index.css.gz");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "If custom files not found, embedded UI is used.");
    ESP_LOGI(TAG, "");

    // Wait for connection
    ret = wifi_cfg_wait_connected(30000);

    if (ret == ESP_OK) {
        wifi_status_t status;
        wifi_cfg_get_status(&status);
        ESP_LOGI(TAG, "Connected! Web UI at http://%s/", status.ip);
    } else {
        ESP_LOGW(TAG, "No saved networks. Connect to AP: ESP32-Custom-XXXXXX");
        ESP_LOGI(TAG, "Then open http://192.168.4.1/");
    }

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
