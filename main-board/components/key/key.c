#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "key.h"

#define KEY_QUEUE_DEPTH  8

/* ---------- 内部类型 ---------- */
typedef struct {
    key_dir_t   dir;
    key_press_t press;
} key_cb_data_t;

/* ---------- 模块静态状态 ---------- */
static QueueHandle_t s_key_queue = NULL;

/*
 * 每个按键 × 3 种按压类型 = 15 个回调数据槽。
 * 必须是静态存储，因为 iot_button 持有指向 usr_data 的指针。
 */
static key_cb_data_t s_cb_data[KEY_DIR_MAX][3];

/* ---------- 统一回调：写入队列 ---------- */
static void key_event_cb(void *btn_handle, void *usr_data) {
    (void)btn_handle;
    key_cb_data_t *d = (key_cb_data_t *)usr_data;
    key_event_t ev = { .dir = d->dir, .press = d->press };
    /* esp_timer 回调运行在专用任务中，使用普通 xQueueSend；
     * timeout=0：队列满时直接丢弃，不阻塞定时器任务 */
    xQueueSend(s_key_queue, &ev, 0);
}

/* ---------- 初始化单个按键的三种事件 ---------- */
static esp_err_t register_button(key_dir_t dir, int gpio_num,
                                 const button_config_t *btn_cfg) {
    button_gpio_config_t gpio_cfg = {
        .gpio_num     = gpio_num,
        .active_level = 0,   /* 按下为低电平 */
    };

    button_handle_t handle = NULL;
    esp_err_t ret = iot_button_new_gpio_device(btn_cfg, &gpio_cfg, &handle);
    if (ret != ESP_OK) return ret;

    /* 短按：按键抬起时立即触发，无双击消歧等待 */
    s_cb_data[dir][KEY_PRESS_SHORT] = (key_cb_data_t){ dir, KEY_PRESS_SHORT };
    iot_button_register_cb(handle, BUTTON_PRESS_UP, NULL,
                           key_event_cb, &s_cb_data[dir][KEY_PRESS_SHORT]);

    /* 长按开始：持续超过 long_press_time 触发一次 */
    s_cb_data[dir][KEY_PRESS_LONG] = (key_cb_data_t){ dir, KEY_PRESS_LONG };
    iot_button_register_cb(handle, BUTTON_LONG_PRESS_START, NULL,
                           key_event_cb, &s_cb_data[dir][KEY_PRESS_LONG]);

    /* 长按持续：按住期间以 LONG_PRESS_HOLD_SERIAL_TIME_MS 为间隔周期触发
     * UP/DOWN 用于列表滚动，其他方向用于电机持续控制 */
    s_cb_data[dir][KEY_PRESS_HOLD] = (key_cb_data_t){ dir, KEY_PRESS_HOLD };
    iot_button_register_cb(handle, BUTTON_LONG_PRESS_HOLD, NULL,
                           key_event_cb, &s_cb_data[dir][KEY_PRESS_HOLD]);

    return ESP_OK;
}

/* ---------- 公开 API ---------- */

esp_err_t key_init(const key_config_t *config) {
    if (!config) return ESP_ERR_INVALID_ARG;

    s_key_queue = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(key_event_t));
    if (!s_key_queue) return ESP_ERR_NO_MEM;

    /* 时间参数置 0，让 iot_button 使用 Kconfig 中的默认值
     * (CONFIG_BUTTON_SHORT_PRESS_TIME_MS / CONFIG_BUTTON_LONG_PRESS_TIME_MS) */
    button_config_t btn_cfg = {
        .short_press_time = 0,
        .long_press_time  = 0,
    };

    const int gpios[KEY_DIR_MAX] = {
        config->gpio_up, config->gpio_down, config->gpio_left,
        config->gpio_right, config->gpio_center,
    };

    for (int i = 0; i < KEY_DIR_MAX; i++) {
        esp_err_t ret = register_button((key_dir_t)i, gpios[i], &btn_cfg);
        if (ret != ESP_OK) return ret;
    }

    return ESP_OK;
}

bool key_read(key_event_t *out) {
    return key_read_timeout(out, 0);
}

bool key_read_timeout(key_event_t *out, TickType_t ticks_to_wait) {
    if (!s_key_queue || !out) return false;
    return xQueueReceive(s_key_queue, out, ticks_to_wait) == pdTRUE;
}
