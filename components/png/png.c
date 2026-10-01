#include "png.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "png_inflate.h"
#include "util_crc32.h"

static const uint8_t k_signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

typedef struct {
    const uint8_t *type; /* 4 bytes */
    const uint8_t *data;
    uint32_t len;
} chunk_t;

/* The chunk at *pos with its CRC checked, and *pos past it; false if it is cut short or damaged. */
static bool next_chunk(const uint8_t *data, size_t len, size_t *pos, chunk_t *c)
{
    if (len - *pos < 12) {
        return false;
    }
    uint32_t n = be32(data + *pos);
    if (n > len - *pos - 12) {
        return false;
    }
    c->type = data + *pos + 4;
    c->data = data + *pos + 8;
    c->len = n;
    if (util_crc32(util_crc32(0, c->type, 4), c->data, n) != be32(c->data + n)) {
        return false;
    }
    *pos += 12 + (size_t)n;
    return true;
}

static bool is(const chunk_t *c, const char *type)
{
    return memcmp(c->type, type, 4) == 0;
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* Undoes a row's filter in place; `prev` is the row above, already undone, or NULL for the first. */
static bool unfilter(uint8_t *row, const uint8_t *prev, size_t n, size_t bpp, uint8_t filter)
{
    for (size_t i = 0; i < n; i++) {
        int a = i >= bpp ? row[i - bpp] : 0;
        int b = prev != NULL ? prev[i] : 0;
        int c = prev != NULL && i >= bpp ? prev[i - bpp] : 0;
        switch (filter) {
        case 0: /* None */
            return true;
        case 1: /* Sub */
            row[i] = (uint8_t)(row[i] + a);
            break;
        case 2: /* Up */
            row[i] = (uint8_t)(row[i] + b);
            break;
        case 3: /* Average */
            row[i] = (uint8_t)(row[i] + (a + b) / 2);
            break;
        case 4: /* Paeth */
            row[i] = (uint8_t)(row[i] + paeth(a, b, c));
            break;
        default:
            return false;
        }
    }
    return filter <= 4;
}

/* The first pass: every chunk in order and undamaged, the header supported, the image data counted. */
static png_err_t check(const uint8_t *data, size_t len, png_info_t *info, size_t *idat_len)
{
    size_t pos = 8;
    bool header = false;
    *idat_len = 0;
    for (;;) {
        chunk_t c;
        if (!next_chunk(data, len, &pos, &c)) {
            return PNG_ERR_FORMAT;
        }
        if (!header) {
            if (!is(&c, "IHDR") || c.len != 13) {
                return PNG_ERR_FORMAT;
            }
            uint32_t w = be32(c.data), h = be32(c.data + 4);
            if (w == 0 || h == 0 || c.data[10] != 0 || c.data[11] != 0) { /* compression and filter method 0 */
                return PNG_ERR_FORMAT;
            }
            if (c.data[8] != 8 || (c.data[9] != PNG_PALETTE && c.data[9] != PNG_RGBA) || c.data[12] != 0) {
                return PNG_ERR_UNSUPPORTED;
            }
            if (w > PNG_MAX_SIDE || h > PNG_MAX_SIDE) {
                return PNG_ERR_TOO_BIG;
            }
            info->width = (uint16_t)w;
            info->height = (uint16_t)h;
            info->type = c.data[9];
            header = true;
        } else if (is(&c, "PLTE")) {
            if (c.len % 3 != 0 || c.len > 3 * 256 || *idat_len > 0) {
                return PNG_ERR_FORMAT;
            }
            info->palette_size = (uint16_t)(c.len / 3);
            for (int i = 0; i < info->palette_size; i++) {
                memcpy(info->palette[i], c.data + 3 * i, 3);
                info->palette[i][3] = 255;
            }
        } else if (is(&c, "tRNS")) {
            if (info->type == PNG_PALETTE) {
                if (c.len > info->palette_size) {
                    return PNG_ERR_FORMAT;
                }
                for (uint32_t i = 0; i < c.len; i++) {
                    info->palette[i][3] = c.data[i];
                }
            }
        } else if (is(&c, "IDAT")) {
            *idat_len += c.len;
        } else if (is(&c, "IEND")) {
            break;
        } else if (!(c.type[0] & 0x20)) {
            return PNG_ERR_UNSUPPORTED; /* a critical chunk this reader doesn't know */
        }
    }
    if (*idat_len == 0 || (info->type == PNG_PALETTE && info->palette_size == 0)) {
        return PNG_ERR_FORMAT;
    }
    return PNG_OK;
}

png_err_t png_decode(const uint8_t *data, size_t len, const png_mem_t *mem, png_info_t *info, png_row_fn row,
                     void *ctx)
{
    if (len > PNG_MAX_BYTES) {
        return PNG_ERR_TOO_BIG;
    }
    if (len < sizeof(k_signature) || memcmp(data, k_signature, sizeof(k_signature)) != 0) {
        return PNG_ERR_FORMAT;
    }
    memset(info, 0, sizeof(*info));
    size_t idat_len;
    png_err_t err = check(data, len, info, &idat_len);
    if (err != PNG_OK) {
        return err;
    }
    void *(*alloc)(size_t) = mem != NULL && mem->alloc != NULL ? mem->alloc : malloc;
    void (*release)(void *) = mem != NULL && mem->release != NULL ? mem->release : free;
    size_t bpp = info->type == PNG_RGBA ? 4 : 1;
    size_t stride = 1 + info->width * bpp, raw_len = stride * info->height;
    uint8_t *z = alloc(idat_len);
    if (z == NULL) {
        return PNG_ERR_NO_MEMORY;
    }
    uint8_t *raw = alloc(raw_len);
    if (raw == NULL) {
        release(z);
        return PNG_ERR_NO_MEMORY;
    }
    size_t pos = 8, at = 0;
    chunk_t c;
    do { /* the second pass: the image data in one piece; check() made sure every chunk reads */
        next_chunk(data, len, &pos, &c);
        if (is(&c, "IDAT")) {
            memcpy(z + at, c.data, c.len);
            at += c.len;
        }
    } while (!is(&c, "IEND"));
    bool inflated = png_inflate(z, idat_len, raw, raw_len);
    release(z);
    err = inflated ? PNG_OK : PNG_ERR_INFLATE;
    for (int y = 0; err == PNG_OK && y < info->height; y++) {
        uint8_t *line = raw + (size_t)y * stride;
        const uint8_t *prev = y > 0 ? line - stride + 1 : NULL;
        if (!unfilter(line + 1, prev, stride - 1, bpp, line[0])) {
            err = PNG_ERR_FORMAT;
        } else {
            row(ctx, y, line + 1, info);
        }
    }
    release(raw);
    return err;
}

const char *png_err_name(png_err_t err)
{
    switch (err) {
    case PNG_OK: return "ok";
    case PNG_ERR_FORMAT: return "not a PNG, or damaged";
    case PNG_ERR_UNSUPPORTED: return "unsupported";
    case PNG_ERR_TOO_BIG: return "too big";
    case PNG_ERR_NO_MEMORY: return "no memory";
    case PNG_ERR_INFLATE: return "image data damaged";
    }
    return "?";
}
