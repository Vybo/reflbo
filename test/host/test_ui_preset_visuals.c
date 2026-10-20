#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "ui_dashboard.h"
#include "ui_history.h"
#include "ui_preset.h"
#include "ui_visual.h"
#include "unity.h"

/* Show as in presets.json (spec §5.4, M7, D44): a fixed slot's `visuals`, a split cell's `show`, their shared pool of
 * 48, and the dashboard drawing them where they fit. */

static ui_presets_t s_p;
static char s_err[UI_PRESET_ERR_LEN];
static char s_json[UI_PRESETS_JSON_MAX];
static uint8_t s_buf[400 * 300 / 8], s_other[400 * 300 / 8];

void setUp(void)
{
    memset(&s_p, 0, sizeof(s_p));
    s_err[0] = '\0';
}

void tearDown(void) {}

static const char k_file[] =
    "{\"schema\":1,\"active\":\"grid\",\"presets\":["
    "{\"id\":\"grid\",\"name\":\"Grid\",\"layout\":\"grid\","
    "\"slots\":{\"g1\":\"env.temp\",\"g2\":\"env.hum\",\"g3\":\"bat.level\",\"g4\":\"pc.cpu_load\"},"
    "\"visuals\":{\"g1\":{\"as\":\"chart\",\"min\":\"auto\",\"max\":\"auto\",\"span_s\":7200,\"numbers\":false},"
    "\"g2\":{\"as\":\"ring\"},\"g3\":{\"as\":\"bar\",\"min\":10,\"max\":90.5},"
    "\"g4\":{\"as\":\"gauge\",\"max\":100}}},"
    "{\"id\":\"cells\",\"name\":\"Cells\",\"layout\":\"split\",\"split\":{\"split\":\"columns\",\"ratio\":\"1/2\","
    "\"a\":{\"field\":\"pc.cpu_load\",\"show\":{\"as\":\"chart\",\"span_s\":60}},\"b\":{\"field\":\"env.temp\"}}}]}";

static void test_the_file_names_each_slots_visual(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_file, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL(5, s_p.visual_count);
    const ui_visual_t *g1 = ui_presets_visual(&s_p, 0, 0);
    TEST_ASSERT_NOT_NULL(g1);
    TEST_ASSERT_EQUAL(UI_VIS_CHART, g1->as);
    TEST_ASSERT_EQUAL(UI_LIMIT_AUTO, g1->min_mode);
    TEST_ASSERT_EQUAL(UI_LIMIT_AUTO, g1->max_mode);
    TEST_ASSERT_EQUAL_UINT32(7200, g1->span_s);
    TEST_ASSERT_FALSE(g1->numbers);
    const ui_visual_t *g2 = ui_presets_visual(&s_p, 0, 1);
    TEST_ASSERT_EQUAL(UI_VIS_RING, g2->as);
    TEST_ASSERT_EQUAL(UI_LIMIT_FIELD, g2->min_mode); /* left out: the field's own */
    const ui_visual_t *g3 = ui_presets_visual(&s_p, 0, 2);
    TEST_ASSERT_EQUAL(UI_LIMIT_VALUE, g3->min_mode);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, g3->min);
    TEST_ASSERT_EQUAL_FLOAT(90.5f, g3->max);
    const ui_visual_t *g4 = ui_presets_visual(&s_p, 0, 3);
    TEST_ASSERT_EQUAL(UI_LIMIT_FIELD, g4->min_mode);
    TEST_ASSERT_EQUAL(UI_LIMIT_VALUE, g4->max_mode);
    TEST_ASSERT_NULL(ui_presets_visual(&s_p, 0, 4)); /* g5: its value */
    const ui_visual_t *a = ui_presets_visual(&s_p, 1, 0);
    TEST_ASSERT_EQUAL(UI_VIS_CHART, a->as);
    TEST_ASSERT_EQUAL_UINT32(60, a->span_s);
    TEST_ASSERT_TRUE(a->numbers); /* a chart's numbers are on unless the file turns them off */
    TEST_ASSERT_NULL(ui_presets_visual(&s_p, 1, 1));
    ui_titles_t own;
    ui_presets_titles(&s_p, 0, &own);
    TEST_ASSERT_EQUAL_PTR(g1, own.visual[0]);
    TEST_ASSERT_NULL(own.visual[4]);
}

static void test_the_visuals_are_written_back_as_they_came(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_file, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(s_json, "\"visuals\":{\"g1\":{\"as\":\"chart\",\"min\":\"auto\",\"max\":\"auto\",\"span_s\":7200,"
                       "\"numbers\":false},\"g2\":{\"as\":\"ring\"},\"g3\":{\"as\":\"bar\",\"min\":10,\"max\":90.5},"
                       "\"g4\":{\"as\":\"gauge\",\"max\":100}}"),
        s_json);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"show\":{\"as\":\"chart\",\"span_s\":60,\"numbers\":true}"), s_json);
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(back));
}

static void test_a_bound_keeps_its_digits(void)
{
    static const char k[] = "{\"schema\":1,\"presets\":[{\"id\":\"g\",\"layout\":\"grid\",\"slots\":{\"g1\":\"env.temp\"},"
                            "\"visuals\":{\"g1\":{\"as\":\"bar\",\"min\":-0.1,\"max\":23.7}}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k, &s_p, s_err, sizeof(s_err)), s_err);
    ui_presets_to_json(&s_p, s_json, sizeof(s_json));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "{\"as\":\"bar\",\"min\":-0.1,\"max\":23.7}"), s_json);
}

static void refused(const char *visual, const char *why)
{
    char file[512];
    snprintf(file, sizeof(file),
             "{\"schema\":1,\"presets\":[{\"id\":\"g\",\"layout\":\"grid\",\"slots\":{\"g1\":\"env.temp\","
             "\"g2\":\"date.day\"},\"visuals\":%s}]}",
             visual);
    TEST_ASSERT_FALSE_MESSAGE(ui_presets_from_json(file, &s_p, s_err, sizeof(s_err)), visual);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(why, s_err, visual);
}

static void test_a_visual_beyond_the_rules_is_refused(void)
{
    refused("{\"g2\":{\"as\":\"bar\"}}", "preset \"g\": slot g2 shows no number");
    refused("{\"g3\":{\"as\":\"bar\"}}", "preset \"g\": slot g3 shows no number"); /* an empty slot */
    refused("{\"g1\":{\"as\":\"pie\"}}", "preset \"g\": a visual is chart, bar, ring or gauge");
    refused("{\"g1\":{\"as\":\"bar\",\"min\":5,\"max\":5}}", "preset \"g\": a visual's min must be below its max");
    refused("{\"g1\":{\"as\":\"chart\",\"span_s\":5400}}",
            "preset \"g\": a chart's span is 10-3600 s, or whole hours up to 24 h");
    refused("{\"g1\":{\"as\":\"bar\",\"min\":\"auto\"}}", "preset \"g\": only a chart's min and max can be auto");
    refused("{\"g1\":{\"as\":\"bar\",\"max\":\"high\"}}", "preset \"g\": a visual's min and max are numbers");
    refused("{\"g9\":{\"as\":\"bar\"}}", "preset \"g\": layout grid has no slot \"g9\"");
    refused("[]", "preset \"g\": visuals must be an object of slot names");
    refused("{\"g1\":3}", "preset \"g\": a visual must be an object");
}

static void test_the_pool_holds_48_visuals(void)
{
    /* sixteen grid presets of six numbers: 96 slots, the 49th visual refused */
    char *const end = s_json + sizeof(s_json);
    char *p = s_json + snprintf(s_json, sizeof(s_json), "{\"schema\":1,\"presets\":[");
    for (int i = 0; i < UI_PRESET_MAX; i++) {
        p += snprintf(p, (size_t)(end - p), "%s{\"id\":\"p%d\",\"layout\":\"grid\",\"slots\":{", i ? "," : "", i);
        for (int g = 1; g <= 6; g++) {
            p += snprintf(p, (size_t)(end - p), "%s\"g%d\":\"env.temp\"", g > 1 ? "," : "", g);
        }
        p += snprintf(p, (size_t)(end - p), "},\"visuals\":{");
        for (int g = 1; g <= 6; g++) {
            p += snprintf(p, (size_t)(end - p), "%s\"g%d\":{\"as\":\"ring\"}", g > 1 ? "," : "", g);
        }
        p += snprintf(p, (size_t)(end - p), "}}");
    }
    strcpy(p, "]}");
    TEST_ASSERT_FALSE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("preset \"p8\": 48 visuals at most in all presets", s_err);
    /* and in the API */
    memset(&s_p, 0, sizeof(s_p));
    ui_visual_t ring = { .as = UI_VIS_RING };
    for (int i = 0; i < UI_VISUALS_MAX; i++) {
        TEST_ASSERT_TRUE(ui_presets_set_visual(&s_p, i / 6, i % 6, &ring));
    }
    TEST_ASSERT_FALSE(ui_presets_set_visual(&s_p, 15, 0, &ring));
    TEST_ASSERT_TRUE(ui_presets_set_visual(&s_p, 0, 0, &ring)); /* one already there is replaced */
    TEST_ASSERT_TRUE(ui_presets_set_visual(&s_p, 0, 0, NULL));  /* and cleared */
    TEST_ASSERT_EQUAL(UI_VISUALS_MAX - 1, s_p.visual_count);
    TEST_ASSERT_NULL(ui_presets_visual(&s_p, 0, 0));
    TEST_ASSERT_TRUE(ui_presets_set_visual(&s_p, 15, 0, &ring));
    for (int i = 1; i < s_p.visual_count; i++) { /* kept in order, so a file reads back the same */
        int a = s_p.visuals[i - 1].preset * UI_SLOT_MAX + s_p.visuals[i - 1].slot;
        int b = s_p.visuals[i].preset * UI_SLOT_MAX + s_p.visuals[i].slot;
        TEST_ASSERT_TRUE(a < b);
    }
    ui_visual_t value = { .as = UI_VIS_VALUE };
    TEST_ASSERT_TRUE(ui_presets_set_visual(&s_p, 15, 0, &value)); /* "as the value" is none */
    TEST_ASSERT_NULL(ui_presets_visual(&s_p, 15, 0));
}

static void draw(gfx_fb_t *fb, uint8_t *buf, const ui_context_t *ctx, const ui_preset_t *p, const ui_titles_t *own)
{
    gfx_fb_init(fb, buf, 400, 300);
    ui_draw_dashboard(fb, ctx, p, own);
}

static void test_the_dashboard_draws_a_visual_where_it_fits_and_the_value_where_not(void)
{
    ui_context_t ctx = fixture_context();
    ui_preset_t p = fixture_preset("indoor"); /* Grid: g2 is env.hum */
    TEST_ASSERT_EQUAL(UI_FIELD_ENV_HUM, p.slots[1]);
    ui_visual_t ring = { .as = UI_VIS_RING };
    ui_titles_t own;
    memset(&own, 0, sizeof(own));
    gfx_fb_t a, b;
    draw(&a, s_buf, &ctx, &p, &own);
    own.visual[1] = &ring;
    draw(&b, s_other, &ctx, &p, &own);
    TEST_ASSERT_TRUE(memcmp(s_buf, s_other, sizeof(s_buf)) != 0); /* the ring */
    /* the same visual drawn by hand in the slot's rectangle, the slot otherwise empty */
    ui_value_t v;
    ui_resolve(&ctx, UI_FIELD_ENV_HUM, &v);
    gfx_rect_t r = ui_layout(UI_LAYOUT_GRID)->slots[1].rect;
    ui_preset_t empty = p;
    empty.slots[1] = UI_FIELD_NONE;
    memset(&own, 0, sizeof(own));
    draw(&a, s_buf, &ctx, &empty, &own);
    ui_visual_draw(&a, r, &v, &ring, NULL, 0, ctx.lang);
    TEST_ASSERT_EQUAL_MEMORY(s_buf, s_other, sizeof(s_buf));
    /* a cell too small for the visual, or a field that isn't a number: the value as before */
    ui_visual_t chart = { .as = UI_VIS_CHART, .span_s = 60 };
    uint8_t cells[24] = { UI_FIELD_ENV_TEMP, UI_FIELD_ENV_HUM };
    fixture_grid_split(&p, 4, 6, 0, cells, 2); /* 66×69 cells: no room for a chart */
    memset(&own, 0, sizeof(own));
    draw(&a, s_buf, &ctx, &p, &own);
    own.visual[0] = &chart;
    draw(&b, s_other, &ctx, &p, &own);
    TEST_ASSERT_EQUAL_MEMORY(s_buf, s_other, sizeof(s_buf));
    p = fixture_preset("home"); /* Classic: main is the clock */
    memset(&own, 0, sizeof(own));
    draw(&a, s_buf, &ctx, &p, &own);
    own.visual[0] = &ring;
    draw(&b, s_other, &ctx, &p, &own);
    TEST_ASSERT_EQUAL_MEMORY(s_buf, s_other, sizeof(s_buf));
}

static void test_a_chart_reads_its_fields_history(void)
{
    ui_series_t *mem = calloc(UI_HIST_SERIES, sizeof(ui_series_t));
    ui_history_t h;
    ui_history_init(&h, mem);
    const char *const names[] = { "env.temp" };
    ui_history_track(&h, names, 1);
    for (uint32_t i = 0; i < 60; i++) {
        ui_series_push(ui_history_find(&h, "env.temp"), 1000 + i * 1000, 20.0f + (float)(i % 7));
    }
    ui_context_t ctx = fixture_context();
    ui_preset_t p = fixture_preset("indoor"); /* g1: env.temp */
    ui_visual_t chart = { .as = UI_VIS_CHART, .span_s = 60, .numbers = true };
    ui_titles_t own;
    memset(&own, 0, sizeof(own));
    own.visual[0] = &chart;
    gfx_fb_t a, b;
    draw(&a, s_buf, &ctx, &p, &own); /* no history in the context: an empty chart */
    ctx.history = &h;
    ctx.now_ms = 60 * 1000;
    draw(&b, s_other, &ctx, &p, &own);
    TEST_ASSERT_TRUE(memcmp(s_buf, s_other, sizeof(s_buf)) != 0);
    free(mem);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_file_names_each_slots_visual);
    RUN_TEST(test_the_visuals_are_written_back_as_they_came);
    RUN_TEST(test_a_bound_keeps_its_digits);
    RUN_TEST(test_a_visual_beyond_the_rules_is_refused);
    RUN_TEST(test_the_pool_holds_48_visuals);
    RUN_TEST(test_the_dashboard_draws_a_visual_where_it_fits_and_the_value_where_not);
    RUN_TEST(test_a_chart_reads_its_fields_history);
    return UNITY_END();
}
