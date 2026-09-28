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

typedef struct {
    uint32_t ema_mv16;  /* smoothed voltage, mV x 16; 0 = no sample yet */
    uint8_t level;      /* %, never rises unless charging or full */
    uint8_t count;      /* valid history points */
    uint8_t head;       /* next history slot */
    battery_point_t history[BATTERY_HISTORY];
} battery_gauge_t;

int battery_percent_from_mv(int mv); /* OCV table, 0..100 */
void battery_gauge_init(battery_gauge_t *g);
void battery_gauge_add(battery_gauge_t *g, uint32_t now_s, int mv);
int battery_gauge_mv(const battery_gauge_t *g);    /* smoothed; 0 before the first sample */
int battery_gauge_level(const battery_gauge_t *g); /* %; -1 before the first sample */
battery_state_t battery_gauge_state(const battery_gauge_t *g, uint32_t now_s);
