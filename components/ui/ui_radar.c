#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "map_draw.h"
#include "ui_internal.h"
#include "ui_radar.h"

/* The weather radar's map (spec §11.2): the rain under the map, then the frame's time and the
 * legend, or the loop's progress, on boxes along the bottom. */

#define PAD 3

static bool frame_old(time_t now, const radar_frame_t *f)
{
    return now > (time_t)f->time && now - (time_t)f->time > UI_RADAR_OLD_S;
}

/* The pattern's "%s" replaced by `value`, as the packs' LS_AGO has it ("%s ago", "před %s"). */
static void fill(const char *pattern, const char *value, char *out, size_t size)
{
    const char *at = strstr(pattern, "%s");
    if (at == NULL) {
        snprintf(out, size, "%s", pattern);
        return;
    }
    snprintf(out, size, "%.*s%s%s", (int)(at - pattern), pattern, value, at + 2);
}

/* "20:40 · ČHMÚ", or once the frame is old "17:40 · 3 h ago"; `brief`: the time alone. */
static void caption_text(const ui_context_t *ctx, const radar_frame_t *f, bool brief, char *out, size_t size)
{
    char at[16];
    ui_clock_text(ctx, (time_t)f->time, at, sizeof(at));
    if (brief) {
        snprintf(out, size, "%s", at);
    } else if (frame_old(ctx->now, f)) {
        char age[16], ago[32];
        ui_format_age(ctx->lang, (uint32_t)(ctx->now - (time_t)f->time), age, sizeof(age));
        fill(lang_str(ctx->lang, LS_AGO), age, ago, sizeof(ago));
        snprintf(out, size, "%s \xC2\xB7 %s", at, ago);
    } else {
        snprintf(out, size, "%s \xC2\xB7 %s", at, f->source == RADAR_SOURCE_CHMU ? "\xC4\x8CHM\xC3\x9A" : "RainViewer");
    }
}

static gfx_rect_t bottom_box(gfx_rect_t r, int w, int h, bool right)
{
    return (gfx_rect_t){ (int16_t)(right ? r.x + r.w - w : r.x), (int16_t)(r.y + r.h - h), (int16_t)w, (int16_t)h };
}

static void boxed(gfx_fb_t *fb, gfx_rect_t box, const gfx_font_t *f, const char *text, bool inverted)
{
    gfx_fill_rect(fb, box, inverted ? GFX_BLACK : GFX_WHITE);
    gfx_text(fb, f, box.x + PAD, box.y + PAD + f->ascent, text, inverted ? GFX_WHITE : GFX_BLACK);
}

/* The three levels as swatches with their words. */
static const lang_str_t k_level_words[3] = { LS_RAIN_LIGHT, LS_RAIN_MODERATE, LS_RAIN_HEAVY };
#define SWATCH_W 12
#define SWATCH_H 9

static int legend_w(const ui_context_t *ctx, const gfx_font_t *f)
{
    int w = PAD;
    for (int i = 0; i < 3; i++) {
        w += SWATCH_W + 3 + gfx_text_width(f, lang_str(ctx->lang, k_level_words[i])) + (i < 2 ? 8 : PAD);
    }
    return w;
}

static void legend(gfx_fb_t *fb, gfx_rect_t box, const ui_context_t *ctx, const gfx_font_t *f)
{
    gfx_fill_rect(fb, box, GFX_WHITE);
    int x = box.x + PAD, base = box.y + PAD + f->ascent;
    for (int i = 0; i < 3; i++) {
        gfx_rect_t sw = { (int16_t)x, (int16_t)(base - SWATCH_H + 1), SWATCH_W, SWATCH_H };
        gfx_rect(fb, sw, GFX_BLACK);
        for (int y = sw.y + 1; y < sw.y + sw.h - 1; y++) {
            for (int px = sw.x + 1; px < sw.x + sw.w - 1; px++) {
                if (radar_inks((radar_level_t)(RADAR_LIGHT + i), px, y)) {
                    gfx_pixel(fb, px, y, GFX_BLACK);
                }
            }
        }
        x = gfx_text(fb, f, x + SWATCH_W + 3, base, lang_str(ctx->lang, k_level_words[i]), GFX_BLACK) + 8;
    }
}

/* The loop's progress: a dot a frame, the one shown filled. */
static void loop_dots(gfx_fb_t *fb, gfx_rect_t box, const ui_radar_t *rad)
{
    gfx_fill_rect(fb, box, GFX_WHITE);
    int cy = box.y + box.h / 2;
    for (int i = 0; i < rad->loop_count; i++) {
        int cx = box.x + PAD + 4 + i * 9;
        if (i == rad->loop_at) {
            gfx_fill_circle(fb, cx, cy, 3, GFX_BLACK);
        } else {
            gfx_circle(fb, cx, cy, 2, GFX_BLACK);
        }
    }
}

/* The map with the rain and `caption` at the bottom left; with `ctx`, the Radar layout's: the legend or
 * the loop at the bottom right, and the message before the first frame. */
static void draw_map(gfx_fb_t *fb, gfx_rect_t r, const ui_radar_t *rad, ui_size_t size, const char *caption,
                     bool old, const ui_context_t *ctx)
{
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, r));
    map_view_t v;
    map_view_init(&v, rad->wx_lat_e4, rad->wx_lon_e4, rad->wx_zoom_q / 4.0, r.w, r.h);
    if (rad->frame != NULL) {
        radar_render(fb, r, &v, rad->frame);
    }
    map_style_t style = { .airports = false, .halo = true,
                          .max_towns = size == UI_SIZE_XL ? 12 : size == UI_SIZE_L ? 5 : 3 };
    if (rad->map != NULL) {
        map_draw_lines(fb, r, &v, rad->map, &style);
    }
    static map_labels_t labels; /* 1.5 KB, off the stack: only the app task draws (spec §3.2) */
    map_labels_init(&labels);
    const gfx_font_t *cf = size == UI_SIZE_XL ? &gfx_font_sans_bold_16 : &gfx_font_sans_12, *lf = &gfx_font_sans_12;
    gfx_rect_t cap = { 0 }, right = { 0 };
    if (rad->frame != NULL) { /* the bottom row first, so the places keep clear of it */
        cap = bottom_box(r, gfx_text_width(cf, caption) + 2 * PAD, cf->line_height + 2 * PAD - 2, false);
        map_labels_reserve(&labels, cap);
        if (ctx != NULL) {
            int w = rad->loop_count > 0 ? rad->loop_count * 9 + 2 * PAD : legend_w(ctx, lf);
            right = bottom_box(r, w, lf->line_height + 2 * PAD - 2, true);
            map_labels_reserve(&labels, right);
        }
    }
    map_draw_home(fb, r, &v, rad->home_lat_e4, rad->home_lon_e4, &labels);
    if (rad->map != NULL) {
        map_draw_places(fb, r, &v, rad->map, &style, &labels);
    }
    if (rad->frame != NULL) {
        boxed(fb, cap, cf, caption, old);
        if (ctx != NULL && rad->loop_count > 0) {
            loop_dots(fb, right, rad);
        } else if (ctx != NULL) {
            legend(fb, right, ctx, lf);
        }
    } else if (ctx != NULL) {
        const char *none = lang_str(ctx->lang, LS_NO_RADAR_FRAME);
        const gfx_font_t *nf = &gfx_font_sans_bold_20;
        int w = gfx_text_width(nf, none) + 4 * PAD, h = nf->line_height + 4 * PAD;
        gfx_rect_t box = { (int16_t)(r.x + (r.w - w) / 2), (int16_t)(r.y + (r.h - h) / 2), (int16_t)w, (int16_t)h };
        gfx_fill_rect(fb, box, GFX_WHITE);
        gfx_rect(fb, box, GFX_BLACK);
        gfx_text(fb, nf, box.x + 2 * PAD, box.y + 2 * PAD + nf->ascent, none, GFX_BLACK);
    }
    fb->clip = saved;
}

void ui_draw_radar_view(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx)
{
    ui_radar_t at_home = { .home_lat_e4 = ctx->lat_e4, .home_lon_e4 = ctx->lon_e4, .wx_lat_e4 = ctx->lat_e4,
                           .wx_lon_e4 = ctx->lon_e4, .wx_zoom_q = 26 };
    const ui_radar_t *rad = ctx->radar != NULL ? ctx->radar : &at_home; /* no radar at all: an empty map */
    char caption[64] = "";
    bool loop = rad->loop_count > 0; /* the loop shows each frame's time alone (D28) */
    if (rad->frame != NULL) {
        caption_text(ctx, rad->frame, loop, caption, sizeof(caption));
    }
    draw_map(fb, r, rad, UI_SIZE_XL, caption, !loop && rad->frame != NULL && frame_old(ctx->now, rad->frame), ctx);
}

bool ui_resolve_radar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    if (field != UI_FIELD_RAIN_MAP) {
        return false;
    }
    const ui_radar_t *rad = ctx->radar;
    if (rad == NULL || rad->frame == NULL) {
        out->state = UI_VALUE_MISSING;
        return true;
    }
    out->state = UI_VALUE_FRESH; /* its age shows on the map, not as stale (spec §5.1) */
    out->radar = rad;
    out->age_s = ctx->now > (time_t)rad->frame->time ? (uint32_t)(ctx->now - (time_t)rad->frame->time) : 0;
    caption_text(ctx, rad->frame, true, out->text, sizeof(out->text));
    return true;
}

bool ui_radar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    if (v->kind != UI_FK_RAIN_MAP || v->state == UI_VALUE_MISSING || v->radar == NULL) {
        return false;
    }
    draw_map(fb, r, v->radar, size, v->text, v->age_s > UI_RADAR_OLD_S, NULL); /* the time, inverted once old */
    return true;
}
