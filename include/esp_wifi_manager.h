/**
 * @file esp_wifi_manager.h
 * @brief WiFi Manager - Multi-network support with auto retry and REST API
 * 
 * @section intro Giới thiệu
 * 
 * ESP WiFi Manager cung cấp:
 * - Multi-network: Lưu nhiều mạng WiFi với priority, tự động retry
 * - esp_bus integration: Actions và Events để tương tác
 * - HTTP REST API: Cấu hình từ xa qua web
 * - SoftAP: Captive portal khi không kết nối được
 * - NVS Storage: Lưu networks, variables, AP config
 * - Custom Variables: Key-value storage cho ứng dụng
 * 
 * @section usage Cách sử dụng
 * 
 * @subsection basic Basic Setup
 * @code{.c}
 * #include "esp_wifi_manager.h"
 * 
 * void app_main(void) {
 *     // Init với default networks
 *     wifi_manager_init(&(wifi_manager_config_t){
 *         .default_networks = (wifi_network_t[]){
 *             {"MyWiFi", "password123", 10},      // priority 10
 *             {"BackupWiFi", "backup456", 5},     // priority 5 (fallback)
 *         },
 *         .default_network_count = 2,
 *         .auto_reconnect = true,
 *         
 *         // Enable HTTP REST API
 *         .http = {
 *             .enable = true,
 *             .api_base_path = "/api/wifi",
 *         },
 *     });
 *     
 *     // Chờ kết nối (30 giây timeout)
 *     if (wifi_manager_wait_connected(30000) == ESP_OK) {
 *         ESP_LOGI(TAG, "WiFi connected!");
 *     }
 * }
 * @endcode
 * 
 * @subsection status Lấy trạng thái
 * @code{.c}
 * wifi_status_t status;
 * wifi_manager_get_status(&status);
 * 
 * printf("State: %s\n", status.state == WIFI_STATE_CONNECTED ? "connected" : "disconnected");
 * printf("SSID: %s\n", status.ssid);
 * printf("IP: %s\n", status.ip);
 * printf("RSSI: %d dBm (%d%%)\n", status.rssi, status.quality);
 * @endcode
 * 
 * @subsection espbus esp_bus Integration
 * @code{.c}
 * #include "esp_bus.h"
 * 
 * // Lấy status qua esp_bus
 * wifi_status_t status;
 * esp_bus_req(WIFI_REQ(WIFI_ACTION_GET_STATUS), NULL, 0, 
 *             &status, sizeof(status), NULL, 100);
 * 
 * // Subscribe events
 * void on_connected(const char *event, const void *data, size_t len, void *ctx) {
 *     wifi_connected_t *info = (wifi_connected_t *)data;
 *     ESP_LOGI(TAG, "Connected to %s", info->ssid);
 * }
 * esp_bus_subscribe(WIFI_EVT(WIFI_EVENT_CONNECTED), on_connected, NULL);
 * 
 * // Auto-route: WiFi connected -> LED on
 * esp_bus_connect(WIFI_EVT(WIFI_EVENT_CONNECTED), "led1.on", NULL, 0);
 * @endcode
 * 
 * @subsection softap SoftAP Mode
 * @code{.c}
 * // Start AP với config mặc định
 * wifi_manager_start_ap(NULL);
 * 
 * // Hoặc custom config
 * wifi_manager_start_ap(&(wifi_mgr_ap_config_t){
 *     .ssid = "MyDevice",
 *     .password = "12345678",
 *     .ip = "192.168.10.1",
 * });
 * 
 * // Lấy trạng thái AP
 * wifi_ap_status_t ap_status;
 * wifi_manager_get_ap_status(&ap_status);
 * printf("AP: %s, Clients: %d\n", ap_status.ssid, ap_status.sta_count);
 * @endcode
 * 
 * @subsection vars Custom Variables
 * @code{.c}
 * // Set variable
 * wifi_manager_set_var("server_url", "https://api.example.com");
 * wifi_manager_set_var("device_id", "device-001");
 * 
 * // Get variable
 * char value[128];
 * wifi_manager_get_var("server_url", value, sizeof(value));
 * 
 * // Subscribe variable changes
 * void on_var_changed(const char *event, const void *data, size_t len, void *ctx) {
 *     wifi_var_t *var = (wifi_var_t *)data;
 *     ESP_LOGI(TAG, "Var changed: %s = %s", var->key, var->value);
 * }
 * esp_bus_subscribe(WIFI_EVT(WIFI_EVENT_VAR_CHANGED), on_var_changed, NULL);
 * @endcode
 * 
 * @subsection http HTTP REST API
 * 
 * Khi `http.enable = true`, các endpoints sau khả dụng:
 * 
 * | Method | Endpoint | Mô tả |
 * |--------|----------|-------|
 * | GET | /api/wifi/status | Trạng thái WiFi đầy đủ |
 * | GET | /api/wifi/scan | Quét mạng xung quanh |
 * | GET | /api/wifi/networks | Danh sách mạng đã lưu |
 * | POST | /api/wifi/networks | Thêm mạng mới |
 * | DELETE | /api/wifi/networks/:ssid | Xóa mạng |
 * | POST | /api/wifi/connect | Kết nối (auto hoặc chỉ định SSID) |
 * | POST | /api/wifi/disconnect | Ngắt kết nối |
 * | GET | /api/wifi/ap/status | Trạng thái SoftAP |
 * | GET | /api/wifi/ap/config | Lấy config AP |
 * | PUT | /api/wifi/ap/config | Cập nhật config AP |
 * | POST | /api/wifi/ap/start | Bật SoftAP |
 * | POST | /api/wifi/ap/stop | Tắt SoftAP |
 * | GET | /api/wifi/vars | Danh sách variables |
 * | PUT | /api/wifi/vars/:key | Set variable |
 * | DELETE | /api/wifi/vars/:key | Xóa variable |
 * 
 * @subsection shared Shared HTTP Server
 * @code{.c}
 * // WiFi Manager tạo httpd
 * wifi_manager_init(&(wifi_manager_config_t){
 *     .http = { .enable = true },
 * });
 * 
 * // Components khác dùng chung
 * httpd_handle_t server = wifi_manager_get_httpd();
 * httpd_uri_t my_api = {
 *     .uri = "/api/mymodule/status",
 *     .method = HTTP_GET,
 *     .handler = my_handler,
 * };
 * httpd_register_uri_handler(server, &my_api);
 * @endcode
 * 
 * @section events Events (esp_bus)
 * 
 * | Event | Data | Mô tả |
 * |-------|------|-------|
 * | wifi:connected | wifi_connected_t | Đã kết nối mạng |
 * | wifi:disconnected | wifi_disconnected_t | Mất kết nối |
 * | wifi:connecting | string (ssid) | Đang thử kết nối |
 * | wifi:got_ip | esp_netif_ip_info_t | Đã nhận IP |
 * | wifi:lost_ip | none | Mất IP |
 * | wifi:scan_done | uint16 (count) | Quét xong |
 * | wifi:ap_start | none | AP đã bật |
 * | wifi:ap_stop | none | AP đã tắt |
 * | wifi:network_added | wifi_network_t | Mạng mới được thêm |
 * | wifi:network_removed | string (ssid) | Mạng bị xóa |
 * | wifi:var_changed | wifi_var_t | Variable thay đổi |
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi_types.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Module Constants (esp_bus)
// =============================================================================

#define WIFI_MODULE                 "wifi"  ///< esp_bus module name

// Actions - WiFi Station
#define WIFI_ACTION_CONNECT         "connect"       ///< Kết nối mạng
#define WIFI_ACTION_DISCONNECT      "disconnect"    ///< Ngắt kết nối
#define WIFI_ACTION_SCAN            "scan"          ///< Quét mạng
#define WIFI_ACTION_GET_STATUS      "get_status"    ///< Lấy trạng thái

// Actions - SoftAP
#define WIFI_ACTION_START_AP        "start_ap"      ///< Bật SoftAP
#define WIFI_ACTION_STOP_AP         "stop_ap"       ///< Tắt SoftAP
#define WIFI_ACTION_GET_AP_STATUS   "get_ap_status" ///< Trạng thái AP
#define WIFI_ACTION_SET_AP_CONFIG   "set_ap_config" ///< Cập nhật config AP
#define WIFI_ACTION_GET_AP_CONFIG   "get_ap_config" ///< Lấy config AP

// Actions - Network Config
#define WIFI_ACTION_ADD_NETWORK     "add_network"    ///< Thêm mạng
#define WIFI_ACTION_UPDATE_NETWORK  "update_network" ///< Cập nhật mạng
#define WIFI_ACTION_REMOVE_NETWORK  "remove_network" ///< Xóa mạng
#define WIFI_ACTION_GET_NETWORK     "get_network"    ///< Lấy thông tin 1 mạng
#define WIFI_ACTION_LIST_NETWORKS   "list_networks"  ///< Danh sách mạng

// Actions - Custom Variables
#define WIFI_ACTION_SET_VAR         "set_var"   ///< Set variable
#define WIFI_ACTION_GET_VAR         "get_var"   ///< Get variable
#define WIFI_ACTION_DEL_VAR         "del_var"   ///< Delete variable
#define WIFI_ACTION_LIST_VARS       "list_vars" ///< List all variables

// Actions - System
#define WIFI_ACTION_FACTORY_RESET   "factory_reset" ///< Factory reset (erase all NVS data)

// Events - Connection (prefix WIFI_MGR để tránh conflict với ESP-IDF)
#define WIFI_MGR_EVT_CONNECTED        "connected"     ///< Đã kết nối (data: wifi_connected_t)
#define WIFI_MGR_EVT_DISCONNECTED     "disconnected"  ///< Mất kết nối (data: wifi_disconnected_t)
#define WIFI_MGR_EVT_CONNECTING       "connecting"    ///< Đang kết nối (data: ssid string)
#define WIFI_MGR_EVT_SCAN_DONE        "scan_done"     ///< Quét xong (data: uint16 count)
#define WIFI_MGR_EVT_GOT_IP           "got_ip"        ///< Nhận IP (data: esp_netif_ip_info_t)
#define WIFI_MGR_EVT_LOST_IP          "lost_ip"       ///< Mất IP
#define WIFI_MGR_EVT_AP_START         "ap_start"      ///< AP bật
#define WIFI_MGR_EVT_AP_STOP          "ap_stop"       ///< AP tắt
#define WIFI_MGR_EVT_AP_STA_CONNECTED "ap_sta_connected" ///< Client kết nối AP

// Events - Config Changed
#define WIFI_MGR_EVT_NETWORK_ADDED    "network_added"   ///< Mạng được thêm
#define WIFI_MGR_EVT_NETWORK_UPDATED  "network_updated" ///< Mạng được cập nhật
#define WIFI_MGR_EVT_NETWORK_REMOVED  "network_removed" ///< Mạng bị xóa
#define WIFI_MGR_EVT_VAR_CHANGED      "var_changed"     ///< Variable thay đổi

/**
 * @brief Helper macro tạo request pattern
 * @param action Action name (e.g., WIFI_ACTION_CONNECT)
 * @return Pattern string "wifi.action"
 */
#define WIFI_REQ(action)   WIFI_MODULE "." action

/**
 * @brief Helper macro tạo event pattern
 * @param event Event name (e.g., WIFI_EVENT_CONNECTED)
 * @return Pattern string "wifi:event"
 */
#define WIFI_EVT(event)    WIFI_MODULE ":" event

// =============================================================================
// Data Types
// =============================================================================

/**
 * @brief WiFi connection state
 */
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,    ///< Không kết nối
    WIFI_STATE_CONNECTING,          ///< Đang kết nối
    WIFI_STATE_CONNECTED,           ///< Đã kết nối
} wifi_state_t;

/**
 * @brief Saved network configuration
 * 
 * Cấu hình 1 mạng WiFi. Priority cao hơn sẽ được thử kết nối trước.
 */
typedef struct {
    char ssid[32];          ///< SSID (max 31 chars)
    char password[64];      ///< Password (max 63 chars)
    uint8_t priority;       ///< 0-255, cao = ưu tiên hơn
} wifi_network_t;

/**
 * @brief WiFi station status
 * 
 * Trạng thái đầy đủ của WiFi station bao gồm IP, RSSI, channel, etc.
 */
typedef struct {
    wifi_state_t state;     ///< Trạng thái kết nối
    char ssid[32];          ///< SSID đang kết nối
    uint8_t bssid[6];       ///< BSSID của AP
    int8_t rssi;            ///< Cường độ tín hiệu (dBm), -100 to 0
    uint8_t quality;        ///< Chất lượng tín hiệu 0-100%
    uint8_t channel;        ///< WiFi channel
    
    // IP info
    char ip[16];            ///< IP address "192.168.1.100"
    char netmask[16];       ///< Subnet mask "255.255.255.0"
    char gateway[16];       ///< Gateway "192.168.1.1"
    char dns[16];           ///< DNS server
    char mac[18];           ///< MAC address "AA:BB:CC:DD:EE:FF"
    char hostname[32];      ///< Hostname
    
    // Stats
    uint32_t uptime_ms;     ///< Thời gian kết nối (ms)
    
    bool ap_active;         ///< SoftAP đang chạy?
} wifi_status_t;

/**
 * @brief WiFi scan result
 * 
 * Kết quả quét 1 mạng WiFi xung quanh.
 */
typedef struct {
    char ssid[32];          ///< SSID
    int8_t rssi;            ///< Cường độ tín hiệu (dBm)
    wifi_auth_mode_t auth;  ///< Auth mode: WIFI_AUTH_OPEN, WIFI_AUTH_WPA2_PSK, etc.
} wifi_scan_result_t;

/**
 * @brief SoftAP configuration
 * 
 * Cấu hình SoftAP mode bao gồm SSID, password, IP và DHCP range.
 * @note Đổi tên thành wifi_mgr_ap_config_t để tránh conflict với ESP-IDF
 */
typedef struct {
    char ssid[32];          ///< AP SSID
    char password[64];      ///< AP password (empty = open network)
    uint8_t channel;        ///< Channel 1-13, 0 = auto
    uint8_t max_connections;///< Max clients, default 4
    bool hidden;            ///< Hidden SSID
    
    // Static IP
    char ip[16];            ///< AP IP, default "192.168.4.1"
    char netmask[16];       ///< Netmask, default "255.255.255.0"
    char gateway[16];       ///< Gateway, default = ip
    
    // DHCP range
    char dhcp_start[16];    ///< DHCP range start, default "192.168.4.2"
    char dhcp_end[16];      ///< DHCP range end, default "192.168.4.20"
} wifi_mgr_ap_config_t;

/**
 * @brief SoftAP status
 * 
 * Trạng thái SoftAP bao gồm danh sách clients đang kết nối.
 */
typedef struct {
    bool active;            ///< AP đang chạy?
    char ssid[32];          ///< AP SSID
    char ip[16];            ///< AP IP
    uint8_t channel;        ///< Channel
    uint8_t sta_count;      ///< Số clients kết nối
    
    struct {
        char mac[18];       ///< Client MAC
        char ip[16];        ///< Client IP (nếu có)
    } clients[4];           ///< Danh sách clients (tối đa 4)
} wifi_ap_status_t;

/**
 * @brief Custom variable
 * 
 * Key-value storage cho ứng dụng. Được lưu vào NVS và có thể
 * thay đổi qua HTTP API.
 */
typedef struct {
    char key[32];           ///< Key (max 31 chars)
    char value[128];        ///< Value (max 127 chars)
} wifi_var_t;

/**
 * @brief Connected event data
 * 
 * Data được gửi kèm event WIFI_EVENT_CONNECTED.
 */
typedef struct {
    char ssid[32];          ///< SSID đã kết nối
    int8_t rssi;            ///< RSSI khi kết nối
    uint8_t channel;        ///< Channel
} wifi_connected_t;

/**
 * @brief Disconnected event data
 * 
 * Data được gửi kèm event WIFI_EVENT_DISCONNECTED.
 */
typedef struct {
    char ssid[32];          ///< SSID đã ngắt
    uint8_t reason;         ///< Reason code (wifi_err_reason_t)
} wifi_disconnected_t;

// =============================================================================
// Configuration
// =============================================================================

/**
 * @brief HTTP interface configuration
 *
 * Cấu hình HTTP REST API. Có thể dùng httpd có sẵn hoặc tạo mới.
 */
typedef struct {
    bool enable;                ///< Enable HTTP interface
    httpd_handle_t httpd;       ///< Existing httpd handle, NULL = create new
    const char *api_base_path;  ///< API base path, default "/api/wifi"
    bool enable_auth;           ///< Enable Basic Auth
    const char *auth_username;  ///< Auth username, default "admin"
    const char *auth_password;  ///< Auth password, default "admin"
} wifi_mgr_http_config_t;

/**
 * @brief mDNS configuration
 *
 * Cấu hình mDNS service discovery. Hostname hỗ trợ template {id}.
 */
typedef struct {
    bool enable;                ///< Enable mDNS
    const char *hostname;       ///< Hostname template, e.g., "esp32-{id}", default from Kconfig
    const char *instance_name;  ///< Instance name, default = hostname
} wifi_mgr_mdns_config_t;

/**
 * @brief BLE configuration
 *
 * Cấu hình BLE GATT interface. Device name hỗ trợ template {id}.
 */
typedef struct {
    bool enable;                ///< Enable BLE interface
    const char *device_name;    ///< BLE device name, e.g., "ESP32-WiFi-{id}", default from Kconfig
} wifi_mgr_ble_config_t;

/**
 * @brief Main WiFi Manager configuration
 * 
 * Cấu hình khởi tạo WiFi Manager. Tất cả fields đều optional.
 * 
 * @note default_networks và default_vars là fallback khi NVS trống.
 *       Sau khi user thêm network/var qua API, data sẽ được lưu NVS
 *       và ưu tiên hơn defaults.
 */
typedef struct {
    // Default networks (fallback if NVS empty)
    wifi_network_t *default_networks;   ///< Default networks array
    size_t default_network_count;       ///< Number of default networks
    
    // Default variables
    wifi_var_t *default_vars;           ///< Default variables array
    size_t default_var_count;           ///< Number of default variables
    
    // Retry config
    uint8_t max_retry_per_network;      ///< Max retry per network, default 3
    uint32_t retry_interval_ms;         ///< Initial retry interval (ms), default 5000
    uint32_t retry_max_interval_ms;     ///< Max retry interval for exponential backoff (ms), default 60000
    bool auto_reconnect;                ///< Auto reconnect on disconnect, default true
    
    // SoftAP default config
    wifi_mgr_ap_config_t default_ap;    ///< Default AP config
    bool enable_captive_portal;         ///< Start AP if all networks fail
    bool stop_ap_on_connect;            ///< Stop AP when STA connected successfully
    bool start_ap_on_init;              ///< Start AP immediately on init (AP+STA mode)
    
    // Interfaces
    wifi_mgr_http_config_t http;        ///< HTTP REST API config
    wifi_mgr_mdns_config_t mdns;        ///< mDNS config
    wifi_mgr_ble_config_t ble;          ///< BLE GATT config
} wifi_manager_config_t;

// =============================================================================
// Public API
// =============================================================================

/**
 * @brief Initialize WiFi Manager
 * 
 * Khởi tạo WiFi Manager với config. Sẽ tự động:
 * - Load networks/vars từ NVS (hoặc dùng defaults)
 * - Khởi tạo WiFi station
 * - Bắt đầu auto-connect
 * - Khởi tạo HTTP server (nếu enable)
 * 
 * @param config Configuration, NULL for all defaults
 * @return ESP_OK on success
 * 
 * @code{.c}
 * wifi_manager_init(&(wifi_manager_config_t){
 *     .default_networks = (wifi_network_t[]){
 *         {"MyWiFi", "password", 10},
 *     },
 *     .default_network_count = 1,
 *     .http = { .enable = true },
 * });
 * @endcode
 */
esp_err_t wifi_manager_init(const wifi_manager_config_t *config);

/**
 * @brief Deinitialize WiFi Manager
 * 
 * Stop the HTTP server, BLE, mDNS, and (optionally) WiFi interfaces. Free resources.
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_deinit(bool deinit_wifi);

/**
 * @brief Check if WiFi is connected
 * 
 * @return true if connected with IP, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get current WiFi state
 * 
 * @return Current state: WIFI_STATE_DISCONNECTED, WIFI_STATE_CONNECTING, WIFI_STATE_CONNECTED
 */
wifi_state_t wifi_manager_get_state(void);

/**
 * @brief Wait for WiFi connection
 * 
 * Block cho đến khi WiFi connected hoặc timeout.
 * 
 * @param timeout_ms Timeout in milliseconds, 0 = wait forever
 * @return ESP_OK if connected, ESP_ERR_TIMEOUT if timeout
 * 
 * @code{.c}
 * if (wifi_manager_wait_connected(30000) == ESP_OK) {
 *     ESP_LOGI(TAG, "Connected!");
 * } else {
 *     ESP_LOGW(TAG, "Connection timeout");
 * }
 * @endcode
 */
esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms);

/**
 * @brief Get full WiFi status
 * 
 * Lấy trạng thái đầy đủ bao gồm IP, RSSI, channel, hostname, etc.
 * 
 * @param[out] status Output status structure
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_status(wifi_status_t *status);

/**
 * @brief Get HTTP server handle
 * 
 * Lấy httpd handle để register thêm endpoints từ components khác.
 * 
 * @return httpd_handle_t or NULL if HTTP not enabled
 * 
 * @code{.c}
 * httpd_handle_t server = wifi_manager_get_httpd();
 * if (server) {
 *     httpd_uri_t my_uri = { .uri = "/my/api", .method = HTTP_GET, .handler = my_handler };
 *     httpd_register_uri_handler(server, &my_uri);
 * }
 * @endcode
 */
httpd_handle_t wifi_manager_get_httpd(void);

// =============================================================================
// Network Management API
// =============================================================================

/**
 * @brief Add a network
 * 
 * Thêm network mới vào danh sách. Emit event WIFI_EVENT_NETWORK_ADDED.
 * 
 * @param network Network config
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already exists, ESP_ERR_NO_MEM if full
 */
esp_err_t wifi_manager_add_network(const wifi_network_t *network);

/**
 * @brief Update a network
 * 
 * Cập nhật network theo SSID. Emit event WIFI_EVENT_NETWORK_UPDATED.
 * 
 * @param network Network config (SSID dùng để tìm)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not exists
 */
esp_err_t wifi_manager_update_network(const wifi_network_t *network);

/**
 * @brief Remove a network by SSID
 * 
 * Xóa network. Emit event WIFI_EVENT_NETWORK_REMOVED.
 * 
 * @param ssid SSID to remove
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not exists
 */
esp_err_t wifi_manager_remove_network(const char *ssid);

/**
 * @brief Get a network by SSID
 * 
 * @param ssid SSID to find
 * @param[out] network Output network config
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not exists
 */
esp_err_t wifi_manager_get_network(const char *ssid, wifi_network_t *network);

/**
 * @brief Get all saved networks
 * 
 * @param[out] networks Output array
 * @param max_count Array size
 * @param[out] count Output actual count
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_list_networks(wifi_network_t *networks, size_t max_count, size_t *count);

// =============================================================================
// Variable Management API
// =============================================================================

/**
 * @brief Set a variable
 * 
 * Set/update variable. Emit event WIFI_EVENT_VAR_CHANGED.
 * Biến được lưu vào NVS.
 * 
 * @param key Variable key (max 31 chars)
 * @param value Variable value (max 127 chars)
 * @return ESP_OK on success, ESP_ERR_NO_MEM if full
 */
esp_err_t wifi_manager_set_var(const char *key, const char *value);

/**
 * @brief Get a variable
 * 
 * @param key Variable key
 * @param[out] value Output buffer
 * @param max_len Buffer size
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not exists
 */
esp_err_t wifi_manager_get_var(const char *key, char *value, size_t max_len);

/**
 * @brief Delete a variable
 * 
 * Xóa variable. Emit event WIFI_EVENT_VAR_CHANGED với value rỗng.
 * 
 * @param key Variable key
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not exists
 */
esp_err_t wifi_manager_del_var(const char *key);

// =============================================================================
// SoftAP API
// =============================================================================

/**
 * @brief Start SoftAP
 * 
 * Bật SoftAP mode. Có thể chạy song song với station mode.
 * 
 * @param config Config override, NULL để dùng saved config
 * @return ESP_OK on success
 * 
 * @code{.c}
 * // Dùng config mặc định
 * wifi_manager_start_ap(NULL);
 * 
 * // Hoặc custom config
 * wifi_manager_start_ap(&(wifi_mgr_ap_config_t){
 *     .ssid = "MyAP",
 *     .password = "12345678",
 *     .ip = "10.0.0.1",
 * });
 * @endcode
 */
esp_err_t wifi_manager_start_ap(const wifi_mgr_ap_config_t *config);

/**
 * @brief Stop SoftAP
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_stop_ap(void);

/**
 * @brief Get SoftAP status
 * 
 * @param[out] status Output status bao gồm danh sách clients
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_ap_status(wifi_ap_status_t *status);

/**
 * @brief Set SoftAP config
 * 
 * Cập nhật config và lưu vào NVS. Apply ngay nếu AP đang chạy.
 * 
 * @param config New AP config
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_set_ap_config(const wifi_mgr_ap_config_t *config);

/**
 * @brief Get SoftAP config
 * 
 * @param[out] config Output config
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_ap_config(wifi_mgr_ap_config_t *config);

// =============================================================================
// Connection API
// =============================================================================

/**
 * @brief Connect to a specific network or auto-connect
 * 
 * @param ssid SSID to connect, NULL để auto-connect theo priority
 * @return ESP_OK on success (bắt đầu kết nối, chưa connected)
 * 
 * @code{.c}
 * // Auto-connect theo priority
 * wifi_manager_connect(NULL);
 * 
 * // Kết nối mạng cụ thể
 * wifi_manager_connect("MyWiFi");
 * 
 * // Chờ kết nối
 * wifi_manager_wait_connected(10000);
 * @endcode
 */
esp_err_t wifi_manager_connect(const char *ssid);

/**
 * @brief Disconnect from current network
 * 
 * Ngắt kết nối và tắt auto-reconnect.
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Scan for available networks
 *
 * Quét các mạng WiFi xung quanh. Blocking operation.
 *
 * @param[out] results Output array
 * @param max_count Array size
 * @param[out] count Output actual count
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if timeout
 */
esp_err_t wifi_manager_scan(wifi_scan_result_t *results, size_t max_count, size_t *count);

// =============================================================================
// System API
// =============================================================================

/**
 * @brief Factory reset
 *
 * Xóa toàn bộ dữ liệu NVS: networks, variables, AP config, auth credentials.
 * Sau khi gọi, cần restart hoặc gọi wifi_manager_deinit() rồi init lại.
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_factory_reset(void);

#ifdef __cplusplus
}
#endif
