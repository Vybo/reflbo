#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "ui_fixtures.h"
#include "unity.h"

/* Each fixture must match test/host/golden/clock_<name>.pbm byte for byte. After an intentional
 * change: build-host/render_clock <name> test/host/golden/clock_<name>.pbm for each fixture, look
 * at the PNGs (python3 tools/render.py), commit. */

static uint8_t s_buf[400 * 300 / 8];
static uint8_t s_pbm[16000];
static uint8_t s_golden[16000];

void setUp(void) {}
void tearDown(void) {}

static void check(const char *name, ui_clock_t clock)
{
    gfx_fb_t fb;
    gfx_fb_init(&fb, s_buf, 400, 300);
    ui_draw_clock(&fb, &clock);
    size_t n = gfx_pbm_encode(&fb, s_pbm, sizeof(s_pbm));

    char path[256];
    snprintf(path, sizeof(path), "%s/clock_%s.pbm", GOLDEN_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    size_t golden = fread(s_golden, 1, sizeof(s_golden), f);
    fclose(f);
    if (golden != n || memcmp(s_golden, s_pbm, n) != 0) {
        char actual[64];
        snprintf(actual, sizeof(actual), "clock_%s.actual.pbm", name);
        FILE *out = fopen(actual, "wb");
        if (out != NULL) {
            fwrite(s_pbm, 1, n, out);
            fclose(out);
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)n, (int)golden, path);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_golden, s_pbm, n, path);
}

static void test_valid_time_with_readings(void)
{
    check("valid", fixture_clock_valid());
}

static void test_invalid_time_without_readings(void)
{
    check("invalid", fixture_clock_invalid());
}

static void test_below_zero_and_charging(void)
{
    check("cold", fixture_clock_cold());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_time_with_readings);
    RUN_TEST(test_invalid_time_without_readings);
    RUN_TEST(test_below_zero_and_charging);
    return UNITY_END();
}
