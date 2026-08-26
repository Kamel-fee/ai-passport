#include "ble_comm.h"
#include "task_timer.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ble_comm";

#define PROFILE_APP_ID   0
#define NUM_HANDLES      5

static esp_gatt_if_t s_gatts_if;
static bool s_inited;
static bool s_connected;
static uint16_t s_service_handle;
static uint16_t s_char_write_handle;
static uint16_t s_char_read_handle;

static uint8_t s_task_write_buf[2048];
static uint16_t s_task_write_len;
static uint8_t s_read_buf[2048];
static uint16_t s_read_len;
static uint8_t s_history_buf[2048];
static uint16_t s_history_len;

static void (*s_task_received_cb)(const timer_task_t *task);

static const char *DEVICE_NAME = "FoloToy-Timer";

static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC,
};

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0x100,
    .adv_int_max = 0x100,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t s_adv_config_done;
#define ADV_CONFIG_FLAG (1 << 0)

static esp_gatt_char_prop_t s_char_write_prop = 0;
static esp_gatt_char_prop_t s_char_read_prop = 0;

static esp_attr_value_t s_char_write_val = {
    .attr_max_len = 512,
    .attr_len = 0,
    .attr_value = NULL,
};

static esp_attr_value_t s_char_read_val = {
    .attr_max_len = 2048,
    .attr_len = 0,
    .attr_value = NULL,
};

static void build_task_list_response(void)
{
    int count = timer_task_count();
    uint16_t offset = 0;

    memcpy(&s_read_buf[offset], &count, sizeof(int));
    offset += sizeof(int);

    for (int i = 0; i < count && offset < sizeof(s_read_buf) - sizeof(timer_task_t); i++) {
        timer_task_t task;
        if (timer_task_load(i, &task) == ESP_OK) {
            memcpy(&s_read_buf[offset], &task, sizeof(timer_task_t));
            offset += sizeof(timer_task_t);
        }
    }
    s_read_len = offset;
}

static void build_history_response(void)
{
    int count = timer_history_count();
    uint16_t offset = 0;

    memcpy(&s_history_buf[offset], &count, sizeof(int));
    offset += sizeof(int);

    for (int i = 0; i < count && offset < sizeof(s_history_buf) - sizeof(timer_history_t); i++) {
        timer_history_t record;
        if (timer_history_load(i, &record) == ESP_OK) {
            memcpy(&s_history_buf[offset], &record, sizeof(timer_history_t));
            offset += sizeof(timer_history_t);
        }
    }
    s_history_len = offset;
}

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "adv data set complete, status=%d", param->adv_data_cmpl.status);
        s_adv_config_done &= ~ADV_CONFIG_FLAG;
        if (s_adv_config_done == 0) {
            esp_ble_gap_start_advertising(&s_adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "adv start failed: %d", param->adv_start_cmpl.status);
        } else {
            ESP_LOGI(TAG, "advertising started as %s", DEVICE_NAME);
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "adv stopped");
        break;
    default:
        break;
    }
}

static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                       esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        if (param->reg.status == ESP_GATT_OK) {
            s_gatts_if = gatts_if;
            ESP_LOGI(TAG, "GATT registered, if=%d", gatts_if);

            esp_ble_gap_set_device_name(DEVICE_NAME);

            esp_err_t err = esp_ble_gap_config_adv_data(&s_adv_data);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "config_adv_data failed: %s, starting adv directly", esp_err_to_name(err));
                esp_ble_gap_start_advertising(&s_adv_params);
            } else {
                s_adv_config_done |= ADV_CONFIG_FLAG;
            }

            esp_gatt_srvc_id_t service_id = {
                .is_primary = true,
                .id.inst_id = 0x00,
                .id.uuid.len = ESP_UUID_LEN_128,
                .id.uuid.uuid.uuid128 = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                                         0x00, 0x10, 0x00, 0x00, 0xFF, 0xE0, 0x00, 0x00},
            };
            esp_ble_gatts_create_service(gatts_if, &service_id, NUM_HANDLES);
        } else {
            ESP_LOGE(TAG, "GATT reg failed, status=%d", param->reg.status);
        }
        break;

    case ESP_GATTS_CREATE_EVT:
        if (param->create.status == ESP_GATT_OK) {
            s_service_handle = param->create.service_handle;
            esp_ble_gatts_start_service(s_service_handle);

            esp_bt_uuid_t write_uuid = {
                .len = ESP_UUID_LEN_128,
                .uuid.uuid128 = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                                  0x00, 0x10, 0x00, 0x00, 0xFF, 0xE1, 0x00, 0x00},
            };
            s_char_write_prop = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
            esp_ble_gatts_add_char(s_service_handle, &write_uuid,
                                    ESP_GATT_PERM_WRITE,
                                    s_char_write_prop,
                                    &s_char_write_val, NULL);

            esp_bt_uuid_t read_uuid = {
                .len = ESP_UUID_LEN_128,
                .uuid.uuid128 = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
                                 0x00, 0x10, 0x00, 0x00, 0xFF, 0xE2, 0x00, 0x00},
            };
            s_char_read_prop = ESP_GATT_CHAR_PROP_BIT_READ;
            esp_ble_gatts_add_char(s_service_handle, &read_uuid,
                                    ESP_GATT_PERM_READ,
                                    s_char_read_prop,
                                    &s_char_read_val, NULL);
        }
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        if (param->add_char.status == ESP_GATT_OK) {
            if (param->add_char.attr_handle <= s_service_handle + 1) {
                s_char_write_handle = param->add_char.attr_handle;
            } else {
                s_char_read_handle = param->add_char.attr_handle;
            }
            ESP_LOGI(TAG, "char added: handle=%d", param->add_char.attr_handle);
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        s_connected = true;
        ESP_LOGI(TAG, "BLE connected, conn_id=%d", param->connect.conn_id);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        s_connected = false;
        esp_ble_gap_start_advertising(&s_adv_params);
        ESP_LOGI(TAG, "BLE disconnected");
        break;

    case ESP_GATTS_WRITE_EVT: {
        uint16_t handle = param->write.handle;
        uint16_t len = param->write.len;
        uint8_t *value = param->write.value;

        if (handle == s_char_write_handle) {
            if (len >= sizeof(int) + sizeof(timer_task_t) && s_task_received_cb) {
                int count;
                memcpy(&count, value, sizeof(int));
                uint16_t offset = sizeof(int);
                for (int i = 0; i < count && offset + sizeof(timer_task_t) <= len; i++) {
                    timer_task_t task;
                    memcpy(&task, &value[offset], sizeof(timer_task_t));
                    int idx = timer_task_count();
                    timer_task_save(&task, idx);
                    offset += sizeof(timer_task_t);
                    ESP_LOGI(TAG, "task '%s' saved (idx=%d)", task.name, idx);
                    if (s_task_received_cb) s_task_received_cb(&task);
                }
            }
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                         param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    }

    case ESP_GATTS_READ_EVT: {
        uint16_t handle = param->read.handle;
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(rsp));

        if (handle == s_char_read_handle) {
            build_task_list_response();
            rsp.attr_value.len = s_read_len;
            if (s_read_len > 0) {
                memcpy(rsp.attr_value.value, s_read_buf, s_read_len);
            }
        } else {
            rsp.attr_value.len = 0;
        }
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                     param->read.trans_id, ESP_GATT_OK, &rsp);
        break;
    }

    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "service started");
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

    err = esp_ble_gatts_register_callback(gatts_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gatts_register: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gap_register_callback(gap_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gap_register: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gatts_app_register(PROFILE_APP_ID);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gatts_app_register: %s", esp_err_to_name(err));
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "BLE GATT server initialized, advertising as %s", DEVICE_NAME);
    return ESP_OK;
}

void ble_comm_deinit(void)
{
    if (!s_inited) return;
    esp_ble_gap_stop_advertising();
    esp_ble_gatts_app_unregister(s_gatts_if);
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
