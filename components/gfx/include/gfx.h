#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx_font.h"

/*
 * 1-bpp drawing (spec §4.3). Framebuffer: row-major, MSB = leftmost pixel, bit 1 = black,
 * which is the PBM P4 raster layout. Pure C with no ESP-IDF headers, so it builds on the host.
 */

typedef enum {
    GFX_WHITE = 0,
    GFX_BLACK = 1,
    GFX_INVERT = 2,
} gfx_color_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} gfx_rect_t;

typedef struct {
    uint8_t *buf;
    int16_t width;
    int16_t height;
    int16_t stride;  /* bytes per row: (width + 7) / 8 */
    gfx_rect_t clip; /* drawing is limited to this rectangle */
} gfx_fb_t;

size_t gfx_fb_size(int16_t width, int16_t height);
void gfx_fb_init(gfx_fb_t *fb, uint8_t *buf, int16_t width, int16_t height);
void gfx_clear(gfx_fb_t *fb, gfx_color_t color); /* whole buffer; ignores the clip */

gfx_rect_t gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b); /* empty result has w = h = 0 */
void gfx_set_clip(gfx_fb_t *fb, gfx_rect_t clip);           /* intersected with the framebuffer */
void gfx_reset_clip(gfx_fb_t *fb);

void gfx_pixel(gfx_fb_t *fb, int x, int y, gfx_color_t color);
bool gfx_get_pixel(const gfx_fb_t *fb, int x, int y); /* true = black; false outside the buffer */
void gfx_hline(gfx_fb_t *fb, int x, int y, int w, gfx_color_t color);
void gfx_vline(gfx_fb_t *fb, int x, int y, int h, gfx_color_t color);
void gfx_line(gfx_fb_t *fb, int x0, int y0, int x1, int y1, gfx_color_t color);
void gfx_rect(gfx_fb_t *fb, gfx_rect_t r, gfx_color_t color); /* outline, each pixel drawn once */
void gfx_fill_rect(gfx_fb_t *fb, gfx_rect_t r, gfx_color_t color);

/* PBM P4 image of the framebuffer: header "P4\n<w> <h>\n" followed by the raster. */
size_t gfx_pbm_size(const gfx_fb_t *fb);
size_t gfx_pbm_encode(const gfx_fb_t *fb, uint8_t *out, size_t out_size); /* 0 if out_size is too small */


typedef enum {
    GFX_ALIGN_LEFT,
    GFX_ALIGN_CENTER,
    GFX_ALIGN_RIGHT,
} gfx_align_t;

/* Decodes one UTF-8 codepoint and advances *s. Returns 0 at the terminating NUL and U+FFFD for
 * malformed input; never reads past the NUL. */
uint32_t gfx_utf8_next(const char **s);

bool gfx_font_has_glyph(const gfx_font_t *font, uint32_t codepoint);
int gfx_text_width(const gfx_font_t *font, const char *utf8);
/* Draws one line with its baseline at `baseline`; returns the pen x after the text.
 * A codepoint missing from the font is drawn as a hollow box. */
int gfx_text(gfx_fb_t *fb, const gfx_font_t *font, int x, int baseline, const char *utf8, gfx_color_t color);
/* One line aligned in r, vertically centred on the font's line box, clipped to r. */
void gfx_text_in_rect(gfx_fb_t *fb, const gfx_font_t *font, gfx_rect_t r, gfx_align_t align, const char *utf8,
                      gfx_color_t color);
