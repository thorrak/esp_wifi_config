---
sidebar_position: 0
slug: /
title: Overview
description: ESP WiFi Config — WiFi configuration component for ESP-IDF with multi-network support, auto-reconnect, and provisioning interfaces
---

# ESP WiFi Config

[![Component Registry](https://components.espressif.com/components/thorrak/esp_wifi_config/badge.svg)](https://components.espressif.com/components/thorrak/esp_wifi_config)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

ESP WiFi Config is a WiFi configuration component for ESP-IDF that handles multi-network management, automatic reconnection, and device provisioning through multiple interfaces.

## Features

- **Multi-network support** — Save multiple WiFi networks with priority-based auto-connect
- **Auto-reconnect** — Automatic retry with exponential backoff and failover between saved networks
- **SoftAP mode** — Captive portal for initial configuration (triggers OS popup)
- **Web UI** — Embedded responsive web interface (Preact-based, ~10KB gzipped)
- **CLI interface** — Serial console commands for configuration
- **BLE GATT** — Configure WiFi via Bluetooth Low Energy (smartphone or Python CLI)
- **Improv WiFi** — Open standard provisioning via [Web Bluetooth](https://www.improv-wifi.com/) or Web Serial (Chrome/Edge)
- **REST API** — HTTP endpoints for remote configuration with CORS support
- **Basic Auth** — Optional authentication for HTTP endpoints
- **Custom variables** — Key-value storage for application settings
- **NVS persistence** — Networks, variables, and AP config stored in flash
- **esp_bus integration** — Event-driven architecture with actions and events

## Supported Targets

ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-H2

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

All configuration interfaces feed into the same core API. The library uses [esp_bus](https://components.espressif.com/components/thorrak/esp_bus) for event-driven communication — your application subscribes to events (connected, disconnected, got IP, etc.) rather than polling.

## Dependencies

- **ESP-IDF** >= 5.0.0
- **[esp_bus](https://components.espressif.com/components/thorrak/esp_bus)** — Event bus component (auto-resolved by the component manager)
- **cJSON** — JSON parsing (included in ESP-IDF)
- **mbedTLS** — Base64 for Basic Auth (included in ESP-IDF)

## Next Steps

- [Getting Started](./getting-started.md) — Install the component and build your first project
- [AI Integration Guide](./ai-integration-guide.md) — For AI coding assistants: how to integrate this library
- [Provisioning Modes](./provisioning/modes.md) — Understand when and how provisioning interfaces activate
- [API Reference](./api/c-api.md) — Full C API documentation
