#include <string.h>

#include "sync_ntp.h"
#include "unity.h"

/* SNTP packets (RFC 4330) for the sync's time step (spec §7). */

#define T1 1790859600123456LL /* 2026-10-01 13:00:00.123456 UTC by the local clock */

void setUp(void) {}
void tearDown(void) {}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (56 - 8 * i));
    }
}

/* NTP timestamp of a UTC time in µs, the long way round, for the expected values. */
static uint64_t ntp(int64_t utc_us)
{
    uint64_t s = (uint64_t)(utc_us / 1000000 + 2208988800LL);
    uint64_t frac = (uint64_t)(utc_us % 1000000) * 4294967296ULL / 1000000;
    return (s & 0xFFFFFFFFu) << 32 | frac;
}

/* A server's answer to the request made at T1: it got it at t2 and answered at t3 (true time). */
static void reply(uint8_t r[SYNC_NTP_PACKET], int64_t t2, int64_t t3)
{
    uint8_t req[SYNC_NTP_PACKET];
    sync_ntp_request(req, T1);
    memset(r, 0, SYNC_NTP_PACKET);
    r[0] = 0x24; /* leap 0, version 4, mode 4 (server) */
    r[1] = 2;    /* stratum */
    memcpy(r + 24, req + 40, 8); /* originate: our transmit time */
    put64(r + 32, ntp(t2));
    put64(r + 40, ntp(t3));
}

static void test_a_request_is_a_v4_client_packet_carrying_its_send_time(void)
{
    uint8_t p[SYNC_NTP_PACKET];
    sync_ntp_request(p, T1);
    TEST_ASSERT_EQUAL_HEX8(0x23, p[0]);
    uint64_t sent = 0;
    for (int i = 40; i < 48; i++) {
        sent = sent << 8 | p[i];
    }
    TEST_ASSERT_EQUAL_HEX64(ntp(T1), sent);
}

static void test_the_offset_and_delay_follow_rfc_4330(void)
{
    /* the local clock is 3.4 s slow; 20 ms each way; the server takes 1 ms */
    int64_t slow = 3400000, way = 20000;
    int64_t t2 = T1 + slow + way, t3 = t2 + 1000, t4 = T1 + 2 * way + 1000;
    uint8_t r[SYNC_NTP_PACKET];
    reply(r, t2, t3);
    int64_t offset, delay;
    TEST_ASSERT_TRUE(sync_ntp_offset(r, sizeof(r), T1, t4, &offset, &delay));
    TEST_ASSERT_INT64_WITHIN(2, slow, offset);
    TEST_ASSERT_INT64_WITHIN(2, 2 * way, delay);
}

static void test_a_reply_to_another_request_is_refused(void)
{
    uint8_t r[SYNC_NTP_PACKET];
    reply(r, T1 + 10, T1 + 20);
    int64_t offset, delay;
    TEST_ASSERT_FALSE(sync_ntp_offset(r, sizeof(r), T1 + 1, T1 + 30, &offset, &delay)); /* not our t1 */
    TEST_ASSERT_FALSE(sync_ntp_offset(r, SYNC_NTP_PACKET - 1, T1, T1 + 30, &offset, &delay));
}

static void test_unsynchronised_or_unusable_servers_are_refused(void)
{
    uint8_t r[SYNC_NTP_PACKET];
    int64_t offset, delay;
    reply(r, T1 + 10, T1 + 20);
    r[0] = 0xE4; /* leap 3: the server's clock is unsynchronised */
    TEST_ASSERT_FALSE(sync_ntp_offset(r, sizeof(r), T1, T1 + 30, &offset, &delay));
    reply(r, T1 + 10, T1 + 20);
    r[1] = 0; /* stratum 0: a kiss-o'-death */
    TEST_ASSERT_FALSE(sync_ntp_offset(r, sizeof(r), T1, T1 + 30, &offset, &delay));
    reply(r, T1 + 10, T1 + 20);
    r[1] = 16;
    TEST_ASSERT_FALSE(sync_ntp_offset(r, sizeof(r), T1, T1 + 30, &offset, &delay));
    reply(r, T1 + 10, T1 + 20);
    r[0] = 0x23; /* mode 3: a client, not a server */
    TEST_ASSERT_FALSE(sync_ntp_offset(r, sizeof(r), T1, T1 + 30, &offset, &delay));
    reply(r, T1 + 10, T1 + 20);
    memset(r + 40, 0, 8); /* no transmit time */
    TEST_ASSERT_FALSE(sync_ntp_offset(r, sizeof(r), T1, T1 + 30, &offset, &delay));
}

static void test_a_lost_clock_far_from_the_truth_still_gets_it(void)
{
    /* without the backup cell the clock starts in 2000 (D9); the answer is in 2026 */
    int64_t lost = 946684800000000LL; /* 2000-01-01 */
    int64_t truth = T1;
    uint8_t req[SYNC_NTP_PACKET], r[SYNC_NTP_PACKET];
    sync_ntp_request(req, lost);
    memset(r, 0, sizeof(r));
    r[0] = 0x24;
    r[1] = 1;
    memcpy(r + 24, req + 40, 8);
    put64(r + 32, ntp(truth));
    put64(r + 40, ntp(truth + 500));
    int64_t offset, delay;
    TEST_ASSERT_TRUE(sync_ntp_offset(r, sizeof(r), lost, lost + 30000, &offset, &delay));
    TEST_ASSERT_INT64_WITHIN(20000, truth - lost, offset);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_request_is_a_v4_client_packet_carrying_its_send_time);
    RUN_TEST(test_the_offset_and_delay_follow_rfc_4330);
    RUN_TEST(test_a_reply_to_another_request_is_refused);
    RUN_TEST(test_unsynchronised_or_unusable_servers_are_refused);
    RUN_TEST(test_a_lost_clock_far_from_the_truth_still_gets_it);
    return UNITY_END();
}
