# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).


## [Unreleased]

### Breaking Changes

- **The `esp_bus` dependency is removed; events move to `esp_event`.**
  The library now publishes its events on ESP-IDF's default event loop
  under a new base, `WIFI_CFG_EVENT` — the same loop it already consumed
  `WIFI_EVENT` and `IP_EVENT` from, and which it creates itself in
  `wifi_cfg_init()`. Register with `esp_event_handler_register()`, using
  `ESP_EVENT_ANY_ID` for a catch-all; handlers take the standard
  esp_event signature. Each `WIFI_CFG_EVT_<NAME>` string macro becomes a
  `WIFI_CFG_EVENT_<NAME>` enumerator of the new `wifi_cfg_event_t`.
  `WIFI_EVT()`, `WIFI_REQ()`, `WIFI_MODULE` and every `WIFI_ACTION_*`
  macro were removed — the bus's action surface was a string-dispatch
  shim over the public C API, so call the matching `wifi_cfg_*` function
  directly. See MIGRATION.md.

  To catch events emitted during startup, call
  `esp_event_loop_create_default()` and register before
  `wifi_cfg_init()`; creating the loop twice is harmless.

  Delivery stays asynchronous, so handlers do not run on the WiFi state
  machine's task or on the httpd task. One failure mode is fixed:
  `esp_bus_emit()` enqueued with a zero timeout and all seventeen call
  sites ignored the error, so a full queue dropped events silently. The
  library still posts with a zero timeout — blocking the state machine
  would be worse — but now checks the result and logs a warning naming
  the event.

  Measured on esp32s3 / IDF 5.5.3: **~5.9 KB of flash** saved across
  every example configuration (`basic` 855,120 → 849,232 bytes) and
  about **5.4 KB of heap**, since `esp_bus` ran a 4 KB-stack task and a
  16 × 64-byte queue while `esp_event`'s loop already exists and is
  already paid for. Static RAM is unchanged. The library also drops from
  one third-party dependency to none.

- **Every `wifi_cfg_config_t` initialiser must now start from
  `WIFI_CFG_DEFAULTS`.** The new `WIFI_CFG_DEFAULTS` (a
  designated-initialiser list) and `WIFI_CFG_DEFAULT_CONFIG()` (the same
  thing as a complete struct value) carry every documented default, so
  the defaults exist as a *value* rather than as init-time patching.
  Passing `NULL` to `wifi_cfg_init()` uses them unmodified.
- **`wifi_cfg_init()` no longer patches unset fields, and rejects a
  zero backoff.** It used to rewrite `max_retry_per_network`,
  `retry_interval_ms` and `retry_max_interval_ms` when they were zero.
  Patching is the only way to express "unset" without an initialiser,
  and it works for a scalar whose zero is nonsensical while failing for
  everything else — a `bool` has no spare value, so `auto_reconnect`
  could not be defaulted at all. Zero now means zero;
  `retry_interval_ms == 0` or `retry_max_interval_ms == 0` returns
  `ESP_ERR_INVALID_ARG` (`retry_interval_ms << retry` is the backoff, so
  a zero base retries with no delay), and `max_retry_per_network == 0`
  logs a warning.
- **`wifi_provisioning_mode_t` and `wifi_reconnect_exhausted_action_t`
  renumbered.** Both had a `[DISABLED]` member on zero, so an omitted
  field selected the one value that does nothing. The working value is
  now zero: `WIFI_PROV_ON_FAILURE` (0), `WIFI_PROV_WHEN_UNPROVISIONED`
  (1), `WIFI_PROV_MANUAL` (2), `WIFI_PROV_ALWAYS` (3); and
  `WIFI_ON_RECONNECT_EXHAUSTED_RESTART` (0),
  `WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` (1). Source using the
  enumerator names just needs a recompile. Anything that **stored or
  transmitted the numeric value** (NVS blobs, config files, custom
  protocomm/MQTT payloads) must be versioned or migrated. The library's
  own `esp-wifi-config-network-policy` endpoint reports the mode as a
  string, so stock provisioning clients are unaffected.
- **`wifi_cfg_config_t` layout changed.** Source-compatible for
  designated initialisers — which is what every example and doc uses —
  but dependents must be rebuilt against the new header (automatic for
  an IDF component build).
- **`WIFI_CFG_DEFAULT_AP_SSID` / `_PASSWORD` / `_IP` moved to the public
  header.** `WIFI_CFG_DEFAULTS` expands in the caller's translation
  unit, so everything it names has to be reachable from
  `esp_wifi_config.h`. Applications can now compare against them
  instead of retyping the literals.

### Security

- **A ~109-byte HTTP request could reboot the device, and ~409 bytes
  could wedge it until physically reset.** `read_json_body()` bounded
  the request body's *length* but not its JSON *nesting depth*, and
  cJSON parses by recursive descent — two characters buy one level, so
  a body far inside the 2048-byte limit reached depths that overflowed
  the HTTP task's 4 KB stack. Measured on an ESP32-S3 with IDF 5.5.4:
  depth 40 answered normally, depth 50 rebooted, depth 200 stopped the
  device answering on *any* channel. Basic Auth is off by default and
  the provisioning SoftAP is open by default, so this was reachable by
  anyone in radio range of a device being set up. Bodies nested deeper
  than `WIFI_CFG_JSON_MAX_DEPTH` (16) are now refused with `400` before
  cJSON sees them. No evidence was found of anything beyond a crash,
  and no investigation of exploitability beyond that was done.

### Fixed

- **Improv BLE dropped its own scan response when the neighbourhood packed
  flush with the buffer.** An RPC result is `[command][length][payload]` and
  the length is one byte, so `GET_WIFI_NETWORKS` packed up to 255 payload
  bytes — but `send_rpc_result()` assembled it in a 256-byte buffer, which is
  two of header plus room for 254, and silently returned when it did not fit.
  Measured on a bench with thirteen APs: the device scanned, packed and
  checksummed an answer, and the Improv BLE client waited out its whole
  30-second timeout with nothing to show. It is not a random landing either —
  the total is a function of which SSIDs are on the air, so it stays wrong for
  as long as the neighbourhood does, which is why it read as flakiness. The
  buffer is now sized to the format's own ceiling and the guard logs when it
  refuses.

  Alongside it, the packer is now told what the transport can carry, as
  `max_payload` on the internal RPC entry point. This library asks for an ATT
  MTU of 517 and gets it from desktop and phone clients, so on a normal link
  the format's 255-byte ceiling is reached long first — but a client that
  settles on a smaller MTU carries less than a result can express, and a
  response packed past it is clipped to fit by the host stack and arrives
  claiming bytes that never came. NimBLE reads the negotiated value with
  `ble_att_mtu()`; Bluedroid was not tracking it at all and now does.

  Networks past the budget are still dropped and always will be — no MTU
  changes a one-byte length field. `wifi_cfg_scan()` returns strongest-first
  and deduplicates by SSID, so the cut falls on the faintest neighbours.
  Improv Serial is unaffected: it answers one response per network, which
  cannot overflow. Three silent failure paths in the BLE transport now log.

- **Improv Serial's `driver` dependency is now declared unconditionally.**
  `CMakeLists.txt` appended `driver` to `PRIV_REQUIRES` inside an
  `if(CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL)` block, which never reaches
  ESP-IDF's early dependency-resolution pass — so `driver/uart.h` was never on
  the include path. It compiled anyway because `esp_bus` declared
  `REQUIRES driver` publicly and this component depended on `esp_bus`, so the
  include path arrived transitively. Removing `esp_bus` removed the accident
  and every `improv_serial` build broke. The bug is older than the removal;
  it was simply unobservable. Found by the HIL matrix build (all three boards
  × both IDF series) during pre-merge validation. Costs nothing where Improv
  Serial is off — `examples/basic` measures 0xcf550 bytes either way — since
  the linker drops the unreferenced archive.


- **Improv BLE never checked the RPC command checksum.** The Improv
  packet is `[command, length, ...data, checksum]` in both directions,
  and the checksum is the only integrity check the protocol has above
  the link layer. The library appended a correct one to every result —
  its own comment cites the spec requiring it — while the command path
  handed the raw GATT write straight to `wifi_cfg_improv_handle_rpc()`,
  which validates the length field and nothing else. A corrupted
  `SEND_WIFI_SETTINGS` was therefore executed: the mangled SSID and
  passphrase were written to NVS and connected to.
  `improv_ble_frame_valid()` now checks length and checksum in
  `improv_ble_cmd_task()`, which is the one path both NimBLE and
  Bluedroid reach the core through, and a bad frame answers
  `IMPROV_ERROR_INVALID_RPC`. **Note for client authors:** the length
  check is exact, so a frame that omits the trailing checksum byte is
  now rejected rather than accepted with its last payload byte read as
  the checksum. Conforming clients always send it; one that had been
  getting away without it will start seeing `INVALID_RPC`.
- **`esp-wifi-config-network-info` was unreachable over BLE
  provisioning.** It was registered but never created:
  `wifi_prov_mgr_endpoint_register()` attaches a handler to a name,
  `wifi_prov_mgr_endpoint_create()` allocates the GATT characteristic,
  and only four of the five built-in endpoints got the second call. The
  handler was live with nothing able to reach it — including the
  `network-info` command in this repo's own `tools/wifi_ble_cli`.
  Endpoint discovery on a device returned nine characteristic names and
  that one was not among them. Nothing logged, because registering a
  handler succeeds whether or not a characteristic exists to route to
  it; only the create can tell. Both calls now go through checked
  wrappers that log the failure and name the endpoint.
- **The bundled Web UI was compiled out of every build that used it**,
  so `/` returned 404 — and that is where all eight captive-portal
  detection handlers redirect. A device would come up, advertise its
  SoftAP, hijack DNS and return a correct 302 to the OS, which then
  opened the portal and found nothing there.
  `CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH` is a Kconfig `string` with
  `default ""`, so it is *always defined* and the `#ifndef` guarding
  the embedded asset table was always false. CMake reached the opposite
  conclusion — an empty string is falsy there — and embedded the assets
  anyway, so they shipped and nothing could reach them. CMake now
  defines `WIFI_CFG_WEBUI_EMBEDDED` when it populates `EMBED_FILES`,
  and the C guards test that. No Kconfig change, no flash cost, and
  builds that set a real custom path are unaffected.
- **Every HTTP error response tore down the connection.**
  `send_error()` returned `ESP_FAIL` after successfully sending its
  response, and `esp_http_server` closes the socket whenever a handler
  returns non-OK. Any client that collected a few 4xx responses — a Web
  UI polling an endpoint that 404s, say — burned one connection per
  error against a pool of seven. It now returns `ESP_OK`, because the
  response *was* sent.
- **The 8th concurrent connection hung instead of being refused.**
  `wifi_cfg_http_init()` left `lru_purge_enable` at its default of
  false, so once `max_open_sockets` (7) were in use, further
  connections were accepted at the TCP layer and never answered — the
  client waited out its own timeout while the device looked healthy. A
  browser alone opens up to six connections to one origin, so the Web
  UI plus an OS captive-portal probe was already at the limit. LRU
  purging is now enabled.
- **An oversized request body left the connection unusable.**
  `read_json_body()` refused bodies over `WIFI_CFG_HTTP_MAX_CONTENT`
  without reading them, so the unread bytes were parsed as the *next*
  request and the connection was reset — meaning the intended `400`
  reached the client only about half the time, and a keep-alive
  client's following request never did. The body is now drained before
  the refusal.
- **A request body split across TCP segments was truncated.**
  `httpd_req_recv()` returns what a single read produced;
  `read_json_body()` assumed it returned everything. It now loops until
  `content_len` bytes have arrived.
- **`auto_reconnect` now actually defaults to true**, via
  `WIFI_CFG_DEFAULTS`. `wifi_cfg_init()` applied defaults for
  `max_retry_per_network`, `retry_interval_ms` and
  `retry_max_interval_ms` only, and never touched `auto_reconnect` —
  a `bool` has no spare value to mean "unset". Any application that
  zero-initialised its config — the documented "everything omitted takes
  the library default" style — silently got `auto_reconnect == false`,
  and the whole reconnect block at `esp_wifi_config.c` was gated on it.
  Measured on an ESP32-S3: after a post-connect disconnect (beacon
  timeout when the AP goes away) the device emitted one
  `wifi:disconnected` event and never retried — no `wifi:connecting`,
  ever. `auto_reconnect` stays a plain `bool`: start from the macro and
  `.auto_reconnect = false` means false.
- **`wifi_cfg_init(NULL)` finally means "all defaults".** The header
  claimed this for a long time and did not deliver, because the defaults
  did not exist as a value anywhere. `NULL` now copies
  `WIFI_CFG_DEFAULT_CONFIG()` verbatim.
- **Partial `default_ap` configs now get the per-field AP defaults the
  header documents.** `set_default_ap_config()` ran only when there was
  no AP config at all, so supplying `.default_ap = { .ssid = "X" }`
  (or loading a legacy NVS blob) left `ip`, `netmask`, `gateway`,
  `max_connections` and the DHCP range blank. A new
  `normalize_ap_config()` backfills every documented default after the
  AP config is resolved, whichever source it came from. `password` is
  left alone — empty means "open network", a real choice.
- **The auto-reconnect backoff no longer blocks the manager task.** The
  reconnect branch ended in `vTaskDelay(pdMS_TO_TICKS(delay))`, so the
  task that owns the reconnect state machine slept through the entire
  backoff and could not service its own queue — with the default
  5 s → 60 s schedule that is up to a minute of deafness per retry, and
  the queue is 10 deep with `xQueueSend(..., 0)`, so events arriving in
  that window were silently dropped. Measured on an ESP32-S3 with a 30 s
  backoff: the retry fired at 29.99 s regardless of what happened in
  between. The delay is now a *deadline* — the task's existing
  `xQueueReceive()` uses whatever is left of it as its timeout, so the
  backoff is a maximum that a relevant message can cut short. Cancelled
  by `STA_CONNECTED`, `GOT_IP`, a disconnect request, and any call to
  `wifi_cfg_start_connect_sequence()`; deliberately *not* by scan
  completion or soft-AP events, which say nothing about the upstream
  network and would let an HTTP client collapse the backoff into a
  busy-loop. The interval schedule (`retry_interval_ms << retry`, capped
  at `retry_max_interval_ms`) is unchanged. Side effects: an explicit
  `wifi_cfg_connect()` during a backoff is no longer undone by the retry
  that was already scheduled, a `wifi_cfg_disconnect()` during a backoff
  is no longer reversed, and `wifi_cfg_deinit()` no longer has to wait
  out the backoff before the task observes `WM_INT_EVT_STOP`.

### Changed

- **`wifi_cfg_init()` warns when a `[DISABLED]` provisioning enum value
  is selected.** `WIFI_PROV_ALWAYS` and
  `WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` still behave as
  `WIFI_PROV_MANUAL` and "retry indefinitely" respectively. They are no
  longer the zero value, so reaching them takes a deliberate choice
  rather than an omission — but the warning stays, because the choice
  does not do what its name suggests. The `on_reconnect_exhausted`
  warning also flags the live side effect: with `enable_ap` set and
  `max_reconnect_attempts > 0`, that value makes `wifi_cfg_stop_http()`
  return `ESP_ERR_INVALID_STATE`.

### Documentation

- Every documented default now lives in one place, `WIFI_CFG_DEFAULTS`,
  so the header's field docs and the values init actually applies can no
  longer drift apart.
- `MIGRATION.md` gained a **0.2.0** section covering the macros, the
  removal of init-time patching, and the enum renumbering (with the
  old→new numeric tables).
- Every example and every documented config sample now starts from
  `WIFI_CFG_DEFAULTS` and shows only its non-default settings.
- Every field of `wifi_cfg_config_t` now states its default (or states
  that it has none) in the header, including the previously silent
  `stop_provisioning_on_connect`, `provisioning_teardown_delay_ms`,
  `http_post_prov_mode`, `always_use_ap_defaults` and `enable_ap`.
- `prov_ble.security2_username` no longer claims a `"wificfg"` default.
  The username never reaches `wifi_prov_mgr` from the device side; the
  field is application metadata and no default is substituted.
- `wifi_cfg_ap_config_t.dhcp_start` / `.dhcp_end` now note that they are
  stored, defaulted and reported over the API but are **not** programmed
  into the AP netif's DHCP server.
- `improv.serial_uart_num` / `.serial_baud_rate` now point at the
  Kconfig options that actually supply their defaults, and note that
  because `0` means "unset", `UART_NUM_0` cannot be selected in
  preference to a non-zero Kconfig value.


## [0.1.0] — 2026-05-31 - ESP-IDF Network Provisioning over BLE

### Breaking Changes

- **Custom BLE GATT service removed.** The hand-rolled JSON-over-GATT
  service (UUID `0xFFE0` / characteristics `0xFFE1`–`0xFFE3`) and the
  `CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE` Kconfig option are gone. ESP-IDF's
  official `wifi_provisioning` manager (BLE scheme) is now the
  recommended secure BLE provisioning path. See `MIGRATION.md` for the
  protocol-level migration plan and the steps for updating downstream
  client tools.
- **Minimum ESP-IDF bumped to 5.4.** `idf_component.yml`'s
  `idf.version` is now `>=5.4`. Prior versions are not supported.
- **Network Provisioning and Improv BLE are mutually exclusive at
  Kconfig level.** Both want to own the BLE GAP advertising and the
  NimBLE/Bluedroid host. Pick one per firmware build. Improv Serial is
  independent and remains safe alongside Network Provisioning.
- **`wifi_cfg_ble_config_t` removed; `.ble.device_name` moved to
  `.improv.ble_device_name`.** With the custom 0xFFE0 service gone the
  field only applied to the Improv BLE host bootstrap, but its
  top-level placement implied it controlled BLE for Network
  Provisioning too (it never did — `wifi_prov_mgr` owns its own GAP
  name). Move it under `.improv` where it actually applies, alongside
  `.improv.device_name` (which is the post-connect Device-Info RPC
  value, not the GAP advertised name).

### Added

- `esp_wifi_config_prov_ble.c` — wraps `wifi_prov_mgr_*` with the BLE
  scheme, integrates with the library's lifecycle, and registers four
  custom protocomm endpoints (`esp-wifi-config-version`,
  `esp-wifi-config-capabilities`, `esp-wifi-config-vars`,
  `esp-wifi-config-network-policy`).
- `wifi_cfg_prov_config_t` carries the full runtime configuration:
  `device_name`, `security` version, `pop`, `security2_username`,
  Security 2 salt/verifier, `reset_on_failure`, `max_failed_attempts`,
  `firmware_version`, and the rest of the BLE / lifecycle / endpoint
  knobs. Only two Kconfig options remain:
  `WIFI_CFG_ENABLE_NETWORK_PROVISIONING` (gates the code) and
  `WIFI_CFG_NETWORK_PROVISIONING_BLE` (selects the BLE transport).
- ESP-IDF 6.x compatibility shim — uses the in-tree
  `wifi_provisioning` component on 5.4 and the external
  `espressif/network_provisioning` managed component on 6.x via a
  conditional `idf_component.yml` rule.
- `esp-wifi-config-network-info` protocomm endpoint — reports the
  device's current network state (SSID, IP, connection status) to the
  provisioning client.
- **Automatic reboot after successful BLE provisioning** (default on).
  Espressif's `wifi_provisioning` component has no clean way to tear
  down and rebuild the BLE/NimBLE stack in place after credentials are
  delivered, so the library now reboots on whichever happens first:
  the BLE client disconnecting after `WIFI_PROV_EVT_CRED_RECV`, or a
  backstop timer set on `WIFI_PROV_EVT_CRED_SUCCESS` (default 15000 ms,
  tunable via the new `wifi_cfg_prov_config_t.reboot_max_wait_ms`).
  Opt out with `wifi_cfg_prov_config_t.disable_reboot_on_provisioning_success
  = true` if the application handles the BLE/Wi-Fi handoff itself.
  `prov_ble.stop_after_success` is now ignored while reboot-on-success
  is active. See `MIGRATION.md` for migration guidance.

### Changed

- `examples/with_ble` rewritten to demonstrate Network Provisioning.
- `examples/with_ble_deinit` switched to Improv BLE (the host-stack
  handoff pattern doesn't fit `wifi_prov_mgr` cleanly).
- BLE backends (`esp_wifi_config_ble_nimble.c` /
  `esp_wifi_config_ble_bluedroid.c`) slimmed to the Improv host
  bootstrap only — they no longer carry the `0xFFE0` service.
- **`WIFI_PROV_ALWAYS` and `WIFI_ON_RECONNECT_EXHAUSTED_PROVISION`
  disabled.** Both code paths called `wifi_prov_mgr_start_provisioning()`,
  which in turn calls `nimble_port_init()` — a fatal collision when the
  application has already initialised the BLE stack. The enum values
  remain in the public API (existing configs still compile) but the
  underlying provisioning-start is bypassed with a warning log.
  `WIFI_PROV_ALWAYS` now behaves like `WIFI_PROV_MANUAL` at boot;
  `WIFI_ON_RECONNECT_EXHAUSTED_PROVISION` now keeps retrying
  indefinitely (equivalent to `max_reconnect_attempts = 0`). The
  original code is preserved behind `#if 0` for re-enablement once a
  BLE provisioning path that doesn't depend on Espressif's
  `wifi_provisioning` component is in place. See `MIGRATION.md`.

### Fixed

- **BLE provisioning recovers from client disconnects.** Worked around
  an IDF 5.5.3 NimBLE bug where only the first BLE client to connect
  after boot could complete a provisioning session — subsequent
  reconnects accepted at the link layer then timed out at supervision,
  and the wedged state only cleared on a full reboot. The library now
  subscribes to `PROTOCOMM_TRANSPORT_BLE_DISCONNECTED` and tears
  down + re-initialises the provisioning manager whenever a client
  drops before credentials are delivered. Opt out with the new
  `wifi_cfg_prov_config_t.disable_disconnect_restart = true` if you
  prefer to debug the underlying IDF bug or drive teardown yourself.
- **NimBLE flow-control regression handled.** Added error handling for
  the ESP-IDF 5.5.3 NimBLE flow-control regression encountered during
  provisioning sessions.


## [0.0.4] — 2026-04-26 - BLE KConfig Updates

### Changed

- Added dependencies on the necessary NimBLE configuration options being set when using BLE support


## [0.0.3] — 2026-04-25 - BLE Interface Segregation

### Breaking Changes

- **Renamed `WIFI_CFG_ENABLE_BLE` to `WIFI_CFG_ENABLE_CUSTOM_BLE`** — The Kconfig option now explicitly refers to the custom BLE GATT service (UUID 0xFFE0). Update `sdkconfig.defaults` files accordingly.
- **Removed `ble.enable` from `wifi_cfg_ble_config_t`** — BLE interfaces are now enabled entirely at compile time via Kconfig. Remove `.ble.enable = true` from your `wifi_cfg_config_t` initializer. The `.ble.device_name` field remains.
- **Improv BLE no longer selects custom BLE** — Enabling `WIFI_CFG_ENABLE_IMPROV_BLE` no longer implicitly enables `WIFI_CFG_ENABLE_CUSTOM_BLE`. Each BLE interface is independently configurable. The BLE stack is initialized automatically when either is enabled.

### Changed

- Decoupled the custom BLE GATT interface from Improv WiFi BLE at the Kconfig level, allowing each to be enabled independently.


## [0.0.2] — First release as esp_wifi_config

This is the first release since hard-forking from [tuanpmt/esp_wifi_manager](https://github.com/tuanpmt/esp_wifi_manager). It includes a full rename of the library, numerous bug fixes, significant BLE improvements, and new provisioning capabilities. See the [Migration Guide](MIGRATION.md) for upgrading from esp_wifi_manager.

### Breaking Changes

- **Renamed from `esp_wifi_manager` to `esp_wifi_config`.** All public symbols have been renamed:
  - Functions: `wifi_manager_` → `wifi_cfg_`
  - Types: `wifi_mgr_` → `wifi_cfg_` (config struct is now `wifi_cfg_config_t`)
  - Events: `WIFI_MGR_EVT_` → `WIFI_CFG_EVT_`
  - Kconfig: `WIFI_MGR_` → `WIFI_CFG_`
  - Header: `esp_wifi_manager.h` → `esp_wifi_config.h`
- **`wifi_cfg_deinit()` now takes a `bool deinit_wifi` parameter.** Pass `true` for the old behavior (full teardown), or `false` to tear down the manager while keeping WiFi connected — useful after provisioning completes.
- **Built-in mDNS integration removed.** If you need mDNS, initialize it directly in your application after receiving `WIFI_CFG_EVT_GOT_IP`. See the Migration Guide for details.
- **Provisioning booleans replaced with a unified provisioning mode system.** The separate boolean flags for enabling provisioning methods have been consolidated.
- **Several Kconfig options removed** in favor of runtime config struct fields: `AP_SSID`, `AP_PASSWORD`, `AP_IP`, and `BLE_DEVICE_NAME` are now set via `wifi_cfg_config_t` rather than menuconfig.
- **`esp_bus` dependency** has moved from `tuanpmt/esp_bus` to `thorrak/esp_bus`.

### New Features

- **Improv WiFi support.** Full implementation of the [Improv WiFi](https://www.improv-wifi.com/) standard, with both BLE and Serial transports. Includes a new `with_improv` example.
- **NimBLE Bluetooth stack support.** Choose between Bluedroid and NimBLE for BLE provisioning, enabling use on memory-constrained devices.
- **BLE interface at feature parity with SoftAP.** Added `update_network`, `list_vars`, and `del_var` BLE commands, plus missing status fields.
- **BLE response chunking.** Large BLE responses are now automatically chunked, with an increased SSID buffer size for reliability.
- **BLE command queue.** Commands are now processed via a dedicated task and queue rather than inline in the BLE callback, improving stability.
- **BLE service advertisement.** Service UUID 0xFFE0 is now advertised in BLE broadcasts for easier device discovery.
- **Network upsert.** "Add network" commands now update existing entries instead of failing if the network already exists.
- **Continued BLE use after deinitialization.** BLE can remain active after calling `wifi_cfg_deinit(false)`, with a new `with_ble_deinit` example demonstrating the pattern.
- **Pre-request hook and variable validation** for HTTP API endpoints, giving applications more control over provisioning requests.
- **Option to ignore NVS AP config** and always use the default AP settings provided at init time.
- **HTTP handler registration improvements** for provisioning and API management, including proper URI deregistration when using a shared HTTP server.
- **Mock HTTP test server** (`tools/test_server/`) — a Flask-based server that emulates the esp_wifi_config API for frontend development without hardware.

### Bug Fixes

- **Defer `WIFI_STATE_CONNECTED` until `GOT_IP`** — previously the connected state was emitted before an IP address was assigned, causing race conditions in application code.
- **Fix double AP start/stop events** — AP lifecycle events are now emitted exactly once.
- **Fix reconnect logic** — stale disconnect events from previous connections no longer trigger spurious reconnect attempts. Connected SSID is now properly tracked.
- **Hide hidden networks** from WiFi scan results.
- **Fix AP config loading** to properly prioritize provided defaults over stale NVS data.
- **Fix Web UI initialization order** — fallback page registration now happens at the correct time.
- **Fix custom WebUI builds** — embedded files are no longer included when a custom WebUI path is configured.
- **Fix `set_var` callback** — the "variable changed" callback now fires correctly when setting variables programmatically.
- **Fix gzipped asset serving** — file serving logic now correctly prefers gzipped assets.

### Infrastructure & Documentation

- **Documentation site** at [configwifi.com](https://configwifi.com) built with Docusaurus, including AI-friendly `llms.txt`.
- **GitHub Actions CI** — automated builds for all examples on every push.
- **ESP Component Registry** publishing via GitHub Actions on release.
- **PlatformIO Library Registry** support with `library.json`.
- **Migration Guide** (`MIGRATION.md`) documenting all breaking changes with find-and-replace instructions.
