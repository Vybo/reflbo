#include "radar_fetch.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fetch.h"

static const char *TAG = "radar";

#define GET_TIMEOUT_MS 10000
#define INDEX_MAX 4096           /* weather-maps.json is about 2 KB */
#define CHMU_TRIES 3             /* the newest step, then up to two back on a 404 (spec §11.2) */
#define RV_HOUR (3600 / RADAR_RV_STEP_S) /* the loop's hour of RainViewer's frames */

EXT_RAM_BSS_ATTR static uint8_t s_png[PNG_MAX_BYTES + 1];
EXT_RAM_BSS_ATTR static char s_index[INDEX_MAX];

static void *psram_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
}

const png_mem_t radar_fetch_mem = { psram_alloc, heap_caps_free };

/* What is left before the deadline, at most GET_TIMEOUT_MS; 0 when under a second. */
static int budget_ms(const radar_fetch_req_t *req)
{
    int64_t left = (req->deadline_us - esp_timer_get_time()) / 1000;
    return left < 1000 ? 0 : left > GET_TIMEOUT_MS ? GET_TIMEOUT_MS : (int)left;
}

static esp_err_t get(fetch_session_t *s, const radar_fetch_req_t *req, const char *url, void *buf, size_t size,
                     size_t *len, int *status, char *detail)
{
    *len = 0;
    *status = 0;
    int budget = budget_ms(req);
    if (budget == 0) {
        snprintf(detail, RADAR_FETCH_DETAIL_LEN, "timeout");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = fetch_get(s, url, buf, size, len, budget, status);
    if (err != ESP_OK) {
        if (*status != 0 && *status != 200) {
            snprintf(detail, RADAR_FETCH_DETAIL_LEN, "HTTP %d", *status);
        } else {
            snprintf(detail, RADAR_FETCH_DETAIL_LEN, "%s", err == ESP_ERR_TIMEOUT ? "timeout" : esp_err_to_name(err));
        }
    }
    return err;
}

static void keep(radar_fetch_result_t *out, radar_frame_t *f)
{
    out->frames[out->count++] = *f;
    f->levels = NULL;
}

/* One ČHMÚ frame by its time; a 404 is ESP_ERR_NOT_FOUND. */
static esp_err_t chmu_frame(fetch_session_t *s, const radar_fetch_req_t *req, uint32_t t, radar_fetch_result_t *out)
{
    char name[64], url[160];
    radar_chmu_name((time_t)t, name, sizeof(name));
    snprintf(url, sizeof(url), "%s%s", RADAR_CHMU_URL, name);
    size_t len;
    int status;
    esp_err_t err = get(s, req, url, s_png, sizeof(s_png), &len, &status, out->detail);
    if (status == 404) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }
    radar_frame_t f;
    png_err_t perr = radar_chmu_decode(s_png, len, &radar_fetch_mem, t, &f);
    if (perr != PNG_OK) {
        snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "png: %s", png_err_name(perr));
        return ESP_ERR_INVALID_RESPONSE;
    }
    keep(out, &f);
    return ESP_OK;
}

static esp_err_t fetch_chmu(fetch_session_t *s, const radar_fetch_req_t *req, radar_fetch_result_t *out)
{
    if (req->now == 0) {
        snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "no time"); /* its files are named by the time */
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t newest = (uint32_t)radar_chmu_newest((time_t)req->now);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < CHMU_TRIES; i++) {
        bool kept = false;
        for (int k = 0; k < req->have_count && !kept; k++) {
            kept = req->have[k] == newest;
        }
        err = kept ? ESP_OK : chmu_frame(s, req, newest, out); /* the newest is here already: done */
        if (err != ESP_ERR_NOT_FOUND) {
            break;
        }
        newest -= RADAR_CHMU_STEP_S; /* not there yet: a step back */
    }
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) {
            snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "HTTP 404");
        }
        return err;
    }
    uint32_t times[RADAR_LOOP_FRAMES];
    int n = radar_wanted(newest, RADAR_CHMU_STEP_S, req->want, req->have, req->have_count, times);
    for (int i = 0; i < n && budget_ms(req) > 0; i++) { /* the hour before it, newest first */
        bool fetched = false;
        for (int k = 0; k < out->count && !fetched; k++) {
            fetched = out->frames[k].time == times[i];
        }
        if (!fetched && chmu_frame(s, req, times[i], out) != ESP_OK) {
            ESP_LOGW(TAG, "frame %lu: %s", (unsigned long)times[i], out->detail);
        }
    }
    out->detail[0] = '\0';
    return ESP_OK;
}

static esp_err_t rv_frame(fetch_session_t *s, const radar_fetch_req_t *req, const radar_rv_index_t *idx,
                          const radar_rv_frame_t *rf, const radar_rv_tiles_t *t, radar_fetch_result_t *out)
{
    radar_frame_t f;
    if (!radar_rv_frame_alloc(&f, t, rf->time, &radar_fetch_mem)) {
        snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "no memory");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = ESP_OK;
    for (int y = t->y0; y < t->y0 + t->ny && err == ESP_OK; y++) {
        for (int x = t->x0; x < t->x0 + t->nx && err == ESP_OK; x++) { /* one connection for them all */
            char url[192];
            radar_rv_tile_url(url, sizeof(url), idx->host, rf->path, t->z, x, y);
            size_t len;
            int status;
            err = get(s, req, url, s_png, sizeof(s_png), &len, &status, out->detail);
            png_err_t perr = err == ESP_OK ? radar_rv_decode_tile(s_png, len, &radar_fetch_mem, t, x, y, &f) : PNG_OK;
            if (perr != PNG_OK) {
                snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "png: %s", png_err_name(perr));
                err = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    if (err != ESP_OK) {
        radar_frame_free(&f, &radar_fetch_mem);
        return err;
    }
    keep(out, &f);
    return ESP_OK;
}

static esp_err_t fetch_rainviewer(fetch_session_t *s, const radar_fetch_req_t *req, radar_fetch_result_t *out)
{
    size_t len;
    int status;
    esp_err_t err = get(s, req, RADAR_RV_INDEX_URL, s_index, sizeof(s_index), &len, &status, out->detail);
    if (err != ESP_OK) {
        return err;
    }
    static radar_rv_index_t idx;
    if (!radar_rv_parse_index(s_index, len, &idx)) {
        snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "bad index");
        return ESP_ERR_INVALID_RESPONSE;
    }
    map_view_t v;
    map_view_init(&v, req->lat_e4, req->lon_e4, req->zoom_q / 4.0, RADAR_VIEW_W, RADAR_VIEW_H);
    radar_rv_tiles_t t;
    radar_rv_tiles(&v, &t);
    const radar_rv_frame_t *newest = &idx.frames[idx.count - 1];
    uint32_t times[RADAR_LOOP_FRAMES];
    int want = req->want > 1 ? RV_HOUR : 1; /* the hour holds 6 of its frames */
    int n = radar_wanted(newest->time, RADAR_RV_STEP_S, want, req->have, req->have_count, times);
    for (int i = 0; i < n; i++) { /* newest first; the index's times sit on the 10-minute steps */
        for (int k = idx.count - 1; k >= 0; k--) {
            if (idx.frames[k].time != times[i]) {
                continue;
            }
            err = rv_frame(s, req, &idx, &idx.frames[k], &t, out);
            if (err != ESP_OK && i == 0) {
                return err; /* not even the newest */
            }
            break;
        }
        if (budget_ms(req) == 0) {
            break;
        }
    }
    out->detail[0] = '\0';
    return ESP_OK;
}

esp_err_t radar_fetch(const radar_fetch_req_t *req, radar_fetch_result_t *out)
{
    memset(out, 0, sizeof(*out));
    fetch_session_t s = { 0 };
    bool chmu = radar_chmu_covers(req->lat_e4 / 1e4, req->lon_e4 / 1e4);
    esp_err_t err = chmu ? fetch_chmu(&s, req, out) : fetch_rainviewer(&s, req, out);
    fetch_close(&s);
    ESP_LOGI(TAG, "%s: %u new frame%s%s%s", chmu ? "ChMU" : "RainViewer", out->count, out->count == 1 ? "" : "s",
             err == ESP_OK ? "" : ", ", out->detail);
    return err;
}
