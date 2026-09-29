#include "power.h"

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rtc_time.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "util_snapshot.h"

#define STATE_MAGIC   0x72666c70u /* "rflp" */
#define STATE_VERSION 5
#define NVS_NAMESPACE "sys"
#define NVS_KEY_IDLE  "idle"
#define TEST_END_HOLD_MS 3000

static const char *TAG = "power";

typedef struct {
    util_snapshot_hdr_t hdr;
    power_stats_t stats;
    uint64_t sleep_rtc_us; /* RTC counter at deep-sleep entry, for the slept time */
    int32_t test_cycles;
    uint8_t test_mode;
    uint8_t test_ended; /* the last test cycle just ran: stay awake so the PC finds the board */
    uint8_t idle_valid; /* `idle` mirrors NVS, so routine wakes need not open it */
    uint8_t idle;
    uint8_t retry;      /* boot failed: boot from scratch at the next wake */
    uint8_t masked;     /* POWER_BUTTON_* left out of the deep sleep's wake sources (D16) */
} power_state_t;

static RTC_DATA_ATTR power_state_t s_rtc; /* survives deep sleep only */
static power_idle_t s_strategy;
static power_wake_t s_boot_wake;
static bool s_boot_failed;
static bool s_cpu_pd_ready;
static int64_t s_hold_until_us;
static int64_t s_awake_since_us; /* esp_timer time this awake phase began; -1 after a stats reset */
static unsigned s_masked;        /* POWER_BUTTON_* the sleep that just ended left out */

static const gpio_num_t k_wake_pins[] = { BOARD_PIN_RTC_INT, BOARD_PIN_KEY, BOARD_PIN_BOOT };

static void seal(void)
{
    util_snapshot_seal(&s_rtc, sizeof(s_rtc), STATE_MAGIC, STATE_VERSION);
}

static void count_wake(power_wake_t wake, int64_t slept_ms);
static unsigned held_buttons(void);
static power_wake_t decode_boot_wake(void);
static void finish_test(void);

esp_err_t power_init(void)
{
    s_boot_wake = decode_boot_wake();
    bool valid = util_snapshot_valid(&s_rtc, sizeof(s_rtc), STATE_MAGIC, STATE_VERSION);
    if (!valid) {
        s_rtc = (power_state_t){ 0 };
        seal();
    }
    s_masked = s_boot_wake != POWER_WAKE_COLD ? s_rtc.masked : 0;
    if (s_boot_wake != POWER_WAKE_COLD && s_rtc.retry) {
        s_rtc.retry = 0;
        seal();
        esp_restart(); /* a reset starts RTC RAM over, and every driver with it, the panel's included */
    }
    s_awake_since_us = 0; /* esp_timer starts at 0 on every boot */
    if (s_boot_wake != POWER_WAKE_COLD) {
        int64_t slept_ms = valid ? (int64_t)((esp_rtc_get_time_us() - s_rtc.sleep_rtc_us) / 1000) : -1;
        count_wake(s_boot_wake, slept_ms);
        finish_test();
    }
#if CONFIG_REFLBO_IDLE_DEFAULT_DEEP
    s_strategy = POWER_IDLE_DEEP;
#else
    s_strategy = POWER_IDLE_LIGHT;
#endif
    if (s_rtc.idle_valid && s_rtc.idle <= POWER_IDLE_DEEP) {
        s_strategy = (power_idle_t)s_rtc.idle;
    }
    return ESP_OK;
}

static void cache_strategy(void)
{
    s_rtc.idle = (uint8_t)s_strategy;
    s_rtc.idle_valid = 1;
    seal();
}

esp_err_t power_load_settings(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        uint8_t v;
        err = nvs_get_u8(nvs, NVS_KEY_IDLE, &v);
        nvs_close(nvs);
        if (err == ESP_OK && v <= POWER_IDLE_DEEP) {
            s_strategy = (power_idle_t)v;
        }
    }
    /* NOT_FOUND: nothing stored yet, so the Kconfig default stands. */
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND, err, TAG, "NVS read");
    cache_strategy();
    return ESP_OK;
}

void power_boot_failed(void)
{
    s_boot_failed = true;
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
        .boot_failed = s_boot_failed,
    };
    return power_policy(&in);
}

/* The awake phase ended at `at_us` (esp_timer). */
static void count_sleep_at(bool deep, int64_t at_us);

static void count_sleep(bool deep)
{
    count_sleep_at(deep, esp_timer_get_time());
}

static void count_sleep_at(bool deep, int64_t at_us)
{
    power_stats_t *st = &s_rtc.stats;
    if (deep) {
        st->deep_sleeps++;
    } else {
        st->light_sleeps++;
    }
    if (s_awake_since_us >= 0) {
        uint32_t awake_ms = (uint32_t)((at_us - s_awake_since_us) / 1000);
        st->awake_count++;
        st->awake_ms_total += awake_ms;
        st->awake_ms_last = awake_ms;
        if (awake_ms > st->awake_ms_max) {
            st->awake_ms_max = awake_ms;
        }
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

/* `slept_ms` < 0: unknown, because RTC RAM did not survive. */
static void count_wake(power_wake_t wake, int64_t slept_ms)
{
    power_stats_t *st = &s_rtc.stats;
    st->wakes[wake]++;
    if (slept_ms >= 0) {
        uint32_t ms = slept_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)slept_ms;
        if (st->slept_count == 0 || ms < st->slept_ms_min) {
            st->slept_ms_min = ms;
        }
        st->slept_count++;
        st->slept_ms_total += ms;
        st->slept_ms_last = ms;
    }
    seal();
}

/* To the microsecond, so a wake set for a whole second (the seconds display) lands on it. */
static uint64_t sleep_us_until(time_t until_utc)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t left = (int64_t)until_utc * 1000000 - ((int64_t)tv.tv_sec * 1000000 + tv.tv_usec);
    return left > 1000 ? (uint64_t)left : 1000u;
}

power_wake_t power_sleep_light(time_t until_utc)
{
    if (!s_cpu_pd_ready) { /* on first use: boots that only deep-sleep never need the buffer */
        s_cpu_pd_ready = true;
        esp_err_t err = esp_sleep_cpu_pd_low_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "CPU stays powered in light sleep: %s", esp_err_to_name(err));
        }
    }
    unsigned held = held_buttons();
    for (size_t i = 0; i < sizeof(k_wake_pins) / sizeof(k_wake_pins[0]); i++) {
        gpio_intr_disable(k_wake_pins[i]);
        bool masked = (k_wake_pins[i] == BOARD_PIN_KEY && (held & POWER_BUTTON_KEY)) ||
                      (k_wake_pins[i] == BOARD_PIN_BOOT && (held & POWER_BUTTON_BOOT));
        if (!masked) {
            gpio_wakeup_enable(k_wake_pins[i], GPIO_INTR_LOW_LEVEL);
        }
    }
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(sleep_us_until(until_utc));
    int64_t slept_from_us = esp_timer_get_time();
    esp_err_t err = esp_light_sleep_start();
    int64_t woke_us = esp_timer_get_time(); /* esp_timer counts light sleep too */
    for (size_t i = 0; i < sizeof(k_wake_pins) / sizeof(k_wake_pins[0]); i++) {
        gpio_wakeup_disable(k_wake_pins[i]);
        gpio_set_intr_type(k_wake_pins[i], k_wake_pins[i] == BOARD_PIN_RTC_INT ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE);
        gpio_intr_enable(k_wake_pins[i]);
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (err == ESP_OK) {
        count_sleep_at(false, slept_from_us);
        s_awake_since_us = woke_us;
    }

    s_masked = held;
    power_wake_t wake = POWER_WAKE_OTHER; /* a button held since before the sleep didn't wake it */
    if (!(held & POWER_BUTTON_KEY) && gpio_get_level(BOARD_PIN_KEY) == 0) {
        wake = POWER_WAKE_KEY;
    } else if (!(held & POWER_BUTTON_BOOT) && gpio_get_level(BOARD_PIN_BOOT) == 0) {
        wake = POWER_WAKE_BOOT;
    } else if (gpio_get_level(BOARD_PIN_RTC_INT) == 0) {
        wake = POWER_WAKE_RTC;
    } else if (err == ESP_OK && (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER))) {
        wake = POWER_WAKE_TIMER;
    }
    if (err != ESP_OK) {
        return wake; /* rejected (a wake source was already active): not a sleep, no test cycle used */
    }
    count_wake(wake, (woke_us - slept_from_us) / 1000);
    finish_test();
    return wake;
}

static void start_deep_sleep(void)
{
    fflush(stdout);
    fsync(fileno(stdout));
    esp_deep_sleep_disable_rom_logging();
    s_rtc.sleep_rtc_us = esp_rtc_get_time_us();
    seal();
    esp_deep_sleep_start();
}

/* POWER_BUTTON_* for the buttons held right now. */
static unsigned held_buttons(void)
{
    return (gpio_get_level(BOARD_PIN_KEY) == 0 ? POWER_BUTTON_KEY : 0) |
           (gpio_get_level(BOARD_PIN_BOOT) == 0 ? POWER_BUTTON_BOOT : 0);
}

/* ext1 bits for the buttons that are not held (D16). */
static uint64_t button_wake_bits(unsigned held)
{
    return (held & POWER_BUTTON_KEY ? 0 : BIT64(BOARD_PIN_KEY)) |
           (held & POWER_BUTTON_BOOT ? 0 : BIT64(BOARD_PIN_BOOT));
}

unsigned power_masked_buttons(void)
{
    return s_masked;
}

void power_sleep_deep(time_t until_utc)
{
    count_sleep(true);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    rtc_gpio_pullup_en(BOARD_PIN_RTC_INT); /* INT has no external pull-up */
    rtc_gpio_pulldown_dis(BOARD_PIN_RTC_INT);
    unsigned held = held_buttons();
    s_rtc.masked = (uint8_t)held;
    esp_sleep_enable_ext1_wakeup_io(BIT64(BOARD_PIN_RTC_INT) | button_wake_bits(held), ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_enable_timer_wakeup(sleep_us_until(until_utc));
    start_deep_sleep();
}

void power_sleep_retry(uint32_t seconds)
{
    s_rtc.retry = 1;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    /* Not RTC_INT: whatever broke the boot may leave it low, which would wake the chip at once. */
    unsigned held = held_buttons();
    s_rtc.masked = (uint8_t)held;
    uint64_t bits = button_wake_bits(held);
    if (bits != 0) {
        esp_sleep_enable_ext1_wakeup_io(bits, ESP_EXT1_WAKEUP_ANY_LOW);
    }
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000u);
    start_deep_sleep();
}

void power_sleep_critical(uint32_t recheck_s)
{
    count_sleep(true);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    bool held = held_buttons() & POWER_BUTTON_KEY;
    s_rtc.masked = 0; /* KEY stays the wake source either way */
    /* A KEY still held, usually the press that asked for this check, wakes the chip when it is
     * released, so the next press counts; a timer looks again now and then in case it is stuck. */
    esp_sleep_enable_ext1_wakeup_io(BIT64(BOARD_PIN_KEY), held ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
    if (held) {
        esp_sleep_enable_timer_wakeup((uint64_t)recheck_s * 1000000u);
    }
    start_deep_sleep();
}


power_idle_t power_idle_strategy(void)
{
    return s_strategy;
}

esp_err_t power_set_idle_strategy(power_idle_t idle)
{
    s_strategy = idle; /* applies for this session even if NVS can't keep it */
    cache_strategy();
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "NVS open");
    esp_err_t err = nvs_set_u8(nvs, NVS_KEY_IDLE, (uint8_t)idle);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    ESP_RETURN_ON_ERROR(err, TAG, "NVS write");
    return ESP_OK;
}

void power_start_test(power_idle_t mode, int cycles)
{
    s_rtc.stats = (power_stats_t){ 0 }; /* the test's numbers only */
    s_awake_since_us = -1;
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
    s_awake_since_us = -1;
    seal();
}
