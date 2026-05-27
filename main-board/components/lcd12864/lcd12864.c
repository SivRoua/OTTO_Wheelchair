#include <stdlib.h>      /* 提供 calloc, free */
#include <string.h>      /* 提供 memset 操作帧缓冲区 */
#include "lcd12864.h"
#include "lcd12864_port.h"  /* 获取端口操作表和端口配置类型 */
#include "esp_heap_caps.h"

/*
 * FreeRTOS 任务互斥安全支持
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * ================================================================
 * 内部上下文结构体（真正的屏幕实例）
 * ================================================================
 */
struct lcd12864_ctx {
    lcd12864_port_ops_t ops;              /* 物理端口操作表，提供底层调用接口 */
    bool is_on;                           /* 扫描开关状态 */
    uint8_t *framebuffer;                 /* 帧缓冲区：128 × 64 / 8 = 1024 字节 */
    SemaphoreHandle_t mutex;              /* 互斥信号量 */
    /* 页级脏标记（8 个 bit 对应 LCD 8 个物理存储页，脏页在 flush 时才传输） */
    uint8_t dirty_pages;
};

/*
 * 引用底层注册的物理操作表实例（通常定义在 port/esp_idf_spi.c 中）
 */
extern const lcd12864_port_ops_t lcd12864_port_ops;

/*
 * 内部辅助函数：转换公开配置为端口内部配置，确保类型安全
 */
static lcd12864_port_config_t cfg_to_port(const lcd12864_config_t *cfg) {
    lcd12864_port_config_t port_cfg;
    port_cfg.sclk   = cfg->sclk;
    port_cfg.sda    = cfg->sda;
    port_cfg.rs     = cfg->rs;
    port_cfg.cs     = cfg->cs;
    port_cfg.reset  = cfg->reset;
    port_cfg.freq_hz = cfg->freq_hz;
    return port_cfg;
}

/*
 * ================================================================
 * 核心生命周期 API 实现
 * ================================================================
 */

lcd12864_ctx_t *lcd12864_create(const lcd12864_config_t *cfg) {
    lcd12864_ctx_t *ctx = calloc(1, sizeof(lcd12864_ctx_t));
    if (!ctx) {
        return NULL;
    }

    ctx->ops = lcd12864_port_ops;

    /* 初始化端口 */
    lcd12864_port_config_t port_cfg = cfg_to_port(cfg);
    ctx->ops.init(&port_cfg);

    /* 执行硬件复位，UC1701X 推荐电平脉冲 */
    ctx->ops.reset(false);
    ctx->ops.delay_ms(100);
    ctx->ops.reset(true);
    ctx->ops.delay_ms(100);

    ctx->framebuffer = heap_caps_malloc(1024, MALLOC_CAP_DMA);
    if (!ctx->framebuffer) {
        ctx->ops.deinit();
        free(ctx);
        return NULL;
    }

    ctx->mutex = xSemaphoreCreateMutex();
    if (!ctx->mutex) {
        heap_caps_free(ctx->framebuffer);
        ctx->ops.deinit();
        free(ctx);
        return NULL;
    }

    /* 执行控制 IC 的引导初始化 */
    if (lcd12864_init(ctx) != ESP_OK) {
        vSemaphoreDelete(ctx->mutex);
        free(ctx->framebuffer);
        ctx->ops.deinit();
        free(ctx);
        return NULL;
    }

    /* 脏标记初始化为全脏，保证首次 flush 能完整输出 */
    ctx->dirty_pages = 0xFF;

    lcd12864_clear(ctx);
    lcd12864_flush(ctx);

    return ctx;
}

esp_err_t lcd12864_init(lcd12864_ctx_t *handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;

    handle->ops.delay_ms(10);
    handle->ops.write_cmd(0xE2);   // 软复位
    handle->ops.delay_ms(10);

    // 升压逐步开启（必须）
    handle->ops.write_cmd(0x2C);   handle->ops.delay_ms(5);
    handle->ops.write_cmd(0x2E);   handle->ops.delay_ms(5);
    handle->ops.write_cmd(0x2F);   handle->ops.delay_ms(5);

    // 对比度（这个值决定能否看见内容）
    handle->ops.write_cmd(0x81);
    handle->ops.write_cmd(0x21);   // 对比度 0x20~0x3F，0x30 是安全值

    // 偏压比（根据屏幕数据手册，常用 0xA2 对应 1/9 偏压）
    handle->ops.write_cmd(0xA2);

    // 扫描方向
    handle->ops.write_cmd(0xA0);
    handle->ops.write_cmd(0xC8);

    // 显示开启
    handle->ops.write_cmd(0xAF);
    handle->ops.delay_ms(10);

    handle->is_on = true;
    return ESP_OK;
}

void lcd12864_deinit(lcd12864_ctx_t *handle) {
    if (!handle) return;

    handle->ops.write_cmd(0xAE); /* 关闭硬件扫描 */
    handle->ops.delay_ms(10);
    handle->ops.deinit();

    if (handle->mutex) {
        vSemaphoreDelete(handle->mutex);
    }
    if (handle->framebuffer) {
        free(handle->framebuffer);
    }
    free(handle);
}

void lcd12864_clear(lcd12864_ctx_t *handle) {
    if (!handle || !handle->framebuffer) return;

    memset(handle->framebuffer, 0x00, 1024);
    
    /* 标记全物理页脏，下一次 flush 清空屏幕 */
    handle->dirty_pages = 0xFF;
}

void lcd12864_display_on(lcd12864_ctx_t *handle) {
    if (!handle) return;
    handle->ops.write_cmd(0xAF);
    handle->is_on = true;
}

void lcd12864_display_off(lcd12864_ctx_t *handle) {
    if (!handle) return;
    handle->ops.write_cmd(0xAE);
    handle->is_on = false;
}

void lcd12864_set_contrast(lcd12864_ctx_t *handle, uint8_t contrast) {
    if (!handle) return;
    handle->ops.write_cmd(0x81);              /* 对比度调整指令前缀 */
    handle->ops.write_cmd(contrast & 0x3F);   /* 写入实际电平值 (0~63) */
}

/*
 * ================================================================
 * 绘图与视口控制、多字库自由坐标渲染 API
 * ================================================================
 */

void lcd12864_flush(lcd12864_ctx_t *handle) {
    if (!handle || !handle->framebuffer) return;

    /* 局部刷新：检测无像素脏页直接跳过物理 SPI 发送，极大解放处理器带宽 */
    if (handle->dirty_pages == 0) return;

    for (uint8_t page = 0; page < 8; page++) {
        if (handle->dirty_pages & (1 << page)) {
            handle->ops.write_cmd(0xB0 | page);
            handle->ops.write_cmd(0x10);  /* 列高位 */
            handle->ops.write_cmd(0x00);  /* 列低位 */
            
            handle->ops.write_data_bulk(&handle->framebuffer[page * 128], 128);
        }
    }
    /* 传输完清空页状态 */
    handle->dirty_pages = 0;
}

void lcd12864_draw_pixel(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, bool color) {
    if (!handle || !handle->framebuffer) return;

    if (x >= 128 || y >= 64) return;

    uint8_t page = y >> 3; /* 行像素定位至物理页 */
    uint16_t offset = ((uint16_t)page * 128) + x;
    uint8_t bit = y & 0x07;

    /* 2. 读取缓冲做差，像素无改变则不更新，避免伪脏页过多拖累刷新率 */
    bool old_pixel = (handle->framebuffer[offset] & (1 << bit)) != 0;
    if (old_pixel != color) {
        if (color) {
            handle->framebuffer[offset] |= (1 << bit);
        } else {
            handle->framebuffer[offset] &= ~(1 << bit);
        }
        /* 仅在像素确实改变时标记该页脏，提高局部刷新命中效率 */
        handle->dirty_pages |= (1 << page);
    }
}

void lcd12864_fill_rect(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool color) {
    if (!handle || !handle->framebuffer) return;

    if (x >= 128 || y >= 64) return;
    if (x + w > 128) w = 128 - x;
    if (y + h > 64)  h = 64 - y;

    for (uint8_t row = 0; row < h; row++) {
        for (uint8_t col = 0; col < w; col++) {
            lcd12864_draw_pixel(handle, x + col, y + row, color);
        }
    }
}

void lcd12864_invert_rect(lcd12864_ctx_t *handle, uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    if (!handle || !handle->framebuffer) return;

    if (x >= 128 || y >= 64) return;
    if (x + w > 128) w = 128 - x;
    if (y + h > 64)  h = 64 - y;

    for (uint8_t row = 0; row < h; row++) {
        for (uint8_t col = 0; col < w; col++) {
            uint8_t target_x = x + col;
            uint8_t target_y = y + row;

            if (target_x >= 128 || target_y >= 64) continue;

            uint8_t page = target_y >> 3;
            uint16_t offset = ((uint16_t)page * 128) + target_x;
            uint8_t bit = target_y & 0x07;

            /* XOR 翻转 */
            handle->framebuffer[offset] ^= (1 << bit);
            handle->dirty_pages |= (1 << page);
        }
    }
}

/*
 * ================================================================
 * 互斥锁控制
 * ================================================================
 */

void lcd12864_lock(lcd12864_ctx_t *handle) {
    if (!handle || !handle->mutex) return;
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
}

void lcd12864_unlock(lcd12864_ctx_t *handle) {
    if (!handle || !handle->mutex) return;
    xSemaphoreGive(handle->mutex);
}

/* ================================================================
 * 新增：供 HAL 使用的辅助接口
 * ================================================================ */

/**
 * @brief 获取帧缓冲区指针（用于 UI 层高效块操作）
 */
uint8_t *lcd12864_get_framebuffer(lcd12864_ctx_t *handle) {
    if (!handle) return NULL;
    return handle->framebuffer;
}

/**
 * @brief HAL 的脏页标记回调
 *        根据矩形区域计算涉及的物理页并置脏
 */
void lcd12864_mark_dirty(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h) {
    lcd12864_ctx_t *lcd = (lcd12864_ctx_t *)dev;
    if (!lcd) return;

    // 计算起始和结束的物理页（每 8 行一页）
    uint8_t start_page = (uint8_t)(y / 8);
    uint8_t end_page   = (uint8_t)((y + h - 1) / 8);

    // 边界保护（页号最大为 7）
    if (start_page > 7) start_page = 7;
    if (end_page > 7)   end_page = 7;

    for (uint8_t p = start_page; p <= end_page; p++) {
        lcd->dirty_pages |= (1 << p);
    }
}