#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Inflates the zlib stream in[0, in_len) into exactly out_len bytes; false if it fails, comes out
 * short, or has more. png_inflate_rom.c (the ROM's tinfl) on the device, zlib on the host. */
bool png_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len);
