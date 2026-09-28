#include <string.h>

#include "lang.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static struct tm date(int year, int month, int day, int wday)
{
    return (struct tm){ .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_wday = wday };
}

static void test_english_is_the_default_and_the_fallback(void)
{
    TEST_ASSERT_EQUAL_STRING("en", lang_get("en")->code);
    TEST_ASSERT_EQUAL_STRING("en", lang_get("xx")->code);
    TEST_ASSERT_EQUAL_STRING("en", lang_get(NULL)->code);
    TEST_ASSERT_EQUAL_STRING("Set time", lang_str(lang_get("en"), LS_SET_TIME));
    TEST_ASSERT_EQUAL_STRING("", lang_str(lang_get("en"), LS_COUNT));
}

static void test_dates_in_three_styles(void)
{
    const lang_t *en = lang_get("en");
    struct tm tm = date(2026, 9, 25, 5);
    char out[40];
    lang_format_date(en, &tm, LANG_DATE_LONG, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Friday 25 September", out);
    lang_format_date(en, &tm, LANG_DATE_MEDIUM, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Fri 25 Sep", out);
    lang_format_date(en, &tm, LANG_DATE_SHORT, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("25 Sep", out);
}

static void test_an_impossible_date_formats_as_empty(void)
{
    struct tm tm = date(2026, 13, 1, 0);
    char out[16] = "x";
    lang_format_date(lang_get("en"), &tm, LANG_DATE_LONG, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_times_in_24_and_12_hour_modes(void)
{
    char out[16];
    const char *suffix;
    lang_format_time(20, 48, 5, true, false, out, sizeof(out), &suffix);
    TEST_ASSERT_EQUAL_STRING("20:48", out);
    TEST_ASSERT_EQUAL_STRING("", suffix);
    lang_format_time(7, 5, 9, true, true, out, sizeof(out), &suffix);
    TEST_ASSERT_EQUAL_STRING("07:05:09", out);
    lang_format_time(20, 48, 0, false, false, out, sizeof(out), &suffix);
    TEST_ASSERT_EQUAL_STRING("8:48", out);
    TEST_ASSERT_EQUAL_STRING("PM", suffix);
    lang_format_time(0, 5, 0, false, false, out, sizeof(out), &suffix);
    TEST_ASSERT_EQUAL_STRING("12:05", out);
    TEST_ASSERT_EQUAL_STRING("AM", suffix);
    lang_format_time(12, 0, 0, false, false, out, sizeof(out), &suffix);
    TEST_ASSERT_EQUAL_STRING("12:00", out);
    TEST_ASSERT_EQUAL_STRING("PM", suffix);
}

/* The M2 review found the console printing -0.50 as "0.50"; the sign must survive below 1. */
static void test_decimals_keep_the_sign_below_one(void)
{
    const lang_t *en = lang_get("en");
    char out[16];
    lang_format_decimal(en, 234, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("23.4", out);
    lang_format_decimal(en, -5, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-0.5", out);
    lang_format_decimal(en, -50, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-0.50", out);
    lang_format_decimal(en, 45, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("45", out);
    lang_format_decimal(en, 0, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("0.0", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_english_is_the_default_and_the_fallback);
    RUN_TEST(test_dates_in_three_styles);
    RUN_TEST(test_an_impossible_date_formats_as_empty);
    RUN_TEST(test_times_in_24_and_12_hour_modes);
    RUN_TEST(test_decimals_keep_the_sign_below_one);
    return UNITY_END();
}
