# Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
idf_component_register(SRCS "ui_clock.c" "ui_fields.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx locale datastore util)
