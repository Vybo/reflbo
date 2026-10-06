#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datastore.h"
#include "energy.h"
#include "energy_dev.h"
#include "esp_err.h"
#include "radar_fetch.h"
#include "settings.h"
#include "solar.h"
#include "sync_plan.h"

/*
 * The sync (spec §9.3): Wi-Fi, the time, the weather, the air quality, the weather radar, the PV
 * forecast, the house's energy and the MQTT session (M7), on a task of its own; in sync mode `always`
 * also a refresh of the radar and the house's reading, and from the Solar page a check of those two. It
 * only fetches: the app task applies the report, as it owns the clock, the RTC, the datastore, the
 * radar's frames and the solar state (spec §3.2). Wi-Fi stays on afterwards; the app turns it off
 * unless config mode or sync mode `always` keeps it.
 */

#define SYNC_DETAIL_LEN 24

typedef enum {
    SYNC_KIND_SYNC,    /* every step, on a saved network it joins first */
    SYNC_KIND_REFRESH, /* sync mode `always`: the radar's frames, the house's reading or both, on the network
                          Wi-Fi is on already (D23: never joining) */
    SYNC_KIND_CHECK,   /* the Solar page's Check now (M6d): the Solar and Energy steps, joining first if need be */
} sync_kind_t;

/* The Solar step's request (spec §11.5); keys stay in it, never in a log. */
typedef struct {
    uint8_t source; /* solar_source_t; SOLAR_OFF: the step is skipped */
    uint8_t plane_count;
    solar_plane_t planes[SOLAR_PLANES_MAX];
    uint8_t losses_pct;
    float inverter_w;              /* our model's limit; 0 for none */
    char key[SOLAR_KEY_MAX];       /* Forecast.Solar's, optional; Solcast's */
    char sites[2][SOLAR_KEY_MAX];  /* Solcast's site ids; "" for none */
    uint32_t solcast_asked;        /* when Solcast was last asked (UTC): its budget */
} sync_solar_req_t;

/* The Energy step's request (spec §11.6): SolaX Cloud by its Token ID, or by its Developer API (D37). Keys and the
 * access token stay in it, never in a log. */
typedef struct {
    uint8_t source;                    /* settings_energy_source_t; SETTINGS_ENERGY_OFF: the step is skipped */
    char token[ENERGY_KEY_MAX];        /* the Token ID */
    char sn[ENERGY_KEY_MAX];           /* the dongle's registration number */
    uint8_t region;                    /* energy_dev_region_t */
    char client_id[ENERGY_KEY_MAX];    /* the Developer API's application */
    char client_secret[ENERGY_KEY_MAX];
    char access[ENERGY_DEV_TOKEN_MAX]; /* the access token the app keeps; "" for none */
    uint32_t access_until;             /* when it ends (UTC) */
    energy_dev_site_t site;            /* the plant and its devices found before; plant_id "" for none yet */
} sync_energy_req_t;

typedef struct {
    uint8_t kind;  /* sync_kind_t */
    uint8_t steps; /* settings_step_t bits: the data steps that run (D35); the time always does */
    uint32_t now;  /* the clock at the start (UTC) while it is valid, else 0 */
    int32_t lat_e4, lon_e4;
    char ntp[SETTINGS_NTP_MAX][SETTINGS_HOST_LEN];
    radar_fetch_req_t radar; /* its deadline is the sync's to set */
    bool refresh_radar;      /* SYNC_KIND_REFRESH: the radar's frames */
    bool refresh_energy;     /* SYNC_KIND_REFRESH: the house's reading (D36) */
    sync_solar_req_t solar;
    sync_energy_req_t energy;
    /* M7: the MQTT session on the sync's task within budget_ms, ESP_OK or why not in `detail`; NULL while MQTT is
     * off, which skips the step */
    esp_err_t (*mqtt)(int budget_ms, char *detail, size_t size);
} sync_request_t;

typedef struct {
    uint8_t kind;                                   /* the request's */
    uint8_t result[SYNC_STEP_COUNT];                /* sync_step_result_t */
    char detail[SYNC_STEP_COUNT][SYNC_DETAIL_LEN];  /* why a step failed or kept: "not found", "HTTP 503", "kept" */
    int64_t ntp_utc_us;  /* SYNC_STEP_TIME: the true time (UTC µs) at the monotonic instant below */
    int64_t ntp_mono_us; /* esp_timer_get_time() */
    int64_t ntp_delay_us;
    ds_weather_t weather; /* SYNC_STEP_WEATHER; `fetched` is the app's to set */
    ds_air_t air;         /* SYNC_STEP_AIR */
    radar_fetch_result_t radar; /* SYNC_STEP_RADAR: its frames are the app's to take or free */
    const solar_acc_t *solar;   /* SYNC_STEP_SOLAR ok: the reply, for the app to finish into its forecast */
    uint32_t solcast_asked;     /* when the step asked Solcast (UTC); 0: it didn't */
    uint8_t solcast_sites;      /* Solcast's sites, for the forecast's freshness */
    energy_reading_t energy;    /* SYNC_STEP_ENERGY ok: the reading */
    char energy_access[ENERGY_DEV_TOKEN_MAX]; /* the Developer API's new access token, for the app to keep; "" if none */
    uint32_t energy_access_until;
    bool energy_access_dropped;    /* the kept token was refused and no new one came: the app forgets it */
    energy_dev_site_t energy_site; /* the plant and its devices when the step found them anew; plant_id "" otherwise */
} sync_report_t;

/* Starts a sync; `done` runs on the sync task when it ends, and must hand the report to the app task
 * without blocking (it stays valid until the next sync starts, and the app takes its radar frames).
 * ESP_ERR_INVALID_STATE while one runs. */
esp_err_t sync_start(const sync_request_t *req, void (*done)(sync_report_t *report));
bool sync_running(void);
/* The step running now, for the progress the web UI shows; SYNC_STEP_COUNT when none runs. */
sync_step_t sync_step(void);
const char *sync_step_name(sync_step_t step); /* "wifi", "time", "weather", "air", "radar", "solar", "energy", "mqtt" */
/* The Developer API's last data replies, for `energy raw` (D37): the inverter's, the battery's and the meter's
 * real-time data and the month's statistics; "" for none. Never the token's. Read them while no sync runs. */
#define SYNC_ENERGY_RAW_COUNT 4
const char *sync_energy_raw(int which);
