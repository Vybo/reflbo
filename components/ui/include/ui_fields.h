#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "datastore.h"
#include "gfx.h"
#include "ha_store.h"
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
    UI_FK_LEVEL,  /* a number and its band: the air quality index (D25) */
    UI_FK_POLLEN, /* a pollen level, and its type or count (D25) */
    UI_FK_RAIN_MAP, /* the weather radar's map (M6) */
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
    UI_FIELD_AQ_INDEX,  /* M5 (D25) */
    UI_FIELD_AQ_PM25,
    UI_FIELD_AQ_PM10,
    UI_FIELD_AQ_UV, /* D26 */
    UI_FIELD_POLLEN_TOP,
    UI_FIELD_POLLEN_ALDER, /* the types in ds_pollen_t order */
    UI_FIELD_POLLEN_BIRCH,
    UI_FIELD_POLLEN_GRASS,
    UI_FIELD_POLLEN_MUGWORT,
    UI_FIELD_POLLEN_OLIVE,
    UI_FIELD_POLLEN_RAGWEED,
    UI_FIELD_WX_RAIN2H, /* M6 (D27) */
    UI_FIELD_RAIN_MAP,  /* M6 (D23) */
    UI_FIELD_COUNT,     /* the built-in fields; the MQTT fields follow */
} ui_field_id_t;

/* mqtt.<key> fields (spec §12.5, M7): UI_FIELD_MQTT + k is the presets' key k (ui_presets_t.mqtt), which a
 * mapping in the store may name. They have no ui_field_info(). */
#define UI_MQTT_KEYS 32
#define UI_FIELD_MQTT ((int)UI_FIELD_COUNT)

typedef struct {
    uint8_t count;
    char key[UI_MQTT_KEYS][HA_KEY_LEN]; /* in the order presets.json first names them */
} ui_mqtt_keys_t;

static inline bool ui_field_is_mqtt(int field)
{
    return field >= UI_FIELD_MQTT && field < UI_FIELD_MQTT + UI_MQTT_KEYS;
}

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

typedef enum {
    UI_SYNC_IDLE,
    UI_SYNC_RUNNING,
    UI_SYNC_FAILED, /* the last sync failed a step */
} ui_sync_mark_t;

typedef enum {
    UI_WIFI_NONE,      /* not sync mode `always`: nothing to show */
    UI_WIFI_ON,        /* `always`, on the network */
    UI_WIFI_REJOINING, /* `always`, off it */
} ui_wifi_mark_t;

#define UI_SERIES_MAX 6
#define UI_RAIN_STEPS 8 /* wx.rain2h: two hours of quarter hours */

/* One column of a forecast strip. */
typedef struct {
    char label[8]; /* the local hour "14" or the weekday "Thu" */
    char temp[8];  /* "16°"; daily: the high */
    char temp2[8]; /* daily: the low */
    uint8_t sky;   /* weather_sky_t */
    bool night;
} ui_series_point_t;

typedef struct ui_radar ui_radar_t; /* ui_radar.h */

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
    bool web_session; /* config mode with a phone logged in to the web UI: marked in the status bar (D20) */
    int32_t lat_e4, lon_e4; /* the location, for the sun's times */
    ui_sync_mark_t sync;    /* the status bar's sync state (spec §5.2) */
    ui_wifi_mark_t wifi;
    const ui_radar_t *radar; /* M6: the radars' map, frames and settings; NULL for none */
    const ha_store_t *mqtt;  /* M7: the MQTT fields' values and the message; NULL for none */
    const ui_mqtt_keys_t *mqtt_keys; /* the keys of the presets drawn; NULL for none */
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
    char detail[40];   /* a third line: "Feels like 19 °C", the day length */
    int sky;           /* weather_sky_t of a weather value */
    bool night;        /* its icon's night variant */
    int polar;         /* sun.times: 0, or 1 on a polar day and 2 on a polar night */
    int bands;         /* UI_FK_LEVEL: how many bands its scale has; `percent` is the one it is in */
    int series_count;
    ui_series_point_t series[UI_SERIES_MAX];
    uint8_t rain_mm10[UI_RAIN_STEPS]; /* wx.rain2h: each quarter hour from now, as ds_rain_t keeps them */
    uint8_t rain_prob[UI_RAIN_STEPS];
    const ui_radar_t *radar; /* rain.map: what its map draws; `text` is its frame's time */
} ui_value_t;

void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
/* The store's entry `i` as a field shows it: its label, unit and value (the catalogue lists them all). */
void ui_mqtt_value(const ui_context_t *ctx, int i, ui_value_t *out);
