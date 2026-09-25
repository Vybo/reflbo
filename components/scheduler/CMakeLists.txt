# Wake scheduler (spec §9.2). Pure C: also built on the host by test/host.
idf_component_register(SRCS "scheduler.c"
                       INCLUDE_DIRS "include")
