#include "ui_solar.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "gfx_icons.h"
#include "ui_internal.h"

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

/* "18.4" kWh from Wh. */
static void kwh_text(const lang_t *lang, uint32_t wh, char *out, size_t size)
{
    lang_format_decimal(lang, (long)((wh + 50) / 100), 1, out, size);
}

/* A power and its shorter form where a number doesn't fit (spec §5.1): from 1 kW, one decimal, from 10 kW whole;
 * below 1 kW it would lose most of the value. */
static void set_kw(const lang_t *lang, int32_t w, ui_value_t *out)
{
    kw_text(lang, w, out->text, sizeof(out->text));
    long a = w < 0 ? -(long)w : w;
    if (a >= 10000) {
        snprintf(out->short_text, sizeof(out->short_text), "%ld", (a + 500) / 1000);
    } else if (a >= 1000) {
        lang_format_decimal(lang, (a + 50) / 100, 1, out->short_text, sizeof(out->short_text));
    }
    snprintf(out->unit, sizeof(out->unit), "kW");
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
            kw_text(lang, r->bat_w, t, sizeof(t));
            snprintf(out->extra, sizeof(out->extra), "%s kW", t);
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
 * produced and a line over them is what was forecast. With `labels`, kW on the left and every third hour below. */
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
            f += fq[st] * (uint32_t)SOLAR_UNIT_W;
            a += actual && aq[st] != ENERGY_NONE ? aq[st] * (uint32_t)ENERGY_UNIT_W : 0;
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

/* energy.flow, tall: the panels above a junction, the grid left, the house right, the battery under it. */
static void flow_diagram(gfx_fb_t *fb, gfx_rect_t r, int top, const ui_solar_t *s, const lang_t *lang)
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
    kw_fit(lang, e->pv_w, vf, r.x + r.w - 2 - (cx + 16), t, sizeof(t));
    gfx_text(fb, vf, cx + 16, py + 18, t, GFX_BLACK);
    flow_line(fb, cx, py + 26, cx, jy - 4, flow_dir(e->pv_w), 2, 4);
    gfx_bitmap(fb, gx - 12, jy - 12, &gfx_icon_grid_24, GFX_BLACK);
    gfx_bitmap(fb, hx - 12, jy - 12, &gfx_icon_house_24, GFX_BLACK);
    flow_line(fb, gx + 15, jy, cx - 4, jy, flow_dir(e->grid_w), 2, 4); /* + import: towards the house */
    flow_line(fb, cx + 4, jy, hx - 15, jy, flow_dir(e->load_w), 2, 4);
    gfx_fill_circle(fb, cx, jy, 3, GFX_BLACK);
    int side_w = 2 * (gx - r.x) - 4; /* the grid's and the house's values, centred under them, clear of the edges */
    kw_fit(lang, e->grid_w, vf, side_w, t, sizeof(t));
    centred(fb, vf, gx, jy + 12 + 17, t);
    kw_fit(lang, e->load_w, vf, side_w, t, sizeof(t));
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
static void flow_row(gfx_fb_t *fb, gfx_rect_t r, int top, const ui_solar_t *s, const lang_t *lang)
{
    const energy_reading_t *e = s->reading;
    bool bat = s->battery && e->soc >= 0 && r.w >= 180;
    int n = bat ? 4 : 3, col = (r.w - 8) / n;
    int iy = top + (r.y + r.h - top - 24 - 20) / 2;
    const gfx_bitmap_t *icons[3] = { &gfx_icon_solar_24, &gfx_icon_house_24, &gfx_icon_grid_24 };
    int32_t watts[3] = { e->pv_w, e->load_w, e->grid_w };
    const gfx_font_t *vf = &gfx_font_sans_bold_16;
    for (int i = 0; i < n; i++) {
        int cx = r.x + 4 + col * i + col / 2;
        char t[16];
        if (i == 3) {
            ui_draw_battery(fb, cx - 15, iy + 5, 30, 14, e->soc);
            soc_fit(e->soc, vf, col - 2, t, sizeof(t));
        } else {
            gfx_bitmap(fb, cx - 12, iy, icons[i], GFX_BLACK);
            kw_fit(lang, watts[i], vf, col - 2, t, sizeof(t));
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
    int top = heading(fb, r, lf, v->label, "kW");
    if (r.y + r.h - top >= 76) {
        flow_diagram(fb, r, top, v->solar, lang);
    } else {
        flow_row(fb, r, top, v->solar, lang);
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
