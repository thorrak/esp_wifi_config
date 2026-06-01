/**
 * @file esp_wifi_config_ble_int.h
 * @brief Internal interface between the Improv BLE host bootstrap and stack backends
 *
 * The host bootstrap (this header + the NimBLE / Bluedroid backend files)
 * brings up the BLE controller and host stack, advertises, and dispatches
 * connect/disconnect events into the Improv BLE transport. It is only
 * compiled when CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE is set.
 *
 * For ESP-IDF Network Provisioning BLE
 * (CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING_BLE) the wifi_provisioning
 * manager owns its own BLE bootstrap and these symbols are not used.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Functions: Improv transport -> stack backend
// =============================================================================

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
 * @brief Initialize the BLE stack backend for Improv.
 *
 * If the host stack is already running (detected via
 * wifi_cfg_ble_backend_is_stack_running()), skips stack initialization and
 * only registers the Improv GATT service. On deinit, only the service will
 * be removed — the host stack will be left running for the application.
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
