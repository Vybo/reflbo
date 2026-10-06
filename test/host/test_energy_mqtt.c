#include <string.h>

#include "energy.h"
#include "energy_mqtt.h"
#include "unity.h"

/* The house's energy from mapped MQTT values (spec §12.11, D40): their units, the signs the settings give them, the
 * house's use where no value says it, the counters, and no reading without a fresh solar and grid value. */

#define AT ((uint32_t)1790859600) /* 2026-10-01 13:00 UTC: when a value came */

static energy_mqtt_value_t s_v[ENERGY_MQTT_COUNT];
static energy_reading_t s_r;
static char s_err[32];

static void set(energy_mqtt_item_t i, double value, const char *unit, uint32_t at)
{
    s_v[i] = (energy_mqtt_value_t){ .fresh = true, .value = value, .unit = unit, .at = at };
}

void setUp(void)
{
    memset(s_v, 0, sizeof(s_v));
    memset(&s_r, 0, sizeof(s_r));
    s_err[0] = '\0';
}

void tearDown(void) {}

/* spec §12.11's example: solar in kW, the grid in W (+ import), today's counters in kWh and Wh. */
static void test_a_reading_from_the_mapped_values(void)
{
    set(ENERGY_MQTT_PV, 3.42, "kW", AT - 20);
    set(ENERGY_MQTT_GRID, -1200, "W", AT);
    set(ENERGY_MQTT_YIELD, 12.3, "kWh", AT - 300);
    set(ENERGY_MQTT_TO_GRID, 7.25, "kWh", AT - 300);
    set(ENERGY_MQTT_FROM_GRID, 850, "Wh", AT - 300);
    energy_mqtt_signs_t signs = { 0 };
    TEST_ASSERT_TRUE_MESSAGE(energy_mqtt_reading(s_v, &signs, &s_r, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_UINT32(AT, s_r.at); /* the newest power's arrival */
    TEST_ASSERT_EQUAL_INT32(3420, s_r.pv_w);
    TEST_ASSERT_EQUAL_INT32(-1200, s_r.grid_w); /* 1.2 kW going out */
    TEST_ASSERT_EQUAL_INT32(2220, s_r.load_w);  /* solar + import - export */
    TEST_ASSERT_EQUAL_INT32(0, s_r.bat_w);
    TEST_ASSERT_EQUAL_INT16(-1, s_r.soc);
    TEST_ASSERT_EQUAL_UINT8(0, s_r.inverter);
    TEST_ASSERT_EQUAL_UINT32(12300, s_r.yield_wh);
    TEST_ASSERT_EQUAL_UINT32(7250, s_r.to_grid_wh);
    TEST_ASSERT_EQUAL_UINT32(850, s_r.from_grid_wh);
    TEST_ASSERT_TRUE(s_r.today);
}

/* grid_sign export: a positive grid value goes out; battery_sign discharge: a positive battery value comes out of it.
 * Ours: + from the grid, + charging; the house takes what is left. */
static void test_the_signs_turn_values_into_ours(void)
{
    set(ENERGY_MQTT_PV, 500, "W", AT);
    set(ENERGY_MQTT_GRID, 300, "W", AT);
    set(ENERGY_MQTT_BATTERY, 1.5, "kW", AT);
    set(ENERGY_MQTT_SOC, 64.4, "%", AT);
    energy_mqtt_signs_t signs = { .grid_export = true, .bat_discharge = true };
    TEST_ASSERT_TRUE(energy_mqtt_reading(s_v, &signs, &s_r, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_INT32(-300, s_r.grid_w);
    TEST_ASSERT_EQUAL_INT32(-1500, s_r.bat_w);
    TEST_ASSERT_EQUAL_INT32(1700, s_r.load_w); /* 500 - 300 + 1500 */
    TEST_ASSERT_EQUAL_INT16(64, s_r.soc);
    signs = (energy_mqtt_signs_t){ 0 };
    TEST_ASSERT_TRUE(energy_mqtt_reading(s_v, &signs, &s_r, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_INT32(300, s_r.grid_w);
    TEST_ASSERT_EQUAL_INT32(1500, s_r.bat_w);
    TEST_ASSERT_EQUAL_INT32(0, s_r.load_w); /* 500 + 300 - 1500 is below none */
}

/* A mapped home value wins over the sum; a unit other than kW, W, kWh and Wh counts as W and kWh; the charge stays
 * within 0-100 %. */
static void test_a_mapped_home_and_other_units(void)
{
    set(ENERGY_MQTT_PV, 2100, "", AT);
    set(ENERGY_MQTT_GRID, 0.4, "kW", AT);
    set(ENERGY_MQTT_LOAD, 2.35, "kW", AT + 5);
    set(ENERGY_MQTT_SOC, 104, "%", AT);
    set(ENERGY_MQTT_YIELD, 9.8, "", AT);
    energy_mqtt_signs_t signs = { 0 };
    TEST_ASSERT_TRUE(energy_mqtt_reading(s_v, &signs, &s_r, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_INT32(2100, s_r.pv_w);
    TEST_ASSERT_EQUAL_INT32(400, s_r.grid_w);
    TEST_ASSERT_EQUAL_INT32(2350, s_r.load_w);
    TEST_ASSERT_EQUAL_UINT32(AT + 5, s_r.at);
    TEST_ASSERT_EQUAL_INT16(100, s_r.soc);
    TEST_ASSERT_EQUAL_UINT32(9800, s_r.yield_wh);
}

/* A counter not mapped, or not fresh, is none, so its field shows none rather than 0; counters since installation
 * want their midnight, as the Token ID's do. */
static void test_counters_without_a_value_are_none(void)
{
    set(ENERGY_MQTT_PV, 1000, "W", AT);
    set(ENERGY_MQTT_GRID, 0, "W", AT);
    set(ENERGY_MQTT_TO_GRID, 3210.5, "kWh", AT);
    s_v[ENERGY_MQTT_FROM_GRID] = (energy_mqtt_value_t){ .fresh = false, .value = 99, .unit = "kWh", .at = AT };
    energy_mqtt_signs_t signs = { .lifetime = true };
    TEST_ASSERT_TRUE(energy_mqtt_reading(s_v, &signs, &s_r, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, s_r.yield_wh);
    TEST_ASSERT_EQUAL_UINT32(3210500, s_r.to_grid_wh);
    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, s_r.from_grid_wh);
    TEST_ASSERT_FALSE(s_r.today);
}

/* Without a fresh solar and a fresh grid value there is no reading (spec §12.11). */
static void test_no_reading_without_solar_and_grid(void)
{
    set(ENERGY_MQTT_PV, 1000, "W", AT);
    energy_mqtt_signs_t signs = { 0 };
    TEST_ASSERT_FALSE(energy_mqtt_reading(s_v, &signs, &s_r, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("no data", s_err);
    set(ENERGY_MQTT_GRID, 10, "W", AT);
    s_v[ENERGY_MQTT_PV].fresh = false;
    TEST_ASSERT_FALSE(energy_mqtt_reading(s_v, &signs, &s_r, s_err, sizeof(s_err)));
    s_v[ENERGY_MQTT_PV].fresh = true;
    TEST_ASSERT_TRUE(energy_mqtt_reading(s_v, &signs, &s_r, s_err, sizeof(s_err)));
}

/* The largest values a mapping keeps (32 bits) are clamped as SolaX's are: no overflow, a counter at most 4 GWh. */
static void test_values_out_of_reason_are_clamped(void)
{
    set(ENERGY_MQTT_PV, 2147483647.0, "W", AT);
    set(ENERGY_MQTT_GRID, -2147483647.0, "kW", AT);
    set(ENERGY_MQTT_YIELD, 2147483647.0, "kWh", AT);
    set(ENERGY_MQTT_TO_GRID, -5, "kWh", AT);
    set(ENERGY_MQTT_SOC, -3, "%", AT);
    energy_mqtt_signs_t signs = { 0 };
    TEST_ASSERT_TRUE(energy_mqtt_reading(s_v, &signs, &s_r, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_INT32(10000000, s_r.pv_w);
    TEST_ASSERT_EQUAL_INT32(-10000000, s_r.grid_w);
    TEST_ASSERT_EQUAL_UINT32(4000000000u, s_r.yield_wh);
    TEST_ASSERT_EQUAL_UINT32(0, s_r.to_grid_wh);
    TEST_ASSERT_EQUAL_INT16(0, s_r.soc);
}

/* A reading dated by the device's clock (its values' arrival) keeps its age when a sync then sets the clock: after a
 * power-off without the backup cell (D9) the clock starts in 2000 until the sync's time step. */
static void test_a_reading_moves_with_the_clock(void)
{
    energy_reading_t r = { .at = 946684830 }; /* 2000-01-01 00:00:30 UTC */
    energy_mqtt_shift(&r, 844174770);
    TEST_ASSERT_EQUAL_UINT32(1790859600, r.at);
    r.at = 0; /* none stays none */
    energy_mqtt_shift(&r, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, r.at);
    r.at = 100;
    energy_mqtt_shift(&r, -200);
    TEST_ASSERT_EQUAL_UINT32(1, r.at);
    r.at = 4294967000u;
    energy_mqtt_shift(&r, 1000);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, r.at);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_reading_from_the_mapped_values);
    RUN_TEST(test_the_signs_turn_values_into_ours);
    RUN_TEST(test_a_mapped_home_and_other_units);
    RUN_TEST(test_counters_without_a_value_are_none);
    RUN_TEST(test_no_reading_without_solar_and_grid);
    RUN_TEST(test_values_out_of_reason_are_clamped);
    RUN_TEST(test_a_reading_moves_with_the_clock);
    return UNITY_END();
}
