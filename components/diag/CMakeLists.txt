idf_component_register(SRCS "diag.c" "diag_cmd_buttons.c" "diag_cmd_display.c" "diag_cmd_sensors.c"
                            "diag_cmd_system.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board console display esp_app_format esp_driver_usb_serial_jtag esp_psram gfx
                                     rtc sensors spi_flash timekeeping util vfs)
