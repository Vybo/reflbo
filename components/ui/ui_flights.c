#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "map_draw.h"
#include "ui_internal.h"
#include "ui_radar.h"

/* The flight radar's view (spec §11.3): the map at its centre and range with two rings, the
 * aircraft as arrows turned to their track, and a panel for the nearest one. */

#define PI 3.14159265358979323846
#define PANEL_H 40
#define KMH_PER_KT 1.852
#define ARROW_BOX 14 /* a 12 px arrow and its halo */
#define NEAREST_R 10 /* the ring that marks the panel's aircraft */

static const lang_str_t k_dirs[8] = { LS_DIR_N, LS_DIR_NE, LS_DIR_E, LS_DIR_SE,
                                      LS_DIR_S, LS_DIR_SW, LS_DIR_W, LS_DIR_NW };

/* The arrow pointing north around (0, 0): its tip, left rear, notch and right rear. */
static const int8_t k_arrow[4][2] = { { 0, -6 }, { -5, 5 }, { 0, 2 }, { 5, 5 } };

void ui_flight_altitude(int32_t alt_ft, char *out, size_t size)
{
    if (alt_ft == ADSB_ALT_GROUND) {
        snprintf(out, size, "GND");
    } else if (alt_ft == ADSB_ALT_UNKNOWN) {
        snprintf(out, size, "%s", "");
    } else if (alt_ft >= 10000) {
        snprintf(out, size, "FL%03ld", ((long)alt_ft + 50) / 100);
    } else {
        snprintf(out, size, "%ld ft", (long)alt_ft);
    }
}

/* Aircraft from a poll at most UI_FLIGHTS_OLD_S old; a clock set back counts as recent. */
static bool fresh(const ui_context_t *ctx, const ui_radar_t *r)
{
    return r->fl_always && r->aircraft != NULL && r->fl_updated != 0 &&
           ctx->now - r->fl_updated <= UI_FLIGHTS_OLD_S;
}

/* Its callsign, or its address when it sends none. */
static void name_of(const adsb_aircraft_t *a, char *out, size_t size)
{
    if (a->callsign[0] != '\0') {
        snprintf(out, size, "%s", a->callsign);
        return;
    }
    size_t n = 0;
    for (; a->hex[n] != '\0' && n + 1 < size; n++) {
        char c = a->hex[n];
        out[n] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    out[n] = '\0';
}

static void cat(char *out, size_t size, const char *part)
{
    size_t n = strlen(out);
    if (n + 1 < size) {
        snprintf(out + n, size - n, "%s", part);
    }
}

static void airport_text(const adsb_airport_t *ap, char *out, size_t size)
{
    const char *code = ap->iata[0] != '\0' ? ap->iata : ap->icao;
    snprintf(out, size, "%s%s%s", code, code[0] != '\0' && ap->place[0] != '\0' ? " " : "", ap->place);
}

void ui_flights_panel_text(const ui_context_t *ctx, char *line1, char *line2, char *credit, size_t size)
{
    const ui_radar_t *r = ctx->radar;
    line1[0] = line2[0] = '\0';
    snprintf(credit, size, "adsb.fi");
    if (r == NULL || !r->fl_always) {
        snprintf(line1, size, "%s", lang_str(ctx->lang, LS_FLIGHTS_NEED_ALWAYS));
        return;
    }
    if (!fresh(ctx, r)) {
        snprintf(line1, size, "%s", lang_str(ctx->lang, LS_NO_AIRCRAFT_DATA));
        if (r->fl_updated != 0) { /* "(20:40)": when the last good poll was */
            char at[16], when[24];
            ui_clock_text(ctx, r->fl_updated, at, sizeof(at));
            snprintf(when, sizeof(when), " (%s)", at);
            cat(line1, size, when);
        }
        return;
    }
    if (r->aircraft->count == 0) {
        char km[16];
        snprintf(km, sizeof(km), "%d km", r->fl_range_km);
        ui_fill(lang_str(ctx->lang, LS_NO_AIRCRAFT), km, line1, size);
        return;
    }
    const adsb_aircraft_t *a = &r->aircraft->ac[0];
    char part[48];
    name_of(a, line1, size);
    if (a->type[0] != '\0') {
        snprintf(part, sizeof(part), " \xC2\xB7 %s", a->type);
        cat(line1, size, part);
    }
    char alt[16];
    ui_flight_altitude(a->alt_ft, alt, sizeof(alt));
    if (alt[0] != '\0') {
        snprintf(part, sizeof(part), " \xC2\xB7 %s", alt);
        cat(line1, size, part);
    }
    if (a->speed_kt >= 0) {
        snprintf(part, sizeof(part), " \xC2\xB7 %ld km/h", lround(a->speed_kt * KMH_PER_KT));
        cat(line1, size, part);
    }
    snprintf(line2, size, "%lu km %s", (unsigned long)((a->dist_m + 500) / 1000),
             lang_str(ctx->lang, k_dirs[(a->bearing + 22) / 45 % 8]));
    const adsb_route_t *route = r->route;
    if (route != NULL && route->known && a->callsign[0] != '\0' && strcmp(route->callsign, a->callsign) == 0) {
        char from[40], to[40], leg[96];
        airport_text(&route->from, from, sizeof(from));
        airport_text(&route->to, to, sizeof(to));
        snprintf(leg, sizeof(leg), " \xC2\xB7 %s \xE2\x86\x92 %s", from, to);
        cat(line2, size, leg);
        snprintf(credit, size, "adsb.fi \xC2\xB7 adsb.lol"); /* D27: the route's source too */
    }
}

/* An arrow turned to the nearest of 16 headings; a dot when the track is unknown. */
static void arrow_at(gfx_fb_t *fb, int cx, int cy, int track, gfx_color_t color)
{
    if (track < 0) {
        gfx_fill_circle(fb, cx, cy, 3, color);
        return;
    }
    int step = (track * 2 + 22) / 45 % 16; /* track / 22.5, rounded */
    double a = step * 22.5 * PI / 180, c = cos(a), s = sin(a);
    int x[4], y[4];
    for (int i = 0; i < 4; i++) { /* clockwise on screen, where y grows downwards */
        x[i] = cx + (int)lround(k_arrow[i][0] * c - k_arrow[i][1] * s);
        y[i] = cy + (int)lround(k_arrow[i][0] * s + k_arrow[i][1] * c);
    }
    gfx_fill_triangle(fb, x[0], y[0], x[1], y[1], x[2], y[2], color);
    gfx_fill_triangle(fb, x[0], y[0], x[2], y[2], x[3], y[3], color);
}

static void arrow(gfx_fb_t *fb, int cx, int cy, int track)
{
    for (int dy = -1; dy <= 1; dy++) { /* a white halo, so it reads over a border or a ring */
        for (int dx = -1; dx <= 1; dx++) {
            if (dx != 0 || dy != 0) {
                arrow_at(fb, cx + dx, cy + dy, track, GFX_WHITE);
            }
        }
    }
    arrow_at(fb, cx, cy, track, GFX_BLACK);
}

static void draw_aircraft(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const adsb_list_t *list,
                          map_labels_t *labels)
{
    static int x[ADSB_MAX], y[ADSB_MAX]; /* off the stack, as the labels: only the app task draws */
    for (int i = 0; i < list->count; i++) {
        double px, py;
        map_project(v, list->ac[i].lat, list->ac[i].lon, &px, &py);
        x[i] = area.x + (int)lround(px);
        y[i] = area.y + (int)lround(py);
        map_labels_reserve(labels, (gfx_rect_t){ (int16_t)(x[i] - ARROW_BOX / 2), (int16_t)(y[i] - ARROW_BOX / 2),
                                                 ARROW_BOX, ARROW_BOX });
    }
    for (int i = 0; i < list->count; i++) { /* the nearest first; a label without room is left out */
        char name[12], alt[16], label[32];
        name_of(&list->ac[i], name, sizeof(name));
        ui_flight_altitude(list->ac[i].alt_ft, alt, sizeof(alt));
        snprintf(label, sizeof(label), "%s%s%s", name, alt[0] != '\0' ? " " : "", alt);
        map_label(fb, area, labels, &gfx_font_sans_12, x[i], y[i], ARROW_BOX / 2 + 2, label);
    }
    for (int i = list->count - 1; i >= 0; i--) { /* the nearest on top */
        arrow(fb, x[i], y[i], list->ac[i].track);
    }
    if (list->count > 0) {
        gfx_circle(fb, x[0], y[0], NEAREST_R + 1, GFX_WHITE);
        gfx_circle(fb, x[0], y[0], NEAREST_R, GFX_BLACK);
    }
}

void ui_draw_flights_view(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx)
{
    ui_radar_t at_home = { .home_lat_e4 = ctx->lat_e4, .home_lon_e4 = ctx->lon_e4, .fl_lat_e4 = ctx->lat_e4,
                           .fl_lon_e4 = ctx->lon_e4, .fl_range_km = 50 };
    const ui_radar_t *rad = ctx->radar != NULL ? ctx->radar : &at_home; /* no radar at all: an empty map */
    gfx_rect_t area = { r.x, r.y, r.w, UI_FLIGHTS_MAP_H };
    map_view_t v;
    double range_m = rad->fl_range_km * 1000.0;
    map_view_init(&v, rad->fl_lat_e4, rad->fl_lon_e4, map_zoom_for_range(rad->fl_lat_e4, range_m, area.h / 2), area.w,
                  area.h);
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    map_style_t style = { .airports = true, .halo = false, .max_towns = 8 };
    if (rad->map != NULL) {
        map_draw_lines(fb, area, &v, rad->map, &style);
    }
    static map_labels_t labels; /* 1.5 KB, off the stack: only the app task draws (spec §3.2) */
    map_labels_init(&labels);
    map_draw_rings(fb, area, &v, range_m, &labels);
    map_draw_home(fb, area, &v, rad->home_lat_e4, rad->home_lon_e4, &labels);
    if (fresh(ctx, rad)) {
        draw_aircraft(fb, area, &v, rad->aircraft, &labels);
    }
    if (rad->map != NULL) {
        map_draw_places(fb, area, &v, rad->map, &style, &labels);
    }
    fb->clip = saved;

    gfx_rect_t panel = { r.x, (int16_t)(r.y + r.h - PANEL_H), r.w, PANEL_H };
    gfx_hline(fb, r.x, panel.y - 1, r.w, GFX_BLACK);
    char line1[96], line2[96], credit[32], fit[96];
    ui_flights_panel_text(ctx, line1, line2, credit, sizeof(line1));
    const gfx_font_t *f1 = &gfx_font_sans_bold_16, *f2 = &gfx_font_sans_12;
    gfx_text_ellipsize(f1, line1, panel.w - 12, fit, sizeof(fit));
    gfx_text(fb, f1, panel.x + 6, panel.y + 2 + f1->ascent, fit, GFX_BLACK);
    int credit_w = gfx_text_width(f2, credit);
    int base2 = panel.y + panel.h - 4 - (f2->line_height - f2->ascent);
    gfx_text(fb, f2, panel.x + panel.w - 6 - credit_w, base2, credit, GFX_BLACK);
    gfx_text_ellipsize(f2, line2, panel.w - 12 - credit_w - 10, fit, sizeof(fit));
    gfx_text(fb, f2, panel.x + 6, base2, fit, GFX_BLACK);
}
