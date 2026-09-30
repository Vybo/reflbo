#include "battery_model.h"

#include <stdbool.h>
#include <stddef.h>

#define HISTORY_SPACING_S 270  /* at most one history point per 4.5 min; extra samples are skipped */
#define TREND_WINDOW_S    1800 /* 30 min */
#define CHARGE_RISE_MV    30
#define FULL_MV           4150
#define STEADY_MV         10
#define LEVEL_SPACING_S   3600  /* one days-left point per hour */
#define DAYS_MIN_SPAN_S   21600 /* 6 h of discharge before estimating */
#define DAYS_MAX_SPAN_S   90000 /* levels older than a day (plus one spacing) belong to another clock */

/* Right after a point is added, the oldest one must already be a full window old, at any sample
 * interval, or the state falls back to UNKNOWN until the next point arrives. */
_Static_assert((BATTERY_HISTORY - 1) * HISTORY_SPACING_S >= TREND_WINDOW_S, "history too short for the trend window");

/* Li-ion open-circuit voltage vs state of charge, NCR18650B-like (approximate; tune with data). */
static const struct {
    uint16_t mv;
    uint8_t pct;
} k_ocv[] = {
    { 3270, 0 },  { 3610, 5 },  { 3690, 10 }, { 3710, 15 }, { 3730, 20 }, { 3750, 25 }, { 3770, 30 },
    { 3790, 35 }, { 3800, 40 }, { 3820, 45 }, { 3840, 50 }, { 3850, 55 }, { 3870, 60 }, { 3910, 65 },
    { 3950, 70 }, { 3980, 75 }, { 4020, 80 }, { 4080, 85 }, { 4110, 90 }, { 4150, 95 }, { 4200, 100 },
};
#define OCV_POINTS (sizeof(k_ocv) / sizeof(k_ocv[0]))

int battery_percent_from_mv(int mv)
{
    if (mv <= k_ocv[0].mv) {
        return 0;
    }
    for (unsigned i = 1; i < OCV_POINTS; i++) {
        if (mv <= k_ocv[i].mv) {
            int dv = k_ocv[i].mv - k_ocv[i - 1].mv;
            int dp = k_ocv[i].pct - k_ocv[i - 1].pct;
            return k_ocv[i - 1].pct + ((mv - k_ocv[i - 1].mv) * dp + dv / 2) / dv;
        }
    }
    return 100;
}

int battery_percent10_from_mv(int mv)
{
    if (mv <= k_ocv[0].mv) {
        return 0;
    }
    for (unsigned i = 1; i < OCV_POINTS; i++) {
        if (mv <= k_ocv[i].mv) {
            int dv = k_ocv[i].mv - k_ocv[i - 1].mv;
            int dp = k_ocv[i].pct - k_ocv[i - 1].pct;
            return k_ocv[i - 1].pct * 10 + ((mv - k_ocv[i - 1].mv) * dp * 10 + dv / 2) / dv;
        }
    }
    return 1000;
}

/* `mv` moved onto the built-in curve's own span, where a manual calibration puts it. */
static int curve_mv(const battery_cal_t *cal, int mv)
{
    if (cal == NULL || cal->method != BATTERY_CAL_MANUAL || !battery_cal_valid(cal)) {
        return mv;
    }
    int span = cal->full_mv - cal->empty_mv, rel = mv - cal->empty_mv;
    return BATTERY_EMPTY_MV + (rel * (BATTERY_FULL_MV - BATTERY_EMPTY_MV) + (rel >= 0 ? span / 2 : -span / 2)) / span;
}

int battery_level10(const battery_cal_t *cal, int mv)
{
    return battery_percent10_from_mv(curve_mv(cal, mv));
}

bool battery_cal_valid(const battery_cal_t *cal)
{
    if (cal->method != BATTERY_CAL_MANUAL) {
        return cal->method == BATTERY_CAL_CURVE;
    }
    return cal->empty_mv >= 3000 && cal->empty_mv <= 4000 && cal->full_mv >= 3600 && cal->full_mv <= 4400 &&
           cal->full_mv - cal->empty_mv >= BATTERY_CAL_SPAN_MIN;
}

void battery_gauge_init(battery_gauge_t *g)
{
    *g = (battery_gauge_t){ .cal = { .method = BATTERY_CAL_CURVE, .empty_mv = BATTERY_EMPTY_MV,
                                     .full_mv = BATTERY_FULL_MV } };
}

void battery_gauge_set_cal(battery_gauge_t *g, const battery_cal_t *cal)
{
    g->cal = *cal;
    g->level_count = 0;
    g->level_head = 0;
    if (g->ema_mv16 != 0) {
        g->level = (uint8_t)battery_percent_from_mv(curve_mv(&g->cal, battery_gauge_mv(g)));
    }
}

static const battery_point_t *newest(const battery_gauge_t *g)
{
    return &g->history[(g->head + BATTERY_HISTORY - 1) % BATTERY_HISTORY];
}

/* The newest point at least TREND_WINDOW_S older than `now`, or NULL. */
static const battery_point_t *window_start(const battery_gauge_t *g, uint32_t now_s)
{
    for (unsigned k = 0; k < g->count; k++) {
        const battery_point_t *p = &g->history[(g->head + BATTERY_HISTORY - 1 - k) % BATTERY_HISTORY];
        if (now_s - p->time_s >= TREND_WINDOW_S) {
            return p;
        }
    }
    return NULL;
}

battery_state_t battery_gauge_state(const battery_gauge_t *g, uint32_t now_s)
{
    if (g->ema_mv16 == 0) {
        return BATTERY_UNKNOWN;
    }
    const battery_point_t *start = window_start(g, now_s);
    if (start == NULL) {
        return BATTERY_UNKNOWN;
    }
    int now_mv = battery_gauge_mv(g);
    int rise = now_mv - start->mv;
    if (now_mv >= FULL_MV && rise < STEADY_MV && rise > -STEADY_MV) {
        return BATTERY_FULL;
    }
    return rise >= CHARGE_RISE_MV ? BATTERY_CHARGING : BATTERY_DISCHARGING;
}

void battery_gauge_add(battery_gauge_t *g, uint32_t now_s, int mv)
{
    if (mv < 1) {
        mv = 1; /* ema_mv16 == 0 means "no sample yet" */
    }
    const battery_level_point_t *last_level =
        g->level_count ? &g->levels[(g->level_head + BATTERY_LEVEL_HISTORY - 1) % BATTERY_LEVEL_HISTORY] : NULL;
    if ((g->count && now_s < newest(g)->time_s) || (last_level != NULL && now_s < last_level->time_s)) {
        g->count = 0; /* the clock moved back unshifted: the spacing and windows can't use that history */
        g->head = 0;
        g->level_count = 0;
        g->level_head = 0;
    }
    bool first = g->ema_mv16 == 0;
    if (first) {
        g->ema_mv16 = (uint32_t)mv * 16u;
    } else {
        int32_t ema = (int32_t)g->ema_mv16;
        ema += ((int32_t)mv * 16 - ema) / 4; /* alpha = 1/4 per sample */
        g->ema_mv16 = (uint32_t)ema;
    }
    if (g->count == 0 || now_s - newest(g)->time_s >= HISTORY_SPACING_S) {
        g->history[g->head] = (battery_point_t){ .time_s = now_s, .mv = (uint16_t)battery_gauge_mv(g) };
        g->head = (uint8_t)((g->head + 1) % BATTERY_HISTORY);
        if (g->count < BATTERY_HISTORY) {
            g->count++;
        }
    }
    int pct = battery_percent_from_mv(curve_mv(&g->cal, battery_gauge_mv(g)));
    battery_state_t state = battery_gauge_state(g, now_s);
    if (first || state == BATTERY_CHARGING || state == BATTERY_FULL || pct < g->level) {
        g->level = (uint8_t)pct;
    }

    if (state == BATTERY_CHARGING || state == BATTERY_FULL) {
        g->level_count = 0; /* the discharge rate starts over once the charger lets go */
        return;
    }
    const battery_level_point_t *last =
        g->level_count ? &g->levels[(g->level_head + BATTERY_LEVEL_HISTORY - 1) % BATTERY_LEVEL_HISTORY] : NULL;
    if (last == NULL || now_s - last->time_s >= LEVEL_SPACING_S) {
        g->levels[g->level_head] =
            (battery_level_point_t){ .time_s = now_s, .pct10 = (uint16_t)battery_level10(&g->cal, battery_gauge_mv(g)) };
        g->level_head = (uint8_t)((g->level_head + 1) % BATTERY_LEVEL_HISTORY);
        if (g->level_count < BATTERY_LEVEL_HISTORY) {
            g->level_count++;
        }
    }
}

int battery_gauge_days_left10(const battery_gauge_t *g, uint32_t now_s)
{
    if (g->level_count == 0 || battery_gauge_state(g, now_s) != BATTERY_DISCHARGING) {
        return -1;
    }
    const battery_level_point_t *oldest = NULL; /* the oldest level from the last day */
    for (unsigned k = g->level_count; k > 0 && oldest == NULL; k--) {
        const battery_level_point_t *p = &g->levels[(g->level_head + BATTERY_LEVEL_HISTORY - k) % BATTERY_LEVEL_HISTORY];
        if (p->time_s <= now_s && now_s - p->time_s <= DAYS_MAX_SPAN_S) {
            oldest = p;
        }
    }
    if (oldest == NULL) {
        return -1;
    }
    uint32_t span = now_s - oldest->time_s;
    int now_pct10 = battery_level10(&g->cal, battery_gauge_mv(g));
    int drop = oldest->pct10 - now_pct10;
    if (span < DAYS_MIN_SPAN_S || drop <= 0) {
        return -1;
    }
    int64_t days10 = (int64_t)now_pct10 * span * 10 / ((int64_t)drop * 86400);
    return days10 > 9999 ? 9999 : (int)days10;
}

static uint32_t shifted(uint32_t t, int64_t delta_s)
{
    int64_t v = (int64_t)t + delta_s;
    return v < 0 ? 0 : v > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)v;
}

void battery_gauge_shift_time(battery_gauge_t *g, int64_t delta_s)
{
    for (unsigned i = 0; i < BATTERY_HISTORY; i++) {
        g->history[i].time_s = shifted(g->history[i].time_s, delta_s);
    }
    for (unsigned i = 0; i < BATTERY_LEVEL_HISTORY; i++) {
        g->levels[i].time_s = shifted(g->levels[i].time_s, delta_s);
    }
}

int battery_gauge_mv(const battery_gauge_t *g)
{
    return (int)((g->ema_mv16 + 8) / 16);
}

int battery_gauge_level(const battery_gauge_t *g)
{
    return g->ema_mv16 == 0 ? -1 : g->level;
}

bool battery_critical(bool was_critical, int mv, battery_state_t state)
{
    bool charging = state == BATTERY_CHARGING || state == BATTERY_FULL;
    if (mv <= 0) {
        return was_critical;
    }
    if (was_critical) {
        return !charging && mv < BATTERY_RECOVER_MV;
    }
    return !charging && mv <= BATTERY_CRITICAL_MV;
}
