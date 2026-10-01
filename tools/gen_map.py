#!/usr/bin/env python3
"""Pack the built-in map under the radars (spec §11.1) into assets/map/map.bin.

Run it through tools/gen_map.sh; it needs Python 3 and nothing else. The sources are public domain
and are downloaded once into ref/mapdata/ (git-ignored); their SHA-256 go in assets/map/sources.txt:
  Natural Earth 5.1.2: the 1:10 m land borders, the 1:50 m coastline, the 1:10 m populated places
  OurAirports: airports.csv, of which the large and medium airports
The layout of map.bin is documented in components/map/include/map_data.h; this file writes it.
"""
import argparse
import binascii
import csv
import hashlib
import io
import json
import math
import pathlib
import struct
import sys
import urllib.request

NE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/"
SOURCES = {
    "borders": NE + "ne_10m_admin_0_boundary_lines_land.geojson",
    "coast": NE + "ne_50m_coastline.geojson",
    "places": NE + "ne_10m_populated_places_simple.geojson",
    "airports": "https://davidmegginson.github.io/ourairports-data/airports.csv",
}
VERSION = 1
MAGIC = b"RMAP"
HEADER = struct.Struct("<4sHHI10I")  # magic, version, reserved, crc32, then the ten section words
LINE = struct.Struct("<4hIHBx")      # bbox in 0.01 deg (lat_min, lat_max, lon_min, lon_max), point pos, count, kind
TOWN = struct.Struct("<iiII")        # lat_e4, lon_e4, population, name pos
AIRPORT = struct.Struct("<ii3s4sB")  # lat_e4, lon_e4, IATA, ICAO, kind (0 large, 1 medium)
KIND_BORDER, KIND_COAST = 0, 1
TOLERANCE_DEG = 0.002   # Douglas-Peucker, about 200 m (spec §11.1)
PIECE_POINTS = 128      # a line's points per record, so a render decodes only what lies near
ESCAPE = -32768         # a delta pair (ESCAPE, ESCAPE) is followed by an absolute point

# Natural Earth's own spelling, corrected where a Czech user would see it
NAME_FIXES = {"Pizen": "Plzeň", "Usti Nad Labem": "Ústí nad Labem"}


def line_parts(geojson):
    """GeoJSON LineStrings and MultiLineStrings -> [[(lon, lat), ...], ...]."""
    out = []
    for feature in geojson["features"]:
        geometry = feature.get("geometry")
        if not geometry:
            continue
        parts = [geometry["coordinates"]] if geometry["type"] == "LineString" else geometry["coordinates"]
        out.extend([[(float(x), float(y)) for x, y, *_ in part] for part in parts if len(part) >= 2])
    return out


def _key(point):
    return round(point[0], 6), round(point[1], 6)


def chain(lines):
    """Joins lines that meet end to end where only two meet, so 7980 border pieces become ~400 lines."""
    ends = {}
    for i, line in enumerate(lines):
        for point in (line[0], line[-1]):
            ends.setdefault(_key(point), []).append(i)
    used = [False] * len(lines)
    out = []
    for i, line in enumerate(lines):
        if used[i]:
            continue
        used[i] = True
        current = list(line)
        for _ in range(2):  # grow at the tail, then reversed, at the head
            while True:
                tail = _key(current[-1])
                if len(ends.get(tail, [])) != 2:
                    break
                nxt = [j for j in ends[tail] if not used[j]]
                if not nxt:
                    break
                used[nxt[0]] = True
                other = lines[nxt[0]]
                current.extend(other[1:] if _key(other[0]) == tail else list(reversed(other))[1:])
            current.reverse()
        out.append(current)
    return out


def simplify(points, tolerance):
    """Douglas-Peucker on (lon, lat), with longitude scaled by the cosine of the latitude."""
    if len(points) < 3:
        return list(points)
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        a, b = stack.pop()
        (x1, y1), (x2, y2) = points[a], points[b]
        k = math.cos(math.radians((y1 + y2) / 2))
        dx, dy = (x2 - x1) * k, y2 - y1
        length = math.hypot(dx, dy)
        best, index = -1.0, -1
        for i in range(a + 1, b):
            px, py = (points[i][0] - x1) * k, points[i][1] - y1
            d = abs(dx * py - dy * px) / length if length else math.hypot(px, py)
            if d > best:
                best, index = d, i
        if best > tolerance:
            keep[index] = True
            stack.extend([(a, index), (index, b)])
    return [p for p, kept in zip(points, keep) if kept]


def split(points, size=PIECE_POINTS):
    """Pieces of at most `size` points, each starting where the last one ended."""
    if len(points) <= size:
        return [points]
    return [points[i:i + size] for i in range(0, len(points) - 1, size - 1) if len(points[i:i + size]) >= 2]


def encode_points(points):
    """The first point absolute (int32 lat, lon in 1e-4 degrees), then int16 deltas; a delta that
    doesn't fit is an (ESCAPE, ESCAPE) pair followed by the point absolute."""
    out = bytearray()
    last = None
    for lon, lat in points:
        q = (round(lat * 1e4), round(lon * 1e4))
        if last is None:
            out += struct.pack("<ii", *q)
        else:
            d = (q[0] - last[0], q[1] - last[1])
            if -32767 <= d[0] <= 32767 and -32767 <= d[1] <= 32767:
                out += struct.pack("<hh", *d)
            else:
                out += struct.pack("<hhii", ESCAPE, ESCAPE, *q)
        last = q
    return bytes(out)


def bbox(points):
    """(lat_min, lat_max, lon_min, lon_max) in 0.01 degrees, rounded outwards."""
    lats = [p[1] for p in points]
    lons = [p[0] for p in points]
    return (math.floor(min(lats) * 100), math.ceil(max(lats) * 100),
            math.floor(min(lons) * 100), math.ceil(max(lons) * 100))


def latin(text):
    """Every character within Latin-1 and Latin Extended-A, which the fonts cover (spec §4.4)."""
    return all(ord(c) <= 0x17F for c in text)


def town_name(props):
    """The local name where Natural Earth has one (`namepar`: "Praha"), its own otherwise, in ASCII
    when the fonts lack its letters; corrected where a Czech user would see it."""
    name = props.get("namepar") or props.get("name") or ""
    if not latin(name):
        name = props.get("nameascii") or ""
    return NAME_FIXES.get(name, name)


def towns(geojson):
    """[(lat_e4, lon_e4, population, name)], the largest first."""
    out = []
    for feature in geojson["features"]:
        props = feature["properties"]
        name = town_name(props)
        if not name:
            continue
        out.append((round(props["latitude"] * 1e4), round(props["longitude"] * 1e4),
                    max(0, int(props.get("pop_max") or 0)), name))
    out.sort(key=lambda t: (-t[2], t[3]))
    return out


def airports(csv_text):
    """[(lat_e4, lon_e4, iata, icao, kind)]: the large and medium airports, large first."""
    out = []
    for row in csv.DictReader(io.StringIO(csv_text)):
        kind = {"large_airport": 0, "medium_airport": 1}.get(row["type"])
        if kind is None:
            continue
        icao = (row.get("icao_code") or row.get("ident") or "")[:4]
        out.append((round(float(row["latitude_deg"]) * 1e4), round(float(row["longitude_deg"]) * 1e4),
                    row.get("iata_code", "")[:3], icao, kind))
    out.sort(key=lambda a: (a[4], a[3]))
    return out


def lines_of(border_lines, coast_lines, tolerance=TOLERANCE_DEG):
    """[(kind, points)] for map.bin: the borders chained, both simplified and split into pieces."""
    out = []
    for kind, lines in ((KIND_BORDER, chain(border_lines)), (KIND_COAST, chain(coast_lines))):
        for line in lines:
            for piece in split(simplify(line, tolerance)):
                out.append((kind, piece))
    return out


def pack(lines, town_list, airport_list):
    """map.bin's bytes (layout in components/map/include/map_data.h)."""
    points = bytearray()
    line_bytes = bytearray()
    for kind, piece in lines:
        line_bytes += LINE.pack(*bbox(piece), len(points), len(piece), kind)
        points += encode_points(piece)
    names = bytearray()
    town_bytes = bytearray()
    for lat, lon, pop, name in town_list:
        town_bytes += TOWN.pack(lat, lon, pop, len(names))
        names += name.encode("utf-8") + b"\0"
    airport_bytes = bytearray()
    for lat, lon, iata, icao, kind in airport_list:
        airport_bytes += AIRPORT.pack(lat, lon, iata.encode("ascii", "replace").ljust(3, b"\0"),
                                      icao.encode("ascii", "replace").ljust(4, b"\0"), kind)
    body = bytearray()
    offsets = []
    for section in (line_bytes, points, town_bytes, airport_bytes, names):
        offsets.append(HEADER.size + len(body))
        body += section
        body += b"\0" * (-len(body) % 4)  # every section starts on a word
    words = (len(lines), offsets[0], offsets[1], len(points), len(town_list), offsets[2],
             len(airport_list), offsets[3], offsets[4], len(names))
    crc = binascii.crc32(struct.pack("<10I", *words) + bytes(body)) & 0xFFFFFFFF
    return HEADER.pack(MAGIC, VERSION, 0, crc, *words) + bytes(body)


def fetch(name, cache):
    """A source from the cache, downloaded first if it isn't there."""
    path = cache / pathlib.Path(SOURCES[name]).name
    if not path.exists():
        cache.mkdir(parents=True, exist_ok=True)
        print(f"downloading {SOURCES[name]}", file=sys.stderr)
        with urllib.request.urlopen(SOURCES[name], timeout=120) as response:
            path.write_bytes(response.read())
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cache", default="ref/mapdata", help="where the sources are kept")
    parser.add_argument("--out", default="assets/map/map.bin")
    parser.add_argument("--sources", default="assets/map/sources.txt")
    args = parser.parse_args(argv)
    cache = pathlib.Path(args.cache)
    paths = {name: fetch(name, cache) for name in SOURCES}
    lines = lines_of(line_parts(json.loads(paths["borders"].read_text())),
                     line_parts(json.loads(paths["coast"].read_text())))
    town_list = towns(json.loads(paths["places"].read_text()))
    airport_list = airports(paths["airports"].read_text(encoding="utf-8"))
    blob = pack(lines, town_list, airport_list)
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    with open(args.sources, "w") as f:
        f.write("# The sources of map.bin (tools/gen_map.py), all public domain\n")
        for name, path in paths.items():
            f.write(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {SOURCES[name]}\n")
    print(f"{out}: {len(blob)} bytes, {len(lines)} lines, {len(town_list)} towns, {len(airport_list)} airports")
    return 0


if __name__ == "__main__":
    sys.exit(main())
