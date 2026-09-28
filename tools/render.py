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

# name -> renderer executable in the build directory and its arguments (the output path comes last)
DASHBOARDS = ["home", "indoor", "weather", "focus", "home_invalid", "home_stale", "home_12h_charging", "indoor_cold",
              "focus_seconds", "home_battery_details", "home_inverted"]  # test/host/dashboard_fixtures.h
RENDERERS = {
    "test_pattern": ["render_test_pattern"],
    "clock_valid": ["render_clock", "valid"],
    "clock_invalid": ["render_clock", "invalid"],
    "clock_cold": ["render_clock", "cold"],
    **{f"dash_{name}": ["render_dashboard", name] for name in DASHBOARDS},
}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build-host")
    parser.add_argument("--out-dir", default="captures/render")
    args = parser.parse_args(argv)
    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, command in RENDERERS.items():
        pbm = out_dir / f"{name}.pbm"
        exe = pathlib.Path(args.build_dir) / command[0]
        subprocess.run([str(exe), *command[1:], str(pbm)], check=True)
        (out_dir / f"{name}.png").write_bytes(pbm_png.png_from_pbm(pbm.read_bytes()))
        print(f"{name}: {pbm} and {pbm.with_suffix('.png')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
