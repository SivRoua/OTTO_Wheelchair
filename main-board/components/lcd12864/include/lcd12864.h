#ifndef LCD12864_H
#define LCD12864_H

/*
 * 标准类型头文件：
 * stdint.h 提供 uint8_t、uint32_t 等整数类型。
 * stdbool.h 提供 bool、true、false。
 * esp_err.h 是 ESP‑IDF 的错误码定义（如 ESP_OK）。
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ================================================================
 * 1. 配置结构体
 * ================================================================
 */
typedef struct {
    int sclk;          /* SPI 时钟引脚 */
    int sda;           /* SPI 数据输出引脚（MOSI） */
    int rs;            /* 指令/数据选择引脚（DC），也称 A0 */
    int cs;            /* 片选引脚（低有效） */
    int reset;         /* 硬件复位引脚（低有效） */
    uint32_t freq_hz;  /* SPI 时钟频率，例如 4000000 表示 4MHz */
} lcd12864_config_t;

/*
 * ================================================================
 * 2. 核心句柄与生命周期控制 API
 * ================================================================
 */
typedef struct lcd12864_ctx lcd12864_ctx_t;

/*
 * 创建并初始化屏幕实例。返回指向不透明句柄的指针。
 */
lcd12864_ctx_t *lcd12864_create(const lcd12864_config_t *cfg);

/*
 * 执行屏幕的硬件初始化命令序列。
 */
esp_err_t lcd12864_init(lcd12864_ctx_t *handle);

/*
 * 反初始化，安全释放屏幕所占用的硬件和内存资源。
 */
void lcd12864_deinit(lcd12864_ctx_t *handle);

/*
 * 清空帧缓冲区。
 */
void lcd12864_clear(lcd12864_ctx_t *handle);

/*
 * 打开显示。
 */
void lcd12864_display_on(lcd12864_ctx_t *handle);

/*
 * 关闭显示。屏幕内容保留，仅关闭显示扫描。
 */
void lcd12864_display_off(lcd12864_ctx_t *handle);

/*
 * 调整显示对比度（亮度调节）。
 * contrast: 对比度等级（0x00 ~ 0x3F，UC1701X 控制器标准）。
 */
void lcd12864_set_contrast(lcd12864_ctx_t *handle, uint8_t contrast);

/*
 * ================================================================
 * 3. 帧缓冲绘图与通信同步 API
 * ================================================================
 */

/*
 * 将帧缓冲区中被修改的“脏”物理页整屏刷新到物理硬件，未被修改的页不进行 SPI 传输（局部刷新） [2]。
 */
void lcd12864_flush(lcd12864_ctx_t *handle);

/* 在指定绝对像素位置绘制一个点（写入帧缓冲并标记脏页）。 */
void lcd12864_draw_pixel(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, bool color);

/*
 * 绘制并填充一个实心矩形色块。
 */
void lcd12864_fill_rect(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool color);

/*
 * 快速区域反色操作（对菜单高亮反白极为实用）。
 * 将指定矩形区域内的所有像素点亮灭翻转。
 */
void lcd12864_invert_rect(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, uint8_t w, uint8_t h);

/*
 * 互斥锁保护 API。
 */
void lcd12864_lock(lcd12864_ctx_t *handle);
void lcd12864_unlock(lcd12864_ctx_t *handle);

/* ================================================================
 * 新增：供 HAL 使用的辅助接口
 * ================================================================ */

/**
 * @brief 获取帧缓冲区指针（只读，请勿 free）
 */
uint8_t *lcd12864_get_framebuffer(lcd12864_ctx_t *handle);

/**
 * @brief 标记帧缓冲脏区域（按物理页）
 *        直接操作驱动的 dirty_pages 字段，供 UI 层 blit 等函数使用
 */
void lcd12864_mark_dirty(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h);

#ifdef __cplusplus
}
#endif

#endif /* LCD12864_H */