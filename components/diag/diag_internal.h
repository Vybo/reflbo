#pragma once

#include "esp_err.h"

/* tools/devlog.py waits for exactly this prompt. Keep the two in sync. */
#define DIAG_PROMPT "reflbo> "

/* Registers version, heap and reboot. */
esp_err_t diag_register_system_commands(void);
esp_err_t diag_register_display_commands(void); /* screenshot, panel */
esp_err_t diag_register_button_commands(void);  /* btn */
esp_err_t diag_register_sensor_commands(void);  /* sensors, battery, rtc */

/* Runs a command body on the hardware owner's task (see diag_set_executor) and returns its result. */
int diag_on_owner(int (*body)(int argc, char **argv), int argc, char **argv);
