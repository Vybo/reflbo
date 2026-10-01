#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "util_json.h"
#include "weather.h"

/* Open-Meteo replies (spec §11). They come with `timeformat=unixtime` and `timezone=auto`, so
 * every time is a UTC second and the first hourly entry is the location's local midnight. */

#define DEPTH_MAX 4 /* the replies nest three levels: root, block, series */
#define DAY_S 86400

static bool fail(char *err, size_t err_size, const char *why)
{
    snprintf(err, err_size, "%s", why);
    return false;
}

static const cJSON *member(const cJSON *o, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(o, key);
}

static bool number(const cJSON *item, double *out)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false; /* also null */
    }
    *out = item->valuedouble;
    return true;
}

static bool number_at(const cJSON *array, int i, double *out)
{
    return cJSON_IsArray(array) && number(cJSON_GetArrayItem(array, i), out);
}

static long rounded(double v)
{
    return lround(v); /* halves away from zero: -3.25 °C → -33 tenths */
}

static int16_t tenths(const cJSON *item)
{
    double v;
    if (!number(item, &v)) {
        return DS_WX_NO_TEMP;
    }
    long t = rounded(v * 10.0);
    return (int16_t)(t > INT16_MAX ? INT16_MAX : t <= INT16_MIN ? INT16_MIN + 1 : t);
}

/* A whole number within 0..max; `none` when missing. */
static uint32_t whole(const cJSON *item, uint32_t max, uint32_t none)
{
    double v;
    if (!number(item, &v)) {
        return none;
    }
    long n = rounded(v);
    return n < 0 ? 0 : (uint32_t)n > max ? max : (uint32_t)n;
}

static int32_t local_day(double utc, double offset_s)
{
    return (int32_t)floor((utc + offset_s) / DAY_S);
}

/* Parses the root and checks the hourly times: present, numbers, one hour apart. */
static cJSON *open_reply(const char *json, size_t len, double *hour0, double *offset, char *err, size_t err_size)
{
    if (util_json_depth(json) > DEPTH_MAX) {
        fail(err, err_size, "nested too deeply");
        return NULL;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        fail(err, err_size, "not a JSON object");
        return NULL;
    }
    const cJSON *reason = member(root, "reason");
    if (cJSON_IsTrue(member(root, "error"))) {
        snprintf(err, err_size, "Open-Meteo: %s", cJSON_IsString(reason) ? reason->valuestring : "error");
        cJSON_Delete(root);
        return NULL;
    }
    const cJSON *times = member(member(root, "hourly"), "time");
    int n = cJSON_IsArray(times) ? cJSON_GetArraySize(times) : 0;
    double t0 = 0, t;
    bool ok = n > 0 && number_at(times, 0, &t0);
    for (int i = 1; ok && i < n && i < DS_WX_HOURS; i++) {
        ok = number_at(times, i, &t) && t == t0 + 3600.0 * i;
    }
    if (!ok) {
        cJSON_Delete(root);
        fail(err, err_size, n == 0 ? "no hourly times" : "hourly times are not an hour apart");
        return NULL;
    }
    *hour0 = t0;
    *offset = 0;
    number(member(root, "utc_offset_seconds"), offset);
    return root;
}

bool weather_parse_forecast(const char *json, size_t len, ds_weather_t *out, char *err, size_t err_size)
{
    double hour0, offset;
    cJSON *root = open_reply(json, len, &hour0, &offset, err, err_size);
    if (root == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->hour0 = (uint32_t)hour0;

    const cJSON *now = member(root, "current");
    double t;
    out->now_time = number(member(now, "time"), &t) ? (uint32_t)t : 0;
    out->now_temp_c10 = tenths(member(now, "temperature_2m"));
    out->now_feels_c10 = tenths(member(now, "apparent_temperature"));
    out->now_hum = (uint8_t)whole(member(now, "relative_humidity_2m"), 100, DS_WX_NO_PCT);
    out->now_code = (uint8_t)whole(member(now, "weather_code"), 254, DS_WX_NO_CODE);
    out->now_is_day = (uint8_t)whole(member(now, "is_day"), 1, 1);
    double wind;
    out->now_wind_kmh10 = number(member(now, "wind_speed_10m"), &wind) && wind >= 0 && wind < 6553
                              ? (uint16_t)rounded(wind * 10.0)
                              : DS_WX_NO_WIND;

    const cJSON *hourly = member(root, "hourly");
    const cJSON *temps = member(hourly, "temperature_2m");
    const cJSON *codes = member(hourly, "weather_code");
    const cJSON *precip = member(hourly, "precipitation_probability");
    for (int i = 0; i < DS_WX_HOURS; i++) {
        out->hours[i] = (ds_wx_hour_t){
            .temp_c10 = tenths(cJSON_IsArray(temps) ? cJSON_GetArrayItem(temps, i) : NULL),
            .code = (uint8_t)whole(cJSON_IsArray(codes) ? cJSON_GetArrayItem(codes, i) : NULL, 254, DS_WX_NO_CODE),
            .precip = (uint8_t)whole(cJSON_IsArray(precip) ? cJSON_GetArrayItem(precip, i) : NULL, 100, DS_WX_NO_PCT),
        };
    }

    const cJSON *daily = member(root, "daily");
    double day0;
    out->day0_local = local_day(number_at(member(daily, "time"), 0, &day0) ? day0 : hour0, offset);
    const cJSON *dmin = member(daily, "temperature_2m_min");
    const cJSON *dmax = member(daily, "temperature_2m_max");
    const cJSON *dcode = member(daily, "weather_code");
    const cJSON *dprecip = member(daily, "precipitation_probability_max");
    for (int d = 0; d < DS_WX_DAYS; d++) {
        out->days[d] = (ds_wx_day_t){
            .min_c10 = tenths(cJSON_IsArray(dmin) ? cJSON_GetArrayItem(dmin, d) : NULL),
            .max_c10 = tenths(cJSON_IsArray(dmax) ? cJSON_GetArrayItem(dmax, d) : NULL),
            .code = (uint8_t)whole(cJSON_IsArray(dcode) ? cJSON_GetArrayItem(dcode, d) : NULL, 254, DS_WX_NO_CODE),
            .precip = (uint8_t)whole(cJSON_IsArray(dprecip) ? cJSON_GetArrayItem(dprecip, d) : NULL, 100,
                                     DS_WX_NO_PCT),
        };
    }
    cJSON_Delete(root);
    return true;
}

static const char *const k_pollen_keys[DS_POLLEN_TYPES] = {
    [DS_POLLEN_ALDER] = "alder_pollen",     [DS_POLLEN_BIRCH] = "birch_pollen", [DS_POLLEN_GRASS] = "grass_pollen",
    [DS_POLLEN_MUGWORT] = "mugwort_pollen", [DS_POLLEN_OLIVE] = "olive_pollen", [DS_POLLEN_RAGWEED] = "ragweed_pollen",
};

bool weather_parse_air(const char *json, size_t len, ds_air_t *out, char *err, size_t err_size)
{
    double hour0, offset;
    cJSON *root = open_reply(json, len, &hour0, &offset, err, err_size);
    if (root == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->hour0 = (uint32_t)hour0;
    out->day0_local = local_day(hour0, offset);
    const cJSON *hourly = member(root, "hourly");
    const cJSON *aqi = member(hourly, "european_aqi");
    const cJSON *pm25 = member(hourly, "pm2_5");
    const cJSON *pm10 = member(hourly, "pm10");
    const cJSON *uv = member(hourly, "uv_index");
    for (int i = 0; i < DS_WX_HOURS; i++) {
        out->aqi[i] = (uint8_t)whole(cJSON_IsArray(aqi) ? cJSON_GetArrayItem(aqi, i) : NULL, 254, DS_AQ_NONE);
        out->pm25[i] = (uint8_t)whole(cJSON_IsArray(pm25) ? cJSON_GetArrayItem(pm25, i) : NULL, 254, DS_AQ_NONE);
        out->pm10[i] = (uint8_t)whole(cJSON_IsArray(pm10) ? cJSON_GetArrayItem(pm10, i) : NULL, 254, DS_AQ_NONE);
        double u;
        out->uv10[i] = cJSON_IsArray(uv) && number(cJSON_GetArrayItem(uv, i), &u)
                           ? (uint8_t)(u <= 0 ? 0 : u >= 25.4 ? 254 : rounded(u * 10.0))
                           : DS_AQ_NONE;
    }
    for (int d = 0; d < DS_WX_DAYS; d++) {
        for (int p = 0; p < DS_POLLEN_TYPES; p++) {
            out->pollen[d][p] = DS_POLLEN_NONE;
        }
    }
    const cJSON *times = member(hourly, "time");
    for (int p = 0; p < DS_POLLEN_TYPES; p++) {
        const cJSON *series = member(hourly, k_pollen_keys[p]);
        int n = cJSON_IsArray(series) ? cJSON_GetArraySize(series) : 0;
        for (int i = 0; i < n && i < DS_WX_HOURS; i++) {
            double t, v;
            if (!number_at(times, i, &t) || !number_at(series, i, &v)) {
                continue; /* null: out of season, or outside Europe */
            }
            int d = local_day(t, offset) - out->day0_local; /* the hour's own local day, DST and all */
            if (d < 0 || d >= DS_WX_DAYS) {
                continue;
            }
            long g = v <= 0 ? 0 : rounded(v * 10.0);
            uint16_t g10 = (uint16_t)(g > 65534 ? 65534 : g);
            if (out->pollen[d][p] == DS_POLLEN_NONE || g10 > out->pollen[d][p]) {
                out->pollen[d][p] = g10;
            }
        }
    }
    cJSON_Delete(root);
    return true;
}

static void copy_text(char *out, size_t size, const cJSON *item)
{
    snprintf(out, size, "%s", cJSON_IsString(item) ? item->valuestring : "");
    /* snprintf may cut a UTF-8 sequence: drop a partial one at the end */
    size_t n = strlen(out);
    size_t i = n;
    while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80) {
        i--;
    }
    if (i > 0 && ((unsigned char)out[i - 1] & 0x80)) {
        unsigned char lead = (unsigned char)out[i - 1];
        size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
        if (n - (i - 1) < need) {
            out[i - 1] = '\0';
        }
    }
}

static int32_t e4(double degrees)
{
    return (int32_t)rounded(degrees * 10000.0);
}

int weather_parse_places(const char *json, size_t len, weather_place_t *out, int max)
{
    if (util_json_depth(json) > DEPTH_MAX) {
        return -1;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -1;
    }
    const cJSON *results = member(root, "results"); /* absent when nothing matched */
    const cJSON *list = cJSON_IsArray(results) ? results : NULL;
    int n = 0;
    const cJSON *r;
    cJSON_ArrayForEach(r, list)
    {
        double lat, lon;
        if (n >= max) {
            break;
        }
        if (!cJSON_IsString(member(r, "name")) || !number(member(r, "latitude"), &lat) ||
            !number(member(r, "longitude"), &lon) || fabs(lat) > 90 || fabs(lon) > 180) {
            continue;
        }
        weather_place_t *p = &out[n++];
        copy_text(p->name, sizeof(p->name), member(r, "name"));
        copy_text(p->region, sizeof(p->region), member(r, "admin1"));
        copy_text(p->country, sizeof(p->country), member(r, "country_code"));
        copy_text(p->timezone, sizeof(p->timezone), member(r, "timezone"));
        p->lat_e4 = e4(lat);
        p->lon_e4 = e4(lon);
    }
    cJSON_Delete(root);
    return n;
}
