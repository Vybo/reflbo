#include "ui_dashboard.h"

#include "ui_internal.h"
#include "ui_radar.h"

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

void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset)
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
    if (preset->layout == UI_LAYOUT_RADAR) { /* spec §5.2: the map under the status bar */
        ui_draw_radar_view(fb, (gfx_rect_t){ 0, UI_STATUS_H + 1, fb->width, (int16_t)(fb->height - UI_STATUS_H - 1) },
                           &c);
    } else if (layout != NULL) {
        draw_separators(fb, (ui_layout_id_t)preset->layout);
        for (int i = 0; i < layout->slot_count; i++) {
            ui_value_t v;
            ui_resolve(&c, (ui_field_id_t)preset->slots[i], &v);
            any_stale |= v.state == UI_VALUE_STALE;
            ui_widget_draw(fb, layout->slots[i].rect, layout->slots[i].size, &v,
                           (ui_stale_policy_t)preset->stale_policy, c.lang);
        }
    }
    ui_status_draw(fb, &c, preset, any_stale);
    if (preset->invert) {
        gfx_clear(fb, GFX_INVERT);
    }
}
