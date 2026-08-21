---
sidebar_position: 3
title: Events
description: Event-driven architecture — subscribing to WiFi events with callbacks
---

# Events

ESP WiFi Config notifies your application through callbacks rather than making
you poll for state changes. Subscribe to the events you care about and the
library calls you when they happen.

There is nothing to initialise. Subscriptions live in a fixed-size table that is
valid before `wifi_cfg_init()` — subscribe first, and you will catch the events
emitted during startup.

```c
#include "esp_wifi_config.h"

wifi_cfg_event_subscribe(WIFI_CFG_EVENT_CONNECTED, on_connected, NULL, NULL);
```

## Available Events

| Event | Payload Type | Description |
|---|---|---|
| `WIFI_CFG_EVENT_CONNECTED` | `wifi_connected_t` | STA associated with an AP |
| `WIFI_CFG_EVENT_DISCONNECTED` | `wifi_disconnected_t` | STA lost its association |
| `WIFI_CFG_EVENT_CONNECTING` | `char[]` (SSID) | Association attempt started |
| `WIFI_CFG_EVENT_SCAN_DONE` | `uint16_t` (AP count) | WiFi scan completed |
| `WIFI_CFG_EVENT_GOT_IP` | `esp_netif_ip_info_t` | STA obtained an IP address |
| `WIFI_CFG_EVENT_LOST_IP` | none | IP lease lost |
| `WIFI_CFG_EVENT_AP_START` | none | SoftAP started |
| `WIFI_CFG_EVENT_AP_STOP` | none | SoftAP stopped |
| `WIFI_CFG_EVENT_AP_STA_CONNECTED` | `uint8_t[6]` (MAC) | A station joined the SoftAP |
| `WIFI_CFG_EVENT_NETWORK_ADDED` | `wifi_network_t` | Network added to the store |
| `WIFI_CFG_EVENT_NETWORK_UPDATED` | `wifi_network_t` | Stored network updated |
| `WIFI_CFG_EVENT_NETWORK_REMOVED` | `char[]` (SSID) | Network removed from the store |
| `WIFI_CFG_EVENT_VAR_CHANGED` | `wifi_var_t` | A custom variable was set or deleted |
| `WIFI_CFG_EVENT_PROVISIONING_STARTED` | none | Provisioning interfaces started |
| `WIFI_CFG_EVENT_PROVISIONING_STOPPED` | none | Provisioning interfaces stopped |
| `WIFI_CFG_EVENT_PROV_CRED_RECV` | `wifi_cfg_prov_creds_t` | Provisioning client sent credentials |
| `WIFI_CFG_EVENT_PROV_CRED_FAIL` | `int` (reason) | Connect with provisioned credentials failed |
| `WIFI_CFG_EVENT_PROV_CRED_SUCCESS` | none | Connect with provisioned credentials succeeded |

Pass `WIFI_CFG_EVENT_ANY` instead of a specific id to receive all of them
through one handler.

## Callback Signature

```c
void callback(wifi_cfg_event_t event, const void *data, size_t len, void *ctx)
```

- `event` — Which event fired. Use `wifi_cfg_event_name()` to turn it into a
  string for logging.
- `data` — Pointer to the payload, or `NULL` for events that carry none. Cast
  it to the type in the table above.
- `len` — Payload size in bytes, `0` when `data` is `NULL`.
- `ctx` — The context pointer you passed when subscribing.

:::warning Callbacks run synchronously
Handlers are invoked **on the task that emitted the event** — usually the
library's internal WiFi task. Two rules follow:

- **Keep handlers short.** They run inline in the library's state machine, so
  blocking delays reconnects, scans and provisioning.
- **Do not call back into `wifi_cfg_*` functions that re-enter the state
  machine** from inside a handler. Set a flag, or post to your own queue, and
  do the work on your own task.

The payload is only valid for the duration of the call. Copy anything you need
to keep.
:::

## Example: Subscribing to Events

```c
static void on_connected(wifi_cfg_event_t event, const void *data, size_t len, void *ctx)
{
    const wifi_connected_t *info = (const wifi_connected_t *)data;
    ESP_LOGI(TAG, "Connected to %s, RSSI: %d", info->ssid, info->rssi);
}

static void on_got_ip(wifi_cfg_event_t event, const void *data, size_t len, void *ctx)
{
    const esp_netif_ip_info_t *ip = (const esp_netif_ip_info_t *)data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ip->ip));
}

static void on_var_changed(wifi_cfg_event_t event, const void *data, size_t len, void *ctx)
{
    const wifi_var_t *var = (const wifi_var_t *)data;
    ESP_LOGI(TAG, "Variable changed: %s = %s", var->key, var->value);
}

void app_main(void)
{
    nvs_flash_init();

    // Subscribe before init to catch events emitted during startup
    wifi_cfg_event_subscribe(WIFI_CFG_EVENT_CONNECTED,   on_connected,   NULL, NULL);
    wifi_cfg_event_subscribe(WIFI_CFG_EVENT_GOT_IP,      on_got_ip,      NULL, NULL);
    wifi_cfg_event_subscribe(WIFI_CFG_EVENT_VAR_CHANGED, on_var_changed, NULL, NULL);

    wifi_cfg_init(&(wifi_cfg_config_t){ WIFI_CFG_DEFAULTS, .enable_ap = true });
}
```

### One handler for everything

```c
static void on_any(wifi_cfg_event_t event, const void *data, size_t len, void *ctx)
{
    ESP_LOGI(TAG, "wifi event: %s", wifi_cfg_event_name(event));

    switch (event) {
        case WIFI_CFG_EVENT_GOT_IP:       /* ... */ break;
        case WIFI_CFG_EVENT_DISCONNECTED: /* ... */ break;
        default: break;
    }
}

wifi_cfg_event_subscribe(WIFI_CFG_EVENT_ANY, on_any, NULL, NULL);
```

## Unsubscribing

Pass a handle out of `wifi_cfg_event_subscribe()` and hand it back later:

```c
int handle;
wifi_cfg_event_subscribe(WIFI_CFG_EVENT_GOT_IP, on_got_ip, NULL, &handle);
// ...
wifi_cfg_event_unsubscribe(handle);
```

Subscriptions survive `wifi_cfg_deinit()`, so an application that subscribes
once at startup does not need to re-subscribe after a deinit/init cycle.

## Table Size

The subscription table holds `CONFIG_WIFI_CFG_MAX_EVENT_SUBS` entries (default
8, about 12 bytes each) and never grows. Subscribing past the limit returns
`ESP_ERR_NO_MEM` rather than allocating, so the failure shows up at startup
instead of as a dropped event later. The library itself uses two slots when
Improv is enabled.

## Event Timing

- `CONNECTED` fires when the WiFi STA association completes (before IP assignment)
- `GOT_IP` fires when DHCP assigns an IP address — this is typically the event you want to trigger application logic
- `PROVISIONING_STOPPED` fires after the teardown delay (`provisioning_teardown_delay_ms`) when `stop_provisioning_on_connect` is true
