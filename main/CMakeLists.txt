idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES board diag display esp_app_format gfx rtc sensors timekeeping)
