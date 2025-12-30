# ESP WiFi Manager - REST API

Base URL: `http://<device-ip>/wifi` (configurable via `api_base_path`)

## Authentication

If `enable_auth = true`, all endpoints require Basic Auth:

```bash
curl -u admin:password http://192.168.4.1/wifi/status
```

---

## Networks

### List Networks

Get list of configured networks.

```http
GET /networks
```

**Response:**
```json
{
  "networks": [
    {"ssid": "MyWiFi", "priority": 10},
    {"ssid": "Office", "priority": 5}
  ]
}
```

**curl:**
```bash
curl http://192.168.4.1/wifi/networks
```

---

### Add Network

Add a new WiFi network.

```http
POST /networks
Content-Type: application/json
```

**Request Body:**
```json
{
  "ssid": "NetworkName",
  "password": "password123",
  "priority": 10
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| ssid | string | Yes | WiFi network name (max 31 chars) |
| password | string | No | Password (max 63 chars) |
| priority | number | No | Priority (0-255, higher = preferred) |

**Response:**
- `200 OK`: `{"status": "ok"}`
- `400 Bad Request`: Network already exists or missing ssid

**curl:**
```bash
# Add network with password
curl -X POST http://192.168.4.1/wifi/networks \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyWiFi", "password": "password123", "priority": 10}'

# Add open network (no password)
curl -X POST http://192.168.4.1/wifi/networks \
  -H "Content-Type: application/json" \
  -d '{"ssid": "OpenNetwork"}'
```

---

### Update Network

Update password or priority of a configured network.

```http
PUT /networks/:ssid
Content-Type: application/json
```

**Request Body:**
```json
{
  "password": "newpassword",
  "priority": 5
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| password | string | No | New password |
| priority | number | No | New priority |

**Response:**
- `200 OK`: `{"status": "ok"}`
- `404 Not Found`: Network does not exist

**curl:**
```bash
# Change password
curl -X PUT http://192.168.4.1/wifi/networks/MyWiFi \
  -H "Content-Type: application/json" \
  -d '{"password": "newpassword123"}'

# Change priority
curl -X PUT http://192.168.4.1/wifi/networks/MyWiFi \
  -H "Content-Type: application/json" \
  -d '{"priority": 20}'

# Change both
curl -X PUT http://192.168.4.1/wifi/networks/MyWiFi \
  -H "Content-Type: application/json" \
  -d '{"password": "newpass", "priority": 15}'
```

---

### Delete Network

Delete a configured WiFi network.

```http
DELETE /networks/:ssid
```

**Response:**
- `200 OK`: `{"status": "ok"}`
- `404 Not Found`: Network does not exist

**curl:**
```bash
# Delete network "MyWiFi"
curl -X DELETE http://192.168.4.1/wifi/networks/MyWiFi

# Delete network with space in name (URL encode)
curl -X DELETE http://192.168.4.1/wifi/networks/My%20Network
```

---

## Connection

### Connect

Connect to a WiFi network.

```http
POST /connect
Content-Type: application/json
```

**Request Body (optional):**
```json
{
  "ssid": "NetworkName"
}
```

- If `ssid` provided: Connect to specific network (must be already added)
- If no body: Auto-connect by priority

**Response:**
- `200 OK`: `{"status": "ok"}` - Connection started (async)

**curl:**
```bash
# Connect to specific network
curl -X POST http://192.168.4.1/wifi/connect \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyWiFi"}'

# Auto-connect (choose highest priority network)
curl -X POST http://192.168.4.1/wifi/connect
```

---

### Disconnect

Disconnect from WiFi.

```http
POST /disconnect
```

**Response:**
- `200 OK`: `{"status": "ok"}`

**curl:**
```bash
curl -X POST http://192.168.4.1/wifi/disconnect
```

---

## Status & Scan

### Get Status

Get current connection status.

```http
GET /status
```

**Response:**
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
  "hostname": "photoframe",
  "uptime_ms": 123456,
  "ap_active": false
}
```

| Field | Description |
|-------|-------------|
| state | `disconnected`, `connecting`, `connected` |
| quality | 0-100% (calculated from RSSI) |
| uptime_ms | Connection uptime in milliseconds |
| ap_active | Whether SoftAP is running |

**curl:**
```bash
curl http://192.168.4.1/wifi/status
```

---

### Scan Networks

Scan for available WiFi networks.

```http
GET /scan
```

**Response:**
```json
{
  "networks": [
    {"ssid": "MyWiFi", "rssi": -65, "auth": "WPA2"},
    {"ssid": "Neighbor", "rssi": -80, "auth": "WPA/WPA2"},
    {"ssid": "OpenNet", "rssi": -70, "auth": "OPEN"}
  ]
}
```

| auth values | Description |
|-------------|-------------|
| OPEN | No password |
| WEP | WEP encryption |
| WPA | WPA-PSK |
| WPA2 | WPA2-PSK |
| WPA/WPA2 | Mixed mode |
| WPA3 | WPA3-PSK |

**curl:**
```bash
curl http://192.168.4.1/wifi/scan
```

> Note: Scan may take 2-5 seconds

---

## SoftAP (Access Point)

### Get AP Status

Get SoftAP status.

```http
GET /ap/status
```

**Response:**
```json
{
  "active": true,
  "ssid": "PhotoFrame-Setup",
  "ip": "192.168.4.1",
  "channel": 1,
  "sta_count": 2,
  "clients": [
    {"mac": "AA:BB:CC:DD:EE:01", "ip": "192.168.4.2"},
    {"mac": "AA:BB:CC:DD:EE:02", "ip": "192.168.4.3"}
  ]
}
```

**curl:**
```bash
curl http://192.168.4.1/wifi/ap/status
```

---

### Get AP Config

Get SoftAP configuration.

```http
GET /ap/config
```

**Response:**
```json
{
  "ssid": "PhotoFrame-Setup",
  "password": "",
  "channel": 1,
  "max_connections": 4,
  "hidden": false,
  "ip": "192.168.4.1",
  "netmask": "255.255.255.0",
  "gateway": "192.168.4.1",
  "dhcp_start": "192.168.4.2",
  "dhcp_end": "192.168.4.10"
}
```

**curl:**
```bash
curl http://192.168.4.1/wifi/ap/config
```

---

### Update AP Config

Update SoftAP configuration (saved to NVS, applied on next start).

```http
PUT /ap/config
Content-Type: application/json
```

**Request Body:**
```json
{
  "ssid": "MyDevice-AP",
  "password": "secure123",
  "channel": 6,
  "hidden": false
}
```

All fields are optional, only provided fields are updated.

**curl:**
```bash
# Change AP name and set password
curl -X PUT http://192.168.4.1/wifi/ap/config \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyDevice", "password": "secret123"}'

# Change IP range
curl -X PUT http://192.168.4.1/wifi/ap/config \
  -H "Content-Type: application/json" \
  -d '{"ip": "10.0.0.1", "dhcp_start": "10.0.0.2", "dhcp_end": "10.0.0.20"}'
```

---

### Start AP

Start SoftAP mode.

```http
POST /ap/start
Content-Type: application/json
```

**Request Body (optional):**
```json
{
  "ssid": "TempAP",
  "password": "temp123"
}
```

- If body provided: Temporarily override ssid/password
- If no body: Use saved config

**curl:**
```bash
# Start with saved config
curl -X POST http://192.168.4.1/wifi/ap/start

# Start with temporary name/password
curl -X POST http://192.168.4.1/wifi/ap/start \
  -H "Content-Type: application/json" \
  -d '{"ssid": "QuickSetup", "password": ""}'
```

---

### Stop AP

Stop SoftAP mode.

```http
POST /ap/stop
```

**curl:**
```bash
curl -X POST http://192.168.4.1/wifi/ap/stop
```

---

## Custom Variables

Store custom key-value pairs (persisted in NVS).

### List Variables

```http
GET /vars
```

**Response:**
```json
{
  "vars": [
    {"key": "device_name", "value": "Living Room Frame"},
    {"key": "timezone", "value": "Asia/Ho_Chi_Minh"}
  ]
}
```

**curl:**
```bash
curl http://192.168.4.1/wifi/vars
```

---

### Set Variable

Create or update a variable.

```http
PUT /vars/:key
Content-Type: application/json
```

**Request Body:**
```json
{
  "value": "new value"
}
```

**curl:**
```bash
# Set device name
curl -X PUT http://192.168.4.1/wifi/vars/device_name \
  -H "Content-Type: application/json" \
  -d '{"value": "Kitchen Frame"}'

# Set timezone
curl -X PUT http://192.168.4.1/wifi/vars/timezone \
  -H "Content-Type: application/json" \
  -d '{"value": "Asia/Ho_Chi_Minh"}'
```

---

### Delete Variable

```http
DELETE /vars/:key
```

**Response:**
- `200 OK`: `{"status": "ok"}`
- `404 Not Found`: Variable does not exist

**curl:**
```bash
curl -X DELETE http://192.168.4.1/wifi/vars/device_name
```

---

## Configuration Options

### stop_ap_on_connect

When `stop_ap_on_connect = true` in config:
- After Station connects successfully (got IP)
- SoftAP automatically stops
- Device switches to Station-only mode

```c
wifi_manager_config_t cfg = {
    .enable_captive_portal = true,
    .stop_ap_on_connect = true,  // Auto-stop AP on successful connection
    // ...
};
```

Flow:
1. No network available → Start AP (captive portal)
2. User configures WiFi via AP
3. Connection successful → AP auto-stops
4. Device runs in Station-only mode

---

## Example Workflows

### Configure WiFi (with captive portal)

```bash
# 1. Connect to device AP
#    SSID: PhotoFrame-Setup (default)

# 2. Scan for networks
curl http://192.168.4.1/wifi/scan

# 3. Add new network
curl -X POST http://192.168.4.1/wifi/networks \
  -H "Content-Type: application/json" \
  -d '{"ssid": "HomeWiFi", "password": "secret123", "priority": 10}'

# 4. Connect
curl -X POST http://192.168.4.1/wifi/connect \
  -H "Content-Type: application/json" \
  -d '{"ssid": "HomeWiFi"}'

# 5. AP auto-stops (if stop_ap_on_connect = true)
# 6. Check status via new IP
curl http://192.168.1.100/wifi/status
```

### Change WiFi Password

```bash
# Update password (device will reconnect with new password)
curl -X PUT http://192.168.1.100/wifi/networks/HomeWiFi \
  -H "Content-Type: application/json" \
  -d '{"password": "newpassword456"}'

# Reconnect
curl -X POST http://192.168.1.100/wifi/connect
```

### Add Backup Network

```bash
# Add network with lower priority
curl -X POST http://192.168.1.100/wifi/networks \
  -H "Content-Type: application/json" \
  -d '{"ssid": "BackupWiFi", "password": "backup123", "priority": 5}'

# When HomeWiFi (priority 10) unavailable, device auto-connects to BackupWiFi
```

### Setup SoftAP with Password

```bash
# Configure AP
curl -X PUT http://192.168.4.1/wifi/ap/config \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyDevice-Setup", "password": "setup123", "channel": 6}'

# Stop and restart to apply
curl -X POST http://192.168.4.1/wifi/ap/stop
curl -X POST http://192.168.4.1/wifi/ap/start
```

---

## Error Responses

All errors return JSON format:

```json
{
  "error": "Error message"
}
```

| HTTP Code | Description |
|-----------|-------------|
| 400 | Bad Request - Invalid JSON, missing field |
| 401 | Unauthorized - Auth required |
| 404 | Not Found - Network/Variable does not exist |
| 500 | Internal Error - Operation failed |
