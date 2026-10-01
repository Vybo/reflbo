#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "datastore.h"

/*
 * Weather, air quality and places from Open-Meteo (spec §11, D25). The request URLs, the parsers and
 * the bands and levels are pure C on cJSON, host-tested against recorded fixtures; weather_http.c
 * fetches on the device.
 */

#define WEATHER_URL_MAX 512

/* The forecast and air quality requests of spec §11 for a location in 1e-4 degrees. Return the
 * length written, or 0 if `size` is too small. */
size_t weather_forecast_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4);
size_t weather_air_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4);
/* The geocoding search for `query` (UTF-8, percent-encoded here), names in `language` ("en"). */
size_t weather_geocode_url(char *out, size_t size, const char *query, const char *language);

/* An Open-Meteo forecast into `out`, all but `fetched`, which the caller sets. False with the reason
 * in `err` if it isn't a forecast: not JSON, no hourly times, or hourly times that aren't an hour apart.
 * Values sent as null stay missing. */
bool weather_parse_forecast(const char *json, size_t len, ds_weather_t *out, char *err, size_t err_size);
/* Air quality likewise: the index and particles per hour, each pollen type's daily peak. */
bool weather_parse_air(const char *json, size_t len, ds_air_t *out, char *err, size_t err_size);

typedef struct {
    char name[48];
    char region[48];  /* admin1: "South Moravian" */
    char country[4];  /* ISO 3166-1 alpha-2: "CZ" */
    int32_t lat_e4, lon_e4;
    char timezone[40]; /* IANA: "Europe/Prague" */
} weather_place_t;

/* Geocoding results, at most `max`; 0 when there are none, -1 if it isn't a geocoding reply. */
int weather_parse_places(const char *json, size_t len, weather_place_t *out, int max);

/* What a WMO weather code shows (spec §11): an icon and a word in the language pack. */
typedef enum {
    WEATHER_SKY_CLEAR,
    WEATHER_SKY_MAINLY_CLEAR,
    WEATHER_SKY_PARTLY_CLOUDY,
    WEATHER_SKY_OVERCAST,
    WEATHER_SKY_FOG,
    WEATHER_SKY_DRIZZLE,
    WEATHER_SKY_RAIN,
    WEATHER_SKY_FREEZING_RAIN,
    WEATHER_SKY_SNOW,
    WEATHER_SKY_SHOWERS,
    WEATHER_SKY_SNOW_SHOWERS,
    WEATHER_SKY_THUNDERSTORM,
    WEATHER_SKY_UNKNOWN, /* a code Open-Meteo doesn't document */
} weather_sky_t;

weather_sky_t weather_sky(uint8_t wmo_code);

/* The European air quality index's bands (spec §5.1). */
typedef enum {
    WEATHER_AQ_GOOD,
    WEATHER_AQ_FAIR,
    WEATHER_AQ_MODERATE,
    WEATHER_AQ_POOR,
    WEATHER_AQ_VERY_POOR,
    WEATHER_AQ_EXTREMELY_POOR,
} weather_aq_band_t;

weather_aq_band_t weather_aq_band(int aqi);

/* The UV index's WHO bands (D26): 0-2, 3-5, 6-7, 8-10, 11 and above, of the index rounded. */
typedef enum {
    WEATHER_UV_LOW,
    WEATHER_UV_MODERATE,
    WEATHER_UV_HIGH,
    WEATHER_UV_VERY_HIGH,
    WEATHER_UV_EXTREME,
} weather_uv_band_t;

weather_uv_band_t weather_uv_band(int uv10);

typedef enum {
    WEATHER_POLLEN_NONE,
    WEATHER_POLLEN_LOW,
    WEATHER_POLLEN_MODERATE,
    WEATHER_POLLEN_HIGH,
} weather_pollen_level_t;

/* A concentration in 0.1 grains/m³ (DS_POLLEN_NONE: no data, which reads as none). */
weather_pollen_level_t weather_pollen_level(ds_pollen_t type, uint16_t grains10);
/* pollen.top (spec §5.1): the type with the highest level, then the largest share of its peak
 * threshold; -1 when every type is at none or has no data. */
int weather_pollen_top(const uint16_t day[DS_POLLEN_TYPES]);
/* "alder" ... "ragweed": the field ids' suffixes. */
const char *weather_pollen_name(ds_pollen_t type);
