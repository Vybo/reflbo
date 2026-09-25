import base64
import unittest

import screenshot

PBM = b"P4\n8 2\n\xF0\x0F"


def transcript(body_lines):
    return "\n".join(["I (100) main: reflbo ready", "reflbo> screenshot", screenshot.BEGIN, *body_lines,
                      screenshot.END, "reflbo> "])


class ExtractPbmTest(unittest.TestCase):
    def test_image_between_markers_is_decoded(self):
        encoded = base64.b64encode(PBM).decode()
        self.assertEqual(screenshot.extract_pbm(transcript([encoded[:8], encoded[8:]])), PBM)

    def test_missing_end_marker_is_rejected(self):
        text = "\n".join([screenshot.BEGIN, base64.b64encode(PBM).decode()])
        with self.assertRaises(ValueError):
            screenshot.extract_pbm(text)

    def test_noise_inside_the_image_is_rejected(self):
        encoded = base64.b64encode(PBM).decode()
        with self.assertRaises(ValueError):
            screenshot.extract_pbm(transcript([encoded[:8], "I (120) wifi: scan done", encoded[8:]]))

    def test_truncated_image_is_rejected(self):
        with self.assertRaises(ValueError):
            screenshot.extract_pbm(transcript([base64.b64encode(PBM[:-1]).decode()]))

    def test_extra_data_is_rejected(self):
        with self.assertRaises(ValueError):
            screenshot.extract_pbm(transcript([base64.b64encode(PBM).decode(), "AAAA"]))


if __name__ == "__main__":
    unittest.main()
