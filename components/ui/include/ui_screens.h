#pragma once

#include "gfx.h"
#include "ui_fields.h"

/* Special screens (spec §5.5). Pure C, host-buildable. */

/* A short message over whatever is on screen, for about 3 s: "Preset: Indoor". */
void ui_draw_toast(gfx_fb_t *fb, const char *text);
/* The last screen before the battery gives out (spec §8): nothing else updates after it. */
void ui_draw_critical(gfx_fb_t *fb, const ui_context_t *ctx);
