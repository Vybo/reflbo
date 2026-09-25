#!/usr/bin/env python3
"""Convert binary PBM (P4) images to PNG using only the standard library.
  python3 tools/pbm_png.py IN.pbm OUT.png
"""
import struct
import sys
import zlib


def parse_pbm(data):
    """Returns (width, height, raster) for a P4 image; the raster is row-major, MSB first, 1 = black."""
    fields = []
    pos = 0
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] not in (b"\n", b"\r"):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        if start == pos:
            raise ValueError("truncated PBM header")
        fields.append(data[start:pos])
    if fields[0] != b"P4":
        raise ValueError("not a binary PBM (P4)")
    width, height = int(fields[1]), int(fields[2])
    pos += 1  # the single whitespace after the height
    size = (width + 7) // 8 * height
    raster = data[pos:pos + size]
    if len(raster) != size:
        raise ValueError("truncated PBM raster")
    return width, height, raster


def png_from_pbm(data):
    width, height, raster = parse_pbm(data)
    row_bytes = (width + 7) // 8
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        raw += bytes(b ^ 0xFF for b in raster[y * row_bytes:(y + 1) * row_bytes])  # PNG grey: 0 = black

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 1, 0, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(argv[0], "rb") as src, open(argv[1], "wb") as dst:
        dst.write(png_from_pbm(src.read()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
