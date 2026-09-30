import pathlib
import tempfile
import unittest

import gen_zones

# A TZif version 2 file as far as gen_zones reads it: the magic, some binary data, and the POSIX
# footer between the last two newlines (RFC 8536 §3.3).
PRAGUE = b"TZif2" + b"\x00" * 40 + b"\nCET-1CEST,M3.5.0,M10.5.0/3\n"
DUBAI = b"TZif2" + b"\x00" * 40 + b"\n<+04>-4\n"


class PosixTest(unittest.TestCase):
    def write(self, data):
        f = tempfile.NamedTemporaryFile(delete=False)
        f.write(data)
        f.close()
        self.addCleanup(pathlib.Path(f.name).unlink)
        return pathlib.Path(f.name)

    def test_the_footer_is_the_posix_rule(self):
        self.assertEqual(gen_zones.posix_of(self.write(PRAGUE)), "CET-1CEST,M3.5.0,M10.5.0/3")
        self.assertEqual(gen_zones.posix_of(self.write(DUBAI)), "<+04>-4")

    def test_a_file_without_a_footer_has_no_rule(self):
        self.assertIsNone(gen_zones.posix_of(self.write(b"TZif" + b"\x00" * 40)))  # version 1
        self.assertIsNone(gen_zones.posix_of(self.write(b"not a tzfile\n")))
        self.assertIsNone(gen_zones.posix_of(self.write(b"TZif2" + b"\x00" * 40 + b"\n\n")))  # empty footer


class ZonesTest(unittest.TestCase):
    def test_the_table_holds_utc_and_the_zones_of_zone_tab(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            (root / "Europe").mkdir()
            (root / "Asia").mkdir()
            (root / "Europe" / "Prague").write_bytes(PRAGUE)
            (root / "Asia" / "Dubai").write_bytes(DUBAI)
            (root / "UTC").write_bytes(b"TZif2" + b"\x00" * 40 + b"\nUTC0\n")
            (root / "zone.tab").write_text("# comment\nCZ\t+5005+01426\tEurope/Prague\n"
                                           "AE\t+2518+05518\tAsia/Dubai\nXX\t+0000+00000\tNowhere/Missing\n")
            (root / "+VERSION").write_text("2026c\n")
            self.assertEqual(gen_zones.zones(root), {"Asia/Dubai": "<+04>-4", "Europe/Prague":
                                                     "CET-1CEST,M3.5.0,M10.5.0/3", "UTC": "UTC0"})
            out = root / "zones.js"
            self.assertEqual(gen_zones.main(["--zoneinfo", d, "--out", str(out)]), 0)
            text = out.read_text()
            self.assertIn("from tzdata 2026c", text)
            self.assertIn('const ZONES = {"Asia/Dubai":"<+04>-4",', text)


if __name__ == "__main__":
    unittest.main()
