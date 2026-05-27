/*
 * main/my_new_project.c
 *
 * LoRa 周期发送（每 1s）
 * ------------------------------------------------
 * 将固定的 GPS NMEA 坐标数据通过 LoRa 透明传输发送出去。
 * 发送内容（按用户要求）：
 *   $GPRMC,093809.000,A,3344.8260,N,11312.5340,E,0.0,0.0,220526,,,A*6A
 *
 * 引脚：
 *   LCD: Kconfig 配置（用于简单状态提示，可删除）
 *   LoRa: UART_BUS_NUM_2, TX=10, RX=11, 9600, AUX=12
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd12864.h"
#include "gfx_ui.h"
#include "LoRa.h"

#define PIN_SCLK   CONFIG_LCD12864_PIN_SCLK
#define PIN_SDA    CONFIG_LCD12864_PIN_SDA
#define PIN_RS     CONFIG_LCD12864_PIN_RS
#define PIN_CS     CONFIG_LCD12864_PIN_CS
#define PIN_RESET  CONFIG_LCD12864_PIN_RESET
#define SPI_FREQ   CONFIG_LCD12864_SPI_CLOCK_HZ

#define LORA_AUX_PIN    12
#define LORA_UART_NUM   UART_BUS_NUM_2
#define LORA_TX_PIN     10
#define LORA_RX_PIN     11
#define LORA_BAUD       9600

static const char *GPS_GPRMC_SENTENCE =
    "$GNGGA,111827.000,3345.04155,N,11312.12009,E,1,08,7.2,144.1,M,-20.4,M,,*66\r\n";

static void lcd_display_draw_pixel(void *dev, int16_t x, int16_t y, gfx_color_t color)
{
    lcd12864_draw_pixel((lcd12864_ctx_t *)dev, (uint8_t)x, (uint8_t)y, color);
}

static void lcd_display_fill_rect(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h, gfx_color_t color)
{
    lcd12864_fill_rect((lcd12864_ctx_t *)dev, (uint8_t)x, (uint8_t)y, (uint8_t)w, (uint8_t)h, color);
}

static void lcd_display_invert_rect(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    lcd12864_invert_rect((lcd12864_ctx_t *)dev, (uint8_t)x, (uint8_t)y, (uint8_t)w, (uint8_t)h);
}

static void lcd_display_flush(void *dev)
{
    lcd12864_flush((lcd12864_ctx_t *)dev);
}

static void lcd_display_lock(void *dev)
{
    lcd12864_lock((lcd12864_ctx_t *)dev);
}

static void lcd_display_unlock(void *dev)
{
    lcd12864_unlock((lcd12864_ctx_t *)dev);
}

static uint8_t *lcd_display_get_framebuffer(void *dev)
{
    return lcd12864_get_framebuffer((lcd12864_ctx_t *)dev);
}

static void lcd_display_mark_dirty(void *dev, int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    lcd12864_mark_dirty(dev, x, y, w, h);
}

void app_main(void)
{
    // ---------- LCD ----------
    lcd12864_config_t lcd_cfg = {
        .sclk = PIN_SCLK, .sda = PIN_SDA, .rs = PIN_RS,
        .cs = PIN_CS, .reset = PIN_RESET, .freq_hz = SPI_FREQ,
    };
    lcd12864_ctx_t *lcd = (lcd12864_ctx_t *)lcd12864_create(&lcd_cfg);
    if (!lcd) return;

    gfx_display_hal_t lcd_hal = {
        .width       = 128,
        .height      = 64,
        .draw_pixel  = lcd_display_draw_pixel,
        .fill_rect   = lcd_display_fill_rect,
        .invert_rect = lcd_display_invert_rect,
        .flush       = lcd_display_flush,
        .lock        = lcd_display_lock,
        .unlock      = lcd_display_unlock,
        .get_framebuffer = lcd_display_get_framebuffer,
        .mark_dirty  = lcd_display_mark_dirty,
    };

    gfx_ui_ctx_t *ui = gfx_ui_create(&lcd_hal, lcd);
    if (!ui) {
        lcd12864_deinit(lcd);
        return;
    }

    // ---------- UART + LoRa 实例化 ----------
    uart_bus_config_t uart_cfg = {
        .uart_num  = LORA_UART_NUM,
        .txd_pin   = LORA_TX_PIN,
        .rxd_pin   = LORA_RX_PIN,
        .baud_rate = LORA_BAUD,
    };
    uart_bus_handle_t bus = uart_bus_init(&uart_cfg);
    if (bus < 0) {
        gfx_ui_lock(ui);
        gfx_ui_clear_rect(ui, 0, 0, 128, 64);
        gfx_ui_draw_string(ui, 0, 0, "UART FAIL", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, true);
        gfx_ui_flush(ui);
        gfx_ui_unlock(ui);
        gfx_ui_destroy(ui);
        lcd12864_deinit(lcd);
        return;
    }

    lora_config_t lora_cfg = {
        .aux_pin = LORA_AUX_PIN,
        .uart    = uart_cfg,
        .drssi   = false,
    };
    lora_ctx_t *lora = lora_create(&lora_cfg, bus);

    gfx_ui_lock(ui);
    gfx_ui_clear_rect(ui, 0, 0, 128, 64);
    if (lora) {
        gfx_ui_draw_string(ui, 0, 0, "LoRa TX GPRMC", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, true);
        gfx_ui_draw_string(ui, 0, 2, "Every 1s", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, true);
    } else {
        gfx_ui_draw_string(ui, 0, 0, "LoRa Init FAIL", &gfx_font_5x8_spleen, GFX_COLOR_WHITE, true);
    }
    gfx_ui_flush(ui);
    gfx_ui_unlock(ui);

    if (!lora) {
        gfx_ui_destroy(ui);
        lcd12864_deinit(lcd);
        return;
    }

    // ---------- 周期发送 ----------
    const uint8_t *payload = (const uint8_t *)GPS_GPRMC_SENTENCE;
    uint8_t payload_len = (uint8_t)strlen(GPS_GPRMC_SENTENCE);

    while (1) {
        (void)lora_send(lora, payload, payload_len);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
