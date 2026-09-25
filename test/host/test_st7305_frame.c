#include <stdint.h>
#include <string.h>

#include "st7305_frame.h"
#include "unity.h"

static uint8_t s_canonical[ST7305_FRAME_BYTES];
static uint8_t s_panel[ST7305_FRAME_BYTES];
static uint8_t s_expected[ST7305_FRAME_BYTES];

void setUp(void) {}
void tearDown(void) {}

/* Transcription of the vendor's landscape mapping (Waveshare display_bsp.cpp: InitLandscapeLUT and
 * RLCD_SetPixel, where ColorWhite sets the bit). */
static void vendor_set_pixel(uint8_t *panel, int x, int y, int white)
{
    int inv_y = 300 - 1 - y;
    int block_y = inv_y >> 2;
    int local_y = inv_y & 3;
    int byte_x = x >> 1;
    int local_x = x & 1;
    int index = byte_x * (300 >> 2) + block_y;
    int bit = 7 - ((local_y << 1) | local_x);
    if (white) {
        panel[index] |= (uint8_t)(1 << bit);
    } else {
        panel[index] &= (uint8_t)~(1 << bit);
    }
}

static void set_black(int x, int y)
{
    s_canonical[y * 50 + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
}

static void vendor_expected_from_canonical(void)
{
    for (int y = 0; y < 300; y++) {
        for (int x = 0; x < 400; x++) {
            int black = (s_canonical[y * 50 + (x >> 3)] >> (7 - (x & 7))) & 1;
            vendor_set_pixel(s_expected, x, y, !black);
        }
    }
}

static void test_white_frame_is_all_ones_on_the_panel(void)
{
    memset(s_canonical, 0x00, sizeof(s_canonical));
    st7305_frame_to_panel(s_canonical, s_panel);
    for (size_t i = 0; i < sizeof(s_panel); i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, s_panel[i]);
    }
}

static void test_black_frame_is_all_zeros_on_the_panel(void)
{
    memset(s_canonical, 0xFF, sizeof(s_canonical));
    st7305_frame_to_panel(s_canonical, s_panel);
    for (size_t i = 0; i < sizeof(s_panel); i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, s_panel[i]);
    }
}

static void test_corner_pixels_land_where_the_vendor_puts_them(void)
{
    const int points[][2] = { { 0, 0 }, { 399, 0 }, { 0, 299 }, { 399, 299 }, { 1, 1 }, { 200, 150 } };
    for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
        memset(s_canonical, 0x00, sizeof(s_canonical));
        set_black(points[i][0], points[i][1]);
        vendor_expected_from_canonical();
        st7305_frame_to_panel(s_canonical, s_panel);
        TEST_ASSERT_EQUAL_MEMORY(s_expected, s_panel, sizeof(s_panel));
    }
}

static void test_random_frame_matches_the_vendor_formula(void)
{
    uint32_t state = 12345u;
    for (size_t i = 0; i < sizeof(s_canonical); i++) {
        state = state * 1103515245u + 12345u;
        s_canonical[i] = (uint8_t)(state >> 16);
    }
    vendor_expected_from_canonical();
    st7305_frame_to_panel(s_canonical, s_panel);
    TEST_ASSERT_EQUAL_MEMORY(s_expected, s_panel, sizeof(s_panel));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_white_frame_is_all_ones_on_the_panel);
    RUN_TEST(test_black_frame_is_all_zeros_on_the_panel);
    RUN_TEST(test_corner_pixels_land_where_the_vendor_puts_them);
    RUN_TEST(test_random_frame_matches_the_vendor_formula);
    return UNITY_END();
}
