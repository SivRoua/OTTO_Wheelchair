idf_component_register(
    SRCS
        "main.c"
        "src/lcd_hal.c"
        "src/page_main.c"
        "src/page_menu.c"
        "src/page_sub.c"
    INCLUDE_DIRS
        "."
        "include"
    REQUIRES
        lcd12864 gfx_ui_engine gfx_anim_engine gfx_menu_engine
        uart_bus LoRa motion key buzzer ld2402 gps
)
