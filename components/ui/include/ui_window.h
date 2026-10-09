#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * A preset's cycle window (spec §5.4, M6e, D41): the auto-cycle visits the preset only while it is open. It
 * opens at `from` on each day `days` names and closes at `until` that day, or the next day when `until` comes
 * at or before `from` (two clock ends compared on the clock face, so one inside a DST gap is empty that night).
 * Pure C, host-buildable.
 */

#define UI_BOUND_OFFSET_MAX 180 /* minutes either side of sunrise or sunset */
#define UI_BOUND_TEXT_LEN 12    /* "sunrise-180" and its NUL */

/* A bound in two bytes, as the RTC snapshot keeps presets: minutes after local midnight, 0-1439, or sunrise or
 * sunset plus an offset of -180 to 180 minutes. */
enum {
    UI_BOUND_SUNRISE = 2000,
    UI_BOUND_SUNSET = 3000,
};

typedef struct {
    uint8_t days; /* bit 0 Monday ... bit 6 Sunday; 0: no window, always open */
    int16_t from, until;
} ui_window_t;

/* "HH:MM", "sunrise", "sunset", or a sun bound with "+N" or "-N" minutes, N 1-180 without a leading zero. */
bool ui_bound_parse(const char *text, int16_t *out);
void ui_bound_format(int16_t bound, char *out, size_t size);
/* A window from presets.json's or the console's texts; false with the reason in `err` (spec §5.4). */
bool ui_window_make(const char *from, const char *until, int days, ui_window_t *out, char *err, size_t err_size);

/* Whether `w` is open at `now`, with sunrise and sunset at the location (1e-4 degrees, as settings_t keeps it)
 * and local times as TZ says. *change, if given, gets when that next changes: the closing time when open, the
 * opening time when closed; 0 when nothing changes within the next 8 days (a polar day or night, no window). */
bool ui_window_open(const ui_window_t *w, time_t now, int32_t lat_e4, int32_t lon_e4, time_t *change);

/* For `preset list` (spec §15): "sunrise - sunset-30", with " on Mo Fr" unless every day. */
void ui_window_text(const ui_window_t *w, char *out, size_t size);
/* "open until 18:21", "closed until Sat 07:00" (the weekday when not today), "open", "closed"; `now` 0, the
 * clock not set: "open: the clock isn't set", as every window counts as open then. */
void ui_window_state_text(const ui_window_t *w, time_t now, int32_t lat_e4, int32_t lon_e4, char *out, size_t size);
