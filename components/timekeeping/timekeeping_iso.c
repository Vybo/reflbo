#define _POSIX_C_SOURCE 200809L /* mktime with tzset semantics */

#include "timekeeping_iso.h"

#include <ctype.h>
#include <stddef.h>

#include "util_time.h"

/* Reads exactly `digits` decimal digits. */
static bool number(const char **p, int digits, int *out)
{
    int v = 0;
    for (int i = 0; i < digits; i++) {
        if (!isdigit((unsigned char)(*p)[i])) {
            return false;
        }
        v = v * 10 + ((*p)[i] - '0');
    }
    *p += digits;
    *out = v;
    return true;
}

static bool expect(const char **p, char c)
{
    if (**p != c) {
        return false;
    }
    (*p)++;
    return true;
}

static int days_in_month(int y, int m)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : days[m - 1];
}

bool timekeeping_parse_iso8601(const char *text, time_t *utc)
{
    const char *p = text;
    int y, mo, d, h, mi, s = 0;
    if (text == NULL || !number(&p, 4, &y) || !expect(&p, '-') || !number(&p, 2, &mo) || !expect(&p, '-') ||
        !number(&p, 2, &d) || (*p != 'T' && *p != 't' && *p != ' ')) {
        return false;
    }
    p++;
    if (!number(&p, 2, &h) || !expect(&p, ':') || !number(&p, 2, &mi)) {
        return false;
    }
    if (*p == ':' && (p++, !number(&p, 2, &s))) {
        return false;
    }
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) || h > 23 || mi > 59 || s > 59) {
        return false;
    }

    if (*p == '\0') { /* local time */
        struct tm local = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi,
                            .tm_sec = s, .tm_isdst = -1 };
        time_t t = mktime(&local);
        if (t == (time_t)-1) {
            return false;
        }
        *utc = t;
        return true;
    }

    int offset_min = 0;
    if (*p == 'Z' || *p == 'z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        int sign = *p == '-' ? -1 : 1;
        int oh, om;
        p++;
        if (!number(&p, 2, &oh) || !expect(&p, ':') || !number(&p, 2, &om) || oh > 14 || om > 59) {
            return false;
        }
        offset_min = sign * (oh * 60 + om);
    } else {
        return false;
    }
    if (*p != '\0') {
        return false;
    }
    int64_t days = util_days_from_civil(y, mo, d);
    *utc = (time_t)(((days * 24 + h) * 60 + mi - offset_min) * 60 + s);
    return true;
}
