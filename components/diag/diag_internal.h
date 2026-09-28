#pragma once

#include "diag.h"
#include "esp_err.h"

/* tools/devlog.py waits for exactly this prompt. Keep the two in sync. */
#define DIAG_PROMPT "reflbo> "

esp_err_t diag_register_system_commands(void);  /* version, heap, reboot, tasks */
esp_err_t diag_register_display_commands(void); /* screenshot, panel */
esp_err_t diag_register_button_commands(void);  /* btn */
esp_err_t diag_register_sensor_commands(void);  /* sensors, battery, rtc */
esp_err_t diag_register_power_commands(void);   /* power, sleep */

