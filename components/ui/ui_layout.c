#include "ui_layout.h"

#include <string.h>

#include "ui_fields.h"

/* Slot rectangles for 400x300 below the 20 px status bar (spec §5.2), tuned on host renders. */

#define K_ANY_SMALL                                                                                                  \
    (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_DATE) | UI_KIND(UI_FK_NUMBER) | UI_KIND(UI_FK_BATTERY) |                    \
     UI_KIND(UI_FK_MOON) | UI_KIND(UI_FK_TEXT) | UI_KIND(UI_FK_WEATHER_NOW) | UI_KIND(UI_FK_WEATHER_DAY) |           \
     UI_KIND(UI_FK_SUN) | UI_KIND(UI_FK_LEVEL) | UI_KIND(UI_FK_POLLEN))
#define K_ANY_MEDIUM (K_ANY_SMALL | UI_KIND(UI_FK_SERIES) | UI_KIND(UI_FK_RAIN_MAP))
#define K_LARGE (K_ANY_SMALL | UI_KIND(UI_FK_RAIN_MAP))
#define K_XL (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_NUMBER))

static const ui_slot_t k_classic[] = {
    { "main", { 0, 21, 400, 125 }, UI_SIZE_XL, K_XL },
    { "sub", { 0, 146, 400, 40 }, UI_SIZE_M, UI_KIND(UI_FK_DATE) | UI_KIND(UI_FK_TEXT) },
    { "s1", { 0, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
    { "s2", { 100, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
    { "s3", { 200, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
    { "s4", { 300, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
};

static const ui_slot_t k_weather[] = {
    { "now", { 0, 21, 200, 160 }, UI_SIZE_L, K_LARGE },
    { "today", { 200, 21, 200, 80 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "hourly", { 200, 101, 200, 80 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "s1", { 0, 182, 200, 118 }, UI_SIZE_S, K_ANY_SMALL },
    { "s2", { 200, 182, 200, 118 }, UI_SIZE_S, K_ANY_SMALL },
};

static const ui_slot_t k_grid[] = {
    { "g1", { 0, 21, 133, 139 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "g2", { 133, 21, 134, 139 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "g3", { 267, 21, 133, 139 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "g4", { 0, 160, 133, 140 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "g5", { 133, 160, 134, 140 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "g6", { 267, 160, 133, 140 }, UI_SIZE_M, K_ANY_MEDIUM },
};

static const ui_slot_t k_focus[] = {
    { "main", { 0, 21, 400, 190 }, UI_SIZE_XL, K_XL },
    { "s1", { 0, 212, 200, 88 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "s2", { 200, 212, 200, 88 }, UI_SIZE_M, K_ANY_MEDIUM },
};

static const ui_layout_t k_layouts[UI_LAYOUT_COUNT] = {
    [UI_LAYOUT_CLASSIC] = { "classic", k_classic, sizeof(k_classic) / sizeof(k_classic[0]) },
    [UI_LAYOUT_WEATHER] = { "weather", k_weather, sizeof(k_weather) / sizeof(k_weather[0]) },
    [UI_LAYOUT_GRID] = { "grid", k_grid, sizeof(k_grid) / sizeof(k_grid[0]) },
    [UI_LAYOUT_FOCUS] = { "focus", k_focus, sizeof(k_focus) / sizeof(k_focus[0]) },
    [UI_LAYOUT_RADAR] = { "radar", NULL, 0 },
    [UI_LAYOUT_FLIGHTS] = { "flights", NULL, 0 },
};

const ui_layout_t *ui_layout(ui_layout_id_t id)
{
    return (unsigned)id < UI_LAYOUT_COUNT ? &k_layouts[id] : NULL;
}

int ui_layout_by_name(const char *id)
{
    for (int i = 0; id != NULL && i < UI_LAYOUT_COUNT; i++) {
        if (strcmp(k_layouts[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int ui_slot_by_name(const ui_layout_t *layout, const char *name)
{
    for (int i = 0; layout != NULL && name != NULL && i < layout->slot_count; i++) {
        if (strcmp(layout->slots[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}
