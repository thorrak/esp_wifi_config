---
sidebar_position: 4
title: CLI Commands
description: Serial console commands for WiFi configuration
---

# CLI Commands

ESP WiFi Config provides a set of serial console commands via the ESP-IDF Console component. These are useful for development, debugging, and headless device configuration.

## Enabling the CLI

Add to your `sdkconfig.defaults`:

```kconfig
CONFIG_WIFI_CFG_ENABLE_CLI=y
```

Your application must initialize the ESP Console REPL. See the [with_cli example](../examples/with-cli) for a complete setup.

## Commands

| Command | Description |
|---|---|
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
