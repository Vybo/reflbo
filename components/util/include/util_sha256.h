#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 2104) and PBKDF2-HMAC-SHA256 (RFC 8018), for the web UI
 * password's salted hash (spec §10.4). Pure C, host-buildable. */

#define UTIL_SHA256_LEN 32

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} util_sha256_t;

void util_sha256_init(util_sha256_t *s);
void util_sha256_update(util_sha256_t *s, const void *data, size_t len);
void util_sha256_final(util_sha256_t *s, uint8_t out[UTIL_SHA256_LEN]);
void util_sha256(const void *data, size_t len, uint8_t out[UTIL_SHA256_LEN]);
void util_hmac_sha256(const void *key, size_t key_len, const void *data, size_t len, uint8_t out[UTIL_SHA256_LEN]);
void util_pbkdf2_sha256(const void *pass, size_t pass_len, const void *salt, size_t salt_len, uint32_t iterations,
                        uint8_t *out, size_t out_len);
/* Compares in time that doesn't depend on where the bytes differ. */
bool util_ct_equal(const void *a, const void *b, size_t len);
