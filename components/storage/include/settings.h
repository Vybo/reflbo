#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Non-secret settings in /cfg/settings.json (spec §14.3). Missing or mistyped keys take the
 * defaults the caller passes (built from Kconfig), and out-of-range numbers are clamped, so one
 * bad value never resets the rest. Keys this firmware doesn't know are kept when saving. Pure C
 * on cJSON, host-buildable.
 */

#define SETTINGS_TZ_POSIX_LEN 64
#define SETTINGS_TZ_IANA_LEN 40
#define SETTINGS_PLACE_LEN 32

/* battery.level_from (owner, 2026-09-30); the values match battery_cal_method_t in sensors. */
typedef enum {
    SETTINGS_BAT_CURVE,  /* "curve": the built-in Li-ion curve */
    SETTINGS_BAT_MANUAL, /* "manual": that curve between battery.empty_v and battery.full_v */
} settings_bat_cal_t;

typedef struct {
    char language[4];                    /* "en" */
    bool clock_24h;
    char tz_posix[SETTINGS_TZ_POSIX_LEN]; /* "CET-1CEST,M3.5.0,M10.5.0/3" */
    char tz_iana[SETTINGS_TZ_IANA_LEN];   /* "Europe/Prague", shown to the user */
    bool fahrenheit;
    uint8_t sensors_every_min; /* 1..30 */
    int16_t temp_offset_c100;  /* -1000..1000 */
    int16_t hum_offset_pct100; /* -2000..2000 */
    uint8_t display_every_min; /* 1..15 */
    uint8_t lpm_quarter_hz;    /* panel refresh in LPM, 0.25 Hz steps: 1, 2, 4, 8, 16 or 32 */
    char place[SETTINGS_PLACE_LEN]; /* location.name: "Brno" */
    int32_t lat_e4, lon_e4;         /* location in 1e-4 degrees, north and east positive: 491951, 166068 */
    uint8_t bat_cal;                    /* settings_bat_cal_t */
    uint16_t bat_empty_mv, bat_full_mv; /* 0 % and 100 % for SETTINGS_BAT_MANUAL: 3000-4000, 3600-4400 */
} settings_t;

/* Fails only if the text is not a JSON object with "schema": 1. */
bool settings_from_json(const char *json, const settings_t *defaults, settings_t *out, char *err, size_t err_size);
/* `base_json` (the file as read, or NULL) with the known keys replaced by `s`. Returns the
 * length written, or 0 if `size` is too small. */
size_t settings_to_json(const settings_t *s, const char *base_json, char *out, size_t size);
/* PATCH /api/settings: `patch` merged into `base_json` (the file as read, or NULL for none) as an
 * RFC 7396 merge patch. The result must keep "schema": 1. Returns the length written, or 0 with
 * the reason in `err`. */
size_t settings_patch(const char *base_json, const char *patch, char *out, size_t size, char *err,
                      size_t err_size);
