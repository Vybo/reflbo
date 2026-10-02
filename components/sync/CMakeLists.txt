# The sync (spec §9.3). sync_plan.c (when the next sync runs) and sync_ntp.c (the SNTP packets) are
# pure C and also build on the host; sync.c is the task.
idf_component_register(SRCS "sync_plan.c" "sync_ntp.c" "sync.c"
                       INCLUDE_DIRS "include"
                       REQUIRES scheduler datastore storage esp_common radar
                       PRIV_REQUIRES netmgr weather lwip esp_timer)
