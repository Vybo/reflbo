#!/usr/bin/env bash
# Regenerate components/gfx/fonts from the TTFs in assets/fonts (spec §4.4). Needs uv; the
# pinned Pillow and fontTools come from tools/requirements.txt.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

fontgen() {
    uv run --quiet --python 3.13 --with-requirements tools/requirements.txt tools/fontgen.py "$@"
}

fontgen --ttf assets/fonts/DejaVuSans.ttf --size 12 --charset text --name sans_12 --licence assets/fonts/LICENSE-DejaVu.txt
fontgen --ttf assets/fonts/DejaVuSans.ttf --size 16 --charset text --name sans_16 --licence assets/fonts/LICENSE-DejaVu.txt
fontgen --ttf assets/fonts/DejaVuSans.ttf --size 20 --charset text --name sans_20 --licence assets/fonts/LICENSE-DejaVu.txt
fontgen --ttf assets/fonts/DejaVuSans-Bold.ttf --size 20 --charset text --name sans_bold_20 --licence assets/fonts/LICENSE-DejaVu.txt
