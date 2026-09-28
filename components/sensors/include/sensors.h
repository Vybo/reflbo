#pragma once

#include <stdbool.h>
#include <time.h>

#include "battery_model.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * SHTC3 climate sensor and battery gauge (spec §8). Keeps the latest readings, which the display
 * and the console both show. Call from the app task only.
 */

typedef struct {
    bool valid;
    int temp_c100;  /* 0.01 °C, calibration offset applied */
    int hum_pct100; /* 0.01 %RH */
    time_t time;    /* when it was read (UTC) */
} sensors_env_t;

typedef struct {
    bool valid;
    int last_mv;     /* latest reading */
    int smoothed_mv; /* gauge voltage */
    int level;       /* % */
    battery_state_t state;
    time_t time;     /* latest reading (UTC) */
} sensors_battery_t;

/* State that must survive deep sleep (it lives in the app's RTC-RAM snapshot). */
typedef struct {
    sensors_env_t env;
    battery_gauge_t gauge;
    int last_mv;
    time_t battery_time;
} sensors_state_t;

/* `cold`: after power-on; checks the SHTC3's id and sends it to sleep. */
esp_err_t sensors_init(i2c_master_bus_handle_t bus, bool cold);
esp_err_t sensors_sample_env(time_t now);
esp_err_t sensors_sample_battery(time_t now);
/* Calibration offsets added to every reading (settings sensors.temp_offset_c, hum_offset_pct). */
void sensors_set_offsets(int temp_c100, int hum_pct100);
sensors_env_t sensors_env(void);
sensors_battery_t sensors_battery(time_t now);
/* 0.1 days of battery left, or -1 (battery_gauge_days_left10). */
int sensors_battery_days_left10(time_t now);
void sensors_export(sensors_state_t *out);
void sensors_import(const sensors_state_t *in);
