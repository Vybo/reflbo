#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdio.h>
#include <string.h>

#include "context_fixtures.h"
#include "pc_msg.h"
#include "ui_fields.h"
#include "ui_pc.h"
#include "ui_preset.h"
#include "unity.h"

/* The PC's readings as fields (spec §5.1, §12a, D44): `pc.<id>` resolved from the session's last message, named in
 * presets.json, and kept in the presets' own table of reading ids. */

static ui_context_t s_ctx;
static ui_presets_t s_p;
static pc_catalog_t s_cat;
static pc_values_t s_vals;
static ui_pc_t s_pc;
static char s_err[UI_PRESET_ERR_LEN];
static char s_json[UI_PRESETS_JSON_MAX];

static void reading(int i, const char *id, const char *label, const char *unit, int decimals, float value)
{
    pc_reading_t *r = &s_cat.r[i];
    snprintf(r->id, sizeof(r->id), "%s", id);
    snprintf(r->label, sizeof(r->label), "%s", label);
    snprintf(r->unit, sizeof(r->unit), "%s", unit);
    r->decimals = (uint8_t)decimals;
    s_cat.count = (uint8_t)(i + 1);
    s_vals.has[i] = true;
    s_vals.v[i] = value;
}

void setUp(void)
{
    s_ctx = fixture_context();
    memset(&s_p, 0, sizeof(s_p));
    memset(&s_cat, 0, sizeof(s_cat));
    memset(&s_vals, 0, sizeof(s_vals));
    reading(0, "cpu_load", "CPU load", "%", 0, 37.4f);
    s_cat.r[0].has_min = s_cat.r[0].has_max = true;
    s_cat.r[0].max = 100.0f;
    reading(1, "cpu_temp", "CPU", "\xc2\xb0" "C", 1, 61.25f);
    reading(2, "ram_used", "RAM", "GB", 1, 21.34f);
    s_pc = (ui_pc_t){ .session = true, .catalog = &s_cat, .values = &s_vals, .age_ms = 400, .interval_ms = 1000 };
    s_ctx.pc = &s_pc;
    s_ctx.pc_ids = s_p.pc_ids;
}

void tearDown(void) {}

static ui_value_t pc(const char *id)
{
    ui_field_id_t f = ui_presets_pc_field(&s_p, id);
    TEST_ASSERT_NOT_EQUAL(UI_FIELD_NONE, f);
    ui_value_t v;
    ui_resolve(&s_ctx, f, &v);
    return v;
}

static void test_a_reading_shows_with_its_label_unit_and_decimals(void)
{
    ui_value_t v = pc("cpu_temp");
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL(UI_FK_NUMBER, v.kind);
    TEST_ASSERT_EQUAL_STRING("CPU", v.label);
    TEST_ASSERT_EQUAL_STRING("61.3", v.text); /* rounded half away from zero */
    TEST_ASSERT_EQUAL_STRING("61", v.short_text);
    TEST_ASSERT_EQUAL_STRING("\xc2\xb0" "C", v.unit);
    TEST_ASSERT_TRUE(v.has_num);
    TEST_ASSERT_EQUAL_FLOAT(61.25f, v.num);
    TEST_ASSERT_FALSE(v.has_range); /* the app gave none */
    v = pc("cpu_load");
    TEST_ASSERT_EQUAL_STRING("37", v.text);
    TEST_ASSERT_TRUE(v.has_range);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, v.range_min);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, v.range_max);
    s_vals.v[2] = -0.05f;
    TEST_ASSERT_EQUAL_STRING("-0.1", pc("ram_used").text);
}

static void test_czech_writes_its_comma(void)
{
    s_ctx.lang = lang_get("cs");
    TEST_ASSERT_EQUAL_STRING("61,3", pc("cpu_temp").text);
}

static void test_a_huge_value_drops_its_decimals_and_still_shows(void)
{
    s_vals.v[2] = 3.5e9f; /* past what a 32-bit long holds in tenths */
    ui_value_t v = pc("ram_used");
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("3500000000", v.text);
    s_vals.v[2] = -1e30f;
    TEST_ASSERT_TRUE(strlen(pc("ram_used").text) > 0);
}

static void test_no_value_no_session_or_no_message_is_missing_with_the_label_kept(void)
{
    s_vals.has[1] = false; /* HWiNFO sends none */
    ui_value_t v = pc("cpu_temp");
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    TEST_ASSERT_EQUAL_STRING("CPU", v.label);
    TEST_ASSERT_FALSE(v.has_num);
    s_pc.values = NULL; /* before the first message */
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, pc("cpu_load").state);
    s_pc.values = &s_vals;
    s_pc.session = false; /* the cached catalogue still names it */
    v = pc("cpu_load");
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    TEST_ASSERT_EQUAL_STRING("CPU load", v.label);
    s_ctx.pc = NULL; /* no view at all */
    v = pc("cpu_load");
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    TEST_ASSERT_EQUAL_STRING("cpu_load", v.label);
}

static void test_a_value_goes_stale_after_three_intervals(void)
{
    s_pc.age_ms = 3000;
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, pc("cpu_load").state);
    s_pc.age_ms = 3001;
    ui_value_t v = pc("cpu_load");
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
    TEST_ASSERT_EQUAL_UINT32(3, v.age_s);
    TEST_ASSERT_EQUAL_STRING("37", v.text); /* the value stays, marked */
    s_pc.interval_ms = 250;
    s_pc.age_ms = 800;
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, pc("cpu_load").state);
}

static void test_a_renamed_reading_shows_its_old_id(void)
{
    ui_value_t v = pc("gpu_temp"); /* not in the catalogue: the owner renamed it on the PC */
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    TEST_ASSERT_EQUAL_STRING("gpu_temp", v.label);
    TEST_ASSERT_EQUAL(UI_FK_NUMBER, v.kind);
}

static void test_the_presets_keep_one_entry_a_reading(void)
{
    ui_field_id_t load = ui_presets_pc_field(&s_p, "cpu_load");
    TEST_ASSERT_EQUAL(UI_FIELD_PC_FIRST, load);
    TEST_ASSERT_TRUE(ui_field_is_pc(load));
    TEST_ASSERT_FALSE(ui_field_is_pc(UI_FIELD_ENV_TEMP));
    TEST_ASSERT_EQUAL(UI_FIELD_PC_FIRST + 1, ui_presets_pc_field(&s_p, "cpu_temp"));
    TEST_ASSERT_EQUAL(load, ui_presets_pc_field(&s_p, "cpu_load"));
    TEST_ASSERT_EQUAL_STRING("cpu_load", s_p.pc_ids[0]);
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_presets_pc_field(&s_p, "CPU"));
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_presets_pc_field(&s_p, ""));
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_presets_pc_field(&s_p, "a_very_long_identifier"));
    for (int i = 2; i < UI_PC_FIELDS; i++) {
        char id[8];
        snprintf(id, sizeof(id), "r%d", i);
        TEST_ASSERT_NOT_EQUAL(UI_FIELD_NONE, ui_presets_pc_field(&s_p, id));
    }
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_presets_pc_field(&s_p, "one_more")); /* the table is full */
    TEST_ASSERT_EQUAL(load, ui_presets_pc_field(&s_p, "cpu_load"));       /* a known one still resolves */
    TEST_ASSERT_NOT_NULL(ui_field_info(load));
    TEST_ASSERT_EQUAL(UI_FK_NUMBER, ui_field_info(load)->kind);
}

static void test_fields_are_named_as_the_file_names_them(void)
{
    ui_field_id_t load = ui_presets_pc_field(&s_p, "cpu_load");
    char name[24];
    TEST_ASSERT_EQUAL(11, ui_presets_field_name(&s_p, load, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("pc.cpu_load", name);
    ui_presets_field_name(&s_p, UI_FIELD_ENV_TEMP, name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("env.temp", name);
    TEST_ASSERT_EQUAL(0, ui_presets_field_name(&s_p, UI_FIELD_NONE, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("", name);
    TEST_ASSERT_EQUAL(0, ui_presets_field_name(&s_p, (ui_field_id_t)(UI_FIELD_PC_FIRST + 5), name, sizeof(name)));
    TEST_ASSERT_EQUAL(load, ui_presets_field_by_name(&s_p, "pc.cpu_load"));
    TEST_ASSERT_EQUAL(UI_FIELD_PC_FIRST + 1, ui_presets_field_by_name(&s_p, "pc.ram_used"));
    TEST_ASSERT_EQUAL(UI_FIELD_ENV_TEMP, ui_presets_field_by_name(&s_p, "env.temp"));
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_presets_field_by_name(&s_p, "pc.CPU"));
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_presets_field_by_name(&s_p, "pc."));
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_presets_field_by_name(&s_p, "env.nope"));
}

static const char k_file[] =
    "{\"schema\":1,\"active\":\"pc\",\"presets\":["
    "{\"id\":\"pc\",\"name\":\"PC\",\"layout\":\"grid\",\"slots\":{\"g1\":\"pc.cpu_load\",\"g2\":\"pc.cpu_temp\","
    "\"g3\":\"env.temp\",\"g4\":\"pc.cpu_load\"}},"
    "{\"id\":\"cells\",\"name\":\"Cells\",\"layout\":\"split\",\"split\":{\"split\":\"columns\",\"ratio\":\"1/2\","
    "\"a\":{\"field\":\"pc.ram_used\"},\"b\":{\"field\":\"pc.gone\"}}}]}";

static void test_presets_json_names_pc_readings_and_keeps_them(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_file, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL(UI_FIELD_PC_FIRST, s_p.presets[0].slots[0]);
    TEST_ASSERT_EQUAL(UI_FIELD_PC_FIRST + 1, s_p.presets[0].slots[1]);
    TEST_ASSERT_EQUAL(UI_FIELD_ENV_TEMP, s_p.presets[0].slots[2]);
    TEST_ASSERT_EQUAL(UI_FIELD_PC_FIRST, s_p.presets[0].slots[3]); /* one entry for both */
    TEST_ASSERT_EQUAL(UI_FIELD_PC_FIRST + 2, s_p.presets[1].slots[0]);
    TEST_ASSERT_EQUAL_STRING("gone", s_p.pc_ids[3]); /* a reading the PC no longer sends stays named */
    TEST_ASSERT_EQUAL_STRING("", s_p.pc_ids[4]);
    size_t n = ui_presets_to_json(&s_p, s_json, sizeof(s_json));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"g1\":\"pc.cpu_load\""));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"field\":\"pc.gone\""));
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(back));
}

static void test_presets_json_refuses_a_bad_pc_reading(void)
{
    static const char k_bad[] = "{\"schema\":1,\"presets\":[{\"id\":\"pc\",\"name\":\"PC\",\"layout\":\"grid\","
                                "\"slots\":{\"g1\":\"pc.CPU\"}}]}";
    TEST_ASSERT_FALSE(ui_presets_from_json(k_bad, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("preset \"pc\": unknown field \"pc.CPU\"", s_err);
    /* six grid presets of six readings each: 36 names, 33 of them different */
    char *const end = s_json + sizeof(s_json);
    char *p = s_json + snprintf(s_json, sizeof(s_json), "{\"schema\":1,\"presets\":[");
    for (int i = 0; i < 6; i++) {
        p += snprintf(p, (size_t)(end - p), "%s{\"id\":\"p%d\",\"name\":\"P\",\"layout\":\"grid\",\"slots\":{", i ? "," : "", i);
        for (int g = 0; g < 6; g++) {
            int r = i * 6 + g < 33 ? i * 6 + g : 0;
            p += snprintf(p, (size_t)(end - p), "%s\"g%d\":\"pc.r%d\"", g ? "," : "", g + 1, r);
        }
        p += snprintf(p, (size_t)(end - p), "}}");
    }
    strcpy(p, "]}");
    TEST_ASSERT_FALSE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("preset \"p5\": the presets name more than 32 PC readings", s_err);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_reading_shows_with_its_label_unit_and_decimals);
    RUN_TEST(test_czech_writes_its_comma);
    RUN_TEST(test_a_huge_value_drops_its_decimals_and_still_shows);
    RUN_TEST(test_no_value_no_session_or_no_message_is_missing_with_the_label_kept);
    RUN_TEST(test_a_value_goes_stale_after_three_intervals);
    RUN_TEST(test_a_renamed_reading_shows_its_old_id);
    RUN_TEST(test_the_presets_keep_one_entry_a_reading);
    RUN_TEST(test_fields_are_named_as_the_file_names_them);
    RUN_TEST(test_presets_json_names_pc_readings_and_keeps_them);
    RUN_TEST(test_presets_json_refuses_a_bad_pc_reading);
    return UNITY_END();
}
