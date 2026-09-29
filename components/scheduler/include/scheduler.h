#pragma once

#include <stdbool.h>
#include <time.h>

/*
 * Wake scheduler (spec §9.2): when to wake next and why. Pure C, host-buildable. Periodic jobs run
 * at local wall-clock slots (minute of day divisible by their period), so DST shifts nothing.
 * Local time comes from the TZ environment variable (tzset()). Preset cycling and the seconds
 * display add wakes between minutes; M5 adds syncs, M7 user alarms.
 */

typedef enum {
    SCHED_DISPLAY = 1u << 0, /* display update */
    SCHED_SENSORS = 1u << 1, /* SHTC3 and battery sample */
    SCHED_CYCLE = 1u << 2,   /* the next auto-cycle preset switch */
    SCHED_SECOND = 1u << 3,  /* the active preset shows seconds */
    SCHED_ENTRY = 1u << 4,   /* a schedule entry (spec §5.4) */
} sched_reason_t;

typedef struct {
    time_t now;            /* UTC seconds */
    int display_every_min; /* 1..15 */
    int sensors_every_min; /* 1..30 */
    time_t cycle_at;       /* next auto-cycle switch (UTC); 0 = cycling is off */
    bool every_second;
    time_t schedule_at;    /* next schedule entry (UTC, on a minute); 0 = none */
} sched_input_t;

typedef struct {
    time_t when;      /* UTC; the earliest wake, after `now` */
    unsigned reasons; /* sched_reason_t bits due at `when` */
    time_t alarm;     /* the next minute-aligned wake (display, sensors or an entry), for the RTC alarm */
} sched_wake_t;

sched_wake_t scheduler_next_wake(const sched_input_t *in);
/* True if `t` is a local slot of a job that runs every `every_min` minutes. */
int scheduler_is_slot(time_t t, int every_min);
/* When the local clock first reads `minute` (after midnight) on a local date (spec §9.2): a time
 * that doesn't exist (spring forward) maps to the first valid minute after it, and a repeated one
 * (fall back) to its first occurrence. */
time_t sched_local_to_utc(int year, int month, int day, int minute);
/* The first time strictly after `after` when the local clock reads `at_min` on one of `days`
 * (bit 0 Monday ... bit 6 Sunday); 0 if `days` is empty. */
time_t sched_next_weekly(time_t after, int at_min, unsigned days);
