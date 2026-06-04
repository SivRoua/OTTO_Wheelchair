#include "ld2402.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ld2402";
static gpio_num_t s_io_pin = GPIO_NUM_NC;

esp_err_t ld2402_io_init(int io_pin)
{
    s_io_pin = (io_pin < 0) ? CONFIG_LD2402_IO_PIN : (gpio_num_t)io_pin;

    gpio_config_t cfg = {
        .pin_bit_mask  = (1ULL << s_io_pin),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "GPIO%d ready (radar IO)", s_io_pin);
    } else {
        ESP_LOGE(TAG, "gpio_config GPIO%d failed: %s", s_io_pin, esp_err_to_name(ret));
    }
    return ret;
}

bool ld2402_io_is_person_detected(void)
{
    return gpio_get_level(s_io_pin) == 1;
}
