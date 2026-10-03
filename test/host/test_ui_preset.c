#include <stdio.h>
#include <string.h>

#include "ui_fields.h"
#include "ui_preset.h"
#include "ui_split.h"
#include "unity.h"

static ui_presets_t s_p;
static char s_err[128];
static char s_json[UI_PRESETS_JSON_MAX];

void setUp(void)
{
    ui_presets_defaults(&s_p);
    s_err[0] = '\0';
}

void tearDown(void) {}

static void test_defaults_are_the_six_built_ins(void)
{
    TEST_ASSERT_EQUAL_INT(6, s_p.count);
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
    TEST_ASSERT_EQUAL_UINT8(UI_OFFERED_ALL, s_p.offered); /* nothing left to add */
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "nope"));
}

/* cmd/preset (spec §12.4): HA's select sends a preset's name, as its options are the names; an automation or
 * the console may send an id. Names first, so the select always gets the preset it shows. */
static void test_a_preset_is_found_by_its_name_or_its_id(void)
{
    TEST_ASSERT_EQUAL_INT(4, ui_presets_lookup(&s_p, "Rain radar"));
    TEST_ASSERT_EQUAL_INT(4, ui_presets_lookup(&s_p, "rain"));
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_lookup(&s_p, "rain radar")); /* names match exactly */
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_lookup(&s_p, ""));
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_lookup(&s_p, NULL));
    snprintf(s_p.presets[1].name, sizeof(s_p.presets[1].name), "%s", "rain"); /* a name that is another's id */
    TEST_ASSERT_EQUAL_INT(1, ui_presets_lookup(&s_p, "rain"));
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
    TEST_ASSERT_EQUAL_INT(6, s_p.count);
    TEST_ASSERT_EQUAL_INT(4, ui_presets_find(&s_p, "rain"));
    TEST_ASSERT_EQUAL_INT(5, ui_presets_find(&s_p, "flights"));
    TEST_ASSERT_EQUAL_STRING("weather", s_p.presets[s_p.active].id); /* the active one stays */
    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p));

    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"offered\":[\"rain\",\"flights\"]"));
    s_p.count = 5; /* the owner deletes Flights */
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p)); /* and it stays deleted */
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "flights"));
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
    TEST_ASSERT_EQUAL_INT(2, s_p.count); /* Flights only */
    TEST_ASSERT_EQUAL(UI_LAYOUT_GRID, s_p.presets[0].layout);
    TEST_ASSERT_EQUAL_STRING("flights", s_p.presets[1].id);
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

static void test_bad_split_trees_are_rejected_with_a_reason(void)
{
    static char tree[2048], part[1024];
    /* nine cells, each at least 90×40: two rows of four, one of them split again */
    put_split(part, sizeof(part), "columns", "1/2", "{}", "{}");
    char row[512];
    snprintf(row, sizeof(row), "{\"split\": \"columns\", \"ratio\": \"1/4\", \"a\": {}, \"b\": {\"split\": \"columns\","
             " \"ratio\": \"1/3\", \"a\": {}, \"b\": %s}}", part);
    char split_cell[256];
    put_split(split_cell, sizeof(split_cell), "rows", "1/2", "{}", "{}");
    char row9[512];
    snprintf(row9, sizeof(row9), "{\"split\": \"columns\", \"ratio\": \"1/4\", \"a\": %s,"
             " \"b\": {\"split\": \"columns\", \"ratio\": \"1/3\", \"a\": {}, \"b\": %s}}", split_cell, part);
    put_split(tree, sizeof(tree), "rows", "1/2", row, row9);
    check_tree_rejected(tree, "at most 8 cells");
    put_split(tree, sizeof(tree), "rows", "1/2", row, row); /* eight are fine */
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(split_file(tree), &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT(8, ui_preset_slots(&s_p.presets[0]));

    put_split(part, sizeof(part), "rows", "1/2", "{}", "{}");
    put_split(tree, sizeof(tree), "rows", "1/4", part, "{}"); /* 69 px split in two: 34 */
    check_tree_rejected(tree, "90×40");
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

/* The largest presets.json there can be: 16 split presets of 8 cells, in columns wherever a tree can
 * have them, with every line hidden; the longest field ids a cell takes; names of control characters,
 * which cJSON writes as six bytes each ("\u0001"); every option at its longest; 8 schedule entries
 * that switch to a preset. */
static void test_a_full_set_of_split_presets_fits_the_save_buffer(void)
{
    static const uint8_t k_tree[UI_SPLIT_NODES] = {
        UI_RATIO_1_2 | UI_SPLIT_NO_LINE,
        UI_RATIO_1_4 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0,
        UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0,
        UI_RATIO_1_4 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0,
        UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0,
    };
    memset(&s_p, 0, sizeof(s_p));
    for (int i = 0; i < UI_PRESET_MAX; i++) {
        ui_preset_t *p = &s_p.presets[i];
        snprintf(p->id, sizeof(p->id), "preset-%08d", i);
        memset(p->name, 0x01, UI_PRESET_NAME_LEN - 1);
        p->layout = UI_LAYOUT_SPLIT;
        memcpy(p->split, k_tree, sizeof(k_tree));
        for (int k = 0; k < UI_SPLIT_CELLS; k++) {
            p->slots[k] = (uint8_t)(k % 2 ? UI_FIELD_POLLEN_MUGWORT : UI_FIELD_POLLEN_RAGWEED); /* fit narrow S */
        }
        p->clock = UI_CLOCK_12H;
        p->stale_policy = UI_STALE_PLACEHOLDER;
        p->status_battery = UI_STATUS_BAT_PERCENT | UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
    }
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
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(buf, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
}

/* mqtt.<key> fields (spec §5.4, §12.5, D32): the presets name up to 32 keys between them, kept as names. */
#define MQTT_PRESETS                                                                                               \
    "{\"schema\": 1, \"presets\": ["                                                                                \
    " {\"id\": \"ha\", \"layout\": \"grid\", \"slots\": {\"g1\": \"mqtt.outdoor_temp\", \"g2\": \"mqtt.co2\","        \
    "  \"g3\": \"mqtt.outdoor_temp\", \"g4\": \"env.temp\"}},"                                                          \
    " {\"id\": \"big\", \"layout\": \"classic\", \"slots\": {\"main\": \"mqtt.power\"}},"                           \
    " {\"id\": \"cells\", \"layout\": \"split\", \"split\": {\"split\": \"rows\", \"ratio\": \"1/2\","               \
    "  \"a\": {\"field\": \"mqtt.door\"}, \"b\": {\"field\": \"mqtt.co2\"}}}]}"

static void test_mqtt_fields_name_their_keys(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(MQTT_PRESETS, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(4, s_p.mqtt.count); /* in the order the file first names them */
    TEST_ASSERT_EQUAL_STRING("outdoor_temp", s_p.mqtt.key[0]);
    TEST_ASSERT_EQUAL_STRING("co2", s_p.mqtt.key[1]);
    TEST_ASSERT_EQUAL_STRING("power", s_p.mqtt.key[2]);
    TEST_ASSERT_EQUAL_STRING("door", s_p.mqtt.key[3]);
    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_MQTT + 0, s_p.presets[0].slots[0]);
    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_MQTT + 1, s_p.presets[0].slots[1]);
    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_MQTT + 0, s_p.presets[0].slots[2]); /* the same key, the same field */
    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_ENV_TEMP, s_p.presets[0].slots[3]);
    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_MQTT + 2, s_p.presets[1].slots[0]); /* Classic's XL slot takes numbers */
    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_MQTT + 3, s_p.presets[2].slots[0]);
    TEST_ASSERT_TRUE(ui_field_is_mqtt(s_p.presets[0].slots[0]));
    TEST_ASSERT_FALSE(ui_field_is_mqtt(UI_FIELD_ENV_TEMP));
    TEST_ASSERT_FALSE(ui_field_is_mqtt(UI_FIELD_MQTT + UI_MQTT_KEYS));
    ui_presets_defaults(&s_p);
    TEST_ASSERT_EQUAL_UINT8(0, s_p.mqtt.count);
}

/* The names stay as written, mapped or not: the presets never learn which keys the mappings have. */
static void test_mqtt_fields_survive_a_round_trip(void)
{
    TEST_ASSERT_TRUE(ui_presets_from_json(MQTT_PRESETS, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"g3\":\"mqtt.outdoor_temp\""));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "{\"field\":\"mqtt.door\"}"));
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(back));
}

static void test_bad_mqtt_fields_are_rejected(void)
{
    static const struct {
        const char *field, *err;
    } k_cases[] = {
        { "mqtt.Outdoor", "preset \"x\": unknown field \"mqtt.Outdoor\"" },
        { "mqtt.", "preset \"x\": unknown field \"mqtt.\"" },
        { "mqtt.a2345678901234567890123x", "preset \"x\": unknown field \"mqtt.a2345678901234567890123x\"" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"presets\": [{\"id\": \"x\", \"layout\": \"grid\","
                 " \"slots\": {\"g1\": \"%s\"}}]}", k_cases[i].field);
        TEST_ASSERT_FALSE_MESSAGE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)), k_cases[i].field);
        TEST_ASSERT_EQUAL_STRING(k_cases[i].err, s_err);
    }
    /* 6 presets of 6 slots, each with a key of its own: 36 keys */
    size_t at = (size_t)snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"presets\": [");
    for (int i = 0; i < 6; i++) {
        at += (size_t)snprintf(s_json + at, sizeof(s_json) - at, "%s{\"id\": \"p%d\", \"layout\": \"grid\", \"slots\": {",
                               i ? "," : "", i);
        for (int k = 0; k < 6; k++) {
            at += (size_t)snprintf(s_json + at, sizeof(s_json) - at, "%s\"g%d\": \"mqtt.k%d\"", k ? "," : "", k + 1,
                                   i * 6 + k);
        }
        at += (size_t)snprintf(s_json + at, sizeof(s_json) - at, "}}");
    }
    snprintf(s_json + at, sizeof(s_json) - at, "]}");
    TEST_ASSERT_FALSE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("preset \"p5\": at most 32 different MQTT fields", s_err);
}

/* The largest presets.json with MQTT fields: 16 split presets of 8 cells, 32 keys of 23 bytes, names of
 * control characters, every option at its longest, 8 schedule entries. */
static void test_a_full_set_with_mqtt_fields_fits_the_save_buffer(void)
{
    static const uint8_t k_tree[UI_SPLIT_NODES] = {
        UI_RATIO_1_2 | UI_SPLIT_NO_LINE,
        UI_RATIO_1_4 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0,
        UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0,
        UI_RATIO_1_4 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0,
        UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0,
    };
    memset(&s_p, 0, sizeof(s_p));
    for (int k = 0; k < UI_MQTT_KEYS; k++) {
        snprintf(s_p.mqtt.key[k], sizeof(s_p.mqtt.key[k]), "k%022d", k);
    }
    s_p.mqtt.count = UI_MQTT_KEYS;
    for (int i = 0; i < UI_PRESET_MAX; i++) {
        ui_preset_t *p = &s_p.presets[i];
        snprintf(p->id, sizeof(p->id), "preset-%08d", i);
        memset(p->name, 0x01, UI_PRESET_NAME_LEN - 1);
        p->layout = UI_LAYOUT_SPLIT;
        memcpy(p->split, k_tree, sizeof(k_tree));
        for (int k = 0; k < UI_SPLIT_CELLS; k++) {
            p->slots[k] = (uint8_t)(UI_FIELD_MQTT + (i * UI_SPLIT_CELLS + k) % UI_MQTT_KEYS); /* first use in order */
        }
        p->clock = UI_CLOCK_12H;
        p->stale_policy = UI_STALE_PLACEHOLDER;
        p->status_battery = UI_STATUS_BAT_PERCENT | UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
    }
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
    printf("the largest presets.json with MQTT fields: %u bytes of %u\n", (unsigned)n, (unsigned)sizeof(buf));
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(buf, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_the_six_built_ins);
    RUN_TEST(test_a_preset_is_found_by_its_name_or_its_id);
    RUN_TEST(test_next_follows_cycle_order_and_skips_presets_out_of_it);
    RUN_TEST(test_the_cycle_visits_flights_only_in_sync_mode_always);
    RUN_TEST(test_a_file_from_before_m6_gains_the_radars_once);
    RUN_TEST(test_the_radars_need_room_and_a_free_id);
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
    RUN_TEST(test_a_preset_counts_the_slots_its_layout_uses);
    RUN_TEST(test_mqtt_fields_name_their_keys);
    RUN_TEST(test_mqtt_fields_survive_a_round_trip);
    RUN_TEST(test_bad_mqtt_fields_are_rejected);
    RUN_TEST(test_a_full_set_with_mqtt_fields_fits_the_save_buffer);
    return UNITY_END();
}
