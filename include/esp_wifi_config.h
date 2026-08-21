/**
 * @file esp_wifi_config.h
 * @brief WiFi Config - Multi-network support with auto retry and REST API
 * 
 * @section intro Giới thiệu
 * 
 * ESP WiFi Config cung cấp:
 * - Multi-network: Lưu nhiều mạng WiFi với priority, tự động retry
 * - Event callbacks: Subscribe để nhận thông báo trạng thái
 * - HTTP REST API: Cấu hình từ xa qua web
 * - SoftAP: Captive portal khi không kết nối được
 * - NVS Storage: Lưu networks, variables, AP config
 * - Custom Variables: Key-value storage cho ứng dụng
 * 
 * @section usage Cách sử dụng
 * 
 * @subsection basic Basic Setup
 * @code{.c}
 * #include "esp_wifi_config.h"
 *
 * void app_main(void) {
 *     // Init với default networks
 *     // Always start from WIFI_CFG_DEFAULTS: init does not patch fields you
 *     // leave at zero, so an uninitialised struct is rejected rather than
 *     // silently fixed up.
 *     wifi_cfg_init(&(wifi_cfg_config_t){
 *         WIFI_CFG_DEFAULTS,
 *         .default_networks = (wifi_network_t[]){
 *             {"MyWiFi", "password123", 10},      // priority 10
 *             {"BackupWiFi", "backup456", 5},     // priority 5 (fallback)
 *         },
 *         .default_network_count = 2,
 *
 *         // auto_reconnect and provisioning_mode = WIFI_PROV_ON_FAILURE are
 *         // already the defaults; only the rest needs stating.
 *         .stop_provisioning_on_connect = true,
 *         .provisioning_teardown_delay_ms = 5000,
 *         .enable_ap = true,
 *     });
 *
 *     if (wifi_cfg_wait_connected(30000) == ESP_OK) {
 *         ESP_LOGI(TAG, "WiFi connected!");
 *     }
 * }
 * @endcode
 * 
 * @subsection status Lấy trạng thái
 * @code{.c}
 * wifi_status_t status;
 * wifi_cfg_get_status(&status);
 * 
 * printf("State: %s\n", status.state == WIFI_STATE_CONNECTED ? "connected" : "disconnected");
 * printf("SSID: %s\n", status.ssid);
 * printf("IP: %s\n", status.ip);
 * printf("RSSI: %d dBm (%d%%)\n", status.rssi, status.quality);
 * @endcode
 * 
 * @subsection eventcb Event Callbacks
 * @code{.c}
 * // Handlers run synchronously on the library's task -- keep them short and
 * // do not call back into wifi_cfg from inside one.
 * void on_connected(wifi_cfg_event_t event, const void *data, size_t len, void *ctx) {
 *     const wifi_connected_t *info = (const wifi_connected_t *)data;
 *     ESP_LOGI(TAG, "Connected to %s", info->ssid);
 * }
 * wifi_cfg_event_subscribe(WIFI_CFG_EVENT_CONNECTED, on_connected, NULL, NULL);
 *
 * // One handler for everything, dispatching on the event id
 * void on_any(wifi_cfg_event_t event, const void *data, size_t len, void *ctx) {
 *     ESP_LOGI(TAG, "wifi event: %s", wifi_cfg_event_name(event));
 * }
 * wifi_cfg_event_subscribe(WIFI_CFG_EVENT_ANY, on_any, NULL, NULL);
 * @endcode
 *
 * Anything the bus actions used to reach is a plain function call: status is
 * wifi_cfg_get_status(), connecting is wifi_cfg_connect(), and so on.
 * 
 * @subsection softap SoftAP Mode
 * @code{.c}
 * // Start AP với config mặc định
 * wifi_cfg_start_ap(NULL);
 * 
 * // Hoặc custom config
 * wifi_cfg_start_ap(&(wifi_cfg_ap_config_t){
 *     .ssid = "MyDevice",
 *     .password = "12345678",
 *     .ip = "192.168.10.1",
 * });
 * 
 * // Lấy trạng thái AP
 * wifi_ap_status_t ap_status;
 * wifi_cfg_get_ap_status(&ap_status);
 * printf("AP: %s, Clients: %d\n", ap_status.ssid, ap_status.sta_count);
 * @endcode
 * 
 * @subsection vars Custom Variables
 * @code{.c}
 * // Set variable
 * wifi_cfg_set_var("server_url", "https://api.example.com");
 * wifi_cfg_set_var("device_id", "device-001");
 * 
 * // Get variable
 * char value[128];
 * wifi_cfg_get_var("server_url", value, sizeof(value));
 * 
 * // Subscribe variable changes
 * void on_var_changed(wifi_cfg_event_t event, const void *data, size_t len, void *ctx) {
 *     const wifi_var_t *var = (const wifi_var_t *)data;
 *     ESP_LOGI(TAG, "Var changed: %s = %s", var->key, var->value);
 * }
 * wifi_cfg_event_subscribe(WIFI_CFG_EVENT_VAR_CHANGED, on_var_changed, NULL, NULL);
 * @endcode
 * 
 * @subsection http HTTP REST API
 *
 * HTTP server starts automatically when `enable_ap` is true or
 * `http_post_prov_mode != WIFI_HTTP_DISABLED`. Các endpoints sau khả dụng:
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
 * // WiFi Config tạo httpd (auto when enable_ap=true)
 * wifi_cfg_init(&(wifi_cfg_config_t){
 *     .provisioning_mode = WIFI_PROV_ON_FAILURE,
 *     .enable_ap = true,
 * });
 * 
 * // Components khác dùng chung
 * httpd_handle_t server = wifi_cfg_get_httpd();
 * httpd_uri_t my_api = {
 *     .uri = "/api/mymodule/status",
 *     .method = HTTP_GET,
 *     .handler = my_handler,
 * };
 * httpd_register_uri_handler(server, &my_api);
 * @endcode
 * 
 * @section events Events
 *
 * Subscribe with wifi_cfg_event_subscribe(). The full list of event ids and
 * their payload types is documented on ::wifi_cfg_event_t.
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
// Events
// =============================================================================

/**
 * @brief Event identifiers delivered to subscribers
 *
 * Every event carries an optional payload whose type is fixed per event; the
 * @c data / @c len pair handed to a ::wifi_cfg_event_cb_t points at it. The
 * payload is owned by the library and is only valid for the duration of the
 * callback -- copy anything that must outlive the call.
 *
 * | Event                                | Payload                |
 * |--------------------------------------|------------------------|
 * | ::WIFI_CFG_EVENT_CONNECTED           | wifi_connected_t       |
 * | ::WIFI_CFG_EVENT_DISCONNECTED        | wifi_disconnected_t    |
 * | ::WIFI_CFG_EVENT_CONNECTING          | char[] (SSID, NUL-terminated) |
 * | ::WIFI_CFG_EVENT_SCAN_DONE           | uint16_t (AP count)    |
 * | ::WIFI_CFG_EVENT_GOT_IP              | esp_netif_ip_info_t    |
 * | ::WIFI_CFG_EVENT_LOST_IP             | none                   |
 * | ::WIFI_CFG_EVENT_AP_START            | none                   |
 * | ::WIFI_CFG_EVENT_AP_STOP             | none                   |
 * | ::WIFI_CFG_EVENT_AP_STA_CONNECTED    | uint8_t[6] (STA MAC)   |
 * | ::WIFI_CFG_EVENT_NETWORK_ADDED       | wifi_network_t         |
 * | ::WIFI_CFG_EVENT_NETWORK_UPDATED     | wifi_network_t         |
 * | ::WIFI_CFG_EVENT_NETWORK_REMOVED     | char[] (SSID)          |
 * | ::WIFI_CFG_EVENT_VAR_CHANGED         | wifi_var_t             |
 * | ::WIFI_CFG_EVENT_PROVISIONING_STARTED| none                   |
 * | ::WIFI_CFG_EVENT_PROVISIONING_STOPPED| none                   |
 * | ::WIFI_CFG_EVENT_PROV_CRED_RECV      | wifi_cfg_prov_creds_t  |
 * | ::WIFI_CFG_EVENT_PROV_CRED_FAIL      | int (reason)           |
 * | ::WIFI_CFG_EVENT_PROV_CRED_SUCCESS   | none                   |
 */
typedef enum {
    // Connection
    WIFI_CFG_EVENT_CONNECTED = 0,        ///< Associated with an AP
    WIFI_CFG_EVENT_DISCONNECTED,         ///< Lost association
    WIFI_CFG_EVENT_CONNECTING,           ///< Association attempt started
    WIFI_CFG_EVENT_SCAN_DONE,            ///< Scan finished
    WIFI_CFG_EVENT_GOT_IP,               ///< DHCP/static IP acquired
    WIFI_CFG_EVENT_LOST_IP,              ///< IP lease lost
    WIFI_CFG_EVENT_AP_START,             ///< SoftAP started
    WIFI_CFG_EVENT_AP_STOP,              ///< SoftAP stopped
    WIFI_CFG_EVENT_AP_STA_CONNECTED,     ///< A station joined the SoftAP

    // Configuration changes
    WIFI_CFG_EVENT_NETWORK_ADDED,        ///< Network added to the store
    WIFI_CFG_EVENT_NETWORK_UPDATED,      ///< Stored network updated
    WIFI_CFG_EVENT_NETWORK_REMOVED,      ///< Network removed from the store
    WIFI_CFG_EVENT_VAR_CHANGED,          ///< Custom variable set or deleted

    // Provisioning
    WIFI_CFG_EVENT_PROVISIONING_STARTED, ///< Provisioning interfaces up (AP/BLE)
    WIFI_CFG_EVENT_PROVISIONING_STOPPED, ///< Provisioning interfaces down
    WIFI_CFG_EVENT_PROV_CRED_RECV,       ///< Provisioning client sent credentials
    WIFI_CFG_EVENT_PROV_CRED_FAIL,       ///< Connect with provisioned credentials failed
    WIFI_CFG_EVENT_PROV_CRED_SUCCESS,    ///< Connect with provisioned credentials succeeded

    WIFI_CFG_EVENT_MAX,                  ///< Number of distinct events (not an event)
    WIFI_CFG_EVENT_ANY = 0xFF,           ///< Wildcard: subscribe to every event
} wifi_cfg_event_t;

/**
 * @brief Event subscriber callback
 *
 * Invoked **synchronously**, on whichever task emitted the event -- usually the
 * library's internal WiFi task, and for a few events the caller of the public
 * API that triggered them (e.g. wifi_cfg_add_network()). Two consequences:
 *
 *  - Keep the callback short. It runs inline in the library's state machine,
 *    so blocking here delays reconnects, scans and provisioning.
 *  - Do not call back into a wifi_cfg function that would re-enter the same
 *    state machine. Set a flag or post to your own queue instead.
 *
 * @param event Which event fired
 * @param data  Event payload, or NULL when the event carries none. Valid only
 *              for the duration of this call.
 * @param len   Payload length in bytes, 0 when @p data is NULL
 * @param ctx   The @c ctx passed to wifi_cfg_event_subscribe()
 */
typedef void (*wifi_cfg_event_cb_t)(wifi_cfg_event_t event, const void *data,
                                    size_t len, void *ctx);

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
    char ssid[33];          ///< SSID (max 31 chars)
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
    char ssid[33];          ///< SSID đang kết nối
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
    char ssid[33];          ///< SSID
    int8_t rssi;            ///< Cường độ tín hiệu (dBm)
    wifi_auth_mode_t auth;  ///< Auth mode: WIFI_AUTH_OPEN, WIFI_AUTH_WPA2_PSK, etc.
} wifi_scan_result_t;

/**
 * @brief SoftAP configuration
 * 
 * Cấu hình SoftAP mode bao gồm SSID, password, IP và DHCP range.
 * @note Đổi tên thành wifi_cfg_ap_config_t để tránh conflict với ESP-IDF
 */
typedef struct {
    char ssid[33];          ///< AP SSID
    char password[64];      ///< AP password (empty = open network)
    uint8_t channel;        ///< Channel 1-13, 0 = auto
    uint8_t max_connections;///< Max clients, default 4
    bool hidden;            ///< Hidden SSID
    
    // Static IP
    char ip[16];            ///< AP IP, default "192.168.4.1"
    char netmask[16];       ///< Netmask, default "255.255.255.0"
    char gateway[16];       ///< Gateway, default = ip
    
    // DHCP range
    /// @note Stored, reported via GET /api/wifi/ap/config, and defaulted —
    ///       but NOT currently programmed into the AP netif's DHCP server.
    ///       The pool in use is whatever esp_netif derives from `ip`.
    char dhcp_start[16];    ///< DHCP range start, default "192.168.4.2"
    char dhcp_end[16];      ///< DHCP range end, default "192.168.4.20"
} wifi_cfg_ap_config_t;

/**
 * @brief SoftAP status
 * 
 * Trạng thái SoftAP bao gồm danh sách clients đang kết nối.
 */
typedef struct {
    bool active;            ///< AP đang chạy?
    char ssid[33];          ///< AP SSID
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
    char ssid[33];          ///< SSID đã kết nối
    int8_t rssi;            ///< RSSI khi kết nối
    uint8_t channel;        ///< Channel
} wifi_connected_t;

/**
 * @brief Disconnected event data
 * 
 * Data được gửi kèm event WIFI_EVENT_DISCONNECTED.
 */
typedef struct {
    char ssid[33];          ///< SSID đã ngắt
    uint8_t reason;         ///< Reason code (wifi_err_reason_t)
} wifi_disconnected_t;

// =============================================================================
// Provisioning Enums
// =============================================================================

/**
 * @brief Provisioning mode — controls when provisioning interfaces (AP/BLE) start
 *
 * This replaces the old `start_ap_on_init`, `enable_captive_portal` booleans
 * with a single enum governing startup behavior for all provisioning interfaces.
 */
typedef enum {
    /// Start when unprovisioned OR all networks fail to connect. **The
    /// default**, and deliberately the zero value: a zero-initialised config
    /// must select a mode that works.
    WIFI_PROV_ON_FAILURE = 0,
    WIFI_PROV_WHEN_UNPROVISIONED, ///< Start only if no saved networks exist
    WIFI_PROV_MANUAL,             ///< Only via explicit API call (e.g., button press)
    /// [DISABLED] Start provisioning at init regardless of state. Currently
    /// treated as WIFI_PROV_MANUAL — see the note in esp_wifi_config.c.
    ///
    /// Moved off zero in 0.2.0. It used to be the value a zero-initialised
    /// config selected, so omitting `provisioning_mode` silently chose the one
    /// mode that does nothing.
    WIFI_PROV_ALWAYS,
} wifi_provisioning_mode_t;

/**
 * @brief Action to take when max_reconnect_attempts is exhausted after a post-connect disconnect
 */
typedef enum {
    /// Restart the device (`esp_restart`). **The default**, and deliberately
    /// the zero value — it is also the only one of the two that works.
    /// Only ever reached when `max_reconnect_attempts` is non-zero.
    WIFI_ON_RECONNECT_EXHAUSTED_RESTART = 0,
    /// [DISABLED] Start provisioning + keep retrying. Currently treated as
    /// "continue retrying indefinitely" — see the note in esp_wifi_config.c.
    ///
    /// Moved off zero in 0.2.0, for the same reason as WIFI_PROV_ALWAYS.
    WIFI_ON_RECONNECT_EXHAUSTED_PROVISION,
} wifi_reconnect_exhausted_action_t;

/**
 * @brief HTTP behavior after provisioning stops
 *
 * Controls what happens to HTTP endpoints when provisioning interfaces are torn down.
 * During active provisioning, all endpoints are always registered regardless of this setting.
 */
typedef enum {
    WIFI_HTTP_FULL,       ///< Keep UI + API endpoints active after provisioning stops
    WIFI_HTTP_API_ONLY,   ///< Deregister UI/captive portal endpoints, keep API
    WIFI_HTTP_DISABLED,   ///< Deregister all library-registered endpoints
} wifi_http_post_prov_mode_t;

// =============================================================================
// Configuration
// =============================================================================

/**
 * @brief Pre-request hook callback
 *
 * Called before every API handler (after CORS, before auth check).
 * Return ESP_OK to continue to the handler, ESP_FAIL to reject (sends 403).
 * Only applies to /api/wifi/ endpoints, not static file serving.
 */
typedef esp_err_t (*wifi_cfg_http_hook_t)(httpd_req_t *req, void *ctx);

/**
 * @brief HTTP interface configuration
 *
 * Cấu hình HTTP REST API. Có thể dùng httpd có sẵn hoặc tạo mới.
 */
typedef struct {
    httpd_handle_t httpd;       ///< Existing httpd handle, NULL = create new
    const char *api_base_path;  ///< API base path, default "/api/wifi"
    bool enable_auth;           ///< Enable Basic Auth
    const char *auth_username;  ///< Auth username, default "admin"
    const char *auth_password;  ///< Auth password, default "admin"
    wifi_cfg_http_hook_t pre_request_hook;  ///< Optional pre-request hook for API endpoints
    void *hook_ctx;             ///< Context passed to pre_request_hook
} wifi_cfg_http_config_t;

/**
 * @brief Protocomm security version
 *
 * Selects which security protocol the provisioning manager negotiates with
 * the client. Security 0 is plaintext (testing only). Security 1 uses an
 * X25519 key exchange with an optional proof-of-possession string and
 * AES-CTR. Security 2 uses SRP6a with a salted authenticated key exchange
 * and AES-GCM — the production-recommended option.
 *
 * Use WIFI_CFG_PROV_SECURITY_DEFAULT to fall back to the library default
 * (currently Security 1 — works with or without a PoP).
 */
typedef enum {
    WIFI_CFG_PROV_SECURITY_DEFAULT = 0, ///< Library default (Security 1)
    WIFI_CFG_PROV_SECURITY_0,           ///< Plaintext (testing only)
    WIFI_CFG_PROV_SECURITY_1,           ///< X25519 + PoP + AES-CTR
    WIFI_CFG_PROV_SECURITY_2,           ///< SRP6a + AES-GCM (recommended)
} wifi_cfg_prov_security_t;

/**
 * @brief Bluetooth memory cleanup policy on provisioning deinit
 *
 * Controls how much of the Bluetooth controller/host the wifi_prov_mgr
 * releases when it deinitialises. Pick the policy that matches what the
 * rest of the application needs from Bluetooth after provisioning ends.
 *
 *   - FREE_BTDM — release everything (Classic BT + BLE memory). Use this
 *     when the device does not use Bluetooth post-provisioning. This is
 *     the default and reclaims the most RAM.
 *   - FREE_BLE  — release BLE memory only; keep Classic BT controller
 *     memory. Use when the application still needs Classic BT (A2DP, SPP,
 *     HFP, etc.) after provisioning. Available only on chips that support
 *     Classic BT (ESP32; not C-series or H-series).
 *   - FREE_BT   — release Classic BT memory only; keep BLE alive. Use
 *     when the application still needs BLE (custom GATT service, beacon,
 *     scanner) after provisioning.
 *   - KEEP_ALL  — release nothing. Use when the application brought up
 *     the BLE stack itself before calling wifi_cfg_init() and owns the
 *     full lifecycle. The library also auto-detects this case (BT
 *     controller already enabled) and overrides to KEEP_ALL with a log
 *     warning to prevent freeing memory the app still uses.
 *
 * Setting the wrong policy can crash the app — picking FREE_BTDM and then
 * calling a Classic BT function afterwards will fault inside the BT
 * controller.
 */
typedef enum {
    WIFI_CFG_PROV_MEM_FREE_BTDM = 0,  ///< Release Classic BT + BLE memory (default)
    WIFI_CFG_PROV_MEM_FREE_BLE,       ///< Release BLE memory; keep Classic BT
    WIFI_CFG_PROV_MEM_FREE_BT,        ///< Release Classic BT memory; keep BLE
    WIFI_CFG_PROV_MEM_KEEP_ALL,       ///< Release nothing (app manages lifecycle)
} wifi_cfg_prov_memory_policy_t;

/**
 * @brief WiFi credentials received via provisioning
 *
 * Passed to ::WIFI_CFG_EVENT_PROV_CRED_RECV subscribers and to the
 * on_credentials_received callback. SSID is NUL-terminated.
 */
typedef struct {
    char ssid[33];
    char password[64];
} wifi_cfg_prov_creds_t;

/**
 * @brief Custom protocomm endpoint handler signature
 *
 * Mirrors protocomm_req_handler_t. The handler must allocate `*outbuf` with
 * malloc; protocomm frees it via free().
 */
typedef esp_err_t (*wifi_cfg_prov_endpoint_handler_t)(
    uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
    uint8_t **outbuf, ssize_t *outlen, void *user_ctx);

/**
 * @brief Custom protocomm endpoint definition
 *
 * Registered alongside the library's built-in endpoints during
 * wifi_cfg_prov_start(). The endpoint name is published as part of the
 * provisioning protocol; clients address it by name.
 */
typedef struct {
    const char *name;                            ///< Endpoint name (e.g. "my-cloud-token")
    wifi_cfg_prov_endpoint_handler_t handler;    ///< Protocomm handler
    void *user_ctx;                              ///< Passed to handler as the last arg
} wifi_cfg_prov_custom_endpoint_t;

/**
 * @brief Application metadata surfaced via the proto-ver endpoint
 *
 * The provisioning manager publishes this on the unencrypted "proto-ver"
 * endpoint so clients can branch on product/version/capabilities before
 * any security handshake. Each entry is one JSON object in the response.
 *
 * Do not use the label "prov" — the manager reserves it for its own
 * version/capability metadata.
 */
typedef struct {
    const char *label;                  ///< JSON key (e.g. "my_app")
    const char *version;                ///< Version string
    const char *const *capabilities;    ///< Array of capability strings
    size_t capability_count;
} wifi_cfg_prov_app_info_t;

/**
 * @brief Provisioning event callbacks
 *
 * Optional struct callbacks invoked alongside the corresponding library
 * events (::WIFI_CFG_EVENT_PROV_CRED_RECV and friends). Use whichever path
 * fits the app — these fields when only the provisioning flow matters,
 * wifi_cfg_event_subscribe() when one handler should see everything. Both
 * fire for every event.
 */
typedef void (*wifi_cfg_prov_on_creds_recv_t)(const wifi_cfg_prov_creds_t *creds, void *ctx);
typedef void (*wifi_cfg_prov_on_creds_fail_t)(int reason, void *ctx);
typedef void (*wifi_cfg_prov_on_creds_success_t)(void *ctx);

/**
 * @brief ESP-IDF Network Provisioning configuration
 *
 * Runtime configuration for CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING.
 * Zero/NULL fields fall back to library defaults documented per-field.
 *
 * The library wraps the ESP-IDF wifi_provisioning manager (BLE scheme).
 * Custom protocomm endpoints are registered automatically:
 *
 *   - "esp-wifi-config-version"      — library + IDF + firmware version JSON
 *   - "esp-wifi-config-capabilities" — feature flags (improv-serial, ap, …)
 *   - "esp-wifi-config-vars"         — read/write the custom variable store
 *   - "esp-wifi-config-network-policy" — read provisioning_mode/retry policy
 *
 * Additional endpoints can be supplied via `custom_endpoints`.
 *
 * Provisioning starts/stops via the existing provisioning_mode lifecycle —
 * the same modes (ALWAYS / ON_FAILURE / WHEN_UNPROVISIONED / MANUAL) drive
 * the wifi_prov_mgr session.
 */
typedef struct {
    // ── BLE identity / discovery ─────────────────────────────────────────
    /// BLE GAP device name. Supports the `{id}` token (expanded against the
    /// WiFi STA MAC, last 3 bytes as hex). NULL → library default
    /// "PROV_{id}". Example: "MyDevice-{id}" → "MyDevice-ABC123".
    const char *device_name;
    /// Optional 16-byte (128-bit) BLE service UUID to advertise. NULL →
    /// IDF default (`0000ffff-0000-1000-8000-00805f9b34fb`). Espressif
    /// recommends setting a product-specific UUID for production.
    const uint8_t *service_uuid128;
    /// Optional manufacturer-specific data appended to the BLE scan
    /// response. Must fit alongside the device name: typically
    /// `len + 2 < 31 - (name_len + 2)`. Oversized data is truncated by
    /// the BLE stack.
    const uint8_t *manufacturer_data;
    size_t         manufacturer_data_len;
    /// Optional 6-byte static random BLE address. NULL → use the
    /// controller's default (public) address. Useful when a fresh BLE
    /// identity is desired so phones treat the device as new.
    const uint8_t *random_addr;

    // ── Security ─────────────────────────────────────────────────────────
    /// Security version to negotiate. WIFI_CFG_PROV_SECURITY_DEFAULT → use
    /// the library default (currently Security 1).
    wifi_cfg_prov_security_t security;
    /// Proof-of-possession for Security 1. NULL or empty → no-PoP mode.
    /// Ignored for Security 0 and Security 2.
    const char *pop;
    /// SRP6a username (I) for Security 2. The provisioning client must use
    /// the same value (the salt/verifier below were derived from this
    /// username + password offline).
    /// @note Metadata only — the username never flows into wifi_prov_mgr
    ///       from the device side, so the library substitutes no default
    ///       here. NULL simply means "the application does not surface an
    ///       expected username".
    const char *security2_username;
    /// Pre-computed SRP6a salt for Security 2. Required when Security 2
    /// is selected — wifi_cfg_init() returns ESP_ERR_INVALID_ARG if
    /// missing (the device stores a salt + verifier, NOT the raw PoP).
    /// Derive the salt+verifier offline from a username + password and
    /// embed the bytes in firmware. Two ways:
    ///   - IDF helper: wifi_prov_sec2_get_salt_and_verifier()
    ///   - esp_prov tool (emits ready-to-paste C arrays):
    ///       esp_prov.py --transport ble --sec_ver 2 --sec2_gen_cred
    ///                   --sec2_username USER --sec2_pwd PASSWORD
    /// The provisioning client must authenticate with the SAME username
    /// (security2_username) + password the salt/verifier were derived from.
    const uint8_t *security2_salt;
    size_t         security2_salt_len;
    /// Pre-computed SRP6a verifier for Security 2. See security2_salt.
    const uint8_t *security2_verifier;
    size_t         security2_verifier_len;

    // ── BLE lifecycle ────────────────────────────────────────────────────
    /// Bluetooth memory release policy when the provisioning manager
    /// deinitialises. See wifi_cfg_prov_memory_policy_t for the trade-offs.
    /// 0 → FREE_BTDM (default).
    wifi_cfg_prov_memory_policy_t memory_policy;
    /// Keep BLE advertising alive after the manager stops. Translates to
    /// wifi_prov_mgr_keep_ble_on(true). Must be set before start. Useful
    /// when the application takes over BLE for its own services after
    /// provisioning ends.
    bool keep_ble_on_after_stop;
    /// When true, disable the workaround that restarts the provisioning
    /// manager on every BLE client disconnect that happens mid-flow.
    /// The workaround papers over an IDF 5.5.3 NimBLE bug where only the
    /// first BLE client to connect after boot can complete a provisioning
    /// session; subsequent reconnects accept at LL but then time out at
    /// supervision. Default off (workaround active). Set true only to
    /// debug the underlying IDF bug or for apps that need to handle the
    /// stop/restart sequence themselves.
    bool disable_disconnect_restart;

    // ── Provisioning lifecycle ───────────────────────────────────────────
    /// Grace period the manager observes between a stop request and
    /// protocomm shutdown, in ms. Lets the client read final status before
    /// the transport disappears. 0 → 1000 ms (library default). Values
    /// below 100 ms are clamped to 100 ms by ESP-IDF.
    uint32_t cleanup_delay_ms;
    /// WiFi connection attempts after credentials are applied. 0 →
    /// infinite (ESP-IDF legacy default). A bounded value (e.g. 5) lets
    /// the manager surface CRED_FAIL after a wrong-password loop instead
    /// of retrying forever.
    uint32_t wifi_conn_attempts;
    /// Stop wifi_prov_mgr immediately on CRED_SUCCESS even when
    /// stop_provisioning_on_connect is false. Useful in MANUAL mode where
    /// the library doesn't drive provisioning teardown automatically.
    /// Ignored when reboot-on-success is active (the device reboots
    /// before any in-place stop would matter).
    bool stop_after_success;
    /// When true, suppress the automatic device reboot that normally
    /// fires once the BLE client has delivered credentials. Reboot is
    /// the cleanest way to recover from the post-provisioning BLE
    /// handoff: there is no clean way to tear down and rebuild
    /// Espressif's wifi_provisioning BLE stack in place, so the
    /// library defaults to **off** here (reboot enabled). Set true
    /// only if the application handles the BLE/Wi-Fi handoff itself.
    ///
    /// Reboot fires on whichever happens first: the BLE client
    /// disconnecting after CRED_RECV, or reboot_max_wait_ms expiring
    /// after CRED_SUCCESS.
    bool disable_reboot_on_provisioning_success;
    /// Maximum wait between CRED_SUCCESS and the backstop reboot, in
    /// ms. Gives the BLE client time to poll the status endpoint,
    /// see "connected", and disconnect cleanly so the reboot fires
    /// from the disconnect handler rather than the timer. 0 → 15000 ms.
    ///
    /// IMPORTANT: this must comfortably exceed the client's status-poll
    /// interval (Espressif's ESPProvision SDK polls every ~5 s) plus the
    /// time for the device to associate + get an IP. If the backstop is
    /// too short the device can reboot in the gap between two client
    /// polls — after it is actually connected but before the client has
    /// observed "connected" — so the client keeps polling a rebooted
    /// link and reports a FALSE FAILURE even though provisioning
    /// succeeded. A 3 s backstop reproduced this intermittently (it bit
    /// Security 2, whose slower SRP6a handshake delays the connect past
    /// the first poll). 15 s leaves room for ~3 poll cycles.
    /// Ignored when disable_reboot_on_provisioning_success is true.
    uint32_t reboot_max_wait_ms;
    /// If true, reset the provisioning state machine after
    /// max_failed_attempts consecutive credential failures so a fresh
    /// attempt can be accepted without rebooting. Recommended for the
    /// "re-provisioning just works after a wrong password" UX.
    bool reset_on_failure;
    /// Failed-attempt threshold used when reset_on_failure is true.
    /// 0 → library default (3).
    uint8_t max_failed_attempts;

    // ── App metadata (proto-ver endpoint) ───────────────────────────────
    /// Optional firmware version string surfaced via the built-in
    /// "esp-wifi-config-version" endpoint (separate from app_infos which
    /// targets the standard proto-ver endpoint).
    const char *firmware_version;
    /// Optional application metadata entries written into the manager's
    /// proto-ver JSON. The label "prov" is reserved by ESP-IDF.
    const wifi_cfg_prov_app_info_t *app_infos;
    size_t app_info_count;

    // ── Custom protocomm endpoints ──────────────────────────────────────
    /// Optional list of custom endpoints to register in addition to the
    /// library's four built-in endpoints. The library calls endpoint_create
    /// before start and endpoint_register after start per ESP-IDF guidance.
    const wifi_cfg_prov_custom_endpoint_t *custom_endpoints;
    size_t custom_endpoint_count;

    // ── Event callbacks ─────────────────────────────────────────────────
    /// Fired when the provisioning client delivers WiFi credentials. Runs
    /// before the library persists the credentials to NVS.
    wifi_cfg_prov_on_creds_recv_t    on_credentials_received;
    /// Fired when the STA fails to connect with the supplied credentials.
    /// `reason` is the wifi_prov_sta_fail_reason_t value cast to int.
    wifi_cfg_prov_on_creds_fail_t    on_credentials_failed;
    /// Fired when the STA connects successfully with the supplied
    /// credentials and the manager accepts them.
    wifi_cfg_prov_on_creds_success_t on_credentials_success;
    /// User pointer passed to every callback above.
    void *event_ctx;
} wifi_cfg_prov_config_t;

/**
 * @brief Improv WiFi identify callback
 *
 * Called when an Improv client sends the Identify RPC command.
 * Typically used to flash an LED or beep a buzzer to identify the device.
 */
typedef void (*wifi_cfg_improv_identify_cb_t)(void);

/**
 * @brief Improv WiFi configuration
 *
 * Enables the Improv WiFi standard for provisioning via BLE and/or Serial.
 * Improv BLE is mutually exclusive with ESP-IDF Network Provisioning over
 * BLE — both want to own the BLE GAP advertising and the host stack.
 * Improv Serial is independent of BLE and remains safe alongside Network
 * Provisioning.
 * Reference: https://www.improv-wifi.com/
 */
typedef struct {
    /// UART port number. 0 → Kconfig CONFIG_WIFI_MGR_IMPROV_SERIAL_UART_NUM
    /// (itself defaulting to 0 = UART_NUM_0). Because the library treats 0
    /// as "unset", UART_NUM_0 cannot be selected here in preference to a
    /// non-zero Kconfig value — change the Kconfig option instead.
    int serial_uart_num;
    int serial_baud_rate;                     ///< Baud rate. 0 → Kconfig CONFIG_WIFI_MGR_IMPROV_SERIAL_BAUD (default 115200)
    const char *firmware_name;                ///< Reported in Device Info RPC
    const char *firmware_version;             ///< Reported in Device Info RPC
    const char *device_name;                  ///< Reported by the Improv Device-Info RPC (shown in the companion app after connect)
    /// BLE GAP advertised name template — what BLE scanners (LightBlue,
    /// nRF Connect, the OS Bluetooth picker) show. Supports the `{id}`
    /// token (expanded against the WiFi STA MAC). Default: Kconfig
    /// `WIFI_CFG_DEFAULT_BLE_DEVICE_NAME` ("ESP32-WiFi-{id}").
    ///
    /// Distinct from `device_name` above: that field surfaces only after
    /// an Improv client has already connected; this one is what gets the
    /// user to pick the right device from the BLE scan list.
    const char *ble_device_name;
    wifi_cfg_improv_identify_cb_t on_identify; ///< Optional identify callback
} wifi_cfg_improv_config_t;

/**
 * @brief Variable validation callback
 *
 * Called before writing a variable to NVS on PUT /api/wifi/vars/:key.
 * Return ESP_OK to accept, ESP_FAIL to reject (API returns 400 with "var_invalid").
 */
typedef esp_err_t (*wifi_cfg_var_validator_t)(const char *key, const char *value, void *ctx);

/**
 * @brief Main WiFi Config configuration
 * 
 * Cấu hình khởi tạo WiFi Config. Tất cả fields đều optional.
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

    // Retry / reconnect
    uint8_t max_retry_per_network;      ///< Max retry per network. 0 → default 3 (CONFIG_WIFI_CFG_DEFAULT_RETRY)
    uint32_t retry_interval_ms;         ///< Initial retry interval (ms). 0 → default 5000 (CONFIG_WIFI_CFG_RETRY_INTERVAL_MS)
    uint32_t retry_max_interval_ms;     ///< Max retry interval for exponential backoff (ms). 0 → default 60000
    /// Auto reconnect after a post-connect disconnect. **Default true**, set
    /// by WIFI_CFG_DEFAULT_CONFIG().
    ///
    /// Start from the macro and this behaves like any other field:
    /// `.auto_reconnect = false` disables it. wifi_cfg_init() no longer
    /// rewrites it, so a struct you zero-initialise yourself gets `false` --
    /// which is why the macro is not optional.
    ///
    /// This is configuration, not state. An explicit wifi_cfg_disconnect()
    /// suppresses reconnection until the next wifi_cfg_connect(), and does it
    /// without touching this field.
    bool auto_reconnect;
    uint16_t max_reconnect_attempts;    ///< Max reconnect attempts after post-connect disconnect. Default 0 = infinite; 0 is a real value and is never re-defaulted.
    /// Action when `max_reconnect_attempts` is reached. Defaults to
    /// WIFI_ON_RECONNECT_EXHAUSTED_RESTART, which is both the zero value and
    /// the only member that works — the other is [DISABLED]. Only ever
    /// consulted when `max_reconnect_attempts` is non-zero.
    wifi_reconnect_exhausted_action_t on_reconnect_exhausted;

    // Provisioning lifecycle
    /// Controls when provisioning interfaces (AP/BLE) start. Defaults to
    /// WIFI_PROV_ON_FAILURE, which is the zero value: start when unprovisioned
    /// or when every saved network fails.
    wifi_provisioning_mode_t provisioning_mode;
    bool stop_provisioning_on_connect;              ///< Stop AP/BLE when STA gets IP. Default false.
    uint32_t provisioning_teardown_delay_ms;        ///< Delay before teardown (lets UI show result), ms. Default 0 = tear down immediately.

    // HTTP post-provisioning behavior
    wifi_http_post_prov_mode_t http_post_prov_mode; ///< What to do with HTTP after provisioning stops. Default (zero) = WIFI_HTTP_FULL.

    // SoftAP config
    wifi_cfg_ap_config_t default_ap;    ///< Default AP config. Fields left blank/zero take the per-field defaults documented on wifi_cfg_ap_config_t.
    bool always_use_ap_defaults;        ///< Always use default_ap, ignore NVS-saved AP config. Default false.
    bool enable_ap;                     ///< Enable Soft AP as a provisioning method. Default false.

    // Callbacks
    wifi_cfg_var_validator_t on_before_var_set;  ///< Optional variable validation callback
    void *var_validator_ctx;                      ///< Context passed to on_before_var_set

    // Interfaces
    wifi_cfg_http_config_t http;        ///< HTTP REST API config
    wifi_cfg_improv_config_t improv;    ///< Improv WiFi config
    wifi_cfg_prov_config_t prov_ble;    ///< ESP-IDF Network Provisioning (BLE GATT) config
} wifi_cfg_config_t;

// =============================================================================
// Defaults the public initialiser macro needs
// =============================================================================
//
// These were private. WIFI_CFG_DEFAULTS is public and expands in the caller's
// translation unit, so everything it names has to be reachable from here.
// Naming them is the honest form anyway: the field docs already quote these
// values, and a caller comparing against one should not have to retype it.
//
// The `#ifndef` guards let the component build outside an ESP-IDF Kconfig run
// (PlatformIO via library.json, or a host-side syntax check). Values match
// Kconfig.projbuild; change them there, not here.

#ifndef CONFIG_WIFI_CFG_DEFAULT_RETRY
#define CONFIG_WIFI_CFG_DEFAULT_RETRY 3
#endif

#ifndef CONFIG_WIFI_CFG_RETRY_INTERVAL_MS
#define CONFIG_WIFI_CFG_RETRY_INTERVAL_MS 5000
#endif

#ifndef CONFIG_WIFI_MGR_IMPROV_SERIAL_UART_NUM
#define CONFIG_WIFI_MGR_IMPROV_SERIAL_UART_NUM 0
#endif

#ifndef CONFIG_WIFI_MGR_IMPROV_SERIAL_BAUD
#define CONFIG_WIFI_MGR_IMPROV_SERIAL_BAUD 115200
#endif

#define WIFI_CFG_DEFAULT_AP_SSID     "ESP32-Config"
#define WIFI_CFG_DEFAULT_AP_PASSWORD ""       ///< Empty: an open network
#define WIFI_CFG_DEFAULT_AP_IP       "192.168.4.1"

/**
 * @brief Every documented default, as an initialiser. **Start here.**
 *
 * @code
 * wifi_cfg_config_t cfg = WIFI_CFG_DEFAULT_CONFIG();
 * cfg.enable_ap = true;
 * cfg.auto_reconnect = false;      // means false, because you started here
 * wifi_cfg_init(&cfg);
 * @endcode
 *
 * or, in the compound-literal style ESP-IDF components usually use:
 *
 * @code
 * wifi_cfg_init(&(wifi_cfg_config_t){
 *     WIFI_CFG_DEFAULTS,
 *     .enable_ap = true,
 * });
 * @endcode
 *
 * **Why this exists.** `wifi_cfg_init()` used to patch a handful of fields
 * that were still zero, which is the only way to express "unset" without an
 * initialiser — and it works for a scalar whose zero is nonsensical while
 * failing for everything else. A `bool` has no spare value, so
 * `auto_reconnect` could not be defaulted at all and a zero-initialised
 * config silently never reconnected. Two enums had `[DISABLED]` members
 * sitting on zero. Starting from a fully-populated struct removes the whole
 * class: zero means zero, `false` means false, and init does not rewrite what
 * you passed.
 *
 * @warning A designated initialiser for a **nested struct replaces the whole
 * sub-struct**, so `{ WIFI_CFG_DEFAULTS, .default_ap = {.ssid = "x"} }` blanks
 * the other `default_ap` fields. `wifi_cfg_init()` backfills the per-field AP
 * defaults for exactly this reason; set members individually
 * (`cfg.default_ap.ssid`) if you want to be sure.
 */
#define WIFI_CFG_DEFAULTS                                                      \
    .max_retry_per_network   = CONFIG_WIFI_CFG_DEFAULT_RETRY,                  \
    .retry_interval_ms       = CONFIG_WIFI_CFG_RETRY_INTERVAL_MS,              \
    .retry_max_interval_ms   = 60000,                                          \
    .auto_reconnect          = true,                                           \
    .max_reconnect_attempts  = 0,  /* 0 = keep trying forever */               \
    .on_reconnect_exhausted  = WIFI_ON_RECONNECT_EXHAUSTED_RESTART,            \
    .provisioning_mode       = WIFI_PROV_ON_FAILURE,                           \
    .http_post_prov_mode     = WIFI_HTTP_FULL,                                 \
    .default_ap = {                                                            \
        .ssid            = WIFI_CFG_DEFAULT_AP_SSID,                           \
        .password        = WIFI_CFG_DEFAULT_AP_PASSWORD,                       \
        .max_connections = 4,                                                  \
        .ip              = WIFI_CFG_DEFAULT_AP_IP,                             \
        .netmask         = "255.255.255.0",                                    \
        .gateway         = WIFI_CFG_DEFAULT_AP_IP,                             \
        .dhcp_start      = "192.168.4.2",                                      \
        .dhcp_end        = "192.168.4.20",                                     \
    },                                                                         \
    .http = {                                                                  \
        .api_base_path = "/api/wifi",                                          \
        .auth_username = "admin",                                              \
        .auth_password = "admin",                                              \
    },                                                                         \
    .improv = {                                                                \
        .serial_uart_num  = CONFIG_WIFI_MGR_IMPROV_SERIAL_UART_NUM,            \
        .serial_baud_rate = CONFIG_WIFI_MGR_IMPROV_SERIAL_BAUD,                \
    },                                                                         \
    .prov_ble = {                                                              \
        .cleanup_delay_ms    = 1000,                                           \
        .reboot_max_wait_ms  = 15000,                                          \
        .max_failed_attempts = 3,                                              \
    }

/** @brief `WIFI_CFG_DEFAULTS` as a complete struct value. See above. */
#define WIFI_CFG_DEFAULT_CONFIG() ((wifi_cfg_config_t){ WIFI_CFG_DEFAULTS })

// =============================================================================
// Public API
// =============================================================================

/**
 * @brief Initialize WiFi Config
 * 
 * Khởi tạo WiFi Config với config. Sẽ tự động:
 * - Load networks/vars từ NVS (hoặc dùng defaults)
 * - Khởi tạo WiFi station
 * - Bắt đầu auto-connect
 * - Khởi tạo HTTP server (nếu enable)
 * 
 * @param config Configuration, or NULL for every default unmodified.
 *
 *               Build it from WIFI_CFG_DEFAULTS. init does **not** patch
 *               fields you left at zero — zero means zero — and it rejects a
 *               config with `retry_interval_ms` or `retry_max_interval_ms` at
 *               zero, because a zero base makes the backoff `base << retry`
 *               zero and the device would retry with no delay. Passing a
 *               struct you memset yourself will fail that check; that is
 *               deliberate, and the error names the macro.
 * @return ESP_OK on success
 * 
 * @code{.c}
 * wifi_cfg_init(&(wifi_cfg_config_t){
 *     .default_networks = (wifi_network_t[]){
 *         {"MyWiFi", "password", 10},
 *     },
 *     .default_network_count = 1,
 *     .provisioning_mode = WIFI_PROV_ON_FAILURE,
 *     .enable_ap = true,
 * });
 * @endcode
 */
esp_err_t wifi_cfg_init(const wifi_cfg_config_t *config);

/**
 * @brief Deinitialize WiFi Config
 * 
 * Stop the HTTP server, BLE, and (optionally) WiFi interfaces. Free resources.
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_deinit(bool deinit_wifi);

/**
 * @brief Check if WiFi is connected
 * 
 * @return true if connected with IP, false otherwise
 */
bool wifi_cfg_is_connected(void);

/**
 * @brief Get current WiFi state
 * 
 * @return Current state: WIFI_STATE_DISCONNECTED, WIFI_STATE_CONNECTING, WIFI_STATE_CONNECTED
 */
wifi_state_t wifi_cfg_get_state(void);

/**
 * @brief Wait for WiFi connection
 * 
 * Block cho đến khi WiFi connected hoặc timeout.
 * 
 * @param timeout_ms Timeout in milliseconds, 0 = wait forever
 * @return ESP_OK if connected, ESP_ERR_TIMEOUT if timeout
 * 
 * @code{.c}
 * if (wifi_cfg_wait_connected(30000) == ESP_OK) {
 *     ESP_LOGI(TAG, "Connected!");
 * } else {
 *     ESP_LOGW(TAG, "Connection timeout");
 * }
 * @endcode
 */
esp_err_t wifi_cfg_wait_connected(uint32_t timeout_ms);

/**
 * @brief Get full WiFi status
 * 
 * Lấy trạng thái đầy đủ bao gồm IP, RSSI, channel, hostname, etc.
 * 
 * @param[out] status Output status structure
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_get_status(wifi_status_t *status);

/**
 * @brief Get HTTP server handle
 * 
 * Lấy httpd handle để register thêm endpoints từ components khác.
 * 
 * @return httpd_handle_t or NULL if HTTP not enabled
 * 
 * @code{.c}
 * httpd_handle_t server = wifi_cfg_get_httpd();
 * if (server) {
 *     httpd_uri_t my_uri = { .uri = "/my/api", .method = HTTP_GET, .handler = my_handler };
 *     httpd_register_uri_handler(server, &my_uri);
 * }
 * @endcode
 */
httpd_handle_t wifi_cfg_get_httpd(void);

/**
 * @brief Stop the HTTP server
 *
 * Explicitly tear down the HTTPD server (only when the library owns it).
 * Intended for MANUAL provisioning mode with WIFI_HTTP_DISABLED where the
 * integrator controls the full lifecycle. Will refuse if provisioning is
 * active or if the reconnect constraint requires the server to stay alive.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if server cannot be stopped
 */
esp_err_t wifi_cfg_stop_http(void);

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
esp_err_t wifi_cfg_add_network(const wifi_network_t *network);

/**
 * @brief Update a network
 * 
 * Cập nhật network theo SSID. Emit event WIFI_EVENT_NETWORK_UPDATED.
 * 
 * @param network Network config (SSID dùng để tìm)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not exists
 */
esp_err_t wifi_cfg_update_network(const wifi_network_t *network);

/**
 * @brief Remove a network by SSID
 * 
 * Xóa network. Emit event WIFI_EVENT_NETWORK_REMOVED.
 * 
 * @param ssid SSID to remove
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not exists
 */
esp_err_t wifi_cfg_remove_network(const char *ssid);

/**
 * @brief Get a network by SSID
 * 
 * @param ssid SSID to find
 * @param[out] network Output network config
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not exists
 */
esp_err_t wifi_cfg_get_network(const char *ssid, wifi_network_t *network);

/**
 * @brief Get all saved networks
 * 
 * @param[out] networks Output array
 * @param max_count Array size
 * @param[out] count Output actual count
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_list_networks(wifi_network_t *networks, size_t max_count, size_t *count);

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
esp_err_t wifi_cfg_set_var(const char *key, const char *value);

/**
 * @brief Get a variable
 * 
 * @param key Variable key
 * @param[out] value Output buffer
 * @param max_len Buffer size
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not exists
 */
esp_err_t wifi_cfg_get_var(const char *key, char *value, size_t max_len);

/**
 * @brief Delete a variable
 * 
 * Xóa variable. Emit event WIFI_EVENT_VAR_CHANGED với value rỗng.
 * 
 * @param key Variable key
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not exists
 */
esp_err_t wifi_cfg_del_var(const char *key);

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
 * wifi_cfg_start_ap(NULL);
 * 
 * // Hoặc custom config
 * wifi_cfg_start_ap(&(wifi_cfg_ap_config_t){
 *     .ssid = "MyAP",
 *     .password = "12345678",
 *     .ip = "10.0.0.1",
 * });
 * @endcode
 */
esp_err_t wifi_cfg_start_ap(const wifi_cfg_ap_config_t *config);

/**
 * @brief Stop SoftAP
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_stop_ap(void);

/**
 * @brief Get SoftAP status
 * 
 * @param[out] status Output status bao gồm danh sách clients
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_get_ap_status(wifi_ap_status_t *status);

/**
 * @brief Set SoftAP config
 * 
 * Cập nhật config và lưu vào NVS. Apply ngay nếu AP đang chạy.
 * 
 * @param config New AP config
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_set_ap_config(const wifi_cfg_ap_config_t *config);

/**
 * @brief Get SoftAP config
 * 
 * @param[out] config Output config
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_get_ap_config(wifi_cfg_ap_config_t *config);

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
 * wifi_cfg_connect(NULL);
 * 
 * // Kết nối mạng cụ thể
 * wifi_cfg_connect("MyWiFi");
 * 
 * // Chờ kết nối
 * wifi_cfg_wait_connected(10000);
 * @endcode
 */
esp_err_t wifi_cfg_connect(const char *ssid);

/**
 * @brief Disconnect from current network
 * 
 * Ngắt kết nối và tắt auto-reconnect.
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_disconnect(void);

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
esp_err_t wifi_cfg_scan(wifi_scan_result_t *results, size_t max_count, size_t *count);

// =============================================================================
// System API
// =============================================================================

/**
 * @brief Factory reset
 *
 * Xóa toàn bộ dữ liệu NVS: networks, variables, AP config, auth credentials.
 * Sau khi gọi, cần restart hoặc gọi wifi_cfg_deinit() rồi init lại.
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_factory_reset(void);

// =============================================================================
// Event API
// =============================================================================

/**
 * @brief Subscribe to library events
 *
 * Subscriptions live in a fixed-size table (CONFIG_WIFI_CFG_MAX_EVENT_SUBS,
 * default 8) -- no allocation happens here, and the call fails with
 * ESP_ERR_NO_MEM once the table is full rather than growing it.
 *
 * Subscribing is independent of wifi_cfg_init(): register before init to catch
 * events emitted during startup. Subscriptions survive wifi_cfg_deinit().
 *
 * Read ::wifi_cfg_event_cb_t before writing a handler -- callbacks run
 * synchronously on the emitting task, which constrains what they may do.
 *
 * @param event      Event to listen for, or ::WIFI_CFG_EVENT_ANY for all of them
 * @param cb         Handler, must not be NULL
 * @param ctx        Opaque pointer handed back to @p cb
 * @param out_handle Receives a handle for wifi_cfg_event_unsubscribe(); may be
 *                   NULL if the subscription is never removed
 *
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG if @p cb is NULL or @p event is out of range
 *         ESP_ERR_NO_MEM if the subscription table is full
 */
esp_err_t wifi_cfg_event_subscribe(wifi_cfg_event_t event, wifi_cfg_event_cb_t cb,
                                   void *ctx, int *out_handle);

/**
 * @brief Remove a subscription
 *
 * Safe to call from inside an event callback; the current dispatch still
 * completes against the subscriber list as it stood when the event fired.
 *
 * @param handle Handle from wifi_cfg_event_subscribe()
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if @p handle is not valid
 */
esp_err_t wifi_cfg_event_unsubscribe(int handle);

/**
 * @brief Human-readable name for an event, for logging
 *
 * @param event Event id
 * @return Static string, or "unknown" if @p event is out of range. Never NULL.
 */
const char *wifi_cfg_event_name(wifi_cfg_event_t event);

#ifdef __cplusplus
}
#endif
