# Sharing an HTTP server the application owns

Every other example here lets the library start the HTTP server. This one
starts its own, registers a route on it, and hands it to the library through
`.http.httpd` so both sets of routes live on one server and one port.

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

Then, from the device's IP:

| Route | Owner |
|---|---|
| `GET /hello` | this example |
| `GET /api/wifi/status` | the library |

## Why this example exists

It is the only flow in the repository where the application touches the
network *before* `wifi_cfg_init()` runs, and that makes three things the
application's responsibility that are otherwise the library's. All three are
commented in `main/main.c`, marked `(1)` `(2)` `(3)`:

1. **`esp_netif_init()` must be called first.** `wifi_cfg_init()` calls it too
   and tolerates having been beaten to it — but starting a server opens a
   socket, and that happens first. Without it the device aborts inside lwIP
   with `assert failed: tcpip_send_msg_wait_sem ... (Invalid mbox)`, which
   names neither netif nor this library.
2. **`max_uri_handlers` must leave room for the library's routes.**
   `HTTPD_DEFAULT_CONFIG()` allows 8; the library registers about 22. The
   overflow is silent, so the symptom is an API that answers some paths and
   404s the rest.
3. **`uri_match_fn` must be `httpd_uri_match_wildcard`** for the Web UI's
   `"/*"` catch-all to match, or the captive portal opens on a blank page.

None of that is exotic, but none of it is guessable either, and until this
example existed no build in CI exercised the configuration — every other
example leaves `.http.httpd` unset. A trap that only fires in the one
configuration nothing compiles is a trap that stays.

See also: [Sharing the HTTP Server](https://configwifi.com/docs/guides/http-server-sharing).
