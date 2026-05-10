---
sidebar_position: 3
title: BLE Protocol Reference
description: ESP-IDF Network Provisioning protocol used over BLE, plus custom protocomm endpoints
---

# BLE Protocol Reference

:::caution Removed in 0.1.0
The custom JSON-over-GATT BLE service (UUID `0xFFE0` /
characteristics `0xFFE1`–`0xFFE3`) that this page used to document has
been **removed** in favour of ESP-IDF's official Wi-Fi Provisioning
protocol. See [MIGRATION.md][migrate] for the protocol-level migration
plan and the steps for updating downstream client tools.
:::

[migrate]: https://github.com/thorrak/esp_wifi_config/blob/main/MIGRATION.md

## What runs over BLE now

The library wraps Espressif's `wifi_prov_mgr` with the BLE scheme. The
on-air protocol is the same one Espressif's official mobile apps speak,
so any of the following work out of the box:

- **iOS / Android**: "ESP BLE Provisioning" by Espressif Systems
- **Python**: `esp_prov` tool (in `tools/esp_prov/` of any IDF checkout)
- **Custom apps**: `esp-idf-provisioning-android` /
  `esp-idf-provisioning-ios` SDKs

For the full Wi-Fi Provisioning over BLE specification (advertising
format, GAP service UUIDs, protocomm framing) see
[Espressif's docs][espressif-prov].

[espressif-prov]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/provisioning/provisioning.html

## Library-specific protocomm endpoints

Alongside the standard `prov-config`, `prov-scan`, `prov-session`, etc.,
the library registers four custom endpoints. These give the
provisioning client read access to the higher-level state the library
maintains, and read/write access to the custom variable store.

| Endpoint | Direction | Notes |
|----------|-----------|-------|
| `esp-wifi-config-version` | read | JSON: `{lib, idf, app, fw_version, chip}` |
| `esp-wifi-config-capabilities` | read | JSON: `{capabilities[], max_networks, max_vars}` |
| `esp-wifi-config-vars` | read/write | JSON request, see schema below |
| `esp-wifi-config-network-policy` | read | JSON: `{provisioning_mode, retries, …}` |

### `esp-wifi-config-vars` request/response schema

```jsonc
// list every saved variable
→ {"op": "list"}
← {"vars": [{"k": "server_url", "v": "..."}, …]}

// fetch one
→ {"op": "get", "key": "server_url"}
← {"key": "server_url", "value": "..."}

// upsert
→ {"op": "set", "key": "server_url", "value": "https://api.example.com"}
← {"ok": true}

// delete
→ {"op": "del", "key": "server_url"}
← {"ok": true}
```

Errors are returned as `{"error": "<reason>"}` (e.g. `not_found`,
`store_full`, `bad_json`, `unknown_op`).

## What was intentionally **not** ported

The old custom protocol exposed a number of pre-Wi-Fi management
operations directly over BLE (`scan`, `add_network`, `connect`,
`start_ap`, `factory_reset`, etc.). Most of those are already covered by
the standard provisioning protocol (`prov-scan` and `prov-config`); the
remainder (factory reset, AP control) are intentionally left to the
HTTP/REST API or local UI — the BLE provisioning surface is meant to
get the device on the network, not act as a full management backdoor.

If your application needs a richer command surface during provisioning,
register additional protocomm endpoints by extending
`esp_wifi_config_prov_ble.c`.
