#include <string.h>

#include "netmgr_scan.h"
#include "unity.h"

/* A scan as the driver reports it: one entry per access point, strongest first. */
static const netmgr_seen_t k_seen[] = {
    { .ssid = "Neighbour", .bssid = { 1 }, .channel = 11, .rssi = -48 },
    { .ssid = "Home", .bssid = { 2 }, .channel = 6, .rssi = -55 },
    { .ssid = "", .bssid = { 3 }, .channel = 3, .rssi = -60 }, /* hidden */
    { .ssid = "Home", .bssid = { 4 }, .channel = 1, .rssi = -71 }, /* a second access point */
    { .ssid = "Cafe", .bssid = { 5 }, .channel = 9, .rssi = -80, .open = true },
};
#define SEEN (int)(sizeof(k_seen) / sizeof(k_seen[0]))

void setUp(void) {}
void tearDown(void) {}

static void test_the_best_sighting_is_the_strongest_access_point(void)
{
    TEST_ASSERT_EQUAL_INT(1, netmgr_seen_best(k_seen, SEEN, "Home"));
    TEST_ASSERT_EQUAL_INT(4, netmgr_seen_best(k_seen, SEEN, "Cafe"));
    TEST_ASSERT_EQUAL_INT(-1, netmgr_seen_best(k_seen, SEEN, "Office"));
    TEST_ASSERT_EQUAL_INT(-1, netmgr_seen_best(k_seen, SEEN, "")); /* hidden networks can't be picked */
    netmgr_seen_t weaker_first[] = { k_seen[3], k_seen[1] };
    TEST_ASSERT_EQUAL_INT(1, netmgr_seen_best(weaker_first, 2, "Home")); /* whatever the order */
}

static void test_the_list_for_the_web_ui_has_one_entry_per_name(void)
{
    netmgr_ap_t out[8];
    int n = netmgr_seen_unique(k_seen, SEEN, out, 8);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_STRING("Neighbour", out[0].ssid);
    TEST_ASSERT_EQUAL_STRING("Home", out[1].ssid);
    TEST_ASSERT_EQUAL_INT(-55, out[1].rssi);
    TEST_ASSERT_EQUAL_STRING("Cafe", out[2].ssid);
    TEST_ASSERT_TRUE(out[2].open);
    TEST_ASSERT_EQUAL_INT(2, netmgr_seen_unique(k_seen, SEEN, out, 2)); /* stops at max */
}

/* The AP shares the radio with the station: on the channel of the network the owner will most
 * likely pick, a test doesn't move it and drop the phone. */
static void test_the_ap_starts_on_the_channel_of_the_likely_network(void)
{
    netmgr_list_t saved = { 0 };
    TEST_ASSERT_EQUAL_UINT8(11, netmgr_ap_channel(k_seen, SEEN, &saved)); /* none saved: the strongest */
    netmgr_list_add(&saved, "Home", "12345678");
    TEST_ASSERT_EQUAL_UINT8(6, netmgr_ap_channel(k_seen, SEEN, &saved)); /* a saved one in sight */
    TEST_ASSERT_EQUAL_UINT8(1, netmgr_ap_channel(k_seen, 0, &saved));    /* nothing in sight */
    netmgr_seen_t hidden_only[] = { k_seen[2] };
    TEST_ASSERT_EQUAL_UINT8(1, netmgr_ap_channel(hidden_only, 1, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_best_sighting_is_the_strongest_access_point);
    RUN_TEST(test_the_list_for_the_web_ui_has_one_entry_per_name);
    RUN_TEST(test_the_ap_starts_on_the_channel_of_the_likely_network);
    return UNITY_END();
}
