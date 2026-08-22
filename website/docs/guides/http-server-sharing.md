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
// Bring up the TCP/IP stack yourself here. Everywhere else the library does
// it for you inside wifi_cfg_init() — but starting a server opens a socket,
// and that happens before wifi_cfg_init() is reached.
ESP_ERROR_CHECK(esp_netif_init());

httpd_handle_t my_server = start_my_webserver();

wifi_cfg_init(&(wifi_cfg_config_t){
    WIFI_CFG_DEFAULTS,
    .http = {
        .httpd = my_server,  // Use existing server
        // api_base_path and auth_username/password come from the macro;
        // naming .http here replaces the whole sub-struct, and the library
        // falls back to "/api/wifi" and admin/admin for the blanks.
    },
    // ...
});
```

:::warning Call `esp_netif_init()` first

This is the one documented flow where your application runs network code
*before* `wifi_cfg_init()`, so it is the one place the library cannot
initialise lwIP in time. `wifi_cfg_init()` calls `esp_netif_init()` itself and
tolerates having been beaten to it, so calling it early is safe and calling it
twice is harmless.

Skip it and the device aborts inside lwIP the moment your server opens its
socket:

```
assert failed: tcpip_send_msg_wait_sem ... (Invalid mbox)
```

Nothing in that names netif, or this library, or the line that caused it —
which is why it is called out here rather than left to the reader.

:::

Two more things become yours in this flow, both silent when wrong:

- **`max_uri_handlers` must leave room for the library's routes.**
  `HTTPD_DEFAULT_CONFIG()` allows 8; the library registers about 22 between the
  REST API, the Web UI and the captive-portal probes. It does not check each
  registration, so an undersized server gives you an API that answers some
  paths and 404s the rest, with nothing in the log.
- **`uri_match_fn` must be `httpd_uri_match_wildcard`** for the Web UI's
  catch-all route to match. The default matcher compares URIs exactly, so the
  captive portal answers only its handful of literal paths and the phone's
  "sign in to network" sheet opens blank.

The library sets both on the server it creates itself; on yours, it cannot.
[`examples/with_shared_httpd`](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_shared_httpd)
is this whole flow as a buildable project.

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
    WIFI_CFG_DEFAULTS,
    .stop_provisioning_on_connect = true,
    .enable_ap = true,

    // After provisioning, keep REST API but drop the Web UI
    // (WIFI_CFG_DEFAULTS sets WIFI_HTTP_FULL)
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
- The reconnect constraint applies: `enable_ap = true` AND `on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` AND `max_reconnect_attempts > 0`. The SoftAP might need to restart later after a post-connect disconnect, and it requires the HTTP server alive. _This guard is currently dormant because_ `WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` _is itself disabled — it will reactivate cleanly if that path is re-enabled._

## Automatic HTTPD Teardown

When `http_post_prov_mode = WIFI_HTTP_DISABLED`, the library also tries to fully stop the HTTPD server (not just deregister handlers) — but only when it's safe. The decision depends on three factors: who owns the server, whether provisioning might restart, and whether the SoftAP might need to come back up after a post-connect disconnect.

| Provisioning mode | Library owns HTTPD | Shared HTTPD (you passed `.http.httpd`) |
|---|---|---|
| `WIFI_PROV_WHEN_UNPROVISIONED` | Auto-teardown after transition* | Deregister handlers only — your server stays running |
| `WIFI_PROV_MANUAL` | Keep server alive; you call `wifi_cfg_stop_http()` explicitly* | Deregister handlers only |
| `WIFI_PROV_ON_FAILURE` | Keep server alive (provisioning may restart) | Deregister handlers only |
| `WIFI_PROV_ALWAYS` | Currently disabled — behaves like `WIFI_PROV_MANUAL` (see [Provisioning Modes](../provisioning/modes.md#modes)) | Deregister handlers only |

\* **Reconnect constraint**: If `enable_ap = true` AND `on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` AND `max_reconnect_attempts > 0`, auto-teardown would be suppressed even in `WHEN_UNPROVISIONED`/`MANUAL` mode. This guard is **currently dormant** because `WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` is itself disabled — auto-teardown runs normally regardless of the exhaustion-action config.

The rules are conservative — the library never tears down a server it can't bring back up cleanly later.

## Authentication

If you want to protect the WiFi configuration endpoints:

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    WIFI_CFG_DEFAULTS,
    .http = {
        .enable_auth = true,
        .auth_username = "admin",     // the default; shown for clarity
        .auth_password = "secret",
    },
});
```

When enabled, all `/api/wifi/*` endpoints require HTTP Basic Auth. Your own custom endpoints are not affected.
