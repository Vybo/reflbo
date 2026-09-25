#pragma once

#include <time.h>

/*
 * Wake scheduler (spec §9.2): when to wake next and why. Pure C, host-buildable. Periodic jobs run
 * at local wall-clock slots (minute of day divisible by their period), so DST shifts nothing.
 * Local time comes from the TZ environment variable (tzset()). M3+ adds cycle switches and
 * timeouts, M5 syncs and M7 user alarms.
 */

typedef enum {
    SCHED_DISPLAY = 1u << 0, /* display update */
    SCHED_SENSORS = 1u << 1, /* SHTC3 and battery sample */
} sched_reason_t;

typedef struct {
    time_t now;            /* UTC seconds */
    int display_every_min; /* 1..15 */
    int sensors_every_min; /* 1..30 */
} sched_input_t;

typedef struct {
    time_t when;      /* UTC; a whole minute after `now` */
    unsigned reasons; /* sched_reason_t bits due at `when` */
} sched_wake_t;

sched_wake_t scheduler_next_wake(const sched_input_t *in);
/* True if `t` is a local slot of a job that runs every `every_min` minutes. */
int scheduler_is_slot(time_t t, int every_min);
