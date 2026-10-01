#include "ui_catalog.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "ui_layout.h"

#define PANEL_W 400
#define PANEL_H 300

static const char *const k_kinds[UI_FK_COUNT] = {
    [UI_FK_TIME] = "time",
    [UI_FK_DATE] = "date",
    [UI_FK_NUMBER] = "number",
    [UI_FK_BATTERY] = "battery",
    [UI_FK_MOON] = "moon",
    [UI_FK_TEXT] = "text",
    [UI_FK_WEATHER_NOW] = "weather_now",
    [UI_FK_WEATHER_DAY] = "weather_day",
    [UI_FK_SERIES] = "series",
    [UI_FK_SUN] = "sun",
    [UI_FK_LEVEL] = "level",
    [UI_FK_POLLEN] = "pollen",
};
_Static_assert(UI_FK_COUNT == 12, "every kind has a name in the catalogue");
static const char *const k_sizes[] = { "S", "M", "L", "XL" };

static size_t print(cJSON *root, char *out, size_t size)
{
    bool ok = root != NULL && size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}

size_t ui_catalog_layouts_json(char *out, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "width", PANEL_W);
    cJSON_AddNumberToObject(root, "height", PANEL_H);
    cJSON_AddNumberToObject(root, "status_h", UI_STATUS_H);
    cJSON *layouts = cJSON_AddArrayToObject(root, "layouts");
    for (int l = 0; l < UI_LAYOUT_COUNT; l++) {
        const ui_layout_t *layout = ui_layout((ui_layout_id_t)l);
        cJSON *lo = cJSON_CreateObject();
        cJSON_AddStringToObject(lo, "id", layout->id);
        cJSON *slots = cJSON_AddArrayToObject(lo, "slots");
        for (int s = 0; s < layout->slot_count; s++) {
            const ui_slot_t *slot = &layout->slots[s];
            cJSON *so = cJSON_CreateObject();
            cJSON_AddStringToObject(so, "id", slot->name);
            cJSON_AddNumberToObject(so, "x", slot->rect.x);
            cJSON_AddNumberToObject(so, "y", slot->rect.y);
            cJSON_AddNumberToObject(so, "w", slot->rect.w);
            cJSON_AddNumberToObject(so, "h", slot->rect.h);
            cJSON_AddStringToObject(so, "size", k_sizes[slot->size]);
            cJSON *kinds = cJSON_AddArrayToObject(so, "kinds");
            for (int k = 0; k < UI_FK_COUNT; k++) {
                if (slot->kinds & UI_KIND(k)) {
                    cJSON_AddItemToArray(kinds, cJSON_CreateString(k_kinds[k]));
                }
            }
            cJSON_AddItemToArray(slots, so);
        }
        cJSON_AddItemToArray(layouts, lo);
    }
    return print(root, out, size);
}

size_t ui_catalog_fields_json(const ui_context_t *ctx, char *out, size_t size)
{
    static const char *const k_states[] = { "missing", "fresh", "stale" };
    const lang_t *en = lang_get("en");
    cJSON *root = cJSON_CreateObject();
    cJSON *fields = cJSON_AddArrayToObject(root, "fields");
    for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
        const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
        static ui_value_t v;
        ui_resolve(ctx, (ui_field_id_t)f, &v);
        char value[sizeof(v.text) + sizeof(v.unit) + 2];
        snprintf(value, sizeof(value), "%s%s%s", v.state == UI_VALUE_MISSING ? "" : v.text,
                 v.state != UI_VALUE_MISSING && v.unit[0] ? " " : "", v.state == UI_VALUE_MISSING ? "" : v.unit);
        cJSON *fo = cJSON_CreateObject();
        cJSON_AddStringToObject(fo, "id", info->id);
        cJSON_AddStringToObject(fo, "kind", k_kinds[info->kind]);
        cJSON_AddStringToObject(fo, "label", lang_str(en, info->label));
        cJSON_AddStringToObject(fo, "value", value);
        cJSON_AddStringToObject(fo, "state", k_states[v.state]);
        if (v.state == UI_VALUE_STALE) {
            cJSON_AddNumberToObject(fo, "age_s", v.age_s);
        }
        cJSON_AddItemToArray(fields, fo);
    }
    return print(root, out, size);
}
