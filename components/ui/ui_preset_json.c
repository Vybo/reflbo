#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "ha_fields.h"
#include "ui_fields.h"
#include "ui_preset.h"
#include "ui_split.h"
#include "util_json.h"

#define SCHEMA 1

static const char *const k_policies[] = { [UI_STALE_STALE] = "stale", [UI_STALE_PLACEHOLDER] = "placeholder",
                                          [UI_STALE_HIDE] = "hide" };
static const char *const k_battery_parts[] = { "percent", "voltage", "days" }; /* UI_STATUS_BAT_* bit order */
static const char *const k_offered_ids[] = { "rain", "flights" };              /* UI_OFFERED_* bit order */

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

/* Copies a name, cut to fit at a character boundary (a limit inside "ř" would split it). */
static void copy_name(char *out, size_t size, const char *name)
{
    size_t n = strlen(name);
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)name[n] & 0xC0) == 0x80) { /* name[n] continues a sequence */
            n--;
        }
    }
    memcpy(out, name, n);
    out[n] = '\0';
}

/* "22:30" -> minutes after midnight. */
static bool parse_hhmm(const cJSON *item, uint16_t *out)
{
    const char *s = cJSON_IsString(item) ? item->valuestring : "";
    if (strlen(s) != 5 || s[2] != ':') {
        return false;
    }
    for (int i = 0; i < 5; i++) {
        if (i != 2 && (s[i] < '0' || s[i] > '9')) {
            return false;
        }
    }
    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h > 23 || m > 59) {
        return false;
    }
    *out = (uint16_t)(h * 60 + m);
    return true;
}

/* A field as presets.json names it: a built-in one, or mqtt.<key>, whose key joins the presets' key table
 * (spec §12.5). UI_FIELD_NONE for neither, and *full when it would be a 33rd key. */
static int field_by_name(ui_presets_t *doc, const char *name, bool *full)
{
    ui_field_id_t f = ui_field_by_name(name);
    if (f != UI_FIELD_NONE || strncmp(name, "mqtt.", 5) != 0 || !ha_key_valid(name + 5)) {
        return f;
    }
    for (int k = 0; k < doc->mqtt.count; k++) {
        if (strcmp(doc->mqtt.key[k], name + 5) == 0) {
            return UI_FIELD_MQTT + k;
        }
    }
    if (doc->mqtt.count >= UI_MQTT_KEYS) {
        *full = true;
        return UI_FIELD_NONE;
    }
    snprintf(doc->mqtt.key[doc->mqtt.count], HA_KEY_LEN, "%s", name + 5);
    return UI_FIELD_MQTT + doc->mqtt.count++;
}

/* The kinds a field may be: an MQTT field's mapping says a number or a text, so a slot takes one wherever
 * it takes either (spec §12.5). */
static uint32_t field_kinds(int field)
{
    return ui_field_is_mqtt(field) ? UI_KIND(UI_FK_NUMBER) | UI_KIND(UI_FK_TEXT)
                                   : UI_KIND(ui_field_info((ui_field_id_t)field)->kind);
}

static void field_name(const ui_presets_t *doc, int field, char *out, size_t size)
{
    const ui_field_info_t *info = ui_field_info((ui_field_id_t)field);
    if (ui_field_is_mqtt(field) && field - UI_FIELD_MQTT < doc->mqtt.count) {
        snprintf(out, size, "mqtt.%s", doc->mqtt.key[field - UI_FIELD_MQTT]);
    } else {
        snprintf(out, size, "%s", info != NULL ? info->id : "");
    }
}

static bool unknown_field(const char *id, const char *name, bool full, char *err, size_t size)
{
    return full ? fail(err, size, "preset \"%s\": at most %d different MQTT fields", id, UI_MQTT_KEYS)
                : fail(err, size, "preset \"%s\": unknown field \"%s\"", id, name);
}

static bool parse_slots(const cJSON *slots, const ui_layout_t *layout, ui_presets_t *doc, ui_preset_t *out, char *err,
                        size_t size)
{
    if (!cJSON_IsObject(slots)) {
        return fail(err, size, "preset \"%s\": slots must be an object of slot names", out->id);
    }
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
        bool full = false;
        int field = field_by_name(doc, slot->valuestring, &full);
        if (field == UI_FIELD_NONE) {
            return unknown_field(out->id, slot->valuestring, full, err, size);
        }
        if (!(layout->slots[index].kinds & field_kinds(field))) {
            return fail(err, size, "preset \"%s\": slot %s can't show %s", out->id, slot->string, slot->valuestring);
        }
        out->slots[index] = (uint8_t)field;
    }
    return true;
}

typedef struct {
    ui_presets_t *doc;
    ui_preset_t *out;
    int nodes; /* taken so far, in preorder */
    int cells;
    char *err;
    size_t size;
} tree_t;

/* A split tree's node and everything under it (spec §5.4). The file's depth is checked before it
 * parses, so the recursion is at most UI_JSON_MAX_DEPTH deep. */
static bool parse_node(tree_t *t, const cJSON *node)
{
    const char *id = t->out->id;
    if (t->nodes >= UI_SPLIT_NODES) { /* a tree of n cells has 2n - 1 nodes */
        return fail(t->err, t->size, "preset \"%s\": a split preset has at most %d cells", id, UI_SPLIT_CELLS);
    }
    const cJSON *split = cJSON_GetObjectItemCaseSensitive(node, "split");
    if (split == NULL) {
        t->out->split[t->nodes++] = 0;
        const cJSON *field = cJSON_GetObjectItemCaseSensitive(node, "field");
        if (field != NULL && !cJSON_IsNull(field) && !(cJSON_IsString(field) && field->valuestring[0] == '\0')) {
            if (!cJSON_IsString(field)) {
                return fail(t->err, t->size, "preset \"%s\": a cell's field must be a field id", id);
            }
            bool full = false;
            int f = field_by_name(t->doc, field->valuestring, &full);
            if (f == UI_FIELD_NONE) {
                return unknown_field(id, field->valuestring, full, t->err, t->size);
            }
            t->out->slots[t->cells] = (uint8_t)f;
        }
        t->cells++;
        return true;
    }
    const char *dir = cJSON_IsString(split) ? split->valuestring : "";
    bool columns = strcmp(dir, "columns") == 0;
    if (!columns && strcmp(dir, "rows") != 0) {
        return fail(t->err, t->size, "preset \"%s\": a split is rows or columns", id);
    }
    const cJSON *ratio = cJSON_GetObjectItemCaseSensitive(node, "ratio");
    int r = cJSON_IsString(ratio) ? ui_split_ratio_by_name(ratio->valuestring) : 0;
    if (r == 0) {
        return fail(t->err, t->size, "preset \"%s\": a split's ratio is 1/4, 1/3, 1/2, 2/3 or 3/4", id);
    }
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(node, "a"), *b = cJSON_GetObjectItemCaseSensitive(node, "b");
    if (!cJSON_IsObject(a) || !cJSON_IsObject(b)) {
        return fail(t->err, t->size, "preset \"%s\": a split needs both parts, a and b, as objects", id);
    }
    t->out->split[t->nodes++] =
        (uint8_t)(r | (columns ? UI_SPLIT_COLUMNS : 0) | (optional_bool(node, "line", true) ? 0 : UI_SPLIT_NO_LINE));
    return parse_node(t, a) && parse_node(t, b);
}

/* The cell can show the field: an MQTT field as a number or as a text. */
static bool cell_shows(int field, gfx_rect_t cell)
{
    if (ui_field_is_mqtt(field)) {
        return ui_split_field_size(UI_FK_NUMBER, cell.w, cell.h) >= 0 || ui_split_field_size(UI_FK_TEXT, cell.w, cell.h) >= 0;
    }
    const ui_field_info_t *info = ui_field_info((ui_field_id_t)field);
    return info == NULL || ui_split_field_size(info->kind, cell.w, cell.h) >= 0;
}

/* The split layout's tree, then its geometry and what each cell can show (spec §5.2). */
static bool parse_split(const cJSON *split, ui_presets_t *doc, ui_preset_t *out, char *err, size_t size)
{
    if (split == NULL || cJSON_IsNull(split)) {
        return true; /* one empty cell */
    }
    if (!cJSON_IsObject(split)) {
        return fail(err, size, "preset \"%s\": split must be a tree of splits and cells", out->id);
    }
    tree_t t = { .doc = doc, .out = out, .err = err, .size = size };
    if (!parse_node(&t, split)) {
        return false;
    }
    ui_split_geometry_t g;
    if (!ui_split_layout(out->split, ui_split_area(), &g)) {
        return fail(err, size, "preset \"%s\": a split's parts must be at least %d×%d", out->id, UI_SPLIT_MIN_W,
                    UI_SPLIT_MIN_H);
    }
    for (int i = 0; i < g.cells; i++) {
        if (!cell_shows(out->slots[i], g.cell[i])) {
            char name[HA_KEY_LEN + 8];
            field_name(doc, out->slots[i], name, sizeof(name));
            return fail(err, size, "preset \"%s\": cell %d (%d×%d) can't show %s", out->id, i + 1, g.cell[i].w,
                        g.cell[i].h, name);
        }
    }
    return true;
}

static bool parse_preset(const cJSON *item, ui_presets_t *doc, ui_preset_t *out, char *err, size_t size)
{
    memset(out, 0, sizeof(*out));
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    if (!cJSON_IsString(id) || !valid_id(id->valuestring)) {
        return fail(err, size, "a preset id must be 1-%d characters of a-z, 0-9, - or _", UI_PRESET_ID_LEN - 1);
    }
    snprintf(out->id, sizeof(out->id), "%s", id->valuestring);
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
    if (cJSON_IsString(name)) {
        copy_name(out->name, sizeof(out->name), name->valuestring);
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
    if (slots != NULL && !cJSON_IsNull(slots) &&
        !parse_slots(slots, ui_layout((ui_layout_id_t)layout), doc, out, err, size)) {
        return false;
    }
    if (layout == UI_LAYOUT_SPLIT &&
        !parse_split(cJSON_GetObjectItemCaseSensitive(item, "split"), doc, out, err, size)) {
        return false;
    }
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(item, "options");
    const cJSON *h24 = cJSON_GetObjectItemCaseSensitive(options, "clock_24h");
    out->clock = cJSON_IsBool(h24) ? (cJSON_IsTrue(h24) ? UI_CLOCK_24H : UI_CLOCK_12H) : UI_CLOCK_DEFAULT;
    out->seconds = optional_bool(options, "seconds", false);
    out->invert = optional_bool(options, "invert", false);
    out->stale_policy = UI_STALE_STALE;
    const cJSON *policy = cJSON_GetObjectItemCaseSensitive(options, "stale_policy");
    if (policy != NULL && !cJSON_IsNull(policy)) {
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
    if (battery != NULL && !cJSON_IsNull(battery)) {
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

static bool parse_schedule(const cJSON *schedule, ui_presets_t *out, char *err, size_t size)
{
    out->schedule.enabled = optional_bool(schedule, "enabled", false);
    const cJSON *entries = cJSON_GetObjectItemCaseSensitive(schedule, "entries");
    if (entries == NULL || cJSON_IsNull(entries)) {
        return true;
    }
    if (!cJSON_IsArray(entries)) {
        return fail(err, size, "schedule: entries must be a list");
    }
    if (cJSON_GetArraySize(entries) > UI_SCHEDULE_MAX) {
        return fail(err, size, "schedule: at most %d entries", UI_SCHEDULE_MAX);
    }
    const cJSON *e;
    cJSON_ArrayForEach(e, entries)
    {
        int n = out->schedule.count + 1;
        ui_schedule_entry_t *se = &out->schedule.entries[out->schedule.count];
        if (!parse_hhmm(cJSON_GetObjectItemCaseSensitive(e, "at"), &se->at_min)) {
            return fail(err, size, "schedule entry %d: \"at\" must be HH:MM", n);
        }
        const cJSON *days = cJSON_GetObjectItemCaseSensitive(e, "days");
        se->days = cJSON_IsNumber(days) ? (uint8_t)(days->valueint & 0x7F) : 0x7F;
        const cJSON *action = cJSON_GetObjectItemCaseSensitive(e, "action");
        const char *a = cJSON_IsString(action) ? action->valuestring : "";
        if (strcmp(a, "preset") == 0) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(e, "preset");
            int index = cJSON_IsString(id) ? ui_presets_find(out, id->valuestring) : -1;
            if (index < 0) {
                return fail(err, size, "schedule entry %d: unknown preset", n);
            }
            se->action = UI_SCHED_PRESET;
            se->preset = (uint8_t)index;
        } else if (strcmp(a, "night") == 0) {
            if (!parse_hhmm(cJSON_GetObjectItemCaseSensitive(e, "until"), &se->until_min)) {
                return fail(err, size, "schedule entry %d: night needs \"until\" as HH:MM", n);
            }
            if (se->until_min == se->at_min) {
                return fail(err, size, "schedule entry %d: a night must end at another time than it starts", n);
            }
            se->action = UI_SCHED_NIGHT;
        } else {
            return fail(err, size, "schedule entry %d: action must be preset or night", n);
        }
        out->schedule.count++;
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
        if (!parse_preset(item, out, p, err, size)) {
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
    const cJSON *offered = cJSON_GetObjectItemCaseSensitive(root, "offered");
    const cJSON *names = cJSON_IsArray(offered) ? offered : NULL;
    const cJSON *id;
    cJSON_ArrayForEach(id, names)
    {
        for (int i = 0; cJSON_IsString(id) && i < (int)(sizeof(k_offered_ids) / sizeof(k_offered_ids[0])); i++) {
            if (strcmp(id->valuestring, k_offered_ids[i]) == 0) {
                out->offered |= (uint8_t)(1u << i); /* unknown names are a later firmware's */
            }
        }
    }
    const cJSON *schedule = cJSON_GetObjectItemCaseSensitive(root, "schedule");
    return schedule == NULL || cJSON_IsNull(schedule) || parse_schedule(schedule, out, err, size);
}

bool ui_presets_from_json(const char *json, ui_presets_t *out, char *err, size_t err_size)
{
    if (util_json_depth(json) > UI_JSON_MAX_DEPTH) {
        return fail(err, err_size, "nested more than %d levels", UI_JSON_MAX_DEPTH);
    }
    cJSON *root = json != NULL ? cJSON_Parse(json) : NULL;
    if (root == NULL) {
        return fail(err, err_size, "not valid JSON");
    }
    bool ok = parse(root, out, err, err_size);
    cJSON_Delete(root);
    return ok;
}

/* A split tree's node and everything under it, in preorder: `at` the node, `cell` its first cell. */
static cJSON *node_json(const ui_presets_t *doc, const ui_preset_t *p, int *at, int *cell)
{
    cJSON *obj = cJSON_CreateObject();
    uint8_t node = p->split[(*at)++];
    if ((node & UI_SPLIT_RATIO) == 0) {
        char name[HA_KEY_LEN + 8];
        field_name(doc, p->slots[(*cell)++], name, sizeof(name));
        if (name[0] != '\0') {
            cJSON_AddStringToObject(obj, "field", name);
        }
        return obj;
    }
    cJSON_AddStringToObject(obj, "split", node & UI_SPLIT_COLUMNS ? "columns" : "rows");
    cJSON_AddStringToObject(obj, "ratio", ui_split_ratio_name(node & UI_SPLIT_RATIO));
    cJSON_AddBoolToObject(obj, "line", !(node & UI_SPLIT_NO_LINE));
    cJSON_AddItemToObject(obj, "a", node_json(doc, p, at, cell));
    cJSON_AddItemToObject(obj, "b", node_json(doc, p, at, cell));
    return obj;
}

static cJSON *preset_json(const ui_presets_t *doc, const ui_preset_t *p)
{
    const ui_layout_t *layout = ui_layout((ui_layout_id_t)p->layout);
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id", p->id);
    cJSON_AddStringToObject(obj, "name", p->name);
    cJSON_AddStringToObject(obj, "layout", layout->id);
    cJSON_AddBoolToObject(obj, "in_cycle", p->in_cycle);
    if (p->layout == UI_LAYOUT_SPLIT) {
        int at = 0, cell = 0;
        bool whole = ui_split_nodes(p->split) > 0; /* a tree cut short can't be walked: one empty cell */
        cJSON_AddItemToObject(obj, "split", whole ? node_json(doc, p, &at, &cell) : cJSON_CreateObject());
    } else {
        cJSON *slots = cJSON_AddObjectToObject(obj, "slots");
        for (int i = 0; i < layout->slot_count; i++) {
            char name[HA_KEY_LEN + 8];
            field_name(doc, p->slots[i], name, sizeof(name));
            if (name[0] != '\0') {
                cJSON_AddStringToObject(slots, layout->slots[i].name, name);
            }
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
        cJSON_AddItemToArray(presets, preset_json(p, &p->presets[i]));
    }
    if (p->schedule.enabled || p->schedule.count) {
        cJSON *schedule = cJSON_AddObjectToObject(root, "schedule");
        cJSON_AddBoolToObject(schedule, "enabled", p->schedule.enabled);
        cJSON *entries = cJSON_AddArrayToObject(schedule, "entries");
        for (int i = 0; i < p->schedule.count && i < UI_SCHEDULE_MAX; i++) {
            const ui_schedule_entry_t *e = &p->schedule.entries[i];
            cJSON *obj = cJSON_CreateObject();
            char hhmm[8];
            snprintf(hhmm, sizeof(hhmm), "%02d:%02d", e->at_min / 60 % 24, e->at_min % 60);
            cJSON_AddStringToObject(obj, "at", hhmm);
            cJSON_AddNumberToObject(obj, "days", e->days);
            if (e->action == UI_SCHED_NIGHT) {
                cJSON_AddStringToObject(obj, "action", "night");
                snprintf(hhmm, sizeof(hhmm), "%02d:%02d", e->until_min / 60 % 24, e->until_min % 60);
                cJSON_AddStringToObject(obj, "until", hhmm);
            } else {
                cJSON_AddStringToObject(obj, "action", "preset");
                cJSON_AddStringToObject(obj, "preset", e->preset < p->count ? p->presets[e->preset].id : "");
            }
            cJSON_AddItemToArray(entries, obj);
        }
    }
    if (p->offered) {
        cJSON *offered = cJSON_AddArrayToObject(root, "offered");
        for (int i = 0; i < (int)(sizeof(k_offered_ids) / sizeof(k_offered_ids[0])); i++) {
            if (p->offered & (1u << i)) {
                cJSON_AddItemToArray(offered, cJSON_CreateString(k_offered_ids[i]));
            }
        }
    }
    /* unformatted: the device reads it, and 16 presets must fit UI_PRESETS_JSON_MAX */
    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}
