#include "ha_session.h"

#include <string.h>

#define BACKOFF_FIRST_MS 10000
#define BACKOFF_MAX_MS 300000
#define ALWAYS_EXPECTED_S 600 /* state goes out every 5 min in sync mode `always` (spec §9.3) */

uint32_t ha_backoff_ms(int failures)
{
    uint32_t ms = BACKOFF_FIRST_MS;
    for (int i = 0; i < failures && ms < BACKOFF_MAX_MS; i++) {
        ms *= 2;
    }
    return ms < BACKOFF_MAX_MS ? ms : BACKOFF_MAX_MS;
}

int ha_topic_index(const char *const *topics, int count, const char *topic, size_t len)
{
    for (int i = 0; i < count; i++) {
        if (strlen(topics[i]) == len && memcmp(topics[i], topic, len) == 0) {
            return i;
        }
    }
    return -1;
}

int ha_topics(const ha_fields_t *f, const char *out[HA_FIELDS_MAX])
{
    int n = 0;
    for (int i = 0; i < f->count && i < HA_FIELDS_MAX; i++) {
        const char *t = f->field[i].topic;
        if (ha_topic_index(out, n, t, strlen(t)) < 0) {
            out[n++] = t;
        }
    }
    return n;
}

bool ha_collect_over(int topics, int seen, int64_t now_ms, int64_t quiet_since_ms)
{
    return (topics > 0 && seen >= topics) || now_ms - quiet_since_ms >= HA_QUIET_MS;
}

uint32_t ha_expected_s(bool always, uint32_t sync_expected_s, uint32_t longest_off_s)
{
    return always ? ALWAYS_EXPECTED_S + longest_off_s : sync_expected_s;
}
