#include <string.h>

#include "unity.h"
#include "webui_http.h"

void setUp(void) {}
void tearDown(void) {}

static void test_only_json_counts_as_json(void)
{
    TEST_ASSERT_TRUE(webui_is_json_type("application/json"));
    TEST_ASSERT_TRUE(webui_is_json_type("Application/JSON; charset=utf-8"));
    TEST_ASSERT_FALSE(webui_is_json_type("application/x-www-form-urlencoded")); /* a cross-site form */
    TEST_ASSERT_FALSE(webui_is_json_type("text/plain"));
    TEST_ASSERT_FALSE(webui_is_json_type("application/jsonp"));
    TEST_ASSERT_FALSE(webui_is_json_type(NULL));
}

static void test_query_values_are_url_decoded(void)
{
    char out[16];
    TEST_ASSERT_EQUAL_INT(6, webui_url_decode("My%20Net", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("My Net", out);
    TEST_ASSERT_EQUAL_INT(8, webui_url_decode("Kav%C3%A1rna", out, sizeof(out))); /* UTF-8 bytes */
    TEST_ASSERT_EQUAL_STRING("Kav\xC3\xA1rna", out);
    TEST_ASSERT_EQUAL_INT(3, webui_url_decode("a+b", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a b", out);
    TEST_ASSERT_EQUAL_INT(-1, webui_url_decode("bad%2", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, webui_url_decode("nul%00", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, webui_url_decode("0123456789abcdef", out, sizeof(out))); /* no room */
    TEST_ASSERT_EQUAL_INT(0, webui_url_decode("", out, sizeof(out)));
}

static void test_the_host_header_is_matched_loosely(void)
{
    TEST_ASSERT_TRUE(webui_host_is("reflbo-bb94.local", "reflbo-bb94.local"));
    TEST_ASSERT_TRUE(webui_host_is("REFLBO-BB94.local.", "reflbo-bb94.local"));
    TEST_ASSERT_TRUE(webui_host_is("192.168.4.1:80", "192.168.4.1"));
    TEST_ASSERT_FALSE(webui_host_is("captive.apple.com", "192.168.4.1"));
    TEST_ASSERT_FALSE(webui_host_is("192.168.4.10", "192.168.4.1"));
    TEST_ASSERT_FALSE(webui_host_is(NULL, "192.168.4.1"));
    /* spec §10.4: the device's own name, alone or under a domain, and nothing else */
    TEST_ASSERT_TRUE(webui_host_under("reflbo-bb94", "reflbo-bb94"));
    TEST_ASSERT_TRUE(webui_host_under("reflbo-bb94.local", "reflbo-bb94"));
    TEST_ASSERT_TRUE(webui_host_under("Reflbo-BB94.fritz.box:80", "reflbo-bb94"));
    TEST_ASSERT_FALSE(webui_host_under("reflbo-bb94.", "reflbo-bb94"));
    TEST_ASSERT_FALSE(webui_host_under("reflbo-bb941.local", "reflbo-bb94"));
    TEST_ASSERT_FALSE(webui_host_under("evil.example", "reflbo-bb94"));
    TEST_ASSERT_FALSE(webui_host_under("reflbo-bb94-evil.example", "reflbo-bb94"));
    TEST_ASSERT_FALSE(webui_host_under(NULL, "reflbo-bb94"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_only_json_counts_as_json);
    RUN_TEST(test_query_values_are_url_decoded);
    RUN_TEST(test_the_host_header_is_matched_loosely);
    return UNITY_END();
}
