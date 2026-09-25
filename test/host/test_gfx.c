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
    return UNITY_END();
}
