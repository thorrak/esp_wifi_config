# WiFi Config - Customizable Web UI Example

Example demonstrating WiFi Config with customizable frontend from LittleFS.

## Features

- Custom frontend files served from LittleFS partition
- Replace UI without recompiling firmware
- Fallback to embedded Web UI if files not found
- 512KB LittleFS partition for frontend files

## How It Works

1. WiFi Config first checks `/littlefs/` for frontend files
2. If found, serves custom files from LittleFS
3. If not found, falls back to embedded Web UI

## Build & Flash

### Step 1: Copy and Build Frontend

```bash
# Copy frontend folder to this example
cp -r ../../../frontend ./frontend

# Build frontend
cd frontend
npm install
npm run build
cd ..

# Copy built files to www folder
rm -rf www/*
cp frontend/dist/index.html www/
mkdir -p www/assets
cp frontend/dist/assets/*.gz www/assets/
```

### Step 2: Build and Flash Firmware

```bash
idf.py build flash monitor
```

This will:
1. Build the firmware with embedded Web UI
2. Create LittleFS image from `www/` directory
3. Flash everything including custom frontend

## File Structure

```
www/
├── index.html              # Main HTML file
└── assets/
    ├── app.js.gz           # JavaScript (gzipped)
    └── index.css.gz        # Styles (gzipped)
```

## Customizing the Frontend

### Option 1: Modify Preact Components

```bash
# Edit frontend source files
vim frontend/src/components/StatusCard.tsx
vim frontend/src/App.tsx

# Rebuild
cd frontend && npm run build && cd ..

# Copy to www
cp frontend/dist/index.html www/
cp frontend/dist/assets/*.gz www/assets/

# Flash
idf.py build flash
```

### Option 2: Create Your Own Frontend

Create any frontend (React, Vue, vanilla JS) that uses the REST API:

```
www/
└── index.html    # Single file with inline JS/CSS
```

Or with separate assets:

```
www/
├── index.html
└── assets/
    ├── app.js.gz        # Gzip for smaller size
    └── style.css.gz
```

### Option 3: Update Only LittleFS Partition

After initial flash, update only the frontend:

```bash
# Create new LittleFS image
python $IDF_PATH/components/littlefs/mklittlefs.py -c www -b 4096 -p 256 -s 524288 storage.bin

# Flash only LittleFS partition (check offset in partition table)
esptool.py --port /dev/ttyUSB0 write_flash 0x110000 storage.bin
```

## REST API Reference

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | /api/wifi/status | WiFi status |
| GET | /api/wifi/scan | Scan networks |
| GET | /api/wifi/networks | Saved networks |
| POST | /api/wifi/networks | Add network |
| DELETE | /api/wifi/networks/:ssid | Delete network |
| PUT | /api/wifi/networks/:ssid | Update network |
| POST | /api/wifi/connect | Connect |
| POST | /api/wifi/disconnect | Disconnect |
| POST | /api/wifi/factory_reset | Factory reset |
| GET | /api/wifi/ap/status | AP status |
| GET | /api/wifi/ap/config | AP config |
| PUT | /api/wifi/ap/config | Update AP config |
| POST | /api/wifi/ap/start | Start AP |
| POST | /api/wifi/ap/stop | Stop AP |

### Example API Usage

```javascript
// Get status
const status = await fetch('/api/wifi/status').then(r => r.json());
// { state: "connected", ssid: "MyWiFi", ip: "192.168.1.100", ... }

// Scan networks
const scan = await fetch('/api/wifi/scan').then(r => r.json());
// { networks: [{ ssid: "MyWiFi", rssi: -45, auth: "WPA2" }, ...] }

// Add network
await fetch('/api/wifi/networks', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
        ssid: 'MyWiFi',
        password: 'secret123',
        priority: 10
    })
});

// Connect
await fetch('/api/wifi/connect', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ssid: 'MyWiFi'})
});
```

## Tips

1. **Use Gzip**: Smaller files = faster load. Name files with `.gz` extension
2. **Keep it Small**: LittleFS has 512KB, aim for <100KB total
3. **Mobile First**: Test on phones for captive portal experience
4. **Cache Busting**: Use versioned filenames for assets

## Partition Table

| Name | Type | Size |
|------|------|------|
| nvs | data | 24KB |
| phy_init | data | 4KB |
| factory | app | 1MB |
| storage | littlefs | 512KB |
