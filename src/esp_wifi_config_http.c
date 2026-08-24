/**
 * @file esp_wifi_config_http.c
 * @brief HTTP REST API for WiFi Config
 */

#include "esp_wifi_config_priv.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_wifi_config_json.h"
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

/*
 * Responses stream instead of being built as a tree and serialised.
 *
 * The old path called cJSON_PrintUnformatted(), which allocated a second
 * buffer on top of the node tree -- about 6.4 KB of peak heap for a 20-result
 * /scan -- and turned an allocation failure into a 500. This writes through a
 * 256-byte scratch buffer on the handler's stack and flushes as it fills.
 *
 * One consequence worth knowing: the response is chunked, so the status line
 * and headers go out before the body is built. A failure partway through can
 * no longer become a 500; all it can do is end the response early, which the
 * client sees as a truncated body. That is acceptable here only because the
 * writer cannot fail for the reason the old code could -- it does not allocate.
 */
static esp_err_t json_chunk_sink(void *ctx, const char *data, size_t len)
{
    return httpd_resp_send_chunk((httpd_req_t *)ctx, data, len);
}

static void json_response_begin(httpd_req_t *req, wcfg_json_w *w,
                                char *scratch, size_t cap)
{
    add_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    wcfg_json_init(w, scratch, cap, json_chunk_sink, req);
}

static esp_err_t json_response_end(httpd_req_t *req, wcfg_json_w *w)
{
    esp_err_t err = wcfg_json_finish(w);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "json response failed: %s", esp_err_to_name(err));
    }
    /* Terminating zero-length chunk, sent either way: without it the client
     * waits for a continuation that is never coming. */
    esp_err_t sent = httpd_resp_send_chunk(req, NULL, 0);
    return (err == ESP_OK) ? sent : ESP_FAIL;
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
                /* Clamped at zero. An unbalanced prefix like `]]]` would
                 * otherwise drive the counter negative and hand the rest of
                 * the body that much headroom above the limit. cJSON would
                 * reject such a document before recursing far, so this is
                 * belt rather than braces -- but the invariant "depth is the
                 * nesting seen so far" should not depend on that argument. */
                if (depth > 0) {
                    depth--;
                }
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
    
    char scratch[WCFG_JSON_SCRATCH_SIZE];
    wcfg_json_w w;
    json_response_begin(req, &w, scratch, sizeof(scratch));

    wcfg_json_obj_open(&w, NULL);
    wcfg_json_str(&w, "state",
        status.state == WIFI_STATE_CONNECTED ? "connected" :
        status.state == WIFI_STATE_CONNECTING ? "connecting" : "disconnected");
    wcfg_json_str(&w, "ssid", status.ssid);
    wcfg_json_int(&w, "rssi", status.rssi);
    wcfg_json_int(&w, "quality", status.quality);
    wcfg_json_int(&w, "channel", status.channel);
    wcfg_json_str(&w, "ip", status.ip);
    wcfg_json_str(&w, "netmask", status.netmask);
    wcfg_json_str(&w, "gateway", status.gateway);
    wcfg_json_str(&w, "dns", status.dns);
    wcfg_json_str(&w, "mac", status.mac);
    wcfg_json_str(&w, "hostname", status.hostname);
    /* uint32_t, so it outgrows int32 after ~24.8 days of uptime. int64 keeps
     * it exact; cJSON used to reach its %g branch here and print the same
     * digits by a longer route. */
    wcfg_json_int(&w, "uptime_ms", (int64_t)status.uptime_ms);
    wcfg_json_bool(&w, "ap_active", status.ap_active);
    wcfg_json_obj_close(&w);

    return json_response_end(req, &w);
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
    
    char scratch[WCFG_JSON_SCRATCH_SIZE];
    wcfg_json_w w;
    json_response_begin(req, &w, scratch, sizeof(scratch));

    wcfg_json_obj_open(&w, NULL);
    wcfg_json_arr_open(&w, "networks");

    for (size_t i = 0; i < count; i++) {
        wcfg_json_obj_open(&w, NULL);
        wcfg_json_str(&w, "ssid", results[i].ssid);
        wcfg_json_int(&w, "rssi", results[i].rssi);
        
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
        wcfg_json_str(&w, "auth", auth_str);
        wcfg_json_obj_close(&w);
    }

    wcfg_json_arr_close(&w);
    wcfg_json_obj_close(&w);
    return json_response_end(req, &w);
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
    
    char scratch[WCFG_JSON_SCRATCH_SIZE];
    wcfg_json_w w;
    json_response_begin(req, &w, scratch, sizeof(scratch));

    wcfg_json_obj_open(&w, NULL);
    wcfg_json_arr_open(&w, "networks");
    for (size_t i = 0; i < count; i++) {
        wcfg_json_obj_open(&w, NULL);
        wcfg_json_str(&w, "ssid", networks[i].ssid);
        wcfg_json_int(&w, "priority", networks[i].priority);
        wcfg_json_obj_close(&w);
    }
    wcfg_json_arr_close(&w);
    wcfg_json_obj_close(&w);

    return json_response_end(req, &w);
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
    
    char scratch[WCFG_JSON_SCRATCH_SIZE];
    wcfg_json_w w;
    json_response_begin(req, &w, scratch, sizeof(scratch));

    wcfg_json_obj_open(&w, NULL);
    wcfg_json_bool(&w, "active", status.active);
    wcfg_json_str(&w, "ssid", status.ssid);
    wcfg_json_str(&w, "ip", status.ip);
    wcfg_json_int(&w, "channel", status.channel);
    wcfg_json_int(&w, "sta_count", status.sta_count);

    wcfg_json_arr_open(&w, "clients");
    for (int i = 0; i < status.sta_count && i < 4; i++) {
        wcfg_json_obj_open(&w, NULL);
        wcfg_json_str(&w, "mac", status.clients[i].mac);
        wcfg_json_str(&w, "ip", status.clients[i].ip);
        wcfg_json_obj_close(&w);
    }
    wcfg_json_arr_close(&w);
    wcfg_json_obj_close(&w);

    return json_response_end(req, &w);
}

// GET /ap/config
static esp_err_t handler_get_ap_config(httpd_req_t *req)
{
    if (!check_api_access(req)) {
        return ESP_OK;   /* the 401/403 was sent; see send_error() */
    }
    
    wifi_cfg_ap_config_t config;
    wifi_cfg_get_ap_config(&config);
    
    char scratch[WCFG_JSON_SCRATCH_SIZE];
    wcfg_json_w w;
    json_response_begin(req, &w, scratch, sizeof(scratch));

    wcfg_json_obj_open(&w, NULL);
    wcfg_json_str(&w, "ssid", config.ssid);
    wcfg_json_str(&w, "password", config.password);
    wcfg_json_int(&w, "channel", config.channel);
    wcfg_json_int(&w, "max_connections", config.max_connections);
    wcfg_json_bool(&w, "hidden", config.hidden);
    wcfg_json_str(&w, "ip", config.ip);
    wcfg_json_str(&w, "netmask", config.netmask);
    wcfg_json_str(&w, "gateway", config.gateway);
    wcfg_json_str(&w, "dhcp_start", config.dhcp_start);
    wcfg_json_str(&w, "dhcp_end", config.dhcp_end);
    wcfg_json_obj_close(&w);

    return json_response_end(req, &w);
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
    
    /* Snapshot under the lock, then stream without it.
     *
     * Streaming straight out of g_wifi_cfg->vars[] would mean holding the
     * config lock across socket writes, so a stalled client could block the
     * manager task. The old code did not do that -- it built the whole tree
     * under the lock and sent afterwards -- and this must not regress it.
     *
     * The snapshot is heap rather than stack: wifi_var_t is 160 bytes and
     * WIFI_CFG_MAX_VARS defaults to 10, which is 1.6 KB on a 4 KB httpd stack.
     * One bounded allocation, freed immediately, still replaces the ~6 KB of
     * cJSON nodes plus serialisation buffer this handler used to make. */
    wifi_cfg_lock();
    size_t count = g_wifi_cfg->var_count;
    wifi_var_t *snapshot = NULL;
    if (count > 0) {
        snapshot = malloc(count * sizeof(wifi_var_t));
        if (snapshot == NULL) {
            wifi_cfg_unlock();
            return send_error(req, 500, "Out of memory");
        }
        memcpy(snapshot, g_wifi_cfg->vars, count * sizeof(wifi_var_t));
    }
    wifi_cfg_unlock();

    char scratch[WCFG_JSON_SCRATCH_SIZE];
    wcfg_json_w w;
    json_response_begin(req, &w, scratch, sizeof(scratch));

    wcfg_json_obj_open(&w, NULL);
    wcfg_json_arr_open(&w, "vars");
    for (size_t i = 0; i < count; i++) {
        wcfg_json_obj_open(&w, NULL);
        wcfg_json_str(&w, "key", snapshot[i].key);
        wcfg_json_str(&w, "value", snapshot[i].value);
        wcfg_json_obj_close(&w);
    }
    wcfg_json_arr_close(&w);
    wcfg_json_obj_close(&w);

    free(snapshot);
    return json_response_end(req, &w);
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
    "button:not(:disabled):hover{background:#2563eb}"
    "button:disabled{background:#cbd5e1;color:#64748b;cursor:not-allowed}"
    ".nets{margin:15px 0}"
    ".net{padding:10px;background:#f8fafc;margin:5px 0;border-radius:6px;cursor:pointer}"
    ".net:hover{background:#e2e8f0}"
    ".msg{padding:10px;border-radius:6px;margin:10px 0}"
    ".ok{background:#d1fae5;color:#065f46}"
    ".err{background:#fee2e2;color:#991b1b}"
    "</style></head><body>"
    "<div class='c'><h1>ESP32 WiFi Setup</h1>"
    "<div id='nets' class='nets'><p>Loading networks...</p></div>"
    "<button id='scanBtn' onclick='scan()'>Scan Networks</button>"
    "<hr style='margin:20px 0;border:none;border-top:1px solid #eee'>"
    "<div id='msg'></div>"
    "<input id='ssid' placeholder='WiFi Name (SSID)'>"
    "<input id='pass' type='password' placeholder='Password'>"
    "<button id='connectBtn' onclick='connect()'>Connect</button>"
    "</div>"
    "<script>"
    "const API='/api/wifi';"
    "function msg(t,ok){document.getElementById('msg').innerHTML='<div class=\"msg '+(ok?'ok':'err')+'\">'+t+'</div>';}"
    "function busy(b){const c=document.getElementById('connectBtn');"
    "c.disabled=b;c.textContent=b?'Connecting...':'Connect';"
    "document.getElementById('scanBtn').disabled=b;}"
    "async function scan(){"
    "document.getElementById('nets').innerHTML='<p>Scanning...</p>';"
    "try{const r=await fetch(API+'/scan');const d=await r.json();"
    "let h='';d.networks.forEach(n=>{"
    "h+='<div class=\"net\" onclick=\"document.getElementById(\\'ssid\\').value=\\''+n.ssid+'\\'\">'+"
    "n.ssid+' ('+n.rssi+' dBm)</div>';});"
    "document.getElementById('nets').innerHTML=h||'<p>No networks</p>';"
    "}catch(e){document.getElementById('nets').innerHTML='<p>Scan failed</p>';}}"
    /* Poll /status so 'Connecting...' ends by itself: on success, on a refused
       association, or after the timeout -- a button that greys out forever is
       worse than one that was never disabled. A fetch that throws is the AP
       flapping as the STA joins, not a verdict, so it is ignored. */
    "async function watch(s){"
    "for(let i=0;i<20;i++){"
    "await new Promise(r=>setTimeout(r,1500));"
    "try{const d=await(await fetch(API+'/status')).json();"
    "if(d.state==='connected'){msg('Connected to '+d.ssid+(d.ip?' ('+d.ip+')':''),1);busy(false);return;}"
    "if(i>2&&d.state==='disconnected'){"
    "msg('Could not connect to '+s+'. Check the password and try again.',0);busy(false);return;}"
    "}catch(e){}}"
    "msg('Still connecting to '+s+'. Reload this page to check.',0);busy(false);}"
    "async function connect(){"
    "const s=document.getElementById('ssid').value;"
    "const p=document.getElementById('pass').value;"
    "if(!s){msg('Enter SSID',0);return;}"
    "busy(true);"
    "try{await fetch(API+'/networks',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:s,password:p,priority:10})});"
    "await fetch(API+'/connect',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:s})});"
    "msg('Connecting to '+s+'...',1);"
    "watch(s);"
    "}catch(e){msg('Error: '+e,0);busy(false);}}"
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

/**
 * @brief Register one URI, and remember if the server would not take it.
 *
 * httpd_register_uri_handler() fails with ESP_ERR_HTTPD_HANDLERS_FULL once the
 * server's max_uri_handlers is used up, and these twenty calls used to discard
 * that. On a server the library created it cannot happen -- it sizes its own.
 * On a server the *application* passed in it happens easily:
 * HTTPD_DEFAULT_CONFIG() allows eight and these routes alone need twenty.
 *
 * The old behaviour was the bad kind of quiet. Registration stopped partway,
 * the device answered /api/wifi/status and 404'd /api/wifi/scan, and nothing
 * anywhere said why -- which reads as a library that half works rather than a
 * server that ran out of room. Counted rather than logged per call, so the one
 * cause produces one message instead of twelve.
 */
static int s_uri_reg_failures;

static void register_uri(const httpd_uri_t *uri)
{
    esp_err_t err = httpd_register_uri_handler(g_wifi_cfg->httpd, uri);
    if (err != ESP_OK) {
        if (s_uri_reg_failures == 0) {
            ESP_LOGE(TAG, "cannot register %s: %s", uri->uri, esp_err_to_name(err));
        }
        s_uri_reg_failures++;
    }
}

esp_err_t wifi_cfg_http_register_api_handlers(void)
{
    if (!g_wifi_cfg || !g_wifi_cfg->httpd) return ESP_ERR_INVALID_STATE;

    const char *base = g_wifi_cfg->config.http.api_base_path;
    if (!base) base = "/api/wifi";

    // Status
    snprintf(uri_status, sizeof(uri_status), "%s/status", base);
    httpd_uri_t status_uri = { .uri = uri_status, .method = HTTP_GET, .handler = handler_get_status };
    register_uri(&status_uri);

    // Scan
    snprintf(uri_scan, sizeof(uri_scan), "%s/scan", base);
    httpd_uri_t scan_uri = { .uri = uri_scan, .method = HTTP_GET, .handler = handler_get_scan };
    register_uri(&scan_uri);

    // Networks
    snprintf(uri_networks, sizeof(uri_networks), "%s/networks", base);
    httpd_uri_t networks_get_uri = { .uri = uri_networks, .method = HTTP_GET, .handler = handler_get_networks };
    httpd_uri_t networks_post_uri = { .uri = uri_networks, .method = HTTP_POST, .handler = handler_post_networks };
    register_uri(&networks_get_uri);
    register_uri(&networks_post_uri);

    // Update/Delete network - wildcard
    snprintf(uri_networks_wildcard, sizeof(uri_networks_wildcard), "%s/networks/*", base);
    httpd_uri_t networks_put_uri = { .uri = uri_networks_wildcard, .method = HTTP_PUT, .handler = handler_put_network };
    httpd_uri_t networks_del_uri = { .uri = uri_networks_wildcard, .method = HTTP_DELETE, .handler = handler_delete_network };
    register_uri(&networks_put_uri);
    register_uri(&networks_del_uri);

    // Connect/Disconnect
    snprintf(uri_connect, sizeof(uri_connect), "%s/connect", base);
    httpd_uri_t connect_uri = { .uri = uri_connect, .method = HTTP_POST, .handler = handler_post_connect };
    register_uri(&connect_uri);

    snprintf(uri_disconnect, sizeof(uri_disconnect), "%s/disconnect", base);
    httpd_uri_t disconnect_uri = { .uri = uri_disconnect, .method = HTTP_POST, .handler = handler_post_disconnect };
    register_uri(&disconnect_uri);

    // AP
    snprintf(uri_ap_status, sizeof(uri_ap_status), "%s/ap/status", base);
    httpd_uri_t ap_status_uri = { .uri = uri_ap_status, .method = HTTP_GET, .handler = handler_get_ap_status };
    register_uri(&ap_status_uri);

    snprintf(uri_ap_config, sizeof(uri_ap_config), "%s/ap/config", base);
    httpd_uri_t ap_config_get_uri = { .uri = uri_ap_config, .method = HTTP_GET, .handler = handler_get_ap_config };
    httpd_uri_t ap_config_put_uri = { .uri = uri_ap_config, .method = HTTP_PUT, .handler = handler_put_ap_config };
    register_uri(&ap_config_get_uri);
    register_uri(&ap_config_put_uri);

    snprintf(uri_ap_start, sizeof(uri_ap_start), "%s/ap/start", base);
    httpd_uri_t ap_start_uri = { .uri = uri_ap_start, .method = HTTP_POST, .handler = handler_post_ap_start };
    register_uri(&ap_start_uri);

    snprintf(uri_ap_stop, sizeof(uri_ap_stop), "%s/ap/stop", base);
    httpd_uri_t ap_stop_uri = { .uri = uri_ap_stop, .method = HTTP_POST, .handler = handler_post_ap_stop };
    register_uri(&ap_stop_uri);

    // Vars
    snprintf(uri_vars, sizeof(uri_vars), "%s/vars", base);
    httpd_uri_t vars_uri = { .uri = uri_vars, .method = HTTP_GET, .handler = handler_get_vars };
    register_uri(&vars_uri);

    snprintf(uri_vars_wildcard, sizeof(uri_vars_wildcard), "%s/vars/*", base);
    httpd_uri_t vars_put_uri = { .uri = uri_vars_wildcard, .method = HTTP_PUT, .handler = handler_put_var };
    httpd_uri_t vars_del_uri = { .uri = uri_vars_wildcard, .method = HTTP_DELETE, .handler = handler_delete_var };
    register_uri(&vars_put_uri);
    register_uri(&vars_del_uri);

    // Factory reset
    snprintf(uri_factory_reset, sizeof(uri_factory_reset), "%s/factory_reset", base);
    httpd_uri_t factory_reset_uri = { .uri = uri_factory_reset, .method = HTTP_POST, .handler = handler_post_factory_reset };
    register_uri(&factory_reset_uri);

    // OPTIONS handler for CORS preflight (catch-all)
    snprintf(uri_options_wildcard, sizeof(uri_options_wildcard), "%s/*", base);
    httpd_uri_t options_uri = { .uri = uri_options_wildcard, .method = HTTP_OPTIONS, .handler = handler_options };
    register_uri(&options_uri);

    g_wifi_cfg->http_handlers_registered = true;

    if (s_uri_reg_failures > 0) {
        ESP_LOGE(TAG, "%d of the API's routes could not be registered. The "
                      "server is out of URI slots: this library needs about "
                      "22 and HTTPD_DEFAULT_CONFIG() allows 8. Raise "
                      "max_uri_handlers on the httpd you passed in "
                      "config.http.httpd -- the endpoints that did not fit "
                      "will answer 404 until you do.",
                 s_uri_reg_failures);
        s_uri_reg_failures = 0;
        return ESP_ERR_NO_MEM;
    }

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
        register_uri(&captive_uri);
    }

    // Web UI or simple fallback page
#ifdef CONFIG_WIFI_CFG_ENABLE_WEBUI
    wifi_cfg_webui_init(g_wifi_cfg->httpd);
#else
    httpd_uri_t simple_uri = { .uri = "/", .method = HTTP_GET, .handler = handler_simple_page };
    register_uri(&simple_uri);
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

