#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datastore.h"
#include "esp_err.h"
#include "settings.h"

/*
 * The sync (spec §9.3): Wi-Fi, the time, the weather and the air quality, on a task of its own. It
 * only fetches: the app task applies the report, as it owns the clock, the RTC and the datastore
 * (spec §3.2). Wi-Fi stays on afterwards; the app turns it off unless config mode or sync mode
 * `always` keeps it.
 */

typedef enum {
    SYNC_STEP_WIFI,
    SYNC_STEP_TIME,
    SYNC_STEP_WEATHER,
    SYNC_STEP_AIR,
    SYNC_STEP_COUNT,
} sync_step_t;

typedef enum {
    SYNC_STEP_NOT_RUN, /* skipped: an earlier step failed, or the sync never got there */
    SYNC_STEP_OK,
    SYNC_STEP_FAILED,
} sync_step_result_t;

#define SYNC_DETAIL_LEN 24

typedef struct {
    int32_t lat_e4, lon_e4;
    char ntp[SETTINGS_NTP_MAX][SETTINGS_HOST_LEN];
} sync_request_t;

typedef struct {
    uint8_t result[SYNC_STEP_COUNT];                /* sync_step_result_t */
    char detail[SYNC_STEP_COUNT][SYNC_DETAIL_LEN];  /* why a step failed: "not found", "HTTP 503" */
    int64_t ntp_utc_us;  /* SYNC_STEP_TIME: the true time (UTC µs) at the monotonic instant below */
    int64_t ntp_mono_us; /* esp_timer_get_time() */
    int64_t ntp_delay_us;
    ds_weather_t weather; /* SYNC_STEP_WEATHER; `fetched` is the app's to set */
    ds_air_t air;         /* SYNC_STEP_AIR */
} sync_report_t;

/* Starts a sync; `done` runs on the sync task when it ends, and must hand the report to the app task
 * without blocking (it stays valid until the next sync starts). ESP_ERR_INVALID_STATE while one runs. */
esp_err_t sync_start(const sync_request_t *req, void (*done)(const sync_report_t *report));
bool sync_running(void);
/* The step running now, for the progress the web UI shows; SYNC_STEP_COUNT when none runs. */
sync_step_t sync_step(void);
const char *sync_step_name(sync_step_t step); /* "wifi", "time", "weather", "air" */
