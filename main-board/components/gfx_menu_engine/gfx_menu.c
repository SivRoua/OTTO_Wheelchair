#include "gfx_menu.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---------- 布局常量（128×64 屏幕） ---------- */
#define SCREEN_W          128
#define SCREEN_H           64
#define TITLE_H            10
#define LIST_ITEMS_Y       12
#define LIST_ITEM_H        10
#define WIPE_DELAY_MS      40

extern const gfx_font_t gfx_font_5x8_spleen;
extern const gfx_font_t gfx_font_8x16_terminus;

/* ================================================================
 * 内部结构
 * ================================================================ */
struct gfx_menu_engine {
    gfx_ui_ctx_t    *ui;
    gfx_page_desc_t  registry[GFX_PAGE_REGISTRY_MAX];
    bool             registered[GFX_PAGE_REGISTRY_MAX];
    uint8_t          nav_stack[GFX_NAV_STACK_DEPTH];
    uint8_t          nav_depth;
    bool             dirty;
};

/* ================================================================
 * 内部辅助
 * ================================================================ */

static inline gfx_page_desc_t *current_page_desc(gfx_menu_engine_t *menu) {
    if (menu->nav_depth == 0) return NULL;
    uint8_t id = menu->nav_stack[menu->nav_depth - 1];
    if (id >= GFX_PAGE_REGISTRY_MAX || !menu->registered[id]) return NULL;
    return &menu->registry[id];
}

typedef enum { WIPE_FORWARD, WIPE_BACKWARD } wipe_dir_t;

static void run_wipe_transition(gfx_ui_ctx_t *ui, wipe_dir_t dir) {
    int16_t first_x  = (dir == WIPE_FORWARD) ? 0  : 64;
    int16_t second_x = (dir == WIPE_FORWARD) ? 64 : 0;

    gfx_ui_lock(ui);
    gfx_ui_clear_rect(ui, first_x, 0, 64, SCREEN_H);
    gfx_ui_flush(ui);
    gfx_ui_unlock(ui);

    vTaskDelay(pdMS_TO_TICKS(WIPE_DELAY_MS));

    gfx_ui_lock(ui);
    gfx_ui_clear_rect(ui, second_x, 0, 64, SCREEN_H);
    gfx_ui_flush(ui);
    gfx_ui_unlock(ui);
}

static int16_t str_center_x(const char *str, const gfx_font_t *font) {
    uint16_t w = gfx_ui_get_string_width(str, font);
    int16_t x = (int16_t)((SCREEN_W - (int16_t)w) / 2);
    return x < 0 ? 0 : x;
}

static void draw_title_bar(gfx_ui_ctx_t *ui, const char *title) {
    gfx_ui_fill_rect(ui, 0, 0, SCREEN_W, TITLE_H, GFX_COLOR_WHITE);
    if (title) {
        int16_t tx = str_center_x(title, &gfx_font_5x8_spleen);
        gfx_ui_draw_string(ui, tx, 1, title, &gfx_font_5x8_spleen, GFX_COLOR_BLACK, true);
    }
}

/* ================================================================
 * 引擎生命周期
 * ================================================================ */

gfx_menu_engine_t *gfx_menu_create(gfx_ui_ctx_t *ui) {
    if (!ui) return NULL;
    gfx_menu_engine_t *menu = (gfx_menu_engine_t *)calloc(1, sizeof(gfx_menu_engine_t));
    if (!menu) return NULL;
    menu->ui    = ui;
    menu->dirty = false;
    return menu;
}

void gfx_menu_destroy(gfx_menu_engine_t *menu) {
    free(menu);
}

/* ================================================================
 * 页面注册
 * ================================================================ */

bool gfx_menu_register_page(gfx_menu_engine_t *menu,
                             uint8_t page_id,
                             const gfx_page_desc_t *desc) {
    if (!menu || !desc || page_id >= GFX_PAGE_REGISTRY_MAX) return false;
    menu->registry[page_id]   = *desc;
    menu->registered[page_id] = true;
    return true;
}

/* ================================================================
 * 导航
 * ================================================================ */

bool gfx_menu_push(gfx_menu_engine_t *menu, uint8_t page_id) {
    if (!menu) return false;
    if (page_id >= GFX_PAGE_REGISTRY_MAX || !menu->registered[page_id]) return false;
    if (menu->nav_depth >= GFX_NAV_STACK_DEPTH) return false;

    if (menu->nav_depth > 0) {
        gfx_page_desc_t *cur = current_page_desc(menu);
        if (cur && cur->on_exit) cur->on_exit(menu, cur->user_data);
        run_wipe_transition(menu->ui, WIPE_FORWARD);
    }

    menu->nav_stack[menu->nav_depth] = page_id;
    menu->nav_depth++;
    menu->dirty = true;

    gfx_page_desc_t *next = &menu->registry[page_id];
    if (next->on_enter) next->on_enter(menu, next->user_data);

    return true;
}

bool gfx_menu_pop(gfx_menu_engine_t *menu) {
    if (!menu || menu->nav_depth <= 1) return false;

    gfx_page_desc_t *cur = current_page_desc(menu);
    if (cur && cur->on_exit) cur->on_exit(menu, cur->user_data);

    run_wipe_transition(menu->ui, WIPE_BACKWARD);

    menu->nav_depth--;
    menu->dirty = true;

    gfx_page_desc_t *prev = current_page_desc(menu);
    if (prev && prev->on_enter) prev->on_enter(menu, prev->user_data);

    return true;
}

uint8_t gfx_menu_current_page(const gfx_menu_engine_t *menu) {
    if (!menu || menu->nav_depth == 0) return GFX_PAGE_ID_NONE;
    return menu->nav_stack[menu->nav_depth - 1];
}

uint8_t gfx_menu_stack_depth(const gfx_menu_engine_t *menu) {
    return menu ? menu->nav_depth : 0;
}

/* ================================================================
 * 输入、渲染、时钟
 * ================================================================ */

void gfx_menu_input(gfx_menu_engine_t *menu, gfx_input_t event) {
    if (!menu) return;
    gfx_page_desc_t *p = current_page_desc(menu);
    if (p && p->on_input) p->on_input(menu, event, p->user_data);
}

void gfx_menu_render(gfx_menu_engine_t *menu) {
    if (!menu) return;
    gfx_page_desc_t *p = current_page_desc(menu);
    if (!p || !p->on_render) return;
    gfx_ui_lock(menu->ui);
    p->on_render(menu, p->user_data);
    menu->dirty = false;
    gfx_ui_unlock(menu->ui);
}

void gfx_menu_set_dirty(gfx_menu_engine_t *menu) {
    if (menu) menu->dirty = true;
}

gfx_ui_ctx_t *gfx_menu_get_ui(gfx_menu_engine_t *menu) {
    return menu ? menu->ui : NULL;
}

void gfx_menu_tick(gfx_menu_engine_t *menu) {
    if (!menu || menu->nav_depth == 0) return;
    gfx_page_desc_t *p = current_page_desc(menu);
    if (p && p->on_tick) p->on_tick(menu, p->user_data);
    if (menu->dirty) gfx_menu_render(menu);
}

/* ================================================================
 * 内置助手：开机画面
 * ================================================================ */

static void boot_on_enter(gfx_menu_engine_t *menu, void *ud) {
    (void)menu;
    gfx_boot_cfg_t *cfg = (gfx_boot_cfg_t *)ud;
    cfg->_boot_start = xTaskGetTickCount();
}

static void boot_on_render(gfx_menu_engine_t *menu, void *ud) {
    gfx_ui_ctx_t *ui = menu->ui;
    gfx_boot_cfg_t *cfg = (gfx_boot_cfg_t *)ud;

    gfx_ui_clear_rect(ui, 0, 0, SCREEN_W, SCREEN_H);

    int16_t y = 4;

    if (cfg->logo && cfg->logo_w > 0 && cfg->logo_h > 0) {
        int16_t lx = (int16_t)((SCREEN_W - cfg->logo_w) / 2);
        gfx_ui_draw_bitmap(ui, lx, y, cfg->logo_w, cfg->logo_h, cfg->logo, GFX_COLOR_WHITE);
        y = (int16_t)(y + cfg->logo_h + 4);
    }

    if (cfg->title) {
        int16_t tx = str_center_x(cfg->title, &gfx_font_8x16_terminus);
        if (y + 16 > SCREEN_H) y = (int16_t)(SCREEN_H - 16 - 4);
        gfx_ui_draw_string(ui, tx, y, cfg->title, &gfx_font_8x16_terminus, GFX_COLOR_WHITE, false);
        y = (int16_t)(y + 18);
    }

    if (cfg->subtitle && y + 8 <= SCREEN_H) {
        int16_t sx = str_center_x(cfg->subtitle, &gfx_font_5x8_spleen);
        gfx_ui_draw_string(ui, sx, y, cfg->subtitle, &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    }

    gfx_ui_flush(ui);
}

static void boot_on_input(gfx_menu_engine_t *menu, gfx_input_t event, void *ud) {
    (void)event;
    gfx_boot_cfg_t *cfg = (gfx_boot_cfg_t *)ud;
    if (cfg->duration_ms > 0) {
        gfx_menu_push(menu, cfg->next_page_id);
    }
}

static void boot_on_tick(gfx_menu_engine_t *menu, void *ud) {
    gfx_boot_cfg_t *cfg = (gfx_boot_cfg_t *)ud;
    if (cfg->duration_ms == 0) return;
    TickType_t elapsed_ms = (xTaskGetTickCount() - cfg->_boot_start) * portTICK_PERIOD_MS;
    if (elapsed_ms >= cfg->duration_ms) {
        gfx_menu_push(menu, cfg->next_page_id);
    }
}

void gfx_page_boot_init(gfx_page_desc_t *desc, gfx_boot_cfg_t *cfg) {
    if (!desc || !cfg) return;
    desc->on_enter  = boot_on_enter;
    desc->on_render = boot_on_render;
    desc->on_input  = boot_on_input;
    desc->on_exit   = NULL;
    desc->on_tick   = boot_on_tick;
    desc->user_data = cfg;
}

/* ================================================================
 * 内置助手：列表菜单
 * ================================================================ */

static void list_draw_items(gfx_ui_ctx_t *ui, gfx_list_cfg_t *cfg) {
    uint8_t visible = cfg->item_count < GFX_LIST_VISIBLE_ROWS
                      ? cfg->item_count : GFX_LIST_VISIBLE_ROWS;

    for (uint8_t i = 0; i < visible; i++) {
        uint8_t idx = cfg->scroll_top + i;
        if (idx >= cfg->item_count) break;

        int16_t iy = (int16_t)(LIST_ITEMS_Y + i * LIST_ITEM_H);

        if (idx == cfg->selected) {
            gfx_ui_fill_rect(ui, 0, (int16_t)(iy - 1), SCREEN_W, LIST_ITEM_H, GFX_COLOR_WHITE);
            gfx_ui_draw_string(ui, 4, iy, cfg->items[idx].label,
                               &gfx_font_5x8_spleen, GFX_COLOR_BLACK, false);
        } else {
            gfx_ui_draw_string(ui, 4, iy, cfg->items[idx].label,
                               &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
        }
    }

    if (cfg->item_count > GFX_LIST_VISIBLE_ROWS) {
        uint8_t bar_h = (uint8_t)((GFX_LIST_VISIBLE_ROWS * LIST_ITEM_H * GFX_LIST_VISIBLE_ROWS)
                                  / cfg->item_count);
        uint8_t bar_y = (uint8_t)(LIST_ITEMS_Y + (cfg->scroll_top * LIST_ITEM_H
                                  * GFX_LIST_VISIBLE_ROWS) / cfg->item_count);
        gfx_ui_fill_rect(ui, SCREEN_W - 3, (int16_t)bar_y, 2, bar_h, GFX_COLOR_WHITE);
    }
}

static void list_on_render(gfx_menu_engine_t *menu, void *ud) {
    gfx_ui_ctx_t *ui = menu->ui;
    gfx_list_cfg_t *cfg = (gfx_list_cfg_t *)ud;

    gfx_ui_clear_rect(ui, 0, 0, SCREEN_W, SCREEN_H);
    draw_title_bar(ui, cfg->title);
    gfx_ui_draw_line(ui, 0, TITLE_H, SCREEN_W - 1, TITLE_H, GFX_COLOR_WHITE);

    if (!cfg->items || cfg->item_count == 0) {
        gfx_ui_draw_string(ui, 4, 24, "(empty)", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    } else {
        list_draw_items(ui, cfg);
    }

    gfx_ui_flush(ui);
}

static void list_move_cursor(gfx_menu_engine_t *menu, gfx_list_cfg_t *cfg, int8_t delta) {
    gfx_ui_ctx_t *ui = menu->ui;
    uint8_t old_sel = cfg->selected;

    if (delta < 0 && cfg->selected > 0) {
        cfg->selected--;
        if (cfg->selected < cfg->scroll_top) cfg->scroll_top = cfg->selected;
    } else if (delta > 0 && cfg->item_count > 0 && cfg->selected < cfg->item_count - 1) {
        cfg->selected++;
        if (cfg->selected >= cfg->scroll_top + GFX_LIST_VISIBLE_ROWS) {
            cfg->scroll_top = (uint8_t)(cfg->selected - GFX_LIST_VISIBLE_ROWS + 1);
        }
    } else {
        return;
    }

    /* 局部重绘：擦旧高亮 → 画新高亮，不等下一个 tick */
    gfx_ui_lock(ui);

    /* 擦除旧选中行 */
    uint8_t old_vis = old_sel - cfg->scroll_top;
    if (old_sel >= cfg->scroll_top && old_vis < GFX_LIST_VISIBLE_ROWS) {
        int16_t oy = (int16_t)(LIST_ITEMS_Y + old_vis * LIST_ITEM_H);
        gfx_ui_fill_rect(ui, 0, (int16_t)(oy - 1), SCREEN_W, LIST_ITEM_H, GFX_COLOR_BLACK);
        gfx_ui_draw_string(ui, 4, oy, cfg->items[old_sel].label,
                           &gfx_font_5x8_spleen, GFX_COLOR_WHITE, false);
    }

    /* 绘制新选中行 */
    uint8_t new_vis = cfg->selected - cfg->scroll_top;
    if (cfg->selected >= cfg->scroll_top && new_vis < GFX_LIST_VISIBLE_ROWS) {
        int16_t ny = (int16_t)(LIST_ITEMS_Y + new_vis * LIST_ITEM_H);
        gfx_ui_fill_rect(ui, 0, (int16_t)(ny - 1), SCREEN_W, LIST_ITEM_H, GFX_COLOR_WHITE);
        gfx_ui_draw_string(ui, 4, ny, cfg->items[cfg->selected].label,
                           &gfx_font_5x8_spleen, GFX_COLOR_BLACK, false);
    }

    gfx_ui_flush(ui);
    gfx_ui_unlock(ui);
}

static void list_on_input(gfx_menu_engine_t *menu, gfx_input_t event, void *ud) {
    gfx_list_cfg_t *cfg = (gfx_list_cfg_t *)ud;

    switch (event) {
    case GFX_INPUT_UP:
        list_move_cursor(menu, cfg, -1);
        break;
    case GFX_INPUT_DOWN:
        list_move_cursor(menu, cfg, +1);
        break;
    case GFX_INPUT_ENTER:
        if (cfg->items && cfg->selected < cfg->item_count) {
            if (cfg->items[cfg->selected].action) {
                cfg->items[cfg->selected].action();
            }
        }
        break;
    case GFX_INPUT_BACK:
        if (cfg->on_back) {
            cfg->on_back(menu, ud);
        } else {
            gfx_menu_pop(menu);
        }
        break;
    }
}

void gfx_page_list_init(gfx_page_desc_t *desc, gfx_list_cfg_t *cfg) {
    if (!desc || !cfg) return;
    desc->on_enter  = NULL;
    desc->on_render = list_on_render;
    desc->on_input  = list_on_input;
    desc->on_exit   = NULL;
    desc->on_tick   = NULL;
    desc->user_data = cfg;
}
