#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "map_data.h"
#include "radar.h"
#include "ui_radar.h"

/* The radars' inputs for the golden renders (dashboard_fixtures.h): the built-in map, ČHMÚ's rainy
 * frame of 2026-09-24 11:20 UTC and RainViewer's tile of zoom 3 (test_radar.c's fixtures), each
 * loaded once. REFLBO_MAP_BIN and REFLBO_PNG_FIXTURES come from CMake. */

static inline uint8_t *fixture_bytes(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*len);
    if (data != NULL && fread(data, 1, *len, f) != *len) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

static inline const map_data_t *fixture_map(void)
{
    static map_data_t map;
    static int state; /* 0 not tried, 1 loaded, -1 failed */
    if (state == 0) {
        size_t len = 0;
        uint8_t *blob = fixture_bytes(REFLBO_MAP_BIN, &len);
        state = blob != NULL && map_data_open(&map, blob, len) ? 1 : -1;
    }
    return state > 0 ? &map : NULL;
}

/* ČHMÚ's frame, stamped `time` (UTC). */
static inline const radar_frame_t *fixture_chmu(time_t time)
{
    static radar_frame_t f;
    static int state;
    if (state == 0) {
        size_t len = 0;
        uint8_t *png = fixture_bytes(REFLBO_PNG_FIXTURES "/chmi_rain.png", &len);
        state = png != NULL && radar_chmu_decode(png, len, NULL, 0, &f) == PNG_OK ? 1 : -1;
        free(png);
    }
    f.time = (uint32_t)time;
    return state > 0 ? &f : NULL;
}

/* RainViewer's tile (4, 2) of zoom 3 as a frame, stamped `time`. */
static inline const radar_frame_t *fixture_rainviewer(time_t time)
{
    static radar_frame_t f;
    static int state;
    if (state == 0) {
        size_t len = 0;
        uint8_t *png = fixture_bytes(REFLBO_PNG_FIXTURES "/rainviewer_tile.png", &len);
        radar_rv_tiles_t t = { .z = 3, .x0 = 4, .y0 = 2, .nx = 1, .ny = 1 };
        state = png != NULL && radar_rv_frame_alloc(&f, &t, 0, NULL) &&
                        radar_rv_decode_tile(png, len, NULL, &t, 4, 2, &f) == PNG_OK
                    ? 1
                    : -1;
        free(png);
    }
    f.time = (uint32_t)time;
    return state > 0 ? &f : NULL;
}

/* The weather radar over Brno at zoom 6.5 with `frame` (NULL: none yet). */
static inline ui_radar_t *fixture_radar(const radar_frame_t *frame)
{
    static ui_radar_t r;
    r = (ui_radar_t){ .map = fixture_map(), .home_lat_e4 = 491951, .home_lon_e4 = 166068, .frame = frame,
                      .wx_lat_e4 = 491951, .wx_lon_e4 = 166068, .wx_zoom_q = 26 };
    return &r;
}
