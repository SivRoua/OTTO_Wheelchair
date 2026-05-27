/*
 * motion — 电机控制组件核心实现
 * ================================================================
 * 参考 sub-board motor.rs：struct MotorCommand → Dir 枚举 → apply。
 *
 * 发送逻辑：prepare → set → send
 *   1. motion_prepare()  将内部 MotorCommand 三电机均置为 Stop
 *   2. motion_set(dir)   设置对应电机方向
 *   3. motion_send()     将 MotorCommand 编码为 flag 字节，
 *                        组装 6 字节帧 [S][T][flag][cksum][E][D] 经 I2C 发出
 *
 * 帧格式（与 sub-board protocol.rs 一致）：
 *   [S][T][flag][checksum][E][D]
 *     flag 字节 bits [7:6]=stepper, [5:4]=left, [3:2]=right, [1:0]=0
 *     10=Forward, 01=Backward, 00=Stop
 *     checksum = 'S' ^ 'T' ^ flag（sub-board 解码校验用）
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

/* 当前待发送的电机命令（prepare → set → send） */
static motor_command_t cmd;

/* ================================================================
 * 字符方向 → dir_t 枚举转换
 * ================================================================ */
static dir_t char_to_dir(char c)
{
    switch (c) {
    case 'F': return DIR_FORWARD;
    case 'B': return DIR_BACKWARD;
    default:  return DIR_STOP;
    }
}

/* ================================================================
 * dir_t 枚举 → 2-bit 编码（与 sub-board 编码一致）
 * ================================================================ */
static uint8_t encode_dir(dir_t d)
{
    switch (d) {
    case DIR_FORWARD:  return 0b10;
    case DIR_BACKWARD: return 0b01;
    default:           return 0b00; /* DIR_STOP */
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
    motion_prepare();
    ESP_LOGI(TAG, "I2C%d master ready, SDA=%d SCL=%d, slave 0x%02X",
             port, sda_pin, scl_pin, SLAVE_ADDR);
    return ESP_OK;
}

/* ================================================================
 * motion_prepare — 三电机默认 Stop
 * ================================================================ */
void motion_prepare(void)
{
    cmd.left    = DIR_STOP;
    cmd.right   = DIR_STOP;
    cmd.stepper = DIR_STOP;
}

/* ================================================================
 * motion_left / motion_right / motion_stepper
 * ================================================================ */
void motion_left(char dir)
{
    cmd.left = char_to_dir(dir);
}

void motion_right(char dir)
{
    cmd.right = char_to_dir(dir);
}

void motion_stepper(char dir)
{
    cmd.stepper = char_to_dir(dir);
}

/* ================================================================
 * motion_send — 编码 MotorCommand → flag → 6 字节帧 → I2C 发送
 * ================================================================ */
esp_err_t motion_send(void)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;

    uint8_t flag = (encode_dir(cmd.stepper) << 6)
                 | (encode_dir(cmd.left)    << 4)
                 | (encode_dir(cmd.right)   << 2);

    uint8_t frame[6];
    frame[0] = 'S';
    frame[1] = 'T';
    frame[2] = flag;
    frame[3] = 'S' ^ 'T' ^ flag;
    frame[4] = 'E';
    frame[5] = 'D';

    return i2c_master_transmit(dev_handle, frame, sizeof(frame), 100);
}
