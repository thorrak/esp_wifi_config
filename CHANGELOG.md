# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).


## [0.0.3] — 2026-04-25

### Breaking Changes

- **Renamed `WIFI_CFG_ENABLE_BLE` to `WIFI_CFG_ENABLE_CUSTOM_BLE`** — The Kconfig option now explicitly refers to the custom BLE GATT service (UUID 0xFFE0). Update `sdkconfig.defaults` files accordingly.
- **Removed `ble.enable` from `wifi_cfg_ble_config_t`** — BLE interfaces are now enabled entirely at compile time via Kconfig. Remove `.ble.enable = true` from your `wifi_cfg_config_t` initializer. The `.ble.device_name` field remains.
- **Improv BLE no longer selects custom BLE** — Enabling `WIFI_CFG_ENABLE_IMPROV_BLE` no longer implicitly enables `WIFI_CFG_ENABLE_CUSTOM_BLE`. Each BLE interface is independently configurable. The BLE stack is initialized automatically when either is enabled.

### Changed

- Decoupled the custom BLE GATT interface from Improv WiFi BLE at the Kconfig level, allowing each to be enabled independently.


## [0.0.2] — First release as esp_wifi_config

This is the first release since hard-forking from [tuanpmt/esp_wifi_manager](https://github.com/tuanpmt/esp_wifi_manager). It includes a full rename of the library, numerous bug fixes, significant BLE improvements, and new provisioning capabilities. See the [Migration Guide](MIGRATION.md) for upgrading from esp_wifi_manager.

### Breaking Changes

- **Renamed from `esp_wifi_manager` to `esp_wifi_config`.** All public symbols have been renamed:
  - Functions: `wifi_manager_` → `wifi_cfg_`
  - Types: `wifi_mgr_` → `wifi_cfg_` (config struct is now `wifi_cfg_config_t`)
  - Events: `WIFI_MGR_EVT_` → `WIFI_CFG_EVT_`
  - Kconfig: `WIFI_MGR_` → `WIFI_CFG_`
  - Header: `esp_wifi_manager.h` → `esp_wifi_config.h`
- **`wifi_cfg_deinit()` now takes a `bool deinit_wifi` parameter.** Pass `true` for the old behavior (full teardown), or `false` to tear down the manager while keeping WiFi connected — useful after provisioning completes.
- **Built-in mDNS integration removed.** If you need mDNS, initialize it directly in your application after receiving `WIFI_CFG_EVT_GOT_IP`. See the Migration Guide for details.
- **Provisioning booleans replaced with a unified provisioning mode system.** The separate boolean flags for enabling provisioning methods have been consolidated.
- **Several Kconfig options removed** in favor of runtime config struct fields: `AP_SSID`, `AP_PASSWORD`, `AP_IP`, and `BLE_DEVICE_NAME` are now set via `wifi_cfg_config_t` rather than menuconfig.
- **`esp_bus` dependency** has moved from `tuanpmt/esp_bus` to `thorrak/esp_bus`.

### New Features

- **Improv WiFi support.** Full implementation of the [Improv WiFi](https://www.improv-wifi.com/) standard, with both BLE and Serial transports. Includes a new `with_improv` example.
- **NimBLE Bluetooth stack support.** Choose between Bluedroid and NimBLE for BLE provisioning, enabling use on memory-constrained devices.
- **BLE interface at feature parity with SoftAP.** Added `update_network`, `list_vars`, and `del_var` BLE commands, plus missing status fields.
- **BLE response chunking.** Large BLE responses are now automatically chunked, with an increased SSID buffer size for reliability.
- **BLE command queue.** Commands are now processed via a dedicated task and queue rather than inline in the BLE callback, improving stability.
- **BLE service advertisement.** Service UUID 0xFFE0 is now advertised in BLE broadcasts for easier device discovery.
- **Network upsert.** "Add network" commands now update existing entries instead of failing if the network already exists.
- **Continued BLE use after deinitialization.** BLE can remain active after calling `wifi_cfg_deinit(false)`, with a new `with_ble_deinit` example demonstrating the pattern.
- **Pre-request hook and variable validation** for HTTP API endpoints, giving applications more control over provisioning requests.
- **Option to ignore NVS AP config** and always use the default AP settings provided at init time.
- **HTTP handler registration improvements** for provisioning and API management, including proper URI deregistration when using a shared HTTP server.
- **Mock HTTP test server** (`tools/test_server/`) — a Flask-based server that emulates the esp_wifi_config API for frontend development without hardware.

### Bug Fixes

- **Defer `WIFI_STATE_CONNECTED` until `GOT_IP`** — previously the connected state was emitted before an IP address was assigned, causing race conditions in application code.
- **Fix double AP start/stop events** — AP lifecycle events are now emitted exactly once.
- **Fix reconnect logic** — stale disconnect events from previous connections no longer trigger spurious reconnect attempts. Connected SSID is now properly tracked.
- **Hide hidden networks** from WiFi scan results.
- **Fix AP config loading** to properly prioritize provided defaults over stale NVS data.
- **Fix Web UI initialization order** — fallback page registration now happens at the correct time.
- **Fix custom WebUI builds** — embedded files are no longer included when a custom WebUI path is configured.
- **Fix `set_var` callback** — the "variable changed" callback now fires correctly when setting variables programmatically.
- **Fix gzipped asset serving** — file serving logic now correctly prefers gzipped assets.

### Infrastructure & Documentation

- **Documentation site** at [configwifi.com](https://configwifi.com) built with Docusaurus, including AI-friendly `llms.txt`.
- **GitHub Actions CI** — automated builds for all examples on every push.
- **ESP Component Registry** publishing via GitHub Actions on release.
- **PlatformIO Library Registry** support with `library.json`.
- **Migration Guide** (`MIGRATION.md`) documenting all breaking changes with find-and-replace instructions.
