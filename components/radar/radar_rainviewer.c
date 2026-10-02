#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "radar.h"
#include "util_json.h"

#define PI 3.14159265358979323846
#define JSON_MAX_DEPTH 8 /* weather-maps.json is 4 deep */

/* RainViewer's Universal Blue colours from 20 dBZ up (rainviewer.com/files/rainviewer_api_colors_table.csv,
 * checked 2026-10-01; every scheme number returns it since 2026), sorted by RGB, with their level:
 * light below 35 dBZ, moderate below 45, heavy from there. */
static const struct {
    uint32_t rgb;
    uint8_t level;
} k_colours[] = {
    { 0x004768, 1 }, { 0x004a70, 1 }, { 0x004e78, 1 }, { 0x005180, 1 }, { 0x005588, 1 }, { 0x005b8e, 1 },
    { 0x006295, 1 }, { 0x00699c, 1 }, { 0x0070a3, 1 }, { 0x0077aa, 1 }, { 0x007fb4, 1 }, { 0x0088bf, 1 },
    { 0x0091ca, 1 }, { 0x009ad5, 1 }, { 0x00a3e0, 1 }, { 0x00ff00, 3 }, { 0x5d0000, 3 }, { 0x760000, 3 },
    { 0x8f0000, 3 }, { 0xa80000, 3 }, { 0xc10000, 3 }, { 0xcd0d00, 3 }, { 0xd91b00, 3 }, { 0xe62800, 3 },
    { 0xf23600, 3 }, { 0xff4400, 3 }, { 0xff4eff, 3 }, { 0xff58ff, 3 }, { 0xff62ff, 3 }, { 0xff6cff, 3 },
    { 0xff77ff, 3 }, { 0xff8100, 2 }, { 0xff81ff, 3 }, { 0xff8b00, 2 }, { 0xff8bff, 3 }, { 0xff9500, 2 },
    { 0xff95ff, 3 }, { 0xff9f00, 2 }, { 0xff9fff, 3 }, { 0xffaa00, 2 }, { 0xffaaff, 3 }, { 0xffb700, 2 },
    { 0xffc500, 2 }, { 0xffd200, 2 }, { 0xffe000, 2 }, { 0xffee00, 2 }, { 0xffffff, 3 },
};

static radar_level_t level_of_rgb(uint32_t rgb)
{
    int lo = 0, hi = (int)(sizeof(k_colours) / sizeof(k_colours[0])) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (k_colours[mid].rgb == rgb) {
            return (radar_level_t)k_colours[mid].level;
        }
        if (k_colours[mid].rgb < rgb) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return RADAR_NONE; /* under 20 dBZ, or no colour of the table */
}

bool radar_rv_parse_index(const char *json, size_t len, radar_rv_index_t *out)
{
    memset(out, 0, sizeof(*out));
    if (util_json_depth(json) > JSON_MAX_DEPTH) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    const cJSON *host = cJSON_GetObjectItemCaseSensitive(root, "host");
    const cJSON *past = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "radar"), "past");
    int n = cJSON_GetArraySize(past);
    if (cJSON_IsString(host) && strlen(host->valuestring) < sizeof(out->host)) {
        snprintf(out->host, sizeof(out->host), "%s", host->valuestring);
        for (int i = n > RADAR_RV_FRAMES ? n - RADAR_RV_FRAMES : 0; i < n; i++) { /* the newest 16 */
            const cJSON *item = cJSON_GetArrayItem(past, i);
            const cJSON *time = cJSON_GetObjectItemCaseSensitive(item, "time");
            const cJSON *path = cJSON_GetObjectItemCaseSensitive(item, "path");
            if (cJSON_IsNumber(time) && time->valuedouble > 0 && cJSON_IsString(path) &&
                strlen(path->valuestring) < sizeof(out->frames[0].path)) {
                radar_rv_frame_t *f = &out->frames[out->count++];
                f->time = (uint32_t)time->valuedouble;
                snprintf(f->path, sizeof(f->path), "%s", path->valuestring);
            }
        }
    }
    cJSON_Delete(root);
    return out->count > 0;
}

void radar_rv_tiles(const map_view_t *v, radar_rv_tiles_t *t)
{
    int z = (int)floor(v->zoom);
    t->z = z < 0 ? 0 : z > RADAR_RV_MAX_ZOOM ? RADAR_RV_MAX_ZOOM : z;
    double s = pow(2.0, t->z - v->zoom); /* the view's pixels at the tiles' zoom */
    double left = (v->cx - v->w / 2.0) * s, right = (v->cx + v->w / 2.0) * s;
    double top = (v->cy - v->h / 2.0) * s, bottom = (v->cy + v->h / 2.0) * s;
    int last = (1 << t->z) - 1;
    int x0 = (int)floor(left / MAP_TILE_PX), x1 = (int)floor((right - 1e-6) / MAP_TILE_PX);
    int y0 = (int)floor(top / MAP_TILE_PX), y1 = (int)floor((bottom - 1e-6) / MAP_TILE_PX);
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > last ? last : x1;
    y1 = y1 > last ? last : y1;
    t->x0 = x0;
    t->y0 = y0;
    t->nx = x1 - x0 + 1 > RADAR_RV_TILES_MAX ? RADAR_RV_TILES_MAX : x1 - x0 + 1;
    t->ny = y1 - y0 + 1 > RADAR_RV_TILES_MAX ? RADAR_RV_TILES_MAX : y1 - y0 + 1;
}

int radar_rv_tile_url(char *out, size_t size, const char *host, const char *path, int z, int x, int y)
{
    return snprintf(out, size, "%s%s/256/%d/%d/%d/2/0_0.png", host, path, z, x, y);
}

bool radar_rv_frame_alloc(radar_frame_t *f, const radar_rv_tiles_t *t, uint32_t time, const png_mem_t *mem)
{
    if (!radar_frame_alloc(f, (uint16_t)(t->nx * MAP_TILE_PX), (uint16_t)(t->ny * MAP_TILE_PX), mem)) {
        return false;
    }
    double tiles = pow(2.0, t->z);
    f->time = time;
    f->source = RADAR_SOURCE_RAINVIEWER;
    f->scale = 2 * PI * MAP_EARTH_R / (MAP_TILE_PX * tiles);
    f->mx0 = (t->x0 / tiles - 0.5) * 2 * PI * MAP_EARTH_R;
    f->my0 = (0.5 - t->y0 / tiles) * 2 * PI * MAP_EARTH_R;
    return true;
}

typedef struct {
    radar_frame_t *f;
    int ox, oy; /* the tile's top-left in the frame's grid */
} tile_ctx_t;

static void on_tile_row(void *ctx, int y, const uint8_t *row, const png_info_t *info)
{
    tile_ctx_t *c = ctx;
    if (info->type != PNG_RGBA) {
        return;
    }
    for (int x = 0; x < info->width; x++) {
        const uint8_t *px = row + 4 * x;
        if (px[3] != 0) {
            radar_level_t level = level_of_rgb((uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | px[2]);
            if (level != RADAR_NONE) {
                radar_frame_set(c->f, c->ox + x, c->oy + y, level);
            }
        }
    }
}

png_err_t radar_rv_decode_tile(const uint8_t *png, size_t len, const png_mem_t *mem, const radar_rv_tiles_t *t,
                               int x, int y, radar_frame_t *f)
{
    tile_ctx_t c = { .f = f, .ox = (x - t->x0) * MAP_TILE_PX, .oy = (y - t->y0) * MAP_TILE_PX };
    static png_info_t info; /* 1 KB; off the sync task's stack */
    png_err_t err = png_decode(png, len, mem, &info, on_tile_row, &c);
    return err == PNG_OK && info.type != PNG_RGBA ? PNG_ERR_UNSUPPORTED : err;
}
