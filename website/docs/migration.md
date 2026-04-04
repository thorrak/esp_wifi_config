---
sidebar_position: 7
title: Migration Guide
description: Migrating from v1.x to v2.x
---

# Migration from v1.x

The provisioning configuration was redesigned in v2.x. The old boolean fields have been replaced with a mode-based system.

## Field Mapping

| Old Field (v1.x) | New Equivalent (v2.x) |
|---|---|
| `enable_captive_portal = true` | `provisioning_mode = WIFI_PROV_ON_FAILURE, enable_ap = true` |
| `stop_ap_on_connect = true` | `stop_provisioning_on_connect = true` |
| `start_ap_on_init = true` | `provisioning_mode = WIFI_PROV_ALWAYS, enable_ap = true` |
| `http.enable = true` | Removed — HTTP starts automatically when provisioning or `http_post_prov_mode` requires it |

## Before (v1.x)

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .enable_captive_portal = true,
    .stop_ap_on_connect = true,
    .http = { .enable = true },
});
```

## After (v2.x)

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .provisioning_teardown_delay_ms = 5000,
    .enable_ap = true,
});
```

## Key Changes

- **`provisioning_mode`** replaces the individual boolean flags, giving explicit control over when provisioning activates. See [Provisioning Modes](./provisioning/modes) for details.
- **`provisioning_teardown_delay_ms`** is new — it adds a delay before tearing down provisioning interfaces so the Web UI can show connection results.
- **`http_post_prov_mode`** controls what happens to the HTTP server after provisioning stops (keep full, API only, or disable).
- **`wifi_cfg_deinit()`** signature changed — it no longer takes parameters.
