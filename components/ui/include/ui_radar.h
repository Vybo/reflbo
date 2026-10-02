#pragma once

#include <stdint.h>

#include "gfx.h"
#include "map_data.h"
#include "radar.h"
#include "ui_fields.h"

/*
 * The radars' views (spec §11.1–§11.3): what the app gathers for them, and the weather radar's map
 * as the Radar layout and the rain.map widget draw it. Pure C, host-buildable.
 */

#define UI_RADAR_OLD_S (30 * 60) /* an older frame shows its time inverted, with its age (spec §11.2) */

struct ui_radar {
    const map_data_t *map;            /* the built-in map; NULL: none drawn */
    int32_t home_lat_e4, home_lon_e4; /* location.*: home's ⊙ */
    const radar_frame_t *frame;       /* the weather radar's frame to show; NULL before the first */
    int32_t wx_lat_e4, wx_lon_e4;     /* radar.weather's centre */
    uint8_t wx_zoom_q;                /* and its zoom, in quarters */
    uint8_t loop_at, loop_count;      /* the loop (D28): frame loop_at of loop_count; count 0 outside it */
};

/* The Radar layout's map in `r`: the rain, the frame's time and source at the bottom left, the legend
 * or the loop's progress at the bottom right; "No radar frame yet" before the first. */
void ui_draw_radar_view(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx);
