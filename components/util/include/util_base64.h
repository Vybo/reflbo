#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Standard base64 (RFC 4648) with padding and no line breaks. Pure C, host-buildable. */
size_t util_base64_encoded_len(size_t len);
/* Writes the NUL-terminated encoding of data to out. Returns false, writing nothing, when out_size
 * is smaller than util_base64_encoded_len(len) + 1. */
bool util_base64_encode(const void *data, size_t len, char *out, size_t out_size);
