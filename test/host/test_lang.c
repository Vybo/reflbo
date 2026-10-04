#include <stdio.h>
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
    lang_format_date(en, &tm, LANG_DATE_DAY, out, sizeof(out)); /* M6c: XS's weekday over its day */
    TEST_ASSERT_EQUAL_STRING("Fri 25", out);
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

static void test_every_string_exists_in_every_pack(void)
{
    const char *const codes[] = { "en", "cs" };
    for (int c = 0; c < 2; c++) {
        const lang_t *lang = lang_get(codes[c]);
        TEST_ASSERT_EQUAL_STRING(codes[c], lang->code);
        for (int id = 0; id < LS_COUNT; id++) {
            char msg[32];
            snprintf(msg, sizeof(msg), "%s string %d", codes[c], id);
            TEST_ASSERT_NOT_NULL_MESSAGE(lang->strings[id], msg);
            TEST_ASSERT_TRUE_MESSAGE(lang->strings[id][0] != '\0', msg);
        }
    }
}

static void test_czech_dates_numbers_and_names(void)
{
    const lang_t *cs = lang_get("cs");
    TEST_ASSERT_EQUAL_STRING("Čeština", cs->name);
    struct tm tm = date(2026, 9, 25, 5);
    char out[40];
    lang_format_date(cs, &tm, LANG_DATE_LONG, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Pátek 25. září", out);
    lang_format_date(cs, &tm, LANG_DATE_MEDIUM, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Pá 25. 9.", out);
    lang_format_date(cs, &tm, LANG_DATE_SHORT, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("25. 9.", out);
    lang_format_date(cs, &tm, LANG_DATE_DAY, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Pá 25.", out);
    tm = date(2026, 3, 2, 1);
    lang_format_date(cs, &tm, LANG_DATE_LONG, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Pondělí 2. března", out);
    lang_format_decimal(cs, -125, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-12,5", out);
    TEST_ASSERT_EQUAL_STRING("Úplněk", cs->moon_phases[4]);
    TEST_ASSERT_EQUAL_STRING("Nastavte čas", lang_str(cs, LS_SET_TIME));
    TEST_ASSERT_EQUAL_INT(1, cs->first_weekday);
}

static void test_czech_public_holidays_including_easter(void)
{
    const lang_t *cs = lang_get("cs");
    TEST_ASSERT_NOT_NULL(cs->holiday);
    TEST_ASSERT_NULL(cs->name_day); /* D16: no name-day calendar until a clean source turns up */
    TEST_ASSERT_EQUAL_STRING("Velký pátek", cs->holiday(2026, 4, 3));        /* Easter is 5 April 2026 */
    TEST_ASSERT_EQUAL_STRING("Velikonoční pondělí", cs->holiday(2026, 4, 6));
    TEST_ASSERT_EQUAL_STRING("Velký pátek", cs->holiday(2027, 3, 26));       /* Easter is 28 March 2027 */
    TEST_ASSERT_NULL(cs->holiday(2026, 4, 5));                               /* Easter Sunday is not a day off */
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 1, 1));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 5, 1));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 5, 8));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 7, 5));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 7, 6));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 9, 28));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 10, 28));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 11, 17));
    TEST_ASSERT_EQUAL_STRING("Štědrý den", cs->holiday(2026, 12, 24));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 12, 25));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 12, 26));
    TEST_ASSERT_NULL(cs->holiday(2026, 9, 29));
    TEST_ASSERT_NULL(cs->holiday(2015, 4, 3)); /* Good Friday became a holiday in 2016 */
    TEST_ASSERT_NOT_NULL(cs->holiday(2016, 3, 25));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_english_is_the_default_and_the_fallback);
    RUN_TEST(test_dates_in_three_styles);
    RUN_TEST(test_an_impossible_date_formats_as_empty);
    RUN_TEST(test_times_in_24_and_12_hour_modes);
    RUN_TEST(test_decimals_keep_the_sign_below_one);
    RUN_TEST(test_every_string_exists_in_every_pack);
    RUN_TEST(test_czech_dates_numbers_and_names);
    RUN_TEST(test_czech_public_holidays_including_easter);
    return UNITY_END();
}
