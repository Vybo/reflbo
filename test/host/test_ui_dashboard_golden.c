#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "unity.h"

/* Each fixture must match test/host/golden/dash_<name>.pbm byte for byte. After an intentional
 * change: build-host/render_dashboard <name> test/host/golden/dash_<name>.pbm for each fixture,
 * look at the PNGs (python3 tools/render.py), commit. */

static uint8_t s_buf[400 * 300 / 8];
static uint8_t s_pbm[16000];
static uint8_t s_golden[16000];

void setUp(void) {}
void tearDown(void) {}

static void check(const char *name)
{
    ui_context_t ctx;
    ui_preset_t preset;
    TEST_ASSERT_TRUE_MESSAGE(fixture_dashboard(name, &ctx, &preset), name);
    gfx_fb_t fb;
    gfx_fb_init(&fb, s_buf, 400, 300);
    ui_draw_dashboard(&fb, &ctx, &preset);
    size_t n = gfx_pbm_encode(&fb, s_pbm, sizeof(s_pbm));

    char path[256];
    snprintf(path, sizeof(path), "%s/dash_%s.pbm", GOLDEN_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    size_t golden = fread(s_golden, 1, sizeof(s_golden), f);
    fclose(f);
    if (golden != n || memcmp(s_golden, s_pbm, n) != 0) {
        char actual[64];
        snprintf(actual, sizeof(actual), "dash_%s.actual.pbm", name);
        FILE *out = fopen(actual, "wb");
        if (out != NULL) {
            fwrite(s_pbm, 1, n, out);
            fclose(out);
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)n, (int)golden, path);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_golden, s_pbm, n, path);
}

static void test_every_fixture_matches_its_golden(void)
{
    for (size_t i = 0; i < sizeof(k_dashboard_fixtures) / sizeof(k_dashboard_fixtures[0]); i++) {
        check(k_dashboard_fixtures[i]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_fixture_matches_its_golden);
    return UNITY_END();
}
