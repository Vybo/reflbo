# MQTT and Home Assistant (spec §12, D32). ha_fields.c (the field mappings), ha_payload.c (topics and
# payloads) and ha_store.c (what the dashboard shows) are pure C and also build on the host.
idf_component_register(SRCS "ha_fields.c" "ha_payload.c" "ha_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES util
                       PRIV_REQUIRES json)
