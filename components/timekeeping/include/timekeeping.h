#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

/* System time from the RTC, time zone, manual set (spec §7). Call from the app task only. */

esp_err_t timekeeping_init(const char *tz_posix);
/* Reads the RTC and sets the system time. The time counts as invalid while the RTC's
 * oscillator-stop flag is set, until timekeeping_set_utc(). */
esp_err_t timekeeping_load_from_rtc(void);
bool timekeeping_valid(void);
esp_err_t timekeeping_set_utc(time_t utc);
