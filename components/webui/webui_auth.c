#include "webui_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util_sha256.h"

#define PREFIX "pbkdf2-sha256$"

bool webui_password_valid(const char *password)
{
    size_t n = password ? strlen(password) : 0;
    if (n < WEBUI_PASSWORD_MIN || n > WEBUI_PASSWORD_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)password[i];
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
    }
    return true;
}

static void to_hex(const uint8_t *bytes, size_t len, char *out)
{
    static const char k_hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = k_hex[bytes[i] >> 4];
        out[2 * i + 1] = k_hex[bytes[i] & 15];
    }
    out[2 * len] = '\0';
}

static bool from_hex(const char *hex, size_t hex_len, uint8_t *out, size_t len)
{
    if (hex_len != 2 * len) {
        return false;
    }
    for (size_t i = 0; i < 2 * len; i++) {
        char c = hex[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) {
            return false;
        }
        out[i / 2] = (uint8_t)(i % 2 ? (out[i / 2] | v) : v << 4);
    }
    return true;
}

bool webui_auth_record(const char *password, const uint8_t salt[WEBUI_AUTH_SALT_LEN], uint32_t iterations,
                       char *out, size_t size)
{
    if (!webui_password_valid(password) || iterations == 0) {
        return false;
    }
    uint8_t hash[UTIL_SHA256_LEN];
    util_pbkdf2_sha256(password, strlen(password), salt, WEBUI_AUTH_SALT_LEN, iterations, hash, sizeof(hash));
    char salt_hex[2 * WEBUI_AUTH_SALT_LEN + 1], hash_hex[2 * UTIL_SHA256_LEN + 1];
    to_hex(salt, WEBUI_AUTH_SALT_LEN, salt_hex);
    to_hex(hash, sizeof(hash), hash_hex);
    int n = snprintf(out, size, PREFIX "%lu$%s$%s", (unsigned long)iterations, salt_hex, hash_hex);
    return n > 0 && (size_t)n < size;
}

bool webui_auth_check(const char *record, const char *password)
{
    if (record == NULL || password == NULL || strncmp(record, PREFIX, strlen(PREFIX)) != 0) {
        return false;
    }
    const char *p = record + strlen(PREFIX);
    char *end;
    unsigned long iterations = strtoul(p, &end, 10);
    if (end == p || *end != '$' || iterations == 0 || iterations > 1000000) {
        return false;
    }
    const char *salt_hex = end + 1, *dollar = strchr(salt_hex, '$');
    uint8_t salt[WEBUI_AUTH_SALT_LEN], want[UTIL_SHA256_LEN], got[UTIL_SHA256_LEN];
    if (dollar == NULL || !from_hex(salt_hex, (size_t)(dollar - salt_hex), salt, sizeof(salt)) ||
        !from_hex(dollar + 1, strlen(dollar + 1), want, sizeof(want))) {
        return false;
    }
    util_pbkdf2_sha256(password, strlen(password), salt, sizeof(salt), (uint32_t)iterations, got, sizeof(got));
    return util_ct_equal(got, want, sizeof(got));
}

void webui_sessions_clear(webui_sessions_t *t)
{
    memset(t, 0, sizeof(*t));
}

const char *webui_session_new(webui_sessions_t *t, const uint8_t random[16], int64_t now_s)
{
    int slot = 0;
    for (int i = 0; i < WEBUI_SESSIONS; i++) {
        if (t->s[i].token[0] == '\0') {
            slot = i;
            break;
        }
        if (t->s[i].used_s < t->s[slot].used_s) {
            slot = i;
        }
    }
    to_hex(random, 16, t->s[slot].token);
    t->s[slot].used_s = now_s;
    return t->s[slot].token;
}

bool webui_session_check(webui_sessions_t *t, const char *token, int64_t now_s)
{
    if (token == NULL || strlen(token) != WEBUI_TOKEN_LEN) {
        return false;
    }
    bool found = false;
    for (int i = 0; i < WEBUI_SESSIONS; i++) { /* every slot is compared, in the same time */
        if (t->s[i].token[0] != '\0' && util_ct_equal(t->s[i].token, token, WEBUI_TOKEN_LEN)) {
            t->s[i].used_s = now_s;
            found = true;
        }
    }
    return found;
}

bool webui_session_end(webui_sessions_t *t, const char *token)
{
    if (token == NULL || strlen(token) != WEBUI_TOKEN_LEN) {
        return false;
    }
    bool found = false;
    for (int i = 0; i < WEBUI_SESSIONS; i++) {
        if (t->s[i].token[0] != '\0' && util_ct_equal(t->s[i].token, token, WEBUI_TOKEN_LEN)) {
            memset(&t->s[i], 0, sizeof(t->s[i]));
            found = true;
        }
    }
    return found;
}

bool webui_sessions_any(const webui_sessions_t *t)
{
    for (int i = 0; i < WEBUI_SESSIONS; i++) {
        if (t->s[i].token[0] != '\0') {
            return true;
        }
    }
    return false;
}

bool webui_cookie_token(const char *cookie, char out[WEBUI_TOKEN_LEN + 1])
{
    for (const char *p = cookie; p != NULL && *p != '\0';) {
        while (*p == ' ' || *p == ';') {
            p++;
        }
        size_t len = strcspn(p, ";");
        if (len == 8 + WEBUI_TOKEN_LEN && strncmp(p, "session=", 8) == 0) {
            memcpy(out, p + 8, WEBUI_TOKEN_LEN);
            out[WEBUI_TOKEN_LEN] = '\0';
            return true;
        }
        p += len;
    }
    return false;
}

bool webui_login_allowed(const webui_sessions_t *t, int64_t now_s)
{
    return now_s >= t->wait_until;
}

void webui_login_result(webui_sessions_t *t, bool ok, int64_t now_s)
{
    if (ok) {
        t->fails = 0;
        return;
    }
    if (++t->fails >= WEBUI_FAILS_BEFORE_WAIT) {
        t->fails = 0;
        t->wait_until = now_s + WEBUI_FAIL_WAIT_S;
    }
}
