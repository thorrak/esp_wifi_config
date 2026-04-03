# WiFi Config - Web UI Example

Example demonstrating WiFi Config with embedded Web UI.

## Features

- Modern responsive Web UI
- Dark mode support (auto-detect)
- Captive portal for initial setup
- Embedded files (~10KB gzipped)

## Build & Flash

**Note**: Web UI is pre-built and embedded in the component. Just enable it in config.

```bash
cd examples/with_webui
idf.py build flash monitor
```

If you want to modify the frontend, see `with_webui_customize` example.

## Usage

### First Setup (No Saved Networks)

1. Device starts in AP mode: `ESP32-Setup-XXXXXX`
2. Connect to this WiFi network (open, no password)
3. Captive portal opens automatically, or go to `http://192.168.4.1/`
4. Use Web UI to scan and connect to your WiFi

### After Connection

Access the Web UI at:
- `http://<device-ip>/` (shown in serial monitor)

## Web UI Features

| Feature | Description |
|---------|-------------|
| Status | Connection status, IP, signal strength |
| Scan | Find available networks |
| Connect | Connect to any network |
| Saved | Manage saved networks with priority |
| Settings | AP config, factory reset |

## Screenshot

```
+----------------------------------+
|  ESP WiFi Config                |
+----------------------------------+
|  [Status]                        |
|  Connected to: MyWiFi            |
|  IP: 192.168.1.100               |
|  Signal: 85%                     |
+----------------------------------+
|  [Available Networks]     [Scan] |
|  - MyWiFi (WPA2)        [Connect]|
|  - Guest (Open)         [Connect]|
+----------------------------------+
|  [Saved Networks]                |
|  - MyWiFi (priority: 10)         |
|  - Office (priority: 5)          |
+----------------------------------+
```
