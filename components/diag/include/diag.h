#pragma once

#include "esp_err.h"

/**
 * Start the USB-Serial-JTAG console REPL and register the diagnostic commands (spec §15).
 * Call once from app_main after the rest of the system is up.
 */
esp_err_t diag_start(void);

/* Runs fn(arg) on the task that owns the hardware (the app task, spec §3.2) and waits for it. */
typedef esp_err_t (*diag_executor_t)(void (*fn)(void *arg), void *arg);
/* Commands that touch the display, I²C devices or sleep state run through this; set it before
 * diag_start(). Without one they run on the console task. */
void diag_set_executor(diag_executor_t executor);
