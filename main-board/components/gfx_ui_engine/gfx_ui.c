#include <stdlib.h>
#include "gfx_ui.h"

#define GFX_UI_CLIP_STACK_DEPTH  4

typedef struct {
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
} gfx_clip_rect_t;

struct gfx_ui_ctx {
    const gfx_display_hal_t *hal;
    void *dev;

    int16_t clip_x0;
    int16_t clip_y0;
    int16_t clip_x1;
    int16_t clip_y1;

    gfx_clip_rect_t clip_stack[GFX_UI_CLIP_STACK_DEPTH];
    uint8_t clip_stack_ptr;

    int16_t offset_x;
    int16_t offset_y;

    bool xor_mode;
};

static inline bool clip_contains(const gfx_ui_ctx_t *ctx, int16_t x, int16_t y) {
    return (x >= ctx->clip_x0 && x <= ctx->clip_x1 && y >= ctx->clip_y0 && y <= ctx->clip_y1);
}

static inline void rect_intersect_s16(
    int16_t ax0, int16_t ay0, int16_t ax1, int16_t ay1,
    int16_t bx0, int16_t by0, int16_t bx1, int16_t by1,
    int16_t *ox0, int16_t *oy0, int16_t *ox1, int16_t *oy1
) {
    *ox0 = (ax0 > bx0) ? ax0 : bx0;
    *oy0 = (ay0 > by0) ? ay0 : by0;
    *ox1 = (ax1 < bx1) ? ax1 : bx1;
    *oy1 = (ay1 < by1) ? ay1 : by1;
}

static void draw_circle_corners(gfx_ui_ctx_t *ctx, int16_t xc, int16_t yc, int16_t r, uint8_t corners, gfx_color_t color) {
    int16_t x = 0;
    int16_t y = r;
    int16_t d = 3 - 2 * r;

    while (y >= x) {
        if (corners & 0x01) {
            gfx_ui_draw_pixel(ctx, (int16_t)(xc + x), (int16_t)(yc - y), color);
            gfx_ui_draw_pixel(ctx, (int16_t)(xc + y), (int16_t)(yc - x), color);
        }
        if (corners & 0x02) {
            gfx_ui_draw_pixel(ctx, (int16_t)(xc - x), (int16_t)(yc - y), color);
            gfx_ui_draw_pixel(ctx, (int16_t)(xc - y), (int16_t)(yc - x), color);
        }
        if (corners & 0x04) {
            gfx_ui_draw_pixel(ctx, (int16_t)(xc + x), (int16_t)(yc + y), color);
            gfx_ui_draw_pixel(ctx, (int16_t)(xc + y), (int16_t)(yc + x), color);
        }
        if (corners & 0x08) {
            gfx_ui_draw_pixel(ctx, (int16_t)(xc - x), (int16_t)(yc + y), color);
            gfx_ui_draw_pixel(ctx, (int16_t)(xc - y), (int16_t)(yc + x), color);
        }

        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

/* ================================================================
 * 生命周期与互斥同步
 * ================================================================ */

gfx_ui_ctx_t *gfx_ui_create(const gfx_display_hal_t *hal, void *display_dev) {
    if (!hal || !display_dev || !hal->draw_pixel || !hal->flush) return NULL;
    if (hal->width == 0 || hal->height == 0) return NULL;

    gfx_ui_ctx_t *ctx = (gfx_ui_ctx_t *)calloc(1, sizeof(gfx_ui_ctx_t));
    if (!ctx) return NULL;

    ctx->hal = hal;
    ctx->dev = display_dev;

    ctx->clip_x0 = 0;
    ctx->clip_y0 = 0;
    ctx->clip_x1 = (int16_t)(hal->width - 1);
    ctx->clip_y1 = (int16_t)(hal->height - 1);

    ctx->clip_stack_ptr = 0;
    ctx->offset_x = 0;
    ctx->offset_y = 0;
    ctx->xor_mode = false;

    return ctx;
}

void gfx_ui_destroy(gfx_ui_ctx_t *ctx) {
    free(ctx);
}

void gfx_ui_lock(gfx_ui_ctx_t *ctx) {
    if (ctx && ctx->hal && ctx->hal->lock) {
        ctx->hal->lock(ctx->dev);
    }
}

void gfx_ui_unlock(gfx_ui_ctx_t *ctx) {
    if (ctx && ctx->hal && ctx->hal->unlock) {
        ctx->hal->unlock(ctx->dev);
    }
}

void gfx_ui_flush(gfx_ui_ctx_t *ctx) {
    if (!ctx) return;
    ctx->hal->flush(ctx->dev);
}

/* ================================================================
 * 视口剪裁与剪裁嵌套栈管理
 * ================================================================ */

void gfx_ui_set_clip_window(gfx_ui_ctx_t *ctx, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    if (!ctx) return;

    int16_t max_x = (int16_t)(ctx->hal->width - 1);
    int16_t max_y = (int16_t)(ctx->hal->height - 1);

    ctx->clip_x0 = (x0 < 0) ? 0 : ((x0 > max_x) ? max_x : x0);
    ctx->clip_y0 = (y0 < 0) ? 0 : ((y0 > max_y) ? max_y : y0);
    ctx->clip_x1 = (x1 < 0) ? 0 : ((x1 > max_x) ? max_x : x1);
    ctx->clip_y1 = (y1 < 0) ? 0 : ((y1 > max_y) ? max_y : y1);

    if (ctx->clip_x1 < ctx->clip_x0) {
        int16_t t = ctx->clip_x0;
        ctx->clip_x0 = ctx->clip_x1;
        ctx->clip_x1 = t;
    }
    if (ctx->clip_y1 < ctx->clip_y0) {
        int16_t t = ctx->clip_y0;
        ctx->clip_y0 = ctx->clip_y1;
        ctx->clip_y1 = t;
    }
}

void gfx_ui_reset_clip_window(gfx_ui_ctx_t *ctx) {
    if (!ctx) return;
    ctx->clip_x0 = 0;
    ctx->clip_y0 = 0;
    ctx->clip_x1 = (int16_t)(ctx->hal->width - 1);
    ctx->clip_y1 = (int16_t)(ctx->hal->height - 1);
}

bool gfx_ui_push_clip_window(gfx_ui_ctx_t *ctx, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    if (!ctx) return false;
    if (ctx->clip_stack_ptr >= GFX_UI_CLIP_STACK_DEPTH) return false;

    ctx->clip_stack[ctx->clip_stack_ptr].x0 = ctx->clip_x0;
    ctx->clip_stack[ctx->clip_stack_ptr].y0 = ctx->clip_y0;
    ctx->clip_stack[ctx->clip_stack_ptr].x1 = ctx->clip_x1;
    ctx->clip_stack[ctx->clip_stack_ptr].y1 = ctx->clip_y1;
    ctx->clip_stack_ptr++;

    int16_t max_x = (int16_t)(ctx->hal->width - 1);
    int16_t max_y = (int16_t)(ctx->hal->height - 1);

    int16_t rx0 = (x0 < 0) ? 0 : ((x0 > max_x) ? max_x : x0);
    int16_t ry0 = (y0 < 0) ? 0 : ((y0 > max_y) ? max_y : y0);
    int16_t rx1 = (x1 < 0) ? 0 : ((x1 > max_x) ? max_x : x1);
    int16_t ry1 = (y1 < 0) ? 0 : ((y1 > max_y) ? max_y : y1);

    if (rx1 < rx0) { int16_t t = rx0; rx0 = rx1; rx1 = t; }
    if (ry1 < ry0) { int16_t t = ry0; ry0 = ry1; ry1 = t; }

    rect_intersect_s16(rx0, ry0, rx1, ry1, ctx->clip_x0, ctx->clip_y0, ctx->clip_x1, ctx->clip_y1,
                       &ctx->clip_x0, &ctx->clip_y0, &ctx->clip_x1, &ctx->clip_y1);

    return true;
}

bool gfx_ui_pop_clip_window(gfx_ui_ctx_t *ctx) {
    if (!ctx) return false;
    if (ctx->clip_stack_ptr == 0) return false;

    ctx->clip_stack_ptr--;
    ctx->clip_x0 = ctx->clip_stack[ctx->clip_stack_ptr].x0;
    ctx->clip_y0 = ctx->clip_stack[ctx->clip_stack_ptr].y0;
    ctx->clip_x1 = ctx->clip_stack[ctx->clip_stack_ptr].x1;
    ctx->clip_y1 = ctx->clip_stack[ctx->clip_stack_ptr].y1;

    return true;
}

/* ================================================================
 * 基础与高级图形绘制
 * ================================================================ */

void gfx_ui_draw_pixel(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, gfx_color_t color) {
    if (!ctx) return;

    int16_t abs_x = (int16_t)(x + ctx->offset_x);
    int16_t abs_y = (int16_t)(y + ctx->offset_y);

    if (abs_x < 0 || abs_x >= (int16_t)ctx->hal->width || abs_y < 0 || abs_y >= (int16_t)ctx->hal->height) return;
    if (!clip_contains(ctx, abs_x, abs_y)) return;

    if (ctx->xor_mode && ctx->hal->invert_rect) {
        ctx->hal->invert_rect(ctx->dev, abs_x, abs_y, 1, 1);
    } else {
        ctx->hal->draw_pixel(ctx->dev, abs_x, abs_y, color);
    }
}

void gfx_ui_fill_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h, gfx_color_t color) {
    if (!ctx || w == 0 || h == 0) return;

    int16_t abs_x = (int16_t)(x + ctx->offset_x);
    int16_t abs_y = (int16_t)(y + ctx->offset_y);

    int16_t rx0 = abs_x;
    int16_t ry0 = abs_y;
    int16_t rx1 = (int16_t)(abs_x + w - 1);
    int16_t ry1 = (int16_t)(abs_y + h - 1);

    int16_t ix0, iy0, ix1, iy1;
    rect_intersect_s16(rx0, ry0, rx1, ry1, ctx->clip_x0, ctx->clip_y0, ctx->clip_x1, ctx->clip_y1, &ix0, &iy0, &ix1, &iy1);
    if (ix1 < ix0 || iy1 < iy0) return;

    if (!ctx->xor_mode && ctx->hal->fill_rect) {
        ctx->hal->fill_rect(ctx->dev, ix0, iy0, (uint16_t)(ix1 - ix0 + 1), (uint16_t)(iy1 - iy0 + 1), color);
        return;
    }

    for (int16_t yy = iy0; yy <= iy1; yy++) {
        for (int16_t xx = ix0; xx <= ix1; xx++) {
            if (ctx->xor_mode) {
                gfx_ui_draw_pixel(ctx, (int16_t)(xx - ctx->offset_x), (int16_t)(yy - ctx->offset_y), color);
            } else {
                ctx->hal->draw_pixel(ctx->dev, xx, yy, color);
            }
        }
    }
}

void gfx_ui_invert_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h) {
    if (!ctx || w == 0 || h == 0) return;
    if (!ctx->hal->invert_rect) return;

    int16_t abs_x = (int16_t)(x + ctx->offset_x);
    int16_t abs_y = (int16_t)(y + ctx->offset_y);

    int16_t rx0 = abs_x;
    int16_t ry0 = abs_y;   // 修复：移除了 dbg_y 笔误
    int16_t rx1 = (int16_t)(abs_x + w - 1);
    int16_t ry1 = (int16_t)(abs_y + h - 1);

    int16_t ix0, iy0, ix1, iy1;
    rect_intersect_s16(rx0, ry0, rx1, ry1, ctx->clip_x0, ctx->clip_y0, ctx->clip_x1, ctx->clip_y1, &ix0, &iy0, &ix1, &iy1);
    if (ix1 < ix0 || iy1 < iy0) return;

    ctx->hal->invert_rect(ctx->dev, ix0, iy0, (uint16_t)(ix1 - ix0 + 1), (uint16_t)(iy1 - iy0 + 1));
}

void gfx_ui_draw_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h, gfx_color_t color) {
    if (!ctx || w == 0 || h == 0) return;

    for (uint16_t col = 0; col < w; col++) {
        gfx_ui_draw_pixel(ctx, (int16_t)(x + col), y, color);
        gfx_ui_draw_pixel(ctx, (int16_t)(x + col), (int16_t)(y + h - 1), color);
    }
    for (uint16_t row = 0; row < h; row++) {
        gfx_ui_draw_pixel(ctx, x, (int16_t)(y + row), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(x + w - 1), (int16_t)(y + row), color);
    }
}

void gfx_ui_draw_circle(gfx_ui_ctx_t *ctx, int16_t xc, int16_t yc, int16_t r, gfx_color_t color) {
    if (!ctx || r < 0) return;

    int16_t x = 0;
    int16_t y = r;
    int16_t d = 3 - 2 * r;

    while (y >= x) {
        gfx_ui_draw_pixel(ctx, (int16_t)(xc + x), (int16_t)(yc + y), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(xc - x), (int16_t)(yc + y), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(xc + x), (int16_t)(yc - y), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(xc - x), (int16_t)(yc - y), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(xc + y), (int16_t)(yc + x), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(xc - y), (int16_t)(yc + x), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(xc + y), (int16_t)(yc - x), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(xc - y), (int16_t)(yc - x), color);

        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

void gfx_ui_draw_line(gfx_ui_ctx_t *ctx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, gfx_color_t color) {
    if (!ctx) return;

    int16_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;

    while (1) {
        gfx_ui_draw_pixel(ctx, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_ui_draw_round_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t r, gfx_color_t color) {
    if (!ctx || w == 0 || h == 0) return;

    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    if (r == 0) {
        gfx_ui_draw_rect(ctx, x, y, w, h, color);
        return;
    }

    gfx_ui_draw_line(ctx, (int16_t)(x + r), y, (int16_t)(x + w - 1 - r), y, color);
    gfx_ui_draw_line(ctx, (int16_t)(x + r), (int16_t)(y + h - 1), (int16_t)(x + w - 1 - r), (int16_t)(y + h - 1), color);
    gfx_ui_draw_line(ctx, x, (int16_t)(y + r), x, (int16_t)(y + h - 1 - r), color);
    gfx_ui_draw_line(ctx, (int16_t)(x + w - 1), (int16_t)(y + r), (int16_t)(x + w - 1), (int16_t)(y + h - 1 - r), color);

    draw_circle_corners(ctx, (int16_t)(x + w - 1 - r), (int16_t)(y + r), r, 0x01, color);
    draw_circle_corners(ctx, (int16_t)(x + r), (int16_t)(y + r), r, 0x02, color);
    draw_circle_corners(ctx, (int16_t)(x + w - 1 - r), (int16_t)(y + h - 1 - r), r, 0x04, color);
    draw_circle_corners(ctx, (int16_t)(x + r), (int16_t)(y + h - 1 - r), r, 0x08, color);
}

/* ================================================================
 * 文本、位图与排版计算
 * ================================================================ */

void gfx_ui_draw_char(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, char c, const gfx_font_t *font, gfx_color_t color, bool opaque) {
    if (!ctx || !font) return;

    if (c < font->first_char || c > font->last_char) {
        c = ' ';
    }

    uint32_t char_offset = (uint32_t)(c - font->first_char);
    uint8_t bytes_per_row = (uint8_t)((font->width + 7) / 8);
    uint32_t bytes_per_char = (uint32_t)bytes_per_row * (uint32_t)font->height;
    const uint8_t *char_data = &font->bitmap[char_offset * bytes_per_char];

    for (uint16_t row = 0; row < font->height; row++) {
        for (uint16_t col = 0; col < font->width; col++) {
            uint8_t byte_idx = (uint8_t)(row * bytes_per_row + (col / 8));
            uint8_t bit_shift = (uint8_t)(7 - (col % 8));

            bool pixel_active = (char_data[byte_idx] & (1 << bit_shift)) != 0;
            if (pixel_active) {
                gfx_ui_draw_pixel(ctx, (int16_t)(x + col), (int16_t)(y + row), color);
            } else if (opaque) {
                gfx_ui_draw_pixel(ctx, (int16_t)(x + col), (int16_t)(y + row), !color);
            }
        }
    }
}

void gfx_ui_draw_string(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, const char *str, const gfx_font_t *font, gfx_color_t color, bool opaque) {
    if (!ctx || !str || !font) return;

    int16_t cur_x = x;
    int16_t cur_y = y;

    while (*str) {
        if (cur_x + (int16_t)font->width > (int16_t)ctx->hal->width) {
            cur_x = x;
            cur_y = (int16_t)(cur_y + font->height);
            if (cur_y + (int16_t)font->height > (int16_t)ctx->hal->height) {
                break;
            }
        }

        gfx_ui_draw_char(ctx, cur_x, cur_y, *str, font, color, opaque);
        cur_x = (int16_t)(cur_x + font->width);
        str++;
    }
}

void gfx_ui_draw_bitmap(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t *bitmap, gfx_color_t color) {
    if (!ctx || !bitmap || w == 0 || h == 0) return;

    uint16_t bytes_per_row = (uint16_t)((w + 7) / 8);

    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            uint32_t byte_idx = (uint32_t)row * (uint32_t)bytes_per_row + (uint32_t)(col / 8);
            uint8_t bit_shift = (uint8_t)(7 - (col % 8));

            bool pixel_active = (bitmap[byte_idx] & (1 << bit_shift)) != 0;
            if (pixel_active) {
                gfx_ui_draw_pixel(ctx, (int16_t)(x + col), (int16_t)(y + row), color);
            }
        }
    }
}

uint16_t gfx_ui_get_string_width(const char *str, const gfx_font_t *font) {
    if (!str || !font) return 0;
    
    uint16_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return (uint16_t)(len * font->width);
}

/* ================================================================
 * 高阶动画、渲染与局部擦除
 * ================================================================ */

void gfx_ui_clear_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h) {
    gfx_ui_fill_rect(ctx, x, y, w, h, GFX_COLOR_BLACK);
}

uint8_t *gfx_ui_get_framebuffer(gfx_ui_ctx_t *ctx) {
    if (ctx && ctx->hal && ctx->hal->get_framebuffer) {
        return ctx->hal->get_framebuffer(ctx->dev);
    }
    return NULL;
}

void gfx_ui_set_xor_mode(gfx_ui_ctx_t *ctx, bool enable) {
    if (!ctx) return;
    ctx->xor_mode = enable;
}

void gfx_ui_set_offset(gfx_ui_ctx_t *ctx, int16_t offset_x, int16_t offset_y) {
    if (!ctx) return;
    ctx->offset_x = offset_x;
    ctx->offset_y = offset_y;
}

void gfx_ui_get_offset(gfx_ui_ctx_t *ctx, int16_t *offset_x, int16_t *offset_y) {
    if (!ctx) return;
    if (offset_x) *offset_x = ctx->offset_x;
    if (offset_y) *offset_y = ctx->offset_y;
}

void gfx_ui_draw_visual_circle(gfx_ui_ctx_t *ctx, int16_t cx, int16_t cy, int16_t visual_r, gfx_color_t color) {
    if (!ctx || visual_r < 0) return;

    const float par = 0.8f; 
    int16_t rx = (int16_t)((float)visual_r / par + 0.5f);
    int16_t ry = visual_r;

    int16_t x = 0;
    int16_t y = ry;
    int32_t rx2 = (int32_t)rx * rx;
    int32_t ry2 = (int32_t)ry * ry;
    int32_t tworx2 = 2 * rx2;
    int32_t twory2 = 2 * ry2;
    int32_t p;
    int32_t px = 0;
    int32_t py = tworx2 * y;

    /* 区域 1 */
    p = (int32_t)((float)ry2 - (float)rx2 * (float)ry + 0.25f * (float)rx2 + 0.5f);
    while (px < py) {
        gfx_ui_draw_pixel(ctx, (int16_t)(cx + x), (int16_t)(cy + y), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(cx - x), (int16_t)(cy + y), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(cx + x), (int16_t)(cy - y), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(cx - x), (int16_t)(cy - y), color);
        x++;
        px += twory2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= tworx2;
            p += ry2 + px - py;
        }
    }

    /* 区域 2 */
    p = (int32_t)((float)ry2 * ((float)x + 0.5f) * ((float)x + 0.5f) + (float)rx2 * ((float)y - 1) * ((float)y - 1) - (float)rx2 * (float)ry2 + 0.5f);
    while (y >= 0) {
        gfx_ui_draw_pixel(ctx, (int16_t)(cx + x), (int16_t)(cy + y), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(cx - x), (int16_t)(cy + y), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(cx + x), (int16_t)(cy - y), color);
        gfx_ui_draw_pixel(ctx, (int16_t)(cx - x), (int16_t)(cy - y), color);
        y--;
        py -= tworx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twory2;
            p += rx2 - py + px;
        }
    }
}

void gfx_ui_blit(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t *bitmap, gfx_color_t color, bool xor_mode) {
    if (!ctx || !bitmap || w == 0 || h == 0) return;

    int16_t abs_x = (int16_t)(x + ctx->offset_x);
    int16_t abs_y = (int16_t)(y + ctx->offset_y);

    uint8_t *fb = gfx_ui_get_framebuffer(ctx);
    bool page_aligned = (abs_y % 8 == 0) && (h % 8 == 0);

    if (page_aligned && fb) {
        int16_t start_page = abs_y / 8;
        int16_t num_pages = (int16_t)(h / 8);

        for (uint16_t col = 0; col < w; col++) {
            int16_t target_x = (int16_t)(abs_x + col);
            if (target_x < ctx->clip_x0 || target_x > ctx->clip_x1) {
                continue;
            }

            for (int16_t p = 0; p < num_pages; p++) {
                int16_t target_page = (int16_t)(start_page + p);
                if (target_page * 8 < ctx->clip_y0 || target_page * 8 > ctx->clip_y1) {
                    continue;
                }

                uint32_t fb_idx = (uint32_t)target_page * 128 + (uint32_t)target_x;
                uint32_t bmp_idx = (uint32_t)p * w + col;

                uint8_t data = bitmap[bmp_idx];

                if (ctx->xor_mode || xor_mode) {
                    fb[fb_idx] ^= data;
                } else {
                    if (color) {
                        fb[fb_idx] |= data;
                    } else {
                        fb[fb_idx] &= ~data;
                    }
                }
            }
        }
        /* 修复：标记脏页，确保 flush 时更新到 LCD */
        gfx_ui_mark_dirty_rect(ctx, x, y, w, h);
        return;
    }

    uint16_t num_pages = h / 8;
    for (uint16_t p = 0; p < num_pages; p++) {
        for (uint16_t col = 0; col < w; col++) {
            uint32_t bmp_idx = (uint32_t)p * w + col;
            uint8_t data = bitmap[bmp_idx];
            for (uint8_t bit = 0; bit < 8; bit++) {
                bool pixel_active = (data & (1 << bit)) != 0;
                if (pixel_active) {
                    gfx_ui_draw_pixel(ctx, (int16_t)(x + col), (int16_t)(y + p * 8 + bit), color);
                }
            }
        }
    }
}

/* ================================================================
 * 新增：标记脏区域 & 帧缓冲区域拷贝
 * ================================================================ */

void gfx_ui_mark_dirty_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h) {
    if (!ctx || w == 0 || h == 0) return;

    // 转换到物理坐标
    int16_t abs_x = (int16_t)(x + ctx->offset_x);
    int16_t abs_y = (int16_t)(y + ctx->offset_y);

    // 裁剪到屏幕范围
    if (abs_x < 0) {
        w = (uint16_t)(w + abs_x);
        abs_x = 0;
    }
    if (abs_y < 0) {
        h = (uint16_t)(h + abs_y);
        abs_y = 0;
    }
    if (abs_x + w > ctx->hal->width)  w = ctx->hal->width - abs_x;
    if (abs_y + h > ctx->hal->height) h = ctx->hal->height - abs_y;
    if (w == 0 || h == 0) return;

    if (ctx->hal->mark_dirty) {
        ctx->hal->mark_dirty(ctx->dev, abs_x, abs_y, w, h);
    }
}

void gfx_ui_copy_rect(gfx_ui_ctx_t *ctx, int16_t src_x, int16_t src_y,
                      uint16_t w, uint16_t h, int16_t dst_x, int16_t dst_y) {
    if (!ctx || w == 0 || h == 0) return;

    uint8_t *fb = gfx_ui_get_framebuffer(ctx);
    if (!fb) return;

    // 物理坐标
    int16_t src_abs_x = (int16_t)(src_x + ctx->offset_x);
    int16_t src_abs_y = (int16_t)(src_y + ctx->offset_y);
    int16_t dst_abs_x = (int16_t)(dst_x + ctx->offset_x);
    int16_t dst_abs_y = (int16_t)(dst_y + ctx->offset_y);

    // 边界检查（简单忽略超界）
    if (src_abs_x < 0 || src_abs_y < 0 || dst_abs_x < 0 || dst_abs_y < 0) return;
    if (src_abs_x + w > ctx->hal->width || src_abs_y + h > ctx->hal->height) return;
    if (dst_abs_x + w > ctx->hal->width || dst_abs_y + h > ctx->hal->height) return;

    bool page_aligned = (src_abs_y % 8 == 0) && (dst_abs_y % 8 == 0) && (h % 8 == 0);
    if (page_aligned) {
        int16_t src_page = src_abs_y / 8;
        int16_t dst_page = dst_abs_y / 8;
        int16_t num_pages = h / 8;

        for (uint16_t col = 0; col < w; col++) {
            int16_t s_col = src_abs_x + col;
            int16_t d_col = dst_abs_x + col;
            for (int16_t p = 0; p < num_pages; p++) {
                uint32_t src_idx = (uint32_t)(src_page + p) * 128 + (uint32_t)s_col;
                uint32_t dst_idx = (uint32_t)(dst_page + p) * 128 + (uint32_t)d_col;
                fb[dst_idx] = fb[src_idx];
            }
        }
    } else {
        for (uint16_t row = 0; row < h; row++) {
            for (uint16_t col = 0; col < w; col++) {
                int16_t sx = src_abs_x + col;
                int16_t sy = src_abs_y + row;
                int16_t dx = dst_abs_x + col;
                int16_t dy = dst_abs_y + row;

                uint8_t s_page = sy >> 3;
                uint8_t s_bit  = sy & 0x07;
                uint32_t s_off = (uint32_t)s_page * 128 + (uint32_t)sx;
                bool pixel = (fb[s_off] & (1 << s_bit)) != 0;

                uint8_t d_page = dy >> 3;
                uint8_t d_bit  = dy & 0x07;
                uint32_t d_off = (uint32_t)d_page * 128 + (uint32_t)dx;

                if (pixel) fb[d_off] |= (1 << d_bit);
                else       fb[d_off] &= ~(1 << d_bit);
            }
        }
    }

    // 标记目标区域为脏
    gfx_ui_mark_dirty_rect(ctx, dst_x, dst_y, w, h);
}