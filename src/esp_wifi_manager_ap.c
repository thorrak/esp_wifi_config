/**
 * @file esp_wifi_manager_ap.c
 * @brief SoftAP and variable management
 */

#include "esp_wifi_manager_priv.h"
#include "esp_bus.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "wifi_mgr_ap";

/**
 * @brief Expand {id} placeholder with MAC address suffix
 * @param tmpl Template string (e.g., "ESP32-{id}")
 * @param output Output buffer
 * @param max_len Output buffer size
 */
void wifi_mgr_expand_template(const char *tmpl, char *output, size_t max_len)
{
    const char *placeholder = strstr(tmpl, "{id}");
    if (!placeholder) {
        strncpy(output, tmpl, max_len - 1);
        output[max_len - 1] = '\0';
        return;
    }

    // Get MAC address (use STA MAC for consistency)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Build output: prefix + MAC suffix (last 3 bytes = 6 hex chars)
    size_t prefix_len = placeholder - tmpl;
    if (prefix_len >= max_len) prefix_len = max_len - 1;

    strncpy(output, tmpl, prefix_len);
    snprintf(output + prefix_len, max_len - prefix_len, "%02X%02X%02X%s",
             mac[3], mac[4], mac[5], placeholder + 4);
}

// =============================================================================
// SoftAP Management
// =============================================================================

esp_err_t wifi_manager_start_ap(const wifi_mgr_ap_config_t *config)
{
    if (!g_wifi_mgr) return ESP_ERR_INVALID_STATE;
    
    const wifi_mgr_ap_config_t *ap_cfg = config ? config : &g_wifi_mgr->ap_config;
    
    // Mark as active immediately to prevent reconnect races
    g_wifi_mgr->ap_active = true;
    
    // Expand SSID template (replace {id} with MAC suffix)
    char ssid_expanded[32];
    wifi_mgr_expand_template(ap_cfg->ssid, ssid_expanded, sizeof(ssid_expanded));
    
    ESP_LOGI(TAG, "Starting AP: %s", ssid_expanded);

    // Only change mode if not already in APSTA
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    if (current_mode != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
    
    wifi_config_t wifi_cfg = {
        .ap = {
            .max_connection = ap_cfg->max_connections ? ap_cfg->max_connections : 4,
            .authmode = ap_cfg->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
            .channel = ap_cfg->channel,
            .ssid_hidden = ap_cfg->hidden,
        },
    };
    strncpy((char *)wifi_cfg.ap.ssid, ssid_expanded, sizeof(wifi_cfg.ap.ssid) - 1);
    wifi_cfg.ap.ssid_len = strlen(ssid_expanded);
    if (ap_cfg->password[0]) {
        strncpy((char *)wifi_cfg.ap.password, ap_cfg->password, sizeof(wifi_cfg.ap.password) - 1);
    }
    
    esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);

    // Start DNS server for captive portal
    wifi_mgr_dns_start();

    // Configure static IP
    if (ap_cfg->ip[0]) {
        esp_netif_dhcps_stop(g_wifi_mgr->ap_netif);
        
        esp_netif_ip_info_t ip_info = {0};
        inet_pton(AF_INET, ap_cfg->ip, &ip_info.ip);
        inet_pton(AF_INET, ap_cfg->netmask[0] ? ap_cfg->netmask : "255.255.255.0", &ip_info.netmask);
        inet_pton(AF_INET, ap_cfg->gateway[0] ? ap_cfg->gateway : ap_cfg->ip, &ip_info.gw);
        
        esp_netif_set_ip_info(g_wifi_mgr->ap_netif, &ip_info);
        esp_netif_dhcps_start(g_wifi_mgr->ap_netif);
    }

    // Emit AP start event after config is fully applied, so listeners
    // see the correct SSID/IP. This avoids spurious events from
    // intermediate driver restarts (set_mode then set_config).
    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_AP_START, NULL, 0);

    return ESP_OK;
}

esp_err_t wifi_manager_stop_ap(void)
{
    if (!g_wifi_mgr) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "Stopping AP");

    // Stop DNS server
    wifi_mgr_dns_stop();

    g_wifi_mgr->ap_active = false;
    esp_wifi_set_mode(WIFI_MODE_STA);

    return ESP_OK;
}

// Internal functions called from task
void wifi_mgr_start_ap_mode(void)
{
    wifi_manager_start_ap(NULL);
}

void wifi_mgr_stop_ap_mode(void)
{
    wifi_manager_stop_ap();
}

esp_err_t wifi_manager_get_ap_status(wifi_ap_status_t *status)
{
    if (!g_wifi_mgr || !status) return ESP_ERR_INVALID_ARG;
    
    memset(status, 0, sizeof(wifi_ap_status_t));
    status->active = g_wifi_mgr->ap_active;
    
    if (g_wifi_mgr->ap_active) {
        wifi_config_t cfg;
        if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK) {
            strncpy(status->ssid, (char *)cfg.ap.ssid, sizeof(status->ssid) - 1);
            status->channel = cfg.ap.channel;
        }
        
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(g_wifi_mgr->ap_netif, &ip_info) == ESP_OK) {
            snprintf(status->ip, sizeof(status->ip), IPSTR, IP2STR(&ip_info.ip));
        }
        
        wifi_sta_list_t sta_list;
        if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
            status->sta_count = sta_list.num;
            for (int i = 0; i < sta_list.num && i < 4; i++) {
                snprintf(status->clients[i].mac, sizeof(status->clients[i].mac),
                         MACSTR, MAC2STR(sta_list.sta[i].mac));
            }
        }
    }
    
    return ESP_OK;
}

esp_err_t wifi_manager_set_ap_config(const wifi_mgr_ap_config_t *config)
{
    if (!g_wifi_mgr || !config) return ESP_ERR_INVALID_ARG;
    
    memcpy(&g_wifi_mgr->ap_config, config, sizeof(wifi_mgr_ap_config_t));
    wifi_mgr_nvs_save_ap_config(config);
    
    if (g_wifi_mgr->ap_active) {
        wifi_manager_start_ap(config);
    }
    
    return ESP_OK;
}

esp_err_t wifi_manager_get_ap_config(wifi_mgr_ap_config_t *config)
{
    if (!g_wifi_mgr || !config) return ESP_ERR_INVALID_ARG;
    memcpy(config, &g_wifi_mgr->ap_config, sizeof(wifi_mgr_ap_config_t));
    return ESP_OK;
}

// =============================================================================
// Variable Management
// =============================================================================

esp_err_t wifi_manager_set_var(const char *key, const char *value)
{
    if (!g_wifi_mgr || !key || !value) return ESP_ERR_INVALID_ARG;
    
    wifi_mgr_lock();
    
    for (size_t i = 0; i < g_wifi_mgr->var_count; i++) {
        if (strcmp(g_wifi_mgr->vars[i].key, key) == 0) {
            strncpy(g_wifi_mgr->vars[i].value, value, sizeof(g_wifi_mgr->vars[i].value) - 1);
            wifi_mgr_nvs_save_vars(g_wifi_mgr->vars, g_wifi_mgr->var_count);
            wifi_mgr_unlock();
            
            wifi_var_t var;
            strncpy(var.key, key, sizeof(var.key) - 1);
            strncpy(var.value, value, sizeof(var.value) - 1);
            esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_VAR_CHANGED, &var, sizeof(var));
            return ESP_OK;
        }
    }
    
    if (g_wifi_mgr->var_count >= WIFI_MGR_MAX_VARS) {
        wifi_mgr_unlock();
        return ESP_ERR_NO_MEM;
    }
    
    strncpy(g_wifi_mgr->vars[g_wifi_mgr->var_count].key, key, 
            sizeof(g_wifi_mgr->vars[0].key) - 1);
    strncpy(g_wifi_mgr->vars[g_wifi_mgr->var_count].value, value, 
            sizeof(g_wifi_mgr->vars[0].value) - 1);
    g_wifi_mgr->var_count++;
    
    wifi_mgr_nvs_save_vars(g_wifi_mgr->vars, g_wifi_mgr->var_count);
    wifi_mgr_unlock();
    
    wifi_var_t var = {0};
    strncpy(var.key, key, sizeof(var.key) - 1);
    strncpy(var.value, value, sizeof(var.value) - 1);
    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_VAR_CHANGED, &var, sizeof(var));
    
    return ESP_OK;
}

esp_err_t wifi_manager_get_var(const char *key, char *value, size_t max_len)
{
    if (!g_wifi_mgr || !key || !value) return ESP_ERR_INVALID_ARG;
    
    wifi_mgr_lock();
    
    for (size_t i = 0; i < g_wifi_mgr->var_count; i++) {
        if (strcmp(g_wifi_mgr->vars[i].key, key) == 0) {
            strncpy(value, g_wifi_mgr->vars[i].value, max_len - 1);
            value[max_len - 1] = '\0';
            wifi_mgr_unlock();
            return ESP_OK;
        }
    }
    
    wifi_mgr_unlock();
    return ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_manager_del_var(const char *key)
{
    if (!g_wifi_mgr || !key) return ESP_ERR_INVALID_ARG;
    
    wifi_mgr_lock();
    
    for (size_t i = 0; i < g_wifi_mgr->var_count; i++) {
        if (strcmp(g_wifi_mgr->vars[i].key, key) == 0) {
            wifi_var_t var = {0};
            strncpy(var.key, key, sizeof(var.key) - 1);
            
            for (size_t j = i; j < g_wifi_mgr->var_count - 1; j++) {
                g_wifi_mgr->vars[j] = g_wifi_mgr->vars[j + 1];
            }
            g_wifi_mgr->var_count--;
            wifi_mgr_nvs_save_vars(g_wifi_mgr->vars, g_wifi_mgr->var_count);
            wifi_mgr_unlock();
            
            esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_VAR_CHANGED, &var, sizeof(var));
            return ESP_OK;
        }
    }
    
    wifi_mgr_unlock();
    return ESP_ERR_NOT_FOUND;
}
