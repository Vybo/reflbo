#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * Board bring-up (spec §3.1). Owns the I²C bus and the GPIO ISR service. Call board_init() once,
 * first. `cold`: after power-on, also report the I²C devices and put the audio codecs into standby
 * (they keep that state through deep sleep).
 */
esp_err_t board_init(bool cold);
i2c_master_bus_handle_t board_i2c(void);
