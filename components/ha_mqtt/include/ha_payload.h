#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ha_fields.h"

/*
 * What the device and Home Assistant say to each other over MQTT (spec §12.2-§12.5, D32): the topics,
 * the discovery configs, the state, the commands and the mapped fields' values. Pure C on cJSON,
 * host-buildable.
 */

#define HA_TOPIC_MAX 128   /* the longest topic the device builds: a discovery config's */
#define HA_PAYLOAD_MAX 3072 /* the largest payload it builds: the select's config, 16 names of control characters */
#define HA_TEXT_LEN 48     /* a text value, up to 47 bytes */
#define HA_MESSAGE_LEN 97  /* the message, up to 96 bytes (spec §12.7) */

/* "reflbo/<id>/<leaf>": the state, the action, the commands. */
void ha_topic(char *out, size_t size, const char *id, const char *leaf);

typedef enum {
    HA_CMD_NONE,
    HA_CMD_PRESET,  /* cmd/preset: a preset's id or name */
    HA_CMD_NEXT,    /* cmd/next: PRESS */
    HA_CMD_SYNC,    /* cmd/sync: PRESS */
    HA_CMD_MESSAGE, /* cmd/message: the text; "" clears it */
} ha_cmd_t;

/* The command reflbo/<id>/cmd/<name> carries (its first `len` bytes; esp-mqtt's aren't terminated). */
ha_cmd_t ha_cmd_parse(const char *id, const char *topic, size_t len);
/* A button's payload, as discovery sets it: "PRESS". */
bool ha_cmd_press(const char *payload, size_t len);
/* The message's text: control characters as spaces, cut at a character to 96 bytes. */
void ha_message_text(const char *payload, size_t len, char *out, size_t size);

typedef struct {
    uint8_t kind;     /* ha_kind_t */
    bool none;        /* HA says there is none: unknown, unavailable, null or empty (D40); the field clears */
    uint8_t decimals; /* a number's: `number` is the value times 10^decimals */
    bool date_only;   /* a time's that came as a date alone, at its local midnight (D40): it shows as a date */
    int32_t number;
    uint32_t time;    /* a time's: UTC seconds (D40) */
    char text[HA_TEXT_LEN];
} ha_value_t;

/* A mapped topic's payload as field `f` reads it (spec §12.5): its json_path's value if it has one, else
 * the payload itself, which may also be a JSON number, string or boolean. A number field takes a number,
 * a boolean (1 or 0) or a string holding a number, rounded half away from zero to its precision, or to its
 * own decimals up to 3 (fewer if it is too large for them); a text field takes anything but an object or
 * a list (a boolean reads "on" or "off"), cut at a character; a time field an ISO 8601 time with its zone, or
 * seconds or ms since 1970 from 2001 on (D40); a text's state with a label reads as the label (D40). HA's unknown
 * and unavailable, null and an empty payload are no value: true with `none` (D40). False for a value that doesn't
 * read: the field keeps what it had. */
bool ha_value_parse(const ha_field_t *f, const char *payload, size_t len, ha_value_t *out);

/* The device's state (spec §12.2): what reflbo/<id>/state carries, retained. */
typedef struct {
    bool has_temp, has_hum, has_battery, has_rssi;
    int32_t temp_c100;    /* 0.01 °C */
    int32_t hum_pct100;   /* 0.01 % */
    int bat_pct, bat_mv;
    const char *charging; /* "unknown", "discharging", "charging", "full" */
    int rssi;             /* dBm, on the network */
    const char *preset_id, *preset_name;
    uint32_t last_sync; /* UTC of the last sync that worked; 0 for none */
    const char *fw;
    uint32_t uptime_s;
} ha_state_t;

size_t ha_state_json(const ha_state_t *s, char *out, size_t size); /* 0 if `size` is too small */

/* spec §12.3: twice the expected interval plus 10 min; 0 (none) without one. */
uint32_t ha_expire_after_s(uint32_t expected_s);

/* Discovery (spec §12.3): the sensors, the preset's select, two buttons, the message's notify entity and
 * six device triggers, under one device. */
typedef struct {
    const char *id;     /* "reflbo-bb94": the device, its client id and its topics' node */
    const char *prefix; /* "homeassistant" */
    const char *fw;
    uint32_t expire_after_s; /* the sensors'; 0 leaves it out (manual mode) */
    const char *const *presets; /* the select's options: the presets' names */
    int preset_count;
    const char *broker; /* "host:port": in no config, but in the hash, as another broker hasn't seen them */
} ha_disc_t;

#define HA_DISC_COUNT 17
/* Config `i` of HA_DISC_COUNT: its topic and its payload, retained. False past the last, or if one
 * doesn't fit. */
bool ha_disc_message(const ha_disc_t *d, int i, char *topic, size_t topic_size, char *payload,
                     size_t payload_size);
/* CRC-32 of the broker and every config's topic and payload: what NVS sys/mqtt_disc keeps once they went out. */
uint32_t ha_disc_hash(const ha_disc_t *d);
