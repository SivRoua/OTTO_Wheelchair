#pragma once

#include "gfx_ui.h"
#include "lcd12864.h"

/* 用 lcd 实例构建 HAL 并创建 gfx_ui_ctx_t，失败返回 NULL */
gfx_ui_ctx_t *lcd_hal_create(lcd12864_ctx_t *lcd);
