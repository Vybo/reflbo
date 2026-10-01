#include <stdio.h>
#include <string.h>

#include "weather.h"

/* Request URLs (spec §11). Open-Meteo takes decimal degrees; ours are in 1e-4 degrees. */

static void degrees(char *out, size_t size, int32_t e4)
{
    uint32_t a = e4 < 0 ? (uint32_t)(-(int64_t)e4) : (uint32_t)e4;
    snprintf(out, size, "%s%lu.%04lu", e4 < 0 ? "-" : "", (unsigned long)(a / 10000), (unsigned long)(a % 10000));
}

/* snprintf's result, or 0 when it didn't fit: callers never see half a URL. */
static size_t fitted(int n, size_t size)
{
    return n > 0 && (size_t)n < size ? (size_t)n : 0;
}

size_t weather_forecast_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4)
{
    char lat[16], lon[16];
    degrees(lat, sizeof(lat), lat_e4);
    degrees(lon, sizeof(lon), lon_e4);
    size_t n = fitted(snprintf(out, size,
                               "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
                               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
                               "weather_code,wind_speed_10m"
                               "&hourly=temperature_2m,weather_code,precipitation_probability"
                               "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
                               "precipitation_probability_max,sunrise,sunset"
                               "&timezone=auto&timeformat=unixtime&forecast_days=3",
                               lat, lon),
                      size);
    if (n == 0 && size > 0) {
        out[0] = '\0';
    }
    return n;
}

size_t weather_air_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4)
{
    char lat[16], lon[16];
    degrees(lat, sizeof(lat), lat_e4);
    degrees(lon, sizeof(lon), lon_e4);
    size_t n = fitted(snprintf(out, size,
                               "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%s&longitude=%s"
                               "&hourly=european_aqi,pm2_5,pm10,uv_index,alder_pollen,birch_pollen,grass_pollen,"
                               "mugwort_pollen,olive_pollen,ragweed_pollen"
                               "&timezone=auto&timeformat=unixtime&forecast_days=3",
                               lat, lon),
                      size);
    if (n == 0 && size > 0) {
        out[0] = '\0';
    }
    return n;
}

size_t weather_geocode_url(char *out, size_t size, const char *query, const char *language)
{
    static const char k_head[] = "https://geocoding-api.open-meteo.com/v1/search?name=";
    size_t n = strlen(k_head);
    if (size <= n) {
        return 0;
    }
    memcpy(out, k_head, n);
    for (const unsigned char *c = (const unsigned char *)query; *c; c++) {
        bool plain = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                     *c == '-' || *c == '.' || *c == '_' || *c == '~'; /* RFC 3986 unreserved */
        if (n + (plain ? 1 : 3) >= size) {
            out[0] = '\0';
            return 0;
        }
        if (plain) {
            out[n++] = (char)*c;
        } else {
            n += (size_t)snprintf(out + n, size - n, "%%%02X", *c);
        }
    }
    size_t tail = fitted(snprintf(out + n, size - n, "&count=5&language=%s&format=json", language), size - n);
    if (tail == 0) {
        out[0] = '\0';
        return 0;
    }
    return n + tail;
}
