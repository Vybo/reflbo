#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include "ui_window.h"

#include <stdio.h>
#include <string.h>

#include "astro.h"
#include "scheduler.h"
#include "util_time.h"

/* The windows that can be open now opened from two days back (one opened last evening closes this morning; a
 * bound 3 h past a polar day's sunset falls on the next day) to tomorrow (a bound up to 3 h before sunrise, or a
 * polar sunrise, falls this evening). A week's mask opens within 8 days. */
#define LOOK_BACK_DAYS 2
#define LOOK_AHEAD_DAYS 8

static const char *const k_day_names[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" }; /* bit 0 is Monday */
static const char *const k_weekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" }; /* tm_wday */

static bool is_sun(int16_t b)
{
    return b >= UI_BOUND_SUNRISE - UI_BOUND_OFFSET_MAX;
}

static bool is_sunrise(int16_t b)
{
    return b < UI_BOUND_SUNSET - UI_BOUND_OFFSET_MAX;
}

bool ui_bound_parse(const char *text, int16_t *out)
{
    bool rise = strncmp(text, "sunrise", 7) == 0;
    if (rise || strncmp(text, "sunset", 6) == 0) {
        const char *p = text + (rise ? 7 : 6);
        int base = rise ? UI_BOUND_SUNRISE : UI_BOUND_SUNSET;
        if (*p == '\0') {
            *out = (int16_t)base;
            return true;
        }
        if ((*p != '+' && *p != '-') || p[1] < '1' || p[1] > '9') {
            return false;
        }
        int sign = *p == '-' ? -1 : 1, value = 0, digits = 0;
        for (p++; *p >= '0' && *p <= '9' && digits < 4; p++, digits++) {
            value = value * 10 + (*p - '0');
        }
        if (*p != '\0' || value > UI_BOUND_OFFSET_MAX) {
            return false;
        }
        *out = (int16_t)(base + sign * value);
        return true;
    }
    if (strlen(text) != 5 || text[2] != ':') {
        return false;
    }
    for (int i = 0; i < 5; i++) {
        if (i != 2 && (text[i] < '0' || text[i] > '9')) {
            return false;
        }
    }
    int h = (text[0] - '0') * 10 + (text[1] - '0'), m = (text[3] - '0') * 10 + (text[4] - '0');
    if (h > 23 || m > 59) {
        return false;
    }
    *out = (int16_t)(h * 60 + m);
    return true;
}

void ui_bound_format(int16_t bound, char *out, size_t size)
{
    if (!is_sun(bound)) {
        snprintf(out, size, "%02d:%02d", bound / 60 % 24, bound % 60);
        return;
    }
    bool rise = is_sunrise(bound);
    int offset = bound - (rise ? UI_BOUND_SUNRISE : UI_BOUND_SUNSET);
    if (offset == 0) {
        snprintf(out, size, "%s", rise ? "sunrise" : "sunset");
    } else {
        snprintf(out, size, "%s%+d", rise ? "sunrise" : "sunset", offset);
    }
}

bool ui_window_make(const char *from, const char *until, int days, ui_window_t *out, char *err, size_t err_size)
{
    static const char k_bound_rule[] = "must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes";
    ui_window_t w;
    if (!ui_bound_parse(from, &w.from)) {
        snprintf(err, err_size, "from %s", k_bound_rule);
        return false;
    }
    if (!ui_bound_parse(until, &w.until)) {
        snprintf(err, err_size, "until %s", k_bound_rule);
        return false;
    }
    if (w.from == w.until) {
        snprintf(err, err_size, "from and until can't be the same");
        return false;
    }
    if (days < 1 || days > 0x7F) {
        snprintf(err, err_size, "days must be 1-127");
        return false;
    }
    out->days = (uint8_t)days; /* field by field: a preset's padding stays as its memset left it */
    out->from = w.from;
    out->until = w.until;
    return true;
}

/* The zone's UTC offset at local noon of a date, as astro_sun() takes it. */
static int32_t noon_offset(int year, int month, int day)
{
    struct tm noon = { .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = 12, .tm_isdst = -1 };
    time_t t = mktime(&noon);
    return (int32_t)(util_days_from_civil(year, month, day) * 86400 + 12 * 3600 - (int64_t)t);
}

/* The sun on a local day at a place; a few days are kept, as the S3 computes in software doubles. */
static astro_sun_t sun_on(int64_t day, int32_t lat_e4, int32_t lon_e4)
{
    static struct {
        int64_t day;
        int32_t lat, lon;
        astro_sun_t sun;
        bool used;
    } cache[4];
    static int next;
    for (int i = 0; i < 4; i++) {
        if (cache[i].used && cache[i].day == day && cache[i].lat == lat_e4 && cache[i].lon == lon_e4) {
            return cache[i].sun;
        }
    }
    int y, m, d;
    util_civil_from_days(day, &y, &m, &d);
    astro_sun_t sun;
    astro_sun(y, m, d, noon_offset(y, m, d), lat_e4, lon_e4, &sun);
    cache[next].day = day;
    cache[next].lat = lat_e4;
    cache[next].lon = lon_e4;
    cache[next].sun = sun;
    cache[next].used = true;
    next = (next + 1) % 4;
    return sun;
}

static time_t at_minute(int64_t day, int minute)
{
    int y, m, d;
    util_civil_from_days(day, &y, &m, &d);
    return sched_local_to_utc(y, m, d, minute); /* a skipped minute is the next valid one (spec §9.2) */
}

/* A bound on a local day. Without a sunrise or sunset, a polar day runs from midnight to midnight and a polar
 * night from midnight to midnight the other way round: sunrise at the day's end, sunset at its start. */
static time_t bound_at(int16_t b, int64_t day, int32_t lat_e4, int32_t lon_e4)
{
    if (!is_sun(b)) {
        return at_minute(day, b);
    }
    bool rise = is_sunrise(b);
    int offset = b - (rise ? UI_BOUND_SUNRISE : UI_BOUND_SUNSET);
    astro_sun_t sun = sun_on(day, lat_e4, lon_e4);
    time_t base;
    if (sun.kind == ASTRO_NORMAL) {
        base = (time_t)(rise ? sun.sunrise : sun.sunset);
    } else if (sun.kind == ASTRO_POLAR_DAY) {
        base = at_minute(rise ? day : day + 1, 0);
    } else {
        base = at_minute(rise ? day + 1 : day, 0);
    }
    return base + (time_t)offset * 60;
}

static int64_t local_day(time_t t)
{
    struct tm local;
    localtime_r(&t, &local);
    return util_days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

/* The window that opens on `day`, [*start, *end); false when it doesn't open that day or would be empty. */
static bool span(const ui_window_t *w, int64_t day, int32_t lat_e4, int32_t lon_e4, time_t *start, time_t *end)
{
    int monday_first = (int)((day % 7 + 7 + 3) % 7); /* day 0, 1970-01-01, was a Thursday */
    if (!(w->days & (1u << monday_first))) {
        return false;
    }
    *start = bound_at(w->from, day, lat_e4, lon_e4);
    *end = bound_at(w->until, day, lat_e4, lon_e4);
    /* Two clock ends cross midnight by the clock face: 02:00-02:30 in a spring gap, both 03:00, is empty that
     * night rather than a day long. */
    bool clock = !is_sun(w->from) && !is_sun(w->until);
    if (clock ? w->until <= w->from : *end <= *start) {
        *end = bound_at(w->until, day + 1, lat_e4, lon_e4);
    }
    return *end > *start;
}

bool ui_window_open(const ui_window_t *w, time_t now, int32_t lat_e4, int32_t lon_e4, time_t *change)
{
    if (change != NULL) {
        *change = 0;
    }
    if (w->days == 0) {
        return true;
    }
    int64_t today = local_day(now);
    for (int64_t d = today - LOOK_BACK_DAYS; d <= today + 1; d++) {
        time_t start, end;
        if (!span(w, d, lat_e4, lon_e4, &start, &end) || now < start || now >= end) {
            continue;
        }
        bool joined = true; /* the next day's window starts where this one ends: they are one */
        for (int64_t e = d + 1; joined && e <= today + LOOK_AHEAD_DAYS; e++) {
            time_t s2, e2;
            joined = span(w, e, lat_e4, lon_e4, &s2, &e2) && s2 <= end;
            if (joined && e2 > end) {
                end = e2;
            }
        }
        if (change != NULL && !joined) {
            *change = end;
        }
        return true;
    }
    for (int64_t d = today - LOOK_BACK_DAYS; d <= today + LOOK_AHEAD_DAYS; d++) {
        time_t start, end;
        if (span(w, d, lat_e4, lon_e4, &start, &end) && start > now) {
            if (change != NULL) {
                *change = start;
            }
            break;
        }
    }
    return false;
}

void ui_window_text(const ui_window_t *w, char *out, size_t size)
{
    char from[UI_BOUND_TEXT_LEN], until[UI_BOUND_TEXT_LEN];
    ui_bound_format(w->from, from, sizeof(from));
    ui_bound_format(w->until, until, sizeof(until));
    size_t n = (size_t)snprintf(out, size, "%s - %s", from, until);
    if ((w->days & 0x7F) != 0x7F && n < size) {
        n += (size_t)snprintf(out + n, size - n, " on");
        for (int bit = 0; bit < 7 && n < size; bit++) {
            if (w->days & (1u << bit)) {
                n += (size_t)snprintf(out + n, size - n, " %s", k_day_names[bit]);
            }
        }
    }
}

void ui_window_state_text(const ui_window_t *w, time_t now, int32_t lat_e4, int32_t lon_e4, char *out, size_t size)
{
    if (now == 0) {
        snprintf(out, size, "open: the clock isn't set");
        return;
    }
    time_t change;
    bool open = ui_window_open(w, now, lat_e4, lon_e4, &change);
    if (change == 0) {
        snprintf(out, size, "%s", open ? "open" : "closed");
        return;
    }
    struct tm a, b;
    localtime_r(&now, &a);
    localtime_r(&change, &b);
    bool same_day = a.tm_year == b.tm_year && a.tm_yday == b.tm_yday;
    snprintf(out, size, "%s until %s%s%02d:%02d", open ? "open" : "closed", same_day ? "" : k_weekdays[b.tm_wday],
             same_day ? "" : " ", b.tm_hour, b.tm_min);
}
