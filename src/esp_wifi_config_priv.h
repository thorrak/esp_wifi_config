/**
 * @file esp_wifi_config_priv.h
 * @brief Private header for WiFi Config internal use
 */

#pragma once

#include "esp_wifi_config.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#ifdef __cplusplus
extern "C" {
#endif


// =============================================================================
// Configuration Defaults - Typically set via menuconfig
// =============================================================================


#ifndef CONFIG_WIFI_CFG_MAX_NETWORKS
#define CONFIG_WIFI_CFG_MAX_NETWORKS 5
#endif

#ifndef CONFIG_WIFI_CFG_MAX_VARS
#define CONFIG_WIFI_CFG_MAX_VARS 10
#endif

#ifndef CONFIG_WIFI_CFG_DEFAULT_RETRY
#define CONFIG_WIFI_CFG_DEFAULT_RETRY 3
#endif

#ifndef CONFIG_WIFI_CFG_RETRY_INTERVAL_MS
#define CONFIG_WIFI_CFG_RETRY_INTERVAL_MS 5000
#endif

// Derive CONFIG_WIFI_CFG_ENABLE_IMPROV from transport selections
#if defined(CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE) || defined(CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL)
#ifndef CONFIG_WIFI_CFG_ENABLE_IMPROV
#define CONFIG_WIFI_CFG_ENABLE_IMPROV 1
#endif
#endif

// Convenience: BLE stack is needed if either custom BLE or Improv BLE is enabled
#if defined(CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE) || defined(CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE)
#define WIFI_CFG_NEED_BLE 1
#endif

#define WIFI_CFG_DEFAULT_AP_SSID "ESP32-Config"
#define WIFI_CFG_DEFAULT_AP_PASSWORD ""
#define WIFI_CFG_DEFAULT_AP_IP "192.168.4.1"
#define WIFI_CFG_DEFAULT_BLE_DEVICE_NAME "ESP32-WiFi-{id}"

#ifndef CONFIG_WIFI_CFG_MAX_SCAN_RESULTS
#define CONFIG_WIFI_CFG_MAX_SCAN_RESULTS 20
#endif

#ifndef CONFIG_WIFI_CFG_HTTP_MAX_CONTENT_LEN
#define CONFIG_WIFI_CFG_HTTP_MAX_CONTENT_LEN 2048
#endif

#ifndef CONFIG_WIFI_CFG_TASK_STACK_SIZE
#define CONFIG_WIFI_CFG_TASK_STACK_SIZE 4096
#endif

#ifndef CONFIG_WIFI_CFG_TASK_PRIORITY
#define CONFIG_WIFI_CFG_TASK_PRIORITY 5
#endif

#ifndef CONFIG_WIFI_CFG_HTTP_MAX_URI_HANDLERS
#define CONFIG_WIFI_CFG_HTTP_MAX_URI_HANDLERS 32
#endif


// =============================================================================
// Constants
// =============================================================================

#define WIFI_CFG_NVS_NAMESPACE      "wifi_cfg"
#define WIFI_CFG_MAX_NETWORKS       CONFIG_WIFI_CFG_MAX_NETWORKS
#define WIFI_CFG_MAX_VARS           CONFIG_WIFI_CFG_MAX_VARS
#define WIFI_CFG_MAX_SCAN_RESULTS   CONFIG_WIFI_CFG_MAX_SCAN_RESULTS
#define WIFI_CFG_HTTP_MAX_CONTENT   CONFIG_WIFI_CFG_HTTP_MAX_CONTENT_LEN
#define WIFI_CFG_TASK_STACK_SIZE    CONFIG_WIFI_CFG_TASK_STACK_SIZE
#define WIFI_CFG_TASK_PRIORITY      CONFIG_WIFI_CFG_TASK_PRIORITY
#define WIFI_CFG_HTTP_MAX_HANDLERS  CONFIG_WIFI_CFG_HTTP_MAX_URI_HANDLERS
#define WIFI_CFG_QUEUE_SIZE         10

// Event bits (for sync waits)
#define WIFI_CONNECTED_BIT          BIT0
#define WIFI_FAIL_BIT               BIT1
#define WIFI_SCAN_DONE_BIT          BIT2

// =============================================================================
// Internal Events (sent to task queue)
// =============================================================================

typedef enum {
    WM_INT_EVT_NONE = 0,
    WM_INT_EVT_START,               // Start auto-connect
    WM_INT_EVT_STA_CONNECTED,       // STA connected to AP
    WM_INT_EVT_STA_DISCONNECTED,    // STA disconnected
    WM_INT_EVT_GOT_IP,              // Got IP address
    WM_INT_EVT_LOST_IP,             // Lost IP
    WM_INT_EVT_SCAN_COMPLETE,       // Scan finished
    WM_INT_EVT_AP_STARTED,          // AP started
    WM_INT_EVT_AP_STOPPED,          // AP stopped
    WM_INT_EVT_AP_STA_CONN,         // Client connected to AP
    WM_INT_EVT_CONNECT_REQUEST,     // Manual connect request
    WM_INT_EVT_DISCONNECT_REQUEST,  // Manual disconnect request
    WM_INT_EVT_START_AP_REQUEST,    // Manual start AP request
    WM_INT_EVT_STOP_AP_REQUEST,     // Manual stop AP request
    WM_INT_EVT_TEARDOWN_TIMER,      // Provisioning teardown delay expired
    WM_INT_EVT_START_PROVISIONING,  // Start provisioning from reconnect exhaustion
    WM_INT_EVT_STOP,                // Stop task
} wifi_cfg_internal_evt_t;

typedef struct {
    wifi_cfg_internal_evt_t type;
    union {
        struct {
            char ssid[32];
            uint8_t reason;
        } disconnect;
        struct {
            char ssid[32];
            int8_t rssi;
            uint8_t channel;
        } connected;
        struct {
            esp_netif_ip_info_t ip_info;
        } got_ip;
        struct {
            char ssid[32];
        } connect_req;
        uint8_t mac[6];
    } data;
} wifi_cfg_event_t;

// =============================================================================
// Internal Context
// =============================================================================

typedef struct {
    // State
    wifi_state_t state;
    bool initialized;
    bool ap_active;
    bool ble_active;                // BLE advertising currently running
    bool improv_ble_active;         // Improv BLE transport currently running
    bool improv_serial_active;      // Improv Serial transport currently running
    bool provisioning_active;       // Provisioning interfaces (AP/BLE) currently running
    bool connecting;                // Currently in connect sequence
    int64_t connect_time;           // Connection start time

    // Reconnect exhaustion
    uint16_t reconnect_attempt_count;  // Counter for post-connect reconnect exhaustion

    // Provisioning teardown timer
    TimerHandle_t teardown_timer;      // FreeRTOS one-shot timer for non-blocking teardown delay

    // HTTP handler tracking
    bool http_handlers_registered;           // Prevent double register/unregister of all handlers
    bool provisioning_handlers_registered;   // Track provisioning-specific endpoints

    // Config
    wifi_cfg_config_t config;
    
    // Saved data
    wifi_network_t networks[WIFI_CFG_MAX_NETWORKS];
    size_t network_count;
    wifi_var_t vars[WIFI_CFG_MAX_VARS];
    size_t var_count;
    wifi_cfg_ap_config_t ap_config;
    
    // Auth credentials (from NVS)
    char auth_username[32];
    char auth_password[64];
    
    // ESP-IDF handles
    esp_netif_t *sta_netif;
    esp_netif_t *ap_netif;
    bool sta_netif_owned;           // true if we created it
    bool ap_netif_owned;            // true if we created it
    httpd_handle_t httpd;
    bool httpd_owned;               // true if we created it
    
    // Task & Queue
    TaskHandle_t task;
    QueueHandle_t queue;
    
    // Sync
    EventGroupHandle_t event_group;
    SemaphoreHandle_t mutex;
    
    // Retry state
    int retry_count;
    int current_network_idx;
    char connected_ssid[32];        // SSID of currently connected network (empty if none)
    
    // Scan results (temporary)
    wifi_scan_result_t *scan_results;
    size_t scan_count;
    
} wifi_cfg_ctx_t;

// Global context
extern wifi_cfg_ctx_t *g_wifi_cfg;

// =============================================================================
// NVS Functions (esp_wifi_config_nvs.c)
// =============================================================================

esp_err_t wifi_cfg_nvs_init(void);
esp_err_t wifi_cfg_nvs_load_networks(wifi_network_t *networks, size_t max_count, size_t *count);
esp_err_t wifi_cfg_nvs_save_networks(const wifi_network_t *networks, size_t count);
esp_err_t wifi_cfg_nvs_load_vars(wifi_var_t *vars, size_t max_count, size_t *count);
esp_err_t wifi_cfg_nvs_save_vars(const wifi_var_t *vars, size_t count);
esp_err_t wifi_cfg_nvs_load_ap_config(wifi_cfg_ap_config_t *config);
esp_err_t wifi_cfg_nvs_save_ap_config(const wifi_cfg_ap_config_t *config);
esp_err_t wifi_cfg_nvs_load_auth(char *username, size_t ulen, char *password, size_t plen);
esp_err_t wifi_cfg_nvs_save_auth(const char *username, const char *password);
esp_err_t wifi_cfg_nvs_factory_reset(void);

// =============================================================================
// HTTP Functions (esp_wifi_config_http.c)
// =============================================================================

esp_err_t wifi_cfg_http_init(void);
esp_err_t wifi_cfg_http_unregister_handlers(void);
esp_err_t wifi_cfg_http_deinit(void);

// =============================================================================
// Internal Functions
// =============================================================================

// Send event to task queue (from event handlers or other contexts)
void wifi_cfg_send_event(wifi_cfg_internal_evt_t type);
void wifi_cfg_send_event_data(const wifi_cfg_event_t *event);

// Start connect sequence (non-blocking, called from task)
void wifi_cfg_start_connect_sequence(void);

// AP mode control (called from task)
void wifi_cfg_start_ap_mode(void);
void wifi_cfg_stop_ap_mode(void);

// =============================================================================
// Provisioning Orchestration
// =============================================================================

// Start all enabled provisioning interfaces (AP + BLE) per config
void wifi_cfg_start_provisioning(void);

// Stop all provisioning interfaces, transition HTTP per post-prov mode
void wifi_cfg_stop_provisioning(void);

// =============================================================================
// BLE Start/Stop (advertising control without full init/deinit)
// =============================================================================

esp_err_t wifi_cfg_ble_start(void);
esp_err_t wifi_cfg_ble_stop(void);

// =============================================================================
// HTTP Handler Registration
// =============================================================================

// Register/unregister API handlers (scan, networks, connect, etc.)
esp_err_t wifi_cfg_http_register_api_handlers(void);

// Register/unregister only the provisioning-specific HTTP handlers
// (captive portal detection, simple page, WebUI routes)
esp_err_t wifi_cfg_http_register_provisioning_handlers(void);
esp_err_t wifi_cfg_http_unregister_provisioning_handlers(void);

// Transition HTTP to post-provisioning mode
void wifi_cfg_http_transition_post_prov(wifi_http_post_prov_mode_t mode);

// =============================================================================
// esp_bus Handler (esp_wifi_config_bus.c)
// =============================================================================

esp_err_t wifi_cfg_bus_handler(const char *action,
                               const void *req_data, size_t req_len,
                               void *res_buf, size_t res_buf_size, size_t *res_len,
                               void *ctx);

// =============================================================================
// DNS Server (esp_wifi_config_dns.c) - Captive Portal
// =============================================================================

esp_err_t wifi_cfg_dns_start(void);
esp_err_t wifi_cfg_dns_stop(void);

// =============================================================================
// AP Functions (esp_wifi_config_ap.c)
// =============================================================================

void wifi_cfg_expand_template(const char *tmpl, char *output, size_t max_len);

// =============================================================================
// CLI Functions (esp_wifi_config_cli.c)
// =============================================================================

esp_err_t wifi_cfg_cli_init(void);

// =============================================================================
// Web UI Functions (esp_wifi_config_webui.c)
// =============================================================================

esp_err_t wifi_cfg_webui_init(httpd_handle_t httpd);

// =============================================================================
// BLE Functions (esp_wifi_config_ble.c)
// =============================================================================

esp_err_t wifi_cfg_ble_init(void);
esp_err_t wifi_cfg_ble_deinit(void);

// =============================================================================
// Utility Functions
// =============================================================================

// Convert RSSI to quality percentage (0-100)
static inline uint8_t rssi_to_quality(int8_t rssi) {
    if (rssi <= -100) return 0;
    if (rssi >= -50) return 100;
    return (uint8_t)(2 * (rssi + 100));
}

// Lock/unlock context
static inline void wifi_cfg_lock(void) {
    if (g_wifi_cfg && g_wifi_cfg->mutex) {
        xSemaphoreTake(g_wifi_cfg->mutex, portMAX_DELAY);
    }
}

static inline void wifi_cfg_unlock(void) {
    if (g_wifi_cfg && g_wifi_cfg->mutex) {
        xSemaphoreGive(g_wifi_cfg->mutex);
    }
}

#ifdef __cplusplus
}
#endif
