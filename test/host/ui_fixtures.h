#pragma once

#include "ui_clock.h"

/* Fixed inputs for the clock screen's golden renders (test_ui_clock_golden.c, render_clock.c). */

static inline ui_clock_t fixture_clock_valid(void)
{
    ui_clock_t c = { .time_valid = true, .env_valid = true, .temp_c10 = 234, .hum_pct = 45, .battery_valid = true,
                     .battery_pct = 87, .battery_mv = 3921, .battery_state = UI_BATTERY_DISCHARGING };
    c.local.tm_hour = 20;
    c.local.tm_min = 48;
    c.local.tm_wday = 5; /* Friday */
    c.local.tm_mday = 25;
    c.local.tm_mon = 8; /* September */
    c.local.tm_year = 126;
    return c;
}

/* RTC oscillator stopped: "--:--", "Set time", no readings yet (spec §5.3). */
static inline ui_clock_t fixture_clock_invalid(void)
{
    return (ui_clock_t){ .time_valid = false };
}

/* Below zero and charging: the sign of -0.5 °C, the charging arrow, a full battery. */
static inline ui_clock_t fixture_clock_cold(void)
{
    ui_clock_t c = { .time_valid = true, .env_valid = true, .temp_c10 = -5, .hum_pct = 100, .battery_valid = true,
                     .battery_pct = 100, .battery_mv = 4180, .battery_state = UI_BATTERY_CHARGING };
    c.local.tm_hour = 0;
    c.local.tm_min = 5;
    c.local.tm_wday = 0; /* Sunday */
    c.local.tm_mday = 1;
    c.local.tm_mon = 2; /* March */
    c.local.tm_year = 126;
    return c;
}
