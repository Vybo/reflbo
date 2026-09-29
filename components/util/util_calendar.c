#include "util_calendar.h"

#include <math.h>
#include <stdbool.h>

#include "util_time.h"

#define PI 3.14159265358979323846

/* Monday = 1 … Sunday = 7; 1970-01-01 was a Thursday. */
static int iso_weekday(int64_t days)
{
    return (int)(((days % 7) + 10) % 7) + 1;
}

static bool is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static bool has_week_53(int year)
{
    int jan1 = iso_weekday(util_days_from_civil(year, 1, 1));
    return jan1 == 4 || (jan1 == 3 && is_leap(year));
}

int util_iso_week(int year, int month, int day)
{
    int64_t days = util_days_from_civil(year, month, day);
    int ordinal = (int)(days - util_days_from_civil(year, 1, 1)) + 1;
    int week = (ordinal - iso_weekday(days) + 10) / 7;
    if (week < 1) {
        return has_week_53(year - 1) ? 53 : 52;
    }
    if (week == 53 && !has_week_53(year)) {
        return 1;
    }
    return week;
}

static double norm360(double deg)
{
    deg = fmod(deg, 360.0);
    return deg < 0 ? deg + 360.0 : deg;
}

util_moon_t util_moon_phase(time_t utc)
{
    const double r = PI / 180.0;
    double jd = (double)utc / 86400.0 + 2440587.5;
    double t = (jd - 2451545.0) / 36525.0;
    double d = norm360(297.8501921 + 445267.1114034 * t);  /* mean elongation */
    double m = norm360(357.5291092 + 35999.0502909 * t);   /* the Sun's mean anomaly */
    double mp = norm360(134.9633964 + 477198.8675055 * t); /* the Moon's mean anomaly */
    double phase_angle = 180.0 - d - 6.289 * sin(mp * r) + 2.100 * sin(m * r) - 1.274 * sin((2 * d - mp) * r) -
                         0.658 * sin(2 * d * r) - 0.214 * sin(2 * mp * r) - 0.110 * sin(d * r);
    double elongation = norm360(180.0 - phase_angle); /* 0 new, 180 full */

    util_moon_t moon;
    moon.age = elongation / 360.0;
    moon.index = (int)floor(moon.age * 8.0 + 0.5) % 8;
    moon.illumination = (int)floor((1.0 + cos(phase_angle * r)) / 2.0 * 100.0 + 0.5);
    return moon;
}

void util_easter(int year, int *month, int *day)
{
    int a = year % 19, b = year / 100, c = year % 100;
    int d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4, k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    int n = h + l - 7 * m + 114;
    *month = n / 31;
    *day = n % 31 + 1;
}
