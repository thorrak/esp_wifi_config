/**
 * @file esp_wifi_manager.c
 * @brief WiFi Manager core - task-based state machine
 */

#include "esp_wifi_manager_priv.h"
#include "esp_bus.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

// Global context
wifi_mgr_ctx_t *g_wifi_mgr = NULL;

// =============================================================================
// Forward Declarations
// =============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data);
static void wifi_mgr_task(void *arg);

// =============================================================================
// Event Queue Functions
// =============================================================================

void wifi_mgr_send_event(wifi_mgr_internal_evt_t type)
{
    if (!g_wifi_mgr || !g_wifi_mgr->queue) return;
    wifi_mgr_event_t evt = {0};
    evt.type = type;
    xQueueSend(g_wifi_mgr->queue, &evt, 0);
}

void wifi_mgr_send_event_data(const wifi_mgr_event_t *event)
{
    if (!g_wifi_mgr || !g_wifi_mgr->queue || !event) return;
    xQueueSend(g_wifi_mgr->queue, event, 0);
}

// =============================================================================
// Helpers
// =============================================================================

static void set_default_ap_config(wifi_mgr_ap_config_t *ap)
{
    strncpy(ap->ssid, CONFIG_WIFI_MGR_AP_SSID, sizeof(ap->ssid) - 1);
    strncpy(ap->password, CONFIG_WIFI_MGR_AP_PASSWORD, sizeof(ap->password) - 1);
    ap->channel = 0;
    ap->max_connections = 4;
    ap->hidden = false;
    strncpy(ap->ip, CONFIG_WIFI_MGR_AP_IP, sizeof(ap->ip) - 1);
    strncpy(ap->netmask, "255.255.255.0", sizeof(ap->netmask) - 1);
    strncpy(ap->gateway, CONFIG_WIFI_MGR_AP_IP, sizeof(ap->gateway) - 1);
    strncpy(ap->dhcp_start, "192.168.4.2", sizeof(ap->dhcp_start) - 1);
    strncpy(ap->dhcp_end, "192.168.4.20", sizeof(ap->dhcp_end) - 1);
}

// =============================================================================
// Initialization
// =============================================================================

esp_err_t wifi_manager_init(const wifi_manager_config_t *config)
{
    esp_err_t ret;
    
    if (g_wifi_mgr) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Allocate context
    g_wifi_mgr = calloc(1, sizeof(wifi_mgr_ctx_t));
    if (!g_wifi_mgr) return ESP_ERR_NO_MEM;
    
    // Init sync primitives
    g_wifi_mgr->mutex = xSemaphoreCreateMutex();
    g_wifi_mgr->event_group = xEventGroupCreate();
    g_wifi_mgr->queue = xQueueCreate(WIFI_MGR_QUEUE_SIZE, sizeof(wifi_mgr_event_t));
    
    if (!g_wifi_mgr->mutex || !g_wifi_mgr->event_group || !g_wifi_mgr->queue) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    
    // Copy config
    if (config) {
        memcpy(&g_wifi_mgr->config, config, sizeof(wifi_manager_config_t));
    }
    
    // Set defaults
    if (g_wifi_mgr->config.max_retry_per_network == 0) {
        g_wifi_mgr->config.max_retry_per_network = CONFIG_WIFI_MGR_DEFAULT_RETRY;
    }
    if (g_wifi_mgr->config.retry_interval_ms == 0) {
        g_wifi_mgr->config.retry_interval_ms = CONFIG_WIFI_MGR_RETRY_INTERVAL_MS;
    }
    if (g_wifi_mgr->config.retry_max_interval_ms == 0) {
        g_wifi_mgr->config.retry_max_interval_ms = 60000;  // 60 seconds max backoff
    }
    
    // Init NVS
    ret = wifi_mgr_nvs_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Load saved networks from NVS
    ret = wifi_mgr_nvs_load_networks(g_wifi_mgr->networks, WIFI_MGR_MAX_NETWORKS, &g_wifi_mgr->network_count);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load networks from NVS");
    }
    
    // Merge with default networks if NVS empty
    if (g_wifi_mgr->network_count == 0 && config && config->default_networks) {
        size_t count = config->default_network_count;
        if (count > WIFI_MGR_MAX_NETWORKS) count = WIFI_MGR_MAX_NETWORKS;
        memcpy(g_wifi_mgr->networks, config->default_networks, count * sizeof(wifi_network_t));
        g_wifi_mgr->network_count = count;
        wifi_mgr_nvs_save_networks(g_wifi_mgr->networks, g_wifi_mgr->network_count);
    }
    
    // Load saved variables
    ret = wifi_mgr_nvs_load_vars(g_wifi_mgr->vars, WIFI_MGR_MAX_VARS, &g_wifi_mgr->var_count);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load vars from NVS");
    }
    
    // Merge with default vars if NVS empty
    if (g_wifi_mgr->var_count == 0 && config && config->default_vars) {
        size_t count = config->default_var_count;
        if (count > WIFI_MGR_MAX_VARS) count = WIFI_MGR_MAX_VARS;
        memcpy(g_wifi_mgr->vars, config->default_vars, count * sizeof(wifi_var_t));
        g_wifi_mgr->var_count = count;
        wifi_mgr_nvs_save_vars(g_wifi_mgr->vars, g_wifi_mgr->var_count);
    }
    
    // Load AP config: config->default_ap always wins if provided,
    // otherwise fall back to NVS, then built-in defaults
    if (config && config->default_ap.ssid[0]) {
        memcpy(&g_wifi_mgr->ap_config, &config->default_ap, sizeof(wifi_mgr_ap_config_t));
    } else {
        ret = wifi_mgr_nvs_load_ap_config(&g_wifi_mgr->ap_config);
        if (ret != ESP_OK) {
            set_default_ap_config(&g_wifi_mgr->ap_config);
        }
    }
    
    // Load auth credentials
    strncpy(g_wifi_mgr->auth_username, "admin", sizeof(g_wifi_mgr->auth_username) - 1);
    strncpy(g_wifi_mgr->auth_password, "admin", sizeof(g_wifi_mgr->auth_password) - 1);
    wifi_mgr_nvs_load_auth(g_wifi_mgr->auth_username, sizeof(g_wifi_mgr->auth_username),
                          g_wifi_mgr->auth_password, sizeof(g_wifi_mgr->auth_password));
    
    if (config && config->http.auth_username) {
        strncpy(g_wifi_mgr->auth_username, config->http.auth_username, sizeof(g_wifi_mgr->auth_username) - 1);
    }
    if (config && config->http.auth_password) {
        strncpy(g_wifi_mgr->auth_password, config->http.auth_password, sizeof(g_wifi_mgr->auth_password) - 1);
    }
    
    // Init TCP/IP stack (có thể đã init bởi component khác)
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Event loop (có thể đã init bởi component khác)
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Create netif - check if already exists
    g_wifi_mgr->sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!g_wifi_mgr->sta_netif) {
        g_wifi_mgr->sta_netif = esp_netif_create_default_wifi_sta();
        g_wifi_mgr->sta_netif_owned = true;
    } else {
        g_wifi_mgr->sta_netif_owned = false;
    }
    
    g_wifi_mgr->ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!g_wifi_mgr->ap_netif) {
        g_wifi_mgr->ap_netif = esp_netif_create_default_wifi_ap();
        g_wifi_mgr->ap_netif_owned = true;
    } else {
        g_wifi_mgr->ap_netif_owned = false;
    }
    
    // Init WiFi (có thể đã init bởi component khác)
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&wifi_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Register event handlers (lightweight - just send to queue)
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL);
    
    // Set WiFi mode and start
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set mode failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Check if esp_bus is initialized (user should call esp_bus_init() before wifi_manager_init())
    if (!esp_bus_is_init()) {
        ESP_LOGE(TAG, "esp_bus not initialized. Call esp_bus_init() first.");
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    // Register esp_bus module
    static const esp_bus_action_t actions[] = {
        {WIFI_ACTION_CONNECT, "string", "none", "Connect to network"},
        {WIFI_ACTION_DISCONNECT, "none", "none", "Disconnect"},
        {WIFI_ACTION_SCAN, "none", "wifi_scan_result_t[]", "Scan networks"},
        {WIFI_ACTION_GET_STATUS, "none", "wifi_status_t", "Get status"},
        {WIFI_ACTION_ADD_NETWORK, "wifi_network_t", "none", "Add network"},
        {WIFI_ACTION_UPDATE_NETWORK, "wifi_network_t", "none", "Update network"},
        {WIFI_ACTION_REMOVE_NETWORK, "string", "none", "Remove network"},
        {WIFI_ACTION_LIST_NETWORKS, "none", "wifi_network_t[]", "List networks"},
        {WIFI_ACTION_START_AP, "wifi_mgr_ap_config_t", "none", "Start AP"},
        {WIFI_ACTION_STOP_AP, "none", "none", "Stop AP"},
        {WIFI_ACTION_GET_AP_STATUS, "none", "wifi_ap_status_t", "Get AP status"},
        {WIFI_ACTION_SET_VAR, "wifi_var_t", "none", "Set variable"},
        {WIFI_ACTION_GET_VAR, "string", "wifi_var_t", "Get variable"},
        {WIFI_ACTION_DEL_VAR, "string", "none", "Delete variable"},
    };
    
    static const esp_bus_event_t events[] = {
        {WIFI_MGR_EVT_CONNECTED, "wifi_connected_t", "WiFi connected"},
        {WIFI_MGR_EVT_DISCONNECTED, "wifi_disconnected_t", "WiFi disconnected"},
        {WIFI_MGR_EVT_GOT_IP, "ip_info", "Got IP address"},
        {WIFI_MGR_EVT_SCAN_DONE, "uint16", "Scan completed"},
        {WIFI_MGR_EVT_NETWORK_ADDED, "wifi_network_t", "Network added"},
        {WIFI_MGR_EVT_NETWORK_REMOVED, "string", "Network removed"},
        {WIFI_MGR_EVT_VAR_CHANGED, "wifi_var_t", "Variable changed"},
    };
    
    esp_bus_module_t bus_cfg = {
        .name = WIFI_MODULE,
        .on_req = wifi_mgr_bus_handler,
        .ctx = g_wifi_mgr,
        .actions = actions,
        .action_cnt = sizeof(actions) / sizeof(actions[0]),
        .events = events,
        .event_cnt = sizeof(events) / sizeof(events[0]),
    };
    
    ret = esp_bus_reg(&bus_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bus register failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Init HTTP interface if enabled
    if (config && config->http.enable) {
        g_wifi_mgr->httpd = config->http.httpd;
        ret = wifi_mgr_http_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "HTTP init failed: %s", esp_err_to_name(ret));
        }
    }

    // Init CLI if enabled
#ifdef CONFIG_WIFI_MGR_ENABLE_CLI
    ret = wifi_mgr_cli_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CLI init failed: %s", esp_err_to_name(ret));
    }
#endif

    // Init BLE if enabled
#ifdef CONFIG_WIFI_MGR_ENABLE_BLE
    if (config && config->ble.enable) {
        ret = wifi_mgr_ble_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "BLE init failed: %s", esp_err_to_name(ret));
        }
    }
#endif

    g_wifi_mgr->initialized = true;
    g_wifi_mgr->state = WIFI_STATE_DISCONNECTED;

    // Create manager task
    BaseType_t task_ret = xTaskCreate(wifi_mgr_task, "wifi_mgr", WIFI_MGR_TASK_STACK_SIZE,
                NULL, WIFI_MGR_TASK_PRIORITY, &g_wifi_mgr->task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: %d", task_ret);
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    ESP_LOGI(TAG, "WiFi Manager initialized, %zu networks configured", g_wifi_mgr->network_count);
    return ESP_OK;
    
cleanup:
    if (g_wifi_mgr) {
        if (g_wifi_mgr->mutex) vSemaphoreDelete(g_wifi_mgr->mutex);
        if (g_wifi_mgr->event_group) vEventGroupDelete(g_wifi_mgr->event_group);
        if (g_wifi_mgr->queue) vQueueDelete(g_wifi_mgr->queue);
        free(g_wifi_mgr);
        g_wifi_mgr = NULL;
    }
    return ret;
}

esp_err_t wifi_manager_deinit(bool deinit_wifi)
{
    if (!g_wifi_mgr) return ESP_ERR_INVALID_STATE;
    
    // Stop task
    wifi_mgr_send_event(WM_INT_EVT_STOP);
    vTaskDelay(pdMS_TO_TICKS(100));

#ifdef CONFIG_WIFI_MGR_ENABLE_BLE
    wifi_mgr_ble_deinit();
#endif
    wifi_mgr_mdns_deinit();
    wifi_mgr_http_deinit();
    esp_bus_unreg(WIFI_MODULE);
    
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler);
    
    if (deinit_wifi) {
        esp_wifi_stop();
        esp_wifi_deinit();
    
        // Unless we stop the wifi, we should not destroy the netifs. If the user wants
        // to reuse the netifs in their own code, they can reobtain the handles via
        // esp_netif_get_handle_from_ifkey()
        if (g_wifi_mgr->sta_netif && g_wifi_mgr->sta_netif_owned) {
            esp_netif_destroy(g_wifi_mgr->sta_netif);
        }
        if (g_wifi_mgr->ap_netif && g_wifi_mgr->ap_netif_owned) {
            esp_netif_destroy(g_wifi_mgr->ap_netif);
        }
    }
        
    // Task tự delete khi nhận WM_INT_EVT_STOP, không cần delete lại
    g_wifi_mgr->task = NULL;
    vSemaphoreDelete(g_wifi_mgr->mutex);
    vEventGroupDelete(g_wifi_mgr->event_group);
    vQueueDelete(g_wifi_mgr->queue);
    free(g_wifi_mgr);
    g_wifi_mgr = NULL;
    
    ESP_LOGI(TAG, "WiFi Manager deinitialized");
    return ESP_OK;
}

// =============================================================================
// Event Handlers (Lightweight - just forward to task queue)
// =============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (!g_wifi_mgr) return;
    
    wifi_mgr_event_t evt = {0};
    
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started");
            // Trigger connection sequence now that WiFi is ready
            wifi_mgr_send_event(WM_INT_EVT_START);
            break;
            
        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "STA connected to %s", event->ssid);
            evt.type = WM_INT_EVT_STA_CONNECTED;
            strncpy(evt.data.connected.ssid, (char *)event->ssid, sizeof(evt.data.connected.ssid) - 1);
            evt.data.connected.channel = event->channel;
            wifi_mgr_send_event_data(&evt);
            break;
        }
        
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "STA disconnected from %s, reason: %d", event->ssid, event->reason);
            // Only clear connected bit if this disconnect is for the network we're
            // actually connected to; stale disconnects from failed attempts at other
            // networks should not blow away a working connection.
            bool is_current;
            if (g_wifi_mgr->connected_ssid[0] != '\0') {
                is_current = (strcmp(g_wifi_mgr->connected_ssid, (char *)event->ssid) == 0);
            } else {
                // connected_ssid not populated yet (STA_CONNECTED queued but not
                // processed). If WIFI_CONNECTED_BIT is already set, we have a
                // working connection and this disconnect is stale.
                is_current = !(xEventGroupGetBits(g_wifi_mgr->event_group) & WIFI_CONNECTED_BIT);
            }
            if (is_current) {
                xEventGroupSetBits(g_wifi_mgr->event_group, WIFI_FAIL_BIT);
                xEventGroupClearBits(g_wifi_mgr->event_group, WIFI_CONNECTED_BIT);
            } else {
                // Still set FAIL_BIT to unblock connect sequence retry loop
                xEventGroupSetBits(g_wifi_mgr->event_group, WIFI_FAIL_BIT);
            }
            evt.type = WM_INT_EVT_STA_DISCONNECTED;
            strncpy(evt.data.disconnect.ssid, (char *)event->ssid, sizeof(evt.data.disconnect.ssid) - 1);
            evt.data.disconnect.reason = event->reason;
            wifi_mgr_send_event_data(&evt);
            break;
        }
        
        case WIFI_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "Scan done");
            xEventGroupSetBits(g_wifi_mgr->event_group, WIFI_SCAN_DONE_BIT);
            wifi_mgr_send_event(WM_INT_EVT_SCAN_COMPLETE);
            break;
            
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started");
            wifi_mgr_send_event(WM_INT_EVT_AP_STARTED);
            break;
            
        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "AP stopped");
            wifi_mgr_send_event(WM_INT_EVT_AP_STOPPED);
            break;
            
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Station connected to AP, MAC: " MACSTR, MAC2STR(event->mac));
            evt.type = WM_INT_EVT_AP_STA_CONN;
            memcpy(evt.data.mac, event->mac, 6);
            wifi_mgr_send_event_data(&evt);
            break;
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (!g_wifi_mgr) return;
    
    wifi_mgr_event_t evt = {0};
    
    switch (event_id) {
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            // Set bit immediately to unblock connect sequence (avoid deadlock)
            xEventGroupSetBits(g_wifi_mgr->event_group, WIFI_CONNECTED_BIT);
            xEventGroupClearBits(g_wifi_mgr->event_group, WIFI_FAIL_BIT);
            evt.type = WM_INT_EVT_GOT_IP;
            memcpy(&evt.data.got_ip.ip_info, &event->ip_info, sizeof(esp_netif_ip_info_t));
            wifi_mgr_send_event_data(&evt);
            break;
        }
        
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGW(TAG, "Lost IP");
            wifi_mgr_send_event(WM_INT_EVT_LOST_IP);
            break;
    }
}

// =============================================================================
// Manager Task - State Machine
// =============================================================================

static void wifi_mgr_task(void *arg)
{
    wifi_mgr_event_t evt;
    
    ESP_LOGI(TAG, "Task started");
    
    while (1) {
        if (xQueueReceive(g_wifi_mgr->queue, &evt, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (evt.type) {
                case WM_INT_EVT_START:
                    // Start AP immediately if configured
                    if (g_wifi_mgr->config.start_ap_on_init) {
                        ESP_LOGI(TAG, "Starting AP on init");
                        wifi_manager_start_ap(NULL);
                    }
                    // Start auto-connect if networks configured, else start captive portal
                    if (g_wifi_mgr->network_count > 0) {
                        wifi_mgr_start_connect_sequence();
                    } else if (g_wifi_mgr->config.enable_captive_portal && !g_wifi_mgr->ap_active) {
                        wifi_manager_start_ap(NULL);
                    }
                    break;
                    
                case WM_INT_EVT_STA_CONNECTED:
                    g_wifi_mgr->state = WIFI_STATE_CONNECTED;
                    g_wifi_mgr->connect_time = esp_timer_get_time() / 1000;
                    g_wifi_mgr->retry_count = 0;
                    g_wifi_mgr->connecting = false;
                    strncpy(g_wifi_mgr->connected_ssid, evt.data.connected.ssid,
                            sizeof(g_wifi_mgr->connected_ssid) - 1);
                    g_wifi_mgr->connected_ssid[sizeof(g_wifi_mgr->connected_ssid) - 1] = '\0';
                    break;
                    
                case WM_INT_EVT_STA_DISCONNECTED: {
                    // Check if this disconnect is for the network we're actually
                    // connected to, or a stale event from a failed attempt at a
                    // different network during the connect sequence.
                    bool is_current_network;
                    if (g_wifi_mgr->connected_ssid[0] != '\0') {
                        is_current_network = (strcmp(g_wifi_mgr->connected_ssid, evt.data.disconnect.ssid) == 0);
                    } else {
                        // connected_ssid not populated yet (STA_CONNECTED queued but
                        // not processed). If WIFI_CONNECTED_BIT is set, the ISR
                        // already recorded a successful connection and this disconnect
                        // is stale (e.g. a failed OPT attempt processed after
                        // OPT-Roam connected).
                        is_current_network = !(xEventGroupGetBits(g_wifi_mgr->event_group) & WIFI_CONNECTED_BIT);
                    }

                    if (!is_current_network) {
                        // Stale disconnect for a network we're not connected to
                        // (e.g. failed attempt at OPT while connected to OPT-Roam).
                        // Don't touch connection state or trigger reconnect.
                        ESP_LOGD(TAG, "Ignoring disconnect from %s (connected to %s)",
                                 evt.data.disconnect.ssid, g_wifi_mgr->connected_ssid);
                        break;
                    }

                    g_wifi_mgr->state = WIFI_STATE_DISCONNECTED;
                    g_wifi_mgr->connected_ssid[0] = '\0';
                    xEventGroupClearBits(g_wifi_mgr->event_group, WIFI_CONNECTED_BIT);
                    xEventGroupSetBits(g_wifi_mgr->event_group, WIFI_FAIL_BIT);

                    // Emit esp_bus event
                    wifi_disconnected_t disc = {.reason = evt.data.disconnect.reason};
                    strncpy(disc.ssid, evt.data.disconnect.ssid, sizeof(disc.ssid) - 1);
                    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_DISCONNECTED, &disc, sizeof(disc));

                    // Auto reconnect if enabled and not in AP mode
                    if (g_wifi_mgr->config.auto_reconnect && !g_wifi_mgr->ap_active && !g_wifi_mgr->connecting) {
                        // Exponential backoff for reconnect
                        uint32_t base = g_wifi_mgr->config.retry_interval_ms;
                        uint32_t max_delay = g_wifi_mgr->config.retry_max_interval_ms;
                        uint32_t delay = base << g_wifi_mgr->retry_count;
                        if (delay > max_delay || delay < base) delay = max_delay;

                        ESP_LOGI(TAG, "Auto-reconnect in %lu ms (attempt %d)",
                                 (unsigned long)delay, g_wifi_mgr->retry_count + 1);
                        g_wifi_mgr->retry_count++;

                        vTaskDelay(pdMS_TO_TICKS(delay));
                        if (!g_wifi_mgr->ap_active) {
                            wifi_mgr_start_connect_sequence();
                        }
                    }
                    break;
                }
                    
                case WM_INT_EVT_GOT_IP: {
                    xEventGroupSetBits(g_wifi_mgr->event_group, WIFI_CONNECTED_BIT);
                    xEventGroupClearBits(g_wifi_mgr->event_group, WIFI_FAIL_BIT);

                    // Initialize mDNS if enabled
                    wifi_mgr_mdns_init();

                    // Emit esp_bus events
                    wifi_connected_t conn = {0};
                    wifi_ap_record_t ap_info;
                    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                        strncpy(conn.ssid, (char *)ap_info.ssid, sizeof(conn.ssid) - 1);
                        conn.rssi = ap_info.rssi;
                        conn.channel = ap_info.primary;
                    }
                    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_CONNECTED, &conn, sizeof(conn));
                    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_GOT_IP, &evt.data.got_ip.ip_info, sizeof(esp_netif_ip_info_t));

                    // Stop AP if configured and AP is active
                    if (g_wifi_mgr->config.stop_ap_on_connect && g_wifi_mgr->ap_active) {
                        ESP_LOGI(TAG, "Stopping AP after successful connection");
                        wifi_mgr_stop_ap_mode();
                    }
                    break;
                }
                    
                case WM_INT_EVT_LOST_IP:
                    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_LOST_IP, NULL, 0);
                    break;
                    
                case WM_INT_EVT_AP_STARTED:
                    g_wifi_mgr->ap_active = true;
                    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_AP_START, NULL, 0);
                    break;
                    
                case WM_INT_EVT_AP_STOPPED:
                    g_wifi_mgr->ap_active = false;
                    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_AP_STOP, NULL, 0);
                    break;
                    
                case WM_INT_EVT_AP_STA_CONN:
                    esp_bus_emit(WIFI_MODULE, WIFI_MGR_EVT_AP_STA_CONNECTED, evt.data.mac, 6);
                    break;
                    
                case WM_INT_EVT_CONNECT_REQUEST:
                    wifi_manager_connect(evt.data.connect_req.ssid);
                    break;
                    
                case WM_INT_EVT_DISCONNECT_REQUEST:
                    wifi_manager_disconnect();
                    break;
                    
                case WM_INT_EVT_START_AP_REQUEST:
                    wifi_manager_start_ap(NULL);
                    break;
                    
                case WM_INT_EVT_STOP_AP_REQUEST:
                    wifi_manager_stop_ap();
                    break;
                    
                case WM_INT_EVT_STOP:
                    ESP_LOGI(TAG, "Task stopped");
                    vTaskDelete(NULL);
                    return;
                    
                default:
                    break;
            }
        }
    }
}

// =============================================================================
// Basic Status API
// =============================================================================

bool wifi_manager_is_connected(void)
{
    if (!g_wifi_mgr) return false;
    // Check both the task-queue-driven state and the ISR-set event bit.
    // The event bit is set synchronously in ip_event_handler and reflects
    // connection status immediately, while g_wifi_mgr->state may lag
    // behind due to async task queue processing.
    return g_wifi_mgr->state == WIFI_STATE_CONNECTED ||
           (xEventGroupGetBits(g_wifi_mgr->event_group) & WIFI_CONNECTED_BIT);
}

wifi_state_t wifi_manager_get_state(void)
{
    if (!g_wifi_mgr) return WIFI_STATE_DISCONNECTED;
    return g_wifi_mgr->state;
}

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    if (!g_wifi_mgr) return ESP_ERR_INVALID_STATE;
    
    EventBits_t bits = xEventGroupWaitBits(g_wifi_mgr->event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_get_status(wifi_status_t *status)
{
    if (!g_wifi_mgr || !status) return ESP_ERR_INVALID_ARG;
    
    memset(status, 0, sizeof(wifi_status_t));
    status->state = g_wifi_mgr->state;
    status->ap_active = g_wifi_mgr->ap_active;
    
    if (g_wifi_mgr->state == WIFI_STATE_CONNECTED) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            strncpy(status->ssid, (char *)ap_info.ssid, sizeof(status->ssid) - 1);
            memcpy(status->bssid, ap_info.bssid, 6);
            status->rssi = ap_info.rssi;
            status->quality = rssi_to_quality(ap_info.rssi);
            status->channel = ap_info.primary;
        }
        
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(g_wifi_mgr->sta_netif, &ip_info) == ESP_OK) {
            snprintf(status->ip, sizeof(status->ip), IPSTR, IP2STR(&ip_info.ip));
            snprintf(status->netmask, sizeof(status->netmask), IPSTR, IP2STR(&ip_info.netmask));
            snprintf(status->gateway, sizeof(status->gateway), IPSTR, IP2STR(&ip_info.gw));
        }
        
        esp_netif_dns_info_t dns_info;
        if (esp_netif_get_dns_info(g_wifi_mgr->sta_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
            snprintf(status->dns, sizeof(status->dns), IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        }
        
        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
            snprintf(status->mac, sizeof(status->mac), MACSTR, MAC2STR(mac));
        }
        
        const char *hostname = NULL;
        if (esp_netif_get_hostname(g_wifi_mgr->sta_netif, &hostname) == ESP_OK && hostname) {
            strncpy(status->hostname, hostname, sizeof(status->hostname) - 1);
        }
        
        if (g_wifi_mgr->connect_time > 0) {
            status->uptime_ms = (uint32_t)((esp_timer_get_time() / 1000) - g_wifi_mgr->connect_time);
        }
    }
    
    return ESP_OK;
}

httpd_handle_t wifi_manager_get_httpd(void)
{
    return g_wifi_mgr ? g_wifi_mgr->httpd : NULL;
}

esp_err_t wifi_manager_factory_reset(void)
{
    if (!g_wifi_mgr) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "Factory reset requested");

    // Erase NVS data
    esp_err_t ret = wifi_mgr_nvs_factory_reset();
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear in-memory data
    wifi_mgr_lock();
    g_wifi_mgr->network_count = 0;
    memset(g_wifi_mgr->networks, 0, sizeof(g_wifi_mgr->networks));
    g_wifi_mgr->var_count = 0;
    memset(g_wifi_mgr->vars, 0, sizeof(g_wifi_mgr->vars));
    wifi_mgr_unlock();

    return ESP_OK;
}
