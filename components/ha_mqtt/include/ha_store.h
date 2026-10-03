#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "ha_fields.h"
#include "ha_payload.h"
#include "util_snapshot.h"

/*
 * What the dashboard shows from MQTT (spec §12.5, §12.7): each mapping's label, unit and time to live with
 * the last value that came, and Home Assistant's message. Plain data, sealed into RTC RAM through deep
 * sleep, so a routine wake draws the mqtt.<key> fields without reading a file. The app task owns it.
 * Pure C, host-buildable.
 */

#define HA_STORE_MAGIC 0x72666d71u /* "rfmq" */
#define HA_STORE_VERSION 1
#define HA_MESSAGE_FRESH_S 86400 /* the banner, and the field's freshness: 24 h (spec §12.7) */

typedef enum {
    HA_MISSING, /* no value, or no such entry */
    HA_FRESH,
    HA_STALE,
} ha_freshness_t;

typedef struct {
    char key[HA_KEY_LEN];
    char label[HA_LABEL_LEN];
    char unit[HA_UNIT_LEN];
    uint8_t kind;     /* ha_kind_t */
    uint8_t decimals; /* the value's */
    uint32_t ttl_s;   /* the mapping's; 0: the store's default */
    uint32_t updated; /* UTC; 0: no value yet */
    int32_t number;
    char text[HA_TEXT_LEN];
} ha_entry_t;

typedef struct {
    util_snapshot_hdr_t hdr; /* ha_store_seal() before a deep sleep */
    uint8_t count;
    uint32_t default_ttl_s; /* twice the expected sync interval; 0: never stale (manual mode) */
    ha_entry_t entry[HA_FIELDS_MAX];
    char message[HA_MESSAGE_LEN];
    uint32_t message_at;    /* UTC; 0: none */
    bool message_dismissed; /* KEY short took the banner away; the field keeps showing the message */
} ha_store_t;

void ha_store_init(ha_store_t *s);
/* The mappings changed (a cold boot, the MQTT page, a restore): the entries in their order, each keeping
 * the value of an entry with the same key and kind. The message stays. */
void ha_store_rebuild(ha_store_t *s, const ha_fields_t *f);
int ha_store_find(const ha_store_t *s, const char *key); /* index, or -1 */
void ha_store_set_default_ttl(ha_store_t *s, uint32_t ttl_s);
/* A value for entry `i` came at `now`. True if what it shows changed: a new value, or one that was stale.
 * A value of the wrong kind, or for no entry, is ignored. */
bool ha_store_set(ha_store_t *s, int i, const ha_value_t *v, time_t now);
ha_freshness_t ha_store_freshness(const ha_store_t *s, int i, time_t now);
/* The clock moved by `delta_s` (a sync set it): every value and the message keep their age. */
void ha_store_shift_time(ha_store_t *s, int64_t delta_s);
/* The message (spec §12.7): a new one shows its banner again; "" clears it. */
void ha_store_set_message(ha_store_t *s, const char *text, time_t now);
/* A message under 24 h old that KEY hasn't dismissed. */
bool ha_store_banner(const ha_store_t *s, time_t now);
void ha_store_dismiss(ha_store_t *s);
ha_freshness_t ha_store_message_freshness(const ha_store_t *s, time_t now);
void ha_store_seal(ha_store_t *s);
bool ha_store_valid(const ha_store_t *s);
