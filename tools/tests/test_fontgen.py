import unittest

import fontgen


class ParseCharsetTest(unittest.TestCase):
    def test_ascii_is_the_printable_range(self):
        cps = fontgen.parse_charset("ascii")
        self.assertEqual(cps[0], 0x20)
        self.assertEqual(cps[-1], 0x7E)
        self.assertEqual(len(cps), 95)

    def test_text_covers_czech_and_symbols(self):
        cps = set(fontgen.parse_charset("text"))
        for ch in "ŽžŮůĚěŘřŤťĎďŇň°µ²€–…→":
            self.assertIn(ord(ch), cps, ch)

    def test_ranges_and_single_codepoints_combine_sorted_without_duplicates(self):
        self.assertEqual(fontgen.parse_charset("0x41-0x43,0x20AC,0x41"), [0x41, 0x42, 0x43, 0x20AC])


class PackRowsTest(unittest.TestCase):
    def test_rows_are_msb_first_and_padded_to_bytes(self):
        self.assertEqual(fontgen.pack_rows([[1, 0, 1], [0, 1, 0]]), bytes([0b10100000, 0b01000000]))

    def test_rows_wider_than_a_byte_use_two_bytes(self):
        self.assertEqual(fontgen.pack_rows([[1] * 9]), bytes([0xFF, 0x80]))


class TrimGlyphTest(unittest.TestCase):
    def test_blank_rows_and_columns_go_and_the_offsets_follow(self):
        glyph = dict(cp=0x41, width=4, height=4, x=1, y=-4, advance=5,
                     rows=[[0, 0, 0, 0], [0, 1, 1, 0], [0, 1, 0, 0], [0, 0, 0, 0]])
        trimmed = fontgen.trim_glyph(glyph)
        self.assertEqual((trimmed["width"], trimmed["height"], trimmed["x"], trimmed["y"]), (2, 2, 2, -3))
        self.assertEqual(trimmed["rows"], [[1, 1], [1, 0]])
        self.assertEqual(trimmed["advance"], 5)

    def test_a_glyph_without_ink_keeps_only_its_advance(self):
        glyph = dict(cp=0x20, width=3, height=2, x=0, y=-2, advance=4, rows=[[0, 0, 0], [0, 0, 0]])
        self.assertEqual(fontgen.trim_glyph(glyph), dict(cp=0x20, width=0, height=0, x=0, y=0, advance=4, rows=[]))


class LineMetricsTest(unittest.TestCase):
    def test_grows_to_fit_ink_above_the_ascent_and_below_the_descent(self):
        glyphs = [dict(width=2, height=13, y=-13), dict(width=2, height=4, y=1), dict(width=0, height=0, y=0)]
        self.assertEqual(fontgen.line_metrics(glyphs, 12, 3), (13, 18))
        self.assertEqual(fontgen.line_metrics([], 12, 3), (12, 15))


class EmitCTest(unittest.TestCase):
    def test_emits_glyph_table_with_offsets(self):
        glyphs = [
            dict(cp=0x20, width=0, height=0, x=0, y=0, advance=5, rows=[]),
            dict(cp=0x41, width=3, height=2, x=1, y=-2, advance=6, rows=[[1, 1, 1], [1, 0, 1]]),
        ]
        text = fontgen.emit_c("tiny", "Tiny.ttf", 8, glyphs, ascent=7, line_height=9)
        self.assertIn("{ 0x0020, 0, 0, 0, 0, 0, 5 },", text)
        self.assertIn("{ 0x0041, 0, 3, 2, 1, -2, 6 },", text)
        self.assertIn("0xE0, 0xA0,", text)
        self.assertIn("const gfx_font_t gfx_font_tiny = { s_bitmap, s_glyphs, 2, 7, 9 };", text)

    def test_names_the_font_licence_in_the_header(self):
        text = fontgen.emit_c("tiny", "Tiny.ttf", 8, [], ascent=7, line_height=9,
                              licence="assets/fonts/LICENSE-Tiny.txt")
        self.assertIn("Font licence: assets/fonts/LICENSE-Tiny.txt", text.splitlines()[1])

    def test_rejects_glyphs_that_do_not_fit_the_c_types(self):
        glyph = dict(cp=0x41, width=300, height=1, x=0, y=0, advance=1, rows=[[1] * 300])
        with self.assertRaises(ValueError):
            fontgen.emit_c("big", "Big.ttf", 400, [glyph], ascent=1, line_height=1)


if __name__ == "__main__":
    unittest.main()
