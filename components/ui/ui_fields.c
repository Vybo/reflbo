#include "ui_fields.h"

#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define TEMP_TREND_C100 50 /* spec §5.1: arrows beyond 0.5 °C or 3 % an hour */
#define HUM_TREND_PCT100 300

static const ui_field_info_t k_fields[UI_FIELD_COUNT] = {
    [UI_FIELD_TIME_CLOCK] = { "time.clock", UI_FK_TIME, LS_TIME, -1 },
    [UI_FIELD_DATE_DAY] = { "date.day", UI_FK_DATE, LS_DATE, -1 },
    [UI_FIELD_DATE_WEEK] = { "date.week", UI_FK_NUMBER, LS_WEEK, -1 },
    [UI_FIELD_ENV_TEMP] = { "env.temp", UI_FK_NUMBER, LS_TEMPERATURE, DS_ENV_TEMP },
    [UI_FIELD_ENV_HUM] = { "env.hum", UI_FK_NUMBER, LS_HUMIDITY, DS_ENV_HUM },
    [UI_FIELD_ENV_DEW] = { "env.dew", UI_FK_NUMBER, LS_DEW_POINT, DS_ENV_DEW },
    [UI_FIELD_ENV_TEMP_MIN] = { "env.temp_min", UI_FK_NUMBER, LS_TODAY_MIN, DS_ENV_TEMP_MIN },
    [UI_FIELD_ENV_TEMP_MAX] = { "env.temp_max", UI_FK_NUMBER, LS_TODAY_MAX, DS_ENV_TEMP_MAX },
    [UI_FIELD_BAT_LEVEL] = { "bat.level", UI_FK_BATTERY, LS_BATTERY, DS_BAT_LEVEL },
    [UI_FIELD_BAT_DAYS] = { "bat.days", UI_FK_NUMBER, LS_BATTERY_DAYS, DS_BAT_DAYS },
    [UI_FIELD_MOON_PHASE] = { "moon.phase", UI_FK_MOON, LS_MOON, -1 },
    [UI_FIELD_DATE_NAMEDAY] = { "date.nameday", UI_FK_TEXT, LS_NAME_DAY, -1 },
    [UI_FIELD_DATE_HOLIDAY] = { "date.holiday", UI_FK_TEXT, LS_HOLIDAY, -1 },
    [UI_FIELD_WX_NOW] = { "wx.now", UI_FK_WEATHER_NOW, LS_WEATHER, -1 },
    [UI_FIELD_WX_TODAY] = { "wx.today", UI_FK_WEATHER_DAY, LS_TODAY, -1 },
    [UI_FIELD_WX_HOURLY] = { "wx.hourly", UI_FK_SERIES, LS_FORECAST, -1 },
    [UI_FIELD_WX_DAILY] = { "wx.daily", UI_FK_SERIES, LS_FORECAST, -1 },
    [UI_FIELD_SUN_TIMES] = { "sun.times", UI_FK_SUN, LS_SUN, -1 },
    [UI_FIELD_AQ_INDEX] = { "aq.index", UI_FK_LEVEL, LS_AIR_QUALITY, -1 },
    [UI_FIELD_AQ_PM25] = { "aq.pm25", UI_FK_NUMBER, LS_PM25, -1 },
    [UI_FIELD_AQ_PM10] = { "aq.pm10", UI_FK_NUMBER, LS_PM10, -1 },
    [UI_FIELD_AQ_UV] = { "aq.uv", UI_FK_LEVEL, LS_UV_INDEX, -1 },
    [UI_FIELD_POLLEN_TOP] = { "pollen.top", UI_FK_POLLEN, LS_POLLEN, -1 },
    [UI_FIELD_POLLEN_ALDER] = { "pollen.alder", UI_FK_POLLEN, LS_POLLEN_ALDER, -1 },
    [UI_FIELD_POLLEN_BIRCH] = { "pollen.birch", UI_FK_POLLEN, LS_POLLEN_BIRCH, -1 },
    [UI_FIELD_POLLEN_GRASS] = { "pollen.grass", UI_FK_POLLEN, LS_POLLEN_GRASS, -1 },
    [UI_FIELD_POLLEN_MUGWORT] = { "pollen.mugwort", UI_FK_POLLEN, LS_POLLEN_MUGWORT, -1 },
    [UI_FIELD_POLLEN_OLIVE] = { "pollen.olive", UI_FK_POLLEN, LS_POLLEN_OLIVE, -1 },
    [UI_FIELD_POLLEN_RAGWEED] = { "pollen.ragweed", UI_FK_POLLEN, LS_POLLEN_RAGWEED, -1 },
    [UI_FIELD_WX_RAIN2H] = { "wx.rain2h", UI_FK_SERIES, LS_RAIN_2H, -1 },
    [UI_FIELD_RAIN_MAP] = { "rain.map", UI_FK_RAIN_MAP, LS_RAIN_MAP, -1 },
    [UI_FIELD_HA_MESSAGE] = { "ha.message", UI_FK_TEXT, LS_MESSAGE, -1 },
};

const ui_field_info_t *ui_field_info(ui_field_id_t field)
{
    if (field <= UI_FIELD_NONE || field >= UI_FIELD_COUNT) {
        return NULL;
    }
    return &k_fields[field];
}

ui_field_id_t ui_field_by_name(const char *id)
{
    for (int f = UI_FIELD_NONE + 1; id != NULL && f < UI_FIELD_COUNT; f++) {
        if (strcmp(k_fields[f].id, id) == 0) {
            return (ui_field_id_t)f;
        }
    }
    return UI_FIELD_NONE;
}

static int trend_sign(int32_t trend, int32_t threshold)
{
    if (trend == DS_NO_TREND) {
        return 0;
    }
    return trend > threshold ? 1 : trend < -threshold ? -1 : 0;
}

/* 0.01 °C in the display unit, rounded to 0.1 (half away from zero). */
static long temp_tenths(const ui_context_t *ctx, int32_t c100)
{
    long v = ctx->fahrenheit ? (long)c100 * 9 / 5 + 3200 : c100;
    return (v + (v >= 0 ? 5 : -5)) / 10;
}

static bool from_store(const ui_context_t *ctx, ds_field_t f, ui_value_t *out, ds_entry_t *e)
{
    ds_freshness_t fresh = ds_freshness(ctx->ds, f, ctx->now, ctx->local_day);
    if (fresh == DS_MISSING || !ds_get(ctx->ds, f, e)) {
        return false;
    }
    out->state = fresh == DS_STALE ? UI_VALUE_STALE : UI_VALUE_FRESH;
    out->age_s = (uint32_t)ctx->now > e->updated ? (uint32_t)ctx->now - e->updated : 0;
    return true;
}

static void resolve_temperature(const ui_context_t *ctx, ds_field_t f, ui_value_t *out)
{
    ds_entry_t e;
    if (!from_store(ctx, f, out, &e)) {
        return;
    }
    long tenths = temp_tenths(ctx, e.value);
    lang_format_decimal(ctx->lang, tenths, 1, out->text, sizeof(out->text));
    lang_format_decimal(ctx->lang, (tenths + (tenths >= 0 ? 5 : -5)) / 10, 0, out->short_text, sizeof(out->short_text));
    snprintf(out->unit, sizeof(out->unit), "%s", ctx->fahrenheit ? "°F" : "°C");
    out->trend = trend_sign(e.trend, TEMP_TREND_C100);
}

static void resolve_store(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    ds_entry_t e;
    switch (field) {
    case UI_FIELD_ENV_TEMP:
    case UI_FIELD_ENV_DEW:
    case UI_FIELD_ENV_TEMP_MIN:
    case UI_FIELD_ENV_TEMP_MAX:
        resolve_temperature(ctx, (ds_field_t)ui_field_info(field)->ds_field, out);
        break;
    case UI_FIELD_ENV_HUM:
        if (from_store(ctx, DS_ENV_HUM, out, &e)) {
            snprintf(out->text, sizeof(out->text), "%ld", (long)(e.value + 50) / 100);
            snprintf(out->unit, sizeof(out->unit), "%%");
            out->trend = trend_sign(e.trend, HUM_TREND_PCT100);
        }
        break;
    case UI_FIELD_BAT_LEVEL:
        if (from_store(ctx, DS_BAT_LEVEL, out, &e)) {
            out->percent = e.value;
            out->battery = (ds_bat_state_t)e.bat_state;
            snprintf(out->text, sizeof(out->text), "%ld", (long)e.value);
            snprintf(out->unit, sizeof(out->unit), "%%");
            char volts[12];
            lang_format_decimal(ctx->lang, (e.mv + 5) / 10, 2, volts, sizeof(volts));
            snprintf(out->extra, sizeof(out->extra), "%s V", volts);
        }
        break;
    case UI_FIELD_BAT_DAYS:
        if (from_store(ctx, DS_BAT_DAYS, out, &e)) {
            if (e.value >= 100) { /* whole days from 10 on */
                snprintf(out->text, sizeof(out->text), "%ld", (long)(e.value + 5) / 10);
            } else {
                lang_format_decimal(ctx->lang, e.value, 1, out->text, sizeof(out->text));
                lang_format_decimal(ctx->lang, (e.value + 5) / 10, 0, out->short_text, sizeof(out->short_text));
            }
            snprintf(out->unit, sizeof(out->unit), "%s", lang_str(ctx->lang, LS_DAYS_UNIT));
        }
        break;
    default:
        break;
    }
}

static void resolve_clock(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    const struct tm *tm = &ctx->local;
    if (field == UI_FIELD_TIME_CLOCK) {
        out->state = UI_VALUE_FRESH; /* shown even when invalid: "--:--" (spec §5.3) */
        if (!ctx->time_valid) {
            snprintf(out->text, sizeof(out->text), "--:--");
            return;
        }
        const char *suffix;
        lang_format_time(tm->tm_hour, tm->tm_min, tm->tm_sec, ctx->clock_24h, false, out->text, sizeof(out->text),
                         &suffix);
        snprintf(out->unit, sizeof(out->unit), "%s", suffix);
        if (ctx->seconds) {
            snprintf(out->extra, sizeof(out->extra), "%02d", tm->tm_sec);
        }
        return;
    }
    if (!ctx->time_valid) {
        return; /* everything else needs a valid date */
    }
    int year = tm->tm_year + 1900, month = tm->tm_mon + 1, day = tm->tm_mday;
    switch (field) {
    case UI_FIELD_DATE_DAY:
        lang_format_date(ctx->lang, tm, LANG_DATE_LONG, out->text, sizeof(out->text));
        lang_format_date(ctx->lang, tm, LANG_DATE_MEDIUM, out->extra, sizeof(out->extra));
        out->state = out->text[0] ? UI_VALUE_FRESH : UI_VALUE_MISSING;
        break;
    case UI_FIELD_DATE_WEEK:
        snprintf(out->text, sizeof(out->text), "%d", util_iso_week(year, month, day));
        out->state = UI_VALUE_FRESH;
        break;
    case UI_FIELD_MOON_PHASE:
        out->moon = util_moon_phase(ctx->now);
        out->percent = out->moon.illumination;
        snprintf(out->text, sizeof(out->text), "%s", ctx->lang->moon_phases[out->moon.index]);
        snprintf(out->short_text, sizeof(out->short_text), "%s", ctx->lang->moon_phases_short[out->moon.index]);
        snprintf(out->extra, sizeof(out->extra), "%d%%", out->moon.illumination);
        out->state = UI_VALUE_FRESH;
        break;
    case UI_FIELD_DATE_NAMEDAY:
    case UI_FIELD_DATE_HOLIDAY: {
        const char *name = NULL;
        if (field == UI_FIELD_DATE_NAMEDAY && ctx->lang->name_day != NULL) {
            name = ctx->lang->name_day(month, day);
        } else if (field == UI_FIELD_DATE_HOLIDAY && ctx->lang->holiday != NULL) {
            name = ctx->lang->holiday(year, month, day);
        }
        if (name != NULL && name[0] != '\0') {
            snprintf(out->text, sizeof(out->text), "%s", name);
            out->state = UI_VALUE_FRESH;
        }
        break;
    }
    default:
        break;
    }
}

void ui_mqtt_value(const ui_context_t *ctx, int i, ui_value_t *out)
{
    const ha_entry_t *e = &ctx->mqtt->entry[i];
    out->kind = e->kind == HA_KIND_TEXT ? UI_FK_TEXT : UI_FK_NUMBER;
    out->label = e->label;
    ha_freshness_t fresh = ha_store_freshness(ctx->mqtt, i, ctx->now);
    if (fresh == HA_MISSING) {
        return;
    }
    out->state = fresh == HA_STALE ? UI_VALUE_STALE : UI_VALUE_FRESH;
    out->age_s = (uint32_t)ctx->now > e->updated ? (uint32_t)ctx->now - e->updated : 0;
    if (e->kind == HA_KIND_TEXT) {
        snprintf(out->text, sizeof(out->text), "%s", e->text);
        return;
    }
    lang_format_decimal(ctx->lang, e->number, e->decimals, out->text, sizeof(out->text));
    if (e->decimals > 0) { /* the whole number, for a slot too narrow for the decimals (spec §5.3) */
        int64_t scale = e->decimals == 1 ? 10 : e->decimals == 2 ? 100 : 1000; /* 64 bits: no overflow */
        int64_t whole = ((int64_t)e->number + (e->number >= 0 ? scale / 2 : -scale / 2)) / scale;
        lang_format_decimal(ctx->lang, (long)whole, 0, out->short_text, sizeof(out->short_text));
    }
    snprintf(out->unit, sizeof(out->unit), "%s", e->unit);
}

/* ha.message (spec §12.7): the latest message until it is replaced or cleared, stale after 24 h. */
static void resolve_message(const ui_context_t *ctx, ui_value_t *out)
{
    ha_freshness_t fresh = ctx->mqtt != NULL ? ha_store_message_freshness(ctx->mqtt, ctx->now) : HA_MISSING;
    if (fresh == HA_MISSING) {
        return;
    }
    out->state = fresh == HA_STALE ? UI_VALUE_STALE : UI_VALUE_FRESH;
    out->age_s = (uint32_t)ctx->now > ctx->mqtt->message_at ? (uint32_t)ctx->now - ctx->mqtt->message_at : 0;
    snprintf(out->text, sizeof(out->text), "%s", ctx->mqtt->message);
}

/* mqtt.<key> (spec §12.5): the mapping that names the key; a key no mapping names, or no store, is no
 * field at all, so its slot stays empty. */
static void resolve_mqtt(const ui_context_t *ctx, int k, ui_value_t *out)
{
    const ui_mqtt_keys_t *keys = ctx->mqtt_keys;
    int i = ctx->mqtt != NULL && keys != NULL && k < keys->count ? ha_store_find(ctx->mqtt, keys->key[k]) : -1;
    if (i < 0) {
        out->field = UI_FIELD_NONE;
        return;
    }
    ui_mqtt_value(ctx, i, out);
}

void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    memset(out, 0, sizeof(*out));
    out->field = field;
    if (ui_field_is_mqtt(field)) {
        resolve_mqtt(ctx, field - UI_FIELD_MQTT, out);
        return;
    }
    const ui_field_info_t *info = ui_field_info(field);
    if (info == NULL) {
        return;
    }
    out->kind = info->kind;
    out->label = lang_str(ctx->lang, info->label);
    if (info->ds_field >= 0) {
        resolve_store(ctx, field, out);
    } else if (field == UI_FIELD_HA_MESSAGE) {
        resolve_message(ctx, out);
    } else if (!ui_resolve_forecast(ctx, field, out) && !ui_resolve_radar(ctx, field, out)) {
        resolve_clock(ctx, field, out);
    }
}
