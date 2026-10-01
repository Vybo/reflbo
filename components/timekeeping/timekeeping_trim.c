#include "timekeeping_trim.h"

#define TRIM_VERSION 1

/* a / b to the nearest whole number, halves away from zero; b > 0. */
static int64_t div_round(int64_t a, int64_t b)
{
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

void timekeeping_trim_init(rtc_trim_t *t)
{
    *t = (rtc_trim_t){ .offset = 0, .set_at_ms = 0, .drift_ppb = TRIM_NO_DRIFT };
}

bool timekeeping_trim_measure(rtc_trim_t *t, int64_t now_ms, int64_t rtc_error_ms)
{
    int64_t span = now_ms - t->set_at_ms;
    if (t->set_at_ms == 0 || span < (int64_t)TRIM_MIN_SPAN_S * 1000) {
        return false; /* the correction's two-hour swing would weigh too much */
    }
    const int64_t limit = INT64_MAX / 1000000000; /* keeps error × 10⁹ in range */
    int64_t error = rtc_error_ms > limit ? limit : rtc_error_ms < -limit ? -limit : rtc_error_ms;
    int64_t drift = div_round(error * 1000000000, span);
    drift = drift > INT32_MAX ? INT32_MAX : drift <= INT32_MIN ? INT32_MIN + 1 : drift; /* MIN means none */
    t->drift_ppb = (int32_t)drift;
    if (drift > TRIM_MAX_PPB || drift < -TRIM_MAX_PPB) {
        return false;
    }
    /* The offset in effect already slowed the clock by offset × one step: add that back. */
    int64_t steps = div_round(drift + (int64_t)t->offset * TRIM_STEP_PPB, TRIM_STEP_PPB);
    steps = steps < TRIM_OFFSET_MIN ? TRIM_OFFSET_MIN : steps > TRIM_OFFSET_MAX ? TRIM_OFFSET_MAX : steps;
    int8_t offset = (int8_t)steps;
    bool changed = offset != t->offset;
    t->offset = offset;
    return changed;
}

void timekeeping_trim_set(rtc_trim_t *t, int64_t now_ms)
{
    t->set_at_ms = now_ms;
}

void timekeeping_trim_forget(rtc_trim_t *t)
{
    t->set_at_ms = 0;
}

void timekeeping_trim_pack(const rtc_trim_t *t, uint8_t out[TRIM_RECORD_LEN])
{
    uint64_t at = (uint64_t)t->set_at_ms;
    uint32_t drift = (uint32_t)t->drift_ppb;
    out[0] = TRIM_VERSION;
    out[1] = (uint8_t)t->offset;
    for (int i = 0; i < 8; i++) {
        out[2 + i] = (uint8_t)(at >> (8 * i));
    }
    for (int i = 0; i < 4; i++) {
        out[10 + i] = (uint8_t)(drift >> (8 * i));
    }
}

bool timekeeping_trim_unpack(rtc_trim_t *t, const uint8_t *in, size_t len)
{
    if (in == NULL || len != TRIM_RECORD_LEN || in[0] != TRIM_VERSION) {
        return false;
    }
    int8_t offset = (int8_t)in[1];
    if (offset < TRIM_OFFSET_MIN || offset > TRIM_OFFSET_MAX) {
        return false;
    }
    uint64_t at = 0;
    uint32_t drift = 0;
    for (int i = 0; i < 8; i++) {
        at |= (uint64_t)in[2 + i] << (8 * i);
    }
    for (int i = 0; i < 4; i++) {
        drift |= (uint32_t)in[10 + i] << (8 * i);
    }
    *t = (rtc_trim_t){ .offset = offset, .set_at_ms = (int64_t)at, .drift_ppb = (int32_t)drift };
    return true;
}

int timekeeping_trim_drift_s10_per_day(const rtc_trim_t *t)
{
    if (t->drift_ppb == TRIM_NO_DRIFT) {
        return 0;
    }
    return (int)div_round((int64_t)t->drift_ppb * 864, 1000000); /* ppb × 86 400 s × 10 / 10⁹ */
}
