#pragma once

#include "gfx.h"
#include "ui_fields.h"
#include "ui_preset.h"

/* The dashboard screen (spec §5.5): the active preset's layout under the status bar. Rendering
 * is a pure function of the context and the preset. Pure C, host-buildable. */
void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset);
