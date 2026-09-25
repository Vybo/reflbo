#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "gfx_test_pattern.h"
#include "unity.h"

/* The test pattern must match test/host/golden/test_pattern.pbm byte for byte. After an intentional
 * change: build-host/render_test_pattern test/host/golden/test_pattern.pbm, look at the PNG, commit. */

static uint8_t s_buf[400 * 300 / 8];
static uint8_t s_pbm[16000];
static uint8_t s_golden[16000];

void setUp(void) {}
void tearDown(void) {}

static void test_test_pattern_matches_the_golden_image(void)
{
    gfx_fb_t fb;
    gfx_fb_init(&fb, s_buf, 400, 300);
    gfx_draw_test_pattern(&fb);
    size_t n = gfx_pbm_encode(&fb, s_pbm, sizeof(s_pbm));

    FILE *f = fopen(GOLDEN_DIR "/test_pattern.pbm", "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "golden image missing: " GOLDEN_DIR "/test_pattern.pbm");
    size_t golden = fread(s_golden, 1, sizeof(s_golden), f);
    fclose(f);

    if (golden != n || memcmp(s_golden, s_pbm, n) != 0) {
        FILE *out = fopen("test_pattern.actual.pbm", "wb");
        if (out != NULL) {
            fwrite(s_pbm, 1, n, out);
            fclose(out);
        }
    }
    TEST_ASSERT_EQUAL_INT((int)n, (int)golden);
    TEST_ASSERT_EQUAL_MEMORY(s_golden, s_pbm, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_test_pattern_matches_the_golden_image);
    return UNITY_END();
}
