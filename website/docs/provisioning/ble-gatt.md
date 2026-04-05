---
sidebar_position: 2
title: BLE GATT
description: Configure WiFi via Bluetooth Low Energy using the custom GATT service
---

# BLE GATT Provisioning

ESP WiFi Config provides a BLE GATT service for configuring WiFi from a smartphone app or the included Python CLI tool. Both Bluedroid and NimBLE host stacks are supported.

## Enabling BLE

### 1. Kconfig

```kconfig
CONFIG_WIFI_CFG_ENABLE_BLE=y
```

### 2. Bluetooth Stack (choose one)

**Bluedroid** (~100KB flash / ~40KB RAM):

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
```

**NimBLE** (~50KB flash / ~20KB RAM):

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144
```

### 3. Runtime Config

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .enable_ap = true,
    .ble = {
        .enable = true,
        .device_name = "ESP32-WiFi-{id}",  // NULL uses Kconfig default
    },
});
```

## Service & Characteristics

The device advertises the WiFi Service UUID (`0xFFE0`):

| UUID | Name | Properties | Description |
|---|---|---|---|
| 0xFFE0 | WiFi Service | — | Main service |
| 0xFFE1 | Status | Read, Notify | Current WiFi status (JSON) |
| 0xFFE2 | Command | Write | Send JSON command |
| 0xFFE3 | Response | Notify | Command response (JSON) |

See [BLE Protocol Reference](../api/ble-protocol.md) for the full command/response format.

## Stack Ownership & Deinitialization

The BLE interface detects automatically whether the application owns the BLE stack:

| Mode | Init behavior | Deinit behavior | Use case |
|---|---|---|---|
| **Owns the stack** (default) | Initializes BLE host stack + registers GATT service | Tears down everything | App doesn't use BLE for anything else |
| **Service only** | Detects host stack already running, registers GATT service only | Unregisters service, leaves host stack running | App manages the BLE lifecycle |

### Example: App-Owned NimBLE Stack

```c
// App initializes NimBLE before WiFi Config
nimble_port_init();
nimble_port_freertos_init(nimble_host_task);

wifi_cfg_init(&(wifi_cfg_config_t){
    .ble = { .enable = true },
});

// Later: WiFi Config removes its GATT service but NimBLE keeps running
wifi_cfg_deinit();

// App can continue using BLE for its own services
```

### Example: App-Owned Bluedroid Stack

```c
// App initializes Bluedroid before WiFi Config
esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
esp_bt_controller_init(&bt_cfg);
esp_bt_controller_enable(ESP_BT_MODE_BLE);
esp_bluedroid_init();
esp_bluedroid_enable();

wifi_cfg_init(&(wifi_cfg_config_t){
    .ble = { .enable = true },
});

// Later: WiFi Config unregisters its GATT app but Bluedroid keeps running
wifi_cfg_deinit();
```

## Python CLI Tool

A command-line tool for managing WiFi via BLE from a computer:

```bash
cd tools/wifi_ble_cli
pip install -r requirements.txt

# Scan for BLE devices
python wifi_ble_cli.py devices

# Get WiFi status
python wifi_ble_cli.py status

# Scan WiFi networks
python wifi_ble_cli.py scan

# Add network with priority
python wifi_ble_cli.py add "MyWiFi" "password123" --priority 10

# Connect
python wifi_ble_cli.py connect

# AP management
python wifi_ble_cli.py ap-status
python wifi_ble_cli.py start-ap
python wifi_ble_cli.py stop-ap

# Custom variables
python wifi_ble_cli.py get-var device_name
python wifi_ble_cli.py set-var device_name "My ESP32"

# Factory reset
python wifi_ble_cli.py factory-reset
```

Use `--name` to filter by BLE device name prefix (default: "ESP32-WiFi") or `--device` to connect by MAC address.
