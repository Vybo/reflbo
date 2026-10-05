#include <string.h>

#include "unity.h"
#include "util_url.h"

/* A URL's host, which is all `fetch` logs (spec §10.4): a key in the path (Forecast.Solar's) or the query (SolaX's
 * token) never reaches the log. */

void setUp(void) {}
void tearDown(void) {}

static const char *host(const char *url)
{
    static char out[64];
    util_url_host(url, out, sizeof(out));
    return out;
}

static void test_the_host_alone(void)
{
    TEST_ASSERT_EQUAL_STRING("api.forecast.solar",
                             host("https://api.forecast.solar/AbC123/estimate/watts/49/16"));
    TEST_ASSERT_EQUAL_STRING("www.solaxcloud.com",
                             host("https://www.solaxcloud.com/proxyApp/proxy/api/getRealtimeInfo.do"
                                  "?tokenId=2020&sn=X"));
    TEST_ASSERT_EQUAL_STRING("example.com:8080", host("http://example.com:8080/x"));
    TEST_ASSERT_EQUAL_STRING("example.com", host("https://example.com?q=1"));
    TEST_ASSERT_EQUAL_STRING("example.com", host("https://example.com#frag"));
    TEST_ASSERT_EQUAL_STRING("example.com", host("example.com/path"));
}

static void test_no_user_or_password(void)
{
    TEST_ASSERT_EQUAL_STRING("host", host("https://user:secret@host/x"));
    TEST_ASSERT_EQUAL_STRING("host", host("https://a@b:c@host"));
}

static void test_short_buffers_and_nothing(void)
{
    char out[5];
    TEST_ASSERT_EQUAL_UINT(4, util_url_host("https://example.com/x", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("exam", out);
    TEST_ASSERT_EQUAL_UINT(0, util_url_host(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_STRING("", host(""));
    TEST_ASSERT_EQUAL_STRING("", host("https:///path"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_host_alone);
    RUN_TEST(test_no_user_or_password);
    RUN_TEST(test_short_buffers_and_nothing);
    return UNITY_END();
}
