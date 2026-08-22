---
sidebar_position: 3
title: Events
description: Event-driven architecture — handling WiFi events on the default event loop
---

# Events

ESP WiFi Config publishes its events on ESP-IDF's default event loop, under the
event base `WIFI_CFG_EVENT`. If you already handle `WIFI_EVENT` or `IP_EVENT`,
there is nothing new to learn — it is the same API.

```c
#include "esp_wifi_config.h"

esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP,
                           on_got_ip, NULL);
```

## Available Events

| Event ID | Payload Type | Description |
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

Pass `ESP_EVENT_ANY_ID` instead of a specific id to receive all of them through
one handler.

## Handler Signature

The standard esp_event handler:

```c
void handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
```

- `arg` — the context pointer you passed when registering
- `base` — always `WIFI_CFG_EVENT` for these
- `event_id` — one of the ids above. `wifi_cfg_event_name(event_id)` gives you a
  string for logging.
- `event_data` — pointer to the payload, or `NULL` for events that carry none.
  The loop hands you a private copy, valid for the duration of the handler.

:::note Handlers run on the event loop task
Not on the task that emitted the event — so a slow handler will not stall the
WiFi state machine, block an HTTP request, or consume the httpd task's stack.

It does share the system event loop with `WIFI_EVENT` and `IP_EVENT` handlers,
so a handler that blocks delays those. Keep handlers short and push heavy work
onto your own task.
:::

## Registering

The default event loop must exist before you register. `wifi_cfg_init()` creates
it, so you have two options:

**Register before init** (catches events emitted during startup) — create the
loop yourself first. Creating it twice is harmless; the library tolerates a loop
that already exists.

```c
void app_main(void)
{
    nvs_flash_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_CONNECTED, on_connected, NULL);
    esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP,    on_got_ip,    NULL);

    wifi_cfg_init(&(wifi_cfg_config_t){ WIFI_CFG_DEFAULTS, .enable_ap = true });
}
```

**Register after init** — simpler, but you miss anything emitted while
`wifi_cfg_init()` was running.

## Example Handlers

```c
static void on_connected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const wifi_connected_t *info = (const wifi_connected_t *)data;
    ESP_LOGI(TAG, "Connected to %s, RSSI: %d", info->ssid, info->rssi);
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const esp_netif_ip_info_t *ip = (const esp_netif_ip_info_t *)data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ip->ip));
}

static void on_var_changed(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const wifi_var_t *var = (const wifi_var_t *)data;
    ESP_LOGI(TAG, "Variable changed: %s = %s", var->key, var->value);
}
```

### One handler for everything

```c
static void on_any(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ESP_LOGI(TAG, "wifi event: %s", wifi_cfg_event_name(id));

    switch (id) {
        case WIFI_CFG_EVENT_GOT_IP:       /* ... */ break;
        case WIFI_CFG_EVENT_DISCONNECTED: /* ... */ break;
        default: break;
    }
}

esp_event_handler_register(WIFI_CFG_EVENT, ESP_EVENT_ANY_ID, on_any, NULL);
```

## Unregistering

Use the instance API when you need to unregister later:

```c
esp_event_handler_instance_t inst;
esp_event_handler_instance_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP,
                                    on_got_ip, NULL, &inst);
// ...
esp_event_handler_instance_unregister(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP, inst);
```

Registrations are owned by the event loop, not by the library, so they survive
`wifi_cfg_deinit()`.

## Dropped Events

`esp_event_post()` is called with a zero timeout so that a full loop queue never
blocks the WiFi state machine — trading a stalled reconnect for a lost
notification would be the worse failure. If a post does fail, the library logs
it at warning level:

```
W (12345) wifi_cfg_event: event 'got_ip' not posted: ESP_ERR_TIMEOUT
```

If you see that, raise `CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE` or find the handler
that is blocking the loop.

## Event Timing

- `CONNECTED` fires when the WiFi STA association completes (before IP assignment)
- `GOT_IP` fires when DHCP assigns an IP address — this is typically the event you want to trigger application logic
- `PROVISIONING_STOPPED` fires after the teardown delay (`provisioning_teardown_delay_ms`) when `stop_provisioning_on_connect` is true

Because delivery is asynchronous, a handler runs shortly *after* the state change
that caused it. `wifi_cfg_add_network()` returns before its
`NETWORK_ADDED` handler runs.
