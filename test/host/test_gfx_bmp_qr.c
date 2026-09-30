#include <string.h>

#include "gfx.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void test_a_bmp_has_its_headers_and_rows_bottom_up(void)
{
    static uint8_t buf[4];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 16, 2);
    gfx_clear(&fb, GFX_WHITE);
    gfx_pixel(&fb, 0, 0, GFX_BLACK);  /* top-left */
    gfx_pixel(&fb, 15, 1, GFX_BLACK); /* bottom-right */
    uint8_t out[80];
    TEST_ASSERT_EQUAL_UINT(70, gfx_bmp_size(&fb)); /* 62 bytes of headers, two 4-byte rows */
    TEST_ASSERT_EQUAL_UINT(70, gfx_bmp_encode(&fb, out, sizeof(out)));
    TEST_ASSERT_EQUAL_MEMORY("BM", out, 2);
    TEST_ASSERT_EQUAL_UINT32(70, le32(out + 2));
    TEST_ASSERT_EQUAL_UINT32(62, le32(out + 10));
    TEST_ASSERT_EQUAL_UINT32(16, le32(out + 18));
    TEST_ASSERT_EQUAL_UINT32(2, le32(out + 22));
    TEST_ASSERT_EQUAL_UINT8(1, out[28]); /* 1 bit per pixel */
    TEST_ASSERT_EQUAL_MEMORY("\xFF\xFF\xFF\x00\x00\x00\x00\x00", out + 54, 8);
    const uint8_t bottom[] = { 0x00, 0x01, 0x00, 0x00 }, top[] = { 0x80, 0x00, 0x00, 0x00 };
    TEST_ASSERT_EQUAL_MEMORY(bottom, out + 62, 4); /* the last row comes first */
    TEST_ASSERT_EQUAL_MEMORY(top, out + 66, 4);
    TEST_ASSERT_EQUAL_UINT(0, gfx_bmp_encode(&fb, out, 69));
}

static void test_the_screen_as_bmp_is_15662_bytes(void)
{
    static uint8_t buf[400 * 300 / 8];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    TEST_ASSERT_EQUAL_UINT(62 + 52 * 300, gfx_bmp_size(&fb));
}

/* A URL fits version 2 (25 modules); with the 4-module quiet zone at 2 px a module it is 66 px. */
static void test_a_qr_code_is_drawn_with_its_quiet_zone_and_finders(void)
{
    static uint8_t buf[13 * 100]; /* stride 13: (100 + 7) / 8 */
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 100, 100);
    gfx_clear(&fb, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(66, gfx_qr_side("http://192.168.4.1", 2));
    TEST_ASSERT_EQUAL_INT(66, gfx_qr(&fb, 10, 10, 2, "http://192.168.4.1"));
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 9, 9));        /* outside: untouched */
    TEST_ASSERT_FALSE(gfx_get_pixel(&fb, 10, 10));     /* the quiet zone is white */
    TEST_ASSERT_FALSE(gfx_get_pixel(&fb, 17, 17));
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 18, 18));      /* the top-left finder's corner */
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 18 + 6 * 2, 18));  /* its top-right corner */
    TEST_ASSERT_FALSE(gfx_get_pixel(&fb, 18 + 2, 18 + 2)); /* its white ring */
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 18 + 18 * 2, 18)); /* the top-right finder */
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 18, 18 + 18 * 2)); /* the bottom-left finder */
}

static void test_text_too_long_for_a_qr_code_draws_nothing(void)
{
    static uint8_t buf[13 * 100]; /* stride 13: (100 + 7) / 8 */
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 100, 100);
    char text[301];
    memset(text, 'x', 300);
    text[300] = '\0';
    TEST_ASSERT_EQUAL_INT(0, gfx_qr_side(text, 1));
    TEST_ASSERT_EQUAL_INT(0, gfx_qr(&fb, 0, 0, 1, text));
    TEST_ASSERT_EQUAL_INT(0, gfx_qr_side(NULL, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_bmp_has_its_headers_and_rows_bottom_up);
    RUN_TEST(test_the_screen_as_bmp_is_15662_bytes);
    RUN_TEST(test_a_qr_code_is_drawn_with_its_quiet_zone_and_finders);
    RUN_TEST(test_text_too_long_for_a_qr_code_draws_nothing);
    return UNITY_END();
}
