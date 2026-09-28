#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "ui_fields.h"
#include "ui_preset.h"

#define SCHEMA 1

static const char *const k_policies[] = { [UI_STALE_STALE] = "stale", [UI_STALE_PLACEHOLDER] = "placeholder",
                                          [UI_STALE_HIDE] = "hide" };
static const char *const k_battery_parts[] = { "percent", "voltage", "days" }; /* UI_STATUS_BAT_* bit order */

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

/* Ids travel in console commands and HA select options: keep them short and plain. */
static bool valid_id(const char *id)
{
    size_t n = strlen(id);
    if (n == 0 || n >= UI_PRESET_ID_LEN) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

static bool optional_bool(const cJSON *obj, const char *key, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

static bool parse_slots(const cJSON *slots, const ui_layout_t *layout, ui_preset_t *out, char *err, size_t size)
{
    const cJSON *slot;
    cJSON_ArrayForEach(slot, slots)
    {
        int index = ui_slot_by_name(layout, slot->string);
        if (index < 0) {
            return fail(err, size, "preset \"%s\": layout %s has no slot \"%s\"", out->id, layout->id, slot->string);
        }
        if (cJSON_IsNull(slot) || (cJSON_IsString(slot) && slot->valuestring[0] == '\0')) {
            continue; /* explicitly empty */
        }
        if (!cJSON_IsString(slot)) {
            return fail(err, size, "preset \"%s\": slot %s needs a field id", out->id, slot->string);
        }
        ui_field_id_t field = ui_field_by_name(slot->valuestring);
        if (field == UI_FIELD_NONE) {
            return fail(err, size, "preset \"%s\": unknown field \"%s\"", out->id, slot->valuestring);
        }
        if (!(layout->slots[index].kinds & UI_KIND(ui_field_info(field)->kind))) {
            return fail(err, size, "preset \"%s\": slot %s can't show %s", out->id, slot->string, slot->valuestring);
        }
        out->slots[index] = (uint8_t)field;
    }
    return true;
}

static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t size)
{
    memset(out, 0, sizeof(*out));
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    if (!cJSON_IsString(id) || !valid_id(id->valuestring)) {
        return fail(err, size, "a preset id must be 1-%d characters of a-z, 0-9, - or _", UI_PRESET_ID_LEN - 1);
    }
    snprintf(out->id, sizeof(out->id), "%s", id->valuestring);
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
    if (cJSON_IsString(name)) {
        if (strlen(name->valuestring) >= UI_PRESET_NAME_LEN) {
            return fail(err, size, "preset \"%s\": name longer than %d bytes", out->id, UI_PRESET_NAME_LEN - 1);
        }
        snprintf(out->name, sizeof(out->name), "%s", name->valuestring);
    } else {
        snprintf(out->name, sizeof(out->name), "%s", out->id);
    }
    const cJSON *layout_name = cJSON_GetObjectItemCaseSensitive(item, "layout");
    int layout = cJSON_IsString(layout_name) ? ui_layout_by_name(layout_name->valuestring) : -1;
    if (layout < 0) {
        return fail(err, size, "preset \"%s\": unknown layout", out->id);
    }
    out->layout = (uint8_t)layout;
    out->in_cycle = optional_bool(item, "in_cycle", true);
    const cJSON *slots = cJSON_GetObjectItemCaseSensitive(item, "slots");
    if (slots != NULL && !parse_slots(slots, ui_layout((ui_layout_id_t)layout), out, err, size)) {
        return false;
    }
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(item, "options");
    const cJSON *h24 = cJSON_GetObjectItemCaseSensitive(options, "clock_24h");
    out->clock = cJSON_IsBool(h24) ? (cJSON_IsTrue(h24) ? UI_CLOCK_24H : UI_CLOCK_12H) : UI_CLOCK_DEFAULT;
    out->seconds = optional_bool(options, "seconds", false);
    out->invert = optional_bool(options, "invert", false);
    out->stale_policy = UI_STALE_STALE;
    const cJSON *policy = cJSON_GetObjectItemCaseSensitive(options, "stale_policy");
    if (policy != NULL) {
        int found = -1;
        for (int i = 0; cJSON_IsString(policy) && i < (int)(sizeof(k_policies) / sizeof(k_policies[0])); i++) {
            if (strcmp(policy->valuestring, k_policies[i]) == 0) {
                found = i;
            }
        }
        if (found < 0) {
            return fail(err, size, "preset \"%s\": stale_policy must be stale, placeholder or hide", out->id);
        }
        out->stale_policy = (uint8_t)found;
    }
    out->status_clock = optional_bool(options, "status_clock", false);
    out->status_battery = UI_STATUS_BAT_PERCENT;
    const cJSON *battery = cJSON_GetObjectItemCaseSensitive(options, "status_battery");
    if (battery != NULL) {
        if (!cJSON_IsArray(battery)) {
            return fail(err, size, "preset \"%s\": status_battery must be a list", out->id);
        }
        out->status_battery = 0;
        const cJSON *part;
        cJSON_ArrayForEach(part, battery)
        {
            int bit = -1;
            for (int i = 0; cJSON_IsString(part) && i < 3; i++) {
                if (strcmp(part->valuestring, k_battery_parts[i]) == 0) {
                    bit = i;
                }
            }
            if (bit < 0) {
                return fail(err, size, "preset \"%s\": status_battery takes percent, voltage and days", out->id);
            }
            out->status_battery |= (uint8_t)(1u << bit);
        }
    }
    return true;
}

static bool parse(const cJSON *root, ui_presets_t *out, char *err, size_t size)
{
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    if (!cJSON_IsNumber(schema) || schema->valueint != SCHEMA) {
        return fail(err, size, "schema must be %d", SCHEMA);
    }
    const cJSON *presets = cJSON_GetObjectItemCaseSensitive(root, "presets");
    int n = cJSON_IsArray(presets) ? cJSON_GetArraySize(presets) : 0;
    if (n < 1 || n > UI_PRESET_MAX) {
        return fail(err, size, "presets must hold 1-%d entries", UI_PRESET_MAX);
    }
    memset(out, 0, sizeof(*out));
    const cJSON *item;
    cJSON_ArrayForEach(item, presets)
    {
        ui_preset_t *p = &out->presets[out->count];
        if (!parse_preset(item, p, err, size)) {
            return false;
        }
        if (ui_presets_find(out, p->id) >= 0) {
            return fail(err, size, "duplicate preset id \"%s\"", p->id);
        }
        out->count++;
    }
    const cJSON *active = cJSON_GetObjectItemCaseSensitive(root, "active");
    int index = cJSON_IsString(active) ? ui_presets_find(out, active->valuestring) : -1;
    out->active = (uint8_t)(index < 0 ? 0 : index);
    const cJSON *cycle = cJSON_GetObjectItemCaseSensitive(root, "cycle");
    out->cycle_enabled = optional_bool(cycle, "enabled", false);
    const cJSON *interval = cJSON_GetObjectItemCaseSensitive(cycle, "interval_s");
    int seconds = cJSON_IsNumber(interval) ? interval->valueint : 60;
    out->cycle_interval_s = (uint16_t)(seconds < UI_CYCLE_MIN_S ? UI_CYCLE_MIN_S
                                       : seconds > UI_CYCLE_MAX_S ? UI_CYCLE_MAX_S
                                                                  : seconds);
    return true;
}

bool ui_presets_from_json(const char *json, ui_presets_t *out, char *err, size_t err_size)
{
    cJSON *root = json != NULL ? cJSON_Parse(json) : NULL;
    if (root == NULL) {
        return fail(err, err_size, "not valid JSON");
    }
    bool ok = parse(root, out, err, err_size);
    cJSON_Delete(root);
    return ok;
}

static cJSON *preset_json(const ui_preset_t *p)
{
    const ui_layout_t *layout = ui_layout((ui_layout_id_t)p->layout);
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id", p->id);
    cJSON_AddStringToObject(obj, "name", p->name);
    cJSON_AddStringToObject(obj, "layout", layout->id);
    cJSON_AddBoolToObject(obj, "in_cycle", p->in_cycle);
    cJSON *slots = cJSON_AddObjectToObject(obj, "slots");
    for (int i = 0; i < layout->slot_count; i++) {
        const ui_field_info_t *info = ui_field_info((ui_field_id_t)p->slots[i]);
        if (info != NULL) {
            cJSON_AddStringToObject(slots, layout->slots[i].name, info->id);
        }
    }
    cJSON *options = cJSON_AddObjectToObject(obj, "options");
    if (p->clock != UI_CLOCK_DEFAULT) {
        cJSON_AddBoolToObject(options, "clock_24h", p->clock == UI_CLOCK_24H);
    }
    cJSON_AddBoolToObject(options, "seconds", p->seconds);
    cJSON_AddBoolToObject(options, "invert", p->invert);
    cJSON_AddStringToObject(options, "stale_policy", k_policies[p->stale_policy < 3 ? p->stale_policy : 0]);
    cJSON_AddBoolToObject(options, "status_clock", p->status_clock);
    cJSON *battery = cJSON_AddArrayToObject(options, "status_battery");
    for (int i = 0; i < 3; i++) {
        if (p->status_battery & (1u << i)) {
            cJSON_AddItemToArray(battery, cJSON_CreateString(k_battery_parts[i]));
        }
    }
    return obj;
}

size_t ui_presets_to_json(const ui_presets_t *p, char *out, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema", SCHEMA);
    cJSON_AddStringToObject(root, "active", p->count ? p->presets[p->active].id : "");
    cJSON *cycle = cJSON_AddObjectToObject(root, "cycle");
    cJSON_AddBoolToObject(cycle, "enabled", p->cycle_enabled);
    cJSON_AddNumberToObject(cycle, "interval_s", p->cycle_interval_s);
    cJSON *presets = cJSON_AddArrayToObject(root, "presets");
    for (int i = 0; i < p->count; i++) {
        cJSON_AddItemToArray(presets, preset_json(&p->presets[i]));
    }
    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, true);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}
