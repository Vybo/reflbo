#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "radar.h"
#include "unity.h"

/* The weather radar (spec §11.2). The level counts come from Pillow and numpy over the same files,
 * with ČHMÚ's colour scale (scl/scl-dbz-mmh.png) and RainViewer's Universal Blue table. */

void setUp(void) {}
void tearDown(void) {}

static uint8_t *load(const char *name, size_t *len)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*len + 1);
    TEST_ASSERT_EQUAL_size_t(*len, fread(data, 1, *len, f));
    data[*len] = '\0';
    fclose(f);
    return data;
}

static void count_levels(const radar_frame_t *f, long out[4])
{
    memset(out, 0, 4 * sizeof(long));
    for (int y = 0; y < f->h; y++) {
        for (int x = 0; x < f->w; x++) {
            out[radar_frame_level(f, x, y)]++;
        }
    }
}

#define T_20260924_1120 1790248800 /* 2026-09-24 11:20 UTC */

static void test_chmu_names_and_the_newest_step(void)
{
    char name[64];
    radar_chmu_name(T_20260924_1120, name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("pacz2gmaps3.z_max3d.20260924.1120.0.png", name);
    TEST_ASSERT_EQUAL_INT64(T_20260924_1120, radar_chmu_newest(T_20260924_1120 + 7 * 60 + 30)); /* 11:27:30 */
    TEST_ASSERT_EQUAL_INT64(T_20260924_1120, radar_chmu_newest(T_20260924_1120 + 5 * 60));      /* 11:25:00 */
    TEST_ASSERT_EQUAL_INT64(T_20260924_1120 - 300, radar_chmu_newest(T_20260924_1120 + 4 * 60 + 59));
}

static void test_chmu_covers_czechia_and_its_rim(void)
{
    TEST_ASSERT_TRUE(radar_chmu_covers(49.1951, 16.6068));  /* Brno */
    TEST_ASSERT_TRUE(radar_chmu_covers(48.3069, 14.2858));  /* Linz */
    TEST_ASSERT_TRUE(radar_chmu_covers(48.1372, 11.5756));  /* München */
    TEST_ASSERT_FALSE(radar_chmu_covers(52.5200, 13.4050)); /* Berlin, north of it */
    TEST_ASSERT_FALSE(radar_chmu_covers(47.4979, 19.0402)); /* Budapest, south of it */
    TEST_ASSERT_FALSE(radar_chmu_covers(49.8397, 24.0297)); /* Lviv, east of it */
}

static void test_a_rainy_chmu_frame_decodes_to_its_levels(void)
{
    size_t len;
    uint8_t *png = load("png/chmi_rain.png", &len);
    radar_frame_t f;
    TEST_ASSERT_EQUAL_INT(PNG_OK, radar_chmu_decode(png, len, NULL, T_20260924_1120, &f));
    TEST_ASSERT_EQUAL_UINT16(RADAR_CHMU_W, f.w);
    TEST_ASSERT_EQUAL_UINT16(RADAR_CHMU_H, f.h);
    TEST_ASSERT_EQUAL_UINT8(RADAR_SOURCE_CHMU, f.source);
    TEST_ASSERT_EQUAL_UINT32(T_20260924_1120, f.time);
    long n[4];
    count_levels(&f, n);
    TEST_ASSERT_EQUAL_INT32(192493, n[RADAR_NONE]);
    TEST_ASSERT_EQUAL_INT32(30113, n[RADAR_LIGHT]);
    TEST_ASSERT_EQUAL_INT32(3152, n[RADAR_MODERATE]);
    TEST_ASSERT_EQUAL_INT32(286, n[RADAR_HEAVY]);
    TEST_ASSERT_EQUAL_INT(RADAR_HEAVY, radar_frame_level(&f, 152, 67)); /* the first heavy pixel, 50.87 N 13.40 E */
    radar_frame_free(&f, NULL);
    free(png);

    png = load("png/chmi_dry.png", &len);
    TEST_ASSERT_EQUAL_INT(PNG_OK, radar_chmu_decode(png, len, NULL, T_20260924_1120, &f));
    count_levels(&f, n);
    TEST_ASSERT_EQUAL_INT32(RADAR_CHMU_W * RADAR_CHMU_H, n[RADAR_NONE]); /* the black overlay lines are no rain */
    radar_frame_free(&f, NULL);
    free(png);
}

/* A frame on ČHMÚ's grid with one square of the given level around a place. */
static void square_at(radar_frame_t *f, double lat, double lon, int half, radar_level_t level)
{
    TEST_ASSERT_TRUE(radar_frame_alloc(f, RADAR_CHMU_W, RADAR_CHMU_H, NULL));
    f->mx0 = RADAR_CHMU_MX0;
    f->my0 = RADAR_CHMU_MY0;
    f->scale = RADAR_CHMU_SCALE;
    double mx, my;
    map_mercator(lat, lon, &mx, &my);
    int gx = (int)((mx - f->mx0) / f->scale), gy = (int)((f->my0 - my) / f->scale);
    for (int y = gy - half; y <= gy + half; y++) {
        for (int x = gx - half; x <= gx + half; x++) {
            radar_frame_set(f, x, y, level);
        }
    }
}

static int ink(const gfx_fb_t *fb, gfx_rect_t r)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            n += gfx_get_pixel(fb, x, y);
        }
    }
    return n;
}

static void test_rain_lands_where_it_falls_in_any_view(void)
{
    static uint8_t buf[400 * 300 / 8];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    for (int z = 0; z < 2; z++) {
        double zoom = z == 0 ? 6.5 : 8.0;
        static const radar_level_t k_levels[3] = { RADAR_HEAVY, RADAR_MODERATE, RADAR_LIGHT };
        static const int k_ink[3] = { 64, 32, 16 }; /* of an 8 x 8 square: solid, half, a quarter */
        for (int i = 0; i < 3; i++) {
            radar_frame_t f;
            square_at(&f, 49.1951, 16.6068, 8, k_levels[i]); /* 17 grid pixels around Brno */
            map_view_t v;
            map_view_init(&v, 491951, 166068, zoom, 400, 280);
            gfx_clear(&fb, GFX_WHITE);
            radar_render(&fb, (gfx_rect_t){ 0, 20, 400, 280 }, &v, &f);
            TEST_ASSERT_EQUAL_INT_MESSAGE(k_ink[i], ink(&fb, (gfx_rect_t){ 196, 156, 8, 8 }), "at the centre");
            TEST_ASSERT_EQUAL_INT(0, ink(&fb, (gfx_rect_t){ 0, 20, 60, 60 })); /* dry far away */
            TEST_ASSERT_TRUE(radar_any_rain(&v, &f));
            radar_frame_free(&f, NULL);
        }
    }
}

static void test_outside_the_data_the_edge_is_dotted(void)
{
    static uint8_t buf[400 * 300 / 8];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    gfx_clear(&fb, GFX_WHITE);
    radar_frame_t f;
    square_at(&f, 49.1951, 16.6068, 2, RADAR_HEAVY);
    map_view_t v;
    map_view_init(&v, 479000, 166068, 6.5, 400, 280); /* centred south of ČHMÚ's data: its south edge crosses */
    radar_render(&fb, (gfx_rect_t){ 0, 20, 400, 280 }, &v, &f);
    double x, y;
    map_project(&v, 48.047275, 16.6068, &x, &y); /* the south edge of the data */
    int row = 20 + (int)lround(y);
    int dots = ink(&fb, (gfx_rect_t){ 0, (int16_t)(row - 1), 400, 3 });
    TEST_ASSERT_TRUE_MESSAGE(dots > 100 && dots < 300, "a dotted line, every other pixel");
    TEST_ASSERT_FALSE(radar_any_rain(&(map_view_t){ .cx = 0, .cy = 0, .zoom = 6.5, .w = 400, .h = 280 }, &f));
    radar_frame_free(&f, NULL);
}

static void test_the_rainviewer_index_names_its_frames(void)
{
    size_t len;
    char *json = (char *)load("radar/rainviewer_maps.json", &len);
    radar_rv_index_t idx;
    TEST_ASSERT_TRUE(radar_rv_parse_index(json, len, &idx));
    TEST_ASSERT_EQUAL_STRING("https://tilecache.rainviewer.com", idx.host);
    TEST_ASSERT_EQUAL_INT(13, idx.count);
    TEST_ASSERT_EQUAL_UINT32(1790881800, idx.frames[0].time);
    TEST_ASSERT_EQUAL_STRING("/v2/radar/c15f405b89f4", idx.frames[12].path);
    TEST_ASSERT_EQUAL_UINT32(1790889000, idx.frames[12].time);
    TEST_ASSERT_FALSE(radar_rv_parse_index("{\"radar\":{\"past\":[]}}", 21, &idx));
    TEST_ASSERT_FALSE(radar_rv_parse_index("[]", 2, &idx));
    free(json);
}

static void test_rainviewer_tiles_cover_the_view(void)
{
    map_view_t v;
    map_view_init(&v, 491951, 166068, 6.5, 400, 280);
    radar_rv_tiles_t t;
    radar_rv_tiles(&v, &t);
    TEST_ASSERT_EQUAL_INT(6, t.z);
    TEST_ASSERT_EQUAL_INT(34, t.x0);
    TEST_ASSERT_EQUAL_INT(21, t.y0);
    TEST_ASSERT_EQUAL_INT(2, t.nx);
    TEST_ASSERT_EQUAL_INT(2, t.ny);
    map_view_init(&v, 491951, 166068, 9.0, 400, 280);
    radar_rv_tiles(&v, &t);
    TEST_ASSERT_EQUAL_INT(RADAR_RV_MAX_ZOOM, t.z); /* nothing above zoom 7: enlarged */
    TEST_ASSERT_TRUE(t.nx <= 2 && t.ny <= 2);
    char url[160];
    radar_rv_tile_url(url, sizeof(url), "https://tilecache.rainviewer.com", "/v2/radar/c15f405b89f4", 7, 69, 43);
    TEST_ASSERT_EQUAL_STRING("https://tilecache.rainviewer.com/v2/radar/c15f405b89f4/256/7/69/43/2/0_0.png", url);
}

static void test_a_rainviewer_tile_decodes_by_its_colour_table(void)
{
    size_t len;
    uint8_t *png = load("png/rainviewer_tile.png", &len); /* zoom 3, tile (4, 2) */
    radar_rv_tiles_t t = { .z = 3, .x0 = 4, .y0 = 2, .nx = 1, .ny = 1 };
    radar_frame_t f;
    TEST_ASSERT_TRUE(radar_rv_frame_alloc(&f, &t, 1790889000, NULL));
    TEST_ASSERT_EQUAL_UINT16(256, f.w);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2 * 3.14159265358979323846 * MAP_EARTH_R / (256 * 8), f.scale);
    TEST_ASSERT_EQUAL_INT(PNG_OK, radar_rv_decode_tile(png, len, NULL, &t, 4, 2, &f));
    long n[4];
    count_levels(&f, n);
    TEST_ASSERT_EQUAL_INT32(62286, n[RADAR_NONE]);
    TEST_ASSERT_EQUAL_INT32(2575, n[RADAR_LIGHT]);
    TEST_ASSERT_EQUAL_INT32(629, n[RADAR_MODERATE]);
    TEST_ASSERT_EQUAL_INT32(46, n[RADAR_HEAVY]);
    radar_frame_free(&f, NULL);
    free(png);
}

static void test_a_frame_covers_the_views_it_has_the_rain_for(void)
{
    map_view_t v, slot, deeper, paris;
    map_view_init(&v, 525200, 134050, 6.5, RADAR_VIEW_W, RADAR_VIEW_H); /* Berlin: RainViewer */
    radar_rv_tiles_t t;
    radar_rv_tiles(&v, &t);
    radar_frame_t f;
    TEST_ASSERT_TRUE(radar_rv_frame_alloc(&f, &t, 1790889000, NULL));
    TEST_ASSERT_TRUE(radar_frame_covers(&f, &v));
    map_view_init(&slot, 525200, 134050, 6.5, 196, 120); /* a slot's map: some of the same tiles */
    TEST_ASSERT_TRUE(radar_frame_covers(&f, &slot));
    map_view_init(&deeper, 525200, 134050, 7.0, RADAR_VIEW_W, RADAR_VIEW_H); /* zoom 7's tiles */
    TEST_ASSERT_FALSE(radar_frame_covers(&f, &deeper));
    map_view_init(&paris, 488566, 23522, 6.5, RADAR_VIEW_W, RADAR_VIEW_H);
    TEST_ASSERT_FALSE(radar_frame_covers(&f, &paris));
    radar_frame_free(&f, NULL);
    radar_frame_t chmu = { .source = RADAR_SOURCE_CHMU };
    TEST_ASSERT_TRUE(radar_frame_covers(&chmu, &deeper)); /* one grid for every view: its edge shows */
}

static int s_live;

static void *counted_alloc(size_t n)
{
    s_live++;
    return calloc(1, n);
}

static void counted_free(void *p)
{
    if (p != NULL) {
        s_live--;
    }
    free(p);
}

static void test_the_store_keeps_frames_in_order_and_frees_the_rest(void)
{
    const png_mem_t mem = { counted_alloc, counted_free };
    radar_store_t s;
    radar_store_init(&s, &mem);
    s_live = 0;
    const uint32_t t0 = 1790880000;
    static const uint32_t k_times[] = { 600, 300, 900, 600 }; /* out of order, and one twice */
    for (size_t i = 0; i < sizeof(k_times) / sizeof(k_times[0]); i++) {
        radar_frame_t f;
        TEST_ASSERT_TRUE(radar_frame_alloc(&f, 8, 8, &mem));
        f.time = t0 + k_times[i];
        radar_store_put(&s, &f, RADAR_LOOP_FRAMES);
    }
    TEST_ASSERT_EQUAL_INT(3, s.count);
    TEST_ASSERT_EQUAL_INT(3, s_live); /* the repeated time replaced its frame */
    TEST_ASSERT_EQUAL_UINT32(t0 + 300, s.frames[0].time);
    TEST_ASSERT_EQUAL_UINT32(t0 + 900, radar_store_newest(&s)->time);
    TEST_ASSERT_TRUE(radar_store_has(&s, t0 + 600));

    uint32_t missing[12];
    int n = radar_store_missing(&s, t0 + 900, 300, missing, 12);
    TEST_ASSERT_EQUAL_INT(9, n); /* the hour has 12 steps; 3 are kept */
    TEST_ASSERT_EQUAL_UINT32(t0, missing[0]); /* newest first */
    TEST_ASSERT_EQUAL_UINT32(t0 - 8 * 300, missing[8]);

    radar_store_keep(&s, 2); /* sync mode always ends: the hour goes */
    TEST_ASSERT_EQUAL_INT(2, s.count);
    TEST_ASSERT_EQUAL_INT(2, s_live);
    TEST_ASSERT_EQUAL_UINT32(t0 + 600, s.frames[0].time);
    radar_frame_t f;
    TEST_ASSERT_TRUE(radar_frame_alloc(&f, 8, 8, &mem));
    f.time = t0 + 1200;
    radar_store_put(&s, &f, 1); /* outside sync mode always: only the newest stays */
    TEST_ASSERT_EQUAL_INT(1, s.count);
    TEST_ASSERT_EQUAL_INT(1, s_live);
    TEST_ASSERT_EQUAL_UINT32(t0 + 1200, radar_store_newest(&s)->time);
    radar_store_clear(&s);
    TEST_ASSERT_EQUAL_INT(0, s_live);
    TEST_ASSERT_NULL(radar_store_newest(&s));
}

static void test_a_fetch_wants_the_hour_it_lacks(void)
{
    const uint32_t t0 = 1790880000;
    uint32_t out[RADAR_LOOP_FRAMES];
    TEST_ASSERT_EQUAL_INT(1, radar_wanted(t0, 300, 1, NULL, 0, out)); /* outside sync mode always: the newest */
    TEST_ASSERT_EQUAL_UINT32(t0, out[0]);
    const uint32_t have[] = { t0 - 300, t0 - 900, t0 - 6000 }; /* the last one is older than the hour */
    TEST_ASSERT_EQUAL_INT(10, radar_wanted(t0, 300, RADAR_LOOP_FRAMES, have, 3, out));
    TEST_ASSERT_EQUAL_UINT32(t0, out[0]); /* newest first */
    TEST_ASSERT_EQUAL_UINT32(t0 - 600, out[1]);
    TEST_ASSERT_EQUAL_UINT32(t0 - 3300, out[9]);
    TEST_ASSERT_EQUAL_INT(0, radar_wanted(t0 - 300, 300, 1, have, 3, out)); /* nothing new */
    TEST_ASSERT_EQUAL_INT(6, radar_wanted(t0, 600, 6, NULL, 0, out)); /* RainViewer's hour */
    TEST_ASSERT_EQUAL_UINT32(t0 - 3000, out[5]);
    TEST_ASSERT_EQUAL_INT(RADAR_LOOP_FRAMES, radar_wanted(t0, 300, 40, NULL, 0, out)); /* never more than kept */
}

static void test_a_frame_survives_its_file_and_a_damaged_file_is_refused(void)
{
    radar_frame_t f, back;
    TEST_ASSERT_TRUE(radar_frame_alloc(&f, 30, 10, NULL));
    f.time = 1790880000;
    f.source = RADAR_SOURCE_RAINVIEWER;
    f.mx0 = 1254222.15;
    f.my0 = 6702777.85;
    f.scale = 1555.7;
    radar_frame_set(&f, 29, 9, RADAR_HEAVY);
    radar_frame_set(&f, 3, 4, RADAR_LIGHT);
    static uint8_t file[512];
    size_t n = radar_frame_to_file(&f, file, sizeof(file));
    TEST_ASSERT_EQUAL_size_t(radar_frame_file_size(&f), n);
    TEST_ASSERT_EQUAL_size_t(RADAR_FILE_HEADER + 75, n); /* 300 pixels at 2 bits */
    TEST_ASSERT_TRUE(radar_frame_from_file(file, n, NULL, &back));
    TEST_ASSERT_EQUAL_UINT32(f.time, back.time);
    TEST_ASSERT_EQUAL_UINT8(RADAR_SOURCE_RAINVIEWER, back.source);
    TEST_ASSERT_EQUAL_UINT16(30, back.w);
    TEST_ASSERT_EQUAL_DOUBLE(f.my0, back.my0);
    TEST_ASSERT_EQUAL_DOUBLE(f.scale, back.scale);
    TEST_ASSERT_EQUAL_MEMORY(f.levels, back.levels, 75);
    radar_frame_free(&back, NULL);

    TEST_ASSERT_EQUAL_size_t(0, radar_frame_to_file(&f, file, n - 1)); /* too small: nothing written */
    n = radar_frame_to_file(&f, file, sizeof(file));
    TEST_ASSERT_FALSE(radar_frame_from_file(file, n - 1, NULL, &back)); /* cut short */
    file[RADAR_FILE_HEADER + 10] ^= 0x10;
    TEST_ASSERT_FALSE(radar_frame_from_file(file, n, NULL, &back)); /* its CRC */
    file[RADAR_FILE_HEADER + 10] ^= 0x10;
    file[0] ^= 1;
    TEST_ASSERT_FALSE(radar_frame_from_file(file, n, NULL, &back)); /* not a frame */
    TEST_ASSERT_NULL(back.levels);
    radar_frame_free(&f, NULL);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_chmu_names_and_the_newest_step);
    RUN_TEST(test_chmu_covers_czechia_and_its_rim);
    RUN_TEST(test_a_rainy_chmu_frame_decodes_to_its_levels);
    RUN_TEST(test_rain_lands_where_it_falls_in_any_view);
    RUN_TEST(test_outside_the_data_the_edge_is_dotted);
    RUN_TEST(test_the_rainviewer_index_names_its_frames);
    RUN_TEST(test_rainviewer_tiles_cover_the_view);
    RUN_TEST(test_a_rainviewer_tile_decodes_by_its_colour_table);
    RUN_TEST(test_a_frame_covers_the_views_it_has_the_rain_for);
    RUN_TEST(test_the_store_keeps_frames_in_order_and_frees_the_rest);
    RUN_TEST(test_a_fetch_wants_the_hour_it_lacks);
    RUN_TEST(test_a_frame_survives_its_file_and_a_damaged_file_is_refused);
    return UNITY_END();
}
