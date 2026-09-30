#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "ui_menu.h"

/* The menu screen (spec §5.7): a title bar, up to seven rows, and the button hints. */

#define HEADER_H 30
#define ROW_Y0 36
#define ROW_H 34
#define ROWS 7
#define FOOTER_Y 282

static void draw_header(gfx_fb_t *fb, const char *title)
{
    gfx_fill_rect(fb, (gfx_rect_t){ 0, 0, fb->width, HEADER_H }, GFX_BLACK);
    char fit[64];
    gfx_text_ellipsize(&gfx_font_sans_bold_20, title, fb->width - 24, fit, sizeof(fit));
    gfx_text_in_rect(fb, &gfx_font_sans_bold_20, (gfx_rect_t){ 12, 0, (int16_t)(fb->width - 24), HEADER_H },
                     GFX_ALIGN_LEFT, fit, GFX_WHITE);
}

static void draw_hints(gfx_fb_t *fb, const char *hints)
{
    gfx_hline(fb, 0, FOOTER_Y - 4, fb->width, GFX_BLACK);
    char fit[96];
    gfx_text_ellipsize(&gfx_font_sans_12, hints, fb->width - 12, fit, sizeof(fit));
    gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ 0, FOOTER_Y, fb->width, (int16_t)(fb->height - FOOTER_Y) },
                     GFX_ALIGN_CENTER, fit, GFX_BLACK);
}

static void draw_list(gfx_fb_t *fb, const ui_menu_t *m, const ui_menu_model_t *model, const lang_t *lang)
{
    ui_menu_item_t items[UI_MI_COUNT];
    int n = ui_menu_visible(m, model, items, UI_MI_COUNT);
    int first = m->cursor >= ROWS ? m->cursor - ROWS + 1 : 0;
    const gfx_font_t *f = &gfx_font_sans_20;
    for (int row = 0; row < ROWS && first + row < n; row++) {
        int i = first + row;
        ui_menu_item_t item = items[i];
        gfx_rect_t r = { 6, (int16_t)(ROW_Y0 + row * ROW_H), (int16_t)(fb->width - 12), ROW_H - 2 };
        bool cursor = i == m->cursor;
        bool editing = cursor && m->mode == UI_MENU_EDIT;
        gfx_color_t ink = cursor && !editing ? GFX_WHITE : GFX_BLACK;
        if (cursor && !editing) {
            gfx_fill_rect(fb, r, GFX_BLACK);
        }
        char value[48];
        if (ui_menu_is_section(item)) {
            snprintf(value, sizeof(value), "\xE2\x86\x92"); /* a section: → */
        } else {
            ui_menu_value_text(item, editing ? m->edit : model->value[item], model, lang, value, sizeof(value));
        }
        /* Both fit, or the value is shortened: a zone the web UI chose can be long. A short label
         * keeps its width; a long one gets half the room. */
        const char *name = ui_menu_label(item, lang);
        int room = r.w - 36, name_w = gfx_text_width(f, name);
        int value_w = value[0] ? gfx_text_width(f, value) : 0;
        int value_max = room - (name_w < room / 2 ? name_w : room / 2);
        if (value_w > value_max) {
            char whole[sizeof(value)];
            memcpy(whole, value, sizeof(whole));
            value_w = gfx_text_ellipsize(f, whole, value_max, value, sizeof(value));
        }
        char label[48];
        gfx_text_ellipsize(f, name, room - value_w, label, sizeof(label));
        gfx_text_in_rect(fb, f, (gfx_rect_t){ (int16_t)(r.x + 8), r.y, (int16_t)(r.w - 16), r.h }, GFX_ALIGN_LEFT,
                         label, ink);
        if (value[0] == '\0') {
            continue;
        }
        gfx_rect_t vr = { (int16_t)(r.x + r.w - 8 - value_w), r.y, (int16_t)value_w, r.h };
        if (editing) { /* the value being edited, inverted in a box */
            gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(vr.x - 8), (int16_t)(r.y + 2), (int16_t)(value_w + 16),
                                            (int16_t)(r.h - 4) },
                          GFX_BLACK);
            gfx_text_in_rect(fb, f, vr, GFX_ALIGN_LEFT, value, GFX_WHITE);
        } else {
            gfx_text_in_rect(fb, f, vr, GFX_ALIGN_LEFT, value, ink);
        }
    }
    draw_hints(fb, lang_str(lang, m->mode == UI_MENU_EDIT ? LS_HINT_EDIT : LS_HINT_BROWSE));
}

/* One field of the date-time editor, inverted when it is the one being edited. */
static int draw_field(gfx_fb_t *fb, const gfx_font_t *f, int x, int baseline, const char *text, bool active)
{
    int w = gfx_text_width(f, text);
    if (active) {
        gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x - 4), (int16_t)(baseline - f->ascent - 2), (int16_t)(w + 8),
                                        (int16_t)(f->line_height + 2) },
                      GFX_BLACK);
    }
    gfx_text(fb, f, x, baseline, text, active ? GFX_WHITE : GFX_BLACK);
    return w;
}

static void draw_datetime(gfx_fb_t *fb, const ui_menu_t *m, const lang_t *lang)
{
    const gfx_font_t *f = &gfx_font_num_cb_48;
    char day[12], month[12], year[12], hour[12], minute[12]; /* room for any int, as GCC checks */
    snprintf(day, sizeof(day), "%02d", m->dt.tm_mday);
    snprintf(month, sizeof(month), "%02d", m->dt.tm_mon + 1);
    snprintf(year, sizeof(year), "%04d", m->dt.tm_year + 1900);
    snprintf(hour, sizeof(hour), "%02d", m->dt.tm_hour);
    snprintf(minute, sizeof(minute), "%02d", m->dt.tm_min);
    int gap = 12, dot = gfx_text_width(f, ".");
    int date_w = gfx_text_width(f, day) + gfx_text_width(f, month) + gfx_text_width(f, year) + 2 * dot + 4 * gap;
    int x = (fb->width - date_w) / 2, baseline = 128;
    x += draw_field(fb, f, x, baseline, day, m->dt_field == 0) + gap;
    x = gfx_text(fb, f, x, baseline, ".", GFX_BLACK) + gap;
    x += draw_field(fb, f, x, baseline, month, m->dt_field == 1) + gap;
    x = gfx_text(fb, f, x, baseline, ".", GFX_BLACK) + gap;
    draw_field(fb, f, x, baseline, year, m->dt_field == 2);
    int colon = gfx_text_width(f, ":");
    int time_w = gfx_text_width(f, hour) + gfx_text_width(f, minute) + colon + 2 * gap;
    x = (fb->width - time_w) / 2;
    baseline = 220;
    x += draw_field(fb, f, x, baseline, hour, m->dt_field == 3) + gap;
    x = gfx_text(fb, f, x, baseline, ":", GFX_BLACK) + gap;
    draw_field(fb, f, x, baseline, minute, m->dt_field == 4);
    draw_hints(fb, lang_str(lang, LS_HINT_DATETIME));
}

/* The question on up to two centred lines, split at a space. */
static void draw_question(gfx_fb_t *fb, const char *text, int y)
{
    const gfx_font_t *f = &gfx_font_sans_bold_20;
    int max_w = fb->width - 24;
    char line1[96], line2[96];
    snprintf(line1, sizeof(line1), "%s", text);
    line2[0] = '\0';
    if (gfx_text_width(f, line1) > max_w) {
        for (char *space = strrchr(line1, ' '); space != NULL; space = strrchr(line1, ' ')) {
            *space = '\0';
            if (gfx_text_width(f, line1) <= max_w) {
                snprintf(line2, sizeof(line2), "%s", text + (space - line1) + 1);
                break;
            }
        }
    }
    char fit[96];
    gfx_text_ellipsize(f, line1, max_w, fit, sizeof(fit));
    gfx_text_in_rect(fb, f, (gfx_rect_t){ 0, (int16_t)y, fb->width, 28 }, GFX_ALIGN_CENTER, fit, GFX_BLACK);
    if (line2[0]) {
        gfx_text_ellipsize(f, line2, max_w, fit, sizeof(fit));
        gfx_text_in_rect(fb, f, (gfx_rect_t){ 0, (int16_t)(y + 30), fb->width, 28 }, GFX_ALIGN_CENTER, fit,
                         GFX_BLACK);
    }
}

void ui_draw_menu(gfx_fb_t *fb, const ui_menu_t *m, const ui_menu_model_t *model, const lang_t *lang)
{
    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    ui_menu_item_t item = ui_menu_current(m, model);
    switch (m->mode) {
    case UI_MENU_DATETIME:
        draw_header(fb, ui_menu_label(item, lang));
        draw_datetime(fb, m, lang);
        break;
    case UI_MENU_CONFIRM:
        draw_header(fb, ui_menu_label(item, lang));
        draw_question(fb, ui_menu_question(item, lang), 110);
        draw_hints(fb, lang_str(lang, LS_HINT_CONFIRM));
        break;
    default:
        draw_header(fb, ui_menu_label((ui_menu_item_t)m->section, lang));
        draw_list(fb, m, model, lang);
        break;
    }
}
