#include "ui_preset.h"

#include <stdio.h>
#include <string.h>

#include "ui_fields.h"

static ui_preset_t make(const char *id, const char *name, ui_layout_id_t layout, bool in_cycle,
                        const ui_field_id_t slots[UI_SLOT_MAX])
{
    ui_preset_t p = { .layout = (uint8_t)layout, .in_cycle = in_cycle, .stale_policy = UI_STALE_STALE,
                      .status_battery = UI_STATUS_BAT_PERCENT };
    snprintf(p.id, sizeof(p.id), "%s", id);
    snprintf(p.name, sizeof(p.name), "%s", name);
    for (int i = 0; i < UI_SLOT_MAX; i++) {
        p.slots[i] = (uint8_t)slots[i];
    }
    return p;
}

void ui_presets_defaults(ui_presets_t *p)
{
    memset(p, 0, sizeof(*p));
    p->presets[0] = make("home", "Home", UI_LAYOUT_CLASSIC, true,
                         (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_TIME_CLOCK, UI_FIELD_DATE_DAY, UI_FIELD_ENV_TEMP,
                                                       UI_FIELD_ENV_HUM, UI_FIELD_MOON_PHASE, UI_FIELD_BAT_LEVEL });
    p->presets[1] = make("indoor", "Indoor", UI_LAYOUT_GRID, true,
                         (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_ENV_TEMP, UI_FIELD_ENV_HUM, UI_FIELD_ENV_DEW,
                                                       UI_FIELD_ENV_TEMP_MIN, UI_FIELD_ENV_TEMP_MAX,
                                                       UI_FIELD_BAT_DAYS });
    p->presets[2] = make("weather", "Weather", UI_LAYOUT_WEATHER, true, /* in the cycle since M5 brings its data */
                         (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_WX_NOW, UI_FIELD_WX_TODAY, UI_FIELD_WX_HOURLY,
                                                       UI_FIELD_ENV_TEMP, UI_FIELD_ENV_HUM, UI_FIELD_NONE });
    p->presets[3] = make("focus", "Focus clock", UI_LAYOUT_FOCUS, true,
                         (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_TIME_CLOCK, UI_FIELD_DATE_DAY, UI_FIELD_ENV_TEMP,
                                                       UI_FIELD_NONE, UI_FIELD_NONE, UI_FIELD_NONE });
    p->presets[1].status_clock = true; /* data first: the time goes to the status bar */
    p->presets[2].status_clock = true;
    p->count = 4;
    p->active = 0;
    p->cycle_enabled = false;
    p->cycle_interval_s = 60;
    ui_presets_offer_builtins(p); /* the radars (M6), Solar and Energy (M6d) */
}

/* The built-ins added after M5, whose layouts have no slots: the two radars (D28) in the cycle, Solar and Energy
 * (D35, D36) outside it. */
static const struct {
    uint8_t bit;
    const char *id, *name;
    ui_layout_id_t layout;
    bool in_cycle;
} k_offered[] = {
    { UI_OFFERED_RAIN, "rain", "Rain radar", UI_LAYOUT_RADAR, true },
    { UI_OFFERED_FLIGHTS, "flights", "Flights", UI_LAYOUT_FLIGHTS, true },
    { UI_OFFERED_SOLAR, "solar", "Solar", UI_LAYOUT_SOLAR, false },
    { UI_OFFERED_ENERGY, "energy", "Energy", UI_LAYOUT_ENERGY, false },
};

bool ui_presets_offer_builtins(ui_presets_t *p)
{
    bool changed = false;
    for (size_t i = 0; i < sizeof(k_offered) / sizeof(k_offered[0]); i++) {
        if (p->offered & k_offered[i].bit) {
            continue;
        }
        if (ui_presets_find(p, k_offered[i].id) < 0 && p->count < UI_PRESET_MAX) {
            ui_preset_t *added = &p->presets[p->count++];
            *added = make(k_offered[i].id, k_offered[i].name, k_offered[i].layout, k_offered[i].in_cycle,
                          (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_NONE });
            added->status_clock = true; /* the view fills the screen: the time goes to the status bar */
        }
        p->offered |= k_offered[i].bit;
        changed = true;
    }
    return changed;
}

int ui_preset_slots(const ui_preset_t *p)
{
    if (p->layout == UI_LAYOUT_SPLIT) {
        int nodes = ui_split_nodes(p->split);
        return nodes > 0 ? (nodes + 1) / 2 : 0; /* a tree of n cells has 2n - 1 nodes */
    }
    const ui_layout_t *layout = ui_layout((ui_layout_id_t)p->layout);
    return layout != NULL ? layout->slot_count : 0;
}

int ui_presets_find(const ui_presets_t *p, const char *id)
{
    for (int i = 0; id != NULL && i < p->count; i++) {
        if (strcmp(p->presets[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int ui_presets_next(const ui_presets_t *p, bool always)
{
    for (int step = 1; step < p->count; step++) {
        int i = (p->active + step) % p->count;
        if (p->presets[i].in_cycle && (always || p->presets[i].layout != UI_LAYOUT_FLIGHTS)) {
            return i;
        }
    }
    return p->active;
}
