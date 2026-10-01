#include "timekeeping.h"

#include <stdlib.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "pcf85063.h"
#include "timekeeping_sync.h"

static const char *TAG = "timekeeping";

#define NVS_KEY_TRIM "rtc_trim"

static bool s_valid;
static rtc_trim_t s_trim;
static bool s_trim_up;

esp_err_t timekeeping_init(const char *tz_posix)
{
    ESP_RETURN_ON_FALSE(setenv("TZ", tz_posix, 1) == 0, ESP_ERR_NO_MEM, TAG, "TZ");
    tzset();
    return ESP_OK;
}

esp_err_t timekeeping_load_from_rtc(bool at_edge)
{
    time_t utc;
    bool valid;
    ESP_RETURN_ON_ERROR(pcf85063_read(&utc, &valid), TAG, "RTC read");
    struct timeval now;
    gettimeofday(&now, NULL);
    if (timekeeping_rtc_resync(now.tv_sec, utc, at_edge)) {
        struct timeval tv = { .tv_sec = utc };
        settimeofday(&tv, NULL);
    }
    s_valid = valid;
    return ESP_OK;
}

bool timekeeping_valid(void)
{
    return s_valid;
}

static void trim_save(void)
{
    uint8_t rec[TRIM_RECORD_LEN];
    timekeeping_trim_pack(&s_trim, rec);
    nvs_handle_t nvs;
    if (nvs_open("sys", NVS_READWRITE, &nvs) == ESP_OK) {
        if (nvs_set_blob(nvs, NVS_KEY_TRIM, rec, sizeof(rec)) == ESP_OK) {
            nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
}

esp_err_t timekeeping_trim_start(void)
{
    timekeeping_trim_init(&s_trim);
    nvs_handle_t nvs;
    if (nvs_open("sys", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t rec[TRIM_RECORD_LEN];
        size_t len = sizeof(rec);
        if (nvs_get_blob(nvs, NVS_KEY_TRIM, rec, &len) == ESP_OK && !timekeeping_trim_unpack(&s_trim, rec, len)) {
            timekeeping_trim_init(&s_trim);
        }
        nvs_close(nvs);
    }
    s_trim_up = true;
    ESP_LOGI(TAG, "RTC trim %d steps", s_trim.offset);
    return pcf85063_set_offset(s_trim.offset); /* the chip loses it with its power (D9) */
}

const rtc_trim_t *timekeeping_trim(void)
{
    return &s_trim;
}

static int64_t clock_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

esp_err_t timekeeping_apply_true_time(int64_t true_utc_us, int64_t mono_us, int64_t *moved_ms)
{
    if (!s_trim_up) {
        timekeeping_trim_start();
    }
    int64_t error_ms = 0; /* the RTC against the system clock, while the RTC still keeps its time */
    bool measured = s_valid && pcf85063_error_ms(&error_ms) == ESP_OK;
    int64_t true_now = true_utc_us + (esp_timer_get_time() - mono_us);
    int64_t ahead_ms = (clock_us() - true_now) / 1000; /* the system clock against the truth */
    struct timeval tv = { .tv_sec = (time_t)(true_now / 1000000), .tv_usec = (suseconds_t)(true_now % 1000000) };
    settimeofday(&tv, NULL);
    *moved_ms = -ahead_ms;
    if (measured && timekeeping_trim_measure(&s_trim, true_now / 1000, error_ms + ahead_ms)) {
        ESP_LOGI(TAG, "RTC drift %ld ppb: trim now %d steps", (long)s_trim.drift_ppb, s_trim.offset);
        ESP_RETURN_ON_ERROR(pcf85063_set_offset(s_trim.offset), TAG, "RTC offset");
    } else if (measured) {
        ESP_LOGI(TAG, "RTC off by %lld ms", (long long)(error_ms + ahead_ms));
    }
    int64_t set_at_ms = 0;
    ESP_RETURN_ON_ERROR(pcf85063_write_precise(&set_at_ms), TAG, "RTC write");
    timekeeping_trim_set(&s_trim, set_at_ms);
    trim_save();
    s_valid = true;
    return ESP_OK;
}

esp_err_t timekeeping_set_utc(time_t utc)
{
    ESP_RETURN_ON_ERROR(pcf85063_write(utc), TAG, "RTC write");
    if (s_trim_up && s_trim.set_at_ms != 0) {
        timekeeping_trim_forget(&s_trim);
        trim_save();
    }
    struct timeval tv = { .tv_sec = utc };
    settimeofday(&tv, NULL);
    s_valid = true;
    ESP_LOGI(TAG, "time set to %lld", (long long)utc);
    return ESP_OK;
}
