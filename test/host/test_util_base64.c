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

/* Decoding, for the battery learning session kept in LittleFS (D21). */
static void test_decoding_undoes_encoding(void)
{
    const char *const k_in[] = { "", "f", "fo", "foo", "foob", "fooba", "foobar" };
    for (size_t i = 0; i < sizeof(k_in) / sizeof(k_in[0]); i++) {
        char enc[16];
        uint8_t dec[8];
        TEST_ASSERT_TRUE(util_base64_encode(k_in[i], strlen(k_in[i]), enc, sizeof(enc)));
        TEST_ASSERT_EQUAL_INT((int)strlen(k_in[i]), util_base64_decode(enc, dec, sizeof(dec)));
        if (k_in[i][0] != '\0') { /* Unity won't compare zero bytes */
            TEST_ASSERT_EQUAL_MEMORY(k_in[i], dec, strlen(k_in[i]));
        }
    }
    uint8_t out[4];
    TEST_ASSERT_EQUAL_INT(3, util_base64_decode("AP8Q", out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]);
}

static void test_bad_base64_is_refused(void)
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL_INT(-1, util_base64_decode("Zm9", out, sizeof(out)));   /* not a multiple of 4 */
    TEST_ASSERT_EQUAL_INT(-1, util_base64_decode("Zm9*", out, sizeof(out)));  /* not the alphabet */
    TEST_ASSERT_EQUAL_INT(-1, util_base64_decode("Z===", out, sizeof(out)));  /* too much padding */
    TEST_ASSERT_EQUAL_INT(-1, util_base64_decode("Zm9vYmFy", out, 5));        /* no room */
    TEST_ASSERT_EQUAL_INT(-1, util_base64_decode("Zg==Zg==", out, sizeof(out))); /* padding inside */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc4648_vectors);
    RUN_TEST(test_binary_bytes);
    RUN_TEST(test_encoded_length);
    RUN_TEST(test_decoding_undoes_encoding);
    RUN_TEST(test_bad_base64_is_refused);
    RUN_TEST(test_too_small_buffer_writes_nothing);
    return UNITY_END();
}
