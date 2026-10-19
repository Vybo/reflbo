#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_history.h"
#include "unity.h"

/* The history behind the charts (spec §5.3, D44): each series' last 3 600 values at their own rate and 24 h of
 * one-minute minimum, mean and maximum, and the pixel columns a chart draws from them. RAM only: the app keeps it
 * in PSRAM. */

static ui_series_t *s_mem;
static ui_history_t s_h;
static ui_series_t *s;
static ui_hist_column_t s_cols[UI_HIST_VALUES];

void setUp(void)
{
    s_mem = calloc(UI_HIST_SERIES, sizeof(ui_series_t));
    TEST_ASSERT_NOT_NULL(s_mem);
    ui_history_init(&s_h, s_mem);
    const char *const names[] = { "pc.cpu_load" };
    TEST_ASSERT_EQUAL(1, ui_history_track(&s_h, names, 1));
    s = ui_history_find(&s_h, "pc.cpu_load");
    TEST_ASSERT_NOT_NULL(s);
}

void tearDown(void)
{
    free(s_mem);
}

/* `minutes` minutes of a value every 250 ms from t 0, each the index of its minute; the last push's time. */
static uint32_t quarter_seconds(int minutes)
{
    uint32_t t = 0;
    for (int k = 0; k < minutes * 240; k++) {
        t = (uint32_t)k * 250;
        ui_series_push(s, t, (float)(k / 240));
    }
    return t;
}

static void test_one_value_a_column_newest_at_the_right(void)
{
    for (uint32_t i = 0; i < 60; i++) {
        ui_series_push(s, 1000 + i * 1000, (float)i);
    }
    uint32_t now = 1000 + 59 * 1000;
    TEST_ASSERT_EQUAL(60, ui_series_columns(s, now, 60000, 60, 0, 60, s_cols));
    for (int c = 0; c < 60; c++) {
        TEST_ASSERT_EQUAL(1, s_cols[c].n);
        TEST_ASSERT_EQUAL_FLOAT((float)c, s_cols[c].mean);
        TEST_ASSERT_EQUAL_FLOAT((float)c, s_cols[c].min);
        TEST_ASSERT_EQUAL_FLOAT((float)c, s_cols[c].max);
        TEST_ASSERT_FALSE(s_cols[c].brk);
    }
    TEST_ASSERT_EQUAL(30, ui_series_columns(s, now, 30000, 30, 0, 30, s_cols)); /* half the span: the newer half */
    TEST_ASSERT_EQUAL_FLOAT(30.0f, s_cols[0].mean);
    TEST_ASSERT_EQUAL_FLOAT(59.0f, s_cols[29].mean);
}

static void test_a_column_of_many_values_keeps_their_range_and_mean(void)
{
    for (uint32_t i = 0; i < 3600; i++) {
        ui_series_push(s, i * 1000, (float)(i % 20)); /* 0..19 in every 20 s */
    }
    TEST_ASSERT_EQUAL(180, ui_series_columns(s, 3599 * 1000, 3600000, 180, 0, 180, s_cols)); /* an hour at 1 s */
    for (int c = 0; c < 180; c++) {
        TEST_ASSERT_EQUAL(20, s_cols[c].n);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, s_cols[c].min);
        TEST_ASSERT_EQUAL_FLOAT(19.0f, s_cols[c].max);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.5f, s_cols[c].mean);
    }
}

static void test_columns_without_values_are_empty(void)
{
    ui_series_push(s, 10000, 5.0f);
    ui_series_push(s, 70000, 7.0f);
    TEST_ASSERT_EQUAL(2, ui_series_columns(s, 70000, 120000, 120, 0, 120, s_cols));
    TEST_ASSERT_EQUAL(1, s_cols[59].n); /* 60 s before now */
    TEST_ASSERT_EQUAL_FLOAT(5.0f, s_cols[59].mean);
    TEST_ASSERT_EQUAL(1, s_cols[119].n);
    TEST_ASSERT_EQUAL(0, s_cols[0].n);
    TEST_ASSERT_EQUAL(0, s_cols[100].n);
    TEST_ASSERT_EQUAL(0, ui_series_columns(s, 270000, 60000, 60, 0, 60, s_cols)); /* all of them older than the span */
}

static void test_the_ring_keeps_the_last_3600_values(void)
{
    for (uint32_t i = 0; i < 3700; i++) {
        ui_series_push(s, i * 1000, (float)i);
    }
    TEST_ASSERT_EQUAL(UI_HIST_VALUES, s->count);
    TEST_ASSERT_EQUAL(3600, ui_series_columns(s, 3699 * 1000, 3600000, 3600, 0, 3600, s_cols));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, s_cols[0].mean);
    TEST_ASSERT_EQUAL_FLOAT(3699.0f, s_cols[3599].mean);
}

static void test_a_span_past_the_values_draws_from_the_minutes(void)
{
    uint32_t now = quarter_seconds(20); /* 4 800 values: the last 3 600 cover 15 min */
    TEST_ASSERT_EQUAL(20, ui_series_columns(s, now, 3600000, 60, 0, 60, s_cols));
    for (int c = 0; c < 40; c++) {
        TEST_ASSERT_EQUAL(0, s_cols[c].n);
    }
    for (int m = 0; m < 20; m++) { /* minute m in column 40 + m, the one in progress last */
        TEST_ASSERT_EQUAL_MESSAGE(1, s_cols[40 + m].n, "one minute a column");
        TEST_ASSERT_EQUAL_FLOAT((float)m, s_cols[40 + m].mean);
        TEST_ASSERT_EQUAL_FLOAT((float)m, s_cols[40 + m].min);
        TEST_ASSERT_EQUAL_FLOAT((float)m, s_cols[40 + m].max);
    }
    TEST_ASSERT_EQUAL(15, ui_series_columns(s, now, 900000, 15, 0, 15, s_cols)); /* 15 min: still the values */
}

static void test_24_hours_of_minutes(void)
{
    uint32_t now = quarter_seconds(25 * 60);
    TEST_ASSERT_EQUAL(UI_HIST_MINUTES, s->mcount);
    TEST_ASSERT_EQUAL(144, ui_series_columns(s, now, 24u * 3600000u, 144, 0, 144, s_cols)); /* 10 min a column */
    for (int c = 0; c < 144; c++) {
        TEST_ASSERT_EQUAL(10, s_cols[c].n);
        TEST_ASSERT_EQUAL_FLOAT((float)(60 + 10 * c), s_cols[c].min);
        TEST_ASSERT_EQUAL_FLOAT((float)(69 + 10 * c), s_cols[c].max);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 64.5f + 10.0f * (float)c, s_cols[c].mean);
    }
}

static void test_a_break_keeps_the_line_apart(void)
{
    for (uint32_t i = 0; i <= 10; i++) {
        ui_series_push(s, i * 1000, 1.0f);
    }
    ui_series_break(s, 10500); /* the session reconnected */
    for (uint32_t i = 11; i <= 20; i++) {
        ui_series_push(s, i * 1000, 2.0f);
    }
    ui_series_columns(s, 20000, 30000, 30, 0, 30, s_cols);
    for (int c = 0; c < 30; c++) {
        TEST_ASSERT_EQUAL_MESSAGE(c == 20, s_cols[c].brk, "only the first column after the break");
    }
    ui_series_push(s, 21000, NAN); /* a value no number holds is a break too */
    ui_series_push(s, 22000, 3.0f);
    ui_series_columns(s, 22000, 30000, 30, 0, 30, s_cols);
    TEST_ASSERT_TRUE(s_cols[29].brk);
    TEST_ASSERT_EQUAL(0, s_cols[28].n);
    ui_series_break(s, 23000); /* a break after the last value marks nothing */
    ui_series_columns(s, 23000, 30000, 30, 0, 30, s_cols);
    TEST_ASSERT_TRUE(s_cols[28].brk); /* 22 s, as before */
    TEST_ASSERT_FALSE(s_cols[29].brk);
}

static void test_a_break_inside_a_minute_keeps_the_minutes_apart(void)
{
    uint32_t t = 0;
    for (int k = 0; k < 20 * 240; k++) {
        t = (uint32_t)k * 250;
        ui_series_push(s, t, 1.0f);
        if (k == 10 * 240) {
            ui_series_break(s, t + 125); /* within minute 10 */
        }
    }
    ui_series_columns(s, t, 3600000, 60, 0, 60, s_cols);
    for (int c = 0; c < 60; c++) {
        TEST_ASSERT_EQUAL_MESSAGE(c == 50, s_cols[c].brk, "minute 10's column alone");
    }
}

static void test_a_long_silence_clears_the_minutes(void)
{
    uint32_t now = quarter_seconds(20);
    TEST_ASSERT_EQUAL(19, s->mcount);
    now += 25u * 3600000u;
    ui_series_push(s, now, 4.0f);
    TEST_ASSERT_EQUAL(0, s->mcount);
    TEST_ASSERT_EQUAL(1, ui_series_columns(s, now, 24u * 3600000u, 144, 0, 144, s_cols));
    TEST_ASSERT_EQUAL_FLOAT(4.0f, s_cols[143].mean);
}

static void test_the_clock_may_wrap(void)
{
    uint32_t start = 0xFFFFFFFFu - 30000u;
    for (uint32_t i = 0; i < 60; i++) {
        ui_series_push(s, start + i * 1000, (float)i);
    }
    uint32_t now = start + 59 * 1000;
    TEST_ASSERT_EQUAL(60, ui_series_columns(s, now, 60000, 60, 0, 60, s_cols));
    for (int c = 0; c < 60; c++) {
        TEST_ASSERT_EQUAL_FLOAT((float)c, s_cols[c].mean);
    }
}

static void test_a_few_columns_at_a_time_make_the_same_chart(void)
{
    static ui_hist_column_t part[50];
    for (uint32_t i = 0; i < 3000; i++) {
        if (i % 700 == 350) {
            ui_series_break(s, i * 1000 + 500);
        }
        ui_series_push(s, i * 1000, (float)((i * 7919) % 101));
    }
    uint32_t now = 2999 * 1000;
    int whole = ui_series_columns(s, now, 3600000, 390, 0, 390, s_cols);
    int sum = 0;
    for (int first = 0; first < 390; first += 50) {
        int n = first + 50 <= 390 ? 50 : 390 - first;
        sum += ui_series_columns(s, now, 3600000, 390, first, n, part);
        TEST_ASSERT_EQUAL_MEMORY(&s_cols[first], part, (size_t)n * sizeof(part[0]));
    }
    TEST_ASSERT_EQUAL(whole, sum);
}

/* A board on USB keeps its history while the PC stays off for weeks: those values are weeks old, not new. */
static void test_values_from_weeks_ago_stay_weeks_ago(void)
{
    for (uint32_t i = 0; i < 600; i++) {
        ui_series_push(s, 1000 + i * 1000, (float)i);
    }
    uint32_t now = 1000 + 30u * 86400000u; /* 30 days on */
    TEST_ASSERT_EQUAL(0, ui_series_columns(s, now, 60000, 60, 0, 60, s_cols));
    TEST_ASSERT_EQUAL(1, ui_series_columns(s, 1000 + 599 * 1000 - 500, 60000, 60, 0, 60, s_cols) > 0); /* a clock a
                                                                     moment behind the last value: it counts as now */
}

static void test_tracking_keeps_known_series_and_frees_the_rest(void)
{
    const char *const ab[] = { "env.temp", "pc.gpu_temp" };
    ui_history_track(&s_h, ab, 2);
    TEST_ASSERT_NULL(ui_history_find(&s_h, "pc.cpu_load")); /* no chart shows it any more */
    ui_series_t *temp = ui_history_find(&s_h, "env.temp");
    ui_series_push(temp, 1000, 21.5f);
    const char *const bc[] = { "env.temp", "pc.gpu_temp", "", "env.temp", "pc.cpu_load" };
    TEST_ASSERT_EQUAL(3, ui_history_track(&s_h, bc, 5)); /* "" and the repeat count for nothing */
    TEST_ASSERT_EQUAL_PTR(temp, ui_history_find(&s_h, "env.temp"));
    TEST_ASSERT_EQUAL(1, temp->count); /* kept with its values */
    ui_series_t *load = ui_history_find(&s_h, "pc.cpu_load");
    TEST_ASSERT_NOT_NULL(load);
    TEST_ASSERT_EQUAL(0, load->count); /* back, but from nothing */
    char names[UI_HIST_SERIES + 1][UI_HIST_NAME_LEN];
    const char *list[UI_HIST_SERIES + 1];
    for (int i = 0; i <= UI_HIST_SERIES; i++) {
        snprintf(names[i], sizeof(names[i]), "pc.r%d", i);
        list[i] = names[i];
    }
    TEST_ASSERT_EQUAL(UI_HIST_SERIES, ui_history_track(&s_h, list, UI_HIST_SERIES + 1));
    TEST_ASSERT_NOT_NULL(ui_history_find(&s_h, "pc.r15"));
    TEST_ASSERT_NULL(ui_history_find(&s_h, "pc.r16")); /* 16 series at most */
    TEST_ASSERT_NULL(ui_history_find(&s_h, ""));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_one_value_a_column_newest_at_the_right);
    RUN_TEST(test_a_column_of_many_values_keeps_their_range_and_mean);
    RUN_TEST(test_columns_without_values_are_empty);
    RUN_TEST(test_the_ring_keeps_the_last_3600_values);
    RUN_TEST(test_a_span_past_the_values_draws_from_the_minutes);
    RUN_TEST(test_24_hours_of_minutes);
    RUN_TEST(test_a_break_keeps_the_line_apart);
    RUN_TEST(test_a_break_inside_a_minute_keeps_the_minutes_apart);
    RUN_TEST(test_a_long_silence_clears_the_minutes);
    RUN_TEST(test_the_clock_may_wrap);
    RUN_TEST(test_a_few_columns_at_a_time_make_the_same_chart);
    RUN_TEST(test_values_from_weeks_ago_stay_weeks_ago);
    RUN_TEST(test_tracking_keeps_known_series_and_frees_the_rest);
    return UNITY_END();
}
