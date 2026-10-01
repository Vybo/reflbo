#include "sync.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "netmgr.h"
#include "sync_ntp.h"
#include "weather.h"
#include "weather_http.h"

static const char *TAG = "sync";

#define TASK_STACK 10240 /* TLS on this task */
#define TASK_PRIORITY 3  /* below netmgr (4) and the app (5) */
#define NTP_TIMEOUT_MS 5000 /* spec §9.3 */
#define HTTP_TIMEOUT_MS 10000
#define BODY_MAX (12 * 1024) /* the largest reply, air quality, is about 4 KB */

static volatile bool s_running;
static volatile uint8_t s_step = SYNC_STEP_COUNT;
static sync_request_t s_req;
static void (*s_done)(const sync_report_t *report);
static sync_report_t s_report;
EXT_RAM_BSS_ATTR static char s_body[BODY_MAX];

static void failed(sync_step_t step, const char *detail)
{
    s_report.result[step] = SYNC_STEP_FAILED;
    snprintf(s_report.detail[step], SYNC_DETAIL_LEN, "%s", detail);
    ESP_LOGW(TAG, "%s: %s", sync_step_name(step), detail);
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
        if (s_req.ntp[i][0] != '\0' && ask_ntp(s_req.ntp[i], NTP_TIMEOUT_MS, &true_us, &delay, &mono)) {
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

static bool fetch(sync_step_t step, const char *url)
{
    size_t len;
    int status;
    esp_err_t err = weather_http_get(url, s_body, sizeof(s_body), &len, HTTP_TIMEOUT_MS, &status);
    if (err == ESP_OK) {
        return true;
    }
    char detail[SYNC_DETAIL_LEN];
    if (status != 0 && status != 200) {
        snprintf(detail, sizeof(detail), "HTTP %d", status);
    } else {
        snprintf(detail, sizeof(detail), "%s", err == ESP_ERR_TIMEOUT ? "timeout" : esp_err_to_name(err));
    }
    failed(step, detail);
    return false;
}

static void step_weather(void)
{
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

static void sync_task(void *arg)
{
    (void)arg;
    int64_t start = esp_timer_get_time();
    s_step = SYNC_STEP_WIFI;
    esp_err_t err = netmgr_join();
    if (err == ESP_OK) {
        s_report.result[SYNC_STEP_WIFI] = SYNC_STEP_OK;
        s_step = SYNC_STEP_TIME; /* the steps are independent (spec §9.3): one failing skips nothing */
        step_time();
        s_step = SYNC_STEP_WEATHER;
        step_weather();
        s_step = SYNC_STEP_AIR;
        step_air();
    } else {
        failed(SYNC_STEP_WIFI, err == ESP_ERR_NOT_FOUND       ? "no network saved"
                               : err == ESP_ERR_INVALID_STATE ? "Wi-Fi busy"
                                                              : "not joined");
    }
    ESP_LOGI(TAG, "done in %lld ms", (long long)((esp_timer_get_time() - start) / 1000));
    s_step = SYNC_STEP_COUNT;
    s_running = false;
    s_done(&s_report);
    vTaskDelete(NULL);
}

esp_err_t sync_start(const sync_request_t *req, void (*done)(const sync_report_t *report))
{
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    s_running = true;
    s_req = *req;
    s_done = done;
    memset(&s_report, 0, sizeof(s_report));
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
    static const char *const k_names[SYNC_STEP_COUNT] = { "wifi", "time", "weather", "air" };
    return (unsigned)step < SYNC_STEP_COUNT ? k_names[step] : "";
}
