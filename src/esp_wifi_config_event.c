/**
 * @file esp_wifi_config_event.c
 * @brief Event base definition and posting
 *
 * The library publishes its events on ESP-IDF's default event loop, the same
 * loop it already consumes WIFI_EVENT, IP_EVENT and WIFI_PROV_EVENT from. It
 * creates that loop itself during wifi_cfg_init(), so there is no separate
 * event system to initialise, no task of our own, and no subscriber table --
 * esp_event owns all three.
 *
 * Handlers therefore run on the system event loop task, not on the task that
 * emitted the event. That is what keeps a slow subscriber from stalling the
 * WiFi state machine, and keeps application code off the httpd task's stack.
 */

#include "esp_wifi_config_priv.h"
#include "esp_log.h"

static const char *TAG = "wifi_cfg_event";

ESP_EVENT_DEFINE_BASE(WIFI_CFG_EVENT);

// =============================================================================
// Public API
// =============================================================================

const char *wifi_cfg_event_name(wifi_cfg_event_t event)
{
    // Indexed by wifi_cfg_event_t; keep in step with the enum.
    static const char *const names[WIFI_CFG_EVENT_MAX] = {
        [WIFI_CFG_EVENT_CONNECTED]            = "connected",
        [WIFI_CFG_EVENT_DISCONNECTED]         = "disconnected",
        [WIFI_CFG_EVENT_CONNECTING]           = "connecting",
        [WIFI_CFG_EVENT_SCAN_DONE]            = "scan_done",
        [WIFI_CFG_EVENT_GOT_IP]               = "got_ip",
        [WIFI_CFG_EVENT_LOST_IP]              = "lost_ip",
        [WIFI_CFG_EVENT_AP_START]             = "ap_start",
        [WIFI_CFG_EVENT_AP_STOP]              = "ap_stop",
        [WIFI_CFG_EVENT_AP_STA_CONNECTED]     = "ap_sta_connected",
        [WIFI_CFG_EVENT_NETWORK_ADDED]        = "network_added",
        [WIFI_CFG_EVENT_NETWORK_UPDATED]      = "network_updated",
        [WIFI_CFG_EVENT_NETWORK_REMOVED]      = "network_removed",
        [WIFI_CFG_EVENT_VAR_CHANGED]          = "var_changed",
        [WIFI_CFG_EVENT_PROVISIONING_STARTED] = "provisioning_started",
        [WIFI_CFG_EVENT_PROVISIONING_STOPPED] = "provisioning_stopped",
        [WIFI_CFG_EVENT_PROV_CRED_RECV]       = "prov_cred_recv",
        [WIFI_CFG_EVENT_PROV_CRED_FAIL]       = "prov_cred_fail",
        [WIFI_CFG_EVENT_PROV_CRED_SUCCESS]    = "prov_cred_success",
    };

    if (event < 0 || event >= WIFI_CFG_EVENT_MAX) return "unknown";
    return names[event] ? names[event] : "unknown";
}

// =============================================================================
// Internal
// =============================================================================

void wifi_cfg_event_post(wifi_cfg_event_t event, const void *data, size_t len)
{
    // Zero timeout on purpose. Several of these fire from the WiFi state
    // machine; blocking it because a subscriber is slow would trade a lost
    // notification for a stalled reconnect, which is the worse failure.
    esp_err_t err = esp_event_post(WIFI_CFG_EVENT, (int32_t)event, data, len, 0);

    if (err != ESP_OK) {
        // The predecessor dropped events here silently -- esp_bus_emit()
        // enqueued with a zero timeout and every call site discarded its
        // return. A full loop queue is still a dropped event, but it is no
        // longer an invisible one.
        ESP_LOGW(TAG, "event '%s' not posted: %s",
                 wifi_cfg_event_name(event), esp_err_to_name(err));
    }
}
