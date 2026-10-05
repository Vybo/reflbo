#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include "solar.h"

#include <math.h>
#include <string.h>

#include "util_time.h"

/* Our PV model and the local quarter hours (spec §11.5). */

#define QUARTER_S 900
#define MAX_W (SOLAR_UNIT_W * (float)UINT16_MAX) /* what a quarter hour keeps: 655.35 kW */

float solar_model_w(float kwp, float gti_w_m2, float t_air_c, int losses_pct)
{
    if (!(gti_w_m2 > 0.0f)) {
        return 0.0f;
    }
    float t_cell = t_air_c + 0.031f * gti_w_m2;
    float w = kwp * gti_w_m2 * (1.0f - 0.004f * (t_cell - 25.0f)) * (1.0f - (float)losses_pct / 100.0f);
    return w > 0.0f ? w : 0.0f;
}

static int32_t local_day(time_t t, int *quarter)
{
    struct tm lt;
    localtime_r(&t, &lt);
    *quarter = lt.tm_hour * 4 + lt.tm_min / 15;
    return (int32_t)util_days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

void solar_acc_init(solar_acc_t *acc, time_t now)
{
    memset(acc, 0, sizeof(*acc));
    int quarter;
    acc->day = local_day(now, &quarter);
}

/* `wh` into the quarter hour that holds `t`, if it is one of the three days. */
static void add(solar_acc_t *acc, time_t t, float wh)
{
    int quarter;
    int k = local_day(t, &quarter) - acc->day;
    if (k >= 0 && k < SOLAR_DAYS) {
        acc->wh[k][quarter] += wh;
        acc->got[k][quarter] = true;
    }
}

/* The next quarter-hour mark after `t`: local quarter hours start on UTC ones, as every time zone's
 * offset is a whole number of quarter hours. */
static time_t next_mark(time_t t, time_t end)
{
    time_t next = (t / QUARTER_S + 1) * QUARTER_S;
    return next < end ? next : end;
}

/* Power a provider sent, within 0 and what a quarter hour keeps; 0 for what isn't a number. */
static float sane_w(float w)
{
    return w > 0.0f ? (w < MAX_W ? w : MAX_W) : 0.0f;
}

/* [from, to) cut to the UTC the three local days lie in, with a day to spare for any time zone, so a provider's
 * time years away costs nothing. */
static void clip(const solar_acc_t *acc, time_t *from, time_t *to)
{
    time_t lo = (time_t)(acc->day - 1) * 86400, hi = (time_t)(acc->day + SOLAR_DAYS + 1) * 86400;
    *from = *from > lo ? *from : lo;
    *to = *to < hi ? *to : hi;
}

void solar_acc_period(solar_acc_t *acc, time_t end, int seconds, float watts)
{
    time_t t = end - seconds;
    clip(acc, &t, &end);
    watts = sane_w(watts);
    while (t < end) {
        time_t next = next_mark(t, end);
        add(acc, t, watts * (float)(next - t) / 3600.0f);
        t = next;
    }
}

void solar_acc_line(solar_acc_t *acc, time_t t0, float w0, time_t t1, float w1)
{
    if (t1 <= t0) {
        return;
    }
    w0 = sane_w(w0);
    w1 = sane_w(w1);
    float slope = (w1 - w0) / (float)(t1 - t0);
    time_t from = t0;
    clip(acc, &from, &t1);
    for (time_t t = from; t < t1;) {
        time_t next = next_mark(t, t1);
        float wa = w0 + slope * (float)(t - t0), wb = w0 + slope * (float)(next - t0);
        add(acc, t, (wa + wb) / 2.0f * (float)(next - t) / 3600.0f);
        t = next;
    }
}

void solar_acc_cap(solar_acc_t *acc, float watts)
{
    float cap_wh = watts / 4.0f;
    for (int k = 0; k < SOLAR_DAYS; k++) {
        for (int i = 0; i < SOLAR_STEPS; i++) {
            acc->wh[k][i] = acc->wh[k][i] > cap_wh ? cap_wh : acc->wh[k][i];
        }
    }
}

void solar_acc_finish(const solar_acc_t *acc, const solar_forecast_t *old, uint32_t fetched,
                      solar_forecast_t *out)
{
    solar_forecast_t before;
    bool keep = old != NULL && old->day != 0;
    if (keep) {
        before = *old; /* `out` may be `old` */
    }
    memset(out, 0, sizeof(*out));
    out->day = acc->day;
    out->fetched = fetched;
    for (int k = 0; k < SOLAR_DAYS; k++) {
        const uint16_t *kept = keep && k < 2 ? solar_day(&before, acc->day + k) : NULL;
        bool any = false;
        double sum = 0.0;
        for (int i = 0; i < SOLAR_STEPS; i++) {
            float wh;
            if (acc->got[k][i]) {
                wh = acc->wh[k][i] < MAX_W / 4.0f ? acc->wh[k][i] : MAX_W / 4.0f; /* two planes' sum */
            } else if (kept != NULL) {
                wh = kept[i] * (SOLAR_UNIT_W / 4.0f);
            } else {
                continue;
            }
            any = true;
            if (k < 2) { /* today's and tomorrow's totals are their quarter hours', so the fields agree */
                out->q[k][i] = (uint16_t)lroundf(wh * 4.0f / SOLAR_UNIT_W);
                sum += out->q[k][i] * (SOLAR_UNIT_W / 4.0);
            } else {
                sum += wh;
            }
        }
        out->wh[k] = any ? (uint32_t)lround(sum) : SOLAR_WH_NONE;
    }
}

const uint16_t *solar_day(const solar_forecast_t *f, int32_t day)
{
    if (f == NULL || f->day == 0) {
        return NULL;
    }
    int32_t k = day - f->day;
    return k >= 0 && k < 2 && f->wh[k] != SOLAR_WH_NONE ? f->q[k] : NULL;
}

uint32_t solar_day_wh(const solar_forecast_t *f, int32_t day)
{
    if (f == NULL || f->day == 0) {
        return SOLAR_WH_NONE;
    }
    int32_t k = day - f->day;
    return k >= 0 && k < SOLAR_DAYS ? f->wh[k] : SOLAR_WH_NONE;
}

uint32_t solar_left_wh(const solar_forecast_t *f, int32_t day, int quarter, int seconds)
{
    const uint16_t *q = solar_day(f, day);
    if (q == NULL || quarter < 0 || quarter >= SOLAR_STEPS) {
        return SOLAR_WH_NONE;
    }
    double wh = q[quarter] * (SOLAR_UNIT_W / 4.0) * (QUARTER_S - seconds) / QUARTER_S;
    for (int i = quarter + 1; i < SOLAR_STEPS; i++) {
        wh += q[i] * (SOLAR_UNIT_W / 4.0);
    }
    return (uint32_t)lround(wh);
}

bool solar_peak(const solar_forecast_t *f, int32_t day, uint32_t *w, int *quarter)
{
    const uint16_t *q = solar_day(f, day);
    if (q == NULL) {
        return false;
    }
    int best = 0;
    for (int i = 1; i < SOLAR_STEPS; i++) {
        best = q[i] > q[best] ? i : best;
    }
    *w = (uint32_t)q[best] * SOLAR_UNIT_W;
    *quarter = best;
    return q[best] > 0;
}
