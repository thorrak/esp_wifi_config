/**
 * @file esp_wifi_config_event.c
 * @brief Event subscription table and synchronous dispatch
 *
 * Replaces the esp_bus module this library used to register. The bus gave us
 * asynchronous, pattern-matched delivery on its own task; what the library
 * actually needed was "call these functions when this happens", so that is
 * what this is.
 *
 * The table is a fixed-size array of slots, not a linked list. A subscription
 * never allocates, dispatch never allocates, and a full table is reported to
 * the caller instead of being grown. That trade is deliberate: the failure is
 * visible at subscribe time, at startup, rather than as a heap exhaustion or a
 * silently dropped event much later.
 *
 * Dispatch is synchronous and runs on the emitting task. See the warning on
 * ::wifi_cfg_event_cb_t for what that means for handlers.
 */

#include "esp_wifi_config_priv.h"
#include <string.h>

#ifndef CONFIG_WIFI_CFG_MAX_EVENT_SUBS
#define CONFIG_WIFI_CFG_MAX_EVENT_SUBS 8
#endif

typedef struct {
    wifi_cfg_event_cb_t cb;   ///< NULL marks a free slot
    void               *ctx;
    uint8_t             event; ///< event id, or WIFI_CFG_EVENT_ANY
} sub_slot_t;

static sub_slot_t s_subs[CONFIG_WIFI_CFG_MAX_EVENT_SUBS];

// Guards the table only. Handlers are called with the lock released, so a
// handler is free to subscribe, unsubscribe, or emit without deadlocking.
static portMUX_TYPE s_subs_lock = portMUX_INITIALIZER_UNLOCKED;

// =============================================================================
// Public API
// =============================================================================

esp_err_t wifi_cfg_event_subscribe(wifi_cfg_event_t event, wifi_cfg_event_cb_t cb,
                                   void *ctx, int *out_handle)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    if (event >= WIFI_CFG_EVENT_MAX && event != WIFI_CFG_EVENT_ANY) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_subs_lock);
    for (int i = 0; i < CONFIG_WIFI_CFG_MAX_EVENT_SUBS; i++) {
        if (s_subs[i].cb == NULL) {
            s_subs[i].ctx   = ctx;
            s_subs[i].event = (uint8_t)event;
            s_subs[i].cb    = cb;   // written last: this is what makes it live
            portEXIT_CRITICAL(&s_subs_lock);
            if (out_handle) *out_handle = i;
            return ESP_OK;
        }
    }
    portEXIT_CRITICAL(&s_subs_lock);

    // Out of slots. Raise CONFIG_WIFI_CFG_MAX_EVENT_SUBS rather than retrying.
    return ESP_ERR_NO_MEM;
}

esp_err_t wifi_cfg_event_unsubscribe(int handle)
{
    if (handle < 0 || handle >= CONFIG_WIFI_CFG_MAX_EVENT_SUBS) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_subs_lock);
    s_subs[handle].cb  = NULL;
    s_subs[handle].ctx = NULL;
    portEXIT_CRITICAL(&s_subs_lock);
    return ESP_OK;
}

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

    if (event >= WIFI_CFG_EVENT_MAX) return "unknown";
    return names[event] ? names[event] : "unknown";
}

// =============================================================================
// Internal
// =============================================================================

void wifi_cfg_event_post(wifi_cfg_event_t event, const void *data, size_t len)
{
    // Snapshot the matching slots so handlers run outside the lock. The table
    // is small and fixed, so this is a bounded stack copy rather than a malloc.
    sub_slot_t matched[CONFIG_WIFI_CFG_MAX_EVENT_SUBS];
    int n = 0;

    portENTER_CRITICAL(&s_subs_lock);
    for (int i = 0; i < CONFIG_WIFI_CFG_MAX_EVENT_SUBS; i++) {
        if (s_subs[i].cb &&
            (s_subs[i].event == (uint8_t)event ||
             s_subs[i].event == (uint8_t)WIFI_CFG_EVENT_ANY)) {
            matched[n++] = s_subs[i];
        }
    }
    portEXIT_CRITICAL(&s_subs_lock);

    for (int i = 0; i < n; i++) {
        matched[i].cb(event, data, len, matched[i].ctx);
    }
}
