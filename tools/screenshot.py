#!/usr/bin/env python3
"""Grab the board's framebuffer over USB as a PNG (spec §15).
  tools/idf.sh exec python tools/screenshot.py -p /dev/cu.usbmodemXXXX -o captures/screen.png \
      [--compare test/host/golden/test_pattern.pbm]
Writes OUT.png and the raw OUT.pbm. Exit codes: as devlog.py, plus 5 when the image differs from
--compare and 6 when no valid image arrived.
"""
import argparse
import base64
import binascii
import io
import pathlib
import sys
from types import SimpleNamespace

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import devlog  # noqa: E402
import pbm_png  # noqa: E402

BEGIN = "-----BEGIN RLCD PBM-----"
END = "-----END RLCD PBM-----"


def extract_pbm(text):
    """Returns the PBM carried between the markers in a console transcript; raises ValueError."""
    lines = [line.strip() for line in text.splitlines()]
    try:
        start = next(i for i, line in enumerate(lines) if line.endswith(BEGIN))
        end = next(i for i in range(start + 1, len(lines)) if lines[i] == END)
    except StopIteration:
        raise ValueError("no complete screenshot in the console output") from None
    try:
        data = base64.b64decode("".join(lines[start + 1:end]), validate=True)
    except binascii.Error as err:
        raise ValueError(f"corrupt screenshot data: {err}") from None
    width, height, raster = pbm_png.parse_pbm(data)
    if len(data) != len(f"P4\n{width} {height}\n") + len(raster):
        raise ValueError("screenshot data has the wrong length")  # e.g. base64-looking noise got in
    return data


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--port", help=f"serial port (default: the only {devlog.PORT_GLOB})")
    parser.add_argument("-o", "--out", default="captures/screen.png", help="PNG to write (the .pbm lands next to it)")
    parser.add_argument("-t", "--seconds", type=float, default=15.0, help="give up after this many seconds")
    parser.add_argument("--compare", help="PBM the screenshot must equal byte for byte")
    args = parser.parse_args(argv)

    transcript = io.StringIO()
    session = SimpleNamespace(port=args.port, seconds=args.seconds, until=None, cmd=["screenshot"], reset=False,
                              out=None)
    try:
        code = devlog.run(session, out=transcript)
    except devlog.PortError as err:
        print(f"screenshot: {err}", file=sys.stderr)
        return 2
    if code != 0:
        return code
    try:
        pbm = extract_pbm(transcript.getvalue())
    except ValueError as err:
        print(f"screenshot: {err}", file=sys.stderr)
        return 6

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.with_suffix(".pbm").write_bytes(pbm)
    out.write_bytes(pbm_png.png_from_pbm(pbm))
    print(f"screenshot: {out}")
    if args.compare:
        if pathlib.Path(args.compare).read_bytes() != pbm:
            print(f"screenshot: differs from {args.compare}", file=sys.stderr)
            return 5
        print(f"screenshot: identical to {args.compare}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
