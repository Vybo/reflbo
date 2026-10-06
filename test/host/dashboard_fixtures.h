#pragma once

#include <string.h>

#include "context_fixtures.h"
#include "radar_fixtures.h"
#include "solar_fixtures.h"
#include "ui_dashboard.h"

/* The dashboard fixtures for the golden renders (test_ui_dashboard_golden.c, render_dashboard.c):
 * a context and a preset each, on top of context_fixtures.h. */

/* One reading only, so today's low and high equal it; the battery as in fixture_fill. */
static inline void fixture_single(ds_t *ds, int temp_c100, int hum_pct100)
{
    ds_init(ds);
    ds_set_env(ds, temp_c100, hum_pct100, FIX_NOW, FIX_DAY);
    ds_set_battery(ds, 87, 3921, DS_BAT_DISCHARGING, FIX_NOW);
    ds_set(ds, DS_BAT_DAYS, 85, FIX_NOW);
}

static inline ui_preset_t fixture_preset(const char *id)
{
    ui_presets_t all;
    ui_presets_defaults(&all);
    int i = ui_presets_find(&all, id);
    return all.presets[i < 0 ? 0 : i];
}

/* A preset on the split layout (M6b): its tree in preorder, and its cells' fields in their order. */
static inline void fixture_split(ui_preset_t *p, const uint8_t *tree, size_t nodes, const uint8_t *fields, size_t cells)
{
    p->layout = UI_LAYOUT_SPLIT;
    memset(p->split, 0, sizeof(p->split));
    memcpy(p->split, tree, nodes);
    memset(p->slots, 0, sizeof(p->slots));
    memcpy(p->slots, fields, cells);
}

/* A row of `n` cells (M6c): halves where n is even, a third and the rest where it is odd; `line` is 0 or
 * UI_SPLIT_NO_LINE. Returns the next node. */
static inline int fixture_cols(uint8_t *t, int at, int n, uint8_t line)
{
    if (n == 1) {
        t[at++] = 0;
        return at;
    }
    t[at++] = (uint8_t)((n % 2 == 0 ? UI_RATIO_1_2 : UI_RATIO_1_3) | UI_SPLIT_COLUMNS | line);
    at = fixture_cols(t, at, n % 2 == 0 ? n / 2 : 1, line);
    return fixture_cols(t, at, n % 2 == 0 ? n / 2 : n - 1, line);
}

/* `rows` rows of `cols` cells, split the same way. */
static inline int fixture_rows(uint8_t *t, int at, int rows, int cols, uint8_t line)
{
    if (rows == 1) {
        return fixture_cols(t, at, cols, line);
    }
    t[at++] = (uint8_t)((rows % 2 == 0 ? UI_RATIO_1_2 : UI_RATIO_1_3) | line);
    at = fixture_rows(t, at, rows % 2 == 0 ? rows / 2 : 1, cols, line);
    return fixture_rows(t, at, rows % 2 == 0 ? rows / 2 : rows - 1, cols, line);
}

/* A split preset of rows × cols cells (M6c), the fields in reading order. */
static inline void fixture_grid_split(ui_preset_t *p, int rows, int cols, uint8_t line, const uint8_t *fields,
                                      size_t cells)
{
    uint8_t tree[UI_SPLIT_NODES];
    memset(tree, 0, sizeof(tree));
    int n = fixture_rows(tree, 0, rows, cols, line);
    fixture_split(p, tree, (size_t)n, fields, cells);
}

/* The 24 small fields of M6c's goldens, in the order the cells take them. */
static const uint8_t k_fixture_small24[24] = {
    UI_FIELD_TIME_CLOCK,    UI_FIELD_DATE_DAY,     UI_FIELD_ENV_TEMP,      UI_FIELD_ENV_HUM,    UI_FIELD_WX_NOW,
    UI_FIELD_WX_TODAY,      UI_FIELD_SUN_TIMES,    UI_FIELD_AQ_INDEX,      UI_FIELD_AQ_UV,      UI_FIELD_POLLEN_TOP,
    UI_FIELD_MOON_PHASE,    UI_FIELD_BAT_LEVEL,    UI_FIELD_DATE_WEEK,     UI_FIELD_BAT_DAYS,   UI_FIELD_ENV_DEW,
    UI_FIELD_AQ_PM25,       UI_FIELD_AQ_PM10,      UI_FIELD_ENV_TEMP_MIN,  UI_FIELD_ENV_TEMP_MAX, UI_FIELD_POLLEN_GRASS,
    UI_FIELD_POLLEN_BIRCH,  UI_FIELD_POLLEN_ALDER, UI_FIELD_POLLEN_MUGWORT, UI_FIELD_POLLEN_RAGWEED,
};

/* M6d: 13:20 CEST on the fixture's day, the sun out, the sample day of ui_solar_demo(). */
static inline void fixture_solar_ctx(ui_context_t *ctx, bool live, bool battery)
{
    ctx->now = FIX_SOLAR_NOW;
    ctx->local = fixture_local(13, 20, 0);
    fixture_fill(&s_fix_ds, ctx->now);
    fixture_forecast(&s_fix_ds, ctx->now - 3600);
    static ds_weather_t w;
    w = *ds_weather(&s_fix_ds);
    w.now_is_day = 1;
    w.now_code = 1;
    w.now_time = (uint32_t)(ctx->now - 20 * 60);
    w.now_temp_c10 = 196;
    ds_set_weather(&s_fix_ds, &w);
    ctx->solar = fixture_solar(ctx->now, live, battery);
}

/* Each fixture: a context and a preset. */
static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_preset_t *preset)
{
    *ctx = fixture_context();
    if (strcmp(name, "home") == 0 || strcmp(name, "indoor") == 0 || strcmp(name, "weather") == 0 ||
        strcmp(name, "focus") == 0) {
        *preset = fixture_preset(name);
    } else if (strcmp(name, "home_invalid") == 0) { /* RTC stopped, no readings yet (spec §5.3) */
        *preset = fixture_preset("home");
        ctx->time_valid = false;
        ds_init(&s_fix_ds);
    } else if (strcmp(name, "home_stale") == 0) { /* readings 3 h old */
        *preset = fixture_preset("home");
        fixture_fill(&s_fix_ds, FIX_NOW - 3 * 3600);
    } else if (strcmp(name, "home_12h_charging") == 0) {
        *preset = fixture_preset("home");
        ctx->clock_24h = false;
        ds_set_battery(&s_fix_ds, 64, 4012, DS_BAT_CHARGING, FIX_NOW);
    } else if (strcmp(name, "indoor_cold") == 0) { /* below zero, °F, placeholder policy for stale */
        *preset = fixture_preset("indoor");
        ds_init(&s_fix_ds);
        ds_set_env(&s_fix_ds, -50, 8000, FIX_NOW, FIX_DAY);
        ctx->fahrenheit = true;
        preset->stale_policy = UI_STALE_PLACEHOLDER;
    } else if (strcmp(name, "focus_seconds") == 0) {
        *preset = fixture_preset("focus");
        preset->seconds = true;
        ctx->local = fixture_local(20, 48, 37);
    } else if (strcmp(name, "home_battery_details") == 0) { /* level, voltage and days in the status bar */
        *preset = fixture_preset("home");
        preset->status_clock = true;
        preset->status_battery = UI_STATUS_BAT_PERCENT | UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
    } else if (strcmp(name, "home_inverted") == 0) {
        *preset = fixture_preset("home");
        preset->invert = true;
    } else if (strcmp(name, "indoor_hot_f") == 0) { /* 38.2 °C in °F: three digits */
        *preset = fixture_preset("indoor");
        fixture_single(&s_fix_ds, 3820, 3000);
        ctx->fahrenheit = true;
    } else if (strcmp(name, "indoor_frost") == 0) { /* -12.5 °C; the dew point is -18.7 °C */
        *preset = fixture_preset("indoor");
        fixture_single(&s_fix_ds, -1250, 6000);
    } else if (strcmp(name, "home_frost") == 0) {
        *preset = fixture_preset("home");
        fixture_single(&s_fix_ds, -1250, 6000);
    } else if (strcmp(name, "home_cs") == 0) { /* the Czech pack */
        *preset = fixture_preset("home");
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "indoor_cs") == 0) {
        *preset = fixture_preset("indoor");
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "home_holiday_cs") == 0) { /* Monday 28 September 2026: Den české státnosti */
        *preset = fixture_preset("home");
        preset->slots[4] = UI_FIELD_DATE_HOLIDAY;
        ctx->lang = lang_get("cs");
        ctx->now = FIX_NOW + 3 * 86400;
        ctx->local.tm_mday = 28;
        ctx->local.tm_wday = 1;
        ctx->local.tm_yday = 270;
        ctx->local_day = FIX_DAY + 3;
        fixture_fill(&s_fix_ds, ctx->now);
    } else if (strcmp(name, "home_low_battery") == 0) { /* 12 %: the status bar marks it */
        *preset = fixture_preset("home");
        ds_set_battery(&s_fix_ds, 12, 3650, DS_BAT_DISCHARGING, FIX_NOW);
    } else if (strcmp(name, "home_web") == 0) { /* config mode with a phone logged in (D20) */
        *preset = fixture_preset("home");
        ctx->web_session = true;
    } else if (strcmp(name, "home_stale_web") == 0) { /* the web mark goes after the stale one */
        *preset = fixture_preset("home");
        fixture_fill(&s_fix_ds, FIX_NOW - 3 * 3600);
        ctx->web_session = true;
    } else if (strcmp(name, "weather_now") == 0) { /* M5: the Weather preset after a sync */
        *preset = fixture_preset("weather");
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "weather_noon_cs") == 0) { /* by day, in Czech, the current block an hour old */
        *preset = fixture_preset("weather");
        ctx->lang = lang_get("cs");
        ctx->now = FIX_NOW - 8 * 3600 - 48 * 60; /* 12:00 */
        ctx->local = fixture_local(12, 0, 0);
        fixture_fill(&s_fix_ds, ctx->now);
        fixture_forecast(&s_fix_ds, FIX_NOW - 9 * 3600);
    } else if (strcmp(name, "weather_stale") == 0) { /* a daily sync missed: two days old */
        *preset = fixture_preset("weather");
        fixture_forecast(&s_fix_ds, FIX_NOW - 50 * 3600);
    } else if (strcmp(name, "air_grid") == 0) { /* the air quality and pollen fields in a grid (D25) */
        *preset = fixture_preset("indoor");
        static const uint8_t k_slots[6] = { UI_FIELD_AQ_INDEX, UI_FIELD_AQ_PM25, UI_FIELD_POLLEN_TOP,
                                            UI_FIELD_POLLEN_GRASS, UI_FIELD_SUN_TIMES, UI_FIELD_WX_DAILY };
        memcpy(preset->slots, k_slots, sizeof(k_slots));
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "air_grid_cs") == 0) { /* the same in Czech: the bands and levels' words */
        fixture_dashboard("air_grid", ctx, preset);
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "grid_sun_uv") == 0) { /* D26: the day's change and the UV index, at noon */
        *preset = fixture_preset("indoor");
        static const uint8_t k_slots[6] = { UI_FIELD_WX_NOW, UI_FIELD_SUN_TIMES, UI_FIELD_AQ_UV,
                                            UI_FIELD_WX_TODAY, UI_FIELD_AQ_PM10, UI_FIELD_POLLEN_BIRCH };
        memcpy(preset->slots, k_slots, sizeof(k_slots));
        ctx->now = FIX_NOW - 7 * 3600 - 48 * 60; /* 13:00 */
        ctx->local = fixture_local(13, 0, 0);
        fixture_fill(&s_fix_ds, ctx->now);
        fixture_forecast(&s_fix_ds, ctx->now - 3600);
    } else if (strcmp(name, "home_forecast") == 0) { /* small slots: the weather, the sun, air, pollen */
        *preset = fixture_preset("home");
        preset->slots[2] = UI_FIELD_WX_NOW;
        preset->slots[3] = UI_FIELD_SUN_TIMES;
        preset->slots[4] = UI_FIELD_AQ_INDEX;
        preset->slots[5] = UI_FIELD_POLLEN_TOP;
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "focus_forecast") == 0) { /* medium slots: today and the next hours */
        *preset = fixture_preset("focus");
        preset->slots[1] = UI_FIELD_WX_TODAY;
        preset->slots[2] = UI_FIELD_WX_HOURLY;
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "home_syncing") == 0) { /* the status bar's sync marks (spec §5.2) */
        *preset = fixture_preset("home");
        ctx->sync = UI_SYNC_RUNNING;
    } else if (strcmp(name, "home_sync_failed") == 0) {
        *preset = fixture_preset("home");
        ctx->sync = UI_SYNC_FAILED;
    } else if (strcmp(name, "home_always") == 0) { /* sync mode `always`, on the network */
        *preset = fixture_preset("home");
        ctx->wifi = UI_WIFI_ON;
    } else if (strcmp(name, "home_always_rejoining") == 0) {
        *preset = fixture_preset("home");
        ctx->wifi = UI_WIFI_REJOINING;
        ctx->sync = UI_SYNC_FAILED;
        ctx->web_session = true;
    } else if (strcmp(name, "weather_rain") == 0) { /* M6 (D27): rain in the next 2 h for the hours */
        *preset = fixture_preset("weather");
        preset->slots[2] = UI_FIELD_WX_RAIN2H;
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "focus_rain_now") == 0) { /* raining now; one unlikely quarter hour */
        *preset = fixture_preset("focus");
        preset->slots[1] = UI_FIELD_WX_RAIN2H;
        preset->slots[2] = UI_FIELD_WX_HOURLY;
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
        fixture_rain_now(&s_fix_ds);
    } else if (strcmp(name, "grid_rain_cs") == 0) { /* narrow cells, in Czech: raining now, and dry */
        *preset = fixture_preset("indoor");
        preset->slots[0] = UI_FIELD_WX_RAIN2H;
        preset->slots[1] = UI_FIELD_WX_NOW;
        ctx->lang = lang_get("cs");
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
        fixture_rain_now(&s_fix_ds);
    } else if (strcmp(name, "radar") == 0) { /* M6: the Rain radar preset, ČHMÚ's frame 8 min old */
        *preset = fixture_preset("rain");
        ctx->radar = fixture_radar(fixture_chmu(FIX_NOW - 8 * 60));
    } else if (strcmp(name, "radar_stale") == 0) { /* 3 h old: the time inverted, with its age */
        *preset = fixture_preset("rain");
        ctx->radar = fixture_radar(fixture_chmu(FIX_NOW - 3 * 3600 - 8 * 60));
    } else if (strcmp(name, "radar_stale_cs") == 0) {
        fixture_dashboard("radar_stale", ctx, preset);
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "radar_loop") == 0) { /* the loop's seventh frame of twelve (D28) */
        *preset = fixture_preset("rain");
        ui_radar_t *radar = fixture_radar(fixture_chmu(FIX_NOW - 33 * 60));
        radar->loop_at = 6;
        radar->loop_count = 12;
        ctx->radar = radar;
    } else if (strcmp(name, "radar_none") == 0) { /* before the first frame */
        *preset = fixture_preset("rain");
        ctx->radar = fixture_radar(NULL);
    } else if (strcmp(name, "radar_rainviewer") == 0) { /* outside ČHMÚ: Berlin from RainViewer at zoom 5 */
        *preset = fixture_preset("rain");
        ui_radar_t *radar = fixture_radar(fixture_rainviewer(FIX_NOW - 14 * 60));
        radar->wx_lat_e4 = 525200;
        radar->wx_lon_e4 = 134050;
        radar->wx_zoom_q = 20;
        ctx->radar = radar;
    } else if (strcmp(name, "grid_rain_map") == 0) { /* the rain map in grid cells */
        *preset = fixture_preset("indoor");
        preset->slots[0] = UI_FIELD_RAIN_MAP;
        preset->slots[4] = UI_FIELD_RAIN_MAP;
        ui_radar_t *radar = fixture_radar(fixture_chmu(FIX_NOW - 3 * 3600 - 8 * 60));
        radar->wx_lat_e4 = 506000; /* the rain north of Praha */
        radar->wx_lon_e4 = 139000;
        ctx->radar = radar;
    } else if (strcmp(name, "weather_rain_map") == 0) { /* in the Weather layout's large slot */
        *preset = fixture_preset("weather");
        preset->slots[0] = UI_FIELD_RAIN_MAP;
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
        ctx->radar = fixture_radar(fixture_chmu(FIX_NOW - 8 * 60));
    } else if (strcmp(name, "flights") == 0) { /* M6: the Flights preset at 50 km, the nearest's route known */
        *preset = fixture_preset("flights");
        ctx->radar = fixture_flights(50, fixture_aircraft(50), fixture_route(), ctx->now);
    } else if (strcmp(name, "flights_100") == 0) { /* 100 km: more aircraft, labels that must give way */
        *preset = fixture_preset("flights");
        ctx->radar = fixture_flights(100, fixture_aircraft(100), NULL, ctx->now);
    } else if (strcmp(name, "flights_cs") == 0) {
        fixture_dashboard("flights", ctx, preset);
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "flights_none") == 0) { /* a poll with nobody in the sky */
        *preset = fixture_preset("flights");
        static const adsb_list_t k_empty;
        ctx->radar = fixture_flights(50, &k_empty, NULL, ctx->now);
    } else if (strcmp(name, "flights_failed") == 0) { /* the last good poll at 20:40 */
        *preset = fixture_preset("flights");
        ui_radar_t *radar = fixture_flights(50, fixture_aircraft(50), NULL, ctx->now);
        radar->fl_updated = ctx->now - 8 * 60;
        radar->fl_failed = true;
        ctx->radar = radar;
    } else if (strcmp(name, "flights_off") == 0) { /* not sync mode `always` (spec §5.4) */
        *preset = fixture_preset("flights");
        ui_radar_t *radar = fixture_flights(50, NULL, NULL, ctx->now);
        radar->fl_always = false;
        radar->fl_updated = 0;
        ctx->radar = radar;
    } else if (strcmp(name, "split_weather") == 0) { /* M6b: spec §5.2's example, its bottom line hidden */
        *preset = fixture_preset("weather");
        static const uint8_t k_tree[] = { UI_RATIO_3_4, UI_RATIO_1_2 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_2, 0, 0,
                                          UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0 };
        static const uint8_t k_fields[] = { UI_FIELD_WX_NOW, UI_FIELD_WX_TODAY, UI_FIELD_WX_HOURLY, UI_FIELD_ENV_TEMP,
                                            UI_FIELD_ENV_HUM };
        fixture_split(preset, k_tree, sizeof(k_tree), k_fields, sizeof(k_fields));
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "split_eight") == 0) { /* eight cells: each ratio, both ways, a hidden line */
        *preset = fixture_preset("home");
        static const uint8_t k_tree[] = { UI_RATIO_1_3, UI_RATIO_3_4 | UI_SPLIT_COLUMNS, 0, 0,
                                          UI_RATIO_1_4 | UI_SPLIT_COLUMNS, UI_RATIO_1_2 | UI_SPLIT_NO_LINE, 0, 0,
                                          UI_RATIO_2_3 | UI_SPLIT_COLUMNS, UI_RATIO_1_2, 0, 0, UI_RATIO_1_2, 0, 0 };
        static const uint8_t k_fields[] = { UI_FIELD_TIME_CLOCK, UI_FIELD_DATE_DAY, UI_FIELD_ENV_TEMP,
                                            UI_FIELD_ENV_HUM,    UI_FIELD_WX_NOW,   UI_FIELD_WX_HOURLY,
                                            UI_FIELD_MOON_PHASE, UI_FIELD_BAT_LEVEL };
        fixture_split(preset, k_tree, sizeof(k_tree), k_fields, sizeof(k_fields));
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "weather_frost_cs") == 0) { /* a frosty morning in Czech: -13 °C, the hours to -18 */
        *preset = fixture_preset("weather");
        ctx->lang = lang_get("cs");
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
        fixture_forecast_shift(&s_fix_ds, -125);
    } else if (strcmp(name, "weather_hot_f") == 0) { /* a hot day in °F: 102 °F now, the hours to 109 °F */
        *preset = fixture_preset("weather");
        ctx->fahrenheit = true;
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
        fixture_forecast_shift(&s_fix_ds, 388);
    } else if (strcmp(name, "home_temp_main") == 0) { /* a number in Classic's main slot: 110 px digits fit */
        *preset = fixture_preset("home");
        preset->slots[0] = UI_FIELD_ENV_TEMP;
    } else if (strcmp(name, "home_temp_main_cs") == 0) { /* in Czech the decimals give way to the comma's tail */
        fixture_dashboard("home_temp_main", ctx, preset);
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "split_compact") == 0) { /* M6c (D34): 4 × 3 cells of 133 × 69, S beside */
        *preset = fixture_preset("weather");
        static const uint8_t k_fields[12] = { UI_FIELD_TIME_CLOCK, UI_FIELD_DATE_DAY,   UI_FIELD_WX_NOW,
                                              UI_FIELD_ENV_TEMP,   UI_FIELD_ENV_HUM,    UI_FIELD_WX_TODAY,
                                              UI_FIELD_SUN_TIMES,  UI_FIELD_AQ_INDEX,   UI_FIELD_POLLEN_TOP,
                                              UI_FIELD_AQ_UV,      UI_FIELD_BAT_LEVEL,  UI_FIELD_MOON_PHASE };
        fixture_grid_split(preset, 4, 3, 0, k_fields, sizeof(k_fields));
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "split_compact_cs") == 0) { /* the same in Czech: a word under its number */
        fixture_dashboard("split_compact", ctx, preset);
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "split_xs_rows") == 0) { /* 12 × 2 rows of 200 × 22: the status bar's size */
        *preset = fixture_preset("weather");
        fixture_grid_split(preset, 12, 2, UI_SPLIT_NO_LINE, k_fixture_small24, sizeof(k_fixture_small24));
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "split_xs_grid") == 0) { /* 4 × 6 cells of 66 × 69: XS, the symbol over the value */
        *preset = fixture_preset("weather");
        fixture_grid_split(preset, 4, 6, 0, k_fixture_small24, sizeof(k_fixture_small24));
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "split_xs_narrow") == 0) { /* 3 × 8 cells of 49 × 92: units under the numbers */
        *preset = fixture_preset("weather");
        fixture_grid_split(preset, 3, 8, 0, k_fixture_small24, sizeof(k_fixture_small24));
        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
    } else if (strcmp(name, "mqtt_grid") == 0) { /* M7: MQTT fields, fresh, a text, stale, missing, unmapped */
        *preset = fixture_preset("indoor");
        for (int k = 0; k < 6; k++) {
            preset->slots[k] = (uint8_t)FIX_MQTT(k);
        }
        fixture_mqtt(ctx);
    } else if (strcmp(name, "mqtt_home_cs") == 0) { /* small slots: each label where an icon would be */
        *preset = fixture_preset("home");
        for (int k = 0; k < 4; k++) {
            preset->slots[2 + k] = (uint8_t)FIX_MQTT(k);
        }
        fixture_mqtt(ctx);
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "mqtt_split_xs") == 0) { /* XS: labels before the values, then over them (D40) */
        static const uint8_t k_fields[] = {
            FIX_MQTT(0), FIX_MQTT(1), FIX_MQTT(2),        FIX_MQTT(3),      FIX_MQTT(4),     FIX_MQTT(6),
            FIX_MQTT(5), UI_FIELD_ENV_TEMP, /* 4 × 2 rows of 200 × 34, then 2 × 6 cells of 66 × 69 */
            FIX_MQTT(0), FIX_MQTT(1), FIX_MQTT(2),        FIX_MQTT(3),      FIX_MQTT(4),     FIX_MQTT(6),
            UI_FIELD_TIME_CLOCK, UI_FIELD_ENV_TEMP, UI_FIELD_ENV_HUM, UI_FIELD_DATE_DAY, UI_FIELD_BAT_LEVEL,
            UI_FIELD_MOON_PHASE,
        };
        *preset = fixture_preset("weather");
        uint8_t tree[UI_SPLIT_NODES];
        memset(tree, 0, sizeof(tree));
        tree[0] = UI_RATIO_1_2;
        int n = fixture_rows(tree, 1, 4, 2, 0);
        n = fixture_rows(tree, n, 2, 6, 0);
        fixture_split(preset, tree, (size_t)n, k_fields, sizeof(k_fields));
        fixture_mqtt(ctx);
    } else if (strcmp(name, "home_mqtt_failed") == 0) { /* M7: the last MQTT session failed (spec §5.2) */
        *preset = fixture_preset("home");
        ctx->mqtt_failed = true;
        ctx->wifi = UI_WIFI_ON;
    } else if (strcmp(name, "message_fields") == 0) { /* ha.message, wrapped, in an L and an M slot */
        *preset = fixture_preset("weather");
        preset->slots[0] = UI_FIELD_HA_MESSAGE;
        preset->slots[1] = UI_FIELD_HA_MESSAGE;
        fixture_mqtt(ctx);
        ha_store_set_message(&s_fix_mqtt, FIX_MESSAGE_EN, FIX_NOW - 300);
    } else if (strcmp(name, "grid_clock_12h") == 0) { /* a clock in a grid cell, 12-hour */
        *preset = fixture_preset("indoor");
        preset->slots[0] = UI_FIELD_TIME_CLOCK;
        ctx->clock_24h = false;
        ctx->local = fixture_local(12, 58, 0);
    } else if (strcmp(name, "solar") == 0) { /* M6d: the forecast alone, at 13:20 */
        *preset = fixture_preset("solar");
        fixture_solar_ctx(ctx, false, false);
    } else if (strcmp(name, "solar_actual") == 0) { /* with the inverter's readings: what came, against the line */
        *preset = fixture_preset("solar");
        fixture_solar_ctx(ctx, true, false);
    } else if (strcmp(name, "solar_cs") == 0) {
        fixture_dashboard("solar_actual", ctx, preset);
        ctx->lang = lang_get("cs");
    } else if (strcmp(name, "solar_none") == 0) { /* before the first forecast */
        *preset = fixture_preset("solar");
        fixture_solar_ctx(ctx, false, false);
        ctx->solar = fixture_solar_none();
    } else if (strcmp(name, "energy") == 0) { /* the house now, without a battery: exporting */
        *preset = fixture_preset("energy");
        fixture_solar_ctx(ctx, true, false);
    } else if (strcmp(name, "energy_battery") == 0) { /* with a battery, charging */
        *preset = fixture_preset("energy");
        fixture_solar_ctx(ctx, true, true);
    } else if (strcmp(name, "energy_night_cs") == 0) { /* 22:08 in Czech: nothing from the roof, importing */
        *preset = fixture_preset("energy");
        ctx->lang = lang_get("cs");
        fixture_solar_ctx(ctx, true, false);
        ctx->now = FIX_NOW + 80 * 60;
        ctx->local = fixture_local(22, 8, 0);
        fixture_fill(&s_fix_ds, ctx->now);
        s_fix_reading.at = (uint32_t)(ctx->now - 3 * 60);
        s_fix_reading.pv_w = 0;
        s_fix_reading.load_w = 430;
        s_fix_reading.grid_w = 430;
    } else if (strcmp(name, "energy_low") == 0) { /* under 1 kW in watts: the owner's evening (2026-10-06) */
        *preset = fixture_preset("energy");
        fixture_solar_ctx(ctx, true, false);
        s_fix_reading.pv_w = 382;
        s_fix_reading.load_w = 382;
        s_fix_reading.grid_w = 0;
    } else if (strcmp(name, "energy_mqtt") == 0) { /* D40: the reading from MQTT, with no counter of imports mapped */
        *preset = fixture_preset("energy");
        fixture_solar_ctx(ctx, true, false);
        s_fix_solar.mqtt = true;
        s_fix_reading.from_grid_wh = ENERGY_WH_NONE;
    } else if (strcmp(name, "energy_none") == 0) { /* before the first reading */
        *preset = fixture_preset("energy");
        fixture_solar_ctx(ctx, false, false);
    } else if (strcmp(name, "home_energy") == 0) { /* M6d: Classic's small slots, the house now */
        *preset = fixture_preset("home");
        preset->slots[2] = UI_FIELD_EN_PV;
        preset->slots[3] = UI_FIELD_EN_GRID;
        preset->slots[4] = UI_FIELD_EN_LOAD;
        preset->slots[5] = UI_FIELD_PV_TODAY;
        fixture_solar_ctx(ctx, true, false);
    } else if (strcmp(name, "grid_solar") == 0) { /* Grid's medium cells: the chart, the flow, the numbers */
        *preset = fixture_preset("indoor");
        static const uint8_t k_slots[6] = { UI_FIELD_PV_CHART, UI_FIELD_EN_FLOW, UI_FIELD_PV_LEFT,
                                            UI_FIELD_EN_GRID,  UI_FIELD_EN_SELF, UI_FIELD_PV_TOMORROW };
        memcpy(preset->slots, k_slots, sizeof(k_slots));
        fixture_solar_ctx(ctx, true, false);
    } else if (strcmp(name, "grid_solar_low") == 0) { /* every power under 1 kW: the flow in W */
        fixture_dashboard("grid_solar", ctx, preset);
        preset->slots[2] = UI_FIELD_EN_LOAD;
        preset->slots[5] = UI_FIELD_PV_NOW;
        s_fix_reading.pv_w = 382;
        s_fix_reading.load_w = 382;
        s_fix_reading.grid_w = 0;
        s_fix_forecast.q[0][53] = 39; /* 390 W */
    } else if (strcmp(name, "solar_evening") == 0) { /* 18:30: the stat rows in W */
        *preset = fixture_preset("solar");
        fixture_solar_ctx(ctx, true, false);
        ctx->now = FIX_SOLAR_NOW + 5 * 3600 + 10 * 60;
        ctx->local = fixture_local(18, 30, 0);
        fixture_fill(&s_fix_ds, ctx->now);
        fixture_forecast(&s_fix_ds, ctx->now - 3600);
        ctx->solar = fixture_solar(ctx->now, true, false);
    } else if (strcmp(name, "weather_solar") == 0) { /* the chart in Weather's large slot, the flow beside */
        *preset = fixture_preset("weather");
        static const uint8_t k_slots[5] = { UI_FIELD_PV_CHART, UI_FIELD_WX_TODAY, UI_FIELD_EN_FLOW, UI_FIELD_EN_PV,
                                            UI_FIELD_EN_BATTERY };
        memcpy(preset->slots, k_slots, sizeof(k_slots));
        fixture_solar_ctx(ctx, true, true);
    } else if (strcmp(name, "focus_solar") == 0) { /* the clock, with the chart and the flow under it */
        *preset = fixture_preset("focus");
        preset->slots[1] = UI_FIELD_PV_CHART;
        preset->slots[2] = UI_FIELD_EN_FLOW;
        fixture_solar_ctx(ctx, true, false);
    } else {
        return false;
    }
    return true;
}

static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather", "focus", "home_invalid",
                                                    "home_stale", "home_12h_charging", "indoor_cold",
                                                    "focus_seconds", "home_battery_details", "home_inverted",
                                                    "indoor_hot_f", "indoor_frost", "home_frost", "grid_clock_12h",
                                                    "home_cs", "indoor_cs", "home_holiday_cs", "home_low_battery",
                                                    "home_web", "home_stale_web", "weather_now",
                                                    "weather_noon_cs", "weather_stale", "air_grid", "air_grid_cs",
                                                    "grid_sun_uv", "home_forecast",
                                                    "focus_forecast", "home_syncing", "home_sync_failed",
                                                    "home_always", "home_always_rejoining", "weather_rain",
                                                    "focus_rain_now", "grid_rain_cs", "radar", "radar_stale",
                                                    "radar_stale_cs", "radar_loop", "radar_none",
                                                    "radar_rainviewer", "grid_rain_map", "weather_rain_map",
                                                    "flights", "flights_100", "flights_cs", "flights_none",
                                                    "flights_failed", "flights_off", "split_weather",
                                                    "split_eight", "home_temp_main", "home_temp_main_cs",
                                                    "weather_frost_cs", "weather_hot_f", "split_compact",
                                                    "split_compact_cs", "split_xs_rows", "split_xs_grid",
                                                    "split_xs_narrow", "home_energy", "grid_solar",
                                                    "weather_solar", "focus_solar", "solar", "solar_actual",
                                                    "solar_cs", "solar_none", "energy", "energy_battery",
                                                    "energy_night_cs", "energy_none", "energy_low",
                                                    "grid_solar_low", "solar_evening", "mqtt_grid",
                                                    "mqtt_home_cs", "mqtt_split_xs", "home_mqtt_failed",
                                                    "message_fields", "energy_mqtt" };
