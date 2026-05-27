idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES lcd12864 gfx_ui_engine gfx_anim_engine gfx_menu_engine uart_bus LoRa motion
)
