# MQTT and Home Assistant (spec §12, D32). ha_fields.c (the field mappings) is pure C and also builds on
# the host.
idf_component_register(SRCS "ha_fields.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json util)
