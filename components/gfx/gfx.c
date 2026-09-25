#include "gfx.h"

#include <string.h>

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

size_t gfx_fb_size(int16_t width, int16_t height)
{
    return (size_t)((width + 7) / 8) * (size_t)height;
}

void gfx_fb_init(gfx_fb_t *fb, uint8_t *buf, int16_t width, int16_t height)
{
    fb->buf = buf;
    fb->width = width;
    fb->height = height;
    fb->stride = (int16_t)((width + 7) / 8);
    gfx_reset_clip(fb);
}

void gfx_clear(gfx_fb_t *fb, gfx_color_t color)
{
    size_t size = gfx_fb_size(fb->width, fb->height);
    if (color == GFX_INVERT) {
        for (size_t i = 0; i < size; i++) {
            fb->buf[i] ^= 0xFF;
        }
        return;
    }
    memset(fb->buf, color == GFX_BLACK ? 0xFF : 0x00, size);
}

gfx_rect_t gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b)
{
    int x0 = max_int(a.x, b.x);
    int y0 = max_int(a.y, b.y);
    int x1 = min_int(a.x + a.w, b.x + b.w);
    int y1 = min_int(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) {
        return (gfx_rect_t){ (int16_t)x0, (int16_t)y0, 0, 0 };
    }
    return (gfx_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0) };
}

void gfx_set_clip(gfx_fb_t *fb, gfx_rect_t clip)
{
    fb->clip = gfx_rect_intersect(clip, (gfx_rect_t){ 0, 0, fb->width, fb->height });
}

void gfx_reset_clip(gfx_fb_t *fb)
{
    fb->clip = (gfx_rect_t){ 0, 0, fb->width, fb->height };
}

void gfx_pixel(gfx_fb_t *fb, int x, int y, gfx_color_t color)
{
    const gfx_rect_t *c = &fb->clip;
    if (x < c->x || y < c->y || x >= c->x + c->w || y >= c->y + c->h) {
        return;
    }
    uint8_t *byte = &fb->buf[y * fb->stride + (x >> 3)];
    uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    switch (color) {
    case GFX_BLACK:
        *byte |= mask;
        break;
    case GFX_WHITE:
        *byte &= (uint8_t)~mask;
        break;
    case GFX_INVERT:
        *byte ^= mask;
        break;
    }
}

bool gfx_get_pixel(const gfx_fb_t *fb, int x, int y)
{
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) {
        return false;
    }
    return (fb->buf[y * fb->stride + (x >> 3)] & (0x80u >> (x & 7))) != 0;
}

void gfx_hline(gfx_fb_t *fb, int x, int y, int w, gfx_color_t color)
{
    int x0 = max_int(x, fb->clip.x);
    int x1 = min_int(x + w, fb->clip.x + fb->clip.w);
    for (int i = x0; i < x1; i++) {
        gfx_pixel(fb, i, y, color);
    }
}

void gfx_vline(gfx_fb_t *fb, int x, int y, int h, gfx_color_t color)
{
    int y0 = max_int(y, fb->clip.y);
    int y1 = min_int(y + h, fb->clip.y + fb->clip.h);
    for (int i = y0; i < y1; i++) {
        gfx_pixel(fb, x, i, color);
    }
}

void gfx_line(gfx_fb_t *fb, int x0, int y0, int x1, int y1, gfx_color_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0; /* negative */
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(fb, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_rect(gfx_fb_t *fb, gfx_rect_t r, gfx_color_t color)
{
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    gfx_hline(fb, r.x, r.y, r.w, color);
    if (r.h > 1) {
        gfx_hline(fb, r.x, r.y + r.h - 1, r.w, color);
    }
    if (r.h > 2) {
        gfx_vline(fb, r.x, r.y + 1, r.h - 2, color);
        if (r.w > 1) {
            gfx_vline(fb, r.x + r.w - 1, r.y + 1, r.h - 2, color);
        }
    }
}

void gfx_fill_rect(gfx_fb_t *fb, gfx_rect_t r, gfx_color_t color)
{
    gfx_rect_t area = gfx_rect_intersect(r, fb->clip);
    for (int y = area.y; y < area.y + area.h; y++) {
        gfx_hline(fb, area.x, y, area.w, color);
    }
}
