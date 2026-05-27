#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 硬件配置 ---------- */
typedef struct {
    int gpio_up;
    int gpio_down;
    int gpio_left;
    int gpio_right;
    int gpio_center;
} key_config_t;

/* ---------- 方向 ---------- */
typedef enum {
    KEY_DIR_UP = 0,
    KEY_DIR_DOWN,
    KEY_DIR_LEFT,
    KEY_DIR_RIGHT,
    KEY_DIR_CENTER,
    KEY_DIR_MAX,
} key_dir_t;

/* ---------- 按压类型 ---------- */
typedef enum {
    KEY_PRESS_SHORT = 0,  /* 单击：按下后抬起，时长 < long_press_time */
    KEY_PRESS_LONG,       /* 长按开始：持续超过阈值时触发一次 */
    KEY_PRESS_HOLD,       /* 长按持续：按住期间以固定间隔周期性触发 */
} key_press_t;

/* ---------- 事件 ---------- */
typedef struct {
    key_dir_t   dir;
    key_press_t press;
} key_event_t;

/* ---------- API ---------- */

/**
 * @brief 初始化五向按键模块
 * @param config NULL 则使用 Kconfig 默认引脚，否则使用传入配置
 * @return ESP_OK 成功
 */
esp_err_t key_init(const key_config_t *config);

/**
 * @brief 非阻塞读取一个按键事件
 * @param out 输出事件
 * @return true 读到事件，false 队列为空
 */
bool key_read(key_event_t *out);

/**
 * @brief 带超时读取一个按键事件
 * @param out           输出事件
 * @param ticks_to_wait 0 = 非阻塞，portMAX_DELAY = 永久等待
 * @return true 读到事件，false 超时
 */
bool key_read_timeout(key_event_t *out, TickType_t ticks_to_wait);

#ifdef __cplusplus
}
#endif
