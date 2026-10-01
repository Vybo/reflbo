#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "radar.h"

/*
 * The weather radar's frames over HTTPS (spec §11.2), fetched on the sync task: ČHMÚ's composite
 * where it covers the view's centre, else RainViewer's index and the view's tiles. Device only.
 */

#define RADAR_FETCH_DETAIL_LEN 24

typedef struct {
    int32_t lat_e4, lon_e4; /* radar.weather's centre */
    uint8_t zoom_q;         /* and its zoom, in quarters */
    uint8_t want;           /* the frames kept: 1, or in sync mode `always` the hour's (ČHMÚ 12, RainViewer 6) */
    uint8_t have_count;
    uint32_t have[RADAR_LOOP_FRAMES]; /* the frame times the app keeps, not fetched again */
    uint32_t now;                     /* UTC seconds, ČHMÚ's file names; 0 while the clock is unknown */
    int64_t deadline_us;              /* esp_timer_get_time() by which it stops */
} radar_fetch_req_t;

typedef struct {
    radar_frame_t frames[RADAR_LOOP_FRAMES]; /* in PSRAM, from radar_fetch_mem: the taker frees them */
    uint8_t count;
    char detail[RADAR_FETCH_DETAIL_LEN]; /* why it failed: "HTTP 503", "timeout", "png: bad crc" */
} radar_fetch_result_t;

extern const png_mem_t radar_fetch_mem; /* PSRAM */

/* The newest frame and the older ones `want` asks for that `have` lacks. ESP_OK with what came,
 * maybe nothing new; an error, with `detail`, when the newest didn't come. */
esp_err_t radar_fetch(const radar_fetch_req_t *req, radar_fetch_result_t *out);
