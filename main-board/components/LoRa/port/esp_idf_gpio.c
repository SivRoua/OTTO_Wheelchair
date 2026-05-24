/*
 * ================================================================
 * ESP-IDF 平台的 LoRa (LLCC68) AUX 端口实现
 * ================================================================
 * 本文件实现了 lora_port_ops_t 中定义的两个硬件操作：
 * 读 AUX 引脚和毫秒延时。
 *
 * 比 lcd12864/port/esp_idf_spi.c 简单得多，因为：
 *   - 没有 SPI 总线操作
 *   - 没有中断注册
 *   - 只有 1 个 GPIO（AUX，输入模式）
 *
 * 移植到其他 MCU 时，替换此文件即可。
 */

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lora_port.h"

/* ================================================================
 * 模块内全局变量
 * ================================================================
 * static int aux_pin：保存 AUX 引脚号，供 gpio_read_aux 使用。
 * 放在文件作用域并加 static 限制内部链接，其他 .c 无法访问。
 */
static int aux_pin = -1;

/* ================================================================
 * 公开接口实现（符合 lora_port_ops_t 定义）
 * ================================================================ */

/**
 * @brief 初始化 AUX 引脚为 GPIO 输入模式
 *
 * LLCC68 模块的 AUX 引脚由模块自身驱动，MCU 只需读取。
 * 所以配置为纯输入模式，不上拉也不下拉（模块内部有驱动）。
 *
 * @param pin  AUX 引脚号
 */
static void esp_port_init(int pin)
{
    aux_pin = pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

/**
 * @brief 读取 AUX 引脚电平
 *
 * @return true  = 高电平（模块忙）
 *         false = 低电平（模块空闲）
 *
 * 参考 README.md：
 *   高电平（忙）：发送中、接收中、模式切换中
 *   低电平（空闲）：动作已完成
 */
static bool esp_port_read_aux(void)
{
    if (aux_pin < 0) {
        return false;
    }
    return gpio_get_level(aux_pin) == 1;
}

/**
 * @brief 毫秒级阻塞延时
 *
 * 使用 FreeRTOS vTaskDelay 让出 CPU。
 * 如果 ms=0，立即返回（不做 taskYIELD，因为没有任何等待的必要）。
 */
static void esp_port_delay_ms(uint32_t ms)
{
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

/* ================================================================
 * 平台操作表实例
 * ================================================================ */
const lora_port_ops_t lora_port_ops = {
    .init          = esp_port_init,
    .gpio_read_aux = esp_port_read_aux,
    .delay_ms      = esp_port_delay_ms,
};
