#include "map_draw.h"

#include <math.h>
#include <stdio.h>

#include "gfx_fonts.h"

#define TOWNS_TRIED 300 /* places looked at per map, so a dense view stays quick */
#define HOME_R 5
#define PI_F 3.14159265f

/* The view's projection in single precision, with its constants worked out once: the S3's FPU
 * does floats, not doubles, and the thousands of segments a wide view holds would take most of
 * a second in software doubles. A float stays within a hundredth of a pixel here. */
typedef struct {
    float kx, ox, ky, oy;
} fast_view_t;

static fast_view_t fast_view(const map_view_t *v)
{
    double w = MAP_TILE_PX * pow(2.0, v->zoom); /* the world's width in pixels */
    return (fast_view_t){ .kx = (float)(w / 360.0), .ox = (float)(w / 2 - v->cx + v->w / 2.0),
                          .ky = (float)(w / (2 * PI_F)), .oy = (float)(w / 2 - v->cy + v->h / 2.0) };
}

/* map_project() of a point, faster and as good as a screen needs. */
static void fast_project(const fast_view_t *f, double lat, double lon, float *x, float *y)
{
    float la = (float)(lat > 85.0 ? 85.0 : lat < -85.0 ? -85.0 : lat);
    *x = (float)lon * f->kx + f->ox;
    *y = f->oy - logf(tanf(PI_F / 4 + la * (PI_F / 360))) * f->ky;
}

void map_labels_init(map_labels_t *l)
{
    l->count = 0;
}

void map_labels_reserve(map_labels_t *l, gfx_rect_t r)
{
    if (l->count < MAP_LABELS_MAX) {
        l->r[l->count++] = r;
    }
}

static bool overlaps(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static bool inside(gfx_rect_t r, gfx_rect_t area)
{
    return r.x >= area.x && r.y >= area.y && r.x + r.w <= area.x + area.w && r.y + r.h <= area.y + area.h;
}

static bool is_free(const map_labels_t *l, gfx_rect_t r)
{
    for (int i = 0; i < l->count; i++) {
        if (overlaps(l->r[i], r)) {
            return false;
        }
    }
    return true;
}

/* Text on a white box a pixel larger: on a 1-bit panel it reads better over dithered rain than
 * an outline around each glyph. */
static void boxed_text(gfx_fb_t *fb, const gfx_font_t *font, gfx_rect_t box, const char *text)
{
    gfx_fill_rect(fb, box, GFX_WHITE);
    gfx_text(fb, font, box.x + 1, box.y + 1 + font->ascent, text, GFX_BLACK);
}

bool map_label(gfx_fb_t *fb, gfx_rect_t area, map_labels_t *l, const gfx_font_t *font, int x, int y, int gap,
               const char *text)
{
    if (l->count >= MAP_LABELS_MAX) {
        return false;
    }
    int w = gfx_text_width(font, text) + 2, h = font->line_height + 2; /* the halo's pixel on each side */
    const gfx_rect_t k_spots[4] = {
        /* right and left, centred on y; above and below, clear of those two */
        { (int16_t)(x + gap), (int16_t)(y - h / 2), (int16_t)w, (int16_t)h },
        { (int16_t)(x - gap - w), (int16_t)(y - h / 2), (int16_t)w, (int16_t)h },
        { (int16_t)(x - w / 2), (int16_t)(y - h / 2 - h), (int16_t)w, (int16_t)h },
        { (int16_t)(x - w / 2), (int16_t)(y - h / 2 + h), (int16_t)w, (int16_t)h },
    };
    for (int i = 0; i < 4; i++) {
        gfx_rect_t r = k_spots[i];
        if (inside(r, area) && is_free(l, r)) {
            boxed_text(fb, font, r, text);
            l->r[l->count++] = r;
            return true;
        }
    }
    return false;
}

/* Liang-Barsky: clips the segment to [x0, x1] x [y0, y1]; false if nothing is left. */
static bool clip(float *ax, float *ay, float *bx, float *by, float x0, float y0, float x1, float y1)
{
    float dx = *bx - *ax, dy = *by - *ay, t0 = 0.0f, t1 = 1.0f;
    const float p[4] = { -dx, dx, -dy, dy };
    const float q[4] = { *ax - x0, x1 - *ax, *ay - y0, y1 - *ay };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) {
                return false;
            }
            continue;
        }
        float t = q[i] / p[i];
        if (p[i] < 0.0f) {
            if (t > t1) {
                return false;
            }
            t0 = t > t0 ? t : t0;
        } else {
            if (t < t0) {
                return false;
            }
            t1 = t < t1 ? t : t1;
        }
    }
    float sx = *ax, sy = *ay;
    *ax = sx + t0 * dx;
    *ay = sy + t0 * dy;
    *bx = sx + t1 * dx;
    *by = sy + t1 * dy;
    return true;
}

typedef struct {
    gfx_fb_t *fb;
    gfx_rect_t area;
    fast_view_t fast;
    bool halo;
    bool white; /* this pass draws the halo */
} lines_ctx_t;

static void on_segment(void *arg, map_line_kind_t kind, double lat0, double lon0, double lat1, double lon1)
{
    (void)kind; /* borders and coasts look alike: 1 px lines */
    lines_ctx_t *c = arg;
    float ax, ay, bx, by;
    fast_project(&c->fast, lat0, lon0, &ax, &ay);
    fast_project(&c->fast, lat1, lon1, &bx, &by);
    if (!clip(&ax, &ay, &bx, &by, 0, 0, c->area.w - 1, c->area.h - 1)) {
        return;
    }
    int x0 = c->area.x + (int)lroundf(ax), y0 = c->area.y + (int)lroundf(ay);
    int x1 = c->area.x + (int)lroundf(bx), y1 = c->area.y + (int)lroundf(by);
    if (c->white) {
        static const int8_t k_cross[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        for (int i = 0; i < 4; i++) {
            gfx_line(c->fb, x0 + k_cross[i][0], y0 + k_cross[i][1], x1 + k_cross[i][0], y1 + k_cross[i][1], GFX_WHITE);
        }
    } else {
        gfx_line(c->fb, x0, y0, x1, y1, GFX_BLACK);
    }
}

void map_draw_lines(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const map_data_t *d, const map_style_t *s)
{
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    map_bounds_t b;
    map_view_bounds(v, &b);
    lines_ctx_t c = { .fb = fb, .area = area, .fast = fast_view(v), .halo = s->halo };
    if (s->halo) { /* every halo first, then every line, so no halo cuts a line already drawn */
        c.white = true;
        map_data_segments(d, &b, on_segment, &c);
    }
    c.white = false;
    map_data_segments(d, &b, on_segment, &c);
    fb->clip = saved;
}

typedef struct {
    gfx_fb_t *fb;
    gfx_rect_t area;
    fast_view_t fast;
    map_labels_t *l;
    int placed, tried, max;
} places_ctx_t;

/* A dot at (x, y) with its label; both or neither. */
static bool place(places_ctx_t *c, double lat, double lon, const char *text, bool airport)
{
    float fx, fy;
    fast_project(&c->fast, lat, lon, &fx, &fy);
    int x = c->area.x + (int)lroundf(fx), y = c->area.y + (int)lroundf(fy);
    gfx_rect_t mark = airport ? (gfx_rect_t){ (int16_t)(x - 4), (int16_t)(y - 2), 9, 4 }
                              : (gfx_rect_t){ (int16_t)(x - 2), (int16_t)(y - 2), 5, 5 };
    if (!inside(mark, c->area) || !is_free(c->l, mark)) {
        return false;
    }
    if (!map_label(c->fb, c->area, c->l, &gfx_font_sans_12, x, y, mark.w / 2 + 2, text)) {
        return false;
    }
    gfx_fill_rect(c->fb, mark, GFX_WHITE); /* the halo */
    if (airport) {
        gfx_fill_rect(c->fb, (gfx_rect_t){ (int16_t)(x - 3), (int16_t)(y - 1), 7, 2 }, GFX_BLACK); /* a runway */
    } else {
        gfx_fill_rect(c->fb, (gfx_rect_t){ (int16_t)(x - 1), (int16_t)(y - 1), 3, 3 }, GFX_BLACK);
    }
    map_labels_reserve(c->l, mark);
    return true;
}

static bool on_town(void *arg, const map_town_t *t)
{
    places_ctx_t *c = arg;
    if (place(c, t->lat, t->lon, t->name, false)) {
        c->placed++;
    }
    return c->placed < c->max && ++c->tried < TOWNS_TRIED;
}

static bool on_airport(void *arg, const map_airport_t *a)
{
    places_ctx_t *c = arg;
    if (a->iata[0] != '\0') {
        place(c, a->lat, a->lon, a->iata, true);
    }
    return ++c->tried < TOWNS_TRIED;
}

void map_draw_places(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const map_data_t *d,
                     const map_style_t *s, map_labels_t *l)
{
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    map_bounds_t b;
    map_view_bounds(v, &b);
    places_ctx_t c = { .fb = fb, .area = area, .fast = fast_view(v), .l = l,
                       .max = s->max_towns > 0 ? s->max_towns : 8 };
    if (s->airports) {
        map_data_airports(d, &b, on_airport, &c);
        c.tried = 0;
    }
    map_data_towns(d, &b, on_town, &c);
    fb->clip = saved;
}

void map_draw_home(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, int32_t lat_e4, int32_t lon_e4,
                   map_labels_t *l)
{
    double fx, fy;
    map_project(v, lat_e4 / 1e4, lon_e4 / 1e4, &fx, &fy);
    int x = area.x + (int)lround(fx), y = area.y + (int)lround(fy);
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    gfx_fill_circle(fb, x, y, HOME_R + 1, GFX_WHITE);
    gfx_circle(fb, x, y, HOME_R, GFX_BLACK);
    gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x - 1), (int16_t)(y - 1), 3, 3 }, GFX_BLACK);
    fb->clip = saved;
    map_labels_reserve(l, (gfx_rect_t){ (int16_t)(x - HOME_R - 1), (int16_t)(y - HOME_R - 1), 2 * HOME_R + 3,
                                        2 * HOME_R + 3 });
}

void map_draw_rings(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, double range_m, map_labels_t *l)
{
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    int cx = area.x + v->w / 2, cy = area.y + v->h / 2;
    double px_per_m = 1.0 / map_metres_per_px(v);
    for (int i = 1; i <= 2; i++) {
        double metres = range_m * i / 2;
        int r = (int)lround(metres * px_per_m);
        gfx_circle(fb, cx, cy, r, GFX_BLACK);
        char text[12];
        snprintf(text, sizeof(text), "%d km", (int)lround(metres / 1000));
        int lx = cx + (int)lround(r * 0.7071), ly = cy - (int)lround(r * 0.7071);
        map_label(fb, area, l, &gfx_font_sans_12, lx, ly, 2, text);
    }
    fb->clip = saved;
}
