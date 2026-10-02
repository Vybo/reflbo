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
    SETTINGS_BAT_CURVE,   /* "curve": the built-in Li-ion curve */
    SETTINGS_BAT_MANUAL,  /* "manual": that curve between battery.empty_v and battery.full_v */
    SETTINGS_BAT_LEARNED, /* "learned": battery.learned_mv, from a full discharge (D21) */
} settings_bat_cal_t;

#define SETTINGS_BAT_CURVE_POINTS 21 /* BATTERY_CURVE_POINTS: 0 %, 5 %, ... 100 % */

/* sync.mode (spec §9.3); the values match sync_mode_t in the sync component. */
typedef enum {
    SETTINGS_SYNC_TIMES,
    SETTINGS_SYNC_INTERVAL,
    SETTINGS_SYNC_ALWAYS,
    SETTINGS_SYNC_MANUAL,
} settings_sync_mode_t;

#define SETTINGS_ZOOM_Q_MIN 16 /* radar.weather.zoom in quarters: 4 to 9 (spec §14.3) */
#define SETTINGS_ZOOM_Q_MAX 36

#define SETTINGS_SYNC_TIMES_MAX 8
#define SETTINGS_NTP_MAX 2
#define SETTINGS_HOST_LEN 64

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
    uint16_t bat_learned_mv[SETTINGS_BAT_CURVE_POINTS]; /* all 0 until a discharge was learned */
    uint32_t bat_learned_at;                            /* UTC seconds, 0 for never */
    uint8_t sync_mode;                                  /* settings_sync_mode_t */
    uint8_t sync_mode_before_always;                    /* what BOOT double returns to; never `always` (D31) */
    uint8_t sync_time_count;                            /* 1..SETTINGS_SYNC_TIMES_MAX */
    uint16_t sync_times[SETTINGS_SYNC_TIMES_MAX];       /* minutes after midnight, ascending, no repeats */
    uint16_t sync_interval_min;                         /* 15..1440 */
    bool quiet;                                         /* quiet hours (D25) */
    uint16_t quiet_from, quiet_to;                      /* minutes after midnight */
    char ntp[SETTINGS_NTP_MAX][SETTINGS_HOST_LEN];      /* time.ntp; "" for an unused entry */
    bool wx_centre_set;           /* radar.weather.lat and .lon are given; else they follow the location */
    int32_t wx_lat_e4, wx_lon_e4; /* the weather radar's centre */
    uint8_t wx_zoom_q;            /* radar.weather.zoom in quarters: 16..36 */
    bool fl_centre_set;           /* the same for radar.flights */
    int32_t fl_lat_e4, fl_lon_e4;
    uint8_t fl_range_km;    /* 10..100: from the centre to the map's top edge */
    uint16_t fl_min_alt_ft; /* 0..60000 */
    bool fl_ground;         /* aircraft on the ground too */
    uint8_t fl_max;         /* aircraft shown at most, 1..100 */
} settings_t;

/* The sync's and the NTP servers' defaults (spec §14.3): times mode at 05:30, a 60 min interval,
 * quiet hours 23:00-06:00 but off, cz.pool.ntp.org and pool.ntp.org; times again after `always`.
 * The app's defaults include them, so a settings.json from before M5 syncs. */
void settings_sync_defaults(settings_t *out);

/* sync.mode just changed from `prev_mode` (the menu, a page): entering `always` from another mode
 * remembers that one in sync_mode_before_always (D31). */
void settings_remember_mode(settings_t *s, uint8_t prev_mode);
/* `s` replaces `before` whole (a page's settings, a restored backup): entering `always` remembers
 * the mode left, unless `s` names a mode_before_always of its own. */
void settings_replaced(settings_t *s, const settings_t *before);
/* BOOT double on the dashboard (spec §5.6, D31): sync mode `always` on, remembering the mode
 * before, or off, back to the mode it remembers. */
void settings_toggle_always(settings_t *s);

/* The radars' defaults (spec §14.3): zoom 6.5; a range of 50 km, every altitude, none on the ground,
 * 100 aircraft; both centres on out->lat_e4 and lon_e4, so set the location first. */
void settings_radar_defaults(settings_t *out);

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
