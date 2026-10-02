# HTTPS GETs (spec §11): one at a time, or several over one kept-alive connection. Device only.
idf_component_register(SRCS "fetch.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_http_client mbedtls esp_app_format log)
