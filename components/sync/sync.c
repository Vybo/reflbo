#include "sync.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "energy_dev.h"
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
#define MQTT_STEP_MS 15000   /* spec §9.3 step 8 */

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
/* Why a request failed, for its step's detail. */
static void http_detail(esp_err_t err, int status, char detail[SYNC_DETAIL_LEN])
{
    if (status != 0 && status != 200) {
        snprintf(detail, SYNC_DETAIL_LEN, "HTTP %d", status);
    } else {
        snprintf(detail, SYNC_DETAIL_LEN, "%s", err == ESP_ERR_TIMEOUT        ? "timeout"
                                                : err == ESP_ERR_INVALID_SIZE ? "too big"
                                                                              : esp_err_to_name(err));
    }
}

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
    http_detail(err, *status, detail);
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

/* SolaX Cloud by its Token ID (spec §11.6). */
static void step_energy_token(void)
{
    const sync_energy_req_t *q = &s_req.energy;
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
    if (energy_reading_ahead(&s_report.energy, sync_now())) {
        failed(SYNC_STEP_ENERGY, "upload time ahead"); /* it would hold the day's totals until its own day */
        return;
    }
    s_report.result[SYNC_STEP_ENERGY] = SYNC_STEP_OK;
}

/* ---- SolaX Cloud's Developer API (D37) ---- */

#define DEV_RAW_MAX 2048 /* each last data reply kept for `energy raw` */
EXT_RAM_BSS_ATTR static char s_dev_raw[SYNC_ENERGY_RAW_COUNT][DEV_RAW_MAX];
static fetch_session_t s_dev; /* one connection for the step's requests, all to one host */
static bool s_dev_refused;    /* SolaX refused the last request: a kept token or plant may be stale */

const char *sync_energy_raw(int which)
{
    return which >= 0 && which < SYNC_ENERGY_RAW_COUNT ? s_dev_raw[which] : "";
}

/* A request of the Developer API's: a GET of `path`, or with `body` a POST of it, JSON or the token's form; false
 * with the detail. */
static bool dev_request(const char *path, const char *body, bool json, const char *bearer,
                        char detail[SYNC_DETAIL_LEN])
{
    s_dev_refused = false; /* only a refusal retries: never a timeout or the radio's budget */
    char url[ENERGY_DEV_URL_MAX];
    if (energy_dev_url(url, sizeof(url), (energy_dev_region_t)s_req.energy.region, path) == 0) {
        snprintf(detail, SYNC_DETAIL_LEN, "bad region");
        return false;
    }
    int budget = sync_budget_ms(esp_timer_get_time(), s_deadline_us, HTTP_TIMEOUT_MS);
    if (budget == 0) {
        snprintf(detail, SYNC_DETAIL_LEN, "timeout");
        return false;
    }
    size_t len;
    int status = 0;
    s_dev.bearer = bearer;
    const char *type = json ? "application/json" : "application/x-www-form-urlencoded";
    esp_err_t err = body == NULL ? fetch_get(&s_dev, url, s_body, sizeof(s_body), &len, budget, &status)
                                 : fetch_post(&s_dev, url, type, body, s_body, sizeof(s_body), &len, budget, &status);
    if (err != ESP_OK) {
        s_dev_refused = energy_dev_refused(status, s_body, strlen(s_body));
        http_detail(err, status, detail);
    }
    return err == ESP_OK;
}

/* A reply's parser's result: one that didn't parse was refused when SolaX's own code says so. */
static bool dev_parsed(bool ok)
{
    if (!ok) {
        s_dev_refused = energy_dev_refused(200, s_body, strlen(s_body));
    }
    return ok;
}

/* The access token for the application's client credentials, into the request for this step and the report for the
 * app to keep; never logged. */
static bool dev_login(sync_energy_req_t *q, uint32_t now, char detail[SYNC_DETAIL_LEN])
{
    char body[ENERGY_DEV_BODY_MAX];
    uint32_t life = 0;
    if (energy_dev_token_body(body, sizeof(body), q->client_id, q->client_secret) == 0) {
        snprintf(detail, SYNC_DETAIL_LEN, "bad client id");
        return false;
    }
    bool ok = dev_request(ENERGY_DEV_TOKEN_PATH, body, false, NULL, detail) &&
              energy_dev_parse_token(s_body, strlen(s_body), q->access, sizeof(q->access), &life, detail,
                                     SYNC_DETAIL_LEN);
    if (!ok) {
        q->access[0] = '\0';
        return false;
    }
    q->access_until = now != 0 ? now + life : 0;
    snprintf(s_report.energy_access, sizeof(s_report.energy_access), "%s", q->access);
    s_report.energy_access_until = q->access_until;
    return true;
}

/* The plant, the first residential one or else the first commercial one, and its inverter, battery and meter. */
static bool dev_find(sync_energy_req_t *q, char detail[SYNC_DETAIL_LEN])
{
    energy_dev_site_t site;
    memset(&site, 0, sizeof(site));
    char path[ENERGY_DEV_URL_MAX];
    for (int business = 1; business <= 4 && site.plant_id[0] == '\0'; business += 3) {
        site.business = (uint8_t)business;
        energy_dev_plants_path(path, sizeof(path), business);
        if (!dev_request(path, NULL, false, q->access, detail) ||
            !dev_parsed(energy_dev_parse_plant(s_body, strlen(s_body), &site, detail, SYNC_DETAIL_LEN))) {
            return false;
        }
    }
    if (site.plant_id[0] == '\0') {
        snprintf(detail, SYNC_DETAIL_LEN, "no plant");
        return false;
    }
    for (int d = ENERGY_DEV_INVERTER; d <= ENERGY_DEV_METER; d++) {
        if (energy_dev_devices_path(path, sizeof(path), &site, (energy_dev_device_t)d) == 0 ||
            !dev_request(path, NULL, false, q->access, detail) ||
            !dev_parsed(energy_dev_parse_device(s_body, strlen(s_body), (energy_dev_device_t)d, &site, detail,
                                                SYNC_DETAIL_LEN))) {
            return false;
        }
    }
    if (site.sn[0][0] == '\0') {
        snprintf(detail, SYNC_DETAIL_LEN, "no inverter");
        return false;
    }
    ESP_LOGI(TAG, "SolaX plant %s: inverter %s, battery %s, meter %s", site.plant_id, site.sn[0],
             site.sn[1][0] ? site.sn[1] : "none", site.sn[2][0] ? site.sn[2] : "none");
    q->site = site;
    s_report.energy_site = site;
    return true;
}

/* The devices' real-time values and today's row of the month's statistics, into the report's reading. Only the
 * inverter's failure fails it: without the battery's or the meter's values, or today's totals, the reading stands
 * with what came. */
static bool dev_read(sync_energy_req_t *q, uint32_t now, char detail[SYNC_DETAIL_LEN])
{
    static const char *const k_names[] = { "inverter", "battery", "meter" };
    energy_dev_now_t vals;
    memset(&vals, 0, sizeof(vals));
    char path[ENERGY_DEV_URL_MAX], why[SYNC_DETAIL_LEN] = "";
    for (int d = ENERGY_DEV_INVERTER; d <= ENERGY_DEV_METER; d++) {
        char *err = d == ENERGY_DEV_INVERTER ? detail : why;
        if (q->site.sn[d - 1][0] == '\0') {
            continue;
        }
        bool ok = energy_dev_realtime_path(path, sizeof(path), &q->site, (energy_dev_device_t)d) > 0 &&
                  dev_request(path, NULL, false, q->access, err);
        if (ok) {
            util_json_text(s_dev_raw[d - 1], DEV_RAW_MAX, s_body);
            ok = dev_parsed(energy_dev_parse_realtime(s_body, strlen(s_body), (energy_dev_device_t)d, q->site.business,
                                                      &vals, err, SYNC_DETAIL_LEN));
        }
        if (!ok && d == ENERGY_DEV_INVERTER) {
            return false;
        }
        if (!ok) {
            ESP_LOGW(TAG, "energy: the %s: %s", k_names[d - 1], why);
        }
    }
    energy_dev_today_t today = { 0 };
    char body[ENERGY_DEV_BODY_MAX];
    time_t t = now;
    struct tm lt;
    if (now != 0 && localtime_r(&t, &lt) != NULL &&
        energy_dev_stats_body(body, sizeof(body), &q->site, lt.tm_year + 1900, lt.tm_mon + 1) > 0 &&
        dev_request(ENERGY_DEV_STATS_PATH, body, true, q->access, why)) {
        util_json_text(s_dev_raw[3], DEV_RAW_MAX, s_body);
        if (!energy_dev_parse_today(s_body, strlen(s_body), lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, &today,
                                    why, sizeof(why))) {
            ESP_LOGW(TAG, "energy: today's totals: %s", why);
        }
    }
    if (!energy_dev_reading(&vals, &today, now, &s_report.energy)) {
        s_dev_refused = false; /* answered, without the values: another login wouldn't bring them */
        snprintf(detail, SYNC_DETAIL_LEN, "no data");
        return false;
    }
    if (vals.at == 0) {
        ESP_LOGI(TAG, "energy: no dataTime in SolaX's replies; the sync's clock dates the reading");
    }
    return true;
}

/* A token kept from before and a plant found before are used as they are. When SolaX refuses what follows (an
 * answer, not a timeout), a kept token is renewed and the plant found again, once. */
static bool dev_steps(sync_energy_req_t *q, uint32_t now, char detail[SYNC_DETAIL_LEN])
{
    bool kept = q->access[0] != '\0' && energy_dev_token_fresh(q->access_until, now);
    bool found = q->site.plant_id[0] != '\0';
    if (!kept && !dev_login(q, now, detail)) {
        return false;
    }
    if ((found || dev_find(q, detail)) && dev_read(q, now, detail)) {
        return true;
    }
    if (!(kept || found) || !s_dev_refused) {
        return false;
    }
    if (kept) {
        s_report.energy_access_dropped = true;
        if (!dev_login(q, now, detail)) {
            return false;
        }
    }
    q->site.plant_id[0] = '\0';
    return dev_find(q, detail) && dev_read(q, now, detail);
}

/* SolaX Cloud by its Developer API (D37). */
static void step_energy_dev(void)
{
    sync_energy_req_t *q = &s_req.energy;
    char detail[SYNC_DETAIL_LEN] = "";
    uint32_t now = sync_now();
    if (q->client_id[0] == '\0' || q->client_secret[0] == '\0') {
        failed(SYNC_STEP_ENERGY, q->client_id[0] == '\0' ? "no client id" : "no client secret");
        return;
    }
    memset(s_dev_raw, 0, sizeof(s_dev_raw));
    bool ok = dev_steps(q, now, detail);
    fetch_close(&s_dev);
    if (!ok) {
        failed(SYNC_STEP_ENERGY, detail);
    } else if (energy_reading_ahead(&s_report.energy, now)) {
        failed(SYNC_STEP_ENERGY, "upload time ahead");
    } else {
        s_report.result[SYNC_STEP_ENERGY] = SYNC_STEP_OK;
    }
}

/* The house's energy from mapped MQTT values (D40, spec §12.11): built on the app task from what a sync's MQTT
 * session brought, or for the Solar page's check from what the app has. No request of its own. */
static void step_energy_mqtt(void)
{
    if (s_req.energy_mqtt == NULL) {
        failed(SYNC_STEP_ENERGY, "MQTT off");
        return;
    }
    if (s_req.kind == SYNC_KIND_SYNC && s_report.result[SYNC_STEP_MQTT] != SYNC_STEP_OK) {
        failed(SYNC_STEP_ENERGY, "no session");
        return;
    }
    char detail[SYNC_DETAIL_LEN] = "";
    if (s_req.energy_mqtt(&s_report.energy, detail, sizeof(detail))) {
        s_report.result[SYNC_STEP_ENERGY] = SYNC_STEP_OK;
        s_report.energy_local = true; /* the time step's true time may move the clock it was dated by */
    } else {
        failed(SYNC_STEP_ENERGY, detail[0] != '\0' ? detail : "no data");
    }
}

/* The house's energy (spec §11.6): one reading; its failure shows but doesn't fail the sync. */
static void step_energy(void)
{
    if (!wanted(SYNC_STEP_ENERGY, SETTINGS_STEP_ENERGY)) {
        return;
    }
    if (s_req.energy.source == SETTINGS_ENERGY_SOLAX) {
        step_energy_token();
    } else if (s_req.energy.source == SETTINGS_ENERGY_SOLAX_DEV) {
        step_energy_dev();
    } else if (s_req.energy.source == SETTINGS_ENERGY_MQTT) {
        if (s_req.kind != SYNC_KIND_SYNC) {
            step_energy_mqtt(); /* a sync's comes after its MQTT step */
        }
    } else {
        skipped(SYNC_STEP_ENERGY, "no source");
    }
}

/* M7 (spec §9.3 step 8, D32): the MQTT session, while MQTT is on; its failure shows but doesn't fail the sync. */
static void step_mqtt(void)
{
    if (s_req.mqtt == NULL) {
        skipped(SYNC_STEP_MQTT, "off");
        return;
    }
    int budget = sync_budget_ms(esp_timer_get_time(), s_deadline_us, MQTT_STEP_MS);
    if (budget == 0) {
        failed(SYNC_STEP_MQTT, "timeout");
        return;
    }
    char detail[SYNC_DETAIL_LEN] = "";
    if (s_req.mqtt(budget, detail, sizeof(detail)) == ESP_OK) {
        s_report.result[SYNC_STEP_MQTT] = SYNC_STEP_OK;
    } else {
        failed(SYNC_STEP_MQTT, detail[0] != '\0' ? detail : "failed");
    }
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
        s_step = SYNC_STEP_MQTT;
        step_mqtt();
        if (s_req.energy.source == SETTINGS_ENERGY_MQTT && (s_req.steps & SETTINGS_STEP_ENERGY)) {
            s_step = SYNC_STEP_ENERGY; /* D40: from the values the session brought */
            step_energy_mqtt();
        }
    } else {
        const char *why = err == ESP_ERR_NOT_FOUND ? "no network saved" : err == ESP_ERR_INVALID_STATE ? "Wi-Fi busy"
                                                                                                    : "not joined";
        failed(SYNC_STEP_WIFI, why);
        if (s_req.kind == SYNC_KIND_CHECK) { /* the Solar page shows why its check brought nothing */
            if (s_req.solar.source != SOLAR_OFF) {
                failed(SYNC_STEP_SOLAR, why);
            }
            if (s_req.energy.source != SETTINGS_ENERGY_OFF) {
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
                                                          "energy", "mqtt" };
    return (unsigned)step < SYNC_STEP_COUNT ? k_names[step] : "";
}
