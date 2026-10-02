#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdbool.h>
#include <stdio.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "ui_layout.h"
#include "ui_split.h"
#include "unity.h"

/* Values too wide for their slot shrink to fit (spec §5.3): clipping would cut them at the
 * slot's edges, which reads as a different number ("00.8" for "100.8"). */

static uint8_t s_buf[400 * 300 / 8];
static gfx_fb_t s_fb;

void setUp(void) {}
void tearDown(void) {}

static void render(const char *name)
{
    ui_context_t ctx;
    ui_preset_t preset;
    TEST_ASSERT_TRUE_MESSAGE(fixture_dashboard(name, &ctx, &preset), name);
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    ui_draw_dashboard(&s_fb, &ctx, &preset);
}

static bool inked(int x0, int x1, int y0, int y1)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (gfx_get_pixel(&s_fb, x, y)) {
                return true;
            }
        }
    }
    return false;
}

/* The slot shows something, and nothing touches the columns just inside its edges (separators
 * lie on the edges themselves). */
static void check_fits(const char *fixture, ui_layout_id_t id, const char *slot)
{
    const ui_layout_t *layout = ui_layout(id);
    gfx_rect_t r = layout->slots[ui_slot_by_name(layout, slot)].rect;
    char msg[64];
    snprintf(msg, sizeof(msg), "%s, slot %s", fixture, slot);
    TEST_ASSERT_TRUE_MESSAGE(inked(r.x + 10, r.x + r.w - 11, r.y + 30, r.y + r.h - 11), msg);
    TEST_ASSERT_FALSE_MESSAGE(inked(r.x + 1, r.x + 2, r.y + 2, r.y + r.h - 3), msg);
    TEST_ASSERT_FALSE_MESSAGE(inked(r.x + r.w - 3, r.x + r.w - 2, r.y + 2, r.y + r.h - 3), msg);
}

static void test_three_digit_fahrenheit_fits_the_grid(void)
{
    render("indoor_hot_f"); /* 100.8 °F in the temperature, low and high cells */
    check_fits("indoor_hot_f", UI_LAYOUT_GRID, "g1");
    check_fits("indoor_hot_f", UI_LAYOUT_GRID, "g4");
    check_fits("indoor_hot_f", UI_LAYOUT_GRID, "g5");
}

static void test_negative_temperatures_fit_the_grid(void)
{
    render("indoor_frost"); /* -12.5 °C, and a dew point of -18.7 °C in g3 */
    check_fits("indoor_frost", UI_LAYOUT_GRID, "g1");
    check_fits("indoor_frost", UI_LAYOUT_GRID, "g3");
    check_fits("indoor_frost", UI_LAYOUT_GRID, "g4");
    check_fits("indoor_frost", UI_LAYOUT_GRID, "g5");
}

static void test_a_negative_temperature_fits_a_small_cell(void)
{
    render("home_frost");
    check_fits("home_frost", UI_LAYOUT_CLASSIC, "s1");
}

static void test_a_12_hour_clock_fits_a_grid_cell(void)
{
    render("grid_clock_12h"); /* "12:58" with PM beside it */
    check_fits("grid_clock_12h", UI_LAYOUT_GRID, "g1");
}

/* The Weather preset after a sync, with today's high and low replaced (0.1 °C). */
static void render_today(int max_c10, int min_c10)
{
    ui_context_t ctx;
    ui_preset_t preset;
    TEST_ASSERT_TRUE(fixture_dashboard("weather_now", &ctx, &preset));
    ds_weather_t w = *ds_weather(&s_fix_ds);
    w.days[0].max_c10 = (int16_t)max_c10;
    w.days[0].min_c10 = (int16_t)min_c10;
    ds_set_weather(&s_fix_ds, &w);
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    ui_draw_dashboard(&s_fb, &ctx, &preset);
}

static void grab_slot(ui_layout_id_t id, const char *slot, uint8_t *out)
{
    const ui_layout_t *layout = ui_layout(id);
    gfx_rect_t r = layout->slots[ui_slot_by_name(layout, slot)].rect;
    for (int y = 0; y < r.h; y++) {
        for (int x = 0; x < r.w; x++) {
            out[y * r.w + x] = (uint8_t)gfx_get_pixel(&s_fb, r.x + x, r.y + y);
        }
    }
}

/* Cut with an ellipsis ("23° / ..."), a two-digit low draws the same whatever it is. */
static void check_low_shows(int max_c10, int low_a, int low_b)
{
    static uint8_t a[200 * 80], b[200 * 80]; /* the Weather layout's today slot */
    char msg[48];
    snprintf(msg, sizeof(msg), "high %d, lows %d and %d", max_c10, low_a, low_b);
    render_today(max_c10, low_a);
    check_fits("weather_now", UI_LAYOUT_WEATHER, "today");
    grab_slot(UI_LAYOUT_WEATHER, "today", a);
    render_today(max_c10, low_b);
    check_fits("weather_now", UI_LAYOUT_WEATHER, "today");
    grab_slot(UI_LAYOUT_WEATHER, "today", b);
    TEST_ASSERT_FALSE_MESSAGE(memcmp(a, b, sizeof(a)) == 0, msg);
}

static void test_todays_two_digit_high_and_low_show_whole(void)
{
    check_low_shows(234, 132, 192);   /* 23° / 13°, the owner's first sync */
    check_low_shows(-124, -186, -156); /* the widest in °C */
}

/* A number fits its slot's height too (spec §5.3): in Czech the comma's tail of "23,4" in Classic's
 * main slot (400×125, XL) reached its bottom edge and was cut there, so it read "23.4". */
static void test_a_czech_number_fits_classics_main_slot(void)
{
    ui_context_t ctx;
    ui_preset_t preset;
    TEST_ASSERT_TRUE(fixture_dashboard("home_cs", &ctx, &preset));
    preset.slots[0] = UI_FIELD_ENV_TEMP;
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    ui_draw_dashboard(&s_fb, &ctx, &preset);
    check_fits("home_cs, a temperature", UI_LAYOUT_CLASSIC, "main");
    gfx_rect_t r = ui_layout(UI_LAYOUT_CLASSIC)->slots[0].rect;
    TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + r.w - 1, r.y + r.h - 2, r.y + r.h - 1), "the slot's bottom edge");
}

/* The data a split cell's field is drawn from: 0 fresh in English, 1 fresh in Czech (its decimal
 * comma and longer words), 2 three hours old in Czech with a forecast two days old, 3 nothing yet,
 * 4 a 12-hour clock, 5 a hot day in °F (100.8 °F), 6 frost in Czech (-12,5 °C, a dew point of
 * -18,7 °C), 7 the clock with its seconds. */
static ui_context_t split_context(int variant)
{
    ui_context_t ctx = fixture_context();
    ctx.lang = lang_get(variant == 1 || variant == 2 || variant == 6 ? "cs" : "en");
    ctx.clock_24h = variant != 4;
    ctx.fahrenheit = variant == 5;
    ctx.seconds = variant == 7;
    ctx.local = fixture_local(variant == 4 ? 12 : 20, variant == 4 ? 58 : 48, 37);
    if (variant == 2) {
        fixture_fill(&s_fix_ds, FIX_NOW - 3 * 3600);
    } else if (variant == 5 || variant == 6) {
        fixture_single(&s_fix_ds, variant == 5 ? 3820 : -1250, variant == 5 ? 3000 : 6000);
    }
    if (variant == 3) {
        ds_init(&s_fix_ds);
    } else {
        fixture_forecast(&s_fix_ds, variant == 2 ? FIX_NOW - 50 * 3600 : FIX_NOW - 3600);
        fixture_rain_now(&s_fix_ds);
    }
    return ctx;
}

/* Every cell size a legal split tree can make (spec §5.2): the area, then both parts of each split,
 * at most 7 splits deep, while both parts stay at least 90×40. */
static struct {
    int16_t w, h;
    uint8_t depth;
} s_cells[512];
static int s_cell_count;

static void reach(int w, int h, int depth)
{
    int i = 0;
    while (i < s_cell_count && (s_cells[i].w != w || s_cells[i].h != h)) {
        i++;
    }
    if (i < s_cell_count && s_cells[i].depth <= depth) {
        return; /* found before, with as many splits left */
    }
    if (i == s_cell_count) {
        TEST_ASSERT_TRUE(s_cell_count < (int)(sizeof(s_cells) / sizeof(s_cells[0])));
        s_cell_count++;
    }
    s_cells[i].w = (int16_t)w;
    s_cells[i].h = (int16_t)h;
    s_cells[i].depth = (uint8_t)depth;
    for (int r = UI_RATIO_1_4; depth < UI_SPLIT_CELLS - 1 && r <= UI_RATIO_3_4; r++) {
        int a = ui_split_first(w, r), b = w - a - 1; /* columns */
        if (a >= UI_SPLIT_MIN_W && b >= UI_SPLIT_MIN_W) {
            reach(a, h, depth + 1);
            reach(b, h, depth + 1);
        }
        a = ui_split_first(h, r), b = h - a - 1; /* rows */
        if (a >= UI_SPLIT_MIN_H && b >= UI_SPLIT_MIN_H) {
            reach(w, a, depth + 1);
            reach(w, b, depth + 1);
        }
    }
}

/* Split cells (spec §5.2, D31): in every cell a tree can make, every field it can show draws at the
 * size ui_split.c gives it, shows, and stays clear of the cell's edges. The rain map fills its cell. */
static void test_every_field_fits_every_cell_a_split_can_make(void)
{
    s_cell_count = 0;
    reach(400, 279, 0);
    TEST_ASSERT_EQUAL_INT(336, s_cell_count);
    for (int variant = 0; variant < 8; variant++) {
        ui_context_t ctx = split_context(variant);
        for (int i = 0; i < s_cell_count; i++) {
            int w = s_cells[i].w, h = s_cells[i].h;
            for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
                const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
                int size = ui_split_field_size(info->kind, w, h);
                if (size < 0 || info->kind == UI_FK_RAIN_MAP) {
                    continue;
                }
                gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
                gfx_fb_init(&s_fb, s_buf, 400, 300);
                gfx_clear(&s_fb, GFX_WHITE);
                ui_draw_cell(&s_fb, r, &ctx, (ui_field_id_t)f, UI_STALE_STALE);
                char msg[80];
                snprintf(msg, sizeof(msg), "%s at %d×%d (size %d), variant %d", info->id, w, h, size, variant);
                TEST_ASSERT_TRUE_MESSAGE(inked(r.x + 2, r.x + w - 3, r.y + 2, r.y + h - 3), msg);
                TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y, r.y + 1), msg);
                TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y + h - 2, r.y + h - 1), msg);
                TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + 1, r.y, r.y + h - 1), msg);
                TEST_ASSERT_FALSE_MESSAGE(inked(r.x + w - 2, r.x + w - 1, r.y, r.y + h - 1), msg);
            }
        }
    }
}

/* A field with no room in its cell isn't drawn at all, rather than cut. */
static void test_a_field_without_room_draws_nothing(void)
{
    ui_context_t ctx = split_context(0);
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    gfx_clear(&s_fb, GFX_WHITE);
    ui_draw_cell(&s_fb, (gfx_rect_t){ 0, 21, 199, 69 }, &ctx, UI_FIELD_WX_HOURLY, UI_STALE_STALE); /* needs M */
    TEST_ASSERT_FALSE(inked(0, 399, 0, 299));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_three_digit_fahrenheit_fits_the_grid);
    RUN_TEST(test_negative_temperatures_fit_the_grid);
    RUN_TEST(test_a_negative_temperature_fits_a_small_cell);
    RUN_TEST(test_a_12_hour_clock_fits_a_grid_cell);
    RUN_TEST(test_todays_two_digit_high_and_low_show_whole);
    RUN_TEST(test_a_czech_number_fits_classics_main_slot);
    RUN_TEST(test_every_field_fits_every_cell_a_split_can_make);
    RUN_TEST(test_a_field_without_room_draws_nothing);
    return UNITY_END();
}
