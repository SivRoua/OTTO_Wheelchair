/*
 * motion — 电机控制组件核心实现
 * ================================================================
 * 维护一个待发帧的 flag 字节，prepare 置零。
 * 每个 set 函数修改对应位（10=F, 01=B, 00=S）。
 * send 构建完整 6 字节帧并通过 I2C master 发出。
 *
 * 帧格式（与 sub-board 协议一致）：
 *   [S][T][flag][checksum][E][D]
 */

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "motion.h"

#define SLAVE_ADDR          0x42
#define I2C_FREQ_HZ         100000

static const char *TAG = "motion";

static i2c_master_bus_handle_t  bus_handle;
static i2c_master_dev_handle_t  dev_handle;
static bool initialized = false;

/* 待发帧的 flag 字节：bits [7:6]=stepper, [5:4]=left, [3:2]=right, [1:0]=0 */
static uint8_t pending_flag;

/* ================================================================
 * 内部辅助 — 方向字符 → 位编码
 * ================================================================ */
static uint8_t encode_dir(char dir)
{
    switch (dir) {
    case 'F': return 0b10;
    case 'B': return 0b01;
    default:  return 0b00; /* 'S' or anything else = stop */
    }
}

/* ================================================================
 * motion_init — 初始化 I2C master
 * ================================================================ */
esp_err_t motion_init(int port, int sda_pin, int scl_pin)
{
    if (initialized) return ESP_OK;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = port,
        .sda_io_num        = sda_pin,
        .scl_io_num        = scl_pin,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority     = 0,
        .trans_queue_depth = 4,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %d", ret);
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SLAVE_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %d", ret);
        i2c_del_master_bus(bus_handle);
        return ret;
    }

    initialized = true;
    pending_flag = 0;
    ESP_LOGI(TAG, "I2C%d master ready, SDA=%d SCL=%d, slave 0x%02X",
             port, sda_pin, scl_pin, SLAVE_ADDR);
    return ESP_OK;
}

/* ================================================================
 * motion_prepare — 清空帧缓冲区
 * ================================================================ */
void motion_prepare(void)
{
    pending_flag = 0;
}

/* ================================================================
 * motion_left / motion_right / motion_stepper — 设置方向
 * ================================================================ */

void motion_left(char dir)
{
    pending_flag &= ~(0b11 << 4);
    pending_flag |= (encode_dir(dir) << 4);
}

void motion_right(char dir)
{
    pending_flag &= ~(0b11 << 2);
    pending_flag |= (encode_dir(dir) << 2);
}

void motion_stepper(char dir)
{
    pending_flag &= ~(0b11 << 6);
    pending_flag |= (encode_dir(dir) << 6);
}

/* ================================================================
 * motion_send — 构建帧 + I2C 发送
 * ================================================================ */
esp_err_t motion_send(void)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;

    uint8_t frame[6];
    frame[0] = 'S';
    frame[1] = 'T';
    frame[2] = pending_flag;
    frame[3] = 'S' ^ 'T' ^ pending_flag;
    frame[4] = 'E';
    frame[5] = 'D';

    return i2c_master_transmit(dev_handle, frame, sizeof(frame), 100);
}
