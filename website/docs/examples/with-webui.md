---
sidebar_position: 3
title: With Web UI
description: Embedded responsive web interface for WiFi configuration
---

# Web UI Example

[View source on GitHub](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_webui)

Enables the built-in Preact-based Web UI (~10KB gzipped) that provides a responsive interface for WiFi configuration.

## sdkconfig.defaults

```kconfig
CONFIG_IDF_TARGET="esp32s3"
CONFIG_WIFI_CFG_ENABLE_WEBUI=y
```

## What You Get

With `CONFIG_WIFI_CFG_ENABLE_WEBUI=y`, the device serves a full web interface at its IP address (or `192.168.4.1` in AP mode). Features:

- WiFi network scanning and selection
- Saved network management (add/remove/reorder)
- Real-time connection status
- Custom variable editing
- Dark mode (auto-detects OS preference)
- Captive portal support (auto-opens on phone/laptop)

No additional code is needed beyond the standard WiFi Config init — the Web UI is embedded in the firmware.
