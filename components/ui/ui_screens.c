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

void ui_draw_message_banner(gfx_fb_t *fb, const ui_context_t *ctx)
{
    if (ctx->mqtt == NULL || !ha_store_banner(ctx->mqtt, ctx->now)) {
        return;
    }
    const gfx_font_t *f = &gfx_font_sans_bold_16;
    char line[HA_MESSAGE_LEN + 4];
    gfx_text_ellipsize(f, ctx->mqtt->message, fb->width - 24, line, sizeof(line));
    int h = 10 + f->line_height; /* one line: the slots above stay readable for the hours it may show */
    gfx_rect_t box = { 0, (int16_t)(fb->height - h), fb->width, (int16_t)h };
    gfx_reset_clip(fb);
    /* a white rim keeps it apart from black content, such as an inverted preset */
    gfx_fill_rect(fb, (gfx_rect_t){ 0, (int16_t)(box.y - 3), fb->width, 3 }, GFX_WHITE);
    gfx_fill_rect(fb, box, GFX_BLACK);
    gfx_text_in_rect(fb, f, box, GFX_ALIGN_CENTER, line, GFX_WHITE);
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

void ui_draw_first_run(gfx_fb_t *fb, const ui_context_t *ctx)
{
    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    gfx_fill_rect(fb, (gfx_rect_t){ 0, 0, fb->width, 30 }, GFX_BLACK);
    gfx_text_in_rect(fb, &gfx_font_sans_bold_20, (gfx_rect_t){ 12, 0, (int16_t)(fb->width - 24), 30 },
                     GFX_ALIGN_LEFT, lang_str(ctx->lang, LS_F_TITLE), GFX_WHITE);
    int y = 76;
    if (ctx->time_valid) { /* spec §5.5: the clock, when there is a time to show */
        ui_value_t t;
        ui_resolve(ctx, UI_FIELD_TIME_CLOCK, &t);
        const gfx_font_t *f = &gfx_font_num_cb_72;
        int w = gfx_text_width(f, t.text);
        int suffix_w = t.unit[0] ? gfx_text_width(&gfx_font_sans_bold_20, t.unit) + 6 : 0;
        int x = gfx_text(fb, f, (fb->width - w - suffix_w) / 2, 110, t.text, GFX_BLACK);
        if (suffix_w) {
            gfx_text(fb, &gfx_font_sans_bold_20, x + 6, 110, t.unit, GFX_BLACK);
        }
        y = 138;
    }
    const lang_str_t hints[] = { LS_F_WIFI, LS_F_MENU, LS_F_CONTINUE };
    for (int i = 0; i < 3; i++) {
        const gfx_font_t *f = i == 2 ? &gfx_font_sans_20 : &gfx_font_sans_bold_20;
        char line1[96], line2[96];
        ui_split_two_lines(f, lang_str(ctx->lang, hints[i]), fb->width - 24, line1, line2, sizeof(line1));
        gfx_text_in_rect(fb, f, (gfx_rect_t){ 0, (int16_t)y, fb->width, 28 }, GFX_ALIGN_CENTER, line1, GFX_BLACK);
        y += 26;
        if (line2[0] != '\0') {
            gfx_text_in_rect(fb, f, (gfx_rect_t){ 0, (int16_t)y, fb->width, 28 }, GFX_ALIGN_CENTER, line2,
                             GFX_BLACK);
            y += 26;
        }
        y += 12;
    }
}
