/**
 * @file esp_wifi_manager_http.c
 * @brief HTTP REST API for WiFi Manager
 */

#include "esp_wifi_manager_priv.h"
#include "esp_log.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include <string.h>

static const char *TAG = "wifi_mgr_http";

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
    if (!g_wifi_mgr->config.http.enable_auth) {
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
    return (strcmp(username, g_wifi_mgr->auth_username) == 0 &&
            strcmp(password, g_wifi_mgr->auth_password) == 0);
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
    return ESP_FAIL;
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

static cJSON *read_json_body(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > WIFI_MGR_HTTP_MAX_CONTENT) {
        return NULL;
    }
    
    char *buf = malloc(content_len + 1);
    if (!buf) return NULL;
    
    int ret = httpd_req_recv(req, buf, content_len);
    if (ret <= 0) {
        free(buf);
        return NULL;
    }
    buf[ret] = '\0';
    
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
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    wifi_status_t status;
    wifi_manager_get_status(&status);
    
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
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    wifi_scan_result_t results[WIFI_MGR_MAX_SCAN_RESULTS];
    size_t count = 0;

    esp_err_t ret = wifi_manager_scan(results, WIFI_MGR_MAX_SCAN_RESULTS, &count);
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
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    wifi_network_t networks[WIFI_MGR_MAX_NETWORKS];
    size_t count = 0;
    wifi_manager_list_networks(networks, WIFI_MGR_MAX_NETWORKS, &count);
    
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
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
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
    
    esp_err_t ret = wifi_manager_add_network(&network);
    if (ret != ESP_OK) {
        return send_error(req, 400, ret == ESP_ERR_INVALID_STATE ? "Already exists" : "Failed");
    }
    
    return send_ok(req);
}

// DELETE /networks/:ssid
static esp_err_t handler_delete_network(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
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
    
    esp_err_t ret = wifi_manager_remove_network(ssid);
    if (ret == ESP_ERR_NOT_FOUND) {
        return send_error(req, 404, "Not found");
    }
    
    return send_ok(req);
}

// PUT /networks/:ssid - Update network (password, priority)
static esp_err_t handler_put_network(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
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
    
    esp_err_t ret = wifi_manager_update_network(&network);
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
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    if (req->content_len > 0) {
        cJSON *json = read_json_body(req);
        if (json) {
            cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
            if (cJSON_IsString(ssid_item)) {
                wifi_manager_connect(ssid_item->valuestring);
                cJSON_Delete(json);
                return send_ok(req);
            }
            cJSON_Delete(json);
        }
    }
    
    wifi_manager_connect(NULL);
    return send_ok(req);
}

// POST /disconnect
static esp_err_t handler_post_disconnect(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    wifi_manager_disconnect();
    return send_ok(req);
}

// GET /ap/status
static esp_err_t handler_get_ap_status(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    wifi_ap_status_t status;
    wifi_manager_get_ap_status(&status);
    
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
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    wifi_mgr_ap_config_t config;
    wifi_manager_get_ap_config(&config);
    
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
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    cJSON *json = read_json_body(req);
    if (!json) {
        return send_error(req, 400, "Invalid JSON");
    }
    
    wifi_mgr_ap_config_t config;
    wifi_manager_get_ap_config(&config);
    
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
    
    wifi_manager_set_ap_config(&config);
    return send_ok(req);
}

// POST /ap/start
static esp_err_t handler_post_ap_start(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    wifi_mgr_ap_config_t *config = NULL;
    wifi_mgr_ap_config_t temp_config;
    
    if (req->content_len > 0) {
        cJSON *json = read_json_body(req);
        if (json) {
            wifi_manager_get_ap_config(&temp_config);
            
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
    
    wifi_manager_start_ap(config);
    return send_ok(req);
}

// POST /ap/stop
static esp_err_t handler_post_ap_stop(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    wifi_manager_stop_ap();
    return send_ok(req);
}

// GET /vars
static esp_err_t handler_get_vars(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }
    
    wifi_mgr_lock();
    
    cJSON *json = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(json, "vars");
    
    for (size_t i = 0; i < g_wifi_mgr->var_count; i++) {
        cJSON *var = cJSON_CreateObject();
        cJSON_AddStringToObject(var, "key", g_wifi_mgr->vars[i].key);
        cJSON_AddStringToObject(var, "value", g_wifi_mgr->vars[i].value);
        cJSON_AddItemToArray(arr, var);
    }
    
    wifi_mgr_unlock();
    
    esp_err_t ret = send_json_response(req, json);
    cJSON_Delete(json);
    return ret;
}

// PUT /vars/:key
static esp_err_t handler_put_var(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
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
    
    wifi_manager_set_var(key, value->valuestring);
    cJSON_Delete(json);
    
    return send_ok(req);
}

// DELETE /vars/:key
static esp_err_t handler_delete_var(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
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

    esp_err_t ret = wifi_manager_del_var(key);
    if (ret == ESP_ERR_NOT_FOUND) {
        return send_error(req, 404, "Not found");
    }

    return send_ok(req);
}

// POST /factory_reset
static esp_err_t handler_post_factory_reset(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return send_error(req, 401, "Unauthorized");
    }

    esp_err_t ret = wifi_manager_factory_reset();
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
    if (g_wifi_mgr->ap_config.ip[0]) {
        snprintf(redirect_url, sizeof(redirect_url), "http://%s/", g_wifi_mgr->ap_config.ip);
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

esp_err_t wifi_mgr_http_init(void)
{
    if (!g_wifi_mgr) return ESP_ERR_INVALID_STATE;

    const char *base = g_wifi_mgr->config.http.api_base_path;
    if (!base) base = "/api/wifi";

    ESP_LOGI(TAG, "Initializing HTTP interface at %s", base);

    // Create httpd if not provided
    if (!g_wifi_mgr->httpd) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.max_uri_handlers = WIFI_MGR_HTTP_MAX_HANDLERS;  // Default 32: API(18) + WebUI(3) + Captive(8) + reserve

        esp_err_t ret = httpd_start(&g_wifi_mgr->httpd, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start httpd: %s", esp_err_to_name(ret));
            return ret;
        }
        g_wifi_mgr->httpd_owned = true;
        ESP_LOGI(TAG, "HTTP server started");
    } else {
        g_wifi_mgr->httpd_owned = false;
    }
    
    // Register handlers with static URI strings

    // Status
    snprintf(uri_status, sizeof(uri_status), "%s/status", base);
    httpd_uri_t status_uri = { .uri = uri_status, .method = HTTP_GET, .handler = handler_get_status };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &status_uri);

    // Scan
    snprintf(uri_scan, sizeof(uri_scan), "%s/scan", base);
    httpd_uri_t scan_uri = { .uri = uri_scan, .method = HTTP_GET, .handler = handler_get_scan };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &scan_uri);

    // Networks
    snprintf(uri_networks, sizeof(uri_networks), "%s/networks", base);
    httpd_uri_t networks_get_uri = { .uri = uri_networks, .method = HTTP_GET, .handler = handler_get_networks };
    httpd_uri_t networks_post_uri = { .uri = uri_networks, .method = HTTP_POST, .handler = handler_post_networks };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &networks_get_uri);
    httpd_register_uri_handler(g_wifi_mgr->httpd, &networks_post_uri);

    // Update/Delete network - wildcard
    snprintf(uri_networks_wildcard, sizeof(uri_networks_wildcard), "%s/networks/*", base);
    httpd_uri_t networks_put_uri = { .uri = uri_networks_wildcard, .method = HTTP_PUT, .handler = handler_put_network };
    httpd_uri_t networks_del_uri = { .uri = uri_networks_wildcard, .method = HTTP_DELETE, .handler = handler_delete_network };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &networks_put_uri);
    httpd_register_uri_handler(g_wifi_mgr->httpd, &networks_del_uri);

    // Connect/Disconnect
    snprintf(uri_connect, sizeof(uri_connect), "%s/connect", base);
    httpd_uri_t connect_uri = { .uri = uri_connect, .method = HTTP_POST, .handler = handler_post_connect };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &connect_uri);

    snprintf(uri_disconnect, sizeof(uri_disconnect), "%s/disconnect", base);
    httpd_uri_t disconnect_uri = { .uri = uri_disconnect, .method = HTTP_POST, .handler = handler_post_disconnect };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &disconnect_uri);

    // AP
    snprintf(uri_ap_status, sizeof(uri_ap_status), "%s/ap/status", base);
    httpd_uri_t ap_status_uri = { .uri = uri_ap_status, .method = HTTP_GET, .handler = handler_get_ap_status };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &ap_status_uri);

    snprintf(uri_ap_config, sizeof(uri_ap_config), "%s/ap/config", base);
    httpd_uri_t ap_config_get_uri = { .uri = uri_ap_config, .method = HTTP_GET, .handler = handler_get_ap_config };
    httpd_uri_t ap_config_put_uri = { .uri = uri_ap_config, .method = HTTP_PUT, .handler = handler_put_ap_config };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &ap_config_get_uri);
    httpd_register_uri_handler(g_wifi_mgr->httpd, &ap_config_put_uri);

    snprintf(uri_ap_start, sizeof(uri_ap_start), "%s/ap/start", base);
    httpd_uri_t ap_start_uri = { .uri = uri_ap_start, .method = HTTP_POST, .handler = handler_post_ap_start };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &ap_start_uri);

    snprintf(uri_ap_stop, sizeof(uri_ap_stop), "%s/ap/stop", base);
    httpd_uri_t ap_stop_uri = { .uri = uri_ap_stop, .method = HTTP_POST, .handler = handler_post_ap_stop };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &ap_stop_uri);

    // Vars
    snprintf(uri_vars, sizeof(uri_vars), "%s/vars", base);
    httpd_uri_t vars_uri = { .uri = uri_vars, .method = HTTP_GET, .handler = handler_get_vars };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &vars_uri);

    snprintf(uri_vars_wildcard, sizeof(uri_vars_wildcard), "%s/vars/*", base);
    httpd_uri_t vars_put_uri = { .uri = uri_vars_wildcard, .method = HTTP_PUT, .handler = handler_put_var };
    httpd_uri_t vars_del_uri = { .uri = uri_vars_wildcard, .method = HTTP_DELETE, .handler = handler_delete_var };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &vars_put_uri);
    httpd_register_uri_handler(g_wifi_mgr->httpd, &vars_del_uri);

    // Factory reset
    snprintf(uri_factory_reset, sizeof(uri_factory_reset), "%s/factory_reset", base);
    httpd_uri_t factory_reset_uri = { .uri = uri_factory_reset, .method = HTTP_POST, .handler = handler_post_factory_reset };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &factory_reset_uri);

    // OPTIONS handler for CORS preflight (catch-all)
    snprintf(uri_options_wildcard, sizeof(uri_options_wildcard), "%s/*", base);
    httpd_uri_t options_uri = { .uri = uri_options_wildcard, .method = HTTP_OPTIONS, .handler = handler_options };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &options_uri);

    // Initialize Web UI if enabled, otherwise use simple fallback page
#ifdef CONFIG_WIFI_MGR_ENABLE_WEBUI
    wifi_mgr_webui_init(g_wifi_mgr->httpd);
#else
    // Simple fallback page at root when Web UI not enabled
    httpd_uri_t simple_uri = { .uri = "/", .method = HTTP_GET, .handler = handler_simple_page };
    httpd_register_uri_handler(g_wifi_mgr->httpd, &simple_uri);
    ESP_LOGI(TAG, "Simple setup page registered at /");
#endif

    // Captive portal detection handlers (for specific OS detection paths)
    if (g_wifi_mgr->config.enable_captive_portal) {
        for (int i = 0; captive_detect_paths[i] != NULL; i++) {
            httpd_uri_t captive_uri = {
                .uri = captive_detect_paths[i],
                .method = HTTP_GET,
                .handler = handler_captive_detect
            };
            httpd_register_uri_handler(g_wifi_mgr->httpd, &captive_uri);
        }
        ESP_LOGI(TAG, "Captive portal detection enabled");
    }

    ESP_LOGI(TAG, "HTTP handlers registered");
    return ESP_OK;
}

esp_err_t wifi_mgr_http_unregister_handlers(void)
{
    if (!g_wifi_mgr || !g_wifi_mgr->httpd) return ESP_ERR_INVALID_STATE;

    httpd_handle_t httpd = g_wifi_mgr->httpd;

    // Unregister API handlers initialized in wifi_mgr_http_init
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

    // Unregister Web UI or simple page
#ifdef CONFIG_WIFI_MGR_ENABLE_WEBUI
    // Unregister Web UI handlers initialized in wifi_mgr_webui_init
    httpd_unregister_uri_handler(httpd, "/", HTTP_GET);
    httpd_unregister_uri_handler(httpd, "/assets/app.js", HTTP_GET);
    httpd_unregister_uri_handler(httpd, "/assets/index.css", HTTP_GET);
#else
    httpd_unregister_uri_handler(httpd, "/", HTTP_GET);
#endif

    // Unregister captive portal detection handlers
    if (g_wifi_mgr->config.enable_captive_portal) {
        for (int i = 0; captive_detect_paths[i] != NULL; i++) {
            httpd_unregister_uri_handler(httpd, captive_detect_paths[i], HTTP_GET);
        }
    }

    ESP_LOGI(TAG, "HTTP handlers unregistered");
    return ESP_OK;
}

esp_err_t wifi_mgr_http_deinit(void)
{
    if (!g_wifi_mgr) return ESP_ERR_INVALID_STATE;
    
    if (g_wifi_mgr->httpd) {
        if(g_wifi_mgr->httpd_owned) {
            // Stop and delete httpd if we created (own) it
            httpd_stop(g_wifi_mgr->httpd);
            g_wifi_mgr->httpd = NULL;
            g_wifi_mgr->httpd_owned = false;
            ESP_LOGI(TAG, "Owned HTTP server stopped");
        } else {
            // Just unregister handlers if httpd not owned
            wifi_mgr_http_unregister_handlers();
        }
    }
    
    return ESP_OK;
}

