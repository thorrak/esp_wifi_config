/**
 * @file esp_wifi_config_improv_ble.c
 * @brief Improv WiFi BLE transport — GATT service definition and characteristic handlers
 *
 * Provides the Improv BLE GATT service (UUID 00467768-6228-2272-4663-277478268000)
 * with 5 characteristics. Bridges characteristic writes to the protocol core
 * and state changes to BLE notifications.
 *
 * This transport delegates GATT registration to the existing BLE backends
 * (NimBLE / Bluedroid) so both the custom service (0xFFE0) and Improv service
 * are advertised from the same BLE stack instance.
 *
 * Reference: https://www.improv-wifi.com/ble/
 */

#include "sdkconfig.h"

#if defined(CONFIG_WIFI_CFG_ENABLE_IMPROV) && defined(CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE)

#include "esp_wifi_config_improv.h"
#include "esp_wifi_manager_priv.h"
#include "esp_log.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "wifi_cfg_improv_ble";

// =============================================================================
// Command queue (process RPC off the BLE stack context)
// =============================================================================

#define IMPROV_BLE_CMD_QUEUE_DEPTH  2
#define IMPROV_BLE_CMD_TASK_STACK   4096

typedef struct {
    uint8_t *data;
    uint16_t length;
} improv_ble_cmd_msg_t;

static QueueHandle_t s_cmd_queue = NULL;
static TaskHandle_t  s_cmd_task  = NULL;
static bool s_started = false;

// =============================================================================
// NimBLE backend
// =============================================================================

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"

// 128-bit UUIDs for NimBLE
static const ble_uuid128_t s_improv_svc_uuid = BLE_UUID128_INIT(
    0x00, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00);

static const ble_uuid128_t s_char_state_uuid = BLE_UUID128_INIT(
    0x01, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00);

static const ble_uuid128_t s_char_error_uuid = BLE_UUID128_INIT(
    0x02, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00);

static const ble_uuid128_t s_char_rpc_cmd_uuid = BLE_UUID128_INIT(
    0x03, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00);

static const ble_uuid128_t s_char_rpc_result_uuid = BLE_UUID128_INIT(
    0x04, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00);

static const ble_uuid128_t s_char_capabilities_uuid = BLE_UUID128_INIT(
    0x05, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00);

static uint16_t s_state_val_handle;
static uint16_t s_error_val_handle;
static uint16_t s_rpc_result_val_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

// Forward declarations for access callbacks
static int improv_state_access(uint16_t conn, uint16_t attr,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static int improv_error_access(uint16_t conn, uint16_t attr,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static int improv_rpc_cmd_access(uint16_t conn, uint16_t attr,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int improv_rpc_result_access(uint16_t conn, uint16_t attr,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg);
static int improv_capabilities_access(uint16_t conn, uint16_t attr,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg);

// GATT service definition (exported for NimBLE backend to merge)
const struct ble_gatt_svc_def wifi_cfg_improv_nimble_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_improv_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Current State (read + notify)
                .uuid = &s_char_state_uuid.u,
                .access_cb = improv_state_access,
                .val_handle = &s_state_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // Error State (read + notify)
                .uuid = &s_char_error_uuid.u,
                .access_cb = improv_error_access,
                .val_handle = &s_error_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // RPC Command (write + write-no-rsp)
                .uuid = &s_char_rpc_cmd_uuid.u,
                .access_cb = improv_rpc_cmd_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                // RPC Result (read + notify)
                .uuid = &s_char_rpc_result_uuid.u,
                .access_cb = improv_rpc_result_access,
                .val_handle = &s_rpc_result_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // Capabilities (read)
                .uuid = &s_char_capabilities_uuid.u,
                .access_cb = improv_capabilities_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 }, // Terminator
        },
    },
    { 0 }, // Terminator
};

static int improv_state_access(uint16_t conn, uint16_t attr,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t state = wifi_cfg_improv_get_state();
        os_mbuf_append(ctxt->om, &state, 1);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int improv_error_access(uint16_t conn, uint16_t attr,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t error = wifi_cfg_improv_get_error();
        os_mbuf_append(ctxt->om, &error, 1);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int improv_rpc_cmd_access(uint16_t conn, uint16_t attr,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len == 0 || om_len > 512) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        if (!s_cmd_queue) {
            ESP_LOGE(TAG, "Command queue not initialized");
            return BLE_ATT_ERR_UNLIKELY;
        }

        uint8_t *buf = malloc(om_len);
        if (!buf) return BLE_ATT_ERR_INSUFFICIENT_RES;

        uint16_t len = om_len;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, &len);
        if (rc != 0) {
            free(buf);
            return BLE_ATT_ERR_UNLIKELY;
        }

        improv_ble_cmd_msg_t msg = { .data = buf, .length = len };
        if (xQueueSend(s_cmd_queue, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Command queue full");
            free(buf);
            return BLE_ATT_ERR_UNLIKELY;
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int improv_rpc_result_access(uint16_t conn, uint16_t attr,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return 0;  // Read returns empty; data pushed via notify
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int improv_capabilities_access(uint16_t conn, uint16_t attr,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t caps = wifi_cfg_improv_get_capabilities();
        os_mbuf_append(ctxt->om, &caps, 1);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Notify helpers (NimBLE)
static void nimble_notify_state(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    uint8_t state = wifi_cfg_improv_get_state();
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&state, 1);
    if (om) ble_gatts_notify_custom(s_conn_handle, s_state_val_handle, om);
}

static void nimble_notify_error(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    uint8_t error = wifi_cfg_improv_get_error();
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&error, 1);
    if (om) ble_gatts_notify_custom(s_conn_handle, s_error_val_handle, om);
}

static void nimble_notify_rpc_result(const uint8_t *data, size_t len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om) ble_gatts_notify_custom(s_conn_handle, s_rpc_result_val_handle, om);
}

// Connection tracking (called from the NimBLE backend's GAP handler)
void wifi_cfg_improv_ble_on_connect_nimble(uint16_t conn_handle)
{
    s_conn_handle = conn_handle;
}

void wifi_cfg_improv_ble_on_disconnect_nimble(void)
{
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

#endif // CONFIG_BT_NIMBLE_ENABLED

// =============================================================================
// Bluedroid backend
// =============================================================================

#if defined(CONFIG_BT_BLUEDROID_ENABLED)

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"

#define IMPROV_PROFILE_APP_ID  1  // Different from custom BLE's app 0

enum {
    IMPROV_IDX_SVC,
    IMPROV_IDX_CHAR_STATE,
    IMPROV_IDX_CHAR_STATE_VAL,
    IMPROV_IDX_CHAR_STATE_CCC,
    IMPROV_IDX_CHAR_ERROR,
    IMPROV_IDX_CHAR_ERROR_VAL,
    IMPROV_IDX_CHAR_ERROR_CCC,
    IMPROV_IDX_CHAR_RPC_CMD,
    IMPROV_IDX_CHAR_RPC_CMD_VAL,
    IMPROV_IDX_CHAR_RPC_RESULT,
    IMPROV_IDX_CHAR_RPC_RESULT_VAL,
    IMPROV_IDX_CHAR_RPC_RESULT_CCC,
    IMPROV_IDX_CHAR_CAPABILITIES,
    IMPROV_IDX_CHAR_CAPABILITIES_VAL,
    IMPROV_IDX_NB,
};

static uint16_t improv_handle_table[IMPROV_IDX_NB];

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_read         = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_read_notify  = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_write        = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

// 128-bit service UUID in little-endian for Bluedroid
static const uint8_t improv_svc_uuid128[16] = IMPROV_BLE_SVC_UUID_128;
static const uint8_t improv_char_state_uuid128[16] = IMPROV_BLE_CHAR_STATE_UUID_128;
static const uint8_t improv_char_error_uuid128[16] = IMPROV_BLE_CHAR_ERROR_UUID_128;
static const uint8_t improv_char_rpc_cmd_uuid128[16] = IMPROV_BLE_CHAR_RPC_CMD_UUID_128;
static const uint8_t improv_char_rpc_result_uuid128[16] = IMPROV_BLE_CHAR_RPC_RESULT_UUID_128;
static const uint8_t improv_char_capabilities_uuid128[16] = IMPROV_BLE_CHAR_CAPABILITIES_UUID_128;

static uint8_t improv_state_ccc[2] = {0};
static uint8_t improv_error_ccc[2] = {0};
static uint8_t improv_result_ccc[2] = {0};

static const esp_gatts_attr_db_t improv_gatt_db[IMPROV_IDX_NB] = {
    // Service
    [IMPROV_IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
         16, sizeof(improv_svc_uuid128), (uint8_t *)improv_svc_uuid128}
    },

    // Current State
    [IMPROV_IDX_CHAR_STATE] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         1, 1, (uint8_t *)&char_prop_read_notify}
    },
    [IMPROV_IDX_CHAR_STATE_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, (uint8_t *)improv_char_state_uuid128, ESP_GATT_PERM_READ,
         1, 0, NULL}
    },
    [IMPROV_IDX_CHAR_STATE_CCC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         2, 2, improv_state_ccc}
    },

    // Error State
    [IMPROV_IDX_CHAR_ERROR] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         1, 1, (uint8_t *)&char_prop_read_notify}
    },
    [IMPROV_IDX_CHAR_ERROR_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, (uint8_t *)improv_char_error_uuid128, ESP_GATT_PERM_READ,
         1, 0, NULL}
    },
    [IMPROV_IDX_CHAR_ERROR_CCC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         2, 2, improv_error_ccc}
    },

    // RPC Command
    [IMPROV_IDX_CHAR_RPC_CMD] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         1, 1, (uint8_t *)&char_prop_write}
    },
    [IMPROV_IDX_CHAR_RPC_CMD_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, (uint8_t *)improv_char_rpc_cmd_uuid128, ESP_GATT_PERM_WRITE,
         512, 0, NULL}
    },

    // RPC Result
    [IMPROV_IDX_CHAR_RPC_RESULT] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         1, 1, (uint8_t *)&char_prop_read_notify}
    },
    [IMPROV_IDX_CHAR_RPC_RESULT_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, (uint8_t *)improv_char_rpc_result_uuid128, ESP_GATT_PERM_READ,
         512, 0, NULL}
    },
    [IMPROV_IDX_CHAR_RPC_RESULT_CCC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         2, 2, improv_result_ccc}
    },

    // Capabilities
    [IMPROV_IDX_CHAR_CAPABILITIES] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         1, 1, (uint8_t *)&char_prop_read}
    },
    [IMPROV_IDX_CHAR_CAPABILITIES_VAL] = {
        {ESP_GATT_RSP_BY_APP},
        {ESP_UUID_LEN_128, (uint8_t *)improv_char_capabilities_uuid128, ESP_GATT_PERM_READ,
         1, 0, NULL}
    },
};

static struct {
    esp_gatt_if_t gatts_if;
    uint16_t conn_id;
    bool connected;
} s_bd_profile = {
    .gatts_if = ESP_GATT_IF_NONE,
    .connected = false,
};

void improv_bd_gatts_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                              esp_ble_gatts_cb_param_t *param)
{
    // Only handle events for our interface
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.app_id != IMPROV_PROFILE_APP_ID) return;
        s_bd_profile.gatts_if = gatts_if;
        esp_ble_gatts_create_attr_tab(improv_gatt_db, gatts_if, IMPROV_IDX_NB, 1);
        return;
    }

    if (gatts_if != s_bd_profile.gatts_if && gatts_if != ESP_GATT_IF_NONE) return;

    switch (event) {
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK &&
                param->add_attr_tab.num_handle == IMPROV_IDX_NB) {
                memcpy(improv_handle_table, param->add_attr_tab.handles,
                       sizeof(improv_handle_table));
                esp_ble_gatts_start_service(improv_handle_table[IMPROV_IDX_SVC]);
                ESP_LOGI(TAG, "Improv GATT service started (Bluedroid)");
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_bd_profile.conn_id = param->connect.conn_id;
            s_bd_profile.connected = true;
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            s_bd_profile.connected = false;
            break;

        case ESP_GATTS_READ_EVT:
            if (param->read.handle == improv_handle_table[IMPROV_IDX_CHAR_STATE_VAL]) {
                esp_gatt_rsp_t rsp = {0};
                rsp.attr_value.len = 1;
                rsp.attr_value.value[0] = wifi_cfg_improv_get_state();
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                            param->read.trans_id, ESP_GATT_OK, &rsp);
            } else if (param->read.handle == improv_handle_table[IMPROV_IDX_CHAR_ERROR_VAL]) {
                esp_gatt_rsp_t rsp = {0};
                rsp.attr_value.len = 1;
                rsp.attr_value.value[0] = wifi_cfg_improv_get_error();
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                            param->read.trans_id, ESP_GATT_OK, &rsp);
            } else if (param->read.handle == improv_handle_table[IMPROV_IDX_CHAR_CAPABILITIES_VAL]) {
                esp_gatt_rsp_t rsp = {0};
                rsp.attr_value.len = 1;
                rsp.attr_value.value[0] = wifi_cfg_improv_get_capabilities();
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                            param->read.trans_id, ESP_GATT_OK, &rsp);
            } else if (param->read.handle == improv_handle_table[IMPROV_IDX_CHAR_RPC_RESULT_VAL]) {
                esp_gatt_rsp_t rsp = {0};
                rsp.attr_value.len = 0;
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                            param->read.trans_id, ESP_GATT_OK, &rsp);
            }
            break;

        case ESP_GATTS_WRITE_EVT:
            if (!param->write.is_prep &&
                param->write.handle == improv_handle_table[IMPROV_IDX_CHAR_RPC_CMD_VAL]) {
                // Queue command for processing
                uint8_t *buf = malloc(param->write.len);
                if (buf) {
                    memcpy(buf, param->write.value, param->write.len);
                    improv_ble_cmd_msg_t msg = { .data = buf, .length = param->write.len };
                    if (xQueueSend(s_cmd_queue, &msg, 0) != pdTRUE) {
                        free(buf);
                    }
                }
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                param->write.trans_id, ESP_GATT_OK, NULL);
                }
            }
            break;

        default:
            break;
    }
}

// Notify helpers (Bluedroid)
static void bd_notify_state(void)
{
    if (!s_bd_profile.connected || s_bd_profile.gatts_if == ESP_GATT_IF_NONE) return;
    uint8_t state = wifi_cfg_improv_get_state();
    esp_ble_gatts_send_indicate(s_bd_profile.gatts_if, s_bd_profile.conn_id,
                                improv_handle_table[IMPROV_IDX_CHAR_STATE_VAL],
                                1, &state, false);
}

static void bd_notify_error(void)
{
    if (!s_bd_profile.connected || s_bd_profile.gatts_if == ESP_GATT_IF_NONE) return;
    uint8_t error = wifi_cfg_improv_get_error();
    esp_ble_gatts_send_indicate(s_bd_profile.gatts_if, s_bd_profile.conn_id,
                                improv_handle_table[IMPROV_IDX_CHAR_ERROR_VAL],
                                1, &error, false);
}

static void bd_notify_rpc_result(const uint8_t *data, size_t len)
{
    if (!s_bd_profile.connected || s_bd_profile.gatts_if == ESP_GATT_IF_NONE) return;
    esp_ble_gatts_send_indicate(s_bd_profile.gatts_if, s_bd_profile.conn_id,
                                improv_handle_table[IMPROV_IDX_CHAR_RPC_RESULT_VAL],
                                len, (uint8_t *)data, false);
}

#endif // CONFIG_BT_BLUEDROID_ENABLED

// =============================================================================
// RPC response callback (protocol core -> BLE notify)
// =============================================================================

static void ble_response_cb(uint8_t type, const uint8_t *data, size_t len, void *ctx)
{
    // For BLE, RPC results go to the RPC Result characteristic
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    nimble_notify_rpc_result(data, len);
#elif defined(CONFIG_BT_BLUEDROID_ENABLED)
    bd_notify_rpc_result(data, len);
#endif
}

// =============================================================================
// State change callback (push state/error notifications)
// =============================================================================

static void ble_state_change_cb(improv_state_t state, improv_error_t error, void *ctx)
{
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    nimble_notify_state();
    if (error != IMPROV_ERROR_NONE) nimble_notify_error();
#elif defined(CONFIG_BT_BLUEDROID_ENABLED)
    bd_notify_state();
    if (error != IMPROV_ERROR_NONE) bd_notify_error();
#endif
}

// =============================================================================
// Command processing task
// =============================================================================

static void improv_ble_cmd_task(void *param)
{
    improv_ble_cmd_msg_t msg;
    while (xQueueReceive(s_cmd_queue, &msg, portMAX_DELAY) == pdTRUE) {
        wifi_cfg_improv_handle_rpc(msg.data, msg.length, ble_response_cb, NULL);
        free(msg.data);
    }
    vTaskDelete(NULL);
}

// =============================================================================
// Transport API
// =============================================================================

esp_err_t wifi_cfg_improv_ble_init(void)
{
    ESP_LOGI(TAG, "Initializing Improv BLE");

    s_cmd_queue = xQueueCreate(IMPROV_BLE_CMD_QUEUE_DEPTH, sizeof(improv_ble_cmd_msg_t));
    if (!s_cmd_queue) return ESP_ERR_NO_MEM;

    BaseType_t ret = xTaskCreate(improv_ble_cmd_task, "improv_ble", IMPROV_BLE_CMD_TASK_STACK,
                                  NULL, 5, &s_cmd_task);
    if (ret != pdPASS) {
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    wifi_cfg_improv_register_state_cb(ble_state_change_cb, NULL);

#if defined(CONFIG_BT_BLUEDROID_ENABLED)
    // Register Improv GATT app. The GATTS callback is already registered by the
    // custom BLE backend — we do NOT re-register it here (Bluedroid supports only
    // one global GATTS callback). Instead, the custom backend's handler will
    // ignore events for gatts_if it doesn't own, and we install a profile-based
    // dispatcher. We register a second app; Bluedroid dispatches events to
    // the single callback with the appropriate gatts_if per app.
    //
    // The improv handler filters by app_id in ESP_GATTS_REG_EVT and by gatts_if
    // for all other events, so it will only process its own events.
    //
    // We must hook into the existing callback chain. We do this by registering
    // our app — the existing global callback will receive events for both apps,
    // but will ignore events for gatts_if != its own.
    esp_ble_gatts_app_register(IMPROV_PROFILE_APP_ID);
#endif

    // NimBLE registration happens in the backend file via wifi_cfg_improv_nimble_svcs

    return ESP_OK;
}

esp_err_t wifi_cfg_improv_ble_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing Improv BLE");

#if defined(CONFIG_BT_BLUEDROID_ENABLED)
    if (s_bd_profile.gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_app_unregister(s_bd_profile.gatts_if);
        s_bd_profile.gatts_if = ESP_GATT_IF_NONE;
    }
#endif

    if (s_cmd_task) {
        vTaskDelete(s_cmd_task);
        s_cmd_task = NULL;
    }
    if (s_cmd_queue) {
        improv_ble_cmd_msg_t msg;
        while (xQueueReceive(s_cmd_queue, &msg, 0) == pdTRUE) {
            free(msg.data);
        }
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
    }

    s_started = false;
    return ESP_OK;
}

esp_err_t wifi_cfg_improv_ble_start(void)
{
    s_started = true;
    ESP_LOGI(TAG, "Improv BLE started");
    // Advertising is handled by the main BLE backend — Improv service is
    // already registered in the GATT table. The backend's start_advertising()
    // will include both service UUIDs.
    return ESP_OK;
}

esp_err_t wifi_cfg_improv_ble_stop(void)
{
    s_started = false;
    ESP_LOGI(TAG, "Improv BLE stopped");
    return ESP_OK;
}

#endif // CONFIG_WIFI_CFG_ENABLE_IMPROV && CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
