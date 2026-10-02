#pragma once

#include <stdint.h>

#include "adsb.h"
#include "gfx.h"
#include "map_data.h"
#include "radar.h"
#include "ui_fields.h"

/*
 * The radars' views (spec §11.1–§11.3): what the app gathers for them, and the weather radar's map
 * as the Radar layout and the rain.map widget draw it. Pure C, host-buildable.
 */

#define UI_RADAR_OLD_S (30 * 60) /* an older frame shows its time inverted, with its age (spec §11.2) */
#define UI_FLIGHTS_MAP_H 238      /* the Flights map, 400 px wide, over its separator and 40 px panel */
#define UI_FLIGHTS_OLD_S 120      /* aircraft from an older poll are no longer shown (spec §11.3) */

struct ui_radar {
    const map_data_t *map;            /* the built-in map; NULL: none drawn */
    int32_t home_lat_e4, home_lon_e4; /* location.*: home's ⊙ */
    const radar_frame_t *frame;       /* the weather radar's frame to show; NULL before the first */
    int32_t wx_lat_e4, wx_lon_e4;     /* radar.weather's centre */
    uint8_t wx_zoom_q;                /* and its zoom, in quarters */
    uint8_t loop_at, loop_count;      /* the loop (D28): frame loop_at of loop_count; count 0 outside it */
    int32_t fl_lat_e4, fl_lon_e4;     /* radar.flights' centre */
    uint8_t fl_range_km;              /* and its range, from the centre to the map's top edge */
    bool fl_always;                   /* sync mode `always`: the flight radar runs (D22) */
    time_t fl_updated;                /* the last good poll, UTC; 0 = none yet */
    bool fl_failed;                   /* the last poll failed */
    const adsb_list_t *aircraft;      /* from the last good poll, the nearest first */
    const adsb_route_t *route;        /* the nearest one's route, once known (D27) */
};

/* The Radar layout's map in `r`: the rain, the frame's time and source at the bottom left, the legend
 * or the loop's progress at the bottom right; "No radar frame yet" before the first. */
void ui_draw_radar_view(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx);

/* The Flights layout under the status bar in `r`: the map with its rings, the aircraft, and the
 * panel for the nearest one (spec §11.3). */
void ui_draw_flights_view(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx);
/* The panel's words: the nearest aircraft ("TVS7UZ · B38M · 3675 ft · 459 km/h") over its distance,
 * direction and route, or a message over nothing; `credit` names the sources shown. */
void ui_flights_panel_text(const ui_context_t *ctx, char *line1, char *line2, char *credit, size_t size);
/* "FL338" from 10 000 ft, "9975 ft" below, "GND" on the ground, "" when unknown. */
void ui_flight_altitude(int32_t alt_ft, char *out, size_t size);
