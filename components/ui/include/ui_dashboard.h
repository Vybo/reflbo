#pragma once

#include "gfx.h"
#include "ui_fields.h"
#include "ui_preset.h"

/* The dashboard screen (spec §5.5): the active preset's layout under the status bar. Rendering
 * is a pure function of the context, the preset and its own titles (spec §5.4, D42; NULL: none). Pure C,
 * host-buildable. */
void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, const ui_titles_t *titles);
/* One field in a cell of the split layout, at the size the cell allows it (spec §5.2); nothing when
 * the cell has no room for it. True if what it shows is stale. */
bool ui_draw_cell(gfx_fb_t *fb, gfx_rect_t cell, const ui_context_t *ctx, ui_field_id_t field,
                  ui_stale_policy_t policy);
/* The same with the cell's own title in place of the field's label (NULL: none), and with `force`, the title
 * shown whatever the cell's size: above the field or beside it where the cell has room for both (spec §5.2, D42). */
bool ui_draw_titled_cell(gfx_fb_t *fb, gfx_rect_t cell, const ui_context_t *ctx, ui_field_id_t field,
                         ui_stale_policy_t policy, const char *title, bool force);
