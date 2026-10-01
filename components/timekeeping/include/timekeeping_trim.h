#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The RTC trim's arithmetic (spec §7, D25): the PCF85063's Offset register, MODE 0, corrects
 * 4.34 ppm a step, and a positive value slows the clock, so a crystal that runs fast needs a
 * positive offset (datasheet §8.2.3). Each SNTP sync compares the RTC with true time; the drift
 * since the last set to the millisecond, plus what the offset in effect already corrected, is the
 * crystal's own error. Pure C, host-buildable.
 */

#define TRIM_STEP_PPB 4340
#define TRIM_OFFSET_MIN (-64)
#define TRIM_OFFSET_MAX 63
#define TRIM_MIN_SPAN_S (20 * 3600)
#define TRIM_MAX_PPB 270000 /* beyond the register's reach: the clock was set some other way */
#define TRIM_NO_DRIFT INT32_MIN

typedef struct {
    int8_t offset;     /* Offset register steps, in effect since set_at_ms */
    int64_t set_at_ms; /* UTC ms of the last set to the millisecond; 0 = none since a manual set */
    int32_t drift_ppb; /* last measured drift, positive when the RTC ran fast; TRIM_NO_DRIFT if none */
} rtc_trim_t;

void timekeeping_trim_init(rtc_trim_t *t); /* offset 0, nothing measured */
/* At an SNTP sync at true UTC `now_ms` the RTC was `rtc_error_ms` ahead (negative: behind). If it was
 * set to the millisecond at least TRIM_MIN_SPAN_S before, records the drift and picks the offset that
 * cancels the crystal's own error (drift + offset * TRIM_STEP_PPB), rounded and clamped; a drift
 * beyond TRIM_MAX_PPB is recorded but changes nothing. Returns true if the offset changed. */
bool timekeeping_trim_measure(rtc_trim_t *t, int64_t now_ms, int64_t rtc_error_ms);
void timekeeping_trim_set(rtc_trim_t *t, int64_t now_ms); /* the RTC was just set to the millisecond */
void timekeeping_trim_forget(rtc_trim_t *t); /* a manual set: the next sync sets without measuring */
/* The NVS record (sys/rtc_trim): a version byte and the fields, little-endian. */
#define TRIM_RECORD_LEN 14
void timekeeping_trim_pack(const rtc_trim_t *t, uint8_t out[TRIM_RECORD_LEN]);
bool timekeeping_trim_unpack(rtc_trim_t *t, const uint8_t *in, size_t len); /* false: wrong length or version */
/* For display: the drift as seconds a day, tenths (−3.4 s/day → −34); 0 if none. */
int timekeeping_trim_drift_s10_per_day(const rtc_trim_t *t);
