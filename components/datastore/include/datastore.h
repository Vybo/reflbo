#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * Field store (spec §6): measured and fetched values with their age and freshness. Fields that
 * follow from the clock (time, date, week, moon phase, name day, holiday) are computed by `ui`
 * from the time and the language pack, so they are not stored. A ds_t is plain data: the app
 * keeps it in its RTC-RAM snapshot, so it survives deep sleep. Pure C, host-buildable. The app
 * task owns it: the sync task hands its results over to the app task, which stores them.
 */

typedef enum {
    DS_ENV_TEMP,     /* 0.01 °C, calibration offset applied */
    DS_ENV_HUM,      /* 0.01 %RH */
    DS_ENV_DEW,      /* 0.01 °C, from temperature and humidity */
    DS_ENV_TEMP_MIN, /* 0.01 °C, since local midnight */
    DS_ENV_TEMP_MAX,
    DS_BAT_LEVEL, /* %; the entry also carries the voltage and the charging state */
    DS_BAT_DAYS,  /* 0.1 days of battery left */
    DS_FIELD_COUNT,
} ds_field_t;

typedef enum {
    DS_BAT_UNKNOWN,
    DS_BAT_DISCHARGING,
    DS_BAT_CHARGING,
    DS_BAT_FULL,
} ds_bat_state_t;

typedef enum {
    DS_MISSING, /* never set, cleared, or from another day */
    DS_FRESH,
    DS_STALE,   /* older than the field's time to live */
} ds_freshness_t;

#define DS_NO_TREND INT32_MIN
#define DS_ENV_HISTORY 16

typedef struct {
    int32_t value;    /* in the unit listed above */
    int32_t trend;    /* change over about the last hour, same unit; DS_NO_TREND if unknown */
    uint32_t updated; /* UTC seconds; 0 = not set */
    int16_t mv;       /* DS_BAT_LEVEL: smoothed battery voltage */
    uint8_t bat_state; /* DS_BAT_LEVEL: ds_bat_state_t */
} ds_entry_t;

typedef struct {
    uint32_t time;
    int32_t temp;
    int32_t hum;
} ds_env_point_t;

/* Weather and air quality (spec §6, D25), as fetched from Open-Meteo, kept compact for the snapshot. */
#define DS_WX_HOURS 72 /* 3 days of hourly entries, from the local midnight of the fetch's day */
#define DS_WX_DAYS 3
#define DS_WX_NO_TEMP INT16_MIN
#define DS_WX_NO_CODE 0xFF
#define DS_WX_NO_PCT 0xFF
#define DS_WX_NO_WIND 0xFFFF

typedef struct {
    int16_t temp_c10; /* 0.1 °C; DS_WX_NO_TEMP if missing */
    uint8_t code;     /* WMO weather code; DS_WX_NO_CODE if missing */
    uint8_t precip;   /* precipitation probability, %; DS_WX_NO_PCT if missing */
} ds_wx_hour_t;

typedef struct {
    int16_t min_c10, max_c10;
    uint8_t code;
    uint8_t precip; /* the day's highest precipitation probability, % */
} ds_wx_day_t;

/* Rain in quarter hours (spec §6, §11.4, D27): Open-Meteo's `minutely_15`, 24 h from the fetch. */
#define DS_RAIN_STEPS 96
#define DS_RAIN_STEP_S 900
#define DS_RAIN_NONE 0xFF /* a missing amount or probability; amounts are capped at 254 (25.4 mm) */

typedef struct {
    uint32_t t0;                 /* UTC of mm10[0], on a quarter hour; 0 = none came */
    uint8_t mm10[DS_RAIN_STEPS]; /* precipitation in the quarter hour up to t0 + 900 i, 0.1 mm (Open-Meteo sums
                                  * the 15 minutes before each time) */
    uint8_t prob[DS_RAIN_STEPS]; /* its probability, % */
} ds_rain_t;

typedef struct {
    uint32_t fetched; /* UTC seconds; 0 = never */
    uint32_t now_time; /* the `current` block: when it was valid (UTC) */
    int16_t now_temp_c10, now_feels_c10;
    uint16_t now_wind_kmh10;
    uint8_t now_hum, now_code, now_is_day;
    uint32_t hour0; /* UTC of hours[0], on an hour */
    ds_wx_hour_t hours[DS_WX_HOURS];
    int32_t day0_local; /* days[0] as days since 1970-01-01, the location's local date */
    ds_wx_day_t days[DS_WX_DAYS];
    ds_rain_t rain; /* M6 */
} ds_weather_t;

typedef enum {
    DS_POLLEN_ALDER,
    DS_POLLEN_BIRCH,
    DS_POLLEN_GRASS,
    DS_POLLEN_MUGWORT,
    DS_POLLEN_OLIVE,
    DS_POLLEN_RAGWEED,
    DS_POLLEN_TYPES,
} ds_pollen_t;

#define DS_AQ_NONE 0xFF       /* aqi, pm25, pm10, uv10: missing; values are capped at 254 */
#define DS_POLLEN_NONE 0xFFFF /* no data for that type and day: outside Europe or out of season */

typedef struct {
    uint32_t fetched; /* UTC seconds; 0 = never */
    uint32_t hour0;
    uint8_t aqi[DS_WX_HOURS];  /* European air quality index */
    uint8_t pm25[DS_WX_HOURS]; /* µg/m³ */
    uint8_t pm10[DS_WX_HOURS];
    uint8_t uv10[DS_WX_HOURS]; /* the UV index in tenths (D26) */
    int32_t day0_local;
    uint16_t pollen[DS_WX_DAYS][DS_POLLEN_TYPES]; /* each day's peak, 0.1 grains/m³ */
} ds_air_t;

/* The weather now (spec §5.1): the `current` block while it is at most an hour old, otherwise
 * the hourly entry of the hour now. */
typedef struct {
    int16_t temp_c10;
    int16_t feels_c10;   /* DS_WX_NO_TEMP from an hourly entry */
    uint16_t wind_kmh10; /* DS_WX_NO_WIND from an hourly entry */
    uint8_t code;
    uint8_t hum;         /* DS_WX_NO_PCT from an hourly entry */
    uint8_t precip;      /* this hour's probability */
    int8_t is_day;       /* 1 or 0; -1 from an hourly entry, which doesn't say */
} ds_wx_now_t;

/* Bits of ds_take_changes() beside the ds_field_t ones. */
#define DS_CHANGE_WEATHER (1u << 30)
#define DS_CHANGE_AIR (1u << 31)

typedef struct {
    ds_entry_t entries[DS_FIELD_COUNT];
    uint32_t ttl_s[DS_FIELD_COUNT];
    ds_env_point_t env_history[DS_ENV_HISTORY];
    uint8_t env_head;
    uint8_t env_count;
    int32_t min_max_day; /* local day of the min and max */
    uint32_t changes;    /* one bit per ds_field_t, and DS_CHANGE_*, since the last ds_take_changes() */
    uint32_t forecast_ttl_s; /* weather and air quality: the expected sync interval plus 2 h; 0 = never stale */
    ds_weather_t weather;
    ds_air_t air;
} ds_t;

void ds_init(ds_t *ds);
/* The time after which a value counts as stale (spec §5.1: 15 min for the sensors). */
void ds_set_ttl(ds_t *ds, ds_field_t field, uint32_t ttl_s);
/* An SHTC3 reading. `local_day` numbers the local calendar day; the min and max restart when
 * it changes. Also updates the dew point and the trends. */
void ds_set_env(ds_t *ds, int temp_c100, int hum_pct100, time_t now, int32_t local_day);
void ds_set_battery(ds_t *ds, int level_pct, int mv, ds_bat_state_t state, time_t now);
/* Any field to a plain value, e.g. from the `field set` console command. */
void ds_set(ds_t *ds, ds_field_t field, int32_t value, time_t now);
void ds_clear(ds_t *ds, ds_field_t field);
/* False, and *out untouched, if the field is not set. */
bool ds_get(const ds_t *ds, ds_field_t field, ds_entry_t *out);
ds_freshness_t ds_freshness(const ds_t *ds, ds_field_t field, time_t now, int32_t local_day);
/* The fields changed since the previous call, as a bit mask of ds_field_t and DS_CHANGE_*. */
uint32_t ds_take_changes(ds_t *ds);

/* A sync's forecast or air quality, replacing the last one whole; `fetched` must be set. */
void ds_set_weather(ds_t *ds, const ds_weather_t *w);
void ds_set_air(ds_t *ds, const ds_air_t *a);
/* Spec §5.1: both stay fresh until this long after their fetch, then stale; 0 = never stale. */
void ds_set_forecast_ttl(ds_t *ds, uint32_t ttl_s);
/* NULL until the first fetch. */
const ds_weather_t *ds_weather(const ds_t *ds);
const ds_air_t *ds_air(const ds_t *ds);
/* Missing before the first fetch and past the last hourly entry; stale after the ttl. */
ds_freshness_t ds_weather_freshness(const ds_t *ds, time_t now);
ds_freshness_t ds_air_freshness(const ds_t *ds, time_t now);
/* The hourly entry that holds `t`, from `hour0`: 0..DS_WX_HOURS-1, or -1 outside them (or hour0 0). */
int ds_hour_index(uint32_t hour0, time_t t);
/* The entry whose quarter hour holds `t`, the one that ends at or after it: 0..DS_RAIN_STEPS-1, or -1
 * outside them (or t0 0). */
int ds_rain_index(uint32_t t0, time_t t);
/* False before the first fetch, or when neither the current block nor this hour's entry exists. */
bool ds_weather_now(const ds_t *ds, time_t now, ds_wx_now_t *out);
