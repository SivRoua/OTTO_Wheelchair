#ifndef UART_BUS_H
#define UART_BUS_H

/*
 * uart_bus — 公共 UART 抽象层
 * ================================================================
 * 为 LoRa、GPS 等多个串口设备提供统一的 UART 初始化和读写接口。
 * 每个设备调用 uart_bus_init 获得一个 handle，后续收发都通过这个 handle。
 *
 * 这样做的好处：
 * 1. UART 初始化和中断处理只写一次，所有模块共享
 * 2. 底层接口变更（换平台/换波特率）只改 uart_bus 一个地方
 * 3. 避免每个串口模块各自重复 uart_driver_install
 */

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * UART 编号常量
 *
 * 替代 ESP-IDF 的 UART_NUM_0 / UART_NUM_1 / UART_NUM_2 宏。
 * 定义在这里而不直接引用 driver/uart.h 的原因是：
 *   uart_bus.h 是对外接口，不应依赖 ESP-IDF 特定头文件，
 *   否则换平台时 include/ 目录下的文件也要跟着改。
 */
#define UART_BUS_NUM_0          0
#define UART_BUS_NUM_1          1
#define UART_BUS_NUM_2          2

/* 不透明句柄（本质就是 UART 编号，取值为 0/1/2） */
typedef int uart_bus_handle_t;

/* UART 配置结构体 */
typedef struct {
    int uart_num;          /* UART 编号（使用 UART_BUS_NUM_0/1/2） */
    int txd_pin;           /* TX 引脚 */
    int rxd_pin;           /* RX 引脚 */
    uint32_t baud_rate;    /* 波特率 */
} uart_bus_config_t;

/* 初始化 UART，返回 handle */
uart_bus_handle_t uart_bus_init(const uart_bus_config_t *cfg);

/* 发送数据 */
void uart_bus_write(uart_bus_handle_t bus, const uint8_t *data, uint32_t len);

/* 读取数据（非阻塞），返回实际读取的字节数 */
int uart_bus_read(uart_bus_handle_t bus, uint8_t *buf, uint32_t max_len);

/* 清空接收缓冲区 */
void uart_bus_flush(uart_bus_handle_t bus);

/* 注销 UART */
void uart_bus_deinit(uart_bus_handle_t bus);

#ifdef __cplusplus
}
#endif

#endif /* UART_BUS_H */