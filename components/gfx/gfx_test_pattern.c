#include "gfx_test_pattern.h"

#include "gfx_fonts.h"

void gfx_draw_test_pattern(gfx_fb_t *fb)
{
    const gfx_font_t *label = &gfx_font_sans_bold_20;
    const int16_t w = fb->width;
    const int16_t h = fb->height;

    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    gfx_rect(fb, (gfx_rect_t){ 0, 0, w, h }, GFX_BLACK);
    gfx_fill_rect(fb, (gfx_rect_t){ 2, 2, 6, 6 }, GFX_BLACK); /* origin marker */

    gfx_text_in_rect(fb, label, (gfx_rect_t){ 10, 4, 60, 24 }, GFX_ALIGN_LEFT, "TL", GFX_BLACK);
    gfx_text_in_rect(fb, label, (gfx_rect_t){ (int16_t)(w - 70), 4, 60, 24 }, GFX_ALIGN_RIGHT, "TR", GFX_BLACK);
    gfx_text_in_rect(fb, label, (gfx_rect_t){ 10, (int16_t)(h - 28), 60, 24 }, GFX_ALIGN_LEFT, "BL", GFX_BLACK);
    gfx_text_in_rect(fb, label, (gfx_rect_t){ (int16_t)(w - 70), (int16_t)(h - 28), 60, 24 }, GFX_ALIGN_RIGHT,
                     "BR", GFX_BLACK);

    gfx_text_in_rect(fb, label, (gfx_rect_t){ 0, 34, w, 26 }, GFX_ALIGN_CENTER, "reflbo test pattern", GFX_BLACK);
    gfx_text_in_rect(fb, &gfx_font_sans_16, (gfx_rect_t){ 0, 64, w, 22 }, GFX_ALIGN_CENTER,
                     "Žluťoučký kůň úpěl ďábelské ódy", GFX_BLACK);
    gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ 0, 88, w, 18 }, GFX_ALIGN_CENTER,
                     "0123456789 °C % € – … ABC abc", GFX_BLACK);

    for (int x = 20; x <= 180; x += 10) {
        gfx_vline(fb, x, 120, 141, GFX_BLACK);
    }
    for (int y = 120; y <= 260; y += 10) {
        gfx_hline(fb, 20, y, 161, GFX_BLACK);
    }

    for (int y = 0; y < 60; y++) {
        for (int x = 0; x < 60; x++) {
            if (((x + y) & 1) == 0) {
                gfx_pixel(fb, 220 + x, 120 + y, GFX_BLACK); /* 1-px checkerboard */
            }
            if ((((x >> 2) + (y >> 2)) & 1) == 0) {
                gfx_pixel(fb, 300 + x, 120 + y, GFX_BLACK); /* 4-px checkerboard */
            }
        }
    }
    gfx_line(fb, 220, 200, 360, 260, GFX_BLACK);
    gfx_line(fb, 220, 260, 360, 200, GFX_BLACK);
}
