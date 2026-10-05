#include "sync.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fetch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "netmgr.h"
#include "sync_ntp.h"
#include "sync_plan.h"
#include "util_json.h"
#include "weather.h"

static const char *TAG = "sync";

#define TASK_STACK 10240 /* TLS on this task */
#define TASK_PRIORITY 3  /* below netmgr (4) and the app (5) */
#define NTP_TIMEOUT_MS 5000 /* spec §9.3 */
#define HTTP_TIMEOUT_MS 10000
#define JOIN_MAX_MS 25000 /* three 8 s attempts; the rest of the 45 s is the steps' (spec §9.3) */
#define BODY_MAX (32 * 1024) /* the largest reply, Solcast's 72 h, is about 18 KB (M6d) */
#define RADAR_STEP_MS 10000 /* spec §9.3 */
#define REFRESH_MAX_MS 30000 /* a radar-only refresh, the last hour's 12 frames at most (D28) */

static volatile bool s_running;
static volatile uint8_t s_step = SYNC_STEP_COUNT;
EXT_RAM_BSS_ATTR static sync_request_t s_req; /* in PSRAM, as the keys and the reports grew (M6d) */
static void (*s_done)(sync_report_t *report);
EXT_RAM_BSS_ATTR static sync_report_t s_report;
EXT_RAM_BSS_ATTR static solar_acc_t s_solar; /* the Solar step's reply, the app's until the next sync */
static int64_t s_deadline_us; /* esp_timer: SYNC_RADIO_MAX_MS after the start */
EXT_RAM_BSS_ATTR static char s_body[BODY_MAX];

static void failed(sync_step_t step, const char *detail)
{
    s_report.result[step] = SYNC_STEP_FAILED;
    util_json_text(s_report.detail[step], SYNC_DETAIL_LEN, detail); /* between characters */
    ESP_LOGW(TAG, "%s: %s", sync_step_name(step), detail);
}

/* It ran, and kept what it had (spec §11.5): Solcast's budget, a provider's 429. */
static void kept(sync_step_t step, const char *detail)
{
    s_report.result[step] = SYNC_STEP_KEPT;
    util_json_text(s_report.detail[step], SYNC_DETAIL_LEN, detail); /* between characters */
    ESP_LOGI(TAG, "%s: kept (%s)", sync_step_name(step), detail);
}

/* Not run: switched off ("off", D35) or without a source ("no source"). */
static void skipped(sync_step_t step, const char *detail)
{
    s_report.result[step] = SYNC_STEP_NOT_RUN;
    util_json_text(s_report.detail[step], SYNC_DETAIL_LEN, detail); /* between characters */
}

static bool wanted(sync_step_t step, settings_step_t bit)
{
    if (s_req.steps & bit) {
        return true;
    }
    skipped(step, "off"); /* no requests, and its data age out as usual (spec §9.3) */
    return false;
}

static int64_t clock_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* One server: the true time (UTC µs) at the monotonic instant `*mono_us`, by one request and its
 * answer within the timeout. */
static bool ask_ntp(const char *host, int timeout_ms, int64_t *true_us, int64_t *delay_us, int64_t *mono_us)
{
    const struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, "123", &hints, &res) != 0 || res == NULL) {
        return false;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bool ok = false;
    if (sock >= 0) {
        struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        uint8_t packet[SYNC_NTP_PACKET];
        int64_t t1 = clock_us();
        sync_ntp_request(packet, t1);
        if (sendto(sock, packet, sizeof(packet), 0, res->ai_addr, res->ai_addrlen) == sizeof(packet)) {
            int n = recv(sock, packet, sizeof(packet), 0);
            int64_t t4 = clock_us();
            *mono_us = esp_timer_get_time();
            int64_t offset;
            ok = n > 0 && sync_ntp_offset(packet, (size_t)n, t1, t4, &offset, delay_us);
            *true_us = t4 + offset;
        }
        close(sock);
    }
    freeaddrinfo(res);
    return ok;
}

static void step_time(void)
{
    for (int i = 0; i < SETTINGS_NTP_MAX; i++) {
        int64_t true_us, delay, mono;
        int budget = sync_budget_ms(esp_timer_get_time(), s_deadline_us, NTP_TIMEOUT_MS);
        if (budget == 0) {
            failed(SYNC_STEP_TIME, "timeout");
            return;
        }
        if (s_req.ntp[i][0] != '\0' && ask_ntp(s_req.ntp[i], budget, &true_us, &delay, &mono)) {
            s_report.result[SYNC_STEP_TIME] = SYNC_STEP_OK;
            s_report.ntp_utc_us = true_us;
            s_report.ntp_mono_us = mono;
            s_report.ntp_delay_us = delay;
            ESP_LOGI(TAG, "time from %s, round trip %lld ms", s_req.ntp[i], (long long)(delay / 1000));
            return;
        }
    }
    failed(SYNC_STEP_TIME, "no answer");
}

/* One GET into s_body within its share of the radio's 45 s, with `bearer` for Solcast; false with the reason in
 * `detail` ("HTTP 401", "timeout") and the status in `*status` (0 when no reply came). */
static bool get(const char *url, const char *bearer, int *status, char detail[SYNC_DETAIL_LEN])
{
    size_t len;
    *status = 0;
    int budget = sync_budget_ms(esp_timer_get_time(), s_deadline_us, HTTP_TIMEOUT_MS);
    if (budget == 0) {
        snprintf(detail, SYNC_DETAIL_LEN, "timeout"); /* the radio's 45 s are up (spec §9.3) */
        return false;
    }
    fetch_session_t session = { .bearer = bearer };
    esp_err_t err = fetch_get(&session, url, s_body, sizeof(s_body), &len, budget, status);
    fetch_close(&session);
    if (err == ESP_OK) {
        return true;
    }
    if (*status != 0 && *status != 200) {
        snprintf(detail, SYNC_DETAIL_LEN, "HTTP %d", *status);
    } else {
        snprintf(detail, SYNC_DETAIL_LEN, "%s", err == ESP_ERR_TIMEOUT        ? "timeout"
                                                : err == ESP_ERR_INVALID_SIZE ? "too big"
                                                                              : esp_err_to_name(err));
    }
    return false;
}

static bool fetch(sync_step_t step, const char *url)
{
    int status;
    char detail[SYNC_DETAIL_LEN];
    if (get(url, NULL, &status, detail)) {
        return true;
    }
    failed(step, detail);
    return false;
}

static void step_weather(void)
{
    if (!wanted(SYNC_STEP_WEATHER, SETTINGS_STEP_WEATHER)) {
        return;
    }
    char url[WEATHER_URL_MAX], err[SYNC_DETAIL_LEN];
    weather_forecast_url(url, sizeof(url), s_req.lat_e4, s_req.lon_e4);
    if (!fetch(SYNC_STEP_WEATHER, url)) {
        return;
    }
    if (weather_parse_forecast(s_body, strlen(s_body), &s_report.weather, err, sizeof(err))) {
        s_report.result[SYNC_STEP_WEATHER] = SYNC_STEP_OK;
    } else {
        failed(SYNC_STEP_WEATHER, err);
    }
}

static void step_air(void)
{
    if (!wanted(SYNC_STEP_AIR, SETTINGS_STEP_AIR)) {
        return;
    }
    char url[WEATHER_URL_MAX], err[SYNC_DETAIL_LEN];
    weather_air_url(url, sizeof(url), s_req.lat_e4, s_req.lon_e4);
    if (!fetch(SYNC_STEP_AIR, url)) {
        return;
    }
    if (weather_parse_air(s_body, strlen(s_body), &s_report.air, err, sizeof(err))) {
        s_report.result[SYNC_STEP_AIR] = SYNC_STEP_OK;
    } else {
        failed(SYNC_STEP_AIR, err);
    }
}

/* The sync's own time: its NTP answer where the time step worked, as the app sets the clock only after the
 * sync; else the clock if it is valid; else 0. */
static uint32_t sync_now(void)
{
    if (s_report.result[SYNC_STEP_TIME] == SYNC_STEP_OK) {
        return (uint32_t)((s_report.ntp_utc_us + esp_timer_get_time() - s_report.ntp_mono_us) / 1000000);
    }
    return s_req.now;
}

static void step_radar(int max_ms)
{
    if (!wanted(SYNC_STEP_RADAR, SETTINGS_STEP_RADAR)) {
        return;
    }
    int budget = sync_budget_ms(esp_timer_get_time(), s_deadline_us, max_ms);
    if (budget == 0) {
        failed(SYNC_STEP_RADAR, "timeout");
        return;
    }
    radar_fetch_req_t req = s_req.radar;
    req.deadline_us = esp_timer_get_time() + (int64_t)budget * 1000;
    if (s_report.result[SYNC_STEP_TIME] == SYNC_STEP_OK) { /* the app sets the clock only after the sync */
        req.now = sync_now();
    }
    if (radar_fetch(&req, &s_report.radar) == ESP_OK) {
        s_report.result[SYNC_STEP_RADAR] = SYNC_STEP_OK;
    } else {
        failed(SYNC_STEP_RADAR, s_report.radar.detail);
    }
}

/* A reply that failed: a 429 keeps the forecast and doesn't fail the sync (spec §9.3, D35). */
static void solar_failed(int status, const char *detail)
{
    if (status == 429) {
        kept(SYNC_STEP_SOLAR, detail);
    } else {
        failed(SYNC_STEP_SOLAR, detail);
    }
}

/* One provider's reply into s_solar, or the step's failure. */
static bool solar_reply(const char *url, const char *bearer, const solar_plane_t *plane)
{
    const sync_solar_req_t *q = &s_req.solar;
    int status;
    char detail[SYNC_DETAIL_LEN];
    if (!get(url, bearer, &status, detail)) {
        solar_failed(status, detail);
        return false;
    }
    size_t n = strlen(s_body);
    bool ok = q->source == SOLAR_OPEN_METEO     ? solar_parse_open_meteo(s_body, n, plane, q->losses_pct, &s_solar,
                                                                          detail, sizeof(detail))
              : q->source == SOLAR_FORECAST_SOLAR ? solar_parse_forecast_solar(s_body, n, &s_solar, detail,
                                                                               sizeof(detail))
                                                  : solar_parse_solcast(s_body, n, &s_solar, detail, sizeof(detail));
    if (!ok) {
        failed(SYNC_STEP_SOLAR, detail);
    }
    return ok;
}

/* The PV forecast (spec §11.5): a request a plane at Open-Meteo, one at Forecast.Solar, one a site at Solcast
 * unless its budget says keep. */
static void step_solar(void)
{
    const sync_solar_req_t *q = &s_req.solar;
    if (!wanted(SYNC_STEP_SOLAR, SETTINGS_STEP_SOLAR)) {
        return;
    }
    if (q->source == SOLAR_OFF) {
        skipped(SYNC_STEP_SOLAR, "no source");
        return;
    }
    uint32_t now = sync_now();
    if (now == 0) {
        failed(SYNC_STEP_SOLAR, "no time"); /* the local quarter hours need the date */
        return;
    }
    solar_acc_init(&s_solar, (time_t)now);
    char url[SOLAR_URL_MAX];
    if (q->source == SOLAR_OPEN_METEO) {
        for (int i = 0; i < q->plane_count && i < SOLAR_PLANES_MAX; i++) {
            solar_open_meteo_url(url, sizeof(url), s_req.lat_e4, s_req.lon_e4, &q->planes[i]);
            if (!solar_reply(url, NULL, &q->planes[i])) {
                return;
            }
        }
        if (q->inverter_w > 0) {
            solar_acc_cap(&s_solar, q->inverter_w);
        }
    } else if (q->source == SOLAR_FORECAST_SOLAR) {
        if (solar_forecast_solar_url(url, sizeof(url), s_req.lat_e4, s_req.lon_e4, q->planes, q->plane_count,
                                     q->key) == 0) {
            failed(SYNC_STEP_SOLAR, "bad key");
            return;
        }
        if (!solar_reply(url, NULL, NULL)) {
            return;
        }
    } else {
        int sites = (q->sites[0][0] != '\0') + (q->sites[1][0] != '\0');
        s_report.solcast_sites = (uint8_t)sites;
        if (q->key[0] == '\0' || sites == 0) {
            failed(SYNC_STEP_SOLAR, q->key[0] == '\0' ? "no key" : "no site");
            return;
        }
        if (!solar_solcast_due(q->solcast_asked, sites, now)) {
            kept(SYNC_STEP_SOLAR, "kept"); /* within its 10 calls a day: the step counts as done */
            return;
        }
        s_report.solcast_asked = now; /* a failed call counts too */
        for (int i = 0; i < 2; i++) {
            if (q->sites[i][0] == '\0') {
                continue;
            }
            if (solar_solcast_url(url, sizeof(url), q->sites[i]) == 0) {
                failed(SYNC_STEP_SOLAR, "bad site");
                return;
            }
            if (!solar_reply(url, q->key, NULL)) {
                return;
            }
        }
    }
    s_report.solar = &s_solar;
    s_report.result[SYNC_STEP_SOLAR] = SYNC_STEP_OK;
}

/* The house's energy (spec §11.6): one SolaX Cloud reading; its failure shows but doesn't fail the sync. */
static void step_energy(void)
{
    const sync_energy_req_t *q = &s_req.energy;
    if (!wanted(SYNC_STEP_ENERGY, SETTINGS_STEP_ENERGY)) {
        return;
    }
    if (!q->on) {
        skipped(SYNC_STEP_ENERGY, "no source");
        return;
    }
    if (q->token[0] == '\0' || q->sn[0] == '\0') {
        failed(SYNC_STEP_ENERGY, q->token[0] == '\0' ? "no token" : "no registration number");
        return;
    }
    char url[ENERGY_URL_MAX], detail[SYNC_DETAIL_LEN];
    int status;
    if (energy_solax_url(url, sizeof(url), q->token, q->sn) == 0) {
        failed(SYNC_STEP_ENERGY, "bad token");
        return;
    }
    if (!get(url, NULL, &status, detail) ||
        !energy_parse_solax(s_body, strlen(s_body), &s_report.energy, detail, sizeof(detail))) {
        failed(SYNC_STEP_ENERGY, detail);
        return;
    }
    s_report.result[SYNC_STEP_ENERGY] = SYNC_STEP_OK;
}

/* Sync mode `always`: the radar, the house's reading or both, on the network Wi-Fi is on already (D23: never
 * joining). */
static void refresh_task(int64_t start)
{
    s_deadline_us = start + (int64_t)REFRESH_MAX_MS * 1000;
    netmgr_status_t ns;
    netmgr_status(&ns);
    if (ns.state != NETMGR_STATION || ns.ip[0] == '\0') {
        failed(SYNC_STEP_WIFI, "not joined");
        return;
    }
    s_report.result[SYNC_STEP_WIFI] = SYNC_STEP_OK;
    if (s_req.refresh_radar) {
        s_step = SYNC_STEP_RADAR;
        step_radar(REFRESH_MAX_MS);
    }
    if (s_req.refresh_energy) {
        s_step = SYNC_STEP_ENERGY;
        step_energy();
    }
}

static void sync_task(void *arg)
{
    (void)arg;
    int64_t start = esp_timer_get_time();
    s_deadline_us = start + (int64_t)SYNC_RADIO_MAX_MS * 1000;
    s_step = SYNC_STEP_WIFI;
    bool refresh = s_req.kind == SYNC_KIND_REFRESH;
    esp_err_t err = refresh ? ESP_OK : netmgr_join(JOIN_MAX_MS);
    if (refresh) {
        refresh_task(start);
    } else if (err == ESP_OK && s_req.kind == SYNC_KIND_CHECK) { /* the Solar page's Check now */
        s_report.result[SYNC_STEP_WIFI] = SYNC_STEP_OK;
        s_step = SYNC_STEP_SOLAR;
        step_solar();
        s_step = SYNC_STEP_ENERGY;
        step_energy();
    } else if (err == ESP_OK) {
        s_report.result[SYNC_STEP_WIFI] = SYNC_STEP_OK;
        s_step = SYNC_STEP_TIME; /* the steps are independent (spec §9.3): one failing skips nothing */
        step_time();
        s_step = SYNC_STEP_WEATHER;
        step_weather();
        s_step = SYNC_STEP_AIR;
        step_air();
        s_step = SYNC_STEP_RADAR;
        step_radar(RADAR_STEP_MS);
        s_step = SYNC_STEP_SOLAR;
        step_solar();
        s_step = SYNC_STEP_ENERGY;
        step_energy();
    } else {
        const char *why = err == ESP_ERR_NOT_FOUND ? "no network saved" : err == ESP_ERR_INVALID_STATE ? "Wi-Fi busy"
                                                                                                    : "not joined";
        failed(SYNC_STEP_WIFI, why);
        if (s_req.kind == SYNC_KIND_CHECK) { /* the Solar page shows why its check brought nothing */
            if (s_req.solar.source != SOLAR_OFF) {
                failed(SYNC_STEP_SOLAR, why);
            }
            if (s_req.energy.on) {
                failed(SYNC_STEP_ENERGY, why);
            }
        }
    }
    ESP_LOGI(TAG, "done in %lld ms", (long long)((esp_timer_get_time() - start) / 1000));
    s_step = SYNC_STEP_COUNT;
    s_running = false;
    s_done(&s_report);
    vTaskDelete(NULL);
}

esp_err_t sync_start(const sync_request_t *req, void (*done)(sync_report_t *report))
{
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    s_running = true;
    s_req = *req;
    s_done = done;
    memset(&s_report, 0, sizeof(s_report)); /* the last report's frames were the app's */
    s_report.kind = req->kind;
    BaseType_t ok = xTaskCreatePinnedToCore(sync_task, "sync", TASK_STACK, NULL, TASK_PRIORITY, NULL, 0);
    if (ok != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool sync_running(void)
{
    return s_running;
}

sync_step_t sync_step(void)
{
    return (sync_step_t)s_step;
}

const char *sync_step_name(sync_step_t step)
{
    static const char *const k_names[SYNC_STEP_COUNT] = { "wifi", "time", "weather", "air", "radar", "solar",
                                                          "energy" };
    return (unsigned)step < SYNC_STEP_COUNT ? k_names[step] : "";
}
