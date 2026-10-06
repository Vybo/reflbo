#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The MQTT field mappings (spec §12.5, D7): which topic, and which key in its JSON, brings each
 * mqtt.<key> field. Kept in /cfg/mqtt_fields.json, edited on the web page's MQTT page. A structural
 * error refuses the file whole and names it; labels and units are cut at a character, numbers
 * clamped. Pure C on cJSON, host-buildable.
 *
 *   { "schema": 1, "fields": [ { "key": "outdoor_temp", "label": "Outside", "kind": "number",
 *     "unit": "°C", "precision": 1, "topic": "ha/statestream/sensor/outdoor_temperature/state",
 *     "json_path": null, "ttl_s": 172800 },
 *     { "key": "front_door", "kind": "text", "topic": "...", "states": { "on": "Open", "off": "Closed" } },
 *     { "key": "next_alarm", "kind": "time", "topic": "..." } ] }
 *
 * A text field may name up to 8 exact states and the words that show instead (D40); a time field takes an
 * ISO 8601 time or seconds since 1970 (spec §12.5).
 */

#define HA_FIELDS_MAX 32
#define HA_KEY_LEN 24    /* 1-23 bytes of a-z, 0-9 and _ */
#define HA_LABEL_LEN 24  /* up to 23 bytes; the key when missing */
#define HA_UNIT_LEN 8    /* up to 7 bytes: "µg/m³" */
#define HA_TOPIC_LEN 128 /* 1-127 printable ASCII characters, no wildcards, no quotes or backslashes */
#define HA_PATH_LEN 48   /* dotted keys into the payload's JSON: "a.b.c"; "" for the payload itself */
#define HA_PRECISION_AUTO 0xFF /* decimals as the payload has them, up to 3 */
#define HA_TTL_MIN_S 60
#define HA_TTL_MAX_S 2592000 /* 30 days */
#define HA_FIELDS_JSON_MAX 28672 /* the largest file, with every state label, in test_ha_fields.c */
#define HA_STATES_MAX 8  /* state labels a text field keeps (D40) */
#define HA_STATE_LEN 24  /* a state: 1-23 printable ASCII characters, matched exactly */

typedef enum {
    HA_KIND_NUMBER,
    HA_KIND_TEXT,
    HA_KIND_TIME, /* a timestamp, shown as the clock shows times (D40) */
} ha_kind_t;

typedef struct {
    char state[HA_STATE_LEN];  /* "on", "not_home" */
    char label[HA_LABEL_LEN];  /* what shows instead: "Open", "Away"; 1-23 bytes */
} ha_state_label_t;

typedef struct {
    char key[HA_KEY_LEN];
    char label[HA_LABEL_LEN];
    uint8_t kind;      /* ha_kind_t */
    uint8_t precision; /* 0-3 decimals, or HA_PRECISION_AUTO */
    char unit[HA_UNIT_LEN];
    char topic[HA_TOPIC_LEN];
    char json_path[HA_PATH_LEN];
    uint32_t ttl_s; /* stale after this long; 0: twice the expected sync interval (spec §5.1) */
    uint8_t state_count;                     /* a text's state labels, D40 */
    ha_state_label_t states[HA_STATES_MAX];
} ha_field_t;

typedef struct {
    uint8_t count;
    ha_field_t field[HA_FIELDS_MAX];
} ha_fields_t;

bool ha_key_valid(const char *key);
/* Parses and validates mqtt_fields.json; false with the reason in `err`, *out then unspecified. */
bool ha_fields_from_json(const char *json, ha_fields_t *out, char *err, size_t err_size);
/* Returns the length written, or 0 if `size` is too small. */
size_t ha_fields_to_json(const ha_fields_t *f, char *out, size_t size);
int ha_fields_find(const ha_fields_t *f, const char *key); /* index, or -1 */
const char *ha_kind_name(ha_kind_t kind);                   /* "number", "text" or "time", as the file names it */
/* The word a text field shows for `state`, or NULL to show the state as it came (D40). */
const char *ha_field_state_label(const ha_field_t *f, const char *state);
