#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "energy.h"

/*
 * The house's energy from mapped MQTT values (spec §12.11, D40): a reading in our signs from the values that HA, an
 * inverter's own integration or another local device publish. The app hands over each value with its mapping's unit
 * and whether it is fresh; this builds the reading. Pure C, host-buildable.
 */

/* The values energy.mqtt maps, in the order of settings_energy_mqtt_t. */
typedef enum {
    ENERGY_MQTT_PV,        /* power: the panels */
    ENERGY_MQTT_GRID,      /* power: the grid, by its sign */
    ENERGY_MQTT_LOAD,      /* power: the house; without it, what the others leave */
    ENERGY_MQTT_BATTERY,   /* power: the battery, by its sign */
    ENERGY_MQTT_SOC,       /* the battery's charge, % */
    ENERGY_MQTT_YIELD,     /* energy: produced today */
    ENERGY_MQTT_TO_GRID,   /* energy: a counter of what went out */
    ENERGY_MQTT_FROM_GRID, /* energy: a counter of what came in */
    ENERGY_MQTT_COUNT,
} energy_mqtt_item_t;

/* One mapped value as the app's store has it. */
typedef struct {
    bool fresh;       /* mapped, with a value its time to live still holds (spec §12.5) */
    double value;     /* as it came, in `unit` */
    const char *unit; /* the mapping's: kW or W for a power, kWh or Wh for an energy; another counts as W and kWh */
    uint32_t at;      /* when it came, UTC */
} energy_mqtt_value_t;

/* energy.mqtt's signs and counters (spec §12.11). */
typedef struct {
    bool grid_export;   /* a positive grid value goes out to the grid ("export"); else it comes in */
    bool bat_discharge; /* a positive battery value comes out of it ("discharge"); else it charges */
    bool lifetime;      /* the grid's counters run since installation and want their midnight; else they are today's.
                           The yield is today's either way, as SolaX's is */
} energy_mqtt_signs_t;

/* A reading from the fresh values: solar, grid, the house (its own value, else solar + import - export - charging +
 * discharging), the battery, its charge (-1 for none) and the counters (ENERGY_WH_NONE for none). Its time is the
 * newest power's arrival. False, with "no data" in `err`, without a fresh solar and grid value. */
bool energy_mqtt_reading(const energy_mqtt_value_t v[ENERGY_MQTT_COUNT], const energy_mqtt_signs_t *signs,
                         energy_reading_t *out, char *err, size_t err_size);
/* The clock moved by `delta_s` after the reading was built (a sync sets it after its MQTT step): its time, its values'
 * arrival on the device's clock, keeps its age. None stays none; it stays within 1 to UINT32_MAX. */
void energy_mqtt_shift(energy_reading_t *r, int64_t delta_s);
