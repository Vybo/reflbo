#pragma once

#include <stdbool.h>
#include <time.h>

/*
 * Parses an ISO 8601 date and time for `rtc set`: "YYYY-MM-DDTHH:MM[:SS]" followed by "Z", an
 * offset "+HH:MM" / "-HH:MM", or nothing for local time (the TZ variable). A space may replace the
 * "T". Pure C, host-buildable.
 */
bool timekeeping_parse_iso8601(const char *text, time_t *utc);
