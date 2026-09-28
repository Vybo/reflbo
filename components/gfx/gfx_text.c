#include "gfx.h"

#include <string.h>

uint32_t gfx_utf8_next(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = p[0];
    int extra = 0;    /* initialised for GCC's -Wmaybe-uninitialized; every path below sets them */
    uint32_t min = 0;

    if (c == 0) {
        return 0;
    }
    if (c < 0x80) {
        *s += 1;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        extra = 1;
        c &= 0x1F;
        min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        c &= 0x0F;
        min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        c &= 0x07;
        min = 0x10000;
    } else {
        *s += 1;
        return 0xFFFD;
    }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) { /* also stops at the NUL of a truncated sequence */
            *s += i;
            return 0xFFFD;
        }
        c = (c << 6) | (p[i] & 0x3Fu);
    }
    *s += extra + 1;
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        return 0xFFFD;
    }
    return c;
}

static const gfx_glyph_t *find_glyph(const gfx_font_t *font, uint32_t codepoint)
{
    int lo = 0;
    int hi = (int)font->glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t c = font->glyphs[mid].codepoint;
        if (c == codepoint) {
            return &font->glyphs[mid];
        }
        if (c < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

static void missing_box(const gfx_font_t *font, int *w, int *h)
{
    *w = font->line_height / 2 < 2 ? 2 : font->line_height / 2;
    *h = font->ascent * 2 / 3 < 2 ? 2 : font->ascent * 2 / 3;
}

bool gfx_font_has_glyph(const gfx_font_t *font, uint32_t codepoint)
{
    return find_glyph(font, codepoint) != NULL;
}

const gfx_glyph_t *gfx_font_glyph(const gfx_font_t *font, uint32_t codepoint)
{
    return find_glyph(font, codepoint);
}

int gfx_text_width(const gfx_font_t *font, const char *utf8)
{
    if (utf8 == NULL) {
        return 0;
    }
    int width = 0;
    uint32_t cp;
    while ((cp = gfx_utf8_next(&utf8)) != 0) {
        const gfx_glyph_t *g = find_glyph(font, cp);
        if (g != NULL) {
            width += g->advance;
        } else {
            int w, h;
            missing_box(font, &w, &h);
            width += w + 2;
        }
    }
    return width;
}

static void draw_glyph(gfx_fb_t *fb, const gfx_font_t *font, const gfx_glyph_t *g, int x, int baseline,
                       gfx_color_t color)
{
    int row_bytes = (g->width + 7) / 8;
    const uint8_t *rows = font->bitmap + g->offset;
    for (int r = 0; r < g->height; r++) {
        for (int c = 0; c < g->width; c++) {
            if (rows[r * row_bytes + (c >> 3)] & (0x80u >> (c & 7))) {
                gfx_pixel(fb, x + g->x_offset + c, baseline + g->y_offset + r, color);
            }
        }
    }
}

/* The fallback box, drawn with int coordinates: a gfx_rect_t would wrap at x > 32767. */
static void draw_missing_box(gfx_fb_t *fb, int x, int baseline, int w, int h, gfx_color_t color)
{
    int top = baseline - h;
    gfx_hline(fb, x, top, w, color);
    gfx_hline(fb, x, baseline - 1, w, color);
    gfx_vline(fb, x, top + 1, h - 2, color);
    gfx_vline(fb, x + w - 1, top + 1, h - 2, color);
}

int gfx_text(gfx_fb_t *fb, const gfx_font_t *font, int x, int baseline, const char *utf8, gfx_color_t color)
{
    if (utf8 == NULL) {
        return x;
    }
    uint32_t cp;
    while ((cp = gfx_utf8_next(&utf8)) != 0) {
        const gfx_glyph_t *g = find_glyph(font, cp);
        if (g != NULL) {
            draw_glyph(fb, font, g, x, baseline, color);
            x += g->advance;
        } else {
            int w, h;
            missing_box(font, &w, &h);
            draw_missing_box(fb, x + 1, baseline, w, h, color);
            x += w + 2;
        }
    }
    return x;
}

void gfx_text_in_rect(gfx_fb_t *fb, const gfx_font_t *font, gfx_rect_t r, gfx_align_t align, const char *utf8,
                      gfx_color_t color)
{
    if (utf8 == NULL) {
        return;
    }
    gfx_rect_t saved = fb->clip;
    fb->clip = gfx_rect_intersect(saved, r);

    int x = r.x;
    if (align != GFX_ALIGN_LEFT) {
        int width = gfx_text_width(font, utf8);
        x = align == GFX_ALIGN_CENTER ? r.x + (r.w - width) / 2 : r.x + r.w - width;
    }
    int baseline = r.y + (r.h - font->line_height) / 2 + font->ascent;
    gfx_text(fb, font, x, baseline, utf8, color);

    fb->clip = saved;
}

int gfx_text_ellipsize(const gfx_font_t *font, const char *utf8, int max_width, char *out, size_t out_size)
{
    if (out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (utf8 == NULL) {
        return 0;
    }
    const char *dots = gfx_font_has_glyph(font, 0x2026) ? "\xE2\x80\xA6" : "...";
    int full = gfx_text_width(font, utf8);
    int dots_w = gfx_text_width(font, dots);
    int budget = full <= max_width ? full : max_width - dots_w;
    const char *s = utf8, *end = utf8;
    int width = 0;
    for (;;) {
        const char *before = s;
        uint32_t cp = gfx_utf8_next(&s);
        if (cp == 0) {
            break;
        }
        char one[5] = { 0 };
        memcpy(one, before, (size_t)(s - before));
        int w = gfx_text_width(font, one);
        if (width + w > budget || (size_t)(s - utf8) + (full <= max_width ? 0 : strlen(dots)) >= out_size) {
            break;
        }
        width += w;
        end = s;
    }
    size_t n = (size_t)(end - utf8);
    memcpy(out, utf8, n);
    out[n] = '\0';
    if (*end != '\0') {
        size_t room = out_size - 1 - n;
        size_t d = strlen(dots);
        if (d <= room) {
            memcpy(out + n, dots, d + 1);
            width += dots_w;
        }
    }
    return width;
}
