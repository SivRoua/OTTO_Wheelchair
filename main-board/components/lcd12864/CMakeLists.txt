idf_component_register(SRCS "lcd12864.c" "port/esp_idf_spi.c"
                    INCLUDE_DIRS "include" "port"
                    REQUIRES driver esp_driver_spi esp_driver_gpio
)
