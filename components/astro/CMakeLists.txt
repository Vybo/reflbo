# Sunrise, sunset and day length (spec §11). Pure C: also built on the host by test/host.
idf_component_register(SRCS "astro.c"
                       INCLUDE_DIRS "include")
