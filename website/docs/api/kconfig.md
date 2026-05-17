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

## CLI

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_ENABLE_CLI` | n | Enable serial console CLI commands |

## Web UI

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_ENABLE_WEBUI` | n | Enable the embedded Web UI |
| `CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH` | "" | Path to custom frontend files (LittleFS/SPIFFS) |

## Network Provisioning (BLE)

Only two Kconfig symbols gate Network Provisioning — everything else
(security version, PoP, device name template, SRP6a username,
auto-reset behaviour, retry threshold) is plain runtime configuration
on `wifi_cfg_prov_config_t`. See [C API → `.prov_ble`](./c-api#prov-network-provisioning).

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING` | n | Enable ESP-IDF Wi-Fi/Network Provisioning manager |
| `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE` | y | Use the BLE scheme (currently the only transport supported by this library) |

The previous custom BLE GATT option (`WIFI_CFG_ENABLE_CUSTOM_BLE`) has
been **removed** in 0.1.0. See [MIGRATION.md][migrate] for upgrade
notes.

[migrate]: https://github.com/thorrak/esp_wifi_config/blob/main/MIGRATION.md

## Improv WiFi

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE` | n | Enable Improv BLE transport (mutually exclusive with Network Provisioning) |
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

### WiFi + Network Provisioning over BLE (NimBLE, recommended)

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144
CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y
CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

Pair this sdkconfig with the runtime parameters (security version, PoP,
etc.) in your `wifi_cfg_init()` call — see the [C API
reference](./c-api#prov-network-provisioning).

### WiFi + Network Provisioning over BLE (Bluedroid)

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y
CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

### WiFi + Improv BLE Only

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

### WiFi + Improv Serial + Network Provisioning BLE

```kconfig
# Improv Serial is independent of BLE and safe to combine with Network Provisioning.
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y
CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE=y
CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

### Kitchen Sink (CLI + WebUI + Network Provisioning + Improv Serial)

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_CLI=y
CONFIG_WIFI_CFG_ENABLE_WEBUI=y
CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y
CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE=y
CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```
