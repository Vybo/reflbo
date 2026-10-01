#!/usr/bin/env bash
# Regenerate components/gfx/icons from the icon font in assets/icons (spec §4.5). Needs uv; the
# pinned Pillow comes from tools/requirements.txt.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p components/gfx/icons

uv run --quiet --python 3.13 --with-requirements tools/requirements.txt tools/imggen.py \
    --ttf assets/icons/MaterialIcons-Regular.ttf --codepoints assets/icons/MaterialIcons-Regular.codepoints \
    --manifest assets/icons/icons.txt --licence assets/icons/LICENSE-MaterialIcons.txt \
    --font wi=assets/icons/WeatherIcons-Regular.ttf,assets/icons/WeatherIcons-Regular.codepoints,assets/icons/LICENSE-WeatherIcons.txt
