#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The web UI password and sessions (spec §10.4, D18). Pure C, host-buildable; webui.c keeps the
 * record in NVS `secrets` and draws the random bytes from the hardware RNG.
 */

#define WEBUI_PASSWORD_MIN     8
#define WEBUI_PASSWORD_MAX     64
#define WEBUI_AUTH_ITERATIONS  10000 /* about 2 s per check on the ESP32-S3 (measured at M4) */
#define WEBUI_AUTH_SALT_LEN    16
#define WEBUI_AUTH_RECORD_LEN  128   /* "pbkdf2-sha256$10000$<32 hex>$<64 hex>" and its terminator */
#define WEBUI_TOKEN_LEN        32    /* hex digits of 16 random bytes */
#define WEBUI_SESSIONS         4
#define WEBUI_FAILS_BEFORE_WAIT 5
#define WEBUI_FAIL_WAIT_S      60

/* 8-64 bytes, none of them control characters. */
bool webui_password_valid(const char *password);
/* The stored form of a password: "pbkdf2-sha256$<iterations>$<salt>$<hash>", in hex. */
bool webui_auth_record(const char *password, const uint8_t salt[WEBUI_AUTH_SALT_LEN], uint32_t iterations,
                       char *out, size_t size);
/* True if `password` matches the record; false for a malformed record too. */
bool webui_auth_check(const char *record, const char *password);

typedef struct {
    char token[WEBUI_TOKEN_LEN + 1]; /* empty: a free slot */
    int64_t used_s;
} webui_session_t;

typedef struct {
    webui_session_t s[WEBUI_SESSIONS];
    uint8_t fails;      /* failed logins in a row */
    int64_t wait_until; /* no login is checked before this (monotonic seconds) */
} webui_sessions_t;

void webui_sessions_clear(webui_sessions_t *t);
/* A new session from 16 random bytes; the least recently used one makes room. Returns the token. */
const char *webui_session_new(webui_sessions_t *t, const uint8_t random[16], int64_t now_s);
/* True if `token` is a live session; it counts as used now. */
bool webui_session_check(webui_sessions_t *t, const char *token, int64_t now_s);
/* Logging out: ends the session `token`; false if there was none. */
bool webui_session_end(webui_sessions_t *t, const char *token);
/* Spec §10.4: a session idle this long ends, as the web UI may stay up on the LAN in sync mode `always`. */
#define WEBUI_SESSION_IDLE_S 3600
/* Ends the sessions idle longer than WEBUI_SESSION_IDLE_S; true if one ended. */
bool webui_sessions_expire(webui_sessions_t *t, int64_t now_s);
/* Whether anyone is logged in: config mode shows the dashboard meanwhile (D20). */
bool webui_sessions_any(const webui_sessions_t *t);
/* The session token in a Cookie header ("...; session=<token>; ..."), or false. */
bool webui_cookie_token(const char *cookie, char out[WEBUI_TOKEN_LEN + 1]);
/* Login throttling: false while waiting after WEBUI_FAILS_BEFORE_WAIT failures in a row. */
bool webui_login_allowed(const webui_sessions_t *t, int64_t now_s);
void webui_login_result(webui_sessions_t *t, bool ok, int64_t now_s);
