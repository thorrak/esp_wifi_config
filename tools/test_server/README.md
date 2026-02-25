# Test HTTP Server

Mock HTTP server that emulates the `esp_wifi_manager` REST API, allowing Web UI development without physical ESP32 hardware. All endpoints under `/api/wifi` are supported with in-memory state.

## Setup

```bash
pip install -r tools/test_server/requirements.txt
```

## Usage

```bash
python3 tools/test_server/test_server.py [options]
```

| Option | Description |
|--------|-------------|
| `--port PORT` | Listen port (default: 8080) |
| `--config FILE` | JSON config file for custom networks and variables |
| `--no-aps` | Start with no scan results |
| `--no-vars` | Start with no variables |

By default the server starts with 3 fake APs in scan results and 2 sample variables.

## Config file

Use `--config` to customize the initial scan results and variables. See `config.sample.json` for the format. Both keys are optional -- omitting a key uses the defaults, while providing an empty list/object starts that category empty.

```json
{
  "networks": [
    {"ssid": "MyWiFi", "rssi": -42, "auth": "WPA2"}
  ],
  "variables": {
    "device_name": "My Device"
  }
}
```
