#include "adsb_task.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fetch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "adsb";

#define TASK_STACK 10240 /* TLS on this task */
#define TASK_PRIORITY 2  /* below the sync (3), netmgr (4) and the app (5) */
#define POLL_TIMEOUT_MS 10000
#define ROUTE_TIMEOUT_MS 5000
#define ROUTE_BODY_MAX 2048          /* adsb.lol's route is under 1 KB */
#define ROUTE_PAUSE_S 3600           /* after a 403 or 429 from adsb.lol */

EXT_RAM_BSS_ATTR static char s_body[ADSB_REPLY_MAX + 1];
EXT_RAM_BSS_ATTR static char s_route_body[ROUTE_BODY_MAX];
EXT_RAM_BSS_ATTR static adsb_routes_t s_routes;

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static bool s_running, s_stop;
static adsb_task_req_t s_req;
static void (*s_done)(adsb_report_t *report);
static time_t s_routes_paused_until;

void adsb_report_free(adsb_report_t *report)
{
    heap_caps_free(report);
}

/* The nearest aircraft's route: from the cache, else one lookup at adsb.lol (D27). */
static void route_for(const adsb_aircraft_t *a, adsb_report_t *r)
{
    time_t now = time(NULL);
    if (a->callsign[0] == '\0') {
        return;
    }
    const adsb_route_t *cached = adsb_routes_find(&s_routes, a->callsign, (uint32_t)now);
    char url[128];
    if (cached == NULL && now >= s_routes_paused_until &&
        adsb_route_url(url, sizeof(url), a->callsign, a->lat, a->lon)) {
        fetch_session_t lol = { 0 }; /* closed again at once: its TLS session isn't kept for the rare lookup */
        size_t len;
        int status;
        esp_err_t err = fetch_get(&lol, url, s_route_body, sizeof(s_route_body), &len, ROUTE_TIMEOUT_MS, &status);
        fetch_close(&lol);
        if (status == 403 || status == 429) {
            s_routes_paused_until = now + ROUTE_PAUSE_S; /* adsb.lol asks us to back off */
            ESP_LOGW(TAG, "routes paused: HTTP %d", status);
            return;
        }
        adsb_route_t found;
        if (err != ESP_OK || !adsb_route_parse(s_route_body, len, &found)) {
            memset(&found, 0, sizeof(found)); /* unknown, asked again in an hour */
        }
        snprintf(found.callsign, sizeof(found.callsign), "%s", a->callsign);
        found.looked_up = (uint32_t)now;
        adsb_routes_put(&s_routes, &found);
        ESP_LOGI(TAG, "route of %s: %s", a->callsign, found.known ? "known" : "unknown");
        cached = adsb_routes_find(&s_routes, a->callsign, (uint32_t)now);
    }
    if (cached != NULL && cached->known) {
        r->route = *cached;
        r->has_route = true;
    }
}

static void poll(fetch_session_t *fi, const adsb_task_req_t *req, adsb_report_t *r)
{
    char url[128];
    adsb_url(url, sizeof(url), &req->filter);
    size_t len;
    int status;
    esp_err_t err = fetch_get(fi, url, s_body, sizeof(s_body), &len, POLL_TIMEOUT_MS, &status);
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_SIZE) {
            snprintf(r->detail, sizeof(r->detail), "%s", adsb_err_name(ADSB_ERR_TOO_BIG));
        } else if (status != 0 && status != 200) {
            snprintf(r->detail, sizeof(r->detail), "HTTP %d", status);
        } else {
            snprintf(r->detail, sizeof(r->detail), "%s", err == ESP_ERR_TIMEOUT ? "timeout" : esp_err_to_name(err));
        }
        return;
    }
    adsb_err_t perr = adsb_parse(s_body, len, &req->filter, &r->list);
    r->ok = perr == ADSB_OK;
    if (!r->ok) {
        snprintf(r->detail, sizeof(r->detail), "%s", adsb_err_name(perr));
    } else if (r->list.count > 0) {
        route_for(&r->list.ac[0], r);
    }
}

static void adsb_task(void *arg)
{
    (void)arg;
    fetch_session_t fi = { 0 }; /* kept alive between polls (spec §11.3) */
    int failures = 0;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_stop) {
            s_running = false;
            s_task = NULL;
            xSemaphoreGive(s_lock);
            break;
        }
        adsb_task_req_t req = s_req;
        void (*done)(adsb_report_t *) = s_done;
        xSemaphoreGive(s_lock);
        adsb_report_t *r = heap_caps_calloc(1, sizeof(*r), MALLOC_CAP_SPIRAM);
        if (r != NULL) {
            poll(&fi, &req, r);
            time_t now = time(NULL);
            r->routes_paused_until = s_routes_paused_until > now ? (uint32_t)s_routes_paused_until : 0;
            failures = r->ok ? 0 : failures + 1;
            if (!r->ok) {
                ESP_LOGW(TAG, "poll: %s", r->detail);
            }
            done(r); /* the app task's now */
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(adsb_poll_s(req.range_km, failures) * 1000)); /* or a new request */
    }
    fetch_close(&fi);
    ESP_LOGI(TAG, "stopped");
    vTaskDelete(NULL);
}

esp_err_t adsb_task_start(const adsb_task_req_t *req, void (*done)(adsb_report_t *report))
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock");
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_req = *req;
    s_done = done;
    s_stop = false;
    esp_err_t err = ESP_OK;
    if (s_running) {
        xTaskNotifyGive(s_task); /* a poll for the new request now */
    } else if (xTaskCreatePinnedToCore(adsb_task, "adsb", TASK_STACK, NULL, TASK_PRIORITY, &s_task, 0) == pdPASS) {
        s_running = true;
        ESP_LOGI(TAG, "polling");
    } else {
        err = ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_lock);
    return err;
}

void adsb_task_stop(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_running && !s_stop) {
        s_stop = true;
        xTaskNotifyGive(s_task);
    }
    xSemaphoreGive(s_lock);
}

bool adsb_task_running(void)
{
    return s_running;
}
