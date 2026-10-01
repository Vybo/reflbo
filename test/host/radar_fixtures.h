#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adsb.h"
#include "map_data.h"
#include "radar.h"
#include "ui_radar.h"

/* The radars' inputs for the golden renders (dashboard_fixtures.h): the built-in map, ČHMÚ's rainy
 * frame of 2026-09-24 11:20 UTC and RainViewer's tile of zoom 3 (test_radar.c's fixtures), each
 * loaded once; and adsb.fi's reply and adsb.lol's route of test_adsb.c. REFLBO_MAP_BIN,
 * REFLBO_PNG_FIXTURES and REFLBO_ADSB_FIXTURES come from CMake. */

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

/* adsb.fi's reply around Brno, kept for the Flights map of `range_km`, and the nearest one's route. */
static inline const adsb_list_t *fixture_aircraft(int range_km)
{
    static adsb_list_t list;
    size_t len = 0;
    uint8_t *json = fixture_bytes(REFLBO_ADSB_FIXTURES "/adsb_fi.json", &len);
    char *text = json != NULL ? realloc(json, len + 1) : NULL;
    adsb_filter_t f = { .lat = 49.1951, .lon = 16.6068, .max = ADSB_MAX };
    map_view_init(&f.view, 491951, 166068, map_zoom_for_range(491951, range_km * 1000.0, UI_FLIGHTS_MAP_H / 2), 400,
                  UI_FLIGHTS_MAP_H);
    memset(&list, 0, sizeof(list));
    if (text != NULL) {
        text[len] = '\0';
        adsb_parse(text, len, &f, &list);
    }
    free(text);
    return &list;
}

static inline const adsb_route_t *fixture_route(void)
{
    static adsb_route_t r;
    size_t len = 0;
    uint8_t *json = fixture_bytes(REFLBO_ADSB_FIXTURES "/route_tvs7uz.json", &len);
    char *text = json != NULL ? realloc(json, len + 1) : NULL;
    if (text != NULL) {
        text[len] = '\0';
        adsb_route_parse(text, len, &r);
    }
    free(text);
    return &r;
}

/* The flight radar over Brno: sync mode `always` with a poll a few seconds ago, unless changed. */
static inline ui_radar_t *fixture_flights(int range_km, const adsb_list_t *aircraft, const adsb_route_t *route,
                                          time_t now)
{
    ui_radar_t *r = fixture_radar(NULL);
    r->fl_lat_e4 = 491951;
    r->fl_lon_e4 = 166068;
    r->fl_range_km = (uint8_t)range_km;
    r->fl_always = true;
    r->fl_updated = now - 4;
    r->aircraft = aircraft;
    r->route = route;
    return r;
}
