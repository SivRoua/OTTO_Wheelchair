#include "page_sub.h"
#include "app_ctx.h"
#include "gfx_ui.h"
#include "motion.h"
#include <stdio.h>

extern const gfx_font_t gfx_font_5x8_spleen;

/* ================================================================
 * PAGE_BACKREST — 靠背角度控制
 * 上键按下：步进正转（抬起）；下键按下：步进反转（放下）；松开停止
 * 左键返回菜单（由 dispatch_key 处理，on_input 仅作保底）
 * ================================================================ */
static void backrest_enter(gfx_menu_engine_t *m, void *ud)
    { gfx_menu_set_dirty(m); }

static void backrest_render(gfx_menu_engine_t *m, void *ud)
{
    gfx_ui_ctx_t *ui = gfx_menu_get_ui(m);
    gfx_ui_fill_rect(ui, 0, 0, 128, 64, GFX_COLOR_BLACK);
    gfx_ui_draw_string(ui, 2,  2, "Backrest Angle", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_draw_line(ui, 0, 11, 127, 11, GFX_COLOR_WHITE);
    gfx_ui_draw_string(ui, 20, 20, "^ Hold: Raise",  &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_draw_string(ui, 20, 32, "v Hold: Lower",  &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_draw_string(ui, 20, 44, "Release: Stop",  &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_draw_string(ui,  2, 56, "< Back",         &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_flush(ui);
}

static void backrest_input(gfx_menu_engine_t *m, gfx_input_t ev, void *ud)
    { if (ev == GFX_INPUT_BACK) gfx_menu_pop(m); }

void page_backrest_register(gfx_menu_engine_t *menu)
{
    static const gfx_page_desc_t desc = {
        .on_enter  = backrest_enter,
        .on_render = backrest_render,
        .on_input  = backrest_input,
    };
    gfx_menu_register_page(menu, PAGE_BACKREST, &desc);
}

/* ================================================================
 * PAGE_PRESSURE — 压力传感器（占位）
 * ================================================================ */
static void pressure_enter(gfx_menu_engine_t *m, void *ud)
    { gfx_menu_set_dirty(m); }

static void pressure_render(gfx_menu_engine_t *m, void *ud)
{
    gfx_ui_ctx_t *ui = gfx_menu_get_ui(m);
    gfx_ui_fill_rect(ui, 0, 0, 128, 64, GFX_COLOR_BLACK);
    gfx_ui_draw_string(ui, 2,  2, "Pressure Data",  &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_draw_line(ui, 0, 11, 127, 11, GFX_COLOR_WHITE);
    gfx_ui_draw_string(ui, 8, 26, "Sensor not",     &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_draw_string(ui, 8, 36, "available yet",  &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_draw_string(ui, 2, 56, "< Back",         &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_flush(ui);
}

static void pressure_input(gfx_menu_engine_t *m, gfx_input_t ev, void *ud)
    { if (ev == GFX_INPUT_BACK) gfx_menu_pop(m); }

void page_pressure_register(gfx_menu_engine_t *menu)
{
    static const gfx_page_desc_t desc = {
        .on_enter  = pressure_enter,
        .on_render = pressure_render,
        .on_input  = pressure_input,
    };
    gfx_menu_register_page(menu, PAGE_PRESSURE, &desc);
}

/* ================================================================
 * PAGE_LORA_CFG — LoRa 配置查看
 * 进入时读取一次参数，上下键滚动查看，左键返回
 * ================================================================ */
typedef struct {
    bool          params_loaded;
    lora_config_params_t params;
    uint8_t       scroll;   /* 0=状态行, 1=参数行 */
} lora_page_state_t;

static lora_page_state_t s_lora_state;

static void lora_enter(gfx_menu_engine_t *m, void *ud)
{
    lora_page_state_t *st = (lora_page_state_t *)ud;
    st->scroll = 0;
    st->params_loaded = false;
    if (s_lora_ok && s_lora) {
        st->params_loaded =
            (lora_get_config(s_lora, &st->params) == ESP_OK);
    }
    gfx_menu_set_dirty(m);
}

static void lora_render(gfx_menu_engine_t *m, void *ud)
{
    lora_page_state_t *st = (lora_page_state_t *)ud;
    gfx_ui_ctx_t *ui = gfx_menu_get_ui(m);
    char buf[24];

    gfx_ui_fill_rect(ui, 0, 0, 128, 64, GFX_COLOR_BLACK);
    gfx_ui_draw_string(ui, 2,  2, "LoRa Config", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_draw_line(ui, 0, 11, 127, 11, GFX_COLOR_WHITE);
    gfx_ui_draw_string(ui, 2, 14,
                       s_lora_ok ? "Module:  OK" : "Module:  FAIL",
                       &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);

    if (s_lora_ok && st->params_loaded) {
        snprintf(buf, sizeof(buf), "Channel: %u",    st->params.channel);
        gfx_ui_draw_string(ui, 2, 24, buf, &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
        snprintf(buf, sizeof(buf), "Power:   %u dBm", st->params.power);
        gfx_ui_draw_string(ui, 2, 34, buf, &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
        snprintf(buf, sizeof(buf), "Level:   %u",    st->params.level);
        gfx_ui_draw_string(ui, 2, 44, buf, &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    } else if (!s_lora_ok) {
        gfx_ui_draw_string(ui, 2, 34, "No module found", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    } else {
        gfx_ui_draw_string(ui, 2, 34, "Read failed",    &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    }

    gfx_ui_draw_string(ui, 2, 56, "< Back", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    gfx_ui_flush(ui);
}

static void lora_input(gfx_menu_engine_t *m, gfx_input_t ev, void *ud)
    { if (ev == GFX_INPUT_BACK) gfx_menu_pop(m); }

void page_lora_cfg_register(gfx_menu_engine_t *menu)
{
    static const gfx_page_desc_t desc = {
        .on_enter  = lora_enter,
        .on_render = lora_render,
        .on_input  = lora_input,
        .user_data = &s_lora_state,
    };
    gfx_menu_register_page(menu, PAGE_LORA_CFG, &desc);
}
