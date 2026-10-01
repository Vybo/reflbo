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

/* Local date and minute as one number that grows with local time. */
static long long local_key(const struct tm *t)
{
    return (((long long)t->tm_year * 13 + t->tm_mon) * 32 + t->tm_mday) * 1440 + t->tm_hour * 60 + t->tm_min;
}

time_t sched_local_to_utc(int year, int month, int day, int minute)
{
    struct tm want = { .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = minute / 60,
                       .tm_min = minute % 60, .tm_isdst = -1 };
    long long key = local_key(&want);
    struct tm probe = want, local;
    time_t t = mktime(&probe);
    if (localtime_r(&t, &local) != NULL && local_key(&local) == key) {
        time_t earlier = t - 3600; /* fall back: the same reading an hour before is its first occurrence */
        return localtime_r(&earlier, &local) != NULL && local_key(&local) == key ? earlier : t;
    }
    for (time_t s = t - 3 * 3600; s <= t + 3 * 3600; s += 60) { /* the reading doesn't exist: the gap's end */
        if (localtime_r(&s, &local) != NULL && local_key(&local) >= key) {
            return s;
        }
    }
    return t;
}

time_t sched_next_weekly(time_t after, int at_min, unsigned days)
{
    struct tm local;
    if ((days & 0x7F) == 0 || localtime_r(&after, &local) == NULL) {
        return 0;
    }
    for (int d = 0; d <= 8; d++) {
        struct tm day = { .tm_year = local.tm_year, .tm_mon = local.tm_mon, .tm_mday = local.tm_mday + d,
                          .tm_hour = 12, .tm_isdst = -1 };
        mktime(&day); /* normalises the date and sets tm_wday */
        int monday_first = (day.tm_wday + 6) % 7;
        if (!(days & (1u << monday_first))) {
            continue;
        }
        time_t t = sched_local_to_utc(day.tm_year + 1900, day.tm_mon + 1, day.tm_mday, at_min);
        if (t > after) {
            return t;
        }
    }
    return 0;
}

sched_wake_t scheduler_next_wake(const sched_input_t *in)
{
    time_t display = next_slot(in->now, in->display_every_min);
    time_t sensors = next_slot(in->now, in->sensors_every_min);
    time_t entry = in->schedule_at > in->now ? in->schedule_at : 0;
    time_t sync = in->sync_at > in->now ? in->sync_at : 0;
    time_t cycle = in->cycle_at == 0 ? 0 : in->cycle_at > in->now ? in->cycle_at : in->now + 1; /* overdue: now */
    time_t second = in->every_second ? in->now + 1 : 0;

    sched_wake_t wake = { .alarm = display < sensors ? display : sensors };
    if (entry != 0 && entry < wake.alarm) {
        wake.alarm = entry;
    }
    if (sync != 0 && sync < wake.alarm) {
        wake.alarm = sync;
    }
    wake.when = wake.alarm;
    if (cycle != 0 && cycle < wake.when) {
        wake.when = cycle;
    }
    if (second != 0 && second < wake.when) {
        wake.when = second;
    }
    wake.reasons = (display == wake.when ? SCHED_DISPLAY : 0) | (sensors == wake.when ? SCHED_SENSORS : 0) |
                   (cycle == wake.when ? SCHED_CYCLE : 0) | (second == wake.when ? SCHED_SECOND : 0) |
                   (entry == wake.when ? SCHED_ENTRY : 0) | (sync == wake.when ? SCHED_SYNC : 0);
    return wake;
}
