#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * A small PNG reader for the radar images (spec §11.2): 8-bit palette and 8-bit RGBA, not
 * interlaced, handed over row by row. Pure C, host-buildable: it inflates through png_inflate()
 * (png_inflate.h), which the device takes from the ROM's tinfl and the host tests from zlib.
 */

#define PNG_MAX_SIDE 1024          /* pixels, either way */
#define PNG_MAX_BYTES (256 * 1024) /* the whole file */

typedef enum {
    PNG_OK,
    PNG_ERR_FORMAT,      /* not a PNG, a chunk cut short or out of place, a bad CRC or filter */
    PNG_ERR_UNSUPPORTED, /* interlaced, or not 8-bit palette or 8-bit RGBA */
    PNG_ERR_TOO_BIG,     /* over PNG_MAX_SIDE a side, or PNG_MAX_BYTES in all */
    PNG_ERR_NO_MEMORY,
    PNG_ERR_INFLATE,     /* the image data don't inflate to the image's size */
} png_err_t;

typedef enum {
    PNG_PALETTE = 3, /* the PNG colour types */
    PNG_RGBA = 6,
} png_type_t;

typedef struct {
    uint16_t width, height;
    uint8_t type;            /* png_type_t */
    uint16_t palette_size;   /* PNG_PALETTE: entries */
    uint8_t palette[256][4]; /* PNG_PALETTE: RGBA, the alpha from tRNS, 255 without */
} png_info_t;

/* One decoded row, top first: `width` palette indices, or 4 x `width` bytes of RGBA. */
typedef void (*png_row_fn)(void *ctx, int y, const uint8_t *row, const png_info_t *info);

/* Where the image data are inflated (height x (1 + width x 4) bytes at most): NULL for malloc. */
typedef struct {
    void *(*alloc)(size_t size);
    void (*release)(void *p);
} png_mem_t;

png_err_t png_decode(const uint8_t *data, size_t len, const png_mem_t *mem, png_info_t *info, png_row_fn row,
                     void *ctx);
const char *png_err_name(png_err_t err);
