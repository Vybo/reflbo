#include "util_sha256.h"

#include <string.h>

static const uint32_t k_round[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotr(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void compress(uint32_t h[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 | (uint32_t)block[4 * i + 2] << 8 |
               (uint32_t)block[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k_round[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += k;
}

void util_sha256_init(util_sha256_t *s)
{
    static const uint32_t k_init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, k_init, sizeof(k_init));
    s->bytes = 0;
    s->used = 0;
}

void util_sha256_update(util_sha256_t *s, const void *data, size_t len)
{
    const uint8_t *p = data;
    s->bytes += len;
    while (len > 0) {
        size_t take = 64 - s->used < len ? 64 - s->used : len;
        memcpy(s->block + s->used, p, take);
        s->used += take;
        p += take;
        len -= take;
        if (s->used == 64) {
            compress(s->h, s->block);
            s->used = 0;
        }
    }
}

void util_sha256_final(util_sha256_t *s, uint8_t out[UTIL_SHA256_LEN])
{
    uint64_t bits = s->bytes * 8;
    uint8_t pad = 0x80;
    util_sha256_update(s, &pad, 1);
    pad = 0;
    while (s->used != 56) {
        util_sha256_update(s, &pad, 1);
    }
    uint8_t len[8];
    for (int i = 0; i < 8; i++) {
        len[i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    util_sha256_update(s, len, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(s->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)s->h[i];
    }
}

void util_sha256(const void *data, size_t len, uint8_t out[UTIL_SHA256_LEN])
{
    util_sha256_t s;
    util_sha256_init(&s);
    util_sha256_update(&s, data, len);
    util_sha256_final(&s, out);
}

/* The inner and outer states after the padded key, reused across PBKDF2's iterations. */
typedef struct {
    util_sha256_t inner, outer;
} hmac_t;

static void hmac_init(hmac_t *m, const void *key, size_t key_len)
{
    uint8_t k[64] = { 0 };
    if (key_len > 64) {
        util_sha256(key, key_len, k);
    } else {
        memcpy(k, key, key_len);
    }
    uint8_t pad[64];
    for (int i = 0; i < 64; i++) {
        pad[i] = k[i] ^ 0x36;
    }
    util_sha256_init(&m->inner);
    util_sha256_update(&m->inner, pad, 64);
    for (int i = 0; i < 64; i++) {
        pad[i] = k[i] ^ 0x5c;
    }
    util_sha256_init(&m->outer);
    util_sha256_update(&m->outer, pad, 64);
}

static void hmac_run(const hmac_t *m, const void *data, size_t len, const void *data2, size_t len2,
                     uint8_t out[UTIL_SHA256_LEN])
{
    util_sha256_t s = m->inner;
    util_sha256_update(&s, data, len);
    util_sha256_update(&s, data2, len2);
    uint8_t inner[UTIL_SHA256_LEN];
    util_sha256_final(&s, inner);
    s = m->outer;
    util_sha256_update(&s, inner, sizeof(inner));
    util_sha256_final(&s, out);
}

void util_hmac_sha256(const void *key, size_t key_len, const void *data, size_t len, uint8_t out[UTIL_SHA256_LEN])
{
    hmac_t m;
    hmac_init(&m, key, key_len);
    hmac_run(&m, data, len, NULL, 0, out);
}

void util_pbkdf2_sha256(const void *pass, size_t pass_len, const void *salt, size_t salt_len, uint32_t iterations,
                        uint8_t *out, size_t out_len)
{
    hmac_t m;
    hmac_init(&m, pass, pass_len);
    for (uint32_t block = 1; out_len > 0; block++) {
        uint8_t index[4] = { (uint8_t)(block >> 24), (uint8_t)(block >> 16), (uint8_t)(block >> 8), (uint8_t)block };
        uint8_t u[UTIL_SHA256_LEN], t[UTIL_SHA256_LEN];
        hmac_run(&m, salt, salt_len, index, sizeof(index), u);
        memcpy(t, u, sizeof(t));
        for (uint32_t i = 1; i < iterations; i++) {
            hmac_run(&m, u, sizeof(u), NULL, 0, u);
            for (int j = 0; j < UTIL_SHA256_LEN; j++) {
                t[j] ^= u[j];
            }
        }
        size_t take = out_len < sizeof(t) ? out_len : sizeof(t);
        memcpy(out, t, take);
        out += take;
        out_len -= take;
    }
}

bool util_ct_equal(const void *a, const void *b, size_t len)
{
    const volatile uint8_t *x = a, *y = b;
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= x[i] ^ y[i];
    }
    return diff == 0;
}
