#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "weather.h"

/* Open-Meteo requests and replies (spec §11, D25). The fixtures were fetched from the live APIs on
 * 2026-10-01 for Brno (49.1951, 16.6068) and New York, where CAMS has no pollen. */

static char s_json[16384];
static size_t s_len;
static char s_err[96];

void setUp(void) {}
void tearDown(void) {}

static const char *fixture(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    s_len = fread(s_json, 1, sizeof(s_json) - 1, f);
    fclose(f);
    s_json[s_len] = '\0';
    return s_json;
}

static void test_the_requests_ask_for_what_the_spec_lists(void)
{
    char url[WEATHER_URL_MAX];
    TEST_ASSERT_TRUE(weather_forecast_url(url, sizeof(url), 491951, 166068) > 0);
    TEST_ASSERT_EQUAL_STRING("https://api.open-meteo.com/v1/forecast?latitude=49.1951&longitude=16.6068"
                             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
                             "weather_code,wind_speed_10m"
                             "&hourly=temperature_2m,weather_code,precipitation_probability"
                             "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
                             "precipitation_probability_max,sunrise,sunset"
                             "&minutely_15=precipitation,precipitation_probability&forecast_minutely_15=96"
                             "&timezone=auto&timeformat=unixtime&forecast_days=3",
                             url);
    TEST_ASSERT_TRUE(weather_air_url(url, sizeof(url), -338688, -1512093) > 0); /* Sydney: both signs */
    TEST_ASSERT_EQUAL_STRING("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=-33.8688"
                             "&longitude=-151.2093"
                             "&hourly=european_aqi,pm2_5,pm10,uv_index,alder_pollen,birch_pollen,grass_pollen,"
                             "mugwort_pollen,olive_pollen,ragweed_pollen"
                             "&timezone=auto&timeformat=unixtime&forecast_days=3",
                             url);
    TEST_ASSERT_EQUAL(0, weather_forecast_url(url, 40, 491951, 166068)); /* too small: nothing half-written */
}

static void test_the_place_search_encodes_the_query(void)
{
    char url[WEATHER_URL_MAX];
    TEST_ASSERT_TRUE(weather_geocode_url(url, sizeof(url), "Ústí nad Labem&x", "cs") > 0);
    TEST_ASSERT_EQUAL_STRING("https://geocoding-api.open-meteo.com/v1/search?name=%C3%9Ast%C3%AD%20nad%20Labem%26x"
                             "&count=5&language=cs&format=json",
                             url);
}

static void test_a_forecast_parses_into_the_datastore_form(void)
{
    ds_weather_t w;
    const char *json = fixture("forecast_brno_2026-10-01.json");
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_forecast(json, s_len, &w, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT32(0, w.fetched); /* the caller's to set */
    TEST_ASSERT_EQUAL_UINT32(1790859600u, w.now_time);
    TEST_ASSERT_EQUAL_INT16(232, w.now_temp_c10);
    TEST_ASSERT_EQUAL_INT16(186, w.now_feels_c10);
    TEST_ASSERT_EQUAL_UINT16(203, w.now_wind_kmh10);
    TEST_ASSERT_EQUAL_UINT8(27, w.now_hum);
    TEST_ASSERT_EQUAL_UINT8(0, w.now_code);
    TEST_ASSERT_EQUAL_UINT8(1, w.now_is_day);
    TEST_ASSERT_EQUAL_UINT32(1790805600u, w.hour0); /* local midnight, 22:00 UTC the day before */
    TEST_ASSERT_EQUAL_INT16(160, w.hours[0].temp_c10);
    TEST_ASSERT_EQUAL_UINT8(3, w.hours[0].code);
    TEST_ASSERT_EQUAL_UINT8(0, w.hours[0].precip);
    TEST_ASSERT_EQUAL_INT16(114, w.hours[71].temp_c10);
    TEST_ASSERT_EQUAL_INT32(20727, w.day0_local); /* 2026-10-01 */
    TEST_ASSERT_EQUAL_INT16(131, w.days[0].min_c10);
    TEST_ASSERT_EQUAL_INT16(232, w.days[0].max_c10);
    TEST_ASSERT_EQUAL_UINT8(3, w.days[0].code);
    TEST_ASSERT_EQUAL_INT16(96, w.days[2].min_c10);
    TEST_ASSERT_EQUAL_INT16(225, w.days[2].max_c10);
}

/* Bergen at 00:57 local on 2026-10-02, drizzling: 0.1 mm in the first quarter hour (spec §11.4). */
static void test_the_rain_in_quarter_hours_comes_with_the_forecast(void)
{
    ds_weather_t w;
    const char *json = fixture("forecast_bergen_2026-10-02.json");
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_forecast(json, s_len, &w, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT32(1790895600u, w.rain.t0); /* 22:45 UTC: the quarter hour of the fetch */
    TEST_ASSERT_EQUAL_UINT8(1, w.rain.mm10[0]);
    TEST_ASSERT_EQUAL_UINT8(0, w.rain.mm10[1]);
    TEST_ASSERT_EQUAL_UINT8(100, w.rain.prob[0]);
    TEST_ASSERT_EQUAL_UINT8(97, w.rain.prob[5]);
    TEST_ASSERT_EQUAL_UINT8(27, w.rain.prob[DS_RAIN_STEPS - 1]);
    TEST_ASSERT_EQUAL_UINT32(1790892000u, w.hour0); /* the rest as before: Oslo's midnight */

    json = fixture("forecast_brno_2026-10-01.json"); /* an M5 reply, without them */
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_forecast(json, s_len, &w, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT32(0, w.rain.t0);
}

static void test_rain_is_capped_and_its_gaps_stay_missing(void)
{
    static const char k_json[] =
        "{\"hourly\":{\"time\":[1790892000]},"
        "\"minutely_15\":{\"time\":[1790895600,1790896500,1790897400],"
        "\"precipitation\":[30.0,null,0.04],\"precipitation_probability\":[null,120,55]}}";
    ds_weather_t w;
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_forecast(k_json, strlen(k_json), &w, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT32(1790895600u, w.rain.t0);
    TEST_ASSERT_EQUAL_UINT8(254, w.rain.mm10[0]); /* 25.4 mm at most */
    TEST_ASSERT_EQUAL_UINT8(DS_RAIN_NONE, w.rain.mm10[1]);
    TEST_ASSERT_EQUAL_UINT8(0, w.rain.mm10[2]); /* 0.04 mm rounds to none */
    TEST_ASSERT_EQUAL_UINT8(DS_RAIN_NONE, w.rain.prob[0]);
    TEST_ASSERT_EQUAL_UINT8(100, w.rain.prob[1]);
    TEST_ASSERT_EQUAL_UINT8(DS_RAIN_NONE, w.rain.mm10[3]); /* beyond the series */

    static const char k_gap[] = /* not 15 minutes apart: no rain series, the forecast still stands */
        "{\"hourly\":{\"time\":[1790892000]},"
        "\"minutely_15\":{\"time\":[1790895600,1790897400],\"precipitation\":[1.0,1.0]}}";
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_forecast(k_gap, strlen(k_gap), &w, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT32(0, w.rain.t0);
}

static void test_nulls_and_short_series_stay_missing(void)
{
    static const char k_json[] =
        "{\"utc_offset_seconds\":3600,"
        "\"current\":{\"time\":1790859600,\"temperature_2m\":null,\"weather_code\":2,\"is_day\":0},"
        "\"hourly\":{\"time\":[1790809200,1790812800],\"temperature_2m\":[-3.25,null],"
        "\"weather_code\":[71,null],\"precipitation_probability\":[null,85]},"
        "\"daily\":{\"time\":[1790809200],\"weather_code\":[73],\"temperature_2m_max\":[-1.04],"
        "\"temperature_2m_min\":[-7.96],\"precipitation_probability_max\":[90]}}";
    ds_weather_t w;
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_forecast(k_json, strlen(k_json), &w, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT16(DS_WX_NO_TEMP, w.now_temp_c10);
    TEST_ASSERT_EQUAL_INT16(DS_WX_NO_TEMP, w.now_feels_c10);
    TEST_ASSERT_EQUAL_UINT8(DS_WX_NO_PCT, w.now_hum);
    TEST_ASSERT_EQUAL_UINT16(DS_WX_NO_WIND, w.now_wind_kmh10);
    TEST_ASSERT_EQUAL_INT16(-33, w.hours[0].temp_c10); /* -3.25 rounds away from zero */
    TEST_ASSERT_EQUAL_UINT8(DS_WX_NO_PCT, w.hours[0].precip);
    TEST_ASSERT_EQUAL_INT16(DS_WX_NO_TEMP, w.hours[1].temp_c10);
    TEST_ASSERT_EQUAL_UINT8(DS_WX_NO_CODE, w.hours[1].code);
    TEST_ASSERT_EQUAL_UINT8(85, w.hours[1].precip);
    TEST_ASSERT_EQUAL_INT16(DS_WX_NO_TEMP, w.hours[2].temp_c10); /* beyond the series */
    TEST_ASSERT_EQUAL_INT16(-10, w.days[0].max_c10);
    TEST_ASSERT_EQUAL_INT16(-80, w.days[0].min_c10);
    TEST_ASSERT_EQUAL_INT16(DS_WX_NO_TEMP, w.days[1].max_c10);
    TEST_ASSERT_EQUAL_UINT8(DS_WX_NO_CODE, w.days[1].code);
}

static void test_a_reply_that_is_no_forecast_is_refused(void)
{
    ds_weather_t w;
    static const char *const k_bad[] = {
        "not json",
        "[1,2,3]",
        "{\"error\":true,\"reason\":\"Latitude must be in range of -90 to 90°.\"}",
        "{\"hourly\":{\"time\":[]}}",
        "{\"hourly\":{\"time\":[1790805600,1790809300]}}", /* not an hour apart */
        "{\"hourly\":{\"time\":[\"2026-10-01T00:00\"]}}",   /* ISO times: we ask for unixtime */
    };
    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(weather_parse_forecast(k_bad[i], strlen(k_bad[i]), &w, s_err, sizeof(s_err)),
                                  k_bad[i]);
        TEST_ASSERT_TRUE(s_err[0] != '\0');
    }
}

static void test_air_quality_parses_with_daily_pollen_peaks(void)
{
    ds_air_t a;
    const char *json = fixture("air_brno_2026-10-01.json");
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_air(json, s_len, &a, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT32(1790805600u, a.hour0);
    TEST_ASSERT_EQUAL_INT32(20727, a.day0_local);
    TEST_ASSERT_EQUAL_UINT8(28, a.aqi[0]);
    TEST_ASSERT_EQUAL_UINT8(9, a.pm25[0]);  /* 8.8 µg/m³ */
    TEST_ASSERT_EQUAL_UINT8(12, a.pm10[0]); /* 11.9 */
    TEST_ASSERT_EQUAL_UINT8(33, a.aqi[13]);
    TEST_ASSERT_EQUAL_UINT8(6, a.pm25[13]);
    static const uint16_t k_peaks[DS_WX_DAYS][DS_POLLEN_TYPES] = {
        { 0, 0, 6, 43, 0, 13 }, { 0, 0, 6, 52, 0, 7 }, { 0, 0, 6, 54, 0, 1 }
    };
    TEST_ASSERT_EQUAL_UINT16_ARRAY(&k_peaks[0][0], &a.pollen[0][0], DS_WX_DAYS * DS_POLLEN_TYPES);
}

static void test_the_uv_index_comes_in_tenths(void)
{
    ds_air_t a;
    const char *json = fixture("air_uv_brno_2026-10-01.json"); /* with uv_index, as the request now asks */
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_air(json, s_len, &a, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(0, a.uv10[0]);
    TEST_ASSERT_EQUAL_UINT8(35, a.uv10[13]); /* 3.5 at 13:00 */
    TEST_ASSERT_EQUAL(WEATHER_UV_MODERATE, weather_uv_band(a.uv10[13]));
    json = fixture("air_brno_2026-10-01.json"); /* recorded before the request asked for it */
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_air(json, s_len, &a, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(DS_AQ_NONE, a.uv10[13]);
}

static void test_uv_bands_follow_the_who(void)
{
    TEST_ASSERT_EQUAL(WEATHER_UV_LOW, weather_uv_band(24));      /* 2.4 reads 2 */
    TEST_ASSERT_EQUAL(WEATHER_UV_MODERATE, weather_uv_band(25)); /* 2.5 reads 3 */
    TEST_ASSERT_EQUAL(WEATHER_UV_HIGH, weather_uv_band(60));
    TEST_ASSERT_EQUAL(WEATHER_UV_VERY_HIGH, weather_uv_band(104));
    TEST_ASSERT_EQUAL(WEATHER_UV_EXTREME, weather_uv_band(105));
}

static void test_pollen_outside_europe_is_no_data(void)
{
    ds_air_t a;
    const char *json = fixture("air_new_york_2026-10-01.json");
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_air(json, s_len, &a, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT32(20727, a.day0_local); /* the first hour is 00:00 EDT on 1 October */
    TEST_ASSERT_EQUAL_UINT8(65, a.aqi[0]);
    for (int d = 0; d < DS_WX_DAYS; d++) {
        for (int t = 0; t < DS_POLLEN_TYPES; t++) {
            TEST_ASSERT_EQUAL_UINT16(DS_POLLEN_NONE, a.pollen[d][t]);
        }
    }
}

static void test_big_readings_are_capped_not_wrapped(void)
{
    static const char k_json[] =
        "{\"utc_offset_seconds\":0,\"hourly\":{\"time\":[1790812800],\"european_aqi\":[412],"
        "\"pm2_5\":[999.4],\"pm10\":[-1],\"birch_pollen\":[123456.7]}}";
    ds_air_t a;
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_air(k_json, strlen(k_json), &a, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT8(254, a.aqi[0]);
    TEST_ASSERT_EQUAL_UINT8(254, a.pm25[0]);
    TEST_ASSERT_EQUAL_UINT8(0, a.pm10[0]);
    TEST_ASSERT_EQUAL_UINT16(65534, a.pollen[0][DS_POLLEN_BIRCH]);
    TEST_ASSERT_EQUAL_UINT8(DS_AQ_NONE, a.aqi[1]);
}

static void test_places_come_from_the_geocoding_reply(void)
{
    weather_place_t p[5];
    const char *json = fixture("geocode_brno_2026-10-01.json");
    TEST_ASSERT_EQUAL_INT(5, weather_parse_places(json, s_len, p, 5));
    TEST_ASSERT_EQUAL_STRING("Brno", p[0].name);
    TEST_ASSERT_EQUAL_STRING("South Moravian", p[0].region);
    TEST_ASSERT_EQUAL_STRING("CZ", p[0].country);
    TEST_ASSERT_EQUAL_INT32(491952, p[0].lat_e4);
    TEST_ASSERT_EQUAL_INT32(166080, p[0].lon_e4);
    TEST_ASSERT_EQUAL_STRING("Europe/Prague", p[0].timezone);
    TEST_ASSERT_EQUAL_STRING("Plzeň Region", p[2].region);
    TEST_ASSERT_EQUAL_INT32(-969586, p[1].lon_e4);
    TEST_ASSERT_EQUAL_INT(2, weather_parse_places(json, s_len, p, 2));
    json = fixture("geocode_none_2026-10-01.json");
    TEST_ASSERT_EQUAL_INT(0, weather_parse_places(json, s_len, p, 5));
    TEST_ASSERT_EQUAL_INT(-1, weather_parse_places("<html>", 6, p, 5));
}

static void test_weather_codes_map_to_skies(void)
{
    static const struct {
        uint8_t code;
        weather_sky_t sky;
    } k_map[] = {
        { 0, WEATHER_SKY_CLEAR },         { 1, WEATHER_SKY_MAINLY_CLEAR },  { 2, WEATHER_SKY_PARTLY_CLOUDY },
        { 3, WEATHER_SKY_OVERCAST },      { 45, WEATHER_SKY_FOG },          { 48, WEATHER_SKY_FOG },
        { 51, WEATHER_SKY_DRIZZLE },      { 55, WEATHER_SKY_DRIZZLE },      { 56, WEATHER_SKY_FREEZING_RAIN },
        { 57, WEATHER_SKY_FREEZING_RAIN }, { 61, WEATHER_SKY_RAIN },        { 65, WEATHER_SKY_RAIN },
        { 66, WEATHER_SKY_FREEZING_RAIN }, { 67, WEATHER_SKY_FREEZING_RAIN }, { 71, WEATHER_SKY_SNOW },
        { 77, WEATHER_SKY_SNOW },         { 80, WEATHER_SKY_SHOWERS },      { 82, WEATHER_SKY_SHOWERS },
        { 85, WEATHER_SKY_SNOW_SHOWERS }, { 86, WEATHER_SKY_SNOW_SHOWERS }, { 95, WEATHER_SKY_THUNDERSTORM },
        { 99, WEATHER_SKY_THUNDERSTORM }, { 4, WEATHER_SKY_UNKNOWN },       { DS_WX_NO_CODE, WEATHER_SKY_UNKNOWN },
    };
    for (size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++) {
        TEST_ASSERT_EQUAL_MESSAGE(k_map[i].sky, weather_sky(k_map[i].code), "code");
    }
}

static void test_the_index_bands_close_at_their_upper_bound(void)
{
    TEST_ASSERT_EQUAL(WEATHER_AQ_GOOD, weather_aq_band(0));
    TEST_ASSERT_EQUAL(WEATHER_AQ_GOOD, weather_aq_band(20));
    TEST_ASSERT_EQUAL(WEATHER_AQ_FAIR, weather_aq_band(21));
    TEST_ASSERT_EQUAL(WEATHER_AQ_MODERATE, weather_aq_band(60));
    TEST_ASSERT_EQUAL(WEATHER_AQ_POOR, weather_aq_band(80));
    TEST_ASSERT_EQUAL(WEATHER_AQ_VERY_POOR, weather_aq_band(100));
    TEST_ASSERT_EQUAL(WEATHER_AQ_EXTREMELY_POOR, weather_aq_band(101));
}

static void test_pollen_levels_follow_each_types_thresholds(void)
{
    TEST_ASSERT_EQUAL(WEATHER_POLLEN_NONE, weather_pollen_level(DS_POLLEN_BIRCH, 9));
    TEST_ASSERT_EQUAL(WEATHER_POLLEN_LOW, weather_pollen_level(DS_POLLEN_BIRCH, 10));
    TEST_ASSERT_EQUAL(WEATHER_POLLEN_LOW, weather_pollen_level(DS_POLLEN_BIRCH, 99));
    TEST_ASSERT_EQUAL(WEATHER_POLLEN_MODERATE, weather_pollen_level(DS_POLLEN_BIRCH, 100));
    TEST_ASSERT_EQUAL(WEATHER_POLLEN_HIGH, weather_pollen_level(DS_POLLEN_BIRCH, 1000));
    TEST_ASSERT_EQUAL(WEATHER_POLLEN_LOW, weather_pollen_level(DS_POLLEN_GRASS, 29));
    TEST_ASSERT_EQUAL(WEATHER_POLLEN_MODERATE, weather_pollen_level(DS_POLLEN_GRASS, 30));
    TEST_ASSERT_EQUAL(WEATHER_POLLEN_HIGH, weather_pollen_level(DS_POLLEN_RAGWEED, 500));
    TEST_ASSERT_EQUAL(WEATHER_POLLEN_NONE, weather_pollen_level(DS_POLLEN_GRASS, DS_POLLEN_NONE));
}

static void test_the_top_pollen_is_the_highest_level_then_the_nearest_its_peak(void)
{
    uint16_t day[DS_POLLEN_TYPES] = { 0, 600, 400, 0, 0, 0 }; /* birch 60, grass 40: both moderate */
    TEST_ASSERT_EQUAL_INT(DS_POLLEN_GRASS, weather_pollen_top(day)); /* 40 of 50 beats 60 of 100 */
    day[DS_POLLEN_MUGWORT] = 1200; /* 120: high */
    TEST_ASSERT_EQUAL_INT(DS_POLLEN_MUGWORT, weather_pollen_top(day));
    static const uint16_t k_brno[DS_POLLEN_TYPES] = { 0, 0, 6, 43, 0, 13 }; /* the fixture's first day */
    TEST_ASSERT_EQUAL_INT(DS_POLLEN_MUGWORT, weather_pollen_top(k_brno));
    static const uint16_t k_none[DS_POLLEN_TYPES] = { 9, DS_POLLEN_NONE, 2, 0, DS_POLLEN_NONE, 5 };
    TEST_ASSERT_EQUAL_INT(-1, weather_pollen_top(k_none));
    TEST_ASSERT_EQUAL_STRING("ragweed", weather_pollen_name(DS_POLLEN_RAGWEED));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_requests_ask_for_what_the_spec_lists);
    RUN_TEST(test_the_place_search_encodes_the_query);
    RUN_TEST(test_a_forecast_parses_into_the_datastore_form);
    RUN_TEST(test_the_rain_in_quarter_hours_comes_with_the_forecast);
    RUN_TEST(test_rain_is_capped_and_its_gaps_stay_missing);
    RUN_TEST(test_nulls_and_short_series_stay_missing);
    RUN_TEST(test_a_reply_that_is_no_forecast_is_refused);
    RUN_TEST(test_air_quality_parses_with_daily_pollen_peaks);
    RUN_TEST(test_the_uv_index_comes_in_tenths);
    RUN_TEST(test_uv_bands_follow_the_who);
    RUN_TEST(test_pollen_outside_europe_is_no_data);
    RUN_TEST(test_big_readings_are_capped_not_wrapped);
    RUN_TEST(test_places_come_from_the_geocoding_reply);
    RUN_TEST(test_weather_codes_map_to_skies);
    RUN_TEST(test_the_index_bands_close_at_their_upper_bound);
    RUN_TEST(test_pollen_levels_follow_each_types_thresholds);
    RUN_TEST(test_the_top_pollen_is_the_highest_level_then_the_nearest_its_peak);
    return UNITY_END();
}
