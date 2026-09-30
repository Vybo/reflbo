#include <string.h>

#include "netmgr_list.h"
#include "unity.h"

static netmgr_list_t s_l;

void setUp(void)
{
    memset(&s_l, 0, sizeof(s_l));
}

void tearDown(void) {}

static void test_passwords_follow_wpa2_rules(void)
{
    TEST_ASSERT_TRUE(netmgr_net_valid("home", "12345678"));
    TEST_ASSERT_TRUE(netmgr_net_valid("cafe", "")); /* open */
    TEST_ASSERT_FALSE(netmgr_net_valid("home", "1234567"));
    TEST_ASSERT_FALSE(netmgr_net_valid("", "12345678"));
    TEST_ASSERT_FALSE(netmgr_net_valid("an-ssid-that-is-longer-than-32-by", "12345678"));
    char hex[65];
    memset(hex, 'a', 64);
    hex[64] = '\0';
    TEST_ASSERT_TRUE(netmgr_net_valid("home", hex)); /* a 64-digit key */
    hex[10] = 'g';
    TEST_ASSERT_FALSE(netmgr_net_valid("home", hex));
    TEST_ASSERT_FALSE(netmgr_net_valid("home", "tab\there!"));
    TEST_ASSERT_TRUE(netmgr_net_valid("Kavárna", "presne 8"));   /* a UTF-8 SSID is fine */
    TEST_ASSERT_FALSE(netmgr_net_valid("Kavárna", "přesně 8")); /* a passphrase is ASCII (802.11i) */
}

static void test_new_networks_go_first_and_a_full_list_drops_its_last(void)
{
    const char *names[] = { "a", "b", "c", "d", "e", "f" };
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_TRUE(netmgr_list_add(&s_l, names[i], "password"));
    }
    TEST_ASSERT_EQUAL_INT(5, s_l.count);
    TEST_ASSERT_EQUAL_STRING("f", s_l.nets[0].ssid);
    TEST_ASSERT_EQUAL_STRING("b", s_l.nets[4].ssid); /* "a" dropped */
    TEST_ASSERT_EQUAL_INT(-1, netmgr_list_find(&s_l, "a"));
}

static void test_a_success_moves_a_network_first_and_keeps_where_it_was_found(void)
{
    netmgr_list_add(&s_l, "home", "password");
    netmgr_list_add(&s_l, "office", "password");
    const uint8_t bssid[6] = { 1, 2, 3, 4, 5, 6 };
    netmgr_list_succeeded(&s_l, netmgr_list_find(&s_l, "home"), bssid, 11);
    TEST_ASSERT_EQUAL_STRING("home", s_l.nets[0].ssid);
    TEST_ASSERT_EQUAL_MEMORY(bssid, s_l.nets[0].bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(11, s_l.nets[0].channel);
    netmgr_list_add(&s_l, "home", "password"); /* same password: the cache stays */
    TEST_ASSERT_EQUAL_UINT8(11, s_l.nets[0].channel);
    netmgr_list_add(&s_l, "home", "new-password"); /* a new one drops it */
    TEST_ASSERT_EQUAL_UINT8(0, s_l.nets[0].channel);
    TEST_ASSERT_EQUAL_STRING("new-password", s_l.nets[0].pass);
    TEST_ASSERT_EQUAL_INT(2, s_l.count);
}

static void test_forgetting_a_network_closes_the_gap(void)
{
    netmgr_list_add(&s_l, "a", "password");
    netmgr_list_add(&s_l, "b", "password");
    netmgr_list_add(&s_l, "c", "password"); /* c b a */
    TEST_ASSERT_TRUE(netmgr_list_forget(&s_l, "b"));
    TEST_ASSERT_EQUAL_INT(2, s_l.count);
    TEST_ASSERT_EQUAL_STRING("c", s_l.nets[0].ssid);
    TEST_ASSERT_EQUAL_STRING("a", s_l.nets[1].ssid);
    TEST_ASSERT_FALSE(netmgr_list_forget(&s_l, "b"));
    TEST_ASSERT_FALSE(netmgr_list_add(&s_l, "x", "short"));
    TEST_ASSERT_EQUAL_INT(2, s_l.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_passwords_follow_wpa2_rules);
    RUN_TEST(test_new_networks_go_first_and_a_full_list_drops_its_last);
    RUN_TEST(test_a_success_moves_a_network_first_and_keeps_where_it_was_found);
    RUN_TEST(test_forgetting_a_network_closes_the_gap);
    return UNITY_END();
}
