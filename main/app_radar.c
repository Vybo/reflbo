#include <stdio.h>
#include <string.h>

#include "app_internal.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "map_data.h"
#include "storage.h"
#include "timekeeping.h"

/* The weather radar on the device (spec §11.2): the frames kept in PSRAM, the newest also in
 * /fs/state/radar.bin, and what the views draw. It belongs to the app task; the sync task fetches. */

static const char *TAG = "app_radar";

#define FRAME_PATH "/fs/state/radar.bin"
#define FRAME_FILE_MAX (RADAR_FILE_HEADER + 3 * 256 * 3 * 256 / 4) /* RainViewer's 3 x 3 tiles */
#define SAVE_EVERY_S 1800 /* sync mode `always`: the file follows its new frames at most this often */

extern const uint8_t k_map_start[] asm("_binary_map_bin_start");
extern const uint8_t k_map_end[] asm("_binary_map_bin_end");

static radar_store_t s_store;
static bool s_ready;
static bool s_loaded;       /* the file was read since the boot or the wake */
static uint32_t s_saved_at; /* the time of the frame in the file; 0 = none known */
static map_data_t s_map;
static int s_map_state; /* 0 not opened yet, 1 open, -1 broken */
static app_radar_status_t s_status;
static ui_radar_t s_ui;

static void ready(void)
{
    if (!s_ready) {
        radar_store_init(&s_store, &radar_fetch_mem);
        s_ready = true;
    }
}

static uint8_t source_now(void)
{
    const settings_t *set = app_settings();
    return radar_chmu_covers(set->wx_lat_e4 / 1e4, set->wx_lon_e4 / 1e4) ? RADAR_SOURCE_CHMU : RADAR_SOURCE_RAINVIEWER;
}

/* One frame, or in sync mode `always` the loop's hour (D28): 12 of ČHMÚ's, 6 of RainViewer's. */
static int keep_count(void)
{
    if (app_settings()->sync_mode != SETTINGS_SYNC_ALWAYS) {
        return 1;
    }
    return source_now() == RADAR_SOURCE_CHMU ? RADAR_LOOP_FRAMES : 3600 / RADAR_RV_STEP_S;
}

uint32_t app_radar_step_s(void)
{
    return source_now() == RADAR_SOURCE_CHMU ? RADAR_CHMU_STEP_S : RADAR_RV_STEP_S;
}

/* A frame with the rain for the view as it is now: the Radar layout's map, which holds any slot's. */
static bool fits(const radar_frame_t *f)
{
    const settings_t *set = app_settings();
    if (f->source != source_now()) {
        return false;
    }
    map_view_t v;
    map_view_init(&v, set->wx_lat_e4, set->wx_lon_e4, set->wx_zoom_q / 4.0, RADAR_VIEW_W, RADAR_VIEW_H);
    return radar_frame_covers(f, &v);
}

/* A new centre or zoom, a new mode: frames that no longer fit go, and so do those beyond the keep. */
static void tidy(void)
{
    ready();
    const radar_frame_t *newest = radar_store_newest(&s_store);
    if (newest != NULL && !fits(newest)) {
        ESP_LOGI(TAG, "the view moved: its frames are dropped");
        radar_store_clear(&s_store);
    }
    radar_store_keep(&s_store, keep_count()); /* sync mode `always` ended: the newest stays */
}

/* The newest frame from its file, once a boot or a wake: a deep sleep loses PSRAM. */
static void load(void)
{
    ready();
    if (s_loaded || s_store.count > 0) {
        return;
    }
    s_loaded = true;
    if (storage_init() != ESP_OK) {
        return;
    }
    FILE *file = fopen(FRAME_PATH, "rb");
    if (file == NULL) {
        return;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *buf = size > 0 && size <= FRAME_FILE_MAX ? heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM) : NULL;
    bool read = buf != NULL && fread(buf, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    radar_frame_t f;
    if (read && radar_frame_from_file(buf, (size_t)size, &radar_fetch_mem, &f)) {
        s_saved_at = f.time;
        ESP_LOGI(TAG, "frame of %lu from %s", (unsigned long)f.time, FRAME_PATH);
        radar_store_put(&s_store, &f, keep_count());
        tidy(); /* a frame for another view goes */
    } else if (buf != NULL) {
        ESP_LOGW(TAG, "%s: not a frame of this firmware; left out", FRAME_PATH);
    }
    heap_caps_free(buf);
}

/* The newest frame into its file, so a power-off or a deep sleep keeps it; in sync mode `always`,
 * where one comes every 5 or 10 minutes, at most every SAVE_EVERY_S, to spare the flash. */
static void save_newest(void)
{
    const radar_frame_t *f = radar_store_newest(&s_store);
    if (f == NULL || f->time == s_saved_at) {
        return;
    }
    if (app_settings()->sync_mode == SETTINGS_SYNC_ALWAYS && s_saved_at != 0 && f->time < s_saved_at + SAVE_EVERY_S) {
        return;
    }
    if (storage_init() != ESP_OK) {
        return;
    }
    size_t n = radar_frame_file_size(f);
    uint8_t *buf = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        return;
    }
    radar_frame_to_file(f, buf, n);
    esp_err_t err = storage_write_atomic(FRAME_PATH, (const char *)buf, n);
    heap_caps_free(buf);
    if (err == ESP_OK) {
        s_saved_at = f->time;
    } else {
        ESP_LOGW(TAG, "%s not saved: %s", FRAME_PATH, esp_err_to_name(err));
    }
}

void app_radar_request(radar_fetch_req_t *out)
{
    load(); /* the frame in the file is had already */
    tidy();
    const settings_t *set = app_settings();
    memset(out, 0, sizeof(*out));
    out->lat_e4 = set->wx_lat_e4;
    out->lon_e4 = set->wx_lon_e4;
    out->zoom_q = set->wx_zoom_q;
    out->want = (uint8_t)keep_count();
    for (int i = 0; i < s_store.count; i++) {
        out->have[out->have_count++] = s_store.frames[i].time;
    }
    out->now = timekeeping_valid() ? (uint32_t)time(NULL) : 0; /* a sync's own NTP time replaces it */
}

void app_radar_apply(radar_fetch_result_t *res, uint8_t result, const char *detail)
{
    tidy();
    int added = 0;
    for (int i = 0; i < res->count; i++) {
        radar_frame_t *f = &res->frames[i];
        if (fits(f)) {
            radar_store_put(&s_store, f, keep_count()); /* the store takes it */
            added++;
        } else {
            radar_frame_free(f, &radar_fetch_mem); /* the view moved while it came */
        }
    }
    res->count = 0;
    if (result != SYNC_STEP_NOT_RUN) { /* a sync that never got to it leaves the last fetch's status */
        s_status.fetched_at = time(NULL);
        s_status.ok = result == SYNC_STEP_OK;
        snprintf(s_status.detail, sizeof(s_status.detail), "%s", s_status.ok ? "" : detail);
    }
    if (added > 0) {
        save_newest();
    }
}

void app_radar_status(app_radar_status_t *out)
{
    tidy();
    *out = s_status;
    out->frames = (uint8_t)s_store.count;
    const radar_frame_t *f = radar_store_newest(&s_store);
    out->frame_at = f != NULL ? f->time : 0;
    out->source = source_now();
}

/* The preset draws the weather radar: the Radar layout, or a slot's map. */
static bool shows_weather(const ui_preset_t *p)
{
    if (p->layout == UI_LAYOUT_RADAR) {
        return true;
    }
    const ui_layout_t *layout = ui_layout((ui_layout_id_t)p->layout);
    for (int i = 0; layout != NULL && i < layout->slot_count; i++) {
        if (p->slots[i] == UI_FIELD_RAIN_MAP) {
            return true;
        }
    }
    return false;
}

void app_radar_prepare(const ui_preset_t *p)
{
    bool weather = shows_weather(p);
    if (s_map_state == 0 && (weather || p->layout == UI_LAYOUT_FLIGHTS)) {
        /* the image's own checksum covers it: only its header is checked (map_data_verify is the host's) */
        s_map_state = map_data_open(&s_map, k_map_start, (size_t)(k_map_end - k_map_start)) ? 1 : -1;
        if (s_map_state < 0) {
            ESP_LOGE(TAG, "the built-in map doesn't open");
        }
    }
    if (weather) {
        load();
    }
}

const ui_radar_t *app_radar_ui(void)
{
    tidy();
    const settings_t *set = app_settings();
    s_ui = (ui_radar_t){ .map = s_map_state > 0 ? &s_map : NULL, .home_lat_e4 = set->lat_e4,
                         .home_lon_e4 = set->lon_e4, .frame = radar_store_newest(&s_store),
                         .wx_lat_e4 = set->wx_lat_e4, .wx_lon_e4 = set->wx_lon_e4, .wx_zoom_q = set->wx_zoom_q,
                         .fl_lat_e4 = set->fl_lat_e4, .fl_lon_e4 = set->fl_lon_e4,
                         .fl_range_km = set->fl_range_km,
                         .fl_always = set->sync_mode == SETTINGS_SYNC_ALWAYS };
    return &s_ui;
}
