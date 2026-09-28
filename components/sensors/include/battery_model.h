#pragma once

#include <stdint.h>

/*
 * Battery gauge logic (spec §8): open-circuit-voltage curve, smoothing and inferred charging
 * state. Pure C, host-buildable; the ADC reading happens in battery.c.
 */

#define BATTERY_HISTORY 8 /* samples kept for charging inference, at least 4.5 min apart */

typedef enum {
    BATTERY_UNKNOWN,     /* less than 30 min of history */
    BATTERY_DISCHARGING,
    BATTERY_CHARGING,    /* rose >= 30 mV over the last 30 min */
    BATTERY_FULL,        /* >= 4150 mV and steady */
} battery_state_t;

typedef struct {
    uint32_t time_s;
    uint16_t mv;
} battery_point_t;

#define BATTERY_LEVEL_HISTORY 25 /* hourly levels for the days-left estimate: a day's worth */

typedef struct {
    uint32_t time_s;
    uint16_t pct10; /* 0.1 % */
} battery_level_point_t;

typedef struct {
    uint32_t ema_mv16;  /* smoothed voltage, mV x 16; 0 = no sample yet */
    uint8_t level;      /* %, never rises unless charging or full */
    uint8_t count;      /* valid history points */
    uint8_t head;       /* next history slot */
    battery_point_t history[BATTERY_HISTORY];
    battery_level_point_t levels[BATTERY_LEVEL_HISTORY]; /* restart whenever charging is seen */
    uint8_t level_count;
    uint8_t level_head;
} battery_gauge_t;

int battery_percent_from_mv(int mv);   /* OCV table, 0..100 */
int battery_percent10_from_mv(int mv); /* the same in 0.1 %, 0..1000 */
void battery_gauge_init(battery_gauge_t *g);
void battery_gauge_add(battery_gauge_t *g, uint32_t now_s, int mv);
int battery_gauge_mv(const battery_gauge_t *g);    /* smoothed; 0 before the first sample */
int battery_gauge_level(const battery_gauge_t *g); /* %; -1 before the first sample */
battery_state_t battery_gauge_state(const battery_gauge_t *g, uint32_t now_s);
/* Days of battery left, in 0.1 days: the level divided by the discharge rate over up to the
 * last 24 h (spec §5.1 `bat.days`). -1 unless discharging with at least 6 h of history. */
int battery_gauge_days_left10(const battery_gauge_t *g, uint32_t now_s);
/* Moves every stored timestamp by delta_s. Call it when the clock is set, so the history keeps
 * its spacing on the new clock; a board without the RTC cell starts in 2000 (spec §7). A clock
 * that moves back without it restarts the history instead. */
void battery_gauge_shift_time(battery_gauge_t *g, int64_t delta_s);
