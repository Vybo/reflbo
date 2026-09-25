#pragma once

#include "gfx.h"

/* Draws the display test pattern (spec §4.2) into a 400x300 framebuffer: border, origin marker,
 * corner labels, glyph samples, a 10-px grid, two checkerboards and crossing diagonals. */
void gfx_draw_test_pattern(gfx_fb_t *fb);
