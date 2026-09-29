#!/usr/bin/env python3
"""Render host-side images to PNG (spec §15) with the renderers built in build-host.
  cmake -S test/host -B build-host -G Ninja && cmake --build build-host
  python3 tools/render.py            # captures/render/<name>.{pbm,png}
"""
import argparse
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pbm_png  # noqa: E402

# Renderers that take a fixture name; `--list` prints their fixtures (test/host/*_fixtures.h).
LISTED = {"dash": "render_dashboard", "screen": "render_screen"}


def renderers(build_dir):
    """name -> the command to run, without the output path that comes last."""
    commands = {"test_pattern": [str(build_dir / "render_test_pattern")]}
    for prefix, exe in LISTED.items():
        path = str(build_dir / exe)
        names = subprocess.run([path, "--list"], check=True, capture_output=True, text=True).stdout.split()
        commands.update({f"{prefix}_{name}": [path, name] for name in names})
    return commands


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build-host")
    parser.add_argument("--out-dir", default="captures/render")
    args = parser.parse_args(argv)
    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, command in renderers(pathlib.Path(args.build_dir)).items():
        pbm = out_dir / f"{name}.pbm"
        subprocess.run([*command, str(pbm)], check=True)
        (out_dir / f"{name}.png").write_bytes(pbm_png.png_from_pbm(pbm.read_bytes()))
        print(f"{name}: {pbm} and {pbm.with_suffix('.png')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
