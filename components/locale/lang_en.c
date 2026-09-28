#include <stdio.h>

#include "lang.h"

static void format_date(const lang_t *lang, const struct tm *tm, lang_date_style_t style, char *out, size_t size)
{
    switch (style) {
    case LANG_DATE_LONG:
        snprintf(out, size, "%s %d %s", lang->weekdays[tm->tm_wday], tm->tm_mday, lang->months[tm->tm_mon]);
        break;
    case LANG_DATE_MEDIUM:
        snprintf(out, size, "%s %d %s", lang->weekdays_short[tm->tm_wday], tm->tm_mday,
                 lang->months_short[tm->tm_mon]);
        break;
    case LANG_DATE_SHORT:
        snprintf(out, size, "%d %s", tm->tm_mday, lang->months_short[tm->tm_mon]);
        break;
    }
}

const lang_t lang_en = {
    .code = "en",
    .name = "English",
    .strings = {
        [LS_SET_TIME] = "Set time",
        [LS_TIME] = "Time",
        [LS_DATE] = "Date",
        [LS_TEMPERATURE] = "Temperature",
        [LS_HUMIDITY] = "Humidity",
        [LS_DEW_POINT] = "Dew point",
        [LS_TODAY_MIN] = "Today's low",
        [LS_TODAY_MAX] = "Today's high",
        [LS_BATTERY] = "Battery",
        [LS_BATTERY_DAYS] = "Battery left",
        [LS_WEEK] = "Week",
        [LS_MOON] = "Moon",
        [LS_NAME_DAY] = "Name day",
        [LS_HOLIDAY] = "Holiday",
        [LS_WEATHER] = "Weather",
        [LS_TODAY] = "Today",
        [LS_FORECAST] = "Forecast",
        [LS_SUN] = "Sunrise and sunset",
        [LS_DAYS_UNIT] = "d",
        [LS_HOURS_UNIT] = "h",
        [LS_MINUTES_UNIT] = "min",
    },
    .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
    .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
    .months = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
                "November", "December" },
    .months_short = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" },
    .moon_phases = { "New moon", "Waxing crescent", "First quarter", "Waxing gibbous", "Full moon", "Waning gibbous",
                     "Last quarter", "Waning crescent" },
    .moon_phases_short = { "New", "Crescent", "First qtr", "Gibbous", "Full", "Gibbous", "Last qtr", "Crescent" },
    .decimal_sep = '.',
    .first_weekday = 1,
    .format_date = format_date,
    .name_day = NULL,
    .holiday = NULL,
};
