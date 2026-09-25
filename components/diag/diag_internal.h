#pragma once

#include "esp_err.h"

/* tools/devlog.py waits for exactly this prompt. Keep the two in sync. */
#define DIAG_PROMPT "reflbo> "

/* Registers version, heap and reboot. */
esp_err_t diag_register_system_commands(void);
esp_err_t diag_register_display_commands(void); /* screenshot, panel */
