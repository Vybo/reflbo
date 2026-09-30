#include <string.h>

#include "netmgr_dns.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t k_ip[4] = { 192, 168, 4, 1 };

/* A query for connectivitycheck.gstatic.com: ID 0x1234, RD set, one question. */
static size_t make_query(uint8_t *q, uint16_t type, uint16_t additional)
{
    static const uint8_t header[] = { 0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0 };
    static const uint8_t name[] = "\x11" "connectivitycheck" "\x07" "gstatic" "\x03" "com";
    size_t n = 0;
    memcpy(q, header, sizeof(header));
    n += sizeof(header);
    memcpy(q + n, name, sizeof(name)); /* includes the terminating zero label */
    n += sizeof(name);
    q[n++] = (uint8_t)(type >> 8);
    q[n++] = (uint8_t)type;
    q[n++] = 0;
    q[n++] = 1; /* IN */
    q[11] = (uint8_t)additional;
    return n;
}

static void test_an_a_query_is_answered_with_the_ap_address(void)
{
    uint8_t q[128], r[256];
    size_t n = make_query(q, 1, 0);
    size_t m = netmgr_dns_reply(q, n, k_ip, r, sizeof(r));
    TEST_ASSERT_EQUAL_UINT(n + 16, m);
    TEST_ASSERT_EQUAL_HEX8(0x12, r[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, r[1]);
    TEST_ASSERT_EQUAL_HEX8(0x85, r[2]); /* response, authoritative, recursion desired */
    TEST_ASSERT_EQUAL_HEX8(0x00, r[3]); /* no error */
    TEST_ASSERT_EQUAL_HEX8(1, r[7]);    /* one answer */
    TEST_ASSERT_EQUAL_MEMORY(q + 12, r + 12, n - 12); /* the question as asked */
    const uint8_t answer[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, 192, 168, 4, 1 };
    TEST_ASSERT_EQUAL_MEMORY(answer, r + n, sizeof(answer));
}

static void test_other_types_get_no_records_and_edns_is_not_echoed(void)
{
    uint8_t q[128], r[256];
    size_t n = make_query(q, 28, 0); /* AAAA */
    TEST_ASSERT_EQUAL_UINT(n, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
    TEST_ASSERT_EQUAL_HEX8(0, r[7]);
    n = make_query(q, 1, 1); /* an OPT record announced after the question */
    static const uint8_t opt[] = { 0, 0, 41, 0x10, 0, 0, 0, 0, 0, 0, 0 };
    memcpy(q + n, opt, sizeof(opt));
    size_t m = netmgr_dns_reply(q, n + sizeof(opt), k_ip, r, sizeof(r));
    TEST_ASSERT_EQUAL_UINT(n + 16, m);
    TEST_ASSERT_EQUAL_HEX8(0, r[11]);
}

static void test_malformed_or_foreign_packets_are_dropped(void)
{
    uint8_t q[128], r[256];
    size_t n = make_query(q, 1, 0);
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, 11, k_ip, r, sizeof(r)));    /* shorter than a header */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n - 3, k_ip, r, sizeof(r))); /* the type cut off */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, 20, k_ip, r, sizeof(r)));    /* inside a label */
    q[2] = 0x81; /* a response */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
    q[2] = 0x28; /* opcode 5, an update */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
    q[2] = 0x01;
    q[5] = 2; /* two questions */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
    q[5] = 1;
    q[12] = 0xC0; /* a compression pointer in the question */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
    n = make_query(q, 1, 0);
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, n + 15)); /* no room for the answer */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(NULL, 0, k_ip, r, sizeof(r)));
}

/* Labels whose lengths add up past 255 bytes are refused even if the packet is long enough. */
static void test_an_overlong_name_is_dropped(void)
{
    uint8_t q[600], r[700];
    memset(q, 0, sizeof(q));
    q[5] = 1;
    size_t n = 12;
    for (int i = 0; i < 5; i++) {
        q[n++] = 63;
        memset(q + n, 'a', 63);
        n += 63;
    }
    q[n++] = 0;
    q[n++] = 0;
    q[n++] = 1;
    q[n++] = 0;
    q[n++] = 1;
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_a_query_is_answered_with_the_ap_address);
    RUN_TEST(test_other_types_get_no_records_and_edns_is_not_echoed);
    RUN_TEST(test_malformed_or_foreign_packets_are_dropped);
    RUN_TEST(test_an_overlong_name_is_dropped);
    return UNITY_END();
}
