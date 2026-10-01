#include <string.h>

#include "gfx.h"
#include "unity.h"

#define GUARD 0xA5

/* 16x4 framebuffer (8 bytes) with two guard bytes on each side to catch out-of-bounds writes. */
static uint8_t s_mem[2 + 8 + 2];
static gfx_fb_t s_fb;

void setUp(void)
{
    memset(s_mem, GUARD, sizeof(s_mem));
    memset(s_mem + 2, 0, 8);
    gfx_fb_init(&s_fb, s_mem + 2, 16, 4);
}

void tearDown(void) {}

static void assert_guards_intact(void)
{
    TEST_ASSERT_EQUAL_HEX8(GUARD, s_mem[0]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, s_mem[1]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, s_mem[10]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, s_mem[11]);
}

static int count_black(void)
{
    int n = 0;
    for (int y = 0; y < s_fb.height; y++) {
        for (int x = 0; x < s_fb.width; x++) {
            n += gfx_get_pixel(&s_fb, x, y) ? 1 : 0;
        }
    }
    return n;
}

static void test_pixel_bits_are_msb_first_row_major(void)
{
    gfx_pixel(&s_fb, 0, 0, GFX_BLACK);
    gfx_pixel(&s_fb, 7, 0, GFX_BLACK);
    gfx_pixel(&s_fb, 8, 1, GFX_BLACK);
    TEST_ASSERT_EQUAL_HEX8(0x81, s_fb.buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_fb.buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_fb.buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x80, s_fb.buf[3]);
    TEST_ASSERT_EQUAL_INT(3, count_black());
}

static void test_white_clears_and_invert_toggles(void)
{
    gfx_pixel(&s_fb, 3, 2, GFX_BLACK);
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 3, 2));
    gfx_pixel(&s_fb, 3, 2, GFX_WHITE);
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 3, 2));
    gfx_pixel(&s_fb, 3, 2, GFX_INVERT);
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 3, 2));
    gfx_pixel(&s_fb, 3, 2, GFX_INVERT);
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 3, 2));
}

static void test_drawing_outside_the_framebuffer_changes_nothing(void)
{
    gfx_pixel(&s_fb, -1, 0, GFX_BLACK);
    gfx_pixel(&s_fb, 16, 0, GFX_BLACK);
    gfx_pixel(&s_fb, 0, -1, GFX_BLACK);
    gfx_pixel(&s_fb, 0, 4, GFX_BLACK);
    gfx_pixel(&s_fb, 1000, 1000, GFX_BLACK);
    gfx_fill_rect(&s_fb, (gfx_rect_t){ -100, -100, 20, 20 }, GFX_BLACK);
    gfx_hline(&s_fb, -5, 1, 3, GFX_BLACK);
    gfx_vline(&s_fb, 20, 0, 4, GFX_BLACK);
    gfx_hline(&s_fb, 0, 1, -7, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(0, count_black());
    assert_guards_intact();
}

static void test_huge_rectangles_are_clipped_to_the_framebuffer(void)
{
    gfx_fill_rect(&s_fb, (gfx_rect_t){ -1000, -1000, 30000, 30000 }, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(64, count_black());
    assert_guards_intact();
}

static void test_clip_limits_drawing(void)
{
    gfx_set_clip(&s_fb, (gfx_rect_t){ 2, 1, 3, 2 });
    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 16, 4 }, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(6, count_black());
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 1));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 4, 2));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 5, 2));
    gfx_reset_clip(&s_fb);
    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 16, 4 }, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(64, count_black());
}

static void test_clip_is_intersected_with_the_framebuffer(void)
{
    gfx_set_clip(&s_fb, (gfx_rect_t){ 10, 2, 100, 100 });
    TEST_ASSERT_EQUAL_INT(10, s_fb.clip.x);
    TEST_ASSERT_EQUAL_INT(2, s_fb.clip.y);
    TEST_ASSERT_EQUAL_INT(6, s_fb.clip.w);
    TEST_ASSERT_EQUAL_INT(2, s_fb.clip.h);
    gfx_set_clip(&s_fb, (gfx_rect_t){ 20, 20, 5, 5 });
    TEST_ASSERT_EQUAL_INT(0, s_fb.clip.w);
    TEST_ASSERT_EQUAL_INT(0, s_fb.clip.h);
    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 16, 4 }, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(0, count_black());
}

static void test_rect_outline_draws_each_pixel_once(void)
{
    gfx_rect(&s_fb, (gfx_rect_t){ 0, 0, 4, 3 }, GFX_INVERT);
    TEST_ASSERT_EQUAL_INT(10, count_black());
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 0, 0));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 3, 2));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 1, 1));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 2, 1));
}

static void test_line_includes_both_endpoints_in_either_direction(void)
{
    gfx_line(&s_fb, 3, 3, 0, 0, GFX_BLACK);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, i, i));
    }
    gfx_line(&s_fb, 15, 0, 15, 3, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(8, count_black());
}

static void test_pbm_has_header_and_raster(void)
{
    uint8_t out[16];
    gfx_pixel(&s_fb, 0, 0, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(16, (int)gfx_pbm_size(&s_fb));
    TEST_ASSERT_EQUAL_INT(16, (int)gfx_pbm_encode(&s_fb, out, sizeof(out)));
    TEST_ASSERT_EQUAL_MEMORY("P4\n16 4\n", out, 8);
    TEST_ASSERT_EQUAL_HEX8(0x80, out[8]);
    TEST_ASSERT_EQUAL_INT(0, (int)gfx_pbm_encode(&s_fb, out, 15));
}

/* A clip assigned by hand may reach past the buffer; pixels there must still be dropped. */
static void test_pixel_ignores_a_clip_that_reaches_outside_the_buffer(void)
{
    s_fb.clip = (gfx_rect_t){ -8, -8, 64, 64 };
    gfx_pixel(&s_fb, -1, 0, GFX_BLACK);
    gfx_pixel(&s_fb, 16, 3, GFX_BLACK);
    gfx_pixel(&s_fb, 0, 4, GFX_BLACK);
    assert_guards_intact();
    TEST_ASSERT_EQUAL_INT(0, count_black());
}

/* The review found that lines stepped their whole unclipped length and 2*err could overflow. */
static void test_huge_line_draws_only_its_visible_part(void)
{
    gfx_line(&s_fb, -2000000000, 2, 2000000000, 2, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(16, count_black());
    for (int x = 0; x < 16; x++) {
        TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, x, 2));
    }
    gfx_line(&s_fb, -50, -50, -10, -40, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(16, count_black());
    assert_guards_intact();
}

static uint8_t s_big[16 * 16 / 8];
static gfx_fb_t s_big_fb;

static int count_big(void)
{
    int n = 0;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            n += gfx_get_pixel(&s_big_fb, x, y) ? 1 : 0;
        }
    }
    return n;
}

static void big_setup(void)
{
    memset(s_big, 0, sizeof(s_big));
    gfx_fb_init(&s_big_fb, s_big, 16, 16);
}

static void test_circle_is_symmetric_and_draws_each_pixel_once(void)
{
    big_setup();
    gfx_circle(&s_big_fb, 8, 8, 5, GFX_INVERT);
    int n = count_big();
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_big_fb, 13, 8));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_big_fb, 3, 8));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_big_fb, 8, 3));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_big_fb, 8, 13));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_big_fb, 8, 8));
    for (int y = 1; y < 16; y++) {
        for (int x = 1; x < 16; x++) { /* mirror images around (8, 8) */
            TEST_ASSERT_EQUAL(gfx_get_pixel(&s_big_fb, x, y), gfx_get_pixel(&s_big_fb, 16 - x, y));
            TEST_ASSERT_EQUAL(gfx_get_pixel(&s_big_fb, x, y), gfx_get_pixel(&s_big_fb, y, x));
        }
    }
    gfx_circle(&s_big_fb, 8, 8, 5, GFX_INVERT); /* a pixel drawn twice would survive the second pass */
    TEST_ASSERT_EQUAL_INT(0, count_big());
    TEST_ASSERT_TRUE(n > 20);
}

static void test_filled_circle_covers_its_outline(void)
{
    big_setup();
    gfx_circle(&s_big_fb, 8, 8, 5, GFX_BLACK);
    int outline = count_big();
    big_setup();
    gfx_fill_circle(&s_big_fb, 8, 8, 5, GFX_BLACK);
    int filled = count_big();
    TEST_ASSERT_INT_WITHIN(8, 95, filled); /* pi * 5.5^2: the fill reaches the outline's rim */
    gfx_circle(&s_big_fb, 8, 8, 5, GFX_INVERT);
    TEST_ASSERT_EQUAL_INT(filled - outline, count_big()); /* every outline pixel was inside the fill */
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_big_fb, 8, 8));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_big_fb, 14, 8));
}

static void test_bitmap_draws_ink_only_and_clips(void)
{
    static const uint8_t bits[] = { 0xA0, 0x40, 0xA0 }; /* #.# / .#. / #.# */
    const gfx_bitmap_t x_mark = { bits, 3, 3 };
    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 16, 4 }, GFX_BLACK);
    gfx_bitmap(&s_fb, 1, 0, &x_mark, GFX_WHITE);
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 1, 0));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 0)); /* not ink: left alone */
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 2, 1));
    TEST_ASSERT_EQUAL_INT(64 - 5, count_black());
    gfx_bitmap(&s_fb, 14, 2, &x_mark, GFX_WHITE); /* runs off the right and bottom edges */
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 14, 2));
    assert_guards_intact();
    gfx_bitmap(&s_fb, 0, 0, NULL, GFX_WHITE);
}

static int count_in(const gfx_fb_t *fb)
{
    int n = 0;
    for (int y = 0; y < fb->height; y++) {
        for (int x = 0; x < fb->width; x++) {
            n += gfx_get_pixel(fb, x, y);
        }
    }
    return n;
}

static void test_a_filled_triangle_covers_its_inside_in_any_order_and_clips(void)
{
    static uint8_t buf[32 * 32 / 8];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 32, 32);
    gfx_clear(&fb, GFX_WHITE);
    gfx_fill_triangle(&fb, 2, 2, 20, 2, 2, 20, GFX_BLACK); /* legs of 19 px: 19 + 18 + ... + 1 */
    TEST_ASSERT_EQUAL_INT(190, count_in(&fb));
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 2, 2) && gfx_get_pixel(&fb, 20, 2) && gfx_get_pixel(&fb, 2, 20));
    TEST_ASSERT_FALSE(gfx_get_pixel(&fb, 12, 12)); /* beyond the hypotenuse x + y = 22 */
    gfx_clear(&fb, GFX_WHITE);
    gfx_fill_triangle(&fb, 2, 20, 20, 2, 2, 2, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(190, count_in(&fb));
    gfx_clear(&fb, GFX_WHITE);
    gfx_fill_triangle(&fb, 3, 5, 9, 5, 6, 5, GFX_BLACK); /* flat: a line */
    TEST_ASSERT_EQUAL_INT(7, count_in(&fb));
    gfx_fill_triangle(&s_fb, -100, -100, 300, -100, -100, 300, GFX_BLACK); /* far past the 16 x 4 buffer */
    TEST_ASSERT_EQUAL_INT(64, count_black());
    assert_guards_intact();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pixel_bits_are_msb_first_row_major);
    RUN_TEST(test_white_clears_and_invert_toggles);
    RUN_TEST(test_drawing_outside_the_framebuffer_changes_nothing);
    RUN_TEST(test_huge_rectangles_are_clipped_to_the_framebuffer);
    RUN_TEST(test_clip_limits_drawing);
    RUN_TEST(test_clip_is_intersected_with_the_framebuffer);
    RUN_TEST(test_rect_outline_draws_each_pixel_once);
    RUN_TEST(test_line_includes_both_endpoints_in_either_direction);
    RUN_TEST(test_pbm_has_header_and_raster);
    RUN_TEST(test_pixel_ignores_a_clip_that_reaches_outside_the_buffer);
    RUN_TEST(test_huge_line_draws_only_its_visible_part);
    RUN_TEST(test_circle_is_symmetric_and_draws_each_pixel_once);
    RUN_TEST(test_filled_circle_covers_its_outline);
    RUN_TEST(test_bitmap_draws_ink_only_and_clips);
    RUN_TEST(test_a_filled_triangle_covers_its_inside_in_any_order_and_clips);
    return UNITY_END();
}
