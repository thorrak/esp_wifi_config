---
sidebar_position: 1
title: SoftAP & Captive Portal
description: Configure WiFi via the built-in SoftAP captive portal and Web UI
---

# SoftAP & Captive Portal

When provisioning starts with `enable_ap = true`, the device creates a WiFi access point with a captive portal that automatically opens in the user's browser.

## How It Works

1. Device starts a SoftAP (e.g., "ESP32-AABBCC")
2. User connects their phone/laptop to the AP
3. The OS detects the captive portal and automatically opens a browser popup
4. User configures WiFi via the Web UI or REST API
5. Device connects to the selected network
6. Provisioning stops (if `stop_provisioning_on_connect = true`) after `provisioning_teardown_delay_ms`

## AP Configuration

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .provisioning_teardown_delay_ms = 5000,
    .enable_ap = true,

    // Customize the SoftAP
    .default_ap = {
        .ssid = "MyDevice-{id}",   // {id} is replaced with last 3 bytes of MAC
        .password = "",             // empty = open AP
        .ip = "192.168.4.1",
    },
});
```

The `&#123;id&#125;` placeholder in the SSID is replaced with the last 3 bytes of the device's MAC address (e.g., "MyDevice-AABBCC"). This ensures each device has a unique AP name.

## Starting AP Manually

In `WIFI_PROV_MANUAL` mode, start the AP from your application code:

```c
// Start with default config (from wifi_cfg_config_t)
wifi_cfg_start_ap(NULL);

// Or start with custom config
wifi_cfg_start_ap(&(wifi_cfg_ap_config_t){
    .ssid = "MyDevice",
    .password = "12345678",
    .ip = "192.168.10.1",
});

// Stop the AP
wifi_cfg_stop_ap();
```

## AP Status

```c
wifi_ap_status_t ap_status;
wifi_cfg_get_ap_status(&ap_status);
if (ap_status.active) {
    ESP_LOGI(TAG, "AP: %s, IP: %s, Clients: %d",
             ap_status.ssid, ap_status.ip, ap_status.sta_count);
}
```

## Captive Portal Detection

The library responds to captive portal detection probes from all major platforms:

| Platform | Detection URLs |
|---|---|
| Android | `/generate_204`, `/gen_204` |
| iOS / macOS | `/hotspot-detect.html` |
| Windows | `/ncsi.txt`, `/connecttest.txt` |
| Firefox | `/success.txt`, `/canonical.html` |

All detection probes redirect to the Web UI at the AP's IP address.

## Web UI

Enable the embedded Web UI with `CONFIG_WIFI_CFG_ENABLE_WEBUI=y` in your sdkconfig. The Web UI is a Preact-based responsive interface (~10KB gzipped) that provides:

- WiFi network scanning and selection
- Saved network management
- Connection status display
- Custom variable editing
- AP configuration

For custom frontends, see the [with_webui_customize example](../examples/with-webui-customize).
