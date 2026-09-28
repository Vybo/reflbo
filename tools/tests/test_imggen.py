import unittest

import imggen


class ParseManifestTest(unittest.TestCase):
    def test_reads_names_and_sizes_and_skips_comments(self):
        text = "# header\nthermometer device_thermostat 16 24  # trailing\n\ndrop water_drop 48\n"
        self.assertEqual(imggen.parse_manifest(text),
                         [("thermometer", "device_thermostat", [16, 24]), ("drop", "water_drop", [48])])

    def test_rejects_a_line_without_sizes_or_with_a_bad_c_name(self):
        with self.assertRaises(ValueError):
            imggen.parse_manifest("thermometer device_thermostat\n")
        with self.assertRaises(ValueError):
            imggen.parse_manifest("2x water_drop 16\n")


class ParseCodepointsTest(unittest.TestCase):
    def test_maps_names_to_codepoints(self):
        self.assertEqual(imggen.parse_codepoints("bolt ea0b\nwater_drop e798\n"), {"bolt": 0xEA0B, "water_drop": 0xE798})


class EmitTest(unittest.TestCase):
    def test_c_defines_one_square_bitmap_per_icon_and_size(self):
        rows = [[1, 0, 0, 0, 0, 0, 0, 0, 1], [0] * 9]
        text = imggen.emit_c([("x_9", 9, rows)], "Icons.ttf", licence="assets/icons/LICENSE.txt")
        self.assertIn("Icon licence: assets/icons/LICENSE.txt", text.splitlines()[1])
        self.assertIn("0x80, 0x80, 0x00, 0x00,", text)
        self.assertIn("const gfx_bitmap_t gfx_icon_x_9 = { s_x_9, 9, 9 };", text)

    def test_header_declares_every_icon(self):
        text = imggen.emit_h([("a_16", 16, []), ("b_24", 24, [])], "Icons.ttf")
        self.assertIn("extern const gfx_bitmap_t gfx_icon_a_16;", text)
        self.assertIn("extern const gfx_bitmap_t gfx_icon_b_24;", text)


if __name__ == "__main__":
    unittest.main()
