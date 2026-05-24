idf_component_register(
    SRCS "gps.c"
    INCLUDE_DIRS "include"
    REQUIRES uart_bus driver esp_driver_gpio freertos
)
