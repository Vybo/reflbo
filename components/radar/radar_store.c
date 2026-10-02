#include <string.h>

#include "radar.h"

void radar_store_init(radar_store_t *s, const png_mem_t *mem)
{
    memset(s, 0, sizeof(*s));
    if (mem != NULL) {
        s->mem = *mem;
    }
}

static void drop(radar_store_t *s, int i)
{
    radar_frame_free(&s->frames[i], &s->mem);
    memmove(&s->frames[i], &s->frames[i + 1], (size_t)(s->count - i - 1) * sizeof(s->frames[0]));
    s->count--;
}

void radar_store_put(radar_store_t *s, radar_frame_t *f, int keep)
{
    for (int i = 0; i < s->count; i++) {
        if (s->frames[i].time == f->time) {
            drop(s, i); /* the newer copy of the same frame wins */
            break;
        }
    }
    if (s->count == RADAR_LOOP_FRAMES) {
        if (f->time < s->frames[0].time) { /* older than all the full store holds */
            radar_frame_free(f, &s->mem);
            return;
        }
        drop(s, 0);
    }
    int at = s->count;
    while (at > 0 && s->frames[at - 1].time > f->time) {
        at--;
    }
    memmove(&s->frames[at + 1], &s->frames[at], (size_t)(s->count - at) * sizeof(s->frames[0]));
    s->frames[at] = *f;
    s->count++;
    f->levels = NULL; /* the store owns it now */
    radar_store_keep(s, keep);
}

void radar_store_keep(radar_store_t *s, int keep)
{
    keep = keep < 1 ? 1 : keep > RADAR_LOOP_FRAMES ? RADAR_LOOP_FRAMES : keep;
    while (s->count > keep) {
        drop(s, 0);
    }
}

const radar_frame_t *radar_store_newest(const radar_store_t *s)
{
    return s->count > 0 ? &s->frames[s->count - 1] : NULL;
}

bool radar_store_has(const radar_store_t *s, uint32_t time)
{
    for (int i = 0; i < s->count; i++) {
        if (s->frames[i].time == time) {
            return true;
        }
    }
    return false;
}

int radar_store_missing(const radar_store_t *s, uint32_t newest, uint32_t step_s, uint32_t *out, int max)
{
    int n = 0;
    for (int k = 0; k < RADAR_LOOP_FRAMES && n < max && (uint64_t)k * step_s <= newest; k++) {
        uint32_t t = newest - (uint32_t)k * step_s;
        if (!radar_store_has(s, t)) {
            out[n++] = t;
        }
    }
    return n;
}

int radar_wanted(uint32_t newest, uint32_t step_s, int want, const uint32_t *have, int have_count, uint32_t *out)
{
    want = want < 1 ? 1 : want > RADAR_LOOP_FRAMES ? RADAR_LOOP_FRAMES : want;
    int n = 0;
    for (int k = 0; k < want && (uint64_t)k * step_s <= newest; k++) {
        uint32_t t = newest - (uint32_t)k * step_s;
        bool kept = false;
        for (int i = 0; i < have_count && !kept; i++) {
            kept = have[i] == t;
        }
        if (!kept) {
            out[n++] = t;
        }
    }
    return n;
}

void radar_store_clear(radar_store_t *s)
{
    while (s->count > 0) {
        drop(s, s->count - 1);
    }
}
