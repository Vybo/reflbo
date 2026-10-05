#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "gfx_fonts.h"
#include "gfx_icons.h"
#include "ui_split.h"
#include "unity.h"

/* The chart and the flow (spec §5.3, M6d): where they draw, the flow's diagram and row, its battery and arrows. The
 * goldens (dash_grid_solar, dash_weather_solar, dash_focus_solar) show them whole. */

static uint8_t s_buf[400 * 300 / 8];
static gfx_fb_t s_fb;
static ui_context_t s_ctx;

void setUp(void)
{
    s_ctx = fixture_context();
    ui_preset_t preset;
    fixture_dashboard("weather_solar", &s_ctx, &preset); /* 13:20, the sample day with a battery */
}

void tearDown(void) {}

static void draw(gfx_rect_t r, ui_field_id_t field)
{
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    gfx_clear(&s_fb, GFX_WHITE);
    ui_draw_cell(&s_fb, r, &s_ctx, field, UI_STALE_STALE);
}

/* Where `icon` is drawn whole in `area`, a blank pixel or the edge all round it; false if nowhere. */
static bool find_icon(gfx_rect_t area, const gfx_bitmap_t *icon, int *x, int *y)
{
    int rb = (icon->width + 7) / 8;
    for (int y0 = area.y; y0 + icon->height <= area.y + area.h; y0++) {
        for (int x0 = area.x; x0 + icon->width <= area.x + area.w; x0++) {
            bool same = true;
            for (int iy = 0; same && iy < icon->height; iy++) {
                for (int ix = 0; same && ix < icon->width; ix++) {
                    bool ink = (icon->bits[iy * rb + ix / 8] >> (7 - ix % 8)) & 1;
                    same = gfx_get_pixel(&s_fb, x0 + ix, y0 + iy) == ink;
                }
            }
            if (same) {
                *x = x0;
                *y = y0;
                return true;
            }
        }
    }
    return false;
}

static void snapshot(gfx_rect_t r, uint8_t *out)
{
    for (int y = 0; y < r.h; y++) {
        for (int x = 0; x < r.w; x++) {
            out[y * r.w + x] = (uint8_t)gfx_get_pixel(&s_fb, r.x + x, r.y + y);
        }
    }
}

static void test_the_chart_and_the_flow_need_m_or_more(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_CHART, 129, 279)); /* S at most */
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_FLOW, 400, 79));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_CHART, 130, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_FLOW, 130, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_field_size(UI_FK_CHART, 400, 279));
}

/* In a tall cell the panels sit above the junction, the grid left of it and the house right; in a short one all
 * of them in a row. */
static void test_the_flow_is_a_diagram_in_a_tall_cell_and_a_row_in_a_short_one(void)
{
    gfx_rect_t tall = { 200, 21, 199, 139 }, short_cell = { 200, 21, 199, 80 };
    int px, py, hx, hy, gx, gy;
    draw(tall, UI_FIELD_EN_FLOW);
    TEST_ASSERT_TRUE(find_icon(tall, &gfx_icon_solar_24, &px, &py));
    TEST_ASSERT_TRUE(find_icon(tall, &gfx_icon_house_24, &hx, &hy));
    TEST_ASSERT_TRUE(find_icon(tall, &gfx_icon_grid_24, &gx, &gy));
    TEST_ASSERT_TRUE(py + 24 < hy);       /* the panels above */
    TEST_ASSERT_EQUAL_INT(hy, gy);        /* the grid and the house level */
    TEST_ASSERT_TRUE(gx < px && px < hx); /* the grid left, the house right */
    draw(short_cell, UI_FIELD_EN_FLOW);
    TEST_ASSERT_TRUE(find_icon(short_cell, &gfx_icon_solar_24, &px, &py));
    TEST_ASSERT_TRUE(find_icon(short_cell, &gfx_icon_house_24, &hx, &hy));
    TEST_ASSERT_TRUE(find_icon(short_cell, &gfx_icon_grid_24, &gx, &gy));
    TEST_ASSERT_EQUAL_INT(py, hy); /* one row: the panels, the house, the grid */
    TEST_ASSERT_EQUAL_INT(py, gy);
    TEST_ASSERT_TRUE(px < hx && hx < gx);
}

/* The battery joins the diagram from 100 px under the heading (22 px in M), and the row from 180 px of width. */
static void test_the_battery_joins_the_flow_where_it_has_room(void)
{
    static uint8_t with[200 * 150], without[200 * 150];
    static const struct {
        int16_t w, h;
        bool joins;
    } k_cells[] = { { 199, 139, true }, { 200, 121, false }, { 200, 122, true },
                    { 199, 80, true },  { 179, 80, false },  { 133, 139, true } };
    for (size_t i = 0; i < sizeof(k_cells) / sizeof(k_cells[0]); i++) {
        gfx_rect_t r = { 0, 21, k_cells[i].w, k_cells[i].h };
        s_fix_solar.battery = true;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, with);
        s_fix_solar.battery = false;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, without);
        char msg[32];
        snprintf(msg, sizeof(msg), "%d×%d", r.w, r.h);
        bool differ = memcmp(with, without, (size_t)(r.w * r.h)) != 0;
        TEST_ASSERT_EQUAL_MESSAGE(k_cells[i].joins, differ, msg);
    }
}

/* Less than 20 W flows nowhere: no arrow, so its way makes no difference; from 20 W the arrow shows it. */
static void test_a_flow_under_20_watts_has_no_arrow(void)
{
    static uint8_t a[200 * 150], b[200 * 150];
    gfx_rect_t cells[] = { { 0, 21, 199, 139 }, { 0, 21, 199, 80 } };
    for (size_t i = 0; i < sizeof(cells) / sizeof(cells[0]); i++) {
        gfx_rect_t r = cells[i];
        s_fix_reading.grid_w = 15;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, a);
        s_fix_reading.grid_w = -15;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, b);
        TEST_ASSERT_EQUAL_MEMORY(a, b, (size_t)(r.w * r.h));
        s_fix_reading.grid_w = 25;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, a);
        s_fix_reading.grid_w = -25;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, b);
        TEST_ASSERT_FALSE(memcmp(a, b, (size_t)(r.w * r.h)) == 0);
    }
}

/* Without a reading the flow is missing, and without today's quarter hours the chart is. */
static void test_without_data_the_chart_and_the_flow_are_missing(void)
{
    ui_value_t v;
    ui_resolve(&s_ctx, UI_FIELD_PV_CHART, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("27.4", v.text); /* today's total, its heading */
    ui_resolve(&s_ctx, UI_FIELD_EN_FLOW, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    s_fix_reading.at = 0;
    ui_resolve(&s_ctx, UI_FIELD_EN_FLOW, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    s_ctx.local_day += 2; /* the forecast has no quarter hours for that day */
    ui_resolve(&s_ctx, UI_FIELD_PV_CHART, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
}

/* Whether glyph `cp` of `f` is drawn somewhere in `area`, a blank pixel all round it. */
/* A quarter hour without a reading shows the forecast (spec §11.6), whether the bars are quarter hours or hours: readings
 * every fourth quarter (an hourly sync), or once at 05:30 (the default daily one), draw as readings in every quarter
 * would, the readings being the forecast. */
static void test_a_quarter_hour_without_a_reading_shows_the_forecast(void)
{
    static uint8_t every[400 * 300], sparse[400 * 300];
    static const gfx_rect_t k_cells[] = { { 0, 21, 400, 279 }, { 0, 21, 130, 80 } }; /* quarter bars, hour bars */
    int now_q = s_ctx.local.tm_hour * 4 + s_ctx.local.tm_min / 15;
    for (size_t c = 0; c < sizeof(k_cells) / sizeof(k_cells[0]); c++) {
        gfx_rect_t r = k_cells[c];
        for (int i = 0; i < ENERGY_STEPS; i++) {
            s_fix_energy_day.q[i] = i < now_q ? s_fix_forecast.q[0][i] : ENERGY_NONE;
        }
        draw(r, UI_FIELD_PV_CHART);
        snapshot(r, every);
        for (int i = 0; i < ENERGY_STEPS; i++) {
            s_fix_energy_day.q[i] = i % 4 == 0 ? s_fix_energy_day.q[i] : ENERGY_NONE;
        }
        draw(r, UI_FIELD_PV_CHART);
        snapshot(r, sparse);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(every, sparse, (size_t)r.w * r.h, "a reading every fourth quarter");
        for (int i = 0; i < ENERGY_STEPS; i++) {
            s_fix_energy_day.q[i] = i == 22 ? s_fix_forecast.q[0][22] : ENERGY_NONE;
        }
        draw(r, UI_FIELD_PV_CHART);
        snapshot(r, sparse);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(every, sparse, (size_t)r.w * r.h, "one reading, at 05:30");
    }
}

static bool has_glyph(gfx_rect_t area, const gfx_font_t *f, uint32_t cp)
{
    const gfx_glyph_t *g = gfx_font_glyph(f, cp);
    int rb = (g->width + 7) / 8;
    for (int y0 = area.y + 1; y0 + g->height < area.y + area.h; y0++) {
        for (int x0 = area.x + 1; x0 + g->width < area.x + area.w; x0++) {
            bool same = true;
            for (int y = -1; same && y <= g->height; y++) {
                for (int x = -1; same && x <= g->width; x++) {
                    bool want = x >= 0 && y >= 0 && x < g->width && y < g->height &&
                                ((f->bitmap[g->offset + y * rb + x / 8] >> (7 - x % 8)) & 1);
                    same = gfx_get_pixel(&s_fb, x0 + x, y0 + y) == want;
                }
            }
            if (same) {
                return true;
            }
        }
    }
    return false;
}

static void draw_preset(const char *fixture)
{
    ui_preset_t preset;
    ui_context_t ctx;
    TEST_ASSERT_TRUE(fixture_dashboard(fixture, &ctx, &preset));
    s_ctx = ctx;
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    ui_draw_dashboard(&s_fb, &s_ctx, &preset);
}

static void redraw(const char *preset_id)
{
    ui_preset_t preset = fixture_preset(preset_id);
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    ui_draw_dashboard(&s_fb, &s_ctx, &preset);
}

#define EM_DASH 0x2014

/* A day the forecast has no total for shows a dash in the Solar layout's footer (Forecast.Solar's free tier
 * has two days). */
static void test_the_solar_layout_marks_a_day_without_a_total_with_a_dash(void)
{
    gfx_rect_t after = { 200, 266, 200, 34 }; /* the day after tomorrow, bottom right */
    draw_preset("solar");
    TEST_ASSERT_FALSE(has_glyph(after, &gfx_font_sans_bold_20, EM_DASH));
    s_fix_forecast.wh[2] = SOLAR_WH_NONE;
    redraw("solar");
    TEST_ASSERT_TRUE(has_glyph(after, &gfx_font_sans_bold_20, EM_DASH));
}

/* Old data raises the status bar's stale warning on both layouts, as a stale slot does (spec §5.2). */
static void test_stale_data_raises_the_status_bars_warning(void)
{
    gfx_rect_t bar = { 0, 0, 400, UI_STATUS_H };
    int x, y;
    draw_preset("solar_actual");
    TEST_ASSERT_FALSE(find_icon(bar, &gfx_icon_stale_16, &x, &y));
    s_fix_forecast.fetched = (uint32_t)(s_ctx.now - 27 * 3600); /* past its wait */
    redraw("solar");
    TEST_ASSERT_TRUE(find_icon(bar, &gfx_icon_stale_16, &x, &y));
    draw_preset("energy");
    TEST_ASSERT_FALSE(find_icon(bar, &gfx_icon_stale_16, &x, &y));
    s_fix_reading.at = (uint32_t)(s_ctx.now - 16 * 60); /* older than 15 min */
    redraw("energy");
    TEST_ASSERT_TRUE(find_icon(bar, &gfx_icon_stale_16, &x, &y));
}

/* Without a reading near midnight the day's totals to and from the grid and the own use are dashes; what was
 * produced still shows, as the inverter counts it. */
static void test_the_energy_totals_without_a_midnight_reading_are_dashes(void)
{
    gfx_rect_t totals = { 0, 200, 400, 100 };
    draw_preset("energy");
    TEST_ASSERT_FALSE(has_glyph(totals, &gfx_font_sans_bold_20, EM_DASH));
    s_fix_energy_day.base_at = 0;
    redraw("energy");
    TEST_ASSERT_TRUE(has_glyph(totals, &gfx_font_sans_bold_20, EM_DASH));
}

/* The largest values keep the layouts' numbers apart (Review Focus): today's total in the Solar layout ends before
 * the column beside it, whose labels and values keep apart too, and each of the Energy layout's totals in a row with
 * a battery ends before the next one's number. */
static void test_the_largest_totals_keep_to_their_places(void)
{
    int top = UI_STATUS_H + 1;
    draw_preset("solar");
    fixture_solar_largest(s_ctx.now);
    redraw("solar");
    TEST_ASSERT_TRUE_MESSAGE(has_glyph((gfx_rect_t){ 0, (int16_t)top, 196, 84 }, &gfx_font_sans_bold_20, 'h'),
                             "today's kWh");
    TEST_ASSERT_TRUE_MESSAGE(has_glyph((gfx_rect_t){ 196, (int16_t)(top + 56), 194, 20 }, &gfx_font_sans_16, 'e'),
                             "the column's \"Still to come\" beside its value");
    draw_preset("energy_battery");
    fixture_solar_largest(s_ctx.now);
    redraw("energy");
    for (int i = 0; i < 3; i++) { /* produced, to the grid, from it (the own use is a share): before the next number */
        TEST_ASSERT_TRUE_MESSAGE(has_glyph((gfx_rect_t){ (int16_t)(i * 100), (int16_t)(top + 222), 120, 48 },
                                           &gfx_font_sans_16, 'h'),
                                 "a total's kWh");
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_chart_and_the_flow_need_m_or_more);
    RUN_TEST(test_the_flow_is_a_diagram_in_a_tall_cell_and_a_row_in_a_short_one);
    RUN_TEST(test_the_battery_joins_the_flow_where_it_has_room);
    RUN_TEST(test_a_flow_under_20_watts_has_no_arrow);
    RUN_TEST(test_without_data_the_chart_and_the_flow_are_missing);
    RUN_TEST(test_the_solar_layout_marks_a_day_without_a_total_with_a_dash);
    RUN_TEST(test_stale_data_raises_the_status_bars_warning);
    RUN_TEST(test_the_energy_totals_without_a_midnight_reading_are_dashes);
    RUN_TEST(test_the_largest_totals_keep_to_their_places);
    RUN_TEST(test_a_quarter_hour_without_a_reading_shows_the_forecast);
    return UNITY_END();
}
