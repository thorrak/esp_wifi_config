---
sidebar_position: 4
title: Custom Web UI
description: Serve a custom frontend from LittleFS instead of the embedded Web UI
---

# Custom Web UI Example

[View source on GitHub](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_webui_customize)

Demonstrates loading a custom web frontend from a LittleFS partition instead of the embedded Web UI. This allows updating the UI without recompiling firmware.

## sdkconfig.defaults

```kconfig
CONFIG_IDF_TARGET="esp32s3"
CONFIG_WIFI_CFG_ENABLE_WEBUI=y
CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH="/littlefs"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

## How It Works

1. A custom partition table includes a 512KB LittleFS partition
2. The firmware mounts LittleFS at `/littlefs`
3. If custom frontend files exist at `/littlefs/index.html`, they are served instead of the embedded UI
4. If the LittleFS partition is empty, the embedded UI is used as a fallback

## File Structure

Place your custom frontend files in the `www/` directory:

```
www/
  index.html
  assets/
    app.js.gz      # gzip compression recommended
    index.css.gz
```

The REST API endpoints remain the same — your custom frontend just needs to call them.
