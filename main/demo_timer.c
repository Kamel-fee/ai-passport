#include "demo_timer.h"
#include "task_timer.h"
#include "ble_comm.h"
#include "bsp_display.h"
#include "bsp_audio.h"
#include "bsp_button.h"
#include "ui_pixel.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "demo_timer";
#define TIMER_TASK_STACK  4096
#define TIMER_TASK_PRIO   5
#define TIMER_SLEEP_SEC   30

typedef enum {
    VIEW_HOME = 0,
    VIEW_RUNNING,
    VIEW_DONE,
} view_t;

static view_t s_view;
static lv_obj_t *s_scr;

static timer_task_t s_tasks[TIMER_MAX_TASKS];
static int s_task_count;
static int s_selected;

static timer_runtime_t s_runtime;
static timer_task_t s_current_task;
static timer_history_t s_result;

static lv_obj_t *s_home_list;
static lv_obj_t *s_home_info;
static lv_obj_t *s_home_mascot;

static lv_obj_t *s_run_task_name;
static lv_obj_t *s_run_node_name;
static lv_obj_t *s_run_progress;
static lv_obj_t *s_run_remaining;
static lv_obj_t *s_run_node_bar;
static lv_obj_t *s_run_time_label;

static lv_obj_t *s_done_task_name;
static lv_obj_t *s_done_node_name;
static lv_obj_t *s_done_result;
static lv_obj_t *s_done_time;

static TimerHandle_t s_tick_timer;
static TaskHandle_t s_sound_task;
static bool s_sound_queue[4];
static int s_sound_head;
static int s_sound_tail;

static uint32_t s_last_node_notify;
static uint32_t s_last_5min_notify;
static uint32_t s_last_10min_notify;
static uint32_t s_last_min_notify;
static bool s_screen_asleep;
static uint32_t s_last_activity;

static void home_build(void);
static void home_refresh(void);
static void running_build(void);
static void running_refresh(void);
static void done_build(void);
static void tick_callback(TimerHandle_t xTimer);
static void timer_worker(void *arg);
static void sound_play_notification(int type);
static void enter_view(view_t v);
static void wake_screen(void);
static void check_sleep(void);

static void load_tasks(void)
{
    s_task_count = timer_task_count();
    if (s_task_count > TIMER_MAX_TASKS) s_task_count = TIMER_MAX_TASKS;
    for (int i = 0; i < s_task_count; i++) {
        timer_task_load(i, &s_tasks[i]);
    }
    s_selected = 0;
}

static uint32_t total_duration(const timer_task_t *t)
{
    uint32_t total = 0;
    for (int i = 0; i < t->node_count; i++) {
        total += t->nodes[i].duration_sec;
    }
    return total;
}

static const timer_node_t *current_node(void)
{
    if (s_runtime.current_node_idx < 0 ||
        s_runtime.current_node_idx >= s_current_task.node_count)
        return NULL;
    return &s_current_task.nodes[s_runtime.current_node_idx];
}

static uint32_t elapsed_in_node(void)
{
    return s_runtime.node_elapsed;
}

static uint32_t remaining_in_node(void)
{
    const timer_node_t *n = current_node();
    if (!n) return 0;
    uint32_t dur = n->duration_sec;
    uint32_t el = elapsed_in_node();
    return (el >= dur) ? 0 : (dur - el);
}

static uint32_t elapsed_in_task(void)
{
    return s_runtime.task_elapsed;
}

static uint32_t remaining_in_task(void)
{
    uint32_t total = total_duration(&s_current_task);
    uint32_t el = elapsed_in_task();
    return (el >= total) ? 0 : (total - el);
}

static void format_time(uint32_t sec, char *buf, size_t sz)
{
    uint32_t h = sec / 3600;
    uint32_t m = (sec % 3600) / 60;
    uint32_t s = sec % 60;
    if (h > 0) snprintf(buf, sz, "%lu:%02lu:%02lu", h, m, s);
    else snprintf(buf, sz, "%lu:%02lu", m, s);
}

static void sound_queue_push(int type)
{
    int next = (s_sound_tail + 1) % 4;
    if (next == s_sound_head) return;
    s_sound_queue[s_sound_tail] = (type == 1);
    s_sound_tail = next;
}

static void sound_queue_process(void)
{
    if (s_sound_head == s_sound_tail) return;
    bool is_double = s_sound_queue[s_sound_head];
    s_sound_head = (s_sound_head + 1) % 4;

    uint8_t volume = 60;
    uint32_t freq = 880;
    uint32_t dur_ms = 150;

    if (is_double) {
        bsp_audio_set_format(8000, 16, 1);
        int16_t tone[800];
        for (int i = 0; i < 800; i++) {
            tone[i] = (int16_t)(6000 * (i % 266 < 133 ? 1 : -1));
        }
        bsp_audio_write(tone, 1600);
        vTaskDelay(pdMS_TO_TICKS(120));
        for (int i = 0; i < 800; i++) {
            tone[i] = (int16_t)(6000 * (i % 266 < 133 ? 1 : -1));
        }
        bsp_audio_write(tone, 1600);
    } else {
        bsp_audio_set_format(8000, 16, 1);
        int16_t tone[800];
        for (int i = 0; i < 800; i++) {
            tone[i] = (int16_t)(5000 * (i % 200 < 100 ? 1 : -1));
        }
        bsp_audio_write(tone, 1600);
    }
}

static void sound_play_node_arrival(void)
{
    sound_queue_push(1);
}

static void sound_play_periodic(int minutes)
{
    sound_queue_push(0);
    sound_queue_push(0);
}

static void sound_play_complete(void)
{
    sound_queue_push(1);
    vTaskDelay(pdMS_TO_TICKS(100));
    sound_queue_push(1);
}

static void on_tick(void)
{
    if (s_view != VIEW_RUNNING) return;
    if (s_runtime.state != TIMER_STATE_RUNNING) return;

    s_runtime.node_elapsed++;
    s_runtime.task_elapsed++;

    const timer_node_t *node = current_node();
    uint32_t node_dur = node ? node->duration_sec : 0;

    if (node && s_runtime.node_elapsed >= node_dur) {
        s_runtime.node_elapsed = node_dur;

        if (s_runtime.current_node_idx < s_current_task.node_count - 1) {
            s_runtime.current_node_idx++;
            s_runtime.node_elapsed = 0;
            sound_play_node_arrival();
            s_last_node_notify = s_runtime.task_elapsed;
        } else {
            s_runtime.state = TIMER_STATE_COMPLETED;
            sound_play_complete();

            timer_history_t hist;
            strncpy(hist.task_name, s_current_task.name, TIMER_MAX_NAME - 1);
            hist.task_name[TIMER_MAX_NAME - 1] = '\0';
            if (node) {
                strncpy(hist.node_name, node->name, TIMER_NODE_NAME - 1);
                hist.node_name[TIMER_NODE_NAME - 1] = '\0';
            }
            hist.total_sec = total_duration(&s_current_task);
            hist.actual_sec = s_runtime.task_elapsed;
            hist.completed_on_time = true;
            hist.timestamp = timer_get_time_sec();
            timer_history_save(&hist);
            s_result = hist;

            timer_runtime_save(&s_runtime);
            enter_view(VIEW_DONE);
            return;
        }
    }

    uint32_t task_el = s_runtime.task_elapsed;
    uint32_t remaining_task = remaining_in_task();

    if (task_el > 0 && task_el % 60 == 0) {
        sound_play_periodic(1);
    }
    if (task_el > 0 && task_el % 300 == 0) {
        sound_play_periodic(5);
    }
    if (task_el > 0 && task_el % 600 == 0) {
        sound_play_periodic(10);
    }

    if (remaining_task == 0 && s_runtime.state == TIMER_STATE_RUNNING) {
        s_runtime.state = TIMER_STATE_COMPLETED;
        sound_play_complete();
        timer_runtime_save(&s_runtime);
        enter_view(VIEW_DONE);
    }

    if (bsp_lvgl_lock(200)) {
        running_refresh();
        bsp_lvgl_unlock();
    }
}

static void tick_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    on_tick();
}

static void timer_worker(void *arg)
{
    (void)arg;
    while (1) {
        sound_queue_process();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void home_build(void)
{
    char title[40];
    snprintf(title, sizeof(title), "TIMER");
    s_scr = ui_pixel_screen_create(title);

    s_home_list = lv_obj_create(s_scr);
    lv_obj_set_pos(s_home_list, 10, 55);
    lv_obj_set_size(s_home_list, 220, 170);
    lv_obj_set_style_bg_color(s_home_list, lv_color_hex(UI_PAPER), 0);
    lv_obj_set_style_border_color(s_home_list, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(s_home_list, 3, 0);
    lv_obj_set_style_pad_all(s_home_list, 6, 0);
    lv_obj_set_scrollbar_mode(s_home_list, LV_SCROLLBAR_MODE_OFF);

    s_home_info = ui_pixel_label(s_scr, "", &lv_font_montserrat_12, UI_INK);
    lv_obj_set_pos(s_home_info, 10, 230);
    lv_obj_set_width(s_home_info, 220);
    lv_obj_set_style_text_align(s_home_info, LV_TEXT_ALIGN_CENTER, 0);

    s_home_mascot = ui_pixel_mascot_create(s_scr, 101, 250);

    home_refresh();
    lv_screen_load(s_scr);
}

static void home_refresh(void)
{
    if (!s_home_list) return;

    lv_obj_clean(s_home_list);

    if (s_task_count == 0) {
        lv_obj_t *empty = ui_pixel_label(s_home_list, "NO TASKS\nSync via BLE",
                                         &lv_font_montserrat_14, UI_INK);
        lv_obj_center(empty);
    } else {
        for (int i = 0; i < s_task_count; i++) {
            int y = 4 + i * 40;
            uint32_t total = total_duration(&s_tasks[i]);
            char line[64];
            char time_str[16];
            format_time(total, time_str, sizeof(time_str));
            snprintf(line, sizeof(line), "%s  %d NODES  %s",
                     s_tasks[i].name, s_tasks[i].node_count, time_str);

            lv_obj_t *row = lv_obj_create(s_home_list);
            lv_obj_set_pos(row, 4, y);
            lv_obj_set_size(row, 212, 36);
            lv_color_t bg = (i == s_selected) ? lv_color_hex(UI_YELLOW) : lv_color_hex(0xFFFFFF);
            lv_obj_set_style_bg_color(row, bg, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 4, 0);

            lv_obj_t *lbl = ui_pixel_label(row, line, &lv_font_montserrat_12, UI_INK);
            lv_obj_center(lbl);
        }
    }

    char info[128];
    snprintf(info, sizeof(info),
             "OK: START  UP/DOWN: SELECT\n"
             "HISTORY: %d records  BLE: %s",
             timer_history_count(),
             ble_comm_is_connected() ? "CONNECTED" : "STANDBY");
    lv_label_set_text(s_home_info, info);
}

static void running_build(void)
{
    s_runtime.state = TIMER_STATE_RUNNING;
    s_runtime.current_node_idx = 0;
    s_runtime.node_elapsed = 0;
    s_runtime.task_elapsed = 0;
    s_runtime.start_timestamp = timer_get_time_sec();

    timer_runtime_save(&s_runtime);
    s_last_node_notify = 0;
    s_last_5min_notify = 0;
    s_last_10min_notify = 0;
    s_last_min_notify = 0;

    s_scr = ui_pixel_screen_create("TASK");

    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 10, 55, 220, 200, UI_PAPER);

    s_run_task_name = ui_pixel_label(panel, s_current_task.name,
                                     &lv_font_montserrat_16, UI_INK);
    lv_obj_set_pos(s_run_task_name, 8, 8);

    const timer_node_t *node = current_node();
    s_run_node_name = ui_pixel_label(panel,
                                     node ? node->name : "---",
                                     &lv_font_montserrat_14, UI_SKY_DARK);
    lv_obj_set_pos(s_run_node_name, 8, 32);

    lv_obj_t *progress_label = ui_pixel_label(panel, "PROGRESS",
                                              &lv_font_montserrat_12, UI_INK);
    lv_obj_set_pos(progress_label, 8, 58);

    s_run_node_bar = lv_bar_create(panel);
    lv_obj_set_pos(s_run_node_bar, 8, 76);
    lv_obj_set_size(s_run_node_bar, 204, 20);
    lv_bar_set_range(s_run_node_bar, 0, 100);
    lv_bar_set_value(s_run_node_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_run_node_bar, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_run_node_bar, lv_color_hex(UI_GRASS), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_run_node_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_run_node_bar, 0, LV_PART_INDICATOR);

    s_run_progress = lv_label_create(panel);
    lv_obj_set_style_text_font(s_run_progress, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_run_progress, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(s_run_progress, 8, 100);

    lv_obj_t *remain_label = ui_pixel_label(panel, "REMAIN",
                                            &lv_font_montserrat_12, UI_INK);
    lv_obj_set_pos(remain_label, 8, 130);

    s_run_remaining = lv_label_create(panel);
    lv_obj_set_style_text_font(s_run_remaining, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_run_remaining, lv_color_hex(UI_RED), 0);
    lv_obj_set_pos(s_run_remaining, 8, 148);

    s_run_time_label = ui_pixel_label(panel, "", &lv_font_montserrat_10, 0x78909C);
    lv_obj_set_pos(s_run_time_label, 8, 186);

    lv_screen_load(s_scr);

    if (s_tick_timer) xTimerStart(s_tick_timer, 0);
}

static void running_refresh(void)
{
    if (!s_scr) return;

    const timer_node_t *node = current_node();
    if (!node) return;

    uint32_t node_dur = node->duration_sec;
    uint32_t node_el = elapsed_in_node();
    int pct = (int)((node_el * 100) / (node_dur > 0 ? node_dur : 1));
    if (pct > 100) pct = 100;

    lv_bar_set_value(s_run_node_bar, pct, LV_ANIM_ON);

    char prog[64];
    snprintf(prog, sizeof(prog), "NODE %d/%d  %s",
             s_runtime.current_node_idx + 1,
             s_current_task.node_count,
             node->name);
    lv_label_set_text(s_run_progress, prog);

    char rem_buf[16];
    format_time(remaining_in_node(), rem_buf, sizeof(rem_buf));
    lv_label_set_text(s_run_remaining, rem_buf);

    char time_buf[64];
    snprintf(time_buf, sizeof(time_buf), "TOTAL ELAPSED: ");
    char tbuf[16];
    format_time(elapsed_in_task(), tbuf, sizeof(tbuf));
    strncat(time_buf, tbuf, sizeof(time_buf) - strlen(time_buf) - 1);
    lv_label_set_text(s_run_time_label, time_buf);
}

static void done_build(void)
{
    xTimerStop(s_tick_timer, 0);

    s_scr = ui_pixel_screen_create("DONE");

    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 10, 55, 220, 200, UI_PAPER);

    s_done_task_name = ui_pixel_label(panel, s_result.task_name,
                                      &lv_font_montserrat_16, UI_INK);
    lv_obj_set_pos(s_done_task_name, 8, 8);

    s_done_node_name = ui_pixel_label(panel, s_result.node_name,
                                      &lv_font_montserrat_14, UI_SKY_DARK);
    lv_obj_set_pos(s_done_node_name, 8, 32);

    bool on_time = s_result.completed_on_time;
    uint32_t diff;
    if (on_time) {
        diff = s_result.total_sec - s_result.actual_sec;
    } else {
        diff = s_result.actual_sec - s_result.total_sec;
    }

    s_done_result = ui_pixel_label(panel,
                                   on_time ? "COMPLETED ON TIME!" : "OVERDUE",
                                   &lv_font_montserrat_18,
                                   on_time ? UI_GRASS_DARK : UI_RED);
    lv_obj_set_pos(s_done_result, 8, 64);

    char time_info[128];
    char buf1[16], buf2[16], buf3[16];
    format_time(s_result.total_sec, buf1, sizeof(buf1));
    format_time(s_result.actual_sec, buf2, sizeof(buf2));
    format_time(diff, buf3, sizeof(buf3));
    snprintf(time_info, sizeof(time_info),
             "PLANNED: %s\nACTUAL: %s\n%s BY: %s",
             buf1, buf2, on_time ? "EARLY" : "LATE", buf3);

    s_done_time = ui_pixel_label(panel, time_info,
                                 &lv_font_montserrat_12, UI_INK);
    lv_obj_set_pos(s_done_time, 8, 100);

    lv_obj_t *hint = ui_pixel_label(panel, "OK: HOME  LONG OK: EXIT",
                                    &lv_font_montserrat_10, 0x78909C);
    lv_obj_set_pos(hint, 8, 170);

    lv_screen_load(s_scr);
}

static void enter_view(view_t v)
{
    s_view = v;
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    s_home_list = NULL;
    s_home_info = NULL;
    s_home_mascot = NULL;
    s_run_task_name = s_run_node_name = s_run_progress = NULL;
    s_run_remaining = s_run_node_bar = s_run_time_label = NULL;
    s_done_task_name = s_done_node_name = s_done_result = s_done_time = NULL;

    switch (v) {
    case VIEW_HOME:     home_build(); break;
    case VIEW_RUNNING:  running_build(); break;
    case VIEW_DONE:     done_build(); break;
    }
}

static void wake_screen(void)
{
    if (s_screen_asleep) {
        bsp_display_backlight(100);
        s_screen_asleep = false;
    }
    s_last_activity = timer_get_time_sec();
}

static void check_sleep(void)
{
    uint32_t now = timer_get_time_sec();
    if (s_runtime.state == TIMER_STATE_RUNNING &&
        (now - s_last_activity) >= TIMER_SLEEP_SEC) {
        bsp_display_backlight(0);
        s_screen_asleep = true;
    }
}

static void on_home_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;

    wake_screen();

    if (btn == BSP_BTN_UP) {
        if (s_task_count > 0)
            s_selected = (s_selected + s_task_count - 1) % s_task_count;
        ui_pixel_mascot_jump(s_home_mascot);
        home_refresh();
    } else if (btn == BSP_BTN_DOWN) {
        if (s_task_count > 0)
            s_selected = (s_selected + 1) % s_task_count;
        ui_pixel_mascot_jump(s_home_mascot);
        home_refresh();
    } else if (btn == BSP_BTN_OK) {
        if (s_task_count > 0 && s_selected < s_task_count) {
            s_current_task = s_tasks[s_selected];
            s_runtime.state = TIMER_STATE_IDLE;
            s_runtime.current_task_idx = s_selected;
            enter_view(VIEW_RUNNING);
        }
    }
}

static void on_running_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev == BSP_BTN_CLICK) {
        wake_screen();

        if (btn == BSP_BTN_OK) {
            if (s_runtime.state == TIMER_STATE_RUNNING) {
                s_runtime.state = TIMER_STATE_PAUSED;
                xTimerStop(s_tick_timer, 0);
                bsp_display_backlight(30);
                char paused[64];
                snprintf(paused, sizeof(paused), "PAUSED  OK: RESUME");
                lv_label_set_text(s_run_time_label, paused);
            } else if (s_runtime.state == TIMER_STATE_PAUSED) {
                s_runtime.state = TIMER_STATE_RUNNING;
                xTimerStart(s_tick_timer, 0);
                bsp_display_backlight(100);
                running_refresh();
            }
        } else if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            if (s_runtime.state == TIMER_STATE_RUNNING) {
                sound_queue_push(1);

                if (s_runtime.current_node_idx < s_current_task.node_count - 1) {
                    s_runtime.current_node_idx++;
                    s_runtime.node_elapsed = 0;
                } else {
                    s_runtime.state = TIMER_STATE_COMPLETED;
                    enter_view(VIEW_DONE);
                    return;
                }
                running_refresh();
            }
        }
    } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        if (s_runtime.state == TIMER_STATE_RUNNING) {
            s_runtime.state = TIMER_STATE_PAUSED;
        }
        xTimerStop(s_tick_timer, 0);
        enter_view(VIEW_HOME);
    }
}

static void on_done_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;

    if (btn == BSP_BTN_OK) {
        enter_view(VIEW_HOME);
    }
}

void demo_timer_enter(void)
{
    timer_storage_init();
    load_tasks();

    if (s_tick_timer == NULL) {
        s_tick_timer = xTimerCreate("timer_tick", pdMS_TO_TICKS(1000),
                                    pdTRUE, NULL, tick_callback);
    }
    if (s_sound_task == NULL) {
        xTaskCreate(timer_worker, "timer_sound", TIMER_TASK_STACK,
                    NULL, TIMER_TASK_PRIO, &s_sound_task);
    }

    esp_err_t ble_err = ble_comm_init();
    if (ble_err != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(ble_err));
    }

    timer_runtime_load(&s_runtime);
    s_sound_head = s_sound_tail = 0;
    s_screen_asleep = false;
    s_last_activity = timer_get_time_sec();

    enter_view(VIEW_HOME);
    ESP_LOGI(TAG, "timer app entered, %d tasks loaded", s_task_count);
}

void demo_timer_exit(void)
{
    if (s_tick_timer) { xTimerStop(s_tick_timer, 0); }
    timer_runtime_save(&s_runtime);

    ble_comm_deinit();

    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    s_home_list = s_home_info = s_home_mascot = NULL;
    s_run_task_name = s_run_node_name = s_run_progress = NULL;
    s_run_remaining = s_run_node_bar = s_run_time_label = NULL;
    s_done_task_name = s_done_node_name = s_done_result = s_done_time = NULL;

    ESP_LOGI(TAG, "timer app exited");
}

void demo_timer_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    switch (s_view) {
    case VIEW_HOME:    on_home_key(btn, ev); break;
    case VIEW_RUNNING: on_running_key(btn, ev); break;
    case VIEW_DONE:    on_done_key(btn, ev); break;
    }
}
