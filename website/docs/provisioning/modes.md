---
sidebar_position: 0
title: Provisioning Modes
description: Control when and how provisioning interfaces activate — ALWAYS, ON_FAILURE, WHEN_UNPROVISIONED, MANUAL
---

# Provisioning Modes

The `provisioning_mode` field controls when ESP WiFi Config automatically starts provisioning interfaces (SoftAP, BLE, and/or Improv WiFi).

## Modes

| Mode | Behavior |
|---|---|
| `WIFI_PROV_ALWAYS` | **Disabled** — kept in the API for compatibility but currently treated as `WIFI_PROV_MANUAL` at boot. See note below. |
| `WIFI_PROV_ON_FAILURE` | Start provisioning when no networks are saved or all saved networks fail to connect |
| `WIFI_PROV_WHEN_UNPROVISIONED` | Start provisioning only if no networks exist in NVS |
| `WIFI_PROV_MANUAL` | Never auto-start provisioning; the application calls `wifi_cfg_start_ap()` explicitly |

:::caution `WIFI_PROV_ALWAYS` is disabled
`wifi_cfg_start_provisioning()` eventually calls `wifi_prov_mgr_start_provisioning()`, which calls `nimble_port_init()`. If the application has already initialised the BLE stack, that call fails — so the always-on path is bypassed (with a warning log) and treated as `WIFI_PROV_MANUAL`. The enum value is preserved so existing configs still compile. May be re-enabled when the library has its own BLE provisioning path independent of Espressif's `wifi_provisioning` component.
:::

## Choosing a Mode

- **`WIFI_PROV_ON_FAILURE`** (recommended for most devices) — The device tries its saved networks first. If it can't connect to any of them, it opens provisioning so the user can reconfigure. This is the most common pattern for consumer IoT devices.

- **`WIFI_PROV_WHEN_UNPROVISIONED`** — Only provision on first boot. Once the user has configured at least one network, provisioning never starts automatically again. Use this for devices that should be configured once and left alone.

- **`WIFI_PROV_MANUAL`** — Full control. Provisioning only starts when your code calls `wifi_cfg_start_ap()` (e.g., on a button press or GPIO event). Use this when you have a physical provisioning trigger.

## Configuration Example

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .provisioning_teardown_delay_ms = 5000,
    .enable_ap = true,

    // BLE interfaces are enabled at compile time via Kconfig
    // (mutually exclusive — pick one):
    //   CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y  (ESP-IDF Wi-Fi Provisioning)
    //   CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y            (Improv standard)
});
```

## Post-Connect Behavior

### Provisioning Teardown

When `stop_provisioning_on_connect` is `true`, the library stops AP/BLE/Improv after the STA obtains an IP address. The `provisioning_teardown_delay_ms` value (default: 0) adds a delay before teardown so the Web UI can display connection results to the user.

```
STA gets IP → wait provisioning_teardown_delay_ms
  → emit PROVISIONING_STOPPED
  → stop AP, BLE, Improv
  → transition HTTP per http_post_prov_mode
```

### HTTP Post-Provisioning Mode

Controls the HTTP server after provisioning stops:

| Mode | Behavior |
|---|---|
| `WIFI_HTTP_FULL` | Keep the full HTTP server running (Web UI + API) |
| `WIFI_HTTP_API_ONLY` | Keep only REST API endpoints, remove Web UI and captive portal routes |
| `WIFI_HTTP_DISABLED` | Stop the HTTP server entirely |

### Reconnect Exhaustion

After a post-connect disconnect, the library retries up to `max_reconnect_attempts` times (0 = infinite). When attempts are exhausted:

| `on_reconnect_exhausted` | Behavior |
|---|---|
| `WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` | **Disabled** — kept in the API for compatibility but currently treated as "continue retrying indefinitely" (equivalent to `max_reconnect_attempts = 0`). See note below. |
| `WIFI_ON_RECONNECT_EXHAUSTED_RESTART` | Reboot the device via `esp_restart()` |

:::caution `WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` is disabled
The re-enter-provisioning path called `wifi_prov_mgr_start_provisioning()` → `nimble_port_init()`, which fails when the application owns the BLE stack. The library now logs a warning and falls through to normal exponential-backoff retry instead. Use `WIFI_ON_RECONNECT_EXHAUSTED_RESTART` or leave `max_reconnect_attempts = 0` for indefinite retry. See [MIGRATION.md](https://github.com/thorrak/esp_wifi_config/blob/main/MIGRATION.md).
:::

### Reboot After BLE Provisioning

When the BLE provisioning channel is enabled, the device reboots automatically once a session completes — both because `wifi_provisioning` does not expose a clean BLE-stack tear-down/rebuild path, and because a cold boot is the most reliable way to hand off from "BLE-owned" to "STA-owned" state. The reboot is governed by two fields on `wifi_cfg_prov_config_t`:

| Field | Default | Effect |
|---|---|---|
| `disable_reboot_on_provisioning_success` | `false` (reboot **on**) | Set true if the application handles the BLE/Wi-Fi handoff itself. |
| `reboot_max_wait_ms` | `0` → 3000 ms | Backstop window after `WIFI_PROV_EVT_CRED_SUCCESS` before the forced reboot. The actual reboot fires on whichever happens first: the BLE client disconnecting after `WIFI_PROV_EVT_CRED_RECV`, or this timer expiring. |

While reboot-on-success is active, `stop_provisioning_on_connect` / `provisioning_teardown_delay_ms` / `prov_ble.stop_after_success` are bypassed for the BLE flow — the reboot supersedes any in-place teardown. SoftAP and Improv flows still observe the in-place teardown lifecycle.

See [BLE Provisioning → Reboot on success](./ble-gatt.md#reboot-on-successful-provisioning) for the full discussion.
