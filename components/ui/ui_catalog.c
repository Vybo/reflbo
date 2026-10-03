#include "ui_catalog.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "ui_layout.h"
#include "ui_split.h"

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
    [UI_FK_RAIN_MAP] = "rain_map",
};
_Static_assert(UI_FK_COUNT == 13, "every kind has a name in the catalogue");
static const char *const k_sizes[] = { "S", "M", "L", "XL" };

static size_t print(cJSON *root, char *out, size_t size)
{
    bool ok = root != NULL && size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}

/* The split layout's rules (spec §5.2): its area and limits, its ratios, and per size, largest first,
 * the least cell and the least height each kind it takes needs, narrower than narrow_w or not. */
static void add_split(cJSON *root)
{
    gfx_rect_t area = ui_split_area();
    cJSON *split = cJSON_AddObjectToObject(root, "split");
    cJSON_AddNumberToObject(split, "x", area.x);
    cJSON_AddNumberToObject(split, "y", area.y);
    cJSON_AddNumberToObject(split, "w", area.w);
    cJSON_AddNumberToObject(split, "h", area.h);
    cJSON_AddNumberToObject(split, "cells", UI_SPLIT_CELLS);
    cJSON_AddNumberToObject(split, "min_w", UI_SPLIT_MIN_W);
    cJSON_AddNumberToObject(split, "min_h", UI_SPLIT_MIN_H);
    cJSON_AddNumberToObject(split, "narrow_w", UI_SPLIT_NARROW_W);
    cJSON_AddNumberToObject(split, "inset", UI_SPLIT_INSET);
    cJSON *ratios = cJSON_AddArrayToObject(split, "ratios");
    for (int r = UI_RATIO_1_4; r <= UI_RATIO_3_4; r++) {
        cJSON_AddItemToArray(ratios, cJSON_CreateString(ui_split_ratio_name(r)));
    }
    cJSON *sizes = cJSON_AddArrayToObject(split, "sizes");
    for (int s = UI_SIZE_XL; s >= UI_SIZE_S; s--) {
        cJSON *so = cJSON_CreateObject();
        cJSON_AddStringToObject(so, "size", k_sizes[s]);
        cJSON_AddNumberToObject(so, "min_w", ui_split_min_w((ui_size_t)s));
        const int min_h[2] = { ui_split_min_h((ui_size_t)s, true), ui_split_min_h((ui_size_t)s, false) };
        cJSON_AddItemToObject(so, "min_h", cJSON_CreateIntArray(min_h, 2));
        cJSON *kinds = cJSON_AddObjectToObject(so, "kinds");
        for (int k = 0; k < UI_FK_COUNT; k++) {
            const int need[2] = { ui_split_need((ui_size_t)s, (ui_field_kind_t)k, true),
                                  ui_split_need((ui_size_t)s, (ui_field_kind_t)k, false) };
            if (need[1] >= 0) {
                cJSON_AddItemToObject(kinds, k_kinds[k], cJSON_CreateIntArray(need, 2));
            }
        }
        cJSON_AddItemToArray(sizes, so);
    }
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
    add_split(root);
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
    for (int i = 0; ctx->mqtt != NULL && i < ctx->mqtt->count; i++) { /* the mapped ones (spec §12.5) */
        static ui_value_t v;
        memset(&v, 0, sizeof(v));
        ui_mqtt_value(ctx, i, &v);
        char id[HA_KEY_LEN + 8], value[sizeof(v.text) + sizeof(v.unit) + 2];
        snprintf(id, sizeof(id), "mqtt.%s", ctx->mqtt->entry[i].key);
        snprintf(value, sizeof(value), "%s%s%s", v.state == UI_VALUE_MISSING ? "" : v.text,
                 v.state != UI_VALUE_MISSING && v.unit[0] ? " " : "", v.state == UI_VALUE_MISSING ? "" : v.unit);
        cJSON *fo = cJSON_CreateObject();
        cJSON_AddStringToObject(fo, "id", id);
        cJSON_AddStringToObject(fo, "kind", k_kinds[v.kind]);
        cJSON_AddStringToObject(fo, "label", v.label);
        cJSON_AddStringToObject(fo, "value", value);
        cJSON_AddStringToObject(fo, "state", k_states[v.state]);
        if (v.state == UI_VALUE_STALE) {
            cJSON_AddNumberToObject(fo, "age_s", v.age_s);
        }
        cJSON_AddItemToArray(fields, fo);
    }
    return print(root, out, size);
}
