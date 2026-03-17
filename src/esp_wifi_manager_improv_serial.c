/**
 * @file esp_wifi_manager_improv_serial.c
 * @brief Improv WiFi Serial transport — UART framing and I/O
 *
 * Implements the Improv Serial protocol framing:
 *   Header("IMPROV") + version(1) + type(1) + length(1) + data[length] + checksum(1)
 *
 * Reference: https://www.improv-wifi.com/serial/
 */

#include "sdkconfig.h"

#if defined(CONFIG_WIFI_MGR_ENABLE_IMPROV) && defined(CONFIG_WIFI_MGR_ENABLE_IMPROV_SERIAL)

#include "esp_wifi_manager_improv.h"
#include "esp_wifi_manager_priv.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wifi_mgr_improv_ser";

#define IMPROV_SERIAL_BUF_SIZE  256
#define IMPROV_SERIAL_RX_BUF    512
#define IMPROV_SERIAL_TASK_STACK 4096

static int s_uart_num = -1;
static TaskHandle_t s_rx_task = NULL;
static bool s_running = false;

// =============================================================================
// Packet TX
// =============================================================================

/**
 * Build and send an Improv Serial packet over UART.
 * Format: "IMPROV" + version + type + length + data + checksum
 */
static void serial_send_packet(uint8_t type, const uint8_t *data, size_t len)
{
    if (s_uart_num < 0 || !s_running) return;

    uint8_t buf[IMPROV_SERIAL_BUF_SIZE];
    size_t offset = 0;

    // Header
    memcpy(buf, IMPROV_SERIAL_HEADER, IMPROV_SERIAL_HEADER_LEN);
    offset += IMPROV_SERIAL_HEADER_LEN;

    // Version
    buf[offset++] = IMPROV_SERIAL_VERSION;

    // Type
    buf[offset++] = type;

    // Length
    if (len > 200) len = 200;  // Safety cap
    buf[offset++] = (uint8_t)len;

    // Data
    if (len > 0 && data) {
        memcpy(buf + offset, data, len);
        offset += len;
    }

    // Checksum (sum of all bytes from type to end of data, inclusive)
    uint8_t checksum = 0;
    // Sum from version byte onward (index HEADER_LEN to offset-1)
    for (size_t i = IMPROV_SERIAL_HEADER_LEN; i < offset; i++) {
        checksum += buf[i];
    }
    buf[offset++] = checksum;

    uart_write_bytes(s_uart_num, buf, offset);
}

static void serial_send_state(void)
{
    uint8_t state = wifi_mgr_improv_get_state();
    serial_send_packet(IMPROV_SERIAL_TYPE_CURRENT_STATE, &state, 1);
}

static void serial_send_error(void)
{
    uint8_t error = wifi_mgr_improv_get_error();
    serial_send_packet(IMPROV_SERIAL_TYPE_ERROR_STATE, &error, 1);
}

// =============================================================================
// Response callback (from protocol core -> serial TX)
// =============================================================================

static void serial_response_cb(uint8_t type, const uint8_t *data, size_t len, void *ctx)
{
    serial_send_packet(type, data, len);
}

// =============================================================================
// State change callback
// =============================================================================

static void serial_state_change_cb(improv_state_t state, improv_error_t error, void *ctx)
{
    serial_send_state();
    if (error != IMPROV_ERROR_NONE) {
        serial_send_error();
    }
}

// =============================================================================
// Packet RX Parser
// =============================================================================

typedef enum {
    RX_STATE_HEADER,
    RX_STATE_VERSION,
    RX_STATE_TYPE,
    RX_STATE_LENGTH,
    RX_STATE_DATA,
    RX_STATE_CHECKSUM,
} rx_parse_state_t;

static void serial_rx_task(void *param)
{
    uint8_t byte;
    uint8_t rx_buf[IMPROV_SERIAL_BUF_SIZE];
    rx_parse_state_t parse_state = RX_STATE_HEADER;
    size_t header_idx = 0;
    uint8_t pkt_type = 0;
    uint8_t pkt_len = 0;
    size_t data_idx = 0;
    uint8_t checksum = 0;

    while (s_running) {
        int read = uart_read_bytes(s_uart_num, &byte, 1, pdMS_TO_TICKS(100));
        if (read <= 0) continue;

        switch (parse_state) {
            case RX_STATE_HEADER:
                if (byte == IMPROV_SERIAL_HEADER[header_idx]) {
                    header_idx++;
                    if (header_idx == IMPROV_SERIAL_HEADER_LEN) {
                        header_idx = 0;
                        parse_state = RX_STATE_VERSION;
                    }
                } else {
                    header_idx = 0;
                }
                break;

            case RX_STATE_VERSION:
                checksum = byte;
                if (byte != IMPROV_SERIAL_VERSION) {
                    ESP_LOGW(TAG, "Unsupported serial version: %d", byte);
                    parse_state = RX_STATE_HEADER;
                } else {
                    parse_state = RX_STATE_TYPE;
                }
                break;

            case RX_STATE_TYPE:
                pkt_type = byte;
                checksum += byte;
                parse_state = RX_STATE_LENGTH;
                break;

            case RX_STATE_LENGTH:
                pkt_len = byte;
                checksum += byte;
                data_idx = 0;
                if (pkt_len == 0) {
                    parse_state = RX_STATE_CHECKSUM;
                } else if (pkt_len > sizeof(rx_buf)) {
                    ESP_LOGW(TAG, "Packet too large: %d", pkt_len);
                    parse_state = RX_STATE_HEADER;
                } else {
                    parse_state = RX_STATE_DATA;
                }
                break;

            case RX_STATE_DATA:
                rx_buf[data_idx++] = byte;
                checksum += byte;
                if (data_idx >= pkt_len) {
                    parse_state = RX_STATE_CHECKSUM;
                }
                break;

            case RX_STATE_CHECKSUM:
                if (byte != checksum) {
                    ESP_LOGW(TAG, "Checksum mismatch: got 0x%02x, expected 0x%02x", byte, checksum);
                } else {
                    // Valid packet received
                    switch (pkt_type) {
                        case IMPROV_SERIAL_TYPE_RPC_COMMAND:
                            wifi_mgr_improv_handle_rpc(rx_buf, pkt_len,
                                                       serial_response_cb, NULL);
                            break;

                        case IMPROV_SERIAL_TYPE_CURRENT_STATE:
                            // Request for current state
                            serial_send_state();
                            break;

                        default:
                            ESP_LOGD(TAG, "Ignoring serial packet type 0x%02x", pkt_type);
                            break;
                    }
                }
                parse_state = RX_STATE_HEADER;
                break;
        }
    }

    vTaskDelete(NULL);
}

// =============================================================================
// Transport API
// =============================================================================

esp_err_t wifi_mgr_improv_serial_init(void)
{
    int uart_num = CONFIG_WIFI_MGR_IMPROV_SERIAL_UART_NUM;
    int baud = CONFIG_WIFI_MGR_IMPROV_SERIAL_BAUD;

    if (g_wifi_mgr) {
        if (g_wifi_mgr->config.improv.serial_uart_num > 0) {
            uart_num = g_wifi_mgr->config.improv.serial_uart_num;
        }
        if (g_wifi_mgr->config.improv.serial_baud_rate > 0) {
            baud = g_wifi_mgr->config.improv.serial_baud_rate;
        }
    }

    // Check if UART is already installed (common for UART0 used by logging)
    // If so, skip driver install — just use it
    if (!uart_is_driver_installed(uart_num)) {
        uart_config_t uart_config = {
            .baud_rate = baud,
            .data_bits = UART_DATA_8_BITS,
            .parity    = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };

        esp_err_t ret = uart_driver_install(uart_num, IMPROV_SERIAL_RX_BUF, 0, 0, NULL, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
            return ret;
        }
        uart_param_config(uart_num, &uart_config);
    }

    s_uart_num = uart_num;

    // Register state-change callback
    wifi_mgr_improv_register_state_cb(serial_state_change_cb, NULL);

    ESP_LOGI(TAG, "Improv Serial initialized on UART%d @ %d baud", uart_num, baud);
    return ESP_OK;
}

esp_err_t wifi_mgr_improv_serial_deinit(void)
{
    wifi_mgr_improv_serial_stop();
    s_uart_num = -1;
    return ESP_OK;
}

esp_err_t wifi_mgr_improv_serial_start(void)
{
    if (s_running || s_uart_num < 0) return ESP_ERR_INVALID_STATE;

    s_running = true;

    BaseType_t ret = xTaskCreate(serial_rx_task, "improv_ser", IMPROV_SERIAL_TASK_STACK,
                                  NULL, 5, &s_rx_task);
    if (ret != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "Failed to create serial RX task");
        return ESP_ERR_NO_MEM;
    }

    // Send initial state
    serial_send_state();

    ESP_LOGI(TAG, "Improv Serial started");
    return ESP_OK;
}

esp_err_t wifi_mgr_improv_serial_stop(void)
{
    if (!s_running) return ESP_OK;

    s_running = false;

    // Wait for task to exit
    if (s_rx_task) {
        vTaskDelay(pdMS_TO_TICKS(200));
        s_rx_task = NULL;
    }

    ESP_LOGI(TAG, "Improv Serial stopped");
    return ESP_OK;
}

#endif // CONFIG_WIFI_MGR_ENABLE_IMPROV && CONFIG_WIFI_MGR_ENABLE_IMPROV_SERIAL
