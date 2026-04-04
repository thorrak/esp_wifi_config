---
sidebar_position: 1
title: Getting Started
description: Install and configure ESP WiFi Config in your ESP-IDF project
---

# Getting Started

## Prerequisites

- ESP-IDF >= 5.0.0
- An ESP32-series target (ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, or ESP32-H2)

## Installation

### Using ESP-IDF Component Manager (Recommended)

Add to your project's `idf_component.yml` (in the `main/` directory):

```yaml
dependencies:
  thorrak/esp_wifi_config: "*"
```

The component manager will automatically download `esp_wifi_config` and its dependency `esp_bus` on the next build.

### Manual Installation

Clone into your project's `components/` directory:

```bash
cd components
git clone https://github.com/thorrak/esp_wifi_config.git
```

You will also need to install [esp_bus](https://github.com/thorrak/esp_bus) the same way.

## Quick Start

This minimal example connects to WiFi with automatic provisioning when no networks are saved or all saved networks fail:

```c
#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "my_app";

static void on_connected(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_connected_t *info = (wifi_connected_t *)data;
    ESP_LOGI(TAG, "Connected to %s, RSSI: %d", info->ssid, info->rssi);
}

static void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_got_ip_t *info = (wifi_got_ip_t *)data;
    ESP_LOGI(TAG, "Got IP: %s", info->ip);
}

void app_main(void)
{
    // Initialize NVS (required)
    nvs_flash_init();

    // Initialize esp_bus (required before wifi_cfg_init)
    esp_bus_init();

    // Subscribe to WiFi events (optional)
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_connected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);

    // Initialize WiFi Config
    wifi_cfg_init(&(wifi_cfg_config_t){
        .default_networks = (wifi_network_t[]){
            {"HomeWifi", "password123", 10},   // priority 10 (highest)
            {"OfficeWifi", "office456", 5},    // priority 5 (fallback)
        },
        .default_network_count = 2,

        // Start AP provisioning when no networks saved or all fail
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
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

1. Initializes NVS and esp_bus (both required prerequisites)
2. Subscribes to connected and got-IP events
3. Tries saved networks from NVS first (sorted by priority, highest first)
4. If no networks are saved, uses the default networks provided in the config
5. If all networks fail, starts a SoftAP captive portal so the user can configure WiFi via a web browser
6. After connecting, waits 5 seconds then tears down the provisioning interfaces

### Required sdkconfig

No special sdkconfig is needed for basic WiFi — the defaults work. To enable optional features, see [Kconfig Options](./api/kconfig).

## Building and Flashing

```bash
idf.py set-target esp32s3   # or esp32, esp32c3, etc.
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Next Steps

- [Provisioning Modes](./provisioning/modes) — Control when AP/BLE/Improv start
- [Kconfig Options](./api/kconfig) — Enable Web UI, CLI, BLE, Improv
- [Examples](./examples) — Complete example projects
- [AI Integration Guide](./ai-integration-guide) — Scenario-based configuration recipes
