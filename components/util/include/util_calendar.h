#pragma once

#include <time.h>

/* Calendar helpers for the date fields (spec §5.1). Pure C, host-buildable. */

/* ISO 8601 week number (1–53) of a civil date. */
int util_iso_week(int year, int month, int day);

/* Easter Sunday of a Gregorian year (the anonymous Gregorian algorithm, Meeus chapter 8). */
void util_easter(int year, int *month, int *day);

typedef struct {
    int index;        /* 0 new, 1 waxing crescent, 2 first quarter, 3 waxing gibbous, 4 full,
                         5 waning gibbous, 6 last quarter, 7 waning crescent */
    int illumination; /* lit fraction of the disc, % */
    double age;       /* position in the cycle: 0 new, 0.5 full, towards 1 the next new moon */
} util_moon_t;

/* Moon phase at a UTC time, from Meeus's low-precision terms (Astronomical Algorithms,
 * chapter 48): within an hour or so of the true phase, plenty for a desk display. */
util_moon_t util_moon_phase(time_t utc);
