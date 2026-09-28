#include "app.h"

#include <sys/time.h>
#include <time.h>

#include "app_internal.h"

#include "board.h"
#include "board_buttons.h"
#include "board_pins.h"
#include "diag.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "power.h"
#include "pcf85063.h"
#include "scheduler.h"
#include "st7305.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "timekeeping.h"
#include "util_snapshot.h"
#include "util_ticks.h"

#define APP_STACK         8192 /* internal RAM: deep-sleep entry requires it */
#define APP_PRIORITY      5
#define APP_CORE          1
#define QUEUE_DEPTH       16
#define BACKUP_S          5    /* wake anyway this long after a missed RTC alarm (spec §9.2) */
#define GRACE_MS          2000 /* stay awake after boot or a button so a PC can find the board */
#define TETHER_RECHECK_MS 1000
#define RETRY_S           300  /* after a failed boot with no PC attached */
#define SNAP_MAGIC        0x72666c62u /* "rflb" */
#define SNAP_VERSION      2

#if CONFIG_REFLBO_PANEL_INIT_XIAOZHI
#define PANEL_VARIANT ST7305_VARIANT_XIAOZHI
#else
#define PANEL_VARIANT ST7305_VARIANT_FACTORY
#endif

static const char *TAG = "app";

typedef enum {
    EV_RTC_ALARM,
    EV_BUTTON,
    EV_CALL,
} ev_type_t;

typedef struct {
    ev_type_t type;
    union {
        struct {
            board_button_t button;
            gesture_t gesture;
        } button;
        struct {
            void (*fn)(void *);
            void *arg;
            SemaphoreHandle_t done;
        } call;
    };
} app_event_t;

/* Kept in RTC RAM through deep sleep; invalid after any other reset (spec §3.3). */
typedef struct {
    util_snapshot_hdr_t hdr;
    sensors_state_t sensors;
    display_state_t display;
    app_ui_state_t ui;
    time_t next_alarm;
} app_snapshot_t;

static RTC_DATA_ATTR app_snapshot_t s_snap;
static QueueHandle_t s_queue;
static time_t s_next_alarm; /* the RTC alarm: the next minute slot */
static time_t s_wake_at;    /* the earliest wake: the alarm, or a cycle switch or seconds tick before it */

/* Dashboard bindings (spec §5.6): KEY double toggles auto-cycle, BOOT long needs 3 s. */
static const gesture_config_t k_dashboard_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = true },
    [BOARD_BUTTON_BOOT] = { .long_ms = 3000, .double_enabled = false },
};

static void IRAM_ATTR on_rtc_int(void *arg)
{
    (void)arg;
    app_event_t ev = { .type = EV_RTC_ALARM };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &ev, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void on_button(board_button_t button, gesture_t gesture)
{
    app_event_t ev = { .type = EV_BUTTON, .button = { .button = button, .gesture = gesture } };
    xQueueSend(s_queue, &ev, pdMS_TO_TICKS(100));
}

esp_err_t app_execute(void (*fn)(void *arg), void *arg)
{
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(done != NULL, ESP_ERR_NO_MEM, TAG, "semaphore");
    app_event_t ev = { .type = EV_CALL, .call = { .fn = fn, .arg = arg, .done = done } };
    if (xQueueSend(s_queue, &ev, pdMS_TO_TICKS(1000)) != pdTRUE) {
        vSemaphoreDelete(done);
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    return ESP_OK;
}

static void schedule_next(void)
{
    sched_wake_t wake = app_ui_next_wake(time(NULL));
    s_wake_at = wake.when;
    if (wake.alarm != s_next_alarm) {
        s_next_alarm = wake.alarm;
        esp_err_t err = pcf85063_set_alarm(s_next_alarm); /* also clears the alarm flag, which releases INT */
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RTC alarm: %s; the backup timer takes over", esp_err_to_name(err));
        }
    }
}

/* The timer wake: a cycle switch or seconds tick if one comes before the alarm, else the alarm's backup. */
static time_t sleep_until(void)
{
    return s_wake_at < s_next_alarm ? s_wake_at : s_next_alarm + BACKUP_S;
}

/* A scheduled wake: the RTC alarm, its backup, a cycle switch or a seconds tick. `force` also
 * samples and renders. */
static void on_tick(bool force)
{
    esp_err_t err = timekeeping_load_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC read: %s", esp_err_to_name(err));
    }
    if (time(NULL) >= s_next_alarm) {
        s_next_alarm = 0; /* fired: program the next one, even if it is the same minute again */
    }
    app_ui_tick(force);
    schedule_next();
}

/* Dashboard bindings (spec §5.6). */
static void handle_button(board_button_t button, gesture_t gesture)
{
    power_hold_awake_ms(GRACE_MS);
    if (button == BOARD_BUTTON_KEY && gesture == GESTURE_SHORT) {
        app_ui_select(ui_presets_next(app_presets()), true);
    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_DOUBLE) {
        app_ui_toggle_cycle();
    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT) {
        app_ui_sample(time(NULL));
        app_ui_render();
        ESP_LOGI(TAG, "BOOT short: sensors refreshed");
    } else {
        ESP_LOGI(TAG, "%s %s is not bound yet (the menu comes in M3b, config mode in M4)", board_button_name(button),
                 board_gesture_name(gesture));
    }
    schedule_next();
}

/* If the clock moved back (`rtc set`, later SNTP), the pending alarm is further off than the next
 * slot from now: schedule again. Comparing with a fresh schedule stays right across DST changes
 * and for any update interval. A clock that moved forward is caught by the backup tick. */
static void check_clock_jump(void)
{
    if (s_next_alarm != 0 && app_ui_next_wake(time(NULL)).alarm < s_next_alarm) {
        ESP_LOGW(TAG, "clock moved back; scheduling again");
        on_tick(true);
    }
}

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void handle_event(const app_event_t *ev)
{
    switch (ev->type) {
    case EV_RTC_ALARM:
        on_tick(false);
        break;
    case EV_BUTTON:
        handle_button(ev->button.button, ev->button.gesture);
        break;
    case EV_CALL: {
        bool was_valid = timekeeping_valid();
        int64_t before = now_ms();
        ev->call.fn(ev->call.arg);
        xSemaphoreGive(ev->call.done);
        int64_t moved = now_ms() - before;
        if (timekeeping_valid() != was_valid || moved < 0 || moved > 2000) {
            on_tick(true); /* `rtc set` and friends: show the new time now, not at the next slot */
        }
        power_hold_awake_ms(GRACE_MS);
        break;
    }
    }
}

static void handle_wake(power_wake_t wake)
{
    switch (wake) {
    case POWER_WAKE_RTC:
        on_tick(false);
        break;
    case POWER_WAKE_TIMER: /* a cycle switch or seconds tick, or the alarm's backup */
        if (time(NULL) >= s_next_alarm + BACKUP_S) {
            ESP_LOGW(TAG, "RTC alarm missed; backup wake");
        }
        on_tick(false);
        break;
    case POWER_WAKE_KEY:
    case POWER_WAKE_BOOT:
        board_buttons_resync(); /* edges during sleep raised no interrupt */
        power_hold_awake_ms(GRACE_MS);
        break;
    default:
        break;
    }
}

static void enter_deep_sleep(void)
{
    sensors_export(&s_snap.sensors);
    display_export(&s_snap.display);
    app_ui_export(&s_snap.ui);
    s_snap.next_alarm = s_next_alarm;
    util_snapshot_seal(&s_snap, sizeof(s_snap), SNAP_MAGIC, SNAP_VERSION);
    esp_err_t err = display_prepare_deep_sleep();
    if (err != ESP_OK) {
        /* Without the holds the panel would reset in deep sleep: sleep light this time instead. */
        ESP_LOGE(TAG, "panel pins: %s; light sleep this time", esp_err_to_name(err));
        display_cancel_deep_sleep();
        s_snap.hdr.magic = 0;
        handle_wake(power_sleep_light(sleep_until()));
        return;
    }
    power_sleep_deep(sleep_until());
}

static esp_err_t start_rtc_int(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_PIN_RTC_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, /* open drain, no external pull-up */
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "RTC INT pin");
    return gpio_isr_handler_add(BOARD_PIN_RTC_INT, on_rtc_int, NULL);
}

/* The RTC alarm or its backup timer: back to sleep in a fraction of a second, unless the board
 * then decides to stay awake. */
static bool routine_wake(power_wake_t wake)
{
    return wake == POWER_WAKE_RTC || wake == POWER_WAKE_TIMER;
}

/* Logs, settings and the console: what a board needs once it stays awake. Routine wakes skip
 * them, because they cost time on every wake and nobody can use them (spec §3.3). */
static void come_alive(void)
{
    static bool s_alive;
    if (s_alive) {
        return;
    }
    s_alive = true;
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) {
        err = power_load_settings();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "power settings: %s", esp_err_to_name(err));
        }
    } else {
        /* Never erase NVS on our own (AGENTS.md quick rule 3): settings fall back to defaults. */
        ESP_LOGE(TAG, "NVS unavailable (%s); settings use their defaults", esp_err_to_name(err));
    }
    err = diag_start();
    if (err == ESP_OK) {
        app_register_commands();
    } else {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }
    size_t dump_addr, dump_size; /* spec §16; IDF's own boot check is off, it would run every wake */
    if (esp_core_dump_image_get(&dump_addr, &dump_size) == ESP_OK) {
        ESP_LOGW(TAG, "a %u-byte core dump is in flash; read it with `idf.py coredump-info`", (unsigned)dump_size);
    }
}

static esp_err_t boot(void)
{
    esp_err_t err = power_init();
    power_wake_t wake = power_boot_wake();
    if (!routine_wake(wake)) {
        come_alive();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "power");
    bool warm = wake != POWER_WAKE_COLD && util_snapshot_valid(&s_snap, sizeof(s_snap), SNAP_MAGIC, SNAP_VERSION);
    if (warm) {
        app_ui_import(&s_snap.ui); /* routine wakes skip storage: everything is in RTC RAM */
        s_next_alarm = s_snap.next_alarm;
    } else {
        app_ui_defaults();
        app_ui_load();
    }

    ESP_RETURN_ON_ERROR(board_init(wake == POWER_WAKE_COLD), TAG, "board");
    ESP_RETURN_ON_ERROR(pcf85063_init(board_i2c()), TAG, "RTC");
    ESP_RETURN_ON_ERROR(timekeeping_init(app_settings()->tz_posix), TAG, "time zone");
    err = timekeeping_load_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC read: %s", esp_err_to_name(err));
    }
    ESP_RETURN_ON_ERROR(sensors_init(board_i2c(), !warm), TAG, "sensors");
    if (warm) {
        sensors_import(&s_snap.sensors);
        ESP_RETURN_ON_ERROR(display_init_warm(&s_snap.display), TAG, "display");
    } else if (wake != POWER_WAKE_COLD) {
        /* Woke from deep sleep without a valid snapshot: the panel still runs, don't reset it. */
        display_state_t fallback = { .variant = PANEL_VARIANT, .mode = ST7305_MODE_LPM, .lpm_rate = ST7305_LPM_1HZ };
        ESP_RETURN_ON_ERROR(display_init_warm(&fallback), TAG, "display");
        st7305_set_lpm_rate(ST7305_LPM_1HZ); /* assumed, so send it; safe without a reset */
    } else {
        ESP_RETURN_ON_ERROR(display_init(PANEL_VARIANT), TAG, "display");
    }
    app_ui_apply_settings(); /* offsets, freshness and the panel rate, now that the panel is up */
    ESP_RETURN_ON_ERROR(board_buttons_start(on_button, k_dashboard_buttons), TAG, "buttons");
    ESP_RETURN_ON_ERROR(start_rtc_int(), TAG, "RTC INT");

    if (wake == POWER_WAKE_KEY) {
        board_buttons_woke(BOARD_BUTTON_KEY);
    } else if (wake == POWER_WAKE_BOOT) {
        board_buttons_woke(BOARD_BUTTON_BOOT);
    }
    if (wake == POWER_WAKE_COLD || wake == POWER_WAKE_KEY || wake == POWER_WAKE_BOOT) {
        power_hold_awake_ms(GRACE_MS);
    }
    if (wake == POWER_WAKE_TIMER && time(NULL) >= s_next_alarm + BACKUP_S) {
        ESP_LOGW(TAG, "RTC alarm missed; backup wake");
    }
    on_tick(!warm);
    ESP_LOGI(TAG, "reflbo ready (%s wake%s)", power_wake_name(wake), warm ? ", warm" : "");
    return ESP_OK;
}

static void app_task(void *arg)
{
    (void)arg;
    esp_err_t err = boot();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "boot failed: %s; the console stays up while a PC is attached, otherwise the "
                 "board sleeps and boots again in %d s", esp_err_to_name(err), RETRY_S);
        power_boot_failed();
        power_hold_awake_ms(GRACE_MS);
    }
    for (;;) {
        if (err == ESP_OK) {
            check_clock_jump();
        }
        bool pending = uxQueueMessagesWaiting(s_queue) > 0 || board_buttons_busy();
        switch (power_plan(pending)) {
        case POWER_PLAN_LIGHT:
            handle_wake(power_sleep_light(sleep_until()));
            continue;
        case POWER_PLAN_DEEP:
            enter_deep_sleep(); /* returns only if it had to sleep light instead */
            continue;
        case POWER_PLAN_RETRY:
            power_sleep_retry(RETRY_S);
            break;
        case POWER_PLAN_AWAKE:
            come_alive();
            break;
        }
        int64_t wait_ms = TETHER_RECHECK_MS; /* a failed boot has no schedule to wait for */
        if (err == ESP_OK) {
            int64_t due_ms = (int64_t)sleep_until() * 1000 - now_ms();
            wait_ms = due_ms < wait_ms ? due_ms : wait_ms;
        }
        TickType_t wait = wait_ms <= 0 ? 0 : util_ticks_at_least((uint32_t)wait_ms, portTICK_PERIOD_MS);
        app_event_t ev;
        if (xQueueReceive(s_queue, &ev, wait) == pdTRUE) {
            handle_event(&ev);
        } else if (err == ESP_OK && time(NULL) >= s_next_alarm + BACKUP_S) {
            ESP_LOGW(TAG, "RTC alarm missed; backup tick");
            on_tick(false);
        } else if (err == ESP_OK && time(NULL) >= s_wake_at) {
            on_tick(false); /* a cycle switch or seconds tick */
        }
    }
}

esp_err_t app_start(void)
{
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(app_event_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue");
    BaseType_t ok = xTaskCreatePinnedToCore(app_task, "app", APP_STACK, NULL, APP_PRIORITY, NULL, APP_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}
