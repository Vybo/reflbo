#include "gesture.h"

enum {
    ST_IDLE,
    ST_PRESSED,     /* first press held, timing a long press */
    ST_WAIT_SECOND, /* released, waiting for a second press */
    ST_IGNORE,      /* a gesture fired while pressed; ignore input until release */
};

static bool reached(uint32_t now_ms, uint32_t since_ms, uint32_t span_ms)
{
    return (uint32_t)(now_ms - since_ms) >= span_ms;
}

void gesture_init(gesture_recogniser_t *g, gesture_config_t config)
{
    *g = (gesture_recogniser_t){ .config = config, .state = ST_IDLE };
}

void gesture_set_config(gesture_recogniser_t *g, gesture_config_t config)
{
    g->config = config;
}

static gesture_t on_edge(gesture_recogniser_t *g, bool pressed, uint32_t now_ms)
{
    if (pressed) {
        if (g->state == ST_WAIT_SECOND) {
            g->state = ST_IGNORE;
            return GESTURE_DOUBLE;
        }
        g->state = ST_PRESSED;
        g->mark_ms = now_ms;
        return GESTURE_NONE;
    }
    if (g->state == ST_PRESSED) {
        if (g->config.double_enabled) {
            g->state = ST_WAIT_SECOND;
            g->mark_ms = now_ms;
            return GESTURE_NONE;
        }
        g->state = ST_IDLE;
        return GESTURE_SHORT;
    }
    g->state = ST_IDLE; /* release after a long or double press */
    return GESTURE_NONE;
}

gesture_t gesture_update(gesture_recogniser_t *g, bool pressed, uint32_t now_ms)
{
    gesture_t fired = GESTURE_NONE;
    if (pressed != g->level) {
        if (g->have_edge && !reached(now_ms, g->edge_ms, GESTURE_DEBOUNCE_MS)) {
            g->recheck = true; /* bounce, or a press shorter than the debounce time: look again later */
        } else {
            g->level = pressed;
            g->have_edge = true;
            g->edge_ms = now_ms;
            g->recheck = false;
            fired = on_edge(g, pressed, now_ms);
        }
    } else if (g->recheck && reached(now_ms, g->edge_ms, GESTURE_DEBOUNCE_MS)) {
        g->recheck = false; /* the level settled where it was */
    }
    if (fired != GESTURE_NONE) {
        return fired;
    }
    if (g->state == ST_PRESSED && reached(now_ms, g->mark_ms, g->config.long_ms)) {
        g->state = ST_IGNORE;
        return GESTURE_LONG;
    }
    if (g->state == ST_WAIT_SECOND && reached(now_ms, g->mark_ms, GESTURE_DOUBLE_MS)) {
        g->state = ST_IDLE;
        return GESTURE_SHORT;
    }
    return GESTURE_NONE;
}

void gesture_ignore_press(gesture_recogniser_t *g, uint32_t now_ms)
{
    g->state = ST_IGNORE; /* as after a gesture that fired while pressed: only a release ends it */
    g->level = true;
    g->have_edge = true; /* the release is debounced from here, like any edge */
    g->edge_ms = now_ms;
    g->recheck = false;
}

gesture_t gesture_tap(gesture_recogniser_t *g, uint32_t now_ms)
{
    g->state = ST_PRESSED;
    g->level = false;
    g->have_edge = true;
    g->edge_ms = now_ms;
    g->recheck = false;
    return on_edge(g, false, now_ms);
}

/* A real deadline never equals GESTURE_NO_DEADLINE; one that would lands 1 ms early. */
static uint32_t at(uint32_t ms)
{
    return ms == GESTURE_NO_DEADLINE ? ms - 1 : ms;
}

uint32_t gesture_deadline(const gesture_recogniser_t *g)
{
    uint32_t deadline = GESTURE_NO_DEADLINE;
    if (g->state == ST_PRESSED) {
        deadline = at(g->mark_ms + g->config.long_ms);
    } else if (g->state == ST_WAIT_SECOND) {
        deadline = at(g->mark_ms + GESTURE_DOUBLE_MS);
    }
    if (g->recheck) {
        uint32_t recheck = at(g->edge_ms + GESTURE_DEBOUNCE_MS);
        if (deadline == GESTURE_NO_DEADLINE || (int32_t)(recheck - deadline) < 0) {
            deadline = recheck;
        }
    }
    return deadline;
}

bool gesture_busy(const gesture_recogniser_t *g)
{
    /* A held press whose gesture has fired waits only for its release, which gives nothing: the
     * board may sleep meanwhile, leaving that button out of the wake sources (D16). */
    return g->state == ST_PRESSED || g->state == ST_WAIT_SECOND || g->recheck;
}
