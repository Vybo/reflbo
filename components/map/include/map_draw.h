#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "map_data.h"
#include "map_view.h"

/*
 * Drawing the map (spec §11.1) into a screen rectangle `area`, whose top-left is the view's (0, 0).
 * Lines and dots get a 1 px white halo and labels a white box, so they stay legible over the radar's
 * rain. Pure C.
 */

#define MAP_LABELS_MAX 192 /* the flight radar reserves a box for each of up to 100 aircraft too */

/* What one map has placed so far, so later labels keep clear of it. */
typedef struct {
    gfx_rect_t r[MAP_LABELS_MAX];
    int count;
} map_labels_t;

typedef struct {
    bool airports;  /* the flight radar's map: airports with their IATA codes */
    bool halo;      /* lines get a white halo: there is rain under them */
    int max_towns;  /* labelled towns at most */
} map_style_t;

void map_labels_init(map_labels_t *l);
/* Keeps labels off a rectangle: a symbol, a caption, a panel. */
void map_labels_reserve(map_labels_t *l, gfx_rect_t r);
/* Draws `text` on a white box beside (x, y): right of it at `gap` px, else left, above or below, the
 * first place that is inside `area` and clear; false, and nothing drawn, if none is. */
bool map_label(gfx_fb_t *fb, gfx_rect_t area, map_labels_t *l, const gfx_font_t *font, int x, int y, int gap,
               const char *text);

/* Borders and coasts, clipped to `area`. */
void map_draw_lines(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const map_data_t *d, const map_style_t *s);
/* Towns as dots with labels, the largest first, and with s->airports the airports too (first, as
 * the flight radar cares more for them); a place whose label has no room is left out. */
void map_draw_places(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const map_data_t *d,
                     const map_style_t *s, map_labels_t *l);
/* Home as ⊙ at (lat_e4, lon_e4); its square is reserved. */
void map_draw_home(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, int32_t lat_e4, int32_t lon_e4,
                   map_labels_t *l);
/* Rings around the view's centre at `range_m` and half of it, labelled in km at their top right. */
void map_draw_rings(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, double range_m, map_labels_t *l);
