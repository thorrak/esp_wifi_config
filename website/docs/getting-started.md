---
sidebar_position: 1
title: Getting Started
description: Install and configure ESP WiFi Config in your ESP-IDF project
---

# Getting Started

## Prerequisites

- ESP-IDF >= 5.4
- An ESP32-series target (ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, or ESP32-H2)

## Installation

### Using ESP-IDF Component Manager (Recommended)

Add to your project's `idf_component.yml` (in the `main/` directory):

```yaml
dependencies:
  thorrak/esp_wifi_config: "*"
```

The component manager will download `esp_wifi_config` on the next build. It has no third-party dependencies.

### Manual Installation

Clone into your project's `components/` directory:

```bash
cd components
git clone https://github.com/thorrak/esp_wifi_config.git
```

## Quick Start

This minimal example connects to WiFi with automatic provisioning when no networks are saved or all saved networks fail:

```c
#include "esp_wifi_config.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "my_app";

static void on_connected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    wifi_connected_t *info = (wifi_connected_t *)data;
    ESP_LOGI(TAG, "Connected to %s, RSSI: %d", info->ssid, info->rssi);
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    wifi_got_ip_t *info = (wifi_got_ip_t *)data;
    ESP_LOGI(TAG, "Got IP: %s", info->ip);
}

void app_main(void)
{
    // Initialize NVS (required)
    nvs_flash_init();

    // Events are published on the default event loop. Create it before
    // registering so you catch what is emitted during startup —
    // wifi_cfg_init() creates it too, and a second create is harmless.
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_CONNECTED, on_connected, NULL);
    esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP,    on_got_ip,    NULL);

    // Initialize WiFi Config. Always start from WIFI_CFG_DEFAULTS — it
    // carries every documented default, and wifi_cfg_init() does not patch
    // fields you leave at zero.
    wifi_cfg_init(&(wifi_cfg_config_t){
        WIFI_CFG_DEFAULTS,
        .default_networks = (wifi_network_t[]){
            {"HomeWifi", "password123", 10},   // priority 10 (highest)
            {"OfficeWifi", "office456", 5},    // priority 5 (fallback)
        },
        .default_network_count = 2,

        // provisioning_mode defaults to WIFI_PROV_ON_FAILURE: the AP starts
        // when no networks are saved or every saved network fails.
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,
    });

    // Wait for connection (30 second timeout)
    if (wifi_cfg_wait_connected(30000) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected!");
    }
}
```

### What This Does

1. Initializes NVS (a required prerequisite) and registers event handlers
2. Starts from `WIFI_CFG_DEFAULTS` and overrides only what differs — retry
   policy, `auto_reconnect = true` and `provisioning_mode =
   WIFI_PROV_ON_FAILURE` all come from the macro. `wifi_cfg_init(NULL)`
   gives you the defaults unmodified; a config built *without* the macro is
   rejected with `ESP_ERR_INVALID_ARG` (zero retry interval).
3. Subscribes to connected and got-IP events
4. Tries saved networks from NVS first (sorted by priority, highest first)
5. If no networks are saved, uses the default networks provided in the config
6. If all networks fail, starts a SoftAP captive portal so the user can configure WiFi via a web browser
7. After connecting, waits 5 seconds then tears down the provisioning interfaces

### Required sdkconfig

No special sdkconfig is needed for basic WiFi — the defaults work. To enable optional features, see [Kconfig Options](./api/kconfig.md).

## Building and Flashing

```bash
idf.py set-target esp32s3   # or esp32, esp32c3, etc.
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Next Steps

- [Provisioning Modes](./provisioning/modes.md) — Control when AP/BLE/Improv start
- [Kconfig Options](./api/kconfig.md) — Enable Web UI, CLI, Network Provisioning BLE, Improv
- [Examples](./examples.md) — Complete example projects
- [AI Integration Guide](./ai-integration-guide.md) — Scenario-based configuration recipes
