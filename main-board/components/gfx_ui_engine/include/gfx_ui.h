#ifndef GFX_UI_H
#define GFX_UI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool gfx_color_t;
#define GFX_COLOR_BLACK  false
#define GFX_COLOR_WHITE  true

/*
 * 显示设备抽象接口（HAL）
 * 增加了脏页标记回调 mark_dirty，保持驱动抽象隔离
 */
typedef struct {
	uint16_t width;
	uint16_t height;

	void (*draw_pixel)(void *dev, int16_t x, int16_t y, gfx_color_t color);
	void (*fill_rect)(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h, gfx_color_t color);
	void (*invert_rect)(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h);
	void (*flush)(void *dev);

	void (*lock)(void *dev);
	void (*unlock)(void *dev);

	uint8_t* (*get_framebuffer)(void *dev);
	
	/* 新增：主动标记硬件层脏矩形回调（C语言未指派初始化此项时，自动置为 NULL 兼容） */
	void (*mark_dirty)(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h);
} gfx_display_hal_t;

/*
 * 通用单色点阵字库描述结构体
 */
typedef struct {
	const uint8_t *bitmap;
	uint8_t width;
	uint8_t height;
	char first_char;
	char last_char;
} gfx_font_t;

/* 常用字体声明 */
extern const gfx_font_t gfx_font_5x8_spleen;
extern const gfx_font_t gfx_font_8x16_spleen;
extern const gfx_font_t gfx_font_8x16_terminus;

typedef struct gfx_ui_ctx gfx_ui_ctx_t;

/* 生命周期与互斥保护 */
gfx_ui_ctx_t *gfx_ui_create(const gfx_display_hal_t *hal, void *display_dev);
void gfx_ui_destroy(gfx_ui_ctx_t *ctx);

/* 核心并发安全锁包装 */
void gfx_ui_lock(gfx_ui_ctx_t *ctx);
void gfx_ui_unlock(gfx_ui_ctx_t *ctx);

/* 提交显示 */
void gfx_ui_flush(gfx_ui_ctx_t *ctx);

/* 剪裁窗口（UI 视口逻辑裁剪） */
void gfx_ui_set_clip_window(gfx_ui_ctx_t *ctx, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void gfx_ui_reset_clip_window(gfx_ui_ctx_t *ctx);

/*
 * 将一个新的裁剪区与当前活动剪裁窗口求交集，并将当前区压入栈，使新裁剪区生效 [1]。
 */
bool gfx_ui_push_clip_window(gfx_ui_ctx_t *ctx, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

/*
 * 弹出当前裁剪窗口，恢复至上一级剪裁视口状态 [1]。
 */
bool gfx_ui_pop_clip_window(gfx_ui_ctx_t *ctx);

/* 基础与高级图形绘制 */
void gfx_ui_draw_pixel(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, gfx_color_t color);
void gfx_ui_fill_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h, gfx_color_t color);
void gfx_ui_invert_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h);

void gfx_ui_draw_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h, gfx_color_t color);
void gfx_ui_draw_circle(gfx_ui_ctx_t *ctx, int16_t xc, int16_t yc, int16_t r, gfx_color_t color);
void gfx_ui_draw_line(gfx_ui_ctx_t *ctx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, gfx_color_t color);
void gfx_ui_draw_round_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t r, gfx_color_t color);

/* 文本、位图与排版辅助 */
void gfx_ui_draw_char(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, char c, const gfx_font_t *font, gfx_color_t color, bool opaque);
void gfx_ui_draw_string(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, const char *str, const gfx_font_t *font, gfx_color_t color, bool opaque);
void gfx_ui_draw_bitmap(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t *bitmap, gfx_color_t color);
uint16_t gfx_ui_get_string_width(const char *str, const gfx_font_t *font);

/* 高阶动画与渲染特性 API */
void gfx_ui_clear_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h);
uint8_t *gfx_ui_get_framebuffer(gfx_ui_ctx_t *ctx);
void gfx_ui_set_xor_mode(gfx_ui_ctx_t *ctx, bool enable);
void gfx_ui_set_offset(gfx_ui_ctx_t *ctx, int16_t offset_x, int16_t offset_y);
void gfx_ui_get_offset(gfx_ui_ctx_t *ctx, int16_t *offset_x, int16_t *offset_y);
void gfx_ui_draw_visual_circle(gfx_ui_ctx_t *ctx, int16_t cx, int16_t cy, int16_t visual_r, gfx_color_t color);
void gfx_ui_blit(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t *bitmap, gfx_color_t color, bool xor_mode);

/*
 * 新增：主动标记脏区域
 * 所有直接越过画点 API 写入帧缓冲的操作，在写入后应当调用此函数同步标记
 */
void gfx_ui_mark_dirty_rect(gfx_ui_ctx_t *ctx, int16_t x, int16_t y, uint16_t w, uint16_t h);

/*
 * 新增：帧缓冲内 2D 局域图像拷贝平移（软件滚动加速器）
 *   src_x, src_y: 源区域绝对起始坐标
 *   w, h: 移动区域宽度和高度
 *   dst_x, dst_y: 目标区域绝对起始坐标
 * 支持内存安全自适应重叠拷贝（memmove 行为），支持页对齐极速硬件拷贝，并在平移后自动标记目标脏页
 */
void gfx_ui_copy_rect(gfx_ui_ctx_t *ctx, int16_t src_x, int16_t src_y, uint16_t w, uint16_t h, int16_t dst_x, int16_t dst_y);

#ifdef __cplusplus
}
#endif

#endif /* GFX_UI_H */