# ESP WiFi Manager

[![Component Registry](https://components.espressif.com/components/tuanpmt/esp_wifi_manager/badge.svg)](https://components.espressif.com/components/tuanpmt/esp_wifi_manager)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

WiFi Manager component for ESP-IDF with multi-network support, auto-reconnect, SoftAP captive portal, and REST API configuration.

## Features

- **Multi-network support**: Save multiple WiFi networks with priority-based auto-connect
- **Auto-reconnect**: Automatic retry and failover between saved networks
- **SoftAP mode**: Captive portal for initial configuration
- **REST API**: HTTP endpoints for remote configuration
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
│  │  • Auto retry                 │  • Interface credentials       │  │
│  │  • Reconnect logic            │                                │  │
│  │  • Captive portal             │                                │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ┌─────────────── Configuration Interfaces ───────────────────────┐  │
│  │                                                                │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐       │  │
│  │  │   HTTP   │  │   BLE    │  │   CLI    │  │   ...    │       │  │
│  │  │  Server  │  │ (future) │  │ (future) │  │          │       │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘       │  │
│  │       │              │             │                          │  │
│  │       └──────────────┴─────────────┴──────────┐               │  │
│  │                                               ▼               │  │
│  │                              ┌─────────────────────────┐      │  │
│  │                              │  Interface Handler API  │      │  │
│  │                              │  (get_status, scan,     │      │  │
│  │                              │   add_network, ...)     │      │  │
│  │                              └─────────────────────────┘      │  │
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

## Configuration

### Kconfig Options

Configure via `idf.py menuconfig` → WiFi Manager:

| Option | Default | Description |
|--------|---------|-------------|
| `WIFI_MGR_MAX_NETWORKS` | 5 | Maximum saved networks |
| `WIFI_MGR_MAX_VARS` | 10 | Maximum custom variables |
| `WIFI_MGR_DEFAULT_RETRY` | 3 | Retries per network |
| `WIFI_MGR_RETRY_INTERVAL_MS` | 5000 | Retry interval (ms) |
| `WIFI_MGR_AP_SSID` | "ESP32-Config" | Default AP SSID |
| `WIFI_MGR_AP_PASSWORD` | "" | Default AP password |
| `WIFI_MGR_AP_IP` | "192.168.4.1" | Default AP IP |

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

    // Retry config
    .max_retry_per_network = 3,
    .retry_interval_ms = 5000,
    .auto_reconnect = true,

    // SoftAP config
    .default_ap = {
        .ssid = "MyDevice-Config",
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
};

wifi_manager_init(&config);
```

## API Reference

### C API

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

// Shared HTTP server
httpd_handle_t wifi_manager_get_httpd(void);
```

### esp_bus Integration

**Actions (request/response):**

```c
// Get status
wifi_status_t status;
esp_bus_req(WIFI_REQ(WIFI_ACTION_GET_STATUS), NULL, 0, &status, sizeof(status), NULL, 100);

// Add network
wifi_network_t net = {"NewWifi", "password", 8};
esp_bus_req(WIFI_REQ(WIFI_ACTION_ADD_NETWORK), &net, sizeof(net), NULL, 0, NULL, 100);

// Scan
wifi_scan_result_t results[10];
size_t count;
esp_bus_req(WIFI_REQ(WIFI_ACTION_SCAN), NULL, 0, results, sizeof(results), &count, 5000);
```

**Events (pub/sub):**

```c
void on_connected(const char *event, const void *data, size_t len, void *ctx) {
    wifi_connected_t *info = (wifi_connected_t *)data;
    ESP_LOGI(TAG, "Connected to %s, RSSI: %d", info->ssid, info->rssi);
}

esp_bus_subscribe(WIFI_EVT(WIFI_MGR_EVT_CONNECTED), on_connected, NULL);
esp_bus_subscribe(WIFI_EVT(WIFI_MGR_EVT_DISCONNECTED), on_disconnected, NULL);
esp_bus_subscribe(WIFI_EVT(WIFI_MGR_EVT_GOT_IP), on_got_ip, NULL);
```

### REST API

See [docs/API.md](docs/API.md) for full REST API documentation.

**Quick reference:**

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/wifi/status` | Get connection status |
| GET | `/api/wifi/scan` | Scan available networks |
| GET | `/api/wifi/networks` | List saved networks |
| POST | `/api/wifi/networks` | Add new network |
| DELETE | `/api/wifi/networks/:ssid` | Remove network |
| POST | `/api/wifi/connect` | Connect (auto or specific SSID) |
| POST | `/api/wifi/disconnect` | Disconnect |
| GET | `/api/wifi/ap/status` | Get AP status |
| POST | `/api/wifi/ap/start` | Start SoftAP |
| POST | `/api/wifi/ap/stop` | Stop SoftAP |
| GET | `/api/wifi/vars` | List custom variables |
| PUT | `/api/wifi/vars/:key` | Set variable |

## Connection Flow

```
boot → load saved networks → try connect →
  ├── success → emit connected event
  └── fail all → start captive portal (if enabled)

Auto-connect logic:
1. Load saved networks from NVS (sorted by priority DESC)
2. For each network:
   a. Try connect (max_retry_per_network times)
   b. Wait retry_interval_ms between retries
   c. If success → done
   d. If fail → try next network
3. If all fail and captive_portal enabled → start AP
4. If connected and disconnected → auto-reconnect if enabled
```

## Examples

See [examples/](examples/) directory for complete example projects.

## Dependencies

- ESP-IDF >= 5.0.0
- [esp_bus](https://github.com/tuanpmt/esp_bus) - Event bus component
- json - JSON parsing component

## License

MIT License - see [LICENSE](LICENSE) file.
