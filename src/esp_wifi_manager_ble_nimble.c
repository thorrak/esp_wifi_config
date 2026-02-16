/**
 * @file esp_wifi_manager_ble_nimble.c
 * @brief BLE backend using the NimBLE host stack
 */

#include "sdkconfig.h"

#if defined(CONFIG_WIFI_MGR_ENABLE_BLE) && defined(CONFIG_BT_NIMBLE_ENABLED)

#include "esp_wifi_manager_ble_int.h"
#include "esp_log.h"
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

static const char *TAG = "wifi_mgr_ble_nb";

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
                wifi_mgr_ble_on_connect();
            } else {
                ESP_LOGE(TAG, "Connection failed, status %d", event->connect.status);
                start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGI(TAG, "BLE client disconnected, reason %d", event->disconnect.reason);
            wifi_mgr_ble_on_disconnect();
            start_advertising();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGD(TAG, "Advertising complete");
            start_advertising();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_response_val_handle) {
                bool enabled = event->subscribe.cur_notify;
                wifi_mgr_ble_set_response_notify(enabled);
                ESP_LOGI(TAG, "Response notify %s", enabled ? "enabled" : "disabled");
            }
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU changed to %d", event->mtu.value);
            break;

        default:
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

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)s_device_name;
    fields.name_len = strlen(s_device_name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields, rc=%d", rc);
        return;
    }

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
        wifi_mgr_ble_on_command(msg.data, msg.length);
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

esp_err_t wifi_mgr_ble_backend_notify_response(const uint8_t *data, size_t length)
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

esp_err_t wifi_mgr_ble_backend_init(const char *device_name)
{
    strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
    s_device_name[sizeof(s_device_name) - 1] = '\0';

    // Initialize NimBLE
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure host callbacks
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    // Set preferred MTU
    int mtu_rc = ble_att_set_preferred_mtu(517);
    if (mtu_rc != 0) {
        ESP_LOGW(TAG, "Failed to set preferred MTU, rc=%d", mtu_rc);
    }

    // Set device name
    ble_svc_gap_device_name_set(s_device_name);

    // Initialize GAP and GATT services
    ble_svc_gap_init();
    ble_svc_gatt_init();

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

    // Create command processing queue and task (runs off nimble_host stack)
    s_cmd_queue = xQueueCreate(BLE_CMD_QUEUE_DEPTH, sizeof(ble_cmd_msg_t));
    if (!s_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        nimble_port_deinit();
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret2 = xTaskCreate(ble_cmd_task, "ble_cmd", BLE_CMD_TASK_STACK_SIZE,
                                   NULL, 5, &s_cmd_task);
    if (ret2 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create command task");
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        nimble_port_deinit();
        return ESP_ERR_NO_MEM;
    }

    // Start NimBLE host task
    nimble_port_freertos_init(nimble_host_task);

    return ESP_OK;
}

esp_err_t wifi_mgr_ble_backend_deinit(void)
{
    int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_stop failed, rc=%d", rc);
    }

    rc = nimble_port_deinit();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_deinit failed, rc=%d", rc);
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

    return ESP_OK;
}

#endif // CONFIG_WIFI_MGR_ENABLE_BLE && CONFIG_BT_NIMBLE_ENABLED
