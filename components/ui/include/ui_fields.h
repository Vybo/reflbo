#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "datastore.h"
#include "gfx.h"
#include "lang.h"
#include "util_calendar.h"

/*
 * The field catalogue (spec §5.1) and what a widget shows for a field right now. Fields from
 * the datastore carry their freshness; the rest follow from the clock and the language pack.
 * Pure C, host-buildable.
 */

typedef enum {
    UI_FK_TIME,
    UI_FK_DATE,
    UI_FK_NUMBER,
    UI_FK_BATTERY,
    UI_FK_MOON,
    UI_FK_TEXT,
    UI_FK_WEATHER_NOW,
    UI_FK_WEATHER_DAY,
    UI_FK_SERIES,
    UI_FK_SUN,
    UI_FK_COUNT,
} ui_field_kind_t;

#define UI_KIND(k) (1u << (k))

typedef enum {
    UI_FIELD_NONE, /* an empty slot */
    UI_FIELD_TIME_CLOCK,
    UI_FIELD_DATE_DAY,
    UI_FIELD_DATE_WEEK,
    UI_FIELD_ENV_TEMP,
    UI_FIELD_ENV_HUM,
    UI_FIELD_ENV_DEW,
    UI_FIELD_ENV_TEMP_MIN,
    UI_FIELD_ENV_TEMP_MAX,
    UI_FIELD_BAT_LEVEL,
    UI_FIELD_BAT_DAYS,
    UI_FIELD_MOON_PHASE,
    UI_FIELD_DATE_NAMEDAY,
    UI_FIELD_DATE_HOLIDAY,
    UI_FIELD_WX_NOW,    /* M5 */
    UI_FIELD_WX_TODAY,  /* M5 */
    UI_FIELD_WX_HOURLY, /* M5 */
    UI_FIELD_WX_DAILY,  /* M5 */
    UI_FIELD_SUN_TIMES, /* M5 */
    UI_FIELD_COUNT,
} ui_field_id_t;

typedef struct {
    const char *id; /* as in presets.json: "env.temp" */
    ui_field_kind_t kind;
    lang_str_t label;
    int ds_field; /* ds_field_t, or -1 for a field computed from the clock */
} ui_field_info_t;

/* NULL for UI_FIELD_NONE and out-of-range values. */
const ui_field_info_t *ui_field_info(ui_field_id_t field);
/* UI_FIELD_NONE for an unknown id. */
ui_field_id_t ui_field_by_name(const char *id);

/* Everything the dashboard reads, gathered by the app for one render. */
typedef struct {
    time_t now;          /* UTC */
    struct tm local;     /* local time at `now` */
    bool time_valid;     /* false: the RTC oscillator stopped and nobody set the time (spec §5.3) */
    int32_t local_day;   /* days since 1970-01-01 in local time */
    const ds_t *ds;
    const lang_t *lang;
    bool clock_24h;
    bool seconds;
    bool fahrenheit;
} ui_context_t;

typedef enum {
    UI_VALUE_MISSING,
    UI_VALUE_FRESH,
    UI_VALUE_STALE,
} ui_value_state_t;

typedef struct {
    ui_field_id_t field;
    ui_field_kind_t kind;
    ui_value_state_t state;
    uint32_t age_s;    /* how old a stale value is */
    const char *label; /* from the language pack */
    char text[48];     /* the value: "23.4", "20:48", "Friday 25 September", a name */
    char unit[8];      /* "°C", "%", "d", or the AM/PM suffix of a time */
    char extra[24];    /* secondary text: the seconds, the medium date, the illumination, the voltage */
    char short_text[16]; /* a shorter form: the short phase name, or a number without its decimals */
    int trend;         /* -1 falling, 0 steady or unknown, 1 rising */
    int percent;       /* battery level or moon illumination */
    ds_bat_state_t battery;
    util_moon_t moon;
} ui_value_t;

void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
