#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "util_sha256.h"

void setUp(void) {}
void tearDown(void) {}

static void check_hex(const char *expected, const uint8_t *bytes, size_t len)
{
    char hex[2 * 64 + 1];
    for (size_t i = 0; i < len; i++) {
        snprintf(hex + 2 * i, 3, "%02x", bytes[i]);
    }
    TEST_ASSERT_EQUAL_STRING(expected, hex);
}

/* FIPS 180-4 examples, checked against Python's hashlib. */
static void test_sha256_of_the_standard_messages(void)
{
    uint8_t out[32];
    util_sha256("abc", 3, out);
    check_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", out, 32);
    util_sha256("", 0, out);
    check_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", out, 32);
    const char *two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    util_sha256(two_blocks, strlen(two_blocks), out);
    check_hex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", out, 32);
    util_sha256_t s; /* a million 'a' in uneven pieces */
    util_sha256_init(&s);
    static uint8_t chunk[997];
    memset(chunk, 'a', sizeof(chunk));
    size_t left = 1000000;
    while (left > 0) {
        size_t n = left < sizeof(chunk) ? left : sizeof(chunk);
        util_sha256_update(&s, chunk, n);
        left -= n;
    }
    util_sha256_final(&s, out);
    check_hex("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", out, 32);
}

/* RFC 4231 test case 2. */
static void test_hmac_sha256_of_rfc_4231(void)
{
    uint8_t out[32];
    const char *data = "what do ya want for nothing?";
    util_hmac_sha256("Jefe", 4, data, strlen(data), out);
    check_hex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", out, 32);
}

/* RFC 7914 §11. */
static void test_pbkdf2_sha256_of_rfc_7914(void)
{
    uint8_t out[64];
    util_pbkdf2_sha256("passwd", 6, "salt", 4, 1, out, sizeof(out));
    check_hex("55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
              "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783",
              out, sizeof(out));
    util_pbkdf2_sha256("Password", 8, "NaCl", 4, 80000, out, sizeof(out));
    check_hex("4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
              "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d",
              out, sizeof(out));
}

static void test_constant_time_equal(void)
{
    TEST_ASSERT_TRUE(util_ct_equal("abcd", "abcd", 4));
    TEST_ASSERT_FALSE(util_ct_equal("abcd", "abce", 4));
    TEST_ASSERT_TRUE(util_ct_equal("", "", 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_of_the_standard_messages);
    RUN_TEST(test_hmac_sha256_of_rfc_4231);
    RUN_TEST(test_pbkdf2_sha256_of_rfc_7914);
    RUN_TEST(test_constant_time_equal);
    return UNITY_END();
}
