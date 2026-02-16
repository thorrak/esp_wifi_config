# ESP WiFi Manager

[![Component Registry](https://components.espressif.com/components/tuanpmt/esp_wifi_manager/badge.svg)](https://components.espressif.com/components/tuanpmt/esp_wifi_manager)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

WiFi Manager component for ESP-IDF with multi-network support, auto-reconnect, SoftAP captive portal, Web UI, CLI, BLE, and REST API.

## Features

- **Multi-network support**: Save multiple WiFi networks with priority-based auto-connect
- **Auto-reconnect**: Automatic retry with exponential backoff and failover between saved networks
- **SoftAP mode**: Captive portal for initial configuration (triggers OS popup)
- **Web UI**: Embedded responsive web interface (Preact-based, ~10KB gzipped)
- **CLI interface**: Serial console commands for configuration
- **BLE GATT**: Configure WiFi via Bluetooth Low Energy (smartphone or Python CLI)
- **REST API**: HTTP endpoints for remote configuration with CORS support
- **Basic Auth**: Optional authentication for HTTP endpoints
- **mDNS**: Access device via hostname (e.g., `esp32-abc123.local`)
- **Custom variables**: Key-value storage for application settings
- **NVS persistence**: Networks, variables, and AP config stored in flash
- **esp_bus integration**: Event-driven architecture with actions and events

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                        ESP_WIFI_MANAGER                              │
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
│  │  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐       │  │
│  │  │ Web UI │ │  HTTP  │ │  CLI   │ │  BLE   │ │  mDNS  │       │  │
│  │  │(Preact)│ │  API   │ │(Console│ │  GATT  │ │        │       │  │
│  │  └────────┘ └────────┘ └────────┘ └────────┘ └────────┘       │  │
│  │       │          │          │          │          │           │  │
│  │       └──────────┴──────────┴──────────┴──────────┘           │  │
│  │                              │                                │  │
│  │                              ▼                                │  │
│  │                 ┌─────────────────────────┐                   │  │
│  │                 │  WiFi Manager Core API  │                   │  │
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
  tuanpmt/esp_wifi_manager: "*"
```

### Manual Installation

Clone into your project's `components/` directory:

```bash
cd components
git clone https://github.com/tuanpmt/esp_wifi_manager.git
```

## Quick Start

```c
#include "esp_wifi_manager.h"
#include "esp_bus.h"
#include "nvs_flash.h"

void app_main(void)
{
    // Initialize NVS (required)
    nvs_flash_init();

    // Initialize esp_bus (required before wifi_manager_init)
    esp_bus_init();

    // Subscribe to WiFi events (optional)
    esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_CONNECTED), on_connected_cb, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_GOT_IP), on_got_ip_cb, NULL);

    // Initialize WiFi Manager
    wifi_manager_init(&(wifi_manager_config_t){
        .default_networks = (wifi_network_t[]){
            {"HomeWifi", "password123", 10},   // priority 10 (highest)
            {"OfficeWifi", "office456", 5},    // priority 5 (fallback)
        },
        .default_network_count = 2,

        // Enable HTTP REST API
        .http = {
            .enable = true,
            .api_base_path = "/api/wifi",
        },

        // Enable captive portal if no networks available
        .enable_captive_portal = true,
    });

    // Wait for connection (30 second timeout)
    if (wifi_manager_wait_connected(30000) == ESP_OK) {
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

## Configuration

### Kconfig Options

Configure via `idf.py menuconfig` → WiFi Manager:

| Option | Default | Description |
|--------|---------|-------------|
| `WIFI_MGR_MAX_NETWORKS` | 5 | Maximum saved networks |
| `WIFI_MGR_MAX_VARS` | 10 | Maximum custom variables |
| `WIFI_MGR_DEFAULT_RETRY` | 3 | Retries per network |
| `WIFI_MGR_RETRY_INTERVAL_MS` | 5000 | Base retry interval (ms) |
| `WIFI_MGR_AP_SSID` | "ESP32-Config" | Default AP SSID |
| `WIFI_MGR_AP_PASSWORD` | "" | Default AP password |
| `WIFI_MGR_AP_IP` | "192.168.4.1" | Default AP IP |
| `WIFI_MGR_MDNS_HOSTNAME` | "esp32-{id}" | mDNS hostname template |
| `WIFI_MGR_ENABLE_CLI` | n | Enable CLI interface |
| `WIFI_MGR_ENABLE_WEBUI` | n | Enable embedded Web UI |
| `WIFI_MGR_WEBUI_CUSTOM_PATH` | "" | Custom Web UI path (LittleFS/SPIFFS) |
| `WIFI_MGR_ENABLE_BLE` | n | Enable BLE GATT interface |
| `WIFI_MGR_BLE_DEVICE_NAME` | "ESP32-WiFi-{id}" | BLE device name (supports {id}) |

### Runtime Configuration

```c
wifi_manager_config_t config = {
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
    .enable_captive_portal = true,
    .stop_ap_on_connect = true,

    // HTTP interface
    .http = {
        .enable = true,
        .api_base_path = "/api/wifi",
        .enable_auth = true,
        .auth_username = "admin",
        .auth_password = "secret",
    },

    // mDNS (supports {id} placeholder)
    .mdns = {
        .enable = true,
        .hostname = "esp32-{id}",
    },

    // BLE GATT (requires CONFIG_WIFI_MGR_ENABLE_BLE=y)
    .ble = {
        .enable = true,
        .device_name = "ESP32-WiFi-{id}",  // NULL uses Kconfig default
    },
};

wifi_manager_init(&config);
```

## C API Reference

```c
// Initialization
esp_err_t wifi_manager_init(const wifi_manager_config_t *config);
esp_err_t wifi_manager_deinit(void);

// Status
bool wifi_manager_is_connected(void);
wifi_state_t wifi_manager_get_state(void);
esp_err_t wifi_manager_get_status(wifi_status_t *status);
esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms);

// Connection
esp_err_t wifi_manager_connect(const char *ssid);  // NULL for auto-connect
esp_err_t wifi_manager_disconnect(void);
esp_err_t wifi_manager_scan(wifi_scan_result_t *results, size_t max, size_t *count);

// Network management
esp_err_t wifi_manager_add_network(const wifi_network_t *network);
esp_err_t wifi_manager_update_network(const wifi_network_t *network);
esp_err_t wifi_manager_remove_network(const char *ssid);
esp_err_t wifi_manager_list_networks(wifi_network_t *networks, size_t max, size_t *count);

// SoftAP
esp_err_t wifi_manager_start_ap(const wifi_mgr_ap_config_t *config);
esp_err_t wifi_manager_stop_ap(void);
esp_err_t wifi_manager_get_ap_status(wifi_ap_status_t *status);

// Custom variables
esp_err_t wifi_manager_set_var(const char *key, const char *value);
esp_err_t wifi_manager_get_var(const char *key, char *value, size_t max_len);
esp_err_t wifi_manager_del_var(const char *key);

// Factory reset
esp_err_t wifi_manager_factory_reset(void);

// Shared HTTP server (for adding custom endpoints)
httpd_handle_t wifi_manager_get_httpd(void);
```

## esp_bus Integration

**Events (pub/sub):**

```c
void on_connected(const char *event, const void *data, size_t len, void *ctx) {
    wifi_connected_t *info = (wifi_connected_t *)data;
    ESP_LOGI(TAG, "Connected to %s, RSSI: %d", info->ssid, info->rssi);
}

esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_CONNECTED), on_connected, NULL);
esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_DISCONNECTED), on_disconnected, NULL);
esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_GOT_IP), on_got_ip, NULL);
esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_SCAN_DONE), on_scan_done, NULL);
esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_NETWORK_ADDED), on_network_added, NULL);
esp_bus_sub(WIFI_EVT(WIFI_MGR_EVT_VAR_CHANGED), on_var_changed, NULL);
```

## CLI Commands

Enable with `CONFIG_WIFI_MGR_ENABLE_CLI=y`:

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

Enable with `CONFIG_WIFI_MGR_ENABLE_BLE=y`. Requires Bluetooth enabled in sdkconfig:

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
```

or for NimBLE stack:

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144
```


### Service & Characteristics

| UUID | Name | Properties | Description |
|------|------|------------|-------------|
| 0xFFE0 | WiFi Service | - | Main service |
| 0xFFE1 | Status | Read, Notify | Current WiFi status (JSON) |
| 0xFFE2 | Command | Write | Send JSON command |
| 0xFFE3 | Response | Notify | Command response (JSON) |

### Commands

Send JSON to Command characteristic (0xFFE2):

```json
{"cmd": "get_status"}
{"cmd": "scan"}
{"cmd": "list_networks"}
{"cmd": "add_network", "ssid": "MyWiFi", "pass": "secret", "prio": 10}
{"cmd": "del_network", "ssid": "MyWiFi"}
{"cmd": "connect"}
{"cmd": "connect", "ssid": "MyWiFi"}
{"cmd": "disconnect"}
{"cmd": "get_ap_status"}
{"cmd": "start_ap"}
{"cmd": "stop_ap"}
{"cmd": "get_var", "key": "device_name"}
{"cmd": "set_var", "key": "device_name", "val": "My ESP32"}
{"cmd": "factory_reset"}
```

### Python CLI Client

```bash
cd tools/wifi_ble_cli
pip install -r requirements.txt

# Scan for devices
python wifi_ble_cli.py devices

# Get status
python wifi_ble_cli.py status

# Add network
python wifi_ble_cli.py add "MyWiFi" "password123"

# Scan networks
python wifi_ble_cli.py scan

# Connect
python wifi_ble_cli.py connect
```

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
boot → load saved networks → try connect →
  ├── success → emit CONNECTED event → emit GOT_IP event
  └── fail all → start captive portal (if enabled)

Auto-connect logic:
1. Load saved networks from NVS (sorted by priority DESC)
2. For each network:
   a. Try connect (max_retry_per_network times)
   b. Exponential backoff between retries
   c. If success → done
   d. If fail → try next network
3. If all fail and captive_portal enabled → start AP
4. If connected and disconnected → auto-reconnect if enabled
```

## Captive Portal

When no networks are configured or all connections fail, the device starts a SoftAP with captive portal:

1. Connect to AP (e.g., "ESP32-AABBCC")
2. OS automatically opens captive portal popup
3. Configure WiFi via Web UI or REST API
4. Device connects and AP stops (if `stop_ap_on_connect = true`)

Supported captive portal detection:
- Android: `/generate_204`, `/gen_204`
- iOS/macOS: `/hotspot-detect.html`
- Windows: `/ncsi.txt`, `/connecttest.txt`
- Firefox: `/success.txt`, `/canonical.html`

## Dependencies

- ESP-IDF >= 5.0.0
- [esp_bus](https://components.espressif.com/components/tuanpmt/esp_bus) - Event bus component
- cJSON - JSON parsing (included in ESP-IDF)
- mbedTLS - Base64 for Basic Auth (included in ESP-IDF)

## License

MIT License - see [LICENSE](LICENSE) file.
