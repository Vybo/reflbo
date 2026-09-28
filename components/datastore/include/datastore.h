#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * Field store (spec §6): measured and fetched values with their age and freshness. Fields that
 * follow from the clock (time, date, week, moon phase, name day, holiday) are computed by `ui`
 * from the time and the language pack, so they are not stored. A ds_t is plain data: the app
 * keeps it in its RTC-RAM snapshot, so it survives deep sleep. Pure C, host-buildable. The app
 * task owns it; a lock arrives with the first other writer (the sync task, M5).
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

typedef struct {
    ds_entry_t entries[DS_FIELD_COUNT];
    uint32_t ttl_s[DS_FIELD_COUNT];
    ds_env_point_t env_history[DS_ENV_HISTORY];
    uint8_t env_head;
    uint8_t env_count;
    int32_t min_max_day; /* local day of the min and max */
    uint32_t changes;    /* one bit per ds_field_t since the last ds_take_changes() */
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
/* The fields changed since the previous call, as a bit mask of ds_field_t. */
uint32_t ds_take_changes(ds_t *ds);
