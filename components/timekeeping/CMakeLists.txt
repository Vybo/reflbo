# System time, time zone, manual set, SNTP and the RTC trim (spec §7). timekeeping_iso.c, timekeeping_sync.c,
# timekeeping_trim.c and timekeeping_zones.c are pure C and also built on the host.
idf_component_register(SRCS "timekeeping.c" "timekeeping_iso.c" "timekeeping_sync.c" "timekeeping_trim.c"
                            "timekeeping_zones.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES rtc util nvs_flash esp_timer)
