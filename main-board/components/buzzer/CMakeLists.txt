idf_component_register(
    SRCS "buzzer.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio freertos
)
