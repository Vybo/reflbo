#include "unity.h"
#include "util_json.h"

void setUp(void) {}
void tearDown(void) {}

static void test_depth_counts_objects_and_lists(void)
{
    TEST_ASSERT_EQUAL_INT(0, util_json_depth("42"));
    TEST_ASSERT_EQUAL_INT(1, util_json_depth("{}"));
    TEST_ASSERT_EQUAL_INT(3, util_json_depth("{\"a\": [1, {\"b\": 2}], \"c\": {}}"));
    TEST_ASSERT_EQUAL_INT(0, util_json_depth(NULL));
}

static void test_brackets_inside_strings_do_not_count(void)
{
    TEST_ASSERT_EQUAL_INT(1, util_json_depth("{\"a\": \"[[[{{\"}"));
    TEST_ASSERT_EQUAL_INT(1, util_json_depth("{\"a\": \"quote \\\" [[[\"}"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_depth_counts_objects_and_lists);
    RUN_TEST(test_brackets_inside_strings_do_not_count);
    return UNITY_END();
}
