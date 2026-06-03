#include "lcd_hal.h"

static void hal_draw_pixel(void *dev, int16_t x, int16_t y, gfx_color_t c)
    { lcd12864_draw_pixel(dev, x, y, c); }
static void hal_fill_rect(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h, gfx_color_t c)
    { lcd12864_fill_rect(dev, x, y, w, h, c); }
static void hal_invert_rect(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h)
    { lcd12864_invert_rect(dev, x, y, w, h); }
static void hal_flush(void *dev)       { lcd12864_flush(dev); }
static void hal_lock(void *dev)        { lcd12864_lock(dev); }
static void hal_unlock(void *dev)      { lcd12864_unlock(dev); }
static uint8_t *hal_get_fb(void *dev)  { return lcd12864_get_framebuffer(dev); }
static void hal_mark_dirty(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h)
    { lcd12864_mark_dirty(dev, x, y, w, h); }

gfx_ui_ctx_t *lcd_hal_create(lcd12864_ctx_t *lcd)
{
    static const gfx_display_hal_t hal = {
        .width           = 128,
        .height          = 64,
        .draw_pixel      = hal_draw_pixel,
        .fill_rect       = hal_fill_rect,
        .invert_rect     = hal_invert_rect,
        .flush           = hal_flush,
        .lock            = hal_lock,
        .unlock          = hal_unlock,
        .get_framebuffer = hal_get_fb,
        .mark_dirty      = hal_mark_dirty,
    };
    return gfx_ui_create(&hal, lcd);
}
