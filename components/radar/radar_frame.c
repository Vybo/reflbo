#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "radar.h"

#define PI 3.14159265358979323846
#define VIEW_W_MAX 1024 /* screen columns a render maps at most */

static size_t grid_bytes(uint16_t w, uint16_t h)
{
    return ((size_t)w * h + 3) / 4;
}

bool radar_frame_alloc(radar_frame_t *f, uint16_t w, uint16_t h, const png_mem_t *mem)
{
    memset(f, 0, sizeof(*f));
    size_t n = grid_bytes(w, h);
    f->levels = mem != NULL && mem->alloc != NULL ? mem->alloc(n) : malloc(n);
    if (f->levels == NULL) {
        return false;
    }
    memset(f->levels, 0, n);
    f->w = w;
    f->h = h;
    return true;
}

void radar_frame_free(radar_frame_t *f, const png_mem_t *mem)
{
    if (mem != NULL && mem->release != NULL) {
        mem->release(f->levels);
    } else {
        free(f->levels);
    }
    f->levels = NULL;
}

radar_level_t radar_frame_level(const radar_frame_t *f, int x, int y)
{
    if (f->levels == NULL || x < 0 || y < 0 || x >= f->w || y >= f->h) {
        return RADAR_NONE;
    }
    size_t i = (size_t)y * f->w + x;
    return (radar_level_t)(f->levels[i / 4] >> (6 - 2 * (i % 4)) & 3);
}

void radar_frame_set(radar_frame_t *f, int x, int y, radar_level_t level)
{
    if (f->levels == NULL || x < 0 || y < 0 || x >= f->w || y >= f->h) {
        return;
    }
    size_t i = (size_t)y * f->w + x;
    int shift = 6 - 2 * (int)(i % 4);
    f->levels[i / 4] = (uint8_t)((f->levels[i / 4] & ~(3 << shift)) | (level & 3) << shift);
}

static bool inks(radar_level_t level, int x, int y)
{
    switch (level) {
    case RADAR_LIGHT:
        return (x % 2 == 0) && (y % 2 == 0); /* one pixel in four */
    case RADAR_MODERATE:
        return (x + y) % 2 == 0; /* a checkerboard */
    case RADAR_HEAVY:
        return true;
    default:
        return false;
    }
}

/* Web Mercator metres per screen pixel, and the metres of the view's pixel (0, 0)'s centre. */
static void view_metres(const map_view_t *v, double *step, double *mx, double *my)
{
    *step = 2 * PI * MAP_EARTH_R / (MAP_TILE_PX * pow(2.0, v->zoom));
    map_pixel_to_mercator(v, 0.5, 0.5, mx, my);
}

/* Where a view's pixels fall in a frame's grid, worked out once a render: each screen column's grid
 * column and each row's grid row, -1 off the frame. The S3 does doubles in software, and a pixel's
 * own would cost most of a second a frame. */
typedef struct {
    int w, h;
    int16_t cols[VIEW_W_MAX], rows[VIEW_W_MAX];
} grid_map_t;

static void grid_map(const map_view_t *v, const radar_frame_t *f, int w, int h, grid_map_t *g)
{
    double step, mx0, my0;
    view_metres(v, &step, &mx0, &my0);
    g->w = w < VIEW_W_MAX ? w : VIEW_W_MAX;
    g->h = h < VIEW_W_MAX ? h : VIEW_W_MAX;
    for (int sx = 0; sx < g->w; sx++) {
        int gx = (int)floor((mx0 + sx * step - f->mx0) / f->scale);
        g->cols[sx] = (int16_t)(gx >= 0 && gx < f->w ? gx : -1);
    }
    for (int sy = 0; sy < g->h; sy++) {
        int gy = (int)floor((f->my0 - (my0 - sy * step)) / f->scale);
        g->rows[sy] = (int16_t)(gy >= 0 && gy < f->h ? gy : -1);
    }
}

void radar_render(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const radar_frame_t *f)
{
    if (f == NULL || f->levels == NULL) {
        return;
    }
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    static grid_map_t g; /* 4 KB: off the app task's stack */
    grid_map(v, f, area.w, area.h, &g);
    for (int sy = 0; sy < g.h; sy++) {
        for (int sx = 0; g.rows[sy] >= 0 && sx < g.w; sx++) {
            int x = area.x + sx, y = area.y + sy;
            if (g.cols[sx] >= 0 && inks(radar_frame_level(f, g.cols[sx], g.rows[sy]), x, y)) {
                gfx_pixel(fb, x, y, GFX_BLACK);
            }
        }
    }
    double step, mx0, my0;
    view_metres(v, &step, &mx0, &my0);
    /* the edge of the frame's data, dotted, where it crosses the view */
    double left = (f->mx0 - mx0) / step, right = (f->mx0 + f->w * f->scale - mx0) / step;
    double top = (my0 - f->my0) / step, bottom = (my0 - (f->my0 - f->h * f->scale)) / step;
    int l = (int)lround(left), r = (int)lround(right), t = (int)lround(top), b = (int)lround(bottom);
    for (int sx = l < 0 ? 0 : l; sx <= r && sx < area.w; sx += 2) {
        if (t >= 0 && t < area.h) {
            gfx_pixel(fb, area.x + sx, area.y + t, GFX_BLACK);
        }
        if (b >= 0 && b < area.h) {
            gfx_pixel(fb, area.x + sx, area.y + b, GFX_BLACK);
        }
    }
    for (int sy = t < 0 ? 0 : t; sy <= b && sy < area.h; sy += 2) {
        if (l >= 0 && l < area.w) {
            gfx_pixel(fb, area.x + l, area.y + sy, GFX_BLACK);
        }
        if (r >= 0 && r < area.w) {
            gfx_pixel(fb, area.x + r, area.y + sy, GFX_BLACK);
        }
    }
    fb->clip = saved;
}

bool radar_any_rain(const map_view_t *v, const radar_frame_t *f)
{
    if (f == NULL || f->levels == NULL) {
        return false;
    }
    static grid_map_t g;
    grid_map(v, f, v->w, v->h, &g);
    for (int sy = 0; sy < g.h; sy++) {
        for (int sx = 0; g.rows[sy] >= 0 && sx < g.w; sx++) {
            if (g.cols[sx] >= 0 && radar_frame_level(f, g.cols[sx], g.rows[sy]) != RADAR_NONE) {
                return true;
            }
        }
    }
    return false;
}
