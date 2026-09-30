#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "battery_model.h"

/*
 * Learning the battery's own curve from one full discharge (D21). Pure C, host-buildable.
 *
 * Armed, it waits for a charge: readings while charging, or at the charger's plateau, set the
 * start. The discharge begins when the reading falls 10 mV below that plateau, and it is
 * recorded once an hour until the critical level (spec §8). This board's steady load makes time
 * a fair measure of charge, so each 5 % of the time becomes a point of the curve.
 */

#define BATTERY_LEARN_POINTS 256 /* readings kept; they thin out to cover a discharge of any length */
#define BATTERY_LEARN_MIN_H  12  /* a shorter discharge gives no curve: something drew far more */
#define BATTERY_LEARN_GAP_S  7200 /* no reading for this long: the board was off, start again */

typedef enum {
    BATTERY_LEARN_OFF,
    BATTERY_LEARN_WAITING,   /* for a charge */
    BATTERY_LEARN_RECORDING, /* the discharge since the last charge */
    BATTERY_LEARN_DONE,      /* the critical level came: battery_learn_curve() has the result */
    BATTERY_LEARN_FAILED,    /* it came too soon to trust; the next charge tries again */
} battery_learn_state_t;

typedef struct {
    uint8_t state;       /* battery_learn_state_t */
    uint8_t shift;       /* points are 1 h << shift apart */
    uint16_t count;      /* points in mv[] */
    uint16_t plateau_mv; /* the highest reading while charging or full */
    uint16_t end_mv;     /* the reading at the critical level */
    uint32_t start_s;    /* the time of mv[0]: the last reading at the plateau */
    uint32_t last_s;     /* the latest reading: the end, once done */
    uint16_t mv[BATTERY_LEARN_POINTS];
} battery_learn_t;

void battery_learn_start(battery_learn_t *l);
void battery_learn_stop(battery_learn_t *l);
battery_learn_state_t battery_learn_state(const battery_learn_t *l);
const char *battery_learn_state_name(battery_learn_state_t state); /* "off", "waiting", ... */
/* One smoothed reading on a valid clock. True when the discharge just reached the critical level
 * with at least BATTERY_LEARN_MIN_H of it recorded. */
bool battery_learn_add(battery_learn_t *l, uint32_t now_s, int mv, battery_state_t state);
/* The voltage at 0 %, 5 %, ... 100 %, rising; false unless DONE. */
bool battery_learn_curve(const battery_learn_t *l, uint16_t curve[BATTERY_CURVE_POINTS]);
uint32_t battery_learn_hours(const battery_learn_t *l); /* of discharge recorded */
int battery_learn_points(const battery_learn_t *l);
/* The clock was set by delta_s: the session keeps its spacing on the new clock. */
void battery_learn_shift_time(battery_learn_t *l, int64_t delta_s);

/* For LittleFS, so a restart keeps the session: a version, the session and a CRC-32. */
#define BATTERY_LEARN_PACKED_MAX (8 + sizeof(battery_learn_t))
size_t battery_learn_pack(const battery_learn_t *l, uint8_t *out, size_t size);
/* False for another firmware's layout or a damaged copy. A session still waiting for the
 * discharge waits for a fresh reading on the charger, as the charger may have let go meanwhile. */
bool battery_learn_unpack(battery_learn_t *l, const uint8_t *in, size_t len);
