idf_component_register(SRCS "diag.c" "diag_cmd_system.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES console esp_app_format esp_driver_usb_serial_jtag esp_psram spi_flash vfs)
