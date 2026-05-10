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

| Option | Default | Description |
|---|---|---|
| `CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING` | n | Enable ESP-IDF Wi-Fi/Network Provisioning manager |
| `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE` | y | Use the BLE scheme (currently the only transport supported by this library) |
| `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_SECURITY_0` | n | Security 0 (no encryption — testing only) |
| `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_SECURITY_1` | y | Security 1 (Curve25519 + AES-CTR with PoP) |
| `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_SECURITY_2` | n | Security 2 (SRP6a; requires app-supplied salt+verifier) |
| `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_POP` | "abcd1234" | Proof-of-Possession / SRP password |
| `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_SECURITY2_USERNAME` | "wificfg" | Security 2 SRP username |
| `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_SERVICE_PREFIX` | "PROV_" | BLE GAP name prefix |
| `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_RESET_ON_FAILURE` | y | Erase prov state after N failed attempts |
| `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_MAX_RETRIES` | 3 | Failed-attempt limit before reset |

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
CONFIG_WIFI_CFG_NETWORK_PROVISIONING_SECURITY_1=y
CONFIG_WIFI_CFG_NETWORK_PROVISIONING_POP="<your-secret>"
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

### WiFi + Network Provisioning over BLE (Bluedroid)

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y
CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE=y
CONFIG_WIFI_CFG_NETWORK_PROVISIONING_SECURITY_1=y
CONFIG_WIFI_CFG_NETWORK_PROVISIONING_POP="<your-secret>"
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
