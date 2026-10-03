import struct
import unittest
import zlib

import docs_images

# An 8×2 PBM: the first row's left half black, the second row's right half.
PBM = b"P4\n8 2\n\xF0\x0F"


def chunks(png):
    """The PNG's chunks as (tag, body), after the signature."""
    out, pos = [], 8
    while pos < len(png):
        size = struct.unpack(">I", png[pos:pos + 4])[0]
        out.append((png[pos + 4:pos + 8], png[pos + 8:pos + 8 + size]))
        pos += 12 + size
    return out


class PanelTest(unittest.TestCase):
    def test_scales_the_render_and_frames_it(self):
        img = docs_images.panel(PBM)
        b = docs_images.BORDER
        self.assertEqual((img.width, img.height), (8 * 2 + 2 * b, 2 * 2 + 2 * b))
        self.assertTrue(all(v == docs_images.EDGE for v in img.rows[0]))
        self.assertEqual(img.rows[b][b:b + 16], bytes([docs_images.INK] * 8 + [docs_images.PAPER] * 8))
        self.assertEqual(img.rows[b + 1], img.rows[b])  # each pixel is a 2×2 block
        self.assertEqual(img.rows[b + 2][b:b + 16], bytes([docs_images.PAPER] * 8 + [docs_images.INK] * 8))

    def test_keeps_only_the_rows_asked_for(self):
        img = docs_images.panel(PBM, rows=1)
        self.assertEqual(img.height, 1 * 2 + 2 * docs_images.BORDER)


class GridTest(unittest.TestCase):
    def test_places_images_in_columns_with_clear_gaps(self):
        a = docs_images.panel(PBM)
        img = docs_images.grid([a, a, a], columns=2, gap=6)
        self.assertEqual((img.width, img.height), (2 * a.width + 6, 2 * a.height + 6))
        self.assertEqual(img.rows[0][a.width:a.width + 6], bytes(6))  # the gap is see-through
        self.assertEqual(img.rows[a.height + 6][a.width:], bytes(a.width + 6))  # no fourth image


class PngTest(unittest.TestCase):
    def test_writes_a_two_bit_palette_png_with_a_clear_first_colour(self):
        img = docs_images.panel(PBM)
        png = docs_images.png(img)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        found = dict(chunks(png))
        width, height, depth, colour = struct.unpack(">IIBB", found[b"IHDR"][:10])
        self.assertEqual((width, height, depth, colour), (img.width, img.height, 2, 3))
        self.assertEqual(len(found[b"PLTE"]), 3 * 4)
        self.assertEqual(found[b"tRNS"], b"\x00")
        raw = zlib.decompress(found[b"IDAT"])
        stride = 1 + (img.width * 2 + 7) // 8
        self.assertEqual(len(raw), stride * img.height)
        self.assertEqual(raw[0], 0)  # filter: none
        self.assertEqual(raw[1], docs_images.EDGE * 0b01010101)  # four edge pixels in the first byte


if __name__ == "__main__":
    unittest.main()
