# Migration Guide

This document tracks breaking changes that affect downstream firmware
using `esp_wifi_config`. The first section covers the most recent changes
(the `esp_bus` dependency removed, `WIFI_CFG_DEFAULTS`, and the enum
renumbering); then 0.1.0 (custom BLE replaced by ESP-IDF Network
Provisioning); the rest covers the historic rename from
`esp_wifi_manager`.

Note that the event constants were renamed twice. Anything below that
names `WIFI_CFG_EVT_*` describes an older migration; those names no
longer exist. See "The `esp_bus` dependency is gone" for the current
`WIFI_CFG_EVENT_*` enum.

---

## 0.2.0 — `WIFI_CFG_DEFAULTS`, and two enums renumbered

### The `esp_bus` dependency is gone

`esp_wifi_config` no longer depends on `esp_bus`. Events now go to ESP-IDF's
**default event loop** under a new event base, `WIFI_CFG_EVENT` — the same
loop the library already used for `WIFI_EVENT` and `IP_EVENT`, and which it
creates itself in `wifi_cfg_init()`. The bus's request/action surface is
simply the library's own C API, which every action already forwarded to.

Remove the dependency from `idf_component.yml`, drop
`#include "esp_bus.h"` and the `esp_bus_init()` call, and re-register your
handlers:

```diff
-#include "esp_bus.h"
-
-esp_bus_init();
-esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), on_got_ip, NULL);
+ESP_ERROR_CHECK(esp_event_loop_create_default());
+esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP, on_got_ip, NULL);
```

Handlers take the standard esp_event signature:

```diff
-void on_got_ip(const char *event, const void *data, size_t len, void *ctx)
+void on_got_ip(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
```

Code that branched on the event string becomes a switch on `event_id`:

```diff
-if (strcmp(event, WIFI_EVT(WIFI_CFG_EVT_CONNECTED)) == 0) {
+if (event_id == WIFI_CFG_EVENT_CONNECTED) {
```

Every `WIFI_CFG_EVT_<NAME>` string macro is now a `WIFI_CFG_EVENT_<NAME>`
enumerator of `wifi_cfg_event_t` — the names match, so the rename is
mechanical. A wildcard subscription becomes `ESP_EVENT_ANY_ID`.
`WIFI_EVT()`, `WIFI_REQ()`, `WIFI_MODULE` and every `WIFI_ACTION_*` macro
were removed with no replacement: call the matching public function directly
(`WIFI_ACTION_GET_STATUS` → `wifi_cfg_get_status()`, `WIFI_ACTION_CONNECT` →
`wifi_cfg_connect()`, and so on).

**Register before init to catch startup events.** `esp_event_handler_register()`
requires the default loop to exist. `wifi_cfg_init()` creates it, so either call
`esp_event_loop_create_default()` yourself first (creating it twice is
harmless — the library tolerates `ESP_ERR_INVALID_STATE`), or register after
`wifi_cfg_init()` and accept missing what it emitted.

Two behavioural notes:

- **Delivery is still asynchronous**, as it was under `esp_bus` — handlers run
  on the system event loop task, not on the task that emitted the event. A slow
  handler will not stall the WiFi state machine or consume the httpd task's
  stack. It does share the loop with `WIFI_EVENT` and `IP_EVENT` handlers, so
  keep handlers short.
- **A dropped event is now logged.** `esp_bus_emit()` enqueued with a zero
  timeout and every call site in the library discarded its return, so a full
  queue lost events silently. The library still posts with a zero timeout —
  blocking the state machine would be worse — but it checks the result and logs
  a warning naming the event.

If your application used `esp_bus` for its own modules, it still works — it is
just no longer pulled in as a transitive dependency, so declare it in your own
`idf_component.yml`. Bridging library events onto the bus is four lines:

```c
static void to_bus(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_bus_emit("wifi", wifi_cfg_event_name(id), data, 0);
}
esp_event_handler_register(WIFI_CFG_EVENT, ESP_EVENT_ANY_ID, to_bus, NULL);
```

### The defaults are a value now

`WIFI_CFG_DEFAULTS` is a designated-initialiser list carrying every
documented default; `WIFI_CFG_DEFAULT_CONFIG()` is the same thing as a
complete struct value. Both live in `esp_wifi_config.h` and expand in
your translation unit, so the constants they name
(`CONFIG_WIFI_CFG_DEFAULT_RETRY`, `WIFI_CFG_DEFAULT_AP_SSID`, …) are now
public too.

```c
// Compound-literal style (matches the examples):
wifi_cfg_init(&(wifi_cfg_config_t){
    WIFI_CFG_DEFAULTS,
    .enable_ap = true,
});

// Struct-value style, when you want to compute fields:
wifi_cfg_config_t cfg = WIFI_CFG_DEFAULT_CONFIG();
cfg.enable_ap = true;
cfg.auto_reconnect = false;   // means false, because you started here
wifi_cfg_init(&cfg);
```

### `wifi_cfg_init()` no longer patches unset fields

It used to rewrite `max_retry_per_network`, `retry_interval_ms` and
`retry_max_interval_ms` when they were zero. Patching is the only way to
express "unset" without an initialiser, and it works for a scalar whose
zero is nonsensical while failing for everything else — a `bool` has no
spare value, so `auto_reconnect` could not be defaulted at all and a
zero-initialised config silently never reconnected.

Now zero means zero, and two fields where zero is unsafe are **rejected**:

| Condition | Result |
|-----------|--------|
| `retry_interval_ms == 0` | `ESP_ERR_INVALID_ARG` from `wifi_cfg_init()` |
| `retry_max_interval_ms == 0` | `ESP_ERR_INVALID_ARG` from `wifi_cfg_init()` |
| `max_retry_per_network == 0` | Boots, but logs a warning — no connection is ever attempted |

`retry_interval_ms << retry` is the backoff, so a zero base retries with
no delay at all. Refusing beats both silently patching (what was removed)
and spinning.

**Symptom if you miss this:** `wifi_cfg_init()` returns
`ESP_ERR_INVALID_ARG` and logs a message naming the two fields. That is
the loud failure mode; the quiet one is `auto_reconnect` coming out
`false` on a config that never mentioned it.

### `wifi_cfg_init(NULL)` finally means "all defaults"

The header claimed this for a long time and did not deliver, because the
defaults did not exist as a value anywhere. `NULL` now copies
`WIFI_CFG_DEFAULT_CONFIG()` verbatim: retry policy, `auto_reconnect =
true`, `WIFI_PROV_ON_FAILURE`, `WIFI_ON_RECONNECT_EXHAUSTED_RESTART`,
`WIFI_HTTP_FULL`, and the full `default_ap` / `http` / `improv` /
`prov_ble` default blocks. Any doc or comment you have that says
otherwise predates this release.

### Two enums renumbered — check anything that stores the number

This is the part most likely to bite. Both enums had a `[DISABLED]`
member sitting on zero, so omitting the field selected the one value that
does nothing. They were moved off zero, which **changes the numeric value
of every member**.

`wifi_provisioning_mode_t`:

| Name | Old value | New value |
|------|-----------|-----------|
| `WIFI_PROV_ON_FAILURE` | 1 | **0** (default) |
| `WIFI_PROV_WHEN_UNPROVISIONED` | 2 | **1** |
| `WIFI_PROV_MANUAL` | 3 | **2** |
| `WIFI_PROV_ALWAYS` | 0 | **3** ([DISABLED]) |

`wifi_reconnect_exhausted_action_t`:

| Name | Old value | New value |
|------|-----------|-----------|
| `WIFI_ON_RECONNECT_EXHAUSTED_RESTART` | 1 | **0** (default) |
| `WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` | 0 | **1** ([DISABLED]) |

**Source code that uses the names needs no change** — recompile and the
new numbers follow. What breaks is anything that persisted or transported
the *number*:

- NVS blobs, EEPROM records or config files in which your application
  stored the raw enum value. A stored `0` used to mean `WIFI_PROV_ALWAYS`
  and now means `WIFI_PROV_ON_FAILURE`; a stored `1` used to mean
  `WIFI_PROV_ON_FAILURE` and now means `WIFI_PROV_WHEN_UNPROVISIONED`.
  Version your record, or migrate it on read.
- Custom protocomm endpoints, MQTT payloads, or REST fields of your own
  that carry the integer over the wire to a client built against the old
  header. Both sides must be updated together.
- Any `switch` on the integer literal rather than the enumerator.

The library's own surfaces are safe: the built-in
`esp-wifi-config-network-policy` protocomm endpoint reports
`provisioning_mode` as a **string** (`"on_failure"`, …), not a number, so
existing BLE provisioning clients are unaffected. Nothing in the library
persists either enum to NVS.

Both `[DISABLED]` values still behave as they did in 0.1.0
(`WIFI_PROV_ALWAYS` → like `WIFI_PROV_MANUAL`;
`WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` → retry indefinitely), and
`wifi_cfg_init()` still logs a warning if you select one. Reaching them
now takes a deliberate choice rather than an omission.

### `disable_auto_reconnect` is gone

If you tracked `main` between 0.1.0 and 0.2.0 you may have picked up a
`disable_auto_reconnect` field, added as a workaround for the
"`bool` has no spare value" problem. `WIFI_CFG_DEFAULTS` solves that
properly, so the field was **removed before release**. `auto_reconnect` is
a plain `bool` again, defaulted `true` by the macro:

```diff
 wifi_cfg_init(&(wifi_cfg_config_t){
+    WIFI_CFG_DEFAULTS,
-    .disable_auto_reconnect = true,
+    .auto_reconnect = false,
 });
```

Released 0.1.0 never had the field, so upgrades from 0.1.0 are unaffected.

### AP default constants are public

`WIFI_CFG_DEFAULT_AP_SSID` (`"ESP32-Config"`),
`WIFI_CFG_DEFAULT_AP_PASSWORD` (`""`, an open network) and
`WIFI_CFG_DEFAULT_AP_IP` (`"192.168.4.1"`) moved from the private header
to `esp_wifi_config.h`. `WIFI_CFG_DEFAULTS` needs them, and an
application comparing against one should not have to retype the literal.

### Nested sub-structs still replace wholesale

A designated initialiser for a nested struct replaces the **whole**
sub-struct, so this blanks the other `default_ap` fields:

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    WIFI_CFG_DEFAULTS,
    .default_ap = { .ssid = "MyDevice" },   // ip, netmask, dhcp_* now blank
});
```

`wifi_cfg_init()` backfills the per-field AP defaults for exactly this
reason, so the above is safe — but if you want to be certain, set the
members individually on a `WIFI_CFG_DEFAULT_CONFIG()` value
(`cfg.default_ap.ssid = …`). The same wholesale-replacement rule applies
to `.http`, `.improv` and `.prov_ble`; each of those has its own
init-time fallbacks for the fields `WIFI_CFG_DEFAULTS` would have set.

### Migration checklist

1. Add `WIFI_CFG_DEFAULTS,` as the first initialiser in every
   `wifi_cfg_config_t` you build.
2. Delete the lines that only restated a default —
   `.max_retry_per_network = 3`, `.retry_interval_ms = 5000`,
   `.auto_reconnect = true`, `.provisioning_mode = WIFI_PROV_ON_FAILURE`,
   `.http.api_base_path = "/api/wifi"`, and the `default_ap` fields other
   than a custom `ssid`.
3. Replace any `.disable_auto_reconnect = true` with
   `.auto_reconnect = false`.
4. Audit anything that stored or transmitted the **numeric** value of
   `wifi_provisioning_mode_t` or `wifi_reconnect_exhausted_action_t`.
5. Rebuild. The struct layout changed, so dependents must be recompiled
   against the new header (automatic for an IDF component build).

---

## 0.1.0 — Custom BLE replaced by ESP-IDF Network Provisioning

The hand-rolled JSON-over-GATT BLE service (UUID `0xFFE0` / characteristics
`0xFFE1`–`0xFFE3`) has been **removed**. ESP-IDF's official
`wifi_provisioning` manager (BLE scheme) is now the recommended secure
provisioning path.

### Why

- The custom service duplicated functionality that ESP-IDF already
  provides (BLE transport, framing, encryption, retry).
- It had no formal protocol audit; Espressif's `wifi_provisioning` is
  audited and is the supported path for IDF 6.x.
- Maintaining a parallel BLE GATT service alongside Improv was forcing
  fragile workarounds (single-app GATTS callback for Bluedroid, manual
  GATT db reset for NimBLE).

### What changed (Kconfig)

Only two Network Provisioning Kconfig symbols exist now — everything
else (security version, PoP, device name template, SRP6a username,
auto-reset behaviour, retry threshold) is plain runtime configuration on
`wifi_cfg_prov_config_t`:

| Old | New |
|-----|-----|
| `CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=y` | `CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y` |
| `CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=n` (Improv only) | unchanged — `CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE=y` |
| _none_ | `CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE=y` (BLE transport) |

`CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING` and
`CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE` are now **mutually exclusive** — both
want to own the BLE GAP advertising and the NimBLE/Bluedroid host. Pick
the one that matches your provisioning client tooling.

#### Provisioning Kconfig menu

The pre-0.1.0 single switch (originally `WIFI_MGR_ENABLE_BLE`, then
`CONFIG_WIFI_CFG_ENABLE_BLE`, then `CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE`)
gated the now-removed custom 0xFFE0 BLE service. 0.1.0 keeps the
BLE-provisioning surface as three independent Kconfig options under the
**Provisioning** menu:

| Option | What it enables |
|--------|-----------------|
| `CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING` | ESP-IDF `wifi_prov_mgr` over BLE — the replacement for the custom 0xFFE0 service. |
| `CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE` | Improv BLE transport (Web Bluetooth / ESPHome companion). Mutually exclusive with Network Provisioning (one BLE host). |
| `CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL` | Improv Serial transport over UART. Independent of either BLE option, but mutually exclusive with `CONFIG_WIFI_CFG_ENABLE_CLI` (one console UART). |

If your existing sdkconfig already carries
`# CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE is not set` /
`# CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL is not set` lines, those are
the same Improv options that existed in 0.0.x — Kconfig just lists
them beside the new Network Provisioning switch now rather than under
a combined "BLE" menu.

### What changed (`wifi_cfg_config_t` shape)

The struct gained a `.prov_ble` sub-block that carries the full runtime
configuration for ESP-IDF Network Provisioning. The full shape:

```c
typedef struct {
    // BLE identity / discovery
    const char    *device_name;          // supports {id} token; NULL → "PROV_{id}"
    const uint8_t *service_uuid128;      // optional 128-bit GATT UUID
    const uint8_t *manufacturer_data;
    size_t         manufacturer_data_len;
    const uint8_t *random_addr;          // optional 6-byte BLE address

    // Security
    wifi_cfg_prov_security_t  security;  // _DEFAULT → Security 1
    const char    *pop;                  // NULL/"" → no PoP
    const char    *security2_username;   // metadata only; no default substituted
    const uint8_t *security2_salt;
    size_t         security2_salt_len;
    const uint8_t *security2_verifier;
    size_t         security2_verifier_len;

    // BLE lifecycle
    wifi_cfg_prov_memory_policy_t memory_policy;  // FREE_BTDM/FREE_BLE/FREE_BT/KEEP_ALL
    bool           keep_ble_on_after_stop;
    bool           disable_disconnect_restart;  // opt out of the NimBLE workaround

    // Provisioning lifecycle
    uint32_t       cleanup_delay_ms;     // protocomm grace period; 0 → 1000 ms
    uint32_t       wifi_conn_attempts;   // 0 = infinite
    bool           stop_after_success;
    bool           disable_reboot_on_provisioning_success;  // default false = reboot on
    uint32_t       reboot_max_wait_ms;   // 0 → 15000 ms
    bool           reset_on_failure;     // erase prov state after N failed creds
    uint8_t        max_failed_attempts;  // 0 → 3 (used when reset_on_failure)

    // App metadata
    const char    *firmware_version;
    const wifi_cfg_prov_app_info_t *app_infos;
    size_t         app_info_count;

    // Custom protocomm endpoints
    const wifi_cfg_prov_custom_endpoint_t *custom_endpoints;
    size_t         custom_endpoint_count;

    // Event callbacks (also delivered to event subscribers)
    wifi_cfg_prov_on_creds_recv_t    on_credentials_received;
    wifi_cfg_prov_on_creds_fail_t    on_credentials_failed;
    wifi_cfg_prov_on_creds_success_t on_credentials_success;
    void          *event_ctx;
} wifi_cfg_prov_config_t;
```

The old `wifi_cfg_ble_config_t` and its top-level `.ble` field are
**gone**. Its only remaining purpose was to set the Improv BLE GAP
advertised name, so the field moved to `.improv.ble_device_name`:

```diff
 wifi_cfg_init(&(wifi_cfg_config_t){
     WIFI_CFG_DEFAULTS,
-    .ble = {
-        .device_name = "MyDevice-{id}",
-    },
+    .improv = {
+        .ble_device_name = "MyDevice-{id}",
+    },
 });
```

If you set `.ble.device_name` while building a Network Provisioning BLE
firmware (Improv disabled), the field was being silently ignored —
`wifi_prov_mgr` derives its GAP name from `wifi_cfg_prov_config_t.device_name`.
Use `.prov_ble.device_name` instead. Unlike the old prefix-with-MAC-tail
behaviour, `.prov_ble.device_name` is a full name template that supports the
`{id}` token, matching the rest of the library:

```c
.prov_ble = {
    .device_name = "MyDevice-{id}",  // becomes "MyDevice-ABC123"
}
```

### What changed (runtime API)

The public `wifi_cfg_init()` config keeps its existing fields. The new
`.prov_ble` block carries every runtime parameter for Network Provisioning:

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    WIFI_CFG_DEFAULTS,
    .stop_provisioning_on_connect = true,

    .prov_ble = {
        .device_name        = "MyApp_{id}",         // GAP name template
        .security           = WIFI_CFG_PROV_SECURITY_1,
        .pop                = "1234abcd",           // Security 1 PoP
        .wifi_conn_attempts = 5,                    // give up after 5 STA tries
        .reset_on_failure   = true,                 // accept retries without reboot
        .firmware_version   = "1.0.0",
        .on_credentials_received = on_creds,
    },
});
```

`provisioning_mode` comes from `WIFI_CFG_DEFAULTS` as
`WIFI_PROV_ON_FAILURE`, and `memory_policy` / `max_failed_attempts` /
`device_name` are omitted above because their defaults
(`WIFI_CFG_PROV_MEM_FREE_BTDM`, `3`, `"PROV_{id}"`) are what most apps
want.

The lifecycle (`provisioning_mode`, `stop_provisioning_on_connect`,
`provisioning_teardown_delay_ms`, retry/backoff, multi-network store,
custom variables, captive portal, post-provisioning HTTP behaviour) is
unchanged — `wifi_prov_mgr` is driven by the same state machine.

#### Device-name template

`.prov_ble.device_name` is a **template**, not a prefix. The old
`.ble.device_name` semantics ("string + MAC suffix") changed to "explicit
`{id}` token" — matching `.improv.ble_device_name`. If you were relying
on the old prefix `"PROV_"` to auto-append MAC bytes, set the field to
`"PROV_{id}"` (the library default). A literal `"PROV_"` with no `{id}`
will now advertise as `"PROV_"` with no per-device suffix.

#### Provisioning teardown lifecycle

The library now calls `wifi_prov_mgr_disable_auto_stop()` unconditionally
so that the library lifecycle (`stop_provisioning_on_connect` +
`provisioning_teardown_delay_ms`) is the sole driver of when the
manager tears down. The new `.prov_ble.cleanup_delay_ms` controls only the
protocomm grace window between the library's stop request and protocomm
shutdown (default 1000 ms, minimum 100 ms enforced by ESP-IDF).

If your existing code subscribes to `WIFI_PROV_EVT_END` directly, note
that END now fires only after the library has called stop — not
automatically after `CRED_SUCCESS` as in stock wifi_prov_mgr.

### Disabled provisioning-mode and exhaustion-action values

Two `wifi_cfg_config_t` enum values are now no-ops. Both remain in the
public API (so existing initialisers still compile), but the underlying
provisioning-start path is bypassed and a warning is logged at runtime.
(In 0.1.0 these were both the *zero* value of their enum, so omitting the
field selected them; 0.2.0 moved them off zero — see the 0.2.0 section
above.)

| Field | Value | New behavior | Equivalent to |
|---|---|---|---|
| `.provisioning_mode` | `WIFI_PROV_ALWAYS` | Connect only — provisioning never auto-starts at boot | `WIFI_PROV_MANUAL` |
| `.on_reconnect_exhausted` | `WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` | Reset the counter and keep retrying with normal backoff | `max_reconnect_attempts = 0` |

**Why**: both code paths call `wifi_cfg_start_provisioning()`, which
eventually calls `wifi_prov_mgr_start_provisioning()` inside the IDF
`wifi_provisioning` component, which in turn calls `nimble_port_init()`.
If the application has already initialised the BLE stack — either
before `wifi_cfg_init()` runs or as part of its own runtime — that call
fails and the device ends up unable to re-enter provisioning. The
disabled paths are preserved behind `#if 0` in `src/esp_wifi_config.c`
and may be re-enabled if/when the library grows its own BLE
provisioning transport that doesn't depend on Espressif's
`wifi_provisioning` component.

**What to do**: if your existing config used either value, switch to a
supported one:

```diff
 wifi_cfg_init(&(wifi_cfg_config_t){
     WIFI_CFG_DEFAULTS,
-    .provisioning_mode      = WIFI_PROV_ALWAYS,
+    .provisioning_mode      = WIFI_PROV_MANUAL,
+    // or the WIFI_PROV_ON_FAILURE default / WIFI_PROV_WHEN_UNPROVISIONED
+    // — none of these auto-start provisioning at boot when a network is saved
     ...
-    .on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_PROVISION,
+    // WIFI_CFG_DEFAULTS already sets WIFI_ON_RECONNECT_EXHAUSTED_RESTART
+    // — or leave .max_reconnect_attempts = 0 to retry forever
 });
```

### Reboot on successful provisioning (default on)

The device now reboots automatically after a successful provisioning
session — once the BLE client has delivered credentials and (typically)
seen the device come online, the library forces an `esp_restart()`.

**Why**: Espressif's `wifi_provisioning` component does not expose a
clean way to tear down and rebuild the BLE/NimBLE stack in place. After
provisioning ends, the only fully reliable post-prov state is a cold
boot. Rebooting once at the end avoids a class of latent
BLE-handoff bugs (stale GATT db, supervision-timeout loops, controller
state divergence) and gives the application a deterministic starting
point.

**Trigger**: whichever fires first —

1. The BLE client disconnecting after `WIFI_PROV_EVT_CRED_RECV` (the
   well-behaved-client path), or
2. A backstop timer set on `WIFI_PROV_EVT_CRED_SUCCESS`, default 15000
   ms (catches clients that drop without a clean disconnect).

**Config** — two new fields on `wifi_cfg_prov_config_t`:

```c
.prov_ble = {
    // Default: reboot enabled (false → reboot on).
    // Set true ONLY if the application handles BLE/Wi-Fi handoff itself.
    .disable_reboot_on_provisioning_success = false,

    // Backstop wait between CRED_SUCCESS and forced reboot, in ms.
    // 0 → 15000 ms. Ignored when reboot is disabled above.
    .reboot_max_wait_ms = 15000,
}
```

The backstop must comfortably exceed the client's status-poll interval
(Espressif's ESPProvision SDK polls every ~5 s) plus the time to
associate and get an IP. A 3 s backstop reproduced a false-failure report
intermittently: the device rebooted between two client polls, after it
was actually connected but before the client had observed "connected".

The field uses **negative polarity** to match the existing
`.prov_ble.disable_disconnect_restart` convention — both are
default-on workarounds for `wifi_prov_mgr`/NimBLE limitations, so a
`false` value gives the safer behavior.

**Behavior change from earlier 0.1.0 builds**: previous builds left
the manager running after credentials were accepted and relied on
the library-level `stop_provisioning_on_connect` /
`provisioning_teardown_delay_ms` lifecycle to tear it down in place.
That path still exists, but `disable_reboot_on_provisioning_success
= false` (the default) bypasses it — the device reboots before the
in-place teardown would run. `.prov_ble.stop_after_success` is
therefore ignored while reboot-on-success is active.

**What to do**:

- **Most apps**: nothing. The new default does the right thing.
- **Apps that need to keep BLE up post-provisioning** (e.g. an Improv
  flow that morphs into a BLE companion link, or an app-owned GATT
  service taking over): set
  `.prov_ble.disable_reboot_on_provisioning_success = true` and handle
  the post-prov BLE state yourself. You are accepting the
  `wifi_prov_mgr` teardown sharp edges as a trade-off.
- **Apps doing significant work on `WIFI_CFG_EVT_PROV_CRED_SUCCESS`**:
  audit that work — the reboot will fire shortly after (≤ 15 s by
  default, or sooner on client disconnect). Persist anything important
  to NVS before returning from the callback, or extend
  `.prov_ble.reboot_max_wait_ms` if you need a wider window.

### Provisioning event surface

Two parallel notification paths, both fire for every event — use
whichever suits the app. The struct callbacks are scoped to
provisioning; the subscription API sees every library event.

(Names below are shown as of 0.2.0. In 0.1.0 these were `esp_bus`
strings named `WIFI_CFG_EVT_*`, subscribed with `esp_bus_sub`.)

```c
// Event ids on the WIFI_CFG_EVENT base (esp_event_handler_register)
WIFI_CFG_EVENT_PROVISIONING_STARTED   // no data
WIFI_CFG_EVENT_PROVISIONING_STOPPED   // no data
WIFI_CFG_EVENT_PROV_CRED_RECV         // data: wifi_cfg_prov_creds_t
WIFI_CFG_EVENT_PROV_CRED_FAIL         // data: int (wifi_prov_sta_fail_reason_t)
WIFI_CFG_EVENT_PROV_CRED_SUCCESS      // no data

// Struct callbacks (wired through .prov_ble in wifi_cfg_init)
typedef void (*wifi_cfg_prov_on_creds_recv_t)(const wifi_cfg_prov_creds_t *creds, void *ctx);
typedef void (*wifi_cfg_prov_on_creds_fail_t)(int reason, void *ctx);
typedef void (*wifi_cfg_prov_on_creds_success_t)(void *ctx);

typedef struct {
    char ssid[33];      // NUL-terminated
    char password[64];
} wifi_cfg_prov_creds_t;
```

The `CRED_FAIL` reason is `WIFI_PROV_STA_AUTH_ERROR` (1) for bad
password and `WIFI_PROV_STA_AP_NOT_FOUND` (0) for SSID-not-visible — the
two cases an app typically needs to distinguish to drive a "retry" vs
"re-scan" UI. After failure the manager remains running and accepts
fresh credentials without rebooting; if you set `.prov_ble.reset_on_failure
= true`, the state machine is reset after `.prov_ble.max_failed_attempts`
(default 3) consecutive failures.

### Init-time validation (new failure mode)

`wifi_cfg_init()` now runs `wifi_cfg_prov_validate()` against the
`.prov_ble` block and returns an error early instead of failing later at
provisioning time. The validator currently enforces:

| Condition | Behavior | Return |
|-----------|----------|--------|
| Security 2 selected (runtime or Kconfig) but `security2_salt` / `_verifier` missing | Hard fail at init | `ESP_ERR_INVALID_ARG` |
| `manufacturer_data_len > 25` (won't fit BLE scan response) | Hard fail at init | `ESP_ERR_INVALID_ARG` |

The Security 2 case is a **behavior change**: 0.0.4's predecessor
silently fell back to Security 1 with PoP when the SRP6a parameters
were missing. The new code refuses to boot so the misconfiguration is
visible during development. To derive the salt/verifier offline:

```bash
python $IDF_PATH/tools/esp_prov/esp_prov.py \
    --transport ble --service_name dummy \
    --sec_ver 2 --sec2_gen_cred --sec2_usr <username> --sec2_pwd <password>
```

The tool prints a `salt[]` and `verifier[]` C array snippet you copy
into firmware. (The `--service_name` and `--transport` flags are
required by the tool but irrelevant for the offline derivation path.)
Then wire them into the prov config:

```c
extern const uint8_t sec2_salt[];     // from the helper output
extern const uint8_t sec2_verifier[];

.prov_ble = {
    .security              = WIFI_CFG_PROV_SECURITY_2,
    .security2_salt        = sec2_salt,
    .security2_salt_len    = sizeof(sec2_salt),
    .security2_verifier    = sec2_verifier,
    .security2_verifier_len= sizeof(sec2_verifier),
},
```

### Two field semantics worth flagging

**`.prov_ble.security == 0` means "library default", not "Security 0".**
`WIFI_CFG_PROV_SECURITY_DEFAULT` is the zero value and resolves to
Security 1, so omitting the field in a designated initialiser gives you
Security 1. The explicit security-zero option is `WIFI_CFG_PROV_SECURITY_0`
(= 1).

**`.prov_ble.security2_username` is informational only.** The SRP6a
username flows from the *client* during the handshake; the device
never reads this field. It's kept so the app can echo the expected
username through its own UI (custom endpoint, captive portal, etc.)
and so firmware reads as self-documenting. The actual binding between
username and on-device verifier was established offline when the
salt/verifier were derived.

### Bluetooth memory policy

The library wraps wifi_prov_mgr's BLE memory-cleanup event handler in a
`.prov_ble.memory_policy` enum. Pick based on what the app needs from
Bluetooth **after** provisioning ends:

| App keeps using… | Set `memory_policy` to | Result |
|---|---|---|
| Nothing (BLE-only provisioning, no post-prov BT) | `WIFI_CFG_PROV_MEM_FREE_BTDM` (default) | Releases ~80 KB of BT + BLE memory |
| Classic BT (A2DP, SPP, HFP, …) | `WIFI_CFG_PROV_MEM_FREE_BLE` | Frees BLE only; Classic BT survives |
| BLE (custom GATT, beacon, scanner) | `WIFI_CFG_PROV_MEM_FREE_BT` | Frees Classic BT only; BLE survives |
| Both (app owns the BT stack already) | `WIFI_CFG_PROV_MEM_KEEP_ALL` | Frees nothing |

The library also auto-detects the "app already brought up Bluetooth"
case (BT controller already enabled at start) and forces `KEEP_ALL`
with a log warning, since freeing memory underneath an active host
would fault.

**Behavior change from 0.0.4**: the old custom BLE service did not free
any BT memory on tear-down — it just stopped advertising. The new
default (`FREE_BTDM`) reclaims ~80 KB but breaks any post-prov BT use.
If your existing firmware calls Classic BT or BLE APIs after WiFi is
provisioned, set `memory_policy` explicitly. Default-zero will crash
inside the controller.

### What changed (BLE protocol)

Client tools that previously connected to UUID `0xFFE0` and exchanged
JSON via the custom characteristics will **no longer work**. The
device now speaks the standard ESP-IDF provisioning protocol over BLE.

Recommended replacement clients:

- **Espressif "ESP BLE Provisioning" mobile app** (iOS / Android) —
  sufficient for most end-user provisioning flows.
- **`esp-idf-provisioning-android` / `esp-idf-provisioning-ios`** SDKs
  if you ship your own mobile app.
- **Espressif `esp-prov` Python tool** (in
  `tools/esp_prov/` of any ESP-IDF checkout) for desktop / CI testing.

The library exposes four custom protocomm endpoints alongside the
standard `prov-config`/`prov-scan`/etc. endpoints, available to all
those clients:

| Endpoint | Direction | Payload |
|----------|-----------|---------|
| `esp-wifi-config-version` | read | `{"lib","idf","app","fw_version","compile_time","firmware_version","chip"}` |
| `esp-wifi-config-capabilities` | read | `{"capabilities":[...],"max_networks":N,"max_vars":M}` |
| `esp-wifi-config-vars` | read/write | request/response schema below |
| `esp-wifi-config-network-policy` | read | `{"provisioning_mode","max_retry_per_network","retry_interval_ms","retry_max_interval_ms","auto_reconnect","max_reconnect_attempts","saved_networks"}` |

The `esp-wifi-config-vars` endpoint speaks a small JSON op/key/value
protocol — every request is a JSON object with an `"op"` field:

```
// list
request:  {"op":"list"}
response: {"vars":[{"k":"key1","v":"value1"},...]}

// get one
request:  {"op":"get","key":"foo"}
response: {"key":"foo","value":"bar"}            // or {"error":"not_found"}

// set / upsert
request:  {"op":"set","key":"foo","value":"bar"}
response: {"ok":true}                            // or {"error":"store_full"|"rejected"}

// delete
request:  {"op":"del","key":"foo"}
response: {"ok":true}                            // or {"error":"not_found"}
```

These four endpoints replace the old `set_var` / `get_var` / `list_vars`
/ `get_status` JSON commands. The old commands for direct WiFi
management (`scan` / `add_network` / `connect` / `start_ap` / etc.) are
not recreated — `wifi_prov_mgr` already covers them via standard
endpoints (`prov-scan`, `prov-config`).

### Custom protocomm endpoints (replacement for 0xFFE1–0xFFE3)

Firmware that exposed app-specific characteristics on the old 0xFFE0
service can now register additional protocomm endpoints alongside the
four built-in ones via `.prov_ble.custom_endpoints`:

```c
static esp_err_t my_cloud_token_handler(uint32_t session_id,
        const uint8_t *inbuf, ssize_t inlen,
        uint8_t **outbuf, ssize_t *outlen, void *user_ctx)
{
    // Read inbuf (raw bytes from client), allocate response with malloc.
    // protocomm frees *outbuf via free() after delivery.
    const char *resp = "{\"ok\":true}";
    *outbuf = (uint8_t *)strdup(resp);
    *outlen = strlen(resp);
    return ESP_OK;
}

static const wifi_cfg_prov_custom_endpoint_t my_endpoints[] = {
    { .name = "my-cloud-token", .handler = my_cloud_token_handler, .user_ctx = NULL },
};

.prov_ble = {
    .custom_endpoints      = my_endpoints,
    .custom_endpoint_count = sizeof(my_endpoints) / sizeof(my_endpoints[0]),
},
```

Endpoint names are published as part of the provisioning protocol; the
ESP-IDF Provisioning mobile SDKs and the `esp-prov` tool address them
by name. The library handles the create-before-start / register-after-start
ordering required by `wifi_prov_mgr` automatically.

### How to migrate downstream firmware

1. **Pick a security version.** The default is Security 1
   (Curve25519 + AES-CTR with a static PoP). Security 2 (SRP6a) is
   supported but requires you to embed pre-computed `salt` / `verifier`
   bytes via `wifi_cfg_prov_config_t` — `wifi_cfg_init()` now refuses to
   boot if Security 2 is selected without them (see "Init-time
   validation" above).

2. **Decide what BLE memory you need after provisioning.** If the app
   uses Bluetooth post-provisioning (Classic BT profiles, custom GATT
   services, BLE scanning), set `.prov_ble.memory_policy` explicitly — the
   default `FREE_BTDM` will fault the controller if BT is touched after
   provisioning ends. See the decision tree under "Bluetooth memory
   policy" above.

3. **Update `sdkconfig.defaults`:**

   ```diff
   - CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=y
   + CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y
   + CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE=y
   ```

   Everything else (security version, PoP, device-name template,
   auto-reset behaviour, etc.) is set on `wifi_cfg_prov_config_t` in
   step 4.

4. **Set runtime parameters** in `wifi_cfg_init()`. See the struct
   shape under "What changed (`wifi_cfg_config_t` shape)" for the full
   list — typical fields: `device_name` (e.g. `"MyApp_{id}"` to keep a
   per-device MAC suffix), `security`, `pop`, `memory_policy`,
   `wifi_conn_attempts`, `reset_on_failure`, `max_failed_attempts`, the
   event callbacks, and `custom_endpoints` for app-specific protocomm
   channels.

5. **Subscribe to the new provisioning events** if your app reacts to
   credential delivery/failure/success — see "Provisioning event
   surface" above. Both library events and struct callbacks fire for
   every event.

6. **Replace any custom BLE client tooling** with one of the standard
   ESP-IDF provisioning clients (ESP BLE Provisioning app,
   `esp-idf-provisioning-android/ios`, or `esp-prov`). If you need to
   keep talking to your existing fleet, pin the old library at `0.0.4`
   until firmware rollout completes.

7. **Rebuild.** The library now requires **ESP-IDF 5.4 or newer**.

### ESP-IDF version support

| IDF | wifi_prov_mgr source | Notes |
|-----|----------------------|-------|
| 5.4+ | In-tree `wifi_provisioning` component | Default path. `CMakeLists.txt` lists `PRIV_REQUIRES wifi_provisioning`. |
| 6.0+ | External `espressif/network_provisioning` managed component | Same shape, renamed types (`wifi_prov_mgr_*` → `network_prov_mgr_*`). `idf_component.yml` pulls it in with an `idf_version >=6.0` rule; a compile-time switch in `esp_wifi_config_prov_ble.c` handles the symbol rename. On IDF 6 you must also swap `PRIV_REQUIRES wifi_provisioning` to `network_provisioning` in `CMakeLists.txt`. |

If you encounter build errors against an early IDF 6.x preview where
the managed component shape diverges from the shim, please file an
issue.

### Build matrix verified

The branch was build-verified against **ESP-IDF 5.4.3** on macOS for the
ESP32-S3 target across these example combinations:

| Example | BLE provisioning | Improv | Result |
|---------|------------------|--------|--------|
| `basic` | none | none | OK (~825 KB app) |
| `with_ble` | Network Provisioning, NimBLE | none | OK (~1.07 MB app) |
| `with_improv` | none | Improv BLE (Bluedroid) + Improv Serial | OK |
| `with_ble_deinit` | none | Improv BLE (NimBLE) | OK |

---

# esp_wifi_manager → esp_wifi_config

This document tracks all breaking changes introduced when the project was renamed from `esp_wifi_manager` (by tuanpmt) to `esp_wifi_config` (by thorrak). Use it as a reference when updating existing code that depended on the original library.


## Component Name

| Old | New |
|-----|-----|
| `esp_wifi_manager` | `esp_wifi_config` |

Update your `idf_component.yml` dependency:

```yaml
# Old
dependencies:
  tuanpmt/esp_wifi_manager: "*"

# New
dependencies:
  thorrak/esp_wifi_config: "*"
```

Update your `CMakeLists.txt` REQUIRES:

```cmake
# Old
REQUIRES esp_wifi_manager nvs_flash

# New
REQUIRES esp_wifi_config nvs_flash
```


## Header File

| Old | New |
|-----|-----|
| `esp_wifi_manager.h` | `esp_wifi_config.h` |

```c
// Old
#include "esp_wifi_manager.h"

// New
#include "esp_wifi_config.h"
```


## Function Prefix

All public functions changed from `wifi_manager_` to `wifi_cfg_`.

| Old | New |
|-----|-----|
| `wifi_manager_init()` | `wifi_cfg_init()` |
| `wifi_manager_deinit()` | `wifi_cfg_deinit()` |
| `wifi_manager_connect()` | `wifi_cfg_connect()` |
| `wifi_manager_disconnect()` | `wifi_cfg_disconnect()` |
| `wifi_manager_add_network()` | `wifi_cfg_add_network()` |
| `wifi_manager_remove_network()` | `wifi_cfg_remove_network()` |
| `wifi_manager_update_network()` | `wifi_cfg_update_network()` |
| `wifi_manager_get_network()` | `wifi_cfg_get_network()` |
| `wifi_manager_list_networks()` | `wifi_cfg_list_networks()` |
| `wifi_manager_start_ap()` | `wifi_cfg_start_ap()` |
| `wifi_manager_stop_ap()` | `wifi_cfg_stop_ap()` |
| `wifi_manager_get_ap_status()` | `wifi_cfg_get_ap_status()` |
| `wifi_manager_set_ap_config()` | `wifi_cfg_set_ap_config()` |
| `wifi_manager_get_ap_config()` | `wifi_cfg_get_ap_config()` |
| `wifi_manager_set_var()` | `wifi_cfg_set_var()` |
| `wifi_manager_get_var()` | `wifi_cfg_get_var()` |
| `wifi_manager_del_var()` | `wifi_cfg_del_var()` |
| `wifi_manager_get_status()` | `wifi_cfg_get_status()` |
| `wifi_manager_wait_connected()` | `wifi_cfg_wait_connected()` |
| `wifi_manager_scan()` | `wifi_cfg_scan()` |
| `wifi_manager_get_httpd()` | `wifi_cfg_get_httpd()` |
| `wifi_manager_stop_http()` | `wifi_cfg_stop_http()` |

General rule: find-and-replace `wifi_manager_` with `wifi_cfg_`.


## Type Prefix

Types that previously used `wifi_mgr_` now use `wifi_cfg_`. The main config struct changed from `wifi_manager_config_t` to `wifi_cfg_config_t` (avoiding collision with ESP-IDF's own `wifi_config_t`).

| Old | New |
|-----|-----|
| `wifi_manager_config_t` | `wifi_cfg_config_t` |
| `wifi_mgr_ap_config_t` | `wifi_cfg_ap_config_t` |
| `wifi_mgr_http_config_t` | `wifi_cfg_http_config_t` |
| `wifi_mgr_ble_config_t` | _removed in 0.1.0; was renamed to `wifi_cfg_ble_config_t`, then folded into `wifi_cfg_improv_config_t.ble_device_name`_ |
| `wifi_mgr_http_hook_t` | `wifi_cfg_http_hook_t` |
| `wifi_mgr_var_validator_t` | `wifi_cfg_var_validator_t` |

General rule: find-and-replace `wifi_mgr_` with `wifi_cfg_` and `wifi_manager_config_t` with `wifi_cfg_config_t`.


## Event Constants

| Old | New |
|-----|-----|
| `WIFI_MGR_EVT_AP_START` | `WIFI_CFG_EVT_AP_START` |
| `WIFI_MGR_EVT_AP_STOP` | `WIFI_CFG_EVT_AP_STOP` |
| `WIFI_MGR_EVT_VAR_CHANGED` | `WIFI_CFG_EVT_VAR_CHANGED` |
| `WIFI_MGR_EVT_PROVISIONING_STARTED` | `WIFI_CFG_EVT_PROVISIONING_STARTED` |
| `WIFI_MGR_EVT_PROVISIONING_STOPPED` | `WIFI_CFG_EVT_PROVISIONING_STOPPED` |

General rule: find-and-replace `WIFI_MGR_EVT_` with `WIFI_CFG_EVT_`.


## Kconfig Options

All menuconfig options changed from `WIFI_MGR_` to `WIFI_CFG_`. If you reference these in `sdkconfig.defaults` or C code via `CONFIG_*`, update them.

| Old | New |
|-----|-----|
| `WIFI_MGR_MAX_NETWORKS` | `WIFI_CFG_MAX_NETWORKS` |
| `WIFI_MGR_MAX_VARS` | `WIFI_CFG_MAX_VARS` |
| `WIFI_MGR_DEFAULT_RETRY` | `WIFI_CFG_DEFAULT_RETRY` |
| `WIFI_MGR_RETRY_INTERVAL_MS` | `WIFI_CFG_RETRY_INTERVAL_MS` |
| `WIFI_MGR_AP_SSID` | Removed — use `wifi_cfg_config_t.default_ap.ssid` |
| `WIFI_MGR_AP_PASSWORD` | Removed — use `wifi_cfg_config_t.default_ap.password` |
| `WIFI_MGR_AP_IP` | Removed — use `wifi_cfg_config_t.default_ap.ip` |
| `WIFI_MGR_MAX_SCAN_RESULTS` | `WIFI_CFG_MAX_SCAN_RESULTS` |
| `WIFI_MGR_HTTP_MAX_CONTENT_LEN` | `WIFI_CFG_HTTP_MAX_CONTENT_LEN` |
| `WIFI_MGR_TASK_STACK_SIZE` | `WIFI_CFG_TASK_STACK_SIZE` |
| `WIFI_MGR_TASK_PRIORITY` | `WIFI_CFG_TASK_PRIORITY` |
| `WIFI_MGR_HTTP_MAX_URI_HANDLERS` | `WIFI_CFG_HTTP_MAX_URI_HANDLERS` |
| `WIFI_MGR_ENABLE_CLI` | `WIFI_CFG_ENABLE_CLI` |
| `WIFI_MGR_ENABLE_WEBUI` | `WIFI_CFG_ENABLE_WEBUI` |
| `WIFI_MGR_WEBUI_CUSTOM_PATH` | `WIFI_CFG_WEBUI_CUSTOM_PATH` |
| `WIFI_MGR_ENABLE_BLE` | _removed in 0.1.0; was `WIFI_CFG_ENABLE_CUSTOM_BLE`; replaced by `WIFI_CFG_ENABLE_NETWORK_PROVISIONING` or `WIFI_CFG_ENABLE_IMPROV_BLE`_ |
| `WIFI_MGR_BLE_DEVICE_NAME` | Removed — use `wifi_cfg_config_t.improv.ble_device_name` (0.1.0+) |

General rule: find-and-replace `WIFI_MGR_` with `WIFI_CFG_` in sdkconfig files and C code.

**Important:** After updating, run `idf.py fullclean` and re-run `idf.py menuconfig` to regenerate your sdkconfig with the new option names. Old `CONFIG_WIFI_MGR_*` entries in an existing sdkconfig will be silently ignored, reverting those settings to their defaults.


## BLE Kconfig Rename and `ble.enable` Removal

| Old | New |
|-----|-----|
| `WIFI_CFG_ENABLE_BLE` | `WIFI_CFG_ENABLE_CUSTOM_BLE` |

Update your `sdkconfig.defaults`:

```kconfig
# Old
CONFIG_WIFI_CFG_ENABLE_BLE=y

# New
CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE=y
```

The `enable` field has been removed from `wifi_cfg_ble_config_t`. BLE is now enabled entirely at compile time via Kconfig. The `device_name` field has since (as of 0.1.0) moved under `wifi_cfg_improv_config_t.ble_device_name`:

```c
// Old (esp_wifi_manager and esp_wifi_config 0.0.x)
.ble = {
    .enable = true,
    .device_name = "ESP32-WiFi-{id}",
},

// New (esp_wifi_config 0.1.0+)
.improv = {
    .ble_device_name = "ESP32-WiFi-{id}",
},
```


## Changed Function Signatures

### `wifi_cfg_deinit()` now requires a `bool deinit_wifi` parameter

*This change was originally proposed in [tuanpmt/esp_wifi_manager#7](https://github.com/tuanpmt/esp_wifi_manager/pull/7) but was never merged upstream. If you are already using that patch, this change will be familiar.*

The old `wifi_manager_deinit(void)` always tore down WiFi and destroyed the network interfaces, which meant calling deinit after provisioning would disconnect the WiFi session you just established. The new signature lets you choose:

```c
// Old
wifi_manager_deinit();  // always stopped WiFi and destroyed netifs

// New
wifi_cfg_deinit(true);  // same as old behavior: stop WiFi, destroy netifs, free resources
wifi_cfg_deinit(false); // tear down the manager (HTTP, BLE, event handlers, task)
                        // but keep WiFi connected and netifs alive
```

When `deinit_wifi` is `false`, the STA and AP network interfaces are preserved. You can reobtain their handles later via `esp_netif_get_handle_from_ifkey()` if needed.

**Typical usage**: after provisioning completes and WiFi is connected, call `wifi_cfg_deinit(false)` to free the manager's resources while keeping your WiFi connection active.


## mDNS Removed

The built-in mDNS integration has been removed. It was a convenience wrapper that initialized mDNS on WiFi connect and advertised an HTTP service — no core library functionality depended on it.

**What to do:**

- Remove the `.mdns` field from your `wifi_cfg_config_t` initializer.
- Remove `WIFI_CFG_MDNS_HOSTNAME` from any `sdkconfig.defaults` files.
- If you need mDNS, initialize it directly in your application after receiving the `WIFI_CFG_EVENT_GOT_IP` event:

```c
#include "mdns.h"

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    mdns_init();
    mdns_hostname_set("my-device");
    mdns_instance_name_set("My Device");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
}
```

You will also need to add `espressif/mdns` to your own project's `idf_component.yml` dependencies if you use mDNS.


## esp_bus Dependency

> **Superseded in 0.2.0**: `esp_wifi_config` no longer depends on
> `esp_bus` at all. See "The `esp_bus` dependency is gone" at the top of
> this document. The note below applies only if your own application
> still uses `esp_bus` directly.

The `esp_bus` component is also now maintained under the new owner.

| Old | New |
|-----|-----|
| `tuanpmt/esp_bus` | `thorrak/esp_bus` |


## Repository URLs

| Old | New |
|-----|-----|
| `github.com/tuanpmt/esp_wifi_manager` | `github.com/thorrak/esp_wifi_config` |


## Quick Migration

For most projects, running these find-and-replace operations (in order) covers everything:

1. `esp_wifi_manager` &rarr; `esp_wifi_config`
2. `wifi_manager_` &rarr; `wifi_cfg_`
3. `wifi_mgr_` &rarr; `wifi_cfg_`
4. `WIFI_MGR_` &rarr; `WIFI_CFG_`
5. `tuanpmt/esp_bus` &rarr; `thorrak/esp_bus`
6. `tuanpmt` &rarr; `thorrak` (in dependency/URL contexts)

Then rebuild with `idf.py fullclean && idf.py build`.
