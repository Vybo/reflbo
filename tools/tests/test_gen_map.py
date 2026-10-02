import binascii
import struct
import unittest

import gen_map


class ChainTest(unittest.TestCase):
    def test_lines_that_meet_end_to_end_join(self):
        a, b, c, d, e = (0, 0), (1, 0), (2, 1), (5, 5), (6, 6)
        joined = gen_map.chain([[a, b], [c, b], [d, e]])  # the second one runs backwards
        self.assertEqual(len(joined), 2)
        self.assertIn(max(joined, key=len), ([a, b, c], [c, b, a]))

    def test_three_lines_meeting_at_a_point_stay_apart(self):
        hub = (0, 0)
        joined = gen_map.chain([[hub, (1, 0)], [hub, (0, 1)], [hub, (-1, 0)]])
        self.assertEqual(len(joined), 3)


class SimplifyTest(unittest.TestCase):
    def test_a_point_close_to_the_line_goes_and_a_far_one_stays(self):
        near = [(0, 0), (1, 0.001), (2, 0)]
        self.assertEqual(gen_map.simplify(near, 0.002), [(0, 0), (2, 0)])
        far = [(0, 0), (1, 0.01), (2, 0)]
        self.assertEqual(gen_map.simplify(far, 0.002), far)

    def test_longitude_is_scaled_by_the_latitude(self):
        # At 60 degrees a degree of longitude is half a degree of latitude long
        line = [(0, 60), (1, 60.0015), (2, 60)]
        self.assertEqual(len(gen_map.simplify(line, 0.002)), 2)


class SplitTest(unittest.TestCase):
    def test_pieces_overlap_by_a_point_and_rebuild_the_line(self):
        line = [(i, 0) for i in range(300)]
        pieces = gen_map.split(line, 128)
        self.assertTrue(all(2 <= len(p) <= 128 for p in pieces))
        rebuilt = pieces[0] + [pt for p in pieces[1:] for pt in p[1:]]
        self.assertEqual(rebuilt, line)
        self.assertEqual(gen_map.split(line[:5], 128), [line[:5]])


class EncodePointsTest(unittest.TestCase):
    def test_small_steps_are_int16_deltas(self):
        data = gen_map.encode_points([(16.6068, 49.1951), (16.6168, 49.1851)])
        self.assertEqual(data, struct.pack("<iihh", 491951, 166068, -100, 100))

    def test_a_jump_beyond_int16_is_escaped(self):
        data = gen_map.encode_points([(0, 0), (5, 0)])  # 5 degrees = 50000 units
        self.assertEqual(data, struct.pack("<iihhii", 0, 0, -32768, -32768, 0, 50000))


class NamesTest(unittest.TestCase):
    def test_the_local_name_the_ascii_one_and_the_fixes(self):
        self.assertEqual(gen_map.town_name({"name": "Prague", "namepar": "Praha"}), "Praha")
        self.assertEqual(gen_map.town_name({"name": "Brno"}), "Brno")
        self.assertEqual(gen_map.town_name({"name": "Пермь", "nameascii": "Perm"}), "Perm")
        self.assertEqual(gen_map.town_name({"name": "Pizen", "nameascii": "Pizen"}), "Plzeň")
        self.assertEqual(gen_map.town_name({"name": "Wrocław", "namepar": "Breslau"}), "Wrocław")

    def test_towns_come_largest_first(self):
        geojson = {"features": [
            {"properties": {"name": "Brno", "latitude": 49.2, "longitude": 16.61, "pop_max": 388277}},
            {"properties": {"name": "Prague", "namepar": "Praha", "latitude": 50.08, "longitude": 14.44,
                            "pop_max": 1162000}},
        ]}
        self.assertEqual(gen_map.towns(geojson), [(500800, 144400, 1162000, "Praha"), (492000, 166100, 388277, "Brno")])


class AirportsTest(unittest.TestCase):
    CSV = ('"id","ident","type","name","latitude_deg","longitude_deg","icao_code","iata_code"\n'
           '1,"LKTB","medium_airport","Brno-Tuřany",49.151901,16.6944,"LKTB","BRQ"\n'
           '2,"LKPR","large_airport","Václav Havel",50.1008,14.26,"LKPR","PRG"\n'
           '3,"CZ-0001","small_airport","Somewhere",49.0,16.0,"",""\n'
           '4,"LKCV","heliport","Pad",49.9,15.4,"",""\n')

    def test_only_large_and_medium_airports_and_large_first(self):
        self.assertEqual(gen_map.airports(self.CSV), [(501008, 142600, "PRG", "LKPR", 0), (491519, 166944, "BRQ", "LKTB", 1)])


class GeoNamesTest(unittest.TestCase):
    AREA = (48.0, 51.5, 11.0, 20.0)  # lat_min, lat_max, lon_min, lon_max

    @staticmethod
    def row(name, ascii_name, lat, lon, code, population):
        """A line of cities1000.txt: 19 tab-separated columns."""
        fields = [""] * 19
        fields[1], fields[2], fields[4], fields[5] = name, ascii_name, str(lat), str(lon)
        fields[6], fields[7], fields[14] = "P", code, str(population)
        return "\t".join(fields)

    def test_places_of_a_thousand_inside_the_area_and_no_sections_of_towns(self):
        text = "\n".join([
            self.row("Malá Strana", "Mala Strana", 50.0875, 14.4036, "PPLX", 6350),  # a section of Prague
            self.row("Kuřim", "Kurim", 49.29852, 16.53144, "PPL", 11860),
            self.row("Lhota", "Lhota", 49.5, 16.0, "PPL", 999),                         # too small
            self.row("Warszawa", "Warsaw", 52.22977, 21.01178, "PPLC", 1702139),        # outside the area
            self.row("Sl\u2019ažany", "Sl'azany", 48.39487, 18.32848, "PPL", 1706),    # a letter the fonts lack
        ])
        self.assertEqual(gen_map.geonames_places(text, self.AREA),
                         [(492985, 165314, 11860, "Kuřim"), (483949, 183285, 1706, "Sl'azany")])

    def test_natural_earth_keeps_its_towns_and_geonames_adds_the_others(self):
        natural_earth = [(500800, 144400, 1162000, "Praha"), (492000, 166100, 388277, "Brno")]
        places = [
            (500880, 144208, 1165581, "Prague"),    # Natural Earth's Praha, in English
            (491952, 166080, 404296, "Brno"),       # Natural Earth's Brno
            (491979, 166161, 86685, "Brno střed"),  # 0.7 km from Brno: one of its districts
            (492985, 165314, 11860, "Kuřim"),       # 12.8 km from Brno, beyond its 9 km
            (492775, 169990, 20664, "Vyškov"),
            (492790, 170080, 1500, "Dědice"),       # 0.7 km from Vyškov, inside its 2 km
            (491717, 167112, 3554, "Moravany"),     # 7.9 km from Brno, inside its 9 km
        ]
        self.assertEqual(gen_map.merge_towns(natural_earth, places), [
            (500800, 144400, 1162000, "Praha"), (492000, 166100, 388277, "Brno"),
            (492775, 169990, 20664, "Vyškov"), (492985, 165314, 11860, "Kuřim")])
        self.assertEqual(gen_map.merge_towns(natural_earth, []), natural_earth)  # no GeoNames at all


class PackTest(unittest.TestCase):
    def test_the_header_its_crc_and_the_sections(self):
        lines = [(gen_map.KIND_BORDER, [(16.0, 49.0), (16.5, 49.5)]), (gen_map.KIND_COAST, [(0, 0), (5, 0), (5, 1)])]
        towns = [(492000, 166100, 388277, "Brno")]
        airports = [(491519, 166944, "BRQ", "LKTB", 1)]
        blob = gen_map.pack(lines, towns, airports)
        magic, version, _, crc, *words = gen_map.HEADER.unpack_from(blob)
        self.assertEqual((magic, version), (b"RMAP", 1))
        self.assertEqual(crc, binascii.crc32(blob[12:]) & 0xFFFFFFFF)
        n_lines, line_off, point_off, point_bytes, n_towns, town_off, n_airports, airport_off, name_off, name_bytes = words
        self.assertEqual((n_lines, n_towns, n_airports), (2, 1, 1))
        self.assertTrue(all(off % 4 == 0 for off in (line_off, point_off, town_off, airport_off, name_off)))
        self.assertEqual(gen_map.LINE.unpack_from(blob, line_off), (4900, 4950, 1600, 1650, 0, 2, gen_map.KIND_BORDER))
        self.assertEqual(gen_map.LINE.unpack_from(blob, line_off + 16), (0, 100, 0, 500, 12, 3, gen_map.KIND_COAST))
        self.assertEqual(point_bytes, 12 + 8 + 12 + 4)  # the escaped jump costs 12 bytes, the step after it 4
        self.assertEqual(gen_map.TOWN.unpack_from(blob, town_off), (492000, 166100, 388277, 0))
        self.assertEqual(blob[name_off:name_off + name_bytes], b"Brno\0")
        self.assertEqual(gen_map.AIRPORT.unpack_from(blob, airport_off), (491519, 166944, b"BRQ", b"LKTB", 1))


if __name__ == "__main__":
    unittest.main()
