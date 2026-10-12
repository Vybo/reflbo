#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "gfx_fonts.h"
#include "ui_dashboard.h"
#include "ui_internal.h"
#include "ui_layout.h"
#include "ui_split.h"
#include "unity.h"

/* D42 (spec §5.2, §5.4): a slot's or cell's own title, and a split cell's forced title. */

static uint8_t s_buf[400 * 300 / 8];
static uint8_t s_other[sizeof(s_buf)], s_blank[sizeof(s_buf)];
static int s_labelled, s_unlabelled; /* the cases each way: both must come up */
static gfx_fb_t s_fb;

void setUp(void) {}
void tearDown(void) {}

static void clear(void)
{
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    gfx_clear(&s_fb, GFX_WHITE);
}

/* The fixture's day with every source's data: the weather, the air, the PV forecast and readings, a radar frame. */
static ui_context_t full_context(void)
{
    ui_context_t ctx = fixture_context();
    fixture_solar_ctx(&ctx, true, true);
    ctx.radar = fixture_radar(fixture_chmu(ctx.now - 8 * 60));
    return ctx;
}

/* No data at all: every field missing. */
static ui_context_t empty_context(void)
{
    ui_context_t ctx = fixture_context();
    ds_init(&s_fix_ds);
    return ctx;
}

/* Draws `field` in r at `size` with `label` for its label, into s_fb, cleared first. */
static void draw_with_label(const ui_context_t *ctx, ui_field_id_t field, gfx_rect_t r, ui_size_t size,
                            ui_stale_policy_t policy, const char *label)
{
    ui_value_t v;
    ui_resolve(ctx, field, &v);
    v.label = label;
    clear();
    ui_widget_draw(&s_fb, r, size, &v, policy, ctx->lang);
}

/* Each kind's cells to try at each size: the size's least width and height for the kind, narrow and wide, and the
 * whole split area. */
static int cells_for(ui_field_kind_t kind, gfx_rect_t out[16])
{
    gfx_rect_t area = ui_split_area();
    int n = 0;
    for (int s = UI_SIZE_XS; s <= UI_SIZE_XL; s++) {
        for (int narrow = 0; narrow < 2; narrow++) {
            int h = ui_split_need((ui_size_t)s, kind, narrow);
            int w = narrow ? ui_split_min_w((ui_size_t)s) : UI_SPLIT_NARROW_W;
            if (h < 0 || w > area.w || h > area.h) {
                continue;
            }
            if (narrow && w >= UI_SPLIT_NARROW_W) {
                continue;
            }
            out[n++] = (gfx_rect_t){ area.x, area.y, (int16_t)w, (int16_t)h };
        }
    }
    out[n++] = area;
    return n;
}

/* ui_widget_labelled() says true exactly where a different label draws different pixels. */
static void check_labelled(const ui_context_t *ctx, ui_stale_policy_t policy, const char *what)
{
    for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
        const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
        gfx_rect_t cells[16];
        int n = cells_for(info->kind, cells);
        for (int i = 0; i < n; i++) {
            gfx_rect_t r = cells[i];
            int size = ui_split_field_size(info->kind, r.w, r.h);
            if (size < 0) {
                continue;
            }
            draw_with_label(ctx, (ui_field_id_t)f, r, (ui_size_t)size, policy, "Wq");
            memcpy(s_other, s_buf, sizeof(s_buf));
            draw_with_label(ctx, (ui_field_id_t)f, r, (ui_size_t)size, policy, "");
            bool drawn = memcmp(s_other, s_buf, sizeof(s_buf)) != 0;
            memcpy(s_blank, s_buf, sizeof(s_buf));
            ui_value_t v;
            ui_resolve(ctx, (ui_field_id_t)f, &v);
            v.label = "Wq";
            char msg[96];
            snprintf(msg, sizeof(msg), "%s at %d×%d (size %d), %s, %s", info->id, r.w, r.h, size, what,
                     policy == UI_STALE_HIDE ? "hide" : "stale");
            TEST_ASSERT_EQUAL_MESSAGE(drawn, ui_widget_labelled(&s_fb, r, (ui_size_t)size, &v, policy, ctx->lang), msg);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_blank, s_buf, sizeof(s_buf), msg); /* the dry run drew nothing */
            *(drawn ? &s_labelled : &s_unlabelled) += 1;
        }
    }
}

static void test_labelled_matches_what_is_drawn(void)
{
    ui_context_t full = full_context();
    check_labelled(&full, UI_STALE_STALE, "fresh");
    ui_context_t empty = empty_context();
    check_labelled(&empty, UI_STALE_STALE, "missing");
    check_labelled(&empty, UI_STALE_HIDE, "missing");
    char counts[64];
    snprintf(counts, sizeof(counts), "%d cases with a label, %d without", s_labelled, s_unlabelled);
    TEST_MESSAGE(counts);
    TEST_ASSERT_TRUE(s_labelled > 50 && s_unlabelled > 50);
}

/* ---- titles on the dashboard ---- */

static uint8_t s_plain[sizeof(s_buf)];

/* Draws a cell into s_buf, cleared first. */
static void cell(const ui_context_t *ctx, gfx_rect_t r, ui_field_id_t field, ui_stale_policy_t policy,
                 const char *title, bool force)
{
    clear();
    ui_draw_titled_cell(&s_fb, r, ctx, field, policy, title, force);
}

/* Whether any pixel of r is ink in s_buf. */
static bool inked(gfx_rect_t r)
{
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            if (gfx_get_pixel(&s_fb, x, y)) {
                return true;
            }
        }
    }
    return false;
}

/* Whether r is the same in s_buf and in `other`, a frame of the same profile. */
static bool same_in(gfx_rect_t r, const uint8_t *other)
{
    gfx_fb_t o;
    gfx_fb_init(&o, (uint8_t *)other, 400, 300);
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            if (gfx_get_pixel(&s_fb, x, y) != gfx_get_pixel(&o, x, y)) {
                return false;
            }
        }
    }
    return true;
}

/* A cell `w` wide whose `kind` draws at `size`, as short as it can be: its height. */
static int least_height(ui_field_kind_t kind, ui_size_t size, int w)
{
    for (int h = 1; h <= ui_split_area().h; h++) {
        if (ui_split_field_size(kind, w, h) == (int)size) {
            return h;
        }
    }
    TEST_FAIL_MESSAGE("no such cell");
    return 0;
}

static gfx_rect_t at(int w, int h)
{
    gfx_rect_t area = ui_split_area();
    return (gfx_rect_t){ area.x, area.y, (int16_t)w, (int16_t)h };
}

/* The title takes the label's place, in the widget's own font and position. */
static void test_a_title_replaces_the_label(void)
{
    ui_context_t ctx = full_context();
    gfx_rect_t r = at(200, least_height(UI_FK_NUMBER, UI_SIZE_M, 200));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_STALE, "Obývák", false);
    memcpy(s_plain, s_buf, sizeof(s_buf));
    draw_with_label(&ctx, UI_FIELD_ENV_TEMP, r, UI_SIZE_M, UI_STALE_STALE, "Obývák");
    TEST_ASSERT_EQUAL_MEMORY(s_buf, s_plain, sizeof(s_buf));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_STALE, NULL, false);
    TEST_ASSERT_FALSE(same_in(r, s_plain)); /* the field's own label is another word */
}

/* A fixed layout's slot takes its title too. */
static void test_a_fixed_slot_takes_its_title(void)
{
    ui_context_t ctx;
    ui_preset_t preset;
    TEST_ASSERT_TRUE(fixture_dashboard("indoor", &ctx, &preset));
    const ui_layout_t *layout = ui_layout((ui_layout_id_t)preset.layout);
    ui_titles_t titles = { .text = { [0] = "Obývák" } };
    clear();
    ui_draw_dashboard(&s_fb, &ctx, &preset, NULL);
    memcpy(s_plain, s_buf, sizeof(s_buf));
    clear();
    ui_draw_dashboard(&s_fb, &ctx, &preset, &titles);
    TEST_ASSERT_FALSE(same_in(layout->slots[0].rect, s_plain));
    for (int i = 1; i < layout->slot_count; i++) {
        TEST_ASSERT_TRUE(same_in(layout->slots[i].rect, s_plain));
    }
}

/* Where the widget shows its label, forcing it changes nothing. */
static void test_forced_title_changes_nothing_where_a_label_shows(void)
{
    ui_context_t ctx = full_context();
    gfx_rect_t r = at(200, least_height(UI_FK_NUMBER, UI_SIZE_M, 200));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_STALE, "Obývák", false);
    memcpy(s_plain, s_buf, sizeof(s_buf));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_STALE, "Obývák", true);
    TEST_ASSERT_EQUAL_MEMORY(s_plain, s_buf, sizeof(s_buf));
}

/* An S cell tall enough: the title's line on top, the field under it at the size the rest allows. */
static void test_forced_title_goes_above_in_a_tall_small_cell(void)
{
    ui_context_t ctx = full_context();
    int w = UI_SPLIT_NARROW_W, line = 6 + gfx_font_sans_12.line_height;
    gfx_rect_t r = at(w, least_height(UI_FK_NUMBER, UI_SIZE_M, w) - 1); /* S, as tall as S gets */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_NUMBER, r.w, r.h));
    gfx_rect_t below = { r.x, (int16_t)(r.y + line), r.w, (int16_t)(r.h - line) };
    int size = ui_split_field_size(UI_FK_NUMBER, below.w, below.h);
    TEST_ASSERT_TRUE(size >= 0);
    clear();
    ui_value_t v;
    ui_resolve(&ctx, UI_FIELD_ENV_TEMP, &v);
    ui_widget_draw(&s_fb, below, (ui_size_t)size, &v, UI_STALE_STALE, ctx.lang);
    memcpy(s_plain, s_buf, sizeof(s_buf));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_STALE, "Obývák", true);
    TEST_ASSERT_TRUE(inked((gfx_rect_t){ r.x, r.y, r.w, (int16_t)line }));
    TEST_ASSERT_TRUE(same_in(below, s_plain));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_STALE, NULL, true); /* the field's own label, forced */
    TEST_ASSERT_TRUE(inked((gfx_rect_t){ r.x, r.y, r.w, (int16_t)line }));
}

/* A row too short for a line above: the title on the left, the value on the right. */
static void test_forced_title_goes_beside_in_a_short_wide_cell(void)
{
    ui_context_t ctx = full_context();
    gfx_rect_t r = at(200, least_height(UI_FK_NUMBER, UI_SIZE_XS, 200));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_STALE, "Obývák", false);
    memcpy(s_plain, s_buf, sizeof(s_buf));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_STALE, "Obývák", true);
    gfx_rect_t left = { r.x, r.y, 30, r.h };
    TEST_ASSERT_FALSE(same_in(left, s_plain));
    TEST_ASSERT_TRUE(inked(left));
    TEST_ASSERT_TRUE(inked((gfx_rect_t){ (int16_t)(r.x + r.w / 2), r.y, (int16_t)(r.w / 2), r.h })); /* the value */
}

/* The least XS cell has no room for both: the value alone, as without the title. */
static void test_forced_title_without_room_shows_the_value_alone(void)
{
    ui_context_t ctx = full_context();
    gfx_rect_t r = at(UI_SPLIT_MIN_W, UI_SPLIT_MIN_H);
    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_field_size(UI_FK_NUMBER, r.w, r.h));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_STALE, "Obývák", false);
    memcpy(s_plain, s_buf, sizeof(s_buf));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_STALE, "Obývák", true);
    TEST_ASSERT_EQUAL_MEMORY(s_plain, s_buf, sizeof(s_buf));
}

/* The stale policy that hides a missing value hides its title too. */
static void test_hidden_value_hides_its_title(void)
{
    ui_context_t ctx = empty_context();
    gfx_rect_t r = at(UI_SPLIT_NARROW_W, 60);
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_HIDE, "Obývák", true);
    TEST_ASSERT_FALSE(inked(r));
    cell(&ctx, r, UI_FIELD_ENV_TEMP, UI_STALE_PLACEHOLDER, "Obývák", true); /* a dash, and the title */
    TEST_ASSERT_TRUE(inked(r));
}

/* The split layout passes each cell its title and force (the dashboard's own path). */
static void test_the_split_layout_draws_titles(void)
{
    ui_context_t ctx;
    ui_preset_t preset;
    TEST_ASSERT_TRUE(fixture_dashboard("split_titles", &ctx, &preset));
    clear();
    ui_draw_dashboard(&s_fb, &ctx, &preset, NULL);
    memcpy(s_plain, s_buf, sizeof(s_buf));
    clear();
    ui_draw_dashboard(&s_fb, &ctx, &preset, fixture_titles("split_titles"));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(s_plain, s_buf, sizeof(s_buf)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_labelled_matches_what_is_drawn);
    RUN_TEST(test_a_title_replaces_the_label);
    RUN_TEST(test_a_fixed_slot_takes_its_title);
    RUN_TEST(test_forced_title_changes_nothing_where_a_label_shows);
    RUN_TEST(test_forced_title_goes_above_in_a_tall_small_cell);
    RUN_TEST(test_forced_title_goes_beside_in_a_short_wide_cell);
    RUN_TEST(test_forced_title_without_room_shows_the_value_alone);
    RUN_TEST(test_hidden_value_hides_its_title);
    RUN_TEST(test_the_split_layout_draws_titles);
    return UNITY_END();
}
