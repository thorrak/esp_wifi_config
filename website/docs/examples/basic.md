---
sidebar_position: 1
title: Basic
description: Minimal WiFi Config setup with default networks, REST API, and captive portal
---

# Basic Example

[View source on GitHub](https://github.com/thorrak/esp_wifi_config/tree/main/examples/basic)

Demonstrates the core functionality: default networks, HTTP REST API, and SoftAP captive portal for provisioning.

## Features

- Initialize WiFi Config with default networks and custom variables
- HTTP REST API for remote configuration
- SoftAP captive portal when no networks connect
- Event subscriptions for connected/disconnected/got-IP/var-changed

## sdkconfig.defaults

```kconfig
CONFIG_IDF_TARGET="esp32s3"
```

No special configuration needed — the defaults handle everything.

## main.c

```c
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi_config.h"
#include "esp_bus.h"

static const char *TAG = "wifi_example";

static void on_wifi_connected(const char *event, const void *data, size_t len, void *ctx)
{
    const wifi_connected_t *info = (const wifi_connected_t *)data;
    ESP_LOGI(TAG, "WiFi connected to %s (RSSI: %d dBm, Channel: %d)",
             info->ssid, info->rssi, info->channel);
}

static void on_wifi_disconnected(const char *event, const void *data, size_t len, void *ctx)
{
    const wifi_disconnected_t *info = (const wifi_disconnected_t *)data;
    ESP_LOGW(TAG, "WiFi disconnected from %s (reason: %d)", info->ssid, info->reason);
}

static void on_wifi_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_status_t status;
    if (wifi_cfg_get_status(&status) == ESP_OK) {
        ESP_LOGI(TAG, "Got IP: %s, Gateway: %s", status.ip, status.gateway);
    }
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize esp_bus (required before wifi_cfg_init)
    esp_bus_init();

    // Subscribe to events before init to catch early events
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_wifi_connected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_wifi_disconnected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_wifi_got_ip, NULL);

    // Initialize WiFi Config
    wifi_cfg_init(&(wifi_cfg_config_t){
        .default_networks = (wifi_network_t[]){
            {"YourWiFi", "YourPassword", 10},
            {"BackupWiFi", "BackupPassword", 5},
        },
        .default_network_count = 2,

        .default_vars = (wifi_var_t[]){
            {"device_name", "my-esp32"},
            {"server_url", "https://api.example.com"},
        },
        .default_var_count = 2,

        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,

        .http = {
            .api_base_path = "/api/wifi",
        },
    });

    // Wait for connection
    if (wifi_cfg_wait_connected(30000) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected!");
    } else {
        ESP_LOGW(TAG, "Timeout — captive portal active at http://192.168.4.1");
    }
}
```

## Usage

1. Edit the default network credentials in `main.c` (or leave defaults and use the captive portal)
2. Build and flash: `idf.py build && idf.py -p /dev/ttyUSB0 flash monitor`
3. If WiFi connects, the REST API is available at `http://<device-ip>/api/wifi/`
4. If WiFi fails, connect to the "ESP32-Setup" AP and configure via `http://192.168.4.1/api/wifi/`
