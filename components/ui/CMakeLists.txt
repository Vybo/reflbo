# Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
idf_component_register(SRCS "ui_clock.c" "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                            "ui_status.c" "ui_dashboard.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx locale datastore util
                       PRIV_REQUIRES json)
