#include <stdio.h>
#include <string.h>

#include "ui_fields.h"
#include "ui_preset.h"
#include "unity.h"

static ui_presets_t s_p;
static char s_err[128];
static char s_json[8192];

void setUp(void)
{
    ui_presets_defaults(&s_p);
    s_err[0] = '\0';
}

void tearDown(void) {}

static void test_defaults_are_home_indoor_weather_and_focus(void)
{
    TEST_ASSERT_EQUAL_INT(4, s_p.count);
    TEST_ASSERT_EQUAL_STRING("home", s_p.presets[s_p.active].id);
    TEST_ASSERT_EQUAL(UI_LAYOUT_CLASSIC, s_p.presets[0].layout);
    TEST_ASSERT_EQUAL(UI_FIELD_TIME_CLOCK, s_p.presets[0].slots[0]);
    TEST_ASSERT_EQUAL_INT(2, ui_presets_find(&s_p, "weather"));
    TEST_ASSERT_FALSE(s_p.presets[2].in_cycle);
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "nope"));
}

static void test_next_follows_cycle_order_and_skips_presets_out_of_it(void)
{
    TEST_ASSERT_EQUAL_INT(1, ui_presets_next(&s_p)); /* home -> indoor */
    s_p.active = 1;
    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p)); /* indoor -> focus, skipping weather */
    s_p.active = 3;
    TEST_ASSERT_EQUAL_INT(0, ui_presets_next(&s_p)); /* wraps */
    for (int i = 0; i < s_p.count; i++) {
        s_p.presets[i].in_cycle = i == 3;
    }
    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p)); /* the only one: stays */
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_home_indoor_weather_and_focus);
    RUN_TEST(test_next_follows_cycle_order_and_skips_presets_out_of_it);
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
    return UNITY_END();
}
