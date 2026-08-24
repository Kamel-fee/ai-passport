#include "ble_comm.h"
#include "task_timer.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ble_comm";

#define PROFILE_APP_ID   0x00
#define GATT_DB_MAX_SIZE 15

enum {
    IDX_SVC = 0,
    IDX_CHAR_TASK_WRITE_DECL,
    IDX_CHAR_TASK_WRITE_VAL,
    IDX_CHAR_TASK_READ_DECL,
    IDX_CHAR_TASK_READ_VAL,
    IDX_CHAR_HISTORY_READ_DECL,
    IDX_CHAR_HISTORY_READ_VAL,
    IDX_CHAR_CMD_WRITE_DECL,
    IDX_CHAR_CMD_WRITE_VAL,
    IDX_CHAR_NB,
};

static uint16_t s_gatts_db[IDX_NB];

static esp_ble_gatts_cb_param_t s_gatts_cb_param;
static bool s_inited;
static bool s_connected;
static esp_gatts_if_t s_gatts_if;

static void (*s_task_received_cb)(const timer_task_t *task);
static void (*s_history_request_cb)(void);

#define CHAR_TASK_WRITE_UUID   "0000ffe1-0000-1000-8000-00805f9b34fb"
#define CHAR_TASK_READ_UUID    "0000ffe2-0000-1000-8000-00805f9b34fb"
#define CHAR_HISTORY_READ_UUID "0000ffe3-0000-1000-8000-00805f9b34fb"
#define CHAR_CMD_WRITE_UUID    "0000ffe4-0000-1000-8000-00805f9b34fb"

static uint8_t s_task_read_buf[2048];
static uint16_t s_task_read_len;
static uint8_t s_history_read_buf[2048];
static uint16_t s_history_read_len;

static void parse_task_from_ble(const uint8_t *data, uint16_t len)
{
    if (len < sizeof(timer_task_t)) {
        ESP_LOGW(TAG, "task data too short: %d", len);
        return;
    }

    timer_task_t task;
    memcpy(&task, data, sizeof(timer_task_t));
    if (task.node_count > TIMER_MAX_NODES) task.node_count = TIMER_MAX_NODES;
    if (task.node_count == 0) return;

    int idx = timer_task_count();
    timer_task_save(&task, idx);
    ESP_LOGI(TAG, "task '%s' saved (idx=%d, nodes=%d)", task.name, idx, task.node_count);

    if (s_task_received_cb) s_task_received_cb(&task);
}

static void build_task_list_response(void)
{
    int count = timer_task_count();
    uint16_t offset = 0;

    memcpy(&s_task_read_buf[offset], &count, sizeof(int));
    offset += sizeof(int);

    for (int i = 0; i < count && offset < sizeof(s_task_read_buf) - sizeof(timer_task_t); i++) {
        timer_task_t task;
        if (timer_task_load(i, &task) == ESP_OK) {
            memcpy(&s_task_read_buf[offset], &task, sizeof(timer_task_t));
            offset += sizeof(timer_task_t);
        }
    }
    s_task_read_len = offset;
}

static void build_history_response(void)
{
    int count = timer_history_count();
    uint16_t offset = 0;

    memcpy(&s_history_read_buf[offset], &count, sizeof(int));
    offset += sizeof(int);

    for (int i = 0; i < count && offset < sizeof(s_history_read_buf) - sizeof(timer_history_t); i++) {
        timer_history_t record;
        if (timer_history_load(i, &record) == ESP_OK) {
            memcpy(&s_history_read_buf[offset], &record, sizeof(timer_history_t));
            offset += sizeof(timer_history_t);
        }
    }
    s_history_read_len = offset;
}

static void gatts_cb(esp_gatts_if_t gatts_if,
                       const esp_ble_gatts_cb_param_t *param)
{
    (void)gatts_if;

    switch (param->gatts_cb_type) {
    case ESP_GATTS_CONNECT_EVT:
        s_connected = true;
        ESP_LOGI(TAG, "BLE connected");
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        s_connected = false;
        esp_ble_gap_start_advertising();
        ESP_LOGI(TAG, "BLE disconnected");
        break;

    case ESP_GATTS_WRITE_EVT: {
        uint16_t handle = param->write.handle;
        uint16_t len = param->write.len;
        uint8_t *value = param->write.value;

        if (handle == s_gatts_db[IDX_CHAR_TASK_WRITE_VAL]) {
            parse_task_from_ble(value, len);
        } else if (handle == s_gatts_db[IDX_CHAR_CMD_WRITE_VAL]) {
            if (len >= 1 && value[0] == 0x01) {
                ESP_LOGI(TAG, "BLE: request task list");
                build_task_list_response();
            } else if (len >= 1 && value[0] == 0x02) {
                ESP_LOGI(TAG, "BLE: request history");
                build_history_response();
                if (s_history_request_cb) s_history_request_cb();
            } else if (len >= 1 && value[0] == 0x03) {
                ESP_LOGI(TAG, "BLE: clear history");
                timer_history_clear();
            }
        }
        break;
    }

    case ESP_GATTS_READ_EVT: {
        uint16_t handle = param->read.handle;
        esp_gatt_status_t status = ESP_GATT_OK;
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(rsp));

        if (handle == s_gatts_db[IDX_CHAR_TASK_READ_VAL]) {
            rsp.value_len = s_task_read_len;
            if (s_task_read_len > 0) {
                memcpy(rsp.value, s_task_read_buf, s_task_read_len);
            }
        } else if (handle == s_gatts_db[IDX_CHAR_HISTORY_READ_VAL]) {
            rsp.value_len = s_history_read_len;
            if (s_history_read_len > 0) {
                memcpy(rsp.value, s_history_read_buf, s_history_read_len);
            }
        } else {
            status = ESP_GATT_INVALID_HANDLE;
        }
        esp_ble_gatts_send_response(s_gatts_if, param->read.conn_id,
                                     param->read.trans_id, status, &rsp);
        break;
    }

    case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
        if (param->create.status == ESP_GATT_OK) {
            esp_attr_value_t gatts_demo_attr_tab[IDX_CHAR_NB] = {
                [IDX_SVC] =
                    {{ESP_GATT_AUTO_RSP,
                      {ESP_BLE_UUID_16(ESP_GATT_UUID_PRI_SERVICE),
                       0, ESP_GATT_PERM_READ,
                       IDX_CHAR_TASK_WRITE_VAL, 0}},
                [IDX_CHAR_TASK_WRITE_DECL] =
                    {{ESP_GATT_AUTO_RSP,
                      {ESP_BLE_UUID_16(ESP_GATT_UUID_CHAR_DECLARATION),
                       ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                       ESP_GATT_PERM_WRITE,
                       IDX_CHAR_TASK_WRITE_VAL, 0}},
                [IDX_CHAR_TASK_WRITE_VAL] =
                    {{ESP_GATT_AUTO_RSP,
                      {ESP_BLE_UUID_128(CHAR_TASK_WRITE_UUID),
                       0, ESP_GATT_PERM_WRITE,
                       0, 512}},
                [IDX_CHAR_TASK_READ_DECL] =
                    {{ESP_GATT_AUTO_RSP,
                      {ESP_BLE_UUID_16(ESP_GATT_UUID_CHAR_DECLARATION),
                       ESP_GATT_CHAR_PROP_BIT_READ,
                       ESP_GATT_PERM_READ,
                       IDX_CHAR_TASK_READ_VAL, 0}},
                [IDX_CHAR_TASK_READ_VAL] =
                    {{ESP_GATT_AUTO_RSP,
                      {ESP_BLE_UUID_128(CHAR_TASK_READ_UUID),
                       0, ESP_GATT_PERM_READ,
                       0, 2048}},
                [IDX_CHAR_HISTORY_READ_DECL] =
                    {{ESP_GATT_AUTO_RSP,
                      {ESP_BLE_UUID_16(ESP_GATT_UUID_CHAR_DECLARATION),
                       ESP_GATT_CHAR_PROP_BIT_READ,
                       ESP_GATT_PERM_READ,
                       IDX_CHAR_HISTORY_READ_VAL, 0}},
                [IDX_CHAR_HISTORY_READ_VAL] =
                    {{ESP_GATT_AUTO_RSP,
                      {ESP_BLE_UUID_128(CHAR_HISTORY_READ_UUID),
                       0, ESP_GATT_PERM_READ,
                       0, 2048}},
                [IDX_CHAR_CMD_WRITE_DECL] =
                    {{ESP_GATT_AUTO_RSP,
                      {ESP_BLE_UUID_16(ESP_GATT_UUID_CHAR_DECLARATION),
                       ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                       ESP_GATT_PERM_WRITE,
                       IDX_CHAR_CMD_WRITE_VAL, 0}},
                [IDX_CHAR_CMD_WRITE_VAL] =
                    {{ESP_GATT_AUTO_RSP,
                      {ESP_BLE_UUID_128(CHAR_CMD_WRITE_UUID),
                       0, ESP_GATT_PERM_WRITE,
                       0, 64}},
            };
            esp_ble_gatts_attr_db_init_by_uuid(
                PROFILE_APP_ID, gatts_demo_attr_tab,
                IDX_CHAR_NB, IDX_SVC);
        }
        break;
    }

    default:
        break;
    }
}

static void gap_cb(esp_ble_gap_cb_param_t *param)
{
    switch (param->gap_cb_type) {
    case ESP_BLE_GAP_CONGEST_EVT:
        break;
    case ESP_BLE_GAP_ENC_EVT:
        break;
    case ESP_BLE_GAP_PASS_NOTIFY_EVT:
        break;
    case ESP_BLE_GAP_DH_CP_EVT:
        break;
    case ESP_BLE_GAP_AUTH_KEY_EVT:
        break;
    case ESP_BLE_GAP_SEC_EVT:
        break;
    case ESP_BLE_GAP_BLE_NAME_EVT:
        break;
    case ESP_BLE_GAP_BLE_ADDR_EVT:
        break;
    case ESP_BLE_GAP_SCAN_PARAM_EVT:
        break;
    case ESP_BLE_GAP_SET_PARAMS_EVT:
        break;
    case ESP_BLE_GAP_ADV_DATA_SET_EVT:
        break;
    case ESP_BLE_GAP_ADV_DATA_UPDATE_EVT:
        break;
    case ESP_BLE_GAP_MAX:
        break;
    default:
        break;
    }
}

esp_err_t ble_comm_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t err;

    err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "release classic bt mem: %s", esp_err_to_name(err));
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bt_controller_init: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bt_controller_enable: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_bluedroid_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid_init: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid_enable: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gap_register_callback(gap_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gap_register: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gatts_register_callback(gatts_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gatts_register: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gatts_app_register(PROFILE_APP_ID);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gatts_app_register: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gatts_app_local_cfg_req(PROFILE_APP_ID);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gatts_app_local_cfg_req: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gatts_app_create(PROFILE_APP_ID);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gatts_app_create: %s", esp_err_to_name(err));
        return err;
    }

    esp_ble_gap_set_device_name("FoloToy-Timer");

    esp_ble_gap_config_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .min_interval = 0x0006,
        .max_interval = 0x0010,
        .appearance = 0x00,
        .manufacturer_len = 0,
        .p_manufacturer_data = NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = 16,
        .p_service_uuid = NULL,
        .flag = {
            .general_scan_disc = 1,
        }
    };
    esp_ble_gap_config_adv_data(&adv_data);

    err = esp_ble_gap_start_advertising();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start_advertising: %s", esp_err_to_name(err));
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "BLE GATT server initialized, advertising as FoloToy-Timer");
    return ESP_OK;
}

void ble_comm_deinit(void)
{
    if (!s_inited) return;
    esp_ble_gap_stop_advertising();
    s_connected = false;
    s_inited = false;
    ESP_LOGI(TAG, "BLE deinitialized");
}

bool ble_comm_is_connected(void)
{
    return s_connected;
}

esp_err_t ble_comm_send_task_list(void)
{
    build_task_list_response();
    return ESP_OK;
}

esp_err_t ble_comm_send_history(void)
{
    build_history_response();
    return ESP_OK;
}

void ble_comm_set_task_received_cb(void (*cb)(const timer_task_t *task))
{
    s_task_received_cb = cb;
}

void ble_comm_set_history_request_cb(void (*cb)(void))
{
    s_history_request_cb = cb;
}
