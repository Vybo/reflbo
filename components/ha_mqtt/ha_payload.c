#define _POSIX_C_SOURCE 200809L /* gmtime_r() on the host */

#include "ha_payload.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "util_crc32.h"
#include "util_json.h"

#define MAX_DEPTH 16 /* a payload nested deeper is never parsed (AGENTS.md gotcha 30) */
#define MODEL "ESP32-S3-RLCD-4.2"
#define MANUFACTURER "Waveshare"

void ha_topic(char *out, size_t size, const char *id, const char *leaf)
{
    snprintf(out, size, "reflbo/%s/%s", id, leaf);
}

ha_cmd_t ha_cmd_parse(const char *id, const char *topic, size_t len)
{
    char prefix[HA_TOPIC_MAX];
    ha_topic(prefix, sizeof(prefix), id, "cmd/");
    size_t p = strlen(prefix);
    if (len <= p || memcmp(topic, prefix, p) != 0) {
        return HA_CMD_NONE;
    }
    static const struct {
        const char *name;
        ha_cmd_t cmd;
    } k_cmds[] = { { "preset", HA_CMD_PRESET }, { "next", HA_CMD_NEXT }, { "sync", HA_CMD_SYNC },
                   { "message", HA_CMD_MESSAGE } };
    for (size_t i = 0; i < sizeof(k_cmds) / sizeof(k_cmds[0]); i++) {
        if (len - p == strlen(k_cmds[i].name) && memcmp(topic + p, k_cmds[i].name, len - p) == 0) {
            return k_cmds[i].cmd;
        }
    }
    return HA_CMD_NONE;
}

bool ha_cmd_press(const char *payload, size_t len)
{
    return len == 5 && memcmp(payload, "PRESS", 5) == 0;
}

/* `len` bytes of `text`, control characters as spaces, cut at a character to fit `size`. */
static void copy_cut(const char *text, size_t len, char *out, size_t size)
{
    size_t n = len;
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) { /* text[n] continues a sequence */
            n--;
        }
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        out[i] = c < ' ' || c == 0x7F ? ' ' : (char)c;
    }
    out[n] = '\0';
}

void ha_message_text(const char *payload, size_t len, char *out, size_t size)
{
    copy_cut(payload, len, out, size < HA_MESSAGE_LEN ? size : HA_MESSAGE_LEN);
}

const char *ha_action_payload(bool boot, ha_press_t press)
{
    static const char *const k_payloads[2][3] = {
        { "key_short", "key_double", "key_long" },
        { "boot_short", "boot_double", "boot_long" },
    };
    return (unsigned)press < 3 ? k_payloads[boot ? 1 : 0][press] : NULL;
}

/* A number as field `f` keeps it: rounded to its precision, or to its own decimals up to 3; fewer when
 * the value with them doesn't fit an int32. */
static bool scale_number(const ha_field_t *f, double v, ha_value_t *out)
{
    if (!isfinite(v)) {
        return false;
    }
    int d = f->precision;
    if (f->precision == HA_PRECISION_AUTO) {
        for (d = 0; d < 3; d++) {
            double x = v * pow(10, d);
            if (fabs(x - round(x)) < 1e-6) {
                break;
            }
        }
    }
    for (; d >= 0; d--) {
        double x = round(v * pow(10, d)); /* half away from zero */
        if (fabs(x) <= 2147483647.0) {
            out->number = (int32_t)x;
            out->decimals = (uint8_t)d;
            return true;
        }
    }
    return false;
}

/* A string holding a number, all of it: " 21.5 " yes, "21.5 °C" no. */
static bool number_text(const char *s, double *v)
{
    char *end;
    *v = strtod(s, &end);
    if (end == s) {
        return false;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    return *end == '\0';
}

static bool from_item(const ha_field_t *f, const cJSON *item, ha_value_t *out)
{
    if (f->kind == HA_KIND_NUMBER) {
        double v;
        if (cJSON_IsNumber(item)) {
            v = item->valuedouble;
        } else if (cJSON_IsBool(item)) {
            v = cJSON_IsTrue(item) ? 1 : 0;
        } else if (!cJSON_IsString(item) || !number_text(item->valuestring, &v)) {
            return false;
        }
        return scale_number(f, v, out);
    }
    char text[32];
    const char *s = text;
    if (cJSON_IsString(item)) {
        s = item->valuestring;
    } else if (cJSON_IsNumber(item)) {
        snprintf(text, sizeof(text), "%.15g", item->valuedouble);
    } else if (cJSON_IsBool(item)) {
        s = cJSON_IsTrue(item) ? "on" : "off";
    } else {
        return false; /* null, an object or a list */
    }
    copy_cut(s, strlen(s), out->text, sizeof(out->text));
    return out->text[0] != '\0';
}

bool ha_value_parse(const ha_field_t *f, const char *payload, size_t len, ha_value_t *out)
{
    memset(out, 0, sizeof(*out));
    out->kind = f->kind;
    char *text = malloc(len + 1);
    if (text == NULL) {
        return false;
    }
    memcpy(text, payload, len);
    text[len] = '\0';
    char *start = text, *end = text + len; /* the payload without the space around it */
    while (start < end && isspace((unsigned char)*start)) {
        start++;
    }
    while (end > start && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    bool ok = false;
    cJSON *root = util_json_depth(start) <= MAX_DEPTH ? cJSON_ParseWithOpts(start, NULL, true) : NULL;
    if (f->json_path[0] != '\0') {
        const cJSON *item = root;
        char path[HA_PATH_LEN];
        snprintf(path, sizeof(path), "%s", f->json_path);
        for (char *key = strtok(path, "."); key != NULL && item != NULL; key = strtok(NULL, ".")) {
            item = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, key) : NULL;
        }
        ok = item != NULL && from_item(f, item, out);
    } else if (root != NULL) {
        ok = from_item(f, root, out); /* a JSON number, string or boolean */
    } else if (f->kind == HA_KIND_NUMBER) {
        double v;
        ok = number_text(start, &v) && scale_number(f, v, out);
    } else {
        copy_cut(start, strlen(start), out->text, sizeof(out->text)); /* plain text, as it came */
        ok = out->text[0] != '\0';
    }
    cJSON_Delete(root);
    free(text);
    return ok;
}

static void add_tenths(cJSON *o, const char *key, bool has, int32_t hundredths)
{
    if (has) {
        cJSON_AddNumberToObject(o, key, lround(hundredths / 10.0) / 10.0);
    } else {
        cJSON_AddNullToObject(o, key);
    }
}

size_t ha_state_json(const ha_state_t *s, char *out, size_t size)
{
    cJSON *o = cJSON_CreateObject();
    add_tenths(o, "temp", s->has_temp, s->temp_c100);
    add_tenths(o, "hum", s->has_hum, s->hum_pct100);
    if (s->has_battery) {
        cJSON_AddNumberToObject(o, "bat_pct", s->bat_pct);
        cJSON_AddNumberToObject(o, "bat_v", lround(s->bat_mv / 10.0) / 100.0);
    } else {
        cJSON_AddNullToObject(o, "bat_pct");
        cJSON_AddNullToObject(o, "bat_v");
    }
    cJSON_AddStringToObject(o, "charging", s->charging != NULL ? s->charging : "unknown");
    if (s->has_rssi) {
        cJSON_AddNumberToObject(o, "rssi", s->rssi);
    } else {
        cJSON_AddNullToObject(o, "rssi");
    }
    cJSON_AddStringToObject(o, "preset", s->preset_id != NULL ? s->preset_id : "");
    cJSON_AddStringToObject(o, "preset_name", s->preset_name != NULL ? s->preset_name : "");
    if (s->last_sync != 0) {
        time_t t = (time_t)s->last_sync;
        struct tm tm;
        char iso[24];
        gmtime_r(&t, &tm);
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm);
        cJSON_AddStringToObject(o, "last_sync", iso);
    } else {
        cJSON_AddNullToObject(o, "last_sync");
    }
    cJSON_AddStringToObject(o, "fw", s->fw != NULL ? s->fw : "");
    cJSON_AddNumberToObject(o, "uptime_s", s->uptime_s);
    bool ok = size > 0 && cJSON_PrintPreallocated(o, out, (int)size, false);
    cJSON_Delete(o);
    return ok ? strlen(out) : 0;
}

uint32_t ha_expire_after_s(uint32_t expected_s)
{
    return expected_s == 0 ? 0 : 2 * expected_s + 600;
}

typedef enum { E_SENSOR, E_SELECT, E_BUTTON, E_NOTIFY, E_TRIGGER } entity_kind_t;

/* spec §12.3, in the order the configs go out. */
static const struct {
    entity_kind_t kind;
    const char *object; /* the config's topic and unique id */
    const char *name;   /* a sensor's, select's, button's or notify entity's name; a trigger's type */
    const char *value;  /* a sensor's key in the state; a button's command; a trigger's payload */
    const char *device_class; /* a sensor's; a trigger's subtype */
    const char *unit;
    bool diagnostic;
} k_entities[HA_DISC_COUNT] = {
    { E_SENSOR, "temperature", "Temperature", "temp", "temperature", "\xC2\xB0" "C", false },
    { E_SENSOR, "humidity", "Humidity", "hum", "humidity", "%", false },
    { E_SENSOR, "battery", "Battery", "bat_pct", "battery", "%", false },
    { E_SENSOR, "charging", "Charging", "charging", "enum", NULL, false },
    { E_SENSOR, "battery_voltage", "Battery voltage", "bat_v", "voltage", "V", true },
    { E_SENSOR, "rssi", "Wi-Fi signal", "rssi", "signal_strength", "dBm", true },
    { E_SENSOR, "last_sync", "Last sync", "last_sync", "timestamp", NULL, true },
    { E_SELECT, "preset", "Preset", NULL, NULL, NULL, false },
    { E_BUTTON, "sync_now", "Sync now", "sync", NULL, NULL, false },
    { E_BUTTON, "next_preset", "Next preset", "next", NULL, NULL, false },
    { E_NOTIFY, "message", "Message", "message", NULL, NULL, false },
    { E_TRIGGER, "key_short", "button_short_press", "key_short", "button_1", NULL, false },
    { E_TRIGGER, "key_double", "button_double_press", "key_double", "button_1", NULL, false },
    { E_TRIGGER, "key_long", "button_long_press", "key_long", "button_1", NULL, false },
    { E_TRIGGER, "boot_short", "button_short_press", "boot_short", "button_2", NULL, false },
    { E_TRIGGER, "boot_double", "button_double_press", "boot_double", "button_2", NULL, false },
    { E_TRIGGER, "boot_long", "button_long_press", "boot_long", "button_2", NULL, false },
};

static const char *component(entity_kind_t kind)
{
    static const char *const k_components[] = { "sensor", "select", "button", "notify", "device_automation" };
    return k_components[kind];
}

bool ha_disc_message(const ha_disc_t *d, int i, char *topic, size_t topic_size, char *payload,
                     size_t payload_size)
{
    if (i < 0 || i >= HA_DISC_COUNT) {
        return false;
    }
    entity_kind_t kind = k_entities[i].kind;
    int n = snprintf(topic, topic_size, "%s/%s/%s/%s/config", d->prefix, component(kind), d->id, k_entities[i].object);
    if (n < 0 || (size_t)n >= topic_size) {
        return false;
    }
    char text[HA_TOPIC_MAX];
    cJSON *o = cJSON_CreateObject();
    if (kind == E_TRIGGER) {
        cJSON_AddStringToObject(o, "automation_type", "trigger");
        ha_topic(text, sizeof(text), d->id, "action");
        cJSON_AddStringToObject(o, "topic", text);
        cJSON_AddStringToObject(o, "type", k_entities[i].name);
        cJSON_AddStringToObject(o, "subtype", k_entities[i].device_class);
        cJSON_AddStringToObject(o, "payload", k_entities[i].value);
    } else {
        cJSON_AddStringToObject(o, "name", k_entities[i].name);
        snprintf(text, sizeof(text), "%s_%s", d->id, k_entities[i].object);
        cJSON_AddStringToObject(o, "unique_id", text);
    }
    if (kind == E_SENSOR || kind == E_SELECT) {
        ha_topic(text, sizeof(text), d->id, "state");
        cJSON_AddStringToObject(o, "state_topic", text);
        snprintf(text, sizeof(text), "{{ value_json.%s }}", kind == E_SELECT ? "preset_name" : k_entities[i].value);
        cJSON_AddStringToObject(o, "value_template", text);
    }
    if (kind == E_SENSOR) {
        cJSON_AddStringToObject(o, "device_class", k_entities[i].device_class);
        if (k_entities[i].unit != NULL) {
            cJSON_AddStringToObject(o, "unit_of_measurement", k_entities[i].unit);
            cJSON_AddStringToObject(o, "state_class", "measurement");
        }
        if (strcmp(k_entities[i].device_class, "enum") == 0) {
            const char *options[] = { "unknown", "discharging", "charging", "full" };
            cJSON_AddItemToObject(o, "options", cJSON_CreateStringArray(options, 4));
        }
        if (k_entities[i].diagnostic) {
            cJSON_AddStringToObject(o, "entity_category", "diagnostic");
        }
        if (d->expire_after_s != 0) {
            cJSON_AddNumberToObject(o, "expire_after", d->expire_after_s);
        }
    }
    if (kind == E_SELECT || kind == E_BUTTON || kind == E_NOTIFY) {
        snprintf(text, sizeof(text), "cmd/%s", kind == E_SELECT ? "preset" : k_entities[i].value);
        char cmd[HA_TOPIC_MAX];
        ha_topic(cmd, sizeof(cmd), d->id, text);
        cJSON_AddStringToObject(o, "command_topic", cmd);
        if (kind == E_SELECT) {
            cJSON *options = cJSON_AddArrayToObject(o, "options");
            for (int p = 0; p < d->preset_count; p++) {
                cJSON_AddItemToArray(options, cJSON_CreateString(d->presets[p]));
            }
        }
        if (kind == E_BUTTON) {
            cJSON_AddStringToObject(o, "payload_press", "PRESS");
        }
        cJSON_AddNumberToObject(o, "qos", 1); /* commands wait in the persistent session (spec §12.4) */
    }
    cJSON *dev = cJSON_AddObjectToObject(o, "device");
    cJSON *ids = cJSON_AddArrayToObject(dev, "identifiers");
    cJSON_AddItemToArray(ids, cJSON_CreateString(d->id));
    cJSON_AddStringToObject(dev, "name", d->id);
    cJSON_AddStringToObject(dev, "model", MODEL);
    cJSON_AddStringToObject(dev, "manufacturer", MANUFACTURER);
    cJSON_AddStringToObject(dev, "sw_version", d->fw);
    bool ok = payload_size > 0 && cJSON_PrintPreallocated(o, payload, (int)payload_size, false);
    cJSON_Delete(o);
    return ok;
}

uint32_t ha_disc_hash(const ha_disc_t *d)
{
    char *topic = malloc(HA_TOPIC_MAX), *payload = malloc(HA_PAYLOAD_MAX); /* not for long: no static */
    uint32_t crc = 0;
    if (d->broker != NULL) {
        crc = util_crc32(crc, d->broker, strlen(d->broker) + 1);
    }
    for (int i = 0; topic != NULL && payload != NULL && i < HA_DISC_COUNT; i++) {
        if (ha_disc_message(d, i, topic, HA_TOPIC_MAX, payload, HA_PAYLOAD_MAX)) {
            crc = util_crc32(crc, topic, strlen(topic) + 1);
            crc = util_crc32(crc, payload, strlen(payload) + 1);
        }
    }
    if (topic == NULL || payload == NULL) {
        crc = 0; /* no memory: never the hash of what went out, so the configs go out again */
    }
    free(topic);
    free(payload);
    return crc;
}
