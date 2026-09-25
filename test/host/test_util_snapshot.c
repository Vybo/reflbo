#include <string.h>

#include "unity.h"
#include "util_snapshot.h"

typedef struct {
    util_snapshot_hdr_t hdr;
    uint32_t counter;
    char note[12];
} demo_t;

#define MAGIC 0x72666c62u /* "rflb" */

static demo_t s_demo;

void setUp(void)
{
    memset(&s_demo, 0, sizeof(s_demo));
    s_demo.counter = 42;
    strcpy(s_demo.note, "hello");
    util_snapshot_seal(&s_demo, sizeof(s_demo), MAGIC, 3);
}

void tearDown(void) {}

static void test_sealed_block_is_valid(void)
{
    TEST_ASSERT_TRUE(util_snapshot_valid(&s_demo, sizeof(s_demo), MAGIC, 3));
}

static void test_zeroed_memory_is_invalid(void)
{
    demo_t zero;
    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_FALSE(util_snapshot_valid(&zero, sizeof(zero), MAGIC, 3));
}

static void test_a_changed_byte_is_detected(void)
{
    s_demo.note[0] = 'j';
    TEST_ASSERT_FALSE(util_snapshot_valid(&s_demo, sizeof(s_demo), MAGIC, 3));
}

static void test_other_magic_version_or_size_is_rejected(void)
{
    TEST_ASSERT_FALSE(util_snapshot_valid(&s_demo, sizeof(s_demo), MAGIC + 1, 3));
    TEST_ASSERT_FALSE(util_snapshot_valid(&s_demo, sizeof(s_demo), MAGIC, 4));
    TEST_ASSERT_FALSE(util_snapshot_valid(&s_demo, sizeof(s_demo) - 4, MAGIC, 3));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sealed_block_is_valid);
    RUN_TEST(test_zeroed_memory_is_invalid);
    RUN_TEST(test_a_changed_byte_is_detected);
    RUN_TEST(test_other_magic_version_or_size_is_rejected);
    return UNITY_END();
}
