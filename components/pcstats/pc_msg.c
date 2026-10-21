#include "pc_msg.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "util_json.h"

/* The PC app's messages (spec §12a): a catalogue is taken whole or not at all, so the boards' fields never see half
 * of one; anything the protocol doesn't name is ignored. */

static const char *const k_hwinfo[] = { "ok", "not running", "shm off", "stale" }; /* pc_hwinfo_t order */

static pc_msg_kind_t refuse(char *err, size_t size, const char *why)
{
    snprintf(err, size, "%s", why);
    return PC_MSG_INVALID;
}

pc_hwinfo_t pc_hwinfo_by_name(const char *name)
{
    for (size_t i = 0; i < sizeof(k_hwinfo) / sizeof(k_hwinfo[0]); i++) {
        if (name != NULL && strcmp(name, k_hwinfo[i]) == 0) {
            return (pc_hwinfo_t)i;
        }
    }
    return PC_HW_UNKNOWN;
}

const char *pc_hwinfo_name(pc_hwinfo_t hw)
{
    return hw < PC_HW_UNKNOWN ? k_hwinfo[hw] : "unknown";
}

size_t pc_hello(char *out, size_t size, uint32_t interval_ms)
{
    int n = snprintf(out, size, "{\"type\":\"hello\",\"protocol\":%d,\"interval_ms\":%lu}", PC_PROTOCOL,
                     (unsigned long)interval_ms);
    return n > 0 && (size_t)n < size ? (size_t)n : 0;
}

bool pc_id_valid(const char *id)
{
    size_t n = id != NULL ? strlen(id) : 0;
    if (n == 0 || n >= PC_ID_LEN) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

/* `text` into `out` of `size` bytes, cut at a UTF-8 character. */
static void cut(char *out, size_t size, const char *text)
{
    size_t n = strlen(text);
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) {
            n--; /* not inside a character */
        }
    }
    memcpy(out, text, n);
    out[n] = '\0';
}

static const char *text_of(const cJSON *o, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static bool number_of(const cJSON *o, const char *key, float *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || fabs(item->valuedouble) > FLT_MAX) {
        return false; /* no float holds it: no value, never an infinity */
    }
    *out = (float)item->valuedouble;
    return true;
}

static pc_msg_kind_t parse_catalog(const cJSON *root, pc_catalog_t *cat, char *err, size_t size)
{
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "readings");
    if (!cJSON_IsArray(list)) {
        return refuse(err, size, "a catalogue is a list of readings");
    }
    if (cJSON_GetArraySize(list) > PC_READINGS_MAX) {
        return refuse(err, size, "32 readings at most");
    }
    static pc_catalog_t next; /* the app task's alone */
    memset(&next, 0, sizeof(next));
    int i = 0;
    for (const cJSON *e = list->child; e != NULL; e = e->next, i++) {
        pc_reading_t *r = &next.r[i];
        const char *id = text_of(e, "id");
        if (!pc_id_valid(id)) {
            snprintf(err, size, "reading %d: an id is 1-15 characters of a-z, 0-9 and _", i + 1);
            return PC_MSG_INVALID;
        }
        if (pc_catalog_find(&next, id) >= 0) {
            snprintf(err, size, "reading %d: id \"%s\" is used twice", i + 1, id);
            return PC_MSG_INVALID;
        }
        float decimals = 0;
        if (number_of(e, "decimals", &decimals) && (decimals < 0 || decimals > 3 || decimals != floorf(decimals))) {
            snprintf(err, size, "reading %d: decimals are 0-3", i + 1);
            return PC_MSG_INVALID;
        }
        cut(r->id, sizeof(r->id), id);
        cut(r->label, sizeof(r->label), text_of(e, "label"));
        cut(r->unit, sizeof(r->unit), text_of(e, "unit"));
        r->decimals = (uint8_t)decimals;
        r->has_min = number_of(e, "min", &r->min);
        r->has_max = number_of(e, "max", &r->max);
        next.count = (uint8_t)(i + 1);
    }
    *cat = next;
    return PC_MSG_CATALOG;
}

static pc_msg_kind_t parse_readings(const cJSON *root, const pc_catalog_t *cat, pc_values_t *vals)
{
    const cJSON *values = cJSON_GetObjectItemCaseSensitive(root, "values");
    const cJSON *seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
    memset(vals, 0, sizeof(*vals));
    double n = cJSON_IsNumber(seq) && seq->valuedouble >= 0 ? seq->valuedouble : 0; /* NaN too */
    vals->seq = n >= (double)UINT32_MAX ? UINT32_MAX : (uint32_t)n;
    for (int i = 0; i < cat->count; i++) {
        vals->has[i] = number_of(values, cat->r[i].id, &vals->v[i]); /* null or left out: no value */
    }
    return PC_MSG_READINGS;
}

pc_msg_kind_t pc_parse(const char *json, size_t len, pc_catalog_t *cat, pc_values_t *vals, pc_hwinfo_t *hw,
                       char *err, size_t err_size)
{
    if (len > PC_FRAME_MAX) {
        return refuse(err, err_size, "a frame over 8 KB");
    }
    if (util_json_depth(json) > PC_DEPTH_MAX) {
        return refuse(err, err_size, "nested too deeply");
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return refuse(err, err_size, "not JSON");
    }
    const char *type = text_of(root, "type");
    pc_msg_kind_t kind = PC_MSG_NONE;
    if (strcmp(type, "catalog") == 0) {
        kind = parse_catalog(root, cat, err, err_size);
    } else if (strcmp(type, "readings") == 0) {
        kind = parse_readings(root, cat, vals);
    } else if (strcmp(type, "status") == 0) {
        *hw = pc_hwinfo_by_name(text_of(root, "hwinfo"));
        kind = PC_MSG_STATUS;
    }
    cJSON_Delete(root);
    return kind;
}

bool pc_parse_info(const char *json, size_t len, pc_info_t *info, char *err, size_t err_size)
{
    memset(info, 0, sizeof(*info));
    if (util_json_depth(json) > PC_DEPTH_MAX) {
        refuse(err, err_size, "nested too deeply");
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    bool ok = false;
    if (!cJSON_IsObject(root) || strcmp(text_of(root, "app"), "reflbo-stats") != 0) {
        refuse(err, err_size, "not reflbo-stats");
    } else {
        const cJSON *protocol = cJSON_GetObjectItemCaseSensitive(root, "protocol");
        double p = cJSON_IsNumber(protocol) && isfinite(protocol->valuedouble) ? protocol->valuedouble : 0;
        info->protocol = p >= INT_MAX ? INT_MAX : p <= INT_MIN ? INT_MIN : (int)p;
        cut(info->version, sizeof(info->version), text_of(root, "version"));
        cut(info->name, sizeof(info->name), text_of(root, "name"));
        info->hwinfo = pc_hwinfo_by_name(text_of(root, "hwinfo"));
        if (info->protocol != PC_PROTOCOL) {
            snprintf(err, err_size, "protocol %d", info->protocol);
        } else {
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

pc_probe_t pc_probe_result(int status, const char *body, size_t len, pc_info_t *info, char *detail, size_t size)
{
    memset(info, 0, sizeof(*info));
    if (status == 0) {
        snprintf(detail, size, "off");
        return PC_PROBE_OFF;
    }
    if (status != 200) {
        if (status == 401) {
            snprintf(detail, size, "wrong token");
        } else {
            snprintf(detail, size, "HTTP %d", status);
        }
        return PC_PROBE_REFUSED;
    }
    if (!pc_parse_info(body, len, info, detail, size)) {
        return PC_PROBE_REFUSED;
    }
    if (info->hwinfo == PC_HW_OK) {
        detail[0] = '\0';
    } else {
        snprintf(detail, size, "HWiNFO %s", pc_hwinfo_name(info->hwinfo));
    }
    return PC_PROBE_FOUND;
}

pc_close_t pc_close_action(int close_code, const char *reason, int http_status, char *why, size_t size)
{
    if (http_status == 401) {
        snprintf(why, size, "wrong token");
        return PC_CLOSE_END;
    }
    if (http_status != 0 && http_status != 101) {
        snprintf(why, size, "HTTP %d", http_status);
        return PC_CLOSE_END;
    }
    if (close_code == 1001 && reason != NULL && strcmp(reason, "no pong") == 0) { /* its pings lost: reconnect */
        snprintf(why, size, "no pong");
        return PC_CLOSE_WAIT;
    }
    if (close_code == 1001) { /* going away: the PC app quit */
        snprintf(why, size, "the PC app quit");
        return PC_CLOSE_END;
    }
    if (close_code == 1008) { /* policy: a new token, another protocol, no hello */
        snprintf(why, size, "%s", reason != NULL && reason[0] ? reason : "refused");
        return PC_CLOSE_END;
    }
    snprintf(why, size, "closed");
    return PC_CLOSE_WAIT;
}

int pc_upgrade_status(int handshake_status)
{
    return handshake_status >= 100 && handshake_status <= 599 && handshake_status != 101 ? handshake_status : 0;
}

size_t pc_catalog_json(const pc_catalog_t *cat, char *out, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "catalog");
    cJSON *list = cJSON_AddArrayToObject(root, "readings");
    for (int i = 0; i < cat->count; i++) {
        const pc_reading_t *r = &cat->r[i];
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "id", r->id);
        cJSON_AddStringToObject(e, "label", r->label);
        cJSON_AddStringToObject(e, "unit", r->unit);
        cJSON_AddNumberToObject(e, "decimals", r->decimals);
        if (r->has_min) {
            cJSON_AddNumberToObject(e, "min", r->min);
        }
        if (r->has_max) {
            cJSON_AddNumberToObject(e, "max", r->max);
        }
        cJSON_AddItemToArray(list, e);
    }
    bool ok = cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}

int pc_catalog_find(const pc_catalog_t *cat, const char *id)
{
    for (int i = 0; i < cat->count; i++) {
        if (strcmp(cat->r[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}
