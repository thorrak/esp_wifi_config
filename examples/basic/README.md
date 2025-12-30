# ESP WiFi Manager - Basic Example

This example demonstrates basic usage of the ESP WiFi Manager component.

## Features Demonstrated

- Initialize WiFi Manager with default networks
- Enable HTTP REST API for remote configuration
- Enable captive portal for initial WiFi setup
- Subscribe to WiFi events (connected, disconnected, got IP)
- Monitor connection status

## Hardware Required

Any ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, or ESP32-H2 development board.

## Build and Flash

```bash
# Set target (optional, defaults to esp32s3)
idf.py set-target esp32s3

# Configure (optional)
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

## Configuration

Before building, you may want to edit `main.c` to set your WiFi credentials:

```c
.default_networks = (wifi_network_t[]){
    {"YourWiFi", "YourPassword", 10},
    {"BackupWiFi", "BackupPassword", 5},
},
```

Or leave the defaults and configure via the captive portal after flashing.

## Usage

### Option 1: Pre-configured Networks

If you set valid WiFi credentials in the code, the device will:
1. Boot and attempt to connect to configured networks
2. Connect to the highest priority network available
3. Enable HTTP API at `http://<device-ip>/api/wifi/`

### Option 2: Captive Portal Setup

If no valid networks are configured or none are available:
1. Device starts SoftAP mode with SSID "ESP32-Setup"
2. Connect your phone/computer to "ESP32-Setup" network
3. Open browser to `http://192.168.4.1/api/wifi/scan` to see available networks
4. Add a network via `POST http://192.168.4.1/api/wifi/networks`
5. Connect via `POST http://192.168.4.1/api/wifi/connect`
6. Device connects and AP automatically stops

## REST API Quick Reference

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/wifi/status` | Get connection status |
| GET | `/api/wifi/scan` | Scan available networks |
| GET | `/api/wifi/networks` | List saved networks |
| POST | `/api/wifi/networks` | Add new network |
| POST | `/api/wifi/connect` | Connect to network |
| POST | `/api/wifi/disconnect` | Disconnect |

### Example: Add and Connect to Network

```bash
# Scan for networks
curl http://192.168.4.1/api/wifi/scan

# Add network
curl -X POST http://192.168.4.1/api/wifi/networks \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyWiFi", "password": "secret123", "priority": 10}'

# Connect
curl -X POST http://192.168.4.1/api/wifi/connect \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyWiFi"}'

# Check status (use new IP after connection)
curl http://192.168.1.xxx/api/wifi/status
```

## Expected Output

```
I (xxx) wifi_example: Starting WiFi Manager example
I (xxx) wifi_mgr: WiFi Manager initialized
I (xxx) wifi_example: WiFi Manager initialized
I (xxx) wifi_example: HTTP API available at http://<device-ip>/api/wifi/
I (xxx) wifi_example: Waiting for WiFi connection...
I (xxx) wifi_example: WiFi connected to MyWiFi (RSSI: -45 dBm, Channel: 6)
I (xxx) wifi_example: Got IP address
I (xxx) wifi_example: IP: 192.168.1.100
I (xxx) wifi_example: Gateway: 192.168.1.1
I (xxx) wifi_example: WiFi connected successfully!
I (xxx) wifi_example: Connected to MyWiFi - Signal: 78% - Uptime: 5000 ms
```

## Troubleshooting

### Cannot connect to AP
- Check that your WiFi credentials are correct
- Ensure the network is 2.4GHz (ESP32 doesn't support 5GHz)
- Try moving closer to the access point

### Captive portal not appearing
- Manually navigate to `http://192.168.4.1`
- Check that you're connected to the ESP32's AP
- Some phones may disconnect from AP without internet

### HTTP API not responding
- Ensure `http.enable = true` in config
- Check the device's IP address in serial output
- Verify firewall isn't blocking the connection
