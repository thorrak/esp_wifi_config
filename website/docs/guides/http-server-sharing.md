---
sidebar_position: 4
title: HTTP Server Sharing
description: Share the HTTP server handle with your application for custom endpoints
---

# HTTP Server Sharing

ESP WiFi Config can share its HTTP server with your application, allowing you to add custom endpoints alongside the WiFi configuration API.

## Getting the Server Handle

If the library started the HTTP server (during provisioning or based on `http_post_prov_mode`), you can get the handle:

```c
httpd_handle_t server = wifi_cfg_get_httpd();
if (server) {
    // Register your own endpoints
    httpd_uri_t my_endpoint = {
        .uri = "/api/my-data",
        .method = HTTP_GET,
        .handler = my_data_handler,
    };
    httpd_register_uri_handler(server, &my_endpoint);
}
```

## Passing an Existing Server

If your application already runs an HTTP server, pass it in the config so the library registers its handlers on your server instead of creating its own:

```c
httpd_handle_t my_server = start_my_webserver();

wifi_cfg_init(&(wifi_cfg_config_t){
    .http = {
        .httpd = my_server,  // Use existing server
        .api_base_path = "/api/wifi",
    },
    // ...
});
```

When you pass an existing server:
- The library registers its API routes on your server
- On `wifi_cfg_deinit()`, the library unregisters its routes but does **not** stop the server
- Your server and its other routes remain active

## Post-Provisioning HTTP Behavior

The `http_post_prov_mode` field controls what happens to the HTTP server after provisioning stops:

| Mode | Behavior |
|---|---|
| `WIFI_HTTP_FULL` | Keep the full HTTP server running (Web UI + API) |
| `WIFI_HTTP_API_ONLY` | Keep only REST API endpoints, remove Web UI and captive portal routes |
| `WIFI_HTTP_DISABLED` | Stop the HTTP server entirely |

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .enable_ap = true,

    // After provisioning, keep REST API but drop the Web UI
    .http_post_prov_mode = WIFI_HTTP_API_ONLY,
});
```

## Stopping the HTTP Server

You can manually stop the library-owned HTTP server:

```c
esp_err_t err = wifi_cfg_stop_http();
```

This only works if the library owns the server (i.e., you did not pass an existing `httpd_handle_t`) and provisioning is not currently active.

## Authentication

If you want to protect the WiFi configuration endpoints:

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .http = {
        .api_base_path = "/api/wifi",
        .enable_auth = true,
        .auth_username = "admin",
        .auth_password = "secret",
    },
});
```

When enabled, all `/api/wifi/*` endpoints require HTTP Basic Auth. Your own custom endpoints are not affected.
