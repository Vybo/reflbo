#include <string.h>

#include "ha_session.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* spec §12.9: a lost connection is tried again after 10 s, the wait doubling to at most 5 min. */
static void test_reconnects_back_off_to_five_minutes(void)
{
    static const uint32_t k_waits[] = { 10000, 20000, 40000, 80000, 160000, 300000, 300000 };
    for (int i = 0; i < (int)(sizeof(k_waits) / sizeof(k_waits[0])); i++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(k_waits[i], ha_backoff_ms(i), "failures");
    }
    TEST_ASSERT_EQUAL_UINT32(300000, ha_backoff_ms(1000));
}

static ha_field_t mapping(const char *key, const char *topic)
{
    ha_field_t f = { .kind = HA_KIND_NUMBER };
    strncpy(f.key, key, sizeof(f.key) - 1);
    strncpy(f.topic, topic, sizeof(f.topic) - 1);
    return f;
}

/* One subscription a topic, however many values it brings (Zigbee2MQTT's devices). */
static void test_topics_are_subscribed_once(void)
{
    static ha_fields_t f;
    f.count = 4;
    f.field[0] = mapping("co2", "z2m/living_room");
    f.field[1] = mapping("outdoor", "ha/statestream/sensor/outdoor/state");
    f.field[2] = mapping("voc", "z2m/living_room");
    f.field[3] = mapping("door", "z2m/door");
    const char *topics[HA_FIELDS_MAX];
    TEST_ASSERT_EQUAL_INT(3, ha_topics(&f, topics));
    TEST_ASSERT_EQUAL_STRING("z2m/living_room", topics[0]);
    TEST_ASSERT_EQUAL_STRING("ha/statestream/sensor/outdoor/state", topics[1]);
    TEST_ASSERT_EQUAL_STRING("z2m/door", topics[2]);
    TEST_ASSERT_EQUAL_INT(2, ha_topic_index(topics, 3, "z2m/door", 8));
    TEST_ASSERT_EQUAL_INT(-1, ha_topic_index(topics, 3, "z2m/doors", 9));
    TEST_ASSERT_EQUAL_INT(-1, ha_topic_index(topics, 3, "z2m/do", 6));
    f.count = 0;
    TEST_ASSERT_EQUAL_INT(0, ha_topics(&f, topics));
}

/* spec §9.3 step 6.3: collect until every mapped topic has arrived, or 1 s passes with none. */
static void test_collecting_ends_with_every_topic_or_a_quiet_second(void)
{
    TEST_ASSERT_TRUE(ha_collect_over(3, 3, 5000, 4990));   /* all came */
    TEST_ASSERT_FALSE(ha_collect_over(3, 2, 5000, 4500));  /* one missing, half a second quiet */
    TEST_ASSERT_TRUE(ha_collect_over(3, 2, 5000, 4000));   /* a quiet second: retained messages came by now */
    TEST_ASSERT_TRUE(ha_collect_over(0, 0, 5000, 4000));   /* no mappings: a quiet second for queued commands */
    TEST_ASSERT_FALSE(ha_collect_over(0, 0, 5000, 4001));
}

/* spec §12.3, §9.3: HA's sensors expire by the sync's interval; in sync mode `always` by 10 min, and the
 * longest span Wi-Fi is off besides, so a night or quiet hours don't mark them unavailable. */
static void test_the_interval_sensors_expire_by(void)
{
    TEST_ASSERT_EQUAL_UINT32(86400, ha_expected_s(false, 86400, 0));
    TEST_ASSERT_EQUAL_UINT32(0, ha_expected_s(false, 0, 7 * 3600)); /* manual: none */
    TEST_ASSERT_EQUAL_UINT32(600, ha_expected_s(true, 3600, 0));
    TEST_ASSERT_EQUAL_UINT32(600 + 7 * 3600, ha_expected_s(true, 7 * 3600, 7 * 3600));
}

/* spec §12.9, sync mode `always`: the state goes out on a change, at most every 30 s, and every 5 min. */
static void test_the_state_goes_out_on_change_or_every_five_minutes(void)
{
    TEST_ASSERT_TRUE(ha_state_due(false, -1));   /* none went out yet */
    TEST_ASSERT_FALSE(ha_state_due(true, 29));
    TEST_ASSERT_TRUE(ha_state_due(true, 30));
    TEST_ASSERT_FALSE(ha_state_due(false, 299));
    TEST_ASSERT_TRUE(ha_state_due(false, 300));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reconnects_back_off_to_five_minutes);
    RUN_TEST(test_topics_are_subscribed_once);
    RUN_TEST(test_collecting_ends_with_every_topic_or_a_quiet_second);
    RUN_TEST(test_the_interval_sensors_expire_by);
    RUN_TEST(test_the_state_goes_out_on_change_or_every_five_minutes);
    return UNITY_END();
}
