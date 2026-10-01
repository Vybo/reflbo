#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "board_buttons.h"
#include "datastore.h"
#include "esp_err.h"
#include "scheduler.h"
#include "settings.h"
#include "sync.h"
#include "sync_plan.h"
#include "ui_fields.h"
#include "ui_menu.h"
#include "ui_preset.h"
#include "webui.h"

/* The dashboard's state and behaviour (main/app_ui.c, main/app_menu.c). All of it belongs to the
 * app task. */

/* The syncs' state (main/app_sync.c, spec §9.3): kept through deep sleep with the rest. */
typedef struct {
    sync_history_t history;
    sync_due_t due;             /* the next automatic sync; at 0 = none */
    uint32_t last_at;           /* when the last sync started (UTC); 0 = none since the cold boot */
    uint8_t last_result[SYNC_STEP_COUNT]; /* sync_step_result_t */
    uint8_t last_failed_step;   /* the first step that failed; SYNC_STEP_COUNT if none */
    char last_detail[SYNC_DETAIL_LEN];
} app_sync_state_t;

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
    bool first_run;       /* settings.json didn't exist at boot: the first-run screen (spec §5.5) */
    app_sync_state_t sync;
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
/* Learning the battery curve from the next full discharge (D21): start or stop, saved at once. */
void app_ui_learn(bool start);
/* A cold boot: the learning session saved in LittleFS, if one runs. */
void app_ui_restore_learning(void);

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

/* The first run (spec §5.5): shown until a button leads on; the settings file then exists. */
bool app_ui_first_run(void);
void app_ui_end_first_run(void);
/* settings.json as it would be saved now (GET /api/settings, the backup). */
size_t app_ui_settings_json(char *out, size_t size);
bool app_ui_check_settings(const char *json, char *err, size_t err_size);
/* A whole new settings.json (a restore) or a merge patch (PATCH /api/settings): validated, saved
 * and applied at once. ESP_ERR_INVALID_ARG with the reason in `err` if it doesn't parse. */
esp_err_t app_ui_replace_settings(const char *json, char *err, size_t err_size);
esp_err_t app_ui_patch_settings(const char *patch, char *err, size_t err_size);
/* A validated presets document (PUT /api/presets, a restore): saved and shown. */
esp_err_t app_ui_replace_presets(const ui_presets_t *presets);
/* The phone's zone (POST /api/time): saved and applied. */
void app_ui_set_zone(const char *iana, const char *posix);

/* The on-device menu (main/app_menu.c, spec §5.7). The dashboard's gesture timings (spec §5.6)
 * live there too, since the menu swaps them for its own. */
extern const gesture_config_t k_app_dashboard_buttons[BOARD_BUTTON_COUNT];
void app_menu_open(void);
void app_menu_close(void);
bool app_menu_is_open(void);
void app_menu_key(ui_menu_key_t key);
void app_menu_render(void);
int64_t app_menu_deadline_ms(void); /* the menu closes at this time without input */

/* The weather and air quality in /fs/state/datastore.bin (spec §6): saved after a sync that fetched
 * them, restored at a cold boot, when they show as stale by their age. */
void app_ui_save_forecast(void);
void app_ui_restore_forecast(void);

/* Syncs (main/app_sync.c, spec §9.3). */
void app_sync_schedule(void);    /* the next automatic sync, after a sync, a settings change or a clock move */
time_t app_sync_due(void);       /* for the wake scheduler; 0 = none */
void app_sync_tick(void);        /* starts a sync that is due; quiet hours in sync mode `always` */
esp_err_t app_sync_now(void);    /* on demand: ESP_ERR_NOT_FOUND with no network saved */
bool app_sync_active(void);      /* a sync runs */
bool app_sync_failed(void);      /* the last sync failed a step */
bool app_sync_holds_wifi(void);  /* sync mode `always` keeps Wi-Fi now */
bool app_sync_lan_ui(void);      /* sync mode `always` is on a network: the web UI runs on the LAN (spec §10.4) */
uint32_t app_sync_expected_s(void);
/* Info ▸ Last sync: "12:05 OK", "05:30 Weather: HTTP 503", "Running", "Never". */
void app_sync_summary(char *out, size_t size);

/* Config mode (main/app_config.c, spec §10.2). */
esp_err_t app_net_init(void); /* the Wi-Fi manager, started on first use */
bool app_net_ready(void);     /* it started: netmgr_status() may be called */
void app_config_enter(void);
void app_config_exit(void);
bool app_config_active(void);
void app_config_key(void); /* KEY short: the other QR code, or the setup screen and the dashboard (D20) */
bool app_config_shows_setup(void); /* the setup screen, rather than the dashboard while a phone is logged in */
void app_config_tick(void);      /* the timeout and the minutes left; call from the app loop */
int64_t app_config_deadline_ms(void); /* app_uptime_ms() at which config mode ends; 0 when off */
int64_t app_config_redraw_ms(void);   /* when the minutes left change next; 0 when off */
void app_config_draw(gfx_fb_t *fb, const lang_t *lang);
/* The web UI runs while config mode or sync mode `always` wants it (spec §10.4). */
void app_net_refresh(void);
/* The API routes the app answers (main/app_web.c); a webui_api_fn. */
void app_web_api(const char *method, const char *path, const char *query, const char *body, uint8_t *out,
                 size_t size, webui_reply_t *reply);

/* Implemented in main/app.c. app_post() runs fn(arg) on the app task later, without waiting:
 * for netmgr and webui, whose tasks must not block on the app. */
esp_err_t app_post(void (*fn)(void *arg), void *arg);
/* Shows `message` for a moment, then restarts the chip; `config_after` comes back in config mode. */
void app_restart(lang_str_t message, bool config_after);
/* Spec §14.4: erases storage and the NVS namespaces wifi, secrets and ctr, keeps sys, restarts. */
void app_factory_reset(void);

/* Implemented in main/app.c for the menu: the clock was set, and moved by delta_s. */
void app_clock_moved(int64_t delta_s);
int64_t app_uptime_ms(void); /* milliseconds since boot, unmoved by clock changes: toasts, menu */
/* Logs, NVS and the console, as a board that stays awake has them (spec §3.3): a sync needs NVS. */
void app_alive(void);

/* The `field`, `preset` and `night` console commands (main/app_cmds.c); call after diag_start(). */
void app_register_commands(void);
