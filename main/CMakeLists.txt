idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_menu.c" "app_cmds.c" "app_config.c" "app_web.c" "app_sync.c"
                            "app_radar.c" "app_flights.c" "app_solar.c" "app_secrets.c" "app_mqtt.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES adsb app_update board console datastore diag display energy esp_app_format ha_mqtt
                                     esp_driver_gpio esp_timer espcoredump gfx heap json locale map netmgr nvs_flash
                                     power radar rtc scheduler sensors solar st7305 storage sync timekeeping ui util
                                     weather webui)
