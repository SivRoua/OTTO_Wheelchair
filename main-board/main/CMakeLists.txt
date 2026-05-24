idf_component_register(SRCS "my_new_project.c"
                    INCLUDE_DIRS "."
                    REQUIRES lcd12864 uart_bus LoRa 
)
