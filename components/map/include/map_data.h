#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "map_view.h"

/*
 * The built-in map (spec §11.1): borders, coasts, towns and airports, packed by tools/gen_map.py
 * into assets/map/map.bin and read in place. Pure C, host-buildable. The layout, little-endian:
 *   header, 52 bytes: "RMAP", u16 version 1, u16 0, u32 CRC-32 of everything after this word,
 *     then u32 line count, line offset, point offset, point bytes, town count, town offset,
 *     airport count, airport offset, name offset, name bytes; every section on a word
 *   line, 16 bytes: i16 lat_min, lat_max, lon_min, lon_max (0.01°, rounded outwards), u32 point
 *     pos, u16 point count, u8 kind (map_line_kind_t), u8 0. Its points: the first as i32 lat,
 *     lon (1e-4°), the rest as i16 deltas; a delta pair (-32768, -32768) is followed by an
 *     absolute point
 *   town, 16 bytes: i32 lat, lon (1e-4°), u32 population, u32 name pos; the largest first
 *   airport, 16 bytes: i32 lat, lon (1e-4°), 3 bytes IATA, 4 bytes ICAO (NUL-padded), u8 kind
 *     (0 large, 1 medium); large first
 *   names: NUL-terminated UTF-8
 */

#define MAP_DATA_VERSION 1

typedef enum {
    MAP_LINE_BORDER,
    MAP_LINE_COAST,
} map_line_kind_t;

typedef struct {
    const uint8_t *blob;
    size_t len;
    uint32_t lines, line_off, point_off, point_bytes;
    uint32_t towns, town_off, airports, airport_off, name_off, name_bytes;
} map_data_t;

typedef struct {
    double lat, lon;
    uint32_t population;
    const char *name; /* in the blob */
} map_town_t;

typedef struct {
    double lat, lon;
    char iata[4]; /* "" where it has none */
    char icao[5];
    bool large;
} map_airport_t;

/* False if `blob` isn't a map: the wrong magic or version, or a section outside it. It doesn't read
 * the whole blob: the device's copy is part of its app image, which the bootloader and OTA check. */
bool map_data_open(map_data_t *d, const uint8_t *blob, size_t len);
/* The blob's CRC-32: for a copy that came from elsewhere, such as the file the host tests read. */
bool map_data_verify(const map_data_t *d);

typedef void (*map_segment_fn)(void *ctx, map_line_kind_t kind, double lat0, double lon0, double lat1, double lon1);
/* Every segment whose box meets `b`. */
void map_data_segments(const map_data_t *d, const map_bounds_t *b, map_segment_fn fn, void *ctx);

/* The towns inside `b`, the largest first, until `fn` returns false. */
typedef bool (*map_town_fn)(void *ctx, const map_town_t *t);
void map_data_towns(const map_data_t *d, const map_bounds_t *b, map_town_fn fn, void *ctx);

/* The airports inside `b`, large first, until `fn` returns false. */
typedef bool (*map_airport_fn)(void *ctx, const map_airport_t *a);
void map_data_airports(const map_data_t *d, const map_bounds_t *b, map_airport_fn fn, void *ctx);
