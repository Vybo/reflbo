#!/usr/bin/env bash
# Run idf.py, or any command after "exec", inside the ESP-IDF environment.
# Works from any directory and any fresh shell; paths are relative to the repo root.
#
#   tools/idf.sh build
#   tools/idf.sh -p /dev/cu.usbmodem1101 flash
#   tools/idf.sh exec python tools/devlog.py --cmd version
#
# ESP-IDF comes from REFLBO_IDF_PATH, default ~/esp/esp-idf-v5.5.5. An inherited IDF_PATH is
# ignored on purpose, so an older ESP-IDF install can't be picked up by accident.
# ~/esp/python-shim goes first on PATH because Homebrew's Python 3.14 (3.14.6 and 3.14.7
# checked) cannot load pyexpat on macOS 26 (AGENTS.md §6).
set -eo pipefail

IDF_PATH="${REFLBO_IDF_PATH:-$HOME/esp/esp-idf-v5.5.5}"
if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "idf.sh: ESP-IDF not found at $IDF_PATH (see AGENTS.md §6)" >&2
    exit 1
fi
if [[ -d "$HOME/esp/python-shim" ]]; then
    PATH="$HOME/esp/python-shim:$PATH"
fi
export IDF_PATH PATH

# export.sh prints a status banner on every run; keep it out of the output unless it fails.
export_log="$(mktemp -t reflbo-idf-export)"
set +e
# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >"$export_log" 2>&1
rc=$?
set -e
if [[ $rc -ne 0 ]]; then
    cat "$export_log" >&2
    rm -f "$export_log"
    echo "idf.sh: $IDF_PATH/export.sh failed ($rc)" >&2
    exit "$rc"
fi
rm -f "$export_log"

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ "${1:-}" == "exec" ]]; then
    shift
    exec "$@"
fi
exec idf.py "$@"
