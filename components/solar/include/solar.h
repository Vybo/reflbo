#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * The PV forecast (spec §11.5, D35): the providers' requests and replies, our PV model, and the
 * forecast as the device keeps it, by local quarter hour. Times map to local quarter hours by the
 * TZ variable. Pure C on cJSON, host-buildable; the sync task fetches.
 */

#define SOLAR_STEPS 96           /* local quarter hours a day */
#define SOLAR_DAYS 3             /* days with a total: the forecast's first, the next, the one after */
#define SOLAR_UNIT_W 10          /* a quarter hour's power is kept in tens of W: 655 kW at most */
#define SOLAR_WH_NONE UINT32_MAX /* a day the source sent nothing for */
#define SOLAR_PLANES_MAX 2
#define SOLAR_URL_MAX 320

/* A roof plane (spec §11.5): azimuth 0 south, -90 east, 90 west. */
typedef struct {
    float kwp;   /* 0.1..100 */
    int tilt;    /* 0..90° */
    int azimuth; /* -180..180° */
} solar_plane_t;

/* The forecast as kept (the snapshot, /fs/state/solar.bin): local day `day` and the next by quarter
 * hour, and three days' totals. */
typedef struct {
    int32_t day;                /* the local day of q[0], days since 1970-01-01; 0 = no forecast */
    uint32_t fetched;           /* when it came, UTC */
    uint16_t q[2][SOLAR_STEPS]; /* each quarter hour's mean power, in SOLAR_UNIT_W: `day`, `day` + 1 */
    uint32_t wh[SOLAR_DAYS];    /* each day's energy, Wh, or SOLAR_WH_NONE; the first two are q's */
} solar_forecast_t;

/* A reply on its way into a forecast: each local quarter hour's energy over three days from the
 * local day of the fetch, and which of them the reply covered. */
typedef struct {
    int32_t day;
    float wh[SOLAR_DAYS][SOLAR_STEPS];
    bool got[SOLAR_DAYS][SOLAR_STEPS];
} solar_acc_t;

/* Our model (spec §11.5), a plane's output in W: kWp × GTI ÷ 1000 × (1 − 0.004 × (T_cell − 25 °C))
 * × (1 − losses), with T_cell = T_air + 0.031 × GTI; never below 0. */
float solar_model_w(float kwp, float gti_w_m2, float t_air_c, int losses_pct);

/* Starts with nothing from the local day of `now`. */
void solar_acc_init(solar_acc_t *acc, time_t now);
/* A period's mean power, from `seconds` before `end` to `end` (UTC), over the quarter hours it covers. */
void solar_acc_period(solar_acc_t *acc, time_t end, int seconds, float watts);
/* Power at two instants (Forecast.Solar's points): the line between them, integrated by quarter hour. */
void solar_acc_line(solar_acc_t *acc, time_t t0, float w0, time_t t1, float w1);
/* The inverter's limit on each quarter hour's mean power (our model's `inverter_kw`). */
void solar_acc_cap(solar_acc_t *acc, float watts);
/* The forecast from `acc`, fetched at `fetched` (UTC). Quarter hours the reply didn't cover keep
 * `old`'s of the same local day (Solcast leaves out the past); `old` may be NULL, or `out` itself. */
void solar_acc_finish(const solar_acc_t *acc, const solar_forecast_t *old, uint32_t fetched,
                      solar_forecast_t *out);

/* Local day `day`'s quarter hours: q[0], or q[1] once the forecast's first day is over; NULL for a
 * day the forecast doesn't cover. */
const uint16_t *solar_day(const solar_forecast_t *f, int32_t day);
/* Local day `day`'s energy in Wh, for three days from the forecast's first; else SOLAR_WH_NONE. */
uint32_t solar_day_wh(const solar_forecast_t *f, int32_t day);
/* What is still to come on day `day` from `seconds` into quarter hour `quarter`, Wh; SOLAR_WH_NONE
 * without its quarter hours. */
uint32_t solar_left_wh(const solar_forecast_t *f, int32_t day, int quarter, int seconds);
/* Day `day`'s highest quarter hour: its mean power in W and its index; false without output. */
bool solar_peak(const solar_forecast_t *f, int32_t day, uint32_t *w, int *quarter);

/* Open-Meteo's tilted irradiance for one plane (spec §11.5). Returns the length, or 0 (and "") when
 * the URL doesn't fit. */
size_t solar_open_meteo_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4, const solar_plane_t *plane);
/* Open-Meteo's reply for `plane` through our model, added to `acc`; false with the reason in `err`. */
bool solar_parse_open_meteo(const char *json, size_t len, const solar_plane_t *plane, int losses_pct,
                            solar_acc_t *acc, char *err, size_t err_size);
