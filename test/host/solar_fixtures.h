#pragma once

#include <string.h>

#include "context_fixtures.h"
#include "ui_solar.h"

/* The solar view for the ui tests (M6d): the sample day of ui_solar_demo() on the fixture's day, a 5.2 kWp roof
 * facing south in Brno, as the goldens show it. */

#define FIX_MIDNIGHT ((time_t)FIX_DAY * 86400 - 2 * 3600) /* 00:00 CEST */
#define FIX_SOLAR_NOW (FIX_NOW - 7 * 3600 - 28 * 60)      /* 13:20 CEST */

static solar_forecast_t s_fix_forecast;
static energy_reading_t s_fix_reading;
static energy_day_t s_fix_energy_day;
static ui_solar_t s_fix_solar;

/* The sample day until `now`; without `live`, the forecast alone. Fresh for the next sync in `times` mode. */
static inline const ui_solar_t *fixture_solar(time_t now, bool live, bool battery)
{
    ui_solar_demo(FIX_DAY, FIX_MIDNIGHT, now, live, battery, &s_fix_forecast, &s_fix_reading, &s_fix_energy_day);
    s_fix_solar = (ui_solar_t){ .forecast = &s_fix_forecast, .forecast_ttl_s = 26 * 3600, .reading = &s_fix_reading,
                                .day = &s_fix_energy_day, .battery = battery };
    return &s_fix_solar;
}

/* Nothing yet: no forecast, no reading. */
static inline const ui_solar_t *fixture_solar_none(void)
{
    memset(&s_fix_forecast, 0, sizeof(s_fix_forecast));
    memset(&s_fix_reading, 0, sizeof(s_fix_reading));
    energy_day_init(&s_fix_energy_day);
    s_fix_solar = (ui_solar_t){ .forecast = &s_fix_forecast, .reading = &s_fix_reading, .day = &s_fix_energy_day };
    return &s_fix_solar;
}

/* The largest values: two planes of 100 kWp at full power all day, a house drawing 99.99 kW, a battery at 100 %
 * giving 99.99 kW, 199.99 kW to the grid, and a day's totals in MWh. */
static inline const ui_solar_t *fixture_solar_largest(time_t now)
{
    fixture_solar(now, true, true);
    for (int d = 0; d < 2; d++) {
        for (int i = 24; i < 80; i++) {
            s_fix_forecast.q[d][i] = 19999;
        }
        s_fix_forecast.wh[d] = 56 * 19999 * 10 / 4;
    }
    s_fix_forecast.wh[2] = 2799860;
    for (int i = 0; i < ENERGY_STEPS; i++) {
        s_fix_energy_day.q[i] = s_fix_energy_day.q[i] == ENERGY_NONE ? ENERGY_NONE : 19999;
    }
    s_fix_reading.pv_w = 199990;
    s_fix_reading.load_w = 99990;
    s_fix_reading.bat_w = -99990;
    s_fix_reading.grid_w = -199990;
    s_fix_reading.soc = 100;
    s_fix_reading.yield_wh = 1599000;
    s_fix_reading.to_grid_wh = s_fix_energy_day.base_to_wh + 1199000;
    s_fix_reading.from_grid_wh = s_fix_energy_day.base_from_wh + 999000;
    return &s_fix_solar;
}
