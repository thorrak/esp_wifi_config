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

This returns `ESP_ERR_INVALID_STATE` and refuses to act if any of the following apply:

- You passed an existing `httpd_handle_t` (the library never tears down a server it doesn't own — it only deregisters its own URI handlers).
- Provisioning is currently active.
- The reconnect constraint applies: `enable_ap = true` AND `on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` AND `max_reconnect_attempts > 0`. The SoftAP might need to restart later after a post-connect disconnect, and it requires the HTTP server alive.

## Automatic HTTPD Teardown

When `http_post_prov_mode = WIFI_HTTP_DISABLED`, the library also tries to fully stop the HTTPD server (not just deregister handlers) — but only when it's safe. The decision depends on three factors: who owns the server, whether provisioning might restart, and whether the SoftAP might need to come back up after a post-connect disconnect.

| Provisioning mode | Library owns HTTPD | Shared HTTPD (you passed `.http.httpd`) |
|---|---|---|
| `WIFI_PROV_WHEN_UNPROVISIONED` | Auto-teardown after transition* | Deregister handlers only — your server stays running |
| `WIFI_PROV_MANUAL` | Keep server alive; you call `wifi_cfg_stop_http()` explicitly* | Deregister handlers only |
| `WIFI_PROV_ALWAYS` / `WIFI_PROV_ON_FAILURE` | Keep server alive (provisioning may restart) | Deregister handlers only |

\* **Reconnect constraint**: If `enable_ap = true` AND `on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` AND `max_reconnect_attempts > 0`, auto-teardown is suppressed even in `WHEN_UNPROVISIONED`/`MANUAL` mode. The SoftAP may need to restart after a post-connect disconnect, and it requires the HTTP server. In that case the library deregisters its handlers but leaves the server running.

The rules are conservative — the library never tears down a server it can't bring back up cleanly later.

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
