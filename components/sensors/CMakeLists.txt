# SHTC3 and battery gauge (spec §8). shtc3_codec.c and battery_model.c are pure C and also built on the host.
idf_component_register(SRCS "sensors.c" "shtc3_codec.c" "battery_model.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES esp_adc util)
