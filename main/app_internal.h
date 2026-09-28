#pragma once

#include <stdbool.h>
#include <time.h>

#include "datastore.h"
#include "scheduler.h"
#include "settings.h"
#include "ui_fields.h"
#include "ui_preset.h"

/* The dashboard's state and behaviour (main/app_ui.c). All of it belongs to the app task. */

typedef struct {
    ds_t ds;
    settings_t settings;
    ui_presets_t presets;
    time_t cycle_at;  /* next auto-cycle switch; 0 = not armed (cycling off, or no tick yet) */
    time_t done_slot; /* the last minute slot that was sampled and rendered */
} app_ui_state_t;

/* Kconfig settings, the built-in presets and an empty datastore. */
void app_ui_defaults(void);
/* Mounts storage and reads settings.json and presets.json, keeping defaults for what fails. */
void app_ui_load(void);
void app_ui_export(app_ui_state_t *out); /* for the deep-sleep snapshot */
void app_ui_import(const app_ui_state_t *in);

const settings_t *app_settings(void);
ds_t *app_ds(void);
ui_presets_t *app_presets(void);

/* Applies the settings that live outside the app: time zone, sensor offsets, freshness, panel rate. */
void app_ui_apply_settings(void);
void app_ui_render(void);
/* The context a render uses right now (also for the `field` command). */
void app_ui_context(ui_context_t *ctx);
void app_ui_sample(time_t now); /* SHTC3 and battery into the datastore */
/* Makes preset `index` active and renders it. `persist` saves presets.json (manual switches). */
void app_ui_select(int index, bool persist);
void app_ui_toggle_cycle(void);
/* Samples and renders what is due now: minute slots, the cycle switch, the seconds display.
 * `force` samples and renders anyway. Call after reading the RTC. */
void app_ui_tick(bool force);
sched_wake_t app_ui_next_wake(time_t now);

/* The `field` and `preset` console commands (main/app_cmds.c); call after diag_start(). */
void app_register_commands(void);
