#include "timekeeping.h"

#include <stdlib.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "pcf85063.h"
#include "timekeeping_sync.h"

static const char *TAG = "timekeeping";

static bool s_valid;

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

esp_err_t timekeeping_set_utc(time_t utc)
{
    ESP_RETURN_ON_ERROR(pcf85063_write(utc), TAG, "RTC write");
    struct timeval tv = { .tv_sec = utc };
    settimeofday(&tv, NULL);
    s_valid = true;
    ESP_LOGI(TAG, "time set to %lld", (long long)utc);
    return ESP_OK;
}
