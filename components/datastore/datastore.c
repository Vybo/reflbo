#include "datastore.h"

#include <math.h>
#include <string.h>

#define DEFAULT_TTL_S       900  /* spec §5.1: stale after 15 min without a reading */
#define HISTORY_SPACING_S   300  /* at most one trend point per 5 min */
#define TREND_MIN_AGE_S     3600 /* the trend compares with the newest reading 60–90 min old */
#define TREND_MAX_AGE_S     5400

void ds_init(ds_t *ds)
{
    memset(ds, 0, sizeof(*ds));
    for (int f = 0; f < DS_FIELD_COUNT; f++) {
        ds->entries[f].trend = DS_NO_TREND;
        ds->ttl_s[f] = DEFAULT_TTL_S;
    }
    ds->min_max_day = INT32_MIN;
}

void ds_set_ttl(ds_t *ds, ds_field_t field, uint32_t ttl_s)
{
    if ((unsigned)field < DS_FIELD_COUNT) {
        ds->ttl_s[field] = ttl_s;
    }
}

static void store(ds_t *ds, ds_field_t field, int32_t value, int32_t trend, time_t now)
{
    ds_entry_t *e = &ds->entries[field];
    e->value = value;
    e->trend = trend;
    e->updated = (uint32_t)now;
    ds->changes |= 1u << field;
}

/* Magnus formula (Sonntag 1990 constants); NaN below 1 %RH, where it breaks down. */
static int32_t dew_point_c100(int temp_c100, int hum_pct100)
{
    double t = temp_c100 / 100.0;
    double rh = hum_pct100 / 100.0;
    double gamma = log(rh / 100.0) + 17.62 * t / (243.12 + t);
    return (int32_t)lround(243.12 * gamma / (17.62 - gamma) * 100.0);
}

static const ds_env_point_t *point_ago(const ds_t *ds, uint32_t now)
{
    for (int k = 0; k < ds->env_count; k++) {
        const ds_env_point_t *p = &ds->env_history[(ds->env_head + DS_ENV_HISTORY - 1 - k) % DS_ENV_HISTORY];
        uint32_t age = now - p->time;
        if (age >= TREND_MIN_AGE_S) {
            return age <= TREND_MAX_AGE_S ? p : NULL;
        }
    }
    return NULL;
}

void ds_set_env(ds_t *ds, int temp_c100, int hum_pct100, time_t now, int32_t local_day)
{
    uint32_t t = (uint32_t)now;
    const ds_env_point_t *old = point_ago(ds, t);
    store(ds, DS_ENV_TEMP, temp_c100, old ? temp_c100 - old->temp : DS_NO_TREND, now);
    store(ds, DS_ENV_HUM, hum_pct100, old ? hum_pct100 - old->hum : DS_NO_TREND, now);
    if (hum_pct100 >= 100) {
        store(ds, DS_ENV_DEW, dew_point_c100(temp_c100, hum_pct100), DS_NO_TREND, now);
    } else {
        ds_clear(ds, DS_ENV_DEW);
    }

    if (ds->min_max_day != local_day || ds->entries[DS_ENV_TEMP_MIN].updated == 0) {
        ds->min_max_day = local_day;
        store(ds, DS_ENV_TEMP_MIN, temp_c100, DS_NO_TREND, now);
        store(ds, DS_ENV_TEMP_MAX, temp_c100, DS_NO_TREND, now);
    } else {
        if (temp_c100 < ds->entries[DS_ENV_TEMP_MIN].value) {
            store(ds, DS_ENV_TEMP_MIN, temp_c100, DS_NO_TREND, now);
        }
        if (temp_c100 > ds->entries[DS_ENV_TEMP_MAX].value) {
            store(ds, DS_ENV_TEMP_MAX, temp_c100, DS_NO_TREND, now);
        }
        ds->entries[DS_ENV_TEMP_MIN].updated = t; /* still today's: fresh as long as readings keep coming */
        ds->entries[DS_ENV_TEMP_MAX].updated = t;
    }

    const ds_env_point_t *newest =
        ds->env_count ? &ds->env_history[(ds->env_head + DS_ENV_HISTORY - 1) % DS_ENV_HISTORY] : NULL;
    if (newest == NULL || t - newest->time >= HISTORY_SPACING_S) {
        ds->env_history[ds->env_head] = (ds_env_point_t){ t, temp_c100, hum_pct100 };
        ds->env_head = (uint8_t)((ds->env_head + 1) % DS_ENV_HISTORY);
        if (ds->env_count < DS_ENV_HISTORY) {
            ds->env_count++;
        }
    }
}

void ds_set_battery(ds_t *ds, int level_pct, int mv, ds_bat_state_t state, time_t now)
{
    store(ds, DS_BAT_LEVEL, level_pct, DS_NO_TREND, now);
    ds->entries[DS_BAT_LEVEL].mv = (int16_t)mv;
    ds->entries[DS_BAT_LEVEL].bat_state = (uint8_t)state;
}

void ds_set(ds_t *ds, ds_field_t field, int32_t value, time_t now)
{
    if ((unsigned)field < DS_FIELD_COUNT) {
        store(ds, field, value, DS_NO_TREND, now);
    }
}

void ds_clear(ds_t *ds, ds_field_t field)
{
    if ((unsigned)field < DS_FIELD_COUNT && ds->entries[field].updated != 0) {
        ds->entries[field] = (ds_entry_t){ .trend = DS_NO_TREND };
        ds->changes |= 1u << field;
    }
}

bool ds_get(const ds_t *ds, ds_field_t field, ds_entry_t *out)
{
    if ((unsigned)field >= DS_FIELD_COUNT || ds->entries[field].updated == 0) {
        return false;
    }
    *out = ds->entries[field];
    return true;
}

ds_freshness_t ds_freshness(const ds_t *ds, ds_field_t field, time_t now, int32_t local_day)
{
    ds_entry_t e;
    if (!ds_get(ds, field, &e)) {
        return DS_MISSING;
    }
    if ((field == DS_ENV_TEMP_MIN || field == DS_ENV_TEMP_MAX) && ds->min_max_day != local_day) {
        return DS_MISSING; /* yesterday's extremes */
    }
    return (uint32_t)now - e.updated > ds->ttl_s[field] ? DS_STALE : DS_FRESH;
}

uint32_t ds_take_changes(ds_t *ds)
{
    uint32_t changes = ds->changes;
    ds->changes = 0;
    return changes;
}

void ds_set_weather(ds_t *ds, const ds_weather_t *w)
{
    ds->weather = *w;
    ds->changes |= DS_CHANGE_WEATHER;
}

void ds_set_air(ds_t *ds, const ds_air_t *a)
{
    ds->air = *a;
    ds->changes |= DS_CHANGE_AIR;
}

void ds_set_forecast_ttl(ds_t *ds, uint32_t ttl_s)
{
    ds->forecast_ttl_s = ttl_s;
}

const ds_weather_t *ds_weather(const ds_t *ds)
{
    return ds->weather.fetched != 0 ? &ds->weather : NULL;
}

const ds_air_t *ds_air(const ds_t *ds)
{
    return ds->air.fetched != 0 ? &ds->air : NULL;
}

int ds_hour_index(uint32_t hour0, time_t t)
{
    if (hour0 == 0 || t < (time_t)hour0) {
        return -1;
    }
    time_t i = (t - (time_t)hour0) / 3600;
    return i < DS_WX_HOURS ? (int)i : -1;
}

static ds_freshness_t forecast_freshness(const ds_t *ds, uint32_t fetched, uint32_t hour0, time_t now)
{
    if (fetched == 0 || ds_hour_index(hour0, now) < 0) {
        return DS_MISSING; /* never fetched, or past the forecast (spec §5.1) */
    }
    if (ds->forecast_ttl_s != 0 && now > (time_t)fetched && (uint32_t)(now - fetched) > ds->forecast_ttl_s) {
        return DS_STALE;
    }
    return DS_FRESH;
}

ds_freshness_t ds_weather_freshness(const ds_t *ds, time_t now)
{
    return forecast_freshness(ds, ds->weather.fetched, ds->weather.hour0, now);
}

ds_freshness_t ds_air_freshness(const ds_t *ds, time_t now)
{
    return forecast_freshness(ds, ds->air.fetched, ds->air.hour0, now);
}

bool ds_weather_now(const ds_t *ds, time_t now, ds_wx_now_t *out)
{
    const ds_weather_t *w = ds_weather(ds);
    if (w == NULL) {
        return false;
    }
    int h = ds_hour_index(w->hour0, now);
    uint8_t precip = h >= 0 ? w->hours[h].precip : DS_WX_NO_PCT;
    bool current = w->now_time != 0 && w->now_temp_c10 != DS_WX_NO_TEMP && now >= (time_t)w->now_time &&
                   now - (time_t)w->now_time <= 3600; /* spec §5.1: at most 60 minutes old */
    if (current) {
        *out = (ds_wx_now_t){ .temp_c10 = w->now_temp_c10, .feels_c10 = w->now_feels_c10,
                              .wind_kmh10 = w->now_wind_kmh10, .code = w->now_code, .hum = w->now_hum,
                              .precip = precip, .is_day = (int8_t)(w->now_is_day ? 1 : 0) };
        return true;
    }
    if (h < 0 || w->hours[h].temp_c10 == DS_WX_NO_TEMP) {
        return false;
    }
    *out = (ds_wx_now_t){ .temp_c10 = w->hours[h].temp_c10, .feels_c10 = DS_WX_NO_TEMP, .wind_kmh10 = DS_WX_NO_WIND,
                          .code = w->hours[h].code, .hum = DS_WX_NO_PCT, .precip = precip, .is_day = -1 };
    return true;
}
