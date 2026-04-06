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
| `WIFI_PROV_ALWAYS` | AP/BLE/Improv start at init and remain active, even after STA connects |
| `WIFI_PROV_ON_FAILURE` | Start provisioning when no networks are saved or all saved networks fail to connect |
| `WIFI_PROV_WHEN_UNPROVISIONED` | Start provisioning only if no networks exist in NVS |
| `WIFI_PROV_MANUAL` | Never auto-start provisioning; the application calls `wifi_cfg_start_ap()` explicitly |

## Choosing a Mode

- **`WIFI_PROV_ON_FAILURE`** (recommended for most devices) — The device tries its saved networks first. If it can't connect to any of them, it opens provisioning so the user can reconfigure. This is the most common pattern for consumer IoT devices.

- **`WIFI_PROV_ALWAYS`** — Provisioning runs alongside normal operation. Use this when you want the configuration interfaces to remain accessible at all times (e.g., a development device or a device that needs to be reconfigurable without physical access).

- **`WIFI_PROV_WHEN_UNPROVISIONED`** — Only provision on first boot. Once the user has configured at least one network, provisioning never starts automatically again. Use this for devices that should be configured once and left alone.

- **`WIFI_PROV_MANUAL`** — Full control. Provisioning only starts when your code calls `wifi_cfg_start_ap()` (e.g., on a button press or GPIO event). Use this when you have a physical provisioning trigger.

## Configuration Example

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .provisioning_teardown_delay_ms = 5000,
    .enable_ap = true,

    // BLE interfaces are enabled at compile time via Kconfig:
    //   CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=y  (custom GATT)
    //   CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y  (Improv standard)
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
| `WIFI_RECONNECT_PROVISION` | Re-enter provisioning mode so the user can reconfigure |
| `WIFI_RECONNECT_RESTART` | Reboot the device via `esp_restart()` |
