#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The PC app's messages (spec §12a, D44): the board's hello, the app's catalogue, readings and status, and
 * GET /v1/info. Pure C on cJSON, host-buildable; the WebSocket and the probe are the device's.
 */

#define PC_PROTOCOL 1
#define PC_READINGS_MAX 32
#define PC_ID_LEN 16    /* 1-15 of a-z, 0-9 and _, and the terminator */
#define PC_LABEL_LEN 24 /* 23 bytes and the terminator */
#define PC_UNIT_LEN 8   /* 7 bytes and the terminator */
#define PC_FRAME_MAX 8192
#define PC_DEPTH_MAX 4
#define PC_INTERVAL_MIN_MS 250
#define PC_INTERVAL_MAX_MS 10000

typedef enum { PC_HW_OK, PC_HW_NOT_RUNNING, PC_HW_SHM_OFF, PC_HW_STALE, PC_HW_UNKNOWN } pc_hwinfo_t;

typedef struct {
    char id[PC_ID_LEN];
    char label[PC_LABEL_LEN];
    char unit[PC_UNIT_LEN];
    uint8_t decimals; /* 0-3 */
    bool has_min, has_max;
    float min, max; /* the usual range, when the app gives one */
} pc_reading_t;

typedef struct {
    uint8_t count;
    pc_reading_t r[PC_READINGS_MAX];
} pc_catalog_t;

/* A readings message's values, in the catalogue's order. */
typedef struct {
    uint32_t seq;
    bool has[PC_READINGS_MAX];
    float v[PC_READINGS_MAX];
} pc_values_t;

typedef struct {
    int protocol;
    char version[16];
    char name[32]; /* the PC's */
    pc_hwinfo_t hwinfo;
} pc_info_t;

typedef enum {
    PC_MSG_INVALID, /* refused, with the reason in err: the connection closes */
    PC_MSG_NONE,    /* a message this firmware doesn't know: ignored */
    PC_MSG_CATALOG,
    PC_MSG_READINGS,
    PC_MSG_STATUS,
} pc_msg_kind_t;

/* {"type":"hello","protocol":1,"interval_ms":N}; the length, or 0 if `size` is too small. */
size_t pc_hello(char *out, size_t size, uint32_t interval_ms);

/* One message from the app. A catalogue replaces `cat` only when all of it is good; readings fill `vals` by the
 * catalogue's ids (an id it lacks is ignored, one left out or null is no value); a status sets `hw`. */
pc_msg_kind_t pc_parse(const char *json, size_t len, pc_catalog_t *cat, pc_values_t *vals, pc_hwinfo_t *hw,
                       char *err, size_t err_size);

/* GET /v1/info's reply; false with the reason for another app or another protocol ("protocol 2"). */
bool pc_parse_info(const char *json, size_t len, pc_info_t *info, char *err, size_t err_size);

/* The catalogue as a catalogue message, for /fs/state/pc.json; the length, or 0 if `size` is too small. */
size_t pc_catalog_json(const pc_catalog_t *cat, char *out, size_t size);

/* A reading's id: 1-15 characters of a-z, 0-9 and _ (spec §12a.3); presets.json's `pc.<id>` keeps the same rule. */
bool pc_id_valid(const char *id);

/* The index of the reading with `id`, or -1. */
int pc_catalog_find(const pc_catalog_t *cat, const char *id);

pc_hwinfo_t pc_hwinfo_by_name(const char *name);
const char *pc_hwinfo_name(pc_hwinfo_t hw);
