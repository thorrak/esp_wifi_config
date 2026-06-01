---
sidebar_position: 3
title: Improv WiFi
description: Open-standard provisioning via Web Bluetooth and Web Serial
---

# Improv WiFi

[Improv WiFi](https://www.improv-wifi.com/) is an open standard by ESPHome for provisioning IoT devices over BLE or Serial using web browsers and companion apps.

:::note Mutually exclusive with Network Provisioning BLE
`CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE` and
`CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING` cannot both be enabled in
a single firmware build — they each want to own the BLE GAP advertising
and the NimBLE/Bluedroid host. Pick the protocol that matches your
provisioning client tooling, or ship two firmware variants.

Improv **Serial** (`CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL`) is independent
of BLE and remains safe to enable alongside Network Provisioning BLE.
:::

## Enabling Improv

### Kconfig

```kconfig
# BLE transport (requires Bluetooth enabled)
CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y

# Serial transport (optional)
CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL=y
CONFIG_WIFI_CFG_IMPROV_SERIAL_UART_NUM=0
CONFIG_WIFI_CFG_IMPROV_SERIAL_BAUD=115200
```

### Runtime Config

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .enable_ap = true,
    // Transports selected via Kconfig (CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE / _SERIAL)
    .improv = {
        .firmware_name = "my_project",
        .firmware_version = "1.0.0",
        .device_name = "My Device",
        .on_identify = my_identify_callback,  // Optional: flash LED/make noise on Identify
    },
});
```

## How to Provision

### Via Web Bluetooth (Chrome/Edge)

1. Open [improv-wifi.com](https://www.improv-wifi.com/) in Chrome or Edge
2. Click "Connect device via Bluetooth"
3. Select the device from the browser pairing dialog
4. Enter WiFi credentials — the device connects and returns its IP

### Via ESPHome Companion App

1. Install the ESPHome app (Android/iOS)
2. The device appears automatically for Improv provisioning
3. Tap and enter WiFi credentials

### Via Web Serial (if enabled)

1. Open [improv-wifi.com](https://www.improv-wifi.com/) in Chrome or Edge
2. Click "Connect device via Serial"
3. Select the serial port and enter WiFi credentials

## Supported RPC Commands

| Command | ID | Description |
|---|---|---|
| Send WiFi Settings | 0x01 | Provide SSID + password, device connects |
| Identify | 0x02 | Flash LED / beep (calls `on_identify` callback) |
| Get Device Info | 0x03 | Returns firmware name, version, chip, device name |
| Get WiFi Networks | 0x04 | Triggers a WiFi scan and returns results |

## BLE Stack Requirements

Improv BLE requires `CONFIG_BT_ENABLED=y` and a NimBLE or Bluedroid host
stack. The BLE stack is initialised automatically when Improv BLE is
enabled — the library does not need any other Kconfig opt-in. See the
[with_improv example](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_improv)
for a complete sdkconfig.

If you need the official ESP-IDF provisioning protocol instead of
Improv (e.g. for use with the Espressif "ESP BLE Provisioning" mobile
apps), see [BLE Provisioning](./ble-gatt.md). The two are mutually
exclusive at compile time.
