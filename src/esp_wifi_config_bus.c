/**
 * @file esp_wifi_config_bus.c
 * @brief esp_bus request handler
 */

#include "esp_wifi_config_priv.h"
#include <string.h>

// =============================================================================
// esp_bus Handler
// =============================================================================

esp_err_t wifi_cfg_bus_handler(const char *action,
                               const void *req_data, size_t req_len,
                               void *res_buf, size_t res_buf_size, size_t *res_len,
                               void *ctx)
{
    if (!g_wifi_cfg) return ESP_ERR_INVALID_STATE;
    
    // Connect
    if (strcmp(action, WIFI_ACTION_CONNECT) == 0) {
        if (req_data && req_len > 0) {
            return wifi_cfg_connect((const char *)req_data);
        }
        // Auto-connect: send event to task
        wifi_cfg_send_event(WM_INT_EVT_START);
        return ESP_OK;
    }
    
    // Disconnect
    if (strcmp(action, WIFI_ACTION_DISCONNECT) == 0) {
        return wifi_cfg_disconnect();
    }
    
    // Get status
    if (strcmp(action, WIFI_ACTION_GET_STATUS) == 0) {
        if (!res_buf || res_buf_size < sizeof(wifi_status_t)) {
            return ESP_ERR_INVALID_SIZE;
        }
        esp_err_t ret = wifi_cfg_get_status((wifi_status_t *)res_buf);
        if (res_len) *res_len = sizeof(wifi_status_t);
        return ret;
    }
    
    // Scan
    if (strcmp(action, WIFI_ACTION_SCAN) == 0) {
        if (!res_buf) return ESP_ERR_INVALID_ARG;
        size_t max_count = res_buf_size / sizeof(wifi_scan_result_t);
        size_t count = 0;
        esp_err_t ret = wifi_cfg_scan((wifi_scan_result_t *)res_buf, max_count, &count);
        if (res_len) *res_len = count * sizeof(wifi_scan_result_t);
        return ret;
    }
    
    // Add network
    if (strcmp(action, WIFI_ACTION_ADD_NETWORK) == 0) {
        if (!req_data || req_len < sizeof(wifi_network_t)) {
            return ESP_ERR_INVALID_ARG;
        }
        return wifi_cfg_add_network((const wifi_network_t *)req_data);
    }
    
    // Update network
    if (strcmp(action, WIFI_ACTION_UPDATE_NETWORK) == 0) {
        if (!req_data || req_len < sizeof(wifi_network_t)) {
            return ESP_ERR_INVALID_ARG;
        }
        return wifi_cfg_update_network((const wifi_network_t *)req_data);
    }
    
    // Remove network
    if (strcmp(action, WIFI_ACTION_REMOVE_NETWORK) == 0) {
        if (!req_data) return ESP_ERR_INVALID_ARG;
        return wifi_cfg_remove_network((const char *)req_data);
    }
    
    // List networks
    if (strcmp(action, WIFI_ACTION_LIST_NETWORKS) == 0) {
        if (!res_buf) return ESP_ERR_INVALID_ARG;
        size_t max_count = res_buf_size / sizeof(wifi_network_t);
        size_t count = 0;
        esp_err_t ret = wifi_cfg_list_networks((wifi_network_t *)res_buf, max_count, &count);
        if (res_len) *res_len = count * sizeof(wifi_network_t);
        return ret;
    }
    
    // Start AP
    if (strcmp(action, WIFI_ACTION_START_AP) == 0) {
        const wifi_cfg_ap_config_t *cfg = NULL;
        if (req_data && req_len >= sizeof(wifi_cfg_ap_config_t)) {
            cfg = (const wifi_cfg_ap_config_t *)req_data;
        }
        return wifi_cfg_start_ap(cfg);
    }
    
    // Stop AP
    if (strcmp(action, WIFI_ACTION_STOP_AP) == 0) {
        return wifi_cfg_stop_ap();
    }
    
    // Get AP status
    if (strcmp(action, WIFI_ACTION_GET_AP_STATUS) == 0) {
        if (!res_buf || res_buf_size < sizeof(wifi_ap_status_t)) {
            return ESP_ERR_INVALID_SIZE;
        }
        esp_err_t ret = wifi_cfg_get_ap_status((wifi_ap_status_t *)res_buf);
        if (res_len) *res_len = sizeof(wifi_ap_status_t);
        return ret;
    }
    
    // Set variable
    if (strcmp(action, WIFI_ACTION_SET_VAR) == 0) {
        if (!req_data || req_len < sizeof(wifi_var_t)) {
            return ESP_ERR_INVALID_ARG;
        }
        const wifi_var_t *var = (const wifi_var_t *)req_data;
        return wifi_cfg_set_var(var->key, var->value);
    }
    
    // Get variable
    if (strcmp(action, WIFI_ACTION_GET_VAR) == 0) {
        if (!req_data || !res_buf || res_buf_size < sizeof(wifi_var_t)) {
            return ESP_ERR_INVALID_ARG;
        }
        wifi_var_t *var = (wifi_var_t *)res_buf;
        strncpy(var->key, (const char *)req_data, sizeof(var->key) - 1);
        esp_err_t ret = wifi_cfg_get_var(var->key, var->value, sizeof(var->value));
        if (res_len) *res_len = sizeof(wifi_var_t);
        return ret;
    }
    
    // Delete variable
    if (strcmp(action, WIFI_ACTION_DEL_VAR) == 0) {
        if (!req_data) return ESP_ERR_INVALID_ARG;
        return wifi_cfg_del_var((const char *)req_data);
    }

    // Factory reset
    if (strcmp(action, WIFI_ACTION_FACTORY_RESET) == 0) {
        return wifi_cfg_factory_reset();
    }

    return ESP_ERR_NOT_SUPPORTED;
}

