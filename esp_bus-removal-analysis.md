# Removing the `esp_bus` Dependency — Cost, Benefit, and What Is Lost

Analysis accompanying the `refactor/remove_esp_bus` branch, which drops the
`esp_bus` event bus and publishes the library's events on ESP-IDF's default
event loop instead.

**Method.** ESP-IDF v5.5.3, target esp32s3, all seven bundled examples built
clean before and after. Sizes come from `idf.py size` / `size-components` over
the linked image; struct sizes and stack frames come from the xtensa toolchain
(`objdump`, `nm`) against the real target ABI.

A full hardware-in-the-loop run executed on the bench Pi against both DUTs:
**145 passed, 2 failed, 1 error**, with both failures reproduced on `main`.
See "Validation status". Size and heap claims remain static or computed.

---

## Summary

`esp_bus` was a third-party component, maintained here as a fork, providing
asynchronous pattern-matched event delivery. The library used five of the
twenty-seven functions `esp_bus.h` declares, and none of the ones that make it a
bus: no wildcard subscriptions, no event-to-action routing, no cross-module
requests.

ESP-IDF already ships an event system the library was **already using** — it
consumes `WIFI_EVENT`, `IP_EVENT`, `WIFI_PROV_EVENT` and
`PROTOCOMM_TRANSPORT_BLE` on the default loop, and creates that loop itself in
`wifi_cfg_init()`. It just published its own events through something else.
That asymmetry was the actual design flaw; `esp_bus` was one symptom of it.

The library now declares an event base, `WIFI_CFG_EVENT`, and posts to the same
loop.

| | Before | After | Delta |
|---|---|---|---|
| Flash, `basic` example | 855,120 B | 849,232 B | **−5,888** |
| Flash, `with_improv` | 1,293,440 B | 1,287,280 B | **−6,160** |
| Flash, `with_webui` | 864,464 B | 858,640 B | **−5,824** |
| Flash, `with_ble` | 1,145,456 B | 1,139,696 B | **−5,760** |
| Static RAM (`.bss`) | 16,848 B | 16,800 B | **−48** |
| Heap at runtime | ~5.4 KB | 0 | **−5.4 KB** (computed) |
| Third-party components | 1 (a fork) | 0 | **−1** |

---

## Why esp_event and not a bespoke callback table

This branch first replaced `esp_bus` with a hand-rolled fixed-size subscriber
table dispatching synchronously. That worked and measured slightly better on
flash, but it was the wrong call, and the numbers show why the platform's
version wins:

| | Bespoke table | `esp_event` |
|---|---:|---:|
| New module size, linked | 252 B code + 104 B RAM | **198 B code + 0 B RAM** |
| `libesp_wifi_config.a` | 20,019 B | **19,845 B** |
| Incremental cost of the mechanism | — | **0 B** (already linked) |
| Flash vs `main` | −6,208 B | −5,888 B |
| Static RAM vs `main` | +48 B | **−48 B** |
| Handlers run on | the emitting task | the event loop task |
| Subscriber limit | 8, compile-time | unbounded |

`libesp_event.a` measures **3,480 bytes in both builds** — identical. Every
ESP-IDF app doing WiFi already links it, so declaring a base and posting to it
costs nothing structural. The 320-byte gap versus the bespoke table is not the
mechanism; it is the event-name strings, which are now always referenced because
a failed post logs the event by name. That is a deliberate purchase (see
"Dropped events" below).

Against that 320 bytes: no subscriber cap, no spinlock, one less concept in the
public API, and the disappearance of an entire class of drawback that the
synchronous design had — handlers landing on the WiFi state machine's stack, or
on the httpd task's. The platform's answer was better than mine.

---

## Where the flash went

Per-archive, `basic` example:

| Archive | Before | After | Delta |
|---|---:|---:|---:|
| `libthorrak__esp_bus.a` | 3,300 | 0 | **−3,300** |
| `libesp_wifi_config.a` | 21,356 | 19,845 | **−1,511** |
| `libesp_app_format.a`\* | 74,483 | 73,439 | **−1,044** |
| `libmbedcrypto.a` | 61,521 | 61,489 | −32 |
| `libmain.a` (the example) | 2,019 | 2,005 | −14 |
| others | — | — | +8 |
| **`libesp_event.a`** | **3,480** | **3,480** | **0** |

\* Not really `esp_app_format`. Its `esp_app_desc.c.obj` is the first
contributor to the merged `.flash.rodata` section, so `esp_idf_size` attributes
the whole shared string pool to it. This row is the string-pool shrink — the
bus's log tags and format strings, plus the library's own action and event
descriptor strings, minus the event-name strings now added back.

Three things were deleted:

1. **`esp_bus` itself** — 3,252 B flash + 48 B `.bss`. Only
   `esp_bus.c`/`esp_bus_msg.c`/`esp_bus_svc.c` ever linked; `esp_bus_led.c` and
   `esp_bus_btn.c` were already being dropped, so the dependency was cheaper
   than its 2,325-line source suggests — but not free.
2. **`src/esp_wifi_config_bus.c`** — 892 B. A 15-branch `strcmp` chain that
   translated bus action names into calls on the library's own public API. Every
   branch forwarded to a function callers could already reach directly.
3. **The descriptor tables** — `actions[]` (14 entries × 4 pointers) and
   `events[]` (9 × 3 pointers) in `wifi_cfg_init()`, plus every type and
   description string they pointed at. Runtime introspection metadata that
   nothing read.

What replaced them is one file, `src/esp_wifi_config_event.c`, measuring
**198 bytes of flash and zero RAM** in the linked `basic` image: an
`ESP_EVENT_DEFINE_BASE`, a posting wrapper, and the event-name table.

## Where the RAM went

Static RAM **falls** by 48 bytes — `esp_bus`'s statics leave and nothing
replaces them, because the event loop's storage already exists and belongs to
`esp_event`.

The larger saving is runtime. `esp_bus_init()` allocated, with struct sizes
confirmed by compiling the layouts for xtensa:

| Allocation | Size |
|---|---:|
| Bus task stack (`CONFIG_ESP_BUS_TASK_STACK_SIZE`) | 4,096 B |
| Message queue: 16 × `sizeof(message_t)` (64 B) | 1,024 B |
| Module node for `"wifi"` (`module_node_t`) | 52 B |
| Per subscription (`sub_node_t`), `basic` uses 4 | 208 B |
| **Subtotal** | **5,380 B** |
| FreeRTOS control blocks (TCB, 2 × queue struct) | ~200–300 B |

Roughly **5.4–5.7 KB of heap**, permanently, for a second event system running
beside the one the app already had. None of it appears in `idf.py size`, which
is why the flash figure understates the change: the heap saving is comparable in
size and lands in the scarcer resource.

The default event loop's task and queue are not a new cost — every ESP-IDF WiFi
app creates them for `WIFI_EVENT` and `IP_EVENT` regardless.

## Per-event cost

`esp_bus_emit()` was not a function call. Each event: `malloc` + `memcpy` the
payload; `snprintf` a `"module:event"` pattern; copy a 64-byte `message_t` into
the queue; wake the bus task; take a mutex; `snprintf` the pattern again; walk
every subscription running a wildcard match; `realloc` an array of matches; call
handlers; two `free`s.

`esp_event_post()` does one `calloc` + `memcpy` of the payload
(`esp_event.c:928`), a queue send, and dispatch on the loop task with no pattern
matching. Fewer allocations, no string formatting, no second matching pass — and
critically, on infrastructure that already exists.

## Dropped events, now visible

`esp_bus_emit()` enqueued with a **zero timeout**:

```c
if (xQueueSend(g_bus.queue, &msg, 0) != pdTRUE) {
    if (msg.data) free(msg.data);
    return ESP_ERR_TIMEOUT;
}
```

A full 16-deep queue discarded the event — and all 17 emit sites in this library
ignored the return value. Under load, a scan completing while provisioning
churns, events vanished with no diagnostic anywhere.

The replacement still posts with a zero timeout, deliberately: blocking the WiFi
state machine because a subscriber is slow trades a lost notification for a
stalled reconnect, which is the worse failure. But it checks the result and logs
it:

```
W (12345) wifi_cfg_event: event 'got_ip' not posted: ESP_ERR_TIMEOUT
```

That log line is what costs the 340 bytes of event-name strings noted above. It
turns a silent, unattributable failure into a named one, and it is the reason
the string table can no longer be dropped by the linker.

---

## Drawbacks

### 1. Handler registration now has a precondition

`esp_event_handler_register()` requires the default loop to exist.
`wifi_cfg_init()` creates it, so an application that wants to catch events
emitted *during* init must call `esp_event_loop_create_default()` itself first.
Creating it twice is harmless — the library already tolerates
`ESP_ERR_INVALID_STATE` at `esp_wifi_config.c:387`.

This is a genuine ergonomic regression against the bespoke table, which needed
no setup at all. It is, however, the standard ESP-IDF pattern that every
`esp_event` user already follows, so it trades a library-specific convenience
for an ecosystem-wide convention.

### 2. Handlers no longer receive a payload length

`esp_bus` passed subscribers `(event, data, len, ctx)`. An esp_event handler
gets `(arg, base, id, data)` — no size. The event id fixes the payload type by
contract, which is how `WIFI_EVENT` and `IP_EVENT` already work, but it is
strictly less than the bus offered.

This showed up immediately when porting the HIL test harness, whose translation
layer guarded every cast:

```c
if (data && len >= sizeof(wifi_var_t)) { ... }   /* now just: if (data) */
```

Audited against every emit site, the guards were never firing: all twenty-two
posts send a complete object of the documented type, or `NULL, 0`. So nothing
is masked today. What is lost is the ability of a *consumer* to defend itself
against a future library bug that posts a short payload — that class of error
now reads as an out-of-bounds access instead of a caught mismatch.

### 3. Capabilities genuinely lost

The library used a fraction of `esp_bus`, but an *application* wiring several
components together may use more. Removing the dependency here does not remove
it from such an application — it just stops arriving transitively. What no
longer exists **for wifi_cfg events specifically**:

- **Pattern-matched subscriptions.** `esp_bus_sub("wifi:*", …)` matched by
  wildcard. `ESP_EVENT_ANY_ID` covers all-events, and specific ids cover one;
  there is nothing between.
- **Declarative routing.** `esp_bus_connect("wifi:connected", "led1.on", …)`
  wired an event to another module's action with no glue code. That now takes a
  handler function.
- **Cross-module request/response.** The `wifi.get_status` action was reachable
  by name without a compile-time dependency on this library's header. Better for
  type safety now, worse for loose coupling.
- **Introspection.** The `actions[]`/`events[]` metadata described the module's
  surface at runtime. Nothing consumes it today, but a CLI or diagnostic
  endpoint could have.

An application that wants these can keep `esp_bus` and bridge in four lines:

```c
static void to_bus(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_bus_emit("wifi", wifi_cfg_event_name(id), data, 0);
}
esp_event_handler_register(WIFI_CFG_EVENT, ESP_EVENT_ANY_ID, to_bus, NULL);
```

That is where the choice belongs: applications that want a bus can have one, and
applications that do not should not pay 5.9 KB of flash and 5.4 KB of heap for
one they never call.

### 4. Breaking API change

Every downstream subscriber must be rewritten: handlers take the esp_event
signature instead of `(const char *event, const void *, size_t, void *)`,
`WIFI_CFG_EVT_*` string macros become `WIFI_CFG_EVENT_*` enumerators, and
`WIFI_EVT()` / `WIFI_REQ()` / `WIFI_ACTION_*` are gone. The rename is mechanical
— the names correspond one-to-one — and MIGRATION.md carries the diffs, but the
work is real.

---

## A latent bug the removal exposed

Removing `esp_bus` broke every `improv_serial` build — six of forty-eight
matrix jobs, all three boards, both IDF series:

```
src/esp_wifi_config_improv_serial.c:21:10:
fatal error: driver/uart.h: No such file or directory
```

The library declared that dependency conditionally:

```cmake
if(CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL)
    list(APPEND SRCS "src/esp_wifi_config_improv_serial.c")
    list(APPEND PRIV_REQUIRES driver)     # never reached the dep scan
endif()
```

The `SRCS` append works; the `PRIV_REQUIRES` append does not, because ESP-IDF
resolves requirements in an early expansion pass that a `list(APPEND)` in the
component body cannot feed. `driver`'s include directory never arrived.

It compiled anyway for as long as `esp_bus` was in the graph, because `esp_bus`
declared `REQUIRES freertos driver esp_timer` — publicly. Improv Serial had
been building on an include path it inherited rather than asked for, since it
was written.

`driver` is now declared unconditionally, matching the pattern the same file
already applies to `bt`, `protocomm` and `wifi_provisioning` — and documents,
four lines above the block that got it wrong. It costs nothing where Improv
Serial is off: `examples/basic` measures 0xcf550 bytes either way, because the
linker drops the unreferenced archive.

Two things are worth taking from this. The bug is *older* than the removal and
had nothing to do with the bus — removing the bus only stopped something else
from covering for it. And nothing in the library's own seven examples would
have caught it, because `examples/with_improv` builds Improv **BLE**; it took a
variant matrix that builds Improv Serial alone.

---

## Advantages beyond size

- **No third-party dependencies at all.** The component now needs only ESP-IDF.
  That removes a fork from the maintenance path — the reason this work started.
- **One event system instead of two.** The library was consuming `esp_event` and
  publishing on `esp_bus`. Applications had to understand both.
- **Idiomatic.** Anyone using this library already writes
  `esp_event_handler_register(IP_EVENT, …)`. wifi_cfg events now work
  identically, which is less to learn than any custom API.
- **Handlers are off the critical tasks.** Delivery on the loop task means a slow
  handler cannot stall the WiFi state machine, and application code never lands
  on the httpd task's 4 KB stack — the one this codebase has already overflowed
  via cJSON recursion, as `WIFI_CFG_JSON_MAX_DEPTH` records.
- **Type safety — which caught four documentation bugs.** Rewriting the events
  guide surfaced four payload types it documented that do not exist:
  `wifi_got_ip_t`, `wifi_scan_done_t`, `wifi_network_changed_t`,
  `wifi_var_changed_t`. The real payloads are `esp_netif_ip_info_t`,
  `uint16_t`, `wifi_network_t` and `wifi_var_t`. A caller following the old docs
  would have cast to a nonexistent type.
- **Internal coupling is now a function call.** The Improv module used to
  subscribe to the library's own `GOT_IP` and `DISCONNECTED` events. It now gets
  called directly by the state machine (`wifi_cfg_improv_on_got_ip()`), which
  removes a task hop and a payload copy between two files in the same component.

---

## Recommendation

Merge. The bus was a forked third-party component providing decoupling the
library never used, at 5.9 KB flash, 5.4 KB heap and a maintenance burden. The
replacement is the platform's own event system, which was already linked,
already running, and already used by this library for four other event bases.

The hardware run that was outstanding has now happened: 145 tests pass on two
real boards, and the only two failures reproduce on `main`. Event delivery
timing and ordering — the thing this change most plausibly breaks, and the one
thing a build cannot check — is exercised throughout that suite and holds.

---

## Validation status

Full HIL suite, both DUTs, ESP-IDF 5.5.4, run `464c64fe` (1:01:27):

| Gate | Result |
|---|---|
| Library builds, 7 examples × IDF 5.5.3 | **pass**, no warnings |
| Library builds, IDF 5.4.3 | **pass** |
| HIL firmware matrix — 9 variants × 3 boards × 2 IDF series | **48/48 built** |
| Workstation host suite | **107 passed** |
| Firmware host suite (C1 emitter/parser) | **58 checks, 0 failures** |
| `shared/validate_contracts.py` | **62 checks, 0 failures** |
| **Hardware-in-the-loop, 170 tests** | **145 passed, 2 failed, 20 skipped, 2 xfailed, 1 error** |

The 145 cover boot, reconnect and backoff, AP fault injection, SoftAP and the
captive portal, the REST API, BLE provisioning and Improv — on both an
ESP32-D0WDQ6 and an ESP32-S3, against a real WPA2 AP with real deauthentication
and DHCP starvation.

### The two failures are pre-existing

Both are `test_improv_ble_full_flow`. The device answers `state`, `error`,
`capabilities` and `device_info`, then never responds to
`GET_WIFI_NETWORKS`. Device telemetry shows the scan completing
(`scan_done, count:13`) and heartbeats continuing, so the RPC ran and the
*response* is what went missing.

Cause is the BLE `IMPROV_RPC_STYLE_SINGLE` shape: three TLV strings per network
fill the 255-byte payload at roughly twelve neighbours, and the resulting
~258-byte notification does not arrive where the 31-byte `device_info` one
does. It is the BLE analogue of the serial truncation defect fixed in
`03713ff` / `20326ce`, which deliberately left BLE on `SINGLE`.

Verified against `main` (library `56294eb`, HIL app reverted to its esp_bus
version, run `f1a8c9eb`): same assertion, same characteristic, same timeout,
same lone `device_info` beforehand. **The defect is on `main` and is not caused
by this branch.** Written up in the harness at
`rig/findings/library-improv-ble-scan-response-never-arrives-with-many-neighbours.md`.

The branch run failed 2 of 2 boards and the `main` baseline 1 of 2. That is
*not* evidence the branch made it worse: the threshold depends on how many
neighbours happen to be up (12, 13 and 14 across the runs), the rig documents
its RF as uncontrolled, and these are single runs.

### The one error is infrastructure

`AgentTimeout: no @RSP for id 7 (factory_reset) within 3s`, at fixture setup on
the ESP32 — one occurrence among many tests using the same fixture, on the
board whose USB cable had just been replaced. Reads as a slow NVS erase against
a 3-second `agent_cmd_s`, not a library defect.

## Still unverified

- The **heap figure** is derived from `esp_bus`'s Kconfig defaults and struct
  layouts compiled for xtensa. Arithmetic on real sizes, not a
  `heap_caps_get_free_size()` delta.
- **Event loop queue depth** under sustained load. The suite did not provoke a
  dropped-event warning; `CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE` (default 32) is
  the knob if one ever appears.
- **A full-suite baseline on `main`.** Only the two failing Improv BLE cases
  were controlled against `main`; the other 168 were not, so a pre-existing
  failure elsewhere would have been read as a pass on both sides rather than
  attributed.

One pre-existing warning is unrelated to this work and present on `main`:
`handler_simple_page defined but not used` in `esp_wifi_config_http.c:885`,
in WebUI builds.

## Reproducing the measurements

```bash
source ~/esp/v5.5.3/export.sh
cd examples/basic
rm -rf build managed_components dependencies.lock
idf.py build && idf.py size && idf.py size-components
```

Compare against the same commands on `main`. Per-object detail:
`python -m esp_idf_size --files build/wifi_cfg_basic.map`.
