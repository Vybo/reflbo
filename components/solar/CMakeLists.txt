# The PV forecast (spec §11.5, D35): the providers' requests and replies, our PV model and the local
# quarter hours. Pure C on cJSON, also built on the host; the sync task fetches.
idf_component_register(SRCS "solar.c" "solar_url.c" "solar_parse.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json util timekeeping)
