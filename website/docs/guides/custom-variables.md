---
sidebar_position: 2
title: Custom Variables
description: Store and retrieve application key-value settings via NVS
---

# Custom Variables

ESP WiFi Config includes a key-value store for application settings that persists in NVS alongside WiFi configuration. Variables can be managed via any interface — C API, REST API, BLE, CLI, or Web UI.

## Configuration

Provide default variables in the init config. These are written to NVS only on first boot (when no variables exist):

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .default_vars = (wifi_var_t[]){
        {"server_url", "https://api.example.com"},
        {"device_name", "my-device"},
    },
    .default_var_count = 2,

    // ... other config
});
```

The maximum number of variables is controlled by `CONFIG_WIFI_CFG_MAX_VARS` (default: 10).

## C API Usage

```c
// Set a variable (creates or updates)
wifi_cfg_set_var("server_url", "https://api.example.com");
wifi_cfg_set_var("device_id", "device-001");

// Get a variable
char value[128];
if (wifi_cfg_get_var("server_url", value, sizeof(value)) == ESP_OK) {
    ESP_LOGI(TAG, "Server URL: %s", value);
}

// Delete a variable
wifi_cfg_del_var("device_id");
```

## Listening for Changes

Subscribe to the `WIFI_CFG_EVT_VAR_CHANGED` event to react when any interface modifies a variable:

```c
static void on_var_changed(const char *event, const void *data, size_t len, void *ctx)
{
    wifi_var_changed_t *info = (wifi_var_changed_t *)data;
    ESP_LOGI(TAG, "Variable changed: %s = %s", info->key, info->value);
}

esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_VAR_CHANGED), on_var_changed, NULL);
```

## Managing Variables via Other Interfaces

Variables are accessible from all configuration interfaces:

- **REST API**: `PUT /api/wifi/vars/:key`, `GET /api/wifi/vars`, `DELETE /api/wifi/vars/:key`
- **BLE**: `set_var`, `get_var`, `list_vars`, `del_var` commands
- **CLI**: `wifi var set <key> <value>`, `wifi var get <key>`
- **Web UI**: Variables section in the embedded web interface
