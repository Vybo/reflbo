#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "energy.h"

/*
 * SolaX Cloud's Developer API (D37, developer.solaxcloud.com): an application's client credentials for a bearer
 * token, the plant and its devices, their real-time data and the plant's statistics for today, into one reading in
 * our signs. SolaX's own signs: power to the grid and charging the battery are positive. A commercial plant
 * (businessType 4) reports power in kW. Pure C on cJSON, host-buildable; the sync task fetches (spec §11.6).
 */

#define ENERGY_DEV_TOKEN_MAX 1024 /* an access token, NUL included */
#define ENERGY_DEV_ID_MAX 40      /* a plant id or a serial number, NUL included */
#define ENERGY_DEV_URL_MAX 320
#define ENERGY_DEV_BODY_MAX 256
#define ENERGY_DEV_TOKEN_S 3600 /* a token's life when the reply names none */
#define ENERGY_DEV_RENEW_S 600  /* a token with less left is renewed first */
#define ENERGY_DEV_STATS_PATH "/openapi/v2/plant/energy/get_stat_data"
#define ENERGY_DEV_TOKEN_PATH "/openapi/auth/oauth/token"

/* SolaX's regions; the values match settings_energy_region_t. */
typedef enum { ENERGY_DEV_EU, ENERGY_DEV_CN, ENERGY_DEV_IN } energy_dev_region_t;
/* SolaX's deviceType. */
typedef enum { ENERGY_DEV_INVERTER = 1, ENERGY_DEV_BATTERY = 2, ENERGY_DEV_METER = 3 } energy_dev_device_t;

/* What a step finds once and keeps: the plant and its devices' serial numbers, "" for a device it has none of. */
typedef struct {
    char plant_id[ENERGY_DEV_ID_MAX];
    uint8_t business;              /* businessType: 1 residential, 4 commercial */
    char sn[3][ENERGY_DEV_ID_MAX]; /* the inverter's, the battery's and the meter's: [device - 1] */
} energy_dev_site_t;

/* The real-time values the devices reported, gathered device by device; a value counts only with its `have_`. */
typedef struct {
    bool have_pv, have_ac, have_eps, have_grid, have_meter, have_bat, have_soc, have_yield;
    double pv_w;      /* the panels: MPPTTotalInputPower, else the mpptMap's ...Power entries added up */
    double ac_w;      /* the inverter's output: its phases, acPower1-3, added up, else totalActivePower */
    double eps_w;     /* its backup port's output: EPSL1-3ActivePower added up */
    double feed_w;    /* to the grid +: the meter's totalActivePower, else the inverter's gridPower */
    double bat_w;     /* charging +: chargeDischargePower */
    double soc;       /* batterySOC, % */
    double yield_kwh; /* dailyYield */
    uint32_t at;      /* the newest dataTime, UTC; 0 without */
} energy_dev_now_t;

/* Today's row of the plant's statistics for the month, kWh; `found` false without one, and a value counts only with
 * its `have_`. */
typedef struct {
    bool found;
    bool have_pv, have_to, have_from, have_load;
    double pv_kwh, to_kwh, from_kwh, load_kwh;
} energy_dev_today_t;

/* The region's base and `path`: "https://openapi-eu.solaxcloud.com/..."; 0 (and "") for a region it doesn't know. */
size_t energy_dev_url(char *out, size_t size, energy_dev_region_t region, const char *path);
/* The token request's form body; 0 when the id or the secret holds other than letters, digits, - and _. */
size_t energy_dev_token_body(char *out, size_t size, const char *client_id, const char *client_secret);
/* A token reply: `code` 0 and `result.access_token`, with its life `result.expires_in` (ENERGY_DEV_TOKEN_S
 * without). False with the reason, SolaX's `message` where it gave one. */
bool energy_dev_parse_token(const char *json, size_t len, char *token, size_t token_size, uint32_t *life_s,
                            char *err, size_t err_size);
/* Whether a token kept until `until` (UTC) still serves at `now` for one more step. */
bool energy_dev_token_fresh(uint32_t until, uint32_t now);

/* The requests' paths with their queries: the plants of a business type, a plant's devices of a type and a device's
 * real-time data; and the statistics' JSON body for a month. 0 when an id holds other than letters, digits and -. */
size_t energy_dev_plants_path(char *out, size_t size, int business);
size_t energy_dev_devices_path(char *out, size_t size, const energy_dev_site_t *site, energy_dev_device_t device);
size_t energy_dev_realtime_path(char *out, size_t size, const energy_dev_site_t *site, energy_dev_device_t device);
size_t energy_dev_stats_body(char *out, size_t size, const energy_dev_site_t *site, int year, int month);

/* The replies, each with `code` 10000; false with SolaX's `message` (or "code N") otherwise. The first plant of the
 * page into `site` (its plant_id stays "" when there is none), the first device of a type into `site->sn`, a device's
 * real-time values into `now`, and today's row (`year`, `month`, `day`, local) of the month's statistics. */
bool energy_dev_parse_plant(const char *json, size_t len, energy_dev_site_t *site, char *err, size_t err_size);
bool energy_dev_parse_device(const char *json, size_t len, energy_dev_device_t device, energy_dev_site_t *site,
                             char *err, size_t err_size);
bool energy_dev_parse_realtime(const char *json, size_t len, energy_dev_device_t device, int business,
                               energy_dev_now_t *now, char *err, size_t err_size);
bool energy_dev_parse_today(const char *json, size_t len, int year, int month, int day, energy_dev_today_t *today,
                            char *err, size_t err_size);

/* Whether a failed request was SolaX refusing it: HTTP 401 or 403, or a reply whose `code` isn't the OK one, so a kept
 * token or plant may be stale and one more try after a new login can help. No reply (a timeout, the radio's budget),
 * a rate limit (429), a server's error, or a reply without a code or with the OK one, is not. */
bool energy_dev_refused(int http_status, const char *json, size_t len);

/* The reading in our signs from what came: the time is the newest dataTime, else `fallback_at`; with today's row
 * its totals are today's (`today`), and without one they are missing for the day. False without the panels' or the
 * inverter's power. */
bool energy_dev_reading(const energy_dev_now_t *now, const energy_dev_today_t *today, uint32_t fallback_at,
                        energy_reading_t *out);
