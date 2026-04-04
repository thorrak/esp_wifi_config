---
sidebar_position: 2
title: REST API Reference
description: HTTP REST API endpoints for remote WiFi configuration
---

# REST API Reference

Base URL: `http://<device-ip>/api/wifi` (configurable via `api_base_path`)

## Authentication

If `enable_auth = true` in the HTTP config, all endpoints require HTTP Basic Auth:

```bash
curl -u admin:password http://192.168.4.1/api/wifi/status
```

## Endpoints

| Method | Endpoint | Description |
|---|---|---|
| GET | `/status` | Get connection status |
| GET | `/scan` | Scan available networks |
| GET | `/networks` | List saved networks |
| POST | `/networks` | Add new network |
| PUT | `/networks/:ssid` | Update network |
| DELETE | `/networks/:ssid` | Remove network |
| POST | `/connect` | Connect (auto or specific SSID) |
| POST | `/disconnect` | Disconnect |
| GET | `/ap/status` | Get AP status |
| GET | `/ap/config` | Get AP configuration |
| PUT | `/ap/config` | Update AP configuration |
| POST | `/ap/start` | Start SoftAP |
| POST | `/ap/stop` | Stop SoftAP |
| GET | `/vars` | List custom variables |
| PUT | `/vars/:key` | Set variable |
| DELETE | `/vars/:key` | Delete variable |
| POST | `/factory_reset` | Factory reset |

## Example Requests

### Get Status

```bash
curl http://192.168.4.1/api/wifi/status
```

Response:

```json
{
  "state": "connected",
  "ssid": "MyWiFi",
  "ip": "192.168.1.100",
  "gateway": "192.168.1.1",
  "netmask": "255.255.255.0",
  "dns": "192.168.1.1",
  "rssi": -65,
  "quality": 70,
  "channel": 6,
  "mac": "AA:BB:CC:DD:EE:FF",
  "hostname": "esp32-aabbcc",
  "uptime_ms": 123456,
  "ap_active": false
}
```

### Scan Networks

```bash
curl http://192.168.4.1/api/wifi/scan
```

Response:

```json
{
  "networks": [
    {"ssid": "MyWiFi", "rssi": -65, "auth": "WPA2"},
    {"ssid": "Neighbor", "rssi": -80, "auth": "WPA/WPA2"},
    {"ssid": "OpenNet", "rssi": -70, "auth": "OPEN"}
  ]
}
```

### Add Network

```bash
curl -X POST http://192.168.4.1/api/wifi/networks \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyWiFi", "password": "secret123", "priority": 10}'
```

### Connect

```bash
# Connect to a specific network
curl -X POST http://192.168.4.1/api/wifi/connect \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyWiFi"}'

# Auto-connect (highest priority saved network)
curl -X POST http://192.168.4.1/api/wifi/connect
```

### Delete Network

```bash
curl -X DELETE http://192.168.4.1/api/wifi/networks/MyWiFi
```

### AP Status

```bash
curl http://192.168.4.1/api/wifi/ap/status
```

Response:

```json
{
  "active": true,
  "ssid": "ESP32-AABBCC",
  "ip": "192.168.4.1",
  "channel": 1,
  "sta_count": 2,
  "clients": [
    {"mac": "AA:BB:CC:DD:EE:01", "ip": "192.168.4.2"},
    {"mac": "AA:BB:CC:DD:EE:02", "ip": "192.168.4.3"}
  ]
}
```

### Custom Variables

```bash
# List all variables
curl http://192.168.4.1/api/wifi/vars

# Set a variable
curl -X PUT http://192.168.4.1/api/wifi/vars/device_name \
  -H "Content-Type: application/json" \
  -d '{"value": "Living Room"}'

# Delete a variable
curl -X DELETE http://192.168.4.1/api/wifi/vars/device_name
```

Variables list response:

```json
{
  "vars": [
    {"key": "server_url", "value": "https://api.example.com"},
    {"key": "device_name", "value": "My ESP32"}
  ]
}
```

### Factory Reset

```bash
curl -X POST http://192.168.4.1/api/wifi/factory_reset
```

## Error Responses

All errors return a JSON object with an `error` field:

```json
{"error": "Error message"}
```

| HTTP Code | Description |
|---|---|
| 400 | Bad Request — Invalid JSON, missing required field |
| 401 | Unauthorized — Authentication required |
| 404 | Not Found — Network or variable does not exist |
| 500 | Internal Error — Operation failed |

## CORS

The REST API includes CORS headers, allowing browser-based clients to access the endpoints directly.
