#!/usr/bin/env python3
"""Capture the reflbo serial console without an interactive terminal.

Agents can't use `idf.py monitor`, because it needs a TTY. This tool reads the
board's USB-Serial-JTAG port for a while. It can reset the board first and run
console commands, and it survives the USB re-enumeration that a reset can cause.

Run it through tools/idf.sh so pyserial is available (paths are relative to the repo root):
  tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/boot.log
  tools/idf.sh exec python tools/devlog.py --cmd version --cmd heap

Exit codes: 0 ok, 2 port problem, 3 console prompt never appeared,
4 --until pattern not seen before the timeout.
"""
import argparse
import glob
import os
import re
import sys
import time

PROMPT = "reflbo> "  # must match DIAG_PROMPT in components/diag/diag_internal.h
PORT_GLOB = "/dev/cu.usbmodem*"
NUDGE_INTERVAL_S = 1.0

_ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
_RESET_BANNER_RE = re.compile(r"rst:0x")  # the ROM prints this on every chip reset


class PortError(Exception):
    """No usable serial port."""


def strip_ansi(text):
    return _ANSI_RE.sub("", text)


def pick_port(candidates):
    ports = sorted(set(candidates))
    if not ports:
        raise PortError(f"no {PORT_GLOB} port found; is the board connected and awake (press KEY)?")
    if len(ports) > 1:
        raise PortError("several ports found, choose one with -p: " + ", ".join(ports))
    return ports[0]


class LineSplitter:
    """Turns a byte stream into text lines and keeps the unfinished tail."""

    def __init__(self):
        self._tail = ""

    def feed(self, data):
        text = self._tail + data.decode("utf-8", errors="replace")
        keep = ""
        if text.endswith("\r"):  # a CRLF may be split across reads
            text, keep = text[:-1], "\r"
        text = text.replace("\r\n", "\n").replace("\r", "\n")
        parts = text.split("\n")
        self._tail = parts.pop() + keep
        return [strip_ansi(part) for part in parts]

    def tail(self):
        return strip_ansi(self._tail.rstrip("\r"))

    def clear_tail(self):
        self._tail = ""


def open_port(port, serial_factory=None):
    """Open the port without resetting the chip.

    The OS asserts DTR and RTS when the port opens, and releasing DTR while RTS is still
    asserted resets a USB-Serial-JTAG chip. So open with both asserted, then release RTS
    before DTR (the same order esp-idf-monitor uses for --no-reset).
    """
    if serial_factory is None:
        import serial  # imported here so the unit tests run without pyserial

        serial_factory = serial.Serial
    ser = serial_factory()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.1
    ser.dtr = True
    ser.rts = True
    ser.open()
    ser.rts = False
    ser.dtr = False
    return ser


def hard_reset(ser, sleep):
    """Reset into the app like esptool's USB hard reset: pulse RTS while DTR is low."""
    ser.dtr = False
    ser.rts = True
    sleep(0.2)
    ser.rts = False


def open_with_retry(port, deadline, opener, now, sleep):
    while True:
        try:
            return opener(port)
        except OSError as err:
            if now() >= deadline:
                raise PortError(f"could not open {port}: {err}") from err
            sleep(0.2)


def run(args, opener=open_port, now=time.monotonic, sleep=time.sleep,
        list_ports=lambda: glob.glob(PORT_GLOB), out=sys.stdout):
    port = args.port or pick_port(list_ports())
    deadline = now() + args.seconds
    until = re.compile(args.until) if args.until else None
    pending = list(args.cmd)
    current = None  # the command whose output is being captured
    echo_seen = False  # the console echoes a command when it starts reading it
    awaiting_prompt = bool(pending)
    # Each nudge makes the console print one more prompt, so nudge only before the first
    # command or after a reset; a nudge queued behind a running command becomes a stale prompt.
    nudge_ok = bool(pending)
    until_seen = until is None
    prompt_line = False  # a prompt arrived with other output glued on, e.g. "reflbo> W (20) app: ..."
    last_nudge = now()
    log = None
    if args.out:
        os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
        log = open(args.out, "w", encoding="utf-8")

    def emit(line):
        nonlocal until_seen, echo_seen, nudge_ok, prompt_line
        out.write(line + "\n")
        out.flush()
        if log:
            log.write(line + "\n")
            log.flush()
        if until and until.search(line):
            until_seen = True
        if current is not None and not echo_seen and line.rstrip().endswith(current):
            echo_seen = True
        elif line.startswith(PROMPT) and (current is None or echo_seen):
            prompt_line = True
        if _RESET_BANNER_RE.search(line):
            nudge_ok = True

    try:
        ser = open_with_retry(port, deadline, opener, now, sleep)
        if args.reset:
            hard_reset(ser, sleep)
        splitter = LineSplitter()
        while now() < deadline:
            try:
                data = ser.read(4096)
            except OSError:  # pyserial's SerialException is an OSError
                try:
                    ser.close()
                except OSError:
                    pass
                ser = open_with_retry(port, deadline, opener, now, sleep)
                continue
            for line in splitter.feed(data):
                emit(line)
            prompt_visible = prompt_line or splitter.tail().rstrip().endswith(PROMPT.rstrip())
            # A prompt ends the current command only after its echo: earlier ones are stale.
            if awaiting_prompt and prompt_visible and (current is None or echo_seen):
                prompt_line = False
                if pending:
                    current = pending.pop(0)
                    echo_seen = False
                    nudge_ok = False
                    ser.write((current + "\r").encode())
                    splitter.clear_tail()
                    last_nudge = now()
                else:
                    awaiting_prompt = False
                    current = None
            elif awaiting_prompt and nudge_ok and now() - last_nudge >= NUDGE_INTERVAL_S:
                ser.write(b"\r")  # ask the console to print a fresh prompt
                last_nudge = now()
            if not awaiting_prompt and not pending and until_seen and (args.cmd or until):
                return 0
        if awaiting_prompt:
            print(f"devlog: console prompt {PROMPT!r} never appeared", file=sys.stderr)
            return 3
        if not until_seen:
            print(f"devlog: pattern {args.until!r} not seen within {args.seconds} s", file=sys.stderr)
            return 4
        return 0
    finally:
        if log:
            log.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--port", help=f"serial port (default: the only {PORT_GLOB})")
    parser.add_argument("-t", "--seconds", type=float, default=10.0,
                        help="give up after this many seconds (default 10)")
    parser.add_argument("-o", "--out", help="also write the captured lines to this file")
    parser.add_argument("--reset", action="store_true", help="reset the board first to capture its boot log")
    parser.add_argument("--cmd", action="append", default=[],
                        help="console command to run; repeatable, runs in order")
    parser.add_argument("--until", help="stop once a line matches this regular expression")
    args = parser.parse_args(argv)
    try:
        return run(args)
    except PortError as err:
        print(f"devlog: {err}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
