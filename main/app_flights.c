#include <string.h>

#include "adsb_task.h"
#include "app_internal.h"
#include "esp_log.h"

/* The flight radar on the device (spec §11.3): its task polls while sync mode `always` is on the
 * network and the Flights view is on screen, outside quiet hours and nights (D22); the app task keeps
 * the last good poll's report for the view. */

static const char *TAG = "app_flights";

static adsb_report_t *s_last; /* the last good poll's report; the app task's */
static time_t s_updated;      /* when it came (UTC); 0 = none since the view came up */
static bool s_failed;         /* the last poll failed */
static adsb_task_req_t s_asked; /* what the task polls now */
static bool s_on;

static void apply(void *arg) /* on the app task */
{
    adsb_report_t *r = arg;
    if (!s_on) { /* the view went away meanwhile */
        adsb_report_free(r);
        return;
    }
    s_failed = !r->ok;
    if (r->ok) {
        adsb_report_free(s_last);
        s_last = r;
        s_updated = time(NULL);
    } else {
        adsb_report_free(r);
    }
    app_ui_render();
}

static void done(adsb_report_t *report) /* on the adsb task */
{
    if (app_post(apply, report) != ESP_OK) {
        adsb_report_free(report); /* the app is busy: the next poll comes soon */
    }
}

/* The Flights map as the view draws it, with the settings' filters. */
static void request(adsb_task_req_t *out)
{
    const settings_t *set = app_settings();
    memset(out, 0, sizeof(*out));
    out->range_km = set->fl_range_km;
    out->filter = (adsb_filter_t){ .lat = set->fl_lat_e4 / 1e4, .lon = set->fl_lon_e4 / 1e4,
                                   .min_alt_ft = set->fl_min_alt_ft, .ground = set->fl_ground, .max = set->fl_max };
    map_view_init(&out->filter.view, set->fl_lat_e4, set->fl_lon_e4,
                  map_zoom_for_range(set->fl_lat_e4, set->fl_range_km * 1000.0, UI_FLIGHTS_MAP_H / 2), 400,
                  UI_FLIGHTS_MAP_H);
}

/* The same poll as the task's: compared field by field, as a struct's padding may differ. */
static bool same(const adsb_task_req_t *a, const adsb_task_req_t *b)
{
    return a->range_km == b->range_km && a->filter.lat == b->filter.lat && a->filter.lon == b->filter.lon &&
           a->filter.min_alt_ft == b->filter.min_alt_ft && a->filter.ground == b->filter.ground &&
           a->filter.max == b->filter.max; /* the view follows from the centre and the range */
}

void app_flights_tick(bool shown)
{
    bool wanted = shown && app_sync_lan_ui(); /* `always` on the network: not in quiet hours or a night */
    if (!wanted) {
        if (s_on) {
            adsb_task_stop();
            s_on = false;
            ESP_LOGI(TAG, "off");
        }
        return;
    }
    adsb_task_req_t req;
    request(&req);
    if (s_on && same(&req, &s_asked)) {
        return;
    }
    if (!s_on) { /* aircraft from an earlier visit are old: none until the first poll */
        adsb_report_free(s_last);
        s_last = NULL;
        s_updated = 0;
        s_failed = false;
    }
    if (adsb_task_start(&req, done) == ESP_OK) {
        s_asked = req;
        s_on = true;
    }
}

void app_flights_fill(ui_radar_t *ui)
{
    ui->fl_updated = s_updated;
    ui->fl_failed = s_failed;
    ui->aircraft = s_last != NULL ? &s_last->list : NULL;
    ui->route = s_last != NULL && s_last->has_route ? &s_last->route : NULL;
}

void app_flights_status(app_flights_status_t *out)
{
    *out = (app_flights_status_t){ .on = s_on, .updated = s_updated, .failed = s_failed,
                                   .aircraft = s_last != NULL ? (uint8_t)s_last->list.count : 0 };
}
