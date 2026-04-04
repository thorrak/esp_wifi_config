---
sidebar_position: 6
title: With Improv WiFi
description: Improv WiFi standard provisioning via Web Bluetooth and Web Serial
---

# Improv WiFi Example

[View source on GitHub](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_improv)

Demonstrates the Improv WiFi open standard alongside custom BLE GATT and HTTP captive portal. Supports provisioning from Chrome/Edge via Web Bluetooth, the ESPHome companion app, or Web Serial.

## sdkconfig.defaults

```kconfig
CONFIG_IDF_TARGET="esp32s3"
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_BLE=y
CONFIG_WIFI_CFG_BLE_DEVICE_NAME="ESP32-WiFi-{id}"
CONFIG_WIFI_CFG_ENABLE_IMPROV=y
CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y
```

## Key Code

```c
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
        .enable_serial = false,
        .firmware_name = "my_project",
        .firmware_version = "1.0.0",
        .device_name = "My Device",
        .on_identify = my_identify_callback,  // Flash LED on Identify
    },
});
```

## Provisioning Methods

This example supports all provisioning methods simultaneously:

1. **Improv Web Bluetooth** — Open [improv-wifi.com](https://www.improv-wifi.com/) in Chrome/Edge
2. **ESPHome App** — Device appears automatically in the ESPHome companion app
3. **Custom BLE GATT** — Use the Python CLI tool or any BLE client
4. **Captive Portal** — Connect to the SoftAP and configure via web browser

See [Improv WiFi](../provisioning/improv-wifi) for full details on coexistence and configuration.
