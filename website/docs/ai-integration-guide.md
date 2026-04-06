---
sidebar_position: 2
title: AI Integration Guide
description: For AI coding assistants — structured questionnaire and code generation recipes for integrating ESP WiFi Config
---

# AI Integration Guide

This page is written for AI coding assistants (Claude Code, Codex, Cursor, etc.) to help them integrate ESP WiFi Config into a user's ESP-IDF project. It is structured as a decision tree: walk through the questions with the user, collect their answers, then generate the appropriate code using the recipes at the bottom.

If you are a human reading this, you can follow the same flow — answer the questions, then use the matching recipe.

A standalone version of this questionnaire is available at [/llms-onboarding.txt](/llms-onboarding.txt) for loading into an AI coding agent's context.

---

## Prerequisites (always required)

Every project using ESP WiFi Config needs:

1. **ESP-IDF >= 5.0.0** installed and configured
2. **NVS flash** initialized before calling `wifi_cfg_init()`
3. **esp_bus** initialized before calling `wifi_cfg_init()`

---

## Question Flow

Questions are numbered. Some are conditional — only ask them if the indicated condition is met. Early answers prune later questions.

### Q1: Target chip

> What ESP32 chip are you targeting?

| Target | WiFi | Bluetooth | Notes |
|---|---|---|---|
| ESP32 | Yes | Classic + BLE | Most common, 2.4GHz only |
| ESP32-S2 | Yes | **No** | No BLE — skip all BLE/Improv BLE options |
| ESP32-S3 | Yes | BLE | USB-OTG, 2.4GHz only |
| ESP32-C3 | Yes | BLE | RISC-V, low cost |
| ESP32-C6 | Yes | BLE | WiFi 6, Thread/Zigbee |
| ESP32-H2 | **No** | BLE | **No WiFi — this library cannot be used** |

**Gate:** If ESP32-H2, stop. If ESP32-S2, disable all BLE and Improv BLE options in subsequent questions.

### Q2: Installation method

> How do you want to install the library?

| Method | Action |
|---|---|
| **ESP-IDF Component Manager** (recommended) | Add `thorrak/esp_wifi_config: "*"` to `main/idf_component.yml` |
| **Manual** | Clone into `components/` along with `esp_bus` |
| **PlatformIO** | Add to `lib_deps` in `platformio.ini` |

This affects project setup but not runtime code.

### Q3: Provisioning interfaces

> Which provisioning interfaces do you want to enable? (select all that apply)

| Interface | Description | Requires | Kconfig |
|---|---|---|---|
| **SoftAP + Captive Portal** | Device creates a WiFi AP; user connects and configures via browser popup | Nothing extra | (none — runtime `enable_ap = true`) |
| **Web UI** | Embedded Preact frontend served on the captive portal (richer than plain API) | SoftAP enabled | `CONFIG_WIFI_CFG_ENABLE_WEBUI=y` |
| **BLE GATT** | Configure via Bluetooth (smartphone app or Python CLI) | Bluetooth-capable chip | `CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=y` + BT stack |
| **Improv WiFi (BLE)** | Open standard provisioning via Chrome/Edge Web Bluetooth or ESPHome app | Bluetooth-capable chip | `CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y` + BT stack |
| **Improv WiFi (Serial)** | Open standard provisioning via Chrome/Edge Web Serial | UART access | `CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL=y` |
| **CLI** | Serial console commands (`wifi status`, `wifi scan`, etc.) | ESP Console REPL init in app code | `CONFIG_WIFI_CFG_ENABLE_CLI=y` |
| **None** | No provisioning — device only connects to hardcoded/NVS networks | — | — |

**If "None" is selected:** Skip Q4–Q8, jump to Q9.

**Guidance for the user:** Most consumer IoT devices want **SoftAP + Captive Portal** at minimum. Adding **BLE** or **Improv BLE** gives a smoother mobile experience. **Improv Serial** is useful for development/flashing workflows. **CLI** is primarily for development/debugging.

### Q4: When should provisioning activate?

> When should the provisioning interfaces start?

| Mode | `provisioning_mode` value | Best for |
|---|---|---|
| **When connection fails** | `WIFI_PROV_ON_FAILURE` | Most IoT devices — try saved networks first, fall back to provisioning |
| **Always** | `WIFI_PROV_ALWAYS` | Devices that need a permanently accessible config UI (e.g., local dashboard) |
| **First boot only** | `WIFI_PROV_WHEN_UNPROVISIONED` | Configure once, never show provisioning again |
| **Manual trigger only** | `WIFI_PROV_MANUAL` | App controls when provisioning starts (e.g., button press, GPIO) |

**Default recommendation:** `WIFI_PROV_ON_FAILURE` unless the user has a specific reason for another mode.

### Q5: Per-interface configuration

Only ask about interfaces selected in Q3.

#### Q5a: SoftAP settings (if SoftAP selected)

> Do you want to customize the AP name, password, or IP?

| Setting | Default | Notes |
|---|---|---|
| AP SSID | `"ESP32-Config"` (from Kconfig) | Supports `{id}` placeholder for last 3 MAC bytes (e.g., `"MyDevice-{id}"` → `"MyDevice-AABBCC"`) |
| AP Password | `""` (open network) | Set a password for a secured AP; must be 8+ characters if non-empty |
| AP IP | `"192.168.4.1"` | Only change if it conflicts with the user's network |

#### Q5b: BLE settings (if BLE selected)

> Do you want to customize the BLE device name?

| Setting | Default | Notes |
|---|---|---|
| Device name | `"ESP32-WiFi-{id}"` (from Kconfig) | `{id}` replaced with last 3 MAC bytes |

> Which Bluetooth stack do you prefer?

| Stack | Kconfig | Flash / RAM | Notes |
|---|---|---|---|
| **Bluedroid** | `CONFIG_BT_BLUEDROID_ENABLED=y` | ~100KB / ~40KB | ESP-IDF default, required for Improv BLE |
| **NimBLE** | `CONFIG_BT_NIMBLE_ENABLED=y` | ~50KB / ~20KB | Lighter, but **not compatible with Improv BLE** |

**Important:** If the user selected Improv BLE in Q3, they **must** use Bluedroid.

#### Q5c: Improv settings (if Improv selected)

> What firmware name, version, and device name should Improv report?

| Setting | Required | Notes |
|---|---|---|
| `firmware_name` | Yes | Shown in Improv UI (e.g., project name) |
| `firmware_version` | Yes | Shown in Improv UI (e.g., `"1.0.0"`) |
| `device_name` | Yes | Human-readable device name |
| `on_identify` callback | No | Optional function to flash LED / beep when Improv sends Identify |

#### Q5d: Improv Serial settings (if Improv Serial selected)

> Which UART port and baud rate?

| Setting | Default | Notes |
|---|---|---|
| UART port | `0` | Usually UART0 for USB |
| Baud rate | `115200` | Match the monitor baud rate |

#### Q5e: CLI settings (if CLI selected)

No configuration needed from the user. The library auto-registers commands when `CONFIG_WIFI_CFG_ENABLE_CLI=y`. The user must initialize the ESP Console REPL in their `app_main()`.

### Q6: Shared HTTP server

> Does your application already run an HTTP server, or will it need one alongside the WiFi config endpoints?

| Scenario | Action |
|---|---|
| **No existing server, no custom endpoints needed** | Do nothing — the library creates and manages its own HTTPD |
| **No existing server, but I want to add custom endpoints** | After `wifi_cfg_init()`, call `wifi_cfg_get_httpd()` to get the server handle and register custom routes |
| **I already have an HTTP server** | Pass the existing `httpd_handle_t` via `.http.httpd` — the library registers its routes on your server |

**Why this matters:** If the user passes their own HTTPD, the library never creates or destroys the server — it only registers/unregisters its own URI handlers. This is important for apps that run a web server for purposes beyond WiFi config.

### Q7: Post-provisioning teardown

> After the device successfully connects to WiFi, what should happen to the provisioning interfaces?

#### Q7a: Tear down provisioning?

| Choice | Config |
|---|---|
| **Yes, stop AP/BLE after connecting** (most common) | `.stop_provisioning_on_connect = true` |
| **No, keep provisioning running** | `.stop_provisioning_on_connect = false` |

#### Q7b: Teardown delay (if tearing down)

> Should there be a delay before teardown so the captive portal UI can show the connection result?

| Choice | Config |
|---|---|
| **Yes, 5 seconds** (recommended) | `.provisioning_teardown_delay_ms = 5000` |
| **No delay** | `.provisioning_teardown_delay_ms = 0` |
| **Custom** | `.provisioning_teardown_delay_ms = <value>` |

#### Q7c: HTTP behavior after provisioning stops

> After provisioning stops, what should happen to the HTTP server?

| Choice | `http_post_prov_mode` | Use case |
|---|---|---|
| **Keep everything** (Web UI + API) | `WIFI_HTTP_FULL` | Device serves a local dashboard over STA |
| **Keep API only** | `WIFI_HTTP_API_ONLY` | Other devices on the network can query/manage WiFi config |
| **Shut down HTTP entirely** | `WIFI_HTTP_DISABLED` | Minimal resource usage after provisioning |

**Default recommendation:** `WIFI_HTTP_DISABLED` for resource-constrained headless devices, `WIFI_HTTP_API_ONLY` for devices that benefit from remote management, `WIFI_HTTP_FULL` if a web UI should remain accessible.

### Q8: Reconnection behavior

> If the device loses WiFi after a successful connection, what should happen?

#### Q8a: Auto-reconnect

| Choice | Config |
|---|---|
| **Yes, auto-reconnect** (recommended) | `.auto_reconnect = true` (default) |
| **No, just emit a disconnect event** | `.auto_reconnect = false` |

#### Q8b: Reconnect exhaustion (if auto-reconnect enabled)

> If reconnection fails repeatedly, what should happen?

| Choice | Config |
|---|---|
| **Retry forever** (default) | `.max_reconnect_attempts = 0` |
| **After N failures, re-enter provisioning** | `.max_reconnect_attempts = N`, `.on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` |
| **After N failures, reboot** | `.max_reconnect_attempts = N`, `.on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_RESTART` |

### Q9: Custom variables

> Does your application need key-value settings that are configurable through the provisioning interfaces? (e.g., server URL, device name, update interval)

If yes:
- Collect the variable names and default values
- These are set via `.default_vars` and `.default_var_count`
- Max variables controlled by `CONFIG_WIFI_CFG_MAX_VARS` (default 10)

### Q10: HTTP authentication

> Should the WiFi config REST API endpoints require authentication?

| Choice | Config |
|---|---|
| **No auth** (default) | Do nothing |
| **Basic Auth** | `.http.enable_auth = true`, `.http.auth_username = "..."`, `.http.auth_password = "..."` |

### Q11: Default networks

> Should the firmware include any hardcoded WiFi networks as fallbacks? (These are only written to NVS on first boot)

If yes:
- Collect SSIDs, passwords, and priorities (0–255, higher = tried first)
- Set via `.default_networks` and `.default_network_count`

---

## Code Generation

Once all questions are answered, generate three files:

### 1. sdkconfig.defaults

Build the sdkconfig from Q3 and Q5 answers:

```kconfig
# === Always required ===
# (none — defaults work for basic WiFi)

# === BLE (if Q3 includes BLE or Improv BLE) ===
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y          # or CONFIG_BT_NIMBLE_ENABLED=y (see Q5b)
# CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144  # only if NimBLE
CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=y       # if custom BLE GATT selected
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y  # BLE adds ~100KB flash

# === Improv (if Q3 includes Improv) ===
CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y    # if Improv BLE selected
# CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL=y  # if Improv Serial selected

# === Web UI (if Q3 includes Web UI) ===
CONFIG_WIFI_CFG_ENABLE_WEBUI=y

# === CLI (if Q3 includes CLI) ===
CONFIG_WIFI_CFG_ENABLE_CLI=y
```

### 2. main/idf_component.yml

```yaml
dependencies:
  thorrak/esp_wifi_config: "*"
```

### 3. main/main.c

Assemble the `wifi_cfg_config_t` struct from the collected answers. The template below shows all fields — **only include fields where the user's answer differs from the default or where the field is required for their chosen interfaces**.

```c
#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "app";

static void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_got_ip_t *info = (wifi_got_ip_t *)data;
    ESP_LOGI(TAG, "Connected! IP: %s", info->ip);
    // >>> User's post-connection application logic goes here <<<
}

void app_main(void)
{
    // --- Required initialization ---
    nvs_flash_init();
    esp_bus_init();

    // --- Event subscriptions (before wifi_cfg_init) ---
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);

    // --- WiFi Config initialization ---
    wifi_cfg_init(&(wifi_cfg_config_t){

        // -- Default networks (Q11) --
        // .default_networks = (wifi_network_t[]){
        //     {"SSID", "password", PRIORITY},
        // },
        // .default_network_count = N,

        // -- Provisioning mode (Q4) --
        .provisioning_mode = WIFI_PROV_ON_FAILURE,  // Q4 answer

        // -- Provisioning teardown (Q7) --
        .stop_provisioning_on_connect = true,        // Q7a
        .provisioning_teardown_delay_ms = 5000,      // Q7b

        // -- HTTP post-provisioning (Q7c) --
        // .http_post_prov_mode = WIFI_HTTP_DISABLED, // Q7c answer

        // -- SoftAP (Q3 + Q5a) --
        .enable_ap = true,  // set to true if SoftAP selected in Q3
        // .default_ap = {
        //     .ssid = "MyDevice-{id}",   // Q5a
        //     .password = "",             // Q5a
        // },

        // -- Reconnection (Q8) --
        // .auto_reconnect = true,             // Q8a (default is true)
        // .max_reconnect_attempts = 0,        // Q8b (0 = infinite)
        // .on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_PROVISION, // Q8b

        // -- HTTP config (Q6, Q10) --
        // .http = {
        //     .httpd = my_server,             // Q6: pass existing server
        //     .api_base_path = "/api/wifi",
        //     .enable_auth = true,            // Q10
        //     .auth_username = "admin",       // Q10
        //     .auth_password = "changeme",    // Q10
        // },

        // -- BLE (Q3 + Q5b) — enabled via CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=y --
        // .ble = {
        //     .device_name = "ESP32-WiFi-{id}",  // Q5b
        // },

        // -- Improv (Q3 + Q5c + Q5d) --
        // Transports selected at compile time via Kconfig (CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE / _SERIAL)
        // .improv = {
        //     .firmware_name = "my_project",       // Q5c
        //     .firmware_version = "1.0.0",         // Q5c
        //     .device_name = "My Device",          // Q5c
        // },

        // -- Custom variables (Q9) --
        // .default_vars = (wifi_var_t[]){
        //     {"server_url", "https://api.example.com"},
        //     {"device_name", "my-device"},
        // },
        // .default_var_count = 2,
    });

    // --- CLI setup (if Q3 includes CLI) ---
    // esp_console_repl_t *repl = NULL;
    // esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    // repl_config.prompt = "esp> ";
    // esp_console_register_help_command();
    // esp_console_dev_usb_serial_jtag_config_t hw_config =
    //     ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    // esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl);
    // esp_console_start_repl(repl);

    // --- Shared HTTP server (if Q6 = "add custom endpoints") ---
    // httpd_handle_t server = wifi_cfg_get_httpd();
    // if (server) {
    //     httpd_uri_t my_endpoint = {
    //         .uri = "/api/my-data",
    //         .method = HTTP_GET,
    //         .handler = my_handler,
    //     };
    //     httpd_register_uri_handler(server, &my_endpoint);
    // }

    wifi_cfg_wait_connected(30000);
}
```

**Important code generation rules:**

1. **Only uncomment sections relevant to the user's answers.** Do not include commented-out blocks in the final output — only include active code.
2. **Subscribe to events before `wifi_cfg_init()`** to catch events fired during initialization.
3. **Use compound literals** for the config struct (the `&(wifi_cfg_config_t){...}` pattern) — this is idiomatic ESP-IDF C.
4. **Do not set fields to their default values.** If the user wants `auto_reconnect = true`, omit it (it's the default). Only set fields that differ from defaults.
5. **Include `#include "esp_console.h"`** only if CLI is enabled.

---

## Gotchas

1. **Must init NVS first**: Call `nvs_flash_init()` before `wifi_cfg_init()`. For robustness, handle `ESP_ERR_NVS_NO_FREE_PAGES` by erasing and re-initializing.
2. **Must init esp_bus first**: Call `esp_bus_init()` before `wifi_cfg_init()`.
3. **BLE requires Bluetooth enabled**: `CONFIG_BT_ENABLED=y` and either `CONFIG_BT_BLUEDROID_ENABLED=y` or `CONFIG_BT_NIMBLE_ENABLED=y`.
4. **BLE needs larger partition table**: Use `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` when enabling BLE.
5. **NimBLE needs stack size**: Set `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144` when using NimBLE.
6. **Improv BLE requires Bluedroid**: NimBLE is not compatible with the Improv BLE transport.
7. **Improv BLE is independent**: Improv BLE no longer requires `CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=y`. The BLE stack is initialized automatically when either BLE interface is enabled.
8. **Default networks are seeds**: They're only written to NVS on first boot. After that, NVS is the source of truth.
9. **ESP32 is 2.4GHz only**: The device cannot connect to 5GHz WiFi networks.
10. **Subscribe to events before init**: Call `esp_bus_sub()` before `wifi_cfg_init()` to catch events fired during initialization.
11. **ESP32-S2 has no Bluetooth**: BLE and Improv BLE cannot be used.
12. **ESP32-H2 has no WiFi**: This library cannot be used on ESP32-H2.

---

## Quick Reference: Config Field Defaults

Fields you can omit if the user wants the default behavior:

| Field | Default | Notes |
|---|---|---|
| `auto_reconnect` | `true` | |
| `max_retry_per_network` | `3` | |
| `retry_interval_ms` | `5000` | |
| `retry_max_interval_ms` | `60000` | Exponential backoff cap |
| `max_reconnect_attempts` | `0` | 0 = infinite |
| `provisioning_teardown_delay_ms` | `0` | Recommend 5000 for captive portal |
| `http_post_prov_mode` | `WIFI_HTTP_FULL` | |
| `http.api_base_path` | `"/api/wifi"` | |
| `http.auth_username` | `"admin"` | Only relevant if `enable_auth = true` |
| `http.auth_password` | `"admin"` | Only relevant if `enable_auth = true` |

---

## Scenario Quick-Picks

If the user describes their use case in general terms, map to these common configurations:

| Use case | Q4 mode | Interfaces | Post-prov HTTP | Reconnect |
|---|---|---|---|---|
| **Consumer IoT device** | `ON_FAILURE` | SoftAP + Web UI | `DISABLED` | Retry forever or re-provision after N |
| **Development / prototyping** | `ALWAYS` | SoftAP + Web UI + CLI | `FULL` | Retry forever |
| **Mobile-app-provisioned device** | `ON_FAILURE` | BLE (+ optionally SoftAP) | `DISABLED` | Re-provision after N |
| **ESPHome-style device** | `ON_FAILURE` | Improv BLE + SoftAP | `DISABLED` | Re-provision after N |
| **Local dashboard / gateway** | `ALWAYS` | SoftAP + Web UI | `FULL` | Retry forever |
| **Factory-configured device** | `MANUAL` | SoftAP + BLE (on button press) | `API_ONLY` | Reboot after N |
| **Headless sensor** | `ON_FAILURE` | SoftAP only | `DISABLED` | Reboot after N |
