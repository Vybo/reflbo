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
