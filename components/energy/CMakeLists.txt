# The house's energy (spec §11.6, D36): SolaX Cloud's request and reply, our signs, the battery's rule,
# and today's totals and quarter hours. Pure C on cJSON, also built on the host; the sync task fetches.
idf_component_register(SRCS "energy.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json util timekeeping)
