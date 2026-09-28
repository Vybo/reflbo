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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_three_digit_fahrenheit_fits_the_grid);
    RUN_TEST(test_negative_temperatures_fit_the_grid);
    RUN_TEST(test_a_negative_temperature_fits_a_small_cell);
    RUN_TEST(test_a_12_hour_clock_fits_a_grid_cell);
    return UNITY_END();
}
