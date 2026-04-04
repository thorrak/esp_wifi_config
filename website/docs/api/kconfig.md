---
sidebar_position: 5
title: Kconfig Options
description: Compile-time configuration options via menuconfig
---

# Kconfig Options

Configure via `idf.py menuconfig` → WiFi Config, or set in `sdkconfig.defaults`.

## Core Options

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_MAX_NETWORKS` | 5 | Maximum number of saved networks |
| `CONFIG_WIFI_CFG_MAX_VARS` | 10 | Maximum number of custom variables |
| `CONFIG_WIFI_CFG_DEFAULT_RETRY` | 3 | Retries per network before moving to next |
| `CONFIG_WIFI_CFG_RETRY_INTERVAL_MS` | 5000 | Base retry interval in milliseconds |

## SoftAP Defaults

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_AP_SSID` | "ESP32-Config" | Default AP SSID (supports `&#123;id&#125;` placeholder) |
| `CONFIG_WIFI_CFG_AP_PASSWORD` | "" | Default AP password (empty = open) |
| `CONFIG_WIFI_CFG_AP_IP` | "192.168.4.1" | Default AP IP address |

## CLI

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_ENABLE_CLI` | n | Enable serial console CLI commands |

## Web UI

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_ENABLE_WEBUI` | n | Enable the embedded Web UI |
| `CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH` | "" | Path to custom frontend files (LittleFS/SPIFFS) |

## BLE

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_ENABLE_BLE` | n | Enable BLE GATT interface |
| `CONFIG_WIFI_CFG_BLE_DEVICE_NAME` | "ESP32-WiFi-&#123;id&#125;" | BLE device name (supports `&#123;id&#125;` placeholder) |

## Improv WiFi

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_ENABLE_IMPROV` | n | Enable Improv WiFi (master switch) |
| `CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE` | y* | Enable Improv BLE transport (*when IMPROV + BT enabled) |
| `CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL` | n | Enable Improv Serial transport |
| `CONFIG_WIFI_CFG_IMPROV_SERIAL_UART_NUM` | 0 | UART port for Improv Serial |
| `CONFIG_WIFI_CFG_IMPROV_SERIAL_BAUD` | 115200 | Baud rate for Improv Serial |

## Common sdkconfig.defaults Combinations

### Basic WiFi (no extra features)

```kconfig
# No extra config needed — defaults work
```

### WiFi + Web UI

```kconfig
CONFIG_WIFI_CFG_ENABLE_WEBUI=y
```

### WiFi + BLE (Bluedroid)

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_BLE=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

### WiFi + BLE (NimBLE, lighter)

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144
CONFIG_WIFI_CFG_ENABLE_BLE=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

### WiFi + Improv + BLE

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_BLE=y
CONFIG_WIFI_CFG_ENABLE_IMPROV=y
CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

### Kitchen Sink (all features)

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
