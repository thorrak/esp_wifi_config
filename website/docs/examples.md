---
sidebar_position: 6
title: Examples
description: Complete example projects demonstrating different feature combinations
---

# Examples

All examples are complete ESP-IDF projects you can build and flash directly. Each includes a `main.c`, `CMakeLists.txt`, `sdkconfig.defaults`, and `idf_component.yml`.

```bash
cd examples/<example_name>
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## [basic](https://github.com/thorrak/esp_wifi_config/tree/main/examples/basic)

Minimal setup with default networks, REST API, and SoftAP captive portal. This is the best starting point — it shows event subscriptions, custom variables, and the provisioning-on-failure pattern with no optional features enabled.

---

## [with_cli](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_cli)

Adds serial console commands (`wifi status`, `wifi scan`, `wifi add`, etc.). Requires initializing the ESP Console REPL in your app code — the library registers its commands automatically when `CONFIG_WIFI_CFG_ENABLE_CLI=y`. Uses USB Serial JTAG console on ESP32-S3.

---

## [with_webui](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_webui)

Enables the embedded Preact Web UI (~10KB gzipped) with `CONFIG_WIFI_CFG_ENABLE_WEBUI=y`. No additional code needed — the Web UI is served automatically at the device's IP (or `192.168.4.1` in AP mode). Supports dark mode and captive portal auto-open.

---

## [with_webui_customize](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_webui_customize)

Serves a custom frontend from a LittleFS partition instead of the embedded UI. Requires a custom partition table with a 512KB LittleFS partition and `CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH="/littlefs"`. Falls back to the embedded UI if no custom files are found.

---

## [with_ble](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_ble)

BLE GATT provisioning using Bluedroid. Requires `CONFIG_BT_ENABLED=y`, `CONFIG_BT_BLUEDROID_ENABLED=y`, and `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` (BLE adds ~100KB flash). Testable with the included [Python CLI tool](https://github.com/thorrak/esp_wifi_config/tree/main/tools/wifi_ble_cli).

---

## [with_ble_deinit](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_ble_deinit)

Demonstrates app-owned BLE stack with NimBLE. The app initializes NimBLE first, then WiFi Config registers only its GATT service (service-only mode). After provisioning, `wifi_cfg_deinit()` removes the WiFi Config service while NimBLE stays running for the app's own BLE services.

---

## [with_improv](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_improv)

Improv WiFi standard provisioning alongside custom BLE GATT and captive portal. Supports Web Bluetooth (Chrome/Edge at [improv-wifi.com](https://www.improv-wifi.com/)), the ESPHome companion app, and optional Web Serial. Both the Improv service and custom BLE service (0xFFE0) advertise simultaneously.
