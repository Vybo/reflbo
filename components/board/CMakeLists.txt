# Board bring-up, pins, buttons (spec §3.1). gesture.c is pure C and also built on the host.
idf_component_register(SRCS "gesture.c"
                       INCLUDE_DIRS "include")
