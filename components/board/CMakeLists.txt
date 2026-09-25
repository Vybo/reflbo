# Board bring-up, pins, buttons (spec §3.1). gesture.c is pure C and also built on the host.
idf_component_register(SRCS "board.c" "board_buttons.c" "gesture.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES esp_driver_gpio esp_timer util)
