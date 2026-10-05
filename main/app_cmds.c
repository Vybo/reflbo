#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_internal.h"
#include "diag.h"
#include "esp_console.h"
#include "esp_log.h"
#include "lang.h"
#include "netmgr.h"
#include "ui_fields.h"
#include "ui_layout.h"
#include "util_time.h"

/* `field` and `preset` (spec §15): inspect the dashboard and inject test data on the device. */

static const char *TAG = "app_cmds";

static int usage(const char *text)
{
    printf("usage: %s\n", text);
    return 1;
}

static void print_field(const ui_context_t *ctx, ui_field_id_t field)
{
    ui_value_t v;
    ui_resolve(ctx, field, &v);
    const char *state = v.state == UI_VALUE_FRESH ? "fresh" : v.state == UI_VALUE_STALE ? "stale" : "missing";
    printf("%-13s %-7s %s%s%s", ui_field_info(field)->id, state, v.text, v.unit[0] ? " " : "", v.unit);
    if (v.extra[0]) {
        printf(" (%s)", v.extra);
    }
    if (v.state == UI_VALUE_STALE) {
        printf(", %lu s old", (unsigned long)v.age_s);
    }
    if (v.trend) {
        printf(", %s", v.trend > 0 ? "rising" : "falling");
    }
    printf("\n");
}

/* Datastore units per console unit: 0.01 °C, 0.01 %, %, 0.1 d. */
static int scale(ds_field_t field)
{
    return field == DS_BAT_LEVEL ? 1 : field == DS_BAT_DAYS ? 10 : 100;
}

static int field_body(int argc, char **argv)
{
    static const char *const k_usage = "field list | field get <id> | field set <id> <value> | field clear <id>";
    ui_context_t ctx;
    app_ui_context(&ctx);
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
            print_field(&ctx, (ui_field_id_t)f);
        }
        return 0;
    }
    if (argc < 3) {
        return usage(k_usage);
    }
    ui_field_id_t field = ui_field_by_name(argv[2]);
    if (field == UI_FIELD_NONE) {
        printf("field: no field \"%s\" (see `field list`)\n", argv[2]);
        return 1;
    }
    if (argc == 3 && strcmp(argv[1], "get") == 0) {
        print_field(&ctx, field);
        return 0;
    }
    int ds_field = ui_field_info(field)->ds_field;
    bool set = argc == 4 && strcmp(argv[1], "set") == 0;
    bool clear = argc == 3 && strcmp(argv[1], "clear") == 0;
    if (!set && !clear) {
        return usage(k_usage);
    }
    if (ds_field < 0) {
        printf("field: %s follows the clock and can't be set\n", argv[2]);
        return 1;
    }
    if (set) {
        char *end;
        double value = strtod(argv[3], &end);
        if (end == argv[3] || *end != '\0' || !(value >= -100000.0 && value <= 100000.0)) { /* NaN and inf too */
            return usage(k_usage);
        }
        long scaled = (long)(value * scale((ds_field_t)ds_field) + (value < 0 ? -0.5 : 0.5));
        ds_set(app_ds(), (ds_field_t)ds_field, (int32_t)scaled, ctx.now);
    } else {
        ds_clear(app_ds(), (ds_field_t)ds_field);
    }
    app_ui_render();
    app_ui_context(&ctx);
    print_field(&ctx, field);
    return 0;
}

static int preset_body(int argc, char **argv)
{
    static const char *const k_usage = "preset list | preset set <id>";
    ui_presets_t *p = app_presets();
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        for (int i = 0; i < p->count; i++) {
            const ui_preset_t *pr = &p->presets[i];
            printf("%c %-15s %-23s %-8s%s\n", i == p->active ? '*' : ' ', pr->id, pr->name,
                   ui_layout((ui_layout_id_t)pr->layout)->id, pr->in_cycle ? "" : " (not in the cycle)");
        }
        printf("auto-cycle %s, every %u s\n", p->cycle_enabled ? "on" : "off", (unsigned)p->cycle_interval_s);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "set") == 0) {
        int index = ui_presets_find(p, argv[2]);
        if (index < 0) {
            printf("preset: no preset \"%s\" (see `preset list`)\n", argv[2]);
            return 1;
        }
        app_ui_select(index, true);
        printf("preset: %s\n", p->presets[index].id);
        return 0;
    }
    return usage(k_usage);
}

static int cmd_field(int argc, char **argv)
{
    return diag_on_owner(field_body, argc, argv);
}

static int cmd_preset(int argc, char **argv)
{
    return diag_on_owner(preset_body, argc, argv);
}

/* `night <minutes>` (spec §9.1): night sleep now, for measuring what it saves. */
static int night_body(int argc, char **argv)
{
    static const char *const k_usage = "night <minutes, 1-1440>";
    char *end;
    long minutes = argc == 2 ? strtol(argv[1], &end, 10) : 0;
    if (argc != 2 || *end != '\0' || minutes < 1 || minutes > 1440) {
        return usage(k_usage);
    }
    time_t until = time(NULL) + minutes * 60;
    until += (60 - until % 60) % 60; /* the RTC alarm fires on whole minutes */
    struct tm local;
    localtime_r(&until, &local);
    char text[48];
    snprintf(text, sizeof(text), "%s %02d:%02d", lang_str(lang_get(app_settings()->language), LS_T_NIGHT_UNTIL),
             local.tm_hour, local.tm_min);
    app_ui_start_night(until);
    app_ui_toast(text);
    printf("night: until %02d:%02d local; the board sleeps in 3 s, even while a PC is attached, and the console "
           "drops\n",
           local.tm_hour, local.tm_min);
    return 0;
}

static int cmd_night(int argc, char **argv)
{
    return diag_on_owner(night_body, argc, argv);
}

/* "22:30" -> minutes after midnight, or -1. */
static int parse_hhmm(const char *s)
{
    int h, m;
    char tail;
    if (strlen(s) != 5 || sscanf(s, "%2d:%2d%c", &h, &m, &tail) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return -1;
    }
    return h * 60 + m;
}

static void print_schedule(const ui_presets_t *p)
{
    printf("schedule %s, %d of %d entries\n", p->schedule.enabled ? "on" : "off", p->schedule.count, UI_SCHEDULE_MAX);
    for (int i = 0; i < p->schedule.count; i++) {
        const ui_schedule_entry_t *e = &p->schedule.entries[i];
        printf("%d: %02d:%02d days 0x%02x ", i + 1, e->at_min / 60, e->at_min % 60, e->days);
        if (e->action == UI_SCHED_NIGHT) {
            printf("night until %02d:%02d\n", e->until_min / 60, e->until_min % 60);
        } else {
            printf("preset %s\n", p->presets[e->preset].id);
        }
    }
}

/* `schedule` (spec §5.7: until the M4 web UI, presets.json or the console sets the entries). */
static int schedule_body(int argc, char **argv)
{
    static const char *const k_usage = "schedule list | on | off | clear | add <HH:MM> preset <id> [days] | "
                                       "add <HH:MM> night <HH:MM> [days]  (days: bit 0 Monday, default 127)";
    ui_presets_t *p = app_presets();
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        print_schedule(p);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "off") == 0)) {
        p->schedule.enabled = strcmp(argv[1], "on") == 0;
        app_state()->sched_checked = time(NULL);
    } else if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        p->schedule.count = 0;
    } else if ((argc == 5 || argc == 6) && strcmp(argv[1], "add") == 0) {
        if (p->schedule.count >= UI_SCHEDULE_MAX) {
            printf("schedule: full (%d entries)\n", UI_SCHEDULE_MAX);
            return 1;
        }
        ui_schedule_entry_t e = { .days = 0x7F };
        int at = parse_hhmm(argv[2]);
        char *end = NULL;
        long days = argc == 6 ? strtol(argv[5], &end, 0) : 0x7F;
        if (at < 0 || (argc == 6 && (*end != '\0' || days < 0 || days > 0x7F))) {
            return usage(k_usage);
        }
        e.at_min = (uint16_t)at;
        e.days = (uint8_t)days;
        if (strcmp(argv[3], "preset") == 0) {
            int index = ui_presets_find(p, argv[4]);
            if (index < 0) {
                printf("schedule: no preset \"%s\" (see `preset list`)\n", argv[4]);
                return 1;
            }
            e.action = UI_SCHED_PRESET;
            e.preset = (uint8_t)index;
        } else if (strcmp(argv[3], "night") == 0 && parse_hhmm(argv[4]) >= 0) {
            if (parse_hhmm(argv[4]) == at) {
                printf("schedule: a night must end at another time than it starts\n");
                return 1;
            }
            e.action = UI_SCHED_NIGHT;
            e.until_min = (uint16_t)parse_hhmm(argv[4]);
        } else {
            return usage(k_usage);
        }
        p->schedule.entries[p->schedule.count++] = e;
    } else {
        return usage(k_usage);
    }
    if (app_ui_save_presets() != ESP_OK) {
        printf("schedule: changed for this session, not saved\n");
    }
    print_schedule(p);
    return 0;
}

static int cmd_schedule(int argc, char **argv)
{
    return diag_on_owner(schedule_body, argc, argv);
}

/* `wifi status | scan` (spec §15). netmgr is thread-safe: this runs on the console task, so a
 * scan doesn't hold up the app task. */
static int cmd_wifi(int argc, char **argv)
{
    static const char *const k_usage = "wifi status | wifi scan  (Wi-Fi is on in config mode: `btn boot long`)";
    if (app_net_init() != ESP_OK) {
        printf("wifi: the Wi-Fi manager didn't start\n");
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        static netmgr_status_t st;
        static netmgr_list_t saved;
        static const char *const k_states[] = { "off", "joining", "on a network", "AP only" };
        netmgr_status(&st);
        netmgr_networks(&saved);
        printf("state %s%s\n", k_states[st.state], st.ap_on && st.state != NETMGR_AP ? ", AP too" : "");
        if (st.state == NETMGR_STATION) {
            printf("network \"%s\", %s, %d dBm\n", st.ssid, st.ip[0] ? st.ip : "no address yet", st.rssi);
        }
        if (st.ap_on) {
            printf("AP %s, %u client(s)\n", st.ap_ssid, st.ap_clients);
        }
        printf("host %s.local\n", st.host);
        printf("%d saved network(s)%s", saved.count, saved.count ? ":" : "");
        for (int i = 0; i < saved.count; i++) {
            printf(" \"%s\"", saved.nets[i].ssid);
        }
        printf("\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "scan") == 0) {
        static netmgr_ap_t aps[NETMGR_SCAN_MAX];
        int n = netmgr_scan(aps, NETMGR_SCAN_MAX);
        if (n == 0) {
            printf("wifi: nothing found; Wi-Fi is on only in config mode\n");
        }
        for (int i = 0; i < n; i++) {
            printf("%4d dBm %s %s\n", aps[i].rssi, aps[i].open ? "open" : "WPA ", aps[i].ssid);
        }
        return 0;
    }
    return usage(k_usage);
}

static void print_time(const char *label, time_t t)
{
    if (t == 0) {
        printf("%s none\n", label);
        return;
    }
    struct tm local;
    localtime_r(&t, &local);
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
    printf("%s %s\n", label, text);
}

/* `sync now | status` (spec §15). */
static int sync_body(int argc, char **argv)
{
    static const char *const k_usage = "sync now | sync status";
    if (argc == 2 && strcmp(argv[1], "now") == 0) {
        esp_err_t err = app_sync_now();
        printf("sync: %s\n", err == ESP_OK                  ? "started; `sync status` follows it"
                              : err == ESP_ERR_NOT_FOUND     ? "no Wi-Fi network saved"
                              : err == ESP_ERR_INVALID_STATE ? "one runs already, or the battery is critical"
                                                             : esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        static const char *const k_modes[] = { "times", "interval", "always", "manual" };
        const settings_t *s = app_settings();
        printf("mode %s", k_modes[s->sync_mode <= SETTINGS_SYNC_MANUAL ? s->sync_mode : 0]);
        if (s->sync_mode == SETTINGS_SYNC_TIMES) {
            for (int i = 0; i < s->sync_time_count; i++) {
                printf(" %02d:%02d", s->sync_times[i] / 60, s->sync_times[i] % 60);
            }
        } else if (s->sync_mode == SETTINGS_SYNC_INTERVAL) {
            printf(" every %u min", s->sync_interval_min);
        }
        printf("; quiet hours %02d:%02d-%02d:%02d %s; expected every %lu s\n", s->quiet_from / 60, s->quiet_from % 60,
               s->quiet_to / 60, s->quiet_to % 60, s->quiet ? "on" : "off", (unsigned long)app_sync_expected_s());
        const app_sync_state_t *st = &app_state()->sync;
        if (app_sync_active()) {
            printf("running: %s%s\n", app_sync_refreshing() ? "radar refresh, " : "",
                   sync_running() ? sync_step_name(sync_step()) : "applying its report");
        }
        print_time("last", st->last_at);
        if (st->last_at != 0) {
            static const char *const k_results[] = { "skipped", "ok", "failed" };
            for (int i = 0; i < SYNC_STEP_COUNT; i++) {
                printf("  %-8s %s%s%s\n", sync_step_name((sync_step_t)i),
                       k_results[st->last_result[i] <= SYNC_STEP_FAILED ? st->last_result[i] : 0],
                       i == st->last_failed_step ? ": " : "", i == st->last_failed_step ? st->last_detail : "");
            }
        }
        print_time(st->due.retry ? "next (a retry)" : "next", st->due.at);
        return 0;
    }
    return usage(k_usage);
}

static int cmd_sync(int argc, char **argv)
{
    return diag_on_owner(sync_body, argc, argv);
}

/* `radar status | loop` (spec §15, M6). */
static int radar_body(int argc, char **argv)
{
    static const char *const k_usage = "radar status | radar loop";
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        app_radar_status_t r;
        app_radar_status(&r);
        printf("weather: %s, %u frame%s kept\n", r.source == RADAR_SOURCE_CHMU ? "ChMU" : "RainViewer", r.frames,
               r.frames == 1 ? "" : "s");
        print_time("  newest", (time_t)r.frame_at);
        print_time("  fetched", r.fetched_at);
        if (r.fetched_at != 0 && !r.ok) {
            printf("  failed: %s\n", r.detail);
        }
        app_flights_status_t f;
        app_flights_status(&f);
        printf("flights: %s, %u aircraft%s%s\n", f.on ? "polling" : "off", f.aircraft,
               f.failed ? ", the last poll failed: " : "", f.failed ? f.error : "");
        print_time("  updated", f.updated);
        if (f.routes_paused_until != 0) {
            print_time("  routes paused until", f.routes_paused_until); /* adsb.lol asked for it */
        }
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "loop") == 0) {
        const ui_presets_t *p = app_presets();
        if (p->presets[p->active].layout != UI_LAYOUT_RADAR) {
            printf("radar: the loop plays on the Radar layout\n");
            return 1;
        }
        bool ok = app_radar_loop_start();
        printf("radar: %s\n", ok ? "the loop plays" : "fewer than two frames: sync mode always keeps the hour");
        return ok ? 0 : 1;
    }
    return usage(k_usage);
}

static int cmd_radar(int argc, char **argv)
{
    return diag_on_owner(radar_body, argc, argv);
}

/* "18.4 kWh", or "-" for a day without one. */
static void print_wh(const char *label, uint32_t wh)
{
    if (wh == SOLAR_WH_NONE) {
        printf(" %s -", label);
    } else {
        printf(" %s %lu.%lu kWh", label, (unsigned long)(wh / 1000), (unsigned long)(wh % 1000 / 100));
    }
}

/* `solar status | demo on | demo off` (spec §15, M6d): never a key. */
static int solar_body(int argc, char **argv)
{
    static const char *const k_usage = "solar status | solar demo on | solar demo off";
    if (argc == 3 && strcmp(argv[1], "demo") == 0 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        app_solar_demo(strcmp(argv[2], "on") == 0);
        app_ui_render();
        printf("solar: the sample day %s\n", strcmp(argv[2], "on") == 0 ? "shows" : "is gone");
        return 0;
    }
    if (argc != 2 || strcmp(argv[1], "status") != 0) {
        return usage(k_usage);
    }
    static const char *const k_sources[] = { "off", "open-meteo", "forecast-solar", "solcast" };
    const settings_t *set = app_settings();
    const app_solar_state_t *s = app_solar_state();
    printf("forecast: %s%s\n", k_sources[set->solar_source <= SETTINGS_SOLAR_SOLCAST ? set->solar_source : 0],
           s->demo ? " (the sample day shows)" : "");
    print_time("  fetched", (time_t)s->forecast.fetched);
    if (s->forecast.day != 0) {
        int y, m, d;
        util_civil_from_days(s->forecast.day, &y, &m, &d);
        printf("  from %04d-%02d-%02d:", y, m, d);
        print_wh("first", s->forecast.wh[0]);
        print_wh("next", s->forecast.wh[1]);
        print_wh("after", s->forecast.wh[2]);
        printf("\n");
    }
    print_time("  last step", (time_t)s->forecast_tried);
    if (s->forecast_kept[0] != '\0') {
        printf("  kept: %s\n", s->forecast_kept);
    }
    if (s->forecast_error[0] != '\0') {
        printf("  last call's error: %s\n", s->forecast_error);
    }
    if (set->solar_source == SETTINGS_SOLAR_SOLCAST) {
        print_time("  Solcast asked", (time_t)s->solcast_asked);
        printf("  its sites: %u\n", s->solcast_sites);
    }
    printf("energy: %s, battery %s\n", set->energy_source == SETTINGS_ENERGY_SOLAX ? "solax" : "off",
           set->energy_battery == SETTINGS_BATTERY_ON ? "on" : set->energy_battery == SETTINGS_BATTERY_OFF ? "off"
                                                                                                          : "auto");
    const energy_reading_t *r = &s->reading;
    print_time("  reading", (time_t)r->at);
    if (r->at != 0) {
        printf("  solar %ld W, grid %ld W, home %ld W, battery %ld W at %d %%, inverter type %u\n", (long)r->pv_w,
               (long)r->grid_w, (long)r->load_w, (long)r->bat_w, r->soc, r->inverter);
        int32_t day = energy_reading_day(r);
        uint32_t out = energy_to_grid_wh(&s->day, r, day), in = energy_from_grid_wh(&s->day, r, day);
        printf("  its day:");
        print_wh("produced", r->yield_wh);
        print_wh("to the grid", out == ENERGY_WH_NONE ? SOLAR_WH_NONE : out);
        print_wh("from it", in == ENERGY_WH_NONE ? SOLAR_WH_NONE : in);
        printf("\n");
        print_time("  midnight's reading", (time_t)s->day.base_at);
    }
    print_time("  last step", (time_t)s->energy_tried);
    if (s->energy_error[0] != '\0') {
        printf("  error: %s\n", s->energy_error);
    }
    return 0;
}

static int cmd_solar(int argc, char **argv)
{
    return diag_on_owner(solar_body, argc, argv);
}

void app_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "field", .help = "field list | get <id> | set <id> <value> | clear <id>", .func = &cmd_field },
        { .command = "preset", .help = "preset list | set <id>", .func = &cmd_preset },
        { .command = "night", .help = "night <minutes>: night sleep now (spec §9.1)", .func = &cmd_night },
        { .command = "schedule", .help = "schedule list | on | off | clear | add <HH:MM> preset <id> [days] | "
                                         "add <HH:MM> night <HH:MM> [days]", .func = &cmd_schedule },
        { .command = "wifi", .help = "wifi status | scan", .func = &cmd_wifi },
        { .command = "sync", .help = "sync now | status (spec §9.3)", .func = &cmd_sync },
        { .command = "radar", .help = "radar status | loop (spec §11.2, §11.3)", .func = &cmd_radar },
        { .command = "solar", .help = "solar status | demo on | demo off (spec §11.5, §11.6)", .func = &cmd_solar },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: %s", cmds[i].command, esp_err_to_name(err));
        }
    }
}
