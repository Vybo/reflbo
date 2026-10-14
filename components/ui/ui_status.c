#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "gfx_icons.h"
#include "ui_internal.h"
#include "util_time.h"

void ui_sync_time_text(const ui_context_t *ctx, time_t at, bool brief, char *out, size_t size)
{
    struct tm local;
    localtime_r(&at, &local);
    int64_t days = ctx->local_day - util_days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    char hm[16];
    ui_clock_text(ctx, at, hm, sizeof(hm));
    if (days == 0) {
        snprintf(out, size, "%s", hm);
    } else if (days > 0 && days < 7) {
        snprintf(out, size, "%s%s%s", ctx->lang->weekdays_short[local.tm_wday], brief ? "" : " ", brief ? "" : hm);
    } else { /* a week or more ago, or a later day: the clock went back since */
        lang_format_date(ctx->lang, &local, LANG_DATE_SHORT, out, size);
    }
}

bool ui_sync_time_fit(const ui_context_t *ctx, time_t at, const gfx_font_t *font, int room, char *out, size_t size)
{
    for (int brief = 0; brief < 2; brief++) {
        ui_sync_time_text(ctx, at, brief != 0, out, size);
        if (gfx_text_width(font, out) <= room) {
            return true;
        }
    }
    snprintf(out, size, "%s", "");
    return false;
}

/* Status bar (spec §5.2): "Set time" or a stale warning on the left, then a globe while a phone is
 * logged in to the web UI (D20), the sync state and in sync mode `always` the Wi-Fi state, and the last
 * good sync's time if the preset asks, with a cloud before it when it stands alone (D43); an optional clock in the
 * middle, the battery on the right with its level, voltage or days left as the preset asks. */
void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, bool any_stale)
{
    ui_value_t bat, days;
    ui_resolve(ctx, UI_FIELD_BAT_LEVEL, &bat);
    ui_resolve(ctx, UI_FIELD_BAT_DAYS, &days);
    any_stale |= bat.state == UI_VALUE_STALE; /* the battery shown here counts too */
    int left = 4; /* where the next mark on the left goes */
    if (!ctx->time_valid) {
        const gfx_font_t *f = &gfx_font_sans_bold_16;
        const char *text = lang_str(ctx->lang, LS_SET_TIME);
        int w = gfx_text_width(f, text) + 12;
        gfx_fill_rect(fb, (gfx_rect_t){ 0, 0, (int16_t)w, UI_STATUS_H }, GFX_BLACK);
        gfx_text_in_rect(fb, f, (gfx_rect_t){ 6, 0, (int16_t)(w - 6), UI_STATUS_H }, GFX_ALIGN_LEFT, text, GFX_WHITE);
        left = w + 4;
    } else if (any_stale) {
        gfx_bitmap(fb, left, 2, &gfx_icon_stale_16, GFX_BLACK);
        left += 20;
    }
    if (ctx->web_session) {
        gfx_bitmap(fb, left, 2, &gfx_icon_web_16, GFX_BLACK);
        left += 20;
    }
    if (ctx->sync != UI_SYNC_IDLE) { /* spec §5.2: a sync running, or the last one failed */
        gfx_bitmap(fb, left, 2, ctx->sync == UI_SYNC_RUNNING ? &gfx_icon_sync_16 : &gfx_icon_sync_failed_16,
                   GFX_BLACK);
        left += 20;
    }
    if (ctx->wifi != UI_WIFI_NONE) { /* sync mode `always` (D19) */
        gfx_bitmap(fb, left, 2, ctx->wifi == UI_WIFI_ON ? &gfx_icon_wifi_16 : &gfx_icon_wifi_off_16, GFX_BLACK);
        left += 20;
    }

    if (preset->status_clock && ctx->time_valid) {
        ui_value_t t;
        ui_resolve(ctx, UI_FIELD_TIME_CLOCK, &t);
        char clock[sizeof(t.text) + sizeof(t.unit) + 1];
        snprintf(clock, sizeof(clock), "%s%s%s", t.text, t.unit[0] ? " " : "", t.unit);
        gfx_text_in_rect(fb, &gfx_font_sans_bold_16, (gfx_rect_t){ 120, 0, 160, UI_STATUS_H }, GFX_ALIGN_CENTER,
                         clock, GFX_BLACK);
    }

    int x = fb->width - 6 - 26;
    ui_draw_battery(fb, x, 5, 26, 11, bat.state == UI_VALUE_MISSING ? -1 : bat.percent);
    if (bat.battery == DS_BAT_CHARGING) {
        x -= 16;
        gfx_bitmap(fb, x, 2, &gfx_icon_bolt_16, GFX_BLACK);
    } else if (bat.state != UI_VALUE_MISSING && bat.percent <= UI_BATTERY_LOW_PCT) { /* spec §8: low */
        x -= 10;
        gfx_text(fb, &gfx_font_sans_bold_16, x + 2, 16, "!", GFX_BLACK);
    }
    char text[sizeof(bat.text) + sizeof(bat.extra) + sizeof(days.text) + sizeof(days.unit) + 12] = "";
    size_t n = 0;
    uint8_t parts = preset->status_battery;
    if (bat.state == UI_VALUE_MISSING) {
        snprintf(text, sizeof(text), "\xE2\x80\x94");
    } else {
        if (parts & UI_STATUS_BAT_PERCENT) {
            n += (size_t)snprintf(text + n, sizeof(text) - n, "%s%%", bat.text);
        }
        if ((parts & UI_STATUS_BAT_VOLTAGE) && n < sizeof(text)) {
            n += (size_t)snprintf(text + n, sizeof(text) - n, "%s%s", n ? "  " : "", bat.extra);
        }
        if ((parts & UI_STATUS_BAT_DAYS) && days.state != UI_VALUE_MISSING && n < sizeof(text)) {
            snprintf(text + n, sizeof(text) - n, "%s%s %s", n ? "  " : "", days.text, days.unit);
        }
    }
    char fit[sizeof(text)];
    gfx_text_ellipsize(&gfx_font_sans_12, text, 120, fit, sizeof(fit)); /* cut at the end, not the start */
    gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ (int16_t)(x - 124), 0, 120, UI_STATUS_H }, GFX_ALIGN_RIGHT,
                     fit, GFX_BLACK);
    if (preset->status_sync && ctx->time_valid && ctx->sync_ok_at != 0) { /* D43: after the marks */
        int right = preset->status_clock ? 120 : x - 124; /* the clock, or the battery's text */
        if (left == 4) { /* no mark before it: alone, the time could pass for the clock */
            gfx_bitmap(fb, left, 2, &gfx_icon_synced_16, GFX_BLACK);
            left += 20;
        }
        char when[48];
        if (ui_sync_time_fit(ctx, ctx->sync_ok_at, &gfx_font_sans_12, right - 4 - left, when, sizeof(when))) {
            gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ (int16_t)left, 0, (int16_t)(right - left), UI_STATUS_H },
                             GFX_ALIGN_LEFT, when, GFX_BLACK);
        }
    }
    gfx_hline(fb, 0, UI_STATUS_H, fb->width, GFX_BLACK);
}
