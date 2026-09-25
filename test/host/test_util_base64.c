#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "util_base64.h"

void setUp(void) {}
void tearDown(void) {}

static void check(const char *in, const char *expected)
{
    char out[16];
    TEST_ASSERT_TRUE(util_base64_encode(in, strlen(in), out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(expected, out);
}

static void test_rfc4648_vectors(void)
{
    check("", "");
    check("f", "Zg==");
    check("fo", "Zm8=");
    check("foo", "Zm9v");
    check("foob", "Zm9vYg==");
    check("fooba", "Zm9vYmE=");
    check("foobar", "Zm9vYmFy");
}

static void test_binary_bytes(void)
{
    const uint8_t in[] = { 0x00, 0xFF, 0x10 };
    char out[8];
    TEST_ASSERT_TRUE(util_base64_encode(in, sizeof(in), out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("AP8Q", out);
}

static void test_encoded_length(void)
{
    TEST_ASSERT_EQUAL_INT(0, (int)util_base64_encoded_len(0));
    TEST_ASSERT_EQUAL_INT(4, (int)util_base64_encoded_len(1));
    TEST_ASSERT_EQUAL_INT(4, (int)util_base64_encoded_len(3));
    TEST_ASSERT_EQUAL_INT(8, (int)util_base64_encoded_len(4));
    TEST_ASSERT_EQUAL_INT(76, (int)util_base64_encoded_len(57));
}

static void test_too_small_buffer_writes_nothing(void)
{
    char out[5] = "abcd";
    TEST_ASSERT_FALSE(util_base64_encode("foo", 3, out, 4));
    TEST_ASSERT_EQUAL_STRING("abcd", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc4648_vectors);
    RUN_TEST(test_binary_bytes);
    RUN_TEST(test_encoded_length);
    RUN_TEST(test_too_small_buffer_writes_nothing);
    return UNITY_END();
}
