#include "util_base64.h"

#include <stdint.h>

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
