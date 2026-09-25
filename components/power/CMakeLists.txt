# Power states, sleep entry and policy (spec §3.4, §9). power_policy.c is pure C and also built on the host.
idf_component_register(SRCS "power_policy.c"
                       INCLUDE_DIRS "include")
