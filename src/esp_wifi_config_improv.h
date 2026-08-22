/**
 * @file esp_wifi_config_improv.h
 * @brief Improv WiFi protocol core — internal header
 *
 * Defines Improv protocol constants, BLE UUIDs, packet types,
 * and the transport-agnostic API shared between the protocol core
 * and the Serial / BLE transports.
 *
 * Reference: https://www.improv-wifi.com/
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Improv Protocol Constants
// =============================================================================

#define IMPROV_SERIAL_VERSION       1
#define IMPROV_BLE_VERSION          1

// Serial packet header
#define IMPROV_SERIAL_HEADER        "IMPROV"
#define IMPROV_SERIAL_HEADER_LEN    6

// --- Improv States ---
typedef enum {
    IMPROV_STATE_AUTHORIZATION_REQUIRED = 0x01,
    IMPROV_STATE_AUTHORIZED             = 0x02,
    IMPROV_STATE_PROVISIONING           = 0x03,
    IMPROV_STATE_PROVISIONED            = 0x04,
} improv_state_t;

// --- Improv Error Codes ---
typedef enum {
    IMPROV_ERROR_NONE               = 0x00,
    IMPROV_ERROR_INVALID_RPC        = 0x01,
    IMPROV_ERROR_UNKNOWN_RPC        = 0x02,
    IMPROV_ERROR_UNABLE_TO_CONNECT  = 0x03,
    IMPROV_ERROR_NOT_AUTHORIZED     = 0x04,
    IMPROV_ERROR_UNKNOWN            = 0xFF,
} improv_error_t;

// --- Improv RPC Command IDs ---
#define IMPROV_RPC_SEND_WIFI_SETTINGS   0x01
#define IMPROV_RPC_IDENTIFY             0x02
#define IMPROV_RPC_GET_DEVICE_INFO      0x03
#define IMPROV_RPC_GET_WIFI_NETWORKS    0x04
#define IMPROV_RPC_GET_HOSTNAME         0x05  // Extended
#define IMPROV_RPC_GET_DEVICE_NAME      0x06  // Extended

// --- Improv Serial Packet Types ---
#define IMPROV_SERIAL_TYPE_CURRENT_STATE   0x01
#define IMPROV_SERIAL_TYPE_ERROR_STATE     0x02
#define IMPROV_SERIAL_TYPE_RPC_COMMAND     0x03
#define IMPROV_SERIAL_TYPE_RPC_RESULT      0x04

// --- Improv Capability Flags ---
#define IMPROV_CAPABILITY_IDENTIFY      0x01
#define IMPROV_CAPABILITY_DEVICE_INFO   0x02
#define IMPROV_CAPABILITY_WIFI_SCAN     0x04
#define IMPROV_CAPABILITY_HOSTNAME      0x08

// =============================================================================
// Improv BLE UUIDs
// =============================================================================

// Service UUID: 00467768-6228-2272-4663-277478268000
#define IMPROV_BLE_SVC_UUID_128 \
    { 0x00, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46, \
      0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00 }

// Characteristic UUIDs (last two bytes differ)
// Current State: ...8001
#define IMPROV_BLE_CHAR_STATE_UUID_128 \
    { 0x01, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46, \
      0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00 }

// Error State: ...8002
#define IMPROV_BLE_CHAR_ERROR_UUID_128 \
    { 0x02, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46, \
      0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00 }

// RPC Command: ...8003
#define IMPROV_BLE_CHAR_RPC_CMD_UUID_128 \
    { 0x03, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46, \
      0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00 }

// RPC Result: ...8004
#define IMPROV_BLE_CHAR_RPC_RESULT_UUID_128 \
    { 0x04, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46, \
      0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00 }

// Capabilities: ...8005
#define IMPROV_BLE_CHAR_CAPABILITIES_UUID_128 \
    { 0x05, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46, \
      0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00 }

// =============================================================================
// Response Callback
// =============================================================================

/**
 * @brief Callback invoked by the protocol core to send a response packet.
 *
 * @param type   For Serial: packet type (0x01-0x04). For BLE: ignored (routed by characteristic).
 * @param data   Response payload bytes
 * @param len    Payload length
 * @param ctx    Transport-specific context
 */
typedef void (*improv_response_cb_t)(uint8_t type, const uint8_t *data, size_t len, void *ctx);

// =============================================================================
// Protocol Core API
// =============================================================================

/**
 * @brief How a transport wants multi-part RPC results delivered.
 *
 * The two Improv specifications disagree about GET_WIFI_NETWORKS, and neither
 * is a superset of the other, so the core cannot pick one:
 *
 * - BLE (improv-wifi.com/ble): "This command will trigger one RPC Response
 *   which will contain a multiple of 3 strings where the first contains the
 *   SSID..." -- every network in a single response.
 * - Serial (improv-wifi.com/serial): "This command will trigger at least one
 *   RPC Response. The final response (or the first if no networks are found)
 *   will have 0 strings in the body." -- one response per network, then an
 *   empty one.
 *
 * The difference is not cosmetic on serial. A response is carried in a frame
 * whose length field is one byte, so a single response holding every network
 * overflows on any busy band: measured on a bench with nine neighbours, the
 * combined payload reached 243 bytes against a 253-byte ceiling, and one more
 * SSID put it over. Per-network responses are a few dozen bytes each and
 * cannot overflow.
 */
typedef enum {
    IMPROV_RPC_STYLE_SINGLE,   /**< BLE: one response holding every network. */
    IMPROV_RPC_STYLE_CHUNKED,  /**< Serial: one per network, then an empty one. */
} improv_rpc_style_t;

/**
 * @brief Largest RPC result payload the format itself can express.
 *
 * A result is [command][length][payload...]; the length is one byte, so no
 * payload beyond this can be described no matter what the transport can
 * carry. Transports cap *below* this, never above.
 */
#define IMPROV_RPC_MAX_PAYLOAD  255

/**
 * @brief Process an incoming Improv RPC command.
 *
 * @param data          Raw RPC packet: command, length, payload.
 * @param len           Length of @p data.
 * @param response_cb   Called with each result this command produces.
 * @param cb_ctx        Opaque context handed back to @p response_cb.
 * @param style         How this transport wants multi-part results delivered.
 * @param max_payload   Largest result *payload* this transport can deliver in
 *                      one response -- the TLV bytes only, excluding the two
 *                      header bytes and anything the transport adds around
 *                      them (BLE's trailing checksum, serial's frame). 0 means
 *                      "the format's own ceiling and nothing tighter".
 *
 *                      This exists because a transport's real capacity is not
 *                      a compile-time constant: BLE's is three bytes under the
 *                      ATT MTU that was negotiated with this particular
 *                      client, and is only knowable once a client has
 *                      connected. Packing to a buffer size instead of to that
 *                      number is how a scan response comes to be built,
 *                      checksummed, and then silently dropped.
 */
void wifi_cfg_improv_handle_rpc(const uint8_t *data, size_t len,
                                improv_response_cb_t response_cb, void *cb_ctx,
                                improv_rpc_style_t style, size_t max_payload);

/**
 * @brief Get the current Improv state.
 */
improv_state_t wifi_cfg_improv_get_state(void);

/**
 * @brief Get the current Improv error code.
 */
improv_error_t wifi_cfg_improv_get_error(void);

/**
 * @brief Get the Improv capability bitmask.
 */
uint8_t wifi_cfg_improv_get_capabilities(void);

/**
 * @brief Set the Improv state (called by event listeners on connect/fail).
 */
void wifi_cfg_improv_set_state(improv_state_t state);

/**
 * @brief Set the Improv error code.
 */
void wifi_cfg_improv_set_error(improv_error_t error);

/**
 * @brief State-change callback type.
 *
 * Transports register this to be notified when Improv state or error changes
 * so they can push notifications (BLE notify / Serial state packet).
 */
typedef void (*improv_state_change_cb_t)(improv_state_t state, improv_error_t error, void *ctx);

/**
 * @brief Register a state-change observer.
 *
 * Multiple observers can be registered (one per transport). Max 2.
 */
void wifi_cfg_improv_register_state_cb(improv_state_change_cb_t cb, void *ctx);

// =============================================================================
// Improv Init / Deinit (called from esp_wifi_config.c)
// =============================================================================

/**
 * @brief Initialize the Improv protocol core and enabled transports.
 */
/**
 * @brief Tell Improv the station reached the network
 *
 * Called directly by the WiFi state machine. Improv is part of this library,
 * so it does not go through the event loop to hear from it -- that would add a
 * task hop and a payload copy to a call between two files in the same
 * component.
 */
void wifi_cfg_improv_on_got_ip(void);

/**
 * @brief Tell Improv the station failed or lost its association
 */
void wifi_cfg_improv_on_disconnected(void);

esp_err_t wifi_cfg_improv_init(void);

/**
 * @brief Deinitialize the Improv protocol core and transports.
 */
esp_err_t wifi_cfg_improv_deinit(void);

/**
 * @brief Start Improv provisioning (begin advertising / listening).
 */
esp_err_t wifi_cfg_improv_start(void);

/**
 * @brief Stop Improv provisioning.
 */
esp_err_t wifi_cfg_improv_stop(void);

// =============================================================================
// Transport Init/Deinit (implemented in improv_serial.c / improv_ble.c)
// =============================================================================

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL
esp_err_t wifi_cfg_improv_serial_init(void);
esp_err_t wifi_cfg_improv_serial_deinit(void);
esp_err_t wifi_cfg_improv_serial_start(void);
esp_err_t wifi_cfg_improv_serial_stop(void);
#endif

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
esp_err_t wifi_cfg_improv_ble_init(void);
esp_err_t wifi_cfg_improv_ble_deinit(void);
esp_err_t wifi_cfg_improv_ble_start(void);
esp_err_t wifi_cfg_improv_ble_stop(void);
#endif

#ifdef __cplusplus
}
#endif
