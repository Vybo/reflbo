#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ha_fields.h"

/*
 * The rules of an MQTT session (spec §9.3 step 6, §12.9) that don't need a client: the reconnects' back-off,
 * the topics to subscribe, when collecting ends, and the interval Home Assistant's sensors expire by.
 * Pure C, host-buildable.
 */

#define HA_QUIET_MS 1000 /* collecting ends after a second without a message */

/* After `failures` failed connections in a row: 10 s, then twice as long each time, at most 5 min. */
uint32_t ha_backoff_ms(int failures);
/* The distinct topics the mappings read, in their order (several values may share one); returns how many. */
int ha_topics(const ha_fields_t *f, const char *out[HA_FIELDS_MAX]);
/* `topic` (its first `len` bytes) in that list, or -1. */
int ha_topic_index(const char *const *topics, int count, const char *topic, size_t len);
/* The collect phase is over: every one of `topics` brought a message, or HA_QUIET_MS passed since
 * `quiet_since_ms` (the later of the subscriptions' confirmation and the last message). */
bool ha_collect_over(int topics, int seen, int64_t now_ms, int64_t quiet_since_ms);
/* Sync mode `always` (spec §12.9): the state goes out on a change, at most every 30 s, and every 5 min;
 * `since_s` is the time since it last went out, negative for never. */
bool ha_state_due(bool changed, int64_t since_s);
/* What HA's sensors expire by (spec §12.3): the sync's expected interval, or in sync mode `always` 10 min
 * and the longest span Wi-Fi is off (quiet hours, a night); 0, never, in manual mode. */
uint32_t ha_expected_s(bool always, uint32_t sync_expected_s, uint32_t longest_off_s);
