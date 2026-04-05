# Migration Guide: esp_wifi_manager to esp_wifi_config

This document tracks all breaking changes introduced when the project was renamed from `esp_wifi_manager` (by tuanpmt) to `esp_wifi_config` (by thorrak). Use it as a reference when updating existing code that depended on the original library.


## Component Name

| Old | New |
|-----|-----|
| `esp_wifi_manager` | `esp_wifi_config` |

Update your `idf_component.yml` dependency:

```yaml
# Old
dependencies:
  tuanpmt/esp_wifi_manager: "*"

# New
dependencies:
  thorrak/esp_wifi_config: "*"
```

Update your `CMakeLists.txt` REQUIRES:

```cmake
# Old
REQUIRES esp_wifi_manager nvs_flash

# New
REQUIRES esp_wifi_config nvs_flash
```


## Header File

| Old | New |
|-----|-----|
| `esp_wifi_manager.h` | `esp_wifi_config.h` |

```c
// Old
#include "esp_wifi_manager.h"

// New
#include "esp_wifi_config.h"
```


## Function Prefix

All public functions changed from `wifi_manager_` to `wifi_cfg_`.

| Old | New |
|-----|-----|
| `wifi_manager_init()` | `wifi_cfg_init()` |
| `wifi_manager_deinit()` | `wifi_cfg_deinit()` |
| `wifi_manager_connect()` | `wifi_cfg_connect()` |
| `wifi_manager_disconnect()` | `wifi_cfg_disconnect()` |
| `wifi_manager_add_network()` | `wifi_cfg_add_network()` |
| `wifi_manager_remove_network()` | `wifi_cfg_remove_network()` |
| `wifi_manager_update_network()` | `wifi_cfg_update_network()` |
| `wifi_manager_get_network()` | `wifi_cfg_get_network()` |
| `wifi_manager_list_networks()` | `wifi_cfg_list_networks()` |
| `wifi_manager_start_ap()` | `wifi_cfg_start_ap()` |
| `wifi_manager_stop_ap()` | `wifi_cfg_stop_ap()` |
| `wifi_manager_get_ap_status()` | `wifi_cfg_get_ap_status()` |
| `wifi_manager_set_ap_config()` | `wifi_cfg_set_ap_config()` |
| `wifi_manager_get_ap_config()` | `wifi_cfg_get_ap_config()` |
| `wifi_manager_set_var()` | `wifi_cfg_set_var()` |
| `wifi_manager_get_var()` | `wifi_cfg_get_var()` |
| `wifi_manager_del_var()` | `wifi_cfg_del_var()` |
| `wifi_manager_get_status()` | `wifi_cfg_get_status()` |
| `wifi_manager_wait_connected()` | `wifi_cfg_wait_connected()` |
| `wifi_manager_scan()` | `wifi_cfg_scan()` |
| `wifi_manager_get_httpd()` | `wifi_cfg_get_httpd()` |
| `wifi_manager_stop_http()` | `wifi_cfg_stop_http()` |

General rule: find-and-replace `wifi_manager_` with `wifi_cfg_`.


## Type Prefix

Types that previously used `wifi_mgr_` now use `wifi_cfg_`. The main config struct changed from `wifi_manager_config_t` to `wifi_cfg_config_t` (avoiding collision with ESP-IDF's own `wifi_config_t`).

| Old | New |
|-----|-----|
| `wifi_manager_config_t` | `wifi_cfg_config_t` |
| `wifi_mgr_ap_config_t` | `wifi_cfg_ap_config_t` |
| `wifi_mgr_http_config_t` | `wifi_cfg_http_config_t` |
| `wifi_mgr_ble_config_t` | `wifi_cfg_ble_config_t` |
| `wifi_mgr_http_hook_t` | `wifi_cfg_http_hook_t` |
| `wifi_mgr_var_validator_t` | `wifi_cfg_var_validator_t` |

General rule: find-and-replace `wifi_mgr_` with `wifi_cfg_` and `wifi_manager_config_t` with `wifi_cfg_config_t`.


## Event Constants

| Old | New |
|-----|-----|
| `WIFI_MGR_EVT_AP_START` | `WIFI_CFG_EVT_AP_START` |
| `WIFI_MGR_EVT_AP_STOP` | `WIFI_CFG_EVT_AP_STOP` |
| `WIFI_MGR_EVT_VAR_CHANGED` | `WIFI_CFG_EVT_VAR_CHANGED` |
| `WIFI_MGR_EVT_PROVISIONING_STARTED` | `WIFI_CFG_EVT_PROVISIONING_STARTED` |
| `WIFI_MGR_EVT_PROVISIONING_STOPPED` | `WIFI_CFG_EVT_PROVISIONING_STOPPED` |

General rule: find-and-replace `WIFI_MGR_EVT_` with `WIFI_CFG_EVT_`.


## Kconfig Options

All menuconfig options changed from `WIFI_MGR_` to `WIFI_CFG_`. If you reference these in `sdkconfig.defaults` or C code via `CONFIG_*`, update them.

| Old | New |
|-----|-----|
| `WIFI_MGR_MAX_NETWORKS` | `WIFI_CFG_MAX_NETWORKS` |
| `WIFI_MGR_MAX_VARS` | `WIFI_CFG_MAX_VARS` |
| `WIFI_MGR_DEFAULT_RETRY` | `WIFI_CFG_DEFAULT_RETRY` |
| `WIFI_MGR_RETRY_INTERVAL_MS` | `WIFI_CFG_RETRY_INTERVAL_MS` |
| `WIFI_MGR_AP_SSID` | Removed — use `wifi_cfg_config_t.default_ap.ssid` |
| `WIFI_MGR_AP_PASSWORD` | Removed — use `wifi_cfg_config_t.default_ap.password` |
| `WIFI_MGR_AP_IP` | Removed — use `wifi_cfg_config_t.default_ap.ip` |
| `WIFI_MGR_MAX_SCAN_RESULTS` | `WIFI_CFG_MAX_SCAN_RESULTS` |
| `WIFI_MGR_HTTP_MAX_CONTENT_LEN` | `WIFI_CFG_HTTP_MAX_CONTENT_LEN` |
| `WIFI_MGR_TASK_STACK_SIZE` | `WIFI_CFG_TASK_STACK_SIZE` |
| `WIFI_MGR_TASK_PRIORITY` | `WIFI_CFG_TASK_PRIORITY` |
| `WIFI_MGR_HTTP_MAX_URI_HANDLERS` | `WIFI_CFG_HTTP_MAX_URI_HANDLERS` |
| `WIFI_MGR_ENABLE_CLI` | `WIFI_CFG_ENABLE_CLI` |
| `WIFI_MGR_ENABLE_WEBUI` | `WIFI_CFG_ENABLE_WEBUI` |
| `WIFI_MGR_WEBUI_CUSTOM_PATH` | `WIFI_CFG_WEBUI_CUSTOM_PATH` |
| `WIFI_MGR_ENABLE_BLE` | `WIFI_CFG_ENABLE_BLE` |
| `WIFI_MGR_BLE_DEVICE_NAME` | Removed — use `wifi_cfg_config_t.ble.device_name` |

General rule: find-and-replace `WIFI_MGR_` with `WIFI_CFG_` in sdkconfig files and C code.

**Important:** After updating, run `idf.py fullclean` and re-run `idf.py menuconfig` to regenerate your sdkconfig with the new option names. Old `CONFIG_WIFI_MGR_*` entries in an existing sdkconfig will be silently ignored, reverting those settings to their defaults.


## Changed Function Signatures

### `wifi_cfg_deinit()` now requires a `bool deinit_wifi` parameter

*This change was originally proposed in [tuanpmt/esp_wifi_manager#7](https://github.com/tuanpmt/esp_wifi_manager/pull/7) but was never merged upstream. If you are already using that patch, this change will be familiar.*

The old `wifi_manager_deinit(void)` always tore down WiFi and destroyed the network interfaces, which meant calling deinit after provisioning would disconnect the WiFi session you just established. The new signature lets you choose:

```c
// Old
wifi_manager_deinit();  // always stopped WiFi and destroyed netifs

// New
wifi_cfg_deinit(true);  // same as old behavior: stop WiFi, destroy netifs, free resources
wifi_cfg_deinit(false); // tear down the manager (HTTP, BLE, event handlers, task)
                        // but keep WiFi connected and netifs alive
```

When `deinit_wifi` is `false`, the STA and AP network interfaces are preserved. You can reobtain their handles later via `esp_netif_get_handle_from_ifkey()` if needed.

**Typical usage**: after provisioning completes and WiFi is connected, call `wifi_cfg_deinit(false)` to free the manager's resources while keeping your WiFi connection active.


## mDNS Removed

The built-in mDNS integration has been removed. It was a convenience wrapper that initialized mDNS on WiFi connect and advertised an HTTP service — no core library functionality depended on it.

**What to do:**

- Remove the `.mdns` field from your `wifi_cfg_config_t` initializer.
- Remove `WIFI_CFG_MDNS_HOSTNAME` from any `sdkconfig.defaults` files.
- If you need mDNS, initialize it directly in your application after receiving the `WIFI_CFG_EVT_GOT_IP` event:

```c
#include "mdns.h"

static void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    mdns_init();
    mdns_hostname_set("my-device");
    mdns_instance_name_set("My Device");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}
```

You will also need to add `espressif/mdns` to your own project's `idf_component.yml` dependencies if you use mDNS.


## esp_bus Dependency

The `esp_bus` component is also now maintained under the new owner.

| Old | New |
|-----|-----|
| `tuanpmt/esp_bus` | `thorrak/esp_bus` |


## Repository URLs

| Old | New |
|-----|-----|
| `github.com/tuanpmt/esp_wifi_manager` | `github.com/thorrak/esp_wifi_config` |


## Quick Migration

For most projects, running these find-and-replace operations (in order) covers everything:

1. `esp_wifi_manager` &rarr; `esp_wifi_config`
2. `wifi_manager_` &rarr; `wifi_cfg_`
3. `wifi_mgr_` &rarr; `wifi_cfg_`
4. `WIFI_MGR_` &rarr; `WIFI_CFG_`
5. `tuanpmt/esp_bus` &rarr; `thorrak/esp_bus`
6. `tuanpmt` &rarr; `thorrak` (in dependency/URL contexts)

Then rebuild with `idf.py fullclean && idf.py build`.
