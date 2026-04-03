# ESP WiFi Config BLE CLI Client

Python command-line tool to configure ESP32 WiFi settings over Bluetooth Low Energy.

## Installation

```bash
cd tools/wifi_ble_cli
pip install -r requirements.txt
```

## Usage

### Scan for devices

```bash
python wifi_ble_cli.py devices
```

### Get WiFi status

```bash
python wifi_ble_cli.py status
```

### Scan available WiFi networks

```bash
python wifi_ble_cli.py scan
```

### Add a WiFi network

```bash
python wifi_ble_cli.py add "MyNetwork" "MyPassword"
python wifi_ble_cli.py add "MyNetwork" "MyPassword" --priority 10
```

### List saved networks

```bash
python wifi_ble_cli.py list
```

### Connect to WiFi

```bash
python wifi_ble_cli.py connect           # Auto-connect to saved network
python wifi_ble_cli.py connect "MySSID"  # Connect to specific network
```

### Delete a network

```bash
python wifi_ble_cli.py delete "MyNetwork"
```

### AP Mode

```bash
python wifi_ble_cli.py ap-status
python wifi_ble_cli.py start-ap
python wifi_ble_cli.py stop-ap
```

### Custom variables

```bash
python wifi_ble_cli.py set-var mqtt_host "192.168.1.100"
python wifi_ble_cli.py get-var mqtt_host
```

### Factory reset

```bash
python wifi_ble_cli.py factory-reset
```

## Options

- `-d, --device ADDRESS` - Connect directly to device by MAC/UUID
- `-n, --name PREFIX` - Device name prefix to scan (default: "ESP32-WiFi")

```bash
# Connect to specific device
python wifi_ble_cli.py -d "AA:BB:CC:DD:EE:FF" status

# Scan for devices with custom prefix
python wifi_ble_cli.py -n "MyDevice" status
```

## Troubleshooting

### macOS

If you get permission errors, ensure Bluetooth is enabled and the terminal has Bluetooth access in System Preferences > Security & Privacy > Privacy > Bluetooth.

### Linux

You may need to run with `sudo` or add your user to the `bluetooth` group:

```bash
sudo usermod -a -G bluetooth $USER
```

### Windows

Ensure Bluetooth is enabled and paired devices are discoverable.
