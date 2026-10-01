# The flight radar's data (spec §11.3): adsb.fi's aircraft around the radar's centre and adsb.lol's
# routes. adsb_parse.c and adsb_route.c are pure C and also build on the host; adsb_task.c polls.
idf_component_register(SRCS "adsb_parse.c" "adsb_route.c" "adsb_task.c"
                       INCLUDE_DIRS "include"
                       REQUIRES map esp_common
                       PRIV_REQUIRES json util fetch heap log freertos)
