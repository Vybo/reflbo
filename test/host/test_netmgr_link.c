#include "netmgr_link.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* join() drops the old link first. That link's event arrives later, with our own reason, and must
 * not count as the new network's failure (M4 review). */
static void test_our_own_disconnect_never_fails_a_join(void)
{
    TEST_ASSERT_EQUAL(NETMGR_DISC_IGNORE, netmgr_disconnected(NETMGR_REASON_ASSOC_LEAVE, true, true));
    TEST_ASSERT_EQUAL(NETMGR_DISC_IGNORE, netmgr_disconnected(NETMGR_REASON_ASSOC_LEAVE, true, false));
}

static void test_any_other_reason_fails_the_join(void)
{
    TEST_ASSERT_EQUAL(NETMGR_DISC_FAILED, netmgr_disconnected(201, true, false)); /* no AP found */
    TEST_ASSERT_EQUAL(NETMGR_DISC_FAILED, netmgr_disconnected(15, true, true));   /* 4-way handshake timeout */
}

/* A network that went away is tried again; one we left ourselves is not. */
static void test_a_lost_network_is_tried_again(void)
{
    TEST_ASSERT_EQUAL(NETMGR_DISC_RECONNECT, netmgr_disconnected(200, false, true)); /* beacon timeout */
    TEST_ASSERT_EQUAL(NETMGR_DISC_IGNORE, netmgr_disconnected(NETMGR_REASON_ASSOC_LEAVE, false, true));
    TEST_ASSERT_EQUAL(NETMGR_DISC_IGNORE, netmgr_disconnected(200, false, false)); /* no network joined */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_our_own_disconnect_never_fails_a_join);
    RUN_TEST(test_any_other_reason_fails_the_join);
    RUN_TEST(test_a_lost_network_is_tried_again);
    return UNITY_END();
}
