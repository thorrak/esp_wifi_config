# ESP WiFi Config

[![Component Registry](https://components.espressif.com/components/thorrak/esp_wifi_config/badge.svg)](https://components.espressif.com/components/thorrak/esp_wifi_config)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Docs](https://img.shields.io/badge/docs-configwifi.com-blue)](https://configwifi.com)

WiFi configuration component for ESP-IDF with multi-network support, auto-reconnect, and multiple provisioning interfaces. The library supports four provisioning methods with an optional Serial CLI for debugging:

* Bluetooth (BLE) Provisionining
* SoftAP Provisioning
* Improv WiFi Serial
* Improv WiFi Bluetooth

These are provided "batteries included" with additional features such as:
* Multi-network store (optionally configure multiple networks at once)
* Auto-reconnect with exponential backoff
* Configurable logic when the chip is unable to connect to a saved network or is unexpectedly disconnected
* Configurable state machine that decides when each interface activates and shuts down

It's a one-stop shop: enable the channels you want at build time, fill in a `wifi_cfg_init()` struct, and the rest — captive portal popup, BLE pairing, credential storage, retries, reconnects, and teardown — is handled for you.

## Features

- **Four provisioning channels** plus a serial CLI, all sharing one config struct and one network store (see [Provisioning Methods](#provisioning-methods))
- **Multi-network storage** in NVS with priority-based connect order
- **Auto-reconnect** with exponential backoff and failover between saved networks
- **Provisioning lifecycle** — `ALWAYS` / `ON_FAILURE` / `WHEN_UNPROVISIONED` / `MANUAL` start modes, configurable post-connect teardown delay, and three post-provisioning HTTP behaviours
- **Reconnect-exhaustion policy** — re-enter provisioning or reboot after N failed reconnects
- **Embedded Web UI** — responsive Preact frontend (~10 KB gzipped) served on the captive portal, or [bring your own](https://configwifi.com/docs/guides/custom-webui)
- **REST API** with optional HTTP Basic Auth
- **Custom variable store** — application key/value settings flow through every provisioning interface
- **Event-driven** via [esp_bus](https://components.espressif.com/components/thorrak/esp_bus) (connected, disconnected, got IP, provisioning started/stopped)

**Targets:** ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-H2

## Provisioning Methods

All channels share the same multi-network store and lifecycle policy — enable any compatible combination you want at build time.

| Channel | How a user sets up the device | Typical client |
|---|---|---|
| **SoftAP + Captive Portal** | Phone or laptop joins the device's WiFi AP; the OS pops the configuration page automatically | Any web browser |
| **BLE** | Mobile app or CLI pairs over Bluetooth and pushes credentials | iOS/Android App (see below), `esp_prov` Python tool |
| **Improv WiFi** (BLE and Serial transports) | Open ESPHome standard over Web Bluetooth or Web Serial | [improv-wifi.com](https://www.improv-wifi.com/) in Chrome/Edge, ESPHome companion app |
| **Serial CLI** *(optional)* | Console commands (`wifi scan`, `wifi connect`, …) over UART | Any serial monitor |

The SoftAP + captive portal, Improv WiFi (both transports), Serial CLI, and REST API are implemented **inside this library** — they don't depend on any Espressif provisioning component. The **BLE** channel is built on top of Espressif's official `wifi_provisioning` component (`network_provisioning` on IDF 6.x) as its transport; see the next section for what that means in practice.

The two BLE channels (this library's BLE and Improv WiFi BLE) are mutually exclusive at compile time — they each want to own the BLE GAP advertising slot. Improv Serial is independent of BLE and can be combined with either - but is mutually exclusive with the Serial CLI. Soft AP can be enabled alongside any of the other options. 

## Compatibility with Espressif's Network Provisioning

Because the BLE channel uses **Espressif's `wifi_provisioning` component** as its transport, the on-the-wire protocol is the standard Espressif one:

- **Espressif-compatible clients work out of the box.** "ESP BLE Provisioning" mobile apps, the `esp_prov` Python tool in any ESP-IDF checkout, and the `esp-idf-provisioning-android` / `-ios` SDKs all pair with devices running this library.
- **Security 1 (PoP)** and **Security 2 (SRP6a)** handshakes are both supported. Set `prov.pop` (Security 1) or `prov.security2_salt` / `prov.security2_verifier` (Security 2) in your `wifi_cfg_init()` call — the example uses `"abcd1234"` so Espressif's apps pair out of the box.
- The standard `prov-config` / `prov-scan` endpoints behave exactly as Espressif's clients expect.

What `wifi_provisioning` does on its own is hand a single set of credentials to `esp_wifi` once, over BLE (or SoftAP). It has no opinion about what happens before, after, or alongside that handoff. This library wraps `wifi_provisioning` for the BLE handshake and adds everything else:

| | Espressif `wifi_provisioning` alone | This library |
|---|---|---|
| BLE handshake + single credential transfer | ✅ | uses Espressif's |
| Multi-network store with priority and failover | — | ✅ |
| Auto-reconnect with exponential backoff | — | ✅ |
| Provisioning lifecycle state machine (when to start, when to tear down) | — | ✅ |
| Reconnect-exhaustion behaviour (re-provision or reboot) | — | ✅ |
| SoftAP + captive portal + embedded Web UI | — | ✅ |
| Improv WiFi (BLE + Serial) | — | ✅ |
| Serial CLI | — | ✅ |
| REST API with optional Basic Auth | — | ✅ |
| Custom application key/value store | — | ✅ (exposed via every interface, plus a custom protocomm endpoint over BLE) |
| Event-driven integration via [esp_bus](https://components.espressif.com/components/thorrak/esp_bus) | — | ✅ |

In short: if all you need is to hand off WiFi credentials once over BLE, Espressif's component on its own is enough. If you want a device that remembers multiple networks, decides on its own when to reopen provisioning, exposes the same configuration over a captive portal and serial console, and emits events your application can react to — that's the rest of this library.

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
| [with_ble](examples/with_ble/) | BLE provisioning (on top of Espressif's `wifi_prov_mgr`) |
| [with_ble_deinit](examples/with_ble_deinit/) | Improv BLE host-stack handoff to the application |
| [with_improv](examples/with_improv/) | Improv WiFi (Web Bluetooth / Web Serial) |

## Documentation

Full documentation is available at **[configwifi.com](https://configwifi.com)**:

- [Getting Started](https://configwifi.com/docs/getting-started) — Installation and first project
- [Provisioning Modes](https://configwifi.com/docs/provisioning/modes) — Control when AP/BLE/Improv activate
- [API Reference](https://configwifi.com/docs/api/c-api) — C API, REST API, BLE protocol, CLI
- [Examples](https://configwifi.com/docs/examples) — Complete example walkthroughs

### AI-Friendly Docs

Point your AI coding assistant at [`configwifi.com/llms.txt`](https://configwifi.com/llms.txt) for machine-readable documentation, or see the [AI Integration Guide](https://configwifi.com/docs/ai-integration-guide) for scenario-based recipes.

## Dependencies

- ESP-IDF >= 5.4
  ESP-IDF 6.x is supported via `idf_component.yml`'s automatic switch to
  `espressif/network_provisioning`.
- [esp_bus](https://components.espressif.com/components/thorrak/esp_bus) (auto-resolved by component manager)

## Acknowledgments

Based on the original [esp_wifi_manager](https://github.com/tuanpmt/esp_wifi_manager) by [tuanpmt](https://github.com/tuanpmt).

## License

MIT License — see [LICENSE](LICENSE).
