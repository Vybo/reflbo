#include "map_data.h"

#include <string.h>

#include "util_crc32.h"

#define HEADER_LEN 52
#define RECORD_LEN 16
#define ESCAPE (-32768)

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t le32s(const uint8_t *p)
{
    return (int32_t)le32(p);
}

static int16_t le16s(const uint8_t *p)
{
    return (int16_t)(p[0] | p[1] << 8);
}

static bool fits(uint32_t offset, uint64_t size, size_t len)
{
    return offset >= HEADER_LEN && offset <= len && size <= len - offset;
}

bool map_data_open(map_data_t *d, const uint8_t *blob, size_t len)
{
    memset(d, 0, sizeof(*d));
    if (len < HEADER_LEN || memcmp(blob, "RMAP", 4) != 0 || (blob[4] | blob[5] << 8) != MAP_DATA_VERSION) {
        return false;
    }
    map_data_t m = { .blob = blob, .len = len };
    uint32_t *words[] = { &m.lines, &m.line_off, &m.point_off, &m.point_bytes, &m.towns,
                          &m.town_off, &m.airports, &m.airport_off, &m.name_off, &m.name_bytes };
    for (int i = 0; i < 10; i++) {
        *words[i] = le32(blob + 12 + 4 * i);
    }
    if (!fits(m.line_off, (uint64_t)m.lines * RECORD_LEN, len) || !fits(m.point_off, m.point_bytes, len) ||
        !fits(m.town_off, (uint64_t)m.towns * RECORD_LEN, len) ||
        !fits(m.airport_off, (uint64_t)m.airports * RECORD_LEN, len) || !fits(m.name_off, m.name_bytes, len) ||
        (m.name_bytes > 0 && blob[m.name_off + m.name_bytes - 1] != '\0')) {
        return false;
    }
    *d = m;
    return true;
}

bool map_data_verify(const map_data_t *d)
{
    return d->blob != NULL && util_crc32(0, d->blob + 12, d->len - 12) == le32(d->blob + 8);
}

static bool meets(const map_bounds_t *b, double lat0, double lon0, double lat1, double lon1)
{
    double lat_lo = lat0 < lat1 ? lat0 : lat1, lat_hi = lat0 < lat1 ? lat1 : lat0;
    double lon_lo = lon0 < lon1 ? lon0 : lon1, lon_hi = lon0 < lon1 ? lon1 : lon0;
    return lat_hi >= b->lat_min && lat_lo <= b->lat_max && lon_hi >= b->lon_min && lon_lo <= b->lon_max;
}

void map_data_segments(const map_data_t *d, const map_bounds_t *b, map_segment_fn fn, void *ctx)
{
    const uint8_t *points = d->blob + d->point_off;
    for (uint32_t i = 0; i < d->lines; i++) {
        const uint8_t *l = d->blob + d->line_off + (size_t)i * RECORD_LEN;
        if (!meets(b, le16s(l) / 100.0, le16s(l + 4) / 100.0, le16s(l + 2) / 100.0, le16s(l + 6) / 100.0)) {
            continue;
        }
        uint32_t at = le32(l + 8);
        int count = l[12] | l[13] << 8;
        map_line_kind_t kind = (map_line_kind_t)l[14];
        if (at > d->point_bytes || d->point_bytes - at < 8) {
            continue;
        }
        int32_t lat = le32s(points + at), lon = le32s(points + at + 4);
        at += 8;
        for (int k = 1; k < count; k++) {
            if (d->point_bytes - at < 4) {
                break;
            }
            int16_t dlat = le16s(points + at), dlon = le16s(points + at + 2);
            at += 4;
            int32_t next_lat = lat + dlat, next_lon = lon + dlon;
            if (dlat == ESCAPE && dlon == ESCAPE) {
                if (d->point_bytes - at < 8) {
                    break;
                }
                next_lat = le32s(points + at);
                next_lon = le32s(points + at + 4);
                at += 8;
            }
            double a0 = lat / 1e4, o0 = lon / 1e4, a1 = next_lat / 1e4, o1 = next_lon / 1e4;
            if (meets(b, a0, o0, a1, o1)) {
                fn(ctx, kind, a0, o0, a1, o1);
            }
            lat = next_lat;
            lon = next_lon;
        }
    }
}

void map_data_towns(const map_data_t *d, const map_bounds_t *b, map_town_fn fn, void *ctx)
{
    for (uint32_t i = 0; i < d->towns; i++) {
        const uint8_t *t = d->blob + d->town_off + (size_t)i * RECORD_LEN;
        uint32_t name = le32(t + 12);
        map_town_t town = { .lat = le32s(t) / 1e4, .lon = le32s(t + 4) / 1e4, .population = le32(t + 8) };
        if (name >= d->name_bytes || !map_bounds_contain(b, town.lat, town.lon)) {
            continue;
        }
        town.name = (const char *)d->blob + d->name_off + name;
        if (!fn(ctx, &town)) {
            return;
        }
    }
}

void map_data_airports(const map_data_t *d, const map_bounds_t *b, map_airport_fn fn, void *ctx)
{
    for (uint32_t i = 0; i < d->airports; i++) {
        const uint8_t *a = d->blob + d->airport_off + (size_t)i * RECORD_LEN;
        map_airport_t airport = { .lat = le32s(a) / 1e4, .lon = le32s(a + 4) / 1e4, .large = a[15] == 0 };
        if (!map_bounds_contain(b, airport.lat, airport.lon)) {
            continue;
        }
        memcpy(airport.iata, a + 8, 3);
        memcpy(airport.icao, a + 11, 4);
        if (!fn(ctx, &airport)) {
            return;
        }
    }
}
