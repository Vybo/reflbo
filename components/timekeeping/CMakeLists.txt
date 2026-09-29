# System time, time zone and manual set (spec §7). timekeeping_iso.c and timekeeping_zones.c are
# pure C and also built on the host.
idf_component_register(SRCS "timekeeping.c" "timekeeping_iso.c" "timekeeping_zones.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES rtc util)
