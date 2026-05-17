---
sidebar_position: 2
title: BLE Provisioning
description: Provision Wi-Fi over BLE using ESP-IDF's official Wi-Fi Provisioning manager
---

# BLE Provisioning

ESP WiFi Config integrates with ESP-IDF's official **Wi-Fi Provisioning**
manager (`wifi_prov_mgr` on IDF 5.4, `network_prov_mgr` on IDF 6.x) over
the BLE scheme. This replaces the previous custom JSON-over-GATT service
(UUID `0xFFE0`) which has been removed — see [MIGRATION.md][migrate] for
how to update existing client tools.

[migrate]: https://github.com/thorrak/esp_wifi_config/blob/main/MIGRATION.md

## Why Network Provisioning

- Standard, audited protocol — works with **Espressif's official mobile
  apps** out of the box ("ESP BLE Provisioning" on iOS / Android).
- Encrypted handshake (Security 1 / 2) instead of plaintext JSON.
- Bluedroid and NimBLE supported via the same code path.
- Library still owns the higher-level lifecycle: provisioning mode,
  retry/backoff, multi-network store, custom variables, post-prov HTTP.

## Enabling

### 1. Kconfig

```kconfig
CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING=y
CONFIG_WIFI_CFG_NETWORK_PROVISIONING_BLE=y
```

That's the whole Kconfig surface. Security version, PoP, device-name
template, and the rest of the runtime parameters are set on
`wifi_cfg_prov_config_t` in step 3 below.

Mutually exclusive with `CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE` — both want
to own the BLE GAP advertising and the host stack.

### 2. Bluetooth Stack

```kconfig
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y          # recommended
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144
# Or:
# CONFIG_BT_BLUEDROID_ENABLED=y
```

### 3. Runtime Config

```c
wifi_cfg_init(&(wifi_cfg_config_t){
    .provisioning_mode = WIFI_PROV_ON_FAILURE,
    .stop_provisioning_on_connect = true,
    .provisioning_teardown_delay_ms = 5000,

    .prov_ble = {
        .device_name         = "PROV_{id}", // GAP name template (supports {id})
        .security            = WIFI_CFG_PROV_SECURITY_1, // _DEFAULT → Security 1
        .pop                 = "1234abcd",  // Security 1 PoP (NULL/"" → no PoP)
        .memory_policy       = WIFI_CFG_PROV_MEM_FREE_BTDM, // see c-api.md
        .wifi_conn_attempts  = 5,           // 0 = infinite
        .reset_on_failure    = true,        // accept retries without reboot
        .max_failed_attempts = 3,           // 0 → library default (3)
        .firmware_version    = "1.0.0",
    },
});
```

`provisioning_mode` controls **when** wifi_prov_mgr is started:

| Mode | Behaviour |
|------|-----------|
| `WIFI_PROV_ALWAYS` | start at boot, regardless of saved networks |
| `WIFI_PROV_ON_FAILURE` | start when no networks saved or all failed |
| `WIFI_PROV_WHEN_UNPROVISIONED` | start only if no networks saved |
| `WIFI_PROV_MANUAL` | only via explicit API call |

`stop_provisioning_on_connect` and `provisioning_teardown_delay_ms` work
exactly like they did with the previous custom BLE — once the device is
connected the provisioning manager is stopped (after the configured
delay) and the BLE host is torn down.

## Security versions

| Version | Handshake | Setup cost |
|---------|-----------|-----------|
| Security 0 | none (plaintext) | none — testing only |
| Security 1 | Curve25519 + AES-CTR with PoP | set `prov_ble.pop` (or leave NULL for no-PoP mode) |
| Security 2 | SRP6a (salted authenticated key exchange) | requires pre-computed `salt` + `verifier` |

For Security 2, derive the `salt` / `verifier` offline using
`esp-idf-provisioning`'s helper or the `esp_prov` Python tool, embed the
bytes in firmware, and pass them via `wifi_cfg_prov_config_t`:

```c
extern const uint8_t my_salt[];
extern const size_t  my_salt_len;
extern const uint8_t my_verifier[];
extern const size_t  my_verifier_len;

wifi_cfg_init(&(wifi_cfg_config_t){
    .prov_ble = {
        .security2_username       = "device-fleet-2",
        .security2_salt           = my_salt,
        .security2_salt_len       = my_salt_len,
        .security2_verifier       = my_verifier,
        .security2_verifier_len   = my_verifier_len,
    },
});
```

If `prov_ble.security` is set to `WIFI_CFG_PROV_SECURITY_2` but no
salt/verifier is provided, `wifi_cfg_init()` returns
`ESP_ERR_INVALID_ARG` — the library does not silently fall back.

## Custom protocomm endpoints

Alongside the standard `prov-config` / `prov-scan` endpoints, the library
registers four custom protocomm endpoints. They are minimal by design —
the goal is to expose the library's higher-level state, not to recreate
the broad pre-Wi-Fi management surface that previously lived in the
0xFFE0 service.

| Endpoint | Direction | Purpose |
|----------|-----------|---------|
| `esp-wifi-config-version` | read | library/IDF/firmware versions |
| `esp-wifi-config-capabilities` | read | feature flags + limits |
| `esp-wifi-config-vars` | read/write | the custom variable store |
| `esp-wifi-config-network-policy` | read | `provisioning_mode`, retries |

The `vars` endpoint accepts a small JSON request:

```json
{"op": "list"}
{"op": "get", "key": "server_url"}
{"op": "set", "key": "server_url", "value": "https://api.example.com"}
{"op": "del", "key": "server_url"}
```

## Recommended clients

- **iOS / Android — "ESP BLE Provisioning"** (Espressif official app)
- **`esp-prov` Python tool** (in any ESP-IDF checkout under
  `tools/esp_prov/`) — useful for CI and headless setup
- **`esp-idf-provisioning-android` / `-ios`** SDKs if you ship a
  custom mobile app

## iOS "ESP BLE Provisioning" app — "Encrypted Communication" toggle

Espressif's iOS "ESP BLE Provisioning" app exposes a setting that the
app does **not** auto-negotiate with the device. It is on the Settings
screen reached via the gear icon in the upper-left of the device-list
screen, and is labelled **"Encrypted Communication"**:

| Toggle state | Wire protocol used by the app | Required device build |
|--------------|--------------------------------|------------------------|
| Off ("Unsecured") | plaintext protocomm — no handshake | `.prov_ble.security = WIFI_CFG_PROV_SECURITY_0` |
| On ("Secured")    | Security 2 (SRP6a) — app prompts for a username at connect time | `.prov_ble.security = WIFI_CFG_PROV_SECURITY_2` with a valid salt/verifier |

The toggle is sticky across sessions and is not adjusted based on what
the device advertises in its BLE service data. The app gives no in-app
indication that it exists or that it has to match the firmware.

### Confirmed behaviour

- **Security 0 device, app in "Unsecured" mode** — works. Credentials
  transfer in plaintext as expected.
- **Security 0 device, app in "Secured" mode** — the app hangs after
  the device is tapped. No PoP prompt appears, no Wi-Fi scan list is
  rendered, and there is no error toast or log entry that surfaces the
  mismatch. The fix is to flip the toggle to "Unsecured" and reopen the
  device.

### Not yet confirmed

The interaction between this toggle and **Security 1 (PoP)** has not
yet been validated against this app. Neither toggle position obviously
corresponds to Security 1 on the wire — "Unsecured" is plaintext and
"Secured" is SRP6a — so a Security 1 device may not be reachable from
this app at all. If you require Security 1 specifically, use the
Android app, `esp_prov`, or your own client built on the
`esp-idf-provisioning-*` SDKs until this is confirmed.

### Practical guidance

- Pick the security version at build time and document the matching
  toggle position in any user-facing setup instructions you ship.
- If you support a mixed fleet that uses different security versions,
  prefer the Android app or `esp_prov` for QA — the iOS toggle becomes
  a per-device manual step.
- The same caveat does not appear in Espressif's `esp_prov` Python
  tool: that one is told the security version via a CLI flag
  (`--sec_ver`) and will not silently hang on mismatch.

## Coexistence with Improv

`CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE` is mutually exclusive with
`CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING` — they cannot both be
enabled in a single firmware build. If you need to support both
ecosystems, ship two firmware variants. Improv Serial
(`CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL`) is independent of BLE and
remains safe to enable alongside Network Provisioning.

## ESP-IDF version requirements

- **Minimum**: ESP-IDF 5.4 (uses the in-tree `wifi_provisioning`
  component).
- **6.x**: works via the external `espressif/network_provisioning`
  managed component, declared in this library's `idf_component.yml`
  with an `idf_version >=6.0` rule. The implementation switches between
  the two via a thin compatibility shim in `esp_wifi_config_prov_ble.c`.
