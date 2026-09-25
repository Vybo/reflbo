#pragma once

#include <stdbool.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * PCF85063A real-time clock on the shared I²C bus (spec §7). Stores UTC. Its alarm drives the
 * minute wakes: AF latches and holds INT (GPIO15) low until pcf85063_set_alarm() or pcf85063_clear_alarm().
 * Call from the app task only.
 */

/* Configures the chip: 24 h, CLKOUT off, alarm interrupt on, timer off. Keeps the time. */
esp_err_t pcf85063_init(i2c_master_bus_handle_t bus);
/* `valid` is false while the oscillator-stop flag is set (time lost, e.g. at PWR-off). */
esp_err_t pcf85063_read(time_t *utc, bool *valid);
/* Sets the time and clears the oscillator-stop flag; fails if the oscillator does not run. */
esp_err_t pcf85063_write(time_t utc);
/* Arms the alarm for `wake` (a whole minute) and clears a pending alarm flag. */
esp_err_t pcf85063_set_alarm(time_t wake);
esp_err_t pcf85063_clear_alarm(void);
