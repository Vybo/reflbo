#pragma once

#include <stdbool.h>
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

#define BATTERY_CURVE_POINTS 21 /* a learned curve: the voltage at 0 %, 5 %, ... 100 % */

/* How a voltage becomes a level (owner, 2026-09-30). */
typedef enum {
    BATTERY_CAL_CURVE,  /* the built-in Li-ion curve */
    BATTERY_CAL_MANUAL, /* that curve stretched between the owner's empty and full voltages */
    BATTERY_CAL_LEARNED, /* a curve learned from a full discharge (battery_learn.h, D21) */
} battery_cal_method_t;

typedef struct {
    uint8_t method;    /* battery_cal_method_t */
    uint16_t empty_mv; /* 0 %, for BATTERY_CAL_MANUAL */
    uint16_t full_mv;  /* 100 % */
    uint16_t learned_mv[BATTERY_CURVE_POINTS]; /* BATTERY_CAL_LEARNED: the voltage at 0 %, 5 %, ... 100 % */
} battery_cal_t;

#define BATTERY_EMPTY_MV     3270 /* the built-in curve's own ends */
#define BATTERY_FULL_MV      4200
#define BATTERY_CAL_SPAN_MIN 300  /* a manual calibration's voltages are at least this far apart */

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
    battery_cal_t cal;
} battery_gauge_t;

int battery_percent_from_mv(int mv);   /* OCV table, 0..100 */
int battery_percent10_from_mv(int mv); /* the same in 0.1 %, 0..1000 */
/* The level of `mv` in 0.1 % under `cal`; NULL is the built-in curve. */
int battery_level10(const battery_cal_t *cal, int mv);
/* A manual calibration's voltages: empty 3.0-4.0 V, full 3.6-4.4 V, at least 0.3 V apart. A learned
 * curve: rising, within 3.0-4.4 V, and at least 0.3 V from 0 % to 100 %. An unusable one counts as
 * the built-in curve. */
bool battery_cal_valid(const battery_cal_t *cal);
void battery_gauge_init(battery_gauge_t *g);
void battery_gauge_add(battery_gauge_t *g, uint32_t now_s, int mv);
/* A new calibration: the level follows it at once, and the days-left estimate starts over, as its
 * history is in the old levels. battery_gauge_init() starts with the built-in curve. */
void battery_gauge_set_cal(battery_gauge_t *g, const battery_cal_t *cal);
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

#define BATTERY_CRITICAL_MV 3300 /* spec §8: at or below, the critical screen */
#define BATTERY_RECOVER_MV  3400 /* the critical screen stays up until then, or until charging */
/* Whether the battery is critical after a reading of `mv` (smoothed), given whether it was before:
 * spec §8 with some hysteresis, so the screen doesn't flicker at the threshold. 0 mV means no
 * reading yet and changes nothing. */
bool battery_critical(bool was_critical, int mv, battery_state_t state);
