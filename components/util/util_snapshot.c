#include "util_snapshot.h"

#include "util_crc32.h"

static uint32_t body_crc(const void *block, size_t size)
{
    const uint8_t *bytes = block;
    return util_crc32(0, bytes + sizeof(util_snapshot_hdr_t), size - sizeof(util_snapshot_hdr_t));
}

void util_snapshot_seal(void *block, size_t size, uint32_t magic, uint16_t version)
{
    util_snapshot_hdr_t *hdr = block;
    hdr->magic = magic;
    hdr->version = version;
    hdr->size = (uint16_t)size;
    hdr->crc = body_crc(block, size);
}

bool util_snapshot_valid(const void *block, size_t size, uint32_t magic, uint16_t version)
{
    const util_snapshot_hdr_t *hdr = block;
    if (size < sizeof(*hdr) || size > UINT16_MAX) {
        return false;
    }
    return hdr->magic == magic && hdr->version == version && hdr->size == size && hdr->crc == body_crc(block, size);
}
