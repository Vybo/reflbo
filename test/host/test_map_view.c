#include <math.h>

#include "map_view.h"
#include "unity.h"

/* Reference values from Python's math module on 2026-10-01 (spec §11.1), and ČHMÚ's own grid: its
 * HDF5 composite says its south-west corner, 48.047275 N 11.266869 E, lies at x = 1254222.15 m,
 * and its north edge, 51.458369 N, at y = 6702777.85 m (ODIM `where`). */

void setUp(void) {}
void tearDown(void) {}

#define BRNO_LAT_E4 491951
#define BRNO_LON_E4 166068

static void test_the_centre_lands_mid_view_and_round_trips(void)
{
    map_view_t v;
    map_view_init(&v, BRNO_LAT_E4, BRNO_LON_E4, 6.5, 400, 280);
    double x, y;
    map_project(&v, 49.1951, 16.6068, &x, &y);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 200.0, x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 140.0, y);
    double lat, lon;
    map_project(&v, 50.0755, 14.4378, &x, &y); /* Praha: north-west of Brno, up and left */
    TEST_ASSERT_TRUE(x < 200 && y < 140);
    map_unproject(&v, x, y, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 50.0755, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 14.4378, lon);
}

static void test_world_pixels_match_the_tiles(void)
{
    /* At zoom 7 Brno falls in tile (69, 43), as RainViewer numbers its tiles */
    double x, y;
    map_world_px(49.1951, 16.6068, 7.0, &x, &y);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 17895.588, x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 11226.134, y);
    TEST_ASSERT_EQUAL_INT(69, (int)(x / MAP_TILE_PX));
    TEST_ASSERT_EQUAL_INT(43, (int)(y / MAP_TILE_PX));
}

static void test_mercator_metres_match_chmus_grid(void)
{
    double mx, my;
    map_mercator(48.047275, 11.266869, &mx, &my);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 1254222.12, mx);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 6114723.32, my);
    map_mercator(51.458369, 19.623974, &mx, &my);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 2184530.79, mx);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 6702777.93, my);

    /* A view's pixel in metres, as the radar's reprojection reads it */
    map_view_t v;
    map_view_init(&v, BRNO_LAT_E4, BRNO_LON_E4, 6.5, 400, 280);
    map_pixel_to_mercator(&v, 200.0, 140.0, &mx, &my);
    double bx, by;
    map_mercator(49.1951, 16.6068, &bx, &by);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, bx, mx);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, by, my);
}

static void test_the_scale_and_the_range_zoom(void)
{
    map_view_t v;
    map_view_init(&v, BRNO_LAT_E4, BRNO_LON_E4, 6.5, 400, 280);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1130.25, map_metres_per_px(&v)); /* spec §11.1: about 1.1 km */

    double z = map_zoom_for_range(BRNO_LAT_E4, 50000.0, 119); /* the flight radar: 50 km, centre to the top */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 7.927604, z);
    map_view_init(&v, BRNO_LAT_E4, BRNO_LON_E4, z, 400, 238);
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 50000.0, 119 * map_metres_per_px(&v));
}

static void test_distance_and_bearing(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(50.0, 184332.5, map_distance_m(49.1951, 16.6068, 50.0755, 14.4378)); /* Praha */
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 302.90, map_bearing_deg(49.1951, 16.6068, 50.0755, 14.4378));
    TEST_ASSERT_DOUBLE_WITHIN(50.0, 48785.2, map_distance_m(49.1951, 16.6068, 49.631836, 16.671102));
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 5.45, map_bearing_deg(49.1951, 16.6068, 49.631836, 16.671102));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, map_distance_m(49.1951, 16.6068, 49.1951, 16.6068));
}

static void test_the_bounds_hold_the_corners(void)
{
    map_view_t v;
    map_view_init(&v, BRNO_LAT_E4, BRNO_LON_E4, 6.5, 400, 280);
    map_bounds_t b;
    map_view_bounds(&v, &b);
    double lat, lon;
    map_unproject(&v, 0, 0, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, lat, b.lat_max);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, lon, b.lon_min);
    map_unproject(&v, 400, 280, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, lat, b.lat_min);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, lon, b.lon_max);
    TEST_ASSERT_TRUE(map_bounds_contain(&b, 49.1951, 16.6068));
    TEST_ASSERT_FALSE(map_bounds_contain(&b, 52.52, 13.405)); /* Berlin is off the edge */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_centre_lands_mid_view_and_round_trips);
    RUN_TEST(test_world_pixels_match_the_tiles);
    RUN_TEST(test_mercator_metres_match_chmus_grid);
    RUN_TEST(test_the_scale_and_the_range_zoom);
    RUN_TEST(test_distance_and_bearing);
    RUN_TEST(test_the_bounds_hold_the_corners);
    return UNITY_END();
}
