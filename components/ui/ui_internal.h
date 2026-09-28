#pragma once

#include "gfx.h"
#include "ui_fields.h"
#include "ui_layout.h"
#include "ui_preset.h"

/* Shared by the ui sources; not part of the component's API. */

void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, ui_stale_policy_t policy,
                    const lang_t *lang);
void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, bool any_stale);
/* A battery outline w x h with a nub, filled to `pct` (no fill if pct < 0). */
void ui_draw_battery(gfx_fb_t *fb, int x, int y, int w, int h, int pct);
/* The Moon's disc in a thin outline, its shadow inked. */
void ui_draw_moon(gfx_fb_t *fb, int cx, int cy, int r, double age);
