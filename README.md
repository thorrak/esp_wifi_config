# ESP WiFi Config

[![Component Registry](https://components.espressif.com/components/thorrak/esp_wifi_config/badge.svg)](https://components.espressif.com/components/thorrak/esp_wifi_config)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

WiFi Config component for ESP-IDF with multi-network support, auto-reconnect, SoftAP captive portal, Web UI, CLI, BLE, and REST API.

## Features

- **Multi-network support**: Save multiple WiFi networks with priority-based auto-connect
- **Auto-reconnect**: Automatic retry with exponential backoff and failover between saved networks
- **SoftAP mode**: Captive portal for initial configuration (triggers OS popup)
- **Web UI**: Embedded responsive web interface (Preact-based, ~10KB gzipped)
- **CLI interface**: Serial console commands for configuration
- **BLE GATT**: Configure WiFi via Bluetooth Low Energy (smartphone or Python CLI)
- **Improv WiFi**: Open standard provisioning via [Web Bluetooth](https://www.improv-wifi.com/) or Web Serial (Chrome/Edge)
- **REST API**: HTTP endpoints for remote configuration with CORS support
- **Basic Auth**: Optional authentication for HTTP endpoints
- **Custom variables**: Key-value storage for application settings
- **NVS persistence**: Networks, variables, and AP config stored in flash
- **esp_bus integration**: Event-driven architecture with actions and events

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                        ESP_WIFI_CONFIG                              │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │  WiFi Core                    │  NVS Storage                   │  │
│  │  ─────────                    │  ───────────                   │  │
│  │  • Multi-network              │  • Saved networks              │  │
│  │  • Auto retry + backoff       │  • AP configuration            │  │
│  │  • Reconnect logic            │  • Custom variables            │  │
│  │  • Captive portal + DNS       │                                │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ┌─────────────── Configuration Interfaces ───────────────────────┐  │
│  │                                                                │  │
│  │  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐        │  │
│  │  │ Web UI │ │  HTTP  │ │  CLI   │ │  BLE   │ │ Improv │        │  │
│  │  │(Preact)│ │  API   │ │(Console│ │  GATT  │ │  WiFi  │        │  │
│  │  └────────┘ └────────┘ └────────┘ └────────┘ └────────┘        │  │
│  │       │          │          │          │          │              │  │
│  │       └──────────┴──────────┴──────────┴──────────┘              │  │
│  │                              │                                │  │
│  │                              ▼                                │  │
│  │                 ┌─────────────────────────┐                   │  │
│  │                 │  WiFi Config Core API  │                   │  │
│  │                 └─────────────────────────┘                   │  │
│  └────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
         │                              │
         │ events                       │ requests
         ▼                              ▼
┌──────────────────────────────────────────────────────────────────────┐
│                             ESP_BUS                                  │
└──────────────────────────────────────────────────────────────────────┘
```

## Installation

### Using ESP-IDF Component Manager (Recommended)

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  thorrak/esp_wifi_config: "*"
```

### Manual Installation

Clone into your project's `components/` directory:

```bash
cd components
git clone https://github.com/thorrak/esp_wifi_config.git
```

## Quick Start

```c
#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "nvs_flash.h"

void app_main(void)
{
    // Initialize NVS (required)
    nvs_flash_init();

    // Initialize esp_bus (required before wifi_cfg_init)
    esp_bus_init();

    // Subscribe to WiFi events (optional)
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_connected_cb, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip_cb, NULL);

    // Initialize WiFi Config
    wifi_cfg_init(&(wifi_cfg_config_t){
        .default_networks = (wifi_network_t[]){
            {"HomeWifi", "password123", 10},   // priority 10 (highest)
            {"OfficeWifi", "office456", 5},    // priority 5 (fallback)
        },
        .default_network_count = 2,

        // Start AP provisioning when no networks saved or all fail
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .provisioning_teardown_delay_ms = 5000,
        .enable_ap = true,
    });

    // Wait for connection (30 second timeout)
    if (wifi_cfg_wait_connected(30000) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected!");
    }
}
```

## Examples

| Example | Description |
|---------|-------------|
| [basic](examples/basic/) | Minimal setup with default configuration |
| [with_cli](examples/with_cli/) | CLI interface with ESP Console REPL |
| [with_webui](examples/with_webui/) | Embedded Web UI (no external files needed) |
| [with_webui_customize](examples/with_webui_customize/) | Custom frontend from LittleFS |
| [with_ble](examples/with_ble/) | BLE GATT interface for smartphone/Python CLI |
| [with_improv](examples/with_improv/) | Improv WiFi provisioning (Web Bluetooth / Web Serial) |

## Configuration

### Kconfig Options

Configure via `idf.py menuconfig` → WiFi Config:

| Option | Default | Description |
|--------|---------|-------------|
| `WIFI_CFG_MAX_NETWORKS` | 5 | Maximum saved networks |
| `WIFI_CFG_MAX_VARS` | 10 | Maximum custom variables |
| `WIFI_CFG_DEFAULT_RETRY` | 3 | Retries per network |
| `WIFI_CFG_RETRY_INTERVAL_MS` | 5000 | Base retry interval (ms) |
| `WIFI_CFG_AP_SSID` | "ESP32-Config" | Default AP SSID |
| `WIFI_CFG_AP_PASSWORD` | "" | Default AP password |
| `WIFI_CFG_AP_IP` | "192.168.4.1" | Default AP IP |
| `WIFI_CFG_ENABLE_CLI` | n | Enable CLI interface |
| `WIFI_CFG_ENABLE_WEBUI` | n | Enable embedded Web UI |
| `WIFI_CFG_WEBUI_CUSTOM_PATH` | "" | Custom Web UI path (LittleFS/SPIFFS) |
| `WIFI_CFG_ENABLE_BLE` | n | Enable BLE GATT interface |
| `WIFI_CFG_BLE_DEVICE_NAME` | "ESP32-WiFi-{id}" | BLE device name (supports {id}) |
| `WIFI_CFG_ENABLE_IMPROV` | n | Enable Improv WiFi (master switch) |
| `WIFI_CFG_ENABLE_IMPROV_BLE` | y* | Enable Improv BLE transport (*when IMPROV + BT enabled) |
| `WIFI_CFG_ENABLE_IMPROV_SERIAL` | n | Enable Improv Serial transport |
| `WIFI_CFG_IMPROV_SERIAL_UART_NUM` | 0 | UART port for Improv Serial |
| `WIFI_CFG_IMPROV_SERIAL_BAUD` | 115200 | Baud rate for Improv Serial |

### Runtime Configuration

```c
wifi_cfg_config_t config = {
    // Default networks (fallback if NVS empty)
    .default_networks = networks,
    .default_network_count = 2,

    // Default variables
    .default_vars = (wifi_var_t[]){
        {"server_url", "https://api.example.com"},
        {"device_name", "my-device"},
    },
    .default_var_count = 2,

    // Retry config with exponential backoff
    .max_retry_per_network = 3,
    .retry_interval_ms = 5000,      // Base interval
    .retry_max_interval_ms = 60000, // Max backoff
    .auto_reconnect = true,

    // SoftAP config (supports {id} placeholder for MAC suffix)
    .default_ap = {
        .ssid = "MyDevice-{id}",
        .password = "",
        .ip = "192.168.4.1",
    },

    // Provisioning behavior
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .provisioning_teardown_delay_ms = 5000,
    .enable_ap = true,

    // Reconnect exhaustion
    .max_reconnect_attempts = 10,          // 0 = infinite
    .on_reconnect_exhausted = WIFI_RECONNECT_PROVISION, // or WIFI_RECONNECT_RESTART

    // HTTP post-provisioning mode
    .http_post_prov_mode = WIFI_HTTP_API_ONLY,  // FULL, API_ONLY, or DISABLED

    // HTTP interface
    .http = {
        .api_base_path = "/api/wifi",
        .enable_auth = true,
        .auth_username = "admin",
        .auth_password = "secret",
    },

    // BLE GATT (requires CONFIG_WIFI_CFG_ENABLE_BLE=y)
    .ble = {
        .enable = true,
        .device_name = "ESP32-WiFi-{id}",  // NULL uses Kconfig default
    },

    // Improv WiFi (requires CONFIG_WIFI_MGR_ENABLE_IMPROV=y)
    .improv = {
        .enable_ble = true,                    // Web Bluetooth provisioning
        .enable_serial = false,                // Web Serial provisioning
        .firmware_name = "my_project",         // Shown in Device Info
        .firmware_version = "1.0.0",
        .device_name = "My Device",
        .on_identify = my_identify_callback,   // Optional: flash LED on Identify
    },
};

wifi_cfg_init(&config);
```

### Provisioning Modes

The `provisioning_mode` field controls when the WiFi Config automatically starts provisioning interfaces (AP, BLE, and/or Improv):

| Mode | Behavior |
|------|----------|
| `WIFI_PROV_ALWAYS` | AP/BLE/Improv start at init and remain active, even after STA connects |
| `WIFI_PROV_ON_FAILURE` | Start provisioning when no networks are saved or all saved networks fail to connect |
| `WIFI_PROV_WHEN_UNPROVISIONED` | Start provisioning only if no networks exist in NVS |
| `WIFI_PROV_MANUAL` | Never auto-start provisioning; the application calls `wifi_cfg_start_ap()` explicitly (e.g., on button press) |

### Post-Connect Behavior

**Provisioning teardown:** When `stop_provisioning_on_connect` is `true`, the manager stops AP/BLE/Improv after the STA obtains an IP address. The `provisioning_teardown_delay_ms` value adds a delay before teardown so the Web UI can display connection results to the user.

**Reconnect exhaustion:** After a post-connect disconnect, the manager retries up to `max_reconnect_attempts` times (0 = infinite). When attempts are exhausted, `on_reconnect_exhausted` controls what happens:
- `WIFI_RECONNECT_PROVISION` — Re-enter provisioning mode so the user can reconfigure
- `WIFI_RECONNECT_RESTART` — Reboot the device via `esp_restart()`

**HTTP post-provisioning mode:** The `http_post_prov_mode` field controls the HTTP server after provisioning stops:
- `WIFI_HTTP_FULL` — Keep the full HTTP server running (Web UI + API)
- `WIFI_HTTP_API_ONLY` — Keep only REST API endpoints, remove Web UI and captive portal routes
- `WIFI_HTTP_DISABLED` — Stop the HTTP server entirely

## C API Reference

```c
// Initialization
esp_err_t wifi_cfg_init(const wifi_cfg_config_t *config);
esp_err_t wifi_cfg_deinit(void);

// Status
bool wifi_cfg_is_connected(void);
wifi_state_t wifi_cfg_get_state(void);
esp_err_t wifi_cfg_get_status(wifi_status_t *status);
esp_err_t wifi_cfg_wait_connected(uint32_t timeout_ms);

// Connection
esp_err_t wifi_cfg_connect(const char *ssid);  // NULL for auto-connect
esp_err_t wifi_cfg_disconnect(void);
esp_err_t wifi_cfg_scan(wifi_scan_result_t *results, size_t max, size_t *count);

// Network management
esp_err_t wifi_cfg_add_network(const wifi_network_t *network);
esp_err_t wifi_cfg_update_network(const wifi_network_t *network);
esp_err_t wifi_cfg_remove_network(const char *ssid);
esp_err_t wifi_cfg_list_networks(wifi_network_t *networks, size_t max, size_t *count);

// SoftAP
esp_err_t wifi_cfg_start_ap(const wifi_cfg_ap_config_t *config);
esp_err_t wifi_cfg_stop_ap(void);
esp_err_t wifi_cfg_get_ap_status(wifi_ap_status_t *status);

// Custom variables
esp_err_t wifi_cfg_set_var(const char *key, const char *value);
esp_err_t wifi_cfg_get_var(const char *key, char *value, size_t max_len);
esp_err_t wifi_cfg_del_var(const char *key);

// Factory reset
esp_err_t wifi_cfg_factory_reset(void);

// HTTP server management
httpd_handle_t wifi_cfg_get_httpd(void);
esp_err_t wifi_cfg_stop_http(void);  // Stop HTTP server (if library-owned and provisioning not active)
```

## esp_bus Integration

**Events (pub/sub):**

```c
void on_connected(const char *event, const void *data, size_t len, void *ctx) {
    wifi_connected_t *info = (wifi_connected_t *)data;
    ESP_LOGI(TAG, "Connected to %s, RSSI: %d", info->ssid, info->rssi);
}

esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_connected, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_disconnected, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_SCAN_DONE), on_scan_done, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_NETWORK_ADDED), on_network_added, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_VAR_CHANGED), on_var_changed, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_PROVISIONING_STARTED), on_prov_started, NULL);
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_PROVISIONING_STOPPED), on_prov_stopped, NULL);
```

## CLI Commands

Enable with `CONFIG_WIFI_CFG_ENABLE_CLI=y`:

| Command | Description |
|---------|-------------|
| `wifi status` | Show connection status |
| `wifi scan` | Scan available networks |
| `wifi list` | List saved networks |
| `wifi add <ssid> [password] [priority]` | Add network |
| `wifi del <ssid>` | Remove network |
| `wifi connect [ssid]` | Connect (auto or specific) |
| `wifi disconnect` | Disconnect |
| `wifi ap start` | Start SoftAP |
| `wifi ap stop` | Stop SoftAP |
| `wifi reset` | Factory reset |
| `wifi var get <key>` | Get variable |
| `wifi var set <key> <value>` | Set variable |

## BLE GATT Interface

Enable with `CONFIG_WIFI_CFG_ENABLE_BLE=y`. Both Bluedroid and NimBLE host stacks are supported. Requires Bluetooth enabled in sdkconfig:

**Bluedroid** (~100KB flash / ~40KB RAM):

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
```

**NimBLE** (~50KB flash / ~20KB RAM):

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144
```


The device advertises the WiFi Service UUID (`0xFFE0`), allowing clients to scan and filter by service UUID — the standard BLE discovery pattern. The device name (configurable via `CONFIG_WIFI_CFG_BLE_DEVICE_NAME`) is also included in the advertising data for further filtering.

### Stack Ownership & Deinitialization

The BLE interface supports two modes of operation depending on whether the application manages the BLE stack independently:

| Mode | Init behavior | Deinit behavior | Use case |
|------|---------------|-----------------|----------|
| **Owns the stack** (default) | Initializes BLE host stack + registers GATT service | Tears down everything (service, advertising, host stack, controller) | App doesn't use BLE for anything else |
| **Service only** | Detects host stack already running, registers GATT service only | Unregisters service and stops advertising, leaves host stack running | App manages the BLE lifecycle |

The mode is detected automatically: if the BLE host stack is already initialized when `wifi_cfg_init()` is called, the WiFi Config registers only its GATT service and leaves the stack alone on deinit. This mirrors the HTTP interface's shared server pattern — if you pass an existing `httpd_handle_t`, the HTTP handlers are unregistered on deinit without stopping the server.

**NimBLE:**

```c
// App-owned BLE stack: init NimBLE before WiFi Config
nimble_port_init();
nimble_port_freertos_init(nimble_host_task);

wifi_cfg_init(&(wifi_cfg_config_t){
    .ble = { .enable = true },
});

// Later: WiFi Config removes its GATT service but NimBLE keeps running
wifi_cfg_deinit();

// App can continue using BLE for its own services
```

**Bluedroid:**

```c
// App-owned BLE stack: init Bluedroid before WiFi Config
esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
esp_bt_controller_init(&bt_cfg);
esp_bt_controller_enable(ESP_BT_MODE_BLE);
esp_bluedroid_init();
esp_bluedroid_enable();

wifi_cfg_init(&(wifi_cfg_config_t){
    .ble = { .enable = true },
});

// Later: WiFi Config unregisters its GATT app but Bluedroid keeps running
wifi_cfg_deinit();

// App can continue using BLE for its own services
```

### Service & Characteristics

| UUID | Name | Properties | Description |
|------|------|------------|-------------|
| 0xFFE0 | WiFi Service | - | Main service |
| 0xFFE1 | Status | Read, Notify | Current WiFi status (JSON) |
| 0xFFE2 | Command | Write | Send JSON command |
| 0xFFE3 | Response | Notify | Command response (JSON) |

### Commands

Send JSON to the Command characteristic (0xFFE2). Parameters are passed in a `"params"` object. Responses are sent as notifications on the Response characteristic (0xFFE3). Large responses are automatically chunked across multiple notifications.

| Command          | Params | Description |
|------------------|--------|-------------|
| `get_status`     | (none) | Get connection status |
| `scan`           | (none) | Scan available networks |
| `list_networks`  | (none) | List saved networks |
| `add_network`    | `ssid`, `password`?, `priority`? | Add new network |
| `update_network` | `ssid`, `password`?, `priority`? | Update saved network |
| `del_network`    | `ssid` | Remove network |
| `connect`        | `ssid`? | Connect (auto or specific SSID) |
| `disconnect`     | (none) | Disconnect |
| `get_ap_status`  | (none) | Get AP status |
| `start_ap`       | `ssid`?, `password`? | Start SoftAP |
| `stop_ap`        | (none) | Stop SoftAP |
| `get_var`        | `key` | Get variable |
| `set_var`        | `key`, `value` | Set variable |
| `list_vars`      | (none) | List all variables |
| `del_var`        | `key` | Delete variable |
| `factory_reset`  | (none) | Factory reset |

### Example Commands

```json
{"cmd": "get_status"}
{"cmd": "scan"}
{"cmd": "list_networks"}
{"cmd": "add_network", "params": {"ssid": "MyWiFi", "password": "secret", "priority": 10}}
{"cmd": "update_network", "params": {"ssid": "MyWiFi", "password": "newpass", "priority": 5}}
{"cmd": "del_network", "params": {"ssid": "MyWiFi"}}
{"cmd": "connect", "params": {"ssid": "MyWiFi"}}
{"cmd": "get_var", "params": {"key": "device_name"}}
{"cmd": "set_var", "params": {"key": "device_name", "value": "My ESP32"}}
{"cmd": "list_vars"}
{"cmd": "del_var", "params": {"key": "device_name"}}
{"cmd": "factory_reset"}
```

### Response Format

Successful responses:

```json
{"status": "ok", "data": { ... }}
```

Error responses:

```json
{"status": "error", "error": "Error message"}
```

### Response Examples

**get_status**
```json
{
  "status": "ok",
  "data": {
    "state": "connected",
    "ssid": "MyWiFi",
    "rssi": -65,
    "quality": 70,
    "ip": "192.168.1.100",
    "channel": 6,
    "netmask": "255.255.255.0",
    "gateway": "192.168.1.1",
    "dns": "192.168.1.1",
    "mac": "AA:BB:CC:DD:EE:FF",
    "hostname": "esp32-aabbcc",
    "uptime_ms": 123456,
    "ap_active": false
  }
}
```

**get_ap_status**
```json
{
  "status": "ok",
  "data": {
    "active": true,
    "ssid": "ESP32-AABBCC",
    "ip": "192.168.4.1",
    "channel": 1,
    "sta_count": 2
  }
}
```

**list_vars**
```json
{
  "status": "ok",
  "data": {
    "vars": [
      {"key": "server_url", "value": "https://api.example.com"},
      {"key": "device_name", "value": "My ESP32"}
    ]
  }
}
```

### Python CLI Client

```bash
cd tools/wifi_ble_cli
pip install -r requirements.txt

# Global options: --device/-d (MAC address), --name/-n (name prefix, default "ESP32-WiFi")
python wifi_ble_cli.py --name "ESP32-WiFi" <command>

# Scan for BLE devices
python wifi_ble_cli.py devices

# Get WiFi status
python wifi_ble_cli.py status

# Scan WiFi networks
python wifi_ble_cli.py scan

# List saved networks
python wifi_ble_cli.py list

# Add network (with optional priority)
python wifi_ble_cli.py add "MyWiFi" "password123" --priority 10

# Delete a saved network
python wifi_ble_cli.py delete "MyWiFi"

# Connect (auto or specific SSID)
python wifi_ble_cli.py connect
python wifi_ble_cli.py connect "MyWiFi"

# Disconnect
python wifi_ble_cli.py disconnect

# AP management
python wifi_ble_cli.py ap-status
python wifi_ble_cli.py start-ap
python wifi_ble_cli.py stop-ap

# Custom variables
python wifi_ble_cli.py get-var device_name
python wifi_ble_cli.py set-var device_name "My ESP32"

# Factory reset (with confirmation prompt)
python wifi_ble_cli.py factory-reset
```

## Improv WiFi

[Improv WiFi](https://www.improv-wifi.com/) is an open standard by ESPHome for provisioning IoT devices over BLE or Serial using browsers and companion apps. It coexists with the custom BLE GATT service (0xFFE0) — both are advertised simultaneously.

### Enable

In `sdkconfig.defaults` (or via `idf.py menuconfig` → WiFi Manager → Improv WiFi):

```kconfig
# Master switch
CONFIG_WIFI_MGR_ENABLE_IMPROV=y

# BLE transport (requires Bluetooth enabled)
CONFIG_WIFI_MGR_ENABLE_IMPROV_BLE=y

# Serial transport (optional)
CONFIG_WIFI_MGR_ENABLE_IMPROV_SERIAL=y
CONFIG_WIFI_MGR_IMPROV_SERIAL_UART_NUM=0
CONFIG_WIFI_MGR_IMPROV_SERIAL_BAUD=115200
```

### Configuration

```c
wifi_manager_init(&(wifi_manager_config_t){
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .enable_ap = true,
    .improv = {
        .enable_ble = true,
        .enable_serial = false,
        .firmware_name = "my_project",
        .firmware_version = "1.0.0",
        .device_name = "My Device",
        .on_identify = my_led_flash_callback,  // Optional
    },
});
```

### How to Provision

**Via Web Bluetooth (Chrome/Edge):**
1. Open [improv-wifi.com](https://www.improv-wifi.com/) in Chrome or Edge
2. Click "Connect device via Bluetooth"
3. Select the device from the browser pairing dialog
4. Enter WiFi credentials — the device connects and returns its IP

**Via ESPHome Companion App:**
1. Install the ESPHome app (Android/iOS)
2. The device appears automatically for Improv provisioning
3. Tap and enter WiFi credentials

**Via Web Serial (if enabled):**
1. Open [improv-wifi.com](https://www.improv-wifi.com/) in Chrome or Edge
2. Click "Connect device via Serial"
3. Select the serial port and enter WiFi credentials

### Coexistence with Custom BLE

When both `ble.enable` and `improv.enable_ble` are `true`:
- The custom service (UUID `0xFFE0`) is in the primary advertising packet
- The Improv service (UUID `00467768-6228-2272-4663-277478268000`) is in the scan response
- Both services share the same BLE connection and stack instance
- A BLE scanner (e.g., nRF Connect) will show both services on the device

### Supported RPC Commands

| Command | ID | Description |
|---------|-----|-------------|
| Send WiFi Settings | 0x01 | Provide SSID + password, device connects |
| Identify | 0x02 | Flash LED / beep (calls `on_identify` callback) |
| Get Device Info | 0x03 | Returns firmware name, version, chip, device name |
| Get WiFi Networks | 0x04 | Triggers a WiFi scan and returns results |

## REST API Reference

Base URL: `http://<device-ip>/api/wifi` (configurable via `api_base_path`)

### Authentication

If `enable_auth = true`, all endpoints require Basic Auth:

```bash
curl -u admin:password http://192.168.4.1/api/wifi/status
```

### Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/status` | Get connection status |
| GET | `/scan` | Scan available networks |
| GET | `/networks` | List saved networks |
| POST | `/networks` | Add new network |
| PUT | `/networks/:ssid` | Update network |
| DELETE | `/networks/:ssid` | Remove network |
| POST | `/connect` | Connect (auto or specific SSID) |
| POST | `/disconnect` | Disconnect |
| GET | `/ap/status` | Get AP status |
| GET | `/ap/config` | Get AP configuration |
| PUT | `/ap/config` | Update AP configuration |
| POST | `/ap/start` | Start SoftAP |
| POST | `/ap/stop` | Stop SoftAP |
| GET | `/vars` | List custom variables |
| PUT | `/vars/:key` | Set variable |
| DELETE | `/vars/:key` | Delete variable |
| POST | `/factory_reset` | Factory reset |

### Example Requests

```bash
# Get status
curl http://192.168.4.1/api/wifi/status

# Scan networks
curl http://192.168.4.1/api/wifi/scan

# Add network
curl -X POST http://192.168.4.1/api/wifi/networks \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyWiFi", "password": "secret123", "priority": 10}'

# Connect to specific network
curl -X POST http://192.168.4.1/api/wifi/connect \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyWiFi"}'

# Auto-connect (highest priority)
curl -X POST http://192.168.4.1/api/wifi/connect

# Delete network
curl -X DELETE http://192.168.4.1/api/wifi/networks/MyWiFi

# Set custom variable
curl -X PUT http://192.168.4.1/api/wifi/vars/device_name \
  -H "Content-Type: application/json" \
  -d '{"value": "Living Room"}'
```

### Response Examples

**GET /status**
```json
{
  "state": "connected",
  "ssid": "MyWiFi",
  "ip": "192.168.1.100",
  "gateway": "192.168.1.1",
  "netmask": "255.255.255.0",
  "dns": "192.168.1.1",
  "rssi": -65,
  "quality": 70,
  "channel": 6,
  "mac": "AA:BB:CC:DD:EE:FF",
  "hostname": "esp32-aabbcc",
  "uptime_ms": 123456,
  "ap_active": false
}
```

**GET /scan**
```json
{
  "networks": [
    {"ssid": "MyWiFi", "rssi": -65, "auth": "WPA2"},
    {"ssid": "Neighbor", "rssi": -80, "auth": "WPA/WPA2"},
    {"ssid": "OpenNet", "rssi": -70, "auth": "OPEN"}
  ]
}
```

**GET /ap/status**
```json
{
  "active": true,
  "ssid": "ESP32-AABBCC",
  "ip": "192.168.4.1",
  "channel": 1,
  "sta_count": 2,
  "clients": [
    {"mac": "AA:BB:CC:DD:EE:01", "ip": "192.168.4.2"},
    {"mac": "AA:BB:CC:DD:EE:02", "ip": "192.168.4.3"}
  ]
}
```

### Error Responses

```json
{"error": "Error message"}
```

| HTTP Code | Description |
|-----------|-------------|
| 400 | Bad Request - Invalid JSON, missing field |
| 401 | Unauthorized - Auth required |
| 404 | Not Found - Network/Variable does not exist |
| 500 | Internal Error - Operation failed |

## Connection Flow

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

Try connect:
1. Load saved networks from NVS (sorted by priority DESC)
2. For each network:
   a. Attempt connection (max_retry_per_network times)
   b. Exponential backoff between retries
   c. Success → done
   d. Fail → try next network

Post-connect disconnect:
1. Auto-reconnect up to max_reconnect_attempts (0 = infinite)
2. If exhausted → on_reconnect_exhausted action:
   a. WIFI_RECONNECT_PROVISION → re-enter provisioning
   b. WIFI_RECONNECT_RESTART → esp_restart()

Provisioning teardown (when stop_provisioning_on_connect = true):
1. STA gets IP → wait provisioning_teardown_delay_ms
2. Emit PROVISIONING_STOPPED → stop AP/BLE/Improv
3. Transition HTTP per http_post_prov_mode
```

## Captive Portal

When provisioning starts with `enable_ap = true`, the device starts a SoftAP with captive portal:

1. Connect to AP (e.g., "ESP32-AABBCC")
2. OS automatically opens captive portal popup
3. Configure WiFi via Web UI or REST API
4. Device connects and provisioning stops (if `stop_provisioning_on_connect = true`), after `provisioning_teardown_delay_ms`

Supported captive portal detection:
- Android: `/generate_204`, `/gen_204`
- iOS/macOS: `/hotspot-detect.html`
- Windows: `/ncsi.txt`, `/connecttest.txt`
- Firefox: `/success.txt`, `/canonical.html`

## Migration from v1.x

The provisioning configuration has been redesigned. The old boolean fields have been replaced with a mode-based system:

| Old Field | New Equivalent |
|-----------|---------------|
| `enable_captive_portal = true` | `provisioning_mode = WIFI_PROV_ON_FAILURE, enable_ap = true` |
| `stop_ap_on_connect = true` | `stop_provisioning_on_connect = true` |
| `start_ap_on_init = true` | `provisioning_mode = WIFI_PROV_ALWAYS, enable_ap = true` |
| `http.enable = true` | Removed -- HTTP starts automatically when provisioning or `http_post_prov_mode` requires it |

**Before (v1.x):**
```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .enable_captive_portal = true,
    .stop_ap_on_connect = true,
    .http = { .enable = true },
});
```

**After (v2.x):**
```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .provisioning_teardown_delay_ms = 5000,
    .enable_ap = true,
});
```

## Dependencies

- ESP-IDF >= 5.0.0
- [esp_bus](https://components.espressif.com/components/thorrak/esp_bus) - Event bus component
- cJSON - JSON parsing (included in ESP-IDF)
- mbedTLS - Base64 for Basic Auth (included in ESP-IDF)

## Acknowledgments

This project is based on the original [esp_wifi_manager](https://github.com/tuanpmt/esp_wifi_manager) by [tuanpmt](https://github.com/tuanpmt). We are grateful for his foundational work that made this project possible.

## License

MIT License - see [LICENSE](LICENSE) file.
