#ifndef LCD12864_H
#define LCD12864_H

/*
 * 标准类型头文件：
 * stdint.h 提供 uint8_t、uint32_t 等整数类型，保证跨平台位宽一致。
 * stdbool.h 提供 bool、true、false。
 * esp_err.h 是 ESP‑IDF 的错误码定义（如 ESP_OK、ESP_ERR_INVALID_ARG）。
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
 * 用户在创建屏幕实例时需要传入该结构体，所有与硬件引脚相关的
 * 参数集中于此，避免在驱动代码中硬编码。字段含义与端口层配置
 * 一致，驱动核心不直接使用该结构体，而是透传给端口初始化函数。
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
 * 2. 不透明句柄（Opaque Handle）
 * ================================================================
 * 驱动对外只暴露 void* 类型的句柄，用户无法直接访问内部成员。
 * 这是 C 语言实现封装的核心技巧：前向声明一个结构体，但只通过
 * 指针传递，真正的结构体定义在 .c 文件中（此处未声明具体类型）。
 * 这样既保护了内部实现细节，也避免了头文件依赖。
 */
typedef struct lcd12864_ctx lcd12864_ctx_t;

/*
 * 创建并初始化屏幕实例。
 * 该函数会分配上下文内存、调用端口层初始化硬件、执行软件复位
 * 和初始化序列。成功返回指向上下文的指针，失败返回 NULL。
 */
void* lcd12864_create(const lcd12864_config_t *cfg);

/*
 * 执行屏幕的软件初始化序列（发送一系列命令）。
 * 通常在 lcd12864_create 内部已经自动调用，用户一般无需再手动调用。
 * 返回 ESP_OK 表示成功。
 */
esp_err_t lcd12864_init(lcd12864_ctx_t *handle);

/*
 * 反初始化，释放屏幕资源。
 * 包括关闭显示、释放端口层资源、释放上下文内存。
 */
void lcd12864_deinit(lcd12864_ctx_t *handle);

/*
 * 清空屏幕，并将光标重置到左上角 (0,0)。
 */
void lcd12864_clear(lcd12864_ctx_t *handle);

/*
 * 打开显示。屏幕内容保留，背光常亮。
 */
void lcd12864_display_on(lcd12864_ctx_t *handle);

/*
 * 关闭显示。屏幕内容仍然保留，只是不再刷新显示。
 */
void lcd12864_display_off(lcd12864_ctx_t *handle);

/*
 * 设置光标（DDRAM 地址）。
 * 对于常见的 128x64 点阵屏，坐标系为：
 *   x: 列（0 起始，通常 0~127 或 0~15 取决于像素/字符模式）
 *   y: 行（0 起始，通常 0~7 或 0~3）
 * 具体范围依赖屏幕 IC 和配置，此处约定上层按需要传入。
 */
void lcd12864_set_cursor(lcd12864_ctx_t *handle, uint8_t x, uint8_t y);

/*
 * 在当前光标位置写入一个字节的数据。
 * 对于文本模式，该字节对应 ASCII 字符或汉字编码（与字库有关）。
 * 写入后光标自动右移一位。
 */
void lcd12864_write_char(lcd12864_ctx_t *handle, char ch);

/*
 * 从当前光标位置开始，连续写入一个以 '\0' 结尾的字符串。
 * 自动处理光标移动。
 */
void lcd12864_write_string(lcd12864_ctx_t *handle, const char *str);

/*
 * ================================================================
 * 3. 新增 API（基于帧缓冲）
 * ================================================================
 */

/*
 * 将帧缓冲区内容整屏刷新到硬件。
 * 所有绘图操作（clear、draw_pixel、putc 等）仅修改内存中的帧缓冲，
 * 调用 flush 后才真正通过 SPI 发送到屏幕控制器。
 */
void lcd12864_flush(lcd12864_ctx_t *handle);

/*
 * 在指定像素位置画一个点。
 * x: 列坐标（0~127），y: 行坐标（0~63）。
 * color: true 点亮，false 熄灭。
 * 此操作只修改帧缓冲区，需调用 flush 才能生效。
 */
void lcd12864_draw_pixel(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, bool color);

/*
 * 填充一个矩形区域。
 * (x, y): 左上角像素坐标，w: 宽度，h: 高度。
 * color: true 点亮（白色），false 熄灭（黑色）。
 * 适用于绘制菜单高亮条、进度条或背景色块。
 */
void lcd12864_fill_rect(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool color);

/*
 * 在指定像素位置绘制一个 8x16 字符。
 * 基于 font_8x16.h 字库将 ASCII 字符渲染到帧缓冲区。
 * x 为像素列（0~127，需留出 8 像素宽度），y 为页号（0~6，因字符占 2 页）。
 * 此操作只修改帧缓冲区，需调用 flush 才能生效。
 */
void lcd12864_putc(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, char c);

/*
 * 在指定位置绘制字符串。
 * 从 (x, y) 开始依次绘制字符，超出右边界自动换行。
 * 此操作只修改帧缓冲区，需调用 flush 才能生效。
 */
void lcd12864_puts(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, const char *str);

/*
 * 获取互斥锁，保护帧缓冲和 SPI 通信不被多任务并发破坏。
 * 在绘制序列（clear → draw → flush）开始前调用，
 * 序列结束后调用 lcd12864_unlock。
 * 内部实现基于 FreeRTOS 的 xSemaphoreTake(portMAX_DELAY)。
 */
void lcd12864_lock(lcd12864_ctx_t *handle);

/*
 * 释放互斥锁。
 */
void lcd12864_unlock(lcd12864_ctx_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* LCD12864_H */