#include "page_menu.h"
#include "app_ctx.h"

static void action_backrest(void)  { gfx_menu_push(s_menu, PAGE_BACKREST); }
static void action_pressure(void)  { gfx_menu_push(s_menu, PAGE_PRESSURE); }
static void action_lora_cfg(void)  { gfx_menu_push(s_menu, PAGE_LORA_CFG); }
static void action_exit_menu(void) { gfx_menu_pop(s_menu); }

static const gfx_menu_item_t s_items[] = {
    { "Backrest Angle", action_backrest  },
    { "Pressure Data",  action_pressure  },
    { "LoRa Config",    action_lora_cfg  },
    { "Exit Menu",      action_exit_menu },
};

static gfx_list_cfg_t s_list_cfg = {
    .title      = "Menu",
    .items      = s_items,
    .item_count = 4,
    .selected   = 0,
    .scroll_top = 0,
    .on_back    = NULL,
};

void page_menu_register(gfx_menu_engine_t *menu)
{
    gfx_page_desc_t desc;
    gfx_page_list_init(&desc, &s_list_cfg);
    gfx_menu_register_page(menu, PAGE_MENU, &desc);
}
