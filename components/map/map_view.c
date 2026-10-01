#include "map_view.h"

#include <math.h>

#define PI 3.14159265358979323846
#define MEAN_EARTH_R 6371008.8 /* metres, for distances on the ground */
#define MAX_LAT 85.0           /* web Mercator's own limit is 85.0511 */

static double rad(double deg)
{
    return deg * PI / 180.0;
}

static double deg(double r)
{
    return r * 180.0 / PI;
}

static double world_size(double zoom)
{
    return MAP_TILE_PX * pow(2.0, zoom);
}

static double clamp_lat(double lat)
{
    return lat > MAX_LAT ? MAX_LAT : lat < -MAX_LAT ? -MAX_LAT : lat;
}

void map_world_px(double lat, double lon, double zoom, double *x, double *y)
{
    double w = world_size(zoom);
    *x = (lon + 180.0) / 360.0 * w;
    *y = (0.5 - log(tan(PI / 4 + rad(clamp_lat(lat)) / 2)) / (2 * PI)) * w;
}

void map_mercator(double lat, double lon, double *mx, double *my)
{
    *mx = MAP_EARTH_R * rad(lon);
    *my = MAP_EARTH_R * log(tan(PI / 4 + rad(clamp_lat(lat)) / 2));
}

void map_view_init(map_view_t *v, int32_t lat_e4, int32_t lon_e4, double zoom, int16_t w, int16_t h)
{
    v->zoom = zoom;
    v->w = w;
    v->h = h;
    map_world_px(lat_e4 / 1e4, lon_e4 / 1e4, zoom, &v->cx, &v->cy);
}

double map_zoom_for_range(int32_t lat_e4, double range_m, int px)
{
    double m_per_px = range_m / (px > 0 ? px : 1);
    return log2(2 * PI * MAP_EARTH_R * cos(rad(clamp_lat(lat_e4 / 1e4))) / m_per_px / MAP_TILE_PX);
}

void map_project(const map_view_t *v, double lat, double lon, double *x, double *y)
{
    double wx, wy;
    map_world_px(lat, lon, v->zoom, &wx, &wy);
    *x = wx - v->cx + v->w / 2.0;
    *y = wy - v->cy + v->h / 2.0;
}

void map_unproject(const map_view_t *v, double x, double y, double *lat, double *lon)
{
    double w = world_size(v->zoom);
    double wx = x + v->cx - v->w / 2.0, wy = y + v->cy - v->h / 2.0;
    *lon = wx / w * 360.0 - 180.0;
    *lat = deg(2 * atan(exp((0.5 - wy / w) * 2 * PI)) - PI / 2);
}

void map_pixel_to_mercator(const map_view_t *v, double x, double y, double *mx, double *my)
{
    double w = world_size(v->zoom);
    double wx = x + v->cx - v->w / 2.0, wy = y + v->cy - v->h / 2.0;
    *mx = (wx / w - 0.5) * 2 * PI * MAP_EARTH_R;
    *my = (0.5 - wy / w) * 2 * PI * MAP_EARTH_R;
}

double map_metres_per_px(const map_view_t *v)
{
    double lat, lon;
    map_unproject(v, v->w / 2.0, v->h / 2.0, &lat, &lon);
    return 2 * PI * MAP_EARTH_R * cos(rad(lat)) / world_size(v->zoom);
}

void map_view_bounds(const map_view_t *v, map_bounds_t *b)
{
    map_unproject(v, 0, 0, &b->lat_max, &b->lon_min);
    map_unproject(v, v->w, v->h, &b->lat_min, &b->lon_max);
}

bool map_bounds_contain(const map_bounds_t *b, double lat, double lon)
{
    return lat >= b->lat_min && lat <= b->lat_max && lon >= b->lon_min && lon <= b->lon_max;
}

double map_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = rad(lat1), p2 = rad(lat2), dp = p2 - p1, dl = rad(lon2 - lon1);
    double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 2 * MEAN_EARTH_R * asin(sqrt(h > 1 ? 1 : h));
}

double map_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = rad(lat1), p2 = rad(lat2), dl = rad(lon2 - lon1);
    double b = deg(atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)));
    return b < 0 ? b + 360.0 : b;
}
