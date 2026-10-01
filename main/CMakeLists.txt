idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_menu.c" "app_cmds.c" "app_config.c" "app_web.c" "app_sync.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES app_update board console datastore diag display esp_app_format
                                     esp_driver_gpio esp_timer espcoredump gfx json locale netmgr nvs_flash power
                                     rtc scheduler sensors st7305 storage sync timekeeping ui util weather webui)
