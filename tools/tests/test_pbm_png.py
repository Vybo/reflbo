import struct
import unittest
import zlib

import pbm_png


class ParsePbmTest(unittest.TestCase):
    def test_reads_header_comments_and_raster(self):
        self.assertEqual(pbm_png.parse_pbm(b"P4\n# note\n8 2\n\xF0\x0F"), (8, 2, b"\xF0\x0F"))

    def test_rejects_other_formats_and_short_rasters(self):
        with self.assertRaises(ValueError):
            pbm_png.parse_pbm(b"P1\n8 2\n\xF0\x0F")
        with self.assertRaises(ValueError):
            pbm_png.parse_pbm(b"P4\n8 2\n\xF0")


class PngTest(unittest.TestCase):
    def test_png_is_one_bit_greyscale_with_black_as_zero(self):
        png = pbm_png.png_from_pbm(b"P4\n8 2\n\xF0\x0F")
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        width, height, depth, colour = struct.unpack(">IIBB", png[16:26])
        self.assertEqual((width, height, depth, colour), (8, 2, 1, 0))
        idat_len = struct.unpack(">I", png[33:37])[0]
        self.assertEqual(png[37:41], b"IDAT")
        self.assertEqual(zlib.decompress(png[41:41 + idat_len]), b"\x00\x0F\x00\xF0")


if __name__ == "__main__":
    unittest.main()
