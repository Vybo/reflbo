# The house's energy (spec §11.6, D36, D37): SolaX Cloud's requests and replies by its Token ID (energy.c) and its
# Developer API (energy_dev.c), our signs, the battery's rule, and today's totals and quarter hours. Pure C on
# cJSON, also built on the host; the sync task fetches.
idf_component_register(SRCS "energy.c" "energy_dev.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json util timekeeping)
