#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "gfx_fonts.h"
#include "gfx_icons.h"
#include "ui_internal.h"
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

/* Whether any pixel in x0..x1, y0..y1 is set; a byte at a time, as the fit tests draw a million cells. */
static bool inked(int x0, int x1, int y0, int y1)
{
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= s_fb.width ? s_fb.width - 1 : x1, y1 = y1 >= s_fb.height ? s_fb.height - 1 : y1;
    if (x0 > x1) {
        return false;
    }
    uint8_t first = (uint8_t)(0xFFu >> (x0 & 7)), last = (uint8_t)(0xFFu << (7 - (x1 & 7)));
    for (int y = y0; y <= y1; y++) {
        const uint8_t *row = s_fb.buf + y * s_fb.stride;
        int b0 = x0 >> 3, b1 = x1 >> 3;
        if (b0 == b1) {
            if (row[b0] & first & last) {
                return true;
            }
            continue;
        }
        if ((row[b0] & first) || (row[b1] & last)) {
            return true;
        }
        for (int b = b0 + 1; b < b1; b++) {
            if (row[b]) {
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

/* How tall the ink in columns [x0, x1] of `r` is: a number's digits where nothing else is drawn. */
static int ink_height_in(gfx_rect_t r, int x0, int x1)
{
    int first = -1, last = -1;
    for (int y = r.y; y < r.y + r.h; y++) {
        if (inked(x0, x1, y, y)) {
            first = first < 0 ? y : first;
            last = y;
        }
    }
    return first < 0 ? 0 : last - first + 1;
}

/* A number keeps its size as its digits change: in the 130 px face a "5" dips 2 px below the
 * baseline and a "1" none, so fitting the height digit by digit made 17.5 °C draw smaller than
 * 17.1 °C in the same cell. */
static void test_a_number_keeps_its_size_as_its_digits_change(void)
{
    static const int k_temps[] = { 1710, 1750 }; /* 17.1 °C and 17.5 °C */
    int heights[2];
    for (int i = 0; i < 2; i++) {
        ui_context_t ctx = fixture_context();
        fixture_single(&s_fix_ds, k_temps[i], 3000);
        gfx_rect_t r = { 0, 21, 400, 127 }; /* XL: 102 px under the label */
        gfx_fb_init(&s_fb, s_buf, 400, 300);
        gfx_clear(&s_fb, GFX_WHITE);
        ui_draw_cell(&s_fb, r, &ctx, UI_FIELD_ENV_TEMP, UI_STALE_STALE);
        heights[i] = ink_height_in(r, 150, 250);
    }
    TEST_ASSERT_EQUAL_INT(heights[0], heights[1]);
}

/* The data a split cell's field is drawn from: 0 fresh in English, 1 fresh in Czech (its decimal
 * comma and longer words), 2 three hours old in Czech with a forecast two days old, 3 nothing yet,
 * 4 a 12-hour clock, 5 a hot day in °F (100.8 °F inside, 102 °F out), 6 frost in Czech (-12,5 °C
 * inside, -13 °C out, a dew point of -18,7 °C), 7 the clock with its seconds; from M6c 8 a thunderstorm
 * by day and rain today, 9 snow showers at night in Czech and a battery charging at 100 %, 10 Monday
 * 28 September in Czech, a public holiday, 11 a polar night (89.9° N), 12 a polar day (89.9° S) in Czech,
 * 13 Wednesday 30 September on a 12-hour clock, 14 the largest values (-23.5 °C at 100 %, 123 days of battery, an
 * air quality index of 250, a UV index of 13, PM at 255 µg/m³, pollen at 6 500 grains/m³). */
#define VARIANTS 15

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
    if (variant == 5 || variant == 6) {
        fixture_forecast_shift(&s_fix_ds, variant == 5 ? 388 : -125);
    }
    if (variant == 8 || variant == 9) { /* other skies: their icons differ in size */
        static ds_weather_t w;
        w = *ds_weather(&s_fix_ds);
        w.now_code = variant == 8 ? 95 : 86;
        w.now_is_day = variant == 8;
        w.days[0].code = variant == 8 ? 63 : 75;
        ds_set_weather(&s_fix_ds, &w);
    }
    if (variant == 9) {
        ctx.lang = lang_get("cs");
        ds_set_battery(&s_fix_ds, 100, 4180, DS_BAT_CHARGING, FIX_NOW);
    }
    if (variant == 10 || variant == 13) { /* another day: a holiday, a long weekday; the forecast doesn't cover it */
        int days = variant == 10 ? 3 : 5;
        ctx.lang = lang_get(variant == 10 ? "cs" : "en");
        ctx.clock_24h = variant != 13;
        ctx.now = FIX_NOW + days * 86400;
        ctx.local_day = FIX_DAY + days;
        ctx.local.tm_mday += days;
        ctx.local.tm_wday = (ctx.local.tm_wday + days) % 7;
        ctx.local.tm_yday += days;
        fixture_fill(&s_fix_ds, ctx.now);
    }
    if (variant == 14) { /* the largest values: -23.5 °C at 100 %, 123 days of battery, the air at its worst */
        ds_set_env(&s_fix_ds, -2350, 10000, FIX_NOW, FIX_DAY);
        ds_set(&s_fix_ds, DS_BAT_DAYS, 1234, FIX_NOW);
        static ds_air_t a;
        a = *ds_air(&s_fix_ds);
        for (int i = 0; i < DS_WX_HOURS; i++) {
            a.aqi[i] = 250, a.pm25[i] = 255, a.pm10[i] = 255, a.uv10[i] = 125;
        }
        for (int d = 0; d < DS_WX_DAYS; d++) {
            for (int t = 0; t < DS_POLLEN_TYPES; t++) {
                a.pollen[d][t] = 65000;
            }
        }
        ds_set_air(&s_fix_ds, &a);
    }
    if (variant == 11 || variant == 12) { /* the sun neither rises nor sets */
        ctx.lang = lang_get(variant == 12 ? "cs" : "en");
        ctx.lat_e4 = variant == 11 ? 899000 : -899000;
    }
    return ctx;
}

/* Every cell size a legal split tree can make (spec §5.2): the area, then both parts of each split,
 * at most 23 splits deep, while both parts stay at least 40×20 (M6c). */
static struct {
    int16_t w, h;
    uint8_t depth;
} s_cells[4096];
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
    TEST_ASSERT_EQUAL_INT(2451, s_cell_count);
    for (int variant = 0; variant < VARIANTS; variant++) {
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

/* XS cells (D34): narrower than 90 or lower than 40, down to 40×20, every threshold of the XS drawing on both sides
 * (one line from 120 px of width or under 44 px of height, a 24 px symbol from 34 px, a 24 px stacked symbol from
 * 60). */
static const int16_t k_xs_w[] = { 40, 41, 44, 48, 50, 55, 60, 66, 70, 75, 80, 85, 89 };
static const int16_t k_xs_h[] = { 20, 21, 22, 24, 26, 28, 30, 33, 34, 36, 39, 40, 43, 44, 45, 50, 55, 59, 60, 69,
                                  80, 93, 100, 139, 279 };
static const int16_t k_xs_wide_w[] = { 90, 100, 119, 120, 133, 149, 150, 199, 200, 266, 399, 400 };
static const int16_t k_xs_low_h[] = { 20, 21, 22, 24, 26, 28, 30, 33, 34, 36, 39 };

/* Whether glyph `g` of `f` is drawn with its top left at (x0, y0), a blank pixel all round it. */
static bool glyph_at(const gfx_font_t *f, const gfx_glyph_t *g, int x0, int y0)
{
    int rb = (g->width + 7) / 8;
    for (int y = -1; y <= g->height; y++) {
        for (int x = -1; x <= g->width; x++) {
            bool want = x >= 0 && y >= 0 && x < g->width && y < g->height &&
                        ((f->bitmap[g->offset + y * rb + x / 8] >> (7 - x % 8)) & 1);
            if (gfx_get_pixel(&s_fb, x0 + x, y0 + y) != want) {
                return false;
            }
        }
    }
    return true;
}

/* The first inked pixel of a glyph's or an icon's bits, in reading order: where a match of it starts. */
static void first_ink(const uint8_t *bits, int width, int height, int *fx, int *fy)
{
    int rb = (width + 7) / 8;
    for (int i = 0; i < width * height; i++) {
        if ((bits[(i / width) * rb + (i % width) / 8] >> (7 - (i % width) % 8)) & 1) {
            *fx = i % width, *fy = i / width;
            return;
        }
    }
    *fx = *fy = 0;
}

/* The next inked pixel of `r` from (*x, *y) on, in reading order, a blank byte at a time; false after the last. */
static bool next_ink(gfx_rect_t r, int *x, int *y)
{
    for (; *y < r.y + r.h; (*y)++, *x = r.x) {
        const uint8_t *row = s_fb.buf + *y * s_fb.stride;
        for (; *x < r.x + r.w; (*x)++) {
            if (row[*x >> 3] == 0) {
                *x |= 7; /* the rest of a blank byte */
            } else if ((row[*x >> 3] >> (7 - (*x & 7))) & 1) {
                return true;
            }
        }
    }
    return false;
}

/* Whether `r` shows an ellipsis ("…") in a face a small value takes: something was cut to fit. */
static bool has_ellipsis(gfx_rect_t r)
{
    static const gfx_font_t *const k_faces[] = { &gfx_font_sans_12, &gfx_font_sans_16, &gfx_font_sans_bold_16,
                                                 &gfx_font_sans_bold_20, &gfx_font_sans_bold_28 };
    enum { FACES = sizeof(k_faces) / sizeof(k_faces[0]) };
    const gfx_glyph_t *g[FACES];
    int fx[FACES], fy[FACES];
    for (int i = 0; i < FACES; i++) {
        g[i] = gfx_font_glyph(k_faces[i], 0x2026);
        first_ink(k_faces[i]->bitmap + g[i]->offset, g[i]->width, g[i]->height, &fx[i], &fy[i]);
    }
    for (int x = r.x, y = r.y; next_ink(r, &x, &y); x++) {
        for (int i = 0; i < FACES; i++) {
            int x0 = x - fx[i], y0 = y - fy[i];
            if (x0 >= r.x && y0 >= r.y && x0 + g[i]->width <= r.x + r.w && y0 + g[i]->height <= r.y + r.h &&
                glyph_at(k_faces[i], g[i], x0, y0)) {
                return true;
            }
        }
    }
    return false;
}

static bool icon_ink(const gfx_bitmap_t *icon, int x, int y)
{
    return (icon->bits[y * ((icon->width + 7) / 8) + x / 8] >> (7 - x % 8)) & 1;
}

/* Whether `icon` shows in `area` of cell `r`: its whole box matched, ink and blank alike (a solid bar holds every
 * pattern's ink), looked for only from inked pixels. */
static bool icon_in(gfx_rect_t area, gfx_rect_t r, const gfx_bitmap_t *icon)
{
    int fx, fy;
    first_ink(icon->bits, icon->width, icon->height, &fx, &fy);
    for (int x = area.x, y = area.y; next_ink(area, &x, &y); x++) {
        int x0 = x - fx, y0 = y - fy;
        if (x0 < r.x || y0 < area.y || x0 + icon->width > r.x + r.w || y0 + icon->height > r.y + r.h) {
            continue;
        }
        bool same = true;
        for (int iy = 0; iy < icon->height && same; iy++) {
            for (int ix = 0; ix < icon->width && same; ix++) {
                same = gfx_get_pixel(&s_fb, x0 + ix, y0 + iy) == icon_ink(icon, ix, iy);
            }
        }
        if (same) {
            return true;
        }
    }
    return false;
}

/* Whether `r` shows the stale mark where draw_age() puts it, in the cell's bottom 26 rows. */
static bool stale_mark(gfx_rect_t r)
{
    gfx_rect_t bottom = { r.x, (int16_t)(r.h > 26 ? r.y + r.h - 26 : r.y), r.w, 0 };
    bottom.h = (int16_t)(r.y + r.h - bottom.y);
    return icon_in(bottom, r, &gfx_icon_stale_16);
}

/* The kinds whose value must never be cut: a number, a time, today's high and low, the sun's times. */
static bool never_cut(ui_field_kind_t kind)
{
    return kind == UI_FK_NUMBER || kind == UI_FK_TIME || kind == UI_FK_BATTERY || kind == UI_FK_WEATHER_DAY ||
           kind == UI_FK_SUN;
}

static void check_xs_cell(const ui_context_t *ctx, int w, int h, int variant)
{
    for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
        const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
        if (ui_split_field_size(info->kind, w, h) != UI_SIZE_XS) {
            continue;
        }
        gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
        gfx_fb_init(&s_fb, s_buf, 400, 300);
        gfx_clear(&s_fb, GFX_WHITE);
        ui_draw_cell(&s_fb, r, ctx, (ui_field_id_t)f, UI_STALE_STALE);
        char msg[80];
        snprintf(msg, sizeof(msg), "%s at %d×%d (XS), variant %d", info->id, w, h, variant);
        TEST_ASSERT_TRUE_MESSAGE(inked(r.x + 2, r.x + w - 3, r.y + 2, r.y + h - 3), msg);
        TEST_ASSERT_FALSE_MESSAGE(never_cut(info->kind) && has_ellipsis(r), msg);
        TEST_ASSERT_FALSE_MESSAGE(stale_mark(r), msg);
        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y, r.y + 1), msg);
        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y + h - 2, r.y + h - 1), msg);
        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + 1, r.y, r.y + h - 1), msg);
        TEST_ASSERT_FALSE_MESSAGE(inked(r.x + w - 2, r.x + w - 1, r.y, r.y + h - 1), msg);
    }
}

/* Every small field shows in every XS cell and stays clear of its edges, in every set of data. */
static void test_every_small_field_fits_every_xs_cell(void)
{
    for (int variant = 0; variant < VARIANTS; variant++) {
        ui_context_t ctx = split_context(variant);
        for (size_t i = 0; i < sizeof(k_xs_w) / sizeof(k_xs_w[0]); i++) {
            for (size_t j = 0; j < sizeof(k_xs_h) / sizeof(k_xs_h[0]); j++) {
                check_xs_cell(&ctx, k_xs_w[i], k_xs_h[j], variant);
            }
        }
        for (size_t i = 0; i < sizeof(k_xs_wide_w) / sizeof(k_xs_wide_w[0]); i++) {
            for (size_t j = 0; j < sizeof(k_xs_low_h) / sizeof(k_xs_low_h[0]); j++) {
                check_xs_cell(&ctx, k_xs_wide_w[i], k_xs_low_h[j], variant);
            }
        }
    }
}

/* S cells under 80 px tall (D34): the icon beside the value, narrow or wide, at every height S takes. */
static const int16_t k_s_w[] = { 90,  91,  100, 110, 119, 120, 129, 133, 140,
                                 149, 150, 151, 160, 199, 200, 266, 399, 400 };
static const int16_t k_s_h[] = { 40, 41, 42, 44, 46, 48, 49, 50, 51, 53, 55, 56, 59, 60, 61, 65, 69, 70, 75, 79 };

static void check_s_cell(const ui_context_t *ctx, int w, int h, int variant)
{
    for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
        const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
        if (ui_split_field_size(info->kind, w, h) != UI_SIZE_S) {
            continue;
        }
        gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
        gfx_fb_init(&s_fb, s_buf, 400, 300);
        gfx_clear(&s_fb, GFX_WHITE);
        ui_draw_cell(&s_fb, r, ctx, (ui_field_id_t)f, UI_STALE_STALE);
        char msg[80];
        snprintf(msg, sizeof(msg), "%s at %d×%d (S), variant %d", info->id, w, h, variant);
        TEST_ASSERT_TRUE_MESSAGE(inked(r.x + 2, r.x + w - 3, r.y + 2, r.y + h - 3), msg);
        TEST_ASSERT_FALSE_MESSAGE(never_cut(info->kind) && has_ellipsis(r), msg);
        TEST_ASSERT_FALSE_MESSAGE(stale_mark(r), msg); /* under 80 px the status bar's warning stands for it */
        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y, r.y + 1), msg);
        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y + h - 2, r.y + h - 1), msg);
        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + 1, r.y, r.y + h - 1), msg);
        TEST_ASSERT_FALSE_MESSAGE(inked(r.x + w - 2, r.x + w - 1, r.y, r.y + h - 1), msg);
    }
}

/* Every small field that draws at S in a short cell shows, clear of the cell's edges, in every set of data; a
 * field draws at S from the heights spec §5.2 gives (M6c). */
static void test_every_small_field_fits_every_short_s_cell(void)
{
    for (int variant = 0; variant < VARIANTS; variant++) {
        ui_context_t ctx = split_context(variant);
        for (size_t i = 0; i < sizeof(k_s_w) / sizeof(k_s_w[0]); i++) {
            for (size_t j = 0; j < sizeof(k_s_h) / sizeof(k_s_h[0]); j++) {
                check_s_cell(&ctx, k_s_w[i], k_s_h[j], variant);
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_NOW, 90, 40)); /* the heights measured */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_LEVEL, 90, 40));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_SUN, 90, 49));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_POLLEN, 90, 42));
}

/* Runs of inked rows in `r`: the lines a widget drew, one above the other. */
static int ink_bands(gfx_rect_t r)
{
    int bands = 0;
    bool in = false;
    for (int y = r.y; y < r.y + r.h; y++) {
        bool row = inked(r.x, r.x + r.w - 1, y, y);
        bands += row && !in;
        in = row;
    }
    return bands;
}

static int draw_xs(const ui_context_t *ctx, ui_field_id_t field, int w, int h)
{
    gfx_rect_t r = { 0, 21, (int16_t)w, (int16_t)h };
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    gfx_clear(&s_fb, GFX_WHITE);
    ui_draw_cell(&s_fb, r, ctx, field, UI_STALE_STALE);
    return ink_bands(r);
}

/* XS draws one line, like the status bar, where the cell is 120 px wide or more or under 44 px tall; otherwise
 * the symbol over the value, the date as its weekday over its day (spec §5.3, D34). */
static void test_xs_draws_one_line_or_the_symbol_over_the_value(void)
{
    ui_context_t ctx = split_context(0);
    TEST_ASSERT_EQUAL_INT(1, draw_xs(&ctx, UI_FIELD_ENV_TEMP, 200, 22));
    TEST_ASSERT_EQUAL_INT(1, draw_xs(&ctx, UI_FIELD_ENV_TEMP, 89, 43));
    TEST_ASSERT_EQUAL_INT(1, draw_xs(&ctx, UI_FIELD_WX_NOW, 120, 60));
    TEST_ASSERT_TRUE(draw_xs(&ctx, UI_FIELD_ENV_TEMP, 66, 69) >= 2);
    TEST_ASSERT_TRUE(draw_xs(&ctx, UI_FIELD_WX_NOW, 66, 69) >= 2);
    TEST_ASSERT_EQUAL_INT(2, draw_xs(&ctx, UI_FIELD_DATE_DAY, 50, 69)); /* "Fri" over "25" */
}

/* One field in a cell w × h at the bottom right of the panel, its pixels copied out: what tells two apart. */
static void cell_bits(const ui_context_t *ctx, ui_field_id_t field, int w, int h, uint8_t *out)
{
    gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    gfx_clear(&s_fb, GFX_WHITE);
    ui_draw_cell(&s_fb, r, ctx, field, UI_STALE_STALE);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            out[y * w + x] = (uint8_t)gfx_get_pixel(&s_fb, r.x + x, r.y + y);
        }
    }
}

/* XS keeps what S shows: today's low and high apart from the temperature (↓ and ↑ beside the thermometer) and
 * a charging battery's bolt; and the Moon keeps its disc in a narrow line (D34). */
static void test_xs_keeps_the_marks_s_shows(void)
{
    static uint8_t a[200 * 93], b[200 * 93], c[200 * 93];
    static const int16_t k_cells[][2] = { { 200, 22 }, { 66, 69 }, { 50, 93 }, { 89, 30 } };
    ui_context_t ctx = fixture_context();
    for (size_t i = 0; i < sizeof(k_cells) / sizeof(k_cells[0]); i++) {
        int w = k_cells[i][0], h = k_cells[i][1];
        size_t n = (size_t)(w * h);
        fixture_single(&s_fix_ds, 2340, 4500); /* one reading: the low and the high equal it */
        cell_bits(&ctx, UI_FIELD_ENV_TEMP, w, h, a);
        cell_bits(&ctx, UI_FIELD_ENV_TEMP_MIN, w, h, b);
        cell_bits(&ctx, UI_FIELD_ENV_TEMP_MAX, w, h, c);
        TEST_ASSERT_FALSE(memcmp(a, b, n) == 0);
        TEST_ASSERT_FALSE(memcmp(a, c, n) == 0);
        TEST_ASSERT_FALSE(memcmp(b, c, n) == 0);
        ds_set_battery(&s_fix_ds, 100, 4180, DS_BAT_CHARGING, FIX_NOW);
        cell_bits(&ctx, UI_FIELD_BAT_LEVEL, w, h, a);
        ds_set_battery(&s_fix_ds, 100, 4180, DS_BAT_DISCHARGING, FIX_NOW);
        cell_bits(&ctx, UI_FIELD_BAT_LEVEL, w, h, b);
        TEST_ASSERT_FALSE(memcmp(a, b, n) == 0);
    }
    for (int w = 40; w <= 119; w++) { /* the disc stays, at the start of the line */
        gfx_rect_t r = { (int16_t)(400 - w), 278, (int16_t)w, 22 };
        gfx_fb_init(&s_fb, s_buf, 400, 300);
        gfx_clear(&s_fb, GFX_WHITE);
        ui_draw_cell(&s_fb, r, &ctx, UI_FIELD_MOON_PHASE, UI_STALE_STALE);
        TEST_ASSERT_TRUE(inked(r.x + 3, r.x + 18, r.y + 3, r.y + 18));
        TEST_ASSERT_FALSE(has_ellipsis(r));
    }
}

/* A short S cell shows the date's weekday and day, or the Moon's short name, before it cuts a longer form; a
 * stale value shows its mark where there is room for it, as in Classic's small slots (D34). */
static void test_short_s_cells_show_a_short_form_before_cutting(void)
{
    static const int16_t k_cells[][2] = { { 100, 69 }, { 125, 69 }, { 133, 45 }, { 99, 41 } };
    for (int variant = 0; variant < VARIANTS; variant++) {
        ui_context_t ctx = split_context(variant);
        for (size_t i = 0; i < sizeof(k_cells) / sizeof(k_cells[0]); i++) {
            int16_t w = k_cells[i][0], h = k_cells[i][1];
            gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), w, h };
            static const ui_field_id_t k_fields[] = { UI_FIELD_DATE_DAY, UI_FIELD_MOON_PHASE };
            for (size_t f = 0; f < 2; f++) {
                gfx_fb_init(&s_fb, s_buf, 400, 300);
                gfx_clear(&s_fb, GFX_WHITE);
                ui_draw_cell(&s_fb, r, &ctx, k_fields[f], UI_STALE_STALE);
                char msg[64];
                snprintf(msg, sizeof(msg), "field %d at %d×%d, variant %d", (int)k_fields[f], r.w, r.h, variant);
                TEST_ASSERT_FALSE_MESSAGE(has_ellipsis(r), msg);
            }
        }
    }
    ui_context_t ctx = split_context(2); /* three hours old */
    gfx_rect_t r = { 300, 189, 100, 111 };
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    gfx_clear(&s_fb, GFX_WHITE);
    ui_draw_cell(&s_fb, r, &ctx, UI_FIELD_ENV_TEMP, UI_STALE_STALE);
    TEST_ASSERT_TRUE(stale_mark(r));
}

/* A one-line XS sun cell shows its set beside its rise wherever both fit: in a smaller face, then without a 12-hour
 * suffix, before the set is given up (M6c review). */
static void test_xs_sun_shows_its_set_where_both_times_fit(void)
{
    int checked = 0;
    for (int variant = 0; variant < VARIANTS; variant++) {
        ui_context_t ctx = split_context(variant);
        ui_value_t v;
        ui_resolve(&ctx, UI_FIELD_SUN_TIMES, &v);
        if (v.state == UI_VALUE_MISSING || v.polar) {
            continue;
        }
        char brief[2][16];
        snprintf(brief[0], sizeof(brief[0]), "%s", v.text);
        snprintf(brief[1], sizeof(brief[1]), "%s", v.extra);
        for (int k = 0; k < 2; k++) {
            char *space = strrchr(brief[k], ' '); /* "6:44" for "6:44 AM" */
            if (space != NULL) {
                *space = '\0';
            }
        }
        for (int w = 40; w <= 400; w += 3) {
            for (int h = 20; h <= 43; h++) {
                if (ui_split_field_size(UI_FK_SUN, w, h) != UI_SIZE_XS) {
                    continue;
                }
                int icon = h >= 34 ? 24 : 16; /* the smallest face, the shortest form: does the pair fit at all? */
                int need = 3 + icon + 3 + gfx_text_width(&gfx_font_sans_12, brief[0]) + 5 + icon + 3 +
                           gfx_text_width(&gfx_font_sans_12, brief[1]) + 2;
                if (need > w) {
                    continue;
                }
                gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
                gfx_fb_init(&s_fb, s_buf, 400, 300);
                gfx_clear(&s_fb, GFX_WHITE);
                ui_draw_cell(&s_fb, r, &ctx, UI_FIELD_SUN_TIMES, UI_STALE_STALE);
                char msg[64];
                snprintf(msg, sizeof(msg), "sun.times at %d×%d, variant %d", w, h, variant);
                TEST_ASSERT_TRUE_MESSAGE(icon_in(r, r, icon == 24 ? &gfx_icon_sunset_24 : &gfx_icon_sunset_16), msg);
                checked++;
            }
        }
    }
    TEST_ASSERT_TRUE(checked > 1000);
}

/* Where a one-line XS sun cell has room for one time, it shows the next event beside its icon: the sunrise before it,
 * the sunset in the day, tomorrow's sunrise after the sunset (owner, 2026-10-05). */
static void test_xs_sun_shows_its_next_event_where_one_time_fits(void)
{
    static const struct {
        int hour;
        bool set;
    } k_times[] = { { 5, false }, { 12, true }, { 20, false } };
    int checked = 0;
    for (size_t t = 0; t < sizeof(k_times) / sizeof(k_times[0]); t++) {
        ui_context_t ctx = split_context(0);
        ctx.now = FIX_NOW + (k_times[t].hour - 20) * 3600; /* 20:48 local, moved by whole hours */
        ctx.local = fixture_local(k_times[t].hour, 48, 0);
        for (int w = 40; w <= 400; w += 3) {
            for (int h = 20; h <= 43; h++) {
                if (ui_split_field_size(UI_FK_SUN, w, h) != UI_SIZE_XS) {
                    continue;
                }
                int icon = h >= 34 ? 24 : 16, time_w = gfx_text_width(&gfx_font_sans_12, "18:45");
                if (3 + icon + 3 + time_w + 5 + icon + 3 + time_w + 2 <= w || 3 + icon + 3 + time_w + 2 > w) {
                    continue; /* both times fit, or not even one beside its icon */
                }
                gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
                gfx_fb_init(&s_fb, s_buf, 400, 300);
                gfx_clear(&s_fb, GFX_WHITE);
                ui_draw_cell(&s_fb, r, &ctx, UI_FIELD_SUN_TIMES, UI_STALE_STALE);
                char msg[64];
                snprintf(msg, sizeof(msg), "sun.times at %d×%d, %02d:48", w, h, k_times[t].hour);
                const gfx_bitmap_t *rise = icon == 24 ? &gfx_icon_sunrise_24 : &gfx_icon_sunrise_16;
                const gfx_bitmap_t *set = icon == 24 ? &gfx_icon_sunset_24 : &gfx_icon_sunset_16;
                TEST_ASSERT_TRUE_MESSAGE(icon_in(r, r, k_times[t].set ? set : rise), msg);
                TEST_ASSERT_FALSE_MESSAGE(icon_in(r, r, k_times[t].set ? rise : set), msg);
                checked++;
            }
        }
    }
    TEST_ASSERT_TRUE(checked > 100);
}

/* The age mark goes only where nothing is drawn under it: in every cell a tree makes, a stale value's mark, where it
 * is drawn, has nothing else within 1 px of its icon's ink or its age's (M6c review: narrow S cells 81-104 px tall,
 * and some M cells, had it on the value). The mark alone, drawn as draw_age() places it, tells its pixels from the
 * value's. */
static void test_the_age_mark_never_lands_on_the_value(void)
{
    static uint8_t mark_buf[sizeof(s_buf)];
    gfx_fb_t mark;
    gfx_fb_init(&mark, mark_buf, 400, 300);
    const gfx_font_t *af = &gfx_font_sans_12;
    s_cell_count = 0;
    reach(400, 279, 0);
    int drawn = 0;
    for (int lang = 0; lang < 2; lang++) {
        ui_context_t ctx = split_context(2); /* readings three hours old, a forecast two days old */
        ctx.lang = lang_get(lang ? "cs" : "en");
        for (int i = 0; i < s_cell_count; i++) {
            int w = s_cells[i].w, h = s_cells[i].h;
            gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
            for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
                const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
                ui_value_t v;
                ui_resolve(&ctx, (ui_field_id_t)f, &v);
                if (ui_split_field_size(info->kind, w, h) <= UI_SIZE_XS || info->kind == UI_FK_RAIN_MAP ||
                    v.state != UI_VALUE_STALE) {
                    continue;
                }
                char age[16];
                ui_format_age(ctx.lang, v.age_s, age, sizeof(age));
                int ax = r.x + w - 6 - gfx_text_width(af, age), base = r.y + h - 6 - (af->line_height - af->ascent);
                gfx_clear(&mark, GFX_WHITE);
                gfx_text(&mark, af, ax, base, age, GFX_BLACK);
                gfx_bitmap(&mark, ax - 18, base - 13, &gfx_icon_stale_16, GFX_BLACK);
                gfx_fb_init(&s_fb, s_buf, 400, 300);
                gfx_clear(&s_fb, GFX_WHITE);
                ui_draw_cell(&s_fb, r, &ctx, (ui_field_id_t)f, UI_STALE_STALE);
                bool shown = true; /* every pixel of the mark set: it was drawn */
                for (int y = base - 13; shown && y <= base + 3; y++) {
                    for (int x = ax - 18; shown && x < r.x + w - 6; x++) {
                        shown = !gfx_get_pixel(&mark, x, y) || gfx_get_pixel(&s_fb, x, y);
                    }
                }
                if (!shown) {
                    continue;
                }
                drawn++;
                char msg[80];
                snprintf(msg, sizeof(msg), "%s at %d×%d, %s", info->id, w, h, lang ? "cs" : "en");
                int ix0 = 16, iy0 = 16, ix1 = -1, iy1 = -1; /* the icon's ink, then the age's */
                for (int y = 0; y < 16; y++) {
                    for (int x = 0; x < 16; x++) {
                        if (icon_ink(&gfx_icon_stale_16, x, y)) {
                            ix0 = x < ix0 ? x : ix0, ix1 = x > ix1 ? x : ix1;
                            iy0 = y < iy0 ? y : iy0, iy1 = y > iy1 ? y : iy1;
                        }
                    }
                }
                const int boxes[2][4] = {
                    { ax - 18 + ix0, base - 13 + iy0, ax - 18 + ix1, base - 13 + iy1 },
                    { ax, base - ui_ink_above(af, age), r.x + w - 7, base + ui_ink_below(af, age) },
                };
                for (int b = 0; b < 2; b++) {
                    for (int y = boxes[b][1] - 1; y <= boxes[b][3] + 1; y++) {
                        for (int x = boxes[b][0] - 1; x <= boxes[b][2] + 1; x++) {
                            TEST_ASSERT_EQUAL_MESSAGE(gfx_get_pixel(&mark, x, y), gfx_get_pixel(&s_fb, x, y), msg);
                        }
                    }
                }
            }
        }
    }
    TEST_ASSERT_TRUE(drawn > 1000); /* where there is room, the mark still shows */
}

/* A narrow XS cell stacks the Moon's disc over its short name, or its illumination where the name doesn't fit:
 * never a cut word, in any phase, in English or Czech (M6c review: "Gib…" at 49×92). */
static void test_xs_moon_gives_its_illumination_before_a_cut(void)
{
    static const int16_t k_w[] = { 40, 41, 44, 49, 50, 55, 60, 64, 66, 70, 75, 80, 89 };
    static const int16_t k_h[] = { 44, 45, 50, 59, 60, 69, 92, 139 };
    for (int lang = 0; lang < 2; lang++) {
        for (int d = 0; d < 30; d += 2) { /* a lunar month */
            ui_context_t ctx = fixture_context();
            ctx.lang = lang_get(lang ? "cs" : "en");
            ctx.now = FIX_NOW + d * 86400;
            for (size_t i = 0; i < sizeof(k_w) / sizeof(k_w[0]); i++) {
                for (size_t j = 0; j < sizeof(k_h) / sizeof(k_h[0]); j++) {
                    gfx_rect_t r = { (int16_t)(400 - k_w[i]), (int16_t)(300 - k_h[j]), k_w[i], k_h[j] };
                    gfx_fb_init(&s_fb, s_buf, 400, 300);
                    gfx_clear(&s_fb, GFX_WHITE);
                    ui_draw_cell(&s_fb, r, &ctx, UI_FIELD_MOON_PHASE, UI_STALE_STALE);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "moon.phase at %d×%d, day %d, %s", r.w, r.h, d, lang ? "cs" : "en");
                    TEST_ASSERT_FALSE_MESSAGE(has_ellipsis(r), msg);
                }
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
    RUN_TEST(test_a_number_keeps_its_size_as_its_digits_change);
    RUN_TEST(test_every_field_fits_every_cell_a_split_can_make);
    RUN_TEST(test_a_field_without_room_draws_nothing);
    RUN_TEST(test_xs_sun_shows_its_set_where_both_times_fit);
    RUN_TEST(test_xs_sun_shows_its_next_event_where_one_time_fits);
    RUN_TEST(test_the_age_mark_never_lands_on_the_value);
    RUN_TEST(test_xs_moon_gives_its_illumination_before_a_cut);
    RUN_TEST(test_every_small_field_fits_every_xs_cell);
    RUN_TEST(test_xs_draws_one_line_or_the_symbol_over_the_value);
    RUN_TEST(test_xs_keeps_the_marks_s_shows);
    RUN_TEST(test_every_small_field_fits_every_short_s_cell);
    RUN_TEST(test_short_s_cells_show_a_short_form_before_cutting);
    return UNITY_END();
}
