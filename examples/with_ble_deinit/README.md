# WiFi Config - BLE Soft Deinit Example (NimBLE)

Demonstrates the "service-only" BLE mode where the application owns the NimBLE stack. The WiFi Config registers its GATT service for provisioning, then cleanly removes it on deinit while the NimBLE stack keeps running for the application's own use.

Two post-provisioning use cases are demonstrated (selectable at compile time):

| Mode | `EXAMPLE_MODE` | What it does after deinit |
|------|----------------|--------------------------|
| **GATT service** (default) | `MODE_GATT_SERVICE` | Registers app's own 0xAA00 service and advertises |
| **Scan only** | `MODE_SCAN_ONLY` | Passive BLE scan, logs nearby devices (no advertising) |

## Use Case

Many applications need BLE for more than just WiFi provisioning — sensor data streaming, device control, beacon scanning, etc. With the default BLE mode, calling `wifi_cfg_deinit()` tears down the entire NimBLE stack, making BLE unusable. Service-only mode solves this by letting the application own the stack lifecycle.

## Flow

```
1. App initializes NimBLE stack
2. wifi_cfg_init() detects NimBLE already running
   → registers 0xFFE0 GATT service only (service-only mode)
3. User provisions WiFi via BLE
4. wifi_cfg_deinit() removes 0xFFE0 service, leaves NimBLE running
5a. (GATT mode)  App registers its own 0xAA00 GATT service and advertises
5b. (Scan mode)  App starts passive BLE scan, logs discovered devices
```

## Build & Flash

```bash
cd examples/with_ble_deinit
idf.py build flash monitor
```

This example is configured for ESP32-S3 with NimBLE. To change the target:

```bash
idf.py set-target esp32c3  # or esp32, esp32c6, etc.
```

To switch modes, change the `EXAMPLE_MODE` define at the top of `main/main.c`:

```c
#define EXAMPLE_MODE  MODE_GATT_SERVICE  // or MODE_SCAN_ONLY
```

## Usage

1. Flash the device and open the serial monitor
2. Use the [Python BLE CLI](../../tools/wifi_ble_cli/) to provision WiFi:

```bash
cd tools/wifi_ble_cli
python wifi_ble_cli.py scan
python wifi_ble_cli.py add "MyWiFi" "password123"
python wifi_ble_cli.py connect
```

3. Once WiFi connects (or after 60s timeout), the WiFi Config deinitializes and the app takes over BLE

**GATT service mode:**

4. The device advertises as "ESP32-App" with a custom 0xAA00 GATT service
5. Use any BLE scanner app to verify the 0xFFE0 service is gone and 0xAA00 is present

**Scan-only mode:**

4. The device runs a passive BLE scan every 15 seconds
5. Discovered devices are logged to the serial console with address, RSSI, and name

## Expected Output

### GATT Service Mode

```
I (xxx) ble_deinit_example: === BLE Soft Deinit Example ===
I (xxx) ble_deinit_example: Step 1: Initializing NimBLE stack (app-owned)
I (xxx) ble_deinit_example: NimBLE stack running
I (xxx) ble_deinit_example: Step 2: Initializing WiFi Config (service-only BLE mode)
I (xxx) wifi_cfg_ble_nb: NimBLE stack already running, registering service only
I (xxx) ble_deinit_example: WiFi Config initialized (BLE service-only mode)
I (xxx) ble_deinit_example: Step 3: Waiting for WiFi connection...
  ... user provisions WiFi via BLE ...
I (xxx) ble_deinit_example: WiFi connected!
I (xxx) ble_deinit_example: Step 4: Deinitializing WiFi Config (BLE service removed, stack stays)
I (xxx) ble_deinit_example: WiFi Config deinitialized — NimBLE stack still running
I (xxx) ble_deinit_example: Step 5: Registering app GATT service and advertising
I (xxx) ble_deinit_example: App GATT service 0xAA00 registered
I (xxx) ble_deinit_example: App BLE advertising started: ESP32-App
I (xxx) ble_deinit_example: WiFi: MyWiFi (78%) | BLE: app service active
```

### Scan-Only Mode

```
  ... steps 1–4 same as above ...
I (xxx) ble_deinit_example: Step 5: Starting BLE scan (no advertising, no GATT service)
I (xxx) ble_deinit_example: BLE scan started (10000 ms)...
I (xxx) ble_deinit_example: Found: aa:bb:cc:dd:ee:ff  RSSI=-45  Living Room Speaker
I (xxx) ble_deinit_example: Found: 11:22:33:44:55:66  RSSI=-72  (unknown)
I (xxx) ble_deinit_example: Scan complete (reason=0)
I (xxx) ble_deinit_example: WiFi: MyWiFi (78%) | Restarting scan...
```

## Key Differences from `with_ble` Example

|                  | `with_ble`             | `with_ble_deinit`    |
|------------------|------------------------|----------------------|
| Stack owner      | WiFi Config           | Application          |
| Host stack       | Bluedroid (default)    | NimBLE               |
| After deinit     | BLE completely stopped | BLE keeps running    |
| App BLE services | Not possible           | Yes (0xAA00 example) |
