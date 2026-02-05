/**
 * @file esp_wifi_manager_priv.h
 * @brief Private header for WiFi Manager internal use
 */

#pragma once

#include "esp_wifi_manager.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif


// =============================================================================
// Configuration Defaults - Typically set via menuconfig
// =============================================================================


#ifndef CONFIG_WIFI_MGR_MAX_NETWORKS
#define CONFIG_WIFI_MGR_MAX_NETWORKS 5
#endif

#ifndef CONFIG_WIFI_MGR_MAX_VARS
#define CONFIG_WIFI_MGR_MAX_VARS 10
#endif

#ifndef CONFIG_WIFI_MGR_DEFAULT_RETRY
#define CONFIG_WIFI_MGR_DEFAULT_RETRY 3
#endif

#ifndef CONFIG_WIFI_MGR_RETRY_INTERVAL_MS
#define CONFIG_WIFI_MGR_RETRY_INTERVAL_MS 5000
#endif

#ifndef CONFIG_WIFI_MGR_AP_SSID
#define CONFIG_WIFI_MGR_AP_SSID "ESP32-Config"
#endif

#ifndef CONFIG_WIFI_MGR_AP_PASSWORD
#define CONFIG_WIFI_MGR_AP_PASSWORD ""
#endif

#ifndef CONFIG_WIFI_MGR_AP_IP
#define CONFIG_WIFI_MGR_AP_IP "192.168.4.1"
#endif

#ifndef CONFIG_WIFI_MGR_BLE_DEVICE_NAME
#define CONFIG_WIFI_MGR_BLE_DEVICE_NAME "ESP32-WiFi-{id}"
#endif


// =============================================================================
// Constants
// =============================================================================

#define WIFI_MGR_NVS_NAMESPACE      "wifi_mgr"
#define WIFI_MGR_MAX_NETWORKS       CONFIG_WIFI_MGR_MAX_NETWORKS
#define WIFI_MGR_MAX_VARS           CONFIG_WIFI_MGR_MAX_VARS
#define WIFI_MGR_TASK_STACK_SIZE    4096
#define WIFI_MGR_TASK_PRIORITY      5
#define WIFI_MGR_QUEUE_SIZE         10

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
    WM_INT_EVT_STOP,                // Stop task
} wifi_mgr_internal_evt_t;

typedef struct {
    wifi_mgr_internal_evt_t type;
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
} wifi_mgr_event_t;

// =============================================================================
// Internal Context
// =============================================================================

typedef struct {
    // State
    wifi_state_t state;
    bool initialized;
    bool ap_active;
    bool connecting;                // Currently in connect sequence
    int64_t connect_time;           // Connection start time
    
    // Config
    wifi_manager_config_t config;
    
    // Saved data
    wifi_network_t networks[WIFI_MGR_MAX_NETWORKS];
    size_t network_count;
    wifi_var_t vars[WIFI_MGR_MAX_VARS];
    size_t var_count;
    wifi_mgr_ap_config_t ap_config;
    
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
    
    // Scan results (temporary)
    wifi_scan_result_t *scan_results;
    size_t scan_count;
    
} wifi_mgr_ctx_t;

// Global context
extern wifi_mgr_ctx_t *g_wifi_mgr;

// =============================================================================
// NVS Functions (esp_wifi_manager_nvs.c)
// =============================================================================

esp_err_t wifi_mgr_nvs_init(void);
esp_err_t wifi_mgr_nvs_load_networks(wifi_network_t *networks, size_t max_count, size_t *count);
esp_err_t wifi_mgr_nvs_save_networks(const wifi_network_t *networks, size_t count);
esp_err_t wifi_mgr_nvs_load_vars(wifi_var_t *vars, size_t max_count, size_t *count);
esp_err_t wifi_mgr_nvs_save_vars(const wifi_var_t *vars, size_t count);
esp_err_t wifi_mgr_nvs_load_ap_config(wifi_mgr_ap_config_t *config);
esp_err_t wifi_mgr_nvs_save_ap_config(const wifi_mgr_ap_config_t *config);
esp_err_t wifi_mgr_nvs_load_auth(char *username, size_t ulen, char *password, size_t plen);
esp_err_t wifi_mgr_nvs_save_auth(const char *username, const char *password);
esp_err_t wifi_mgr_nvs_factory_reset(void);

// =============================================================================
// HTTP Functions (esp_wifi_manager_http.c)
// =============================================================================

esp_err_t wifi_mgr_http_init(void);
esp_err_t wifi_mgr_http_deinit(void);

// =============================================================================
// Internal Functions
// =============================================================================

// Send event to task queue (from event handlers or other contexts)
void wifi_mgr_send_event(wifi_mgr_internal_evt_t type);
void wifi_mgr_send_event_data(const wifi_mgr_event_t *event);

// Start connect sequence (non-blocking, called from task)
void wifi_mgr_start_connect_sequence(void);

// AP mode control (called from task)
void wifi_mgr_start_ap_mode(void);
void wifi_mgr_stop_ap_mode(void);

// =============================================================================
// esp_bus Handler (esp_wifi_manager_bus.c)
// =============================================================================

esp_err_t wifi_mgr_bus_handler(const char *action,
                               const void *req_data, size_t req_len,
                               void *res_buf, size_t res_buf_size, size_t *res_len,
                               void *ctx);

// =============================================================================
// DNS Server (esp_wifi_manager_dns.c) - Captive Portal
// =============================================================================

esp_err_t wifi_mgr_dns_start(void);
esp_err_t wifi_mgr_dns_stop(void);

// =============================================================================
// AP Functions (esp_wifi_manager_ap.c)
// =============================================================================

void wifi_mgr_expand_template(const char *tmpl, char *output, size_t max_len);

// =============================================================================
// mDNS Functions (esp_wifi_manager_mdns.c)
// =============================================================================

esp_err_t wifi_mgr_mdns_init(void);
esp_err_t wifi_mgr_mdns_deinit(void);
const char *wifi_mgr_mdns_get_hostname(void);

// =============================================================================
// CLI Functions (esp_wifi_manager_cli.c)
// =============================================================================

esp_err_t wifi_mgr_cli_init(void);

// =============================================================================
// Web UI Functions (esp_wifi_manager_webui.c)
// =============================================================================

esp_err_t wifi_mgr_webui_init(httpd_handle_t httpd);

// =============================================================================
// BLE Functions (esp_wifi_manager_ble.c)
// =============================================================================

esp_err_t wifi_mgr_ble_init(void);
esp_err_t wifi_mgr_ble_deinit(void);

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
static inline void wifi_mgr_lock(void) {
    if (g_wifi_mgr && g_wifi_mgr->mutex) {
        xSemaphoreTake(g_wifi_mgr->mutex, portMAX_DELAY);
    }
}

static inline void wifi_mgr_unlock(void) {
    if (g_wifi_mgr && g_wifi_mgr->mutex) {
        xSemaphoreGive(g_wifi_mgr->mutex);
    }
}

#ifdef __cplusplus
}
#endif
