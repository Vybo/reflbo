#include "app.h"

#include <time.h>

#include "board.h"
#include "board_buttons.h"
#include "board_pins.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pcf85063.h"
#include "scheduler.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "timekeeping.h"
#include "ui_clock.h"
#include "util_ticks.h"

#define APP_STACK         8192
#define APP_PRIORITY      5
#define APP_CORE          1
#define QUEUE_DEPTH       16
#define BACKUP_S          5    /* wake anyway this long after a missed RTC alarm (spec §9.2) */

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

static QueueHandle_t s_queue;
static time_t s_next_wake;

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

static ui_battery_t ui_battery_state(battery_state_t state)
{
    switch (state) {
    case BATTERY_DISCHARGING:
        return UI_BATTERY_DISCHARGING;
    case BATTERY_CHARGING:
        return UI_BATTERY_CHARGING;
    case BATTERY_FULL:
        return UI_BATTERY_FULL;
    default:
        return UI_BATTERY_UNKNOWN;
    }
}

static void render(void)
{
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        return;
    }
    time_t now = time(NULL);
    ui_clock_t clock = { .time_valid = timekeeping_valid() };
    localtime_r(&now, &clock.local);
    sensors_env_t env = sensors_env();
    clock.env_valid = env.valid;
    clock.temp_c10 = (env.temp_c100 + (env.temp_c100 >= 0 ? 5 : -5)) / 10;
    clock.hum_pct = (env.hum_pct100 + 50) / 100;
    sensors_battery_t bat = sensors_battery(now);
    clock.battery_valid = bat.valid;
    clock.battery_pct = bat.level;
    clock.battery_mv = bat.smoothed_mv;
    clock.battery_state = ui_battery_state(bat.state);
    ui_draw_clock(fb, &clock);
    esp_err_t err = display_commit(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display: %s", esp_err_to_name(err));
    }
}

static void sample_sensors(time_t now)
{
    esp_err_t err = sensors_sample_env(now);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SHTC3: %s; keeping the last reading", esp_err_to_name(err));
    }
    err = sensors_sample_battery(now);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "battery: %s", esp_err_to_name(err));
    }
}

static void schedule_next(void)
{
    sched_input_t in = {
        .now = time(NULL),
        .display_every_min = CONFIG_REFLBO_DISPLAY_UPDATE_MIN,
        .sensors_every_min = CONFIG_REFLBO_SENSOR_INTERVAL_MIN,
    };
    s_next_wake = scheduler_next_wake(&in).when;
    esp_err_t err = pcf85063_set_alarm(s_next_wake); /* also clears the alarm flag, which releases INT */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC alarm: %s; the backup timer takes over", esp_err_to_name(err));
    }
}

/* A scheduled minute, from the RTC alarm or its backup. `force` also samples and renders. */
static void on_tick(bool force)
{
    esp_err_t err = timekeeping_load_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC read: %s", esp_err_to_name(err));
    }
    time_t now = time(NULL);
    time_t slot = now - now % 60;
    if (force || scheduler_is_slot(slot, CONFIG_REFLBO_SENSOR_INTERVAL_MIN)) {
        sample_sensors(now);
    }
    if (force || scheduler_is_slot(slot, CONFIG_REFLBO_DISPLAY_UPDATE_MIN)) {
        render();
    }
    schedule_next();
}

static void handle_button(board_button_t button, gesture_t gesture)
{
    if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT) {
        sample_sensors(time(NULL)); /* spec §5.6: refresh sensors */
        render();
        ESP_LOGI(TAG, "BOOT short: sensors refreshed");
        return;
    }
    ESP_LOGI(TAG, "%s %s is not bound yet (menu and presets come in M3, config mode in M4)",
             board_button_name(button), board_gesture_name(gesture));
}

/* The next wake must be at most one update interval away. If the clock moved back (`rtc set`,
 * later SNTP), the pending alarm and its backup timer are too far off: schedule again. A clock
 * that moved forward is caught by the backup tick instead. */
static void check_clock_jump(void)
{
    if (s_next_wake - time(NULL) > (CONFIG_REFLBO_DISPLAY_UPDATE_MIN + 1) * 60) {
        ESP_LOGW(TAG, "clock moved back; scheduling again");
        on_tick(true);
    }
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
    case EV_CALL:
        ev->call.fn(ev->call.arg);
        xSemaphoreGive(ev->call.done);
        break;
    }
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

static esp_err_t boot(void)
{
    ESP_RETURN_ON_ERROR(board_init(true), TAG, "board");
    ESP_RETURN_ON_ERROR(pcf85063_init(board_i2c()), TAG, "RTC");
    ESP_RETURN_ON_ERROR(timekeeping_init(CONFIG_REFLBO_TZ), TAG, "time zone");
    esp_err_t err = timekeeping_load_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC read: %s", esp_err_to_name(err));
    }
    ESP_RETURN_ON_ERROR(sensors_init(board_i2c(), true), TAG, "sensors");
    ESP_RETURN_ON_ERROR(display_init(PANEL_VARIANT), TAG, "display");
    ESP_RETURN_ON_ERROR(board_buttons_start(on_button, k_dashboard_buttons), TAG, "buttons");
    ESP_RETURN_ON_ERROR(start_rtc_int(), TAG, "RTC INT");
    on_tick(true);
    ESP_LOGI(TAG, "reflbo ready");
    return ESP_OK;
}

/* Awake-only loop: sleep arrives with the power component (M2 Task 13). */
static void app_task(void *arg)
{
    (void)arg;
    esp_err_t err = boot();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "boot failed: %s; staying awake for the console", esp_err_to_name(err));
    }
    for (;;) {
        check_clock_jump();
        int64_t backup_ms = ((int64_t)(s_next_wake + BACKUP_S) - time(NULL)) * 1000;
        TickType_t wait = backup_ms <= 0 ? 0 : util_ticks_at_least((uint32_t)backup_ms, portTICK_PERIOD_MS);
        app_event_t ev;
        if (xQueueReceive(s_queue, &ev, wait) == pdTRUE) {
            handle_event(&ev);
        } else if (err == ESP_OK && time(NULL) >= s_next_wake + BACKUP_S) {
            ESP_LOGW(TAG, "RTC alarm missed; backup tick");
            on_tick(false);
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
