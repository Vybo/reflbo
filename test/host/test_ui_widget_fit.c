#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdbool.h>
#include <stdio.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "ui_layout.h"
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_three_digit_fahrenheit_fits_the_grid);
    RUN_TEST(test_negative_temperatures_fit_the_grid);
    RUN_TEST(test_a_negative_temperature_fits_a_small_cell);
    RUN_TEST(test_a_12_hour_clock_fits_a_grid_cell);
    RUN_TEST(test_todays_two_digit_high_and_low_show_whole);
    return UNITY_END();
}
