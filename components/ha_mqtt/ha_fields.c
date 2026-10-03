#include "ha_fields.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "util_json.h"

#define SCHEMA 1
#define MAX_DEPTH 8 /* the file nests 3 levels */

static bool fail(char *err, size_t size, const char *fmt, ...)
{
    if (size > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, size, fmt, ap);
        va_end(ap);
    }
    return false;
}

bool ha_key_valid(const char *key)
{
    size_t n = key != NULL ? strlen(key) : 0;
    if (n == 0 || n >= HA_KEY_LEN) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

/* 1 to size - 1 printable ASCII characters, none of them in `banned`. */
static bool printable(const char *s, size_t size, const char *banned)
{
    size_t n = strlen(s);
    if (n == 0 || n >= size) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (s[i] < ' ' || s[i] > '~' || strchr(banned, s[i]) != NULL) {
            return false;
        }
    }
    return true;
}

/* A text up to its first control character, cut to fit at a character boundary. */
static void copy_text(char *out, size_t size, const char *text)
{
    size_t n = 0;
    while (text[n] != '\0' && (unsigned char)text[n] >= ' ' && text[n] != 0x7F) {
        n++;
    }
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) { /* text[n] continues a sequence */
            n--;
        }
    }
    memcpy(out, text, n);
    out[n] = '\0';
}

/* Dotted keys, none empty: "co2", "update.state". */
static bool valid_path(const char *s)
{
    if (!printable(s, HA_PATH_LEN, "\"\\")) {
        return false;
    }
    size_t n = strlen(s);
    return s[0] != '.' && s[n - 1] != '.' && strstr(s, "..") == NULL;
}

static long clamped(const cJSON *item, long fallback, long lo, long hi)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return fallback;
    }
    long v = lround(item->valuedouble);
    return v < lo ? lo : v > hi ? hi : v;
}

static bool parse_field(const cJSON *item, int n, ha_field_t *out, char *err, size_t size)
{
    if (!cJSON_IsObject(item)) {
        return fail(err, size, "field %d must be an object", n);
    }
    const cJSON *key = cJSON_GetObjectItemCaseSensitive(item, "key");
    if (!cJSON_IsString(key) || !ha_key_valid(key->valuestring)) {
        return fail(err, size, "field %d: a key is 1-23 characters of a-z, 0-9 or _", n);
    }
    snprintf(out->key, sizeof(out->key), "%s", key->valuestring);
    const char *k = out->key;
    const cJSON *topic = cJSON_GetObjectItemCaseSensitive(item, "topic");
    if (!cJSON_IsString(topic) || !printable(topic->valuestring, HA_TOPIC_LEN, "\"\\")) {
        return fail(err, size, "field %s: a topic of 1-127 printable characters", k);
    }
    if (strpbrk(topic->valuestring, "+#") != NULL) {
        return fail(err, size, "field %s: the topic has a wildcard", k);
    }
    snprintf(out->topic, sizeof(out->topic), "%s", topic->valuestring);
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(item, "kind");
    if (kind != NULL && !cJSON_IsNull(kind)) {
        const char *s = cJSON_IsString(kind) ? kind->valuestring : "";
        if (strcmp(s, "number") != 0 && strcmp(s, "text") != 0) {
            return fail(err, size, "field %s: kind is number or text", k);
        }
        out->kind = strcmp(s, "text") == 0 ? HA_KIND_TEXT : HA_KIND_NUMBER;
    }
    const cJSON *path = cJSON_GetObjectItemCaseSensitive(item, "json_path");
    if (path != NULL && !cJSON_IsNull(path) && !(cJSON_IsString(path) && path->valuestring[0] == '\0')) {
        if (!cJSON_IsString(path) || !valid_path(path->valuestring)) {
            return fail(err, size, "field %s: json_path is keys joined by dots", k);
        }
        snprintf(out->json_path, sizeof(out->json_path), "%s", path->valuestring);
    }
    const cJSON *label = cJSON_GetObjectItemCaseSensitive(item, "label");
    copy_text(out->label, sizeof(out->label), cJSON_IsString(label) && label->valuestring[0] ? label->valuestring : k);
    if (out->label[0] == '\0') {
        snprintf(out->label, sizeof(out->label), "%s", k); /* it began with a control character */
    }
    const cJSON *unit = cJSON_GetObjectItemCaseSensitive(item, "unit");
    if (cJSON_IsString(unit)) {
        copy_text(out->unit, sizeof(out->unit), unit->valuestring);
    }
    const cJSON *precision = cJSON_GetObjectItemCaseSensitive(item, "precision");
    out->precision = cJSON_IsNumber(precision) ? (uint8_t)clamped(precision, 0, 0, 3) : HA_PRECISION_AUTO;
    const cJSON *ttl = cJSON_GetObjectItemCaseSensitive(item, "ttl_s");
    long t = clamped(ttl, 0, 0, HA_TTL_MAX_S);
    out->ttl_s = (uint32_t)(t == 0 ? 0 : t < HA_TTL_MIN_S ? HA_TTL_MIN_S : t);
    return true;
}

bool ha_fields_from_json(const char *json, ha_fields_t *out, char *err, size_t err_size)
{
    if (util_json_depth(json) > MAX_DEPTH) {
        return fail(err, err_size, "nested more than %d levels", MAX_DEPTH);
    }
    cJSON *root = json != NULL ? cJSON_Parse(json) : NULL;
    if (root == NULL) {
        return fail(err, err_size, "not valid JSON");
    }
    memset(out, 0, sizeof(*out));
    bool ok = true;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const cJSON *fields = cJSON_GetObjectItemCaseSensitive(root, "fields");
    if (!cJSON_IsNumber(schema) || schema->valueint != SCHEMA) {
        ok = fail(err, err_size, "schema must be %d", SCHEMA);
    } else if (fields != NULL && !cJSON_IsNull(fields) && !cJSON_IsArray(fields)) {
        ok = fail(err, err_size, "fields must be a list");
    } else if (cJSON_GetArraySize(fields) > HA_FIELDS_MAX) {
        ok = fail(err, err_size, "at most %d MQTT fields", HA_FIELDS_MAX);
    }
    const cJSON *list = ok && cJSON_IsArray(fields) ? fields : NULL;
    const cJSON *item;
    cJSON_ArrayForEach(item, list)
    {
        ha_field_t *f = &out->field[out->count];
        if (!parse_field(item, out->count + 1, f, err, err_size)) {
            ok = false;
            break;
        }
        if (ha_fields_find(out, f->key) >= 0) {
            ok = fail(err, err_size, "duplicate key \"%s\"", f->key);
            break;
        }
        out->count++;
    }
    cJSON_Delete(root);
    return ok;
}

size_t ha_fields_to_json(const ha_fields_t *f, char *out, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema", SCHEMA);
    cJSON *fields = cJSON_AddArrayToObject(root, "fields");
    for (int i = 0; i < f->count && i < HA_FIELDS_MAX; i++) {
        const ha_field_t *x = &f->field[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "key", x->key);
        cJSON_AddStringToObject(o, "label", x->label);
        cJSON_AddStringToObject(o, "kind", x->kind == HA_KIND_TEXT ? "text" : "number");
        cJSON_AddStringToObject(o, "unit", x->unit);
        if (x->precision == HA_PRECISION_AUTO) {
            cJSON_AddNullToObject(o, "precision");
        } else {
            cJSON_AddNumberToObject(o, "precision", x->precision);
        }
        cJSON_AddStringToObject(o, "topic", x->topic);
        if (x->json_path[0] == '\0') {
            cJSON_AddNullToObject(o, "json_path");
        } else {
            cJSON_AddStringToObject(o, "json_path", x->json_path);
        }
        cJSON_AddNumberToObject(o, "ttl_s", x->ttl_s);
        cJSON_AddItemToArray(fields, o);
    }
    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}

int ha_fields_find(const ha_fields_t *f, const char *key)
{
    for (int i = 0; key != NULL && i < f->count; i++) {
        if (strcmp(f->field[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}
