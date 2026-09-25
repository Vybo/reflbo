#include <string.h>

#include "gfx.h"
#include "unity.h"

/* Hand-made font: 'A' is a 3x3 arch, 'Ž' a 2x2 block, space has no ink. */
static const uint8_t s_bitmap[] = {
    0x40, 0xA0, 0xE0, /* 'A': .#. / #.# / ### */
    0xC0, 0xC0,       /* 'Ž': ## / ## */
};
static const gfx_glyph_t s_glyphs[] = {
    { 0x0020, 0, 0, 0, 0, 0, 2 },
    { 0x0041, 0, 3, 3, 0, -3, 4 },
    { 0x017D, 3, 2, 2, 0, -2, 3 },
};
static const gfx_font_t s_font = { s_bitmap, s_glyphs, 3, 3, 4 };

static uint8_t s_buf[16]; /* 16x8 */
static gfx_fb_t s_fb;

void setUp(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    gfx_fb_init(&s_fb, s_buf, 16, 8);
}

void tearDown(void) {}

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

static void test_utf8_decodes_multibyte_characters(void)
{
    const char *s = "Ž°€A";
    TEST_ASSERT_EQUAL_HEX32(0x017D, gfx_utf8_next(&s));
    TEST_ASSERT_EQUAL_HEX32(0x00B0, gfx_utf8_next(&s));
    TEST_ASSERT_EQUAL_HEX32(0x20AC, gfx_utf8_next(&s));
    TEST_ASSERT_EQUAL_HEX32(0x0041, gfx_utf8_next(&s));
    TEST_ASSERT_EQUAL_HEX32(0, gfx_utf8_next(&s));
}

static void test_malformed_utf8_becomes_replacement_and_stops_at_nul(void)
{
    const char *cases[] = { "\xFF", "\xC5", "\xE2\x82", "\xC0\x80", "\xED\xA0\x80" };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char *s = cases[i];
        TEST_ASSERT_EQUAL_HEX32(0xFFFD, gfx_utf8_next(&s));
        while (*s != '\0') {
            TEST_ASSERT_EQUAL_HEX32(0xFFFD, gfx_utf8_next(&s));
        }
        TEST_ASSERT_EQUAL_HEX32(0, gfx_utf8_next(&s));
    }
}

static void test_text_width_sums_advances(void)
{
    TEST_ASSERT_EQUAL_INT(10, gfx_text_width(&s_font, "A A"));
    TEST_ASSERT_EQUAL_INT(7, gfx_text_width(&s_font, "AŽ"));
    TEST_ASSERT_EQUAL_INT(0, gfx_text_width(&s_font, ""));
}

static void test_missing_glyph_uses_box_advance(void)
{
    TEST_ASSERT_TRUE(gfx_font_has_glyph(&s_font, 'A'));
    TEST_ASSERT_FALSE(gfx_font_has_glyph(&s_font, 'B'));
    TEST_ASSERT_EQUAL_INT(4, gfx_text_width(&s_font, "B"));
}

static void test_glyph_is_placed_relative_to_pen_and_baseline(void)
{
    TEST_ASSERT_EQUAL_INT(5, gfx_text(&s_fb, &s_font, 1, 4, "A", GFX_BLACK));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 1));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 1, 2));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 3, 2));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 1, 3));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 3));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 3, 3));
    TEST_ASSERT_EQUAL_INT(6, count_black());
}

static void test_missing_glyph_draws_a_hollow_box(void)
{
    TEST_ASSERT_EQUAL_INT(4, gfx_text(&s_fb, &s_font, 0, 4, "B", GFX_BLACK));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 1, 2));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 2));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 1, 3));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 3));
    TEST_ASSERT_EQUAL_INT(4, count_black());
}

static void test_text_in_rect_centres_horizontally_and_vertically(void)
{
    gfx_text_in_rect(&s_fb, &s_font, (gfx_rect_t){ 0, 0, 16, 8 }, GFX_ALIGN_CENTER, "A", GFX_BLACK);
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 7, 2));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 6, 4));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 8, 4));
    TEST_ASSERT_EQUAL_INT(6, count_black());
}

static void test_text_in_rect_aligns_right(void)
{
    gfx_text_in_rect(&s_fb, &s_font, (gfx_rect_t){ 0, 0, 16, 8 }, GFX_ALIGN_RIGHT, "A", GFX_BLACK);
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 13, 2));
    TEST_ASSERT_EQUAL_INT(6, count_black());
}

static void test_text_in_rect_clips_to_the_rect_and_restores_the_clip(void)
{
    gfx_text_in_rect(&s_fb, &s_font, (gfx_rect_t){ 0, 0, 5, 8 }, GFX_ALIGN_LEFT, "AA", GFX_BLACK);
    for (int y = 0; y < 8; y++) {
        for (int x = 5; x < 16; x++) {
            TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, x, y));
        }
    }
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 4, 4));
    TEST_ASSERT_EQUAL_INT(16, s_fb.clip.w);
    TEST_ASSERT_EQUAL_INT(8, s_fb.clip.h);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_utf8_decodes_multibyte_characters);
    RUN_TEST(test_malformed_utf8_becomes_replacement_and_stops_at_nul);
    RUN_TEST(test_text_width_sums_advances);
    RUN_TEST(test_missing_glyph_uses_box_advance);
    RUN_TEST(test_glyph_is_placed_relative_to_pen_and_baseline);
    RUN_TEST(test_missing_glyph_draws_a_hollow_box);
    RUN_TEST(test_text_in_rect_centres_horizontally_and_vertically);
    RUN_TEST(test_text_in_rect_aligns_right);
    RUN_TEST(test_text_in_rect_clips_to_the_rect_and_restores_the_clip);
    return UNITY_END();
}
