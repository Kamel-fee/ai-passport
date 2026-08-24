// main/demo_punchcard.c —— 离线打卡计时应用。
// 三键语义(页面内,OK 长按由 main 拦截为返回菜单):
//   OK CLICK   : 开始 / 停止 当前会话
//   UP CLICK   : 切换到上一个视图(当前计时 / 累计统计 / 历史记录)
//   DOWN CLICK : 切换到下一个视图;历史视图内为切换记录
//
// 掉电不丢失保证:
//   - 开始时 NVS 写入 running=1 与 start_ts
//   - 结束时写入一条历史记录、累加 total_sec、清 running
//   - 进入本页时若发现 running=1,按当前时间差恢复显示继续计时
//     (相当于"断电再上电不会丢正在进行的会话时长")
#include "demo.h"
#include "bsp_storage.h"
#include "ui_pixel.h"
#include "lvgl.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "demo_punchcard";

// --- UI 状态 ---------------------------------------------------------------
static lv_obj_t   *s_scr;
static lv_obj_t   *s_panel;           // 中间主面板(颜色会根据状态变化)
static lv_obj_t   *s_big_time;        // 大号 HH:MM:SS
static lv_obj_t   *s_status;          // 状态文字 RUNNING/STOPPED
static lv_obj_t   *s_sub1;            // 辅助信息行 1
static lv_obj_t   *s_sub2;            // 辅助信息行 2
static lv_obj_t   *s_hint;            // 底部按键提示
static lv_timer_t *s_timer;           // 1 Hz 刷新

typedef enum {
    VIEW_NOW = 0,   // 当前会话计时
    VIEW_TOTAL,     // 累计统计
    VIEW_HISTORY,   // 历史记录浏览
    VIEW_COUNT,
} view_t;

static view_t   s_view;
static int      s_hist_idx;           // VIEW_HISTORY 下的相对索引 0=最新

// 运行时(不从 NVS 每帧读,保持本地缓存一致)
static bool     s_running;
static uint32_t s_start_ts;           // 开始时间戳
static uint32_t s_total_sec;          // 已完成会话累计秒
static uint32_t s_rec_count;          // 已写入历史条数(用于环形判定)

// --- 辅助:秒数 → "HH:MM:SS" 字符串 ----------------------------------------
static void fmt_hms(char *buf, size_t n, uint32_t sec) {
    uint32_t h = sec / 3600;
    uint32_t m = (sec % 3600) / 60;
    uint32_t s = sec % 60;
    snprintf(buf, n, "%02lu:%02lu:%02lu",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
}

// 当前会话已过秒数(运行中或停止中都能算出)
static uint32_t current_session_sec(void) {
    if (s_start_ts == 0) return 0;
    time_t now = time(NULL);
    if ((uint32_t)now < s_start_ts) return 0;   // 时间戳倒拨(断电/RTC 重置)时兜底
    if (s_running) return (uint32_t)now - s_start_ts;
    return 0;   // 非运行中显示 0;完成会话已经累计入 total_sec
}

// 已完成会话总条数(上限)
static uint32_t total_sessions(void) {
    return s_rec_count < BSP_PUNCH_MAX_RECORDS ? s_rec_count : BSP_PUNCH_MAX_RECORDS;
}

// 把历史"相对索引 0=最新"转换为 NVS 环形下标
static uint32_t hist_rel_to_abs(int rel) {
    if (s_rec_count == 0) return 0;
    // ring_head: 下一条要写入的位置 = rec_count % MAX
    uint32_t head = s_rec_count % BSP_PUNCH_MAX_RECORDS;
    // 最新一条在 head - 1 (mod MAX)
    int total = (int)total_sessions();
    if (rel < 0) rel = 0;
    if (rel >= total) rel = total - 1;
    int abs_idx = (int)head - 1 - rel;
    while (abs_idx < 0) abs_idx += BSP_PUNCH_MAX_RECORDS;
    return (uint32_t)abs_idx;
}

// --- 业务:开始 / 停止 ------------------------------------------------------
static void punch_start(void) {
    if (s_running) return;
    s_running  = true;
    s_start_ts = (uint32_t)time(NULL);
    if (s_start_ts < 100000) s_start_ts = 100000;   // 时间戳未同步时给个下限,避免看起来是 0
    bsp_storage_set_running(true);
    bsp_storage_set_start_ts(s_start_ts);
    ESP_LOGI(TAG, "打卡开始 ts=%lu", (unsigned long)s_start_ts);
}

static void punch_stop(void) {
    if (!s_running) return;
    uint32_t dur = current_session_sec();

    // 写入历史(环形)
    uint32_t abs_idx = s_rec_count % BSP_PUNCH_MAX_RECORDS;
    bsp_punch_record_t rec = {
        .start_ts     = s_start_ts,
        .duration_sec = dur,
    };
    bsp_storage_set_record(abs_idx, &rec);
    s_rec_count += 1;
    bsp_storage_set_record_count(s_rec_count);

    // 累计
    s_total_sec += dur;
    bsp_storage_set_total_sec(s_total_sec);

    // 清运行态
    s_running  = false;
    s_start_ts = 0;
    bsp_storage_set_running(false);
    bsp_storage_set_start_ts(0);

    ESP_LOGI(TAG, "打卡结束 dur=%lus 累计=%lus 总条数=%lu",
             (unsigned long)dur, (unsigned long)s_total_sec, (unsigned long)s_rec_count);
}

// --- 视图刷新 --------------------------------------------------------------
static void refresh(void) {
    char buf[64];

    // 主面板颜色:计时中=红,停止=绿
    uint32_t panel_color = s_running ? UI_RED : UI_GRASS;
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(panel_color), 0);

    switch (s_view) {
    case VIEW_NOW: {
        uint32_t sec = current_session_sec();
        fmt_hms(buf, sizeof(buf), sec);
        lv_label_set_text(s_big_time, buf);
        lv_label_set_text(s_status, s_running ? "● RUNNING" : "■ STOPPED");
        lv_obj_set_style_text_color(s_status,
            s_running ? lv_color_hex(UI_YELLOW) : lv_color_white(), 0);

        snprintf(buf, sizeof(buf), "Start: %s", s_start_ts ? ctime((time_t*)&(time_t){s_start_ts}) : "--");
        // ctime 返回的字符串带 \n,去掉
        buf[strcspn(buf, "\n")] = '\0';
        lv_label_set_text(s_sub1, buf);

        lv_label_set_text_fmt(s_sub2, "Done sessions: %lu   Total saved: %lus",
                              (unsigned long)s_rec_count, (unsigned long)s_total_sec);
        break;
    }
    case VIEW_TOTAL: {
        fmt_hms(buf, sizeof(buf), s_total_sec);
        lv_label_set_text(s_big_time, buf);
        lv_label_set_text(s_status, "★ TOTAL ACCUMULATED");
        lv_obj_set_style_text_color(s_status, lv_color_white(), 0);
        lv_label_set_text_fmt(s_sub1, "Completed sessions : %lu", (unsigned long)s_rec_count);

        // 估算平均时长(若有)
        if (s_rec_count > 0) {
            uint32_t avg = s_total_sec / s_rec_count;
            char avgbuf[16];
            fmt_hms(avgbuf, sizeof(avgbuf), avg);
            lv_label_set_text_fmt(s_sub2, "Avg per session   : %s", avgbuf);
        } else {
            lv_label_set_text(s_sub2, "Avg per session   : --:--:--");
        }
        break;
    }
    case VIEW_HISTORY: {
        uint32_t total = total_sessions();
        if (total == 0) {
            lv_label_set_text(s_big_time, "--:--:--");
            lv_label_set_text(s_status, "☆ NO RECORDS YET");
            lv_obj_set_style_text_color(s_status, lv_color_white(), 0);
            lv_label_set_text(s_sub1, "Complete a session first.");
            lv_label_set_text(s_sub2, "Press OK to start / stop.");
        } else {
            if (s_hist_idx >= (int)total) s_hist_idx = 0;
            if (s_hist_idx < 0) s_hist_idx = (int)total - 1;
            uint32_t abs_idx = hist_rel_to_abs(s_hist_idx);
            bsp_punch_record_t rec;
            bsp_storage_get_record(abs_idx, &rec);
            fmt_hms(buf, sizeof(buf), rec.duration_sec);
            lv_label_set_text(s_big_time, buf);
            lv_label_set_text_fmt(s_status, "◇ HISTORY #%d / %lu",
                                  s_hist_idx + 1, (unsigned long)total);
            lv_obj_set_style_text_color(s_status, lv_color_white(), 0);

            char when[64];
            if (rec.start_ts) {
                snprintf(when, sizeof(when), "%s", ctime((time_t*)&(time_t){rec.start_ts}));
                when[strcspn(when, "\n")] = '\0';
            } else {
                snprintf(when, sizeof(when), "--");
            }
            lv_label_set_text(s_sub1, when);
            lv_label_set_text_fmt(s_sub2, "Index in ring : %lu  (newest=0)", (unsigned long)abs_idx);
        }
        break;
    }
    default: break;
    }

    // 底部按键提示(随视图微调)
    switch (s_view) {
    case VIEW_NOW:
        lv_label_set_text(s_hint, "OK: Start / Stop   UP/DOWN: Views");
        break;
    case VIEW_TOTAL:
        lv_label_set_text(s_hint, "OK: Start / Stop   UP/DOWN: Views");
        break;
    case VIEW_HISTORY:
        lv_label_set_text(s_hint, "OK: Start / Stop   UP/DOWN: Records");
        break;
    default:
        lv_label_set_text(s_hint, "OK: Start / Stop");
        break;
    }
}

static void tick(lv_timer_t *t) {
    (void)t;
    refresh();
}

// --- 入口 / 出口 / 按键 ----------------------------------------------------
void demo_punchcard_enter(void) {
    // 初始化存储(幂等),并从 NVS 恢复上次状态
    esp_err_t err = bsp_storage_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "bsp_storage_init err %s", esp_err_to_name(err));

    s_running   = bsp_storage_get_running();
    s_start_ts  = bsp_storage_get_start_ts();
    s_total_sec = bsp_storage_get_total_sec();
    s_rec_count = bsp_storage_get_record_count();

    // 若断电前正在打卡,恢复后 running=1,start_ts 存在;直接续算即可
    // 若此时 RTC 已重置(start_ts 比当前时间大或极接近 0),兜底清掉避免显示超大值
    time_t now = time(NULL);
    if (s_running && s_start_ts > 0 && (uint32_t)now - s_start_ts > 24UL * 3600 * 365 * 10) {
        ESP_LOGW(TAG, "RTC 看起来已重置,丢弃过期 running 会话 ts=%lu now=%lu",
                 (unsigned long)s_start_ts, (unsigned long)now);
        s_running  = false;
        s_start_ts = 0;
        bsp_storage_set_running(false);
        bsp_storage_set_start_ts(0);
    }

    ESP_LOGI(TAG, "恢复: running=%d start_ts=%lu total_sec=%lu rec_count=%lu",
             s_running, (unsigned long)s_start_ts,
             (unsigned long)s_total_sec, (unsigned long)s_rec_count);

    s_view     = VIEW_NOW;
    s_hist_idx = 0;

    s_scr = ui_pixel_screen_create("PUNCHCARD");

    // 主面板 24x58 → 192x170, 内部放时间 + 状态 + 两行辅助
    s_panel = ui_pixel_panel_create(s_scr, 24, 58, 192, 170, s_running ? UI_RED : UI_GRASS);

    s_big_time = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_big_time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_big_time, lv_color_white(), 0);
    lv_obj_align(s_big_time, LV_ALIGN_TOP_MID, 0, 14);

    s_status = lv_label_create(s_panel);
    lv_obj_set_style_text_color(s_status, lv_color_white(), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 46);

    s_sub1 = lv_label_create(s_panel);
    lv_obj_set_style_text_color(s_sub1, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_font(s_sub1, &lv_font_montserrat_14, 0);
    lv_obj_align(s_sub1, LV_ALIGN_TOP_LEFT, 8, 78);

    s_sub2 = lv_label_create(s_panel);
    lv_obj_set_style_text_color(s_sub2, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_font(s_sub2, &lv_font_montserrat_14, 0);
    lv_obj_align(s_sub2, LV_ALIGN_TOP_LEFT, 8, 104);

    // 底部按键提示:放在 panel 内部最后一行
    s_hint = lv_label_create(s_panel);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    ui_pixel_mascot_create(s_scr, 101, 238);

    refresh();
    s_timer = lv_timer_create(tick, 1000, NULL);
    lv_screen_load(s_scr);
}

void demo_punchcard_exit(void) {
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_scr)   { lv_obj_delete(s_scr); s_scr = NULL;
                   s_panel = s_big_time = s_status = s_sub1 = s_sub2 = s_hint = NULL; }
}

void demo_punchcard_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;

    if (btn == BSP_BTN_OK) {
        if (s_running) punch_stop();
        else           punch_start();
    } else if (btn == BSP_BTN_UP) {
        if (s_view == VIEW_HISTORY && total_sessions() > 0) {
            // 历史视图内 UP 翻到上一条(更新)
            uint32_t total = total_sessions();
            s_hist_idx = (s_hist_idx + (int)total - 1) % (int)total;
        } else {
            s_view = (view_t)((s_view + VIEW_COUNT - 1) % VIEW_COUNT);
        }
    } else if (btn == BSP_BTN_DOWN) {
        if (s_view == VIEW_HISTORY && total_sessions() > 0) {
            uint32_t total = total_sessions();
            s_hist_idx = (s_hist_idx + 1) % (int)total;
        } else {
            s_view = (view_t)((s_view + 1) % VIEW_COUNT);
        }
    }
    refresh();
}
