#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "ui_dashboard.h"
#include "ui_history.h"
#include "ui_layout.h"
#include "ui_pc.h"
#include "ui_preset.h"
#include "unity.h"

/* The PC layout and preset (spec §5.2, §5.4, M7, D44): four charts and three bars, the built-in PC preset filled
 * from the PC app's first catalogue, the status bar's monitor, and "PC asleep" without a session. */

static ui_presets_t s_p;
static pc_catalog_t s_cat;
static pc_values_t s_vals;
static char s_err[UI_PRESET_ERR_LEN];
static char s_json[UI_PRESETS_JSON_MAX];
static uint8_t s_buf[400 * 300 / 8], s_other[400 * 300 / 8];

void setUp(void)
{
    ui_presets_defaults(&s_p);
    ui_pc_demo(&s_cat, &s_vals);
}

void tearDown(void) {}

static void test_the_pc_layout_has_four_charts_and_three_bars(void)
{
    const ui_layout_t *l = ui_layout(UI_LAYOUT_PC);
    TEST_ASSERT_NOT_NULL(l);
    TEST_ASSERT_EQUAL_STRING("pc", l->id);
    TEST_ASSERT_EQUAL(7, l->slot_count);
    static const char *const k_names[] = { "c1", "c2", "c3", "c4", "b1", "b2", "b3" };
    for (int i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_STRING(k_names[i], l->slots[i].name);
    }
    TEST_ASSERT_TRUE(ui_visual_fits(UI_VIS_CHART, l->slots[0].rect.w, l->slots[0].rect.h));
    TEST_ASSERT_TRUE(ui_visual_fits(UI_VIS_BAR, l->slots[4].rect.w, l->slots[4].rect.h));
    TEST_ASSERT_TRUE(l->slots[0].kinds & UI_KIND(UI_FK_NUMBER));
    TEST_ASSERT_EQUAL(UI_LAYOUT_PC, ui_layout_by_name("pc"));
}

static void test_the_built_in_pc_preset_waits_for_the_first_catalogue(void)
{
    int i = ui_presets_find(&s_p, "pc");
    TEST_ASSERT_EQUAL_INT(8, i);
    TEST_ASSERT_EQUAL_INT(9, s_p.count);
    const ui_preset_t *pc = &s_p.presets[i];
    TEST_ASSERT_EQUAL_STRING("PC", pc->name);
    TEST_ASSERT_EQUAL(UI_LAYOUT_PC, pc->layout);
    TEST_ASSERT_FALSE(pc->in_cycle);
    TEST_ASSERT_TRUE(pc->status_clock);
    for (int s = 0; s < 7; s++) {
        TEST_ASSERT_EQUAL(UI_FIELD_NONE, pc->slots[s]);
    }
    TEST_ASSERT_EQUAL(0, s_p.visual_count);
    TEST_ASSERT_TRUE(s_p.offered & UI_OFFERED_PC);
}

static void test_the_first_catalogue_fills_its_charts_and_bars(void)
{
    TEST_ASSERT_TRUE(ui_presets_fill_pc(&s_p, &s_cat)); /* changed: save it */
    int i = ui_presets_find(&s_p, "pc");
    char name[24];
    static const char *const k_fields[] = { "pc.cpu_load", "pc.cpu_temp", "pc.gpu_load", "pc.gpu_temp",
                                            "pc.ram_used", "pc.vram_used", "pc.net_down" };
    for (int s = 0; s < 7; s++) {
        ui_presets_field_name(&s_p, (ui_field_id_t)s_p.presets[i].slots[s], name, sizeof(name));
        TEST_ASSERT_EQUAL_STRING(k_fields[s], name);
        const ui_visual_t *v = ui_presets_visual(&s_p, i, s);
        TEST_ASSERT_NOT_NULL(v);
        TEST_ASSERT_EQUAL(s < 4 ? UI_VIS_CHART : UI_VIS_BAR, v->as);
        TEST_ASSERT_EQUAL(UI_LIMIT_FIELD, v->min_mode);
        if (s < 4) {
            TEST_ASSERT_EQUAL_UINT32(60, v->span_s);
            TEST_ASSERT_TRUE(v->numbers);
        }
    }
    TEST_ASSERT_FALSE(ui_presets_fill_pc(&s_p, &s_cat)); /* filled once */
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0); /* and the file reads back */
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(back));
}

static void test_a_short_catalogue_fills_what_it_has(void)
{
    s_cat.count = 2;
    TEST_ASSERT_TRUE(ui_presets_fill_pc(&s_p, &s_cat));
    const ui_preset_t *pc = &s_p.presets[ui_presets_find(&s_p, "pc")];
    TEST_ASSERT_TRUE(ui_field_is_pc((ui_field_id_t)pc->slots[1]));
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, pc->slots[2]);
    TEST_ASSERT_EQUAL(2, s_p.visual_count);
    s_cat.count = 0;
    ui_presets_defaults(&s_p);
    TEST_ASSERT_FALSE(ui_presets_fill_pc(&s_p, &s_cat)); /* nothing to fill with */
}

static void test_the_owners_pc_preset_is_left_as_it_is(void)
{
    int i = ui_presets_find(&s_p, "pc");
    s_p.presets[i].slots[6] = UI_FIELD_ENV_TEMP; /* the owner put something in it */
    TEST_ASSERT_FALSE(ui_presets_fill_pc(&s_p, &s_cat));
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, s_p.presets[i].slots[0]);
    ui_presets_defaults(&s_p);
    s_p.presets[ui_presets_find(&s_p, "pc")].layout = UI_LAYOUT_GRID; /* a preset of its own with that id */
    TEST_ASSERT_FALSE(ui_presets_fill_pc(&s_p, &s_cat));
    s_p.count = 8; /* deleted */
    TEST_ASSERT_FALSE(ui_presets_fill_pc(&s_p, &s_cat));
}

static void test_a_file_from_m6d_gains_the_pc_preset_once(void)
{
    static const char k_m6d[] = "{\"schema\":1,\"active\":\"home\",\"offered\":[\"rain\",\"flights\",\"solar\","
                                "\"energy\"],\"presets\":[{\"id\":\"home\",\"layout\":\"classic\"}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_m6d, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p));
    TEST_ASSERT_EQUAL_INT(2, s_p.count);
    TEST_ASSERT_EQUAL_INT(1, ui_presets_find(&s_p, "pc"));
    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p));
    ui_presets_to_json(&s_p, s_json, sizeof(s_json));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"offered\":[\"rain\",\"flights\",\"solar\",\"energy\",\"pc\"]"));
}

static void test_the_charted_fields_are_the_historys_series(void)
{
    ui_presets_fill_pc(&s_p, &s_cat);
    ui_visual_t chart = { .as = UI_VIS_CHART, .span_s = 3600 };
    TEST_ASSERT_TRUE(ui_presets_set_visual(&s_p, 1, 0, &chart)); /* Indoor's g1: env.temp */
    TEST_ASSERT_TRUE(ui_presets_set_visual(&s_p, 0, 2, &chart)); /* Home's s1: env.temp again */
    char names[UI_HIST_SERIES][UI_HIST_NAME_LEN];
    int n = ui_presets_chart_fields(&s_p, names);
    TEST_ASSERT_EQUAL(5, n);
    TEST_ASSERT_EQUAL_STRING("env.temp", names[0]); /* in preset order, each once */
    TEST_ASSERT_EQUAL_STRING("pc.cpu_load", names[1]);
    TEST_ASSERT_EQUAL_STRING("pc.gpu_temp", names[4]);
}

static void draw(uint8_t *buf, const ui_context_t *ctx, const ui_preset_t *p)
{
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    ui_draw_dashboard(&fb, ctx, p, NULL);
}

static void test_a_session_shows_a_monitor_in_the_status_bar(void)
{
    ui_context_t ctx = fixture_context();
    ui_preset_t home = fixture_preset("home");
    ui_pc_t pc = { .session = false, .catalog = &s_cat };
    ctx.pc = &pc;
    draw(s_buf, &ctx, &home);
    pc.session = true;
    draw(s_other, &ctx, &home);
    TEST_ASSERT_TRUE(memcmp(s_buf, s_other, 400 / 8 * 20) != 0); /* the status bar */
    TEST_ASSERT_EQUAL_MEMORY(s_buf + 400 / 8 * 20, s_other + 400 / 8 * 20, sizeof(s_buf) - 400 / 8 * 20);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_pc_layout_has_four_charts_and_three_bars);
    RUN_TEST(test_the_built_in_pc_preset_waits_for_the_first_catalogue);
    RUN_TEST(test_the_first_catalogue_fills_its_charts_and_bars);
    RUN_TEST(test_a_short_catalogue_fills_what_it_has);
    RUN_TEST(test_the_owners_pc_preset_is_left_as_it_is);
    RUN_TEST(test_a_file_from_m6d_gains_the_pc_preset_once);
    RUN_TEST(test_the_charted_fields_are_the_historys_series);
    RUN_TEST(test_a_session_shows_a_monitor_in_the_status_bar);
    return UNITY_END();
}
