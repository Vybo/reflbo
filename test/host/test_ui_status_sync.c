#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "gfx_fonts.h"
#include "ui_internal.h"
#include "unity.h"

/* D43 (spec §5.2): the last good sync's time in the status bar. FIX_NOW is Friday 25 September 2026, 20:48 in
 * Prague. */

#define HOURS(h) ((time_t)(h) * 3600)
#define WED_1405 (FIX_NOW - 2 * 86400 - HOURS(6) - 43 * 60) /* two days before, at 14:05 */

static uint8_t s_a[400 * 300 / 8], s_b[400 * 300 / 8];
static char s_text[48];

void setUp(void) {}

void tearDown(void) {}

static ui_context_t context(void)
{
    ui_context_t ctx;
    ui_preset_t preset;
    TEST_ASSERT_TRUE(fixture_dashboard("home", &ctx, &preset));
    return ctx;
}

static void test_sync_time_reads_today_a_weekday_or_a_date(void)
{
    ui_context_t ctx = context();
    ui_sync_time_text(&ctx, FIX_NOW - 45 * 60, false, s_text, sizeof(s_text));
    TEST_ASSERT_EQUAL_STRING("20:03", s_text);
    ui_sync_time_text(&ctx, FIX_NOW - HOURS(20) - 48 * 60, false, s_text, sizeof(s_text)); /* 00:00 today */
    TEST_ASSERT_EQUAL_STRING("00:00", s_text);
    ui_sync_time_text(&ctx, FIX_NOW - HOURS(20) - 49 * 60, false, s_text, sizeof(s_text)); /* 23:59 yesterday */
    TEST_ASSERT_EQUAL_STRING("Thu 23:59", s_text);
    ui_sync_time_text(&ctx, WED_1405, false, s_text, sizeof(s_text));
    TEST_ASSERT_EQUAL_STRING("Wed 14:05", s_text);
    ui_sync_time_text(&ctx, FIX_NOW - 6 * 86400, false, s_text, sizeof(s_text)); /* six days: still its weekday */
    TEST_ASSERT_EQUAL_STRING("Sat 20:48", s_text);
    ui_sync_time_text(&ctx, FIX_NOW - 7 * 86400, false, s_text, sizeof(s_text)); /* a week: the date */
    TEST_ASSERT_EQUAL_STRING("18 Sep", s_text);
}

static void test_sync_time_follows_12h_and_the_language(void)
{
    ui_context_t ctx = context();
    ctx.clock_24h = false;
    ui_sync_time_text(&ctx, FIX_NOW - 45 * 60, false, s_text, sizeof(s_text));
    TEST_ASSERT_EQUAL_STRING("8:03 PM", s_text);
    ui_sync_time_text(&ctx, WED_1405, false, s_text, sizeof(s_text));
    TEST_ASSERT_EQUAL_STRING("Wed 2:05 PM", s_text);
    ctx.clock_24h = true;
    ctx.lang = lang_get("cs");
    char expected[32];
    snprintf(expected, sizeof(expected), "%s 14:05", ctx.lang->weekdays_short[3]);
    ui_sync_time_text(&ctx, WED_1405, false, s_text, sizeof(s_text));
    TEST_ASSERT_EQUAL_STRING(expected, s_text);
    ui_sync_time_text(&ctx, FIX_NOW - 7 * 86400, false, s_text, sizeof(s_text));
    TEST_ASSERT_EQUAL_STRING("18. 9.", s_text);
}

static void test_sync_time_from_a_later_day_shows_its_date(void)
{
    ui_context_t ctx = context(); /* the clock went back after the sync */
    ui_sync_time_text(&ctx, FIX_NOW + 86400, false, s_text, sizeof(s_text));
    TEST_ASSERT_EQUAL_STRING("26 Sep", s_text);
}

static void test_brief_sync_time_drops_an_earlier_days_time(void)
{
    ui_context_t ctx = context();
    ui_sync_time_text(&ctx, FIX_NOW - 45 * 60, true, s_text, sizeof(s_text));
    TEST_ASSERT_EQUAL_STRING("20:03", s_text);
    ui_sync_time_text(&ctx, WED_1405, true, s_text, sizeof(s_text));
    TEST_ASSERT_EQUAL_STRING("Wed", s_text);
    ui_sync_time_text(&ctx, FIX_NOW - 7 * 86400, true, s_text, sizeof(s_text));
    TEST_ASSERT_EQUAL_STRING("18 Sep", s_text);
}

static void test_sync_time_fit_shortens_then_gives_up(void)
{
    ui_context_t ctx = context();
    const gfx_font_t *f = &gfx_font_sans_12;
    int full = gfx_text_width(f, "Wed 14:05"), brief = gfx_text_width(f, "Wed");
    TEST_ASSERT_TRUE(ui_sync_time_fit(&ctx, WED_1405, f, full, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("Wed 14:05", s_text);
    TEST_ASSERT_TRUE(ui_sync_time_fit(&ctx, WED_1405, f, full - 1, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("Wed", s_text);
    TEST_ASSERT_FALSE(ui_sync_time_fit(&ctx, WED_1405, f, brief - 1, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("", s_text);
}

/* `name` on the profile in use into `buf`, with the option and the time as given. */
static void render(const char *name, bool status_sync, time_t ok_at, bool status_clock, uint8_t *buf, gfx_fb_t *fb)
{
    ui_context_t ctx;
    ui_preset_t preset;
    TEST_ASSERT_TRUE_MESSAGE(fixture_dashboard(name, &ctx, &preset), name);
    preset.status_sync = status_sync;
    preset.status_clock = status_clock;
    ctx.sync_ok_at = ok_at;
    gfx_fb_init(fb, buf, 400, 300);
    ui_draw_dashboard(fb, &ctx, &preset, NULL);
}

/* The columns where the two frames differ: [*lo, *hi], or -1 for none; every difference must be in the status bar. */
static void diff_columns(const gfx_fb_t *a, const gfx_fb_t *b, int *lo, int *hi)
{
    *lo = -1;
    *hi = -1;
    for (int y = 0; y < a->height; y++) {
        for (int x = 0; x < a->width; x++) {
            if (gfx_get_pixel(a, x, y) != gfx_get_pixel(b, x, y)) {
                TEST_ASSERT_TRUE_MESSAGE(y < UI_STATUS_H, "only the status bar changes");
                *lo = *lo < 0 || x < *lo ? x : *lo;
                *hi = x > *hi ? x : *hi;
            }
        }
    }
}

static void test_status_sync_off_unknown_or_unset_clock_draws_nothing_new(void)
{
    gfx_fb_t a, b;
    int lo, hi;
    render("home", false, WED_1405, false, s_a, &a);
    render("home", true, 0, false, s_b, &b); /* no good sync since the cold boot */
    diff_columns(&a, &b, &lo, &hi);
    TEST_ASSERT_EQUAL_INT(-1, lo);
    render("home_invalid", false, WED_1405, false, s_a, &a);
    render("home_invalid", true, WED_1405, false, s_b, &b); /* the clock unset: "Set time" says it all */
    diff_columns(&a, &b, &lo, &hi);
    TEST_ASSERT_EQUAL_INT(-1, lo);
}

/* After the marks (failed sync, web, Wi-Fi: three) and before the clock in the middle. */
static void test_status_sync_goes_after_the_marks_and_before_the_clock(void)
{
    gfx_fb_t a, b;
    int lo, hi;
    render("home_always_rejoining", false, WED_1405, true, s_a, &a);
    render("home_always_rejoining", true, WED_1405, true, s_b, &b);
    diff_columns(&a, &b, &lo, &hi);
    int marks_end = 4 + 3 * 20;
    TEST_ASSERT_GREATER_OR_EQUAL_INT(marks_end, lo);
    TEST_ASSERT_LESS_THAN_INT(120, hi); /* the clock's 160 px in the middle */
    TEST_ASSERT_LESS_THAN_INT(marks_end + 4, lo); /* right after them */
}

/* Without the clock it may run to the battery's text: today's time fits beside three marks. */
static void test_status_sync_without_the_clock_fits_beside_three_marks(void)
{
    gfx_fb_t a, b;
    int lo, hi;
    render("home_always_rejoining", false, FIX_NOW - 45 * 60, false, s_a, &a);
    render("home_always_rejoining", true, FIX_NOW - 45 * 60, false, s_b, &b);
    diff_columns(&a, &b, &lo, &hi);
    TEST_ASSERT_GREATER_THAN_INT(0, lo);
    TEST_ASSERT_GREATER_THAN_INT(gfx_text_width(&gfx_font_sans_12, "20:0"), hi - lo + 1); /* all of "20:03" */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sync_time_reads_today_a_weekday_or_a_date);
    RUN_TEST(test_sync_time_follows_12h_and_the_language);
    RUN_TEST(test_sync_time_from_a_later_day_shows_its_date);
    RUN_TEST(test_brief_sync_time_drops_an_earlier_days_time);
    RUN_TEST(test_sync_time_fit_shortens_then_gives_up);
    RUN_TEST(test_status_sync_off_unknown_or_unset_clock_draws_nothing_new);
    RUN_TEST(test_status_sync_goes_after_the_marks_and_before_the_clock);
    RUN_TEST(test_status_sync_without_the_clock_fits_beside_three_marks);
    return UNITY_END();
}
