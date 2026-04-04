---
sidebar_position: 2
title: AI Integration Guide
description: For AI coding assistants — how to integrate ESP WiFi Config into an ESP-IDF project based on user requirements
---

# AI Integration Guide

This page is written for AI coding assistants (Claude Code, Codex, Cursor, etc.) to help them integrate ESP WiFi Config into a user's ESP-IDF project. If you're a human, you may also find the scenario-based recipes useful.

## Prerequisites

Every project using ESP WiFi Config needs:

1. **ESP-IDF >= 5.0.0** installed and configured
2. **NVS flash** initialized before calling `wifi_cfg_init()`
3. **esp_bus** initialized before calling `wifi_cfg_init()`

### Minimal idf_component.yml

Create or add to `main/idf_component.yml`:

```yaml
dependencies:
  thorrak/esp_wifi_config: "*"
```

This automatically pulls in `esp_bus` as a transitive dependency.

### Minimal CMakeLists.txt (project root)

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/cmake/project.cmake)
project(my_project)
```

### Minimal CMakeLists.txt (main/)

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ".")
```

## Scenario Recipes

Choose the scenario that matches the user's requirements:

---

### Scenario 1: Basic WiFi (no provisioning UI)

The device connects to known networks. No captive portal, no BLE, no Web UI.

**sdkconfig.defaults:**
```kconfig
# No special config needed
```

**main.c:**
```c
#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "app";

static void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_got_ip_t *info = (wifi_got_ip_t *)data;
    ESP_LOGI(TAG, "Got IP: %s", info->ip);
}

void app_main(void)
{
    nvs_flash_init();
    esp_bus_init();
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);

    wifi_cfg_init(&(wifi_cfg_config_t){
        .default_networks = (wifi_network_t[]){
            {"MyWiFi", "password", 10},
        },
        .default_network_count = 1,
        .auto_reconnect = true,
    });

    wifi_cfg_wait_connected(30000);
}
```

---

### Scenario 2: WiFi + Captive Portal (most common)

The device tries saved networks, falls back to a captive portal if none work. This is the recommended pattern for consumer IoT devices.

**sdkconfig.defaults:**
```kconfig
# No special config needed for basic captive portal
# Optionally enable the Web UI for a nicer experience:
CONFIG_WIFI_CFG_ENABLE_WEBUI=y
```

**main.c:**
```c
#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "app";

static void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_got_ip_t *info = (wifi_got_ip_t *)data;
    ESP_LOGI(TAG, "Connected! IP: %s", info->ip);
}

void app_main(void)
{
    nvs_flash_init();
    esp_bus_init();
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);

    wifi_cfg_init(&(wifi_cfg_config_t){
        .default_networks = (wifi_network_t[]){
            {"MyWiFi", "password", 10},
        },
        .default_network_count = 1,

        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,
    });

    wifi_cfg_wait_connected(30000);
}
```

---

### Scenario 3: WiFi + BLE Provisioning

The device supports provisioning via Bluetooth Low Energy (for smartphone apps or the Python CLI tool).

**sdkconfig.defaults:**
```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144
CONFIG_WIFI_CFG_ENABLE_BLE=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

**main.c:**
```c
#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "app";

static void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_got_ip_t *info = (wifi_got_ip_t *)data;
    ESP_LOGI(TAG, "Connected! IP: %s", info->ip);
}

void app_main(void)
{
    nvs_flash_init();
    esp_bus_init();
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);

    wifi_cfg_init(&(wifi_cfg_config_t){
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,
        .ble = {
            .enable = true,
        },
    });

    wifi_cfg_wait_connected(30000);
}
```

---

### Scenario 4: WiFi + Improv WiFi (Web Bluetooth)

Supports the open Improv WiFi standard for provisioning from Chrome/Edge browsers and the ESPHome companion app.

**sdkconfig.defaults:**
```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_BLE=y
CONFIG_WIFI_CFG_ENABLE_IMPROV=y
CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

**main.c:**
```c
#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "app";

static void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_got_ip_t *info = (wifi_got_ip_t *)data;
    ESP_LOGI(TAG, "Connected! IP: %s", info->ip);
}

void app_main(void)
{
    nvs_flash_init();
    esp_bus_init();
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);

    wifi_cfg_init(&(wifi_cfg_config_t){
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,
        .ble = {
            .enable = true,
        },
        .improv = {
            .enable_ble = true,
            .firmware_name = "my_project",
            .firmware_version = "1.0.0",
            .device_name = "My Device",
        },
    });

    wifi_cfg_wait_connected(30000);
}
```

---

### Scenario 5: Kitchen Sink (all interfaces)

Captive portal + Web UI + CLI + BLE + Improv WiFi. Maximum provisioning flexibility.

**sdkconfig.defaults:**
```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_CLI=y
CONFIG_WIFI_CFG_ENABLE_WEBUI=y
CONFIG_WIFI_CFG_ENABLE_BLE=y
CONFIG_WIFI_CFG_ENABLE_IMPROV=y
CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

**main.c:**
```c
#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "esp_console.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "app";

static void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_got_ip_t *info = (wifi_got_ip_t *)data;
    ESP_LOGI(TAG, "Connected! IP: %s", info->ip);
}

void app_main(void)
{
    nvs_flash_init();
    esp_bus_init();
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);

    wifi_cfg_init(&(wifi_cfg_config_t){
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,
        .http = {
            .api_base_path = "/api/wifi",
            .enable_auth = true,
            .auth_username = "admin",
            .auth_password = "changeme",
        },
        .ble = {
            .enable = true,
        },
        .improv = {
            .enable_ble = true,
            .firmware_name = "my_project",
            .firmware_version = "1.0.0",
            .device_name = "My Device",
        },
    });

    // Start CLI (requires CONFIG_WIFI_CFG_ENABLE_CLI=y)
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp> ";
    esp_console_register_help_command();

    esp_console_dev_usb_serial_jtag_config_t hw_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl);
    esp_console_start_repl(repl);

    wifi_cfg_wait_connected(30000);
}
```

## Common Patterns

### Subscribing to Events

```c
// Subscribe BEFORE wifi_cfg_init() to catch early events
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_connected, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_disconnected, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);
```

### Custom Variables for App Settings

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .default_vars = (wifi_var_t[]){
        {"server_url", "https://api.example.com"},
        {"device_name", "my-device"},
        {"update_interval", "60"},
    },
    .default_var_count = 3,
    // ... other config
});

// Read a variable
char url[128];
wifi_cfg_get_var("server_url", url, sizeof(url));
```

### Sharing the HTTP Server

```c
// After wifi_cfg_init(), get the HTTP server handle to add custom routes
httpd_handle_t server = wifi_cfg_get_httpd();
if (server) {
    httpd_uri_t my_endpoint = {
        .uri = "/api/my-data",
        .method = HTTP_GET,
        .handler = my_handler,
    };
    httpd_register_uri_handler(server, &my_endpoint);
}
```

## Gotchas

1. **Must init NVS first**: Call `nvs_flash_init()` before `wifi_cfg_init()`. Handle the `ESP_ERR_NVS_NO_FREE_PAGES` case for robustness.
2. **Must init esp_bus first**: Call `esp_bus_init()` before `wifi_cfg_init()`.
3. **BLE requires Bluetooth enabled**: `CONFIG_BT_ENABLED=y` and either `CONFIG_BT_BLUEDROID_ENABLED=y` or `CONFIG_BT_NIMBLE_ENABLED=y`.
4. **BLE needs larger partition table**: Use `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` when enabling BLE.
5. **NimBLE needs stack size**: Set `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144` when using NimBLE.
6. **Improv requires BLE**: Enabling Improv BLE implicitly requires `CONFIG_WIFI_CFG_ENABLE_BLE=y` and a Bluetooth stack.
7. **Default networks are seeds**: They're only written to NVS on first boot. After that, NVS is the source of truth.
8. **ESP32 is 2.4GHz only**: The device cannot connect to 5GHz WiFi networks.
9. **Subscribe to events before init**: Call `esp_bus_sub()` before `wifi_cfg_init()` to catch events fired during initialization.
