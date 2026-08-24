#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define TIMER_MAX_NAME    32
#define TIMER_MAX_NODES   8
#define TIMER_MAX_HISTORY 20
#define TIMER_MAX_TASKS   8
#define TIMER_NODE_NAME   16

typedef enum {
    TIMER_STATE_IDLE = 0,
    TIMER_STATE_RUNNING,
    TIMER_STATE_PAUSED,
    TIMER_STATE_COMPLETED,
} timer_state_t;

typedef struct {
    char name[TIMER_NODE_NAME];
    uint32_t duration_sec;
} timer_node_t;

typedef struct {
    char name[TIMER_MAX_NAME];
    uint8_t node_count;
    timer_node_t nodes[TIMER_MAX_NODES];
} timer_task_t;

typedef struct {
    char task_name[TIMER_MAX_NAME];
    char node_name[TIMER_NODE_NAME];
    uint32_t total_sec;
    uint32_t actual_sec;
    uint32_t timestamp;
    bool completed_on_time;
} timer_history_t;

typedef struct {
    timer_state_t state;
    int current_task_idx;
    int current_node_idx;
    uint32_t node_elapsed;
    uint32_t task_elapsed;
    uint32_t start_timestamp;
    bool repeat_mode;
} timer_runtime_t;

esp_err_t timer_storage_init(void);

esp_err_t timer_task_save(const timer_task_t *task, int idx);
esp_err_t timer_task_load(int idx, timer_task_t *task);
esp_err_t timer_task_delete(int idx);
int       timer_task_count(void);

esp_err_t timer_history_save(const timer_history_t *record);
esp_err_t timer_history_load(int idx, timer_history_t *record);
int       timer_history_count(void);
esp_err_t timer_history_clear(void);

esp_err_t timer_runtime_save(const timer_runtime_t *rt);
esp_err_t timer_runtime_load(timer_runtime_t *rt);

uint32_t timer_get_time_sec(void);
