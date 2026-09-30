#include <string.h>

#include "ui_screens.h"
#include "unity.h"

static ui_config_view_t s_v;
static char s_text[128];

void setUp(void)
{
    s_v = (ui_config_view_t){ .state = UI_NET_AP, .ap_on = true, .ssid = "", .ip = "", .host = "reflbo-bb94",
                              .ap_ssid = "reflbo-bb94", .ap_pass = "k7m2xq9pde", .minutes_left = 10 };
}

void tearDown(void) {}

static void test_the_ap_code_joins_the_devices_network(void)
{
    TEST_ASSERT_EQUAL(UI_QR_JOIN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:reflbo-bb94;P:k7m2xq9pde;;", s_text);
    s_v.qr_url = true; /* KEY: the address of the web UI on the AP */
    TEST_ASSERT_EQUAL(UI_QR_OPEN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("http://192.168.4.1/", s_text);
    TEST_ASSERT_TRUE(ui_config_can_switch(&s_v));
}

/* The Wi-Fi QR format escapes \ ; , : and " with a backslash. */
static void test_special_characters_in_the_join_code_are_escaped(void)
{
    s_v.ap_ssid = "a;b,c";
    s_v.ap_pass = "p:\"q\\";
    TEST_ASSERT_EQUAL(UI_QR_JOIN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:a\\;b\\,c;P:p\\:\\\"q\\\\;;", s_text);
}

static void test_on_a_network_the_code_opens_the_devices_address(void)
{
    s_v = (ui_config_view_t){ .state = UI_NET_STATION, .ssid = "Home", .ip = "192.168.1.57", .host = "reflbo-bb94",
                              .ap_ssid = "reflbo-bb94", .ap_pass = "k7m2xq9pde" };
    TEST_ASSERT_EQUAL(UI_QR_OPEN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.57/", s_text); /* Android may not resolve .local names */
    TEST_ASSERT_FALSE(ui_config_can_switch(&s_v)); /* no AP to join */
}

/* D18: while no web password is set the AP runs beside the station, and its code comes first. */
static void test_with_the_ap_beside_a_network_key_switches_the_codes(void)
{
    s_v = (ui_config_view_t){ .state = UI_NET_STATION, .ap_on = true, .ssid = "Home", .ip = "192.168.1.57",
                              .host = "reflbo-bb94", .ap_ssid = "reflbo-bb94", .ap_pass = "k7m2xq9pde" };
    TEST_ASSERT_EQUAL(UI_QR_JOIN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_TRUE(ui_config_can_switch(&s_v));
    s_v.qr_url = true;
    TEST_ASSERT_EQUAL(UI_QR_OPEN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.57/", s_text);
}

static void test_no_code_until_there_is_something_to_join_or_open(void)
{
    s_v.state = UI_NET_STARTING;
    s_v.ap_on = false;
    TEST_ASSERT_EQUAL(UI_QR_NONE, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("", s_text);
    s_v.state = UI_NET_JOINING;
    s_v.ssid = "Home";
    TEST_ASSERT_EQUAL(UI_QR_NONE, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    s_v.ap_on = true; /* joining with the AP up (D18) */
    s_v.qr_url = true;
    TEST_ASSERT_EQUAL(UI_QR_JOIN, ui_config_qr(&s_v, s_text, sizeof(s_text))); /* no address yet */
    TEST_ASSERT_FALSE(ui_config_can_switch(&s_v));
    s_v = (ui_config_view_t){ .state = UI_NET_STATION, .ssid = "Home", .ip = "", .host = "reflbo-bb94" };
    TEST_ASSERT_EQUAL(UI_QR_NONE, ui_config_qr(&s_v, s_text, sizeof(s_text))); /* lost the network */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_ap_code_joins_the_devices_network);
    RUN_TEST(test_special_characters_in_the_join_code_are_escaped);
    RUN_TEST(test_on_a_network_the_code_opens_the_devices_address);
    RUN_TEST(test_with_the_ap_beside_a_network_key_switches_the_codes);
    RUN_TEST(test_no_code_until_there_is_something_to_join_or_open);
    return UNITY_END();
}
