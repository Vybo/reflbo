#include "util_ticks.h"

uint32_t util_ticks_at_least(uint32_t ms, uint32_t tick_period_ms)
{
    if (ms == 0) {
        return 0;
    }
    return (ms + tick_period_ms - 1) / tick_period_ms + 1;
}
