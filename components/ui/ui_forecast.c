#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "astro.h"
#include "gfx_fonts.h"
#include "gfx_icons.h"
#include "ui_internal.h"
#include "util_time.h"
#include "weather.h"

/* The weather, air quality, pollen and sun fields (spec §5.1, §11, D25): what they show, and their
 * widgets. */

#define PLACEHOLDER "\xE2\x80\x94"
#define DEGREE "\xC2\xB0"

/* ---- the sun ---- */

/* The zone's UTC offset at local noon of a date, from the TZ the app set. */
static int32_t noon_offset(int year, int month, int day)
{
    struct tm noon = { .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = 12, .tm_isdst = -1 };
    time_t t = mktime(&noon);
    int64_t as_utc = util_days_from_civil(year, month, day) * 86400 + 12 * 3600;
    return (int32_t)(as_utc - (int64_t)t);
}

/* The sun on a local day; a few days are kept, as the S3 computes in software doubles. */
static astro_sun_t sun_on(const ui_context_t *ctx, int32_t local_day)
{
    static struct {
        int32_t day, lat, lon;
        astro_sun_t sun;
        bool used;
    } cache[4];
    static int next;
    for (int i = 0; i < 4; i++) {
        if (cache[i].used && cache[i].day == local_day && cache[i].lat == ctx->lat_e4 && cache[i].lon == ctx->lon_e4) {
            return cache[i].sun;
        }
    }
    int y, m, d;
    util_civil_from_days(local_day, &y, &m, &d);
    astro_sun_t sun;
    astro_sun(y, m, d, noon_offset(y, m, d), ctx->lat_e4, ctx->lon_e4, &sun);
    cache[next].day = local_day;
    cache[next].lat = ctx->lat_e4;
    cache[next].lon = ctx->lon_e4;
    cache[next].sun = sun;
    cache[next].used = true;
    next = (next + 1) % 4;
    return sun;
}

static int32_t local_day_of(time_t t)
{
    struct tm local;
    localtime_r(&t, &local);
    return (int32_t)util_days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

/* Night at `t` by the sun at the location: before sunrise or after sunset of that local day. */
static bool night_at(const ui_context_t *ctx, time_t t)
{
    astro_sun_t sun = sun_on(ctx, local_day_of(t));
    if (sun.kind != ASTRO_NORMAL) {
        return sun.kind == ASTRO_POLAR_NIGHT;
    }
    return t < sun.sunrise || t >= sun.sunset;
}

void ui_clock_text(const ui_context_t *ctx, time_t t, char *out, size_t size)
{
    struct tm local;
    localtime_r(&t, &local);
    const char *suffix;
    char hm[12];
    lang_format_time(local.tm_hour, local.tm_min, 0, ctx->clock_24h, false, hm, sizeof(hm), &suffix);
    snprintf(out, size, "%s%s%s", hm, suffix[0] ? " " : "", suffix);
}

static void resolve_sun(const ui_context_t *ctx, ui_value_t *out)
{
    if (!ctx->time_valid) {
        return;
    }
    astro_sun_t sun = sun_on(ctx, ctx->local_day);
    out->state = UI_VALUE_FRESH;
    if (sun.kind != ASTRO_NORMAL) {
        out->polar = sun.kind == ASTRO_POLAR_DAY ? 1 : 2;
        snprintf(out->text, sizeof(out->text), "%s", lang_str(ctx->lang, out->polar == 1 ? LS_POLAR_DAY : LS_POLAR_NIGHT));
        return;
    }
    ui_clock_text(ctx, (time_t)sun.sunrise, out->text, sizeof(out->text));
    ui_clock_text(ctx, (time_t)sun.sunset, out->extra, sizeof(out->extra));
    int minutes = (sun.day_length_s + 30) / 60;
    const char *h = lang_str(ctx->lang, LS_HOURS_UNIT), *min = lang_str(ctx->lang, LS_MINUTES_UNIT);
    astro_sun_t before = sun_on(ctx, ctx->local_day - 1); /* D26: the change since yesterday */
    if (before.kind == ASTRO_NORMAL) {
        int change = sun.day_length_s - before.day_length_s;
        int change_min = (change + (change >= 0 ? 30 : -30)) / 60;
        snprintf(out->detail, sizeof(out->detail), "%d %s %d %s, %s%d %s", minutes / 60, h, minutes % 60, min,
                 change_min > 0 ? "+" : "", change_min, min);
    } else {
        snprintf(out->detail, sizeof(out->detail), "%d %s %d %s", minutes / 60, h, minutes % 60, min);
    }
}

/* ---- the weather ---- */

/* 0.1 °C as whole degrees in the display unit, "23" or "-4". */
static void degrees(const ui_context_t *ctx, int16_t c10, char *out, size_t size)
{
    long v = ctx->fahrenheit ? (long)c10 * 9 / 5 + 320 : c10;
    snprintf(out, size, "%ld", (v + (v >= 0 ? 5 : -5)) / 10);
}

static bool forecast_state(ds_freshness_t f, uint32_t fetched, const ui_context_t *ctx, ui_value_t *out)
{
    if (f == DS_MISSING) {
        return false;
    }
    out->state = f == DS_STALE ? UI_VALUE_STALE : UI_VALUE_FRESH;
    out->age_s = (uint32_t)ctx->now > fetched ? (uint32_t)ctx->now - fetched : 0;
    return true;
}

static const lang_str_t k_sky_words[] = {
    [WEATHER_SKY_CLEAR] = LS_WX_CLEAR,
    [WEATHER_SKY_MAINLY_CLEAR] = LS_WX_MAINLY_CLEAR,
    [WEATHER_SKY_PARTLY_CLOUDY] = LS_WX_PARTLY_CLOUDY,
    [WEATHER_SKY_OVERCAST] = LS_WX_OVERCAST,
    [WEATHER_SKY_FOG] = LS_WX_FOG,
    [WEATHER_SKY_DRIZZLE] = LS_WX_DRIZZLE,
    [WEATHER_SKY_RAIN] = LS_WX_RAIN,
    [WEATHER_SKY_FREEZING_RAIN] = LS_WX_FREEZING_RAIN,
    [WEATHER_SKY_SNOW] = LS_WX_SNOW,
    [WEATHER_SKY_SHOWERS] = LS_WX_SHOWERS,
    [WEATHER_SKY_SNOW_SHOWERS] = LS_WX_SNOW_SHOWERS,
    [WEATHER_SKY_THUNDERSTORM] = LS_WX_THUNDERSTORM,
    [WEATHER_SKY_UNKNOWN] = LS_WX_UNKNOWN,
};

static void resolve_now(const ui_context_t *ctx, ui_value_t *out)
{
    ds_wx_now_t n;
    const ds_weather_t *w = ds_weather(ctx->ds);
    if (w == NULL || !forecast_state(ds_weather_freshness(ctx->ds, ctx->now), w->fetched, ctx, out) ||
        !ds_weather_now(ctx->ds, ctx->now, &n)) {
        out->state = UI_VALUE_MISSING;
        return;
    }
    degrees(ctx, n.temp_c10, out->text, sizeof(out->text));
    snprintf(out->unit, sizeof(out->unit), "%s", ctx->fahrenheit ? DEGREE "F" : DEGREE "C");
    out->sky = weather_sky(n.code);
    out->night = n.is_day >= 0 ? n.is_day == 0 : night_at(ctx, ctx->now);
    snprintf(out->extra, sizeof(out->extra), "%s", lang_str(ctx->lang, k_sky_words[out->sky]));
    if (n.feels_c10 != DS_WX_NO_TEMP) {
        char feels[8];
        degrees(ctx, n.feels_c10, feels, sizeof(feels));
        snprintf(out->detail, sizeof(out->detail), "%s %s%s", lang_str(ctx->lang, LS_FEELS_LIKE), feels, out->unit);
    }
    out->percent = n.precip == DS_WX_NO_PCT ? -1 : n.precip;
}

static void resolve_today(const ui_context_t *ctx, ui_value_t *out)
{
    const ds_weather_t *w = ds_weather(ctx->ds);
    int d = w != NULL ? ctx->local_day - w->day0_local : -1;
    if (w == NULL || d < 0 || d >= DS_WX_DAYS || w->days[d].max_c10 == DS_WX_NO_TEMP ||
        !forecast_state(ds_weather_freshness(ctx->ds, ctx->now), w->fetched, ctx, out)) {
        out->state = UI_VALUE_MISSING;
        return;
    }
    const ds_wx_day_t *day = &w->days[d];
    char hi[6], lo[6]; /* "-128" at most: 0.1 °C fits an int16 */
    degrees(ctx, day->max_c10, hi, sizeof(hi));
    degrees(ctx, day->min_c10, lo, sizeof(lo));
    snprintf(out->text, sizeof(out->text), "%s" DEGREE " / %s" DEGREE, hi, lo);
    snprintf(out->short_text, sizeof(out->short_text), "%s/%s" DEGREE, hi, lo);
    out->sky = weather_sky(day->code);
    snprintf(out->extra, sizeof(out->extra), "%s", lang_str(ctx->lang, k_sky_words[out->sky]));
    out->percent = day->precip == DS_WX_NO_PCT ? -1 : day->precip;
}

static void resolve_hourly(const ui_context_t *ctx, ui_value_t *out)
{
    const ds_weather_t *w = ds_weather(ctx->ds);
    if (w == NULL || !forecast_state(ds_weather_freshness(ctx->ds, ctx->now), w->fetched, ctx, out)) {
        out->state = UI_VALUE_MISSING;
        return;
    }
    time_t first = ctx->now - ctx->now % 3600 + 3600; /* spec §5.1: the next 12 h, every 2 h */
    for (int i = 0; i < UI_SERIES_MAX; i++) {
        time_t t = first + (time_t)i * 2 * 3600;
        int h = ds_hour_index(w->hour0, t);
        if (h < 0 || w->hours[h].temp_c10 == DS_WX_NO_TEMP) {
            break;
        }
        ui_series_point_t *p = &out->series[out->series_count++];
        struct tm local;
        localtime_r(&t, &local);
        const char *suffix;
        char hour[8];
        lang_format_time(local.tm_hour, 0, 0, true, false, hour, sizeof(hour), &suffix);
        snprintf(p->label, sizeof(p->label), "%.2s", hour); /* "14" of "14:00" */
        char temp[6];
        degrees(ctx, w->hours[h].temp_c10, temp, sizeof(temp));
        snprintf(p->temp, sizeof(p->temp), "%s" DEGREE, temp);
        p->sky = (uint8_t)weather_sky(w->hours[h].code);
        p->night = night_at(ctx, t + 1800);
    }
    if (out->series_count == 0) {
        out->state = UI_VALUE_MISSING;
    }
}

static void resolve_daily(const ui_context_t *ctx, ui_value_t *out)
{
    const ds_weather_t *w = ds_weather(ctx->ds);
    if (w == NULL || !forecast_state(ds_weather_freshness(ctx->ds, ctx->now), w->fetched, ctx, out)) {
        out->state = UI_VALUE_MISSING;
        return;
    }
    for (int d = ctx->local_day - w->day0_local; d >= 0 && d < DS_WX_DAYS; d++) {
        const ds_wx_day_t *day = &w->days[d];
        if (day->max_c10 == DS_WX_NO_TEMP) {
            break;
        }
        ui_series_point_t *p = &out->series[out->series_count++];
        int y, m, dd;
        util_civil_from_days(w->day0_local + d, &y, &m, &dd);
        int wday = (int)((w->day0_local + d + 4) % 7); /* 1970-01-01 was a Thursday */
        snprintf(p->label, sizeof(p->label), "%s", ctx->lang->weekdays_short[wday]);
        char t[6];
        degrees(ctx, day->max_c10, t, sizeof(t));
        snprintf(p->temp, sizeof(p->temp), "%s" DEGREE, t);
        degrees(ctx, day->min_c10, t, sizeof(t));
        snprintf(p->temp2, sizeof(p->temp2), "%s" DEGREE, t);
        p->sky = (uint8_t)weather_sky(day->code);
    }
    if (out->series_count == 0) {
        out->state = UI_VALUE_MISSING;
    }
}

/* wx.rain2h (spec §11.4, D27): the next 8 quarter hours, and when the rain is. Missing unless all 8
 * are stored. */
static void resolve_rain(const ui_context_t *ctx, ui_value_t *out)
{
    const ds_weather_t *w = ds_weather(ctx->ds);
    int i0 = w != NULL ? ds_rain_index(w->rain.t0, ctx->now) : -1;
    if (i0 < 0 || i0 + UI_RAIN_STEPS > DS_RAIN_STEPS ||
        !forecast_state(ds_weather_freshness(ctx->ds, ctx->now), w->fetched, ctx, out)) {
        out->state = UI_VALUE_MISSING;
        return;
    }
    int first = -1, known = 0; /* the first quarter hour with 0.1 mm or more; those with an amount */
    for (int i = 0; i < UI_RAIN_STEPS; i++) {
        out->rain_mm10[i] = w->rain.mm10[i0 + i];
        out->rain_prob[i] = w->rain.prob[i0 + i];
        known += out->rain_mm10[i] != DS_RAIN_NONE;
        if (first < 0 && out->rain_mm10[i] != DS_RAIN_NONE && out->rain_mm10[i] > 0) {
            first = i;
        }
    }
    if (known == 0) {
        out->state = UI_VALUE_MISSING; /* nulls: no data, not a dry spell */
        return;
    }
    out->series_count = UI_RAIN_STEPS;
    if (first == 0) {
        snprintf(out->text, sizeof(out->text), "%s", lang_str(ctx->lang, LS_RAIN_NOW));
        char rate[12];
        lang_format_decimal(ctx->lang, (long)out->rain_mm10[0] * 4, 1, rate, sizeof(rate)); /* per hour */
        snprintf(out->extra, sizeof(out->extra), "%s %s", rate, lang_str(ctx->lang, LS_MM_PER_H));
    } else if (first > 0) {
        char at[12]; /* where its quarter hour starts */
        ui_clock_text(ctx, (time_t)w->rain.t0 + (time_t)(i0 + first - 1) * DS_RAIN_STEP_S, at, sizeof(at));
        snprintf(out->text, sizeof(out->text), "%s %s", lang_str(ctx->lang, LS_RAIN_FROM), at);
    } else {
        snprintf(out->text, sizeof(out->text), "%s", lang_str(ctx->lang, LS_DRY_2H));
    }
}

/* ---- air quality and pollen ---- */

static const lang_str_t k_bands[] = { LS_AQ_GOOD, LS_AQ_FAIR, LS_AQ_MODERATE,
                                      LS_AQ_POOR, LS_AQ_VERY_POOR, LS_AQ_EXTREMELY_POOR };
static const lang_str_t k_levels[] = { LS_POLLEN_NONE, LS_POLLEN_LOW, LS_POLLEN_MODERATE, LS_POLLEN_HIGH };
static const lang_str_t k_types[] = { LS_POLLEN_ALDER, LS_POLLEN_BIRCH, LS_POLLEN_GRASS,
                                      LS_POLLEN_MUGWORT, LS_POLLEN_OLIVE, LS_POLLEN_RAGWEED };

static const ds_air_t *air_now(const ui_context_t *ctx, ui_value_t *out)
{
    const ds_air_t *a = ds_air(ctx->ds);
    if (a == NULL || !forecast_state(ds_air_freshness(ctx->ds, ctx->now), a->fetched, ctx, out)) {
        out->state = UI_VALUE_MISSING;
        return NULL;
    }
    return a;
}

static void resolve_air(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    const ds_air_t *a = air_now(ctx, out);
    int h = a != NULL ? ds_hour_index(a->hour0, ctx->now) : -1;
    if (h < 0) {
        out->state = UI_VALUE_MISSING;
        return;
    }
    const uint8_t *series = field == UI_FIELD_AQ_INDEX  ? a->aqi
                            : field == UI_FIELD_AQ_PM25 ? a->pm25
                            : field == UI_FIELD_AQ_PM10 ? a->pm10
                                                        : a->uv10;
    if (series[h] == DS_AQ_NONE) {
        out->state = UI_VALUE_MISSING;
        return;
    }
    if (field == UI_FIELD_AQ_UV) { /* D26: read rounded, in the WHO's bands */
        static const lang_str_t k_uv[] = { LS_UV_LOW, LS_UV_MODERATE, LS_UV_HIGH, LS_UV_VERY_HIGH, LS_UV_EXTREME };
        snprintf(out->text, sizeof(out->text), "%u", (unsigned)((series[h] + 5) / 10));
        out->percent = (int)weather_uv_band(series[h]);
        out->bands = 5;
        snprintf(out->extra, sizeof(out->extra), "%s", lang_str(ctx->lang, k_uv[out->percent]));
        return;
    }
    snprintf(out->text, sizeof(out->text), "%u", series[h]);
    if (field == UI_FIELD_AQ_INDEX) {
        out->bands = 6;
        out->percent = (int)weather_aq_band(series[h]);
        snprintf(out->extra, sizeof(out->extra), "%s", lang_str(ctx->lang, k_bands[out->percent]));
    } else {
        snprintf(out->unit, sizeof(out->unit), "\xC2\xB5g/m\xC2\xB3"); /* µg/m³ */
    }
}

static void resolve_pollen(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    const ds_air_t *a = air_now(ctx, out);
    int d = a != NULL ? ctx->local_day - a->day0_local : -1;
    if (d < 0 || d >= DS_WX_DAYS) {
        out->state = UI_VALUE_MISSING;
        return;
    }
    const uint16_t *day = a->pollen[d];
    int type;
    if (field == UI_FIELD_POLLEN_TOP) {
        bool any = false;
        for (int p = 0; p < DS_POLLEN_TYPES; p++) {
            any |= day[p] != DS_POLLEN_NONE;
        }
        if (!any) {
            out->state = UI_VALUE_MISSING; /* CAMS has no pollen here (outside Europe) */
            return;
        }
        type = weather_pollen_top(day);
        if (type < 0) { /* nothing in the air: "None" */
            out->percent = WEATHER_POLLEN_NONE;
            snprintf(out->text, sizeof(out->text), "%s", lang_str(ctx->lang, k_levels[WEATHER_POLLEN_NONE]));
            return;
        }
        snprintf(out->extra, sizeof(out->extra), "%s", lang_str(ctx->lang, k_types[type]));
    } else {
        type = field - UI_FIELD_POLLEN_ALDER;
        if (day[type] == DS_POLLEN_NONE) {
            out->state = UI_VALUE_MISSING;
            return;
        }
        snprintf(out->extra, sizeof(out->extra), "%u/m\xC2\xB3", (unsigned)((day[type] + 5) / 10));
    }
    out->percent = (int)weather_pollen_level((ds_pollen_t)type, day[type]);
    snprintf(out->text, sizeof(out->text), "%s", lang_str(ctx->lang, k_levels[out->percent]));
}

bool ui_resolve_forecast(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    switch (field) {
    case UI_FIELD_WX_NOW:
        resolve_now(ctx, out);
        return true;
    case UI_FIELD_WX_TODAY:
        resolve_today(ctx, out);
        return true;
    case UI_FIELD_WX_HOURLY:
        resolve_hourly(ctx, out);
        return true;
    case UI_FIELD_WX_DAILY:
        resolve_daily(ctx, out);
        return true;
    case UI_FIELD_WX_RAIN2H:
        resolve_rain(ctx, out);
        return true;
    case UI_FIELD_SUN_TIMES:
        resolve_sun(ctx, out);
        return true;
    case UI_FIELD_AQ_INDEX:
    case UI_FIELD_AQ_PM25:
    case UI_FIELD_AQ_PM10:
    case UI_FIELD_AQ_UV:
        resolve_air(ctx, field, out);
        return true;
    case UI_FIELD_POLLEN_TOP:
    case UI_FIELD_POLLEN_ALDER:
    case UI_FIELD_POLLEN_BIRCH:
    case UI_FIELD_POLLEN_GRASS:
    case UI_FIELD_POLLEN_MUGWORT:
    case UI_FIELD_POLLEN_OLIVE:
    case UI_FIELD_POLLEN_RAGWEED:
        resolve_pollen(ctx, field, out);
        return true;
    default:
        return false;
    }
}

/* ---- widgets ---- */

const gfx_bitmap_t *ui_sky_icon(int sky, bool night, int size)
{
#define ICON(name) (size >= 48 ? &gfx_icon_##name##_48 : size >= 24 ? &gfx_icon_##name##_24 : &gfx_icon_##name##_16)
    switch (sky) {
    case WEATHER_SKY_CLEAR:
        return night ? ICON(wx_clear_night) : ICON(wx_clear_day);
    case WEATHER_SKY_MAINLY_CLEAR:
        return night ? ICON(wx_mainly_night) : ICON(wx_mainly_day);
    case WEATHER_SKY_PARTLY_CLOUDY:
        return night ? ICON(wx_partly_night) : ICON(wx_partly_day);
    case WEATHER_SKY_OVERCAST:
        return ICON(wx_overcast);
    case WEATHER_SKY_FOG:
        return ICON(wx_fog);
    case WEATHER_SKY_DRIZZLE:
        return ICON(wx_drizzle);
    case WEATHER_SKY_RAIN:
        return ICON(wx_rain);
    case WEATHER_SKY_FREEZING_RAIN:
        return ICON(wx_freezing);
    case WEATHER_SKY_SNOW:
        return ICON(wx_snow);
    case WEATHER_SKY_SHOWERS:
        return night ? ICON(wx_showers_night) : ICON(wx_showers_day);
    case WEATHER_SKY_SNOW_SHOWERS:
        return night ? ICON(wx_snow_showers_night) : ICON(wx_snow_showers_day);
    case WEATHER_SKY_THUNDERSTORM:
        return ICON(wx_thunder);
    default:
        return ICON(wx_unknown);
    }
#undef ICON
}

/* Text centred across r with `pad` px free on either side, cut with an ellipsis to fit. */
static void centred_in(gfx_fb_t *fb, const gfx_font_t *f, gfx_rect_t r, int pad, int baseline, const char *text)
{
    char fit[64];
    gfx_text_ellipsize(f, text, r.w - 2 * pad, fit, sizeof(fit));
    gfx_text(fb, f, r.x + (r.w - gfx_text_width(f, fit)) / 2, baseline, fit, GFX_BLACK);
}

static void centred(gfx_fb_t *fb, const gfx_font_t *f, gfx_rect_t r, int baseline, const char *text)
{
    centred_in(fb, f, r, 4, baseline, text);
}

static int label_line(gfx_fb_t *fb, gfx_rect_t r, const gfx_font_t *f, const char *label)
{
    char fit[40];
    gfx_text_ellipsize(f, label, r.w - 12, fit, sizeof(fit));
    gfx_text(fb, f, r.x + 6, r.y + 6 + f->ascent, fit, GFX_BLACK);
    return 6 + f->line_height;
}

/* A value and its unit as one group: "23" in `vf`, "°C" in `uf`, left at x. Returns the width. */
static int value_unit(gfx_fb_t *fb, const gfx_font_t *vf, const gfx_font_t *uf, int x, int baseline, const char *value,
                      const char *unit)
{
    int pen = gfx_text(fb, vf, x, baseline, value, GFX_BLACK);
    if (unit[0]) {
        pen = gfx_text(fb, uf, pen + 2, baseline - (vf->ascent - uf->ascent), unit, GFX_BLACK);
    }
    return pen - x;
}

static int value_unit_width(const gfx_font_t *vf, const gfx_font_t *uf, const char *value, const char *unit)
{
    return gfx_text_width(vf, value) + (unit[0] ? 2 + gfx_text_width(uf, unit) : 0);
}

static int ink_height(const gfx_font_t *f)
{
    const gfx_glyph_t *g = gfx_font_glyph(f, '0');
    return g != NULL ? g->height : f->ascent;
}

/* A row of `count` cells, the first `filled` inked: the band of an index or a pollen level. */
static void level_bar(gfx_fb_t *fb, int x, int y, int count, int filled, int cell_w, int cell_h)
{
    for (int i = 0; i < count; i++) {
        gfx_rect_t c = { (int16_t)(x + i * (cell_w + 3)), (int16_t)y, (int16_t)cell_w, (int16_t)cell_h };
        if (i < filled) {
            gfx_fill_rect(fb, c, GFX_BLACK);
        } else {
            gfx_rect(fb, c, GFX_BLACK);
        }
    }
}

/* ---- XS (M6c, D34): the status bar's look ---- */

/* An XS icon's size: 24 px from 34 px of height in one line, from 60 px stacked; 16 px otherwise. */
static int tiny_icon(gfx_rect_t r)
{
    return r.h >= (ui_tiny_stacked(r) ? 60 : 34) ? 24 : 16;
}

/* The largest of bold 28, bold 20, bold 16 and sans 12 in which `text` fits max_w, its ink 2 px clear of either
 * edge of max_h; with `cut`, sans 12 cut to max_w when none fits, else NULL. */
static const gfx_font_t *tiny_face(const char *text, int max_w, int max_h, bool cut, char *fit, size_t size)
{
    static const gfx_font_t *const k_faces[] = { &gfx_font_sans_bold_28, &gfx_font_sans_bold_20,
                                                 &gfx_font_sans_bold_16, &gfx_font_sans_12 };
    for (size_t i = 0; i < sizeof(k_faces) / sizeof(k_faces[0]); i++) {
        const gfx_font_t *f = k_faces[i];
        if (gfx_text_width(f, text) <= max_w && ui_ink_above(f, text) + ui_ink_below(f, text) + 4 <= max_h) {
            snprintf(fit, size, "%s", text);
            return f;
        }
    }
    if (!cut) {
        return NULL;
    }
    gfx_text_ellipsize(&gfx_font_sans_12, text, max_w, fit, size);
    return &gfx_font_sans_12;
}

/* The symbol over `first` in the largest face that fits, `second` in sans 12 under it where there is room; all
 * centred, 2 px clear of either edge, and 2 px more for words, whose accents may overhang their advance. */
static void tiny_stack(gfx_fb_t *fb, gfx_rect_t r, const gfx_bitmap_t *icon, const char *first, const char *second,
                       bool digits)
{
    int cx = r.x + r.w / 2, max_w = digits ? r.w - 4 : r.w - 8;
    int icon_h = icon != NULL ? icon->height + 4 : 0;
    char fit[48];
    const gfx_font_t *f = tiny_face(first, max_w, r.h - icon_h, true, fit, sizeof(fit));
    int fh = ui_ink_above(f, fit) + ui_ink_below(f, fit);
    const gfx_font_t *sf = &gfx_font_sans_12;
    int sh = second != NULL ? ui_ink_above(sf, second) + ui_ink_below(sf, second) : 0;
    bool two = second != NULL && second[0] && gfx_text_width(sf, second) <= max_w && icon_h + fh + 4 + sh + 4 <= r.h;
    int block = icon_h + fh + (two ? 4 + sh : 0);
    int top = r.y + (r.h - block) / 2;
    if (icon != NULL) {
        gfx_bitmap(fb, cx - icon->width / 2, top, icon, GFX_BLACK);
    }
    int y = top + icon_h;
    gfx_text(fb, f, cx - gfx_text_width(f, fit) / 2, y + ui_ink_above(f, fit), fit, GFX_BLACK);
    if (two) {
        y += fh + 4;
        gfx_text(fb, sf, cx - gfx_text_width(sf, second) / 2, y + ui_ink_above(sf, second), second, GFX_BLACK);
    }
}

/* One line, like the status bar: the icon, `first` in the largest face the height takes, then `second` smaller on
 * its baseline where it fits; with `icon2`, that icon and `third` after (the sun's rise, then its set). `first`
 * alone, centred, where it would be cut beside the icon. Digits keep 2 px from the right edge, words 4, as an
 * accent may overhang its advance. A narrow, tall cell stacks `first` and `second` under the icon instead. */
static void tiny_row(gfx_fb_t *fb, gfx_rect_t r, const gfx_bitmap_t *icon, const char *first, const char *second,
                     const gfx_bitmap_t *icon2, const char *third, bool digits)
{
    if (ui_tiny_stacked(r)) {
        tiny_stack(fb, r, icon, first, second, digits);
        return;
    }
    int cy = r.y + r.h / 2, x = r.x + 3, right = r.x + r.w - (digits ? 2 : 4), words = r.x + r.w - 4;
    char fit[48];
    const gfx_font_t *f = icon != NULL ? tiny_face(first, right - x - icon->width - 3, r.h, false, fit, sizeof(fit))
                                       : NULL;
    if (f != NULL) { /* beside the icon, or alone where it would be cut there */
        gfx_bitmap(fb, x, cy - icon->height / 2, icon, GFX_BLACK);
        x += icon->width + 3;
    } else {
        f = tiny_face(first, digits ? r.w - 4 : right - x, r.h, true, fit, sizeof(fit));
        x = digits ? r.x + (r.w - gfx_text_width(f, fit)) / 2 : x;
        icon2 = NULL;
    }
    int base = r.y + (r.h + ui_ink_above(f, fit) - ui_ink_below(f, fit)) / 2;
    x = gfx_text(fb, f, x, base, fit, GFX_BLACK) + 5;
    if (second != NULL && second[0]) {
        const gfx_font_t *sf = f == &gfx_font_sans_bold_28 ? &gfx_font_sans_16 : &gfx_font_sans_12;
        if (gfx_text_width(sf, second) <= words - x && base - ui_ink_above(sf, second) >= r.y + 2 &&
            base + ui_ink_below(sf, second) <= r.y + r.h - 2) {
            x = gfx_text(fb, sf, x, base, second, GFX_BLACK) + 6;
        }
    }
    if (icon2 != NULL && third != NULL && x + icon2->width + 3 + gfx_text_width(f, third) <= right) {
        gfx_bitmap(fb, x, cy - icon2->height / 2, icon2, GFX_BLACK);
        gfx_text(fb, f, x + icon2->width + 3, base, third, GFX_BLACK);
    }
}

/* Whether digits `text` fit a line of `r` uncut, beside `icon` or alone, as tiny_row() places them. */
static bool tiny_line_fits(gfx_rect_t r, const gfx_bitmap_t *icon, const char *text)
{
    char fit[48];
    int beside = r.w - 2 - 3 - (icon != NULL ? icon->width + 3 : 0);
    return (icon != NULL && tiny_face(text, beside, r.h, false, fit, sizeof(fit)) != NULL) ||
           tiny_face(text, r.w - 4, r.h, false, fit, sizeof(fit)) != NULL;
}

/* A 12-hour time without its suffix ("6:44" for "6:44 AM"), for a cell too narrow for it. */
static void time_short(const char *text, char *out, size_t size)
{
    snprintf(out, size, "%s", text);
    char *space = strrchr(out, ' ');
    if (space != NULL) {
        *space = '\0';
    }
}

/* The sun's times stacked: a row each, its icon beside the time; where that is too wide, each icon over its time;
 * where that is too tall, the two times alone, one over the other. */
static void tiny_sun_stack(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
{
    const gfx_bitmap_t *icons[2] = { &gfx_icon_sunrise_16, &gfx_icon_sunset_16 };
    char brief[2][16];
    time_short(v->text, brief[0], sizeof(brief[0]));
    time_short(v->extra, brief[1], sizeof(brief[1]));
    static const gfx_font_t *const k_faces[] = { &gfx_font_sans_bold_16, &gfx_font_sans_12 };
    for (int form = 0; form < 2; form++) { /* the times as they are, then without a 12-hour suffix */
        const char *times[2] = { form ? brief[0] : v->text, form ? brief[1] : v->extra };
        for (size_t i = 0; i < sizeof(k_faces) / sizeof(k_faces[0]); i++) { /* rows */
            const gfx_font_t *f = k_faces[i];
            int tw = gfx_text_width(f, times[0]) > gfx_text_width(f, times[1]) ? gfx_text_width(f, times[0])
                                                                               : gfx_text_width(f, times[1]);
            if (19 + tw <= r.w - 4 && 2 * 18 + 4 <= r.h) { /* digits: 2 px from the edges */
                int top = r.y + (r.h - 2 * 18) / 2, x = r.x + (r.w - 19 - tw) / 2;
                for (int k = 0; k < 2; k++) {
                    gfx_bitmap(fb, x, top + k * 18 + 1, icons[k], GFX_BLACK);
                    gfx_text(fb, f, x + 19, top + k * 18 + 9 + ink_height(f) / 2, times[k], GFX_BLACK);
                }
                return;
            }
        }
    }
    const gfx_font_t *f = &gfx_font_sans_12;
    bool whole = gfx_text_width(f, v->text) <= r.w - 4 && gfx_text_width(f, v->extra) <= r.w - 4;
    const char *times[2] = { whole ? v->text : brief[0], whole ? v->extra : brief[1] };
    int th = ink_height(f) + 3; /* a time's ink, its tails included */
    bool icons_fit = 2 * (16 + 2 + th) + 4 + 4 <= r.h;
    int line = (icons_fit ? 16 + 2 : 0) + th;
    int block = 2 * line + 4;
    int top = r.y + (r.h - block) / 2, cx = r.x + r.w / 2;
    char fit[24];
    for (int k = 0; k < 2; k++) {
        int y = top + k * (line + 4);
        if (icons_fit) {
            gfx_bitmap(fb, cx - 8, y, icons[k], GFX_BLACK);
            y += 16 + 2;
        }
        gfx_text_ellipsize(f, times[k], r.w - 4, fit, sizeof(fit));
        gfx_text(fb, f, cx - gfx_text_width(f, fit) / 2, y + ink_height(f), fit, GFX_BLACK);
    }
}

static void draw_weather_now(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    const gfx_bitmap_t *icon = ui_sky_icon(v->sky, v->night, 48);
    if (size == UI_SIZE_XS) {
        char t[sizeof(v->text) + 2];
        snprintf(t, sizeof(t), "%s" DEGREE, v->text);
        tiny_row(fb, r, ui_sky_icon(v->sky, v->night, tiny_icon(r)), t, v->extra, NULL, NULL, true);
        return;
    }
    if (size == UI_SIZE_S) {
        if (r.w < 150) { /* narrow: the sky above, the temperature below */
            gfx_bitmap(fb, r.x + (r.w - 48) / 2, r.y + 8, icon, GFX_BLACK);
            char t[sizeof(v->text) + 2];
            snprintf(t, sizeof(t), "%s" DEGREE, v->text);
            centred(fb, &gfx_font_sans_bold_28, r, r.y + 8 + 48 + 8 + ink_height(&gfx_font_sans_bold_28), t);
            return;
        }
        gfx_bitmap(fb, r.x + 10, r.y + (r.h - 48) / 2 - 8, icon, GFX_BLACK);
        int x = r.x + 10 + 48 + 12;
        value_unit(fb, &gfx_font_sans_bold_28, &gfx_font_sans_16, x, r.y + r.h / 2 + 4, v->text, v->unit);
        char word[32];
        gfx_text_ellipsize(&gfx_font_sans_16, v->extra, r.x + r.w - 6 - x, word, sizeof(word));
        gfx_text(fb, &gfx_font_sans_16, x, r.y + r.h / 2 + 26, word, GFX_BLACK);
        return;
    }
    const gfx_font_t *label = size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16;
    int top = r.y + label_line(fb, r, label, v->label);
    const gfx_font_t *uf = size == UI_SIZE_M ? &gfx_font_sans_16 : &gfx_font_sans_bold_20;
    if (size == UI_SIZE_M && r.w < 150) { /* a tall, narrow grid cell: stacked */
        gfx_bitmap(fb, r.x + (r.w - 48) / 2, top, icon, GFX_BLACK);
        int vw = value_unit_width(&gfx_font_sans_bold_28, &gfx_font_sans_16, v->text, v->unit);
        int base = top + 48 + 6 + ink_height(&gfx_font_sans_bold_28);
        value_unit(fb, &gfx_font_sans_bold_28, &gfx_font_sans_16, r.x + (r.w - vw) / 2, base, v->text, v->unit);
        if (base + 20 <= r.y + r.h) {
            centred(fb, &gfx_font_sans_16, r, base + 20, v->extra);
        }
        return;
    }
    /* "-13 °C" or "102 °F" beside the sky: smaller digits before they reach the cell's edges */
    static const gfx_font_t *const k_fit_m[] = { &gfx_font_num_cb_48, &gfx_font_sans_bold_28 };
    static const gfx_font_t *const k_fit_l[] = { &gfx_font_num_cb_72, &gfx_font_num_cb_48, &gfx_font_sans_bold_28 };
    const gfx_font_t *const *fonts = size == UI_SIZE_M ? k_fit_m : k_fit_l;
    int count = size == UI_SIZE_M ? 2 : 3;
    const gfx_font_t *vf = fonts[0];
    for (int i = 0; i < count; i++) {
        vf = fonts[i];
        if (48 + 10 + value_unit_width(vf, uf, v->text, v->unit) <= r.w - 8) {
            break;
        }
    }
    int ih = ink_height(vf);
    int vw = value_unit_width(vf, uf, v->text, v->unit);
    int group = 48 + 10 + vw;
    int x = r.x + (r.w - group) / 2;
    int row_h = ih > 48 ? ih : 48; /* the digits, or the sky's icon when it is taller */
    int body_h = size == UI_SIZE_M ? (r.y + r.h - top) : row_h + 8;
    int icon_y = size == UI_SIZE_M ? top + (body_h - 48) / 2 : top + (row_h - 48) / 2 + 4;
    gfx_bitmap(fb, x, icon_y, icon, GFX_BLACK);
    int base = size == UI_SIZE_M ? top + (body_h + ih) / 2 : top + (row_h + ih) / 2 + 4;
    value_unit(fb, vf, uf, x + 58, base, v->text, v->unit);
    if (size == UI_SIZE_M) {
        return; /* no room for the word below in a short slot */
    }
    centred(fb, &gfx_font_sans_bold_20, r, base + 30, v->extra);
    if (v->detail[0]) {
        centred(fb, &gfx_font_sans_16, r, base + 54, v->detail);
    }
}

static void precip(gfx_fb_t *fb, int x, int baseline, int pct)
{
    if (pct < 0) {
        return;
    }
    char text[16];
    snprintf(text, sizeof(text), "%d %%", pct);
    gfx_bitmap(fb, x, baseline - 14, &gfx_icon_drop_16, GFX_BLACK);
    gfx_text(fb, &gfx_font_sans_16, x + 18, baseline, text, GFX_BLACK);
}

static void draw_weather_day(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    if (size == UI_SIZE_XS) { /* "23/13°"; where it doesn't fit, "23°" over "13°", or the high alone */
        const gfx_bitmap_t *icon = ui_sky_icon(v->sky, false, tiny_icon(r));
        char high[sizeof(v->short_text) + 2];
        snprintf(high, sizeof(high), "%s", v->short_text);
        char *slash = strchr(high, '/');
        const char *low = slash != NULL ? slash + 1 : "";
        if (slash != NULL) {
            *slash = '\0';
            snprintf(slash, sizeof(high) - (size_t)(slash - high), DEGREE);
        }
        char fit[48];
        bool whole = ui_tiny_stacked(r)
                         ? tiny_face(v->short_text, r.w - 4, r.h - icon->height - 4, false, fit, sizeof(fit)) != NULL
                         : tiny_line_fits(r, icon, v->short_text);
        if (whole || slash == NULL) {
            tiny_row(fb, r, icon, v->short_text, NULL, NULL, NULL, true);
        } else {
            tiny_row(fb, r, icon, high, ui_tiny_stacked(r) ? v->short_text + (low - high) : NULL, NULL, NULL, true);
        }
        return;
    }
    if (size == UI_SIZE_S && r.w < 150) {
        gfx_bitmap(fb, r.x + (r.w - 48) / 2, r.y + 8, ui_sky_icon(v->sky, false, 48), GFX_BLACK);
        centred(fb, &gfx_font_sans_bold_20, r, r.y + 8 + 48 + 8 + ink_height(&gfx_font_sans_bold_20), v->short_text);
        if (v->percent >= 0) {
            char text[16];
            snprintf(text, sizeof(text), "%d %%", v->percent);
            centred(fb, &gfx_font_sans_12, r, r.y + 8 + 48 + 8 + ink_height(&gfx_font_sans_bold_20) + 18, text);
        }
        return;
    }
    int top = r.y;
    if (size != UI_SIZE_S) {
        top += label_line(fb, r, size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16, v->label);
    }
    const gfx_font_t *vf = &gfx_font_sans_bold_28;
    int body_h = r.y + r.h - top;
    if (r.w < 150) { /* a grid cell: stacked */
        gfx_bitmap(fb, r.x + (r.w - 48) / 2, top, ui_sky_icon(v->sky, false, 48), GFX_BLACK);
        int base = top + 48 + 6 + ink_height(&gfx_font_sans_bold_20);
        centred(fb, &gfx_font_sans_bold_20, r, base, v->short_text);
        if (base + 20 <= r.y + r.h && v->percent >= 0) {
            int w = 18 + gfx_text_width(&gfx_font_sans_16, "100 %");
            precip(fb, r.x + (r.w - w) / 2, base + 20, v->percent);
        }
        return;
    }
    int x = r.x + 10;
    gfx_bitmap(fb, x, top + (body_h - 48) / 2, ui_sky_icon(v->sky, false, 48), GFX_BLACK);
    int tx = x + 48 + 12;
    int room = r.x + r.w - 6 - tx;
    /* "23° / 13°" is wider than "18° / 9°": the short form, then a smaller font, before an ellipsis */
    const char *text = v->text;
    if (gfx_text_width(vf, text) > room) {
        text = v->short_text;
        if (gfx_text_width(vf, text) > room) {
            vf = &gfx_font_sans_bold_20;
            text = gfx_text_width(vf, v->text) <= room ? v->text : v->short_text;
        }
    }
    char fit[24];
    gfx_text_ellipsize(vf, text, room, fit, sizeof(fit));
    int base = top + body_h / 2 + (v->percent >= 0 ? 2 : ink_height(vf) / 2);
    gfx_text(fb, vf, tx, base, fit, GFX_BLACK);
    precip(fb, tx, base + 22, v->percent);
}

static void draw_series(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
{
    int n = v->series_count;
    int fit = r.w / 33;
    n = n < fit ? n : fit;
    if (n <= 0) {
        return;
    }
    bool daily = v->field == UI_FIELD_WX_DAILY;
    int col = r.w / n;
    /* "-13°" in a frost or "102°" on a hot day is wider than a column: the strip's temperatures go
     * smaller together, rather than one of them ending in "…" */
    static const gfx_font_t *const k_temp_fonts[] = { &gfx_font_sans_bold_16, &gfx_font_sans_16, &gfx_font_sans_12 };
    const gfx_font_t *tf = k_temp_fonts[0];
    for (int f = 0; f < 3; f++) {
        tf = k_temp_fonts[f];
        bool fits = true;
        for (int i = 0; i < n && fits; i++) {
            fits = gfx_text_width(tf, v->series[i].temp) <= col - 2;
        }
        if (fits) {
            break;
        }
    }
    int block = 13 + 2 + 24 + 2 + 16 + (daily ? 14 : 0);
    int top = v->state == UI_VALUE_STALE ? r.y + 3 : r.y + (r.h - block) / 2; /* the age mark goes below */
    for (int i = 0; i < n; i++) {
        const ui_series_point_t *p = &v->series[i];
        gfx_rect_t c = { (int16_t)(r.x + i * col), r.y, (int16_t)col, r.h };
        centred_in(fb, &gfx_font_sans_12, c, 1, top + gfx_font_sans_12.ascent, p->label);
        gfx_bitmap(fb, c.x + (col - 24) / 2, top + 15, ui_sky_icon(p->sky, p->night, 24), GFX_BLACK);
        centred_in(fb, tf, c, 1, top + 15 + 24 + 2 + tf->ascent, p->temp);
        if (daily) {
            centred_in(fb, &gfx_font_sans_12, c, 1, top + 15 + 24 + 2 + 16 + gfx_font_sans_12.ascent, p->temp2);
        }
    }
}

/* wx.rain2h: its words over 8 bars, one a quarter hour (D27). A bar is a third, two thirds or the
 * whole height for light, moderate or heavy rain (below 2.5 mm/h, below 7.6 mm/h, above); a likely
 * one is solid, one under 50 % an outline; a dry one leaves the baseline. */
static void draw_rain(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
{
    const gfx_font_t *f = &gfx_font_sans_bold_16;
    int max_w = r.w - 12;
    char line1[sizeof(v->text) + sizeof(v->extra) + 4], line2[sizeof(line1)] = "";
    snprintf(line1, sizeof(line1), "%s%s%s", v->text, v->extra[0] ? " \xC2\xB7 " : "", v->extra);
    if (gfx_text_width(f, line1) > max_w) { /* two lines: the words over the rate, or split at a space */
        if (v->extra[0]) {
            snprintf(line1, sizeof(line1), "%s", v->text);
            snprintf(line2, sizeof(line2), "%s", v->extra);
        } else {
            ui_split_two_lines(f, v->text, max_w, line1, line2, sizeof(line1));
        }
    }
    int text_h = (line2[0] ? 2 : 1) * f->line_height;
    int room = r.h - 12 - (v->state == UI_VALUE_STALE ? 18 : 0) - text_h - 8 - 5; /* the age mark goes below */
    int bar_h = room < 36 ? room : 36;
    if (bar_h < 9) {
        bar_h = 0; /* no room for bars: the words alone */
    }
    int block = text_h + (bar_h ? 8 + bar_h + 5 : 0);
    int top = v->state == UI_VALUE_STALE ? r.y + 6 : r.y + (r.h - block) / 2;
    centred(fb, f, r, top + f->ascent, line1);
    if (line2[0]) {
        centred(fb, f, r, top + f->line_height + f->ascent, line2);
    }
    if (bar_h == 0) {
        return;
    }
    int base = top + text_h + 8 + bar_h;
    int w = r.w - 16;
    int pitch = w / UI_RAIN_STEPS;
    int x0 = r.x + 8 + (w - pitch * UI_RAIN_STEPS) / 2;
    for (int i = 0; i < UI_RAIN_STEPS; i++) {
        int mm10 = v->rain_mm10[i];
        int level = mm10 == DS_RAIN_NONE ? 0 : mm10 >= 19 ? 3 : mm10 >= 6 ? 2 : mm10 >= 1 ? 1 : 0;
        if (level == 0) {
            continue;
        }
        int h = level * bar_h / 3;
        gfx_rect_t bar = { (int16_t)(x0 + i * pitch + 1), (int16_t)(base - h), (int16_t)(pitch - 3), (int16_t)h };
        if (v->rain_prob[i] != DS_RAIN_NONE && v->rain_prob[i] < 50) {
            gfx_rect(fb, bar, GFX_BLACK);
        } else {
            gfx_fill_rect(fb, bar, GFX_BLACK);
        }
    }
    gfx_hline(fb, x0, base, pitch * UI_RAIN_STEPS, GFX_BLACK);
    for (int i = 0; i <= UI_RAIN_STEPS; i += UI_RAIN_STEPS / 2) { /* now, in 1 h, in 2 h */
        gfx_vline(fb, x0 + i * pitch - (i == UI_RAIN_STEPS ? 1 : 0), base, 5, GFX_BLACK);
    }
}

static void draw_sun(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    if (size == UI_SIZE_XS) {
        bool big = tiny_icon(r) == 24;
        if (v->polar) {
            tiny_row(fb, r, ui_sky_icon(WEATHER_SKY_CLEAR, v->polar != 1, tiny_icon(r)), v->text, NULL, NULL, NULL,
                     false);
        } else if (ui_tiny_stacked(r)) {
            tiny_sun_stack(fb, r, v);
        } else {
            const gfx_bitmap_t *rise = big ? &gfx_icon_sunrise_24 : &gfx_icon_sunrise_16;
            char brief[2][16];
            time_short(v->text, brief[0], sizeof(brief[0]));
            time_short(v->extra, brief[1], sizeof(brief[1]));
            bool whole = tiny_line_fits(r, rise, v->text);
            tiny_row(fb, r, rise, whole ? v->text : brief[0], NULL, big ? &gfx_icon_sunset_24 : &gfx_icon_sunset_16,
                     whole ? v->extra : brief[1], true);
        }
        return;
    }
    int top = r.y;
    if (size != UI_SIZE_S) {
        top += label_line(fb, r, size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16, v->label);
    }
    if (v->polar) {
        const gfx_bitmap_t *icon = v->polar == 1 ? &gfx_icon_wx_clear_day_24 : &gfx_icon_wx_clear_night_24;
        gfx_bitmap(fb, r.x + (r.w - 24) / 2, top + 8, icon, GFX_BLACK);
        centred(fb, &gfx_font_sans_bold_16, r, top + 8 + 24 + 20, v->text);
        return;
    }
    const gfx_font_t *tf = size == UI_SIZE_S ? &gfx_font_sans_bold_16 : &gfx_font_sans_bold_20;
    int rows = 2 * 28 + (v->detail[0] && size != UI_SIZE_S ? 18 : 0);
    int y = top + (r.y + r.h - top - rows) / 2;
    /* a 12-hour time ("7:01 AM") in a narrow split cell: smaller faces before it reaches the edges */
    static const gfx_font_t *const k_smaller[] = { &gfx_font_sans_16, &gfx_font_sans_12 };
    int w = 0;
    for (int i = 0; i <= 2; i++) {
        int w1 = 24 + 6 + gfx_text_width(tf, v->text), w2 = 24 + 6 + gfx_text_width(tf, v->extra);
        w = w1 > w2 ? w1 : w2;
        if (w <= r.w - 8 || i == 2) {
            break;
        }
        tf = k_smaller[i];
    }
    int x = r.x + (r.w - w) / 2;
    gfx_bitmap(fb, x, y, &gfx_icon_sunrise_24, GFX_BLACK);
    gfx_text(fb, tf, x + 30, y + 12 + ink_height(tf) / 2, v->text, GFX_BLACK);
    gfx_bitmap(fb, x, y + 28, &gfx_icon_sunset_24, GFX_BLACK);
    gfx_text(fb, tf, x + 30, y + 28 + 12 + ink_height(tf) / 2, v->extra, GFX_BLACK);
    if (v->detail[0] && size != UI_SIZE_S) {
        centred(fb, &gfx_font_sans_12, r, y + 56 + 14, v->detail);
    }
}

/* aq.index and aq.uv: the number and its band (D25, D26). */
static void draw_level(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    const gfx_bitmap_t *icon = v->field == UI_FIELD_AQ_UV ? &gfx_icon_uv_24 : &gfx_icon_air_24;
    int bands = v->bands > 0 ? v->bands : 6;
    if (size == UI_SIZE_XS) {
        bool big = tiny_icon(r) == 24;
        const gfx_bitmap_t *xs_icon = v->field == UI_FIELD_AQ_UV ? (big ? &gfx_icon_uv_24 : &gfx_icon_uv_16)
                                                                 : (big ? &gfx_icon_air_24 : &gfx_icon_air_16);
        tiny_row(fb, r, xs_icon, v->text, v->extra, NULL, NULL, true);
        return;
    }
    if (size == UI_SIZE_S) {
        bool narrow = r.w < 150;
        int y = r.y + 10;
        if (narrow) {
            gfx_bitmap(fb, r.x + (r.w - 24) / 2, y, icon, GFX_BLACK);
            centred(fb, &gfx_font_sans_bold_28, r, y + 24 + 6 + ink_height(&gfx_font_sans_bold_28), v->text);
            centred(fb, &gfx_font_sans_12, r, y + 24 + 6 + ink_height(&gfx_font_sans_bold_28) + 18, v->extra);
            return;
        }
        gfx_bitmap(fb, r.x + 14, r.y + (r.h - 24) / 2, icon, GFX_BLACK);
        int x = r.x + 14 + 24 + 10;
        int base = r.y + r.h / 2 + 2;
        int pen = gfx_text(fb, &gfx_font_sans_bold_28, x, base, v->text, GFX_BLACK);
        char word[32];
        gfx_text_ellipsize(&gfx_font_sans_16, v->extra, r.x + r.w - 8 - pen - 8, word, sizeof(word));
        gfx_text(fb, &gfx_font_sans_16, pen + 8, base, word, GFX_BLACK);
        level_bar(fb, x, base + 10, bands, v->percent + 1, 12, 6);
        return;
    }
    int top = r.y + label_line(fb, r, size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16, v->label);
    const gfx_font_t *vf = size == UI_SIZE_M ? &gfx_font_num_cb_48 : &gfx_font_num_cb_72;
    int ih = ink_height(vf);
    int block = ih + 8 + 18 + 12;
    int y = top + (r.y + r.h - top - block) / 2;
    centred(fb, vf, r, y + ih, v->text);
    centred(fb, &gfx_font_sans_16, r, y + ih + 8 + 14, v->extra);
    int bar_w = bands * 12 + (bands - 1) * 3;
    level_bar(fb, r.x + (r.w - bar_w) / 2, y + ih + 8 + 20, bands, v->percent + 1, 12, 6);
}

/* pollen.*: a level, and its type or count (D25). */
static void draw_pollen(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    if (size == UI_SIZE_XS) {
        const gfx_bitmap_t *icon = tiny_icon(r) == 24 ? &gfx_icon_pollen_24 : &gfx_icon_pollen_16;
        if (ui_tiny_stacked(r) && gfx_text_width(&gfx_font_sans_12, v->text) > r.w - 8) { /* its level bar */
            int top = r.y + (r.h - icon->height - 4 - 6) / 2;
            gfx_bitmap(fb, r.x + (r.w - icon->width) / 2, top, icon, GFX_BLACK);
            level_bar(fb, r.x + (r.w - (3 * 10 + 2 * 3)) / 2, top + icon->height + 4, 3, v->percent, 10, 6);
            return;
        }
        tiny_row(fb, r, icon, v->text, v->extra[0] ? v->extra : v->label, NULL, NULL, false);
        return;
    }
    if (size == UI_SIZE_S) {
        int y = r.y + 10;
        if (r.w < 150) {
            gfx_bitmap(fb, r.x + (r.w - 24) / 2, y, &gfx_icon_pollen_24, GFX_BLACK);
            centred(fb, &gfx_font_sans_bold_16, r, y + 24 + 6 + 14, v->text);
            centred(fb, &gfx_font_sans_12, r, y + 24 + 6 + 14 + 16, v->extra[0] ? v->extra : v->label);
            level_bar(fb, r.x + (r.w - (3 * 14 + 2 * 3)) / 2, y + 24 + 6 + 14 + 24, 3, v->percent, 14, 6);
            return;
        }
        gfx_bitmap(fb, r.x + 14, r.y + (r.h - 24) / 2, &gfx_icon_pollen_24, GFX_BLACK);
        int x = r.x + 14 + 24 + 10;
        char line[48];
        gfx_text_ellipsize(&gfx_font_sans_bold_20, v->text, r.x + r.w - 6 - x, line, sizeof(line));
        gfx_text(fb, &gfx_font_sans_bold_20, x, r.y + r.h / 2, line, GFX_BLACK);
        gfx_text_ellipsize(&gfx_font_sans_12, v->extra[0] ? v->extra : v->label, r.x + r.w - 6 - x, line, sizeof(line));
        gfx_text(fb, &gfx_font_sans_12, x, r.y + r.h / 2 + 16, line, GFX_BLACK);
        return;
    }
    int top = r.y + label_line(fb, r, size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16, v->label);
    const gfx_font_t *vf = size == UI_SIZE_M ? &gfx_font_sans_bold_20 : &gfx_font_sans_bold_28;
    int block = 24 + 6 + vf->ascent + 6 + 16 + 12;
    int y = top + (r.y + r.h - top - block) / 2;
    gfx_bitmap(fb, r.x + (r.w - 24) / 2, y, &gfx_icon_pollen_24, GFX_BLACK);
    centred(fb, vf, r, y + 24 + 6 + vf->ascent, v->text);
    if (v->extra[0]) {
        centred(fb, &gfx_font_sans_16, r, y + 24 + 6 + vf->ascent + 18, v->extra);
    }
    level_bar(fb, r.x + (r.w - (3 * 16 + 2 * 3)) / 2, y + 24 + 6 + vf->ascent + 26, 3, v->percent, 16, 6);
}

bool ui_forecast_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    if (v->state == UI_VALUE_MISSING) {
        return false; /* the placeholder, like any other field */
    }
    switch (v->kind) {
    case UI_FK_WEATHER_NOW:
        draw_weather_now(fb, r, size, v);
        return true;
    case UI_FK_WEATHER_DAY:
        draw_weather_day(fb, r, size, v);
        return true;
    case UI_FK_SERIES:
        if (v->field == UI_FIELD_WX_RAIN2H) {
            draw_rain(fb, r, v);
        } else {
            draw_series(fb, r, v);
        }
        return true;
    case UI_FK_SUN:
        draw_sun(fb, r, size, v);
        return true;
    case UI_FK_LEVEL:
        draw_level(fb, r, size, v);
        return true;
    case UI_FK_POLLEN:
        draw_pollen(fb, r, size, v);
        return true;
    default:
        return false;
    }
}
