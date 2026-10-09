#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "ui_layout.h"
#include "ui_split.h"
#include "ui_window.h"

/*
 * Presets (spec §5.4): a layout, its slot bindings and options, stored in /cfg/presets.json.
 * Pure C, host-buildable.
 */

#define UI_PRESET_MAX 16
#define UI_PRESET_ID_LEN 16   /* with the terminator */
#define UI_PRESET_NAME_LEN 24
#define UI_CYCLE_MIN_S 10
#define UI_CYCLE_MAX_S 3600
#define UI_SCHEDULE_MAX 8
#define UI_PRESETS_JSON_MAX 49152 /* presets.json at its largest: 24 cells a preset (test_ui_preset.c, M6c) */
#define UI_JSON_MAX_DEPTH 20      /* presets.json nests 17 levels at most (a split tree's chain of 13 splits),
                                     settings.json 5; anything deeper is rejected unparsed */

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
    ui_window_t window; /* the auto-cycle skips the preset while it is closed (spec §5.4, D41); days 0: none */
    uint8_t slots[UI_SLOT_MAX]; /* ui_field_id_t per slot, in the layout's slot order, or per cell */
    uint8_t split[UI_SPLIT_NODES]; /* the split layout's tree (ui_split.h); all 0 for the others */
    uint8_t clock;              /* ui_clock_mode_t */
    bool seconds;
    bool invert;
    uint8_t stale_policy;   /* ui_stale_policy_t */
    bool status_clock;      /* a small clock in the middle of the status bar */
    uint8_t status_battery; /* UI_STATUS_BAT_* bits */
} ui_preset_t;

typedef enum {
    UI_SCHED_PRESET, /* make a preset active */
    UI_SCHED_NIGHT,  /* night sleep until `until_min` (spec §9.1) */
} ui_sched_action_t;

/* A schedule entry (spec §5.4, D15). Times are local minutes after midnight. */
typedef struct {
    uint16_t at_min;
    uint8_t days;   /* bit 0 Monday ... bit 6 Sunday */
    uint8_t action; /* ui_sched_action_t */
    uint8_t preset; /* index into presets, for UI_SCHED_PRESET */
    uint16_t until_min; /* for UI_SCHED_NIGHT, never `at_min`: before it means the next day */
} ui_schedule_entry_t;

typedef struct {
    bool enabled;
    uint8_t count;
    ui_schedule_entry_t entries[UI_SCHEDULE_MAX];
} ui_schedule_t;

/* When the entries run (spec §5.4), with the local-time rules of spec §9.2. The first time after
 * `after` that an entry runs, with that entry's index in *index; 0 if none ever will. */
time_t ui_schedule_next(const ui_schedule_t *schedule, time_t after, int *index);
/* The entries due in (after, now], in the order they run: by time, and within a minute the presets
 * in list order before a night. Returns how many; `order` gets their indexes. */
int ui_schedule_due(const ui_schedule_t *schedule, time_t after, time_t now, int order[UI_SCHEDULE_MAX]);
/* One check (spec §5.4): the entries due since *checked, as ui_schedule_due() gives them, and
 * *checked becomes now. Nothing runs late: the first check (*checked is 0), a clock that moved
 * back, and a gap longer than max_gap_s, a sleep no entry could end, only move the mark. */
int ui_schedule_step(const ui_schedule_t *schedule, time_t *checked, time_t now, time_t max_gap_s,
                     int order[UI_SCHEDULE_MAX]);
/* Where the checks resume after a night that ended at `until`: a night covers [start, until), so
 * the entries inside it don't run, and one at the end minute does. */
time_t ui_schedule_after_night(time_t until);

/* Built-in presets a file from an earlier firmware is offered once (spec §5.4): presets.json's
 * "offered" names them, so one deleted later stays deleted. */
enum {
    UI_OFFERED_RAIN = 1u << 0,    /* "rain": Rain radar (M6) */
    UI_OFFERED_FLIGHTS = 1u << 1, /* "flights": Flights (M6) */
    UI_OFFERED_SOLAR = 1u << 2,   /* "solar": Solar (M6d) */
    UI_OFFERED_ENERGY = 1u << 3,  /* "energy": Energy (M6d) */
};
#define UI_OFFERED_ALL (UI_OFFERED_RAIN | UI_OFFERED_FLIGHTS | UI_OFFERED_SOLAR | UI_OFFERED_ENERGY)

typedef struct {
    uint8_t count;
    uint8_t active; /* index into presets */
    bool cycle_enabled;
    uint16_t cycle_interval_s;
    ui_preset_t presets[UI_PRESET_MAX];
    ui_schedule_t schedule;
    uint8_t offered; /* UI_OFFERED_* */
} ui_presets_t;

/* How many of `p`'s slots its layout uses: a fixed layout's slots, or the cells of its split tree
 * (0 for a tree cut short). */
int ui_preset_slots(const ui_preset_t *p);

/* The built-in presets (spec §5.4): used when presets.json is missing or invalid. */
void ui_presets_defaults(ui_presets_t *p);
int ui_presets_find(const ui_presets_t *p, const char *id); /* index, or -1 */
/* The next preset in cycle order after the active one, wrapping; the active one if no other
 * preset is in the cycle (spec §5.4, KEY short). Presets on the Flights layout are in it only in
 * sync mode `always` (D28). */
int ui_presets_next(const ui_presets_t *p, bool always);
/* The auto-cycle's next preset (spec §5.4, D41): as ui_presets_next(), skipping presets whose cycle window is
 * closed at `now` at the location (1e-4 degrees); the active one when no other is open. `now` 0, the clock not
 * set: every window counts as open. */
int ui_presets_cycle_next(const ui_presets_t *p, bool always, time_t now, int32_t lat_e4, int32_t lon_e4);
/* Offers the built-ins `offered` doesn't name yet: each joins at the end if its id is free and
 * there is room, and is marked offered either way. True if anything changed: save the file. */
bool ui_presets_offer_builtins(ui_presets_t *p);
/* Parses and validates presets.json. On failure returns false with a reason in `err` and leaves
 * *out unspecified. */
bool ui_presets_from_json(const char *json, ui_presets_t *out, char *err, size_t err_size);
/* Serialises; returns the length written, or 0 if `size` is too small. */
size_t ui_presets_to_json(const ui_presets_t *p, char *out, size_t size);
