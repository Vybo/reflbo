#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"
#include "timekeeping_trim.h"

/* System time from the RTC, time zone, manual set (spec §7). Call from the app task only. */

esp_err_t timekeeping_init(const char *tz_posix);
/* Reads the RTC and sets the system time from it where they disagree (timekeeping_sync.h);
 * `at_edge`: the RTC's minute alarm woke us, as its second began. The time counts as invalid
 * while the RTC's oscillator-stop flag is set, until timekeeping_set_utc(). */
esp_err_t timekeeping_load_from_rtc(bool at_edge);
bool timekeeping_valid(void);
/* A manual set (menu, `rtc set`, the phone): good to a second, so the RTC trim's next sync only sets
 * the RTC (spec §7). */
esp_err_t timekeeping_set_utc(time_t utc);

/* The RTC trim (spec §7, D25), kept in NVS `sys/rtc_trim`. Loads it and writes the offset to the RTC;
 * call once NVS is up (a cold boot, or the first wake that stays awake). */
esp_err_t timekeeping_trim_start(void);
const rtc_trim_t *timekeeping_trim(void);
/* A sync's time (sync_report_t): the true UTC at a monotonic instant. Times the RTC's error, sets the
 * system clock, trims the RTC when the measurement allows and sets it to the millisecond. Takes up to
 * 2.6 s. `*moved_ms`: how far the system clock moved. */
esp_err_t timekeeping_apply_true_time(int64_t true_utc_us, int64_t mono_us, int64_t *moved_ms);
