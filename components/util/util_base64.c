#include "util_base64.h"

#include <stdint.h>
#include <string.h>

static const char s_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t util_base64_encoded_len(size_t len)
{
    return (len + 2) / 3 * 4;
}

bool util_base64_encode(const void *data, size_t len, char *out, size_t out_size)
{
    if (out_size < util_base64_encoded_len(len) + 1) {
        return false;
    }
    const uint8_t *in = data;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)in[i] << 16;
        if (i + 1 < len) {
            n |= (uint32_t)in[i + 1] << 8;
        }
        if (i + 2 < len) {
            n |= in[i + 2];
        }
        out[o++] = s_alphabet[(n >> 18) & 0x3F];
        out[o++] = s_alphabet[(n >> 12) & 0x3F];
        out[o++] = i + 1 < len ? s_alphabet[(n >> 6) & 0x3F] : '=';
        out[o++] = i + 2 < len ? s_alphabet[n & 0x3F] : '=';
    }
    out[o] = '\0';
    return true;
}

static int value_of(char c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
           : c == '+'           ? 62
           : c == '/'           ? 63
                                : -1;
}

int util_base64_decode(const char *in, void *out, size_t out_size)
{
    size_t len = strlen(in);
    if (len % 4 != 0) {
        return -1;
    }
    uint8_t *o = out;
    size_t n = 0;
    for (size_t i = 0; i < len; i += 4) {
        bool last = i + 4 == len;
        int pad = last ? (in[i + 3] == '=') + (in[i + 2] == '=') : 0;
        if (last && pad == 1 && in[i + 2] == '=') {
            return -1; /* "x=y=" */
        }
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) {
            int d = k >= 4 - pad ? 0 : value_of(in[i + k]);
            if (d < 0) {
                return -1; /* outside the alphabet, or '=' where it can't be */
            }
            v = v << 6 | (uint32_t)d;
        }
        size_t bytes = 3u - (size_t)pad;
        if (n + bytes > out_size) {
            return -1;
        }
        for (size_t b = 0; b < bytes; b++) {
            o[n++] = (uint8_t)(v >> (16 - 8 * b));
        }
    }
    return (int)n;
}
