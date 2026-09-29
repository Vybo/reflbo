#include "scheduler.h"
#include "ui_preset.h"

time_t ui_schedule_next(const ui_schedule_t *schedule, time_t after, int *index)
{
    time_t best = 0;
    for (int i = 0; i < schedule->count && i < UI_SCHEDULE_MAX; i++) {
        const ui_schedule_entry_t *e = &schedule->entries[i];
        time_t t = sched_next_weekly(after, e->at_min, e->days);
        if (t != 0 && (best == 0 || t < best)) {
            best = t;
            *index = i;
        }
    }
    return best;
}

/* An entry is due at most once: the app checks at every entry's minute, and a clock that jumps
 * restarts the checks (main/app.c), so a span never covers a day. */
int ui_schedule_due(const ui_schedule_t *schedule, time_t after, time_t now, int order[UI_SCHEDULE_MAX])
{
    long long key[UI_SCHEDULE_MAX]; /* the time, with a night after the presets of its minute */
    int n = 0;
    for (int i = 0; i < schedule->count && i < UI_SCHEDULE_MAX; i++) {
        const ui_schedule_entry_t *e = &schedule->entries[i];
        time_t t = sched_next_weekly(after, e->at_min, e->days);
        if (t == 0 || t > now) {
            continue;
        }
        long long k = (long long)t * 2 + (e->action == UI_SCHED_NIGHT);
        int j = n++;
        for (; j > 0 && key[j - 1] > k; j--) { /* equal keys keep their list order */
            key[j] = key[j - 1];
            order[j] = order[j - 1];
        }
        key[j] = k;
        order[j] = i;
    }
    return n;
}
