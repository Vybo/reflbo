#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "gfx_icons.h"
#include "ui_internal.h"

#define PLACEHOLDER "\xE2\x80\x94" /* em dash */
#define ARROW_UP "\xE2\x86\x91"
#define ARROW_DOWN "\xE2\x86\x93"
#define PI 3.14159265358979323846

typedef struct {
    const gfx_font_t *label; /* NULL: no label */
    const gfx_font_t *value; /* numbers */
    const gfx_font_t *text;  /* words, and "—" */
    const gfx_font_t *unit;
    int icon; /* icon size in px */
} ui_fonts_t;

static const ui_fonts_t k_fonts[] = {
    [UI_SIZE_S] = { NULL, &gfx_font_sans_bold_28, &gfx_font_sans_bold_16, &gfx_font_sans_16, 24 },
    [UI_SIZE_M] = { &gfx_font_sans_12, &gfx_font_num_cb_48, &gfx_font_sans_bold_20, &gfx_font_sans_16, 48 },
    [UI_SIZE_L] = { &gfx_font_sans_16, &gfx_font_num_cb_72, &gfx_font_sans_bold_28, &gfx_font_sans_bold_20, 48 },
    [UI_SIZE_XL] = { &gfx_font_sans_16, &gfx_font_num_cb_130, &gfx_font_sans_bold_28, &gfx_font_sans_bold_28, 48 },
};

/* Fonts for a number that doesn't fit its slot, largest first; each list starts with the size's
 * own value font (spec §5.3). */
static const gfx_font_t *const k_fit_s[] = { &gfx_font_sans_bold_28, &gfx_font_sans_bold_20, &gfx_font_sans_bold_16 };
static const gfx_font_t *const k_fit_m[] = { &gfx_font_num_cb_48, &gfx_font_sans_bold_28, &gfx_font_sans_bold_20 };
static const gfx_font_t *const k_fit_l[] = { &gfx_font_num_cb_72, &gfx_font_num_cb_48, &gfx_font_sans_bold_28 };
static const gfx_font_t *const k_fit_xl[] = { &gfx_font_num_cb_130, &gfx_font_num_cb_110, &gfx_font_num_cb_72,
                                              &gfx_font_num_cb_48 };

/* Height of a digit's ink, for centring numbers on what shows rather than on the line box. */
static int digit_height(const gfx_font_t *f)
{
    const gfx_glyph_t *g = gfx_font_glyph(f, '0');
    return g != NULL ? g->height : f->ascent;
}

static const gfx_bitmap_t *field_icon(ui_field_id_t field, int size)
{
    const gfx_bitmap_t *s24 = NULL, *s48 = NULL;
    switch (field) {
    case UI_FIELD_ENV_TEMP:
    case UI_FIELD_ENV_TEMP_MIN:
    case UI_FIELD_ENV_TEMP_MAX:
        s24 = &gfx_icon_thermometer_24, s48 = &gfx_icon_thermometer_48;
        break;
    case UI_FIELD_ENV_HUM:
        s24 = &gfx_icon_drop_24, s48 = &gfx_icon_drop_48;
        break;
    case UI_FIELD_ENV_DEW:
        s24 = &gfx_icon_dew_24, s48 = &gfx_icon_dew_48;
        break;
    case UI_FIELD_TIME_CLOCK:
        s24 = &gfx_icon_clock_24, s48 = &gfx_icon_clock_48;
        break;
    case UI_FIELD_DATE_DAY:
    case UI_FIELD_DATE_WEEK:
        s24 = &gfx_icon_calendar_24, s48 = &gfx_icon_calendar_48;
        break;
    case UI_FIELD_DATE_NAMEDAY:
        s24 = &gfx_icon_person_24, s48 = &gfx_icon_person_48;
        break;
    case UI_FIELD_DATE_HOLIDAY:
        s24 = &gfx_icon_celebration_24, s48 = &gfx_icon_celebration_48;
        break;
    case UI_FIELD_WX_NOW:
    case UI_FIELD_WX_TODAY:
    case UI_FIELD_WX_HOURLY:
    case UI_FIELD_WX_DAILY:
        s24 = &gfx_icon_cloud_24, s48 = &gfx_icon_cloud_48; /* missing: drawn as the placeholder */
        break;
    case UI_FIELD_SUN_TIMES:
        s24 = &gfx_icon_sunrise_24, s48 = &gfx_icon_sunrise_48;
        break;
    case UI_FIELD_AQ_INDEX:
        s24 = &gfx_icon_air_24, s48 = &gfx_icon_air_48;
        break;
    case UI_FIELD_AQ_PM25:
    case UI_FIELD_AQ_PM10:
        s24 = &gfx_icon_particles_24, s48 = &gfx_icon_particles_48;
        break;
    case UI_FIELD_AQ_UV:
        s24 = &gfx_icon_uv_24, s48 = &gfx_icon_uv_48;
        break;
    case UI_FIELD_POLLEN_TOP:
    case UI_FIELD_POLLEN_ALDER:
    case UI_FIELD_POLLEN_BIRCH:
    case UI_FIELD_POLLEN_GRASS:
    case UI_FIELD_POLLEN_MUGWORT:
    case UI_FIELD_POLLEN_OLIVE:
    case UI_FIELD_POLLEN_RAGWEED:
        s24 = &gfx_icon_pollen_24, s48 = &gfx_icon_pollen_48;
        break;
    default:
        break;
    }
    return size >= 48 ? s48 : s24;
}

/* Draws value, unit and trend arrow as one group centred on cx; returns the group's width. */
static int group_width(const ui_fonts_t *f, const gfx_font_t *vf, const ui_value_t *v, const char *value)
{
    int w = gfx_text_width(vf, value);
    if (v->unit[0]) {
        w += 2 + gfx_text_width(f->unit, v->unit);
    }
    if (v->trend) {
        w += 2 + gfx_text_width(f->unit, ARROW_UP);
    }
    return w;
}

static void draw_group(gfx_fb_t *fb, const ui_fonts_t *f, const gfx_font_t *vf, const ui_value_t *v,
                       const char *value, int x, int baseline)
{
    int pen = gfx_text(fb, vf, x, baseline, value, GFX_BLACK);
    if (v->unit[0]) {
        pen = gfx_text(fb, f->unit, pen + 2, baseline, v->unit, GFX_BLACK);
    }
    if (v->trend) {
        gfx_text(fb, f->unit, pen + 2, baseline, v->trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
    }
}

/* Picks the font and text for a number group so that it fits `max_w` (with `extra_w` beside it):
 * at each font, largest first, the value as it is, then without its decimals ("101" for "100.8").
 * If even the smallest font is too wide, the value is cut with an ellipsis into `buf`. */
static const gfx_font_t *fit_number(const ui_fonts_t *f, const gfx_font_t *const *fonts, int count,
                                    const ui_value_t *v, const char **value, int max_w, int extra_w, char *buf,
                                    size_t size)
{
    for (int i = 0; i < count; i++) {
        if (group_width(f, fonts[i], v, *value) + extra_w <= max_w) {
            return fonts[i];
        }
        if (v->short_text[0] && group_width(f, fonts[i], v, v->short_text) + extra_w <= max_w) {
            *value = v->short_text;
            return fonts[i];
        }
    }
    const gfx_font_t *vf = fonts[count - 1];
    int beside = group_width(f, vf, v, "") + extra_w; /* unit and trend arrow */
    gfx_text_ellipsize(vf, v->short_text[0] ? v->short_text : *value, max_w - beside, buf, size);
    *value = buf;
    return vf;
}

void ui_split_two_lines(const gfx_font_t *font, const char *text, int max_w, char *line1, char *line2, size_t size)
{
    line2[0] = '\0';
    if (gfx_text_width(font, text) <= max_w) {
        gfx_text_ellipsize(font, text, max_w, line1, size);
        return;
    }
    char first[96];
    snprintf(first, sizeof(first), "%s", text ? text : "");
    for (char *space = strrchr(first, ' '); space != NULL; space = strrchr(first, ' ')) {
        *space = '\0';
        if (gfx_text_width(font, first) <= max_w) {
            gfx_text_ellipsize(font, text + (space - first) + 1, max_w, line2, size);
            break;
        }
    }
    gfx_text_ellipsize(font, first, max_w, line1, size); /* no space that helps: one cut line */
}

void ui_format_age(const lang_t *lang, uint32_t age_s, char *out, size_t size)
{
    if (age_s < 3600) {
        snprintf(out, size, "%lu %s", (unsigned long)(age_s / 60), lang_str(lang, LS_MINUTES_UNIT));
    } else if (age_s < 86400) {
        snprintf(out, size, "%lu %s", (unsigned long)(age_s / 3600), lang_str(lang, LS_HOURS_UNIT));
    } else {
        snprintf(out, size, "%lu %s", (unsigned long)(age_s / 86400), lang_str(lang, LS_DAYS_UNIT));
    }
}

/* "⟲ 2 h" in the rect's bottom-right corner (spec §5.3). */
static void draw_age(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v, const lang_t *lang)
{
    char age[16];
    ui_format_age(lang, v->age_s, age, sizeof(age));
    const gfx_font_t *f = &gfx_font_sans_12;
    int w = gfx_text_width(f, age);
    int x = r.x + r.w - 6 - w;
    int baseline = r.y + r.h - 6 - (f->line_height - f->ascent);
    gfx_text(fb, f, x, baseline, age, GFX_BLACK);
    gfx_bitmap(fb, x - 18, baseline - 13, &gfx_icon_stale_16, GFX_BLACK);
}

static void draw_min_max_mark(gfx_fb_t *fb, const ui_value_t *v, int x, int y)
{
    if (v->field == UI_FIELD_ENV_TEMP_MIN || v->field == UI_FIELD_ENV_TEMP_MAX) {
        gfx_text(fb, &gfx_font_sans_bold_16, x, y, v->field == UI_FIELD_ENV_TEMP_MIN ? ARROW_DOWN : ARROW_UP,
                 GFX_BLACK);
    }
}

/* The small visual that stands for the field: an icon, a battery, or the Moon. */
static int draw_symbol(gfx_fb_t *fb, const ui_value_t *v, int x, int y, int size)
{
    if (v->kind == UI_FK_BATTERY) {
        int w = size * 3 / 2, h = size * 3 / 4;
        ui_draw_battery(fb, x, y + (size - h) / 2, w, h, v->state == UI_VALUE_MISSING ? -1 : v->percent);
        if (v->battery == DS_BAT_CHARGING) {
            gfx_bitmap(fb, x + w + 2, y + (size - 16) / 2, &gfx_icon_bolt_16, GFX_BLACK);
            return w + 18;
        }
        return w;
    }
    if (v->kind == UI_FK_MOON) {
        int r = size / 2 - 1;
        if (v->state == UI_VALUE_MISSING) {
            gfx_circle(fb, x + size / 2, y + size / 2, r, GFX_BLACK);
        } else {
            ui_draw_moon(fb, x + size / 2, y + size / 2, r, v->moon.age);
        }
        return size;
    }
    const gfx_bitmap_t *icon = field_icon(v->field, size);
    if (icon == NULL) {
        return 0;
    }
    gfx_bitmap(fb, x, y, icon, GFX_BLACK);
    draw_min_max_mark(fb, v, x + icon->width - 6, y + icon->height);
    return icon->width;
}

/* The value as text: the number for most kinds, a word or name otherwise. */
static const char *display_text(const ui_value_t *v, ui_size_t size)
{
    if (v->state == UI_VALUE_MISSING) {
        return PLACEHOLDER;
    }
    switch (v->kind) {
    case UI_FK_DATE:
        return size == UI_SIZE_S ? v->extra : v->text;
    case UI_FK_MOON:
        return v->text;
    default:
        return v->text;
    }
}

static bool numeric(const ui_value_t *v)
{
    return v->state != UI_VALUE_MISSING &&
           (v->kind == UI_FK_NUMBER || v->kind == UI_FK_TIME || v->kind == UI_FK_BATTERY);
}

static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
{
    const ui_fonts_t *f = &k_fonts[UI_SIZE_S];
    const gfx_font_t *vf = numeric(v) ? f->value : v->state == UI_VALUE_MISSING ? f->value : f->text;
    const char *value = display_text(v, UI_SIZE_S);
    ui_value_t shown = *v;
    if (!numeric(v)) {
        shown.unit[0] = '\0';
        shown.trend = 0;
    }
    char fit[48];
    if (r.w < 150) { /* narrow: symbol above, value below, the trend arrow beside the symbol */
        int sym_size = v->kind == UI_FK_MOON ? 28 : f->icon;
        int sym_w = v->kind == UI_FK_BATTERY ? sym_size * 3 / 2 : sym_size;
        int sym_x = r.x + (r.w - sym_w) / 2;
        draw_symbol(fb, v, sym_x, r.y + 12, sym_size);
        if (shown.trend) {
            gfx_text(fb, &gfx_font_sans_bold_16, sym_x + sym_w + 4, r.y + 12 + sym_size - 4,
                     shown.trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
            shown.trend = 0;
        }
        if (v->kind == UI_FK_MOON && v->state != UI_VALUE_MISSING) { /* the phase name, small */
            const char *name = gfx_text_width(&gfx_font_sans_12, v->text) <= r.w - 8 ? v->text : v->short_text;
            gfx_text_ellipsize(&gfx_font_sans_12, name, r.w - 8, fit, sizeof(fit));
            gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ r.x, (int16_t)(r.y + 12 + sym_size + 14), r.w, 20 },
                             GFX_ALIGN_CENTER, fit, GFX_BLACK);
            return;
        }
        int max_w = r.w - 8;
        if (!numeric(v) && gfx_text_width(vf, value) > max_w) { /* a name: two lines in the regular face */
            const gfx_font_t *tf = f->unit;
            char second[sizeof(fit)];
            ui_split_two_lines(tf, value, max_w, fit, second, sizeof(fit));
            int top = r.y + 12 + f->icon + 10;
            gfx_text_in_rect(fb, tf, (gfx_rect_t){ r.x, (int16_t)top, r.w, tf->line_height }, GFX_ALIGN_CENTER, fit,
                             GFX_BLACK);
            gfx_text_in_rect(fb, tf, (gfx_rect_t){ r.x, (int16_t)(top + tf->line_height), r.w, tf->line_height },
                             GFX_ALIGN_CENTER, second, GFX_BLACK);
            return;
        }
        if (!numeric(v)) {
            gfx_text_ellipsize(vf, value, max_w, fit, sizeof(fit));
            value = fit;
        } else {
            vf = fit_number(f, k_fit_s, 3, &shown, &value, max_w, 0, fit, sizeof(fit));
        }
        int w = group_width(f, vf, &shown, value);
        int baseline = r.y + 12 + f->icon + 14 + digit_height(vf);
        draw_group(fb, f, vf, &shown, value, r.x + (r.w - w) / 2, baseline);
        return;
    }
    int sym_w = draw_symbol(fb, v, r.x + 14, r.y + (r.h - f->icon) / 2, f->icon); /* wide: side by side */
    int x = r.x + 14 + sym_w + 10;
    if (!numeric(v)) {
        gfx_text_ellipsize(vf, value, r.x + r.w - 6 - x, fit, sizeof(fit));
        value = fit;
    } else {
        vf = fit_number(f, k_fit_s, 3, &shown, &value, r.x + r.w - 6 - x, 0, fit, sizeof(fit));
    }
    draw_group(fb, f, vf, &shown, value, x, r.y + (r.h + digit_height(vf)) / 2);
}

static void draw_labelled(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    const ui_fonts_t *f = &k_fonts[size];
    int top = r.y + 6;
    if (f->label != NULL && v->kind != UI_FK_TIME && !(size == UI_SIZE_M && v->kind == UI_FK_DATE)) {
        char label[40];
        int arrow_w = v->trend ? gfx_text_width(f->label, ARROW_UP) + 4 : 0;
        gfx_text_ellipsize(f->label, v->label, r.w - 12 - arrow_w, label, sizeof(label));
        int pen = gfx_text(fb, f->label, r.x + 6, top + f->label->ascent, label, GFX_BLACK);
        if (v->trend) { /* beside the label, where it doesn't widen the value */
            gfx_text(fb, f->label, pen + 4, top + f->label->ascent, v->trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
        }
        top += f->label->line_height;
    }
    gfx_rect_t body = { r.x, (int16_t)top, r.w, (int16_t)(r.y + r.h - top) };
    char fit[48];
    const char *value = display_text(v, size);

    if (v->kind == UI_FK_MOON && v->state != UI_VALUE_MISSING) { /* disc, then the phase name */
        int d = size == UI_SIZE_M ? 40 : 64;
        int cx = body.x + 10 + d / 2, cy = body.y + body.h / 2;
        ui_draw_moon(fb, cx, cy, d / 2 - 1, v->moon.age);
        int x = body.x + 20 + d;
        gfx_text_ellipsize(&gfx_font_sans_16, v->text, body.x + body.w - 6 - x, fit, sizeof(fit));
        gfx_text(fb, &gfx_font_sans_16, x, cy - 2, fit, GFX_BLACK);
        gfx_text(fb, &gfx_font_sans_bold_20, x, cy + 22, v->extra, GFX_BLACK);
        return;
    }
    if (!numeric(v)) { /* words: centred, cut to fit */
        const gfx_font_t *tf = v->state == UI_VALUE_MISSING ? &gfx_font_sans_bold_28 : f->text;
        if (v->kind == UI_FK_DATE && size == UI_SIZE_M) {
            tf = &gfx_font_sans_20;
            if (gfx_text_width(tf, value) > body.w - 12) {
                value = v->extra; /* the medium form: "Fri 25 Sep" */
            }
        }
        gfx_text_ellipsize(tf, value, body.w - 12, fit, sizeof(fit));
        gfx_text_in_rect(fb, tf, body, GFX_ALIGN_CENTER, fit, GFX_BLACK);
        return;
    }
    ui_value_t shown = *v;
    shown.trend = 0; /* drawn beside the label */
    if (v->kind == UI_FK_TIME) {
        shown.unit[0] = '\0'; /* AM/PM and seconds go beside the digits, smaller */
    }
    const gfx_font_t *side = size == UI_SIZE_XL ? &gfx_font_sans_bold_28 : &gfx_font_sans_bold_20;
    int extra_w = 0;
    if (v->kind == UI_FK_TIME) { /* AM/PM and the seconds share one column beside the digits */
        int unit_w = v->unit[0] ? gfx_text_width(side, v->unit) : 0;
        int sec_w = v->extra[0] ? gfx_text_width(side, v->extra) : 0;
        extra_w = unit_w || sec_w ? 6 + (unit_w > sec_w ? unit_w : sec_w) : 0;
    }
    static const struct {
        const gfx_font_t *const *fonts;
        int count;
    } k_fit[] = { [UI_SIZE_M] = { k_fit_m, 3 }, [UI_SIZE_L] = { k_fit_l, 3 }, [UI_SIZE_XL] = { k_fit_xl, 4 } };
    const gfx_font_t *vf =
        fit_number(f, k_fit[size].fonts, k_fit[size].count, &shown, &value, body.w - 6, extra_w, fit, sizeof(fit));
    int w = group_width(f, vf, &shown, value);
    int x = body.x + (body.w - w - extra_w) / 2;
    int extra_lines = v->kind == UI_FK_BATTERY ? f->unit->line_height : 0;
    int baseline = body.y + (body.h + digit_height(vf) - extra_lines) / 2;
    draw_group(fb, f, vf, &shown, value, x, baseline);
    if (v->kind == UI_FK_TIME) {
        int sx = x + w + 6;
        if (v->extra[0]) { /* seconds, level with the top of the digits */
            gfx_text(fb, side, sx, baseline - digit_height(vf) + side->ascent, v->extra, GFX_BLACK);
        }
        if (v->unit[0]) {
            gfx_text(fb, side, sx, baseline, v->unit, GFX_BLACK);
        }
    }
    if (v->kind == UI_FK_BATTERY) {
        int tw = gfx_text_width(f->unit, v->extra);
        gfx_text(fb, f->unit, body.x + (body.w - tw) / 2, baseline + f->unit->line_height, v->extra, GFX_BLACK);
    }
}

void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, ui_stale_policy_t policy,
                    const lang_t *lang)
{
    if (v->field == UI_FIELD_NONE) {
        return;
    }
    ui_value_t shown = *v;
    if (v->state == UI_VALUE_STALE && policy != UI_STALE_STALE) {
        shown.state = UI_VALUE_MISSING;
    }
    if (shown.state == UI_VALUE_MISSING && policy == UI_STALE_HIDE) {
        return;
    }
    if (shown.state == UI_VALUE_STALE) {
        shown.trend = 0; /* an old reading's trend says nothing about now */
    }
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, r));
    if (ui_forecast_draw(fb, r, size, &shown)) {
        /* the weather, air quality, pollen and sun widgets (ui_forecast.c) */
    } else if (ui_radar_widget(fb, r, size, &shown)) {
        /* rain.map (ui_radar.c) */
    } else if (size == UI_SIZE_S) {
        draw_small(fb, r, &shown);
    } else {
        draw_labelled(fb, r, size, &shown);
    }
    if (shown.state == UI_VALUE_STALE) {
        draw_age(fb, r, &shown, lang);
    }
    fb->clip = saved;
}

void ui_draw_battery(gfx_fb_t *fb, int x, int y, int w, int h, int pct)
{
    int nub = w / 10 < 2 ? 2 : w / 10;
    gfx_rect(fb, (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)(w - nub), (int16_t)h }, GFX_BLACK);
    gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x + w - nub), (int16_t)(y + h / 4), (int16_t)nub, (int16_t)(h - h / 2) },
                  GFX_BLACK);
    if (pct >= 0) {
        int inner = w - nub - 4;
        int fill = (pct > 100 ? 100 : pct) * inner / 100;
        gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x + 2), (int16_t)(y + 2), (int16_t)fill, (int16_t)(h - 4) },
                      GFX_BLACK);
    }
}

void ui_draw_moon(gfx_fb_t *fb, int cx, int cy, int r, double age)
{
    /* The shadow is inked, like the monochrome moon symbols: new is a black disc, full an empty
     * circle. Row by row, the terminator sits at half-width * cos(2 pi age); waxing, the lit part
     * is on the right (as seen from the northern hemisphere). */
    double c = cos(2.0 * PI * age);
    bool waxing = age < 0.5;
    for (int dy = -r; dy <= r; dy++) {
        double half = sqrt((double)(r * r - dy * dy));
        double t = half * c;
        int x0 = (int)lround(waxing ? cx - half : cx - t);
        int x1 = (int)lround(waxing ? cx + t : cx + half);
        if (x1 > x0) {
            gfx_hline(fb, x0, cy + dy, x1 - x0 + 1, GFX_BLACK);
        }
    }
    gfx_circle(fb, cx, cy, r, GFX_BLACK);
}
