#!/usr/bin/env python3
"""The panel images in docs/images/panel, from the golden renders (test/host/golden/*.pbm): each at
twice its size in the reflective panel's grey, framed, and a few composites. Standard library only.
  python3 tools/docs_images.py      # after rewriting goldens that the docs show
"""
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pbm_png  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "test" / "host" / "golden"
OUT = ROOT / "docs" / "images" / "panel"

SCALE = 2
BORDER = 2  # the frame, in output pixels
CLEAR, PAPER, INK, EDGE = 0, 1, 2, 3  # palette indices
PALETTE = [(0, 0, 0), (0xE4, 0xE6, 0xDF), (0x1E, 0x21, 0x1D), (0x9A, 0x9D, 0x95)]

# docs/images/panel/<name>.png from test/host/golden/<golden>.pbm
SINGLE = {
    "home": "dash_home",
    "home-cs": "dash_home_cs",
    "home-inverted": "dash_home_inverted",
    "home-stale": "dash_home_stale",
    "indoor": "dash_indoor",
    "weather": "dash_weather_now",
    "weather-rain": "dash_weather_rain",
    "focus": "dash_focus_forecast",
    "focus-rain": "dash_focus_rain_now",
    "air": "dash_air_grid",
    "sun-uv": "dash_grid_sun_uv",
    "rain-map": "dash_grid_rain_map",
    "radar": "dash_radar",
    "radar-loop": "dash_radar_loop",
    "radar-rainviewer": "dash_radar_rainviewer",
    "flights": "dash_flights",
    "split-weather": "dash_split_weather",
    "split-eight": "dash_split_eight",
    "split-compact": "dash_split_compact",
    "split-small": "dash_split_xs_grid",
    "menu": "screen_menu_root_en",
    "menu-time": "screen_menu_edit_zone_en",
    "config": "screen_config_ap_en",
    "first-run": "screen_first_run_en",
    "critical": "screen_critical_en",
    "toast": "screen_toast_preset_cs",
}
# Composites: name -> (columns, goldens).
GRIDS = {"hero": (2, ["dash_home", "dash_weather_now", "dash_radar", "dash_flights"])}
# The status bar (its 20 rows and the line under it) of each golden, stacked.
STATUS_ROWS = 22
STRIPS = {"status-bars": ["dash_home_battery_details", "dash_home_syncing", "dash_home_sync_failed",
                          "dash_home_always", "dash_home_always_rejoining", "dash_home_stale_web",
                          "dash_home_invalid"]}
GAP = 16


class Image:
    """Palette indices, one bytes object per row."""

    def __init__(self, width, height, rows=None):
        self.width, self.height = width, height
        self.rows = rows if rows is not None else [bytearray(width) for _ in range(height)]


def panel(pbm, rows=None):
    """A render at SCALE, framed: black pixels as INK, white as PAPER; only its first `rows` if given."""
    width, height, raster = pbm_png.parse_pbm(pbm)
    height = min(height, rows) if rows else height
    row_bytes = (width + 7) // 8
    out = Image(width * SCALE + 2 * BORDER, height * SCALE + 2 * BORDER)
    edge = bytes([EDGE]) * out.width
    for y in range(BORDER):
        out.rows[y][:] = edge
        out.rows[-1 - y][:] = edge
    for y in range(height):
        line = bytearray([EDGE]) * out.width
        src = raster[y * row_bytes:(y + 1) * row_bytes]
        for x in range(width):
            black = src[x >> 3] & (0x80 >> (x & 7))
            line[BORDER + x * SCALE:BORDER + (x + 1) * SCALE] = bytes([INK if black else PAPER]) * SCALE
        for k in range(SCALE):
            out.rows[BORDER + y * SCALE + k][:] = line
    return out


def grid(images, columns, gap=GAP):
    """The images left to right, then top to bottom, in cells of the largest one's size; gaps are CLEAR."""
    cell_w = max(i.width for i in images)
    cell_h = max(i.height for i in images)
    count_rows = (len(images) + columns - 1) // columns
    out = Image(columns * cell_w + (columns - 1) * gap, count_rows * cell_h + (count_rows - 1) * gap)
    for n, img in enumerate(images):
        ox = (n % columns) * (cell_w + gap)
        oy = (n // columns) * (cell_h + gap)
        for y in range(img.height):
            out.rows[oy + y][ox:ox + img.width] = img.rows[y]
    return out


def png(img):
    """A palette PNG, 2 bits a pixel, its first colour transparent."""
    raw = bytearray()
    for row in img.rows:
        raw.append(0)  # filter: none
        packed = bytearray((img.width * 2 + 7) // 8)
        for x, v in enumerate(row):
            packed[x >> 2] |= v << (6 - 2 * (x & 3))
        raw += packed

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", img.width, img.height, 2, 3, 0, 0, 0))
            + chunk(b"PLTE", b"".join(bytes(c) for c in PALETTE))
            + chunk(b"tRNS", b"\x00")
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def golden(name):
    return (GOLDEN / f"{name}.pbm").read_bytes()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    made = {name: panel(golden(src)) for name, src in SINGLE.items()}
    made.update({name: grid([panel(golden(g)) for g in goldens], columns) for name, (columns, goldens) in GRIDS.items()})
    made.update({name: grid([panel(golden(g), rows=STATUS_ROWS) for g in goldens], 1, gap=GAP // 2)
                 for name, goldens in STRIPS.items()})
    for name, img in made.items():
        path = OUT / f"{name}.png"
        path.write_bytes(png(img))
        print(f"{path.relative_to(ROOT)}: {img.width}×{img.height}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
