# Power states, sleep entry and policy (spec §3.4, §9). power_policy.c is pure C and also built on the host.
idf_component_register(SRCS "power.c" "power_policy.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board esp_driver_gpio esp_driver_usb_serial_jtag esp_timer nvs_flash util)
