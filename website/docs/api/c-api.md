---
sidebar_position: 1
title: C API Reference
description: Complete C API reference — initialization, status, network management, SoftAP, variables
---

# C API Reference

All functions are declared in `esp_wifi_config.h` and return `esp_err_t` unless otherwise noted.

## Initialization

```c
// Initialize WiFi Config with the given configuration
esp_err_t wifi_cfg_init(const wifi_cfg_config_t *config);

// Deinitialize WiFi Config, stop all interfaces, free resources
esp_err_t wifi_cfg_deinit(void);
```

## Status

```c
// Check if STA is connected
bool wifi_cfg_is_connected(void);

// Get the current WiFi state enum
wifi_state_t wifi_cfg_get_state(void);

// Get detailed status (SSID, IP, RSSI, etc.)
esp_err_t wifi_cfg_get_status(wifi_status_t *status);

// Block until connected or timeout (0 = wait forever)
esp_err_t wifi_cfg_wait_connected(uint32_t timeout_ms);
```

## Connection

```c
// Connect to a specific SSID, or pass NULL for auto-connect (highest priority)
esp_err_t wifi_cfg_connect(const char *ssid);

// Disconnect from the current network
esp_err_t wifi_cfg_disconnect(void);

// Scan for available WiFi networks
esp_err_t wifi_cfg_scan(wifi_scan_result_t *results, size_t max, size_t *count);
```

## Network Management

```c
// Add a new network to NVS
esp_err_t wifi_cfg_add_network(const wifi_network_t *network);

// Update an existing network (matched by SSID)
esp_err_t wifi_cfg_update_network(const wifi_network_t *network);

// Remove a network from NVS
esp_err_t wifi_cfg_remove_network(const char *ssid);

// List all saved networks
esp_err_t wifi_cfg_list_networks(wifi_network_t *networks, size_t max, size_t *count);
```

## SoftAP

```c
// Start SoftAP with the given config, or NULL for defaults
esp_err_t wifi_cfg_start_ap(const wifi_cfg_ap_config_t *config);

// Stop SoftAP
esp_err_t wifi_cfg_stop_ap(void);

// Get AP status (active, SSID, IP, connected clients)
esp_err_t wifi_cfg_get_ap_status(wifi_ap_status_t *status);
```

## Custom Variables

```c
// Set a key-value variable (creates or updates in NVS)
esp_err_t wifi_cfg_set_var(const char *key, const char *value);

// Get a variable value
esp_err_t wifi_cfg_get_var(const char *key, char *value, size_t max_len);

// Delete a variable from NVS
esp_err_t wifi_cfg_del_var(const char *key);
```

## Factory Reset

```c
// Erase all saved networks, variables, and AP config from NVS
esp_err_t wifi_cfg_factory_reset(void);
```

## HTTP Server

```c
// Get the HTTP server handle (for registering custom endpoints)
httpd_handle_t wifi_cfg_get_httpd(void);

// Stop the HTTP server (only if library-owned and provisioning not active)
esp_err_t wifi_cfg_stop_http(void);
```

## Configuration Struct

The `wifi_cfg_config_t` struct controls all behavior:

```c
wifi_cfg_config_t config = {
    // Default networks (seed data for first boot when NVS is empty)
    .default_networks = networks,
    .default_network_count = 2,

    // Default variables (seed data for first boot)
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

    // SoftAP config ({id} = last 3 bytes of MAC)
    .default_ap = {
        .ssid = "MyDevice-{id}",
        .password = "",
        .ip = "192.168.4.1",
    },

    // Provisioning behavior
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .provisioning_teardown_delay_ms = 5000,
    .enable_ap = true,

    // Reconnect exhaustion
    .max_reconnect_attempts = 10,          // 0 = infinite
    .on_reconnect_exhausted = WIFI_RECONNECT_PROVISION,

    // HTTP post-provisioning mode
    .http_post_prov_mode = WIFI_HTTP_API_ONLY,

    // HTTP interface
    .http = {
        .api_base_path = "/api/wifi",
        .enable_auth = true,
        .auth_username = "admin",
        .auth_password = "secret",
    },

    // BLE GATT (requires CONFIG_WIFI_CFG_ENABLE_BLE=y)
    .ble = {
        .enable = true,
        .device_name = "ESP32-WiFi-{id}",
    },

    // Improv WiFi (requires CONFIG_WIFI_CFG_ENABLE_IMPROV=y)
    .improv = {
        .enable_ble = true,
        .enable_serial = false,
        .firmware_name = "my_project",
        .firmware_version = "1.0.0",
        .device_name = "My Device",
        .on_identify = my_identify_callback,
    },
};

wifi_cfg_init(&config);
```
