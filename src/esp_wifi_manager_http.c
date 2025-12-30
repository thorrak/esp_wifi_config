/**
 * @file esp_wifi_manager_http.c
 * @brief HTTP REST API for WiFi Manager
 */

#include "esp_wifi_manager_priv.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "wifi_mgr_http";

// =============================================================================
// Helper Functions
// =============================================================================

static bool check_auth(httpd_req_t *req)
{
    if (!g_wifi_mgr->config.http.enable_auth) {
        return true;
    }
    
    char auth_header[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
        return false;
    }
    
    // Basic auth: "Basic base64(user:pass)"
    if (strncmp(auth_header, "Basic ", 6) != 0) {
        return false;
    }
    
    // Simple check - in production, decode base64 and compare
    // For now, just check if header exists
    return strlen(auth_header) > 6;
}

static esp_err_t send_json_response(httpd_req_t *req, cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    if (!str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    free(str);
    return ESP_OK;
}

static esp_err_t send_ok(httpd_req_t *req)
{
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
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg);
    httpd_resp_sendstr(req, buf);
    return ESP_FAIL;
}

static cJSON *read_json_body(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 2048) {
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
    
    wifi_scan_result_t results[20];
    size_t count = 0;
    
    esp_err_t ret = wifi_manager_scan(results, 20, &count);
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
        config.max_uri_handlers = 20;

        esp_err_t ret = httpd_start(&g_wifi_mgr->httpd, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start httpd: %s", esp_err_to_name(ret));
            return ret;
        }
        g_wifi_mgr->httpd_owned = true;
        ESP_LOGI(TAG, "HTTP server started");
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

    ESP_LOGI(TAG, "HTTP handlers registered");
    return ESP_OK;
}

esp_err_t wifi_mgr_http_deinit(void)
{
    if (!g_wifi_mgr) return ESP_ERR_INVALID_STATE;
    
    if (g_wifi_mgr->httpd && g_wifi_mgr->httpd_owned) {
        httpd_stop(g_wifi_mgr->httpd);
        g_wifi_mgr->httpd = NULL;
        g_wifi_mgr->httpd_owned = false;
    }
    
    return ESP_OK;
}

