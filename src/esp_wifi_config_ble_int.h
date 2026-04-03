/**
 * @file esp_wifi_config_ble_int.h
 * @brief Internal interface between BLE shared logic and stack backends
 *
 * The shared layer (esp_wifi_config_ble.c) handles JSON command routing.
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
void wifi_cfg_ble_on_command(const uint8_t *data, size_t length);

/**
 * @brief Called by the stack backend when a client connects.
 */
void wifi_cfg_ble_on_connect(void);

/**
 * @brief Called by the stack backend when a client disconnects.
 */
void wifi_cfg_ble_on_disconnect(void);

/**
 * @brief Called by the stack backend when the Response CCCD is written.
 *
 * @param enabled true if notifications were enabled, false if disabled
 */
void wifi_cfg_ble_set_response_notify(bool enabled);

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
esp_err_t wifi_cfg_ble_backend_notify_response(const uint8_t *data, size_t length);

/**
 * @brief Get the current negotiated MTU for the active connection.
 *
 * @return Negotiated MTU in bytes, or 0 if not connected
 */
uint16_t wifi_cfg_ble_backend_get_mtu(void);

/**
 * @brief Check whether the BLE host stack is already running.
 *
 * Used during init to detect whether the application has already initialized
 * the BLE stack. If so, the backend will only register its GATT service
 * ("service-only" mode) and skip full stack teardown on deinit.
 *
 * @return true if the host stack is currently active
 */
bool wifi_cfg_ble_backend_is_stack_running(void);

/**
 * @brief Initialize the BLE stack backend.
 *
 * If the host stack is already running (detected via
 * wifi_cfg_ble_backend_is_stack_running()), skips stack initialization and
 * only registers the GATT service. On deinit, only the service will be
 * removed — the host stack will be left running for the application.
 *
 * @param device_name  Advertised device name (already expanded from template)
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_ble_backend_init(const char *device_name);

/**
 * @brief Deinitialize the BLE stack backend.
 *
 * Performs a graceful service-level teardown (stop advertising, disconnect,
 * unregister GATT service). Full stack teardown only runs when the backend
 * owns the stack (i.e., it was not already running at init time).
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_ble_backend_deinit(void);

/**
 * @brief Start BLE advertising (without full stack init).
 * Requires that the backend has been initialized via wifi_cfg_ble_backend_init().
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_ble_backend_start(void);

/**
 * @brief Stop BLE advertising and disconnect active client (without full stack deinit).
 * The GATT service and command task remain alive.
 * @return ESP_OK on success
 */
esp_err_t wifi_cfg_ble_backend_stop(void);

#ifdef __cplusplus
}
#endif
