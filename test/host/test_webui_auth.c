#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "webui_auth.h"

static webui_sessions_t s_t;
static const uint8_t k_salt[WEBUI_AUTH_SALT_LEN] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

void setUp(void)
{
    webui_sessions_clear(&s_t);
}

void tearDown(void) {}

static void test_passwords_are_8_to_64_bytes_without_control_characters(void)
{
    TEST_ASSERT_TRUE(webui_password_valid("12345678"));
    TEST_ASSERT_TRUE(webui_password_valid("heslo s háčky"));
    TEST_ASSERT_FALSE(webui_password_valid("1234567"));
    TEST_ASSERT_FALSE(webui_password_valid("tab\tinside"));
    TEST_ASSERT_FALSE(webui_password_valid(NULL));
    char long_one[66];
    memset(long_one, 'x', 65);
    long_one[65] = '\0';
    TEST_ASSERT_FALSE(webui_password_valid(long_one));
    long_one[64] = '\0';
    TEST_ASSERT_TRUE(webui_password_valid(long_one));
}

/* The record holds the salt and PBKDF2-HMAC-SHA256 (checked against Python's hashlib). */
static void test_a_record_is_salted_pbkdf2_and_checks_the_password(void)
{
    char record[WEBUI_AUTH_RECORD_LEN];
    TEST_ASSERT_TRUE(webui_auth_record("correct horse", k_salt, 3, record, sizeof(record)));
    TEST_ASSERT_EQUAL_STRING("pbkdf2-sha256$3$000102030405060708090a0b0c0d0e0f$"
                             "a788b6bff5da2b8d9a4a5f41df62a5cc17d939de95f5e7a677d654c0edbeac73",
                             record);
    TEST_ASSERT_TRUE(webui_auth_check(record, "correct horse"));
    TEST_ASSERT_FALSE(webui_auth_check(record, "correct horsE"));
    TEST_ASSERT_FALSE(webui_auth_check(record, ""));
    TEST_ASSERT_FALSE(webui_auth_record("short", k_salt, 3, record, sizeof(record)));
    TEST_ASSERT_TRUE(webui_auth_record("correct horse", k_salt, WEBUI_AUTH_ITERATIONS, record, sizeof(record)));
    TEST_ASSERT_TRUE(webui_auth_check(record, "correct horse"));
}

static void test_a_malformed_record_matches_nothing(void)
{
    const char *bad[] = {
        "",
        "sha1$3$00$00",
        "pbkdf2-sha256$x$000102030405060708090a0b0c0d0e0f$a788",
        "pbkdf2-sha256$3$0001$a788b6bff5da2b8d9a4a5f41df62a5cc17d939de95f5e7a677d654c0edbeac73",
        "pbkdf2-sha256$3$000102030405060708090a0b0c0d0e0f$a788b6",
        "pbkdf2-sha256$3$000102030405060708090a0b0c0d0e0f",
        ("pbkdf2-sha256$0$000102030405060708090a0b0c0d0e0f$"
         "a788b6bff5da2b8d9a4a5f41df62a5cc17d939de95f5e7a677d654c0edbeac73"),
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(webui_auth_check(bad[i], "correct horse"), bad[i]);
    }
    TEST_ASSERT_FALSE(webui_auth_check(NULL, "correct horse"));
}

static void test_sessions_are_random_tokens_and_the_oldest_makes_room(void)
{
    uint8_t random[16];
    char first[WEBUI_TOKEN_LEN + 1];
    for (int i = 0; i < WEBUI_SESSIONS; i++) {
        memset(random, i + 1, sizeof(random));
        const char *token = webui_session_new(&s_t, random, 100 + i);
        TEST_ASSERT_EQUAL_UINT(WEBUI_TOKEN_LEN, strlen(token));
        if (i == 0) {
            strcpy(first, token);
        }
    }
    TEST_ASSERT_EQUAL_STRING("01010101010101010101010101010101", first);
    TEST_ASSERT_TRUE(webui_session_check(&s_t, first, 200)); /* used again: now the newest */
    memset(random, 9, sizeof(random));
    webui_session_new(&s_t, random, 300); /* evicts the one made at 101 */
    TEST_ASSERT_TRUE(webui_session_check(&s_t, first, 301));
    TEST_ASSERT_FALSE(webui_session_check(&s_t, "02020202020202020202020202020202", 302));
    TEST_ASSERT_FALSE(webui_session_check(&s_t, "0101", 302));
    webui_sessions_clear(&s_t); /* config mode ended */
    TEST_ASSERT_FALSE(webui_session_check(&s_t, first, 303));
}

static void test_the_cookie_header_yields_the_token(void)
{
    char token[WEBUI_TOKEN_LEN + 1];
    TEST_ASSERT_TRUE(webui_cookie_token("theme=dark; session=0123456789abcdef0123456789abcdef; x=1", token));
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", token);
    TEST_ASSERT_TRUE(webui_cookie_token("session=0123456789abcdef0123456789abcdef", token));
    TEST_ASSERT_FALSE(webui_cookie_token("session=0123", token));
    TEST_ASSERT_FALSE(webui_cookie_token("mysession=0123456789abcdef0123456789abcdef", token));
    TEST_ASSERT_FALSE(webui_cookie_token(NULL, token));
}

static void test_five_failed_logins_make_the_next_wait_a_minute(void)
{
    for (int i = 0; i < WEBUI_FAILS_BEFORE_WAIT - 1; i++) {
        webui_login_result(&s_t, false, 10);
        TEST_ASSERT_TRUE(webui_login_allowed(&s_t, 10));
    }
    webui_login_result(&s_t, false, 10);
    TEST_ASSERT_FALSE(webui_login_allowed(&s_t, 69));
    TEST_ASSERT_TRUE(webui_login_allowed(&s_t, 70));
    webui_login_result(&s_t, false, 70);
    webui_login_result(&s_t, true, 71); /* a success starts the count over */
    for (int i = 0; i < WEBUI_FAILS_BEFORE_WAIT - 1; i++) {
        webui_login_result(&s_t, false, 72);
    }
    TEST_ASSERT_TRUE(webui_login_allowed(&s_t, 72));
}

/* Config mode shows the dashboard while any phone is logged in (D20). */
static void test_any_session_tells_whether_someone_is_logged_in(void)
{
    uint8_t random[16] = { 7 };
    TEST_ASSERT_FALSE(webui_sessions_any(&s_t));
    const char *token = webui_session_new(&s_t, random, 100);
    TEST_ASSERT_TRUE(webui_sessions_any(&s_t));
    TEST_ASSERT_TRUE(webui_session_end(&s_t, token)); /* logging out */
    TEST_ASSERT_FALSE(webui_sessions_any(&s_t));
    TEST_ASSERT_FALSE(webui_session_end(&s_t, token));
    webui_session_new(&s_t, random, 101);
    webui_sessions_clear(&s_t);
    TEST_ASSERT_FALSE(webui_sessions_any(&s_t));
}

/* Spec §10.4: on the LAN the web UI stays up, so an idle session ends after an hour. */
static void test_a_session_idle_for_an_hour_ends(void)
{
    webui_sessions_t t;
    webui_sessions_clear(&t);
    static const uint8_t k_random[16] = { 1, 2, 3 };
    char token[WEBUI_TOKEN_LEN + 1];
    snprintf(token, sizeof(token), "%s", webui_session_new(&t, k_random, 1000));
    TEST_ASSERT_TRUE(webui_session_check(&t, token, 1000 + WEBUI_SESSION_IDLE_S)); /* in time: used again */
    TEST_ASSERT_FALSE(webui_sessions_expire(&t, 1000 + 2 * WEBUI_SESSION_IDLE_S));
    TEST_ASSERT_TRUE(webui_sessions_any(&t));
    TEST_ASSERT_TRUE(webui_sessions_expire(&t, 1000 + 2 * WEBUI_SESSION_IDLE_S + 1));
    TEST_ASSERT_FALSE(webui_sessions_any(&t));
    TEST_ASSERT_FALSE(webui_session_check(&t, token, 1000 + 2 * WEBUI_SESSION_IDLE_S + 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_session_idle_for_an_hour_ends);
    RUN_TEST(test_passwords_are_8_to_64_bytes_without_control_characters);
    RUN_TEST(test_a_record_is_salted_pbkdf2_and_checks_the_password);
    RUN_TEST(test_a_malformed_record_matches_nothing);
    RUN_TEST(test_sessions_are_random_tokens_and_the_oldest_makes_room);
    RUN_TEST(test_any_session_tells_whether_someone_is_logged_in);
    RUN_TEST(test_the_cookie_header_yields_the_token);
    RUN_TEST(test_five_failed_logins_make_the_next_wait_a_minute);
    return UNITY_END();
}
