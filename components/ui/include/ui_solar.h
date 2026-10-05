#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "energy.h"
#include "gfx.h"
#include "solar.h"
#include "ui_fields.h"

/*
 * The solar view (spec §11.5, §11.6, M6d): what the app gathers for the pv.* and energy.* fields, and the
 * sample day the goldens and `solar demo` show. Pure C, host-buildable.
 */

#define UI_SOLAR_IDLE_W 20 /* less than this flows nowhere: no arrow, a dotted line (spec §11.6) */

struct ui_solar {
    const solar_forecast_t *forecast; /* the PV forecast; NULL, or day 0, before the first */
    uint32_t forecast_ttl_s;          /* fresh this long after it came; 0: never stale (sync mode `manual`) */
    const energy_reading_t *reading;  /* the house's last reading; NULL, or at 0, before the first */
    const energy_day_t *day;          /* today's totals and quarter hours, from the readings */
    bool battery;                     /* the battery shows (energy.battery, spec §11.6) */
};

/* The Solar layout under the status bar in `a` (spec §11.5): today's total, now, the peak and what is still to
 * come; the day's chart; the next two days' totals with their weather. "No solar forecast yet" without today's
 * quarter hours. Returns whether what it shows is stale, for the status bar's warning. */
bool ui_draw_solar_layout(gfx_fb_t *fb, gfx_rect_t a, const ui_context_t *ctx);
/* The Energy layout (spec §11.6): the reading's time, the panels above a junction, the grid left, the house right,
 * the battery below when it shows; today's totals. "No data from the inverter yet" before the first reading.
 * Returns whether the reading is stale. */
bool ui_draw_energy_layout(gfx_fb_t *fb, gfx_rect_t a, const ui_context_t *ctx);

/* The sample day (spec §15): a 5.2 kWp roof facing south on local day `day`, whose midnight is `midnight` (UTC),
 * its forecast fetched at 05:48: a clear morning with cloud passing in the afternoon, tomorrow overcast, the day
 * after sunny. With `live`, the inverter's readings until `now`: a misty start, then a little above the forecast,
 * and the house drawing 0.86 kW, exporting the rest; with `battery`, a battery at 64 % charging at 1.2 kW. */
void ui_solar_demo(int32_t day, time_t midnight, time_t now, bool live, bool battery, solar_forecast_t *forecast,
                   energy_reading_t *reading, energy_day_t *energy_day);
