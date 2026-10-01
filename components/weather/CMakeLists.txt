# Weather and air quality from Open-Meteo (spec §11). The URLs, parsers and levels are pure C and
# also build on the host; weather_http.c fetches on the device.
idf_component_register(SRCS "weather_url.c" "weather_parse.c" "weather_levels.c" "weather_http.c"
                       INCLUDE_DIRS "include"
                       REQUIRES datastore esp_http_client
                       PRIV_REQUIRES json util mbedtls esp_app_format)
