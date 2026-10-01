#define ZLIB_CONST
#include <zlib.h>

#include "png_inflate.h"

/* The host tests inflate through zlib; the device has the ROM's tinfl (components/png/png_inflate_rom.c). */
bool png_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    z_stream s = { 0 };
    if (inflateInit(&s) != Z_OK) {
        return false;
    }
    s.next_in = in;
    s.avail_in = (uInt)in_len;
    s.next_out = out;
    s.avail_out = (uInt)out_len;
    int r = inflate(&s, Z_FINISH);
    bool ok = r == Z_STREAM_END && s.total_out == out_len;
    inflateEnd(&s);
    return ok;
}
