/**
 * @file esp_wifi_config_http.c
 * @brief HTTP REST API for WiFi Config
 */

#include "esp_wifi_config_priv.h"
#include "esp_log.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include <string.h>

static const char *TAG = "wifi_cfg_http";

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Add CORS headers to response
 */
static void add_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
}

/**
 * @brief Check HTTP Basic Auth
 */
static bool check_auth(httpd_req_t *req)
{
    if (!g_wifi_cfg->config.http.enable_auth) {
        return true;
    }

    char auth_header[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
        return false;
    }

    // Basic auth: "Basic base64(user:pass)"
    if (strncmp(auth_header, "Basic ", 6) != 0) {
        return false;
    }

    // Decode Base64
    unsigned char decoded[128];
    size_t decoded_len = 0;
    const char *b64_data = auth_header + 6;
    size_t b64_len = strlen(b64_data);

    int ret = mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
                                     (const unsigned char *)b64_data, b64_len);
    if (ret != 0 || decoded_len == 0) {
        return false;
    }
    decoded[decoded_len] = '\0';

    // Parse user:pass
    char *colon = strchr((char *)decoded, ':');
    if (!colon) {
        return false;
    }
    *colon = '\0';
    const char *username = (char *)decoded;
    const char *password = colon + 1;

    // Verify credentials
    return (strcmp(username, g_wifi_cfg->auth_username) == 0 &&
            strcmp(password, g_wifi_cfg->auth_password) == 0);
}

static esp_err_t send_json_response(httpd_req_t *req, cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    if (!str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    add_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    free(str);
    return ESP_OK;
}

static esp_err_t send_ok(httpd_req_t *req)
{
    add_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t send_error(httpd_req_t *req, int code, const char *msg)
{
    const char *status;
    switch (code) {
        case 400: status = "400 Bad Request"; break;
        case 401: status = "401 Unauthorized"; break;
        case 403: status = "403 Forbidden"; break;
        case 404: status = "404 Not Found"; break;
        case 500: status = "500 Internal Server Error"; break;
        default:  status = "400 Bad Request"; break;
    }
    add_cors_headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg);
    httpd_resp_sendstr(req, buf);

    /* ESP_OK, not ESP_FAIL: the response was produced and sent successfully.
     *
     * esp_http_server treats a non-OK return as "this handler could not
     * respond, close the socket" -- httpd_uri.c:
     *
     *     if (uri->handler(req) != ESP_OK) {
     *         ESP_LOGW(TAG, LOG_FMT("uri handler execution failed"));
     *         return ESP_FAIL;
     *     }
     *
     * Returning ESP_FAIL here meant every 4xx tore down the connection. With
     * seven sockets and no LRU purge, a client that collected a few error
     * responses -- a Web UI polling an endpoint that 404s, say -- churned the
     * pool until the device stopped accepting connections altogether. */
    return ESP_OK;
}

/**
 * @brief Run pre-request hook and auth check for API handlers
 *
 * Calls the pre-request hook first (if configured), then the built-in auth check.
 * Returns true if the request should proceed, false if rejected.
 * On rejection, the appropriate error response has already been sent.
 */
static bool check_api_access(httpd_req_t *req)
{
    // Pre-request hook (called before auth, so hook can replace/supplement auth)
    if (g_wifi_cfg->config.http.pre_request_hook) {
        esp_err_t hook_ret = g_wifi_cfg->config.http.pre_request_hook(req, g_wifi_cfg->config.http.hook_ctx);
        if (hook_ret != ESP_OK) {
            send_error(req, 403, "Forbidden");
            return false;
        }
    }

    // Built-in auth check
    if (!check_auth(req)) {
        send_error(req, 401, "Unauthorized");
        return false;
    }

    return true;
}

/**
 * @brief OPTIONS handler for CORS preflight
 */
static esp_err_t handler_options(httpd_req_t *req)
{
    add_cors_headers(req);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Read and discard the rest of a request body.
 *
 * Every path that refuses a body has to do this. When a handler returns,
 * esp_http_server hands the socket back to its request parser, and anything
 * left unread is then taken as the start of the *next* request -- so the
 * client's following request on that connection is met with a reset instead of
 * the response it was owed.
 *
 * Measured on an ESP32-S3 (IDF 5.5.4, 2026-08-10) before this existed: with an
 * oversized body the intended 400 raced the server's close and arrived only
 * about half the time, and on a keep-alive connection the *next* request was
 * reset every time.
 */
static void drain_body(httpd_req_t *req, int remaining)
{
    char sink[128];
    while (remaining > 0) {
        int want = remaining < (int)sizeof(sink) ? remaining : (int)sizeof(sink);
        int got = httpd_req_recv(req, sink, want);
        if (got <= 0) {
            break;      // timeout or the peer went away; nothing more to do
        }
        remaining -= got;
    }
}

/**
 * @brief Reject bodies nested deeper than the httpd task's stack can survive.
 *
 * cJSON parses by recursive descent, so nesting depth translates directly into
 * stack frames -- and `WIFI_CFG_HTTP_MAX_CONTENT` bounds a body's *length*,
 * which is not the same thing at all: two characters buy one level, so 109
 * bytes buys fifty.
 *
 * Measured on an ESP32-S3 (IDF 5.5.4, 2026-08-10) against the 4 KB
 * `HTTPD_DEFAULT_CONFIG()` stack: depth 40 was answered normally, depth 50
 * overflowed the stack and rebooted the device, and depth 200 wedged it hard
 * enough to need a physical reset. Basic Auth is off by default and the SoftAP
 * is open by default, so that was reachable by anyone in radio range.
 *
 * The limit is deliberately far below the measured cliff and far above
 * anything this API's own payloads need -- the deepest of them is a flat
 * object of scalars.
 */
static bool json_depth_within_limit(const char *buf, size_t len)
{
    int depth = 0;
    bool in_string = false;

    for (size_t i = 0; i < len; i++) {
        char c = buf[i];

        if (in_string) {
            if (c == '\\') {
                i++;                    // skip whatever was escaped
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        switch (c) {
            case '"':
                in_string = true;
                break;
            case '[':
            case '{':
                if (++depth > WIFI_CFG_JSON_MAX_DEPTH) {
                    return false;
                }
                break;
            case ']':
            case '}':
                depth--;
                break;
            default:
                break;
        }
    }
    return true;
}

static cJSON *read_json_body(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0) {
        return NULL;
    }

    if (content_len > WIFI_CFG_HTTP_MAX_CONTENT) {
        // Refused without allocating -- but the body still has to come off the
        // socket, or the connection is left mid-request. See drain_body().
        ESP_LOGW(TAG, "body of %d bytes exceeds the %d-byte limit",
                 content_len, WIFI_CFG_HTTP_MAX_CONTENT);
        drain_body(req, content_len);
        return NULL;
    }

    char *buf = malloc(content_len + 1);
    if (!buf) {
        drain_body(req, content_len);
        return NULL;
    }

    // Loop, because httpd_req_recv() returns what one read produced. A body
    // split across TCP segments came back short, which left the tail in the
    // socket and handed cJSON a truncated document.
    int received = 0;
    while (received < content_len) {
        int got = httpd_req_recv(req, buf + received, content_len - received);
        if (got <= 0) {
            free(buf);
            return NULL;
        }
        received += got;
    }
    buf[received] = '\0';

    if (!json_depth_within_limit(buf, (size_t)received)) {
        ESP_LOGW(TAG, "body nested deeper than %d; refusing before cJSON sees it",
                 WIFI_CFG_JSON_MAX_DEPTH);
        free(buf);
        return NULL;
    }

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    return json;
}

// =============================================================================
// Handlers
// =============================================================================

// GET /status
static esp_err_t handler_get_status(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    wifi_status_t status;
    wifi_cfg_get_status(&status);
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "state", 
        status.state == WIFI_STATE_CONNECTED ? "connected" :
        status.state == WIFI_STATE_CONNECTING ? "connecting" : "disconnected");
    cJSON_AddStringToObject(json, "ssid", status.ssid);
    cJSON_AddNumberToObject(json, "rssi", status.rssi);
    cJSON_AddNumberToObject(json, "quality", status.quality);
    cJSON_AddNumberToObject(json, "channel", status.channel);
    cJSON_AddStringToObject(json, "ip", status.ip);
    cJSON_AddStringToObject(json, "netmask", status.netmask);
    cJSON_AddStringToObject(json, "gateway", status.gateway);
    cJSON_AddStringToObject(json, "dns", status.dns);
    cJSON_AddStringToObject(json, "mac", status.mac);
    cJSON_AddStringToObject(json, "hostname", status.hostname);
    cJSON_AddNumberToObject(json, "uptime_ms", status.uptime_ms);
    cJSON_AddBoolToObject(json, "ap_active", status.ap_active);
    
    esp_err_t ret = send_json_response(req, json);
    cJSON_Delete(json);
    return ret;
}

// GET /scan
static esp_err_t handler_get_scan(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    wifi_scan_result_t results[WIFI_CFG_MAX_SCAN_RESULTS];
    size_t count = 0;

    esp_err_t ret = wifi_cfg_scan(results, WIFI_CFG_MAX_SCAN_RESULTS, &count);
    if (ret != ESP_OK) {
        return send_error(req, 500, "Scan failed");
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(json, "networks");
    
    for (size_t i = 0; i < count; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", results[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", results[i].rssi);
        
        const char *auth_str = "UNKNOWN";
        switch (results[i].auth) {
            case WIFI_AUTH_OPEN: auth_str = "OPEN"; break;
            case WIFI_AUTH_WEP: auth_str = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: auth_str = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: auth_str = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: auth_str = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK: auth_str = "WPA3"; break;
            default: break;
        }
        cJSON_AddStringToObject(net, "auth", auth_str);
        cJSON_AddItemToArray(arr, net);
    }
    
    ret = send_json_response(req, json);
    cJSON_Delete(json);
    return ret;
}

// GET /networks
static esp_err_t handler_get_networks(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    wifi_network_t networks[WIFI_CFG_MAX_NETWORKS];
    size_t count = 0;
    wifi_cfg_list_networks(networks, WIFI_CFG_MAX_NETWORKS, &count);
    
    cJSON *json = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(json, "networks");
    
    for (size_t i = 0; i < count; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", networks[i].ssid);
        cJSON_AddNumberToObject(net, "priority", networks[i].priority);
        cJSON_AddItemToArray(arr, net);
    }
    
    esp_err_t ret = send_json_response(req, json);
    cJSON_Delete(json);
    return ret;
}

// POST /networks
static esp_err_t handler_post_networks(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    cJSON *json = read_json_body(req);
    if (!json) {
        return send_error(req, 400, "Invalid JSON");
    }
    
    cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *password = cJSON_GetObjectItem(json, "password");
    cJSON *priority = cJSON_GetObjectItem(json, "priority");
    
    if (!cJSON_IsString(ssid)) {
        cJSON_Delete(json);
        return send_error(req, 400, "Missing ssid");
    }
    
    wifi_network_t network = {0};
    strncpy(network.ssid, ssid->valuestring, sizeof(network.ssid) - 1);
    if (cJSON_IsString(password)) {
        strncpy(network.password, password->valuestring, sizeof(network.password) - 1);
    }
    if (cJSON_IsNumber(priority)) {
        network.priority = (uint8_t)priority->valueint;
    }
    
    cJSON_Delete(json);
    
    esp_err_t ret = wifi_cfg_add_network(&network);
    if (ret == ESP_ERR_INVALID_STATE) {
        // Network already exists — update it instead (upsert)
        ret = wifi_cfg_update_network(&network);
    }
    if (ret != ESP_OK) {
        return send_error(req, 400, "Failed");
    }
    
    return send_ok(req);
}

// DELETE /networks/:ssid
static esp_err_t handler_delete_network(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    // Extract SSID from URI
    char ssid[32] = {0};
    const char *uri = req->uri;
    const char *last_slash = strrchr(uri, '/');
    if (last_slash && strlen(last_slash) > 1) {
        strncpy(ssid, last_slash + 1, sizeof(ssid) - 1);
    }
    
    if (!ssid[0]) {
        return send_error(req, 400, "Missing ssid");
    }
    
    esp_err_t ret = wifi_cfg_remove_network(ssid);
    if (ret == ESP_ERR_NOT_FOUND) {
        return send_error(req, 404, "Not found");
    }
    
    return send_ok(req);
}

// PUT /networks/:ssid - Update network (password, priority)
static esp_err_t handler_put_network(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    // Extract SSID from URI
    char ssid[32] = {0};
    const char *uri = req->uri;
    const char *last_slash = strrchr(uri, '/');
    if (last_slash && strlen(last_slash) > 1) {
        strncpy(ssid, last_slash + 1, sizeof(ssid) - 1);
    }
    
    if (!ssid[0]) {
        return send_error(req, 400, "Missing ssid");
    }
    
    cJSON *json = read_json_body(req);
    if (!json) {
        return send_error(req, 400, "Invalid JSON");
    }
    
    wifi_network_t network = {0};
    strncpy(network.ssid, ssid, sizeof(network.ssid) - 1);
    
    cJSON *password = cJSON_GetObjectItem(json, "password");
    cJSON *priority = cJSON_GetObjectItem(json, "priority");
    
    if (cJSON_IsString(password)) {
        strncpy(network.password, password->valuestring, sizeof(network.password) - 1);
    }
    if (cJSON_IsNumber(priority)) {
        network.priority = (uint8_t)priority->valueint;
    }
    
    cJSON_Delete(json);
    
    esp_err_t ret = wifi_cfg_update_network(&network);
    if (ret == ESP_ERR_NOT_FOUND) {
        return send_error(req, 404, "Not found");
    }
    if (ret != ESP_OK) {
        return send_error(req, 400, "Failed");
    }
    
    return send_ok(req);
}

// POST /connect
static esp_err_t handler_post_connect(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    if (req->content_len > 0) {
        cJSON *json = read_json_body(req);
        if (json) {
            cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
            if (cJSON_IsString(ssid_item)) {
                wifi_cfg_connect(ssid_item->valuestring);
                cJSON_Delete(json);
                return send_ok(req);
            }
            cJSON_Delete(json);
        }
    }
    
    wifi_cfg_connect(NULL);
    return send_ok(req);
}

// POST /disconnect
static esp_err_t handler_post_disconnect(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    wifi_cfg_disconnect();
    return send_ok(req);
}

// GET /ap/status
static esp_err_t handler_get_ap_status(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    wifi_ap_status_t status;
    wifi_cfg_get_ap_status(&status);
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "active", status.active);
    cJSON_AddStringToObject(json, "ssid", status.ssid);
    cJSON_AddStringToObject(json, "ip", status.ip);
    cJSON_AddNumberToObject(json, "channel", status.channel);
    cJSON_AddNumberToObject(json, "sta_count", status.sta_count);
    
    cJSON *clients = cJSON_AddArrayToObject(json, "clients");
    for (int i = 0; i < status.sta_count && i < 4; i++) {
        cJSON *client = cJSON_CreateObject();
        cJSON_AddStringToObject(client, "mac", status.clients[i].mac);
        cJSON_AddStringToObject(client, "ip", status.clients[i].ip);
        cJSON_AddItemToArray(clients, client);
    }
    
    esp_err_t ret = send_json_response(req, json);
    cJSON_Delete(json);
    return ret;
}

// GET /ap/config
static esp_err_t handler_get_ap_config(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    wifi_cfg_ap_config_t config;
    wifi_cfg_get_ap_config(&config);
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "ssid", config.ssid);
    cJSON_AddStringToObject(json, "password", config.password);
    cJSON_AddNumberToObject(json, "channel", config.channel);
    cJSON_AddNumberToObject(json, "max_connections", config.max_connections);
    cJSON_AddBoolToObject(json, "hidden", config.hidden);
    cJSON_AddStringToObject(json, "ip", config.ip);
    cJSON_AddStringToObject(json, "netmask", config.netmask);
    cJSON_AddStringToObject(json, "gateway", config.gateway);
    cJSON_AddStringToObject(json, "dhcp_start", config.dhcp_start);
    cJSON_AddStringToObject(json, "dhcp_end", config.dhcp_end);
    
    esp_err_t ret = send_json_response(req, json);
    cJSON_Delete(json);
    return ret;
}

// PUT /ap/config
static esp_err_t handler_put_ap_config(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    cJSON *json = read_json_body(req);
    if (!json) {
        return send_error(req, 400, "Invalid JSON");
    }
    
    wifi_cfg_ap_config_t config;
    wifi_cfg_get_ap_config(&config);
    
    cJSON *item;
    if ((item = cJSON_GetObjectItem(json, "ssid")) && cJSON_IsString(item)) {
        strncpy(config.ssid, item->valuestring, sizeof(config.ssid) - 1);
    }
    if ((item = cJSON_GetObjectItem(json, "password")) && cJSON_IsString(item)) {
        strncpy(config.password, item->valuestring, sizeof(config.password) - 1);
    }
    if ((item = cJSON_GetObjectItem(json, "channel")) && cJSON_IsNumber(item)) {
        config.channel = (uint8_t)item->valueint;
    }
    if ((item = cJSON_GetObjectItem(json, "max_connections")) && cJSON_IsNumber(item)) {
        config.max_connections = (uint8_t)item->valueint;
    }
    if ((item = cJSON_GetObjectItem(json, "hidden")) && cJSON_IsBool(item)) {
        config.hidden = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(json, "ip")) && cJSON_IsString(item)) {
        strncpy(config.ip, item->valuestring, sizeof(config.ip) - 1);
    }
    if ((item = cJSON_GetObjectItem(json, "netmask")) && cJSON_IsString(item)) {
        strncpy(config.netmask, item->valuestring, sizeof(config.netmask) - 1);
    }
    if ((item = cJSON_GetObjectItem(json, "gateway")) && cJSON_IsString(item)) {
        strncpy(config.gateway, item->valuestring, sizeof(config.gateway) - 1);
    }
    if ((item = cJSON_GetObjectItem(json, "dhcp_start")) && cJSON_IsString(item)) {
        strncpy(config.dhcp_start, item->valuestring, sizeof(config.dhcp_start) - 1);
    }
    if ((item = cJSON_GetObjectItem(json, "dhcp_end")) && cJSON_IsString(item)) {
        strncpy(config.dhcp_end, item->valuestring, sizeof(config.dhcp_end) - 1);
    }
    
    cJSON_Delete(json);
    
    wifi_cfg_set_ap_config(&config);
    return send_ok(req);
}

// POST /ap/start
static esp_err_t handler_post_ap_start(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    wifi_cfg_ap_config_t *config = NULL;
    wifi_cfg_ap_config_t temp_config;
    
    if (req->content_len > 0) {
        cJSON *json = read_json_body(req);
        if (json) {
            wifi_cfg_get_ap_config(&temp_config);
            
            cJSON *item;
            if ((item = cJSON_GetObjectItem(json, "ssid")) && cJSON_IsString(item)) {
                strncpy(temp_config.ssid, item->valuestring, sizeof(temp_config.ssid) - 1);
            }
            if ((item = cJSON_GetObjectItem(json, "password")) && cJSON_IsString(item)) {
                strncpy(temp_config.password, item->valuestring, sizeof(temp_config.password) - 1);
            }
            cJSON_Delete(json);
            config = &temp_config;
        }
    }
    
    wifi_cfg_start_ap(config);
    return send_ok(req);
}

// POST /ap/stop
static esp_err_t handler_post_ap_stop(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    wifi_cfg_stop_ap();
    return send_ok(req);
}

// GET /vars
static esp_err_t handler_get_vars(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    wifi_cfg_lock();
    
    cJSON *json = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(json, "vars");
    
    for (size_t i = 0; i < g_wifi_cfg->var_count; i++) {
        cJSON *var = cJSON_CreateObject();
        cJSON_AddStringToObject(var, "key", g_wifi_cfg->vars[i].key);
        cJSON_AddStringToObject(var, "value", g_wifi_cfg->vars[i].value);
        cJSON_AddItemToArray(arr, var);
    }
    
    wifi_cfg_unlock();
    
    esp_err_t ret = send_json_response(req, json);
    cJSON_Delete(json);
    return ret;
}

// PUT /vars/:key
static esp_err_t handler_put_var(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    // Extract key from URI
    char key[32] = {0};
    const char *uri = req->uri;
    const char *last_slash = strrchr(uri, '/');
    if (last_slash && strlen(last_slash) > 1) {
        strncpy(key, last_slash + 1, sizeof(key) - 1);
    }
    
    if (!key[0]) {
        return send_error(req, 400, "Missing key");
    }
    
    cJSON *json = read_json_body(req);
    if (!json) {
        return send_error(req, 400, "Invalid JSON");
    }
    
    cJSON *value = cJSON_GetObjectItem(json, "value");
    if (!cJSON_IsString(value)) {
        cJSON_Delete(json);
        return send_error(req, 400, "Missing value");
    }

    esp_err_t set_ret = wifi_cfg_set_var(key, value->valuestring);
    cJSON_Delete(json);

    if (set_ret != ESP_OK) {
        return send_error(req, 400, "var_invalid");
    }

    return send_ok(req);
}

// DELETE /vars/:key
static esp_err_t handler_delete_var(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }

    // Extract key from URI
    char key[32] = {0};
    const char *uri = req->uri;
    const char *last_slash = strrchr(uri, '/');
    if (last_slash && strlen(last_slash) > 1) {
        strncpy(key, last_slash + 1, sizeof(key) - 1);
    }

    if (!key[0]) {
        return send_error(req, 400, "Missing key");
    }

    esp_err_t ret = wifi_cfg_del_var(key);
    if (ret == ESP_ERR_NOT_FOUND) {
        return send_error(req, 404, "Not found");
    }

    return send_ok(req);
}

// POST /factory_reset
static esp_err_t handler_post_factory_reset(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }

    esp_err_t ret = wifi_cfg_factory_reset();
    if (ret != ESP_OK) {
        return send_error(req, 500, "Reset failed");
    }

    return send_ok(req);
}

// =============================================================================
// Simple Fallback Page (when Web UI not enabled)
// =============================================================================

static const char *simple_page_html =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 WiFi Setup</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:system-ui,sans-serif;background:#f0f4f8;padding:20px}"
    ".c{max-width:400px;margin:0 auto;background:#fff;padding:20px;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,.1)}"
    "h1{font-size:1.5em;margin-bottom:20px;color:#333}"
    "input,select,button{width:100%;padding:12px;margin:8px 0;border:1px solid #ddd;border-radius:8px;font-size:16px}"
    "button{background:#3b82f6;color:#fff;border:none;cursor:pointer}"
    "button:hover{background:#2563eb}"
    ".nets{margin:15px 0}"
    ".net{padding:10px;background:#f8fafc;margin:5px 0;border-radius:6px;cursor:pointer}"
    ".net:hover{background:#e2e8f0}"
    ".msg{padding:10px;border-radius:6px;margin:10px 0}"
    ".ok{background:#d1fae5;color:#065f46}"
    ".err{background:#fee2e2;color:#991b1b}"
    "</style></head><body>"
    "<div class='c'><h1>ESP32 WiFi Setup</h1>"
    "<div id='msg'></div>"
    "<div id='nets' class='nets'><p>Loading networks...</p></div>"
    "<button onclick='scan()'>Scan Networks</button>"
    "<hr style='margin:20px 0;border:none;border-top:1px solid #eee'>"
    "<input id='ssid' placeholder='WiFi Name (SSID)'>"
    "<input id='pass' type='password' placeholder='Password'>"
    "<button onclick='connect()'>Connect</button>"
    "</div>"
    "<script>"
    "const API='/api/wifi';"
    "function msg(t,ok){document.getElementById('msg').innerHTML='<div class=\"msg '+(ok?'ok':'err')+'\">'+t+'</div>';}"
    "async function scan(){"
    "document.getElementById('nets').innerHTML='<p>Scanning...</p>';"
    "try{const r=await fetch(API+'/scan');const d=await r.json();"
    "let h='';d.networks.forEach(n=>{"
    "h+='<div class=\"net\" onclick=\"document.getElementById(\\'ssid\\').value=\\''+n.ssid+'\\'\">'+"
    "n.ssid+' ('+n.rssi+' dBm)</div>';});"
    "document.getElementById('nets').innerHTML=h||'<p>No networks</p>';"
    "}catch(e){document.getElementById('nets').innerHTML='<p>Scan failed</p>';}}"
    "async function connect(){"
    "const s=document.getElementById('ssid').value;"
    "const p=document.getElementById('pass').value;"
    "if(!s){msg('Enter SSID',0);return;}"
    "try{await fetch(API+'/networks',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:s,password:p,priority:10})});"
    "await fetch(API+'/connect',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:s})});"
    "msg('Connecting to '+s+'...',1);"
    "}catch(e){msg('Error: '+e,0);}}"
    "scan();"
    "</script></body></html>";

static esp_err_t handler_simple_page(httpd_req_t *req)
{
    add_cors_headers(req);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, simple_page_html, strlen(simple_page_html));
    return ESP_OK;
}

// =============================================================================
// Captive Portal
// =============================================================================

// Captive portal detection paths
static const char *captive_detect_paths[] = {
    "/generate_204",        // Android
    "/gen_204",             // Android alt
    "/hotspot-detect.html", // iOS/macOS
    "/library/test/success.html", // iOS
    "/ncsi.txt",            // Windows
    "/connecttest.txt",     // Windows
    "/success.txt",         // Firefox
    "/canonical.html",      // Firefox
    NULL
};

/**
 * @brief Captive portal detection handler - triggers OS popup
 */
static esp_err_t handler_captive_detect(httpd_req_t *req)
{
    // Get AP IP for redirect
    char redirect_url[64];
    if (g_wifi_cfg->ap_config.ip[0]) {
        snprintf(redirect_url, sizeof(redirect_url), "http://%s/", g_wifi_cfg->ap_config.ip);
    } else {
        snprintf(redirect_url, sizeof(redirect_url), "http://192.168.4.1/");
    }

    add_cors_headers(req);

    // Return 302 redirect to trigger captive portal popup
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", redirect_url);
    httpd_resp_send(req, NULL, 0);

    ESP_LOGD(TAG, "Captive detect: %s -> %s", req->uri, redirect_url);
    return ESP_OK;
}

// =============================================================================
// Init / Deinit
// =============================================================================

// Static URI strings (must persist after function returns)
static char uri_status[64];
static char uri_scan[64];
static char uri_networks[64];
static char uri_networks_wildcard[64];
static char uri_connect[64];
static char uri_disconnect[64];
static char uri_ap_status[64];
static char uri_ap_config[64];
static char uri_ap_start[64];
static char uri_ap_stop[64];
static char uri_vars[64];
static char uri_vars_wildcard[64];
static char uri_factory_reset[64];
static char uri_options_wildcard[64];

// =============================================================================
// Register API-only handlers (persist after provisioning stops)
// =============================================================================

esp_err_t wifi_cfg_http_register_api_handlers(void)
{
    if (!g_wifi_cfg || !g_wifi_cfg->httpd) return ESP_ERR_INVALID_STATE;

    const char *base = g_wifi_cfg->config.http.api_base_path;
    if (!base) base = "/api/wifi";

    // Status
    snprintf(uri_status, sizeof(uri_status), "%s/status", base);
    httpd_uri_t status_uri = { .uri = uri_status, .method = HTTP_GET, .handler = handler_get_status };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &status_uri);

    // Scan
    snprintf(uri_scan, sizeof(uri_scan), "%s/scan", base);
    httpd_uri_t scan_uri = { .uri = uri_scan, .method = HTTP_GET, .handler = handler_get_scan };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &scan_uri);

    // Networks
    snprintf(uri_networks, sizeof(uri_networks), "%s/networks", base);
    httpd_uri_t networks_get_uri = { .uri = uri_networks, .method = HTTP_GET, .handler = handler_get_networks };
    httpd_uri_t networks_post_uri = { .uri = uri_networks, .method = HTTP_POST, .handler = handler_post_networks };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &networks_get_uri);
    httpd_register_uri_handler(g_wifi_cfg->httpd, &networks_post_uri);

    // Update/Delete network - wildcard
    snprintf(uri_networks_wildcard, sizeof(uri_networks_wildcard), "%s/networks/*", base);
    httpd_uri_t networks_put_uri = { .uri = uri_networks_wildcard, .method = HTTP_PUT, .handler = handler_put_network };
    httpd_uri_t networks_del_uri = { .uri = uri_networks_wildcard, .method = HTTP_DELETE, .handler = handler_delete_network };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &networks_put_uri);
    httpd_register_uri_handler(g_wifi_cfg->httpd, &networks_del_uri);

    // Connect/Disconnect
    snprintf(uri_connect, sizeof(uri_connect), "%s/connect", base);
    httpd_uri_t connect_uri = { .uri = uri_connect, .method = HTTP_POST, .handler = handler_post_connect };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &connect_uri);

    snprintf(uri_disconnect, sizeof(uri_disconnect), "%s/disconnect", base);
    httpd_uri_t disconnect_uri = { .uri = uri_disconnect, .method = HTTP_POST, .handler = handler_post_disconnect };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &disconnect_uri);

    // AP
    snprintf(uri_ap_status, sizeof(uri_ap_status), "%s/ap/status", base);
    httpd_uri_t ap_status_uri = { .uri = uri_ap_status, .method = HTTP_GET, .handler = handler_get_ap_status };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &ap_status_uri);

    snprintf(uri_ap_config, sizeof(uri_ap_config), "%s/ap/config", base);
    httpd_uri_t ap_config_get_uri = { .uri = uri_ap_config, .method = HTTP_GET, .handler = handler_get_ap_config };
    httpd_uri_t ap_config_put_uri = { .uri = uri_ap_config, .method = HTTP_PUT, .handler = handler_put_ap_config };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &ap_config_get_uri);
    httpd_register_uri_handler(g_wifi_cfg->httpd, &ap_config_put_uri);

    snprintf(uri_ap_start, sizeof(uri_ap_start), "%s/ap/start", base);
    httpd_uri_t ap_start_uri = { .uri = uri_ap_start, .method = HTTP_POST, .handler = handler_post_ap_start };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &ap_start_uri);

    snprintf(uri_ap_stop, sizeof(uri_ap_stop), "%s/ap/stop", base);
    httpd_uri_t ap_stop_uri = { .uri = uri_ap_stop, .method = HTTP_POST, .handler = handler_post_ap_stop };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &ap_stop_uri);

    // Vars
    snprintf(uri_vars, sizeof(uri_vars), "%s/vars", base);
    httpd_uri_t vars_uri = { .uri = uri_vars, .method = HTTP_GET, .handler = handler_get_vars };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &vars_uri);

    snprintf(uri_vars_wildcard, sizeof(uri_vars_wildcard), "%s/vars/*", base);
    httpd_uri_t vars_put_uri = { .uri = uri_vars_wildcard, .method = HTTP_PUT, .handler = handler_put_var };
    httpd_uri_t vars_del_uri = { .uri = uri_vars_wildcard, .method = HTTP_DELETE, .handler = handler_delete_var };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &vars_put_uri);
    httpd_register_uri_handler(g_wifi_cfg->httpd, &vars_del_uri);

    // Factory reset
    snprintf(uri_factory_reset, sizeof(uri_factory_reset), "%s/factory_reset", base);
    httpd_uri_t factory_reset_uri = { .uri = uri_factory_reset, .method = HTTP_POST, .handler = handler_post_factory_reset };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &factory_reset_uri);

    // OPTIONS handler for CORS preflight (catch-all)
    snprintf(uri_options_wildcard, sizeof(uri_options_wildcard), "%s/*", base);
    httpd_uri_t options_uri = { .uri = uri_options_wildcard, .method = HTTP_OPTIONS, .handler = handler_options };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &options_uri);

    g_wifi_cfg->http_handlers_registered = true;
    ESP_LOGI(TAG, "API handlers registered");
    return ESP_OK;
}

static esp_err_t wifi_cfg_http_unregister_api_handlers(void)
{
    if (!g_wifi_cfg || !g_wifi_cfg->httpd) return ESP_ERR_INVALID_STATE;

    httpd_handle_t httpd = g_wifi_cfg->httpd;

    httpd_unregister_uri_handler(httpd, uri_status, HTTP_GET);
    httpd_unregister_uri_handler(httpd, uri_scan, HTTP_GET);
    httpd_unregister_uri_handler(httpd, uri_networks, HTTP_GET);
    httpd_unregister_uri_handler(httpd, uri_networks, HTTP_POST);
    httpd_unregister_uri_handler(httpd, uri_networks_wildcard, HTTP_PUT);
    httpd_unregister_uri_handler(httpd, uri_networks_wildcard, HTTP_DELETE);
    httpd_unregister_uri_handler(httpd, uri_connect, HTTP_POST);
    httpd_unregister_uri_handler(httpd, uri_disconnect, HTTP_POST);
    httpd_unregister_uri_handler(httpd, uri_ap_status, HTTP_GET);
    httpd_unregister_uri_handler(httpd, uri_ap_config, HTTP_GET);
    httpd_unregister_uri_handler(httpd, uri_ap_config, HTTP_PUT);
    httpd_unregister_uri_handler(httpd, uri_ap_start, HTTP_POST);
    httpd_unregister_uri_handler(httpd, uri_ap_stop, HTTP_POST);
    httpd_unregister_uri_handler(httpd, uri_vars, HTTP_GET);
    httpd_unregister_uri_handler(httpd, uri_vars_wildcard, HTTP_PUT);
    httpd_unregister_uri_handler(httpd, uri_vars_wildcard, HTTP_DELETE);
    httpd_unregister_uri_handler(httpd, uri_factory_reset, HTTP_POST);
    httpd_unregister_uri_handler(httpd, uri_options_wildcard, HTTP_OPTIONS);

    g_wifi_cfg->http_handlers_registered = false;
    ESP_LOGI(TAG, "API handlers unregistered");
    return ESP_OK;
}

// =============================================================================
// Register/Unregister Provisioning-specific handlers
// (captive portal detection, simple/WebUI pages)
// =============================================================================

esp_err_t wifi_cfg_http_register_provisioning_handlers(void)
{
    if (!g_wifi_cfg || !g_wifi_cfg->httpd) return ESP_ERR_INVALID_STATE;
    if (g_wifi_cfg->provisioning_handlers_registered) return ESP_OK;

    // Captive portal detection paths
    for (int i = 0; captive_detect_paths[i] != NULL; i++) {
        httpd_uri_t captive_uri = {
            .uri = captive_detect_paths[i],
            .method = HTTP_GET,
            .handler = handler_captive_detect
        };
        httpd_register_uri_handler(g_wifi_cfg->httpd, &captive_uri);
    }

    // Web UI or simple fallback page
#ifdef CONFIG_WIFI_CFG_ENABLE_WEBUI
    wifi_cfg_webui_init(g_wifi_cfg->httpd);
#else
    httpd_uri_t simple_uri = { .uri = "/", .method = HTTP_GET, .handler = handler_simple_page };
    httpd_register_uri_handler(g_wifi_cfg->httpd, &simple_uri);
#endif

    g_wifi_cfg->provisioning_handlers_registered = true;
    ESP_LOGI(TAG, "Provisioning handlers registered");
    return ESP_OK;
}

esp_err_t wifi_cfg_http_unregister_provisioning_handlers(void)
{
    if (!g_wifi_cfg || !g_wifi_cfg->httpd) return ESP_ERR_INVALID_STATE;
    if (!g_wifi_cfg->provisioning_handlers_registered) return ESP_OK;

    httpd_handle_t httpd = g_wifi_cfg->httpd;

    // Unregister captive portal detection paths
    for (int i = 0; captive_detect_paths[i] != NULL; i++) {
        httpd_unregister_uri_handler(httpd, captive_detect_paths[i], HTTP_GET);
    }

    // Unregister Web UI or simple page
#ifdef CONFIG_WIFI_CFG_ENABLE_WEBUI
    httpd_unregister_uri_handler(httpd, "/", HTTP_GET);
    httpd_unregister_uri_handler(httpd, "/assets/app.js", HTTP_GET);
    httpd_unregister_uri_handler(httpd, "/assets/index.css", HTTP_GET);
    // Wildcard handler for additional static files (only if custom path)
#ifdef CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH
    httpd_unregister_uri_handler(httpd, "/*", HTTP_GET);
#endif
#else
    httpd_unregister_uri_handler(httpd, "/", HTTP_GET);
#endif

    g_wifi_cfg->provisioning_handlers_registered = false;
    ESP_LOGI(TAG, "Provisioning handlers unregistered");
    return ESP_OK;
}

// =============================================================================
// HTTP Post-Provisioning Transition
// =============================================================================

void wifi_cfg_http_transition_post_prov(wifi_http_post_prov_mode_t mode)
{
    switch (mode) {
        case WIFI_HTTP_FULL:
            // No-op: keep all endpoints active
            break;

        case WIFI_HTTP_API_ONLY:
            // Remove provisioning UI endpoints, keep API
            wifi_cfg_http_unregister_provisioning_handlers();
            break;

        case WIFI_HTTP_DISABLED:
            // Remove all library-registered endpoints
            wifi_cfg_http_unregister_provisioning_handlers();
            wifi_cfg_http_unregister_api_handlers();
            break;
    }
}

// =============================================================================
// Public: wifi_cfg_stop_http()
// =============================================================================

esp_err_t wifi_cfg_stop_http(void)
{
    if (!g_wifi_cfg || !g_wifi_cfg->httpd) return ESP_ERR_INVALID_STATE;

    // Refuse if provisioning is active
    if (g_wifi_cfg->provisioning_active) {
        ESP_LOGW(TAG, "Cannot stop HTTP while provisioning is active");
        return ESP_ERR_INVALID_STATE;
    }

    // Refuse if reconnect constraint applies: AP may need to restart
    if (g_wifi_cfg->config.enable_ap &&
        g_wifi_cfg->config.on_reconnect_exhausted == WIFI_ON_RECONNECT_EXHAUSTED_PROVISION &&
        g_wifi_cfg->config.max_reconnect_attempts > 0) {
        ESP_LOGW(TAG, "Cannot stop HTTP: reconnect constraint requires server for AP restart");
        return ESP_ERR_INVALID_STATE;
    }

    // Only stop if library owns the server
    if (!g_wifi_cfg->httpd_owned) {
        ESP_LOGW(TAG, "Cannot stop HTTP: server not owned by library");
        return ESP_ERR_INVALID_STATE;
    }

    // Unregister all handlers first
    wifi_cfg_http_unregister_provisioning_handlers();
    wifi_cfg_http_unregister_api_handlers();

    httpd_stop(g_wifi_cfg->httpd);
    g_wifi_cfg->httpd = NULL;
    g_wifi_cfg->httpd_owned = false;
    ESP_LOGI(TAG, "HTTP server stopped by user request");
    return ESP_OK;
}

// =============================================================================
// Init / Deinit
// =============================================================================

esp_err_t wifi_cfg_http_init(void)
{
    if (!g_wifi_cfg) return ESP_ERR_INVALID_STATE;

    const char *base = g_wifi_cfg->config.http.api_base_path;
    if (!base) base = "/api/wifi";

    ESP_LOGI(TAG, "Initializing HTTP interface at %s", base);

    // Create httpd if not provided
    if (!g_wifi_cfg->httpd) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.max_uri_handlers = WIFI_CFG_HTTP_MAX_HANDLERS;
        /* Without this, the 8th concurrent connection is accepted at the TCP
         * layer and then never answered -- the client waits out its own
         * timeout while the device looks healthy. `max_open_sockets` defaults
         * to 7, and a browser alone opens up to six to one origin, so the
         * ordinary case of the Web UI plus an OS captive-portal probe is
         * already at the limit. Purging the least-recently-used connection to
         * make room is the behaviour almost every embedded server wants, and
         * turns "the next client hangs forever" into "the oldest idle one is
         * dropped". */
        config.lru_purge_enable = true;

        esp_err_t ret = httpd_start(&g_wifi_cfg->httpd, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start httpd: %s", esp_err_to_name(ret));
            return ret;
        }
        g_wifi_cfg->httpd_owned = true;
        ESP_LOGI(TAG, "HTTP server started");
    } else {
        g_wifi_cfg->httpd_owned = false;
    }

    ESP_LOGI(TAG, "HTTP server ready (handlers deferred)");
    return ESP_OK;
}

esp_err_t wifi_cfg_http_unregister_handlers(void)
{
    if (!g_wifi_cfg || !g_wifi_cfg->httpd) return ESP_ERR_INVALID_STATE;

    wifi_cfg_http_unregister_provisioning_handlers();
    wifi_cfg_http_unregister_api_handlers();

    ESP_LOGI(TAG, "All HTTP handlers unregistered");
    return ESP_OK;
}

esp_err_t wifi_cfg_http_deinit(void)
{
    if (!g_wifi_cfg) return ESP_ERR_INVALID_STATE;

    if (g_wifi_cfg->httpd) {
        wifi_cfg_http_unregister_handlers();  // Always unregister handlers

        if (g_wifi_cfg->httpd_owned) {
            // Also stop and delete httpd if we created (own) it
            httpd_stop(g_wifi_cfg->httpd);
            g_wifi_cfg->httpd = NULL;
            g_wifi_cfg->httpd_owned = false;
            ESP_LOGI(TAG, "Owned HTTP server stopped");
        }
    }

    return ESP_OK;
}

