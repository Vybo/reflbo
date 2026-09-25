# System time, time zone and manual set (spec §7). timekeeping_iso.c is pure C and also built on the host.
idf_component_register(SRCS "timekeeping.c" "timekeeping_iso.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES rtc util)
