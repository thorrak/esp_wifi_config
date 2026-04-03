# WiFi Config - CLI Example

Example demonstrating WiFi Config with CLI interface.

## Features

- Interactive CLI via ESP Console REPL
- All WiFi Config commands available
- USB Serial JTAG console (ESP32-S3)

## Build & Flash

```bash
cd examples/with_cli
idf.py build flash monitor
```

## CLI Commands

| Command | Description |
|---------|-------------|
| `wifi_status` | Show current WiFi status |
| `wifi_scan` | Scan for available networks |
| `wifi_list` | List saved networks |
| `wifi_add <ssid> [password] [-p priority]` | Add network |
| `wifi_del <ssid>` | Delete saved network |
| `wifi_connect [ssid]` | Connect to network |
| `wifi_disconnect` | Disconnect from WiFi |
| `wifi_ap_start` | Start access point |
| `wifi_ap_stop` | Stop access point |
| `wifi_reset` | Factory reset |
| `wifi_var_get <key>` | Get variable |
| `wifi_var_set <key> <value>` | Set variable |

## Example Usage

```
esp32> wifi_scan
Scanning...
Found 5 networks:
SSID                             RSSI   Auth
-------------------------------- ------ --------
MyHomeWiFi                       -45    WPA2
OfficeNetwork                    -60    WPA2
GuestWiFi                        -72    OPEN

esp32> wifi_add MyHomeWiFi mypassword123 -p 10
Added: MyHomeWiFi

esp32> wifi_connect
Connecting...

esp32> wifi_status
State:    connected
SSID:     MyHomeWiFi
IP:       192.168.1.100
Gateway:  192.168.1.1
RSSI:     -45 dBm (90%)
Channel:  6
MAC:      AA:BB:CC:DD:EE:FF
AP:       inactive
```
