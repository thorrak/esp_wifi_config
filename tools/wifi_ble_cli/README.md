# ESP WiFi Config BLE CLI Client

Python command-line tool to provision and inspect ESP32 devices running the
`esp_wifi_config` library's Network Provisioning (BLE scheme) backend.

This talks Espressif's standard protocomm protocol (protobuf-over-GATT) with
security version 0, 1, or 2, plus the optional `esp-wifi-config-*` JSON
endpoints exposed when the firmware sets
`wifi_cfg_prov_config_t.expose_library_endpoints = true`.

The protocomm protocol modules from
[esp-idf/tools/esp_prov](https://github.com/espressif/esp-idf/tree/release/v5.5/tools/esp_prov)
are vendored under `./esp_prov/` so the tool is self-contained — no IDF
checkout required at runtime.

## Installation

```bash
cd tools/wifi_ble_cli
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

Requirements: `bleak`, `click`, `protobuf`, `cryptography`. The last two are
required by the protocomm protocol itself (protobuf wire format + crypto for
sec1/sec2) and are not optional.

## Global options

All commands accept these (placed before the command name):

| Flag | Purpose |
|---|---|
| `-n, --name NAME` | BLE device name as advertised (e.g. `TiltBridge-E3F6B0`) |
| `--sec-ver {0,1,2}` | Protocomm security version (default: 1) |
| `--pop POP` | Proof of Possession string (security version 1) |
| `--sec2-user USER` | SRP6a username (security version 2) |
| `--sec2-pwd PWD`   | SRP6a password (security version 2) |
| `-v, --verbose` | Trace protocomm packets (encrypted bytes + decoded fields) |

## Commands

### Always available (standard provisioning endpoints)

```bash
# Find devices advertising over BLE
python wifi_ble_cli.py devices

# Read the device's protocol version + capability list
python wifi_ble_cli.py -n TiltBridge-XXXX --sec-ver 1 --pop 12345678 status

# Scan for nearby Wi-Fi APs as the device sees them
python wifi_ble_cli.py -n TiltBridge-XXXX --sec-ver 1 --pop 12345678 scan

# Send Wi-Fi credentials and wait for the device to report Connected
python wifi_ble_cli.py -n TiltBridge-XXXX --sec-ver 1 --pop 12345678 \
    provision MyWiFi MyPassword

# Read the device's current Wi-Fi connection state
python wifi_ble_cli.py -n TiltBridge-XXXX --sec-ver 1 --pop 12345678 wifi-status

# Clear stored credentials / re-enter provisioning mode
python wifi_ble_cli.py -n TiltBridge-XXXX --sec-ver 1 --pop 12345678 reset
python wifi_ble_cli.py -n TiltBridge-XXXX --sec-ver 1 --pop 12345678 reprov
```

### Library extension endpoints (require `expose_library_endpoints = true`)

These call the JSON endpoints the library adds beyond the standard
provisioning set. If the firmware wasn't built with them enabled, the
command will fail with a clear error.

```bash
python wifi_ble_cli.py -n ... lib-version          # IDF/lib/firmware versions
python wifi_ble_cli.py -n ... lib-capabilities     # enabled feature flags
python wifi_ble_cli.py -n ... network-policy       # retry/reconnect config

python wifi_ble_cli.py -n ... list-vars            # all custom variables
python wifi_ble_cli.py -n ... get-var KEY
python wifi_ble_cli.py -n ... set-var KEY VALUE
python wifi_ble_cli.py -n ... del-var KEY
```

## Security version examples

```bash
# No security — only useful for bench testing
python wifi_ble_cli.py -n MyDev --sec-ver 0 scan

# Security 1 (X25519 + AES-CTR + PoP)
python wifi_ble_cli.py -n MyDev --sec-ver 1 --pop 12345678 scan

# Security 2 (SRP6a + AES-GCM)
python wifi_ble_cli.py -n MyDev --sec-ver 2 \
    --sec2-user alice --sec2-pwd hunter2 scan
```

## Troubleshooting

### macOS Bluetooth permissions

The first time you run a BLE-touching command, macOS will prompt to grant
Bluetooth access to the terminal application. Approve it in
System Settings → Privacy & Security → Bluetooth.

Note: on macOS, BLE devices often appear with `name=None` from
`BleakScanner`. The vendored transport layer already falls back to
`advertisement_data.local_name`, so name-based discovery works regardless.

### Linux

You may need to add your user to the `bluetooth` group:

```bash
sudo usermod -a -G bluetooth $USER
```

### Windows

Ensure Bluetooth is enabled.

## How this compares to esp_prov

This tool is a thin click-based wrapper around esp_prov's protocol code with
extra commands for the library extension endpoints. If you only need
vanilla provisioning, esp_prov itself works fine; this tool exists so the
library has a self-contained test client that also exercises its
`esp-wifi-config-*` endpoints.
