# MQTT and Home Assistant (spec §12, D32). ha_fields.c (the field mappings) and ha_payload.c (topics and
# payloads) are pure C and also build on the host.
idf_component_register(SRCS "ha_fields.c" "ha_payload.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json util timekeeping)
