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
#define SETTINGS_MQTT_USER_LEN 64   /* mqtt.user, up to 63 bytes without control characters */
#define SETTINGS_MQTT_PREFIX_LEN 32 /* mqtt.discovery_prefix, up to 31 bytes */
#define SETTINGS_FILE_MAX 4096 /* settings.json as the app reads and writes it */

/* sync.steps (spec §9.3, D35): the data steps that run, as bits. The time always runs. */
typedef enum {
    SETTINGS_STEP_WEATHER = 1 << 0,
    SETTINGS_STEP_AIR = 1 << 1,
    SETTINGS_STEP_RADAR = 1 << 2,
    SETTINGS_STEP_SOLAR = 1 << 3,
    SETTINGS_STEP_ENERGY = 1 << 4,
} settings_step_t;
#define SETTINGS_STEPS_ALL 0x1F

/* solar.source (spec §11.5); the values match solar_source_t in the solar component. */
typedef enum {
    SETTINGS_SOLAR_OFF,
    SETTINGS_SOLAR_OPEN_METEO,
    SETTINGS_SOLAR_FORECAST_SOLAR,
    SETTINGS_SOLAR_SOLCAST,
} settings_solar_source_t;

/* energy.source, energy.region and energy.battery (spec §11.6): SolaX Cloud by its Token ID ("solax") or by its
 * Developer API ("solax-dev", D37) in one of SolaX's regions, or mapped MQTT values ("mqtt", D40, spec §12.11). The
 * regions' and the battery's values match energy_dev_region_t and energy_battery_t. */
typedef enum {
    SETTINGS_ENERGY_OFF,
    SETTINGS_ENERGY_SOLAX,
    SETTINGS_ENERGY_SOLAX_DEV,
    SETTINGS_ENERGY_MQTT,
} settings_energy_source_t;
/* energy.mqtt's values (spec §12.11): each names a number mapping's key, "" for none; the order of
 * energy_mqtt_item_t. */
typedef enum {
    SETTINGS_EM_PV,
    SETTINGS_EM_GRID,
    SETTINGS_EM_LOAD,
    SETTINGS_EM_BATTERY,
    SETTINGS_EM_SOC,
    SETTINGS_EM_YIELD,
    SETTINGS_EM_TO_GRID,
    SETTINGS_EM_FROM_GRID,
    SETTINGS_EM_COUNT,
} settings_energy_mqtt_t;
#define SETTINGS_MQTT_KEY_LEN 24 /* a mapping's key, 1-23 of a-z, 0-9 and _ (HA_KEY_LEN) */
typedef enum { SETTINGS_REGION_EU, SETTINGS_REGION_CN, SETTINGS_REGION_IN } settings_energy_region_t;
typedef enum { SETTINGS_BATTERY_AUTO, SETTINGS_BATTERY_ON, SETTINGS_BATTERY_OFF } settings_energy_battery_t;

#define SETTINGS_PLANES_MAX 2

/* A roof plane (spec §11.5). */
typedef struct {
    uint16_t kwp_e2; /* 10..10000: 0.1 to 100 kWp in hundredths */
    uint8_t tilt;    /* 0..90° */
    int16_t azimuth; /* -180..180°: 0 south, -90 east, 90 west */
} settings_plane_t;

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
    uint8_t sync_steps;     /* settings_step_t bits (D35) */
    uint8_t solar_source;   /* settings_solar_source_t */
    uint8_t solar_plane_count;                         /* 1..SETTINGS_PLANES_MAX */
    settings_plane_t solar_planes[SETTINGS_PLANES_MAX];
    uint8_t solar_losses_pct;      /* 0..50, our model's */
    uint16_t solar_inverter_kw_e2; /* 0..10000: the inverter's limit in hundredths of kW, 0 for none */
    uint8_t energy_source;         /* settings_energy_source_t */
    uint8_t energy_battery;        /* settings_energy_battery_t */
    uint8_t energy_region;         /* settings_energy_region_t: the Developer API's */
    char energy_mqtt[SETTINGS_EM_COUNT][SETTINGS_MQTT_KEY_LEN]; /* energy.mqtt: the mappings' keys (D40); "" for none */
    bool energy_grid_export;       /* energy.mqtt.grid_sign "export": a positive grid value goes out */
    bool energy_bat_discharge;     /* energy.mqtt.battery_sign "discharge": a positive battery value comes out of it */
    bool energy_lifetime;          /* energy.mqtt.totals "lifetime": the grid's counters run since installation */
    bool mqtt_enabled;                          /* MQTT and Home Assistant (spec §12.1, D32) */
    char mqtt_host[SETTINGS_HOST_LEN];          /* the broker: a host name or an address; "" for none */
    uint16_t mqtt_port;                         /* 1..65535 */
    char mqtt_user[SETTINGS_MQTT_USER_LEN];     /* "" logs in without a user */
    bool mqtt_discovery;                        /* publish Home Assistant's discovery configs */
    char mqtt_prefix[SETTINGS_MQTT_PREFIX_LEN]; /* their topics' first part: "homeassistant" */
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

/* The steps', the PV forecast's and the house's defaults (spec §14.3): every step on; no source; one
 * plane of 5 kWp tilted 35° to the south, 14 % losses, no inverter limit; the battery on auto. */
void settings_solar_defaults(settings_t *out);
/* MQTT's defaults (spec §14.3, D32): off, no broker or user, port 1883, discovery on under "homeassistant". */
void settings_mqtt_defaults(settings_t *out);
/* A step's name in sync.steps: "weather", "air", "radar", "solar", "energy". */
const char *settings_step_name(settings_step_t step);
/* Forecast.Solar takes a second plane only with a key (spec §11.5), and the house's energy from MQTT its solar and
 * grid values (spec §12.11): false with the reason. */
bool settings_check_solar(const settings_t *s, bool fs_key_set, char *err, size_t err_size);

/* The secrets a PATCH may carry (spec §10.3, §14.2): write-only, kept in NVS `secrets`, never in the
 * file. */
typedef enum {
    SETTINGS_SECRET_FS_KEY,        /* solar.fs_key */
    SETTINGS_SECRET_SOLCAST_KEY,   /* solar.solcast_key */
    SETTINGS_SECRET_SOLCAST_SITE1, /* solar.solcast_sites[0] */
    SETTINGS_SECRET_SOLCAST_SITE2, /* solar.solcast_sites[1] */
    SETTINGS_SECRET_SOLAX_TOKEN,   /* energy.solax_token */
    SETTINGS_SECRET_SOLAX_SN,      /* energy.solax_sn */
    SETTINGS_SECRET_SOLAX_CLIENT_ID,     /* energy.solax_client_id: the Developer API's application (D37) */
    SETTINGS_SECRET_SOLAX_CLIENT_SECRET, /* energy.solax_client_secret */
    SETTINGS_SECRET_MQTT_PASS,           /* mqtt.password: the broker's (spec §12.1, D32) */
    SETTINGS_SECRET_COUNT,
} settings_secret_t;
#define SETTINGS_SECRET_LEN 64

typedef struct {
    bool given[SETTINGS_SECRET_COUNT];                    /* the patch names it; "" or null clears it */
    char value[SETTINGS_SECRET_COUNT][SETTINGS_SECRET_LEN];
} settings_secrets_t;

/* Its key in NVS `secrets`: "fs_key", "solcast_key", "solcast_site1", ... */
const char *settings_secret_key(settings_secret_t secret);
/* `patch` without its secrets into `out`, the secrets into `secrets`. Each must be one the requests
 * can carry: letters and digits (the Solcast key also - and _, its sites -), 63 at most. Returns the
 * length written, or 0 with the reason in `err`. */
size_t settings_take_secrets(const char *patch, char *out, size_t size, settings_secrets_t *secrets, char *err,
                             size_t err_size);
/* Whether `secrets` sets or clears Solcast's key or a site: its budget then starts over (spec §11.5). */
bool settings_secrets_solcast(const settings_secrets_t *secrets);
/* Whether `secrets` sets or clears the Developer API's client id or secret: the device then logs in afresh and finds
 * its plant again (D37). */
bool settings_secrets_solax_dev(const settings_secrets_t *secrets);

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
