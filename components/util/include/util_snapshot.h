#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Sealed state blocks, e.g. the RTC-RAM snapshot that survives deep sleep (spec §3.3): a header
 * with magic, version, size and a CRC-32 of everything after it. Pure C, host-buildable.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size; /* whole block, header included */
    uint32_t crc;  /* util_crc32 of the bytes after the header */
} util_snapshot_hdr_t;

/* `block` starts with a util_snapshot_hdr_t and is `size` bytes long. */
void util_snapshot_seal(void *block, size_t size, uint32_t magic, uint16_t version);
bool util_snapshot_valid(const void *block, size_t size, uint32_t magic, uint16_t version);
