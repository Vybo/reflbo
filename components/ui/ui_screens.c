#include "ui_screens.h"

#include <stdio.h>

#include "gfx_fonts.h"
#include "ui_internal.h"

void ui_draw_toast(gfx_fb_t *fb, const char *text)
{
    const gfx_font_t *f = &gfx_font_sans_bold_20;
    char fit[96];
    int w = gfx_text_ellipsize(f, text, fb->width - 60, fit, sizeof(fit)) + 40;
    gfx_rect_t box = { (int16_t)((fb->width - w) / 2), (int16_t)(fb->height - 62), (int16_t)w, 44 };
    gfx_reset_clip(fb);
    /* a white rim keeps the box visible over black content, such as an inverted preset */
    gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(box.x - 3), (int16_t)(box.y - 3), (int16_t)(box.w + 6),
                                    (int16_t)(box.h + 6) },
                  GFX_WHITE);
    gfx_fill_rect(fb, box, GFX_BLACK);
    gfx_text_in_rect(fb, f, box, GFX_ALIGN_CENTER, fit, GFX_WHITE);
}

void ui_draw_critical(gfx_fb_t *fb, const ui_context_t *ctx)
{
    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    ui_draw_battery(fb, 130, 44, 140, 64, 0);
    gfx_text_in_rect(fb, &gfx_font_sans_bold_28, (gfx_rect_t){ 0, 132, fb->width, 36 }, GFX_ALIGN_CENTER,
                     lang_str(ctx->lang, LS_BATTERY_EMPTY), GFX_BLACK);
    gfx_text_in_rect(fb, &gfx_font_sans_20, (gfx_rect_t){ 0, 172, fb->width, 28 }, GFX_ALIGN_CENTER,
                     lang_str(ctx->lang, LS_CHARGE_ME), GFX_BLACK);
    if (ctx->time_valid) { /* when the screen was drawn: it stays up while the battery recovers */
        ui_value_t t, d;
        ui_resolve(ctx, UI_FIELD_TIME_CLOCK, &t);
        ui_resolve(ctx, UI_FIELD_DATE_DAY, &d);
        char line[sizeof(t.text) + sizeof(t.unit) + sizeof(d.extra) + 8];
        snprintf(line, sizeof(line), "%s%s%s \xC2\xB7 %s", t.text, t.unit[0] ? " " : "", t.unit, d.extra);
        gfx_text_in_rect(fb, &gfx_font_sans_16, (gfx_rect_t){ 0, 244, fb->width, 24 }, GFX_ALIGN_CENTER, line,
                         GFX_BLACK);
    }
}
