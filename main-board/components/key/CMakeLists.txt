idf_component_register(
    SRCS "key.c" "port/iot_button.c" "port/button_gpio.c"
    INCLUDE_DIRS "include" "port/include"
    REQUIRES esp_driver_gpio esp_timer
)