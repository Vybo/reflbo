#include "power.h"

#include <stdio.h>
#include <unistd.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "util_snapshot.h"

#define STATE_MAGIC   0x72666c70u /* "rflp" */
#define STATE_VERSION 1
#define NVS_NAMESPACE "sys"
#define NVS_KEY_IDLE  "idle"
#define TEST_END_HOLD_MS 3000

static const char *TAG = "power";

typedef struct {
    util_snapshot_hdr_t hdr;
    power_stats_t stats;
    int32_t test_cycles;
    uint8_t test_mode;
    uint8_t test_ended; /* the last test cycle just ran: stay awake so the PC finds the board */
} power_state_t;

static RTC_DATA_ATTR power_state_t s_rtc; /* survives deep sleep only */
static power_idle_t s_strategy;
static power_wake_t s_boot_wake;
static int64_t s_hold_until_us;
static int64_t s_awake_since_us;

static const gpio_num_t k_wake_pins[] = { BOARD_PIN_RTC_INT, BOARD_PIN_KEY, BOARD_PIN_BOOT };

static void seal(void)
{
    util_snapshot_seal(&s_rtc, sizeof(s_rtc), STATE_MAGIC, STATE_VERSION);
}

static void count_wake(power_wake_t wake);
static power_wake_t decode_boot_wake(void);
static void finish_test(void);

esp_err_t power_init(void)
{
    s_boot_wake = decode_boot_wake();
    if (!util_snapshot_valid(&s_rtc, sizeof(s_rtc), STATE_MAGIC, STATE_VERSION)) {
        s_rtc = (power_state_t){ 0 };
        seal();
    }
    if (s_boot_wake != POWER_WAKE_COLD) {
        count_wake(s_boot_wake);
        finish_test();
    }
#if CONFIG_REFLBO_IDLE_DEFAULT_DEEP
    s_strategy = POWER_IDLE_DEEP;
#else
    s_strategy = POWER_IDLE_LIGHT;
#endif
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(nvs, NVS_KEY_IDLE, &v) == ESP_OK && v <= POWER_IDLE_DEEP) {
            s_strategy = (power_idle_t)v;
        }
        nvs_close(nvs);
    }
    s_awake_since_us = 0; /* esp_timer starts at 0 on every boot */
    ESP_RETURN_ON_ERROR(esp_sleep_cpu_pd_low_init(), TAG, "CPU power-down in light sleep");
    return ESP_OK;
}

static power_wake_t decode_boot_wake(void)
{
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        return POWER_WAKE_COLD;
    }
    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & BIT(ESP_SLEEP_WAKEUP_EXT1)) {
        uint64_t pins = esp_sleep_get_ext1_wakeup_status();
        if (pins & BIT64(BOARD_PIN_KEY)) {
            return POWER_WAKE_KEY;
        }
        if (pins & BIT64(BOARD_PIN_BOOT)) {
            return POWER_WAKE_BOOT;
        }
        if (pins & BIT64(BOARD_PIN_RTC_INT)) {
            return POWER_WAKE_RTC;
        }
    }
    return causes & BIT(ESP_SLEEP_WAKEUP_TIMER) ? POWER_WAKE_TIMER : POWER_WAKE_OTHER;
}

power_wake_t power_boot_wake(void)
{
    return s_boot_wake;
}

const char *power_wake_name(power_wake_t wake)
{
    static const char *const names[POWER_WAKE_COUNT] = { "cold", "rtc", "key", "boot", "timer", "other" };
    return (unsigned)wake < POWER_WAKE_COUNT ? names[wake] : "?";
}

bool power_tethered(void)
{
    return usb_serial_jtag_is_connected();
}

void power_hold_awake_ms(uint32_t ms)
{
    int64_t until = esp_timer_get_time() + (int64_t)ms * 1000;
    if (until > s_hold_until_us) {
        s_hold_until_us = until;
    }
}

power_plan_t power_plan(bool work_pending)
{
    power_policy_input_t in = {
        .strategy = s_strategy,
        .tethered = power_tethered(),
        .hold_awake = work_pending || esp_timer_get_time() < s_hold_until_us,
        .test_cycles = s_rtc.test_cycles,
        .test_mode = (power_idle_t)s_rtc.test_mode,
    };
    return power_policy(&in);
}

static void count_sleep(bool deep)
{
    uint32_t awake_ms = (uint32_t)((esp_timer_get_time() - s_awake_since_us) / 1000);
    power_stats_t *st = &s_rtc.stats;
    if (deep) {
        st->deep_sleeps++;
    } else {
        st->light_sleeps++;
    }
    st->awake_ms_total += awake_ms;
    st->awake_ms_last = awake_ms;
    if (awake_ms > st->awake_ms_max) {
        st->awake_ms_max = awake_ms;
    }
    if (s_rtc.test_cycles > 0 && --s_rtc.test_cycles == 0) {
        s_rtc.test_ended = 1;
    }
    seal();
}

/* After the last `sleep test` cycle, give the USB host time to enumerate the board again. */
static void finish_test(void)
{
    if (s_rtc.test_ended) {
        s_rtc.test_ended = 0;
        seal();
        power_hold_awake_ms(TEST_END_HOLD_MS);
        ESP_LOGI(TAG, "sleep test finished");
    }
}

static void count_wake(power_wake_t wake)
{
    s_rtc.stats.wakes[wake]++;
    seal();
}

static uint64_t sleep_us_until(time_t until_utc)
{
    time_t now = time(NULL);
    time_t left = until_utc > now ? until_utc - now : 1;
    return (uint64_t)left * 1000000u;
}

power_wake_t power_sleep_light(time_t until_utc)
{
    count_sleep(false);
    for (size_t i = 0; i < sizeof(k_wake_pins) / sizeof(k_wake_pins[0]); i++) {
        gpio_intr_disable(k_wake_pins[i]);
        gpio_wakeup_enable(k_wake_pins[i], GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(sleep_us_until(until_utc));
    esp_err_t err = esp_light_sleep_start();
    for (size_t i = 0; i < sizeof(k_wake_pins) / sizeof(k_wake_pins[0]); i++) {
        gpio_wakeup_disable(k_wake_pins[i]);
        gpio_set_intr_type(k_wake_pins[i], k_wake_pins[i] == BOARD_PIN_RTC_INT ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE);
        gpio_intr_enable(k_wake_pins[i]);
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    s_awake_since_us = esp_timer_get_time();

    power_wake_t wake = POWER_WAKE_OTHER;
    if (gpio_get_level(BOARD_PIN_KEY) == 0) {
        wake = POWER_WAKE_KEY;
    } else if (gpio_get_level(BOARD_PIN_BOOT) == 0) {
        wake = POWER_WAKE_BOOT;
    } else if (gpio_get_level(BOARD_PIN_RTC_INT) == 0) {
        wake = POWER_WAKE_RTC;
    } else if (err == ESP_OK && (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER))) {
        wake = POWER_WAKE_TIMER;
    }
    count_wake(wake);
    finish_test();
    return wake;
}

void power_sleep_deep(time_t until_utc)
{
    count_sleep(true);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    rtc_gpio_pullup_en(BOARD_PIN_RTC_INT); /* INT has no external pull-up */
    rtc_gpio_pulldown_dis(BOARD_PIN_RTC_INT);
    esp_sleep_enable_ext1_wakeup_io(BIT64(BOARD_PIN_RTC_INT) | BIT64(BOARD_PIN_KEY) | BIT64(BOARD_PIN_BOOT),
                                    ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_enable_timer_wakeup(sleep_us_until(until_utc));
    fflush(stdout);
    fsync(fileno(stdout));
    esp_deep_sleep_disable_rom_logging();
    esp_deep_sleep_start();
}


power_idle_t power_idle_strategy(void)
{
    return s_strategy;
}

esp_err_t power_set_idle_strategy(power_idle_t idle)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "NVS open");
    esp_err_t err = nvs_set_u8(nvs, NVS_KEY_IDLE, (uint8_t)idle);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    ESP_RETURN_ON_ERROR(err, TAG, "NVS write");
    s_strategy = idle;
    return ESP_OK;
}

void power_start_test(power_idle_t mode, int cycles)
{
    s_rtc.stats = (power_stats_t){ 0 }; /* the test's numbers only */
    s_rtc.test_cycles = cycles;
    s_rtc.test_mode = (uint8_t)mode;
    seal();
}

int power_test_cycles_left(void)
{
    return s_rtc.test_cycles;
}

power_stats_t power_stats(void)
{
    return s_rtc.stats;
}

void power_reset_stats(void)
{
    s_rtc.stats = (power_stats_t){ 0 };
    seal();
}
