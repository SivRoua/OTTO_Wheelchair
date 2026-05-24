/*
 * ================================================================
 * ESP‑IDF 平台的 LCD12864 (UC1701X) 端口实现
 * ================================================================
 * 本文件实现了 lcd12864_port_ops_t 中定义的各个硬件操作。
 * 只依赖于 ESP‑IDF 的驱动程序（SPI、GPIO）和 FreeRTOS。
 * 如需移植到其他 MCU，只需替换此文件。
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

/*
 * 显式包含 FreeRTOS 延时相关头文件。
 * 虽然其他 ESP‑IDF 头文件可能会间接引入，
 * 但显式包含可保证代码的可移植性和自明性。
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lcd12864_port.h"

static const char *TAG = "lcd12864_port";

/* ================================================================
 * 模块内全局变量（用 static 限制作用域，避免外部访问）
 * ================================================================ */
static spi_device_handle_t spi_handle; /* SPI 设备句柄 */
static int cs_pin, rs_pin, reset_pin; /* 保存实际使用的 GPIO 编号 */

/* ================================================================
 * 内部辅助函数：进行一次 8 位的 SPI 传输
 * ================================================================
 * 该函数仅负责将一字节数据通过 SPI 发送出去，不涉及
 * 片选和指令/数据选择，这些由上层 write_cmd / write_data 控制。
 */
static void spi_transfer_byte(uint8_t data) {
    /*
     * SPI 事务结构体。.length 表示传输位数，此处为 8 位。
     * .tx_buffer 指向发送缓冲区。若数据在函数栈上，必须确保
     * 事务完成前该地址有效。这里直接使用形参 data 的地址，
     * 因为 poll 模式会立即完成传输，无需担心生命周期。
     */
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &data,
    };
    spi_device_polling_transmit(spi_handle, &trans);
}

/* ================================================================
 * 公开接口实现（符合 lcd12864_port_ops_t 定义）
 * ================================================================ */

/**
 * @brief 初始化所有硬件资源
 *
 * 完成以下步骤：
 * 1. 初始化 SPI 总线（仅一次，其他设备可共享此总线）
 * 2. 挂载本屏幕为一个 SPI 设备（设置模式、频率、手动 CS）
 * 3. 配置 CS、RS、RST 为普通 GPIO 输出，并开启内部上拉
 * 4. 设置初始电平状态
 */
static void esp_port_init(const lcd12864_port_config_t *cfg) {
    /* 保存引脚号，供后续操作使用 */
    cs_pin    = cfg->cs;
    rs_pin    = cfg->rs;
    reset_pin = cfg->reset;

    /* ---------- 1. SPI 总线初始化 ---------- */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = cfg->sda,
        .sclk_io_num = cfg->sclk,
        .miso_io_num = -1,          /* 屏幕只写，不读 */
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    /* ---------- 2. 添加 SPI 设备 ---------- */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = cfg->freq_hz,
        .mode = 0,                    /* CPOL=0, CPHA=0，根据 UC1701X 要求 */
        .spics_io_num = -1,           /* 不使用硬件片选，手动控制 */
        .queue_size = 1,
        .pre_cb = NULL,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi_handle));

    /* ---------- 3. GPIO 初始化 ---------- */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << cs_pin) | (1ULL << rs_pin) | (1ULL << reset_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,   /* 启用内部上拉，确保空闲电平确定 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* ---------- 4. 设置默认电平 ---------- */
    gpio_set_level(cs_pin, 1);     /* 片选高：未选中 */
    gpio_set_level(rs_pin, 0);     /* RS 低：命令模式 */
    gpio_set_level(reset_pin, 1);  /* 复位高：正常运行 */
}

/**
 * @brief 控制硬件复位引脚
 * @param level  true: 高电平（正常）  false: 低电平（复位）
 */
static void esp_port_reset(bool level) {
    gpio_set_level(reset_pin, level ? 1 : 0);
}

/**
 * @brief FreeRTOS 毫秒级延时
 *        调用 vTaskDelay 会使当前任务进入阻塞态，
 *        从而让出 CPU 给其他任务。
 */
static void esp_port_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief 发送一个命令字节
 *        时序要求：RS=0（命令），CS 拉低使能，传输后 CS 拉高
 */
static void esp_port_write_cmd(uint8_t cmd) {
    gpio_set_level(cs_pin, 0);   /* 片选使能 */
    gpio_set_level(rs_pin, 0);   /* 命令模式 */
    spi_transfer_byte(cmd);
    gpio_set_level(cs_pin, 1);   /* 片选关闭 */
}

/**
 * @brief 发送一个数据字节
 *        时序要求：RS=1（数据），CS 拉低使能，传输后 CS 拉高
 */
static void esp_port_write_data(uint8_t data) {
    gpio_set_level(cs_pin, 0);
    gpio_set_level(rs_pin, 1);   /* 数据模式 */
    spi_transfer_byte(data);
    gpio_set_level(cs_pin, 1);
}

/**
 * @brief 批量发送数据字节（性能优化）
 *        整个缓冲区在一次 SPI 事务中完成，减少事务开销。
 *        要求在整个传输期间保持 CS 有效。
 */
static void esp_port_write_data_bulk(const uint8_t *data, uint32_t len) {
    if (len == 0) return;

    gpio_set_level(cs_pin, 0);
    gpio_set_level(rs_pin, 1);   /* 数据模式 */

    /*
     * 构造一次 SPI 事务，发送全部 len 字节。
     * .length 单位为位，因此需要乘以 8。
     * .tx_buffer 指向待发送的数据缓冲区。
     */
    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(spi_handle, &trans);

    gpio_set_level(cs_pin, 1);
}

/**
 * @brief 释放所有硬件资源
 *        顺序：移除设备 → 释放总线
 */
static void esp_port_deinit(void) {
    spi_bus_remove_device(spi_handle);
    spi_bus_free(SPI2_HOST);
    ESP_LOGI(TAG, "SPI bus deinitialized");
}

/* ================================================================
 * 平台操作表实例
 * ================================================================
 * 此处定义全局常量 lcd12864_port_ops，并使用 C99 指定的
 * 初始化方式（.成员名 = 函数名），将各个函数指针与上面的
 * 实现绑定。驱动核心通过 extern 声明引用该实例，实现解耦。
 */
const lcd12864_port_ops_t lcd12864_port_ops = {
    .init             = esp_port_init,
    .reset            = esp_port_reset,
    .delay_ms         = esp_port_delay_ms,
    .write_cmd        = esp_port_write_cmd,
    .write_data       = esp_port_write_data,
    .write_data_bulk  = esp_port_write_data_bulk,
    .deinit           = esp_port_deinit,
};