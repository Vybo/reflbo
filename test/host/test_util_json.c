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

/* A provider's words cut to fit end between characters, never inside one (UTF-8). */
static void test_text_is_cut_between_characters(void)
{
    char out[8];
    TEST_ASSERT_EQUAL_UINT(7, util_json_text(out, sizeof(out), "abcdefghij"));
    TEST_ASSERT_EQUAL_STRING("abcdefg", out);
    TEST_ASSERT_EQUAL_UINT(7, util_json_text(out, sizeof(out), "to 90\xc2\xb0."));
    TEST_ASSERT_EQUAL_STRING("to 90\xc2\xb0", out);
    TEST_ASSERT_EQUAL_UINT(6, util_json_text(out, sizeof(out), "to -90\xc2\xb0")); /* ° would need 8 */
    TEST_ASSERT_EQUAL_STRING("to -90", out);
    TEST_ASSERT_EQUAL_UINT(5, util_json_text(out, sizeof(out), "abcde\xe2\x82\xac"));
    TEST_ASSERT_EQUAL_UINT(4, util_json_text(out, sizeof(out), "abcd\xf0\x9f\x8c\x9e"));
    TEST_ASSERT_EQUAL_UINT(0, util_json_text(out, sizeof(out), NULL));
    TEST_ASSERT_EQUAL_STRING("", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_depth_counts_objects_and_lists);
    RUN_TEST(test_brackets_inside_strings_do_not_count);
    RUN_TEST(test_text_is_cut_between_characters);
    return UNITY_END();
}
