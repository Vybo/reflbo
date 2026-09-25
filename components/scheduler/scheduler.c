#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include "scheduler.h"

int scheduler_is_slot(time_t t, int every_min)
{
    struct tm local;
    if (every_min <= 0 || t % 60 != 0 || localtime_r(&t, &local) == NULL) {
        return 0;
    }
    return (local.tm_hour * 60 + local.tm_min) % every_min == 0;
}

/* First slot strictly after `now`. A slot every <= 1440 min exists within a day plus a DST hour. */
static time_t next_slot(time_t now, int every_min)
{
    time_t t = now - now % 60 + 60;
    for (int i = 0; i < 25 * 60; i++, t += 60) {
        if (scheduler_is_slot(t, every_min)) {
            return t;
        }
    }
    return t;
}

sched_wake_t scheduler_next_wake(const sched_input_t *in)
{
    time_t display = next_slot(in->now, in->display_every_min);
    time_t sensors = next_slot(in->now, in->sensors_every_min);
    sched_wake_t wake = { .when = display < sensors ? display : sensors, .reasons = 0 };
    if (display == wake.when) {
        wake.reasons |= SCHED_DISPLAY;
    }
    if (sensors == wake.when) {
        wake.reasons |= SCHED_SENSORS;
    }
    return wake;
}
