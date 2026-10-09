# Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                            "ui_status.c" "ui_dashboard.c" "ui_schedule.c" "ui_menu.c" "ui_menu_draw.c"
                            "ui_screens.c" "ui_config.c" "ui_catalog.c" "ui_forecast.c" "ui_radar.c"
                            "ui_flights.c" "ui_split.c" "ui_solar.c" "ui_window.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx locale datastore util map radar adsb solar energy
                       PRIV_REQUIRES json scheduler weather astro)
