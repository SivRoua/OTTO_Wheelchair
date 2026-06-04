#include "page_main.h"
#include "app_ctx.h"
#include "gfx_ui.h"
#include "motion.h"
#include <stdio.h>

extern const gfx_font_t gfx_font_5x8_spleen;

/* ----------------------------------------------------------------
 * 状态栏图标 8×8 位图（每行1字节，共8行，MSB=左）
 * ---------------------------------------------------------------- */

/* 电池图标（满格占位） */
static const uint8_t ICON_BAT[8] = {
    0b00111100,
    0b00100100,
    0b11111111,
    0b10011001,
    0b10011001,
    0b10011001,
    0b10111101,
    0b11111111,
};

/* GPS 图标（卫星信号） */
static const uint8_t ICON_GPS_ON[8] = {
    0b00010000,
    0b00101000,
    0b01000100,
    0b10000010,
    0b00010000,
    0b00010000,
    0b00111000,
    0b00010000,
};

/* GPS 无信号（叉） */
static const uint8_t ICON_GPS_OFF[8] = {
    0b10000010,
    0b01000100,
    0b00101000,
    0b00010000,
    0b00101000,
    0b01000100,
    0b10000010,
    0b00000000,
};

/* LoRa 图标（无线波） */
static const uint8_t ICON_LORA_ON[8] = {
    0b00000000,
    0b00111100,
    0b01000010,
    0b10011001,
    0b00100100,
    0b00011000,
    0b00000000,
    0b00011000,
};

/* LoRa 离线（叉） */
static const uint8_t ICON_LORA_OFF[8] = {
    0b10000010,
    0b01000100,
    0b00101000,
    0b00010000,
    0b00101000,
    0b01000100,
    0b10000010,
    0b00000000,
};

/* ----------------------------------------------------------------
 * 状态栏渲染（y=0~9，高10px，图标居中于8px行）
 * ---------------------------------------------------------------- */
static void draw_status_bar(gfx_ui_ctx_t *ui)
{
    /* 背景 */
    gfx_ui_fill_rect(ui, 0, 0, 128, 10, GFX_COLOR_BLACK);

    /* 时间（左对齐，x=1，y=1） */
    char time_str[8];
    if (s_time_hour == 0xFF) {
        snprintf(time_str, sizeof(time_str), "--:--");
    } else {
        snprintf(time_str, sizeof(time_str), "%02u:%02u",
                 (unsigned)(s_time_hour % 24),
                 (unsigned)(s_time_min  % 60));
    }
    gfx_ui_draw_string(ui, 1, 1, time_str, &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);

    /* 图标区（右对齐，三个图标各8px宽，间距2px，从右往左排） */
    /* LoRa: x=118, GPS: x=108, BAT: x=98 */
    gfx_ui_draw_bitmap(ui, 118, 1, 8, 8,
                       s_lora_ok ? ICON_LORA_ON : ICON_LORA_OFF, GFX_COLOR_WHITE);
    gfx_ui_draw_bitmap(ui, 107, 1, 8, 8,
                       s_gps_located ? ICON_GPS_ON : ICON_GPS_OFF, GFX_COLOR_WHITE);
    gfx_ui_draw_bitmap(ui, 96, 1, 8, 8, ICON_BAT, GFX_COLOR_WHITE);

    /* 分隔线 */
    gfx_ui_draw_line(ui, 0, 9, 127, 9, GFX_COLOR_WHITE);
}

/* ----------------------------------------------------------------
 * 控制区渲染（y=10~63）
 * ---------------------------------------------------------------- */
static void draw_control_area(gfx_ui_ctx_t *ui)
{
    gfx_ui_fill_rect(ui, 0, 10, 128, 54, GFX_COLOR_BLACK);

    /* ↑ Forward  y=18 */
    gfx_ui_draw_string(ui, 44, 18, "^ Forward", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);

    /* ← Left  Right →  y=32 */
    gfx_ui_draw_string(ui,  1, 32, "< Left",   &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_draw_string(ui, 79, 32, "Right >",  &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);

    /* ↓ Back  y=46 */
    gfx_ui_draw_string(ui, 47, 46, "v Back",   &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);

    /* [CTR] Menu  y=57 */
    gfx_ui_draw_string(ui, 30, 57, "[CTR] Menu", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
}

/* ----------------------------------------------------------------
 * 页面回调
 * ---------------------------------------------------------------- */
static void on_enter(gfx_menu_engine_t *menu, void *ud)
{
    gfx_menu_set_dirty(menu);
}

static void on_render(gfx_menu_engine_t *menu, void *ud)
{
    gfx_ui_ctx_t *ui = gfx_menu_get_ui(menu);
    draw_status_bar(ui);
    draw_control_area(ui);
    gfx_ui_flush(ui);
}

static void on_input(gfx_menu_engine_t *menu, gfx_input_t ev, void *ud)
{
    /* 方向键和中键路由已在 main.c dispatch_key 处理，
     * Menu Engine 只会在非 PAGE_MAIN 时调用此回调，
     * 此处仅处理 ENTER（中键）进菜单的情况 */
    if (ev == GFX_INPUT_ENTER) {
        gfx_menu_push(menu, PAGE_MENU);
    }
}

/* Status Bar 每秒刷新一次（on_tick 约 50ms 调用一次，计 20 次） */
static uint8_t s_tick_count = 0;

static void on_tick(gfx_menu_engine_t *menu, void *ud)
{
    if (++s_tick_count >= 20) {
        s_tick_count = 0;
        gfx_menu_set_dirty(menu);
    }
}

/* ----------------------------------------------------------------
 * 注册
 * ---------------------------------------------------------------- */
void page_main_register(gfx_menu_engine_t *menu)
{
    static const gfx_page_desc_t desc = {
        .on_enter  = on_enter,
        .on_render = on_render,
        .on_input  = on_input,
        .on_tick   = on_tick,
    };
    gfx_menu_register_page(menu, PAGE_MAIN, &desc);
}
