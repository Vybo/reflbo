#include "ui_dashboard.h"

#include "gfx_fonts.h"
#include "ui_history.h"
#include "ui_internal.h"
#include "ui_radar.h"
#include "ui_solar.h"
#include "ui_split.h"

static void draw_separators(gfx_fb_t *fb, ui_layout_id_t layout)
{
    switch (layout) {
    case UI_LAYOUT_CLASSIC:
        gfx_hline(fb, 12, 188, 376, GFX_BLACK);
        for (int i = 1; i < 4; i++) {
            gfx_vline(fb, i * 100, 199, 90, GFX_BLACK);
        }
        break;
    case UI_LAYOUT_WEATHER:
        gfx_vline(fb, 200, 29, 144, GFX_BLACK);
        gfx_hline(fb, 208, 101, 184, GFX_BLACK);
        gfx_hline(fb, 12, 181, 376, GFX_BLACK);
        gfx_vline(fb, 200, 190, 102, GFX_BLACK);
        break;
    case UI_LAYOUT_GRID:
        gfx_vline(fb, 133, 29, 263, GFX_BLACK);
        gfx_vline(fb, 267, 29, 263, GFX_BLACK);
        gfx_hline(fb, 8, 160, 384, GFX_BLACK);
        break;
    case UI_LAYOUT_FOCUS:
        gfx_hline(fb, 12, 211, 376, GFX_BLACK);
        gfx_vline(fb, 200, 220, 72, GFX_BLACK);
        break;
    default:
        break;
    }
}

/* A slot's or cell's visual (spec §5.3, M7, D44), where its field is a number and the visual fits; the stale policy
 * as the widgets take it. True if it is the visual's place, drawn or hidden. */
static bool draw_visual(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx, const ui_value_t *v,
                        const ui_visual_t *vis, ui_stale_policy_t policy)
{
    if (vis == NULL || vis->as == UI_VIS_VALUE || !ui_field_numeric(v->field) ||
        !ui_visual_fits((ui_vis_as_t)vis->as, r.w, r.h)) {
        return false;
    }
    if (ui_widget_hidden(v, policy)) {
        return true;
    }
    ui_value_t shown = *v;
    if (shown.state == UI_VALUE_STALE && policy != UI_STALE_STALE) {
        shown.state = UI_VALUE_MISSING;
    }
    const ui_series_t *series = NULL;
    char name[UI_HIST_NAME_LEN];
    if (vis->as == UI_VIS_CHART && ctx->history != NULL &&
        ui_field_name(ctx->pc_ids, v->field, name, sizeof(name)) > 0) {
        series = ui_history_find(ctx->history, name);
    }
    ui_visual_draw(fb, r, &shown, vis, series, ctx->now_ms, ctx->lang);
    return true;
}

bool ui_draw_cell(gfx_fb_t *fb, gfx_rect_t cell, const ui_context_t *ctx, ui_field_id_t field,
                  ui_stale_policy_t policy)
{
    return ui_draw_titled_cell(fb, cell, ctx, field, policy, NULL, false);
}

/* Draws `text` in `f` with its baseline at y, cut to max_w, clipped to the cell. */
static void title_text(gfx_fb_t *fb, gfx_rect_t cell, const gfx_font_t *f, int x, int y, const char *text)
{
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, cell));
    gfx_text(fb, f, x, y, text, GFX_BLACK);
    fb->clip = saved;
}

/* A forced title where the field shows no label (spec §5.2, D42): a line above the field where the rest of the cell
 * still fits it, else beside it, cut to half the cell. Draws the title and returns the field's part with its size in
 * *size; the whole cell, with nothing drawn, where neither has room. */
static gfx_rect_t place_title(gfx_fb_t *fb, gfx_rect_t cell, const ui_value_t *v, ui_stale_policy_t policy,
                              const lang_t *lang, int *size)
{
    char fit[48];
    const gfx_font_t *lf = &gfx_font_sans_12; /* the M size's label */
    int line = 6 + lf->line_height;
    gfx_rect_t below = { cell.x, (int16_t)(cell.y + line), cell.w, (int16_t)(cell.h - line) };
    int s = below.h > 0 ? ui_split_field_size(v->kind, below.w, below.h) : -1;
    if (s >= 0 && !ui_widget_labelled(fb, below, (ui_size_t)s, v, policy, lang)) {
        gfx_text_ellipsize(lf, v->label, cell.w - 12, fit, sizeof(fit));
        title_text(fb, cell, lf, cell.x + 6, cell.y + 6 + lf->ascent, fit);
        *size = s;
        return below;
    }
    const gfx_font_t *bf = gfx_font_sans_16.line_height <= cell.h ? &gfx_font_sans_16 : lf;
    int max_w = cell.w / 2 - 12;
    if (max_w > 0 && gfx_text_ellipsize(bf, v->label, max_w, fit, sizeof(fit)) > 0) {
        int column = 6 + gfx_text_width(bf, fit) + 6;
        gfx_rect_t beside = { (int16_t)(cell.x + column), cell.y, (int16_t)(cell.w - column), cell.h };
        s = ui_split_field_size(v->kind, beside.w, beside.h);
        if (s >= 0 && !ui_widget_labelled(fb, beside, (ui_size_t)s, v, policy, lang)) {
            title_text(fb, cell, bf, cell.x + 6, cell.y + (cell.h + ui_ink_above(bf, fit) - ui_ink_below(bf, fit)) / 2,
                       fit);
            *size = s;
            return beside;
        }
    }
    return cell;
}

bool ui_draw_titled_cell(gfx_fb_t *fb, gfx_rect_t cell, const ui_context_t *ctx, ui_field_id_t field,
                         ui_stale_policy_t policy, const char *title, bool force)
{
    const ui_field_info_t *info = ui_field_info(field);
    int size = info != NULL ? ui_split_field_size(info->kind, cell.w, cell.h) : -1;
    if (size < 0) {
        return false;
    }
    ui_value_t v;
    ui_resolve(ctx, field, &v);
    if (title != NULL && title[0]) {
        v.label = title;
    }
    gfx_rect_t body = cell;
    if (force && v.label != NULL && v.label[0] && !ui_widget_hidden(&v, policy) &&
        !ui_widget_labelled(fb, cell, (ui_size_t)size, &v, policy, ctx->lang)) {
        body = place_title(fb, cell, &v, policy, ctx->lang, &size);
    }
    ui_widget_draw(fb, body, (ui_size_t)size, &v, policy, ctx->lang);
    return v.state == UI_VALUE_STALE;
}

/* The split layout (spec §5.2): each split's separator unless it is hidden, then each cell's field.
 * Returns whether any value shown is stale. */
static bool draw_split(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, const ui_titles_t *titles)
{
    ui_split_geometry_t g;
    if (!ui_split_layout(preset->split, ui_split_area(), &g)) {
        return false; /* presets.json checks its trees: nothing to draw for one cut short */
    }
    for (int i = 0; i < g.lines; i++) {
        const ui_split_line_t *l = &g.line[i];
        if (l->node & UI_SPLIT_NO_LINE) {
            continue;
        }
        if (l->node & UI_SPLIT_COLUMNS) {
            gfx_vline(fb, l->at, l->rect.y + UI_SPLIT_INSET, l->rect.h - 2 * UI_SPLIT_INSET, GFX_BLACK);
        } else {
            gfx_hline(fb, l->rect.x + UI_SPLIT_INSET, l->at, l->rect.w - 2 * UI_SPLIT_INSET, GFX_BLACK);
        }
    }
    bool any_stale = false;
    for (int i = 0; i < g.cells; i++) {
        const ui_visual_t *vis = titles != NULL ? titles->visual[i] : NULL;
        if (vis != NULL) { /* M7: the cell's visual, where it fits */
            ui_value_t v;
            ui_resolve(ctx, (ui_field_id_t)preset->slots[i], &v);
            if (titles->text[i] != NULL && titles->text[i][0]) {
                v.label = titles->text[i];
            }
            if (draw_visual(fb, g.cell[i], ctx, &v, vis, (ui_stale_policy_t)preset->stale_policy)) {
                any_stale |= v.state == UI_VALUE_STALE;
                continue;
            }
        }
        any_stale |= ui_draw_titled_cell(fb, g.cell[i], ctx, (ui_field_id_t)preset->slots[i],
                                         (ui_stale_policy_t)preset->stale_policy, titles ? titles->text[i] : NULL,
                                         preset->force_title & (1u << i));
    }
    return any_stale;
}

void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, const ui_titles_t *titles)
{
    ui_context_t c = *ctx;
    if (preset->clock != UI_CLOCK_DEFAULT) {
        c.clock_24h = preset->clock == UI_CLOCK_24H;
    }
    c.seconds = preset->seconds;

    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    const ui_layout_t *layout = ui_layout((ui_layout_id_t)preset->layout);
    bool any_stale = false;
    gfx_rect_t below = { 0, UI_STATUS_H + 1, fb->width, (int16_t)(fb->height - UI_STATUS_H - 1) };
    if (preset->layout == UI_LAYOUT_RADAR) { /* spec §5.2: the map under the status bar */
        ui_draw_radar_view(fb, below, &c);
    } else if (preset->layout == UI_LAYOUT_FLIGHTS) {
        ui_draw_flights_view(fb, below, &c);
    } else if (preset->layout == UI_LAYOUT_SPLIT) {
        any_stale = draw_split(fb, &c, preset, titles);
    } else if (preset->layout == UI_LAYOUT_SOLAR) { /* M6d */
        any_stale = ui_draw_solar_layout(fb, below, &c);
    } else if (preset->layout == UI_LAYOUT_ENERGY) {
        any_stale = ui_draw_energy_layout(fb, below, &c);
    } else if (layout != NULL) {
        draw_separators(fb, (ui_layout_id_t)preset->layout);
        for (int i = 0; i < layout->slot_count; i++) {
            ui_value_t v;
            ui_resolve(&c, (ui_field_id_t)preset->slots[i], &v);
            if (titles != NULL && titles->text[i] != NULL && titles->text[i][0]) {
                v.label = titles->text[i]; /* the slot's own title (spec §5.2, D42) */
            }
            any_stale |= v.state == UI_VALUE_STALE;
            if (!draw_visual(fb, layout->slots[i].rect, &c, &v, titles != NULL ? titles->visual[i] : NULL,
                             (ui_stale_policy_t)preset->stale_policy)) {
                ui_widget_draw(fb, layout->slots[i].rect, layout->slots[i].size, &v,
                               (ui_stale_policy_t)preset->stale_policy, c.lang);
            }
        }
    }
    ui_status_draw(fb, &c, preset, any_stale);
    if (preset->invert) {
        gfx_clear(fb, GFX_INVERT);
    }
}
