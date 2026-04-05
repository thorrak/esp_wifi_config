---
sidebar_position: 3
title: BLE Protocol Reference
description: BLE GATT service UUIDs, characteristics, JSON command/response format
---

# BLE Protocol Reference

This page documents the wire protocol for the custom BLE GATT service. For setup instructions, see [BLE GATT Provisioning](../provisioning/ble-gatt.md).

## Service & Characteristics

| UUID | Name | Properties | Description |
|---|---|---|---|
| 0xFFE0 | WiFi Service | — | Main service |
| 0xFFE1 | Status | Read, Notify | Current WiFi status (JSON) |
| 0xFFE2 | Command | Write | Send JSON command |
| 0xFFE3 | Response | Notify | Command response (JSON) |

The device advertises the WiFi Service UUID (`0xFFE0`), allowing BLE scanners to filter by service UUID.

## Command Format

Send JSON to the Command characteristic (0xFFE2). Parameters are passed in a `"params"` object:

```json
{"cmd": "<command_name>", "params": { ... }}
```

Responses are sent as notifications on the Response characteristic (0xFFE3). Large responses are automatically chunked across multiple notifications.

## Commands

| Command | Params | Description |
|---|---|---|
| `get_status` | (none) | Get connection status |
| `scan` | (none) | Scan available networks |
| `list_networks` | (none) | List saved networks |
| `add_network` | `ssid`, `password`?, `priority`? | Add new network |
| `update_network` | `ssid`, `password`?, `priority`? | Update saved network |
| `del_network` | `ssid` | Remove network |
| `connect` | `ssid`? | Connect (auto or specific SSID) |
| `disconnect` | (none) | Disconnect |
| `get_ap_status` | (none) | Get AP status |
| `start_ap` | `ssid`?, `password`? | Start SoftAP |
| `stop_ap` | (none) | Stop SoftAP |
| `get_var` | `key` | Get variable |
| `set_var` | `key`, `value` | Set variable |
| `list_vars` | (none) | List all variables |
| `del_var` | `key` | Delete variable |
| `factory_reset` | (none) | Factory reset |

## Example Commands

```json
{"cmd": "get_status"}
{"cmd": "scan"}
{"cmd": "list_networks"}
{"cmd": "add_network", "params": {"ssid": "MyWiFi", "password": "secret", "priority": 10}}
{"cmd": "update_network", "params": {"ssid": "MyWiFi", "password": "newpass", "priority": 5}}
{"cmd": "del_network", "params": {"ssid": "MyWiFi"}}
{"cmd": "connect", "params": {"ssid": "MyWiFi"}}
{"cmd": "get_var", "params": {"key": "device_name"}}
{"cmd": "set_var", "params": {"key": "device_name", "value": "My ESP32"}}
{"cmd": "list_vars"}
{"cmd": "del_var", "params": {"key": "device_name"}}
{"cmd": "factory_reset"}
```

## Response Format

### Successful Response

```json
{"status": "ok", "data": { ... }}
```

### Error Response

```json
{"status": "error", "error": "Error message"}
```

## Response Examples

### get_status

```json
{
  "status": "ok",
  "data": {
    "state": "connected",
    "ssid": "MyWiFi",
    "rssi": -65,
    "quality": 70,
    "ip": "192.168.1.100",
    "channel": 6,
    "netmask": "255.255.255.0",
    "gateway": "192.168.1.1",
    "dns": "192.168.1.1",
    "mac": "AA:BB:CC:DD:EE:FF",
    "hostname": "esp32-aabbcc",
    "uptime_ms": 123456,
    "ap_active": false
  }
}
```

### get_ap_status

```json
{
  "status": "ok",
  "data": {
    "active": true,
    "ssid": "ESP32-AABBCC",
    "ip": "192.168.4.1",
    "channel": 1,
    "sta_count": 2
  }
}
```

### list_vars

```json
{
  "status": "ok",
  "data": {
    "vars": [
      {"key": "server_url", "value": "https://api.example.com"},
      {"key": "device_name", "value": "My ESP32"}
    ]
  }
}
```
