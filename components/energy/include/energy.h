#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The house's energy (spec §11.6, D36): SolaX Cloud's real-time request and reply, our signs, the
 * battery's rule, and today's totals and quarter hours from the readings. Local days and quarter
 * hours follow the TZ variable. Pure C on cJSON, host-buildable; the sync task fetches.
 */

#define ENERGY_URL_MAX 256
#define ENERGY_KEY_MAX 64          /* the token or the registration number, NUL included */
#define ENERGY_STEPS 96            /* local quarter hours a day */
#define ENERGY_UNIT_W 10           /* a quarter hour's mean output is kept in tens of W */
#define ENERGY_NONE UINT16_MAX     /* a quarter hour without a reading */
#define ENERGY_WH_NONE UINT32_MAX  /* a total there is no reading for */
#define ENERGY_FRESH_S (15 * 60)   /* a reading is fresh for 15 min (spec §5.1) */
#define ENERGY_AHEAD_S (24 * 3600) /* a reading further ahead of the clock is refused */
#define ENERGY_MIDNIGHT_S 3600     /* the reading taken as midnight's lies within an hour of it */

/* energy.battery; the values match settings_energy_battery_t. */
typedef enum { ENERGY_BATTERY_AUTO, ENERGY_BATTERY_ON, ENERGY_BATTERY_OFF } energy_battery_t;

/* One reading, in our signs (W unless named). */
typedef struct {
    uint32_t at;          /* the inverter's upload, UTC; 0 = none */
    int32_t pv_w;         /* the panels: its strings added up, else the inverter's output */
    int32_t grid_w;       /* + from the grid, - to it */
    int32_t load_w;       /* the house */
    int32_t bat_w;        /* + charging, - discharging */
    int16_t soc;          /* the battery's charge, %; -1 when the reply has none */
    uint8_t inverter;     /* SolaX's inverter type, 0 when unknown */
    uint32_t yield_wh;    /* produced today, as the inverter counts it */
    uint32_t to_grid_wh;  /* the totals since installation */
    uint32_t from_grid_wh;
} energy_reading_t;

/* A local day from the readings: its midnight totals and each quarter hour's mean output. */
typedef struct {
    int32_t day;                       /* days since 1970-01-01; 0 = none yet */
    uint32_t base_at;                  /* the reading taken as its midnight's, UTC; 0 = none */
    uint32_t base_to_wh, base_from_wh; /* its totals */
    uint32_t next_at;                  /* the latest reading in the hour before the next midnight */
    uint32_t next_to_wh, next_from_wh;
    uint32_t last_at;                  /* the last upload counted: one is counted once */
    uint16_t q[ENERGY_STEPS];          /* mean output in ENERGY_UNIT_W, or ENERGY_NONE */
    uint8_t n[ENERGY_STEPS];           /* the readings in each mean */
} energy_day_t;

/* SolaX Cloud's real-time request. A token or registration number of other characters than letters
 * and digits makes no URL (0 and ""), as both go into the query as they are. */
size_t energy_solax_url(char *out, size_t size, const char *token, const char *sn);
/* Its reply; false with the reason in `err`: SolaX's `exception` when it says `success: false`. */
bool energy_parse_solax(const char *json, size_t len, energy_reading_t *out, char *err, size_t err_size);
/* The battery shows: `on`; with `auto`, a hybrid inverter or a charge above 0 % (spec §11.6). */
bool energy_battery_shown(energy_battery_t setting, const energy_reading_t *r);
/* At most ENERGY_FRESH_S old at `now` (UTC), or ahead of it (the inverter's clock) by no more. */
bool energy_fresh(const energy_reading_t *r, uint32_t now);
/* More than ENERGY_AHEAD_S ahead of `now` (UTC): a dongle's clock or a site's time zone gone wrong. Such a reading
 * would hold the day's totals until its own day came, so the Energy step refuses it; false without a clock (0). */
bool energy_reading_ahead(const energy_reading_t *r, uint32_t now);

void energy_day_init(energy_day_t *d);
/* A reading into its local day: a later day starts afresh, an earlier one is left out. */
void energy_day_add(energy_day_t *d, const energy_reading_t *r);
/* Local day `day`'s quarter hours, or NULL when the readings are of another day. */
const uint16_t *energy_day_q(const energy_day_t *d, int32_t day);
/* Today's totals to and from the grid at reading `r`, since day `day`'s midnight; ENERGY_WH_NONE
 * without a reading near that midnight, or when `r` is of another day. */
uint32_t energy_to_grid_wh(const energy_day_t *d, const energy_reading_t *r, int32_t day);
uint32_t energy_from_grid_wh(const energy_day_t *d, const energy_reading_t *r, int32_t day);
/* Own use: the share of today's production the house kept, %; -1 without a production or an export. */
int energy_self_pct(const energy_day_t *d, const energy_reading_t *r, int32_t day);
/* The local day of reading `r`, days since 1970-01-01; 0 for none. */
int32_t energy_reading_day(const energy_reading_t *r);
