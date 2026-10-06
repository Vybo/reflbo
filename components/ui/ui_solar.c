#include "ui_solar.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "gfx_icons.h"
#include "ui_internal.h"
#include "weather.h"

/* The pv.* and energy.* fields (spec §5.1, §11.5, §11.6, M6d), the chart and the flow (spec §5.3), and the
 * sample day. */

/* "2.31" kW from W, "12.3" from 10 kW up. */
static void kw_text(const lang_t *lang, int32_t w, char *out, size_t size)
{
    long a = w < 0 ? -(long)w : w;
    if (a >= 10000) {
        lang_format_decimal(lang, (a + 50) / 100, 1, out, size);
    } else {
        lang_format_decimal(lang, (a + 5) / 10, 2, out, size);
    }
}

static long magnitude(int32_t w)
{
    return w < 0 ? -(long)w : w;
}

/* A power's number, and its unit as the return: whole W under 1 kW, as the SolaX app shows it (owner, 2026-10-06),
 * then kW: "2.31", "12.3" from 10 kW. */
static const char *power_text(const lang_t *lang, int32_t w, char *out, size_t size)
{
    if (magnitude(w) < 1000) {
        snprintf(out, size, "%ld", magnitude(w));
        return "W";
    }
    kw_text(lang, w, out, size);
    return "kW";
}

/* "18.4" kWh from Wh. */
static void kwh_text(const lang_t *lang, uint32_t wh, char *out, size_t size)
{
    lang_format_decimal(lang, (long)((wh + 50) / 100), 1, out, size);
}

/* A power, in W under 1 kW, and its shorter form where a number doesn't fit (spec §5.1): from 1 kW, one decimal,
 * from 10 kW whole; whole watts have none. */
static void set_kw(const lang_t *lang, int32_t w, ui_value_t *out)
{
    snprintf(out->unit, sizeof(out->unit), "%s", power_text(lang, w, out->text, sizeof(out->text)));
    long a = magnitude(w);
    if (a >= 10000) {
        snprintf(out->short_text, sizeof(out->short_text), "%ld", (a + 500) / 1000);
    } else if (a >= 1000) {
        lang_format_decimal(lang, (a + 50) / 100, 1, out->short_text, sizeof(out->short_text));
    }
}

/* An energy and its shorter form: whole kWh from 10 kWh. */
static void set_kwh(const lang_t *lang, uint32_t wh, ui_value_t *out)
{
    kwh_text(lang, wh, out->text, sizeof(out->text));
    if (wh >= 9950) {
        snprintf(out->short_text, sizeof(out->short_text), "%lu", (unsigned long)((wh + 500) / 1000));
    }
    snprintf(out->unit, sizeof(out->unit), "kWh");
}

/* A quarter hour's start as the clock setting shows it: "12:45", "12:45 PM". */
static void quarter_time(const ui_context_t *ctx, int quarter, char *out, size_t size)
{
    const char *suffix;
    char hm[12];
    lang_format_time(quarter / 4, quarter % 4 * 15, 0, ctx->clock_24h, false, hm, sizeof(hm), &suffix);
    snprintf(out, size, "%s%s%s", hm, suffix[0] ? " " : "", suffix);
}

static int flow_dir(int32_t w)
{
    return w >= UI_SOLAR_IDLE_W ? 1 : w <= -UI_SOLAR_IDLE_W ? -1 : 0;
}

/* pv.*: the forecast's day today, fresh until its wait is over (spec §5.1). */
static void resolve_forecast(const ui_context_t *ctx, const ui_solar_t *s, ui_field_id_t field, ui_value_t *out)
{
    const solar_forecast_t *f = s->forecast;
    if (f == NULL || f->day == 0) {
        return;
    }
    const lang_t *lang = ctx->lang;
    int32_t today = ctx->local_day;
    int quarter = ctx->local.tm_hour * 4 + ctx->local.tm_min / 15;
    int seconds = ctx->local.tm_min % 15 * 60 + ctx->local.tm_sec;
    const uint16_t *q = solar_day(f, today);
    uint32_t wh = SOLAR_WH_NONE;
    switch (field) {
    case UI_FIELD_PV_NOW:
        if (q != NULL) {
            set_kw(lang, q[quarter] * SOLAR_UNIT_W, out);
            out->state = UI_VALUE_FRESH;
        }
        break;
    case UI_FIELD_PV_TODAY:
    case UI_FIELD_PV_TOMORROW:
        wh = solar_day_wh(f, field == UI_FIELD_PV_TODAY ? today : today + 1);
        break;
    case UI_FIELD_PV_CHART: /* the day's bars under its total */
        if (q != NULL) {
            wh = solar_day_wh(f, today);
            out->quarter = quarter;
            out->minute = ctx->local.tm_min;
            out->chart_q = q;
            out->chart_read = s->reading != NULL && s->reading->at != 0 ? energy_day_q(s->day, today) : NULL;
        }
        break;
    case UI_FIELD_PV_LEFT:
        wh = solar_left_wh(f, today, quarter, seconds);
        break;
    case UI_FIELD_PV_PEAK: {
        uint32_t w = 0;
        int at = 0;
        if (q != NULL) {
            if (solar_peak(f, today, &w, &at)) {
                quarter_time(ctx, at, out->extra, sizeof(out->extra));
            }
            set_kw(lang, (int32_t)w, out);
            out->state = UI_VALUE_FRESH;
        }
        break;
    }
    default:
        break;
    }
    if (wh != SOLAR_WH_NONE) {
        set_kwh(lang, wh, out);
        out->state = UI_VALUE_FRESH;
    }
    if (out->state == UI_VALUE_FRESH) {
        out->age_s = (uint32_t)ctx->now > f->fetched ? (uint32_t)ctx->now - f->fetched : 0;
        out->state = s->forecast_ttl_s == 0 || out->age_s <= s->forecast_ttl_s ? UI_VALUE_FRESH : UI_VALUE_STALE;
    }
}

/* energy.*: the last reading, fresh for 15 min; today's totals from a reading of today. */
static void resolve_house(const ui_context_t *ctx, const ui_solar_t *s, ui_field_id_t field, ui_value_t *out)
{
    const energy_reading_t *r = s->reading;
    if (r == NULL || r->at == 0) {
        return;
    }
    const lang_t *lang = ctx->lang;
    int32_t today = ctx->local_day;
    bool of_today = energy_reading_day(r) == today;
    uint32_t wh = ENERGY_WH_NONE;
    out->state = UI_VALUE_FRESH;
    switch (field) {
    case UI_FIELD_EN_PV:
        set_kw(lang, r->pv_w, out);
        break;
    case UI_FIELD_EN_LOAD:
        set_kw(lang, r->load_w, out);
        break;
    case UI_FIELD_EN_GRID: /* the way it goes: the label in M and up, an arrow in S and XS (up: to the grid) */
        set_kw(lang, r->grid_w, out);
        if (flow_dir(r->grid_w) != 0) {
            out->label = lang_str(lang, r->grid_w < 0 ? LS_EN_EXPORTING : LS_EN_IMPORTING);
            out->trend = r->grid_w < 0 ? 1 : -1;
        }
        break;
    case UI_FIELD_EN_BATTERY: /* like the device's: its charge, a bolt while charging, an arrow down while not */
        if (!s->battery || r->soc < 0) {
            out->state = UI_VALUE_MISSING;
            break;
        }
        out->percent = r->soc;
        out->battery = flow_dir(r->bat_w) > 0 ? DS_BAT_CHARGING : DS_BAT_DISCHARGING;
        out->trend = flow_dir(r->bat_w) < 0 ? -1 : 0;
        snprintf(out->text, sizeof(out->text), "%d", r->soc);
        snprintf(out->unit, sizeof(out->unit), "%%");
        if (flow_dir(r->bat_w) != 0) {
            char t[16];
            const char *unit = power_text(lang, r->bat_w, t, sizeof(t));
            snprintf(out->extra, sizeof(out->extra), "%s %s", t, unit);
        }
        break;
    case UI_FIELD_EN_YIELD:
        wh = of_today ? r->yield_wh : ENERGY_WH_NONE;
        break;
    case UI_FIELD_EN_EXPORT:
        wh = energy_to_grid_wh(s->day, r, today);
        out->trend = 1;
        break;
    case UI_FIELD_EN_IMPORT:
        wh = energy_from_grid_wh(s->day, r, today);
        out->trend = -1;
        break;
    case UI_FIELD_EN_FLOW:
        break;
    case UI_FIELD_EN_SELF: {
        int pct = energy_self_pct(s->day, r, today);
        if (pct < 0) {
            out->state = UI_VALUE_MISSING;
        } else {
            snprintf(out->text, sizeof(out->text), "%d", pct);
            snprintf(out->unit, sizeof(out->unit), "%%");
        }
        break;
    }
    default:
        break;
    }
    if (field == UI_FIELD_EN_YIELD || field == UI_FIELD_EN_EXPORT || field == UI_FIELD_EN_IMPORT) {
        if (wh == ENERGY_WH_NONE) {
            out->state = UI_VALUE_MISSING;
            out->trend = 0;
        } else {
            set_kwh(lang, wh, out);
        }
    }
    if (out->state == UI_VALUE_FRESH) {
        out->age_s = (uint32_t)ctx->now > r->at ? (uint32_t)ctx->now - r->at : 0;
        out->state = energy_fresh(r, (uint32_t)ctx->now) ? UI_VALUE_FRESH : UI_VALUE_STALE;
    }
}

bool ui_resolve_solar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    if (field < UI_FIELD_PV_NOW || field > UI_FIELD_EN_FLOW) {
        return false;
    }
    const ui_solar_t *s = ctx->solar;
    out->solar = s;
    if (s == NULL) {
        return true;
    }
    if (field <= UI_FIELD_PV_PEAK || field == UI_FIELD_PV_CHART) {
        resolve_forecast(ctx, s, field, out);
    } else {
        resolve_house(ctx, s, field, out);
    }
    return true;
}

/* ---- the chart ---- */

static void dotted_hline(gfx_fb_t *fb, int x, int y, int w)
{
    for (int i = 0; i < w; i += 3) {
        gfx_pixel(fb, x + i, y, GFX_BLACK);
    }
}

static void dashed_vline(gfx_fb_t *fb, int x, int y, int h)
{
    for (int i = 0; i < h; i += 4) {
        gfx_vline(fb, x, y + i, h - i < 2 ? h - i : 2, GFX_BLACK);
    }
}

/* A gridline every 0.5, 1, 2, 10 or 20 kW, as many as the chart's highest bar needs. */
static uint32_t grid_step(uint32_t top_w)
{
    return top_w > 60000 ? 20000 : top_w > 25000 ? 10000 : top_w > 6000 ? 2000 : top_w > 2500 ? 1000 : 500;
}

/* A gridline's label: "4", "0.5", "180". */
static void grid_label(const lang_t *lang, uint32_t g, char *out, size_t size)
{
    if (g % 1000 == 0) {
        lang_format_decimal(lang, (long)(g / 1000), 0, out, size);
    } else {
        lang_format_decimal(lang, (long)(g / 100), 1, out, size);
    }
}

/* The left edge of the plot with labels: room for the widest label the highest quarter hour could need. */
static int labels_left(gfx_rect_t r, const uint16_t *fq, const uint16_t *aq, const lang_t *lang)
{
    uint32_t top_w = 0;
    for (int i = 0; i < SOLAR_STEPS; i++) {
        uint32_t f = fq[i] * (uint32_t)SOLAR_UNIT_W, a = aq != NULL && aq[i] != ENERGY_NONE ? aq[i] * 10u : 0;
        top_w = f > top_w ? f : top_w;
        top_w = a > top_w ? a : top_w;
    }
    uint32_t step = grid_step(top_w), scale = (top_w / step + 1) * step;
    int widest = 0;
    for (uint32_t g = step; g < scale; g += step) {
        char t[8];
        grid_label(lang, g, t, sizeof(t));
        int w = gfx_text_width(&gfx_font_sans_12, t);
        widest = w > widest ? w : widest;
    }
    return widest + 8 > 24 ? r.x + widest + 8 : r.x + 24;
}

/* The day's forecast as bars from the first hour with any to the last: a bar a quarter hour, or an hour where a
 * quarter's would be under 3 px; the past solid, the rest outlined. With readings, the past's bars are what was
 * produced, a quarter hour without a reading what was forecast (spec §11.6), and a line over them is what was
 * forecast. With `labels`, kW on the left and every third hour below. */
static void draw_chart(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v, const lang_t *lang, bool labels)
{
    const uint16_t *fq = v->chart_q, *aq = v->chart_read;
    int step = v->quarter;
    bool actual = aq != NULL;
    int first = SOLAR_STEPS, last = -1;
    for (int i = 0; i < SOLAR_STEPS; i++) {
        bool read = actual && aq[i] != ENERGY_NONE && aq[i] > 0;
        if (fq[i] > 0 || read) {
            first = i < first ? i : first;
            last = i;
        }
    }
    if (last < 0) {
        first = 24, last = 71; /* a day without sun: 6 to 18 */
    }
    first = first / 4 * 4;
    last = last / 4 * 4 + 3;
    int left = labels ? labels_left(r, fq, aq, lang) : r.x + 2;
    int bottom = labels ? r.y + r.h - 15 : r.y + r.h - 2, top = r.y + 6;
    int plot_w = r.x + r.w - 2 - left, plot_h = bottom - top;
    int per = plot_w / (last - first + 1) >= 3 ? 1 : 4; /* quarter hours a bar */
    int n = (last - first + 1) / per;
    uint32_t fw[SOLAR_STEPS], aw[SOLAR_STEPS], top_w = 0;
    for (int i = 0; i < n; i++) {
        uint32_t f = 0, a = 0;
        for (int k = 0; k < per; k++) {
            int st = first + i * per + k;
            uint32_t forecast = fq[st] * (uint32_t)SOLAR_UNIT_W;
            f += forecast;
            a += actual && aq[st] != ENERGY_NONE ? aq[st] * (uint32_t)ENERGY_UNIT_W : forecast;
        }
        fw[i] = f / (uint32_t)per, aw[i] = a / (uint32_t)per;
        top_w = fw[i] > top_w ? fw[i] : top_w;
        top_w = aw[i] > top_w ? aw[i] : top_w;
    }
    uint32_t grid_w = grid_step(top_w);
    uint32_t scale = (top_w / grid_w + 1) * grid_w;
    int pitch = plot_w / n > 1 ? plot_w / n : 1;
    int bar = pitch >= 4 ? pitch - 1 - (per == 4 && pitch >= 8) : pitch;
    int x0 = left + (plot_w - pitch * n) / 2;
    for (uint32_t g = grid_w; g < scale; g += grid_w) { /* a gridline each kW or two, labelled */
        int y = bottom - (int)((uint64_t)g * (uint32_t)plot_h / scale);
        dotted_hline(fb, x0, y, pitch * n);
        if (labels) {
            char t[8];
            grid_label(lang, g, t, sizeof(t));
            gfx_text(fb, &gfx_font_sans_12, left - 4 - gfx_text_width(&gfx_font_sans_12, t), y + 4, t, GFX_BLACK);
        }
    }
    if (labels) {
        gfx_text(fb, &gfx_font_sans_12, r.x + 2, top + 3, "kW", GFX_BLACK);
    }
    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < n; i++) {
        int start = first + i * per;
        int x = x0 + i * pitch;
        int fh = (int)((uint64_t)fw[i] * (uint32_t)plot_h / scale);
        bool past = start + per <= step;
        if (actual && past) {
            int ah = (int)((uint64_t)aw[i] * (uint32_t)plot_h / scale);
            if (ah > 0) {
                gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)x, (int16_t)(bottom - ah), (int16_t)bar, (int16_t)ah },
                              GFX_BLACK);
            }
        } else if (fh > 0) {
            gfx_rect_t b = { (int16_t)x, (int16_t)(bottom - fh), (int16_t)bar, (int16_t)fh };
            if (past) {
                gfx_fill_rect(fb, b, GFX_BLACK);
            } else {
                gfx_rect(fb, b, GFX_BLACK);
            }
        }
        if (actual && start < step) { /* the forecast's line over what came */
            int cx = x + bar / 2, cy = bottom - fh;
            if (prev_x >= 0) {
                gfx_line(fb, prev_x, prev_y, cx, cy, GFX_BLACK);
                gfx_line(fb, prev_x, prev_y - 1, cx, cy - 1, GFX_BLACK);
            }
            prev_x = cx, prev_y = cy;
        }
    }
    gfx_hline(fb, x0, bottom, pitch * n, GFX_BLACK);
    if (step >= first && step <= last) { /* now: a dashed line and a mark above it, the mark inside the plot */
        int x = x0 + ((step - first) * 15 + v->minute % 15) * pitch / (per * 15);
        int mark = x < left + 4 ? left + 4 : x > left + plot_w - 4 ? left + plot_w - 4 : x;
        dashed_vline(fb, x, top, plot_h);
        gfx_fill_triangle(fb, mark - 4, top - 5, mark + 4, top - 5, mark, top + 1, GFX_BLACK);
    }
    if (labels) {
        for (int h = first / 4; h * 4 <= last + 1; h++) {
            if (h % 3 != 0) {
                continue;
            }
            int x = x0 + (h * 4 - first) * pitch / per;
            gfx_vline(fb, x, bottom, 3, GFX_BLACK);
            char t[12];
            snprintf(t, sizeof(t), "%d", h);
            gfx_text(fb, &gfx_font_sans_12, x - gfx_text_width(&gfx_font_sans_12, t) / 2, bottom + 13, t, GFX_BLACK);
        }
    }
}

/* pv.chart in an M or L slot: the sun and today's total over the chart, the label right where it fits. */
static void draw_chart_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, const lang_t *lang)
{
    int top = r.y + 4;
    gfx_bitmap(fb, r.x + 6, top, &gfx_icon_forecast_16, GFX_BLACK);
    const gfx_font_t *tf = &gfx_font_sans_bold_16;
    const char *number = v->short_text[0] ? v->short_text : v->text;
    char forms[3][sizeof(v->text) + sizeof(v->unit) + 2], t[sizeof(forms[0])];
    snprintf(forms[0], sizeof(forms[0]), "%s %s", v->text, v->unit); /* then "2800 kWh", then "2800" */
    snprintf(forms[1], sizeof(forms[1]), "%s %s", number, v->unit);
    snprintf(forms[2], sizeof(forms[2]), "%s", number);
    int room = r.w - 32;
    int k = 0;
    while (k < 2 && gfx_text_width(tf, forms[k]) > room) {
        k++;
    }
    gfx_text_ellipsize(tf, forms[k], room, t, sizeof(t));
    int pen = gfx_text(fb, tf, r.x + 26, top + 13, t, GFX_BLACK);
    const gfx_font_t *lf = size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16;
    if (pen + 8 + gfx_text_width(lf, v->label) <= r.x + r.w - 6) {
        gfx_text(fb, lf, r.x + r.w - 6 - gfx_text_width(lf, v->label), top + 13, v->label, GFX_BLACK);
    }
    top += 20;
    draw_chart(fb, (gfx_rect_t){ (int16_t)(r.x + 2), (int16_t)top, (int16_t)(r.w - 4), (int16_t)(r.y + r.h - top - 2) },
               v, lang, size != UI_SIZE_M);
}

/* ---- the flow ---- */

static void arrow(gfx_fb_t *fb, int x, int y, int dx, int dy, int k)
{
    if (dx != 0) {
        gfx_fill_triangle(fb, x + dx * k, y, x - dx * k, y - k, x - dx * k, y + k, GFX_BLACK);
    } else {
        gfx_fill_triangle(fb, x, y + dy * k, x - k, y - dy * k, x + k, y - dy * k, GFX_BLACK);
    }
}

/* A line from (x0, y0) to (x1, y1), horizontal or vertical, `t` px thick: solid while power flows, dotted when
 * not; its arrow (`k` px) midway points the way the power goes (`dir` 1: towards the second point, -1 back). */
static void flow_line(gfx_fb_t *fb, int x0, int y0, int x1, int y1, int dir, int t, int k)
{
    bool h = y0 == y1;
    int len = h ? x1 - x0 : y1 - y0;
    for (int i = 0; i <= len; i++) {
        if (dir == 0 && i % 4 >= 2) {
            continue;
        }
        if (h) {
            gfx_vline(fb, x0 + i, y0 - t / 2, t, GFX_BLACK);
        } else {
            gfx_hline(fb, x0 - t / 2, y0 + i, t, GFX_BLACK);
        }
    }
    if (dir != 0) {
        arrow(fb, h ? x0 + len / 2 : x0, h ? y0 : y0 + len / 2, h ? dir : 0, h ? 0 : dir, k);
    }
}

/* The widgets' heading: the label left (cut to fit) and a note right, in the label's face. */
static int heading(gfx_fb_t *fb, gfx_rect_t r, const gfx_font_t *f, const char *label, const char *note)
{
    int note_w = note != NULL ? gfx_text_width(f, note) + 8 : 0;
    char cut[40];
    gfx_text_ellipsize(f, label, r.w - 12 - note_w, cut, sizeof(cut));
    int base = r.y + 4 + f->ascent;
    gfx_text(fb, f, r.x + 6, base, cut, GFX_BLACK);
    if (note != NULL) {
        gfx_text(fb, f, r.x + r.w - 6 - gfx_text_width(f, note), base, note, GFX_BLACK);
    }
    return r.y + 4 + f->line_height + 2;
}

static void centred(gfx_fb_t *fb, const gfx_font_t *f, int cx, int baseline, const char *text)
{
    gfx_text(fb, f, cx - gfx_text_width(f, text) / 2, baseline, text, GFX_BLACK);
}

/* A power as the flow shows it within `max_w` px: "2.31", or its shorter form, "2.3" or "200" (spec §5.1). */
static void kw_fit(const lang_t *lang, int32_t w, const gfx_font_t *f, int max_w, char *out, size_t size)
{
    kw_text(lang, w, out, size);
    if (gfx_text_width(f, out) > max_w) {
        ui_value_t v = { 0 };
        set_kw(lang, w, &v);
        if (v.short_text[0]) {
            snprintf(out, size, "%s", v.short_text);
        }
    }
}

/* A battery's charge within `max_w` px: "64 %", else "64%". */
static void soc_fit(int soc, const gfx_font_t *f, int max_w, char *out, size_t size)
{
    snprintf(out, size, "%d %%", soc);
    if (gfx_text_width(f, out) > max_w) {
        snprintf(out, size, "%d%%", soc);
    }
}

/* The flow's unit, its heading's: W while every power it shows is under 1 kW, else kW. */
static bool flow_in_watts(const energy_reading_t *e)
{
    return magnitude(e->pv_w) < 1000 && magnitude(e->load_w) < 1000 && magnitude(e->grid_w) < 1000;
}

/* A power as the flow shows it within `max_w` px, in the flow's unit: "382" W, or "2.31" kW or its shorter form. */
static void flow_text(const lang_t *lang, int32_t w, bool watts, const gfx_font_t *f, int max_w, char *out,
                      size_t size)
{
    if (watts) {
        snprintf(out, size, "%ld", magnitude(w));
    } else {
        kw_fit(lang, w, f, max_w, out, size);
    }
}

/* energy.flow, tall: the panels above a junction, the grid left, the house right, the battery under it. */
static void flow_diagram(gfx_fb_t *fb, gfx_rect_t r, int top, const ui_solar_t *s, const lang_t *lang, bool watts)
{
    const energy_reading_t *e = s->reading;
    char t[16];
    int body = r.y + r.h - top;
    bool bat = s->battery && e->soc >= 0 && body >= 100;
    int cx = r.x + r.w / 2;
    int py = top + (body - (bat ? 100 : 78)) / 2;
    int jy = py + 24 + 20;
    int gx = r.x + 22, hx = r.x + r.w - 22;
    const gfx_font_t *vf = &gfx_font_sans_bold_16;
    gfx_bitmap(fb, cx - 12, py, &gfx_icon_solar_24, GFX_BLACK);
    flow_text(lang, e->pv_w, watts, vf, r.x + r.w - 2 - (cx + 16), t, sizeof(t));
    gfx_text(fb, vf, cx + 16, py + 18, t, GFX_BLACK);
    flow_line(fb, cx, py + 26, cx, jy - 4, flow_dir(e->pv_w), 2, 4);
    gfx_bitmap(fb, gx - 12, jy - 12, &gfx_icon_grid_24, GFX_BLACK);
    gfx_bitmap(fb, hx - 12, jy - 12, &gfx_icon_house_24, GFX_BLACK);
    flow_line(fb, gx + 15, jy, cx - 4, jy, flow_dir(e->grid_w), 2, 4); /* + import: towards the house */
    flow_line(fb, cx + 4, jy, hx - 15, jy, flow_dir(e->load_w), 2, 4);
    gfx_fill_circle(fb, cx, jy, 3, GFX_BLACK);
    int side_w = 2 * (gx - r.x) - 4; /* the grid's and the house's values, centred under them, clear of the edges */
    flow_text(lang, e->grid_w, watts, vf, side_w, t, sizeof(t));
    centred(fb, vf, gx, jy + 12 + 17, t);
    flow_text(lang, e->load_w, watts, vf, side_w, t, sizeof(t));
    centred(fb, vf, hx, jy + 12 + 17, t);
    if (bat) {
        int by = jy + 24;
        flow_line(fb, cx, jy + 4, cx, by - 3, flow_dir(e->bat_w), 2, 4);
        ui_draw_battery(fb, cx - 15, by, 30, 14, e->soc);
        soc_fit(e->soc, vf, r.w - 4, t, sizeof(t));
        centred(fb, vf, cx, by + 14 + 16, t);
    }
}

/* energy.flow, short: the panels, the house, the grid and the battery in a row, arrows between. */
static void flow_row(gfx_fb_t *fb, gfx_rect_t r, int top, const ui_solar_t *s, const lang_t *lang, bool watts)
{
    const energy_reading_t *e = s->reading;
    bool bat = s->battery && e->soc >= 0 && r.w >= 180;
    int n = bat ? 4 : 3, col = (r.w - 8) / n;
    int iy = top + (r.y + r.h - top - 24 - 20) / 2;
    const gfx_bitmap_t *icons[3] = { &gfx_icon_solar_24, &gfx_icon_house_24, &gfx_icon_grid_24 };
    int32_t power[3] = { e->pv_w, e->load_w, e->grid_w };
    const gfx_font_t *vf = &gfx_font_sans_bold_16;
    for (int i = 0; i < n; i++) {
        int cx = r.x + 4 + col * i + col / 2;
        char t[16];
        if (i == 3) {
            ui_draw_battery(fb, cx - 15, iy + 5, 30, 14, e->soc);
            soc_fit(e->soc, vf, col - 2, t, sizeof(t));
        } else {
            gfx_bitmap(fb, cx - 12, iy, icons[i], GFX_BLACK);
            flow_text(lang, power[i], watts, vf, col - 2, t, sizeof(t));
        }
        centred(fb, vf, cx, iy + 24 + 17, t);
    }
    int ay = iy + 12;
    if (flow_dir(e->pv_w) > 0) { /* the panels feed the house */
        arrow(fb, r.x + 4 + col, ay, 1, 0, 5);
    }
    int gd = flow_dir(e->grid_w);
    if (gd != 0) { /* to the grid, or from it */
        arrow(fb, r.x + 4 + 2 * col, ay, -gd, 0, 5);
    }
    if (bat && flow_dir(e->bat_w) > 0) { /* a branch, not in line: a bolt while it charges */
        gfx_bitmap(fb, r.x + 4 + 3 * col - 8, iy + 4, &gfx_icon_bolt_16, GFX_BLACK);
    }
}

/* energy.flow in an M or L slot: a diagram from 76 px under the heading, a row below that. */
static void draw_flow_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, const lang_t *lang)
{
    const gfx_font_t *lf = size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16;
    bool watts = flow_in_watts(v->solar->reading);
    int top = heading(fb, r, lf, v->label, watts ? "W" : "kW");
    if (r.y + r.h - top >= 76) {
        flow_diagram(fb, r, top, v->solar, lang, watts);
    } else {
        flow_row(fb, r, top, v->solar, lang, watts);
    }
}

bool ui_solar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, const lang_t *lang)
{
    if (v->state == UI_VALUE_MISSING || v->solar == NULL || (v->kind != UI_FK_CHART && v->kind != UI_FK_FLOW)) {
        return false;
    }
    if (v->kind == UI_FK_FLOW) {
        draw_flow_widget(fb, r, size, v, lang);
    } else {
        draw_chart_widget(fb, r, size, v, lang);
    }
    return true;
}

/* ---- the Solar and Energy layouts ---- */

#define PLACEHOLDER "\xE2\x80\x94" /* em dash: a value there is none of */

static int ink(const gfx_font_t *f)
{
    const gfx_glyph_t *g = gfx_font_glyph(f, '0');
    return g != NULL ? g->height : f->ascent;
}

static void stat_row(gfx_fb_t *fb, int x, int right, int baseline, const char *label, const char *value)
{
    gfx_text(fb, &gfx_font_sans_16, x, baseline, label, GFX_BLACK);
    gfx_text(fb, &gfx_font_sans_bold_16, right - gfx_text_width(&gfx_font_sans_bold_16, value), baseline, value,
             GFX_BLACK);
}

/* What a stat row's number may take beside `label` between `x` and `right`, 8 px apart, before its `unit`. */
static int stat_room(int x, int right, const char *label, const char *unit)
{
    return right - x - 8 - gfx_text_width(&gfx_font_sans_16, label) - gfx_text_width(&gfx_font_sans_bold_16, unit);
}

/* A stat row's power: "390 W", "4.12 kW", a kW number shorter where it wouldn't fit ("200 kW"). */
static void stat_kw(const lang_t *lang, int32_t w, int x, int right, const char *label, char *out, size_t size)
{
    char n[16];
    const char *unit = power_text(lang, w, n, sizeof(n));
    if (strcmp(unit, "kW") == 0) {
        kw_fit(lang, w, &gfx_font_sans_bold_16, stat_room(x, right, label, " kW"), n, sizeof(n));
    }
    snprintf(out, size, "%s %s", n, unit);
}

/* A stat row's energy: "18.4 kWh", whole kWh where it wouldn't fit ("1333 kWh"); a dash without one. */
static void stat_kwh(const lang_t *lang, uint32_t wh, int x, int right, const char *label, char *out, size_t size)
{
    char n[16];
    if (wh == SOLAR_WH_NONE) {
        snprintf(out, size, "%s", PLACEHOLDER);
        return;
    }
    kwh_text(lang, wh, n, sizeof(n));
    if (gfx_text_width(&gfx_font_sans_bold_16, n) > stat_room(x, right, label, " kWh") && wh >= 9950) {
        snprintf(n, sizeof(n), "%lu", (unsigned long)((wh + 500) / 1000));
    }
    snprintf(out, size, "%s kWh", n);
}

static void placeholder(gfx_fb_t *fb, gfx_rect_t a, const gfx_bitmap_t *icon, const char *text)
{
    gfx_bitmap(fb, a.x + (a.w - icon->width) / 2, a.y + a.h / 2 - 48, icon, GFX_BLACK);
    gfx_text_in_rect(fb, &gfx_font_sans_bold_20, (gfx_rect_t){ a.x, (int16_t)(a.y + a.h / 2 + 10), a.w, 28 },
                     GFX_ALIGN_CENTER, text, GFX_BLACK);
}

/* The weather forecast's sky for local day `day`, or -1 where it has none. */
static int sky_on(const ui_context_t *ctx, int32_t day)
{
    const ds_weather_t *w = ctx->ds != NULL ? ds_weather(ctx->ds) : NULL;
    int k = w != NULL ? day - w->day0_local : -1;
    return k >= 0 && k < DS_WX_DAYS ? (int)weather_sky(w->days[k].code) : -1;
}

/* A day's total in a footer half, centred: its weekday, its weather and its energy, or a dash without one. */
static void day_total(gfx_fb_t *fb, gfx_rect_t c, const ui_context_t *ctx, const char *name, int sky, uint32_t wh)
{
    char t[16];
    bool known = wh != SOLAR_WH_NONE;
    if (known) {
        kwh_text(ctx->lang, wh, t, sizeof(t));
    } else {
        snprintf(t, sizeof(t), "%s", PLACEHOLDER);
    }
    const gfx_bitmap_t *icon = sky >= 0 ? ui_sky_icon(sky, false, 24) : NULL;
    int w = gfx_text_width(&gfx_font_sans_16, name) + 8 + (icon ? 30 : 0) + gfx_text_width(&gfx_font_sans_bold_20, t) +
            (known ? 3 + gfx_text_width(&gfx_font_sans_16, "kWh") : 0);
    int base = c.y + (c.h + ink(&gfx_font_sans_bold_20)) / 2;
    int x = c.x + (c.w - w) / 2;
    x = gfx_text(fb, &gfx_font_sans_16, x, base, name, GFX_BLACK) + 8;
    if (icon != NULL) {
        gfx_bitmap(fb, x, c.y + (c.h - 24) / 2, icon, GFX_BLACK);
        x += 30;
    }
    x = gfx_text(fb, &gfx_font_sans_bold_20, x, base, t, GFX_BLACK) + 3;
    if (known) {
        gfx_text(fb, &gfx_font_sans_16, x, base, "kWh", GFX_BLACK);
    }
}

bool ui_draw_solar_layout(gfx_fb_t *fb, gfx_rect_t a, const ui_context_t *ctx)
{
    const lang_t *lang = ctx->lang;
    ui_value_t chart;
    ui_resolve(ctx, UI_FIELD_PV_CHART, &chart); /* today's quarter hours, the quarter hour now, the readings' */
    if (chart.state == UI_VALUE_MISSING) {
        placeholder(fb, a, &gfx_icon_forecast_48, lang_str(lang, LS_NO_SOLAR));
        return false;
    }
    const solar_forecast_t *f = ctx->solar->forecast;
    int32_t today = ctx->local_day;
    char t[32], u[sizeof(t) + 8];
    /* today, large, and three numbers beside it */
    gfx_bitmap(fb, a.x + 8, a.y + 6, &gfx_icon_forecast_24, GFX_BLACK);
    gfx_text(fb, &gfx_font_sans_16, a.x + 38, a.y + 23, lang_str(lang, LS_PV_TODAY), GFX_BLACK);
    int base = a.y + 36 + ink(&gfx_font_num_cb_48);
    int col = a.x + 196, right = a.x + a.w - 10;
    int room = col - 6 - (a.x + 8) - 5 - gfx_text_width(&gfx_font_sans_bold_20, "kWh");
    const char *big = gfx_text_width(&gfx_font_num_cb_48, chart.text) > room && chart.short_text[0] != '\0'
                          ? chart.short_text /* from 100 kWh: whole kWh */
                          : chart.text;
    int pen = gfx_text(fb, &gfx_font_num_cb_48, a.x + 8, base, big, GFX_BLACK);
    gfx_text(fb, &gfx_font_sans_bold_20, pen + 5, base, "kWh", GFX_BLACK);
    stat_kw(lang, chart.chart_q[chart.quarter] * SOLAR_UNIT_W, col, right, lang_str(lang, LS_NOW), u, sizeof(u));
    stat_row(fb, col, right, a.y + 22, lang_str(lang, LS_NOW), u);
    uint32_t peak_w = 0;
    int peak_at = 0;
    char label[40];
    if (solar_peak(f, today, &peak_w, &peak_at)) { /* "Peak 12:45 ... 4.12 kW" */
        char at[16];
        quarter_time(ctx, peak_at, at, sizeof(at));
        snprintf(label, sizeof(label), "%s %s", lang_str(lang, LS_PV_PEAK), at);
    } else {
        snprintf(label, sizeof(label), "%s", lang_str(lang, LS_PV_PEAK));
    }
    stat_kw(lang, (int32_t)peak_w, col, right, label, u, sizeof(u));
    stat_row(fb, col, right, a.y + 46, label, u);
    int seconds = ctx->local.tm_min % 15 * 60 + ctx->local.tm_sec;
    stat_kwh(lang, solar_left_wh(f, today, chart.quarter, seconds), col, right, lang_str(lang, LS_PV_LEFT), u,
             sizeof(u));
    stat_row(fb, col, right, a.y + 70, lang_str(lang, LS_PV_LEFT), u);
    gfx_hline(fb, a.x + 8, a.y + 82, a.w - 16, GFX_BLACK);
    /* the day's chart */
    draw_chart(fb, (gfx_rect_t){ (int16_t)(a.x + 2), (int16_t)(a.y + 88), (int16_t)(a.w - 6), 152 }, &chart, lang,
               true);
    /* tomorrow and the day after */
    gfx_hline(fb, a.x + 8, a.y + 245, a.w - 16, GFX_BLACK);
    gfx_vline(fb, a.x + a.w / 2, a.y + 251, 22, GFX_BLACK);
    for (int d = 1; d <= 2; d++) {
        gfx_rect_t c = { (int16_t)(a.x + (d - 1) * a.w / 2), (int16_t)(a.y + 247), (int16_t)(a.w / 2), 32 };
        day_total(fb, c, ctx, lang->weekdays_short[(ctx->local.tm_wday + d) % 7], sky_on(ctx, today + d),
                  solar_day_wh(f, today + d));
    }
    return chart.state == UI_VALUE_STALE;
}

/* A node's words under its icon: the label, then the power, centred on cx. */
static void node_text(gfx_fb_t *fb, int cx, int y, const char *label, const char *value)
{
    gfx_text(fb, &gfx_font_sans_16, cx - gfx_text_width(&gfx_font_sans_16, label) / 2, y + 14, label, GFX_BLACK);
    gfx_text(fb, &gfx_font_sans_bold_20, cx - gfx_text_width(&gfx_font_sans_bold_20, value) / 2, y + 36, value,
             GFX_BLACK);
}

/* One of today's totals: its icon and label, then its value and unit, or a dash without one. The value may run on
 * under the next cell's icon, ending before `limit`: in a row of four, from 100 kWh whole kWh, then under its own
 * icon. */
static void total(gfx_fb_t *fb, gfx_rect_t c, int limit, const gfx_bitmap_t *icon, const char *label,
                  const ui_value_t *v)
{
    gfx_bitmap(fb, c.x + 6, c.y + 4, icon, GFX_BLACK);
    gfx_text(fb, &gfx_font_sans_12, c.x + 26, c.y + 15, label, GFX_BLACK);
    bool known = v->state != UI_VALUE_MISSING;
    const char *text = known ? v->text : PLACEHOLDER;
    int x = c.x + 26, end = limit - (known ? 3 + gfx_text_width(&gfx_font_sans_16, v->unit) : 0);
    if (known && x + gfx_text_width(&gfx_font_sans_bold_20, text) > end && v->short_text[0] != '\0') {
        text = v->short_text;
        x = x + gfx_text_width(&gfx_font_sans_bold_20, text) > end ? c.x + 6 : x;
    }
    int pen = gfx_text(fb, &gfx_font_sans_bold_20, x, c.y + 38, text, GFX_BLACK);
    if (known) {
        gfx_text(fb, &gfx_font_sans_16, pen + 3, c.y + 38, v->unit, GFX_BLACK);
    }
}

bool ui_draw_energy_layout(gfx_fb_t *fb, gfx_rect_t a, const ui_context_t *ctx)
{
    const ui_solar_t *s = ctx->solar;
    const lang_t *lang = ctx->lang;
    const energy_reading_t *e = s != NULL ? s->reading : NULL;
    if (e == NULL || e->at == 0) {
        placeholder(fb, a, &gfx_icon_house_48, lang_str(lang, LS_NO_ENERGY));
        return false;
    }
    bool bat = s->battery && e->soc >= 0;
    char t[24], v[32];
    int cx = a.x + a.w / 2;
    int jy = a.y + (bat ? 92 : 104); /* where the lines meet */
    int gx = a.x + 56, hx = a.x + a.w - 56;
    /* the panels, top centre, the power beside them */
    gfx_bitmap(fb, cx - 24, a.y + 4, &gfx_icon_solar_48, GFX_BLACK);
    const char *unit = power_text(lang, e->pv_w, t, sizeof(t));
    int pen = gfx_text(fb, &gfx_font_sans_bold_28, cx + 34, a.y + 44, t, GFX_BLACK);
    gfx_text(fb, &gfx_font_sans_16, pen + 3, a.y + 44, unit, GFX_BLACK);
    gfx_text(fb, &gfx_font_sans_16, cx + 34, a.y + 18, lang_str(lang, LS_EN_PV), GFX_BLACK);
    /* when the inverter's reading came */
    char when[16];
    ui_clock_text(ctx, (time_t)e->at, when, sizeof(when));
    snprintf(v, sizeof(v), "SolaX %s", when);
    gfx_text(fb, &gfx_font_sans_12, a.x + 6, a.y + 14, v, GFX_BLACK);
    /* the lines, then the junction */
    flow_line(fb, cx, a.y + 56, cx, jy - 6, flow_dir(e->pv_w), 2, 6);
    flow_line(fb, gx + 30, jy, cx - 6, jy, flow_dir(e->grid_w), 2, 6); /* + import: towards the house */
    flow_line(fb, cx + 6, jy, hx - 30, jy, flow_dir(e->load_w), 2, 6);
    gfx_fill_circle(fb, cx, jy, 5, GFX_BLACK);
    /* the grid, left; the house, right */
    gfx_bitmap(fb, gx - 24, jy - 24, &gfx_icon_grid_48, GFX_BLACK);
    gfx_bitmap(fb, hx - 24, jy - 24, &gfx_icon_house_48, GFX_BLACK);
    unit = power_text(lang, e->grid_w, t, sizeof(t));
    snprintf(v, sizeof(v), "%s %s", t, unit);
    int gd = flow_dir(e->grid_w);
    node_text(fb, gx, jy + 26, lang_str(lang, gd < 0 ? LS_EN_EXPORTING : gd > 0 ? LS_EN_IMPORTING : LS_EN_GRID), v);
    unit = power_text(lang, e->load_w, t, sizeof(t));
    snprintf(v, sizeof(v), "%s %s", t, unit);
    node_text(fb, hx, jy + 26, lang_str(lang, LS_EN_LOAD), v);
    int totals_y = a.y + (bat ? 222 : 176);
    if (bat) { /* the battery, below the junction: its state and power left of it, its charge right */
        int by = jy + 70;
        flow_line(fb, cx, jy + 6, cx, by - 6, flow_dir(e->bat_w), 2, 6);
        ui_draw_battery(fb, cx - 30, by, 60, 28, e->soc);
        snprintf(t, sizeof(t), "%d %%", e->soc);
        gfx_text(fb, &gfx_font_sans_bold_28, cx + 40, by + 24, t, GFX_BLACK);
        int bd = flow_dir(e->bat_w);
        const char *state = lang_str(lang, bd > 0 ? LS_EN_CHARGING : bd < 0 ? LS_EN_DISCHARGING : LS_EN_BATTERY);
        unit = power_text(lang, e->bat_w, t, sizeof(t));
        snprintf(v, sizeof(v), "%s %s", t, unit);
        int rx = cx - 40;
        gfx_text(fb, &gfx_font_sans_16, rx - gfx_text_width(&gfx_font_sans_16, state), by + 10, state, GFX_BLACK);
        gfx_text(fb, &gfx_font_sans_bold_20, rx - gfx_text_width(&gfx_font_sans_bold_20, v), by + 32, v, GFX_BLACK);
    }
    /* today's totals, two by two, or in a row with a battery */
    gfx_hline(fb, a.x + 8, totals_y - 6, a.w - 16, GFX_BLACK);
    static const ui_field_id_t k_totals[4] = { UI_FIELD_EN_YIELD, UI_FIELD_EN_EXPORT, UI_FIELD_EN_IMPORT,
                                               UI_FIELD_EN_SELF };
    static const lang_str_t k_labels[4] = { LS_EN_PRODUCED, LS_EN_EXPORTED, LS_EN_IMPORTED, LS_EN_SELF };
    const gfx_bitmap_t *icons[4] = { &gfx_icon_solar_16, &gfx_icon_grid_16, &gfx_icon_grid_16,
                                     &gfx_icon_self_use_16 };
    int w = a.w / (bat ? 4 : 2), h = bat ? 48 : 50;
    bool stale = false;
    for (int i = 0; i < 4; i++) {
        gfx_rect_t c = bat ? (gfx_rect_t){ (int16_t)(a.x + i * w), (int16_t)totals_y, (int16_t)w, (int16_t)h }
                           : (gfx_rect_t){ (int16_t)(a.x + (i % 2) * w), (int16_t)(totals_y + (i / 2) * h),
                                           (int16_t)w, (int16_t)h };
        ui_value_t value;
        ui_resolve(ctx, k_totals[i], &value);
        bool last = bat ? i == 3 : i % 2 == 1; /* the others end 6 px before the next one's number */
        total(fb, c, last ? a.x + a.w - 4 : c.x + c.w + 20, icons[i], lang_str(lang, k_labels[i]), &value);
    }
    ui_value_t power;
    ui_resolve(ctx, UI_FIELD_EN_PV, &power);
    stale = power.state == UI_VALUE_STALE;
    return stale;
}

/* ---- the sample day ---- */

#define PI 3.14159265358979323846
#define DEMO_PEAK_W 4120.0
#define DEMO_RISE_H 6.75
#define DEMO_SET_H 18.85

/* The forecast's mean power for quarter hour `i` of day `d` (0 today, 1 tomorrow), in W. */
static double demo_w(int d, int i)
{
    double t = (i + 0.5) / 4.0;
    if (t <= DEMO_RISE_H || t >= DEMO_SET_H) {
        return 0;
    }
    double w = DEMO_PEAK_W * pow(sin(PI * (t - DEMO_RISE_H) / (DEMO_SET_H - DEMO_RISE_H)), 1.25);
    if (d == 0 && t > 15.0 && t < 17.0) {
        w *= 0.55 + 0.25 * sin((t - 15.0) * 6.0); /* cloud */
    }
    if (d == 1) {
        w *= 0.38 + 0.12 * sin(t * 2.1); /* overcast */
    }
    return w;
}

void ui_solar_demo(int32_t day, time_t midnight, time_t now, bool live, bool battery, solar_forecast_t *forecast,
                   energy_reading_t *reading, energy_day_t *energy_day)
{
    memset(forecast, 0, sizeof(*forecast));
    forecast->day = day;
    forecast->fetched = (uint32_t)(midnight + 5 * 3600 + 48 * 60);
    for (int d = 0; d < 2; d++) {
        uint32_t q_sum = 0;
        for (int i = 0; i < SOLAR_STEPS; i++) {
            forecast->q[d][i] = (uint16_t)lround(demo_w(d, i) / SOLAR_UNIT_W);
            q_sum += forecast->q[d][i];
        }
        forecast->wh[d] = (uint32_t)lround(q_sum * (SOLAR_UNIT_W / 4.0));
    }
    forecast->wh[2] = 21400;
    memset(reading, 0, sizeof(*reading));
    energy_day_init(energy_day);
    if (!live) {
        return;
    }
    double yield = 0;
    for (int i = 0; i < SOLAR_STEPS && midnight + (i + 1) * 900 <= now; i++) {
        double f = demo_w(0, i), a = i < 38 ? f * 0.7 : f * (1.04 + 0.03 * sin(i * 1.7)); /* mist, then sun */
        energy_day->q[i] = (uint16_t)lround(a / ENERGY_UNIT_W);
        energy_day->n[i] = 3;
        yield += a / 4.0;
    }
    uint32_t yield_wh = (uint32_t)lround(yield);
    uint32_t exported = battery ? yield_wh * 38 / 100 : yield_wh * 62 / 100;
    *reading = (energy_reading_t){
        .at = (uint32_t)(now - 180),
        .pv_w = 3420,
        .load_w = 860,
        .bat_w = battery ? 1200 : 0,
        .soc = (int16_t)(battery ? 64 : -1),
        .inverter = battery ? 5 : 4, /* X3-Hybrid, X1-Boost */
        .yield_wh = yield_wh,
        .to_grid_wh = 1000000 + exported,
        .from_grid_wh = 2000000 + (battery ? 600 : 1400),
    };
    reading->grid_w = -(reading->pv_w - reading->load_w - reading->bat_w);
    energy_day->day = day;
    energy_day->base_at = (uint32_t)(midnight + 120);
    energy_day->base_to_wh = 1000000;
    energy_day->base_from_wh = 2000000;
    energy_day->last_at = reading->at;
}
