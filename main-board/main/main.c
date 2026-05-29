/*
 * main.c — OTTO Wheelchair 主板
 *
 * 五向按键 → motion 指令 → I2C → 下位机电机
 * 按下方向键：发送对应运动指令
 * 松开任意键：发送停止指令
 * 中键按下：停止
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "motion.h"
#include "key.h"

static const char *TAG = "main";

static const char *dir_str(key_dir_t d)
{
    switch (d) {
    case KEY_DIR_UP:     return "UP";
    case KEY_DIR_DOWN:   return "DOWN";
    case KEY_DIR_LEFT:   return "LEFT";
    case KEY_DIR_RIGHT:  return "RIGHT";
    case KEY_DIR_CENTER: return "CENTER";
    default:             return "NONE";
    }
}

static void send_stop(void)
{
    motion_prepare();
    esp_err_t err = motion_send();
    ESP_LOGI(TAG, "STOP    -> %s", err == ESP_OK ? "OK" : esp_err_to_name(err));
}

static void send_go(key_dir_t dir)
{
    motion_prepare();
    switch (dir) {
    case KEY_DIR_UP:
        motion_left(MOTION_F);
        motion_right(MOTION_F);
        break;
    case KEY_DIR_DOWN:
        motion_left(MOTION_B);
        motion_right(MOTION_B);
        break;
    case KEY_DIR_LEFT:
        motion_left(MOTION_B);
        motion_right(MOTION_F);
        break;
    case KEY_DIR_RIGHT:
        motion_left(MOTION_F);
        motion_right(MOTION_B);
        break;
    default:
        send_stop();
        return;
    }
    esp_err_t err = motion_send();
    ESP_LOGI(TAG, "GO %-5s -> %s", dir_str(dir), err == ESP_OK ? "OK" : esp_err_to_name(err));
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== OTTO Wheelchair Main Board ===");

    esp_err_t err = motion_init(CONFIG_MOTION_I2C_PORT,
                                CONFIG_MOTION_SDA_PIN,
                                CONFIG_MOTION_SCL_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "motion_init failed: %s", esp_err_to_name(err));
        return;
    }

    send_stop();

    key_config_t key_cfg = {
        .gpio_up     = CONFIG_BUTTON_GPIO_UP,
        .gpio_down   = CONFIG_BUTTON_GPIO_DOWN,
        .gpio_left   = CONFIG_BUTTON_GPIO_LEFT,
        .gpio_right  = CONFIG_BUTTON_GPIO_RIGHT,
        .gpio_center = CONFIG_BUTTON_GPIO_CENTER,
    };
    err = key_init(&key_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "key_init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "ready — press to move, release to stop");

    key_event_t ev;
    while (1) {
        if (!key_read_timeout(&ev, portMAX_DELAY)) {
            continue;
        }

        if (ev.press == KEY_PRESS_DOWN) {
            /* 按下：中键停止，方向键运动 */
            if (ev.dir == KEY_DIR_CENTER) {
                send_stop();
            } else {
                send_go(ev.dir);
            }
        } else if (ev.press == KEY_PRESS_SHORT) {
            /* 抬起（SHORT = BUTTON_PRESS_UP）：停止 */
            send_stop();
        }
        /* LONG / HOLD 不处理 */
    }
}
