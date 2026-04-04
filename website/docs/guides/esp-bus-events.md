---
sidebar_position: 3
title: esp_bus Events
description: Event-driven architecture — subscribing to WiFi events and using actions
---

# esp_bus Events

ESP WiFi Config uses [esp_bus](https://components.espressif.com/components/thorrak/esp_bus) for event-driven communication. Your application subscribes to events rather than polling for state changes.

## Prerequisites

```c
#include "esp_bus.h"

// esp_bus must be initialized before wifi_cfg_init
esp_bus_init();
```

## Available Events

Subscribe using the `WIFI_EVT()` macro, which generates the full event name string:

```c
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_connected, NULL);
```

| Event | Payload Type | Description |
|---|---|---|
| `WIFI_CFG_EVT_CONNECTED` | `wifi_connected_t` | STA connected to an AP |
| `WIFI_CFG_EVT_DISCONNECTED` | `wifi_disconnected_t` | STA disconnected from AP |
| `WIFI_CFG_EVT_GOT_IP` | `wifi_got_ip_t` | STA obtained an IP address |
| `WIFI_CFG_EVT_SCAN_DONE` | `wifi_scan_done_t` | WiFi scan completed |
| `WIFI_CFG_EVT_NETWORK_ADDED` | `wifi_network_changed_t` | A network was added or updated |
| `WIFI_CFG_EVT_VAR_CHANGED` | `wifi_var_changed_t` | A custom variable was changed |
| `WIFI_CFG_EVT_PROVISIONING_STARTED` | `NULL` | Provisioning interfaces started |
| `WIFI_CFG_EVT_PROVISIONING_STOPPED` | `NULL` | Provisioning interfaces stopped |

## Callback Signature

All event callbacks use the same signature:

```c
void callback(const char *event, const void *data, size_t len, void *ctx)
```

- `event` — The event name string
- `data` — Pointer to the payload struct (cast to the appropriate type)
- `len` — Size of the payload
- `ctx` — User context pointer (passed when subscribing)

## Example: Subscribing to Events

```c
static void on_connected(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_connected_t *info = (wifi_connected_t *)data;
    ESP_LOGI(TAG, "Connected to %s, RSSI: %d", info->ssid, info->rssi);
}

static void on_disconnected(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_disconnected_t *info = (wifi_disconnected_t *)data;
    ESP_LOGI(TAG, "Disconnected from %s, reason: %d", info->ssid, info->reason);
}

static void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_got_ip_t *info = (wifi_got_ip_t *)data;
    ESP_LOGI(TAG, "Got IP: %s", info->ip);
}

static void on_scan_done(const char *event, const void *data, size_t len, void *ctx)
{
    ESP_LOGI(TAG, "WiFi scan completed");
}

// Subscribe to all events
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_connected, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_disconnected, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_SCAN_DONE), on_scan_done, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_NETWORK_ADDED), on_network_added, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_VAR_CHANGED), on_var_changed, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_PROVISIONING_STARTED), on_prov_started, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_PROVISIONING_STOPPED), on_prov_stopped, NULL);
```

## Event Timing

- `CONNECTED` fires when the WiFi STA association completes (before IP assignment)
- `GOT_IP` fires when DHCP assigns an IP address — this is typically the event you want to trigger application logic
- `PROVISIONING_STOPPED` fires after the teardown delay (`provisioning_teardown_delay_ms`) when `stop_provisioning_on_connect` is true
