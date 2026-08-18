/**
 * @file esp_wifi_config_improv_serial.c
 * @brief Improv WiFi Serial transport — UART framing and I/O
 *
 * Implements the Improv Serial protocol framing:
 *   Header("IMPROV") + version(1) + type(1) + length(1) + data[length] + checksum(1)
 *
 * Reference: https://www.improv-wifi.com/serial/
 */

#include "sdkconfig.h"

// Note: CONFIG_WIFI_CFG_ENABLE_IMPROV is derived from the transport flags inside
// esp_wifi_config_priv.h, but priv.h hasn't been included yet at this point —
// gate on the transport flag directly (which always implies IMPROV).
#if defined(CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL)

#include "esp_wifi_config_improv.h"
#include "esp_wifi_config_priv.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "soc/uart_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wifi_cfg_improv_ser";

/* Frame overhead: "IMPROV" + version + type + length + checksum. */
#define IMPROV_SERIAL_FRAME_OVERHEAD (IMPROV_SERIAL_HEADER_LEN + 4)
/* The frame's length field is one byte, so this is the hard ceiling on what a
 * frame can carry -- and it is below what the protocol core can build: an RPC
 * packet is [cmd][len][<=255 payload], so its payload must stay under 253 to
 * be expressible here at all. */
#define IMPROV_SERIAL_MAX_DATA  255
#define IMPROV_SERIAL_BUF_SIZE  (IMPROV_SERIAL_FRAME_OVERHEAD + IMPROV_SERIAL_MAX_DATA)
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

    /*
     * Length.
     *
     * This used to clamp: `if (len > 200) len = 200;`. That wrote the clamped
     * length into the frame and copied only that many bytes -- but the RPC
     * packet nested inside still declared its original length, so the client
     * received a frame whose inner header claimed more payload than had been
     * sent. A Wi-Fi scan reaches this immediately: measured 2026-08-17, a
     * bench with eleven neighbouring APs produced a 218-byte RPC packet that
     * arrived as 200 bytes declaring 216, with a neighbour's SSID severed
     * mid-string.
     *
     * Dropping is strictly better than truncating. A missing response is a
     * timeout the caller can see; a malformed one is a parse error that looks
     * like a client bug. The buffer is now sized to the format's own ceiling,
     * so this is reachable only by a packet no frame could have expressed.
     */
    if (len > IMPROV_SERIAL_MAX_DATA) {
        ESP_LOGE(TAG, "refusing to send a %u-byte packet: the frame length "
                      "field is one byte", (unsigned)len);
        return;
    }
    buf[offset++] = (uint8_t)len;

    // Data
    if (len > 0 && data) {
        memcpy(buf + offset, data, len);
        offset += len;
    }

    // Checksum: sum of ALL preceding bytes (header + version + type + length + data)
    uint8_t checksum = 0;
    for (size_t i = 0; i < offset; i++) {
        checksum += buf[i];
    }
    buf[offset++] = checksum;

    uart_write_bytes(s_uart_num, buf, offset);
}

static void serial_send_state(void)
{
    uint8_t state = wifi_cfg_improv_get_state();
    serial_send_packet(IMPROV_SERIAL_TYPE_CURRENT_STATE, &state, 1);
}

static void serial_send_error(void)
{
    uint8_t error = wifi_cfg_improv_get_error();
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
                        // Seed checksum with the header bytes we just matched
                        checksum = 0;
                        for (int i = 0; i < IMPROV_SERIAL_HEADER_LEN; i++) {
                            checksum += IMPROV_SERIAL_HEADER[i];
                        }
                        parse_state = RX_STATE_VERSION;
                    }
                } else {
                    header_idx = 0;
                }
                break;

            case RX_STATE_VERSION:
                checksum += byte;
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
                            wifi_cfg_improv_handle_rpc(rx_buf, pkt_len,
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

esp_err_t wifi_cfg_improv_serial_init(void)
{
    int uart_num = CONFIG_WIFI_MGR_IMPROV_SERIAL_UART_NUM;
    int baud = CONFIG_WIFI_MGR_IMPROV_SERIAL_BAUD;

    if (g_wifi_cfg) {
        if (g_wifi_cfg->config.improv.serial_uart_num > 0) {
            uart_num = g_wifi_cfg->config.improv.serial_uart_num;
        }
        if (g_wifi_cfg->config.improv.serial_baud_rate > 0) {
            baud = g_wifi_cfg->config.improv.serial_baud_rate;
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

        // Documented order is configure, route, then install.
        esp_err_t ret = uart_param_config(uart_num, &uart_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
            return ret;
        }

        /*
         * Route the peripheral to pins.
         *
         * Without this the UART is clocked, configured and driven, and its TX
         * signal reaches no pad unless something else happened to have muxed
         * it already. With the console on UART0 something else does —
         * `cpu_start` logs "GPIO 3 and 1 are used as console UART I/O pins" —
         * which is why this omission stayed invisible.
         *
         * It is invisible in the worst possible place. Improv Serial frames
         * binary packets, so the console MUST be disabled on its UART
         * (CONFIG_ESP_CONSOLE_NONE, as this component's own Kconfig help
         * says) or log text corrupts the stream. That is precisely the case
         * with nothing left to configure the pins — so the transport was mute
         * in the only configuration it is correct to run it in, while logging
         * that it had initialised and started.
         *
         * Only UART0's IOMUX defaults are applied. On several targets the
         * defaults for UART1/UART2 land on the SPI flash bus (ESP32: U1TXD is
         * GPIO10, U1RXD GPIO9), so driving them blind would be worse than
         * doing nothing.
         */
        if (uart_num == 0) {
            ret = uart_set_pin(uart_num, U0TXD_GPIO_NUM, U0RXD_GPIO_NUM,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "UART pin routing failed: %s", esp_err_to_name(ret));
                return ret;
            }
        } else {
            ESP_LOGW(TAG, "UART%d pins are not configured by this component. "
                          "If nothing else has routed them, Improv Serial will "
                          "run and transmit nothing; call uart_set_pin() for "
                          "UART%d before wifi_cfg_init().",
                     uart_num, uart_num);
        }

        ret = uart_driver_install(uart_num, IMPROV_SERIAL_RX_BUF, 0, 0, NULL, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    s_uart_num = uart_num;

    // Register state-change callback
    wifi_cfg_improv_register_state_cb(serial_state_change_cb, NULL);

    ESP_LOGI(TAG, "Improv Serial initialized on UART%d @ %d baud", uart_num, baud);
    return ESP_OK;
}

esp_err_t wifi_cfg_improv_serial_deinit(void)
{
    wifi_cfg_improv_serial_stop();
    s_uart_num = -1;
    return ESP_OK;
}

esp_err_t wifi_cfg_improv_serial_start(void)
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

    ESP_LOGI(TAG, "Improv Serial started");
    return ESP_OK;
}

esp_err_t wifi_cfg_improv_serial_stop(void)
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

#endif // CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL
