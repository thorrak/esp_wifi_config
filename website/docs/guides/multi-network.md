---
sidebar_position: 1
title: Multi-Network & Auto-Reconnect
description: Configure multiple WiFi networks with priority-based failover and exponential backoff
---

# Multi-Network & Auto-Reconnect

ESP WiFi Config supports saving multiple WiFi networks and automatically connecting to the best available one.

## How It Works

Networks are tried in **priority order** (highest first). For each network, the library retries up to `max_retry_per_network` times with exponential backoff before moving to the next one.

```
boot → load saved networks from NVS (sorted by priority DESC)
  → For each network:
      1. Attempt connection (up to max_retry_per_network times)
      2. Exponential backoff between retries
      3. Success → done
      4. Fail → try next network
  → All networks failed → trigger provisioning (if configured)
```

## Configuration

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    // Default networks used when NVS is empty
    .default_networks = (wifi_network_t[]){
        {"PrimaryWifi", "password1", 10},    // priority 10 — tried first
        {"BackupWifi", "password2", 5},      // priority 5 — fallback
        {"EmergencyWifi", "password3", 1},   // priority 1 — last resort
    },
    .default_network_count = 3,

    // Retry settings
    .max_retry_per_network = 3,          // attempts per network before moving on
    .retry_interval_ms = 5000,           // base interval between retries (5s)
    .retry_max_interval_ms = 60000,      // max backoff cap (60s)
    .auto_reconnect = true,              // reconnect after disconnect

    // What to do after reconnect retries are exhausted
    .max_reconnect_attempts = 10,        // 0 = infinite
    .on_reconnect_exhausted = WIFI_RECONNECT_PROVISION,
});
```

## Default Networks vs. Saved Networks

- **Default networks** (in the config struct) are used as initial seed data when NVS has no saved networks
- **Saved networks** (in NVS) are the runtime set — these are what actually get tried
- Once a network is added via any interface (Web UI, REST API, BLE, CLI), it's saved to NVS
- Default networks are only written to NVS on first boot (when NVS is empty)

## Reconnect Exhaustion

After a successful connection is lost (post-connect disconnect), the library retries up to `max_reconnect_attempts` times. When attempts are exhausted:

| `on_reconnect_exhausted` | Behavior |
|---|---|
| `WIFI_RECONNECT_PROVISION` | Re-enter provisioning mode so the user can reconfigure |
| `WIFI_RECONNECT_RESTART` | Reboot the device via `esp_restart()` |

Set `max_reconnect_attempts = 0` for infinite retries (never exhausted).

## Managing Networks at Runtime

```c
// Add a new network
wifi_cfg_add_network(&(wifi_network_t){"NewWifi", "password", 8});

// Update an existing network (matched by SSID)
wifi_cfg_update_network(&(wifi_network_t){"NewWifi", "newpass", 12});

// Remove a network
wifi_cfg_remove_network("NewWifi");

// List all saved networks
wifi_network_t networks[5];
size_t count;
wifi_cfg_list_networks(networks, 5, &count);
```

Networks can also be managed via the [REST API](../api/rest-api), [BLE GATT](../provisioning/ble-gatt), or [CLI](../api/cli-commands).

## Connection Flow Diagram

```
boot → evaluate provisioning_mode →
  ├── WIFI_PROV_ALWAYS → start provisioning + try connect in parallel
  ├── WIFI_PROV_MANUAL → try connect only (user starts provisioning explicitly)
  ├── WIFI_PROV_WHEN_UNPROVISIONED →
  │     ├── no saved networks → start provisioning
  │     └── has saved networks → try connect
  └── WIFI_PROV_ON_FAILURE →
        ├── no saved networks → start provisioning
        └── has saved networks → try connect →
              ├── success → emit CONNECTED → GOT_IP
              └── all fail → start provisioning

Post-connect disconnect:
  1. Auto-reconnect up to max_reconnect_attempts (0 = infinite)
  2. If exhausted → on_reconnect_exhausted action:
     a. WIFI_RECONNECT_PROVISION → re-enter provisioning
     b. WIFI_RECONNECT_RESTART → esp_restart()
```
