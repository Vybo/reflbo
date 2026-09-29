#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "gesture.h"

/*
 * KEY and BOOT buttons -> gestures (spec §5.6). Edges arrive by interrupt; a task times the
 * gestures and reports them through the callback, from that task.
 */

typedef enum {
    BOARD_BUTTON_KEY,
    BOARD_BUTTON_BOOT,
    BOARD_BUTTON_COUNT,
} board_button_t;

typedef void (*board_button_cb_t)(board_button_t button, gesture_t gesture);

esp_err_t board_buttons_start(board_button_cb_t cb, const gesture_config_t config[BOARD_BUTTON_COUNT]);
/* The `btn` console command: reports a gesture as if the button had made it. */
void board_buttons_inject(board_button_t button, gesture_t gesture);
/* A button woke the chip: time it from now if it is still held, else count a tap (spec §3.3). */
void board_buttons_woke(board_button_t button);
/* Re-reads both levels, e.g. after light sleep, when edge interrupts may have been missed. */
void board_buttons_resync(void);
/* A button held since before the last sleep (D16): no gestures until it has been released. */
void board_buttons_ignore_until_released(board_button_t button);
/* New gesture timings for the context (spec §5.6): the menu has no double press on KEY and a 1 s
 * BOOT long press. `config` must stay valid; it is applied on the buttons task. */
void board_buttons_set_config(const gesture_config_t config[BOARD_BUTTON_COUNT]);
/* True while a gesture is being timed or an edge is queued; sleeping then would lose it. */
bool board_buttons_busy(void);
bool board_buttons_pressed(board_button_t button);
const char *board_button_name(board_button_t button);
const char *board_gesture_name(gesture_t gesture);
