/**
 * @file main.c
 * @brief ESP WiFi Config - BLE Soft Deinit Example (NimBLE)
 *
 * Demonstrates the "service-only" BLE mode where the application owns the
 * NimBLE stack and the WiFi Config only registers/unregisters its GATT
 * service. After WiFi provisioning completes, the WiFi Config's BLE service
 * is removed but the NimBLE stack keeps running so the application can use
 * BLE for its own purposes.
 *
 * Two post-provisioning use cases are shown (toggle with EXAMPLE_MODE below):
 *   MODE_GATT_SERVICE — Register an app GATT service and advertise
 *   MODE_SCAN_ONLY    — Scan for nearby BLE devices (observer/central role)
 *
 * Flow:
 *   1. App initializes NimBLE stack
 *   2. wifi_cfg_init() detects NimBLE already running, registers GATT
 *      service only (service-only mode)
 *   3. User provisions WiFi via BLE
 *   4. Once connected, wifi_cfg_deinit() removes the WiFi Config GATT
 *      service but leaves NimBLE running
 *   5a. (GATT mode) App registers its own GATT service and advertises
 *   5b. (Scan mode) App starts a BLE scan and logs discovered devices
 */

/** Change this to switch between the two demo modes. */
#define MODE_GATT_SERVICE  0
#define MODE_SCAN_ONLY     1
#define EXAMPLE_MODE       MODE_GATT_SERVICE

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi_config.h"
#include "esp_bus.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_deinit_example";

// ============================================================================
// Application's own GATT service (registered after WiFi provisioning)
// ============================================================================

#if EXAMPLE_MODE == MODE_GATT_SERVICE

// Custom service UUID: 0xAA00
static const ble_uuid16_t s_app_svc_uuid = BLE_UUID16_INIT(0xAA00);
static const ble_uuid16_t s_app_char_uuid = BLE_UUID16_INIT(0xAA01);

static int app_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char *msg = "Hello from app!";
        os_mbuf_append(ctxt->om, msg, strlen(msg));
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_app_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_app_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_app_char_uuid.u,
                .access_cb = app_gatt_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 },
        },
    },
    { 0 },
};

// ============================================================================
// App BLE advertising (after WiFi Config deinit)
// ============================================================================

static char s_app_device_name[32] = "ESP32-App";

static void app_start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)s_app_device_name;
    fields.name_len = strlen(s_app_device_name);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &adv_params, NULL, NULL);
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        ESP_LOGI(TAG, "App BLE advertising started: %s", s_app_device_name);
    } else {
        ESP_LOGE(TAG, "App advertising failed, rc=%d", rc);
    }
}

static esp_err_t app_register_gatt_services(void)
{
    // After wifi_cfg_deinit(), the GATT table has been committed with only
    // mandatory GAP/GATT services. To add new services we must reset and
    // re-register everything (mandatory + app) then commit.
    ble_gatts_reset();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_app_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed, rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_app_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed, rc=%d", rc);
        return ESP_FAIL;
    }

    ble_gatts_start();

    ESP_LOGI(TAG, "App GATT service 0xAA00 registered");
    return ESP_OK;
}

#endif // MODE_GATT_SERVICE

// ============================================================================
// BLE scanning (observer/central role — no advertising, no GATT service)
// ============================================================================

#if EXAMPLE_MODE == MODE_SCAN_ONLY

/** Scan duration in milliseconds (0 = indefinite). */
#define SCAN_DURATION_MS  10000

static int app_scan_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            const struct ble_gap_disc_desc *desc = &event->disc;

            // Extract device name from advertising data if present
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, desc->data,
                                              desc->length_data);

            char name_buf[32] = "(unknown)";
            if (rc == 0 && fields.name != NULL && fields.name_len > 0) {
                size_t copy_len = fields.name_len < sizeof(name_buf) - 1
                                  ? fields.name_len : sizeof(name_buf) - 1;
                memcpy(name_buf, fields.name, copy_len);
                name_buf[copy_len] = '\0';
            }

            ESP_LOGI(TAG, "Found: %02x:%02x:%02x:%02x:%02x:%02x  RSSI=%d  %s",
                     desc->addr.val[5], desc->addr.val[4], desc->addr.val[3],
                     desc->addr.val[2], desc->addr.val[1], desc->addr.val[0],
                     desc->rssi, name_buf);
            break;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            ESP_LOGI(TAG, "Scan complete (reason=%d)", event->disc_complete.reason);
            break;

        default:
            break;
    }
    return 0;
}

static void app_start_scan(void)
{
    struct ble_gap_disc_params scan_params = {0};
    scan_params.passive = 1;        // passive scan — don't send scan requests
    scan_params.filter_duplicates = 1;
    scan_params.itvl = 0x50;        // 50ms interval
    scan_params.window = 0x30;      // 30ms window

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, SCAN_DURATION_MS,
                          &scan_params, app_scan_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scan, rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "BLE scan started (%d ms)...", SCAN_DURATION_MS);
    }
}

#endif // MODE_SCAN_ONLY

// ============================================================================
// NimBLE host task and callbacks (owned by the application)
// ============================================================================

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ESP_LOGI(TAG, "NimBLE host synced");
    // Don't start advertising here — WiFi Config will handle it during
    // provisioning, and the app will start its own advertising after deinit.
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset, reason=%d", reason);
}

// ============================================================================
// WiFi Config event callbacks
// ============================================================================

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

// ============================================================================
// Main
// ============================================================================

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== BLE Soft Deinit Example ===");

    // ── Step 1: App initializes NimBLE stack ──
    ESP_LOGI(TAG, "Step 1: Initializing NimBLE stack (app-owned)");

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return;
    }

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_svc_gap_device_name_set("ESP32-WiFi-Prov");
    ble_svc_gap_init();
    ble_svc_gatt_init();

    nimble_port_freertos_init(nimble_host_task);
    ESP_LOGI(TAG, "NimBLE stack running");

    // ── Step 2: Initialize esp_bus + WiFi Config ──
    ESP_LOGI(TAG, "Step 2: Initializing WiFi Config (service-only BLE mode)");

    ret = esp_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize esp_bus: %s", esp_err_to_name(ret));
        return;
    }

    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_wifi_connected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_wifi_disconnected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_wifi_got_ip, NULL);

    wifi_cfg_config_t config = {
        .max_retry_per_network = 3,
        .retry_interval_ms = 5000,
        .auto_reconnect = true,

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

        .http = {
            .httpd = NULL,
            .api_base_path = "/api/wifi",
            .enable_auth = false,
        },

        // WiFi Config will detect NimBLE already running and use service-only mode
        .ble = {
            .enable = true,
            .device_name = NULL,
        },
    };

    ret = wifi_cfg_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi Config: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "WiFi Config initialized (BLE service-only mode)");
    ESP_LOGI(TAG, "Use BLE or captive portal to configure WiFi...");

    // ── Step 3: Wait for WiFi provisioning ──
    ESP_LOGI(TAG, "Step 3: Waiting for WiFi connection...");
    ret = wifi_cfg_wait_connected(60000);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No WiFi connection after 60s — keeping WiFi Config active");
        // In a real app you might loop or retry. For this example, we proceed
        // with deinit to demonstrate the BLE handoff regardless.
    } else {
        ESP_LOGI(TAG, "WiFi connected!");
    }

    // ── Step 4: Deinit WiFi Config (soft BLE teardown) ──
    ESP_LOGI(TAG, "Step 4: Deinitializing WiFi Config (BLE service removed, stack stays)");
    wifi_cfg_deinit(false);
    ESP_LOGI(TAG, "WiFi Config deinitialized — NimBLE stack still running");

    // ── Step 5: App takes over BLE ──
#if EXAMPLE_MODE == MODE_GATT_SERVICE
    ESP_LOGI(TAG, "Step 5: Registering app GATT service and advertising");

    ret = app_register_gatt_services();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register app GATT services");
        return;
    }

    app_start_advertising();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "App is now advertising its own BLE service (0xAA00).");
    ESP_LOGI(TAG, "The WiFi Config's 0xFFE0 service is gone.");

    while (1) {
        if (wifi_cfg_is_connected()) {
            wifi_status_t status;
            if (wifi_cfg_get_status(&status) == ESP_OK) {
                ESP_LOGI(TAG, "WiFi: %s (%d%%) | BLE: app service active",
                         status.ssid, status.quality);
            }
        } else {
            ESP_LOGW(TAG, "WiFi not connected | BLE: app service active");
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

#elif EXAMPLE_MODE == MODE_SCAN_ONLY
    ESP_LOGI(TAG, "Step 5: Starting BLE scan (no advertising, no GATT service)");
    ESP_LOGI(TAG, "");

    while (1) {
        app_start_scan();

        // Wait for scan to complete, then pause before the next round
        vTaskDelay(pdMS_TO_TICKS(SCAN_DURATION_MS + 5000));

        if (wifi_cfg_is_connected()) {
            wifi_status_t status;
            if (wifi_cfg_get_status(&status) == ESP_OK) {
                ESP_LOGI(TAG, "WiFi: %s (%d%%) | Restarting scan...",
                         status.ssid, status.quality);
            }
        } else {
            ESP_LOGI(TAG, "WiFi not connected | Restarting scan...");
        }
    }
#endif
}
