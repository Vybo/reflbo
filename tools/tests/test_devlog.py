import io
import unittest
from types import SimpleNamespace

import devlog


class FakeClock:
    def __init__(self):
        self.t = 0.0

    def now(self):
        return self.t

    def sleep(self, seconds):
        self.t += seconds


class FakeSerial:
    """Replays a script of reads. Each item is bytes, an OSError to raise, or a
    (ready, bytes) pair released only once ready(self) is true."""

    def __init__(self, script, clock):
        self.script = list(script)
        self.clock = clock
        self.written = []
        self.dtr = None
        self._rts = None
        self.rts_history = []

    @property
    def rts(self):
        return self._rts

    @rts.setter
    def rts(self, value):
        self._rts = value
        self.rts_history.append(value)

    def read(self, _size):
        self.clock.t += 0.1
        if not self.script:
            return b""
        item = self.script[0]
        if isinstance(item, OSError):
            self.script.pop(0)
            raise item
        if isinstance(item, tuple):
            ready, data = item
            if not ready(self):
                return b""
            self.script.pop(0)
            return data
        self.script.pop(0)
        return item

    def write(self, data):
        self.written.append(data)
        return len(data)

    def close(self):
        pass


def make_args(**overrides):
    values = dict(port="/dev/cu.usbmodemTEST", seconds=5.0, until=None, cmd=[], reset=False, out=None)
    values.update(overrides)
    return SimpleNamespace(**values)


def after_sending(command):
    return lambda fake: bool(fake.written) and fake.written[-1] == (command + "\r").encode()


class StripAnsiTest(unittest.TestCase):
    def test_removes_color_codes(self):
        self.assertEqual(devlog.strip_ansi("\x1b[0;32mI (12) main: ok\x1b[0m"), "I (12) main: ok")


class PickPortTest(unittest.TestCase):
    def test_single_port_is_used(self):
        self.assertEqual(devlog.pick_port(["/dev/cu.usbmodem1101"]), "/dev/cu.usbmodem1101")

    def test_no_port_is_an_error(self):
        with self.assertRaises(devlog.PortError):
            devlog.pick_port([])

    def test_several_ports_are_an_error_naming_them(self):
        with self.assertRaises(devlog.PortError) as ctx:
            devlog.pick_port(["/dev/cu.usbmodem2", "/dev/cu.usbmodem1"])
        self.assertIn("/dev/cu.usbmodem1, /dev/cu.usbmodem2", str(ctx.exception))


class LineSplitterTest(unittest.TestCase):
    def test_joins_chunks_and_keeps_partial_tail(self):
        splitter = devlog.LineSplitter()
        self.assertEqual(splitter.feed(b"I (1) a: he"), [])
        self.assertEqual(splitter.feed(b"llo\r\nreflbo> "), ["I (1) a: hello"])
        self.assertEqual(splitter.tail(), "reflbo> ")

    def test_crlf_split_across_reads_gives_one_line(self):
        splitter = devlog.LineSplitter()
        self.assertEqual(splitter.feed(b"one\r"), [])
        self.assertEqual(splitter.feed(b"\ntwo\r\n"), ["one", "two"])


class RunTest(unittest.TestCase):
    def run_tool(self, script, **overrides):
        clock = FakeClock()
        fake = FakeSerial(script, clock)
        out = io.StringIO()
        opened = []

        def opener(port):
            opened.append(port)
            return fake

        code = devlog.run(make_args(**overrides), opener=opener, now=clock.now,
                          sleep=clock.sleep, out=out)
        return code, out.getvalue(), fake, opened

    def test_command_is_sent_at_prompt_and_capture_stops_at_next_prompt(self):
        code, out, fake, _ = self.run_tool(
            [b"I (310) main: reflbo ready\r\n", b"reflbo> ",
             (after_sending("version"), b"version\r\nreflbo 0.1.0-dev\r\nreflbo> ")],
            cmd=["version"])
        self.assertEqual(code, 0)
        self.assertEqual(fake.written, [b"version\r"])
        self.assertIn("reflbo 0.1.0-dev", out)

    def test_nudges_for_a_prompt_when_none_is_visible(self):
        code, _, fake, _ = self.run_tool(
            [(lambda f: b"\r" in f.written, b"\r\nreflbo> "),
             (after_sending("heap"), b"heap\r\ninternal free 1\r\nreflbo> ")],
            cmd=["heap"])
        self.assertEqual(code, 0)
        self.assertEqual(fake.written[0], b"\r")
        self.assertEqual(fake.written[-1], b"heap\r")

    def test_reconnects_after_usb_disconnect(self):
        code, out, _, opened = self.run_tool(
            [b"rst:0x15\r\n", OSError("device disconnected"), b"I (300) main: reflbo ready\r\n"],
            until="reflbo ready")
        self.assertEqual(code, 0)
        self.assertEqual(len(opened), 2)
        self.assertIn("reflbo ready", out)

    def test_missing_pattern_times_out_with_code_4(self):
        code, _, _, _ = self.run_tool([b"I (1) main: starting\r\n"], until="reflbo ready", seconds=1.0)
        self.assertEqual(code, 4)

    def test_prompt_never_appearing_returns_code_3(self):
        code, _, _, _ = self.run_tool([b"garbage\r\n"], cmd=["version"], seconds=2.0)
        self.assertEqual(code, 3)

    def test_reset_pulses_rts_with_dtr_low(self):
        code, _, fake, _ = self.run_tool([b"I (300) main: reflbo ready\r\n"], until="ready", reset=True)
        self.assertEqual(code, 0)
        self.assertFalse(fake.dtr)
        self.assertEqual(fake.rts_history, [True, False])


if __name__ == "__main__":
    unittest.main()
