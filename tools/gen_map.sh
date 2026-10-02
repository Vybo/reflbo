#!/usr/bin/env bash
# Regenerate assets/map/map.bin from Natural Earth, OurAirports and GeoNames (spec §11.1). Plain Python 3;
# the sources are downloaded once into ref/mapdata/.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
python3 tools/gen_map.py "$@"
