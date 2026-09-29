#pragma once

#include "gfx.h"
#include "ui_fields.h"
#include "ui_layout.h"
#include "ui_preset.h"

/* Shared by the ui sources; not part of the component's API. */

#define UI_BATTERY_LOW_PCT 15 /* spec §8: the status bar marks a low battery */

void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, ui_stale_policy_t policy,
                    const lang_t *lang);
void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, bool any_stale);
/* Splits text that is wider than max_w into two lines at the last space that lets the first
 * line fit; each line is then cut with an ellipsis if it is still too wide. line2 is empty when
 * the text fits on one line. */
void ui_split_two_lines(const gfx_font_t *font, const char *text, int max_w, char *line1, char *line2, size_t size);
/* A battery outline w x h with a nub, filled to `pct` (no fill if pct < 0). */
void ui_draw_battery(gfx_fb_t *fb, int x, int y, int w, int h, int pct);
/* The Moon's disc in a thin outline, its shadow inked. */
void ui_draw_moon(gfx_fb_t *fb, int cx, int cy, int r, double age);
