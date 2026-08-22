# Migrating off esp_bus — quick reference

For applications that subscribe to this library's events. The change is
mechanical; budget ten minutes per project.

Full detail in [MIGRATION.md](MIGRATION.md). This page is the cheat sheet.

---

## 1. Drop the dependency

```diff
 dependencies:
   thorrak/esp_wifi_config: "*"
-  thorrak/esp_bus: "^1.0.3"
```

Keep it only if *your own* code uses the bus for something else — it is no
longer pulled in transitively.

## 2. Fix up app_main

```diff
-#include "esp_bus.h"
-
 void app_main(void)
 {
     nvs_flash_init();
-    esp_bus_init();
+    ESP_ERROR_CHECK(esp_event_loop_create_default());   // only if you register
+                                                        // before wifi_cfg_init()
```

`wifi_cfg_init()` creates the loop itself. Create it first only when you want
to catch events emitted *during* init; creating it twice is harmless.

## 3. Rewrite handlers and subscriptions

```diff
-static void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
+static void on_got_ip(void *arg, esp_event_base_t base, int32_t event_id, void *data)
 {
     ...
 }

-esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);
+esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP, on_got_ip, NULL);
```

Branching on the event name becomes a switch on the id:

```diff
-if (strcmp(event, WIFI_EVT(WIFI_CFG_EVT_CONNECTED)) == 0) {
+if (event_id == WIFI_CFG_EVENT_CONNECTED) {
```

Wildcards: `esp_bus_sub("wifi:*", ...)` → `ESP_EVENT_ANY_ID`.
`wifi_cfg_event_name(event_id)` gives you a string for logging.

## 4. Rename the constants

Every `WIFI_CFG_EVT_<NAME>` string macro is now a `WIFI_CFG_EVENT_<NAME>`
enumerator. The names correspond one to one, so find-and-replace
`WIFI_CFG_EVT_` → `WIFI_CFG_EVENT_` does it.

Removed with no replacement: `WIFI_EVT()`, `WIFI_REQ()`, `WIFI_MODULE`, and
every `WIFI_ACTION_*`. Bus actions were a string-dispatch shim over the public
API — call the function directly:

| Was | Now |
|---|---|
| `esp_bus_req(WIFI_REQ(WIFI_ACTION_GET_STATUS), ...)` | `wifi_cfg_get_status(&status)` |
| `WIFI_ACTION_CONNECT` | `wifi_cfg_connect(ssid)` |
| `WIFI_ACTION_SCAN` | `wifi_cfg_scan(...)` |
| `WIFI_ACTION_ADD_NETWORK` | `wifi_cfg_add_network(...)` |
| …and so on | the matching `wifi_cfg_*` |

## 5. Check your payload casts

The old events guide documented four payload types **that do not exist**. If
you copied from it, these casts were wrong:

| Event | Docs used to say | Actually |
|---|---|---|
| `GOT_IP` | ~~`wifi_got_ip_t`~~ | `esp_netif_ip_info_t` |
| `SCAN_DONE` | ~~`wifi_scan_done_t`~~ | `uint16_t` (AP count) |
| `NETWORK_ADDED` / `_UPDATED` | ~~`wifi_network_changed_t`~~ | `wifi_network_t` |
| `VAR_CHANGED` | ~~`wifi_var_changed_t`~~ | `wifi_var_t` |

Unchanged: `CONNECTED` → `wifi_connected_t`, `DISCONNECTED` →
`wifi_disconnected_t`, `PROV_CRED_RECV` → `wifi_cfg_prov_creds_t`,
`PROV_CRED_FAIL` → `int`. `CONNECTING` and `NETWORK_REMOVED` carry a
NUL-terminated SSID; `AP_STA_CONNECTED` carries 6 raw MAC bytes. The rest carry
nothing.

The full table lives on `wifi_cfg_event_t` in `esp_wifi_config.h`.

---

## Two behavioural notes

**No payload length.** Handlers get `data` but no `len` — the event id fixes
the type, as with `WIFI_EVENT` and `IP_EVENT`. If you had
`if (data && len >= sizeof(T))` guards, they become NULL checks.

**Handlers run on the system event loop task**, not on the emitting task and
not on a bus task of their own. They share that loop with `WIFI_EVENT` and
`IP_EVENT` handlers, so keep them short — blocking there delays IDF's own
networking callbacks. Do the heavy work on your own task.

## Worked example

"Do something when WiFi connects", before and after:

```c
/* before */
static void on_connected(const char *event, const void *data, size_t len, void *ctx) {
    const wifi_connected_t *info = data;
    ESP_LOGI(TAG, "up on %s", info->ssid);
}
esp_bus_init();
esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_connected, NULL);

/* after */
static void on_connected(void *arg, esp_event_base_t base, int32_t id, void *data) {
    const wifi_connected_t *info = data;
    ESP_LOGI(TAG, "up on %s", info->ssid);
}
ESP_ERROR_CHECK(esp_event_loop_create_default());
esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_CONNECTED, on_connected, NULL);
```

If you want the old asynchronous bus behaviour back — because you route events
between your own modules — keep `esp_bus` as your own dependency and bridge in
four lines:

```c
static void to_bus(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_bus_emit("wifi", wifi_cfg_event_name(id), data, 0);
}
esp_event_handler_register(WIFI_CFG_EVENT, ESP_EVENT_ANY_ID, to_bus, NULL);
```
