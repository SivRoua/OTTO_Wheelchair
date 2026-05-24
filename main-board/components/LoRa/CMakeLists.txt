idf_component_register(SRCS "LoRa.c" "port/esp_idf_gpio.c"
                    INCLUDE_DIRS "include" "port"
                    REQUIRES uart_bus driver esp_driver_gpio
)
