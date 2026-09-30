#include "battery_learn.h"

#include <string.h>

#include "util_crc32.h"

#define HOUR_S       3600u
#define PLATEAU_MV   10 /* this far below the plateau, the charger has let go */

void battery_learn_start(battery_learn_t *l)
{
    memset(l, 0, sizeof(*l));
    l->state = BATTERY_LEARN_WAITING;
}

void battery_learn_stop(battery_learn_t *l)
{
    memset(l, 0, sizeof(*l));
}

battery_learn_state_t battery_learn_state(const battery_learn_t *l)
{
    return (battery_learn_state_t)l->state;
}

const char *battery_learn_state_name(battery_learn_state_t state)
{
    static const char *const k_names[] = { "off", "waiting", "recording", "done", "failed" };
    return (unsigned)state < sizeof(k_names) / sizeof(k_names[0]) ? k_names[state] : "?";
}

/* The start of a discharge to come: the latest reading on the charger. */
static void at_plateau(battery_learn_t *l, uint32_t now_s, int mv)
{
    if (l->count == 0 || mv > l->plateau_mv) {
        l->plateau_mv = (uint16_t)mv;
    }
    l->state = BATTERY_LEARN_WAITING;
    l->shift = 0;
    l->count = 1;
    l->mv[0] = (uint16_t)mv;
    l->start_s = now_s;
}

static uint32_t interval_s(const battery_learn_t *l)
{
    return HOUR_S << l->shift;
}

/* Keeps every other point, twice as far apart, when the buffer is full. */
static void thin_out(battery_learn_t *l)
{
    for (int i = 0; i < BATTERY_LEARN_POINTS / 2; i++) {
        l->mv[i] = l->mv[2 * i];
    }
    l->count = BATTERY_LEARN_POINTS / 2;
    l->shift++;
}

bool battery_learn_add(battery_learn_t *l, uint32_t now_s, int mv, battery_state_t state)
{
    if (l->state == BATTERY_LEARN_OFF || l->state == BATTERY_LEARN_DONE || mv <= 0) {
        return false;
    }
    bool gap = l->last_s != 0 && now_s > l->last_s && now_s - l->last_s > BATTERY_LEARN_GAP_S;
    l->last_s = now_s;
    if (state == BATTERY_CHARGING || (l->state != BATTERY_LEARN_RECORDING && state == BATTERY_FULL) ||
        (l->state == BATTERY_LEARN_WAITING && l->count > 0 && mv >= l->plateau_mv - PLATEAU_MV)) {
        at_plateau(l, now_s, mv);
        return false;
    }
    if (l->state == BATTERY_LEARN_FAILED) {
        return false; /* until the next charge */
    }
    if (gap && l->state == BATTERY_LEARN_RECORDING) {
        l->state = BATTERY_LEARN_WAITING; /* the timeline has a hole: wait for the next charge */
        l->count = 0;
        return false;
    }
    if (l->state == BATTERY_LEARN_WAITING) {
        if (l->count == 0) {
            return false; /* no charge seen yet */
        }
        l->state = BATTERY_LEARN_RECORDING;
    }
    if (mv <= BATTERY_CRITICAL_MV) {
        l->end_mv = (uint16_t)mv;
        bool enough = now_s - l->start_s >= BATTERY_LEARN_MIN_H * HOUR_S;
        l->state = enough ? BATTERY_LEARN_DONE : BATTERY_LEARN_FAILED;
        return enough;
    }
    if (now_s >= l->start_s + l->count * interval_s(l)) {
        if (l->count == BATTERY_LEARN_POINTS) {
            thin_out(l);
        }
        if (now_s >= l->start_s + l->count * interval_s(l)) {
            l->mv[l->count++] = (uint16_t)mv;
        }
    }
    return false;
}

/* The voltage `t` seconds into the discharge: the points, then the reading at the end. */
static int mv_at(const battery_learn_t *l, uint32_t t)
{
    uint32_t step = interval_s(l);
    uint32_t i = t / step;
    if (i + 1 < l->count) {
        uint32_t f = t - i * step;
        return l->mv[i] + ((int)l->mv[i + 1] - (int)l->mv[i]) * (int64_t)f / (int64_t)step;
    }
    uint32_t last_t = (uint32_t)(l->count - 1) * step, end_t = l->last_s - l->start_s;
    if (end_t <= last_t) {
        return l->end_mv;
    }
    int64_t f = t > last_t ? t - last_t : 0;
    return l->mv[l->count - 1] + ((int)l->end_mv - (int)l->mv[l->count - 1]) * f / (int64_t)(end_t - last_t);
}

bool battery_learn_curve(const battery_learn_t *l, uint16_t curve[BATTERY_CURVE_POINTS])
{
    if (l->state != BATTERY_LEARN_DONE || l->count == 0) {
        return false;
    }
    uint32_t end_t = l->last_s - l->start_s;
    for (int p = 0; p < BATTERY_CURVE_POINTS; p++) { /* p * 5 % is left at (1 - p/20) of the time */
        uint32_t t = (uint32_t)((uint64_t)end_t * (uint32_t)(BATTERY_CURVE_POINTS - 1 - p) / (BATTERY_CURVE_POINTS - 1));
        int v = mv_at(l, t);
        if (p > 0 && v <= curve[p - 1]) {
            v = curve[p - 1] + 1; /* a lookup needs it rising */
        }
        curve[p] = (uint16_t)v;
    }
    return true;
}

uint32_t battery_learn_hours(const battery_learn_t *l)
{
    return l->count == 0 || l->last_s < l->start_s ? 0 : (l->last_s - l->start_s) / HOUR_S;
}

int battery_learn_points(const battery_learn_t *l)
{
    return l->count;
}

static uint32_t shifted(uint32_t t, int64_t delta_s)
{
    int64_t v = (int64_t)t + delta_s;
    return t == 0 ? 0 : v < 0 ? 0 : v > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)v;
}

void battery_learn_shift_time(battery_learn_t *l, int64_t delta_s)
{
    l->start_s = shifted(l->start_s, delta_s);
    l->last_s = shifted(l->last_s, delta_s);
}

#define PACK_MAGIC   0x4E524C42u /* "BLRN" */
#define PACK_VERSION 1

size_t battery_learn_pack(const battery_learn_t *l, uint8_t *out, size_t size)
{
    if (size < BATTERY_LEARN_PACKED_MAX) {
        return 0;
    }
    uint32_t head[1] = { PACK_MAGIC ^ (PACK_VERSION << 24 | (uint32_t)sizeof(*l)) };
    memcpy(out, head, 4);
    memcpy(out + 4, l, sizeof(*l));
    uint32_t crc = util_crc32(0, out, 4 + sizeof(*l));
    memcpy(out + 4 + sizeof(*l), &crc, 4);
    return BATTERY_LEARN_PACKED_MAX;
}

bool battery_learn_unpack(battery_learn_t *l, const uint8_t *in, size_t len)
{
    uint32_t head, crc;
    if (len != BATTERY_LEARN_PACKED_MAX) {
        return false;
    }
    memcpy(&head, in, 4);
    memcpy(&crc, in + 4 + sizeof(*l), 4);
    if (head != (PACK_MAGIC ^ (PACK_VERSION << 24 | (uint32_t)sizeof(*l))) || crc != util_crc32(0, in, 4 + sizeof(*l))) {
        return false;
    }
    memcpy(l, in + 4, sizeof(*l));
    if (l->state == BATTERY_LEARN_WAITING) {
        l->count = 0;
    }
    return l->state <= BATTERY_LEARN_FAILED;
}
