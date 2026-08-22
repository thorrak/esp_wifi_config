---
sidebar_position: 1
title: C API Reference
description: Complete C API reference — initialization, status, network management, SoftAP, variables
---

# C API Reference

All functions are declared in `esp_wifi_config.h` and return `esp_err_t` unless otherwise noted.

## Initialization

```c
// Initialize WiFi Config with the given configuration
esp_err_t wifi_cfg_init(const wifi_cfg_config_t *config);

// Deinitialize WiFi Config, stop all interfaces, free resources.
// deinit_wifi = false keeps WiFi connected and the netifs alive.
esp_err_t wifi_cfg_deinit(bool deinit_wifi);
```

## Status

```c
// Check if STA is connected
bool wifi_cfg_is_connected(void);

// Get the current WiFi state enum
wifi_state_t wifi_cfg_get_state(void);

// Get detailed status (SSID, IP, RSSI, etc.)
esp_err_t wifi_cfg_get_status(wifi_status_t *status);

// Block until connected or timeout (0 = wait forever)
esp_err_t wifi_cfg_wait_connected(uint32_t timeout_ms);
```

## Events

The push half of the section above: `wifi_cfg_get_status()` answers when you
ask, events tell you when something changes. The library publishes them on
ESP-IDF's **default event loop** under its own base, alongside `WIFI_EVENT`
and `IP_EVENT`.

```c
// The event base. Declared in esp_wifi_config.h; register against it as you
// would for any IDF event source.
ESP_EVENT_DECLARE_BASE(WIFI_CFG_EVENT);

// Every event id the library posts, as wifi_cfg_event_t.
// WIFI_CFG_EVENT_MAX is the count, not an event.
esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP,
                           on_got_ip, NULL);

// ESP_EVENT_ANY_ID for a catch-all.
esp_event_handler_register(WIFI_CFG_EVENT, ESP_EVENT_ANY_ID, on_any, NULL);

// Human-readable name for an event id, for logging.
// Static string, "unknown" if out of range. Never NULL.
const char *wifi_cfg_event_name(wifi_cfg_event_t event);
```

`wifi_cfg_init()` creates the default loop itself. Call
`esp_event_loop_create_default()` and register first only if you need to catch
events emitted *during* init; creating it twice is harmless.

Handlers run on the system event loop task, shared with IDF's own networking
callbacks — keep them short and do heavy work on your own task.

**[Events guide](../guides/events.md)** has the full id-to-payload table, the
handler signature, and worked examples. It is not repeated here: one table that
drifts is worse than one table that is looked up.

## Connection

```c
// Connect to a specific SSID, or pass NULL for auto-connect (highest priority)
esp_err_t wifi_cfg_connect(const char *ssid);

// Disconnect from the current network
esp_err_t wifi_cfg_disconnect(void);

// Scan for available WiFi networks
esp_err_t wifi_cfg_scan(wifi_scan_result_t *results, size_t max, size_t *count);
```

## Network Management

```c
// Add a new network to NVS
esp_err_t wifi_cfg_add_network(const wifi_network_t *network);

// Update an existing network (matched by SSID)
esp_err_t wifi_cfg_update_network(const wifi_network_t *network);

// Remove a network from NVS
esp_err_t wifi_cfg_remove_network(const char *ssid);

// Look one up by SSID. ESP_ERR_NOT_FOUND if it is not stored.
esp_err_t wifi_cfg_get_network(const char *ssid, wifi_network_t *network);

// List all saved networks
esp_err_t wifi_cfg_list_networks(wifi_network_t *networks, size_t max, size_t *count);
```

## SoftAP

```c
// Start SoftAP with the given config, or NULL for defaults
esp_err_t wifi_cfg_start_ap(const wifi_cfg_ap_config_t *config);

// Stop SoftAP
esp_err_t wifi_cfg_stop_ap(void);

// Get AP status (active, SSID, IP, connected clients)
esp_err_t wifi_cfg_get_ap_status(wifi_ap_status_t *status);

// Read the stored SoftAP config
esp_err_t wifi_cfg_get_ap_config(wifi_cfg_ap_config_t *config);

// Update it and persist to NVS. Applied immediately if the AP is running.
esp_err_t wifi_cfg_set_ap_config(const wifi_cfg_ap_config_t *config);
```

`wifi_cfg_start_ap()` takes a config for one run; `wifi_cfg_set_ap_config()`
stores one. Use the setter when the change should outlive a reboot.

## Custom Variables

```c
// Set a key-value variable (creates or updates in NVS)
esp_err_t wifi_cfg_set_var(const char *key, const char *value);

// Get a variable value
esp_err_t wifi_cfg_get_var(const char *key, char *value, size_t max_len);

// Delete a variable from NVS
esp_err_t wifi_cfg_del_var(const char *key);
```

## Factory Reset

```c
// Erase all saved networks, variables, and AP config from NVS
esp_err_t wifi_cfg_factory_reset(void);
```

## HTTP Server

```c
// Get the HTTP server handle (for registering custom endpoints)
httpd_handle_t wifi_cfg_get_httpd(void);

// Stop the HTTP server (only if library-owned and provisioning not active)
esp_err_t wifi_cfg_stop_http(void);
```

## Configuration Struct

The `wifi_cfg_config_t` struct controls all behavior. **Always start from
`WIFI_CFG_DEFAULTS`** — see [Starting from the defaults](#defaults) below for
why, and for the full list of what the macro sets.

```c
wifi_cfg_config_t config = {
    WIFI_CFG_DEFAULTS,

    // Default networks (seed data for first boot when NVS is empty)
    .default_networks = networks,
    .default_network_count = 2,

    // Default variables (seed data for first boot)
    .default_vars = (wifi_var_t[]){
        {"server_url", "https://api.example.com"},
        {"device_name", "my-device"},
    },
    .default_var_count = 2,

    // SoftAP config ({id} = last 3 bytes of MAC). A designated initialiser
    // replaces the whole sub-struct; wifi_cfg_init() backfills the rest.
    .default_ap = {
        .ssid = "MyDevice-{id}",
    },

    // Provisioning behavior. provisioning_mode is already
    // WIFI_PROV_ON_FAILURE from the macro.
    .stop_provisioning_on_connect = true,
    .provisioning_teardown_delay_ms = 5000,
    .enable_ap = true,

    // Reconnect exhaustion. on_reconnect_exhausted is already
    // WIFI_ON_RECONNECT_EXHAUSTED_RESTART from the macro —
    // _PROVISION is currently disabled (treated as infinite retry).
    .max_reconnect_attempts = 10,          // 0 = infinite (the default)

    // HTTP post-provisioning mode (the macro sets WIFI_HTTP_FULL)
    .http_post_prov_mode = WIFI_HTTP_API_ONLY,

    // HTTP interface. api_base_path ("/api/wifi") and auth_username
    // ("admin") already come from the macro.
    .http = {
        .enable_auth = true,
        .auth_password = "secret",
    },

    // ESP-IDF Network Provisioning over BLE
    // (requires CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y; mutually
    // exclusive with Improv BLE). Omitted here and left at their defaults:
    // device_name ("PROV_{id}"), security (Security 1),
    // memory_policy (FREE_BTDM), max_failed_attempts (3),
    // cleanup_delay_ms (1000), reboot_max_wait_ms (15000).
    .prov_ble = {
        .pop                = "1234abcd",   // Security 1 PoP
        .wifi_conn_attempts = 5,            // 0 = infinite (legacy default)
        .reset_on_failure   = true,         // accept retries without reboot
        .firmware_version   = "1.0.0",
    },

    // Improv WiFi (transports gated by Kconfig: CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE / _SERIAL).
    // `ble_device_name` is the BLE GAP advertised name (what scanners show);
    // `device_name` is the human-readable name reported via the Improv
    // Device-Info RPC (what the Improv companion app shows after connect).
    .improv = {
        .ble_device_name = "ESP32-WiFi-{id}",   // also the Kconfig default
        .firmware_name = "my_project",
        .firmware_version = "1.0.0",
        .device_name = "My Device",
        .on_identify = my_identify_callback,
    },
};

wifi_cfg_init(&config);
```

## Starting from the defaults {#defaults}

`wifi_cfg_init()` does **not** patch fields you leave at zero. Two macros in
`esp_wifi_config.h` supply the documented defaults as a value:

```c
// Compound-literal style — the house style in the examples
wifi_cfg_init(&(wifi_cfg_config_t){
    WIFI_CFG_DEFAULTS,
    .enable_ap = true,
});

// Struct-value style, when you want to compute fields
wifi_cfg_config_t cfg = WIFI_CFG_DEFAULT_CONFIG();
cfg.enable_ap = true;
cfg.auto_reconnect = false;   // means false, because you started here
wifi_cfg_init(&cfg);

// Or take them unmodified
wifi_cfg_init(NULL);
```

What the macros set:

| Field | Default |
|-------|---------|
| `max_retry_per_network` | `CONFIG_WIFI_CFG_DEFAULT_RETRY` (3) |
| `retry_interval_ms` | `CONFIG_WIFI_CFG_RETRY_INTERVAL_MS` (5000) |
| `retry_max_interval_ms` | 60000 |
| `auto_reconnect` | `true` |
| `max_reconnect_attempts` | 0 (retry forever) |
| `on_reconnect_exhausted` | `WIFI_ON_RECONNECT_EXHAUSTED_RESTART` |
| `provisioning_mode` | `WIFI_PROV_ON_FAILURE` |
| `http_post_prov_mode` | `WIFI_HTTP_FULL` |
| `default_ap.ssid` | `WIFI_CFG_DEFAULT_AP_SSID` (`"ESP32-Config"`) |
| `default_ap.password` | `WIFI_CFG_DEFAULT_AP_PASSWORD` (`""` — open) |
| `default_ap.max_connections` | 4 |
| `default_ap.ip` / `.gateway` | `WIFI_CFG_DEFAULT_AP_IP` (`"192.168.4.1"`) |
| `default_ap.netmask` | `"255.255.255.0"` |
| `default_ap.dhcp_start` / `.dhcp_end` | `"192.168.4.2"` / `"192.168.4.20"` |
| `http.api_base_path` | `"/api/wifi"` |
| `http.auth_username` / `.auth_password` | `"admin"` / `"admin"` |
| `improv.serial_uart_num` | `CONFIG_WIFI_MGR_IMPROV_SERIAL_UART_NUM` (0) |
| `improv.serial_baud_rate` | `CONFIG_WIFI_MGR_IMPROV_SERIAL_BAUD` (115200) |
| `prov_ble.cleanup_delay_ms` | 1000 |
| `prov_ble.reboot_max_wait_ms` | 15000 |
| `prov_ble.max_failed_attempts` | 3 |

Everything not listed defaults to zero / `false` / `NULL`, which is the
intended value for that field.

:::warning
`wifi_cfg_init()` returns `ESP_ERR_INVALID_ARG` if `retry_interval_ms` or
`retry_max_interval_ms` is zero — `retry_interval_ms << retry` is the
backoff, so a zero base retries with no delay at all. Building a config
without `WIFI_CFG_DEFAULTS` is the usual way to hit this.
:::

:::note
A designated initialiser for a nested struct replaces the **whole**
sub-struct, so `{ WIFI_CFG_DEFAULTS, .default_ap = {.ssid = "x"} }` blanks
the other `default_ap` fields. `wifi_cfg_init()` backfills the per-field AP
defaults for exactly this reason; set members individually
(`cfg.default_ap.ssid`) if you want to be certain. The same applies to
`.http`, `.improv` and `.prov_ble`.
:::

## Network Provisioning configuration (`wifi_cfg_prov_config_t`) {#prov-network-provisioning}

The `.prov_ble` sub-struct carries every runtime parameter for the ESP-IDF
`wifi_provisioning` manager (BLE scheme). Only `CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING`
and `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE` are set via Kconfig;
everything below is plain runtime configuration. Zero/NULL fields fall
back to the library defaults documented in the table.

| Field | Purpose |
|-------|---------|
| `device_name` | BLE GAP advertised name template. Supports `{id}` (expanded to the last 3 bytes of the STA MAC). NULL → `"PROV_{id}"`. |
| `service_uuid128` | Optional 16-byte 128-bit GATT service UUID. NULL → IDF default. Espressif recommends a product-specific UUID. |
| `manufacturer_data` / `_len` | Optional bytes appended to the BLE scan response. Total must fit in 31 bytes alongside the device name. |
| `random_addr` | Optional 6-byte static random BLE address. |
| `security` | `WIFI_CFG_PROV_SECURITY_{0,1,2,DEFAULT}`. DEFAULT → Security 1. |
| `pop` | Security 1 proof-of-possession. NULL or empty → no PoP. |
| `security2_username` | Security 2 SRP6a username. Metadata only — the username never flows into `wifi_prov_mgr` from the device side, so no default is substituted. |
| `security2_salt` / `_verifier` (+ lens) | Pre-computed SRP6a parameters. Required when Security 2 is selected — `wifi_cfg_init()` returns `ESP_ERR_INVALID_ARG` if missing. |
| `memory_policy` | Bluetooth memory cleanup policy on provisioning deinit. See below. |
| `keep_ble_on_after_stop` | If true, BLE stays advertising after the manager stops. Useful when the app takes over BLE post-provisioning. |
| `cleanup_delay_ms` | Grace period the manager observes between stop and protocomm teardown. 0 → 1000 ms. Min 100 ms. |
| `wifi_conn_attempts` | STA connection attempts before CRED_FAIL. 0 → infinite (legacy default). A bounded value gives the manager a chance to report failure cleanly. |
| `stop_after_success` | Stop the manager on CRED_SUCCESS even when `stop_provisioning_on_connect` is false (useful in MANUAL mode). Ignored while reboot-on-success is active — the reboot supersedes any in-place stop. |
| `disable_reboot_on_provisioning_success` | **Default false** (reboot enabled). Set true only when the app handles the BLE/Wi-Fi handoff itself. See [Reboot on successful provisioning](../provisioning/ble-gatt.md#reboot-on-successful-provisioning). |
| `reboot_max_wait_ms` | Backstop window after CRED_SUCCESS before the forced reboot, in ms. 0 → 15000 ms. Must comfortably exceed the client's status-poll interval (~5 s for Espressif's SDK) plus associate + DHCP, or the client can report a false failure. Ignored when `disable_reboot_on_provisioning_success` is true. |
| `reset_on_failure` | If true, reset the state machine after `max_failed_attempts` consecutive credential failures so a fresh attempt can be accepted without rebooting. |
| `max_failed_attempts` | Threshold used when `reset_on_failure` is true. 0 → library default (3). |
| `firmware_version` | Surfaced via the built-in `esp-wifi-config-version` endpoint. |
| `app_infos` / `_count` | Optional metadata published via the standard `proto-ver` endpoint (label "prov" is reserved). |
| `custom_endpoints` / `_count` | Additional protocomm endpoints registered alongside the library's four built-in endpoints. |
| `on_credentials_received` | Callback invoked when the client sends WiFi credentials. Receives `wifi_cfg_prov_creds_t *`. |
| `on_credentials_failed` | Callback invoked on STA connect failure. Receives the `wifi_prov_sta_fail_reason_t` value as `int`. |
| `on_credentials_success` | Callback invoked when STA connects with the supplied credentials. |
| `event_ctx` | User pointer passed to every callback above. |

All three credential callbacks also fire as library events
(`WIFI_CFG_EVENT_PROV_CRED_RECV`, `_FAIL`, `_SUCCESS`) — pick whichever
path fits the app.

### Bluetooth memory policy

The wifi_provisioning manager hooks the Bluetooth controller's `disable`
event and can release controller/host memory at deinit time. Pick the
policy that matches what the rest of the application needs from
Bluetooth **after** provisioning ends:

| Policy | Frees | Use when |
|--------|-------|----------|
| `WIFI_CFG_PROV_MEM_FREE_BTDM` (default) | Classic BT + BLE | The device does not use Bluetooth post-provisioning. Reclaims the most RAM. |
| `WIFI_CFG_PROV_MEM_FREE_BLE` | BLE only | The app still needs **Classic BT** (A2DP, SPP, HFP, etc.). Only valid on chips that support Classic BT (ESP32). |
| `WIFI_CFG_PROV_MEM_FREE_BT` | Classic BT only | The app still needs **BLE** (custom GATT service, beacon, scanner). |
| `WIFI_CFG_PROV_MEM_KEEP_ALL` | Nothing | The app brought up the BLE/BT stack itself before `wifi_cfg_init()` and owns the lifecycle. |

The library auto-detects the "app already owns the stack" case (BT
controller already enabled at start time) and forces `KEEP_ALL` with a
log warning. Setting the wrong policy crashes the app — picking
`FREE_BTDM` and then calling a Classic BT function afterwards will fault
inside the controller.

If your firmware uses **Classic Bluetooth (A2DP, SPP, HFP, etc.)** after
WiFi is provisioned, set `memory_policy = WIFI_CFG_PROV_MEM_FREE_BLE`
explicitly — the default reclaims Classic BT memory and will break those
profiles. Likewise, the BLE-keep-alive pattern (custom GATT service or
scanning after provisioning) needs `WIFI_CFG_PROV_MEM_FREE_BT` or
`KEEP_ALL` to avoid freeing BLE host state.
