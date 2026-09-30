#include "settings.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "util_json.h"

#define SCHEMA 1
#define SETTINGS_JSON_MAX_DEPTH 16 /* the sketch nests 3 levels */

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

static const cJSON *child(const cJSON *obj, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(obj, key);
}

static void read_string(const cJSON *obj, const char *key, char *out, size_t size)
{
    const cJSON *item = child(obj, key);
    if (cJSON_IsString(item) && strlen(item->valuestring) < size && item->valuestring[0] != '\0') {
        snprintf(out, size, "%s", item->valuestring);
    }
}

static void read_bool(const cJSON *obj, const char *key, bool *out)
{
    const cJSON *item = child(obj, key);
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
    }
}

/* A number scaled by `scale`, rounded and clamped to [lo, hi]. */
static long read_scaled(const cJSON *obj, const char *key, long fallback, double scale, long lo, long hi)
{
    const cJSON *item = child(obj, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return fallback;
    }
    long v = lround(item->valuedouble * scale);
    return v < lo ? lo : v > hi ? hi : v;
}

static uint8_t lpm_from_hz(const cJSON *display, uint8_t fallback)
{
    const cJSON *item = child(display, "lpm_hz");
    if (!cJSON_IsNumber(item)) {
        return fallback;
    }
    for (int q = 1; q <= 32; q *= 2) {
        if (fabs(item->valuedouble - q / 4.0) < 0.01) {
            return (uint8_t)q;
        }
    }
    return fallback;
}

bool settings_from_json(const char *json, const settings_t *defaults, settings_t *out, char *err, size_t err_size)
{
    if (util_json_depth(json) > SETTINGS_JSON_MAX_DEPTH) {
        return fail(err, err_size, "nested more than %d levels", SETTINGS_JSON_MAX_DEPTH);
    }
    cJSON *root = json != NULL ? cJSON_Parse(json) : NULL;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return fail(err, err_size, "not a JSON object");
    }
    const cJSON *schema = child(root, "schema");
    if (!cJSON_IsNumber(schema) || schema->valueint != SCHEMA) {
        cJSON_Delete(root);
        return fail(err, err_size, "schema must be %d", SCHEMA);
    }
    *out = *defaults;
    read_string(root, "language", out->language, sizeof(out->language));
    const cJSON *time = child(root, "time");
    read_string(time, "tz_posix", out->tz_posix, sizeof(out->tz_posix));
    read_string(time, "tz_iana", out->tz_iana, sizeof(out->tz_iana));
    read_bool(time, "clock_24h", &out->clock_24h);
    const cJSON *temp_unit = child(child(root, "units"), "temp");
    if (cJSON_IsString(temp_unit)) {
        out->fahrenheit = strcmp(temp_unit->valuestring, "F") == 0;
    }
    const cJSON *sensors = child(root, "sensors");
    out->sensors_every_min = (uint8_t)read_scaled(sensors, "interval_min", out->sensors_every_min, 1, 1, 30);
    out->temp_offset_c100 = (int16_t)read_scaled(sensors, "temp_offset_c", out->temp_offset_c100, 100, -1000, 1000);
    out->hum_offset_pct100 = (int16_t)read_scaled(sensors, "hum_offset_pct", out->hum_offset_pct100, 100, -2000, 2000);
    const cJSON *display = child(root, "display");
    out->display_every_min = (uint8_t)read_scaled(display, "update_min", out->display_every_min, 1, 1, 15);
    out->lpm_quarter_hz = lpm_from_hz(display, out->lpm_quarter_hz);
    const cJSON *location = child(root, "location");
    read_string(location, "name", out->place, sizeof(out->place));
    out->lat_e4 = (int32_t)read_scaled(location, "lat", out->lat_e4, 1e4, -900000, 900000);
    out->lon_e4 = (int32_t)read_scaled(location, "lon", out->lon_e4, 1e4, -1800000, 1800000);
    cJSON_Delete(root);
    return true;
}

/* obj[key], created as an empty object if it is missing or not an object. */
static cJSON *object_at(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsObject(item)) {
        cJSON_DeleteItemFromObjectCaseSensitive(obj, key);
        item = cJSON_AddObjectToObject(obj, key);
    }
    return item;
}

static void put(cJSON *obj, const char *key, cJSON *value)
{
    if (cJSON_GetObjectItemCaseSensitive(obj, key) != NULL) {
        cJSON_ReplaceItemInObjectCaseSensitive(obj, key, value);
    } else {
        cJSON_AddItemToObject(obj, key, value);
    }
}

size_t settings_to_json(const settings_t *s, const char *base_json, char *out, size_t size)
{
    cJSON *root = base_json != NULL ? cJSON_Parse(base_json) : NULL;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    put(root, "schema", cJSON_CreateNumber(SCHEMA));
    put(root, "language", cJSON_CreateString(s->language));
    cJSON *time = object_at(root, "time");
    put(time, "tz_iana", cJSON_CreateString(s->tz_iana));
    put(time, "tz_posix", cJSON_CreateString(s->tz_posix));
    put(time, "clock_24h", cJSON_CreateBool(s->clock_24h));
    put(object_at(root, "units"), "temp", cJSON_CreateString(s->fahrenheit ? "F" : "C"));
    cJSON *sensors = object_at(root, "sensors");
    put(sensors, "interval_min", cJSON_CreateNumber(s->sensors_every_min));
    put(sensors, "temp_offset_c", cJSON_CreateNumber(s->temp_offset_c100 / 100.0));
    put(sensors, "hum_offset_pct", cJSON_CreateNumber(s->hum_offset_pct100 / 100.0));
    cJSON *display = object_at(root, "display");
    put(display, "update_min", cJSON_CreateNumber(s->display_every_min));
    put(display, "lpm_hz", cJSON_CreateNumber(s->lpm_quarter_hz / 4.0));
    cJSON *location = object_at(root, "location");
    put(location, "name", cJSON_CreateString(s->place));
    put(location, "lat", cJSON_CreateNumber(s->lat_e4 / 1e4));
    put(location, "lon", cJSON_CreateNumber(s->lon_e4 / 1e4));
    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, true);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}

/* RFC 7396 on cJSON trees: `patch` is an object; its members merge into `target`. */
static void merge(cJSON *target, const cJSON *patch)
{
    for (const cJSON *p = patch->child; p != NULL; p = p->next) {
        if (cJSON_IsNull(p)) {
            cJSON_DeleteItemFromObjectCaseSensitive(target, p->string);
        } else if (cJSON_IsObject(p)) {
            merge(object_at(target, p->string), p);
        } else {
            put(target, p->string, cJSON_Duplicate(p, true));
        }
    }
}

size_t settings_patch(const char *base_json, const char *patch, char *out, size_t size, char *err, size_t err_size)
{
    if (util_json_depth(patch) > SETTINGS_JSON_MAX_DEPTH) {
        fail(err, err_size, "nested more than %d levels", SETTINGS_JSON_MAX_DEPTH);
        return 0;
    }
    cJSON *p = patch != NULL ? cJSON_Parse(patch) : NULL;
    if (!cJSON_IsObject(p)) {
        cJSON_Delete(p);
        fail(err, err_size, "the patch must be a JSON object");
        return 0;
    }
    cJSON *root = base_json != NULL ? cJSON_Parse(base_json) : NULL;
    if (!cJSON_IsObject(root)) { /* no file yet, or one the loader rejected */
        cJSON_Delete(root);
        root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "schema", SCHEMA);
    }
    merge(root, p);
    cJSON_Delete(p);
    const cJSON *schema = child(root, "schema");
    size_t n = 0;
    if (!cJSON_IsNumber(schema) || schema->valuedouble != SCHEMA) {
        fail(err, err_size, "schema must be %d", SCHEMA);
    } else if (size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false)) {
        n = strlen(out);
    } else {
        fail(err, err_size, "the settings don't fit %u bytes", (unsigned)size);
    }
    cJSON_Delete(root);
    return n;
}
