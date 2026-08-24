// main/demo_simple_menu.c —— 独立子菜单示例:UP/DOWN 切换、OK 确认进入、再按 OK 返回。
// 展示如何在单个 demo 页内部维护“菜单态 / 信息态”两个子状态,并显示简单状态信息。
#include "demo.h"
#include "bsp_button.h"
#include "ui_pixel.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdint.h>

typedef enum {
    PAGE_MENU = 0,       // 子菜单页
    PAGE_INFO,           // 信息展示页
} page_t;

typedef enum {
    ITEM_WIFI = 0,
    ITEM_BT,
    ITEM_DISPLAY,
    ITEM_ABOUT,
} item_idx_t;

#define ITEM_COUNT 4

static const char *ITEM_NAME[ITEM_COUNT] = {
    "WiFi Status",
    "Bluetooth",
    "Display",
    "About",
};

static const uint32_t ITEM_COLOR[ITEM_COUNT] = {
    UI_SKY_DARK,
    UI_GRASS_DARK,
    UI_ORANGE,
    0x7557D9,
};

static lv_obj_t   *s_scr;
static lv_obj_t   *s_panel;
static lv_obj_t   *s_title_label;
static lv_obj_t   *s_items[ITEM_COUNT];
static lv_obj_t   *s_info_label;
static lv_obj_t   *s_status_label;
static lv_obj_t   *s_mascot;
static lv_timer_t *s_timer;

static page_t s_page;
static int    s_sel;               // 子菜单选中索引
static int    s_info_item;         // 当前信息页对应的菜单项
static uint32_t s_tick_seconds;    // 模拟运行时长

// 每个 item 显示行的高度、面板布局参数
#define ITEM_H       38
#define ITEM_PANEL_Y 56
#define ITEM_PANEL_H (ITEM_COUNT * ITEM_H + 14)

// ===== 信息页内容(按需生成字符串) =====
static void info_build_text(int item, char *buf, size_t buf_size)
{
    switch (item) {
    case ITEM_WIFI:
        snprintf(buf, buf_size,
                 "WiFi:   ON\n"
                 "SSID:   FoloToy-AP\n"
                 "Signal: -58 dBm\n"
                 "IP:     192.168.4.1\n"
                 "Uptime: %lus",
                 (unsigned long)s_tick_seconds);
        break;
    case ITEM_BT:
        snprintf(buf, buf_size,
                 "BLE:      ON\n"
                 "Name:     FoloToy-%02X%02X\n"
                 "Paired:   2 devices\n"
                 "Battery:  ---\n"
                 "Uptime:   %lus",
                 (unsigned)(0xA5), (unsigned)(0xF1),
                 (unsigned long)s_tick_seconds);
        break;
    case ITEM_DISPLAY: {
        int mv = bsp_button_read_mv();
        snprintf(buf, buf_size,
                 "Panel:    ST7789P3\n"
                 "Res:      240 x 320\n"
                 "SPI:      40 MHz\n"
                 "Format:   RGB565\n"
                 "ADC(mV):  %d",
                 mv);
        break;
    }
    case ITEM_ABOUT:
    default:
        snprintf(buf, buf_size,
                 "FoloToy AI Passport\n"
                 "Chip:   ESP32-C3\n"
                 "Flash:  8 MB\n"
                 "FW:     baseline v1.0\n"
                 "Uptime: %lus",
                 (unsigned long)s_tick_seconds);
        break;
    }
}

// ===== 刷新菜单态的选中高亮 =====
static void menu_refresh(void)
{
    for (int i = 0; i < ITEM_COUNT; i++) {
        ui_pixel_set_selected(s_items[i], i == s_sel, true);
    }
    char title[32];
    snprintf(title, sizeof(title), "SIMPLE MENU  %d/%d", s_sel + 1, ITEM_COUNT);
    lv_label_set_text(s_title_label, title);
}

// ===== 构建菜单页 UI =====
static void build_menu_page(void)
{
    // 标题条
    if (s_title_label) { lv_obj_delete(s_title_label); s_title_label = NULL; }
    s_title_label = ui_pixel_label(s_scr, "SIMPLE MENU  1/4",
                                   &lv_font_montserrat_14, UI_INK);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 48);

    // 每菜单项一行(选中时用 ui_pixel_set_selected 变色)
    int panel_y = ITEM_PANEL_Y;
    for (int i = 0; i < ITEM_COUNT; i++) {
        s_items[i] = ui_pixel_panel_create(
            s_scr,
            18, panel_y + i * ITEM_H,
            204, ITEM_H - 6,
            UI_PAPER);

        // 左:彩色条,右:文本
        lv_obj_t *bar = lv_obj_create(s_items[i]);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(bar, 0, 0);
        lv_obj_set_size(bar, 6, ITEM_H - 6 - 8);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(ITEM_COLOR[i]), 0);

        lv_obj_t *label = ui_pixel_label(s_items[i], ITEM_NAME[i],
                                         &lv_font_montserrat_16, UI_INK);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 16, 0);
    }

    if (s_status_label) { lv_obj_delete(s_status_label); s_status_label = NULL; }
    s_status_label = ui_pixel_label(s_scr,
        "UP/DOWN select  |  OK enter  |  LONG OK back",
        &lv_font_montserrat_12, UI_PAPER);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -16);

    menu_refresh();
}

// ===== 切换到信息页 =====
static void build_info_page(int item)
{
    s_info_item = item;

    // 清理菜单行
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (s_items[i]) { lv_obj_delete(s_items[i]); s_items[i] = NULL; }
    }
    if (s_status_label) { lv_obj_delete(s_status_label); s_status_label = NULL; }

    char title[40];
    snprintf(title, sizeof(title), "%s", ITEM_NAME[item]);
    if (s_title_label) lv_label_set_text(s_title_label, title);
    else s_title_label = ui_pixel_label(s_scr, title, &lv_font_montserrat_14, UI_INK);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 48);

    s_panel = ui_pixel_panel_create(s_scr, 18, ITEM_PANEL_Y,
                                    204, ITEM_PANEL_H, UI_PAPER);
    s_info_label = ui_pixel_label(s_panel, "", &lv_font_montserrat_14, UI_INK);
    lv_obj_set_style_text_align(s_info_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_info_label, LV_ALIGN_TOP_LEFT, 10, 10);

    char buf[256];
    info_build_text(item, buf, sizeof(buf));
    lv_label_set_text(s_info_label, buf);

    s_status_label = ui_pixel_label(s_scr,
        "OK: back to menu  |  LONG OK: exit demo",
        &lv_font_montserrat_12, UI_PAPER);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -16);
}

// ===== 从信息页返回菜单 =====
static void return_to_menu(void)
{
    if (s_panel)      { lv_obj_delete(s_panel);      s_panel = NULL; }
    s_info_label = NULL;
    build_menu_page();
}

// ===== LVGL 定时器(1s):模拟 uptime、刷新信息页文本 =====
static void tick(lv_timer_t *t)
{
    (void)t;
    s_tick_seconds++;
    if (s_page == PAGE_INFO && s_info_label) {
        char buf[256];
        info_build_text(s_info_item, buf, sizeof(buf));
        lv_label_set_text(s_info_label, buf);
    }
}

// ===== demo_entry_t: enter =====
void demo_simple_menu_enter(void)
{
    s_page        = PAGE_MENU;
    s_sel         = 0;
    s_info_item   = 0;
    s_tick_seconds = 0;
    s_panel       = NULL;
    s_title_label = NULL;
    s_info_label  = NULL;
    s_status_label = NULL;
    for (int i = 0; i < ITEM_COUNT; i++) s_items[i] = NULL;

    s_scr = ui_pixel_screen_create("SIMPLE");
    build_menu_page();
    s_mascot = ui_pixel_mascot_create(s_scr, 101, 238);
    s_timer  = lv_timer_create(tick, 1000, NULL);
    lv_screen_load(s_scr);
}

// ===== demo_entry_t: exit =====
void demo_simple_menu_exit(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_panel = s_title_label = s_info_label = s_status_label = s_mascot = NULL;
    for (int i = 0; i < ITEM_COUNT; i++) s_items[i] = NULL;
}

// ===== demo_entry_t: key(长按 OK 已由 main.c 拦截,不会到这里) =====
void demo_simple_menu_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;

    if (s_page == PAGE_MENU) {
        if (btn == BSP_BTN_UP) {
            s_sel = (s_sel + ITEM_COUNT - 1) % ITEM_COUNT;
            ui_pixel_mascot_jump(s_mascot);
            menu_refresh();
        } else if (btn == BSP_BTN_DOWN) {
            s_sel = (s_sel + 1) % ITEM_COUNT;
            ui_pixel_mascot_jump(s_mascot);
            menu_refresh();
        } else if (btn == BSP_BTN_OK) {
            s_page = PAGE_INFO;
            ui_pixel_mascot_jump(s_mascot);
            build_info_page(s_sel);
        }
    } else if (s_page == PAGE_INFO) {
        // 信息页内只处理 OK 返回,上/下留给未来扩展(当前忽略,不干扰)
        if (btn == BSP_BTN_OK) {
            s_page = PAGE_MENU;
            ui_pixel_mascot_jump(s_mascot);
            return_to_menu();
        }
    }
}
