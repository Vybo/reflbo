#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Button gesture recogniser (spec §5.6). Pure C, host-buildable.
 *
 * Call gesture_update() on every raw edge and whenever gesture_deadline() passes, always with the
 * button's current level. It debounces, times the press and returns at most one gesture per call.
 * Times are milliseconds from any monotonic clock; wrap-around is handled.
 */

#define GESTURE_DEBOUNCE_MS 30
#define GESTURE_DOUBLE_MS   300
#define GESTURE_NO_DEADLINE UINT32_MAX

typedef enum {
    GESTURE_NONE,
    GESTURE_SHORT,
    GESTURE_DOUBLE,
    GESTURE_LONG,
} gesture_t;

typedef struct {
    uint32_t long_ms;    /* hold time for a long press: 1000, or 3000 for BOOT on the dashboard */
    bool double_enabled; /* wait GESTURE_DOUBLE_MS for a second press before reporting a short one */
} gesture_config_t;

typedef struct {
    gesture_config_t config;
    int state;            /* internal: gesture.c */
    bool level;           /* debounced level: true = pressed */
    bool recheck;         /* an edge arrived inside the debounce time */
    bool have_edge;       /* edge_ms is valid */
    uint32_t edge_ms;     /* last accepted edge */
    uint32_t mark_ms;     /* press time (pressed) or release time (waiting for a second press) */
} gesture_recogniser_t;

void gesture_init(gesture_recogniser_t *g, gesture_config_t config);
void gesture_set_config(gesture_recogniser_t *g, gesture_config_t config);
gesture_t gesture_update(gesture_recogniser_t *g, bool pressed, uint32_t now_ms);
/* A press that ended before it could be timed, e.g. the one that woke the chip (spec §3.3). */
gesture_t gesture_tap(gesture_recogniser_t *g, uint32_t now_ms);
uint32_t gesture_deadline(const gesture_recogniser_t *g); /* absolute ms, or GESTURE_NO_DEADLINE */
bool gesture_busy(const gesture_recogniser_t *g);         /* a gesture is still being timed */
