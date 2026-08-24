// components/bsp/include/bsp_storage.h
// 基于 ESP-IDF NVS(Non-Volatile Storage) 的掉电不丢失键值存储抽象。
// 打卡应用用它保存计时状态和历史记录,避免断电/重启后数据丢失。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// 单条打卡记录:一次"开始-停止"会话。
typedef struct {
    uint32_t start_ts;        // Unix 时间戳(秒);0 表示空槽
    uint32_t duration_sec;    // 本次会话持续秒数
} bsp_punch_record_t;

// NVS 初始化(内部会调用 nvs_flash_init,幂等)。
esp_err_t bsp_storage_init(void);

// ---------- 计时运行时状态(正在打卡时,每秒会写入开始时间戳) ----------
esp_err_t bsp_storage_set_running(bool running);
bool      bsp_storage_get_running(void);

esp_err_t bsp_storage_set_start_ts(uint32_t ts);
uint32_t  bsp_storage_get_start_ts(void);

// 累计总打卡秒数(所有已完成会话之和,不含进行中的)
esp_err_t bsp_storage_set_total_sec(uint32_t sec);
uint32_t  bsp_storage_get_total_sec(void);

// ---------- 历史记录环形缓冲区(最多 BSP_PUNCH_MAX_RECORDS 条) ----------
#define BSP_PUNCH_MAX_RECORDS 50

// 记录总条数(已写过的记录数,用于环形覆盖判定)
esp_err_t bsp_storage_set_record_count(uint32_t n);
uint32_t  bsp_storage_get_record_count(void);

// 按索引 0..BSP_PUNCH_MAX_RECORDS-1 读写一条记录。
esp_err_t bsp_storage_set_record(uint32_t idx, const bsp_punch_record_t *rec);
esp_err_t bsp_storage_get_record(uint32_t idx, bsp_punch_record_t *rec);
