# SHTC3 and battery gauge (spec §8). shtc3_codec.c, battery_model.c and battery_learn.c are pure C and also
# built on the host.
idf_component_register(SRCS "sensors.c" "shtc3_codec.c" "battery_model.c" "battery_learn.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES esp_adc util)
