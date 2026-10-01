#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "gfx.h"
#include "map_view.h"
#include "png.h"

/*
 * The weather radar (spec §11.2): ČHMÚ's composite where it covers the view, RainViewer's tiles
 * elsewhere, both decoded once into a frame of three rain levels and drawn into any view. Pure C,
 * host-buildable; the fetching is in the sync task.
 */

typedef enum {
    RADAR_NONE,
    RADAR_LIGHT,    /* from about 20 dBZ */
    RADAR_MODERATE, /* from about 35 dBZ (ČHMÚ: 36) */
    RADAR_HEAVY,    /* from about 45 dBZ (ČHMÚ: 44) */
} radar_level_t;

typedef enum {
    RADAR_SOURCE_CHMU,
    RADAR_SOURCE_RAINVIEWER,
} radar_source_t;

/* A frame: rain levels on a grid of web-Mercator pixels. */
typedef struct {
    uint32_t time;   /* UTC seconds: the end of ČHMÚ's 5 minutes, RainViewer's frame time */
    uint8_t source;  /* radar_source_t */
    uint16_t w, h;   /* grid pixels */
    double mx0, my0; /* web Mercator metres of the grid's top-left corner */
    double scale;    /* metres per grid pixel */
    uint8_t *levels; /* w x h, 2 bits a pixel, 4 to a byte, the first in the top bits */
} radar_frame_t;

/* The grid zeroed (no rain), from `mem` (NULL: malloc); false without memory. */
bool radar_frame_alloc(radar_frame_t *f, uint16_t w, uint16_t h, const png_mem_t *mem);
void radar_frame_free(radar_frame_t *f, const png_mem_t *mem);
radar_level_t radar_frame_level(const radar_frame_t *f, int x, int y); /* RADAR_NONE outside */
void radar_frame_set(radar_frame_t *f, int x, int y, radar_level_t level);

/* ---- ČHMÚ's composite of maximum reflectivity (CC BY 4.0) ---- */

#define RADAR_CHMU_URL "https://opendata.chmi.cz/meteorology/weather/radar/composite/maxz/png/"
#define RADAR_CHMU_STEP_S 300
/* Its data area, from the HDF5 composite's corners (ODIM `where`): 598 x 378 pixels of 1555.7 m. */
#define RADAR_CHMU_W 598
#define RADAR_CHMU_H 378
#define RADAR_CHMU_SCALE 1555.7
#define RADAR_CHMU_MX0 1254222.15 /* 11.266869 E */
#define RADAR_CHMU_MY0 6702777.85 /* 51.458369 N */
#define RADAR_CHMU_PNG_TOP 82     /* the PNG's rows above the data: title and side projection */

/* The file name of the frame that ends at `t` (UTC), "pacz2gmaps3.z_max3d.20260924.1120.0.png". */
void radar_chmu_name(time_t t, char *out, size_t size);
/* The newest frame to ask for at `now`: the latest 5-minute step at least 5 minutes old. */
time_t radar_chmu_newest(time_t now);
/* The point lies inside ČHMÚ's data area. */
bool radar_chmu_covers(double lat, double lon);
/* ČHMÚ's PNG into `out`: its data area, each palette colour to its level by ČHMÚ's colour scale. */
png_err_t radar_chmu_decode(const uint8_t *png, size_t len, const png_mem_t *mem, uint32_t time,
                            radar_frame_t *out);

/* ---- RainViewer (personal and educational use) ---- */

#define RADAR_RV_INDEX_URL "https://api.rainviewer.com/public/weather-maps.json"
#define RADAR_RV_MAX_ZOOM 7
#define RADAR_RV_STEP_S 600 /* its frames come every 10 min */
#define RADAR_RV_FRAMES 16
#define RADAR_RV_TILES_MAX 3 /* a side: at most 3 x 3 tiles a view */
#define RADAR_VIEW_W 400 /* the Radar layout's map, which holds any slot's: the view a fetch covers */
#define RADAR_VIEW_H 279

typedef struct {
    uint32_t time;
    char path[48]; /* "/v2/radar/c15f405b89f4" */
} radar_rv_frame_t;

typedef struct {
    char host[64]; /* "https://tilecache.rainviewer.com" */
    int count;     /* the past frames, oldest first */
    radar_rv_frame_t frames[RADAR_RV_FRAMES];
} radar_rv_index_t;

typedef struct {
    int z;      /* RainViewer's zoom: the view's, rounded down, at most 7 */
    int x0, y0; /* the top-left tile */
    int nx, ny; /* tiles across and down */
} radar_rv_tiles_t;

/* weather-maps.json; false if it isn't one, or names no frames. */
bool radar_rv_parse_index(const char *json, size_t len, radar_rv_index_t *out);
/* The tiles that cover the view. */
void radar_rv_tiles(const map_view_t *v, radar_rv_tiles_t *t);
/* A tile's URL, in RainViewer's one colour scheme and without smoothing, so its colours are exact. */
int radar_rv_tile_url(char *out, size_t size, const char *host, const char *path, int z, int x, int y);
/* A frame for those tiles: its grid allocated, its place set. */
bool radar_rv_frame_alloc(radar_frame_t *f, const radar_rv_tiles_t *t, uint32_t time, const png_mem_t *mem);
/* One tile's PNG into the frame at tile (x, y) of the world. */
png_err_t radar_rv_decode_tile(const uint8_t *png, size_t len, const png_mem_t *mem, const radar_rv_tiles_t *t,
                               int x, int y, radar_frame_t *f);
/* The frame has the rain for all of the view: ČHMÚ's for any (beyond its grid the edge shows),
 * RainViewer's when it holds the tiles the view needs, at the view's tile zoom. */
bool radar_frame_covers(const radar_frame_t *f, const map_view_t *v);

/* A frame as /fs/state/radar.bin keeps it (spec §11.2, D28), so a power-off or a deep sleep keeps
 * the latest: 48 bytes of header (magic "rfrm", version, source, size, time, georeference, the
 * levels' length and CRC-32), then the levels. */
#define RADAR_FILE_MAGIC 0x6d726672u
#define RADAR_FILE_VERSION 1
#define RADAR_FILE_HEADER 48
size_t radar_frame_file_size(const radar_frame_t *f);
/* The file's bytes; 0 if `size` is too small. */
size_t radar_frame_to_file(const radar_frame_t *f, uint8_t *out, size_t size);
/* A frame from them, its levels from `mem`; false, and out->levels NULL, for anything else. */
bool radar_frame_from_file(const uint8_t *data, size_t len, const png_mem_t *mem, radar_frame_t *out);

/* ---- drawing ---- */

/* The frame's rain in `area`, whose top-left is the view's (0, 0): light as one pixel in four,
 * moderate as a checkerboard, heavy solid; where the frame has no data, the edge of its data
 * dotted. Draws over what is there, so the map goes on top. */
void radar_render(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const radar_frame_t *f);
/* Any rain inside the view. */
bool radar_any_rain(const map_view_t *v, const radar_frame_t *f);
/* The dither: whether screen pixel (x, y) is inked at `level`, as radar_render() draws it. */
bool radar_inks(radar_level_t level, int x, int y);

/* ---- the frames kept ---- */

#define RADAR_LOOP_FRAMES 12 /* the last hour at ČHMÚ's 5 minutes (D28) */

typedef struct {
    radar_frame_t frames[RADAR_LOOP_FRAMES]; /* oldest first */
    int count;
    png_mem_t mem; /* where their grids came from */
} radar_store_t;

void radar_store_init(radar_store_t *s, const png_mem_t *mem);
/* Takes `f`, its grid with it: in time order, replacing a frame of the same time; then keeps the
 * `keep` newest (1 outside sync mode `always`, up to RADAR_LOOP_FRAMES) and frees the rest. */
void radar_store_put(radar_store_t *s, radar_frame_t *f, int keep);
const radar_frame_t *radar_store_newest(const radar_store_t *s); /* NULL when empty */
bool radar_store_has(const radar_store_t *s, uint32_t time);
/* The frame times of the hour before `newest` (in steps of `step_s`) not kept yet, newest first. */
int radar_store_missing(const radar_store_t *s, uint32_t newest, uint32_t step_s, uint32_t *out, int max);
void radar_store_clear(radar_store_t *s);
/* Frees the oldest frames beyond `keep` (clamped to 1..RADAR_LOOP_FRAMES). */
void radar_store_keep(radar_store_t *s, int keep);
/* The frame times a fetch wants (spec §11.2): `newest` and the `want - 1` steps before it, newest
 * first, less those in `have`; `want` is clamped to 1..RADAR_LOOP_FRAMES. Returns how many. */
int radar_wanted(uint32_t newest, uint32_t step_s, int want, const uint32_t *have, int have_count, uint32_t *out);
