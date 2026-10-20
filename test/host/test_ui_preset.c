#define _POSIX_C_SOURCE 200809L /* setenv */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ui_fields.h"
#include "ui_preset.h"
#include "ui_split.h"
#include "unity.h"
#include "util_json.h"

static ui_presets_t s_p;
static char s_err[128];
static char s_json[UI_PRESETS_JSON_MAX];

void setUp(void)
{
    ui_presets_defaults(&s_p);
    s_err[0] = '\0';
}

void tearDown(void) {}

static void test_defaults_are_the_eight_built_ins(void)
{
    TEST_ASSERT_EQUAL_INT(8, s_p.count);
    TEST_ASSERT_EQUAL_STRING("home", s_p.presets[s_p.active].id);
    TEST_ASSERT_EQUAL(UI_LAYOUT_CLASSIC, s_p.presets[0].layout);
    TEST_ASSERT_EQUAL(UI_FIELD_TIME_CLOCK, s_p.presets[0].slots[0]);
    TEST_ASSERT_EQUAL_INT(2, ui_presets_find(&s_p, "weather"));
    TEST_ASSERT_TRUE(s_p.presets[2].in_cycle); /* M5 brings its data (spec §5.4) */
    TEST_ASSERT_EQUAL_INT(4, ui_presets_find(&s_p, "rain")); /* M6 (D28) */
    TEST_ASSERT_EQUAL(UI_LAYOUT_RADAR, s_p.presets[4].layout);
    TEST_ASSERT_EQUAL_STRING("Rain radar", s_p.presets[4].name);
    TEST_ASSERT_TRUE(s_p.presets[4].in_cycle);
    TEST_ASSERT_EQUAL_INT(5, ui_presets_find(&s_p, "flights"));
    TEST_ASSERT_EQUAL(UI_LAYOUT_FLIGHTS, s_p.presets[5].layout);
    TEST_ASSERT_TRUE(s_p.presets[5].in_cycle);
    TEST_ASSERT_TRUE(s_p.presets[4].status_clock && s_p.presets[5].status_clock); /* a map fills the screen */
    TEST_ASSERT_EQUAL_INT(6, ui_presets_find(&s_p, "solar")); /* M6d (D35, D36) */
    TEST_ASSERT_EQUAL(UI_LAYOUT_SOLAR, s_p.presets[6].layout);
    TEST_ASSERT_EQUAL_STRING("Solar", s_p.presets[6].name);
    TEST_ASSERT_EQUAL_INT(7, ui_presets_find(&s_p, "energy"));
    TEST_ASSERT_EQUAL(UI_LAYOUT_ENERGY, s_p.presets[7].layout);
    TEST_ASSERT_EQUAL_STRING("Energy", s_p.presets[7].name);
    TEST_ASSERT_FALSE(s_p.presets[6].in_cycle || s_p.presets[7].in_cycle); /* outside the cycle */
    TEST_ASSERT_TRUE(s_p.presets[6].status_clock && s_p.presets[7].status_clock);
    TEST_ASSERT_EQUAL_UINT8(UI_OFFERED_ALL, s_p.offered); /* nothing left to add */
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "nope"));
}

static void test_next_follows_cycle_order_and_skips_presets_out_of_it(void)
{
    TEST_ASSERT_EQUAL_INT(1, ui_presets_next(&s_p, true)); /* home -> indoor */
    s_p.active = 1;
    TEST_ASSERT_EQUAL_INT(2, ui_presets_next(&s_p, true)); /* indoor -> weather */
    s_p.presets[2].in_cycle = false;
    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p, true)); /* indoor -> focus, skipping weather out of the cycle */
    s_p.active = 5;
    TEST_ASSERT_EQUAL_INT(0, ui_presets_next(&s_p, true)); /* wraps */
    for (int i = 0; i < s_p.count; i++) {
        s_p.presets[i].in_cycle = i == 3;
    }
    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p, true)); /* the only one: stays */
}

static void test_the_cycle_visits_flights_only_in_sync_mode_always(void)
{
    s_p.active = 4; /* rain */
    TEST_ASSERT_EQUAL_INT(5, ui_presets_next(&s_p, true));
    TEST_ASSERT_EQUAL_INT(0, ui_presets_next(&s_p, false)); /* past flights to home */
    for (int i = 0; i < s_p.count; i++) {
        s_p.presets[i].in_cycle = i == 5;
    }
    s_p.active = 0;
    TEST_ASSERT_EQUAL_INT(0, ui_presets_next(&s_p, false)); /* only flights in the cycle: stays */
    TEST_ASSERT_EQUAL_INT(5, ui_presets_next(&s_p, true));
}

/* Europe/Prague and Brno, the defaults (AGENTS.md §1), for the cycle windows (spec §5.4, D41). */
#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"
#define BRNO_LAT 491951
#define BRNO_LON 166068

static time_t local_time(int y, int mo, int d, int h, int mi)
{
    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi, .tm_isdst = -1 };
    return mktime(&t);
}

static int cycle_next(bool always, time_t now)
{
    return ui_presets_cycle_next(&s_p, always, now, BRNO_LAT, BRNO_LON);
}

/* The auto-cycle skips a preset whose window is closed; KEY short (ui_presets_next) doesn't (D41). */
static void test_the_auto_cycle_skips_a_closed_window(void)
{
    setenv("TZ", TZ_PRAGUE, 1);
    tzset();
    TEST_ASSERT_TRUE(ui_window_make("07:00", "08:00", 127, &s_p.presets[1].window, s_err, sizeof(s_err)));
    time_t noon = local_time(2026, 10, 9, 12, 0), morning = local_time(2026, 10, 9, 7, 30);
    TEST_ASSERT_EQUAL_INT(2, cycle_next(true, noon));  /* home -> weather, past indoor's closed window */
    TEST_ASSERT_EQUAL_INT(1, cycle_next(true, morning)); /* open: home -> indoor */
    TEST_ASSERT_EQUAL_INT(1, ui_presets_next(&s_p, true)); /* KEY short still reaches it */
    TEST_ASSERT_EQUAL_INT(1, cycle_next(true, 0));       /* no valid time: every window open */
    s_p.active = 4;                                      /* rain: flights still only in sync mode always */
    TEST_ASSERT_EQUAL_INT(5, cycle_next(true, noon));
    TEST_ASSERT_EQUAL_INT(0, cycle_next(false, noon));
}

/* With no other preset in the cycle open, the one on screen stays, its own window open or not. */
static void test_the_auto_cycle_keeps_the_preset_when_none_is_open(void)
{
    setenv("TZ", TZ_PRAGUE, 1);
    tzset();
    for (int i = 0; i < s_p.count; i++) {
        TEST_ASSERT_TRUE(ui_window_make("07:00", "08:00", 127, &s_p.presets[i].window, s_err, sizeof(s_err)));
    }
    s_p.active = 2;
    TEST_ASSERT_EQUAL_INT(2, cycle_next(true, local_time(2026, 10, 9, 12, 0)));
    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p, true));
}

/* The built-in Solar preset has a window from sunrise to sunset; the others none (spec §5.4, M6e). */
static void test_the_built_in_solar_preset_has_a_daylight_window(void)
{
    for (int i = 0; i < s_p.count; i++) {
        const ui_window_t *w = &s_p.presets[i].window;
        if (strcmp(s_p.presets[i].id, "solar") == 0) {
            TEST_ASSERT_EQUAL_UINT8(0x7F, w->days);
            TEST_ASSERT_EQUAL_INT16(UI_BOUND_SUNRISE, w->from);
            TEST_ASSERT_EQUAL_INT16(UI_BOUND_SUNSET, w->until);
        } else {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, w->days, s_p.presets[i].id);
        }
    }
}

static void test_a_window_survives_a_round_trip(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"window\": "
                       "{\"from\": \"sunrise+30\", \"until\": \"22:00\", \"days\": 31}}, "
                       "{\"id\": \"b\", \"layout\": \"grid\"}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(31, s_p.presets[0].window.days);
    TEST_ASSERT_EQUAL_INT16(UI_BOUND_SUNRISE + 30, s_p.presets[0].window.from);
    TEST_ASSERT_EQUAL_INT16(22 * 60, s_p.presets[0].window.until);
    TEST_ASSERT_EQUAL_UINT8(0, s_p.presets[1].window.days); /* none: always open */
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"window\":{\"from\":\"sunrise+30\",\"until\":\"22:00\",\"days\":31}"));
    TEST_ASSERT_NULL(strstr(strstr(s_json, "\"id\":\"b\""), "\"window\"")); /* not for b */
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
}

/* A window without days opens every day; `null` is no window. */
static void test_a_window_without_days_opens_every_day(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"window\": "
                       "{\"from\": \"sunset\", \"until\": \"sunrise\"}}, "
                       "{\"id\": \"b\", \"layout\": \"grid\", \"window\": null}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(0x7F, s_p.presets[0].window.days);
    TEST_ASSERT_EQUAL_UINT8(0, s_p.presets[1].window.days);
}

static void test_bad_windows_are_rejected_with_a_reason(void)
{
    static const struct {
        const char *window, *why;
    } k_cases[] = {
        { "\"sunrise\"", "preset \"a\": window must be an object" },
        { "{\"from\": \"dawn\", \"until\": \"sunset\"}",
          "preset \"a\": window: from must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes" },
        { "{\"from\": \"07:00\"}",
          "preset \"a\": window: until must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes" },
        { "{\"from\": \"sunset+0\", \"until\": \"23:00\"}",
          "preset \"a\": window: from must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes" },
        { "{\"from\": \"07:00\", \"until\": \"07:00\"}", "preset \"a\": window: from and until can't be the same" },
        { "{\"from\": \"07:00\", \"until\": \"22:00\", \"days\": 0}", "preset \"a\": window: days must be 1-127" },
        { "{\"from\": \"07:00\", \"until\": \"22:00\", \"days\": 255}", "preset \"a\": window: days must be 1-127" },
        { "{\"from\": \"07:00\", \"until\": \"22:00\", \"days\": \"Mo\"}", "preset \"a\": window: days must be 1-127" },
        { "{\"from\": \"07:00\", \"until\": \"22:00\", \"days\": 3.5}", "preset \"a\": window: days must be 1-127" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"window\": %s}]}",
                 k_cases[i].window);
        s_err[0] = '\0';
        TEST_ASSERT_FALSE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), json);
        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
    }
}

/* The longest reason, a window's with a 15-character id, fits UI_PRESET_ERR_LEN whole: the page shows it. */
static void test_the_longest_reason_fits_ui_preset_err_len(void)
{
    char err[UI_PRESET_ERR_LEN];
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"abcdefghijklmno\", \"layout\": \"grid\", "
                       "\"window\": {\"from\": \"07:00\", \"until\": \"dusk\"}}]}";
    TEST_ASSERT_FALSE(ui_presets_from_json(json, &s_p, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("preset \"abcdefghijklmno\": window: until must be HH:MM, or sunrise or sunset "
                             "with an offset of 1-180 minutes", err);
}

/* A presets.json saved by M5: the four presets of its day, no marker. */
static const char k_m5_file[] =
    "{\"schema\":1,\"active\":\"weather\",\"presets\":["
    "{\"id\":\"home\",\"layout\":\"classic\"},{\"id\":\"indoor\",\"layout\":\"grid\"},"
    "{\"id\":\"weather\",\"layout\":\"weather\"},{\"id\":\"focus\",\"layout\":\"focus\"}]}";

static void test_a_file_from_before_m6_gains_the_radars_once(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_m5_file, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(0, s_p.offered);
    TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p)); /* changed: save it */
    TEST_ASSERT_EQUAL_INT(8, s_p.count);
    TEST_ASSERT_EQUAL_INT(4, ui_presets_find(&s_p, "rain"));
    TEST_ASSERT_EQUAL_INT(5, ui_presets_find(&s_p, "flights"));
    TEST_ASSERT_EQUAL_INT(6, ui_presets_find(&s_p, "solar")); /* and M6d's */
    TEST_ASSERT_EQUAL_STRING("weather", s_p.presets[s_p.active].id); /* the active one stays */
    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p));

    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"offered\":[\"rain\",\"flights\",\"solar\",\"energy\"]"));
    memmove(&s_p.presets[5], &s_p.presets[6], 2 * sizeof(s_p.presets[0])); /* the owner deletes Flights */
    s_p.count = 7;
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p)); /* and it stays deleted */
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "flights"));
}

/* A presets.json saved by M6c: the radars offered, its own presets, the Weather preset active. */
static const char k_m6c_file[] =
    "{\"schema\":1,\"active\":\"weather\",\"offered\":[\"rain\",\"flights\"],\"presets\":["
    "{\"id\":\"home\",\"layout\":\"classic\"},{\"id\":\"weather\",\"layout\":\"weather\"},"
    "{\"id\":\"rain\",\"layout\":\"radar\"}]}";

static void test_a_file_from_m6c_gains_solar_and_energy_once(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_m6c_file, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(UI_OFFERED_RAIN | UI_OFFERED_FLIGHTS, s_p.offered);
    TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p));
    TEST_ASSERT_EQUAL_INT(5, s_p.count); /* Flights was deleted under M6c: it stays deleted */
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "flights"));
    TEST_ASSERT_EQUAL_INT(3, ui_presets_find(&s_p, "solar"));
    TEST_ASSERT_EQUAL_INT(4, ui_presets_find(&s_p, "energy"));
    TEST_ASSERT_FALSE(s_p.presets[3].in_cycle || s_p.presets[4].in_cycle);
    TEST_ASSERT_EQUAL_STRING("weather", s_p.presets[s_p.active].id);
    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p));
    s_p.count = 4; /* the owner deletes Energy */
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p)); /* and it stays deleted */
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "energy"));
}

static void test_the_radars_need_room_and_a_free_id(void)
{
    memset(&s_p, 0, sizeof(s_p));
    for (int i = 0; i < UI_PRESET_MAX; i++) {
        snprintf(s_p.presets[i].id, sizeof(s_p.presets[i].id), "p%d", i);
    }
    s_p.count = UI_PRESET_MAX;
    TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p)); /* the marker changed, nothing was added */
    TEST_ASSERT_EQUAL_INT(UI_PRESET_MAX, s_p.count);
    TEST_ASSERT_EQUAL_UINT8(UI_OFFERED_ALL, s_p.offered);

    memset(&s_p, 0, sizeof(s_p));
    snprintf(s_p.presets[0].id, sizeof(s_p.presets[0].id), "rain"); /* the owner's own, on another layout */
    s_p.presets[0].layout = UI_LAYOUT_GRID;
    s_p.count = 1;
    TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p));
    TEST_ASSERT_EQUAL_INT(4, s_p.count); /* Flights, Solar and Energy */
    TEST_ASSERT_EQUAL(UI_LAYOUT_GRID, s_p.presets[0].layout);
    TEST_ASSERT_EQUAL_STRING("flights", s_p.presets[1].id);
    TEST_ASSERT_EQUAL_STRING("energy", s_p.presets[3].id);
}

static void test_the_solar_layouts_have_no_slots(void)
{
    static const char k_ok[] = "{\"schema\":1,\"presets\":[{\"id\":\"s\",\"layout\":\"solar\"},"
                               "{\"id\":\"e\",\"layout\":\"energy\",\"slots\":{}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_ok, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL(UI_LAYOUT_SOLAR, s_p.presets[0].layout);
    TEST_ASSERT_EQUAL(UI_LAYOUT_ENERGY, s_p.presets[1].layout);
    TEST_ASSERT_EQUAL_INT(0, ui_preset_slots(&s_p.presets[0]));
    static const char k_slot[] =
        "{\"schema\":1,\"presets\":[{\"id\":\"e\",\"layout\":\"energy\",\"slots\":{\"flow\":\"energy.flow\"}}]}";
    TEST_ASSERT_FALSE(ui_presets_from_json(k_slot, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_NOT_NULL(strstr(s_err, "has no slot"));
}

static void test_the_radar_layouts_have_no_slots_and_the_rain_map_needs_room(void)
{
    static const char k_ok[] =
        "{\"schema\":1,\"presets\":[{\"id\":\"r\",\"layout\":\"radar\"},"
        "{\"id\":\"f\",\"layout\":\"flights\",\"slots\":{}},"
        "{\"id\":\"g\",\"layout\":\"grid\",\"slots\":{\"g1\":\"rain.map\"}},"
        "{\"id\":\"w\",\"layout\":\"weather\",\"slots\":{\"now\":\"rain.map\"}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_ok, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL(UI_FIELD_RAIN_MAP, s_p.presets[2].slots[0]);
    static const char k_slot[] =
        "{\"schema\":1,\"presets\":[{\"id\":\"r\",\"layout\":\"radar\",\"slots\":{\"map\":\"rain.map\"}}]}";
    TEST_ASSERT_FALSE(ui_presets_from_json(k_slot, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_NOT_NULL(strstr(s_err, "has no slot"));
    static const char k_small[] =
        "{\"schema\":1,\"presets\":[{\"id\":\"h\",\"layout\":\"classic\",\"slots\":{\"s1\":\"rain.map\"}}]}";
    TEST_ASSERT_FALSE(ui_presets_from_json(k_small, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_NOT_NULL(strstr(s_err, "can't show"));
}

static void test_defaults_survive_a_json_round_trip(void)
{
    s_p.active = 3;
    s_p.cycle_enabled = true;
    s_p.cycle_interval_s = 120;
    s_p.presets[3].clock = UI_CLOCK_12H;
    s_p.presets[3].seconds = true;
    s_p.presets[3].stale_policy = UI_STALE_HIDE;
    s_p.presets[3].status_battery = UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
    s_p.presets[3].status_clock = true;
    s_p.presets[3].status_sync = true;
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
}

static void test_the_spec_example_parses(void)
{
    const char *json = "{ \"schema\": 1, \"active\": \"home\", \"cycle\": { \"enabled\": false, \"interval_s\": 60 },"
                       "  \"presets\": [ { \"id\": \"home\", \"name\": \"Home\", \"layout\": \"classic\", \"in_cycle\": true,"
                       "    \"slots\": { \"main\": \"time.clock\", \"sub\": \"date.day\", \"s1\": \"env.temp\","
                       "                 \"s2\": \"env.hum\", \"s3\": \"wx.now\", \"s4\": \"bat.level\" },"
                       "    \"options\": { \"clock_24h\": true, \"seconds\": false, \"invert\": false,"
                       "                   \"stale_policy\": \"stale\" } } ] }";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT(1, s_p.count);
    TEST_ASSERT_EQUAL(UI_FIELD_WX_NOW, s_p.presets[0].slots[4]);
    TEST_ASSERT_EQUAL(UI_CLOCK_24H, s_p.presets[0].clock);
}

static void check_rejected(const char *json, const char *reason_part)
{
    s_err[0] = '\0';
    TEST_ASSERT_FALSE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), json);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_err, reason_part), s_err);
}

static void test_invalid_files_are_rejected_with_a_reason(void)
{
    check_rejected("not json", "not valid JSON");
    check_rejected("{\"schema\": 2, \"presets\": []}", "schema");
    check_rejected("{\"schema\": 1, \"presets\": []}", "1-16");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"round\"}]}", "unknown layout");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"Big Id\", \"layout\": \"grid\"}]}", "preset id");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"slots\": {\"g7\": \"env.temp\"}}]}",
                   "no slot \"g7\"");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"slots\": {\"g1\": \"env.cold\"}}]}",
                   "unknown field");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"classic\", \"slots\": {\"main\": \"bat.level\"}}]}",
                   "can't show");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\"}, {\"id\": \"a\", \"layout\": \"grid\"}]}",
                   "duplicate");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"options\": {\"stale_policy\": \"x\"}}]}",
                   "stale_policy");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"options\": {\"status_battery\": [\"amps\"]}}]}",
                   "status_battery");
}

static void test_too_many_presets_are_rejected(void)
{
    size_t n = (size_t)snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"presets\": [");
    for (int i = 0; i < UI_PRESET_MAX + 1; i++) {
        n += (size_t)snprintf(s_json + n, sizeof(s_json) - n, "%s{\"id\": \"p%d\", \"layout\": \"grid\"}", i ? "," : "", i);
    }
    snprintf(s_json + n, sizeof(s_json) - n, "]}");
    check_rejected(s_json, "1-16");
}

static void test_lenient_parts_fall_back_to_defaults(void)
{
    const char *json = "{\"schema\": 1, \"active\": \"gone\", \"cycle\": {\"enabled\": true, \"interval_s\": 3},"
                       " \"presets\": [{\"id\": \"a\", \"layout\": \"focus\", \"slots\": {\"s1\": null, \"s2\": \"\"}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT(0, s_p.active);                 /* unknown active: the first */
    TEST_ASSERT_EQUAL_UINT16(UI_CYCLE_MIN_S, s_p.cycle_interval_s); /* clamped to 10 s */
    TEST_ASSERT_TRUE(s_p.presets[0].in_cycle);
    TEST_ASSERT_EQUAL_STRING("a", s_p.presets[0].name);  /* no name: the id */
    TEST_ASSERT_EQUAL(UI_CLOCK_DEFAULT, s_p.presets[0].clock);
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, s_p.presets[0].slots[1]);
    TEST_ASSERT_FALSE(s_p.presets[0].status_clock);
    TEST_ASSERT_EQUAL_HEX8(UI_STATUS_BAT_PERCENT, s_p.presets[0].status_battery);
}

static void test_status_bar_options_parse(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"options\":"
                       " {\"status_clock\": true, \"status_battery\": [\"days\", \"voltage\"]}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_TRUE(s_p.presets[0].status_clock);
    TEST_ASSERT_EQUAL_HEX8(UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS, s_p.presets[0].status_battery);
    TEST_ASSERT_FALSE(s_p.presets[0].status_sync);
}

static void test_status_sync_round_trips_and_is_written_only_when_on(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"options\":"
                       " {\"status_sync\": true}}, {\"id\": \"b\", \"layout\": \"grid\"}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_TRUE(s_p.presets[0].status_sync);
    TEST_ASSERT_FALSE(s_p.presets[1].status_sync);
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    const char *first = strstr(s_json, "\"status_sync\"");
    TEST_ASSERT_NOT_NULL_MESSAGE(first, s_json);
    TEST_ASSERT_NULL_MESSAGE(strstr(first + 1, "\"status_sync\""), s_json); /* b's is off: left out */
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
    ui_presets_defaults(&s_p);
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NULL_MESSAGE(strstr(s_json, "status_sync"), s_json);
}

static void test_json_that_does_not_fit_the_buffer_returns_zero(void)
{
    TEST_ASSERT_EQUAL_UINT(0, ui_presets_to_json(&s_p, s_json, 64));
}

static const char *k_with_schedule =
    "{ \"schema\": 1, \"presets\": [ { \"id\": \"home\", \"layout\": \"classic\" },"
    "                                { \"id\": \"focus\", \"layout\": \"focus\" } ],"
    "  \"schedule\": { \"enabled\": true, \"entries\": ["
    "    { \"at\": \"22:30\", \"days\": 127, \"action\": \"preset\", \"preset\": \"focus\" },"
    "    { \"at\": \"23:00\", \"days\": 31, \"action\": \"night\", \"until\": \"06:00\" } ] } }";

static void test_the_spec_schedule_parses(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_with_schedule, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_TRUE(s_p.schedule.enabled);
    TEST_ASSERT_EQUAL_INT(2, s_p.schedule.count);
    const ui_schedule_entry_t *e = &s_p.schedule.entries[0];
    TEST_ASSERT_EQUAL_INT(22 * 60 + 30, e->at_min);
    TEST_ASSERT_EQUAL_HEX8(0x7F, e->days);
    TEST_ASSERT_EQUAL(UI_SCHED_PRESET, e->action);
    TEST_ASSERT_EQUAL_INT(1, e->preset);
    e = &s_p.schedule.entries[1];
    TEST_ASSERT_EQUAL(UI_SCHED_NIGHT, e->action);
    TEST_ASSERT_EQUAL_INT(23 * 60, e->at_min);
    TEST_ASSERT_EQUAL_INT(6 * 60, e->until_min);
    TEST_ASSERT_EQUAL_HEX8(0x1F, e->days); /* Monday to Friday */
}

static void test_a_schedule_survives_a_round_trip(void)
{
    TEST_ASSERT_TRUE(ui_presets_from_json(k_with_schedule, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
}

static void test_bad_schedules_are_rejected_with_a_reason(void)
{
    const char *base = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\"}], \"schedule\": ";
    const struct {
        const char *schedule, *reason;
    } k_cases[] = {
        { "{\"entries\": [{\"at\": \"25:00\", \"action\": \"night\", \"until\": \"06:00\"}]}", "at" },
        { "{\"entries\": [{\"at\": \"7:5\", \"action\": \"night\", \"until\": \"06:00\"}]}", "at" },
        { "{\"entries\": [{\"at\": \"22:00\", \"action\": \"dance\"}]}", "action" },
        { "{\"entries\": [{\"at\": \"22:00\", \"action\": \"preset\", \"preset\": \"gone\"}]}", "preset" },
        { "{\"entries\": [{\"at\": \"22:00\", \"action\": \"night\"}]}", "until" },
        { "{\"entries\": [{\"at\": \"22:00\", \"action\": \"night\", \"until\": \"22:00\"}]}", "another time" },
        { "{\"entries\": {}}", "list" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        snprintf(s_json, sizeof(s_json), "%s%s}", base, k_cases[i].schedule);
        check_rejected(s_json, k_cases[i].reason);
    }
    size_t n = (size_t)snprintf(s_json, sizeof(s_json), "%s{\"entries\": [", base);
    for (int i = 0; i <= UI_SCHEDULE_MAX; i++) {
        n += (size_t)snprintf(s_json + n, sizeof(s_json) - n,
                              "%s{\"at\": \"0%d:00\", \"action\": \"night\", \"until\": \"09:00\"}", i ? "," : "", i);
    }
    snprintf(s_json + n, sizeof(s_json) - n, "]}}");
    check_rejected(s_json, "at most");
}

static void test_a_missing_days_mask_means_every_day(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\"}],"
                       " \"schedule\": {\"entries\": [{\"at\": \"23:00\", \"action\": \"night\","
                       " \"until\": \"06:00\"}]}}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_FALSE(s_p.schedule.enabled); /* entries alone don't switch it on */
    TEST_ASSERT_EQUAL_HEX8(0x7F, s_p.schedule.entries[0].days);
}

static void test_a_long_name_is_cut_at_a_character(void)
{
    /* 24 bytes: the 23-byte limit falls inside the last "ř" (2 bytes), which must not be split */
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\","
                       " \"name\": \"Obývák a pracovna ř\\u0159\"}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    size_t n = strlen(s_p.presets[0].name);
    TEST_ASSERT_TRUE(n <= UI_PRESET_NAME_LEN - 1);
    TEST_ASSERT_TRUE((s_p.presets[0].name[n - 1] & 0xC0) != 0xC0); /* no dangling lead byte */
    TEST_ASSERT_EQUAL_STRING_LEN("Obývák a pracovna", s_p.presets[0].name, 18);
}

static void test_null_options_take_their_defaults(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\","
                       " \"options\": {\"stale_policy\": null, \"status_battery\": null}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL(UI_STALE_STALE, s_p.presets[0].stale_policy);
    TEST_ASSERT_EQUAL_HEX8(UI_STATUS_BAT_PERCENT, s_p.presets[0].status_battery);
}

static void test_slots_given_as_a_list_are_rejected(void)
{
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"slots\": [\"env.temp\"]}]}",
                   "slots");
}

static void test_deep_nesting_is_rejected_before_parsing(void)
{
    size_t n = (size_t)snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"deep\": ");
    for (int i = 0; i < 40; i++) {
        s_json[n++] = '[';
    }
    for (int i = 0; i < 40; i++) {
        s_json[n++] = ']';
    }
    snprintf(s_json + n, sizeof(s_json) - n, ", \"presets\": [{\"id\": \"a\", \"layout\": \"grid\"}]}");
    check_rejected(s_json, "nested");
}

static void test_a_full_set_fits_the_save_buffer(void)
{
    memset(&s_p, 0, sizeof(s_p));
    for (int i = 0; i < UI_PRESET_MAX; i++) {
        ui_preset_t *p = &s_p.presets[i];
        snprintf(p->id, sizeof(p->id), "preset-%08d", i);
        snprintf(p->name, sizeof(p->name), "Předvolba číslo %04d", i); /* 23 bytes: the longest name */
        p->layout = UI_LAYOUT_GRID;
        for (int k = 0; k < 6; k++) {
            p->slots[k] = (uint8_t)(UI_FIELD_ENV_TEMP + k);
        }
        p->clock = UI_CLOCK_12H;
        p->status_battery = UI_STATUS_BAT_PERCENT | UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
    }
    s_p.count = UI_PRESET_MAX;
    s_p.cycle_interval_s = 60;
    s_p.offered = UI_OFFERED_ALL;
    s_p.schedule.count = UI_SCHEDULE_MAX;
    for (int i = 0; i < UI_SCHEDULE_MAX; i++) {
        s_p.schedule.entries[i] = (ui_schedule_entry_t){ .at_min = 600, .days = 0x7F, .action = UI_SCHED_NIGHT,
                                                          .until_min = 700 };
    }
    static char buf[UI_PRESETS_JSON_MAX];
    size_t n = ui_presets_to_json(&s_p, buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "the worst case must fit UI_PRESETS_JSON_MAX");
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(buf, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
}


/* Spec §5.2's example as presets.json has it (spec §5.4): Weather with smaller bottom cells. */
#define SPLIT_WEATHER                                                                                              \
    "{\"schema\": 1, \"presets\": [{\"id\": \"wx\", \"name\": \"Weather\", \"layout\": \"split\", \"split\": "        \
    "{\"split\": \"rows\", \"ratio\": \"3/4\", \"line\": true,"                                                      \
    " \"a\": {\"split\": \"columns\", \"ratio\": \"1/2\","                                                           \
    "        \"a\": {\"field\": \"wx.now\"},"                                                                        \
    "        \"b\": {\"split\": \"rows\", \"ratio\": \"1/2\", \"a\": {\"field\": \"wx.today\"},"                      \
    "               \"b\": {\"field\": \"wx.hourly\"}}},"                                                            \
    " \"b\": {\"split\": \"columns\", \"ratio\": \"1/2\", \"line\": false, \"a\": {\"field\": \"env.temp\"},"         \
    "        \"b\": {\"field\": \"env.hum\"}}}}]}"

static void test_the_spec_split_example_parses(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(SPLIT_WEATHER, &s_p, s_err, sizeof(s_err)), s_err);
    const ui_preset_t *p = &s_p.presets[0];
    TEST_ASSERT_EQUAL(UI_LAYOUT_SPLIT, p->layout);
    static const uint8_t k_tree[UI_SPLIT_NODES] = { UI_RATIO_3_4, UI_RATIO_1_2 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_2, 0,
                                                    0, UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_tree, p->split, UI_SPLIT_NODES); /* a missing line is shown */
    static const uint8_t k_fields[UI_SLOT_MAX] = { UI_FIELD_WX_NOW, UI_FIELD_WX_TODAY, UI_FIELD_WX_HOURLY,
                                                   UI_FIELD_ENV_TEMP, UI_FIELD_ENV_HUM };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_fields, p->slots, UI_SLOT_MAX); /* the cells' fields, in preorder */
    TEST_ASSERT_EQUAL_INT(5, ui_preset_slots(p));
}

static void test_a_split_preset_survives_a_round_trip(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(SPLIT_WEATHER, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"layout\":\"split\",\"in_cycle\":true,\"split\":{\"split\":\"rows\","
                                                "\"ratio\":\"3/4\",\"line\":true,\"a\":{\"split\":\"columns\""),
                                 s_json);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"line\":false,\"a\":{\"field\":\"env.temp\"},\"b\":{\"field\":"
                                                "\"env.hum\"}}},\"options\""),
                                 s_json);
    TEST_ASSERT_NULL_MESSAGE(strstr(s_json, "\"slots\""), s_json); /* a split preset has its tree instead */
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
}

static void test_a_split_preset_without_a_tree_is_one_empty_cell(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"split\"},"
                       " {\"id\": \"b\", \"layout\": \"split\", \"split\": {\"field\": \"time.clock\"}},"
                       " {\"id\": \"c\", \"layout\": \"split\", \"split\": {}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT(1, ui_preset_slots(&s_p.presets[0]));
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, s_p.presets[0].slots[0]);
    TEST_ASSERT_EQUAL(UI_FIELD_TIME_CLOCK, s_p.presets[1].slots[0]); /* 400×279: the clock at XL */
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, s_p.presets[2].slots[0]);
}

/* A cell of the tree as JSON: `field` or empty; a split of two parts. */
static int put_split(char *out, size_t size, const char *split, const char *ratio, const char *a, const char *b)
{
    return snprintf(out, size, "{\"split\": \"%s\", \"ratio\": \"%s\", \"a\": %s, \"b\": %s}", split, ratio, a, b);
}

/* presets.json with one split preset of `tree`, in s_json. */
static const char *split_file(const char *tree)
{
    snprintf(s_json, sizeof(s_json),
             "{\"schema\": 1, \"presets\": [{\"id\": \"t\", \"layout\": \"split\", \"split\": %s}]}", tree);
    return s_json;
}

static void check_tree_rejected(const char *tree, const char *reason_part)
{
    check_rejected(split_file(tree), reason_part);
}

/* Eight columns of halves, a row of cells 50 or 49 px wide; `first` is the leftmost cell. */
static void eight_columns(char *out, size_t size, const char *first)
{
    static char two[512], two_first[512], four[1024], four_first[1024];
    put_split(two, sizeof(two), "columns", "1/2", "{}", "{}");
    put_split(two_first, sizeof(two_first), "columns", "1/2", first, "{}");
    put_split(four, sizeof(four), "columns", "1/2", two, two);
    put_split(four_first, sizeof(four_first), "columns", "1/2", two_first, two);
    put_split(out, size, "columns", "1/2", four_first, four);
}

static void test_bad_split_trees_are_rejected_with_a_reason(void)
{
    static char tree[8192], part[1024], row[2048], row25[2048], rows[4096];
    /* 25 cells, each at least 40×20: three rows of eight, one of them split again (M6c) */
    put_split(part, sizeof(part), "rows", "1/2", "{}", "{}");
    eight_columns(row, sizeof(row), "{}");
    eight_columns(row25, sizeof(row25), part);
    put_split(rows, sizeof(rows), "rows", "1/2", row, row);
    put_split(tree, sizeof(tree), "rows", "1/3", row25, rows);
    check_tree_rejected(tree, "at most 24 cells");
    put_split(tree, sizeof(tree), "rows", "1/3", row, rows); /* 24 are fine */
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(split_file(tree), &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT(24, ui_preset_slots(&s_p.presets[0]));

    put_split(part, sizeof(part), "rows", "1/4", "{}", "{}");
    put_split(tree, sizeof(tree), "rows", "1/4", part, "{}"); /* 69 px, a quarter of it: 17 */
    check_tree_rejected(tree, "40×20");
    check_tree_rejected("{\"split\": \"diagonal\", \"ratio\": \"1/2\", \"a\": {}, \"b\": {}}", "rows or columns");
    check_tree_rejected("{\"split\": \"rows\", \"ratio\": \"2/5\", \"a\": {}, \"b\": {}}", "1/4, 1/3, 1/2, 2/3 or 3/4");
    check_tree_rejected("{\"split\": \"rows\", \"ratio\": \"1/2\", \"a\": {}}", "both parts");
    check_tree_rejected("{\"split\": \"rows\", \"ratio\": \"1/2\", \"a\": {}, \"b\": \"env.temp\"}", "both parts");
    check_tree_rejected("{\"field\": \"env.cold\"}", "unknown field");
    check_tree_rejected("{\"field\": 7}", "field id");
    check_tree_rejected("[]", "split must be");
    /* the rain map needs M: the bottom cell is 400×69 */
    put_split(tree, sizeof(tree), "rows", "3/4", "{\"field\": \"time.clock\"}", "{\"field\": \"rain.map\"}");
    check_tree_rejected(tree, "cell 2 (400×69) can't show rain.map");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"split\","
                   " \"slots\": {\"main\": \"env.temp\"}}]}",
                   "no slot \"main\"");
}

#define C (UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE) /* a hidden column split: the longest node written */
#define EIGHT_COLUMNS C, C, C, 0, 0, C, 0, 0, C, C, 0, 0, C, 0, 0

/* The largest presets.json there can be (M6c, D42): 16 split presets of 24 cells, three rows of eight columns, the
 * most columns a tree can have, with every line hidden; the longest field ids a cell takes, a PC reading's with
 * 15 characters (M7); names of control
 * characters, which cJSON writes as six bytes each ("\u0001"); every option at its longest; 8 schedule entries
 * that switch to a preset; every cell's title forced, and the pool full of one-quote titles. */
static void test_a_full_set_of_split_presets_fits_the_save_buffer(void)
{
    static const uint8_t k_tree[UI_SPLIT_NODES] = { UI_RATIO_1_3 | UI_SPLIT_NO_LINE, EIGHT_COLUMNS,
                                                    UI_RATIO_1_2 | UI_SPLIT_NO_LINE, EIGHT_COLUMNS, EIGHT_COLUMNS };
    memset(&s_p, 0, sizeof(s_p));
    for (int i = 0; i < UI_PRESET_MAX; i++) {
        ui_preset_t *p = &s_p.presets[i];
        snprintf(p->id, sizeof(p->id), "preset-%08d", i);
        memset(p->name, 0x01, UI_PRESET_NAME_LEN - 1);
        p->layout = UI_LAYOUT_SPLIT;
        memcpy(p->split, k_tree, sizeof(k_tree));
        for (int k = 0; k < UI_SPLIT_CELLS; k++) { /* the table's 32 ids, in the order the file names them */
            char id[PC_ID_LEN];
            snprintf(id, sizeof(id), "reading_%07d", (i * UI_SPLIT_CELLS + k) % UI_PC_FIELDS);
            p->slots[k] = (uint8_t)ui_presets_pc_field(&s_p, id);
            TEST_ASSERT_TRUE(ui_field_is_pc((ui_field_id_t)p->slots[k]));
        }
        p->clock = UI_CLOCK_12H;
        p->stale_policy = UI_STALE_PLACEHOLDER;
        p->status_battery = UI_STATUS_BAT_PERCENT | UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
        TEST_ASSERT_TRUE(ui_window_make("sunrise-180", "sunset-180", 0x7E, &p->window, s_err, sizeof(s_err))); /* M6e */
        p->force_title = (1u << UI_SPLIT_CELLS) - 1; /* D42: every cell's title forced */
        p->status_sync = true; /* D43: written only when on */
    }
    /* D42: as many titles as the pool holds, each a quote, which cJSON writes as two bytes: the most per byte */
    int titled = 0;
    while (ui_presets_set_title(&s_p, titled / UI_SPLIT_CELLS, titled % UI_SPLIT_CELLS, "\"")) {
        titled++;
    }
    TEST_ASSERT_EQUAL_INT(UI_TITLES_BYTES / 4, titled); /* 4 bytes each */
    s_p.count = UI_PRESET_MAX;
    s_p.cycle_interval_s = UI_CYCLE_MAX_S;
    s_p.offered = UI_OFFERED_ALL;
    s_p.schedule.count = UI_SCHEDULE_MAX;
    for (int i = 0; i < UI_SCHEDULE_MAX; i++) {
        s_p.schedule.entries[i] = (ui_schedule_entry_t){ .at_min = 600, .days = 0x7F, .action = UI_SCHED_PRESET,
                                                          .preset = (uint8_t)i };
    }
    static char buf[UI_PRESETS_JSON_MAX];
    size_t n = ui_presets_to_json(&s_p, buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "the worst case must fit UI_PRESETS_JSON_MAX");
    char size_msg[48];
    snprintf(size_msg, sizeof(size_msg), "the largest presets.json: %zu bytes", n);
    TEST_MESSAGE(size_msg);
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(buf, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
}

/* The deepest tree there can be (M6c): 13 splits in a chain, seven rows then six columns, each leaving a cell
 * beside the next split; its last cells are 41×20. Its file nests 17 levels, under UI_JSON_MAX_DEPTH. */
static void test_the_deepest_tree_fits_the_nesting_limit(void)
{
    static const uint8_t k_tree[UI_SPLIT_NODES] = {
        UI_RATIO_1_4, 0, UI_RATIO_1_4, 0, UI_RATIO_1_4, 0, UI_RATIO_1_4, 0, UI_RATIO_1_4, 0, UI_RATIO_1_3, 0,
        UI_RATIO_1_2, 0,
        UI_RATIO_1_4 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_4 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_4 | UI_SPLIT_COLUMNS, 0,
        UI_RATIO_1_4 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_2 | UI_SPLIT_COLUMNS, 0, 0,
    };
    ui_split_geometry_t g;
    TEST_ASSERT_TRUE(ui_split_layout(k_tree, ui_split_area(), &g));
    TEST_ASSERT_EQUAL_INT(14, g.cells);
    TEST_ASSERT_EQUAL_INT(41, g.cell[13].w);
    TEST_ASSERT_EQUAL_INT(20, g.cell[13].h);
    memset(&s_p, 0, sizeof(s_p));
    ui_preset_t *p = &s_p.presets[0];
    snprintf(p->id, sizeof(p->id), "deep");
    snprintf(p->name, sizeof(p->name), "Deep");
    p->layout = UI_LAYOUT_SPLIT;
    memcpy(p->split, k_tree, sizeof(k_tree));
    p->slots[13] = UI_FIELD_ENV_TEMP;
    s_p.count = 1;
    static char buf[UI_PRESETS_JSON_MAX];
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_INT(17, util_json_depth(buf));
    TEST_ASSERT_TRUE(17 <= UI_JSON_MAX_DEPTH);
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(buf, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(k_tree, back.presets[0].split, UI_SPLIT_NODES);
}

static void test_a_preset_counts_the_slots_its_layout_uses(void)
{
    TEST_ASSERT_EQUAL_INT(6, ui_preset_slots(&s_p.presets[0])); /* Home: Classic */
    TEST_ASSERT_EQUAL_INT(0, ui_preset_slots(&s_p.presets[ui_presets_find(&s_p, "rain")]));
    ui_preset_t split = { .layout = UI_LAYOUT_SPLIT };
    TEST_ASSERT_EQUAL_INT(1, ui_preset_slots(&split));
    memset(split.split, UI_RATIO_1_2, sizeof(split.split)); /* cut short: no cells */
    TEST_ASSERT_EQUAL_INT(0, ui_preset_slots(&split));
}

/* ---- D42 (spec §5.4): titles of slots and cells ---- */

#define TITLED                                                                                                        \
    "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"classic\", \"slots\": {\"main\": \"time.clock\","     \
    " \"sub\": \"date.day\"}, \"titles\": {\"sub\": \"Dnes\"}},"                                                       \
    " {\"id\": \"s\", \"layout\": \"split\", \"split\": {\"split\": \"rows\", \"ratio\": \"1/2\","                     \
    " \"a\": {\"field\": \"env.temp\", \"title\": \"Obývák\", \"force_title\": true},"                                 \
    " \"b\": {\"field\": \"env.hum\", \"force_title\": true}}}]}"

static void test_titles_round_trip(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(TITLED, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_STRING("Dnes", ui_presets_title(&s_p, 0, 1));
    TEST_ASSERT_NULL(ui_presets_title(&s_p, 0, 0));
    TEST_ASSERT_EQUAL_STRING("Obývák", ui_presets_title(&s_p, 1, 0));
    TEST_ASSERT_NULL(ui_presets_title(&s_p, 1, 1)); /* forced, with the field's own label */
    TEST_ASSERT_EQUAL_HEX32(0x3, s_p.presets[1].force_title);
    TEST_ASSERT_EQUAL_HEX32(0, s_p.presets[0].force_title);
    ui_titles_t t;
    ui_presets_titles(&s_p, 1, &t);
    TEST_ASSERT_EQUAL_STRING("Obývák", t.text[0]);
    TEST_ASSERT_NULL(t.text[1]);
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"slots\":{\"main\":\"time.clock\",\"sub\":\"date.day\"},"
                                                "\"titles\":{\"sub\":\"Dnes\"}"),
                                 s_json);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"a\":{\"field\":\"env.temp\",\"title\":\"Obývák\","
                                                "\"force_title\":true},\"b\":{\"field\":\"env.hum\","
                                                "\"force_title\":true}"),
                                 s_json);
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
}

/* A preset name's rule: 23 bytes at most, cut where a letter starts. */
static void test_titles_are_cut_at_a_letter(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"s\", \"layout\": \"split\", \"split\":"
                       " {\"field\": \"env.temp\", \"title\": \"řřřřřřřřřřřř\"}}]}"; /* 24 bytes */
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_STRING("řřřřřřřřřřř", ui_presets_title(&s_p, 0, 0)); /* 22 bytes */
}

static void test_title_control_characters_become_spaces(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"s\", \"layout\": \"split\", \"split\":"
                       " {\"field\": \"env.temp\", \"title\": \"a\\tb\\nc\"}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_STRING("a b c", ui_presets_title(&s_p, 0, 0));
}

/* Empty titles are none, and a file without titles has none of their keys. */
static void test_titles_unset_are_not_written(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"classic\", \"titles\":"
                       " {\"main\": \"\"}}, {\"id\": \"s\", \"layout\": \"split\", \"split\": {\"field\": \"env.temp\","
                       " \"title\": \"\", \"force_title\": false}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_NULL(ui_presets_title(&s_p, 0, 0));
    TEST_ASSERT_NULL(ui_presets_title(&s_p, 1, 0));
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NULL_MESSAGE(strstr(s_json, "title"), s_json);
    ui_presets_defaults(&s_p);
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NULL_MESSAGE(strstr(s_json, "title"), s_json);
}

static void test_bad_titles_are_rejected_with_a_reason(void)
{
    const char *cases[][2] = {
        { "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"classic\", \"titles\": {\"nope\": \"x\"}}]}",
          "no slot" },
        { "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"classic\", \"titles\": [\"x\"]}]}",
          "titles" },
        { "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"classic\", \"titles\": {\"main\": 5}}]}",
          "title" },
        { "{\"schema\": 1, \"presets\": [{\"id\": \"s\", \"layout\": \"split\", \"split\": {\"title\": 5}}]}",
          "title" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        s_err[0] = '\0';
        TEST_ASSERT_FALSE_MESSAGE(ui_presets_from_json(cases[i][0], &s_p, s_err, sizeof(s_err)), cases[i][0]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_err, cases[i][1]), s_err);
    }
}

/* Every preset's titles share UI_TITLES_BYTES: a file with more is refused, saying so. */
static void test_titles_overflowing_the_pool_are_refused(void)
{
    static char json[UI_PRESETS_JSON_MAX];
    int n = snprintf(json, sizeof(json), "{\"schema\": 1, \"presets\": [");
    for (int i = 0; i < UI_PRESET_MAX; i++) { /* 16 × 2 titles of 23 bytes: 832 bytes with their 3 each */
        n += snprintf(json + n, sizeof(json) - (size_t)n,
                      "%s{\"id\": \"p%d\", \"layout\": \"classic\", \"titles\": {\"main\": \"%023d\","
                      " \"sub\": \"%023d\"}}",
                      i ? ", " : "", i, i, i);
    }
    snprintf(json + n, sizeof(json) - (size_t)n, "]}");
    TEST_ASSERT_FALSE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_err, "titles: 768 bytes at most"), s_err);
}

static void test_set_title_keeps_order_and_clears(void)
{
    TEST_ASSERT_TRUE(ui_presets_set_title(&s_p, 2, 3, "c"));
    TEST_ASSERT_TRUE(ui_presets_set_title(&s_p, 0, 5, "a"));
    TEST_ASSERT_TRUE(ui_presets_set_title(&s_p, 2, 1, "b"));
    TEST_ASSERT_EQUAL_STRING("a", ui_presets_title(&s_p, 0, 5));
    TEST_ASSERT_EQUAL_STRING("b", ui_presets_title(&s_p, 2, 1));
    TEST_ASSERT_EQUAL_STRING("c", ui_presets_title(&s_p, 2, 3));
    TEST_ASSERT_EQUAL_UINT8(0, s_p.titles[0]); /* in preset and slot order: (0, 5) first */
    TEST_ASSERT_EQUAL_UINT8(5, s_p.titles[1]);
    TEST_ASSERT_TRUE(ui_presets_set_title(&s_p, 2, 1, "longer")); /* overwritten in place */
    TEST_ASSERT_EQUAL_STRING("longer", ui_presets_title(&s_p, 2, 1));
    TEST_ASSERT_EQUAL_STRING("c", ui_presets_title(&s_p, 2, 3));
    TEST_ASSERT_TRUE(ui_presets_set_title(&s_p, 0, 5, ""));
    TEST_ASSERT_NULL(ui_presets_title(&s_p, 0, 5));
    TEST_ASSERT_TRUE(ui_presets_set_title(&s_p, 2, 1, NULL));
    TEST_ASSERT_NULL(ui_presets_title(&s_p, 2, 1));
    TEST_ASSERT_EQUAL_STRING("c", ui_presets_title(&s_p, 2, 3));
    ui_presets_t empty;
    ui_presets_defaults(&empty);
    TEST_ASSERT_TRUE(ui_presets_set_title(&empty, 2, 3, "c"));
    TEST_ASSERT_EQUAL_MEMORY(empty.titles, s_p.titles, UI_TITLES_BYTES); /* what is cleared is gone, zeros after */
}

/* Full: 32 titles of 21 bytes take all 768 bytes (24 each); one more is refused, and so is a title that grows,
 * the pool unchanged; one that shrinks fits. */
static void test_set_title_refuses_what_overflows_the_pool(void)
{
    int fitted = 0;
    while (fitted < 40 &&
           ui_presets_set_title(&s_p, fitted / UI_SLOT_MAX, fitted % UI_SLOT_MAX, "123456789012345678901")) {
        fitted++;
    }
    TEST_ASSERT_EQUAL_INT(32, fitted);
    uint8_t before[UI_TITLES_BYTES];
    memcpy(before, s_p.titles, sizeof(before));
    TEST_ASSERT_FALSE(ui_presets_set_title(&s_p, 5, 0, "1"));
    TEST_ASSERT_FALSE(ui_presets_set_title(&s_p, 0, 0, "1234567890123456789012")); /* one byte longer, in place */
    TEST_ASSERT_EQUAL_MEMORY(before, s_p.titles, sizeof(before));
    TEST_ASSERT_TRUE(ui_presets_set_title(&s_p, 0, 0, "1234567890"));
    TEST_ASSERT_EQUAL_STRING("1234567890", ui_presets_title(&s_p, 0, 0));
    TEST_ASSERT_EQUAL_STRING("123456789012345678901", ui_presets_title(&s_p, 1, 7)); /* the last one */
}

/* A built-in offered at the end takes the next index: the titles before it stay where they are. */
static void test_offer_builtins_keeps_titles(void)
{
    s_p.count = 4;
    s_p.offered = 0;
    TEST_ASSERT_TRUE(ui_presets_set_title(&s_p, 1, 2, "Kept"));
    TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p));
    TEST_ASSERT_EQUAL_STRING("Kept", ui_presets_title(&s_p, 1, 2));
    for (int i = 4; i < s_p.count; i++) {
        TEST_ASSERT_NULL(ui_presets_title(&s_p, i, 0));
    }
}

static void test_defaults_have_no_titles(void)
{
    for (int i = 0; i < s_p.count; i++) {
        TEST_ASSERT_EQUAL_HEX32(0, s_p.presets[i].force_title);
        for (int k = 0; k < UI_SLOT_MAX; k++) {
            TEST_ASSERT_NULL(ui_presets_title(&s_p, i, k));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_the_eight_built_ins);
    RUN_TEST(test_next_follows_cycle_order_and_skips_presets_out_of_it);
    RUN_TEST(test_the_cycle_visits_flights_only_in_sync_mode_always);
    RUN_TEST(test_the_auto_cycle_skips_a_closed_window);
    RUN_TEST(test_the_auto_cycle_keeps_the_preset_when_none_is_open);
    RUN_TEST(test_the_built_in_solar_preset_has_a_daylight_window);
    RUN_TEST(test_a_window_survives_a_round_trip);
    RUN_TEST(test_a_window_without_days_opens_every_day);
    RUN_TEST(test_bad_windows_are_rejected_with_a_reason);
    RUN_TEST(test_the_longest_reason_fits_ui_preset_err_len);
    RUN_TEST(test_a_file_from_before_m6_gains_the_radars_once);
    RUN_TEST(test_a_file_from_m6c_gains_solar_and_energy_once);
    RUN_TEST(test_the_radars_need_room_and_a_free_id);
    RUN_TEST(test_the_solar_layouts_have_no_slots);
    RUN_TEST(test_the_radar_layouts_have_no_slots_and_the_rain_map_needs_room);
    RUN_TEST(test_defaults_survive_a_json_round_trip);
    RUN_TEST(test_the_spec_example_parses);
    RUN_TEST(test_invalid_files_are_rejected_with_a_reason);
    RUN_TEST(test_too_many_presets_are_rejected);
    RUN_TEST(test_lenient_parts_fall_back_to_defaults);
    RUN_TEST(test_status_bar_options_parse);
    RUN_TEST(test_json_that_does_not_fit_the_buffer_returns_zero);
    RUN_TEST(test_the_spec_schedule_parses);
    RUN_TEST(test_a_schedule_survives_a_round_trip);
    RUN_TEST(test_bad_schedules_are_rejected_with_a_reason);
    RUN_TEST(test_a_missing_days_mask_means_every_day);
    RUN_TEST(test_a_long_name_is_cut_at_a_character);
    RUN_TEST(test_null_options_take_their_defaults);
    RUN_TEST(test_slots_given_as_a_list_are_rejected);
    RUN_TEST(test_deep_nesting_is_rejected_before_parsing);
    RUN_TEST(test_a_full_set_fits_the_save_buffer);
    RUN_TEST(test_the_spec_split_example_parses);
    RUN_TEST(test_a_split_preset_survives_a_round_trip);
    RUN_TEST(test_a_split_preset_without_a_tree_is_one_empty_cell);
    RUN_TEST(test_bad_split_trees_are_rejected_with_a_reason);
    RUN_TEST(test_a_full_set_of_split_presets_fits_the_save_buffer);
    RUN_TEST(test_the_deepest_tree_fits_the_nesting_limit);
    RUN_TEST(test_a_preset_counts_the_slots_its_layout_uses);
    RUN_TEST(test_titles_round_trip);
    RUN_TEST(test_titles_are_cut_at_a_letter);
    RUN_TEST(test_title_control_characters_become_spaces);
    RUN_TEST(test_titles_unset_are_not_written);
    RUN_TEST(test_status_sync_round_trips_and_is_written_only_when_on);
    RUN_TEST(test_bad_titles_are_rejected_with_a_reason);
    RUN_TEST(test_titles_overflowing_the_pool_are_refused);
    RUN_TEST(test_set_title_keeps_order_and_clears);
    RUN_TEST(test_set_title_refuses_what_overflows_the_pool);
    RUN_TEST(test_offer_builtins_keeps_titles);
    RUN_TEST(test_defaults_have_no_titles);
    RUN_TEST(test_titles_round_trip);
    RUN_TEST(test_titles_are_cut_at_a_letter);
    RUN_TEST(test_title_control_characters_become_spaces);
    RUN_TEST(test_titles_unset_are_not_written);
    RUN_TEST(test_bad_titles_are_rejected_with_a_reason);
    RUN_TEST(test_titles_overflowing_the_pool_are_refused);
    RUN_TEST(test_set_title_keeps_order_and_clears);
    RUN_TEST(test_set_title_refuses_what_overflows_the_pool);
    RUN_TEST(test_offer_builtins_keeps_titles);
    RUN_TEST(test_defaults_have_no_titles);
    return UNITY_END();
}
