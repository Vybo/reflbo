#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "gfx_icons.h"
#include "ui_internal.h"

#define PLACEHOLDER "\xE2\x80\x94" /* em dash */
#define ELLIPSIS "\xE2\x80\xA6"
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
    [UI_SIZE_XS] = { NULL, &gfx_font_sans_bold_16, &gfx_font_sans_bold_16, &gfx_font_sans_12, 16 },
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
    const gfx_bitmap_t *s16 = NULL, *s24 = NULL, *s48 = NULL;
    switch (field) {
    case UI_FIELD_ENV_TEMP:
    case UI_FIELD_ENV_TEMP_MIN:
    case UI_FIELD_ENV_TEMP_MAX:
        s16 = &gfx_icon_thermometer_16, s24 = &gfx_icon_thermometer_24, s48 = &gfx_icon_thermometer_48;
        break;
    case UI_FIELD_ENV_HUM:
        s16 = &gfx_icon_drop_16, s24 = &gfx_icon_drop_24, s48 = &gfx_icon_drop_48;
        break;
    case UI_FIELD_ENV_DEW:
        s16 = &gfx_icon_dew_16, s24 = &gfx_icon_dew_24, s48 = &gfx_icon_dew_48;
        break;
    case UI_FIELD_TIME_CLOCK:
        s16 = &gfx_icon_clock_16, s24 = &gfx_icon_clock_24, s48 = &gfx_icon_clock_48;
        break;
    case UI_FIELD_DATE_DAY:
    case UI_FIELD_DATE_WEEK:
        s16 = &gfx_icon_calendar_16, s24 = &gfx_icon_calendar_24, s48 = &gfx_icon_calendar_48;
        break;
    case UI_FIELD_DATE_NAMEDAY:
        s16 = &gfx_icon_person_16, s24 = &gfx_icon_person_24, s48 = &gfx_icon_person_48;
        break;
    case UI_FIELD_DATE_HOLIDAY:
        s16 = &gfx_icon_celebration_16, s24 = &gfx_icon_celebration_24, s48 = &gfx_icon_celebration_48;
        break;
    case UI_FIELD_WX_NOW:
    case UI_FIELD_WX_TODAY:
    case UI_FIELD_WX_HOURLY:
    case UI_FIELD_WX_DAILY: /* missing: drawn as the placeholder */
        s16 = &gfx_icon_cloud_16, s24 = &gfx_icon_cloud_24, s48 = &gfx_icon_cloud_48;
        break;
    case UI_FIELD_SUN_TIMES:
        s16 = &gfx_icon_sunrise_16, s24 = &gfx_icon_sunrise_24, s48 = &gfx_icon_sunrise_48;
        break;
    case UI_FIELD_AQ_INDEX:
        s16 = &gfx_icon_air_16, s24 = &gfx_icon_air_24, s48 = &gfx_icon_air_48;
        break;
    case UI_FIELD_AQ_PM25:
    case UI_FIELD_AQ_PM10:
        s16 = &gfx_icon_particles_16, s24 = &gfx_icon_particles_24, s48 = &gfx_icon_particles_48;
        break;
    case UI_FIELD_AQ_UV:
        s16 = &gfx_icon_uv_16, s24 = &gfx_icon_uv_24, s48 = &gfx_icon_uv_48;
        break;
    case UI_FIELD_POLLEN_TOP:
    case UI_FIELD_POLLEN_ALDER:
    case UI_FIELD_POLLEN_BIRCH:
    case UI_FIELD_POLLEN_GRASS:
    case UI_FIELD_POLLEN_MUGWORT:
    case UI_FIELD_POLLEN_OLIVE:
    case UI_FIELD_POLLEN_RAGWEED:
        s16 = &gfx_icon_pollen_16, s24 = &gfx_icon_pollen_24, s48 = &gfx_icon_pollen_48;
        break;
    case UI_FIELD_PV_NOW: /* the forecast: a sun; what the panels make: the panels (spec §5.1, D35) */
    case UI_FIELD_PV_TODAY:
    case UI_FIELD_PV_LEFT:
    case UI_FIELD_PV_TOMORROW:
    case UI_FIELD_PV_PEAK:
    case UI_FIELD_PV_CHART:
        s16 = &gfx_icon_forecast_16, s24 = &gfx_icon_forecast_24, s48 = &gfx_icon_forecast_48;
        break;
    case UI_FIELD_EN_PV:
    case UI_FIELD_EN_YIELD:
        s16 = &gfx_icon_solar_16, s24 = &gfx_icon_solar_24, s48 = &gfx_icon_solar_48;
        break;
    case UI_FIELD_EN_LOAD:
    case UI_FIELD_EN_FLOW:
        s16 = &gfx_icon_house_16, s24 = &gfx_icon_house_24, s48 = &gfx_icon_house_48;
        break;
    case UI_FIELD_EN_GRID:
    case UI_FIELD_EN_EXPORT:
    case UI_FIELD_EN_IMPORT:
        s16 = &gfx_icon_grid_16, s24 = &gfx_icon_grid_24, s48 = &gfx_icon_grid_48;
        break;
    case UI_FIELD_EN_SELF:
        s16 = &gfx_icon_self_use_16, s24 = &gfx_icon_self_use_24, s48 = &gfx_icon_self_use_48;
        break;
    default:
        break;
    }
    return size >= 48 ? s48 : size >= 24 ? s24 : s16;
}

/* How far `text`'s ink reaches below the baseline in `f`: a comma's tail, the "g" of "µg/m³". */
static int ink_below(const gfx_font_t *f, const char *text)
{
    int below = 0;
    for (const char *s = text; *s != '\0';) {
        const gfx_glyph_t *g = gfx_font_glyph(f, gfx_utf8_next(&s));
        if (g != NULL && g->y_offset + g->height > below) {
            below = g->y_offset + g->height;
        }
    }
    return below;
}

/* How far `text`'s ink reaches above the baseline in `f`: a capital's caron ("Čt") rises above the digits. */
static int ink_above(const gfx_font_t *f, const char *text)
{
    int above = 0;
    for (const char *s = text; *s != '\0';) {
        const gfx_glyph_t *g = gfx_font_glyph(f, gfx_utf8_next(&s));
        if (g != NULL && -g->y_offset > above) {
            above = -g->y_offset;
        }
    }
    return above;
}

int ui_ink_above(const gfx_font_t *f, const char *text)
{
    return ink_above(f, text);
}

int ui_ink_below(const gfx_font_t *f, const char *text)
{
    return ink_below(f, text);
}

/* The number's digits centred in max_h, with room below for its tail and its unit's, and 2 px to
 * each edge. Every digit counts as the deepest one ("5" dips 2 px in the 130 px face), so a number
 * keeps its size as its digits change. */
static bool fits_height(const ui_fonts_t *f, const gfx_font_t *vf, const ui_value_t *v, const char *value, int max_h)
{
    if (max_h <= 0) {
        return true;
    }
    int below = ink_below(vf, value);
    int digits = ink_below(vf, "0123456789");
    int unit_below = v->unit[0] ? ink_below(f->unit, v->unit) : 0;
    below = below > digits ? below : digits;
    below = below > unit_below ? below : unit_below;
    return digit_height(vf) + 2 * below + 4 <= max_h;
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

/* Picks the font and text for a number group so that it fits `max_w` (with `extra_w` beside it)
 * and `max_h` (0: any height): at each font, largest first, the value as it is, then without its
 * decimals ("101" for "100.8"; in Czech without the comma, whose tail reaches below the digits).
 * If even the smallest font is too wide, the value is cut with an ellipsis into `buf`. */
static const gfx_font_t *fit_number(const ui_fonts_t *f, const gfx_font_t *const *fonts, int count,
                                    const ui_value_t *v, const char **value, int max_w, int extra_w, int max_h,
                                    char *buf, size_t size)
{
    for (int i = 0; i < count; i++) {
        if (group_width(f, fonts[i], v, *value) + extra_w <= max_w && fits_height(f, fonts[i], v, *value, max_h)) {
            return fonts[i];
        }
        if (v->short_text[0] && group_width(f, fonts[i], v, v->short_text) + extra_w <= max_w &&
            fits_height(f, fonts[i], v, v->short_text, max_h)) {
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

/* The box of a bitmap's ink, offset from its top left. */
static void bitmap_ink_box(const gfx_bitmap_t *b, int *x0, int *y0, int *x1, int *y1)
{
    int rb = (b->width + 7) / 8;
    *x0 = b->width, *y0 = b->height, *x1 = -1, *y1 = -1;
    for (int y = 0; y < b->height; y++) {
        for (int x = 0; x < b->width; x++) {
            if ((b->bits[y * rb + x / 8] >> (7 - x % 8)) & 1) {
                *x0 = x < *x0 ? x : *x0, *x1 = x > *x1 ? x : *x1;
                *y0 = y < *y0 ? y : *y0, *y1 = y > *y1 ? y : *y1;
            }
        }
    }
}

/* Whether anything is drawn in x0..x1, y0..y1 or within 1 px of it. */
static bool ink_near(const gfx_fb_t *fb, int x0, int y0, int x1, int y1)
{
    for (int y = y0 - 1; y <= y1 + 1; y++) {
        for (int x = x0 - 1; x <= x1 + 1; x++) {
            if (gfx_get_pixel(fb, x, y)) {
                return true;
            }
        }
    }
    return false;
}

/* "⟲ 2 h" in the rect's bottom-right corner (spec §5.3), only where nothing is drawn within 1 px of its icon's ink or
 * its age's: a value that reaches that corner keeps it, and the status bar's stale warning stands for the mark
 * (M6c). */
static void draw_age(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v, const lang_t *lang)
{
    char age[16];
    ui_format_age(lang, v->age_s, age, sizeof(age));
    const gfx_font_t *f = &gfx_font_sans_12;
    int w = gfx_text_width(f, age);
    int x = r.x + r.w - 6 - w;
    int baseline = r.y + r.h - 6 - (f->line_height - f->ascent);
    int ix0, iy0, ix1, iy1;
    bitmap_ink_box(&gfx_icon_stale_16, &ix0, &iy0, &ix1, &iy1);
    if (ink_near(fb, x - 18 + ix0, baseline - 13 + iy0, x - 18 + ix1, baseline - 13 + iy1) ||
        ink_near(fb, x, baseline - ink_above(f, age), x + w - 1, baseline + ink_below(f, age))) {
        return;
    }
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

/* An MQTT field has no icon (spec §12.5): its label stands where the icon would, in the small face, cut to max_w;
 * none where not even its first character fits before the ellipsis. Its width, 0 for none. */
static int label_symbol(const ui_value_t *v, int max_w, char *out, size_t size)
{
    out[0] = '\0';
    if (!ui_field_is_mqtt(v->field) || v->label == NULL) {
        return 0;
    }
    int w = gfx_text_ellipsize(&gfx_font_sans_12, v->label, max_w, out, size);
    if (w > max_w || strcmp(out, ELLIPSIS) == 0) {
        out[0] = '\0';
        return 0;
    }
    return w;
}

/* The small visual that stands for the field: an icon, a battery (its bolt while it charges, with `bolt`), the
 * Moon, or an MQTT field's label, up to `max_w` wide. Returns its width. */
static int draw_symbol(gfx_fb_t *fb, const ui_value_t *v, int x, int y, int size, bool bolt, int max_w)
{
    if (ui_field_is_mqtt(v->field)) { /* its ink centred on the icon's box */
        char label[48];
        int w = label_symbol(v, max_w, label, sizeof(label));
        const gfx_font_t *f = &gfx_font_sans_12;
        gfx_text(fb, f, x, y + (size + ink_above(f, label) - ink_below(f, label)) / 2, label, GFX_BLACK);
        return w;
    }
    if (v->kind == UI_FK_BATTERY) {
        int w = size * 3 / 2, h = size * 3 / 4;
        ui_draw_battery(fb, x, y + (size - h) / 2, w, h, v->state == UI_VALUE_MISSING ? -1 : v->percent);
        if (bolt && v->battery == DS_BAT_CHARGING) {
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

/* The width draw_symbol() takes at `size` px. */
static int symbol_width(const ui_value_t *v, int size, bool bolt, int max_w)
{
    if (ui_field_is_mqtt(v->field)) {
        char label[48];
        return label_symbol(v, max_w, label, sizeof(label));
    }
    if (v->kind == UI_FK_BATTERY) {
        return size * 3 / 2 + (bolt && v->battery == DS_BAT_CHARGING ? 18 : 0);
    }
    if (v->kind == UI_FK_MOON) {
        return size;
    }
    const gfx_bitmap_t *icon = field_icon(v->field, size);
    return icon != NULL ? icon->width : 0;
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

/* S beside the value, from 150 px of width or under 80 px of height: the symbol, then the value. Nothing is cut
 * before the rest gives way: words take their shorter forms (the date's "Fri 25"; the Moon's short name, then its
 * illumination), then leave their symbol behind (the Moon keeps its disc); a number gives up the battery's bolt,
 * its trend arrow, its unit (a time its clock first, as AM/PM says more), then its symbol, and stands alone,
 * centred. */
static void draw_small_beside(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
{
    const ui_fonts_t *f = &k_fonts[UI_SIZE_S];
    int pad = r.w < 150 ? 6 : 14, gap = r.w < 150 ? 6 : 10;
    int sym_y = r.y + (r.h - f->icon) / 2;
    int label_w = r.w * 2 / 5; /* an MQTT field's label: up to two fifths of the cell */
    char fit[48];
    if (!numeric(v)) {
        const gfx_font_t *vf = v->state == UI_VALUE_MISSING ? f->value : f->text;
        const char *forms[3] = { display_text(v, UI_SIZE_S), "", "" };
        if (v->state != UI_VALUE_MISSING && v->kind == UI_FK_DATE) {
            forms[1] = v->short_text;
        } else if (v->state != UI_VALUE_MISSING && v->kind == UI_FK_MOON) {
            forms[1] = v->short_text, forms[2] = v->extra;
        }
        ui_value_t shown = *v;
        shown.unit[0] = '\0';
        shown.trend = 0;
        int baseline = r.y + (r.h + digit_height(vf)) / 2;
        for (int with = 1; with >= 0; with--) {
            int x = with ? r.x + pad + symbol_width(v, f->icon, true, label_w) + gap : r.x + 6;
            for (int k = 0; k < 3; k++) {
                int w = gfx_text_width(vf, forms[k]);
                if (forms[k][0] && w <= r.x + r.w - 6 - x) {
                    if (with) {
                        draw_symbol(fb, v, r.x + pad, sym_y, f->icon, true, label_w);
                    }
                    draw_group(fb, f, vf, &shown, forms[k], with ? x : r.x + (r.w - w) / 2, baseline);
                    return;
                }
            }
            if (v->kind == UI_FK_MOON && v->state != UI_VALUE_MISSING) {
                break; /* the disc is the phase */
            }
        }
        int x = r.x + pad + draw_symbol(fb, v, r.x + pad, sym_y, f->icon, true, label_w) + gap;
        gfx_text_ellipsize(vf, forms[0], r.x + r.w - 6 - x, fit, sizeof(fit));
        draw_group(fb, f, vf, &shown, fit, x, baseline);
        return;
    }
    static const struct {
        bool sym, bolt, trend, unit;
    } k_tries[] = {
        { true, true, true, true },    { true, false, true, true },   { true, false, false, true },
        { true, false, false, false }, { false, false, false, true }, { false, false, false, false },
    };
    size_t count = sizeof(k_tries) / sizeof(k_tries[0]);
    for (size_t i = 0; i < count; i++) {
        if (v->kind == UI_FK_TIME && k_tries[i].sym && !k_tries[i].unit) {
            continue;
        }
        ui_value_t shown = *v;
        shown.trend = k_tries[i].trend ? v->trend : 0;
        if (!k_tries[i].unit) {
            shown.unit[0] = '\0';
        }
        int x = r.x + pad + (k_tries[i].sym ? symbol_width(v, f->icon, k_tries[i].bolt, label_w) : 0) + gap;
        int max_w = k_tries[i].sym ? r.x + r.w - 6 - x : r.w - 12;
        const char *value = v->text;
        const gfx_font_t *vf = fit_number(f, k_fit_s, 3, &shown, &value, max_w, 0, 0, fit, sizeof(fit));
        if (value == fit && i + 1 < count) {
            continue; /* cut: give up something else first */
        }
        if (k_tries[i].sym) {
            draw_symbol(fb, v, r.x + pad, sym_y, f->icon, k_tries[i].bolt, label_w);
        } else {
            x = r.x + (r.w - group_width(f, vf, &shown, value)) / 2;
        }
        draw_group(fb, f, vf, &shown, value, x, r.y + (r.h + digit_height(vf)) / 2);
        return;
    }
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
    /* a name on two lines ends 84 px down: it stacks from 86 px, its tails 2 px clear */
    bool two_lines = !numeric(v) && v->kind != UI_FK_MOON && gfx_text_width(vf, value) > r.w - 8;
    if (r.w < 150 && r.h >= (two_lines ? 86 : 80)) { /* narrow and tall: symbol above, value below, the arrow beside */
        int sym_size = v->kind == UI_FK_MOON ? 28 : f->icon;
        int sym_w = v->kind == UI_FK_BATTERY        ? sym_size * 3 / 2
                    : ui_field_is_mqtt(v->field) ? symbol_width(v, sym_size, true, r.w - 8)
                                                 : sym_size;
        int sym_x = r.x + (r.w - sym_w) / 2;
        draw_symbol(fb, v, sym_x, r.y + 12, sym_size, true, r.w - 8);
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
        if (two_lines) { /* a name: two lines in the regular face */
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
            vf = fit_number(f, k_fit_s, 3, &shown, &value, max_w, 0, 0, fit, sizeof(fit));
        }
        int w = group_width(f, vf, &shown, value);
        int baseline = r.y + 12 + f->icon + 14 + digit_height(vf);
        draw_group(fb, f, vf, &shown, value, r.x + (r.w - w) / 2, baseline);
        return;
    }
    draw_small_beside(fb, r, v);
}

/* ---- XS (M6c, D34): the status bar's look ---- */

/* The faces an XS value tries, largest first; a number's unit goes smaller beside it. */
static const gfx_font_t *const k_fit_xs[] = { &gfx_font_sans_bold_28, &gfx_font_sans_bold_20, &gfx_font_sans_bold_16,
                                              &gfx_font_sans_12 };

static const gfx_font_t *xs_unit_font(const gfx_font_t *vf)
{
    return vf == &gfx_font_sans_bold_28 ? &gfx_font_sans_16 : &gfx_font_sans_12;
}

bool ui_tiny_stacked(gfx_rect_t r)
{
    return r.w < 120 && r.h >= 44;
}

/* A number's group in the largest of `count` faces from k_fit_xs that fits max_w and, its digits and tails,
 * max_h (fits_height()), with the unit and the trend arrow, then without the arrow, then (with `below`, where
 * `unit_h` more fits) with the unit under it, then without the unit; each with the value as it is or in its short
 * form. Else sans 12 alone, cut. It changes *shown's unit and trend in place, as the fit drew them; *below says
 * the unit goes under the value. */
static const gfx_font_t *fit_tiny(ui_value_t *shown, const char **value, int max_w, int max_h, size_t count,
                                  ui_fonts_t *uf, bool *below, int unit_h, char *buf, size_t size)
{
    char unit[sizeof(shown->unit)];
    memcpy(unit, shown->unit, sizeof(unit));
    int trend = shown->trend;
    const char *full = *value;
    if (below != NULL) {
        *below = false;
    }
    for (int pass = 0; pass < 4; pass++) {
        if (pass == 2 && (below == NULL || !unit[0] || gfx_text_width(&gfx_font_sans_12, unit) > max_w)) {
            continue;
        }
        shown->trend = pass >= 1 ? 0 : trend;
        memcpy(shown->unit, unit, sizeof(unit));
        if (pass >= 2) {
            shown->unit[0] = '\0';
        }
        for (size_t i = 0; i < count; i++) {
            const gfx_font_t *f = k_fit_xs[i];
            uf->unit = xs_unit_font(f);
            const char *forms[2] = { full, shown->short_text };
            for (int k = 0; k < 2; k++) {
                if (forms[k][0] && group_width(uf, f, shown, forms[k]) <= max_w &&
                    fits_height(uf, f, shown, forms[k], max_h - (pass == 2 ? unit_h : 0))) {
                    *value = forms[k];
                    if (below != NULL) {
                        *below = pass == 2;
                    }
                    return f;
                }
            }
        }
    }
    shown->trend = 0;
    shown->unit[0] = '\0';
    uf->unit = xs_unit_font(&gfx_font_sans_12);
    gfx_text_ellipsize(&gfx_font_sans_12, shown->short_text[0] ? shown->short_text : full, max_w, buf, size);
    *value = buf;
    return &gfx_font_sans_12;
}

/* Words: the largest of bold 20, bold 16 and sans 12 that fits max_w, its ink 2 px clear of either edge of max_h;
 * NULL when none does. */
static const gfx_font_t *tiny_text_face(const char *text, int max_w, int max_h)
{
    static const gfx_font_t *const k_faces[] = { &gfx_font_sans_bold_20, &gfx_font_sans_bold_16, &gfx_font_sans_12 };
    for (size_t i = 0; i < sizeof(k_faces) / sizeof(k_faces[0]); i++) {
        const gfx_font_t *f = k_faces[i];
        if (gfx_text_width(f, text) <= max_w && ink_above(f, text) + ink_below(f, text) + 4 <= max_h) {
            return f;
        }
    }
    return NULL;
}

/* Words as tiny_text_face() fits them, or sans 12 cut to max_w. */
static const gfx_font_t *fit_tiny_text(const char *text, int max_w, int max_h, char *buf, size_t size)
{
    const gfx_font_t *f = tiny_text_face(text, max_w, max_h);
    if (f != NULL) {
        snprintf(buf, size, "%s", text);
        return f;
    }
    gfx_text_ellipsize(&gfx_font_sans_12, text, max_w, buf, size);
    return &gfx_font_sans_12;
}

/* What S marks beside a symbol, XS too: today's low (↓) or high (↑); a charging battery's bolt. */
static const char *tiny_mark(const ui_value_t *v)
{
    return v->field == UI_FIELD_ENV_TEMP_MIN ? ARROW_DOWN : v->field == UI_FIELD_ENV_TEMP_MAX ? ARROW_UP : NULL;
}

static bool tiny_bolt(const ui_value_t *v)
{
    return v->kind == UI_FK_BATTERY && v->state != UI_VALUE_MISSING && v->battery == DS_BAT_CHARGING;
}

/* The bolt's ink: the first of its 16 px icon's columns that hold any, and how many, so it sits 2 px from the
 * battery, not its icon's box. */
static void bolt_ink(int *x0, int *w)
{
    const gfx_bitmap_t *b = &gfx_icon_bolt_16;
    int rb = (b->width + 7) / 8, lo = b->width, hi = -1;
    for (int y = 0; y < b->height; y++) {
        for (int x = 0; x < b->width; x++) {
            if ((b->bits[y * rb + x / 8] >> (7 - x % 8)) & 1) {
                lo = x < lo ? x : lo;
                hi = x > hi ? x : hi;
            }
        }
    }
    *x0 = lo, *w = hi - lo + 1;
}

/* The symbol's size at `sym` px, its marks included: the battery's outline and bolt, the Moon, the field's icon and
 * its arrow, or an MQTT field's label up to `max_w` wide (its ink's height); 0 × 0 for a time and for a field
 * without one. */
static void tiny_symbol_size(const ui_value_t *v, int sym, int max_w, int *w, int *h)
{
    *w = *h = 0;
    if (v->kind == UI_FK_BATTERY) {
        int bolt_x, bolt_w;
        bolt_ink(&bolt_x, &bolt_w);
        *w = sym + 6 + (tiny_bolt(v) ? 2 + bolt_w : 0), *h = sym / 2 + 2;
        *h = tiny_bolt(v) && *h < 16 ? 16 : *h;
    } else if (v->kind == UI_FK_MOON) {
        *w = *h = sym;
    } else if (ui_field_is_mqtt(v->field)) {
        char label[48];
        *w = label_symbol(v, max_w, label, sizeof(label));
        *h = *w > 0 ? ink_above(&gfx_font_sans_12, label) + ink_below(&gfx_font_sans_12, label) : 0;
    } else if (v->kind != UI_FK_TIME) {
        const gfx_bitmap_t *icon = field_icon(v->field, sym);
        if (icon != NULL) {
            const char *mark = tiny_mark(v);
            *w = icon->width + (mark != NULL ? 1 + gfx_text_width(&gfx_font_sans_12, mark) : 0), *h = icon->height;
        }
    }
}

/* The symbol with its top left at (x, y), `h` px tall as tiny_symbol_size() gave it. */
static void draw_tiny_symbol(gfx_fb_t *fb, const ui_value_t *v, int x, int y, int sym, int h, int max_w)
{
    int cy = y + h / 2;
    if (v->kind == UI_FK_BATTERY) {
        int bh = sym / 2 + 2;
        ui_draw_battery(fb, x, cy - bh / 2, sym + 6, bh, v->state == UI_VALUE_MISSING ? -1 : v->percent);
        if (tiny_bolt(v)) {
            int bolt_x, bolt_w;
            bolt_ink(&bolt_x, &bolt_w);
            gfx_bitmap(fb, x + sym + 6 + 2 - bolt_x, cy - 8, &gfx_icon_bolt_16, GFX_BLACK);
        }
    } else if (v->kind == UI_FK_MOON) {
        if (v->state == UI_VALUE_MISSING) {
            gfx_circle(fb, x + sym / 2, cy, sym / 2 - 1, GFX_BLACK);
        } else {
            ui_draw_moon(fb, x + sym / 2, cy, sym / 2 - 1, v->moon.age);
        }
    } else if (ui_field_is_mqtt(v->field)) {
        char label[48];
        label_symbol(v, max_w, label, sizeof(label));
        gfx_text(fb, &gfx_font_sans_12, x, y + ink_above(&gfx_font_sans_12, label), label, GFX_BLACK);
    } else if (v->kind != UI_FK_TIME) {
        const gfx_bitmap_t *icon = field_icon(v->field, sym);
        if (icon != NULL) {
            gfx_bitmap(fb, x, cy - icon->height / 2, icon, GFX_BLACK);
            const char *mark = tiny_mark(v);
            if (mark != NULL) {
                gfx_text(fb, &gfx_font_sans_12, x + icon->width + 1, cy + icon->height / 2, mark, GFX_BLACK);
            }
        }
    }
}

/* One line, like the status bar: the 16 px symbol (24 px from 34 px of height), then the value. A number that would
 * only fit cut beside its symbol stands alone, centred, as a time does; words keep 4 px at the right for an
 * accent's overhang. The date is "Fri 25 Sep", or "Fri 25" where that doesn't fit; the Moon keeps its disc, with
 * its name, its short name or its illumination, whichever fits. */
static void draw_tiny_line(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
{
    int sym = r.h >= 34 ? 24 : 16;
    int label_w = r.w * 2 / 5; /* an MQTT field's label: up to two fifths of the cell */
    int sym_w, sym_h;
    tiny_symbol_size(v, sym, label_w, &sym_w, &sym_h);
    int cy = r.y + r.h / 2, top = cy - sym_h / 2;
    char fit[48];
    if (numeric(v)) {
        for (int with = sym_w > 0; with >= 0; with--) {
            bool centre = v->kind == UI_FK_TIME || (sym_w > 0 && !with); /* alone, or its symbol given up */
            int x = r.x + 3 + (with ? sym_w + 3 : 0), max_w = centre ? r.w - 4 : r.x + r.w - 2 - x;
            ui_value_t shown = *v;
            ui_fonts_t uf = k_fonts[UI_SIZE_XS];
            const char *value = v->text;
            const gfx_font_t *vf = fit_tiny(&shown, &value, max_w, r.h, 4, &uf, NULL, 0, fit, sizeof(fit));
            if (value == fit && with) {
                continue; /* cut beside the symbol: the value alone */
            }
            if (with) {
                draw_tiny_symbol(fb, v, r.x + 3, top, sym, sym_h, label_w);
            }
            int w = group_width(&uf, vf, &shown, value);
            draw_group(fb, &uf, vf, &shown, value, centre ? r.x + (r.w - w) / 2 : x, cy + digit_height(vf) / 2);
            return;
        }
    }
    if (v->kind == UI_FK_MOON && v->state != UI_VALUE_MISSING) {
        draw_tiny_symbol(fb, v, r.x + 3, top, sym, sym_h, label_w);
        int x = r.x + 3 + sym_w + 4, max_w = r.x + r.w - 4 - x;
        const char *forms[3] = { v->text, v->short_text, v->extra };
        for (int k = r.w >= 120 ? 0 : 1; k < 3; k++) {
            const gfx_font_t *tf = tiny_text_face(forms[k], max_w, r.h);
            if (tf != NULL) {
                gfx_text(fb, tf, x, r.y + (r.h + ink_above(tf, forms[k]) - ink_below(tf, forms[k])) / 2, forms[k],
                         GFX_BLACK);
                return;
            }
        }
        return; /* the disc alone */
    }
    const char *text = v->state == UI_VALUE_MISSING ? PLACEHOLDER : v->kind == UI_FK_DATE ? v->extra : v->text;
    for (int with = sym_w > 0; with >= 0; with--) {
        int x = r.x + 3 + (with ? sym_w + 3 : 0), max_w = r.x + r.w - 4 - x;
        const char *t = text;
        if (v->kind == UI_FK_DATE && v->state != UI_VALUE_MISSING && gfx_text_width(&gfx_font_sans_12, t) > max_w) {
            t = v->short_text;
        }
        if (with && gfx_text_width(&gfx_font_sans_12, t) > max_w) {
            continue;
        }
        const gfx_font_t *tf = fit_tiny_text(t, max_w, r.h, fit, sizeof(fit));
        if (with) {
            draw_tiny_symbol(fb, v, r.x + 3, top, sym, sym_h, label_w);
        }
        gfx_text(fb, tf, x, r.y + (r.h + ink_above(tf, fit) - ink_below(tf, fit)) / 2, fit, GFX_BLACK);
        return;
    }
}

/* The symbol over the value, both centred: a 16 px symbol (24 px from 60 px of height) with its marks; the date
 * as its weekday over its day; a time alone. A number's group fits 2 px inside either edge, words 4 px. */
static void draw_tiny_stacked(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
{
    int sym = r.h >= 60 ? 24 : 16;
    int cx = r.x + r.w / 2;
    char fit[48];
    if (v->kind == UI_FK_DATE && v->state != UI_VALUE_MISSING) { /* "Fri" over "25", "Pá" over "25." */
        char wd[16];
        snprintf(wd, sizeof(wd), "%s", v->short_text);
        char *space = strrchr(wd, ' ');
        const char *day = space != NULL ? space + 1 : wd;
        if (space != NULL) {
            *space = '\0';
        }
        const char *weekday = space != NULL ? wd : "";
        const gfx_font_t *wf = &gfx_font_sans_12;
        int above = ink_above(wf, weekday);
        const gfx_font_t *df = &gfx_font_sans_12;
        for (size_t i = 0; i < sizeof(k_fit_xs) / sizeof(k_fit_xs[0]); i++) {
            if (above + 4 + digit_height(k_fit_xs[i]) + 4 <= r.h && gfx_text_width(k_fit_xs[i], day) <= r.w - 4) {
                df = k_fit_xs[i];
                break;
            }
        }
        int block = above + 4 + digit_height(df);
        int top = r.y + (r.h - block) / 2;
        gfx_text(fb, wf, cx - gfx_text_width(wf, weekday) / 2, top + above, weekday, GFX_BLACK);
        gfx_text(fb, df, cx - gfx_text_width(df, day) / 2, top + block, day, GFX_BLACK);
        return;
    }
    int sym_w, sym_h;
    tiny_symbol_size(v, sym, r.w - 4, &sym_w, &sym_h);
    if (sym == 24 && sym_w > r.w - 4) { /* a charging battery's bolt beside the 24 px outline: the 16 px one */
        sym = 16;
        tiny_symbol_size(v, sym, r.w - 4, &sym_w, &sym_h);
    }
    int gap = sym_h ? 4 : 0;
    int room = r.h - 4 - sym_h - gap;
    const gfx_font_t *vf;
    const char *value;
    ui_value_t shown = *v;
    ui_fonts_t uf = k_fonts[UI_SIZE_XS];
    bool below = false;
    int unit_h = 4 + ink_above(&gfx_font_sans_12, v->unit) + ink_below(&gfx_font_sans_12, v->unit);
    int value_h; /* the value's ink: from its top to its lowest tail */
    if (numeric(v)) {
        value = v->text;
        vf = fit_tiny(&shown, &value, r.w - 4, room + 4, 3, &uf, &below, unit_h, fit, sizeof(fit));
        int tail = ink_below(vf, value);
        int unit_tail = shown.unit[0] ? ink_below(uf.unit, shown.unit) : 0;
        value_h = digit_height(vf) + (tail > unit_tail ? tail : unit_tail);
    } else {
        const char *text = v->state == UI_VALUE_MISSING ? PLACEHOLDER : v->kind == UI_FK_MOON ? v->short_text : v->text;
        if (v->kind == UI_FK_MOON && v->state != UI_VALUE_MISSING && tiny_text_face(text, r.w - 8, room + 4) == NULL) {
            text = v->extra; /* its illumination, where its short name would be cut (M6c review) */
        }
        vf = fit_tiny_text(text, r.w - 8, room + 4, fit, sizeof(fit));
        value = fit;
        value_h = ink_above(vf, value) + ink_below(vf, value);
    }
    int block = sym_h + gap + value_h + (below ? unit_h : 0);
    int top = r.y + (r.h - block) / 2;
    draw_tiny_symbol(fb, v, cx - sym_w / 2, top, sym, sym_h, r.w - 4);
    int y = top + sym_h + gap;
    if (numeric(v)) {
        int baseline = y + digit_height(vf);
        draw_group(fb, &uf, vf, &shown, value, cx - group_width(&uf, vf, &shown, value) / 2, baseline);
        if (below) {
            int base = top + block - ink_below(&gfx_font_sans_12, v->unit);
            gfx_text(fb, &gfx_font_sans_12, cx - gfx_text_width(&gfx_font_sans_12, v->unit) / 2, base, v->unit,
                     GFX_BLACK);
        }
        return;
    }
    gfx_text(fb, vf, cx - gfx_text_width(vf, value) / 2, y + ink_above(vf, value), value, GFX_BLACK);
}

static void draw_tiny(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
{
    if (ui_tiny_stacked(r)) {
        draw_tiny_stacked(fb, r, v);
    } else {
        draw_tiny_line(fb, r, v);
    }
}

static void draw_labelled(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    const ui_fonts_t *f = &k_fonts[size];
    int top = r.y + 6;
    if (f->label != NULL && v->kind != UI_FK_TIME && !(size == UI_SIZE_M && v->kind == UI_FK_DATE)) {
        char label[40];
        bool says = v->field == UI_FIELD_EN_GRID || v->field == UI_FIELD_EN_EXPORT || v->field == UI_FIELD_EN_IMPORT;
        int trend = says ? 0 : v->trend; /* "Export" or "To grid today" says which way */
        int arrow_w = trend ? gfx_text_width(f->label, ARROW_UP) + 4 : 0;
        gfx_text_ellipsize(f->label, v->label, r.w - 12 - arrow_w, label, sizeof(label));
        int pen = gfx_text(fb, f->label, r.x + 6, top + f->label->ascent, label, GFX_BLACK);
        if (trend) { /* beside the label, where it doesn't widen the value */
            gfx_text(fb, f->label, pen + 4, top + f->label->ascent, trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
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
    int extra_lines = v->kind == UI_FK_BATTERY ? f->unit->line_height : 0;
    const gfx_font_t *vf = fit_number(f, k_fit[size].fonts, k_fit[size].count, &shown, &value, body.w - 6, extra_w,
                                      body.h - extra_lines, fit, sizeof(fit));
    int w = group_width(f, vf, &shown, value);
    int x = body.x + (body.w - w - extra_w) / 2;
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
    } else if (ui_solar_widget(fb, r, size, &shown, lang)) {
        /* pv.chart and energy.flow (ui_solar.c) */
    } else if (size == UI_SIZE_XS) {
        draw_tiny(fb, r, &shown);
    } else if (size == UI_SIZE_S) {
        draw_small(fb, r, &shown);
    } else {
        draw_labelled(fb, r, size, &shown);
    }
    /* XS and a short S cell have no room beside the value; the status bar's stale warning covers them */
    if (shown.state == UI_VALUE_STALE && size != UI_SIZE_XS && !(size == UI_SIZE_S && r.h < 80)) {
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
