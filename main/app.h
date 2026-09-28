#pragma once

#include "esp_err.h"

/* The app task (spec §3.2): owns the display, the I²C devices and sleep. */
esp_err_t app_start(void);
/* diag executor: runs fn(arg) on the app task and waits for it. */
esp_err_t app_execute(void (*fn)(void *arg), void *arg);
