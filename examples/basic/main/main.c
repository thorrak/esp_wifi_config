/**
 * @file main.c
 * @brief ESP WiFi Config - Basic Example
 *
 * This example demonstrates basic usage of the WiFi Config component:
 * - Initialize with default networks
 * - Enable HTTP REST API for configuration
 * - Provisioning mode: AP starts when no networks or all connections fail
 * - Subscribe to WiFi events
 *
 * app_main() builds the config twice on purpose. `config` is the one that runs
 * and is written the way you should write it: WIFI_CFG_DEFAULTS, then only
 * what differs. `all_options` is never used — it writes out every field with
 * its default so this file is also the catalogue of what there is to
 * configure, which is easier to skim than the header.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi_config.h"

static const char *TAG = "wifi_example";

/**
 * @brief Callback for WiFi connected event
 */
static void on_wifi_connected(wifi_cfg_event_t event, const void *data, size_t len, void *ctx)
{
    const wifi_connected_t *info = (const wifi_connected_t *)data;
    ESP_LOGI(TAG, "WiFi connected to %s (RSSI: %d dBm, Channel: %d)",
             info->ssid, info->rssi, info->channel);
}

/**
 * @brief Callback for WiFi disconnected event
 */
static void on_wifi_disconnected(wifi_cfg_event_t event, const void *data, size_t len, void *ctx)
{
    const wifi_disconnected_t *info = (const wifi_disconnected_t *)data;
    ESP_LOGW(TAG, "WiFi disconnected from %s (reason: %d)", info->ssid, info->reason);
}

/**
 * @brief Callback for got IP event
 */
static void on_wifi_got_ip(wifi_cfg_event_t event, const void *data, size_t len, void *ctx)
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
static void on_var_changed(wifi_cfg_event_t event, const void *data, size_t len, void *ctx)
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

    // Subscribe to WiFi events (before wifi_cfg_init to catch early events)
    wifi_cfg_event_subscribe(WIFI_CFG_EVENT_CONNECTED, on_wifi_connected, NULL, NULL);
    wifi_cfg_event_subscribe(WIFI_CFG_EVENT_DISCONNECTED, on_wifi_disconnected, NULL, NULL);
    wifi_cfg_event_subscribe(WIFI_CFG_EVENT_GOT_IP, on_wifi_got_ip, NULL, NULL);
    wifi_cfg_event_subscribe(WIFI_CFG_EVENT_VAR_CHANGED, on_var_changed, NULL, NULL);

    // This is how to write it: start from WIFI_CFG_DEFAULTS, then set only
    // what differs. The macro is not optional — wifi_cfg_init() no longer
    // patches fields left at zero, so a struct built without it gets a zero
    // retry interval (rejected with ESP_ERR_INVALID_ARG) and
    // auto_reconnect = false.
    //
    // Everything this example does not name keeps its documented default. For
    // what those are and what else there is to set, read `all_options` below —
    // it is the same struct with every field written out.
    wifi_cfg_config_t config = {
        WIFI_CFG_DEFAULTS,

        // Default networks and variables, used only if NVS is empty. Both are
        // also settable at runtime over the REST API or captive portal, and
        // NVS always wins over these.
        .default_networks = (wifi_network_t[]){
            {"YourWiFi", "YourPassword", 10},      // Priority 10 (highest)
            {"BackupWiFi", "BackupPassword", 5},   // Priority 5 (fallback)
        },
        .default_network_count = 2,
        .default_vars = (wifi_var_t[]){
            {"device_name", "my-esp32"},
            {"server_url", "https://api.example.com"},
        },
        .default_var_count = 2,

        .stop_provisioning_on_connect   = true,   // default false
        .provisioning_teardown_delay_ms = 5000,   // default 0; grace period so
                                                  // the UI can show the result
        .enable_ap                      = true,   // default false
    };

    // Nested sub-structs are set afterwards, not in the initialiser above. A
    // designated initialiser for a nested struct replaces the *whole*
    // sub-struct, so `.default_ap = { .ssid = "x" }` would blank the IP,
    // netmask, DHCP range and connection limit that WIFI_CFG_DEFAULTS just
    // filled in — and GCC says so (-Woverride-init). wifi_cfg_init() backfills
    // the AP defaults to cover that mistake, but not making it is better.
    snprintf(config.default_ap.ssid, sizeof(config.default_ap.ssid),
             "ESP_{id}");   // {id} expands to the STA MAC suffix

    // ---- Every option, with its default ---------------------------------
    //
    // Never passed to wifi_cfg_init(); it exists to be read. This example is
    // the reference for what there is to configure, and a catalogue is easier
    // to skim than the header.
    //
    // Two things it deliberately does differently from `config` above, and
    // neither is a pattern to copy:
    //
    //   - No WIFI_CFG_DEFAULTS. Restating a field the macro already set is an
    //     override, and GCC warns (-Woverride-init) on all eleven of them.
    //     That is fine for a struct nothing initialises, and wrong for one you
    //     pass to init: a field added to wifi_cfg_config_t after this was
    //     written would silently be zero rather than its default.
    //   - Values are pinned. If a library default changes, the literals here
    //     go stale — whereas `config` above follows it automatically.
    //
    // So: read this one, write the one above.
    const wifi_cfg_config_t all_options = {
        // ── Defaults seeded into NVS on first boot ───────────────────────
        .default_networks       = NULL,
        .default_network_count  = 0,
        .default_vars           = NULL,
        .default_var_count      = 0,

        // ── Retry and reconnect ──────────────────────────────────────────
        .max_retry_per_network  = 3,        // attempts per network before moving on
        .retry_interval_ms      = 5000,     // first backoff; doubles per retry
        .retry_max_interval_ms  = 60000,    // backoff ceiling
        .auto_reconnect         = true,     // reconnect after a post-connect drop
        .max_reconnect_attempts = 0,        // 0 = keep trying forever
        // Only consulted when max_reconnect_attempts is non-zero. The other
        // member of this enum is [DISABLED].
        .on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_RESTART,

        // ── Provisioning lifecycle ───────────────────────────────────────
        // ON_FAILURE: start AP+BLE when nothing is saved, or when every saved
        // network fails. WHEN_UNPROVISIONED starts only on the first; MANUAL
        // waits for wifi_cfg_start_provisioning(). ALWAYS exists but is
        // currently [DISABLED] and behaves as MANUAL.
        .provisioning_mode              = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect   = false,
        .provisioning_teardown_delay_ms = 0,      // 0 = tear down immediately

        // What happens to HTTP once provisioning stops. API_ONLY drops the UI
        // and captive-portal routes; DISABLED drops everything the library
        // registered.
        .http_post_prov_mode = WIFI_HTTP_FULL,

        // ── SoftAP ───────────────────────────────────────────────────────
        .default_ap = {
            .ssid            = WIFI_CFG_DEFAULT_AP_SSID,      // "ESP32-Config"
            .password        = WIFI_CFG_DEFAULT_AP_PASSWORD,  // "" = open
            .channel         = 0,                             // 0 = auto
            .max_connections = 4,
            .hidden          = false,
            .ip              = WIFI_CFG_DEFAULT_AP_IP,        // "192.168.4.1"
            .netmask         = "255.255.255.0",
            .gateway         = WIFI_CFG_DEFAULT_AP_IP,
            // Stored and reported over the API, but not yet programmed into
            // the netif's DHCP server — see the note on wifi_cfg_ap_config_t.
            .dhcp_start      = "192.168.4.2",
            .dhcp_end        = "192.168.4.20",
        },
        .always_use_ap_defaults = false,  // true = ignore NVS-saved AP config
        .enable_ap              = false,  // enable SoftAP as a provisioning method

        // ── Callbacks ────────────────────────────────────────────────────
        .on_before_var_set  = NULL,  // reject invalid values before they hit NVS
        .var_validator_ctx  = NULL,

        // ── HTTP REST API ────────────────────────────────────────────────
        .http = {
            .httpd            = NULL,   // NULL = create a server; pass your
                                        // own handle to share one
            .api_base_path    = "/api/wifi",
            .enable_auth      = false,
            .auth_username    = "admin",
            .auth_password    = "admin",
            .pre_request_hook = NULL,   // runs before every API handler
            .hook_ctx         = NULL,
        },

        // ── Improv Wi-Fi ─────────────────────────────────────────────────
        // Inert unless CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL or _BLE is set.
        // NULL means "use the library default". See examples/with_improv.
        .improv = {
            .serial_uart_num  = CONFIG_WIFI_MGR_IMPROV_SERIAL_UART_NUM,
            .serial_baud_rate = CONFIG_WIFI_MGR_IMPROV_SERIAL_BAUD,
            .firmware_name    = NULL,   // reported by the Device-Info RPC
            .firmware_version = NULL,
            .device_name      = NULL,   // shown after a client connects
            .ble_device_name  = NULL,   // advertised name; "ESP32-WiFi-{id}"
            .on_identify      = NULL,   // flash an LED, beep, etc.
        },

        // ── BLE provisioning (Espressif protocomm) ───────────────────────
        // Inert unless CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE is set. Only
        // the three members with non-zero defaults are listed here; the other
        // ~24 cover advertising identity, security 0/1/2 with its PoP and
        // SRP6a salt/verifier, the Bluetooth memory policy, custom protocomm
        // endpoints and the credential callbacks. All of them are catalogued
        // the same way as this struct, in examples/with_ble.
        .prov_ble = {
            .cleanup_delay_ms    = 1000,   // grace window before protocomm
                                           // shutdown, for custom endpoints
            .reboot_max_wait_ms  = 15000,  // backstop if the client never
                                           // disconnects after success
            .max_failed_attempts = 3,
        },
    };
    (void)all_options;  // documentation, not configuration

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
