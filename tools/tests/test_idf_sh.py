import os
import pathlib
import subprocess
import tempfile
import unittest

IDF_SH = pathlib.Path(__file__).resolve().parents[1] / "idf.sh"


class FakeIdf:
    """A stand-in ESP-IDF: its export.sh puts an idf.py on PATH that records its arguments."""

    def __init__(self, root):
        self.root = pathlib.Path(root)
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        self.calls = self.root / "calls.txt"
        idf_py = bin_dir / "idf.py"
        idf_py.write_text(f'#!/bin/sh\necho "args=$* ESPPORT=${{ESPPORT:-}}" >> "{self.calls}"\n')
        idf_py.chmod(0o755)
        (self.root / "export.sh").write_text(f'export PATH="{bin_dir}:$PATH"\n')

    def calls_made(self):
        return self.calls.read_text().splitlines() if self.calls.exists() else []


class PortGuardTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fake = FakeIdf(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_idf_sh(self, *args, espport=None):
        env = {k: v for k, v in os.environ.items() if k != "ESPPORT"}
        env["REFLBO_IDF_PATH"] = str(self.fake.root)
        if espport:
            env["ESPPORT"] = espport
        return subprocess.run(["bash", str(IDF_SH), *args], env=env, capture_output=True, text=True)

    def test_flash_without_a_port_is_refused(self):
        result = self.run_idf_sh("flash")
        self.assertEqual(result.returncode, 2)
        self.assertIn("-p", result.stderr)
        self.assertEqual(self.fake.calls_made(), [])

    def test_erase_flash_without_a_port_is_refused(self):
        result = self.run_idf_sh("erase-flash")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(self.fake.calls_made(), [])

    def test_flash_with_an_explicit_port_runs(self):
        result = self.run_idf_sh("-p", "/dev/cu.usbmodemTEST", "flash")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.fake.calls_made(), ["args=-p /dev/cu.usbmodemTEST flash ESPPORT="])

    def test_espport_counts_as_an_explicit_port(self):
        result = self.run_idf_sh("flash", espport="/dev/cu.usbmodemTEST")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.fake.calls_made(), ["args=flash ESPPORT=/dev/cu.usbmodemTEST"])

    def test_build_needs_no_port(self):
        result = self.run_idf_sh("build")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.fake.calls_made(), ["args=build ESPPORT="])


if __name__ == "__main__":
    unittest.main()
