#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/*
 * Language packs (spec §5.8): UI strings, weekday, month and moon-phase names, and the date, time
 * and number formats. The component is `locale`; the prefix is `lang_` because libc's <locale.h>
 * already owns `locale_t`. Pure C, host-buildable.
 */

typedef enum {
    LS_SET_TIME,
    LS_TIME,
    LS_DATE,
    LS_TEMPERATURE,
    LS_HUMIDITY,
    LS_DEW_POINT,
    LS_TODAY_MIN,
    LS_TODAY_MAX,
    LS_BATTERY,
    LS_BATTERY_DAYS,
    LS_WEEK,
    LS_MOON,
    LS_NAME_DAY,
    LS_HOLIDAY,
    LS_WEATHER,
    LS_TODAY,
    LS_FORECAST,
    LS_SUN,
    LS_DAYS_UNIT,    /* after a number of days: "d" */
    LS_HOURS_UNIT,   /* "h" */
    LS_MINUTES_UNIT, /* "min" */
    LS_COUNT,
} lang_str_t;

typedef enum {
    LANG_DATE_LONG,   /* Friday 25 September */
    LANG_DATE_MEDIUM, /* Fri 25 Sep */
    LANG_DATE_SHORT,  /* 25 Sep */
} lang_date_style_t;

typedef struct lang {
    const char *code; /* "en" */
    const char *name; /* in the language itself: "English" */
    const char *strings[LS_COUNT];
    const char *weekdays[7]; /* struct tm order: Sunday first */
    const char *weekdays_short[7];
    const char *months[12];
    const char *months_short[12];
    const char *moon_phases[8];       /* util_moon_t.index order */
    const char *moon_phases_short[8]; /* for small slots; the disc shows waxing or waning */
    char decimal_sep;
    int first_weekday; /* 0 Sunday, 1 Monday */
    void (*format_date)(const struct lang *lang, const struct tm *tm, lang_date_style_t style, char *out,
                        size_t size);
    const char *(*name_day)(int month, int day);         /* NULL: the pack has no calendar */
    const char *(*holiday)(int year, int month, int day); /* NULL, or NULL result: not a holiday */
} lang_t;

/* The pack for a code such as "en"; English for NULL or an unknown code. */
const lang_t *lang_get(const char *code);
const char *lang_str(const lang_t *lang, lang_str_t id);
/* An out-of-range tm writes an empty string. */
void lang_format_date(const lang_t *lang, const struct tm *tm, lang_date_style_t style, char *out, size_t size);
/* "20:48", "20:48:05", or "8:48" with *suffix "PM" in 12-hour mode (*suffix is "" otherwise). */
void lang_format_time(int hour, int minute, int second, bool h24, bool seconds, char *out, size_t size,
                      const char **suffix);
/* `value` scaled by 10^decimals: (-5, 1) is "-0.5" and (234, 1) is "23.4", with the pack's
 * decimal separator. Returns the length written. */
int lang_format_decimal(const lang_t *lang, long value, int decimals, char *out, size_t size);
