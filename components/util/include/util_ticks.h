#pragma once

#include <stdint.h>

/* Tick count for vTaskDelay() that sleeps at least `ms`, for datasheet minimum delays.
 * vTaskDelay(n) can return after n - 1 full ticks because the first tick is partial, and
 * pdMS_TO_TICKS() rounds down, so this rounds up and adds one tick. Pure C, host-buildable. */
uint32_t util_ticks_at_least(uint32_t ms, uint32_t tick_period_ms);
