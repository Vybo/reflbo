#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "gfx_fonts.h"
#include "map_draw.h"
#include "unity.h"

/* Drawing the map (spec §11.1) on the real assets/map/map.bin. */

static uint8_t s_buf[400 * 300 / 8];
static gfx_fb_t s_fb;
static uint8_t *s_blob;
static map_data_t s_map;

void setUp(void)
{
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    gfx_clear(&s_fb, GFX_WHITE);
}

void tearDown(void) {}

static void load(void)
{
    FILE *f = fopen(MAP_BIN, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, MAP_BIN);
    fseek(f, 0, SEEK_END);
    size_t len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    s_blob = malloc(len);
    TEST_ASSERT_EQUAL_size_t(len, fread(s_blob, 1, len, f));
    fclose(f);
    TEST_ASSERT_TRUE(map_data_open(&s_map, s_blob, len));
}

static int ink_in(gfx_rect_t r)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            n += gfx_get_pixel(&s_fb, x, y);
        }
    }
    return n;
}

static bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void test_labels_keep_clear_and_give_up_when_boxed_in(void)
{
    gfx_rect_t area = { 0, 0, 200, 100 };
    map_labels_t l;
    map_labels_init(&l);
    for (int i = 0; i < 4; i++) { /* right, left, above, below */
        TEST_ASSERT_TRUE_MESSAGE(map_label(&s_fb, area, &l, &gfx_font_sans_12, 100, 50, 3, "Brno"), "a free side");
    }
    TEST_ASSERT_EQUAL_INT(4, l.count);
    for (int i = 0; i < l.count; i++) {
        TEST_ASSERT_TRUE(ink_in(l.r[i]) > 0);
        for (int j = i + 1; j < l.count; j++) {
            TEST_ASSERT_FALSE(overlap(l.r[i], l.r[j]));
        }
    }
    TEST_ASSERT_FALSE(map_label(&s_fb, area, &l, &gfx_font_sans_12, 100, 50, 3, "Brno")); /* boxed in */
    TEST_ASSERT_EQUAL_INT(4, l.count);
    /* a label that would leave the area tries the other sides */
    TEST_ASSERT_TRUE(map_label(&s_fb, area, &l, &gfx_font_sans_12, 195, 20, 3, "Olomouc"));
    TEST_ASSERT_TRUE(l.r[4].x + l.r[4].w <= 200);
}

static void test_a_reserved_rectangle_stays_free(void)
{
    gfx_rect_t area = { 0, 0, 200, 100 };
    map_labels_t l;
    map_labels_init(&l);
    map_labels_reserve(&l, (gfx_rect_t){ 103, 40, 60, 20 }); /* where the label would go first */
    TEST_ASSERT_TRUE(map_label(&s_fb, area, &l, &gfx_font_sans_12, 100, 50, 3, "Brno"));
    TEST_ASSERT_FALSE(overlap(l.r[1], (gfx_rect_t){ 103, 40, 60, 20 }));
}

static void test_the_halo_clears_ink_around_a_label(void)
{
    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 200, 100 }, GFX_BLACK); /* heavy rain everywhere */
    map_labels_t l;
    map_labels_init(&l);
    TEST_ASSERT_TRUE(map_label(&s_fb, (gfx_rect_t){ 0, 0, 200, 100 }, &l, &gfx_font_sans_12, 100, 50, 3, "Brno"));
    gfx_rect_t r = l.r[0];
    int white = r.w * r.h - ink_in(r);
    TEST_ASSERT_TRUE_MESSAGE(white > r.w * r.h / 3, "the halo opens the rain around the text");
    TEST_ASSERT_TRUE(ink_in(r) > 0); /* and the text is still there */
}

static void test_lines_stay_inside_their_area(void)
{
    map_view_t v;
    map_view_init(&v, 491951, 166068, 6.5, 200, 140);
    gfx_rect_t area = { 100, 80, 200, 140 };
    map_style_t s = { .halo = false };
    map_draw_lines(&s_fb, area, &v, &s_map, &s);
    TEST_ASSERT_TRUE(ink_in(area) > 50);
    int outside = ink_in((gfx_rect_t){ 0, 0, 400, 80 }) + ink_in((gfx_rect_t){ 0, 220, 400, 80 }) +
                  ink_in((gfx_rect_t){ 0, 80, 100, 140 }) + ink_in((gfx_rect_t){ 300, 80, 100, 140 });
    TEST_ASSERT_EQUAL_INT(0, outside);
}

static void test_the_border_lands_where_it_is_projected(void)
{
    map_view_t v;
    map_view_init(&v, 487967, 166368, 9.0, 400, 280); /* Mikulov, on the Czech-Austrian border */
    gfx_rect_t area = { 0, 20, 400, 280 };
    map_style_t s = { .halo = true };
    map_draw_lines(&s_fb, area, &v, &s_map, &s);
    double x, y;
    map_project(&v, 48.7783, 16.6431, &x, &y); /* a vertex of the border south of Mikulov, from map.bin */
    gfx_rect_t near = { (int16_t)(x - 3), (int16_t)(20 + y - 3), 7, 7 };
    TEST_ASSERT_TRUE_MESSAGE(ink_in(near) > 0, "the border within 3 px of where it is");
}

static void test_home_is_a_ring_with_a_dot_and_is_reserved(void)
{
    map_view_t v;
    map_view_init(&v, 491951, 166068, 6.5, 400, 280);
    gfx_rect_t area = { 0, 20, 400, 280 };
    map_labels_t l;
    map_labels_init(&l);
    map_draw_home(&s_fb, area, &v, 491951, 166068, &l);
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 200, 160));  /* the dot at the centre */
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 202, 160)); /* white inside the ring */
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 205, 160));  /* the ring, 5 px out */
    TEST_ASSERT_EQUAL_INT(1, l.count);
}

static void test_rings_mark_the_range_and_its_half(void)
{
    map_view_t v;
    double z = map_zoom_for_range(491951, 50000.0, 119);
    map_view_init(&v, 491951, 166068, z, 400, 238);
    gfx_rect_t area = { 0, 21, 400, 238 };
    map_labels_t l;
    map_labels_init(&l);
    map_draw_rings(&s_fb, area, &v, 50000.0, &l);
    int cx = 200, cy = 21 + 119;
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, cx, cy - 119 + 1) || gfx_get_pixel(&s_fb, cx, cy - 119)); /* 50 km */
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, cx, cy - 60) || gfx_get_pixel(&s_fb, cx, cy - 59));   /* 25 km */
    TEST_ASSERT_EQUAL_INT(2, l.count); /* "50 km" and "25 km" */
}

static void test_places_are_labelled_without_overlaps(void)
{
    map_view_t v;
    double z = map_zoom_for_range(491951, 100000.0, 119);
    map_view_init(&v, 491951, 166068, z, 400, 238);
    gfx_rect_t area = { 0, 21, 400, 238 };
    map_labels_t l;
    map_labels_init(&l);
    map_style_t s = { .airports = true, .max_towns = 12 };
    map_draw_places(&s_fb, area, &v, &s_map, &s, &l);
    TEST_ASSERT_TRUE(l.count >= 2); /* Brno and its airport, BRQ, at least */
    for (int i = 0; i < l.count; i++) {
        TEST_ASSERT_TRUE(l.r[i].x >= area.x && l.r[i].x + l.r[i].w <= area.x + area.w);
        for (int j = i + 1; j < l.count; j++) {
            TEST_ASSERT_FALSE(overlap(l.r[i], l.r[j]));
        }
    }
}

int main(void)
{
    load();
    UNITY_BEGIN();
    RUN_TEST(test_labels_keep_clear_and_give_up_when_boxed_in);
    RUN_TEST(test_a_reserved_rectangle_stays_free);
    RUN_TEST(test_the_halo_clears_ink_around_a_label);
    RUN_TEST(test_lines_stay_inside_their_area);
    RUN_TEST(test_the_border_lands_where_it_is_projected);
    RUN_TEST(test_home_is_a_ring_with_a_dot_and_is_reserved);
    RUN_TEST(test_rings_mark_the_range_and_its_half);
    RUN_TEST(test_places_are_labelled_without_overlaps);
    int failures = UNITY_END();
    free(s_blob);
    return failures;
}
