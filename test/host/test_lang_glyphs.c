#include <stdio.h>

#include "gfx.h"
#include "gfx_fonts.h"
#include "lang.h"
#include "unity.h"

/* Every text of every language pack must render in the text fonts without the missing-glyph box:
 * a string may only use characters the fonts carry (spec §4.4). */

void setUp(void) {}
void tearDown(void) {}

static void check_text(const lang_t *lang, const char *what, int index, const char *text)
{
    static const struct {
        const char *name;
        const gfx_font_t *font;
    } k_fonts[] = {
        { "sans_12", &gfx_font_sans_12 },           { "sans_16", &gfx_font_sans_16 },
        { "sans_20", &gfx_font_sans_20 },           { "sans_bold_16", &gfx_font_sans_bold_16 },
        { "sans_bold_20", &gfx_font_sans_bold_20 }, { "sans_bold_28", &gfx_font_sans_bold_28 },
    };
    for (size_t f = 0; f < sizeof(k_fonts) / sizeof(k_fonts[0]); f++) {
        const char *p = text;
        for (uint32_t cp = gfx_utf8_next(&p); cp != 0; cp = gfx_utf8_next(&p)) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s %s %d: U+%04X missing in %s", lang->code, what, index, (unsigned)cp,
                     k_fonts[f].name);
            TEST_ASSERT_TRUE_MESSAGE(gfx_font_has_glyph(k_fonts[f].font, cp), msg);
        }
    }
}

static void test_every_pack_text_has_its_glyphs(void)
{
    const char *const codes[] = { "en", "cs" };
    for (int c = 0; c < 2; c++) {
        const lang_t *lang = lang_get(codes[c]);
        for (int id = 0; id < LS_COUNT; id++) {
            check_text(lang, "string", id, lang_str(lang, (lang_str_t)id));
        }
        for (int i = 0; i < 7; i++) {
            check_text(lang, "weekday", i, lang->weekdays[i]);
            check_text(lang, "weekday_short", i, lang->weekdays_short[i]);
        }
        for (int i = 0; i < 12; i++) {
            check_text(lang, "month", i, lang->months[i]);
            check_text(lang, "month_short", i, lang->months_short[i]);
        }
        for (int i = 0; i < 8; i++) {
            check_text(lang, "moon", i, lang->moon_phases[i]);
            check_text(lang, "moon_short", i, lang->moon_phases_short[i]);
        }
    }
}

static void test_every_holiday_name_has_its_glyphs(void)
{
    const lang_t *cs = lang_get("cs");
    for (int month = 1; month <= 12; month++) {
        for (int day = 1; day <= 31; day++) {
            const char *name = cs->holiday(2026, month, day);
            if (name != NULL) {
                check_text(cs, "holiday", month * 100 + day, name);
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_pack_text_has_its_glyphs);
    RUN_TEST(test_every_holiday_name_has_its_glyphs);
    return UNITY_END();
}
