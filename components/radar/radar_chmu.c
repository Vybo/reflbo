#define _POSIX_C_SOURCE 200809L /* gmtime_r */

#include <stdio.h>
#include <string.h>

#include "radar.h"

/* ČHMÚ's colour scale (radar/scl/scl-dbz-mmh.png): each class's colour and its lowest dBZ, 4 dBZ
 * apart; checked on 2026-10-01 against the HDF5 composite of the same frame. */
static const struct {
    uint8_t rgb[3];
    int8_t dbz;
} k_scale[] = {
    { { 252, 252, 252 }, 60 }, { { 160, 0, 0 }, 56 },   { { 252, 0, 0 }, 52 },   { { 252, 88, 0 }, 48 },
    { { 252, 132, 0 }, 44 },   { { 252, 176, 0 }, 40 }, { { 224, 220, 0 }, 36 }, { { 156, 220, 0 }, 32 },
    { { 52, 216, 0 }, 28 },    { { 0, 188, 0 }, 24 },   { { 0, 160, 0 }, 20 },   { { 0, 108, 192 }, 16 },
    { { 0, 0, 252 }, 12 },     { { 48, 0, 168 }, 8 },   { { 56, 0, 112 }, 4 },
};

static radar_level_t level_of_dbz(int dbz)
{
    return dbz >= 44 ? RADAR_HEAVY : dbz >= 36 ? RADAR_MODERATE : dbz >= 20 ? RADAR_LIGHT : RADAR_NONE;
}

void radar_chmu_name(time_t t, char *out, size_t size)
{
    struct tm u;
    gmtime_r(&t, &u);
    snprintf(out, size, "pacz2gmaps3.z_max3d.%04d%02d%02d.%02d%02d.0.png", u.tm_year + 1900, u.tm_mon + 1,
             u.tm_mday, u.tm_hour, u.tm_min);
}

time_t radar_chmu_newest(time_t now)
{
    time_t t = now - RADAR_CHMU_STEP_S;
    return t - t % RADAR_CHMU_STEP_S;
}

bool radar_chmu_covers(double lat, double lon)
{
    double mx, my;
    map_mercator(lat, lon, &mx, &my);
    return mx >= RADAR_CHMU_MX0 && mx < RADAR_CHMU_MX0 + RADAR_CHMU_W * RADAR_CHMU_SCALE &&
           my <= RADAR_CHMU_MY0 && my > RADAR_CHMU_MY0 - RADAR_CHMU_H * RADAR_CHMU_SCALE;
}

typedef struct {
    radar_frame_t *f;
    bool lut_ready;
    uint8_t lut[256];
} chmu_ctx_t;

static void build_lut(chmu_ctx_t *c, const png_info_t *info)
{
    memset(c->lut, RADAR_NONE, sizeof(c->lut));
    for (int i = 0; i < info->palette_size; i++) {
        if (info->palette[i][3] == 0) {
            continue; /* no echo */
        }
        for (size_t k = 0; k < sizeof(k_scale) / sizeof(k_scale[0]); k++) {
            if (memcmp(info->palette[i], k_scale[k].rgb, 3) == 0) {
                c->lut[i] = (uint8_t)level_of_dbz(k_scale[k].dbz);
                break;
            }
        }
    }
    c->lut_ready = true;
}

static void on_row(void *ctx, int y, const uint8_t *row, const png_info_t *info)
{
    chmu_ctx_t *c = ctx;
    if (!c->lut_ready) {
        build_lut(c, info);
    }
    int gy = y - RADAR_CHMU_PNG_TOP;
    if (gy < 0 || gy >= c->f->h) {
        return;
    }
    for (int x = 0; x < c->f->w && x < info->width; x++) {
        if (c->lut[row[x]] != RADAR_NONE) {
            radar_frame_set(c->f, x, gy, (radar_level_t)c->lut[row[x]]);
        }
    }
}

png_err_t radar_chmu_decode(const uint8_t *png, size_t len, const png_mem_t *mem, uint32_t time,
                            radar_frame_t *out)
{
    if (!radar_frame_alloc(out, RADAR_CHMU_W, RADAR_CHMU_H, mem)) {
        return PNG_ERR_NO_MEMORY;
    }
    out->time = time;
    out->source = RADAR_SOURCE_CHMU;
    out->mx0 = RADAR_CHMU_MX0;
    out->my0 = RADAR_CHMU_MY0;
    out->scale = RADAR_CHMU_SCALE;
    static chmu_ctx_t c; /* 260 bytes; off the sync task's stack */
    c.f = out;
    c.lut_ready = false;
    static png_info_t info;
    png_err_t err = png_decode(png, len, mem, &info, on_row, &c);
    if (err == PNG_OK && (info.type != PNG_PALETTE || info.width < RADAR_CHMU_W ||
                          info.height < RADAR_CHMU_PNG_TOP + RADAR_CHMU_H)) {
        err = PNG_ERR_UNSUPPORTED; /* not the composite's layout */
    }
    if (err != PNG_OK) {
        radar_frame_free(out, mem);
    }
    return err;
}
