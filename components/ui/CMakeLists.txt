# Screens and layouts (spec §5). Pure C: also built on the host by test/host.
idf_component_register(SRCS "ui_clock.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx)
