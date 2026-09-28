#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_layout.h"

/*
 * Presets (spec §5.4): a layout, its slot bindings and options, stored in /cfg/presets.json.
 * Pure C, host-buildable.
 */

#define UI_PRESET_MAX 16
#define UI_PRESET_ID_LEN 16   /* with the terminator */
#define UI_PRESET_NAME_LEN 24
#define UI_CYCLE_MIN_S 10
#define UI_CYCLE_MAX_S 3600

typedef enum {
    UI_STALE_STALE,       /* show the value with its age (the default) */
    UI_STALE_PLACEHOLDER, /* show "—" */
    UI_STALE_HIDE,        /* leave the slot empty */
} ui_stale_policy_t;

/* What the status bar shows beside the battery icon (option status_battery). */
enum {
    UI_STATUS_BAT_PERCENT = 1u << 0,
    UI_STATUS_BAT_VOLTAGE = 1u << 1,
    UI_STATUS_BAT_DAYS = 1u << 2,
};

typedef enum {
    UI_CLOCK_DEFAULT, /* follow the time.clock_24h setting */
    UI_CLOCK_24H,
    UI_CLOCK_12H,
} ui_clock_mode_t;

typedef struct {
    char id[UI_PRESET_ID_LEN];
    char name[UI_PRESET_NAME_LEN];
    uint8_t layout; /* ui_layout_id_t */
    bool in_cycle;
    uint8_t slots[UI_SLOT_MAX]; /* ui_field_id_t per slot, in the layout's slot order */
    uint8_t clock;              /* ui_clock_mode_t */
    bool seconds;
    bool invert;
    uint8_t stale_policy;   /* ui_stale_policy_t */
    bool status_clock;      /* a small clock in the middle of the status bar */
    uint8_t status_battery; /* UI_STATUS_BAT_* bits */
} ui_preset_t;

typedef struct {
    uint8_t count;
    uint8_t active; /* index into presets */
    bool cycle_enabled;
    uint16_t cycle_interval_s;
    ui_preset_t presets[UI_PRESET_MAX];
} ui_presets_t;

/* The built-in presets (spec §5.4): used when presets.json is missing or invalid. */
void ui_presets_defaults(ui_presets_t *p);
int ui_presets_find(const ui_presets_t *p, const char *id); /* index, or -1 */
/* The next preset in cycle order after the active one, wrapping; the active one if no other
 * preset is in the cycle (spec §5.4, KEY short). */
int ui_presets_next(const ui_presets_t *p);
/* Parses and validates presets.json. On failure returns false with a reason in `err` and leaves
 * *out unspecified. */
bool ui_presets_from_json(const char *json, ui_presets_t *out, char *err, size_t err_size);
/* Serialises; returns the length written, or 0 if `size` is too small. */
size_t ui_presets_to_json(const ui_presets_t *p, char *out, size_t size);
