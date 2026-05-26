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

void app_main(void)
{
    // ---------- LCD ----------
    lcd12864_config_t lcd_cfg = {
        .sclk = PIN_SCLK, .sda = PIN_SDA, .rs = PIN_RS,
        .cs = PIN_CS, .reset = PIN_RESET, .freq_hz = SPI_FREQ,
    };
    lcd12864_ctx_t *lcd = (lcd12864_ctx_t *)lcd12864_create(&lcd_cfg);
    if (!lcd) return;

    // ---------- UART + LoRa 实例化 ----------
    uart_bus_config_t uart_cfg = {
        .uart_num  = LORA_UART_NUM,
        .txd_pin   = LORA_TX_PIN,
        .rxd_pin   = LORA_RX_PIN,
        .baud_rate = LORA_BAUD,
    };
    uart_bus_handle_t bus = uart_bus_init(&uart_cfg);
    if (bus < 0) {
        lcd12864_puts(lcd, 0, 0, "UART FAIL");
        lcd12864_flush(lcd);
        return;
    }

    lora_config_t lora_cfg = {
        .aux_pin = LORA_AUX_PIN,
        .uart    = uart_cfg,
        .drssi   = false,
    };
    lora_ctx_t *lora = lora_create(&lora_cfg, bus);

    lcd12864_lock(lcd);
    lcd12864_clear(lcd);
    if (lora) {
        lcd12864_puts(lcd, 0, 0, "LoRa TX GPRMC");
        lcd12864_puts(lcd, 0, 2, "Every 1s");
    } else {
        lcd12864_puts(lcd, 0, 0, "LoRa Init FAIL");
    }
    lcd12864_flush(lcd);
    lcd12864_unlock(lcd);

    if (!lora) {
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
