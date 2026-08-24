// components/bsp/src/bsp_storage.c
// NVS 掉电不丢失存储实现。使用 "punchcard" 命名空间,所有读写均阻塞式同步完成。
// NVS 单条值最大为 1984 字节,这里每条记录 8 字节,50 条也远未到上限。
#include "bsp_storage.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "bsp_storage";
static const char *NVS_NS = "punchcard";

// 每个 NVS key 名不超过 15 字符(NVS 限制)。
#define KEY_RUNNING   "run"
#define KEY_START_TS  "start_ts"
#define KEY_TOTAL_SEC "total_sec"
#define KEY_REC_CNT   "rec_cnt"
// 记录 i: key = "r%02u_s" / "r%02u_d"  (start / duration)

static bool s_inited = false;

esp_err_t bsp_storage_init(void) {
    if (s_inited) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需擦除后重建: %s", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    s_inited = true;
    return ESP_OK;
}

// ---------- 通用 u8 / u32 辅助 ----------
static esp_err_t put_u8(const char *key, uint8_t v) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, key, v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static uint8_t get_u8(const char *key, uint8_t def) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return def;
    uint8_t v = def;
    nvs_get_u8(h, key, &v);
    nvs_close(h);
    return v;
}

static esp_err_t put_u32(const char *key, uint32_t v) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, key, v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static uint32_t get_u32(const char *key, uint32_t def) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return def;
    uint32_t v = def;
    nvs_get_u32(h, key, &v);
    nvs_close(h);
    return v;
}

// ---------- 计时运行时状态 ----------
esp_err_t bsp_storage_set_running(bool running) {
    return put_u8(KEY_RUNNING, running ? 1 : 0);
}

bool bsp_storage_get_running(void) {
    return get_u8(KEY_RUNNING, 0) != 0;
}

esp_err_t bsp_storage_set_start_ts(uint32_t ts) {
    return put_u32(KEY_START_TS, ts);
}

uint32_t bsp_storage_get_start_ts(void) {
    return get_u32(KEY_START_TS, 0);
}

esp_err_t bsp_storage_set_total_sec(uint32_t sec) {
    return put_u32(KEY_TOTAL_SEC, sec);
}

uint32_t bsp_storage_get_total_sec(void) {
    return get_u32(KEY_TOTAL_SEC, 0);
}

// ---------- 记录条数 ----------
esp_err_t bsp_storage_set_record_count(uint32_t n) {
    return put_u32(KEY_REC_CNT, n);
}

uint32_t bsp_storage_get_record_count(void) {
    return get_u32(KEY_REC_CNT, 0);
}

// ---------- 单条记录 ----------
esp_err_t bsp_storage_set_record(uint32_t idx, const bsp_punch_record_t *rec) {
    if (idx >= BSP_PUNCH_MAX_RECORDS || rec == NULL) return ESP_ERR_INVALID_ARG;
    char k1[16], k2[16];
    snprintf(k1, sizeof(k1), "r%02lu_s", (unsigned long)idx);
    snprintf(k2, sizeof(k2), "r%02lu_d", (unsigned long)idx);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, k1, rec->start_ts);
    if (err == ESP_OK) err = nvs_set_u32(h, k2, rec->duration_sec);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t bsp_storage_get_record(uint32_t idx, bsp_punch_record_t *rec) {
    if (idx >= BSP_PUNCH_MAX_RECORDS || rec == NULL) return ESP_ERR_INVALID_ARG;
    char k1[16], k2[16];
    snprintf(k1, sizeof(k1), "r%02lu_s", (unsigned long)idx);
    snprintf(k2, sizeof(k2), "r%02lu_d", (unsigned long)idx);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        memset(rec, 0, sizeof(*rec));
        return err;
    }
    rec->start_ts     = 0;
    rec->duration_sec = 0;
    nvs_get_u32(h, k1, &rec->start_ts);
    nvs_get_u32(h, k2, &rec->duration_sec);
    nvs_close(h);
    return ESP_OK;
}
