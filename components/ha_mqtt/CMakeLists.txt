# MQTT and Home Assistant (spec §12, D32). ha_fields.c (the field mappings), ha_payload.c (topics and
# payloads), ha_store.c (what the dashboard shows) and ha_session.c (a session's rules) are pure C and also
# build on the host; ha_mqtt.c is the client.
idf_component_register(SRCS "ha_fields.c" "ha_payload.c" "ha_store.c" "ha_session.c" "ha_mqtt.c"
                       INCLUDE_DIRS "include"
                       REQUIRES util esp_common
                       PRIV_REQUIRES json timekeeping mqtt nvs_flash esp_timer esp-tls)
