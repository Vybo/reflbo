idf_component_register(SRCS "main.c" "app.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES board diag display esp_app_format esp_driver_gpio espcoredump gfx nvs_flash power rtc
                                     scheduler sensors timekeeping ui util)
