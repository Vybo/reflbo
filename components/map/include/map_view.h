#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Web Mercator views (spec §11.1), the projection of both radar sources: a centre and a zoom over
 * a w x h pixel rectangle. At zoom z the world is 256 x 2^z pixels across, as for map tiles.
 * Pure C, host-buildable.
 */

#define MAP_EARTH_R 6378137.0 /* web Mercator's sphere (EPSG:3857), metres */
#define MAP_TILE_PX 256

typedef struct {
    double cx, cy; /* the centre in world pixels at `zoom` */
    double zoom;
    int16_t w, h; /* the view in screen pixels */
} map_view_t;

typedef struct {
    double lat_min, lat_max, lon_min, lon_max; /* degrees */
} map_bounds_t;

/* A w x h view centred on (lat_e4, lon_e4) at `zoom`; latitudes beyond ±85° are clamped. */
void map_view_init(map_view_t *v, int32_t lat_e4, int32_t lon_e4, double zoom, int16_t w, int16_t h);
/* The zoom at which `range_m` on the ground spans `px` pixels at latitude lat_e4: the flight
 * radar's range from its centre to the top edge. */
double map_zoom_for_range(int32_t lat_e4, double range_m, int px);

/* World pixels of a point at `zoom`. */
void map_world_px(double lat, double lon, double zoom, double *x, double *y);
/* Web Mercator metres of a point (EPSG:3857), as ČHMÚ's grid and the projection strings use them. */
void map_mercator(double lat, double lon, double *mx, double *my);

/* Screen pixels of a point, from the view's top-left; it may fall outside the view. */
void map_project(const map_view_t *v, double lat, double lon, double *x, double *y);
/* The point at a screen pixel. */
void map_unproject(const map_view_t *v, double x, double y, double *lat, double *lon);
/* Web Mercator metres at a screen pixel: what the radar's reprojection reads. */
void map_pixel_to_mercator(const map_view_t *v, double x, double y, double *mx, double *my);
/* Ground metres per screen pixel at the view's centre. */
double map_metres_per_px(const map_view_t *v);

void map_view_bounds(const map_view_t *v, map_bounds_t *b);
bool map_bounds_contain(const map_bounds_t *b, double lat, double lon);

/* Great-circle distance (metres, the mean Earth radius) and initial bearing (degrees, 0 = north,
 * clockwise) from point 1 to point 2. */
double map_distance_m(double lat1, double lon1, double lat2, double lon2);
double map_bearing_deg(double lat1, double lon1, double lat2, double lon2);
