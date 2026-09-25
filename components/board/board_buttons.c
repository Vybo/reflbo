#include "board_buttons.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "util_ticks.h"

#define TASK_STACK    3072
#define TASK_PRIORITY 6 /* above the app task: gestures are timed while it renders */
#define QUEUE_DEPTH   16

static const char *TAG = "buttons";

typedef enum {
    MSG_EDGE,
    MSG_INJECT,
    MSG_WOKE,
    MSG_RESYNC,
} msg_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t button;
    uint8_t gesture;
} msg_t;

static const gpio_num_t k_pins[BOARD_BUTTON_COUNT] = { BOARD_PIN_KEY, BOARD_PIN_BOOT };
static QueueHandle_t s_queue;
static board_button_cb_t s_cb;
static gesture_recogniser_t s_rec[BOARD_BUTTON_COUNT];
static volatile bool s_busy;

static void IRAM_ATTR on_edge(void *arg)
{
    msg_t msg = { .kind = MSG_EDGE, .button = (uint8_t)(uintptr_t)arg };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &msg, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool board_buttons_pressed(board_button_t button)
{
    return gpio_get_level(k_pins[button]) == 0;
}

static void report(board_button_t button, gesture_t gesture)
{
    if (gesture != GESTURE_NONE) {
        ESP_LOGI(TAG, "%s %s", board_button_name(button), board_gesture_name(gesture));
        s_cb(button, gesture);
    }
}

static TickType_t next_wait(void)
{
    TickType_t wait = portMAX_DELAY;
    uint32_t now = now_ms();
    for (int b = 0; b < BOARD_BUTTON_COUNT; b++) {
        uint32_t deadline = gesture_deadline(&s_rec[b]);
        if (deadline == GESTURE_NO_DEADLINE) {
            continue;
        }
        int32_t left = (int32_t)(deadline - now);
        TickType_t ticks = left <= 0 ? 0 : util_ticks_at_least((uint32_t)left, portTICK_PERIOD_MS);
        if (ticks < wait) {
            wait = ticks;
        }
    }
    return wait;
}

static void buttons_task(void *arg)
{
    (void)arg;
    for (;;) {
        msg_t msg;
        if (xQueueReceive(s_queue, &msg, next_wait()) == pdTRUE) {
            board_button_t b = (board_button_t)msg.button;
            switch (msg.kind) {
            case MSG_EDGE:
                report(b, gesture_update(&s_rec[b], board_buttons_pressed(b), now_ms()));
                break;
            case MSG_INJECT:
                report(b, (gesture_t)msg.gesture);
                break;
            case MSG_WOKE:
                if (board_buttons_pressed(b)) {
                    report(b, gesture_update(&s_rec[b], true, now_ms()));
                } else {
                    report(b, gesture_tap(&s_rec[b], now_ms()));
                }
                break;
            case MSG_RESYNC:
                for (int i = 0; i < BOARD_BUTTON_COUNT; i++) {
                    report((board_button_t)i, gesture_update(&s_rec[i], board_buttons_pressed(i), now_ms()));
                }
                break;
            }
        }
        bool busy = false;
        for (int b = 0; b < BOARD_BUTTON_COUNT; b++) {
            report((board_button_t)b, gesture_update(&s_rec[b], board_buttons_pressed(b), now_ms()));
            busy |= gesture_busy(&s_rec[b]);
        }
        s_busy = busy;
    }
}

static void post(msg_t msg)
{
    s_busy = true;
    xQueueSend(s_queue, &msg, pdMS_TO_TICKS(100));
}

esp_err_t board_buttons_start(board_button_cb_t cb, const gesture_config_t config[BOARD_BUTTON_COUNT])
{
    s_cb = cb;
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(msg_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue");
    for (int b = 0; b < BOARD_BUTTON_COUNT; b++) {
        gesture_init(&s_rec[b], config[b]);
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << k_pins[b],
            .mode = GPIO_MODE_INPUT,
            .intr_type = GPIO_INTR_ANYEDGE, /* both buttons have external pull-ups */
        };
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "button pin");
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(k_pins[b], on_edge, (void *)(uintptr_t)b), TAG, "button ISR");
    }
    BaseType_t ok = xTaskCreatePinnedToCore(buttons_task, "buttons", TASK_STACK, NULL, TASK_PRIORITY, NULL,
                                            tskNO_AFFINITY);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

void board_buttons_inject(board_button_t button, gesture_t gesture)
{
    post((msg_t){ .kind = MSG_INJECT, .button = (uint8_t)button, .gesture = (uint8_t)gesture });
}

void board_buttons_woke(board_button_t button)
{
    post((msg_t){ .kind = MSG_WOKE, .button = (uint8_t)button });
}

void board_buttons_resync(void)
{
    post((msg_t){ .kind = MSG_RESYNC });
}

bool board_buttons_busy(void)
{
    return s_busy || uxQueueMessagesWaiting(s_queue) > 0;
}

const char *board_button_name(board_button_t button)
{
    return button == BOARD_BUTTON_KEY ? "KEY" : "BOOT";
}

const char *board_gesture_name(gesture_t gesture)
{
    switch (gesture) {
    case GESTURE_SHORT:
        return "short";
    case GESTURE_DOUBLE:
        return "double";
    case GESTURE_LONG:
        return "long";
    default:
        return "none";
    }
}
