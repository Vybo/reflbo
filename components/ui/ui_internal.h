#pragma once

#include "gfx.h"
#include "ui_fields.h"
#include "ui_layout.h"
#include "ui_preset.h"

/* Shared by the ui sources; not part of the component's API. */

#define UI_BATTERY_LOW_PCT 15 /* spec §8: the status bar marks a low battery */

/* "20:48" or "8:48 PM": a UTC time in local time, as the clock setting shows it (ui_forecast.c). */
void ui_clock_text(const ui_context_t *ctx, time_t t, char *out, size_t size);
/* An age as the stale mark shows it: "45 min", "3 h", "2 d" (ui_widget.c). */
void ui_format_age(const lang_t *lang, uint32_t age_s, char *out, size_t size);
/* rain.map (ui_radar.c, M6); false for any other field. */
bool ui_resolve_radar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
/* Its widget in an M or L slot; false for any other kind. */
bool ui_radar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v);

/* The weather, air quality, pollen and sun fields (ui_forecast.c, D25); false for any other field. */
bool ui_resolve_forecast(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
/* Their widgets; false for a kind they don't draw. */
bool ui_forecast_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v);
/* A sky's icon at 24 or 48 px, its night variant where it has one. */
const gfx_bitmap_t *ui_sky_icon(int sky, bool night, int size);

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
