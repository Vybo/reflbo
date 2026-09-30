#pragma once

#include <stdbool.h>
#include <stdint.h>

/* When the running clock takes the RTC's time (spec §7). Pure C, host-buildable. */

/* Whether the system clock should be set to `rtc_s`, the RTC's second, with no fraction.
 * `at_edge`: the RTC's minute alarm woke us, so its second has only just begun. */
bool timekeeping_rtc_resync(int64_t system_s, int64_t rtc_s, bool at_edge);
