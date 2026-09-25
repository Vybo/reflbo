#include "battery_model.h"

#include <stdbool.h>
#include <stddef.h>

#define HISTORY_SPACING_S 240  /* keep one history point per ~5 min sample, skip extra samples */
#define TREND_WINDOW_S    1800 /* 30 min */
#define CHARGE_RISE_MV    30
#define FULL_MV           4150
#define STEADY_MV         10

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

void battery_gauge_init(battery_gauge_t *g)
{
    *g = (battery_gauge_t){ 0 };
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
    int pct = battery_percent_from_mv(battery_gauge_mv(g));
    battery_state_t state = battery_gauge_state(g, now_s);
    if (first || state == BATTERY_CHARGING || state == BATTERY_FULL || pct < g->level) {
        g->level = (uint8_t)pct;
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
