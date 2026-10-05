#include "ui_solar.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

/* The pv.* and energy.* fields (spec §5.1, §11.5, §11.6, M6d) and the sample day. */

/* "2.31" kW from W, "12.3" from 10 kW up. */
static void kw_text(const lang_t *lang, int32_t w, char *out, size_t size)
{
    long a = w < 0 ? -(long)w : w;
    if (a >= 10000) {
        lang_format_decimal(lang, (a + 50) / 100, 1, out, size);
    } else {
        lang_format_decimal(lang, (a + 5) / 10, 2, out, size);
    }
}

/* "18.4" kWh from Wh. */
static void kwh_text(const lang_t *lang, uint32_t wh, char *out, size_t size)
{
    lang_format_decimal(lang, (long)((wh + 50) / 100), 1, out, size);
}

/* A power and its shorter form where a number doesn't fit (spec §5.1): from 1 kW, one decimal, from 10 kW whole;
 * below 1 kW it would lose most of the value. */
static void set_kw(const lang_t *lang, int32_t w, ui_value_t *out)
{
    kw_text(lang, w, out->text, sizeof(out->text));
    long a = w < 0 ? -(long)w : w;
    if (a >= 10000) {
        snprintf(out->short_text, sizeof(out->short_text), "%ld", (a + 500) / 1000);
    } else if (a >= 1000) {
        lang_format_decimal(lang, (a + 50) / 100, 1, out->short_text, sizeof(out->short_text));
    }
    snprintf(out->unit, sizeof(out->unit), "kW");
}

/* An energy and its shorter form: whole kWh from 10 kWh. */
static void set_kwh(const lang_t *lang, uint32_t wh, ui_value_t *out)
{
    kwh_text(lang, wh, out->text, sizeof(out->text));
    if (wh >= 9950) {
        snprintf(out->short_text, sizeof(out->short_text), "%lu", (unsigned long)((wh + 500) / 1000));
    }
    snprintf(out->unit, sizeof(out->unit), "kWh");
}

/* A quarter hour's start as the clock setting shows it: "12:45", "12:45 PM". */
static void quarter_time(const ui_context_t *ctx, int quarter, char *out, size_t size)
{
    const char *suffix;
    char hm[12];
    lang_format_time(quarter / 4, quarter % 4 * 15, 0, ctx->clock_24h, false, hm, sizeof(hm), &suffix);
    snprintf(out, size, "%s%s%s", hm, suffix[0] ? " " : "", suffix);
}

static int flow_dir(int32_t w)
{
    return w >= UI_SOLAR_IDLE_W ? 1 : w <= -UI_SOLAR_IDLE_W ? -1 : 0;
}

/* pv.*: the forecast's day today, fresh until its wait is over (spec §5.1). */
static void resolve_forecast(const ui_context_t *ctx, const ui_solar_t *s, ui_field_id_t field, ui_value_t *out)
{
    const solar_forecast_t *f = s->forecast;
    if (f == NULL || f->day == 0) {
        return;
    }
    const lang_t *lang = ctx->lang;
    int32_t today = ctx->local_day;
    int quarter = ctx->local.tm_hour * 4 + ctx->local.tm_min / 15;
    int seconds = ctx->local.tm_min % 15 * 60 + ctx->local.tm_sec;
    const uint16_t *q = solar_day(f, today);
    uint32_t wh = SOLAR_WH_NONE;
    switch (field) {
    case UI_FIELD_PV_NOW:
        if (q != NULL) {
            set_kw(lang, q[quarter] * SOLAR_UNIT_W, out);
            out->state = UI_VALUE_FRESH;
        }
        break;
    case UI_FIELD_PV_TODAY:
    case UI_FIELD_PV_TOMORROW:
        wh = solar_day_wh(f, field == UI_FIELD_PV_TODAY ? today : today + 1);
        break;
    case UI_FIELD_PV_LEFT:
        wh = solar_left_wh(f, today, quarter, seconds);
        break;
    case UI_FIELD_PV_PEAK: {
        uint32_t w = 0;
        int at = 0;
        if (q != NULL) {
            if (solar_peak(f, today, &w, &at)) {
                quarter_time(ctx, at, out->extra, sizeof(out->extra));
            }
            set_kw(lang, (int32_t)w, out);
            out->state = UI_VALUE_FRESH;
        }
        break;
    }
    default:
        break;
    }
    if (wh != SOLAR_WH_NONE) {
        set_kwh(lang, wh, out);
        out->state = UI_VALUE_FRESH;
    }
    if (out->state == UI_VALUE_FRESH) {
        out->age_s = (uint32_t)ctx->now > f->fetched ? (uint32_t)ctx->now - f->fetched : 0;
        out->state = s->forecast_ttl_s == 0 || out->age_s <= s->forecast_ttl_s ? UI_VALUE_FRESH : UI_VALUE_STALE;
    }
}

/* energy.*: the last reading, fresh for 15 min; today's totals from a reading of today. */
static void resolve_house(const ui_context_t *ctx, const ui_solar_t *s, ui_field_id_t field, ui_value_t *out)
{
    const energy_reading_t *r = s->reading;
    if (r == NULL || r->at == 0) {
        return;
    }
    const lang_t *lang = ctx->lang;
    int32_t today = ctx->local_day;
    bool of_today = energy_reading_day(r) == today;
    uint32_t wh = ENERGY_WH_NONE;
    out->state = UI_VALUE_FRESH;
    switch (field) {
    case UI_FIELD_EN_PV:
        set_kw(lang, r->pv_w, out);
        break;
    case UI_FIELD_EN_LOAD:
        set_kw(lang, r->load_w, out);
        break;
    case UI_FIELD_EN_GRID: /* the way it goes: the label in M and up, an arrow in S and XS (up: to the grid) */
        set_kw(lang, r->grid_w, out);
        if (flow_dir(r->grid_w) != 0) {
            out->label = lang_str(lang, r->grid_w < 0 ? LS_EN_EXPORTING : LS_EN_IMPORTING);
            out->trend = r->grid_w < 0 ? 1 : -1;
        }
        break;
    case UI_FIELD_EN_BATTERY: /* like the device's: its charge, a bolt while charging, an arrow down while not */
        if (!s->battery || r->soc < 0) {
            out->state = UI_VALUE_MISSING;
            break;
        }
        out->percent = r->soc;
        out->battery = flow_dir(r->bat_w) > 0 ? DS_BAT_CHARGING : DS_BAT_DISCHARGING;
        out->trend = flow_dir(r->bat_w) < 0 ? -1 : 0;
        snprintf(out->text, sizeof(out->text), "%d", r->soc);
        snprintf(out->unit, sizeof(out->unit), "%%");
        if (flow_dir(r->bat_w) != 0) {
            char t[16];
            kw_text(lang, r->bat_w, t, sizeof(t));
            snprintf(out->extra, sizeof(out->extra), "%s kW", t);
        }
        break;
    case UI_FIELD_EN_YIELD:
        wh = of_today ? r->yield_wh : ENERGY_WH_NONE;
        break;
    case UI_FIELD_EN_EXPORT:
        wh = energy_to_grid_wh(s->day, r, today);
        out->trend = 1;
        break;
    case UI_FIELD_EN_IMPORT:
        wh = energy_from_grid_wh(s->day, r, today);
        out->trend = -1;
        break;
    case UI_FIELD_EN_SELF: {
        int pct = energy_self_pct(s->day, r, today);
        if (pct < 0) {
            out->state = UI_VALUE_MISSING;
        } else {
            snprintf(out->text, sizeof(out->text), "%d", pct);
            snprintf(out->unit, sizeof(out->unit), "%%");
        }
        break;
    }
    default:
        break;
    }
    if (field == UI_FIELD_EN_YIELD || field == UI_FIELD_EN_EXPORT || field == UI_FIELD_EN_IMPORT) {
        if (wh == ENERGY_WH_NONE) {
            out->state = UI_VALUE_MISSING;
            out->trend = 0;
        } else {
            set_kwh(lang, wh, out);
        }
    }
    if (out->state == UI_VALUE_FRESH) {
        out->age_s = (uint32_t)ctx->now > r->at ? (uint32_t)ctx->now - r->at : 0;
        out->state = energy_fresh(r, (uint32_t)ctx->now) ? UI_VALUE_FRESH : UI_VALUE_STALE;
    }
}

bool ui_resolve_solar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    if (field < UI_FIELD_PV_NOW || field > UI_FIELD_EN_SELF) {
        return false;
    }
    const ui_solar_t *s = ctx->solar;
    out->solar = s;
    if (s == NULL) {
        return true;
    }
    if (field <= UI_FIELD_PV_PEAK) {
        resolve_forecast(ctx, s, field, out);
    } else {
        resolve_house(ctx, s, field, out);
    }
    return true;
}

/* ---- the sample day ---- */

#define PI 3.14159265358979323846
#define DEMO_PEAK_W 4120.0
#define DEMO_RISE_H 6.75
#define DEMO_SET_H 18.85

/* The forecast's mean power for quarter hour `i` of day `d` (0 today, 1 tomorrow), in W. */
static double demo_w(int d, int i)
{
    double t = (i + 0.5) / 4.0;
    if (t <= DEMO_RISE_H || t >= DEMO_SET_H) {
        return 0;
    }
    double w = DEMO_PEAK_W * pow(sin(PI * (t - DEMO_RISE_H) / (DEMO_SET_H - DEMO_RISE_H)), 1.25);
    if (d == 0 && t > 15.0 && t < 17.0) {
        w *= 0.55 + 0.25 * sin((t - 15.0) * 6.0); /* cloud */
    }
    if (d == 1) {
        w *= 0.38 + 0.12 * sin(t * 2.1); /* overcast */
    }
    return w;
}

void ui_solar_demo(int32_t day, time_t midnight, time_t now, bool live, bool battery, solar_forecast_t *forecast,
                   energy_reading_t *reading, energy_day_t *energy_day)
{
    memset(forecast, 0, sizeof(*forecast));
    forecast->day = day;
    forecast->fetched = (uint32_t)(midnight + 5 * 3600 + 48 * 60);
    for (int d = 0; d < 2; d++) {
        uint32_t q_sum = 0;
        for (int i = 0; i < SOLAR_STEPS; i++) {
            forecast->q[d][i] = (uint16_t)lround(demo_w(d, i) / SOLAR_UNIT_W);
            q_sum += forecast->q[d][i];
        }
        forecast->wh[d] = (uint32_t)lround(q_sum * (SOLAR_UNIT_W / 4.0));
    }
    forecast->wh[2] = 21400;
    memset(reading, 0, sizeof(*reading));
    energy_day_init(energy_day);
    if (!live) {
        return;
    }
    double yield = 0;
    for (int i = 0; i < SOLAR_STEPS && midnight + (i + 1) * 900 <= now; i++) {
        double f = demo_w(0, i), a = i < 38 ? f * 0.7 : f * (1.04 + 0.03 * sin(i * 1.7)); /* mist, then sun */
        energy_day->q[i] = (uint16_t)lround(a / ENERGY_UNIT_W);
        energy_day->n[i] = 3;
        yield += a / 4.0;
    }
    uint32_t yield_wh = (uint32_t)lround(yield);
    uint32_t exported = battery ? yield_wh * 38 / 100 : yield_wh * 62 / 100;
    *reading = (energy_reading_t){
        .at = (uint32_t)(now - 180),
        .pv_w = 3420,
        .load_w = 860,
        .bat_w = battery ? 1200 : 0,
        .soc = (int16_t)(battery ? 64 : -1),
        .inverter = battery ? 5 : 4, /* X3-Hybrid, X1-Boost */
        .yield_wh = yield_wh,
        .to_grid_wh = 1000000 + exported,
        .from_grid_wh = 2000000 + (battery ? 600 : 1400),
    };
    reading->grid_w = -(reading->pv_w - reading->load_w - reading->bat_w);
    energy_day->day = day;
    energy_day->base_at = (uint32_t)(midnight + 120);
    energy_day->base_to_wh = 1000000;
    energy_day->base_from_wh = 2000000;
    energy_day->last_at = reading->at;
}
