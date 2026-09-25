#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320), compatible with zlib's crc32().
 * Start with crc = 0 and pass the previous result to continue over more data:
 *   util_crc32(util_crc32(0, a, na), b, nb) == util_crc32(0, a||b, na + nb)
 * Pure C with no ESP-IDF dependency, so it also builds on the host.
 */
uint32_t util_crc32(uint32_t crc, const void *data, size_t len);
