#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "buzzer.h"

static int buzzer_pin = -1;

void buzzer_init(int pin)
{
    buzzer_pin = pin;
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, 1);
}

void buzzer_on(void)
{
    if (buzzer_pin >= 0) gpio_set_level(buzzer_pin, 0);
}

void buzzer_off(void)
{
    if (buzzer_pin >= 0) gpio_set_level(buzzer_pin, 1);
}

static void beep_task(void *arg)
{
    uint32_t duration_ms = (uint32_t)(uintptr_t)arg;
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_off();
    vTaskDelete(NULL);
}

void buzzer_beep(uint32_t duration_ms)
{
    xTaskCreate(beep_task, "buzzer_beep", 512, (void *)(uintptr_t)duration_ms, 5, NULL);
}

