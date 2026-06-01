# Future Enhancements

Independent enhancement ideas not yet implemented. Each is self-contained
and can be developed in isolation; no ordering constraints.

**Design principle:** This library runs on flash-constrained devices and
is primarily used for initial setup, not ongoing operation. Every
addition must justify its flash/RAM cost. Prefer reusing existing logic
over adding new logic.

---

## 1. Extensible Static File Serving & URL Prefix

Currently only 3 hardcoded URI handlers exist (`/`, `/assets/app.js`,
`/assets/index.css`). This has two limitations:

1. A custom app that needs additional assets (images, locale JSON files,
   extra JS chunks) has no way to serve them.
2. When the device hosts multiple web services from a shared filesystem,
   there is no way to namespace the WiFi Config UI under a URL prefix
   (e.g., `/wifi/`).

**Proposal:**

**Wildcard file serving:** When `CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH` is
set, register a low-priority wildcard handler that calls
`serve_from_filesystem()` for any unmatched path under the WebUI prefix.
The content-type detection logic already exists — extend it to cover a
few additional types (`.woff2`, `.ico`, `.jpg`/`.jpeg`).

**URL prefix:** Add a `const char *webui_base_path` field to
`wifi_cfg_http_config_t` (default: `""`). When set (e.g., `"/wifi"`),
all WebUI endpoints are registered under that prefix:

- `/wifi/` serves `index.html`
- `/wifi/assets/app.js` serves the JS bundle
- `/wifi/assets/*` serves additional static assets
- Captive portal detection paths redirect to `http://<AP_IP><webui_base_path>/`

This mirrors how `api_base_path` already works for API endpoints.

**Scope:**
- Add `webui_base_path` to `wifi_cfg_http_config_t`
- Prefix all WebUI URI registrations with the configured path
- Add wildcard handler registration in `wifi_cfg_webui_register_handlers()`
- Expand MIME type map in `serve_from_filesystem()` (a few additional entries)
- Update captive portal redirect target to use the prefix

---

## 2. Error Codes & Internationalization

Full-stack. The backend error codes and the frontend i18n system that
interprets them ship together so there is no intermediate state where
raw codes are shown to users.

### 2a. Structured API Error Codes (firmware)

Replace bare English error strings with short, machine-readable codes.
The code **replaces** the current message (not a second field) to avoid
increasing flash usage for string storage. Codes are kept short and are
themselves human-readable enough for debugging.

**Current behavior:**
```json
{"error": "Already exists"}
```

**Proposed behavior:**
```json
{"error": "net_exists"}
```

**Scope:**
- Define string constants for all error codes in a header (e.g., `wifi_cfg_errors.h`)
- Update all `handler_*` functions in `esp_wifi_config_http.c` to use the code constants
- Document error codes in the header

**Error codes to define (approx.):**
- `net_exists`, `net_not_found`, `net_limit`
- `var_not_found`, `var_limit`, `var_key_long`, `var_val_long`
- `bad_json`, `missing_field`, `field_long`
- `scan_fail`, `connect_fail`, `disconnect_fail`
- `ap_start_fail`, `ap_stop_fail`, `ap_inactive`
- `auth_required`, `auth_fail`
- `reset_fail`, `internal`

### 2b. Extract Hardcoded Strings (frontend)

Pull all user-facing strings out of components into a central locale file.

**Scope:**
- Create `src/i18n/strings/en.json` with all UI strings keyed by dot-notation (e.g., `"status.connected"`, `"networks.scan_button"`, `"errors.net_not_found"`)
- Audit all components for hardcoded text:
  - `StatusCard.tsx` (~8 strings)
  - `NetworkList.tsx` (~10 strings)
  - `SavedNetworks.tsx` (~6 strings)
  - `APSettings.tsx` (~8 strings)
  - `App.tsx` (~2 strings)
- Replace inline strings with imports from the JSON

### 2c. i18n Framework (frontend)

Implement a lightweight translation system. A full library like
`i18next` (~15KB) would nearly double the ~25KB gzipped bundle — a
custom Preact context + hook at <1KB is the right approach given flash
constraints.

**Proposal:**
- `src/i18n/index.ts` — translation context provider and `useTranslate()` hook
- `src/i18n/strings/<locale>.json` — one file per locale
- Interpolation support: `t('status.connected_to', { ssid })` → `"Connected to MyNetwork"`
- Locale detection: check `navigator.language`, fall back to `"en"`
- Locale override: store preference in a custom variable (`_locale`) via the vars API
- RTL support: set `dir="rtl"` on `<html>` when locale is Arabic, Hebrew, etc.
- Backend error mapping: the `error` code (from 2a) is used as a translation key under `errors.*`

**Bundle impact:** The i18n hook itself is <1KB. Each locale JSON file
is ~1-2KB uncompressed, ~0.5KB gzipped. Only the active locale is
loaded at runtime. The embedded build can ship with just English;
additional locales can be served from the filesystem (enabled by
enhancement 1's extensible file serving).

**Scope:**
- New files: `src/i18n/index.ts`, `src/i18n/strings/en.json` (from 2b), additional locale files
- Wrap `<App>` in a `<TranslationProvider>`
- Update all components to use `useTranslate()` instead of raw string imports
- Add locale switcher to the UI (small dropdown or auto-detect only)

---

## 3. Real-Time Updates (SSE)

Full-stack. Firmware SSE endpoint + frontend consumption.

*Lower priority than the other enhancements. This library is primarily
used for initial setup — a session lasting a few minutes where 5-second
polling is adequate. SSE is a nice-to-have that reduces unnecessary
requests on the device during setup, but the UX improvement is marginal
for a brief interaction. Should be behind a compile-time flag
(`CONFIG_WIFI_CFG_ENABLE_SSE`, default off) so it adds zero cost when
unused.*

### 3a. SSE Endpoint (firmware)

**Proposal:**
- Add `GET /api/wifi/events` endpoint (guarded by `CONFIG_WIFI_CFG_ENABLE_SSE`)
- Events to stream: `status_change`, `scan_complete`, `network_added`, `network_removed`, `var_changed`
- Format: standard SSE (`event: <type>\ndata: <json>\n\n`)
- Subscribe internally to esp_bus events and forward to all connected SSE clients
- Add `CONFIG_WIFI_CFG_SSE_MAX_CLIENTS` Kconfig option (default 2)

**Flash/RAM cost:** New source file (~1-2KB compiled), plus per-client
tracking overhead. Zero cost when `CONFIG_WIFI_CFG_ENABLE_SSE` is
disabled — the file is excluded from compilation entirely.

**Scope:**
- New source file `esp_wifi_config_sse.c` (conditionally compiled)
- esp_bus subscriptions for relevant events
- Client tracking (linked list of `httpd_req_t` handles)
- Kconfig options
- Register URI handler alongside other API endpoints

### 3b. SSE Frontend Consumption

**Proposal:**
- `src/api/events.ts` — `EventSource` wrapper with auto-reconnect and polling fallback
- On `status_change` event, update the status display immediately
- On `scan_complete` event, refresh the network list
- Fallback: if `EventSource` connection fails or the endpoint returns 404 (firmware compiled without SSE), revert to 5-second polling transparently

**Scope:**
- New file: `src/api/events.ts`
- Update `App.tsx` to use event-driven state updates with polling fallback
- Update `StatusCard.tsx` and `NetworkList.tsx` to accept pushed data
