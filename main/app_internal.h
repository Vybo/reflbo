#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "board_buttons.h"
#include "datastore.h"
#include "esp_err.h"
#include "scheduler.h"
#include "settings.h"
#include "ui_fields.h"
#include "ui_menu.h"
#include "ui_preset.h"

/* The dashboard's state and behaviour (main/app_ui.c, main/app_menu.c). All of it belongs to the
 * app task. */

typedef struct {
    ds_t ds;
    settings_t settings;
    ui_presets_t presets;
    time_t cycle_at;      /* next auto-cycle switch; 0 = not armed (cycling off, or no tick yet) */
    time_t done_slot;     /* the last minute slot that was sampled and rendered */
    time_t night_until;   /* night sleep ends here (UTC, on a minute); 0 = no night (spec §9.1) */
    time_t sched_checked; /* schedule entries up to this time have run (spec §5.4) */
    time_t cold_boot_at;  /* for Info > Uptime; 0 while the clock is unset */
    bool critical;        /* the critical-battery screen is up (spec §8) */
} app_ui_state_t;

/* Kconfig settings, the built-in presets and an empty datastore. */
void app_ui_defaults(void);
/* Mounts storage and reads settings.json and presets.json, keeping defaults for what fails. */
void app_ui_load(void);
void app_ui_export(app_ui_state_t *out); /* for the deep-sleep snapshot */
void app_ui_import(const app_ui_state_t *in);
app_ui_state_t *app_state(void); /* the menu edits it (main/app_menu.c) */

const settings_t *app_settings(void);
ds_t *app_ds(void);
ui_presets_t *app_presets(void);

/* Applies the settings that live outside the app: time zone, sensor offsets, freshness, panel rate. */
void app_ui_apply_settings(void);
/* Draws what the screen shows now (the menu, the critical screen or the dashboard, and a toast
 * over it) and pushes it. */
void app_ui_render(void);
/* The context a render uses right now (also for the `field` command). */
void app_ui_context(ui_context_t *ctx);
void app_ui_sample(time_t now); /* SHTC3 and battery into the datastore */
/* Makes preset `index` active and renders it. `persist` saves presets.json (manual switches). */
void app_ui_select(int index, bool persist);
void app_ui_toggle_cycle(void);
void app_ui_set_cycle(bool enabled);
/* Samples and renders what is due now: minute slots, the cycle switch, the seconds display and
 * the schedule. `force` samples and renders anyway. Call after reading the RTC. */
void app_ui_tick(bool force);
sched_wake_t app_ui_next_wake(time_t now);
esp_err_t app_ui_save_settings(void);
esp_err_t app_ui_save_presets(void);

/* A short message over the screen for about 3 s (spec §5.5). */
void app_ui_toast(const char *text);
bool app_ui_toast_active(void);
int64_t app_ui_toast_until_ms(void);
/* The toast's time is up: draws the screen without it. */
void app_ui_toast_expire(void);

/* Night sleep (spec §9.1): starts now and ends at `until` (UTC, on a minute). */
void app_ui_start_night(time_t until);
void app_ui_end_night(void);
bool app_ui_night(void);

/* The on-device menu (main/app_menu.c, spec §5.7). The dashboard's gesture timings (spec §5.6)
 * live there too, since the menu swaps them for its own. */
extern const gesture_config_t k_app_dashboard_buttons[BOARD_BUTTON_COUNT];
void app_menu_open(void);
void app_menu_close(void);
bool app_menu_is_open(void);
void app_menu_key(ui_menu_key_t key);
void app_menu_render(void);
int64_t app_menu_deadline_ms(void); /* the menu closes at this time without input */

/* Implemented in main/app.c for the menu: the clock was set, and moved by delta_s. */
void app_clock_moved(int64_t delta_s);
int64_t app_uptime_ms(void); /* milliseconds since boot, unmoved by clock changes: toasts, menu */

/* The `field`, `preset` and `night` console commands (main/app_cmds.c); call after diag_start(). */
void app_register_commands(void);
