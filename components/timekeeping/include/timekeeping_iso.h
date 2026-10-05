#pragma once

#include <stdbool.h>
#include <time.h>

/*
 * Parses an ISO 8601 date and time for `rtc set` and the providers' replies:
 * "YYYY-MM-DDTHH:MM[:SS[.fff]]" followed by "Z", an offset "+HH:MM" / "-HH:MM", or nothing for local
 * time (the TZ variable). A space may replace the "T"; a fraction of the second is dropped. Pure C,
 * host-buildable.
 */
bool timekeeping_parse_iso8601(const char *text, time_t *utc);
