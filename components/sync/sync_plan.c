#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include "sync_plan.h"

#include "scheduler.h"

#define DAY_MIN 1440
#define INTERVAL_MIN 15
#define ALL_DAYS 0x7F

static const int k_retry_min[SYNC_RETRY_COUNT] = { 15, 30, 60 };

/* The local calendar date `days` after the one `t` falls on. */
static bool local_date(time_t t, int days, int *y, int *mo, int *d)
{
    struct tm local;
    if (localtime_r(&t, &local) == NULL) {
        return false;
    }
    struct tm noon = { .tm_year = local.tm_year, .tm_mon = local.tm_mon, .tm_mday = local.tm_mday + days,
                       .tm_hour = 12, .tm_isdst = -1 };
    mktime(&noon); /* normalises the date */
    *y = noon.tm_year + 1900;
    *mo = noon.tm_mon + 1;
    *d = noon.tm_mday;
    return true;
}

static int local_minute(time_t t)
{
    struct tm local;
    return localtime_r(&t, &local) != NULL ? local.tm_hour * 60 + local.tm_min : -1;
}

uint32_t sync_quiet_span_s(const sync_schedule_t *s)
{
    if (!s->quiet || s->quiet_from == s->quiet_to || s->quiet_from >= DAY_MIN || s->quiet_to >= DAY_MIN) {
        return 0;
    }
    return (uint32_t)((s->quiet_to - s->quiet_from + DAY_MIN) % DAY_MIN) * 60;
}

bool sync_quiet_at(const sync_schedule_t *s, time_t t)
{
    if (!s->quiet || s->quiet_from == s->quiet_to || s->quiet_from >= DAY_MIN || s->quiet_to >= DAY_MIN) {
        return false;
    }
    int m = local_minute(t);
    if (m < 0) {
        return false;
    }
    return s->quiet_from < s->quiet_to ? m >= s->quiet_from && m < s->quiet_to
                                       : m >= s->quiet_from || m < s->quiet_to; /* across midnight */
}

/* A sync due at `t` inside quiet hours waits for their end. Every moment between `t` and that end
 * is quiet too, so moving the earliest candidate is enough. */
static time_t after_quiet(const sync_schedule_t *s, time_t t)
{
    return t != 0 && sync_quiet_at(s, t) ? sched_next_weekly(t, s->quiet_to, ALL_DAYS) : t;
}

static time_t next_time(const sync_schedule_t *s, time_t after)
{
    time_t best = 0;
    for (int i = 0; i < s->time_count && i < SYNC_TIMES_MAX; i++) {
        time_t t = s->times[i] < DAY_MIN ? sched_next_weekly(after, s->times[i], ALL_DAYS) : 0;
        if (t != 0 && (best == 0 || t < best)) {
            best = t;
        }
    }
    return best;
}

/* The first slot every `every` minutes from local midnight strictly after `after`. A slot the clock
 * skips (spring forward) maps to the first minute after the gap; a repeated one (fall back) runs
 * once, at its first occurrence, as sched_local_to_utc() maps it. */
static time_t next_slot(time_t after, int every)
{
    int m = local_minute(after);
    for (int day = 0; day <= 2 && m >= 0; day++) {
        int y, mo, d;
        if (!local_date(after, day, &y, &mo, &d)) {
            return 0;
        }
        for (int slot = day == 0 ? m / every * every : 0; slot < DAY_MIN; slot += every) {
            time_t t = sched_local_to_utc(y, mo, d, slot);
            if (t > after) {
                return t;
            }
        }
    }
    return 0;
}

static int interval_of(const sync_schedule_t *s)
{
    int n = s->interval_min;
    return n < INTERVAL_MIN ? INTERVAL_MIN : n > DAY_MIN ? DAY_MIN : n;
}

time_t sync_next_scheduled(const sync_schedule_t *s, time_t after)
{
    time_t t;
    switch (s->mode) {
    case SYNC_MODE_TIMES:
        t = next_time(s, after);
        break;
    case SYNC_MODE_INTERVAL:
        t = next_slot(after, interval_of(s));
        break;
    case SYNC_MODE_ALWAYS:
        t = next_slot(after, SYNC_ALWAYS_REFRESH_MIN);
        break;
    default:
        return 0; /* manual */
    }
    return after_quiet(s, t);
}

sync_due_t sync_next_due(const sync_schedule_t *s, const sync_history_t *h, time_t now, bool low_battery)
{
    sync_due_t due = { .at = sync_next_scheduled(s, now) };
    if (s->mode == SYNC_MODE_MANUAL || low_battery || h->failed_at == 0 || h->retries >= SYNC_RETRY_COUNT) {
        return due;
    }
    time_t base = h->failed_at < now ? h->failed_at : now; /* a clock moved back counts from now */
    time_t retry = base + (time_t)k_retry_min[h->retries] * 60;
    retry = after_quiet(s, retry < now ? now : retry); /* overdue, after a restart: now */
    if (due.at == 0 || retry < due.at) { /* a tie is the scheduled sync */
        due = (sync_due_t){ .at = retry, .retry = true };
    }
    return due;
}

sync_need_t sync_need(bool clock_valid, bool always_wifi_off, bool have_forecast, bool weather_on)
{
    return !clock_valid                   ? SYNC_NEED_TIME
           : always_wifi_off              ? SYNC_NEED_WIFI
           : !have_forecast && weather_on ? SYNC_NEED_FORECAST
                                          : SYNC_NEED_NOTHING;
}

sync_due_t sync_next_due_needing(const sync_schedule_t *s, const sync_history_t *h, time_t now, bool low_battery,
                                 sync_need_t need)
{
    if (need == SYNC_NEED_TIME) {
        if (h->failed_at == 0) {
            return (sync_due_t){ .at = now };
        }
        time_t base = h->failed_at < now ? h->failed_at : now; /* a clock moved back counts from now */
        int step = h->retries < SYNC_RETRY_COUNT ? h->retries : SYNC_RETRY_COUNT - 1;
        time_t retry = base + (time_t)k_retry_min[step] * 60;
        return (sync_due_t){ .at = retry < now ? now : retry, .retry = true };
    }
    if ((need == SYNC_NEED_FORECAST || need == SYNC_NEED_WIFI) && s->mode != SYNC_MODE_MANUAL && h->failed_at == 0) {
        return (sync_due_t){ .at = now };
    }
    return sync_next_due(s, h, now, low_battery);
}

int sync_budget_ms(int64_t now_us, int64_t deadline_us, int step_max_ms)
{
    int64_t left_ms = (deadline_us - now_us) / 1000;
    if (left_ms < SYNC_STEP_MIN_MS) {
        return 0;
    }
    return left_ms < step_max_ms ? (int)left_ms : step_max_ms;
}

void sync_history_record(sync_history_t *h, sync_due_t due, bool ok, time_t ended)
{
    if (ok) {
        *h = (sync_history_t){ 0 };
        return;
    }
    h->retries = !due.retry ? 0 : h->retries < SYNC_RETRY_COUNT ? h->retries + 1 : SYNC_RETRY_COUNT;
    h->failed_at = ended;
}

uint32_t sync_expected_interval_s(const sync_schedule_t *s, time_t now)
{
    int y, mo, d;
    if (s->mode != SYNC_MODE_TIMES && s->mode != SYNC_MODE_INTERVAL && s->mode != SYNC_MODE_ALWAYS) {
        return 0; /* manual: data never expires */
    }
    if (!local_date(now, 0, &y, &mo, &d)) {
        return 0;
    }
    time_t start = sched_local_to_utc(y, mo, d, 0);
    if (!local_date(now, 2, &y, &mo, &d)) {
        return 0;
    }
    time_t end = sched_local_to_utc(y, mo, d, 0);
    time_t prev = 0, longest = 0;
    for (time_t t = sync_next_scheduled(s, start - 1); t != 0 && t < end; t = sync_next_scheduled(s, t)) {
        if (prev != 0 && t - prev > longest) {
            longest = t - prev;
        }
        prev = t;
    }
    return prev == 0 ? 0 : longest > 0 ? (uint32_t)longest : 86400; /* nothing scheduled: nothing expires */
}

bool sync_wifi_wanted(const sync_schedule_t *s, time_t t)
{
    return s->mode == SYNC_MODE_ALWAYS && !sync_quiet_at(s, t);
}

int sync_parse_hhmm(const char *text)
{
    if (text == NULL) {
        return -1;
    }
    for (int i = 0; i < 5; i++) { /* stops at the terminator of a shorter text */
        if (i == 2 ? text[i] != ':' : text[i] < '0' || text[i] > '9') {
            return -1;
        }
    }
    int h = (text[0] - '0') * 10 + (text[1] - '0'), m = (text[3] - '0') * 10 + (text[4] - '0');
    return text[5] == '\0' && h < 24 && m < 60 ? h * 60 + m : -1;
}

time_t sync_radar_next(time_t after, uint32_t step_s)
{
    time_t t = after - SYNC_RADAR_DELAY_S;
    return t - t % (time_t)step_s + (time_t)step_s + SYNC_RADAR_DELAY_S;
}

bool sync_report_failed(const uint8_t result[SYNC_STEP_COUNT])
{
    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
        if (result[i] == SYNC_STEP_FAILED && i != SYNC_STEP_ENERGY && i != SYNC_STEP_MQTT) {
            return true;
        }
    }
    return false;
}

int sync_first_failed(const uint8_t result[SYNC_STEP_COUNT])
{
    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
        if (result[i] == SYNC_STEP_FAILED) {
            return i;
        }
    }
    return SYNC_STEP_COUNT;
}
