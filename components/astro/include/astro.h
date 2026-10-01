#pragma once

#include <stdint.h>

/* Sunrise, sunset and day length (spec §11): the NOAA solar algorithm, offline. Pure C, host-buildable. */

typedef enum {
    ASTRO_NORMAL,      /* the sun rises and sets on that date */
    ASTRO_POLAR_DAY,   /* it never sets */
    ASTRO_POLAR_NIGHT, /* it never rises */
} astro_day_kind_t;

typedef struct {
    astro_day_kind_t kind;
    int64_t sunrise;      /* UTC seconds; 0 unless ASTRO_NORMAL */
    int64_t sunset;       /* UTC seconds; 0 unless ASTRO_NORMAL */
    int32_t day_length_s; /* sunset - sunrise; 86400 on a polar day, 0 on a polar night */
} astro_sun_t;

/* Sunrise and sunset on the local civil date year-month-day at the location (1e-4 degrees, north and
 * east positive, as settings_t keeps them). `utc_offset_s` is the zone's offset at local noon of that
 * date (CET: 3600), so the events are the ones of that local day. NOAA solar algorithm with the
 * standard 0.833° for refraction and the sun's radius. */
void astro_sun(int year, int month, int day, int32_t utc_offset_s, int32_t lat_e4, int32_t lon_e4, astro_sun_t *out);
