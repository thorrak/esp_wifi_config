# ESP WiFi Config

[![Component Registry](https://components.espressif.com/components/thorrak/esp_wifi_config/badge.svg)](https://components.espressif.com/components/thorrak/esp_wifi_config)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Docs](https://img.shields.io/badge/docs-configwifi.com-blue)](https://configwifi.com)

WiFi configuration component for ESP-IDF with multi-network support, auto-reconnect, and multiple provisioning interfaces.

**Targets:** ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-H2

## Features

- Multi-network support with priority-based auto-connect
- Auto-reconnect with exponential backoff and failover
- SoftAP captive portal (triggers OS popup)
- Embedded Web UI (Preact, ~10KB gzipped)
- Serial CLI, BLE GATT, Improv WiFi, REST API
- Custom key-value variable storage (NVS)
- Event-driven via [esp_bus](https://components.espressif.com/components/thorrak/esp_bus)

## Quick Start

Add to `main/idf_component.yml`:

```yaml
dependencies:
  thorrak/esp_wifi_config: "*"
```

```c
#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "nvs_flash.h"

void app_main(void)
{
    nvs_flash_init();
    esp_bus_init();

    wifi_cfg_init(&(wifi_cfg_config_t){
        .default_networks = (wifi_network_t[]){
            {"MyWiFi", "password", 10},
        },
        .default_network_count = 1,
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .enable_ap = true,
    });

    wifi_cfg_wait_connected(30000);
}
```

## Examples

| Example | Description |
|---------|-------------|
| [basic](examples/basic/) | Minimal setup with REST API and captive portal |
| [with_cli](examples/with_cli/) | Serial console CLI interface |
| [with_webui](examples/with_webui/) | Embedded Web UI |
| [with_webui_customize](examples/with_webui_customize/) | Custom frontend from LittleFS |
| [with_ble](examples/with_ble/) | BLE GATT provisioning |
| [with_improv](examples/with_improv/) | Improv WiFi (Web Bluetooth / Web Serial) |

## Documentation

Full documentation is available at **[configwifi.com](https://configwifi.com)**:

- [Getting Started](https://configwifi.com/docs/getting-started) — Installation and first project
- [Provisioning Modes](https://configwifi.com/docs/provisioning/modes) — Control when AP/BLE/Improv activate
- [API Reference](https://configwifi.com/docs/api/c-api) — C API, REST API, BLE protocol, CLI
- [Examples](https://configwifi.com/docs/examples/basic) — Complete example walkthroughs

### AI-Friendly Docs

Point your AI coding assistant at [`configwifi.com/llms.txt`](https://configwifi.com/llms.txt) for machine-readable documentation, or see the [AI Integration Guide](https://configwifi.com/docs/ai-integration-guide) for scenario-based recipes.

## Dependencies

- ESP-IDF >= 5.0.0
- [esp_bus](https://components.espressif.com/components/thorrak/esp_bus) (auto-resolved by component manager)

## Acknowledgments

Based on the original [esp_wifi_manager](https://github.com/tuanpmt/esp_wifi_manager) by [tuanpmt](https://github.com/tuanpmt).

## License

MIT License — see [LICENSE](LICENSE).
