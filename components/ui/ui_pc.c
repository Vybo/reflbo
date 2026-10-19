#include "ui_pc.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
