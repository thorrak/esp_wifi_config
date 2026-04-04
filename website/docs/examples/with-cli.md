---
sidebar_position: 2
title: With CLI
description: Serial console interface for WiFi configuration
---

# CLI Example

[View source on GitHub](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_cli)

Adds interactive serial console commands to the basic WiFi Config setup. Useful for development and headless devices.

## sdkconfig.defaults

```kconfig
CONFIG_IDF_TARGET="esp32s3"
CONFIG_WIFI_CFG_ENABLE_CLI=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

## Key Code

The CLI requires initializing the ESP Console REPL in your application:

```c
#include "esp_console.h"

// After wifi_cfg_init():
esp_console_repl_t *repl = NULL;
esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
repl_config.prompt = "esp> ";

esp_console_register_help_command();

// USB Serial JTAG console (for ESP32-S3)
esp_console_dev_usb_serial_jtag_config_t hw_config =
    ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl);
esp_console_start_repl(repl);
```

WiFi Config automatically registers its CLI commands when `CONFIG_WIFI_CFG_ENABLE_CLI=y`. See [CLI Commands](../api/cli-commands) for the full command reference.
