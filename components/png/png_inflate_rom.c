#include "png_inflate.h"

#include "esp_heap_caps.h"
#include "miniz.h"

/* The ESP32-S3 ROM's tinfl (esp_rom/include/miniz.h), into one buffer that holds the whole image, so
 * it needs no dictionary of its own. Its state is some 11 KB: in PSRAM, not on the caller's stack. */
bool png_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    tinfl_decompressor *d = heap_caps_malloc(sizeof(*d), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (d == NULL) {
        return false;
    }
    tinfl_init(d);
    size_t in_size = in_len, out_size = out_len;
    tinfl_status st = tinfl_decompress(d, in, &in_size, out, out, &out_size,
                                       TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    heap_caps_free(d);
    return st == TINFL_STATUS_DONE && out_size == out_len;
}
