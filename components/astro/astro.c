#include "astro.h"

#include <math.h>

/* The NOAA solar calculator's formulas (NOAA Global Monitoring Laboratory, "General Solar Position
 * Calculations"; public domain), evaluated at the instant of each event rather than once a day. */

#define PI 3.14159265358979323846
#define RAD (PI / 180.0)
#define DEG (180.0 / PI)
#define SUN_ALTITUDE (-0.833) /* degrees: refraction and the sun's radius put the upper limb on the horizon */
#define ITERATIONS 4          /* each pass moves the time by a minute at most; four settle it under a second */

typedef struct {
    double declination; /* degrees */
    double eq_time;     /* minutes: apparent solar time minus mean solar time */
} sun_position_t;

/* Days since 1970-01-01 of a civil date (proleptic Gregorian), as util_days_from_civil() counts. */
static int64_t days_from_civil(int year, int month, int day)
{
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    int64_t yoe = year - era * 400;
    int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static sun_position_t sun_at(double t)
{
    double jd = t / 86400.0 + 2440587.5;
    double c = (jd - 2451545.0) / 36525.0; /* Julian centuries since J2000.0 */
    double l0 = fmod(280.46646 + c * (36000.76983 + c * 0.0003032), 360.0); /* mean longitude */
    double m = 357.52911 + c * (35999.05029 - 0.0001537 * c);               /* mean anomaly */
    double e = 0.016708634 - c * (0.000042037 + 0.0000001267 * c);          /* the orbit's eccentricity */
    double centre = sin(m * RAD) * (1.914602 - c * (0.004817 + 0.000014 * c)) +
                    sin(2 * m * RAD) * (0.019993 - 0.000101 * c) + sin(3 * m * RAD) * 0.000289;
    double omega = 125.04 - 1934.136 * c;
    double lambda = l0 + centre - 0.00569 - 0.00478 * sin(omega * RAD); /* apparent longitude */
    double eps0 = 23.0 + (26.0 + (21.448 - c * (46.815 + c * (0.00059 - c * 0.001813))) / 60.0) / 60.0;
    double eps = eps0 + 0.00256 * cos(omega * RAD); /* obliquity, corrected for nutation */
    double y = tan(eps / 2 * RAD);
    y *= y;
    sun_position_t p;
    p.declination = asin(sin(eps * RAD) * sin(lambda * RAD)) * DEG;
    p.eq_time = 4 * DEG *
                (y * sin(2 * l0 * RAD) - 2 * e * sin(m * RAD) + 4 * e * y * sin(m * RAD) * cos(2 * l0 * RAD) -
                 0.5 * y * y * sin(4 * l0 * RAD) - 1.25 * e * e * sin(2 * m * RAD));
    return p;
}

/* Moves `t` to the nearest instant when apparent solar time at longitude `lon` reads `target` minutes
 * after midnight, with the sun's position taken at `t`. */
static double step_to(double t, double lon, double target, const sun_position_t *p)
{
    double utc_minutes = (t - 86400.0 * floor(t / 86400.0)) / 60.0;
    double solar = utc_minutes + p->eq_time + 4.0 * lon;
    return t + 60.0 * remainder(target - solar, 1440.0);
}

/* cos of the hour angle at which the sun's upper limb touches the horizon. */
static double cos_hour_angle(double lat, double declination)
{
    return (sin(SUN_ALTITUDE * RAD) - sin(lat * RAD) * sin(declination * RAD)) /
           (cos(lat * RAD) * cos(declination * RAD));
}

/* Sunrise (sign -1) or sunset (+1) near solar noon `noon`, for a day on which the sun does both. */
static double event(double noon, double lat, double lon, int sign)
{
    double t = noon;
    for (int i = 0; i < ITERATIONS; i++) {
        sun_position_t p = sun_at(t);
        double cos_h = cos_hour_angle(lat, p.declination);
        cos_h = cos_h > 1.0 ? 1.0 : cos_h < -1.0 ? -1.0 : cos_h; /* a day at the edge of the polar season */
        double h = acos(cos_h) * DEG;
        t = step_to(t, lon, 720.0 + sign * 4.0 * h, &p);
    }
    return t;
}

void astro_sun(int year, int month, int day, int32_t utc_offset_s, int32_t lat_e4, int32_t lon_e4, astro_sun_t *out)
{
    double lat = lat_e4 / 1e4, lon = lon_e4 / 1e4;
    lat = lat > 89.999 ? 89.999 : lat < -89.999 ? -89.999 : lat; /* the poles themselves: no division by zero */
    double noon = (double)days_from_civil(year, month, day) * 86400.0 + 43200.0 - utc_offset_s; /* local noon */
    for (int i = 0; i < ITERATIONS; i++) { /* the solar noon of that local day */
        sun_position_t p = sun_at(noon);
        noon = step_to(noon, lon, 720.0, &p);
    }
    double cos_h = cos_hour_angle(lat, sun_at(noon).declination);
    if (cos_h > 1.0) {
        *out = (astro_sun_t){ .kind = ASTRO_POLAR_NIGHT };
        return;
    }
    if (cos_h < -1.0) {
        *out = (astro_sun_t){ .kind = ASTRO_POLAR_DAY, .day_length_s = 86400 };
        return;
    }
    int64_t sunrise = llround(event(noon, lat, lon, -1));
    int64_t sunset = llround(event(noon, lat, lon, 1));
    *out = (astro_sun_t){ .kind = ASTRO_NORMAL, .sunrise = sunrise, .sunset = sunset,
                          .day_length_s = (int32_t)(sunset - sunrise) };
}
