#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "lang.h"
#include "ui_visual.h"
#include "unity.h"

/* Show as (spec §5.3, D44): which visuals have room, a chart's spans, the range each draws between, and that none
 * draws outside its cell. Their looks are pinned by the goldens (test_ui_visual_golden.c). */

static uint8_t s_buf[400 * 300 / 8];
static gfx_fb_t s_fb;
static ui_series_t *s_series;

void setUp(void)
{
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    s_series = calloc(1, sizeof(ui_series_t));
}

void tearDown(void)
{
    free(s_series);
}

static ui_value_t value(const char *label, const char *text, const char *unit, float num)
{
    ui_value_t v;
    memset(&v, 0, sizeof(v));
    v.kind = UI_FK_NUMBER;
    v.state = UI_VALUE_FRESH;
    v.label = label;
    snprintf(v.text, sizeof(v.text), "%s", text);
    snprintf(v.unit, sizeof(v.unit), "%s", unit);
    v.has_num = true;
    v.num = num;
    return v;
}

static void test_each_visual_needs_its_room(void)
{
    TEST_ASSERT_TRUE(ui_visual_fits(UI_VIS_CHART, 80, 40));
    TEST_ASSERT_FALSE(ui_visual_fits(UI_VIS_CHART, 79, 40));
    TEST_ASSERT_FALSE(ui_visual_fits(UI_VIS_CHART, 80, 39));
    TEST_ASSERT_TRUE(ui_visual_fits(UI_VIS_GAUGE, 60, 40));
    TEST_ASSERT_FALSE(ui_visual_fits(UI_VIS_GAUGE, 59, 40));
    TEST_ASSERT_TRUE(ui_visual_fits(UI_VIS_RING, 40, 40));
    TEST_ASSERT_FALSE(ui_visual_fits(UI_VIS_RING, 40, 39));
    TEST_ASSERT_TRUE(ui_visual_fits(UI_VIS_BAR, 60, 16));
    TEST_ASSERT_FALSE(ui_visual_fits(UI_VIS_BAR, 60, 15));
    TEST_ASSERT_TRUE(ui_visual_fits(UI_VIS_VALUE, 1, 1));
    TEST_ASSERT_FALSE(ui_visual_fits(UI_VIS_COUNT, 400, 300));
}

static void test_a_charts_span_and_its_words(void)
{
    static const struct {
        uint32_t s;
        bool ok;
        const char *text;
    } k[] = {
        { 9, false, "9 s" },        { 10, true, "10 s" },    { 60, true, "60 s" },     { 90, true, "90 s" },
        { 120, true, "2 min" },     { 300, true, "5 min" },  { 150, true, "150 s" },   { 3600, true, "1 h" },
        { 5400, false, "90 min" },   { 3601, false, "3601 s" }, { 7200, true, "2 h" }, { 7300, false, "7300 s" },
        { 86400, true, "24 h" },    { 90000, false, "25 h" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        char text[16];
        TEST_ASSERT_EQUAL_MESSAGE(k[i].ok, ui_visual_span_ok(k[i].s), k[i].text);
        ui_visual_span_text(k[i].s, text, sizeof(text));
        TEST_ASSERT_EQUAL_STRING(k[i].text, text);
    }
}

static void test_the_range_takes_its_own_bounds_then_the_fields_then_automatic(void)
{
    ui_value_t v = value("CPU", "61", "%", 61.0f);
    v.has_range = true;
    v.range_min = 0.0f;
    v.range_max = 100.0f;
    ui_visual_t bar = { .as = UI_VIS_BAR };
    float lo, hi;
    ui_visual_range(&bar, &v, false, 0, 0, &lo, &hi);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lo);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, hi);
    bar.max_mode = UI_LIMIT_VALUE;
    bar.max = 80.0f;
    ui_visual_range(&bar, &v, false, 0, 0, &lo, &hi);
    TEST_ASSERT_EQUAL_FLOAT(80.0f, hi);
    /* a chart's automatic bounds: the values in view, a tenth's room, rounded */
    ui_visual_t chart = { .as = UI_VIS_CHART, .min_mode = UI_LIMIT_AUTO, .max_mode = UI_LIMIT_AUTO };
    ui_visual_range(&chart, &v, true, 52.0f, 72.0f, &lo, &hi);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, lo);
    TEST_ASSERT_EQUAL_FLOAT(75.0f, hi);
    ui_visual_range(&chart, &v, true, 1.21f, 1.25f, &lo, &hi); /* a small range keeps its decimals */
    TEST_ASSERT_TRUE(lo <= 1.21f && lo >= 1.19f);
    TEST_ASSERT_TRUE(hi >= 1.25f && hi <= 1.27f);
    chart.min_mode = UI_LIMIT_FIELD; /* the field's minimum, an automatic maximum */
    ui_visual_range(&chart, &v, true, 52.0f, 72.0f, &lo, &hi);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lo);
    TEST_ASSERT_EQUAL_FLOAT(75.0f, hi);
    /* without the field's range: 0 to a round number past the value */
    ui_value_t t = value("Indoor", "21.3", "\xc2\xb0" "C", 21.3f);
    ui_visual_t gauge = { .as = UI_VIS_GAUGE };
    ui_visual_range(&gauge, &t, false, 0, 0, &lo, &hi);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lo);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, hi);
    t.num = -12.0f;
    ui_visual_range(&gauge, &t, false, 0, 0, &lo, &hi);
    TEST_ASSERT_EQUAL_FLOAT(-20.0f, lo);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, hi);
    t.has_num = false; /* no value and no range: still a range to draw in */
    ui_visual_range(&gauge, &t, false, 0, 0, &lo, &hi);
    TEST_ASSERT_TRUE(lo < hi);
    ui_visual_range(&chart, &t, false, 0, 0, &lo, &hi);
    TEST_ASSERT_TRUE(lo < hi);
    /* bounds the wrong way round still leave a range */
    gauge.min_mode = gauge.max_mode = UI_LIMIT_VALUE;
    gauge.min = gauge.max = 5.0f;
    ui_visual_range(&gauge, &t, false, 0, 0, &lo, &hi);
    TEST_ASSERT_TRUE(lo < hi);
}

/* Every pixel outside `r` still white. */
static void assert_inside(gfx_rect_t r, const char *what)
{
    for (int y = 0; y < 300; y++) {
        for (int x = 0; x < 400; x++) {
            bool in = x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
            if (!in && gfx_get_pixel(&s_fb, x, y)) {
                char msg[96];
                snprintf(msg, sizeof(msg), "%s drew at %d,%d outside %dx%d", what, x, y, r.w, r.h);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

static void test_no_visual_draws_outside_its_cell(void)
{
    for (uint32_t i = 0; i < 600; i++) {
        ui_series_push(s_series, i * 1000, (float)((i * 37) % 150) - 20.0f); /* past the range both ways */
    }
    static const int k_sizes[][2] = { { 400, 279 }, { 199, 99 }, { 132, 77 }, { 100, 69 }, { 80, 40 }, { 60, 40 },
                                      { 40, 40 },   { 60, 16 },  { 66, 279 }, { 400, 34 }, { 41, 20 } };
    ui_value_t big = value("Network download rate", "123456.7", "MB/s", 123456.7f);
    ui_value_t none = value("GPU", "", "%", 0);
    none.state = UI_VALUE_MISSING;
    none.has_num = false;
    const ui_value_t *values[] = { &big, &none };
    for (int as = UI_VIS_CHART; as < UI_VIS_COUNT; as++) {
        for (size_t k = 0; k < sizeof(k_sizes) / sizeof(k_sizes[0]); k++) {
            int w = k_sizes[k][0], h = k_sizes[k][1];
            if (!ui_visual_fits((ui_vis_as_t)as, w, h)) {
                continue;
            }
            for (int n = 0; n < 2; n++) {
                for (int numbers = 0; numbers < 2; numbers++) {
                    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 400, 300 }, GFX_WHITE);
                    gfx_rect_t r = { (int16_t)((400 - w) / 2), (int16_t)((300 - h) / 2), (int16_t)w, (int16_t)h };
                    ui_visual_t vis = { .as = (uint8_t)as, .numbers = numbers, .span_s = 300,
                                        .min_mode = UI_LIMIT_VALUE, .max_mode = UI_LIMIT_VALUE, .min = 0, .max = 100 };
                    ui_visual_draw(&s_fb, r, values[n], &vis, s_series, 599 * 1000, lang_get("en"));
                    char what[48];
                    snprintf(what, sizeof(what), "visual %d at %dx%d", as, w, h);
                    assert_inside(r, what);
                }
            }
        }
    }
}

static void test_a_chart_without_history_draws_its_frame(void)
{
    ui_value_t v = value("CPU", "61", "%", 61.0f);
    ui_visual_t chart = { .as = UI_VIS_CHART, .span_s = 60, .numbers = true };
    gfx_rect_t r = { 10, 10, 199, 99 };
    ui_visual_draw(&s_fb, r, &v, &chart, NULL, 0, lang_get("en"));
    int black = 0;
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            black += gfx_get_pixel(&s_fb, x, y);
        }
    }
    TEST_ASSERT_TRUE(black > 100); /* the label, the value and the baseline */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_visual_needs_its_room);
    RUN_TEST(test_a_charts_span_and_its_words);
    RUN_TEST(test_the_range_takes_its_own_bounds_then_the_fields_then_automatic);
    RUN_TEST(test_no_visual_draws_outside_its_cell);
    RUN_TEST(test_a_chart_without_history_draws_its_frame);
    return UNITY_END();
}
