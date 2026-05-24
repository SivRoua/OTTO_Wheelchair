#ifndef LORA_PORT_H
#define LORA_PORT_H

/*
 * LoRa 端口抽象层（Port Abstraction Layer）
 * ================================================================
 * LoRa 透传模块（LLCC68）不同于 lcd12864：
 *   - 不需要 SPI 读写（UART 交给 uart_bus）
 *   - 不需要 M0/M1 引脚输出（AT+SWITCH=0 时引脚不参与）
 *   - 唯一需要的引脚是 AUX（输入，检测模块忙闲状态）
 *
 * 所以端口层比 lcd12864 的端口层薄很多，只有 3 个函数：
 *   init          — 初始化 AUX 引脚（配置为 GPIO 输入）
 *   gpio_read_aux — 读取 AUX 电平（true=忙，false=空闲）
 *   delay_ms      — 毫秒级延时
 *
 * 对比 lcd12864_port_ops_t：
 *   多了 init 用于配置引脚
 *   少了 write_cmd / write_data / write_data_bulk 等 SPI 操作
 *   少了 attach_interrupt（AUX 中断在后续阶段用中断接收时再加）
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 平台操作表（抽象接口）
 * ================================================================ */
typedef struct {
    /*
     * 初始化 AUX 引脚。
     * 参数 pin：AUX 引脚号。
     * 内部应配置为 GPIO 输入、无上下拉（模块自身驱动 AUX 引脚）。
     */
    void (*init)(int aux_pin);

    /*
     * 读取 AUX 引脚电平。
     * 返回 true  = 模块忙（正在发送/接收/切换模式）
     * 返回 false = 模块空闲（可以发数据或已处理完成）
     *
     * 参考 README.md 中"AUX 电平"节：
     *   高电平 = 数据发送中 / 数据接收中 / 工作模式切换中（忙）
     *   低电平 = 数据发送完成 / 数据接收完成 / 模式切换完成（空闲）
     *
     * 注意：文档指出模块检测到接收数据时，
     *       AUX 会提前 2-3ms 输出高电平提示 MCU。
     *       所以出现了"AUX=高"不一定是发送导致的忙，
     *       也可能是"有数据来了，准备接收"的信号。
     *       驱动核心在判断忙闲时需要结合上下文。
     */
    bool (*gpio_read_aux)(void);

    /*
     * 毫秒级阻塞延时。
     * 参数 ms：延时时长（毫秒），ms=0 时立即返回。
     * 实现层应调用 vTaskDelay，保证非忙等。
     */
    void (*delay_ms)(uint32_t ms);
} lora_port_ops_t;

/*
 * 全局操作表实例声明。
 * 在 port/esp_idf_gpio.c 中定义并填充函数指针。
 * 核心驱动通过 extern 引用此实例。
 */
extern const lora_port_ops_t lora_port_ops;

#ifdef __cplusplus
}
#endif

#endif /* LORA_PORT_H */
