#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "solar.h"

/* The providers' requests (spec §11.5). Ours are in 1e-4 degrees; they take decimal degrees. */

static void degrees(char *out, size_t size, int32_t e4)
{
    uint32_t a = e4 < 0 ? (uint32_t)(-(int64_t)e4) : (uint32_t)e4;
    snprintf(out, size, "%s%lu.%04lu", e4 < 0 ? "-" : "", (unsigned long)(a / 10000), (unsigned long)(a % 10000));
}

/* snprintf's result, or 0 and "" when it didn't fit: callers never see half a URL. */
static size_t fitted(int n, char *out, size_t size)
{
    if (n > 0 && (size_t)n < size) {
        return (size_t)n;
    }
    if (size > 0) {
        out[0] = '\0';
    }
    return 0;
}

size_t solar_open_meteo_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4, const solar_plane_t *plane)
{
    char lat[16], lon[16];
    degrees(lat, sizeof(lat), lat_e4);
    degrees(lon, sizeof(lon), lon_e4);
    return fitted(snprintf(out, size,
                           "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
                           "&minutely_15=global_tilted_irradiance,temperature_2m&tilt=%d&azimuth=%d"
                           "&timezone=auto&timeformat=unixtime&forecast_days=3",
                           lat, lon, plane->tilt, plane->azimuth),
                  out, size);
}

/* Letters and digits, and `extra` where given: what goes into a path as it is. */
static bool plain(const char *s, char extra)
{
    for (; *s != '\0'; s++) {
        if (!isalnum((unsigned char)*s) && *s != extra) {
            return false;
        }
    }
    return true;
}

/* kWp as Forecast.Solar takes it: "5", "5.2", "2.45". */
static void kwp_text(char *out, size_t size, float kwp)
{
    snprintf(out, size, "%.2f", (double)kwp);
    char *end = out + strlen(out);
    while (end > out && end[-1] == '0') {
        *--end = '\0';
    }
    if (end > out && end[-1] == '.') {
        end[-1] = '\0';
    }
}

size_t solar_forecast_solar_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4, const solar_plane_t *planes,
                                int count, const char *key)
{
    if (!plain(key, '\0')) {
        return fitted(-1, out, size);
    }
    char lat[16], lon[16], path[96] = "";
    degrees(lat, sizeof(lat), lat_e4);
    degrees(lon, sizeof(lon), lon_e4);
    for (int i = 0; i < (key[0] != '\0' ? count : 1) && i < SOLAR_PLANES_MAX; i++) {
        char kwp[16];
        kwp_text(kwp, sizeof(kwp), planes[i].kwp);
        size_t n = strlen(path);
        snprintf(path + n, sizeof(path) - n, "/%d/%d/%s", planes[i].tilt, planes[i].azimuth, kwp);
    }
    return fitted(snprintf(out, size, "https://api.forecast.solar/%s%sestimate/watts/%s/%s%s?time=utc", key,
                           key[0] != '\0' ? "/" : "", lat, lon, path),
                  out, size);
}

size_t solar_solcast_url(char *out, size_t size, const char *site)
{
    if (site[0] == '\0' || !plain(site, '-')) {
        return fitted(-1, out, size);
    }
    return fitted(snprintf(out, size, "https://api.solcast.com.au/rooftop_sites/%s/forecasts?format=json&hours=72",
                           site),
                  out, size);
}
