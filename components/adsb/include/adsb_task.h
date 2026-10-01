#pragma once

#include <stdbool.h>

#include "adsb.h"
#include "esp_err.h"

/*
 * The flight radar's polling (spec §11.3): adsb.fi every 5-15 s over one kept-alive connection, and
 * the nearest aircraft's route from adsb.lol, on a task of its own. It only fetches: each report goes
 * to the app task, which shows it. Device only.
 */

#define ADSB_DETAIL_LEN 24

typedef struct {
    adsb_filter_t filter; /* the Flights map and the settings' filters */
    int range_km;         /* radar.flights.range_km: the poll interval follows it */
} adsb_task_req_t;

typedef struct {
    bool ok;
    char detail[ADSB_DETAIL_LEN]; /* why the poll failed: "HTTP 429", "too big" */
    adsb_list_t list;             /* when ok: the nearest first */
    bool has_route;
    adsb_route_t route; /* the nearest one's, when the cache has it */
} adsb_report_t;

/* Starts polling, or changes what the running task polls; `done` runs on the adsb task after each
 * poll and hands the report to the app task, which frees it with adsb_report_free(). */
esp_err_t adsb_task_start(const adsb_task_req_t *req, void (*done)(adsb_report_t *report));
/* Stops after the poll in flight. */
void adsb_task_stop(void);
bool adsb_task_running(void);
void adsb_report_free(adsb_report_t *report);
