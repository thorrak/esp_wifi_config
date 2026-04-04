/**
 * @file esp_wifi_config_ble_nimble.c
 * @brief BLE backend using the NimBLE host stack
 */

#include "sdkconfig.h"

#if defined(CONFIG_WIFI_CFG_ENABLE_BLE) && defined(CONFIG_BT_NIMBLE_ENABLED)

#include "esp_wifi_config_ble_int.h"
#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
#include "esp_wifi_config_improv.h"
#endif
#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_bt.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "wifi_cfg_ble_nb";

/** Stack size for the command-processing task (handles JSON + WiFi scan). */
#define BLE_CMD_TASK_STACK_SIZE  4096
#define BLE_CMD_QUEUE_DEPTH      2

/** Queued command message. */
typedef struct {
    uint8_t *data;   /**< heap-allocated copy of command bytes */
    uint16_t length;
} ble_cmd_msg_t;

// =============================================================================
// UUIDs
// =============================================================================

static const ble_uuid16_t s_svc_uuid =
    BLE_UUID16_INIT(WIFI_BLE_SVC_UUID);
static const ble_uuid16_t s_char_status_uuid =
    BLE_UUID16_INIT(WIFI_BLE_CHAR_STATUS_UUID);
static const ble_uuid16_t s_char_command_uuid =
    BLE_UUID16_INIT(WIFI_BLE_CHAR_COMMAND_UUID);
static const ble_uuid16_t s_char_response_uuid =
    BLE_UUID16_INIT(WIFI_BLE_CHAR_RESPONSE_UUID);

// =============================================================================
// State
// =============================================================================

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_response_val_handle;
static char s_device_name[32];
static QueueHandle_t s_cmd_queue;
static TaskHandle_t  s_cmd_task;

/** true if we initialized the NimBLE stack ourselves; false if it was already running. */
static bool s_ble_stack_owned = false;

// Forward declarations
static void nimble_host_task(void *param);
static void ble_on_sync(void);
static void ble_on_reset(int reason);
static int gap_event_handler(struct ble_gap_event *event, void *arg);
static void start_advertising(void);

// =============================================================================
// GATT Access Callbacks
// =============================================================================

static int gatt_status_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Status characteristic: read returns empty for now (notifications push data)
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_command_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        // Flatten the mbuf chain into a heap buffer and queue for processing
        // off the nimble_host task (whose stack is too small for command handling).
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len == 0 || om_len > 512) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        uint8_t *buf = malloc(om_len);
        if (!buf) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        uint16_t len = om_len;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, &len);
        if (rc != 0) {
            free(buf);
            return BLE_ATT_ERR_UNLIKELY;
        }

        ble_cmd_msg_t msg = { .data = buf, .length = len };
        if (xQueueSend(s_cmd_queue, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Command queue full, dropping command");
            free(buf);
        }

        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_response_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// =============================================================================
// GATT Service Definition
// =============================================================================

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Status (read + notify)
                .uuid = &s_char_status_uuid.u,
                .access_cb = gatt_status_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // Command (write)
                .uuid = &s_char_command_uuid.u,
                .access_cb = gatt_command_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                // Response (read + notify)
                .uuid = &s_char_response_uuid.u,
                .access_cb = gatt_response_access,
                .val_handle = &s_response_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }, // Terminator
        },
    },
    { 0 }, // Terminator
};

// =============================================================================
// GAP Event Handler
// =============================================================================

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "BLE client connected, conn_handle %d", s_conn_handle);

                // Request Apple-compliant connection parameters.  The
                // supervision_timeout of 600 (6 s) is the maximum macOS
                // accepts.  Without this, macOS's default (~1 s) is in
                // effect and the reconnection window is too short.
                struct ble_gap_upd_params conn_params = {
                    .itvl_min = 24,              // 30 ms
                    .itvl_max = 48,              // 60 ms
                    .latency = 0,
                    .supervision_timeout = 600,  // 6 seconds (Apple max)
                    .min_ce_len = 0,
                    .max_ce_len = 0,
                };
                ble_gap_update_params(s_conn_handle, &conn_params);

                wifi_cfg_ble_on_connect();
#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
                extern void wifi_cfg_improv_ble_on_connect_nimble(uint16_t conn_handle);
                wifi_cfg_improv_ble_on_connect_nimble(s_conn_handle);
#endif
            } else {
                ESP_LOGE(TAG, "Connection failed, status %d", event->connect.status);
                start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT: {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGI(TAG, "BLE client disconnected, reason %d (0x%03x)",
                     event->disconnect.reason, event->disconnect.reason);
            wifi_cfg_ble_on_disconnect();
#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
            extern void wifi_cfg_improv_ble_on_disconnect_nimble(void);
            wifi_cfg_improv_ble_on_disconnect_nimble();
#endif
            start_advertising();
            break;
        }

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGD(TAG, "Advertising complete");
            start_advertising();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGD(TAG, "Subscribe: handle=%d notify=%d indicate=%d",
                     event->subscribe.attr_handle,
                     event->subscribe.cur_notify,
                     event->subscribe.cur_indicate);
            if (event->subscribe.attr_handle == s_response_val_handle) {
                bool enabled = event->subscribe.cur_notify;
                wifi_cfg_ble_set_response_notify(enabled);
            }
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU changed to %d", event->mtu.value);
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(TAG, "Encryption change, status=%d", event->enc_change.status);
            if (event->enc_change.status != 0) {
                ESP_LOGW(TAG, "Encryption failed — deleting stale bond");
                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                    ble_store_util_delete_peer(&desc.peer_id_addr);
                }
            }
            break;

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // A previously bonded client is trying to pair again (e.g. after
            // the device was reflashed). Delete the stale bond and allow the
            // new pairing to proceed.
            struct ble_gap_conn_desc desc;
            ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            ble_store_util_delete_peer(&desc.peer_id_addr);
            ESP_LOGI(TAG, "Repeat pairing — deleted stale bond, retrying");
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        case BLE_GAP_EVENT_CONN_UPDATE:
            ESP_LOGD(TAG, "Conn params update, status=%d", event->conn_update.status);
            break;

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            ESP_LOGI(TAG, "Passkey action: %d", event->passkey.params.action);
            if (event->passkey.params.action == BLE_SM_IOACT_NONE) {
                struct ble_sm_io pk = { .action = BLE_SM_IOACT_NONE };
                ble_sm_inject_io(event->passkey.conn_handle, &pk);
            }
            break;

        case BLE_GAP_EVENT_NOTIFY_TX:
            ESP_LOGD(TAG, "Notify TX: handle=%d status=%d",
                     event->notify_tx.attr_handle, event->notify_tx.status);
            break;

        case BLE_GAP_EVENT_DATA_LEN_CHG:
            ESP_LOGD(TAG, "Data length changed: tx=%d rx=%d",
                     event->data_len_chg.max_tx_octets,
                     event->data_len_chg.max_rx_octets);
            break;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
        case BLE_GAP_EVENT_LINK_ESTAB:
            ESP_LOGD(TAG, "Link established");
            break;

        case BLE_GAP_EVENT_PARING_COMPLETE:
            ESP_LOGI(TAG, "Pairing complete, status=%d",
                     event->pairing_complete.status);
            break;
#endif

        default:
            ESP_LOGD(TAG, "Unhandled GAP event: %d", event->type);
            break;
    }
    return 0;
}

// =============================================================================
// Advertising
// =============================================================================

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 0x20;
    adv_params.itvl_max = 0x40;

    int rc;

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
    // Improv spec: Service UUID + Service Data MUST be in primary adv (not scan response).
    // NimBLE's ble_hs_adv_fields doesn't support service data, so use raw bytes.

    // Primary adv: Flags + Improv 128-bit UUID + Service Data (UUID16 0x4677)
    static const uint8_t improv_uuid_le[] = IMPROV_BLE_SVC_UUID_128;
    uint8_t adv_buf[31];
    int pos = 0;

    // Flags (3 bytes)
    adv_buf[pos++] = 2;
    adv_buf[pos++] = 0x01;  // AD type: Flags
    adv_buf[pos++] = 0x06;  // General Discoverable + BR/EDR Not Supported

    // Complete List of 128-bit UUIDs (18 bytes)
    adv_buf[pos++] = 17;
    adv_buf[pos++] = 0x07;  // AD type: Complete List of 128-bit UUIDs
    memcpy(&adv_buf[pos], improv_uuid_le, 16);
    pos += 16;

    // Service Data - 16 bit UUID (10 bytes)
    adv_buf[pos++] = 9;
    adv_buf[pos++] = 0x16;  // AD type: Service Data - 16 bit UUID
    adv_buf[pos++] = 0x77;  // UUID16 0x4677 in little-endian
    adv_buf[pos++] = 0x46;
    adv_buf[pos++] = wifi_cfg_improv_get_state();
    adv_buf[pos++] = wifi_cfg_improv_get_capabilities();
    adv_buf[pos++] = 0x00;  // Reserved
    adv_buf[pos++] = 0x00;
    adv_buf[pos++] = 0x00;
    adv_buf[pos++] = 0x00;

    rc = ble_gap_adv_set_data(adv_buf, pos);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set raw adv data, rc=%d", rc);
        return;
    }

    // Scan response: device name + custom 16-bit UUID (0xFFE0)
    size_t name_len = strlen(s_device_name);
    uint8_t rsp_buf[31];
    int rpos = 0;

    // Complete Local Name
    rsp_buf[rpos++] = (uint8_t)(name_len + 1);
    rsp_buf[rpos++] = 0x09;  // AD type: Complete Local Name
    memcpy(&rsp_buf[rpos], s_device_name, name_len);
    rpos += name_len;

    // Complete List of 16-bit UUIDs (custom service)
    rsp_buf[rpos++] = 3;
    rsp_buf[rpos++] = 0x03;  // AD type: Complete List of 16-bit UUIDs
    rsp_buf[rpos++] = 0xE0;  // 0xFFE0 in little-endian
    rsp_buf[rpos++] = 0xFF;

    rc = ble_gap_adv_rsp_set_data(rsp_buf, rpos);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to set scan response data, rc=%d", rc);
    }

#else
    // Without Improv: original layout — name + custom UUID in primary adv
    static const ble_uuid16_t adv_service_uuid = BLE_UUID16_INIT(WIFI_BLE_SVC_UUID);

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)s_device_name;
    fields.name_len = strlen(s_device_name);
    fields.name_is_complete = 1;
    fields.uuids16 = &adv_service_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields, rc=%d", rc);
        return;
    }
#endif

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                            &adv_params, gap_event_handler, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Failed to start advertising, rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising started: %s", s_device_name);
    }
}

// =============================================================================
// NimBLE Host Callbacks
// =============================================================================

static void ble_on_sync(void)
{
    // Use best available address
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address, rc=%d", rc);
        return;
    }

    // Clear any stale bonds left in NVS from a previous flash.  Without
    // this, a client that bonded before a reflash can trigger an LL-level
    // encryption timeout (reason 0x208) because the device still has the
    // old peer record but the keys no longer match.
    ble_store_clear();

    // Disable DLE from the device side — set suggested TX to the BLE
    // default of 27 bytes.  Must happen here (not init) because the
    // controller isn't synced during backend_init().
    ble_gap_write_sugg_def_data_len(27, 328);

    start_advertising();
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
}

static void ble_cmd_task(void *param)
{
    ble_cmd_msg_t msg;
    while (xQueueReceive(s_cmd_queue, &msg, portMAX_DELAY) == pdTRUE) {
        wifi_cfg_ble_on_command(msg.data, msg.length);
        free(msg.data);
    }
    vTaskDelete(NULL);
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// =============================================================================
// Backend Interface Implementation
// =============================================================================

esp_err_t wifi_cfg_ble_backend_notify_response(const uint8_t *data, size_t length)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, length);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_response_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Notify failed, rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

uint16_t wifi_cfg_ble_backend_get_mtu(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return 0;
    }
    return ble_att_mtu(s_conn_handle);
}

bool wifi_cfg_ble_backend_is_stack_running(void)
{
    // Cannot call ble_hs_is_enabled() before nimble_port_init() — the NimBLE
    // host data structures aren't allocated yet and the access will fault.
    // Check the BT controller status instead, which is always safe to query.
    return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
}

esp_err_t wifi_cfg_ble_backend_init(const char *device_name)
{
    strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
    s_device_name[sizeof(s_device_name) - 1] = '\0';

    if (wifi_cfg_ble_backend_is_stack_running()) {
        // Stack already running — service-only mode
        s_ble_stack_owned = false;
        ESP_LOGI(TAG, "NimBLE stack already running, registering service only");
    } else {
        // Full stack init
        s_ble_stack_owned = true;

        esp_err_t ret = nimble_port_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // Configure host callbacks
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    // No encryption required for provisioning characteristics.
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    // Set preferred MTU
    int mtu_rc = ble_att_set_preferred_mtu(517);
    if (mtu_rc != 0) {
        ESP_LOGW(TAG, "Failed to set preferred MTU, rc=%d", mtu_rc);
    }

    // Initialize GAP service (required for device name / appearance).
    // Must be called before ble_svc_gap_device_name_set() — init
    // resets the name to the default ("nimble").
    ble_svc_gap_init();
    ble_svc_gap_device_name_set(s_device_name);

    // Deliberately skip ble_svc_gatt_init().  The standard GATT service
    // registers Service Changed and Database Hash characteristics.  On
    // reconnection, macOS CoreBluetooth tries to validate its cached
    // GATT data against these characteristics and gets stuck for ~30 s
    // before timing out.  Without the GATT service, CoreBluetooth falls
    // back to fresh service discovery, which completes immediately.
    // The GATT service is technically mandatory per spec, but omitting
    // it is widely practiced (ESPHome, many BLE peripherals) and all
    // major BLE centrals handle its absence gracefully.

    // Register custom GATT service
    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed, rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed, rc=%d", rc);
        return ESP_FAIL;
    }

#ifdef CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE
    // Register Improv GATT service alongside custom service.
    // NOTE: NimBLE requires all services to be registered before ble_gatts_start().
    // In "service-only" mode (stack already running), this may fail if ble_gatts_start()
    // was already called. In that case, Improv BLE will not be available, and the user
    // must ensure BLE init happens before the host stack is fully started.
    extern const struct ble_gatt_svc_def wifi_cfg_improv_nimble_svcs[];
    rc = ble_gatts_count_cfg(wifi_cfg_improv_nimble_svcs);
    if (rc != 0) {
        ESP_LOGW(TAG, "Improv ble_gatts_count_cfg failed, rc=%d (stack already started?)", rc);
    } else {
        rc = ble_gatts_add_svcs(wifi_cfg_improv_nimble_svcs);
        if (rc != 0) {
            ESP_LOGW(TAG, "Improv ble_gatts_add_svcs failed, rc=%d (stack already started?)", rc);
        } else {
            ESP_LOGI(TAG, "Improv BLE GATT service registered");
        }
    }
#endif

    // Create command processing queue and task (runs off nimble_host stack)
    s_cmd_queue = xQueueCreate(BLE_CMD_QUEUE_DEPTH, sizeof(ble_cmd_msg_t));
    if (!s_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        if (s_ble_stack_owned) {
            nimble_port_deinit();
        }
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret2 = xTaskCreate(ble_cmd_task, "ble_cmd", BLE_CMD_TASK_STACK_SIZE,
                                   NULL, 5, &s_cmd_task);
    if (ret2 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create command task");
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        if (s_ble_stack_owned) {
            nimble_port_deinit();
        }
        return ESP_ERR_NO_MEM;
    }

    // Start NimBLE host task only if we own the stack
    if (s_ble_stack_owned) {
        nimble_port_freertos_init(nimble_host_task);
    } else {
        // Stack is already running — trigger advertising via on_sync path
        start_advertising();
    }

    return ESP_OK;
}

esp_err_t wifi_cfg_ble_backend_start(void)
{
    start_advertising();
    return ESP_OK;
}

esp_err_t wifi_cfg_ble_backend_stop(void)
{
    // Disconnect active client
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        for (int i = 0; i < 50 && s_conn_handle != BLE_HS_CONN_HANDLE_NONE; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // Stop advertising (do NOT reset GATT or stop nimble port)
    ble_gap_adv_stop();
    return ESP_OK;
}

esp_err_t wifi_cfg_ble_backend_deinit(void)
{
    // Disconnect active client
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        // Wait for async disconnect to complete (gap_event_handler sets
        // s_conn_handle = BLE_HS_CONN_HANDLE_NONE on disconnect event)
        for (int i = 0; i < 50 && s_conn_handle != BLE_HS_CONN_HANDLE_NONE; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // Stop advertising
    ble_gap_adv_stop();

    // Reset GATT services so our service is unregistered.
    // ble_gatts_reset() removes all services; re-register the mandatory ones
    // and commit with ble_gatts_start() so the server is in a stable state.
    int rc = ble_gatts_reset();
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gatts_reset failed, rc=%d", rc);
    }
    ble_svc_gap_init();
    ble_svc_gap_device_name_set(s_device_name);
    ble_gatts_start();

    // Full stack teardown only if we own it
    if (s_ble_stack_owned) {
        int rc = nimble_port_stop();
        if (rc != 0) {
            ESP_LOGE(TAG, "nimble_port_stop failed, rc=%d", rc);
        }

        rc = nimble_port_deinit();
        if (rc != 0) {
            ESP_LOGE(TAG, "nimble_port_deinit failed, rc=%d", rc);
        }
    }

    // Clean up command processing task and queue
    if (s_cmd_task) {
        vTaskDelete(s_cmd_task);
        s_cmd_task = NULL;
    }
    if (s_cmd_queue) {
        // Drain any pending messages to free their heap buffers
        ble_cmd_msg_t msg;
        while (xQueueReceive(s_cmd_queue, &msg, 0) == pdTRUE) {
            free(msg.data);
        }
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
    }

    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_ble_stack_owned = false;

    return ESP_OK;
}

#endif // CONFIG_WIFI_CFG_ENABLE_BLE && CONFIG_BT_NIMBLE_ENABLED
