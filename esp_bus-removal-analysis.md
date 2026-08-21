# Removing the `esp_bus` Dependency — Cost, Benefit, and What Is Lost

Analysis accompanying the `refactor/remove_esp_bus` branch, which replaces the
`esp_bus` event bus with direct callbacks.

**Method.** ESP-IDF v5.5.3, target esp32s3, all seven bundled examples built
clean before and after. Sizes come from `idf.py size` / `size-components` over
the linked image; struct sizes and stack frames come from the xtensa toolchain
(`objdump`, `nm`) against the real target ABI, not from host assumptions.
Nothing was flashed, so every claim below is static or computed — the
"Unverified" section says what that leaves open.

---

## Summary

Removing `esp_bus` saves **~6.1–6.2 KB of flash** and **~5.4 KB of heap** in
every configuration tested, costs **48 bytes of static RAM**, and makes event
delivery synchronous. It also removes a class of silent event loss.

The bus was carrying almost none of its own weight here: of its five source
files, two (LED and button drivers) were already dropped by the linker, and of
the three that remained, the library used `emit`, `sub`, `unsub`, `reg` and
`is_init` — five entry points out of the twenty-seven `esp_bus.h` declares,
before counting the LED and button headers.

| | Before | After | Delta |
|---|---|---|---|
| Flash, `basic` example | 855,120 B | 848,912 B | **−6,208** |
| Flash, `with_improv` | 1,293,440 B | 1,287,248 B | **−6,192** |
| Flash, `with_webui` | 864,464 B | 858,320 B | **−6,144** |
| Flash, `with_ble` | 1,145,456 B | 1,139,392 B | **−6,064** |
| Static RAM (`.bss`) | — | — | **+48** |
| Heap at runtime | ~5.4 KB | 0 | **−5.4 KB** (computed) |
| Third-party components | 1 | 0 | **−1** |

---

## Where the flash went

Per-archive, `basic` example:

| Archive | Before | After | Delta |
|---|---:|---:|---:|
| `libthorrak__esp_bus.a` | 3,300 | 0 | **−3,300** |
| `libesp_wifi_config.a` | 21,356 | 20,019 | **−1,337** |
| `libesp_app_format.a`\* | 74,483 | 73,099 | **−1,384** |
| `libmain.a` (the example) | 2,019 | 1,949 | −70 |
| `libmbedcrypto.a` | 61,521 | 61,489 | −32 |
| others | — | — | +8 |

\* Not really `esp_app_format`. `esp_app_desc.c.obj` is the first contributor to
the merged `.flash.rodata` output section, so `esp_idf_size` attributes the
whole shared string pool to it. This row is the string-pool shrink: `esp_bus`'s
log tags and format strings, plus the library's own action/event descriptor
strings. It checks out against the totals — `.rodata` fell 1,712 B overall,
which is this 1,384 plus the 332 B that left `libesp_wifi_config.a` directly.

Three things were deleted:

1. **`esp_bus` itself** — 3,252 B flash + 48 B `.bss`. Only
   `esp_bus.c`/`esp_bus_msg.c`/`esp_bus_svc.c` ever linked; `esp_bus_led.c` and
   `esp_bus_btn.c` were already being dropped, so the dependency was cheaper
   than its 2,325-line source suggests — but not free.
2. **`src/esp_wifi_config_bus.c`** — 892 B. A 15-branch `strcmp` chain that
   translated bus action names into calls on the library's own public API. Every
   branch forwarded to a function callers could already reach directly, so this
   was pure adapter.
3. **The descriptor tables** — `actions[]` (14 entries × 4 pointers) and
   `events[]` (9 × 3 pointers) in `wifi_cfg_init()`, plus every type and
   description string they pointed at. Metadata for a bus introspection feature
   the library never used.

What replaced them, measured section by section:

| Symbol | Size |
|---|---:|
| `.text.wifi_cfg_event_subscribe` | 121 B |
| `.text.wifi_cfg_event_post` | 131 B |
| `.text.wifi_cfg_event_unsubscribe` | 58 B |
| `.text.wifi_cfg_event_name` | 30 B |
| `.rodata` (name table + strings) | 346 B |
| `.bss.s_subs` (8 slots × 12 B) | 96 B |
| `.data.s_subs_lock` | 8 B |

In the `basic` image the linker discards `wifi_cfg_event_name`,
`wifi_cfg_event_unsubscribe` and all 346 B of `.rodata`, because nothing
references them — the actual linked cost there is **252 B of code and 104 B of
RAM**. The event-name table is pay-for-what-you-use.

## Where the RAM went

Static RAM rises by 48 bytes: the 96-byte subscription table and 8-byte
spinlock arrive, `esp_bus`'s 48 bytes of statics leave, and a few bytes shift
elsewhere in the link.

That trade buys back a much larger runtime allocation. `esp_bus_init()`
allocated, with struct sizes confirmed by compiling the layouts for xtensa:

| Allocation | Size |
|---|---:|
| Bus task stack (`CONFIG_ESP_BUS_TASK_STACK_SIZE`) | 4,096 B |
| Message queue: 16 × `sizeof(message_t)` (64 B) | 1,024 B |
| Module node for `"wifi"` (`module_node_t`) | 52 B |
| Per subscription (`sub_node_t`), `basic` uses 4 | 208 B |
| **Subtotal** | **5,380 B** |
| FreeRTOS control blocks (TCB, 2 × queue struct) | ~200–300 B |

So roughly **5.4–5.7 KB of heap**, permanently, for a component the library
used five functions of. None of this appears in `idf.py size`, which is why the
flash figure understates the change: the heap saving is comparable to the flash
saving and lands in the scarcer resource.

## Per-event cost

`esp_bus_emit()` was not a function call. Each event:

1. `malloc(len)` and `memcpy` the payload onto the heap,
2. `snprintf` a `"module:event"` pattern string,
3. copy a 64-byte `message_t` into the queue,
4. wake the bus task (context switch),
5. take the bus mutex, `snprintf` the pattern again, walk every subscription
   running a wildcard pattern match,
6. `realloc` a temporary array of matched subscribers,
7. call the handlers, then `free` the array and `free` the payload.

Two heap allocations and a context switch per event. The replacement takes a
spinlock, compares at most eight integers, copies the matches into a 96-byte
stack array, releases the lock, and calls the handlers directly. Zero
allocations.

## A failure mode that disappears

`esp_bus_emit()` enqueued with a **zero timeout**:

```c
if (xQueueSend(g_bus.queue, &msg, 0) != pdTRUE) {
    if (msg.data) free(msg.data);
    return ESP_ERR_TIMEOUT;
}
```

A full 16-deep queue therefore discarded the event — and all 17 emit sites in
this library ignored the return value. Under load (a scan completing while
provisioning churns) events could be dropped with no diagnostic anywhere.
Synchronous dispatch has no queue to overflow, so this failure mode is gone
rather than merely less likely.

The new table has a bounded-capacity failure of its own, but it surfaces at
`wifi_cfg_event_subscribe()` time — at startup, with an `ESP_ERR_NO_MEM` the
caller can act on — instead of silently, later, in the field.

---

## Drawbacks

### 1. Handlers now run synchronously, on the emitting task

This is the substantive change, and it is a real constraint on application code.
Under `esp_bus`, a handler ran on the bus's own task with its own 4 KB stack,
fully decoupled from whatever emitted the event. Now it runs inline.

Which task that is depends on the event:

- Connection events (`GOT_IP`, `DISCONNECTED`, `CONNECTED`) → the library's
  `wifi_cfg` task, 4 KB by default.
- Config-change events (`NETWORK_ADDED`, `VAR_CHANGED`) → **whichever task
  called the API**. For REST requests that is the httpd task — whose 4 KB stack
  this codebase has already overflowed once, via cJSON recursion, as
  `WIFI_CFG_JSON_MAX_DEPTH` in `esp_wifi_config_priv.h` records.

A slow or stack-hungry handler now delays reconnects, scans and provisioning
instead of running out of the way. This is documented on `wifi_cfg_event_cb_t`,
in the events guide, and in MIGRATION.md, but documentation is weaker than the
structural guarantee the bus provided.

Concrete nesting cost, measured from the `entry` instruction: the library's own
`on_wifi_connected` handler in the Improv module has a **400-byte** frame, and
`wifi_cfg_event_post` adds **128 bytes** beneath it — so ~530 bytes plus the
`send_rpc_result` call chain, now charged to the `wifi_cfg` task rather than to
a stack of its own.

**Deadlock was the obvious worry, and it does not materialise.** I checked all
22 emit sites against the library's mutex: every one fires with the lock
released. The internal task queue uses zero-timeout sends, so a callback that
calls back into `wifi_cfg_connect()` cannot block either. A handler may call any
public API — it just should not do so much work that the state machine stalls.

### 2. Capabilities genuinely lost

The library used a fraction of `esp_bus`, but an *application* wiring several
components together may use more. Removing the dependency from this library
does not remove it from such an application — it just stops arriving
transitively. What no longer exists **for wifi_cfg events specifically**:

- **Wildcard subscriptions.** `esp_bus_sub("wifi:*", ...)` matched by pattern.
  The replacement offers `WIFI_CFG_EVENT_ANY` (all events) but nothing between
  that and a single event.
- **Declarative routing.** `esp_bus_connect("wifi:connected", "led1.on", ...)`
  wired an event to another module's action with no glue code. That now takes a
  handler function.
- **Cross-module request/response.** The `wifi.get_status` action was reachable
  by name from any module without a compile-time dependency on this library's
  header. Callers now `#include "esp_wifi_config.h"` and call the function —
  better for type safety, worse for loose coupling.
- **Introspection.** The `actions[]`/`events[]` metadata described the module's
  surface at runtime. Nothing consumes this today, but a CLI or diagnostic
  endpoint could have.

If any of these matter to a downstream application, it can still depend on
`esp_bus` itself and bridge in one small handler:

```c
static void to_bus(wifi_cfg_event_t e, const void *d, size_t n, void *ctx) {
    esp_bus_emit("wifi", wifi_cfg_event_name(e), d, n);
}
wifi_cfg_event_subscribe(WIFI_CFG_EVENT_ANY, to_bus, NULL, NULL);
```

That is 4 lines, and it restores the async decoupling too — which is arguably
where the choice belongs: applications that want a bus can have one, and
applications that do not should not pay 6 KB of flash and 5 KB of heap for it.

### 3. Breaking API change

Every downstream subscriber must be rewritten: handler signatures take
`wifi_cfg_event_t` instead of `const char *`, `WIFI_CFG_EVT_*` string macros
become `WIFI_CFG_EVENT_*` enumerators, and `WIFI_EVT()`/`WIFI_REQ()`/
`WIFI_ACTION_*` are gone. The rename is mechanical (the names correspond
one-to-one) and MIGRATION.md carries the diffs, but it is not source-compatible
and the compiler will not catch a missed `strcmp`-on-event-name — it will fail
to compile, which is the good outcome, but the work is real.

---

## Advantages beyond size

- **No third-party dependencies at all.** The component now needs only ESP-IDF.
  For a library whose selling point is drop-in provisioning, "add one line to
  `idf_component.yml`" is a materially better story than "add one line, and it
  pulls in a component you did not ask for."
- **Type safety.** `WIFI_CFG_EVENT_GOT_IP` is checked by the compiler;
  `"wifi:got_ip"` was not. The old string path also made two payload-type
  mistakes reachable — while rewriting the events guide I found it documented
  `wifi_got_ip_t`, `wifi_scan_done_t`, `wifi_network_changed_t` and
  `wifi_var_changed_t` as payload types. None of those types exist. The real
  payloads are `esp_netif_ip_info_t`, `uint16_t`, `wifi_network_t` and
  `wifi_var_t`. A caller following the old documentation would have cast to a
  nonexistent type. The enum table in the header is now generated from the
  actual emit sites.
- **Debuggability.** A synchronous call stack shows which state transition
  produced an event. The bus dispatched everything from one task, so every
  backtrace bottomed out in `bus_task` regardless of cause.
- **Determinism.** No queue depth, no task priority interaction, no ordering
  question between an event and the state change that caused it.

---

## Recommendation

Merge. The bus was providing async decoupling and pattern routing that this
library never used, at 6.2 KB flash, 5.4 KB heap and one external dependency.
The synchronous-callback constraint is real and must stay documented, but it is
the same constraint ESP-IDF's own `esp_event` default loop imposes on handlers,
and it is bridgeable in four lines for anyone who wants the old behaviour.

The one item worth hardware validation before release is stack headroom on the
`wifi_cfg` task in an Improv build, where the library's own handler now nests
~530 bytes deep. If that proves tight, the fix is a larger default for
`CONFIG_WIFI_CFG_TASK_STACK_SIZE` — not a return to the bus.

---

## Unverified

Nothing was flashed to hardware, so the following are static or computed rather
than observed:

- The **heap figure** is derived from `esp_bus`'s Kconfig defaults and struct
  layouts compiled for xtensa. It is arithmetic on real sizes, not a
  `heap_caps_get_free_size()` delta.
- **Stack headroom** under load is unmeasured. Frame sizes are exact; what they
  leave free on a 4 KB task running the full Improv path is not.
- **Runtime behaviour** — that events still fire correctly, in the right order,
  with the right payloads — has been verified only by compilation and by
  reading the call sites. The seven examples build clean; none has been run.

## Reproducing the measurements

```bash
source ~/esp/v5.5.3/export.sh
cd examples/basic
rm -rf build managed_components dependencies.lock
idf.py build && idf.py size && idf.py size-components
```

Compare against the same commands on `main`. Per-object detail:
`python -m esp_idf_size --files build/wifi_cfg_basic.map`.
