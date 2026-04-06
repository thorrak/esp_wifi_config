# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).


## [Unreleased]

### Breaking Changes

- **Renamed `WIFI_CFG_ENABLE_BLE` to `WIFI_CFG_ENABLE_CUSTOM_BLE`** — The Kconfig option now explicitly refers to the custom BLE GATT service (UUID 0xFFE0). Update `sdkconfig.defaults` files accordingly.
- **Removed `ble.enable` from `wifi_cfg_ble_config_t`** — BLE interfaces are now enabled entirely at compile time via Kconfig. Remove `.ble.enable = true` from your `wifi_cfg_config_t` initializer. The `.ble.device_name` field remains.
- **Improv BLE no longer selects custom BLE** — Enabling `WIFI_CFG_ENABLE_IMPROV_BLE` no longer implicitly enables `WIFI_CFG_ENABLE_CUSTOM_BLE`. Each BLE interface is independently configurable. The BLE stack is initialized automatically when either is enabled.

### Changed

- Decoupled the custom BLE GATT interface from Improv WiFi BLE at the Kconfig level, allowing each to be enabled independently.
