#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "gfx.h"
#include "lang.h"

/*
 * The on-device menu (spec §5.7) as a state machine. The app maps gestures to keys (spec §5.6),
 * passes the current values in a model, and carries out the intents the menu returns; the menu
 * itself changes nothing outside its own state. Pure C, host-buildable.
 */

/* Every section and item, in display order: a section's children follow it. */
typedef enum {
    UI_MI_ROOT,
    UI_MI_PRESETS,
    UI_MI_ACTIVE_PRESET,  /* choice: the presets' names */
    UI_MI_AUTO_CYCLE,     /* toggle */
    UI_MI_CYCLE_INTERVAL, /* choice: interval labels */
    UI_MI_SCHEDULE,       /* toggle */
    UI_MI_WIFI,
    UI_MI_CONFIG_MODE,     /* action */
    UI_MI_FORGET_NETWORKS, /* action, confirmed first */
    UI_MI_RESET_PASSWORD,  /* action, confirmed first (D18) */
    UI_MI_SYNC,
    UI_MI_SYNC_NOW,      /* action */
    UI_MI_SYNC_MODE,     /* choice: times, interval, always, manual (spec §9.3) */
    UI_MI_SYNC_INTERVAL, /* choice: interval labels; shown in interval mode */
    UI_MI_QUIET_HOURS,   /* toggle (D25) */
    UI_MI_TIME,
    UI_MI_SET_DATETIME, /* the date-time editor */
    UI_MI_CLOCK_24H,    /* toggle */
    UI_MI_TIME_ZONE,    /* choice: the zone short list */
    UI_MI_DISPLAY,
    UI_MI_UPDATE_INTERVAL, /* number, minutes 1-15 */
    UI_MI_REFRESH_RATE,    /* choice: 0.25-8 Hz */
    UI_MI_SENSORS,
    UI_MI_TEMP_OFFSET, /* number, 0.1 °C, -10.0 to +10.0 */
    UI_MI_HUM_OFFSET,  /* number, 0.1 %, -20.0 to +20.0 in 0.5 steps */
    UI_MI_UNITS,       /* choice: °C, °F */
    UI_MI_INFO,
    UI_MI_INFO_BATTERY, /* info texts */
    UI_MI_INFO_FIRMWARE,
    UI_MI_INFO_DEVICE,
    UI_MI_INFO_IP,
    UI_MI_INFO_MAC,
    UI_MI_INFO_SYNC, /* the last sync's result (spec §5.7) */
    UI_MI_INFO_UPTIME,
    UI_MI_INFO_MEMORY,
    UI_MI_INFO_MQTT, /* M7: the last MQTT session, or the kept connection (D32) */
    UI_MI_SYSTEM,
    UI_MI_LANGUAGE,      /* choice: the language packs */
    UI_MI_REBOOT,        /* action */
    UI_MI_FACTORY_RESET, /* action, confirmed first */
    UI_MI_COUNT,
} ui_menu_item_t;

typedef enum {
    UI_MENU_KEY_NEXT,   /* KEY short: the next item, or + while editing */
    UI_MENU_KEY_SELECT, /* KEY long: open, edit, save, or confirm */
    UI_MENU_KEY_BACK,   /* BOOT short: up a level, or - while editing */
    UI_MENU_KEY_EXIT,   /* BOOT long: close the menu, or cancel an edit */
} ui_menu_key_t;

/* What the app shows and lets the user edit. Values are in each item's own unit (above). */
typedef struct {
    int32_t value[UI_MI_COUNT];
    const char *const *choices[UI_MI_COUNT]; /* labels of choice items, built by the app */
    uint8_t choice_count[UI_MI_COUNT];
    const char *info[UI_MI_COUNT]; /* texts of info items */
    bool hidden[UI_MI_COUNT];      /* features that don't exist yet */
    struct tm local;               /* the date-time editor starts from this */
} ui_menu_model_t;

typedef enum {
    UI_MENU_NONE,     /* only the menu changed: redraw it */
    UI_MENU_SET,      /* item = value */
    UI_MENU_SET_TIME, /* the local date and time in `local` */
    UI_MENU_ACTION,   /* run item: config mode, forget networks, reset the password, sync now, reboot, reset */
    UI_MENU_CLOSE,
} ui_menu_intent_kind_t;

typedef struct {
    ui_menu_intent_kind_t kind;
    ui_menu_item_t item;
    int32_t value;
    struct tm local;
} ui_menu_intent_t;

typedef enum {
    UI_MENU_BROWSE,
    UI_MENU_EDIT,     /* a choice or number */
    UI_MENU_DATETIME, /* the date-time editor */
    UI_MENU_CONFIRM,  /* an action that asks first */
} ui_menu_mode_t;

typedef struct {
    uint8_t section; /* the list shown: UI_MI_ROOT or a section */
    uint8_t cursor;  /* index among the section's visible items */
    uint8_t mode;    /* ui_menu_mode_t */
    int32_t edit;    /* the value being edited */
    struct tm dt;    /* the date and time being edited */
    uint8_t dt_field; /* 0 day, 1 month, 2 year, 3 hour, 4 minute */
} ui_menu_t;

void ui_menu_open(ui_menu_t *m);
ui_menu_intent_t ui_menu_input(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_key_t key);
/* The visible items of the current section, in order; returns their number. */
int ui_menu_visible(const ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t *out, int max);
ui_menu_item_t ui_menu_current(const ui_menu_t *m, const ui_menu_model_t *model);
/* The label of an item in the pack's language. */
const char *ui_menu_label(ui_menu_item_t item, const lang_t *lang);
bool ui_menu_is_section(ui_menu_item_t item);
/* What an action that asks first asks; NULL for any other item. */
const char *ui_menu_question(ui_menu_item_t item, const lang_t *lang);
/* An item's value as the list shows it: "On", "Europe/Prague", "+0,5 °C". */
void ui_menu_value_text(ui_menu_item_t item, int32_t value, const ui_menu_model_t *model, const lang_t *lang,
                        char *out, size_t size);

/* The menu screen: the current list, or the editor or question it shows (spec §5.7). */
void ui_draw_menu(gfx_fb_t *fb, const ui_menu_t *m, const ui_menu_model_t *model, const lang_t *lang);
