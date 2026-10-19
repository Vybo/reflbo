#include "ui_pc.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "ui_internal.h"

/* The PC's readings as fields (spec §5.1, §12a, D44). */

#define TEXT_LIMIT 2e9 /* past this a scaled value leaves a 32-bit long (lang_format_decimal()) */

/* `v` with `decimals` in the language's form; a value too large for them drops them, and then prints whole. */
static void number_text(const lang_t *lang, float v, int decimals, char *out, size_t size)
{
    double scaled = (double)v;
    for (int i = 0; i < decimals; i++) {
        scaled *= 10.0;
    }
    if (fabs(scaled) < TEXT_LIMIT) {
        lang_format_decimal(lang, lround(scaled), decimals, out, size);
    } else if (fabs((double)v) < TEXT_LIMIT) {
        lang_format_decimal(lang, lround((double)v), 0, out, size);
    } else {
        snprintf(out, size, "%.0f", (double)v);
    }
}

bool ui_resolve_pc(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    if (!ui_field_is_pc(field)) {
        return false;
    }
    const char *id = ctx->pc_ids != NULL ? ctx->pc_ids[field - UI_FIELD_PC_FIRST] : "";
    if (id[0]) {
        out->label = id; /* a reading the catalogue doesn't name: the PC no longer sends it */
    }
    const ui_pc_t *pc = ctx->pc;
    int i = pc != NULL && pc->catalog != NULL && id[0] ? pc_catalog_find(pc->catalog, id) : -1;
    if (i < 0) {
        return true;
    }
    const pc_reading_t *r = &pc->catalog->r[i];
    out->label = r->label[0] ? r->label : id;
    snprintf(out->unit, sizeof(out->unit), "%s", r->unit);
    if (r->has_min && r->has_max && r->min < r->max) {
        ui_set_range(out, r->min, r->max);
    }
    if (!pc->session || pc->values == NULL || !pc->values->has[i]) {
        return true;
    }
    float v = pc->values->v[i];
    ui_set_num(out, v);
    number_text(ctx->lang, v, r->decimals, out->text, sizeof(out->text));
    if (r->decimals > 0) {
        number_text(ctx->lang, v, 0, out->short_text, sizeof(out->short_text));
    }
    bool fresh = pc->age_ms <= UI_PC_FRESH_INTERVALS * pc->interval_ms;
    out->state = fresh ? UI_VALUE_FRESH : UI_VALUE_STALE;
    out->age_s = pc->age_ms / 1000;
    return true;
}

void ui_pc_since_text(const ui_context_t *ctx, time_t since, char *out, size_t size)
{
    char when[24];
    ui_sync_time_text(ctx, since, false, when, sizeof(when));
    ui_fill(lang_str(ctx->lang, LS_PC_SINCE), when, out, size);
}

void ui_pc_draw_asleep(gfx_fb_t *fb, const ui_context_t *ctx)
{
    const ui_pc_t *pc = ctx->pc;
    gfx_rect_t box = { 100, 140, 200, 60 };
    gfx_fill_rect(fb, box, GFX_WHITE);
    gfx_rect(fb, box, GFX_BLACK);
    gfx_text_in_rect(fb, &gfx_font_sans_bold_28, (gfx_rect_t){ 100, 142, 200, 34 }, GFX_ALIGN_CENTER,
                     lang_str(ctx->lang, LS_PC_ASLEEP), GFX_BLACK);
    char since[32] = "", line[80];
    if (pc != NULL && pc->since != 0 && ctx->time_valid) {
        ui_pc_since_text(ctx, pc->since, since, sizeof(since));
    }
    const char *name = pc != NULL ? pc->name : "";
    snprintf(line, sizeof(line), "%s%s%s", since, since[0] && name[0] ? " · " : "", name);
    char fit[80];
    if (line[0] && gfx_text_ellipsize(&gfx_font_sans_12, line, box.w - 8, fit, sizeof(fit)) > 0) {
        gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ 100, 176, 200, 20 }, GFX_ALIGN_CENTER, fit, GFX_BLACK);
    }
}

/* ---- the sample PC ---- */

#define DEMO_N 60

static const struct {
    const char *id, *label, *unit;
    uint8_t decimals;
    float min, max, value; /* `value` for the readings without a history */
} k_demo[] = {
    { "cpu_load", "CPU load", "%", 0, 0, 100, 0 },     { "cpu_temp", "CPU", "°C", 0, 20, 100, 0 },
    { "gpu_load", "GPU load", "%", 0, 0, 100, 0 },     { "gpu_temp", "GPU", "°C", 0, 20, 100, 0 },
    { "ram_used", "RAM", "GB", 1, 0, 64, 21.3f },      { "vram_used", "VRAM", "GB", 1, 0, 12, 6.2f },
    { "net_down", "Network", "MB/s", 1, 0, 100, 12.4f },
};

/* The charted readings' last minute, as the spike made them up. */
static void demo_series(float out[4][DEMO_N])
{
    uint32_t seed = 12345;
    for (int i = 0; i < DEMO_N; i++) {
        float t = (float)i, r[4];
        for (int k = 0; k < 4; k++) {
            seed = seed * 1103515245u + 12345u;
            r[k] = (float)((seed >> 8) & 0xFFFF) / 65535.0f;
        }
        out[0][i] = 18.0f + 10.0f * sinf(t / 6.0f) + 8.0f * r[0] + (i > 40 ? 35.0f : 0.0f);
        out[1][i] = 52.0f + 6.0f * sinf(t / 9.0f) + 2.0f * r[1] + (i > 40 ? 14.0f : 0.0f);
        out[2][i] = i < 20 ? 3.0f + 2.0f * r[2] : 88.0f + 10.0f * r[2];
        out[3][i] = i < 20 ? 41.0f + r[3] : 41.0f + (t - 20.0f) * 0.6f + r[3];
    }
}

void ui_pc_demo(pc_catalog_t *cat, pc_values_t *vals)
{
    float series[4][DEMO_N];
    demo_series(series);
    memset(cat, 0, sizeof(*cat));
    memset(vals, 0, sizeof(*vals));
    for (size_t i = 0; i < sizeof(k_demo) / sizeof(k_demo[0]); i++) {
        pc_reading_t *r = &cat->r[i];
        snprintf(r->id, sizeof(r->id), "%s", k_demo[i].id);
        snprintf(r->label, sizeof(r->label), "%s", k_demo[i].label);
        snprintf(r->unit, sizeof(r->unit), "%s", k_demo[i].unit);
        r->decimals = k_demo[i].decimals;
        r->has_min = r->has_max = true;
        r->min = k_demo[i].min;
        r->max = k_demo[i].max;
        vals->has[i] = true;
        vals->v[i] = i < 4 ? series[i][DEMO_N - 1] : k_demo[i].value;
    }
    cat->count = (uint8_t)(sizeof(k_demo) / sizeof(k_demo[0]));
    vals->seq = 812;
}

void ui_pc_demo_history(ui_history_t *h, uint32_t now_ms)
{
    float series[4][DEMO_N];
    demo_series(series);
    for (int k = 0; k < 4; k++) {
        char name[UI_HIST_NAME_LEN];
        snprintf(name, sizeof(name), "pc.%s", k_demo[k].id);
        ui_series_t *s = ui_history_find(h, name);
        for (int i = 0; s != NULL && i < DEMO_N; i++) {
            ui_series_push(s, now_ms - (uint32_t)(DEMO_N - 1 - i) * 1000u, series[k][i]);
        }
    }
}
