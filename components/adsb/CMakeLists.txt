# The flight radar's data (spec §11.3): adsb.fi's aircraft around the radar's centre and adsb.lol's
# routes. adsb_parse.c and adsb_route.c are pure C and also build on the host.
idf_component_register(SRCS "adsb_parse.c" "adsb_route.c"
                       INCLUDE_DIRS "include"
                       REQUIRES map
                       PRIV_REQUIRES json util)
