#pragma once

#include <stdbool.h>
#include <time.h>

#include "gfx.h"

/*
 * The Classic clock screen (spec §5.2, M2 version): status bar, large time, date and a bottom row
 * with temperature, humidity, battery level and voltage. Rendering is a pure function of this
 * struct. Pure C, host-buildable.
 */

typedef enum {
    UI_BATTERY_UNKNOWN,
    UI_BATTERY_DISCHARGING,
    UI_BATTERY_CHARGING,
    UI_BATTERY_FULL,
} ui_battery_t;

typedef struct {
    bool time_valid;     /* false: "--:--" and "Set time" (spec §5.3) */
    struct tm local;     /* local time and date */
    bool env_valid;
    int temp_c10;        /* 0.1 °C */
    int hum_pct;
    bool battery_valid;
    int battery_pct;
    int battery_mv;
    ui_battery_t battery_state;
} ui_clock_t;

void ui_draw_clock(gfx_fb_t *fb, const ui_clock_t *clock);
