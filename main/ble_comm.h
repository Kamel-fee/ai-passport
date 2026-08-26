#pragma once

#include "esp_err.h"
#include "task_timer.h"

esp_err_t ble_comm_init(void);
void      ble_comm_deinit(void);

bool ble_comm_is_connected(void);

// BLE 总状态:0=未初始化/失败 1=广播中 2=已连接(供屏幕显示诊断)
int ble_comm_get_state(void);

esp_err_t ble_comm_send_task_list(void);
esp_err_t ble_comm_send_history(void);
esp_err_t ble_comm_send_runtime(void);

void ble_comm_set_task_received_cb(void (*cb)(const timer_task_t *task));
void ble_comm_set_history_request_cb(void (*cb)(void));
