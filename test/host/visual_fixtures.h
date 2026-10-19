#pragma once

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "ui_history.h"
#include "ui_split.h"
#include "ui_visual.h"

/* The visuals' golden sheets (spec §5.3, M7, D44; test_ui_visual_golden.c, render_visual.c): the spike's renders
 * that the owner approved, drawn by the real visuals into empty split cells, with histories made up the same way. */

#define VIS_NOW_MS 89999000u /* 25 h of the monotonic clock, at 1 s */
#define VIS_NONE NAN

enum { VIS_S_CPU, VIS_S_TEMP, VIS_S_GPU, VIS_S_GTEMP, VIS_S_ROOM, VIS_S_NET, VIS_S_GAP, VIS_S_SILENCE, VIS_S_COUNT };

static uint32_t s_vis_seed;
static inline float vis_rnd(void)
{
    s_vis_seed = s_vis_seed * 1103515245u + 12345u;
    return (float)((s_vis_seed >> 8) & 0xFFFF) / 65535.0f;
}

/* The histories, made once: a day of CPU load at 1 s, the last minute of the others, the indoor temperature every
 * 5 min, a minute with a reconnect, and two hours with a silence. */
static inline const ui_series_t *vis_series(int which)
{
    static ui_series_t *s_all;
    if (s_all == NULL) {
        s_all = calloc(VIS_S_COUNT, sizeof(ui_series_t));
        s_vis_seed = 12345;
        for (uint32_t t = 0; t <= VIS_NOW_MS / 1000; t++) { /* CPU load */
            uint32_t a = VIS_NOW_MS / 1000 - t, m = (t / 60) % 1440;
            float v;
            if (a < 60) {
                float i = (float)(59 - a);
                v = 18.0f + 10.0f * sinf(i / 6.0f) + 8.0f * vis_rnd() + (i > 40.0f ? 35.0f : 0.0f);
            } else if (a < 3600) {
                v = 8.0f + 6.0f * vis_rnd();
                if ((a > 550 && a < 1000) || (a > 2300 && a < 2700)) {
                    v = 70.0f + 25.0f * vis_rnd();
                }
                v = vis_rnd() > 0.97f ? 55.0f + 40.0f * vis_rnd() : v;
            } else {
                bool day = m > 8 * 60 && m < 19 * 60;
                v = day ? 25.0f + 30.0f * vis_rnd() + (vis_rnd() > 0.9f ? 40.0f : 0.0f) : 4.0f + 3.0f * vis_rnd();
            }
            ui_series_push(&s_all[VIS_S_CPU], t * 1000, v);
        }
        for (int i = 0; i < 60; i++) {
            uint32_t t = VIS_NOW_MS - (uint32_t)(59 - i) * 1000;
            float f = (float)i;
            ui_series_push(&s_all[VIS_S_TEMP], t, 52.0f + 6.0f * sinf(f / 9.0f) + 2.0f * vis_rnd() + (i > 40 ? 14.0f : 0));
            ui_series_push(&s_all[VIS_S_GPU], t, i < 20 ? 3.0f + 2.0f * vis_rnd() : 88.0f + 10.0f * vis_rnd());
            ui_series_push(&s_all[VIS_S_GTEMP], t, i < 20 ? 41.0f + vis_rnd() : 41.0f + (f - 20.0f) * 0.6f + vis_rnd());
            ui_series_push(&s_all[VIS_S_NET], t, 6.0f + 8.0f * vis_rnd() + (i > 30 && i < 45 ? 30.0f : 0.0f));
            if (i == 26) {
                ui_series_break(&s_all[VIS_S_GAP], t); /* the session reconnected after ten silent seconds */
            }
            if (i <= 25 || i >= 36) {
                ui_series_push(&s_all[VIS_S_GAP], t, 40.0f + 15.0f * sinf(f / 5.0f) + 4.0f * vis_rnd());
            }
        }
        for (int i = 0; i < 25; i++) { /* two hours of the indoor temperature */
            ui_series_push(&s_all[VIS_S_ROOM], VIS_NOW_MS - (uint32_t)(24 - i) * 300000,
                           22.1f + 0.08f * (float)i + 0.2f * vis_rnd());
        }
        bool quiet = false;
        for (uint32_t k = 0; k < 2 * 3600 * 4; k++) { /* two hours at 250 ms, quiet for 20 min in the middle */
            uint32_t t = VIS_NOW_MS - 2 * 3600 * 1000 + k * 250;
            uint32_t a = (VIS_NOW_MS - t) / 60000;
            if (a >= 50 && a < 70) {
                quiet = true;
                continue;
            }
            if (quiet) { /* the next session's first message */
                ui_series_break(&s_all[VIS_S_SILENCE], t);
                quiet = false;
            }
            ui_series_push(&s_all[VIS_S_SILENCE], t, 30.0f + 20.0f * sinf((float)k / 2000.0f) + 10.0f * vis_rnd());
        }
    }
    return which >= 0 && which < VIS_S_COUNT ? &s_all[which] : NULL;
}

/* One cell: how it shows, what it shows. */
typedef struct {
    ui_visual_t vis;
    const char *label, *unit;
    int decimals;
    float value; /* VIS_NONE: no value */
    float lo, hi; /* the field's own range; lo == hi: none */
    int series;   /* VIS_S_*, or -1 */
} vis_cell_t;

#define CHART(span, over) { .as = UI_VIS_CHART, .span_s = (span), .numbers = (over) }
#define CHART_AUTO(span, over)                                                                                   \
    { .as = UI_VIS_CHART, .span_s = (span), .numbers = (over), .min_mode = UI_LIMIT_AUTO, .max_mode = UI_LIMIT_AUTO }
#define SHOW(a) { .as = (a) }

/* The value a cell shows, resolved as a pc.* field would be, in `lang`. */
static inline ui_value_t vis_value(const vis_cell_t *c, const lang_t *lang)
{
    ui_value_t v;
    memset(&v, 0, sizeof(v));
    v.kind = UI_FK_NUMBER;
    v.label = c->label;
    snprintf(v.unit, sizeof(v.unit), "%s", c->unit);
    if (c->lo < c->hi) {
        v.has_range = true;
        v.range_min = c->lo;
        v.range_max = c->hi;
    }
    if (!isnan(c->value)) {
        v.state = UI_VALUE_FRESH;
        v.has_num = true;
        v.num = c->value;
        long scaled = lroundf(c->value * powf(10.0f, (float)c->decimals));
        lang_format_decimal(lang, scaled, c->decimals, v.text, sizeof(v.text));
        if (c->decimals > 0) {
            lang_format_decimal(lang, lroundf(c->value), 0, v.short_text, sizeof(v.short_text));
        }
    }
    return v;
}

/* An empty split dashboard of rows × cols (the status bar, the lines) and its cells. */
static inline void vis_base(gfx_fb_t *fb, int rows, int cols, const lang_t *lang, ui_split_geometry_t *g)
{
    ui_context_t ctx;
    ui_preset_t p;
    fixture_dashboard("home", &ctx, &p);
    ctx.lang = lang;
    uint8_t none[UI_SPLIT_CELLS] = { 0 };
    fixture_grid_split(&p, rows, cols, 0, none, (size_t)(rows * cols));
    ui_draw_dashboard(fb, &ctx, &p, NULL);
    ui_split_layout(p.split, ui_split_area(), g);
}

static inline void vis_sheet(gfx_fb_t *fb, int rows, int cols, const vis_cell_t *cells, int count, const char *lang_code)
{
    const lang_t *lang = lang_get(lang_code);
    ui_split_geometry_t g;
    vis_base(fb, rows, cols, lang, &g);
    for (int i = 0; i < count && i < g.cells; i++) {
        gfx_rect_t c = g.cell[i];
        gfx_rect_t r = { (int16_t)(c.x + 1), (int16_t)(c.y + 1), (int16_t)(c.w - 2), (int16_t)(c.h - 2) };
        ui_value_t v = vis_value(&cells[i], lang);
        ui_visual_draw(fb, r, &v, &cells[i].vis, vis_series(cells[i].series), VIS_NOW_MS, lang);
    }
}

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static inline bool fixture_visual(const char *name, gfx_fb_t *fb)
{
    if (strcmp(name, "visuals_m") == 0) { /* the four, 199×99 */
        static const vis_cell_t k[] = {
            { CHART(60, true), "CPU load", "%", 0, 61, 0, 100, VIS_S_CPU },
            { SHOW(UI_VIS_RING), "GPU load", "%", 0, 94, 0, 100, -1 },
            { SHOW(UI_VIS_GAUGE), "CPU temperature", "°C", 0, 71, 20, 100, -1 },
            { SHOW(UI_VIS_BAR), "RAM", "GB", 1, 21.3f, 0, 64, -1 },
        };
        vis_sheet(fb, 2, 2, k, N(k), "en");
    } else if (strcmp(name, "visuals_s") == 0) { /* 3×3, 132×92 */
        static const vis_cell_t k[] = {
            { CHART(60, true), "CPU load", "%", 0, 61, 0, 100, VIS_S_CPU },
            { CHART_AUTO(60, false), "CPU", "°C", 0, 72, 0, 0, VIS_S_TEMP },
            { SHOW(UI_VIS_RING), "GPU", "%", 0, 94, 0, 100, -1 },
            { SHOW(UI_VIS_GAUGE), "CPU", "°C", 0, 71, 20, 100, -1 },
            { SHOW(UI_VIS_BAR), "RAM", "%", 0, 33, 0, 100, -1 },
            { SHOW(UI_VIS_GAUGE), "Fan", "rpm", 0, 1240, 0, 2000, -1 },
            { CHART(3600, true), "Indoor", "°C", 1, 23.8f, 0, 0, VIS_S_ROOM },
            { SHOW(UI_VIS_RING), "Humidity", "%", 0, 45, 0, 100, -1 },
            { SHOW(UI_VIS_BAR), "Battery", "%", 0, 87, 0, 100, -1 },
        };
        vis_sheet(fb, 3, 3, k, N(k), "en");
    } else if (strcmp(name, "visuals_small") == 0) { /* 4×4, 100×69 */
        static const vis_cell_t k[] = {
            { CHART(60, true), "CPU", "%", 0, 61, 0, 100, VIS_S_CPU },
            { CHART(60, true), "GPU", "%", 0, 94, 0, 100, VIS_S_GPU },
            { SHOW(UI_VIS_RING), "CPU", "%", 0, 61, 0, 100, -1 },
            { SHOW(UI_VIS_RING), "GPU", "%", 0, 94, 0, 100, -1 },
            { SHOW(UI_VIS_GAUGE), "CPU", "°C", 0, 71, 20, 100, -1 },
            { SHOW(UI_VIS_GAUGE), "GPU", "°C", 0, 64, 20, 100, -1 },
            { SHOW(UI_VIS_BAR), "RAM", "GB", 1, 21.3f, 0, 64, -1 },
            { SHOW(UI_VIS_BAR), "VRAM", "GB", 1, 6.2f, 0, 12, -1 },
            { CHART(3600, true), "Temp", "°C", 1, 23.8f, 0, 0, VIS_S_ROOM },
            { SHOW(UI_VIS_RING), "Hum", "%", 0, 45, 0, 100, -1 },
            { SHOW(UI_VIS_BAR), "Bat", "%", 0, 87, 0, 100, -1 },
            { SHOW(UI_VIS_GAUGE), "PV", "kW", 1, 3.4f, 0, 7, -1 },
            { SHOW(UI_VIS_BAR), "Disk", "%", 0, 71, 0, 100, -1 },
            { SHOW(UI_VIS_RING), "Fan", "%", 0, 38, 0, 100, -1 },
            { CHART_AUTO(60, true), "Net", "MB/s", 0, 12, 0, 0, VIS_S_NET },
            { SHOW(UI_VIS_BAR), "CPU", "%", 0, VIS_NONE, 0, 100, -1 },
        };
        vis_sheet(fb, 4, 4, k, N(k), "en");
    } else if (strcmp(name, "chart_spans") == 0) { /* 10 s, 60 s, 1 h from the values, 24 h from the minutes */
        static const vis_cell_t k[] = {
            { CHART(10, true), "CPU load", "%", 0, 61, 0, 100, VIS_S_CPU },
            { CHART(60, true), "CPU load", "%", 0, 61, 0, 100, VIS_S_CPU },
            { CHART(3600, true), "CPU load", "%", 0, 61, 0, 100, VIS_S_CPU },
            { CHART(86400, true), "CPU load", "%", 0, 61, 0, 100, VIS_S_CPU },
        };
        vis_sheet(fb, 2, 2, k, N(k), "en");
    } else if (strcmp(name, "chart_variants") == 0) { /* numbers over it or not, automatic, no value */
        static const vis_cell_t k[] = {
            { CHART(60, true), "CPU temperature", "°C", 0, 72, 20, 100, VIS_S_TEMP },
            { CHART(60, false), "CPU temperature", "°C", 0, 72, 20, 100, VIS_S_TEMP },
            { CHART_AUTO(60, true), "CPU temperature", "°C", 0, 72, 20, 100, VIS_S_TEMP },
            { CHART(60, true), "CPU temperature", "°C", 0, VIS_NONE, 20, 100, VIS_S_TEMP },
        };
        vis_sheet(fb, 2, 2, k, N(k), "en");
    } else if (strcmp(name, "visuals_missing") == 0) { /* no value, and a chart without history */
        static const vis_cell_t k[] = {
            { CHART(60, true), "GPU load", "%", 0, VIS_NONE, 0, 100, -1 },
            { SHOW(UI_VIS_BAR), "RAM", "GB", 1, VIS_NONE, 0, 64, -1 },
            { SHOW(UI_VIS_RING), "GPU load", "%", 0, VIS_NONE, 0, 100, -1 },
            { SHOW(UI_VIS_GAUGE), "CPU temperature", "°C", 0, VIS_NONE, 20, 100, -1 },
        };
        vis_sheet(fb, 2, 2, k, N(k), "en");
    } else if (strcmp(name, "visuals_cs") == 0) { /* Czech: the comma */
        static const vis_cell_t k[] = {
            { CHART(7200, true), "Uvnitř", "°C", 1, 23.8f, 0, 0, VIS_S_ROOM },
            { SHOW(UI_VIS_BAR), "Paměť", "GB", 1, 21.3f, 0, 64, -1 },
            { SHOW(UI_VIS_RING), "Vlhkost", "%", 0, 45, 0, 100, -1 },
            { SHOW(UI_VIS_GAUGE), "Výkon FVE", "kW", 1, 3.4f, 0, 7.5f, -1 },
        };
        vis_sheet(fb, 2, 2, k, N(k), "cs");
    } else if (strcmp(name, "chart_gap") == 0) { /* a reconnect, and 20 min of silence in two hours */
        static const vis_cell_t k[] = {
            { CHART(60, true), "GPU load", "%", 0, 47, 0, 100, VIS_S_GAP },
            { CHART(7200, true), "CPU load", "%", 0, 33, 0, 100, VIS_S_SILENCE },
        };
        vis_sheet(fb, 2, 1, k, N(k), "en");
    } else if (strcmp(name, "visuals_tall") == 0) { /* six columns, 64×277: upright bars */
        static const vis_cell_t k[] = {
            { SHOW(UI_VIS_BAR), "RAM", "GB", 1, 21.3f, 0, 64, -1 },
            { SHOW(UI_VIS_BAR), "VRAM", "GB", 1, 6.2f, 0, 12, -1 },
            { SHOW(UI_VIS_RING), "GPU", "%", 0, 94, 0, 100, -1 },
            { SHOW(UI_VIS_GAUGE), "CPU", "°C", 0, 71, 20, 100, -1 },
            { SHOW(UI_VIS_BAR), "Disk", "%", 0, VIS_NONE, 0, 100, -1 },
            { SHOW(UI_VIS_RING), "Fan", "%", 0, 38, 0, 100, -1 },
        };
        vis_sheet(fb, 1, 6, k, N(k), "en");
    } else if (strcmp(name, "visuals_rows") == 0) { /* six rows, 398×44: wide and low */
        static const vis_cell_t k[] = {
            { CHART(60, false), "CPU load", "%", 0, 61, 0, 100, VIS_S_CPU },
            { SHOW(UI_VIS_GAUGE), "CPU temperature", "°C", 0, 71, 20, 100, -1 },
            { SHOW(UI_VIS_RING), "GPU load", "%", 0, 94, 0, 100, -1 },
            { SHOW(UI_VIS_BAR), "RAM", "GB", 1, 21.3f, 0, 64, -1 },
            { SHOW(UI_VIS_BAR), "Network", "MB/s", 1, VIS_NONE, 0, 100, -1 },
            { CHART_AUTO(60, true), "Network", "MB/s", 0, 12, 0, 0, VIS_S_NET },
        };
        vis_sheet(fb, 6, 1, k, N(k), "en");
    } else {
        return false;
    }
    return true;
}

static const char *const k_visual_fixtures[] = { "visuals_m",       "visuals_s",   "visuals_small", "chart_spans",
                                                 "chart_variants",  "visuals_missing", "visuals_cs", "chart_gap",
                                                 "visuals_tall",    "visuals_rows" };
