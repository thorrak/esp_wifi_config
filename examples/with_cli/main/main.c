/**
 * @file main.c
 * @brief ESP WiFi Config - CLI Example
 *
 * This example demonstrates:
 * - WiFi Config with CLI interface enabled
 * - Using ESP Console REPL for interactive commands
 * - Available commands: wifi_status, wifi_scan, wifi_list, wifi_add, wifi_del,
 *   wifi_connect, wifi_disconnect, wifi_ap_start, wifi_ap_stop, wifi_reset,
 *   wifi_var_get, wifi_var_set
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_wifi_config.h"
#include "esp_bus.h"

static const char *TAG = "wifi_cli_example";

/**
 * @brief Initialize console with REPL
 */
static void init_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp32>";
    repl_config.max_cmdline_length = 256;

    // Register help command
    esp_console_register_help_command();

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

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
    ESP_LOGI(TAG, "  ESP WiFi Config - CLI Example");
    ESP_LOGI(TAG, "==============================================");

    // Initialize esp_bus
    ESP_ERROR_CHECK(esp_bus_init());

    // Subscribe to WiFi events
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_wifi_event, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_wifi_event, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_wifi_event, NULL);

    // Initialize WiFi Config with CLI enabled
    wifi_cfg_config_t config = {
        .max_retry_per_network = 3,
        .retry_interval_ms = 5000,
        .auto_reconnect = true,

        .default_ap = {
            .ssid = "ESP32-CLI-{id}",
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
        .enable_ap = true,

        .http = {
            .api_base_path = "/api/wifi",
        },

        // CLI is auto-enabled via CONFIG_WIFI_CFG_ENABLE_CLI
    };

    ret = wifi_cfg_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi Config: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize console REPL
    init_console();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "CLI is ready. Available commands:");
    ESP_LOGI(TAG, "  wifi_status     - Show WiFi status");
    ESP_LOGI(TAG, "  wifi_scan       - Scan for networks");
    ESP_LOGI(TAG, "  wifi_list       - List saved networks");
    ESP_LOGI(TAG, "  wifi_add        - Add network: wifi_add <ssid> [password] [-p priority]");
    ESP_LOGI(TAG, "  wifi_del        - Delete network: wifi_del <ssid>");
    ESP_LOGI(TAG, "  wifi_connect    - Connect: wifi_connect [ssid]");
    ESP_LOGI(TAG, "  wifi_disconnect - Disconnect from WiFi");
    ESP_LOGI(TAG, "  wifi_ap_start   - Start access point");
    ESP_LOGI(TAG, "  wifi_ap_stop    - Stop access point");
    ESP_LOGI(TAG, "  wifi_reset      - Factory reset");
    ESP_LOGI(TAG, "  wifi_var_get    - Get variable: wifi_var_get <key>");
    ESP_LOGI(TAG, "  wifi_var_set    - Set variable: wifi_var_set <key> <value>");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Type 'help' for more commands.");
    ESP_LOGI(TAG, "");
}
