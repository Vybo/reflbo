#include "ui_clock.h"

#include <stdio.h>

#include "gfx_fonts.h"

#define STATUS_H   20
#define TIME_BASE  132 /* baseline of the large time */
#define DATE_BASE  170
#define ROW_TOP    188 /* separator above the bottom row */
#define CELLS      4

static const char *const k_weekdays[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                                          "Saturday" };
static const char *const k_months[] = { "January", "February", "March", "April", "May", "June", "July",
                                        "August", "September", "October", "November", "December" };

static void draw_battery_icon(gfx_fb_t *fb, int x, int y, const ui_clock_t *c)
{
    gfx_rect(fb, (gfx_rect_t){ (int16_t)x, (int16_t)y, 22, 11 }, GFX_BLACK);
    gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x + 22), (int16_t)(y + 3), 2, 5 }, GFX_BLACK);
    if (c->battery_valid) {
        int fill = c->battery_pct * 18 / 100;
        gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x + 2), (int16_t)(y + 2), (int16_t)fill, 7 }, GFX_BLACK);
    }
}

static void draw_status_bar(gfx_fb_t *fb, const ui_clock_t *c)
{
    const gfx_font_t *f = &gfx_font_sans_12;
    if (!c->time_valid) {
        gfx_fill_rect(fb, (gfx_rect_t){ 0, 0, 84, STATUS_H }, GFX_BLACK);
        gfx_text_in_rect(fb, &gfx_font_sans_16, (gfx_rect_t){ 6, 0, 78, STATUS_H }, GFX_ALIGN_LEFT, "Set time",
                         GFX_WHITE);
    }
    char text[16];
    if (!c->battery_valid) {
        snprintf(text, sizeof(text), "—");
    } else if (c->battery_state == UI_BATTERY_CHARGING) {
        snprintf(text, sizeof(text), "↑ %d%%", c->battery_pct);
    } else {
        snprintf(text, sizeof(text), "%d%%", c->battery_pct);
    }
    int icon_x = fb->width - 6 - 24;
    gfx_text_in_rect(fb, f, (gfx_rect_t){ (int16_t)(icon_x - 106), 0, 100, STATUS_H }, GFX_ALIGN_RIGHT, text,
                     GFX_BLACK);
    draw_battery_icon(fb, icon_x, 5, c);
    gfx_hline(fb, 0, STATUS_H, fb->width, GFX_BLACK);
}

static void draw_cell(gfx_fb_t *fb, int index, const char *label, const char *value, const char *unit)
{
    int w = fb->width / CELLS;
    int x = index * w;
    gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ (int16_t)x, ROW_TOP + 10, (int16_t)w, 16 }, GFX_ALIGN_CENTER,
                     label, GFX_BLACK);
    const gfx_font_t *vf = &gfx_font_sans_bold_28;
    const gfx_font_t *uf = &gfx_font_sans_16;
    int vw = gfx_text_width(vf, value);
    int uw = unit[0] ? gfx_text_width(uf, unit) + 2 : 0;
    int start = x + (w - vw - uw) / 2;
    int base = ROW_TOP + 64;
    int pen = gfx_text(fb, vf, start, base, value, GFX_BLACK);
    if (unit[0]) {
        gfx_text(fb, uf, pen + 2, base, unit, GFX_BLACK);
    }
    if (index > 0) {
        gfx_vline(fb, x, ROW_TOP + 10, 64, GFX_BLACK);
    }
}

void ui_draw_clock(gfx_fb_t *fb, const ui_clock_t *c)
{
    char buf[40];
    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    draw_status_bar(fb, c);

    if (c->time_valid) {
        snprintf(buf, sizeof(buf), "%02d:%02d", c->local.tm_hour, c->local.tm_min);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    const gfx_font_t *tf = &gfx_font_num_cb_130;
    gfx_text(fb, tf, (fb->width - gfx_text_width(tf, buf)) / 2, TIME_BASE, buf, GFX_BLACK);

    if (c->time_valid && c->local.tm_wday >= 0 && c->local.tm_wday < 7 && c->local.tm_mon >= 0 &&
        c->local.tm_mon < 12) {
        snprintf(buf, sizeof(buf), "%s %d %s", k_weekdays[c->local.tm_wday], c->local.tm_mday,
                 k_months[c->local.tm_mon]);
        const gfx_font_t *df = &gfx_font_sans_20;
        gfx_text(fb, df, (fb->width - gfx_text_width(df, buf)) / 2, DATE_BASE, buf, GFX_BLACK);
    }

    gfx_hline(fb, 12, ROW_TOP, fb->width - 24, GFX_BLACK);
    char value[16];
    if (c->env_valid) {
        int magnitude = c->temp_c10 < 0 ? -c->temp_c10 : c->temp_c10;
        snprintf(value, sizeof(value), "%s%d.%d", c->temp_c10 < 0 ? "-" : "", magnitude / 10, magnitude % 10);
        draw_cell(fb, 0, "Temperature", value, "°C");
        snprintf(value, sizeof(value), "%d", c->hum_pct);
        draw_cell(fb, 1, "Humidity", value, "%");
    } else {
        draw_cell(fb, 0, "Temperature", "—", "");
        draw_cell(fb, 1, "Humidity", "—", "");
    }
    if (c->battery_valid) {
        snprintf(value, sizeof(value), "%d", c->battery_pct);
        draw_cell(fb, 2, "Battery", value, "%");
        snprintf(value, sizeof(value), "%d.%02d", c->battery_mv / 1000, (c->battery_mv % 1000) / 10);
        draw_cell(fb, 3, "Voltage", value, "V");
    } else {
        draw_cell(fb, 2, "Battery", "—", "");
        draw_cell(fb, 3, "Voltage", "—", "");
    }
}
