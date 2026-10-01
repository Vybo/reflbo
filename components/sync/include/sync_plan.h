#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * When the next sync runs (spec §9.3): the schedule's modes, quiet hours (D25) and the retries
 * after a failure. Pure C, host-buildable. Local time comes from the TZ environment variable
 * (tzset()), as in the scheduler; a local time that doesn't exist (spring forward) maps to the
 * first valid minute after it, and a repeated one (fall back) runs once, at its first occurrence.
 */

typedef enum { SYNC_MODE_TIMES, SYNC_MODE_INTERVAL, SYNC_MODE_ALWAYS, SYNC_MODE_MANUAL } sync_mode_t;

#define SYNC_TIMES_MAX 8
#define SYNC_ALWAYS_REFRESH_MIN 60 /* weather and air quality in `always` mode */
#define SYNC_RETRY_COUNT 3         /* retries 15, 30 and 60 min after each failure */

typedef struct {
    uint8_t mode;                   /* sync_mode_t */
    uint8_t time_count;             /* SYNC_MODE_TIMES: 1..SYNC_TIMES_MAX */
    uint16_t times[SYNC_TIMES_MAX]; /* minutes after local midnight, ascending, no repeats */
    uint16_t interval_min;          /* SYNC_MODE_INTERVAL: 15..1440, slots aligned to local midnight */
    bool quiet;                     /* quiet hours on */
    uint16_t quiet_from, quiet_to;  /* minutes after local midnight, [from, to), may cross midnight; equal: none */
} sync_schedule_t;

typedef struct {
    time_t failed_at; /* when the last sync failed (UTC); 0 = it succeeded, or none yet */
    uint8_t retries;  /* retries already run since the last failure of a scheduled or on-demand sync */
} sync_history_t;

typedef struct {
    time_t at;  /* UTC; 0 = nothing due */
    bool retry; /* a retry of a failed sync, not a scheduled one */
} sync_due_t;

bool sync_quiet_at(const sync_schedule_t *s, time_t t);
/* The next scheduled sync strictly after `after`: a time, an interval slot, or in `always` mode the
 * hourly refresh (aligned like interval 60); one inside quiet hours moves to their end. 0 in `manual`. */
time_t sync_next_scheduled(const sync_schedule_t *s, time_t after);
/* The next automatic sync after a sync ended (or at boot / after a settings change) at `now`: the
 * scheduled one, or the next retry if that comes first: 15 min after the first failure, then 30 min
 * after the first retry failed, then 60 min after the second; none after the third, none when
 * `low_battery`, none in `manual` mode. A retry inside quiet hours moves to their end too. A retry
 * that is already overdue, after a restart, is due at `now`. */
sync_due_t sync_next_due(const sync_schedule_t *s, const sync_history_t *h, time_t now, bool low_battery);
/* After a sync ended at `ended`: `due` is what started it ({0, false} for one on demand), `ok` whether
 * every step passed. Success clears the history; a failed retry counts up; any other failure starts
 * the retries over. */
void sync_history_record(sync_history_t *h, sync_due_t due, bool ok, time_t ended);
/* Spec §9.3 expected interval: the longest gap between consecutive scheduled syncs over the local day
 * of `now` and the next, quiet hours included; 0 in `manual` mode (data never expires). */
uint32_t sync_expected_interval_s(const sync_schedule_t *s, time_t now);
/* `always` mode wants Wi-Fi at `t`: on unless quiet hours; false in the other modes. */
bool sync_wifi_wanted(const sync_schedule_t *s, time_t t);
/* Settings text: "HH:MM" <-> minutes after midnight; -1 for anything else. */
int sync_parse_hhmm(const char *text);
