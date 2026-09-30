#include "timekeeping_sync.h"

bool timekeeping_rtc_resync(int64_t system_s, int64_t rtc_s, bool at_edge)
{
    /* Otherwise the running clock keeps its fraction of a second. Setting it to the RTC's whole
     * second at every read lost each read's latency, which added up to a skipped second (M3a
     * review). It is ahead by up to a second while the RTC's second hasn't turned yet. */
    int64_t ahead = system_s - rtc_s;
    return at_edge || ahead < 0 || ahead > 1;
}
