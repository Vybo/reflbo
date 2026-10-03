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
 *     "json_path": null, "ttl_s": 172800 } ] }
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
#define HA_FIELDS_JSON_MAX 12288 /* the largest file is 10 615 bytes (test_ha_fields.c) */

typedef enum {
    HA_KIND_NUMBER,
    HA_KIND_TEXT,
} ha_kind_t;

typedef struct {
    char key[HA_KEY_LEN];
    char label[HA_LABEL_LEN];
    uint8_t kind;      /* ha_kind_t */
    uint8_t precision; /* 0-3 decimals, or HA_PRECISION_AUTO */
    char unit[HA_UNIT_LEN];
    char topic[HA_TOPIC_LEN];
    char json_path[HA_PATH_LEN];
    uint32_t ttl_s; /* stale after this long; 0: twice the expected sync interval (spec §5.1) */
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
