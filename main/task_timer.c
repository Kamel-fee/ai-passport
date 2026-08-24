#include "task_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "timer_store";
static const char *NVS_NAMESPACE = "timer_data";

#define KEY_TASK_PREFIX    "task_"
#define KEY_TASK_COUNT     "task_cnt"
#define KEY_HISTORY_PREFIX "hist_"
#define KEY_HISTORY_COUNT  "hist_cnt"
#define KEY_RUNTIME        "runtime"
#define KEY_TIME_BASE      "time_base"

static nvs_handle_t s_nvs;
static bool s_inited;

static char key_buf[32];

static void make_key(const char *prefix, int idx, char *out, size_t out_sz)
{
    snprintf(out, out_sz, "%s%d", prefix, idx);
}

esp_err_t timer_storage_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "storage ready");
    return ESP_OK;
}

esp_err_t timer_task_save(const timer_task_t *task, int idx)
{
    if (!s_inited) timer_storage_init();
    if (idx < 0 || idx >= TIMER_MAX_TASKS) return ESP_ERR_INVALID_ARG;

    make_key(KEY_TASK_PREFIX, idx, key_buf, sizeof(key_buf));
    esp_err_t err = nvs_set_blob(s_nvs, key_buf, task, sizeof(timer_task_t));
    if (err != ESP_OK) return err;

    int cnt = timer_task_count();
    if (idx >= cnt) {
        nvs_set_u8(s_nvs, KEY_TASK_COUNT, (uint8_t)(idx + 1));
    }
    return nvs_commit(s_nvs);
}

esp_err_t timer_task_load(int idx, timer_task_t *task)
{
    if (!s_inited) timer_storage_init();
    if (idx < 0 || idx >= TIMER_MAX_TASKS) return ESP_ERR_INVALID_ARG;

    make_key(KEY_TASK_PREFIX, idx, key_buf, sizeof(key_buf));
    size_t sz = sizeof(timer_task_t);
    esp_err_t err = nvs_get_blob(s_nvs, key_buf, task, &sz);
    return err;
}

esp_err_t timer_task_delete(int idx)
{
    if (!s_inited) timer_storage_init();
    if (idx < 0 || idx >= TIMER_MAX_TASKS) return ESP_ERR_INVALID_ARG;

    int cnt = timer_task_count();
    if (idx < cnt - 1) {
        timer_task_t next;
        for (int i = idx; i < cnt - 1; i++) {
            make_key(KEY_TASK_PREFIX, i + 1, key_buf, sizeof(key_buf));
            size_t sz = sizeof(timer_task_t);
            if (nvs_get_blob(s_nvs, key_buf, &next, &sz) == ESP_OK) {
                timer_task_save(&next, i);
            }
        }
    }
    make_key(KEY_TASK_PREFIX, cnt - 1, key_buf, sizeof(key_buf));
    nvs_erase_key(s_nvs, key_buf);
    nvs_set_u8(s_nvs, KEY_TASK_COUNT, (uint8_t)(cnt - 1));
    return nvs_commit(s_nvs);
}

int timer_task_count(void)
{
    if (!s_inited) timer_storage_init();
    uint8_t cnt = 0;
    nvs_get_u8(s_nvs, KEY_TASK_COUNT, &cnt);
    return (int)cnt;
}

esp_err_t timer_history_save(const timer_history_t *record)
{
    if (!s_inited) timer_storage_init();

    int cnt = timer_history_count();
    int target_idx;

    if (cnt >= TIMER_MAX_HISTORY) {
        timer_history_t tmp;
        for (int i = 0; i < TIMER_MAX_HISTORY - 1; i++) {
            make_key(KEY_HISTORY_PREFIX, i + 1, key_buf, sizeof(key_buf));
            size_t sz = sizeof(timer_history_t);
            if (nvs_get_blob(s_nvs, key_buf, &tmp, &sz) == ESP_OK) {
                make_key(KEY_HISTORY_PREFIX, i, key_buf, sizeof(key_buf));
                nvs_set_blob(s_nvs, key_buf, &tmp, sizeof(timer_history_t));
            }
        }
        target_idx = TIMER_MAX_HISTORY - 1;
    } else {
        target_idx = cnt;
    }

    make_key(KEY_HISTORY_PREFIX, target_idx, key_buf, sizeof(key_buf));
    esp_err_t err = nvs_set_blob(s_nvs, key_buf, record, sizeof(timer_history_t));
    if (err != ESP_OK) return err;
    nvs_set_u8(s_nvs, KEY_HISTORY_COUNT, (uint8_t)(target_idx + 1));
    return nvs_commit(s_nvs);
}

esp_err_t timer_history_load(int idx, timer_history_t *record)
{
    if (!s_inited) timer_storage_init();
    if (idx < 0 || idx >= TIMER_MAX_HISTORY) return ESP_ERR_INVALID_ARG;

    make_key(KEY_HISTORY_PREFIX, idx, key_buf, sizeof(key_buf));
    size_t sz = sizeof(timer_history_t);
    return nvs_get_blob(s_nvs, key_buf, record, &sz);
}

int timer_history_count(void)
{
    if (!s_inited) timer_storage_init();
    uint8_t cnt = 0;
    nvs_get_u8(s_nvs, KEY_HISTORY_COUNT, &cnt);
    return (int)cnt;
}

esp_err_t timer_history_clear(void)
{
    if (!s_inited) timer_storage_init();
    for (int i = 0; i < TIMER_MAX_HISTORY; i++) {
        make_key(KEY_HISTORY_PREFIX, i, key_buf, sizeof(key_buf));
        nvs_erase_key(s_nvs, key_buf);
    }
    nvs_set_u8(s_nvs, KEY_HISTORY_COUNT, 0);
    return nvs_commit(s_nvs);
}

esp_err_t timer_runtime_save(const timer_runtime_t *rt)
{
    if (!s_inited) timer_storage_init();
    esp_err_t err = nvs_set_blob(s_nvs, KEY_RUNTIME, rt, sizeof(timer_runtime_t));
    if (err != ESP_OK) return err;
    return nvs_commit(s_nvs);
}

esp_err_t timer_runtime_load(timer_runtime_t *rt)
{
    if (!s_inited) timer_storage_init();
    size_t sz = sizeof(timer_runtime_t);
    esp_err_t err = nvs_get_blob(s_nvs, KEY_RUNTIME, rt, &sz);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(rt, 0, sizeof(*rt));
        rt->state = TIMER_STATE_IDLE;
        return ESP_OK;
    }
    return err;
}

uint32_t timer_get_time_sec(void)
{
    if (!s_inited) timer_storage_init();
    uint32_t base = 0;
    nvs_get_u32(s_nvs, KEY_TIME_BASE, &base);
    int64_t now_us = esp_timer_get_time();
    uint32_t now_sec = (uint32_t)(now_us / 1000000);
    if (base == 0) {
        base = now_sec;
        nvs_set_u32(s_nvs, KEY_TIME_BASE, base);
        nvs_commit(s_nvs);
        return 0;
    }
    return now_sec - base;
}
