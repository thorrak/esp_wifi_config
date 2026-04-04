---
sidebar_position: 5
title: With BLE
description: BLE GATT interface for WiFi configuration via smartphone or Python CLI
---

# BLE Example

[View source on GitHub](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_ble)

Adds the BLE GATT interface alongside HTTP API and captive portal, enabling WiFi configuration from a smartphone app or the included Python CLI tool.

## sdkconfig.defaults

```kconfig
CONFIG_IDF_TARGET="esp32s3"
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_WIFI_CFG_ENABLE_BLE=y
CONFIG_WIFI_CFG_BLE_DEVICE_NAME="ESP32-WiFi-{id}"
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
        .device_name = NULL,  // Uses Kconfig default
    },
});
```

## Testing with the Python CLI

```bash
cd tools/wifi_ble_cli
pip install -r requirements.txt

python wifi_ble_cli.py devices          # Find the device
python wifi_ble_cli.py status           # Get WiFi status
python wifi_ble_cli.py scan             # Scan WiFi networks
python wifi_ble_cli.py add "MyWiFi" "password" --priority 10
python wifi_ble_cli.py connect
```

See [BLE GATT Provisioning](../provisioning/ble-gatt) for full details including NimBLE setup and stack ownership.
