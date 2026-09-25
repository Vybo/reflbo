#include "gfx.h"
#include "gfx_fonts.h"
#include "unity.h"

static const gfx_font_t *const s_fonts[] = {
    &gfx_font_sans_12, &gfx_font_sans_16, &gfx_font_sans_20, &gfx_font_sans_bold_20,
};
#define FONT_COUNT (sizeof(s_fonts) / sizeof(s_fonts[0]))

void setUp(void) {}
void tearDown(void) {}

static void test_fonts_cover_czech_and_symbols(void)
{
    for (size_t f = 0; f < FONT_COUNT; f++) {
        const char *p = "ÁáČčĎďÉéĚěÍíŇňÓóŘřŠšŤťÚúŮůÝýŽž°µ²€–…→";
        uint32_t cp;
        while ((cp = gfx_utf8_next(&p)) != 0) {
            TEST_ASSERT_TRUE_MESSAGE(gfx_font_has_glyph(s_fonts[f], cp), "glyph missing");
        }
    }
}

static void test_glyphs_are_sorted_for_binary_search(void)
{
    for (size_t f = 0; f < FONT_COUNT; f++) {
        for (uint16_t i = 1; i < s_fonts[f]->glyph_count; i++) {
            TEST_ASSERT_TRUE(s_fonts[f]->glyphs[i - 1].codepoint < s_fonts[f]->glyphs[i].codepoint);
        }
    }
}

static void test_metrics_grow_with_size(void)
{
    TEST_ASSERT_TRUE(gfx_font_sans_12.line_height < gfx_font_sans_16.line_height);
    TEST_ASSERT_TRUE(gfx_font_sans_16.line_height < gfx_font_sans_20.line_height);
    for (size_t f = 0; f < FONT_COUNT; f++) {
        TEST_ASSERT_TRUE(s_fonts[f]->ascent > 0);
        TEST_ASSERT_TRUE(s_fonts[f]->ascent < s_fonts[f]->line_height);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fonts_cover_czech_and_symbols);
    RUN_TEST(test_glyphs_are_sorted_for_binary_search);
    RUN_TEST(test_metrics_grow_with_size);
    return UNITY_END();
}
