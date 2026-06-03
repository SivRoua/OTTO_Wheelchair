#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"

#include "lcd12864.h"
#include "lcd_hal.h"
#include "gfx_menu.h"
#include "motion.h"
#include "key.h"
#include "buzzer.h"
#include "ld2402.h"
#include "gps.h"
#include "uart_bus.h"

#include "app_ctx.h"
#include "page_main.h"
#include "page_menu.h"
#include "page_sub.h"

static const char *TAG = "main";

/* ----------------------------------------------------------------
 * 共享句柄定义（声明在 app_ctx.h）
 * ---------------------------------------------------------------- */
gfx_menu_engine_t *s_menu      = NULL;
lora_ctx_t        *s_lora      = NULL;
gps_ctx_t         *s_gps       = NULL;
bool               s_lora_ok   = false;
uint8_t            s_time_hour = 0xFF; /* 0xFF = 未同步，显示 "--:--" */
uint8_t            s_time_min  = 0;
bool               s_gps_located = false;

/* ----------------------------------------------------------------
 * Boot Page 配置
 * ---------------------------------------------------------------- */
static gfx_boot_cfg_t s_boot_cfg = {
    .logo         = NULL,
    .logo_w       = 0,
    .logo_h       = 0,
    .title        = "OTTO Wheelchair",
    .subtitle     = "Main Board v1.0",
    .duration_ms  = 1500,
    .next_page_id = PAGE_MAIN,
};

/* ----------------------------------------------------------------
 * 按键路由
 * ---------------------------------------------------------------- */
static void dispatch_key(const key_event_t *ev)
{
    uint8_t page = gfx_menu_current_page(s_menu);

    if (page == PAGE_MAIN) {
        if (ev->press == KEY_PRESS_DOWN) {
            /* 方向键按下：发运动指令 */
            switch (ev->dir) {
            case KEY_DIR_UP:
                motion_prepare(); motion_left(MOTION_F); motion_right(MOTION_F);
                motion_send(); break;
            case KEY_DIR_DOWN:
                motion_prepare(); motion_left(MOTION_B); motion_right(MOTION_B);
                motion_send(); break;
            case KEY_DIR_LEFT:
                motion_prepare(); motion_left(MOTION_B); motion_right(MOTION_F);
                motion_send(); break;
            case KEY_DIR_RIGHT:
                motion_prepare(); motion_left(MOTION_F); motion_right(MOTION_B);
                motion_send(); break;
            default: break;
            }
        } else if (ev->press == KEY_PRESS_SHORT) {
            if (ev->dir == KEY_DIR_CENTER) {
                /* 中键短按（完整按下+松开）：进菜单 */
                gfx_menu_push(s_menu, PAGE_MENU);
            } else {
                /* 方向键松开：停止 */
                motion_prepare();
                motion_send();
            }
        }
        return;
    }

    if (page == PAGE_BACKREST) {
        if (ev->press == KEY_PRESS_DOWN) {
            if (ev->dir == KEY_DIR_UP) {
                motion_prepare(); motion_stepper(MOTION_F); motion_send();
            } else if (ev->dir == KEY_DIR_DOWN) {
                motion_prepare(); motion_stepper(MOTION_B); motion_send();
            }
        } else if (ev->press == KEY_PRESS_SHORT) {
            if (ev->dir == KEY_DIR_UP || ev->dir == KEY_DIR_DOWN) {
                motion_prepare(); motion_send();
            } else if (ev->dir == KEY_DIR_LEFT) {
                gfx_menu_pop(s_menu);
            }
        }
        return;
    }

    /* 其他页面：只响应 SHORT，上下选中，右键进入，左键返回，屏蔽中键 */
    if (ev->press != KEY_PRESS_SHORT) return;
    switch (ev->dir) {
    case KEY_DIR_UP:    gfx_menu_input(s_menu, GFX_INPUT_UP);   break;
    case KEY_DIR_DOWN:  gfx_menu_input(s_menu, GFX_INPUT_DOWN); break;
    case KEY_DIR_RIGHT: gfx_menu_input(s_menu, GFX_INPUT_ENTER); break;
    case KEY_DIR_LEFT:  gfx_menu_input(s_menu, GFX_INPUT_BACK);  break;
    default: break; /* CENTER 屏蔽 */
    }
}

/* ----------------------------------------------------------------
 * app_main
 * ---------------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "=== OTTO Wheelchair booting ===");

    /* LCD */
    lcd12864_config_t lcd_cfg = {
        .sclk    = CONFIG_LCD12864_PIN_SCLK,
        .sda     = CONFIG_LCD12864_PIN_SDA,
        .rs      = CONFIG_LCD12864_PIN_RS,
        .cs      = CONFIG_LCD12864_PIN_CS,
        .reset   = CONFIG_LCD12864_PIN_RESET,
        .freq_hz = CONFIG_LCD12864_SPI_CLOCK_HZ,
    };
    lcd12864_ctx_t *lcd = lcd12864_create(&lcd_cfg);
    if (!lcd) { ESP_LOGE(TAG, "lcd12864_create failed"); return; }

    gfx_ui_ctx_t *ui = lcd_hal_create(lcd);
    if (!ui)  { ESP_LOGE(TAG, "lcd_hal_create failed"); return; }

    /* Menu Engine */
    s_menu = gfx_menu_create(ui);
    if (!s_menu) { ESP_LOGE(TAG, "gfx_menu_create failed"); return; }

    /* 注册页面 */
    gfx_page_desc_t boot_desc;
    gfx_page_boot_init(&boot_desc, &s_boot_cfg);
    gfx_menu_register_page(s_menu, PAGE_BOOT, &boot_desc);

    page_main_register(s_menu);
    page_menu_register(s_menu);
    page_backrest_register(s_menu);
    page_pressure_register(s_menu);
    page_lora_cfg_register(s_menu);

    /* Motion */
    if (motion_init(CONFIG_MOTION_I2C_PORT,
                    CONFIG_MOTION_SDA_PIN,
                    CONFIG_MOTION_SCL_PIN) != ESP_OK) {
        ESP_LOGE(TAG, "motion_init failed"); return;
    }
    motion_prepare();
    motion_send();

    /* Buzzer */
    buzzer_init(CONFIG_BUZZER_GPIO_PIN);

    /* LD2402 */
    ld2402_io_init(-1);

    /* GPS */
    uart_bus_config_t gps_uart_cfg = {
        .uart_num  = CONFIG_GPS_UART_NUM,
        .txd_pin   = CONFIG_GPS_TX_PIN,
        .rxd_pin   = CONFIG_GPS_RX_PIN,
        .baud_rate = CONFIG_GPS_BAUD,
    };
    uart_bus_handle_t gps_bus = uart_bus_init(&gps_uart_cfg);
    gps_config_t gps_cfg = { .uart = gps_uart_cfg };
    s_gps = gps_create(&gps_cfg, gps_bus);
    if (!s_gps) ESP_LOGW(TAG, "gps_create failed, GPS disabled");

    /* LoRa */
    uart_bus_config_t lora_uart_cfg = {
        .uart_num  = CONFIG_LORA_UART_NUM,
        .txd_pin   = CONFIG_LORA_TX_PIN,
        .rxd_pin   = CONFIG_LORA_RX_PIN,
        .baud_rate = CONFIG_LORA_BAUD,
    };
    uart_bus_handle_t lora_bus = uart_bus_init(&lora_uart_cfg);
    lora_config_t lora_cfg = {
        .aux_pin = CONFIG_LORA_AUX_PIN,
        .uart    = lora_uart_cfg,
        .drssi   = false,
    };
    s_lora = lora_create(&lora_cfg, lora_bus);
    s_lora_ok = (s_lora != NULL);
    if (!s_lora_ok) ESP_LOGW(TAG, "lora_create failed, LoRa disabled");
    key_config_t key_cfg = {
        .gpio_up     = CONFIG_BUTTON_GPIO_UP,
        .gpio_down   = CONFIG_BUTTON_GPIO_DOWN,
        .gpio_left   = CONFIG_BUTTON_GPIO_LEFT,
        .gpio_right  = CONFIG_BUTTON_GPIO_RIGHT,
        .gpio_center = CONFIG_BUTTON_GPIO_CENTER,
    };
    if (key_init(&key_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "key_init failed"); return;
    }

    /* 启动 */
    gfx_menu_push(s_menu, PAGE_BOOT);
    ESP_LOGI(TAG, "boot complete");

    /* 主循环 */
    key_event_t ev;
    gps_data_t  gps_data;
    while (1) {
        gfx_menu_tick(s_menu);

        /* GPS 轮询：有新帧就尝试同步时间 */
        if (s_gps && gps_read(s_gps, &gps_data) == ESP_OK) {
            if (gps_is_located(&gps_data)) {
                s_time_hour   = gps_data.hour;
                s_time_min    = gps_data.minute;
                s_gps_located = true;
            }
        }

        if (key_read(&ev)) {
            dispatch_key(&ev);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
