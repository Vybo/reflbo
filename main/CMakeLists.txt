idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_menu.c" "app_cmds.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES board console datastore diag display esp_app_format esp_driver_gpio esp_timer
                                     espcoredump gfx locale nvs_flash power rtc scheduler sensors st7305 storage
                                     timekeeping ui util)
