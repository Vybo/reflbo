#include "ha_store.h"

#include <stdio.h>
#include <string.h>

void ha_store_init(ha_store_t *s)
{
    memset(s, 0, sizeof(*s));
}

int ha_store_find(const ha_store_t *s, const char *key)
{
    for (int i = 0; key != NULL && i < s->count; i++) {
        if (strcmp(s->entry[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

void ha_store_rebuild(ha_store_t *s, const ha_fields_t *f)
{
    int n = f->count < HA_FIELDS_MAX ? f->count : HA_FIELDS_MAX;
    int old_count = s->count < HA_FIELDS_MAX ? s->count : HA_FIELDS_MAX;
    int8_t src[HA_FIELDS_MAX]; /* the old entry each mapping keeps, or -1 */
    int8_t at[HA_FIELDS_MAX];  /* the old entry in each place now, or -1 */
    int8_t pos[HA_FIELDS_MAX]; /* the place of each old entry now */
    for (int p = 0; p < HA_FIELDS_MAX; p++) {
        at[p] = (int8_t)(p < old_count ? p : -1);
        pos[p] = (int8_t)p;
        src[p] = -1;
    }
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < old_count && src[i] < 0; j++) {
            if (strcmp(s->entry[j].key, f->field[i].key) == 0 && s->entry[j].kind == f->field[i].kind) {
                src[i] = (int8_t)j;
            }
        }
    }
    /* In place, with no copy of the block: each kept entry swaps into its place, and what was there takes the
     * kept one's old place, where a later mapping may still find it. */
    for (int i = 0; i < n; i++) {
        int k = src[i];
        if (k < 0 || pos[k] == i) {
            continue;
        }
        int p = pos[k], other = at[i];
        ha_entry_t tmp = s->entry[i];
        s->entry[i] = s->entry[p];
        s->entry[p] = tmp;
        at[i] = (int8_t)k;
        pos[k] = (int8_t)i;
        at[p] = (int8_t)other;
        if (other >= 0) {
            pos[other] = (int8_t)p;
        }
    }
    for (int i = 0; i < HA_FIELDS_MAX; i++) {
        if (i >= n || src[i] < 0) {
            memset(&s->entry[i], 0, sizeof(s->entry[i])); /* a new mapping, or none: no value */
        }
        if (i < n) {
            const ha_field_t *m = &f->field[i];
            ha_entry_t *e = &s->entry[i];
            snprintf(e->key, sizeof(e->key), "%s", m->key);
            snprintf(e->label, sizeof(e->label), "%s", m->label);
            snprintf(e->unit, sizeof(e->unit), "%s", m->unit);
            e->kind = m->kind;
            e->ttl_s = m->ttl_s;
        }
    }
    s->count = (uint8_t)n;
}

void ha_store_set_default_ttl(ha_store_t *s, uint32_t ttl_s)
{
    s->default_ttl_s = ttl_s;
}

ha_freshness_t ha_store_freshness(const ha_store_t *s, int i, time_t now)
{
    if (i < 0 || i >= s->count || s->entry[i].updated == 0 || s->entry[i].none) {
        return HA_MISSING; /* none yet, or HA said there is none (D40) */
    }
    const ha_entry_t *e = &s->entry[i];
    uint32_t ttl = e->ttl_s != 0 ? e->ttl_s : s->default_ttl_s;
    return ttl != 0 && now > (time_t)e->updated + (time_t)ttl ? HA_STALE : HA_FRESH;
}

bool ha_store_set(ha_store_t *s, int i, const ha_value_t *v, time_t now)
{
    if (i < 0 || i >= s->count || v->kind != s->entry[i].kind) {
        return false;
    }
    ha_entry_t *e = &s->entry[i];
    ha_freshness_t was = ha_store_freshness(s, i, now);
    bool same = v->none ? e->none
                : e->none ? false
                : e->kind == HA_KIND_NUMBER ? e->number == v->number && e->decimals == v->decimals
                : e->kind == HA_KIND_TIME   ? e->time == v->time && e->date_only == v->date_only
                                            : strcmp(e->text, v->text) == 0;
    e->none = v->none;
    e->date_only = v->date_only;
    if (e->kind == HA_KIND_TIME) {
        e->time = v->time;
    } else {
        e->number = v->number;
    }
    e->decimals = v->decimals;
    snprintf(e->text, sizeof(e->text), "%s", v->text);
    bool had = e->updated != 0;
    e->updated = (uint32_t)now;
    return v->none ? !(had && same) : !(was == HA_FRESH && same);
}

/* A time moved by `delta_s`, kept at 1 or later: 0 means none. */
static uint32_t shifted(uint32_t t, int64_t delta_s)
{
    int64_t moved = (int64_t)t + delta_s;
    return t == 0 ? 0 : moved < 1 ? 1 : moved > UINT32_MAX ? UINT32_MAX : (uint32_t)moved;
}

void ha_store_shift_time(ha_store_t *s, int64_t delta_s)
{
    for (int i = 0; i < s->count && i < HA_FIELDS_MAX; i++) {
        s->entry[i].updated = shifted(s->entry[i].updated, delta_s);
    }
    s->message_at = shifted(s->message_at, delta_s);
}

void ha_store_set_message(ha_store_t *s, const char *text, time_t now)
{
    snprintf(s->message, sizeof(s->message), "%s", text);
    s->message_at = text[0] != '\0' ? (uint32_t)now : 0;
    s->message_dismissed = false;
}

ha_freshness_t ha_store_message_freshness(const ha_store_t *s, time_t now)
{
    if (s->message_at == 0) {
        return HA_MISSING;
    }
    return now > (time_t)s->message_at + HA_MESSAGE_FRESH_S ? HA_STALE : HA_FRESH;
}

bool ha_store_banner(const ha_store_t *s, time_t now)
{
    return !s->message_dismissed && ha_store_message_freshness(s, now) == HA_FRESH;
}

void ha_store_dismiss(ha_store_t *s)
{
    s->message_dismissed = true;
}

void ha_store_seal(ha_store_t *s)
{
    util_snapshot_seal(s, sizeof(*s), HA_STORE_MAGIC, HA_STORE_VERSION);
}

bool ha_store_valid(const ha_store_t *s)
{
    return util_snapshot_valid(s, sizeof(*s), HA_STORE_MAGIC, HA_STORE_VERSION);
}
