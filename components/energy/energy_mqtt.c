#include "energy_mqtt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The house's energy from mapped MQTT values (spec §12.11, D40). */

static bool is(const char *unit, const char *name)
{
    return unit != NULL && strcmp(unit, name) == 0;
}

/* A power in W, its sign as it came: kW and W, anything else as W. */
static double power_w(const energy_mqtt_value_t *v)
{
    return is(v->unit, "kW") ? v->value * 1000.0 : v->value;
}

static int32_t watts(double w)
{
    return (int32_t)lround(w < -1e7 ? -1e7 : w > 1e7 ? 1e7 : w); /* as SolaX's are clamped */
}

/* A counter in Wh: Wh and kWh, anything else as kWh; at most 4 GWh, never below 0, ENERGY_WH_NONE without one. */
static uint32_t counter_wh(const energy_mqtt_value_t *v)
{
    if (!v->fresh) {
        return ENERGY_WH_NONE;
    }
    double wh = is(v->unit, "Wh") ? v->value : v->value * 1000.0;
    return wh <= 0 ? 0 : wh >= 4e9 ? 4000000000u : (uint32_t)lround(wh);
}

void energy_mqtt_shift(energy_reading_t *r, int64_t delta_s)
{
    if (r->at != 0) {
        int64_t at = (int64_t)r->at + delta_s;
        r->at = at < 1 ? 1 : at > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)at;
    }
}

bool energy_mqtt_reading(const energy_mqtt_value_t v[ENERGY_MQTT_COUNT], const energy_mqtt_signs_t *signs,
                         energy_reading_t *out, char *err, size_t err_size)
{
    if (!v[ENERGY_MQTT_PV].fresh || !v[ENERGY_MQTT_GRID].fresh) {
        snprintf(err, err_size, "no data");
        return false;
    }
    memset(out, 0, sizeof(*out));
    double pv = power_w(&v[ENERGY_MQTT_PV]);
    double grid = power_w(&v[ENERGY_MQTT_GRID]) * (signs->grid_export ? -1 : 1);             /* + import */
    double bat = v[ENERGY_MQTT_BATTERY].fresh ? power_w(&v[ENERGY_MQTT_BATTERY]) : 0;
    bat *= signs->bat_discharge ? -1 : 1;                                                   /* + charging */
    double load = v[ENERGY_MQTT_LOAD].fresh ? power_w(&v[ENERGY_MQTT_LOAD]) : pv + grid - bat;
    out->pv_w = watts(pv < 0 ? 0 : pv);
    out->grid_w = watts(grid);
    out->bat_w = watts(bat);
    out->load_w = watts(load < 0 ? 0 : load);
    for (int i = ENERGY_MQTT_PV; i <= ENERGY_MQTT_BATTERY; i++) { /* the newest power's arrival */
        if (v[i].fresh && v[i].at > out->at) {
            out->at = v[i].at;
        }
    }
    const energy_mqtt_value_t *soc = &v[ENERGY_MQTT_SOC];
    out->soc = !soc->fresh || isnan(soc->value) ? -1
               : soc->value <= 0               ? 0
               : soc->value >= 100             ? 100
                                               : (int16_t)lround(soc->value);
    out->yield_wh = counter_wh(&v[ENERGY_MQTT_YIELD]);
    out->to_grid_wh = counter_wh(&v[ENERGY_MQTT_TO_GRID]);
    out->from_grid_wh = counter_wh(&v[ENERGY_MQTT_FROM_GRID]);
    out->today = !signs->lifetime;
    return true;
}
