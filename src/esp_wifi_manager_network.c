/**
 * @file esp_wifi_manager_network.c
 * @brief Network management, connection logic, scan
 */

#include "esp_wifi_manager_priv.h"
#include "esp_bus.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wifi_mgr_net";

// =============================================================================
// Connection Logic (runs in task context)
// =============================================================================

static void sort_networks_by_priority(void)
{
    // Avoid underflow when network_count < 2
    if (g_wifi_mgr->network_count < 2) return;
    
    // Simple bubble sort by priority (descending)
    for (size_t i = 0; i < g_wifi_mgr->network_count - 1; i++) {
        for (size_t j = 0; j < g_wifi_mgr->network_count - i - 1; j++) {
            if (g_wifi_mgr->networks[j].priority < g_wifi_mgr->networks[j + 1].priority) {
                wifi_network_t tmp = g_wifi_mgr->networks[j];
                g_wifi_mgr->networks[j] = g_wifi_mgr->networks[j + 1];
                g_wifi_mgr->networks[j + 1] = tmp;
            }
        }
    }
}

/**
 * @brief Calculate exponential backoff delay
 * @param retry Current retry number (0-based)
 * @return Delay in milliseconds
 */
static uint32_t calc_backoff_delay(int retry)
{
    uint32_t base = g_wifi_mgr->config.retry_interval_ms;
    uint32_t max_delay = g_wifi_mgr->config.retry_max_interval_ms;

    // Exponential backoff: base * 2^retry
    uint32_t delay = base << retry;  // base * 2^retry

    // Cap at max delay
    if (delay > max_delay || delay < base) {  // overflow check
        delay = max_delay;
    }

    return delay;
}

void wifi_mgr_start_connect_sequence(void)
{
    if (!g_wifi_mgr || g_wifi_mgr->network_count == 0) {
        if (g_wifi_mgr && g_wifi_mgr->config.enable_captive_portal) {
            ESP_LOGI(TAG, "No networks, starting captive portal");
            wifi_manager_start_ap(NULL);
        }
        return;
    }

    wifi_mgr_lock();
    g_wifi_mgr->connecting = true;
    sort_networks_by_priority();
    wifi_mgr_unlock();

    // Try each network (AP can run in parallel - AP+STA mode)
    for (size_t i = 0; i < g_wifi_mgr->network_count; i++) {
        wifi_network_t *net = &g_wifi_mgr->networks[i];
        ESP_LOGI(TAG, "Trying network: %s (priority: %d)", net->ssid, net->priority);

        g_wifi_mgr->state = WIFI_STATE_CONNECTING;
        g_wifi_mgr->current_network_idx = i;
        esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_CONNECTING, net->ssid, strlen(net->ssid) + 1);

        wifi_config_t wifi_cfg = {0};
        strncpy((char *)wifi_cfg.sta.ssid, net->ssid, sizeof(wifi_cfg.sta.ssid) - 1);
        strncpy((char *)wifi_cfg.sta.password, net->password, sizeof(wifi_cfg.sta.password) - 1);

        esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);

        for (int retry = 0; retry < g_wifi_mgr->config.max_retry_per_network; retry++) {
            xEventGroupClearBits(g_wifi_mgr->event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
                continue;
            }

            // Wait for connection result
            EventBits_t bits = xEventGroupWaitBits(g_wifi_mgr->event_group,
                                                   WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                   pdFALSE, pdFALSE,
                                                   pdMS_TO_TICKS(15000));

            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "Connected to %s", net->ssid);
                g_wifi_mgr->connecting = false;
                g_wifi_mgr->retry_count = 0;  // Reset backoff counter on success
                return;
            }

            ESP_LOGW(TAG, "Failed to connect to %s, retry %d/%d",
                     net->ssid, retry + 1, g_wifi_mgr->config.max_retry_per_network);

            if (retry < g_wifi_mgr->config.max_retry_per_network - 1) {
                uint32_t delay = calc_backoff_delay(retry);
                ESP_LOGI(TAG, "Backoff delay: %lu ms", (unsigned long)delay);
                vTaskDelay(pdMS_TO_TICKS(delay));
            }
        }
    }

    g_wifi_mgr->connecting = false;

    ESP_LOGW(TAG, "Failed to connect to any network");
    g_wifi_mgr->state = WIFI_STATE_DISCONNECTED;

    // Start captive portal if enabled and AP not already running
    if (g_wifi_mgr->config.enable_captive_portal && !g_wifi_mgr->ap_active) {
        ESP_LOGI(TAG, "Starting captive portal");
        wifi_manager_start_ap(NULL);
    }
}

// Legacy wrapper for backward compatibility
void wifi_mgr_try_connect(void)
{
    wifi_mgr_start_connect_sequence();
}

// =============================================================================
// Network Management
// =============================================================================

esp_err_t wifi_manager_add_network(const wifi_network_t *network)
{
    if (!g_wifi_mgr || !network) return ESP_ERR_INVALID_ARG;
    
    wifi_mgr_lock();
    
    // Check if exists
    for (size_t i = 0; i < g_wifi_mgr->network_count; i++) {
        if (strcmp(g_wifi_mgr->networks[i].ssid, network->ssid) == 0) {
            wifi_mgr_unlock();
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // Check capacity
    if (g_wifi_mgr->network_count >= WIFI_MGR_MAX_NETWORKS) {
        wifi_mgr_unlock();
        return ESP_ERR_NO_MEM;
    }
    
    // Add network
    memcpy(&g_wifi_mgr->networks[g_wifi_mgr->network_count], network, sizeof(wifi_network_t));
    g_wifi_mgr->network_count++;
    
    wifi_mgr_nvs_save_networks(g_wifi_mgr->networks, g_wifi_mgr->network_count);
    wifi_mgr_unlock();
    
    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_NETWORK_ADDED, network, sizeof(wifi_network_t));
    ESP_LOGI(TAG, "Network added: %s", network->ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_update_network(const wifi_network_t *network)
{
    if (!g_wifi_mgr || !network) return ESP_ERR_INVALID_ARG;
    
    wifi_mgr_lock();
    
    for (size_t i = 0; i < g_wifi_mgr->network_count; i++) {
        if (strcmp(g_wifi_mgr->networks[i].ssid, network->ssid) == 0) {
            memcpy(&g_wifi_mgr->networks[i], network, sizeof(wifi_network_t));
            wifi_mgr_nvs_save_networks(g_wifi_mgr->networks, g_wifi_mgr->network_count);
            wifi_mgr_unlock();
            esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_NETWORK_UPDATED, network, sizeof(wifi_network_t));
            ESP_LOGI(TAG, "Network updated: %s", network->ssid);
            return ESP_OK;
        }
    }
    
    wifi_mgr_unlock();
    return ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_manager_remove_network(const char *ssid)
{
    if (!g_wifi_mgr || !ssid) return ESP_ERR_INVALID_ARG;
    
    wifi_mgr_lock();
    
    for (size_t i = 0; i < g_wifi_mgr->network_count; i++) {
        if (strcmp(g_wifi_mgr->networks[i].ssid, ssid) == 0) {
            for (size_t j = i; j < g_wifi_mgr->network_count - 1; j++) {
                g_wifi_mgr->networks[j] = g_wifi_mgr->networks[j + 1];
            }
            g_wifi_mgr->network_count--;
            wifi_mgr_nvs_save_networks(g_wifi_mgr->networks, g_wifi_mgr->network_count);
            wifi_mgr_unlock();
            esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_NETWORK_REMOVED, ssid, strlen(ssid) + 1);
            ESP_LOGI(TAG, "Network removed: %s", ssid);
            return ESP_OK;
        }
    }
    
    wifi_mgr_unlock();
    return ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_manager_get_network(const char *ssid, wifi_network_t *network)
{
    if (!g_wifi_mgr || !ssid || !network) return ESP_ERR_INVALID_ARG;
    
    wifi_mgr_lock();
    
    for (size_t i = 0; i < g_wifi_mgr->network_count; i++) {
        if (strcmp(g_wifi_mgr->networks[i].ssid, ssid) == 0) {
            memcpy(network, &g_wifi_mgr->networks[i], sizeof(wifi_network_t));
            wifi_mgr_unlock();
            return ESP_OK;
        }
    }
    
    wifi_mgr_unlock();
    return ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_manager_list_networks(wifi_network_t *networks, size_t max_count, size_t *count)
{
    if (!g_wifi_mgr || !networks || !count) return ESP_ERR_INVALID_ARG;
    
    wifi_mgr_lock();
    
    size_t copy_count = g_wifi_mgr->network_count;
    if (copy_count > max_count) copy_count = max_count;
    
    memcpy(networks, g_wifi_mgr->networks, copy_count * sizeof(wifi_network_t));
    *count = copy_count;
    
    wifi_mgr_unlock();
    return ESP_OK;
}

// =============================================================================
// Connection API
// =============================================================================

esp_err_t wifi_manager_connect(const char *ssid)
{
    if (!g_wifi_mgr) return ESP_ERR_INVALID_STATE;
    
    if (!ssid) {
        // Auto connect - send event to task
        wifi_mgr_send_event(WM_INT_EVT_START);
        return ESP_OK;
    }
    
    // Skip if already connected to the same SSID
    if (g_wifi_mgr->state == WIFI_STATE_CONNECTED) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            if (strcmp((char *)ap_info.ssid, ssid) == 0) {
                ESP_LOGI(TAG, "Already connected to %s", ssid);
                return ESP_OK;
            }
        }
    }
    
    // Find network
    wifi_network_t *net = NULL;
    wifi_mgr_lock();
    for (size_t i = 0; i < g_wifi_mgr->network_count; i++) {
        if (strcmp(g_wifi_mgr->networks[i].ssid, ssid) == 0) {
            net = &g_wifi_mgr->networks[i];
            break;
        }
    }
    wifi_mgr_unlock();
    
    if (!net) return ESP_ERR_NOT_FOUND;
    
    ESP_LOGI(TAG, "Connecting to %s", ssid);
    
    g_wifi_mgr->state = WIFI_STATE_CONNECTING;
    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_CONNECTING, ssid, strlen(ssid) + 1);
    
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, net->ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, net->password, sizeof(wifi_cfg.sta.password) - 1);
    
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    return esp_wifi_connect();
}

esp_err_t wifi_manager_disconnect(void)
{
    if (!g_wifi_mgr) return ESP_ERR_INVALID_STATE;
    
    g_wifi_mgr->config.auto_reconnect = false;
    g_wifi_mgr->connecting = false;
    return esp_wifi_disconnect();
}

esp_err_t wifi_manager_scan(wifi_scan_result_t *results, size_t max_count, size_t *count)
{
    if (!g_wifi_mgr || !results || !count) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Starting scan");

    // Stop connection attempt if in progress (scan not allowed while connecting)
    if (g_wifi_mgr->connecting || g_wifi_mgr->state == WIFI_STATE_CONNECTING) {
        ESP_LOGI(TAG, "Stopping connection for scan");
        g_wifi_mgr->connecting = false;
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));  // Brief delay for disconnect
    }

    xEventGroupClearBits(g_wifi_mgr->event_group, WIFI_SCAN_DONE_BIT);

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, false);
    if (ret != ESP_OK) return ret;
    
    EventBits_t bits = xEventGroupWaitBits(g_wifi_mgr->event_group,
                                           WIFI_SCAN_DONE_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(10000));
    
    if (!(bits & WIFI_SCAN_DONE_BIT)) {
        return ESP_ERR_TIMEOUT;
    }
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    wifi_ap_record_t *ap_list = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!ap_list) return ESP_ERR_NO_MEM;
    
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    
    size_t copy_count = ap_count;
    if (copy_count > max_count) copy_count = max_count;
    
    for (size_t i = 0; i < copy_count; i++) {
        strncpy(results[i].ssid, (char *)ap_list[i].ssid, sizeof(results[i].ssid) - 1);
        results[i].rssi = ap_list[i].rssi;
        results[i].auth = ap_list[i].authmode;
    }

    // Deduplicate by SSID, keeping the entry with strongest RSSI
    // This handles mesh networks and enterprise setups with multiple APs
    for (size_t i = 0; i < copy_count; i++) {
        for (size_t j = i + 1; j < copy_count; ) {
            if (strcmp(results[i].ssid, results[j].ssid) == 0) {
                // Keep stronger signal
                if (results[j].rssi > results[i].rssi) {
                    results[i].rssi = results[j].rssi;
                    results[i].auth = results[j].auth;
                }
                // Remove duplicate by shifting remaining entries
                memmove(&results[j], &results[j + 1], (copy_count - j - 1) * sizeof(wifi_scan_result_t));
                copy_count--;
            } else {
                j++;
            }
        }
    }

    *count = copy_count;
    free(ap_list);
    
    uint16_t cnt = (uint16_t)copy_count;
    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_SCAN_DONE, &cnt, sizeof(cnt));
    
    ESP_LOGI(TAG, "Scan done, found %zu networks", copy_count);
    return ESP_OK;
}
