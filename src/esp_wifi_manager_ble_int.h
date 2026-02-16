/**
 * @file esp_wifi_manager_ble_int.h
 * @brief Internal interface between BLE shared logic and stack backends
 *
 * The shared layer (esp_wifi_manager_ble.c) handles JSON command routing.
 * Stack backends (bluedroid/nimble) implement the transport below.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// UUIDs (shared between all backends)
// =============================================================================

#define WIFI_BLE_SVC_UUID           0xFFE0
#define WIFI_BLE_CHAR_STATUS_UUID   0xFFE1
#define WIFI_BLE_CHAR_COMMAND_UUID  0xFFE2
#define WIFI_BLE_CHAR_RESPONSE_UUID 0xFFE3

// =============================================================================
// Callbacks: stack backend -> shared layer
// =============================================================================

/**
 * @brief Called by the stack backend when a command is written to the Command characteristic.
 *
 * @param data   Raw bytes written (not necessarily null-terminated)
 * @param length Number of bytes
 */
void wifi_mgr_ble_on_command(const uint8_t *data, size_t length);

/**
 * @brief Called by the stack backend when a client connects.
 */
void wifi_mgr_ble_on_connect(void);

/**
 * @brief Called by the stack backend when a client disconnects.
 */
void wifi_mgr_ble_on_disconnect(void);

/**
 * @brief Called by the stack backend when the Response CCCD is written.
 *
 * @param enabled true if notifications were enabled, false if disabled
 */
void wifi_mgr_ble_set_response_notify(bool enabled);

// =============================================================================
// Functions: shared layer -> stack backend
// =============================================================================

/**
 * @brief Send a notification on the Response characteristic.
 *
 * @param data   Data to send
 * @param length Number of bytes
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_ble_backend_notify_response(const uint8_t *data, size_t length);

/**
 * @brief Get the current negotiated MTU for the active connection.
 *
 * @return Negotiated MTU in bytes, or 0 if not connected
 */
uint16_t wifi_mgr_ble_backend_get_mtu(void);

/**
 * @brief Initialize the BLE stack backend.
 *
 * Must set up the BT controller, host stack, GATT service, and start advertising.
 *
 * @param device_name  Advertised device name (already expanded from template)
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_ble_backend_init(const char *device_name);

/**
 * @brief Deinitialize the BLE stack backend.
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_ble_backend_deinit(void);

#ifdef __cplusplus
}
#endif
