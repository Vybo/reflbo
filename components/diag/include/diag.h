#pragma once

#include "esp_err.h"

/**
 * Start the USB-Serial-JTAG console REPL and register the diagnostic commands (spec §15).
 * Call once from app_main after the rest of the system is up.
 */
esp_err_t diag_start(void);
