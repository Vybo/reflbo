#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_labelled_matches_what_is_drawn);
    return UNITY_END();
}
