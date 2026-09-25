# M0: Toolchain and Skeleton, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** At the end of this plan:

- ESP-IDF v5.5.5 installs reproducibly on the owner's Mac.
- A firmware skeleton boots on the board, and its USB console answers `version`, `heap` and `reboot`.
- A host-side test harness exists.
- Agent tools can build, flash and capture logs without an interactive terminal.

**Architecture:**

- **Project layout.** The ESP-IDF project sits at the repo root: `main/` holds only wiring, and features live in `components/`.
- **Console.** The `esp_console` REPL runs on USB-Serial-JTAG inside a `diag` component.
- **Host tests.** CMake plus a vendored copy of Unity compile the IDF-free components natively on macOS.
- **Tools.** Python tools run through `tools/idf.sh`, which provides the IDF environment (Python 3.13 with pyserial) from any shell.

**Tech stack:** ESP-IDF v5.5.5 (C17, CMake/Ninja), Unity v2.6.1, uv-managed Python 3.13 with pyserial from the IDF environment, Apple clang for host tests.

**Spec:** `docs/specs/2026-09-25-firmware-design.md`. Relevant sections: §3.1 (components), §14.1 (partition table), §15 (diagnostics), §17 (testing), §18 (the M0 row). Also `AGENTS.md` §6–§8.

## Global Constraints

- **Framework:** ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`.
- **Languages:** C17 firmware, Python 3 host tools.
- **Host-buildable code:** `[host]` components include no ESP-IDF headers.
- **Code style:** ESP-IDF style, 4-space indent and `snake_case`. Public APIs carry a component prefix (`st7305_…`, `ds_…`) and live in `include/`.
- **Errors:** return `esp_err_t` and use `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`. No `ESP_ERROR_CHECK` on recoverable paths.
- **Logging:** one `TAG` per module. `ESP_LOGI` for state changes, `ESP_LOGD` for detail. Never log secrets.
- **Configuration:** change it through `sdkconfig.defaults`. `sdkconfig` is generated and gitignored. Personal overrides go in the gitignored `sdkconfig.defaults.local`.
- **Partition table:** exactly as spec §14.1 (nvs 0x9000/64 KB, otadata 0x19000/8 KB, phy_init 0x1B000/4 KB, ota_0 0x20000/4 MB, ota_1 0x420000/4 MB, coredump 0x820000/64 KB, storage littlefs 0x830000 to the end of flash).
- **Monitor:** do not run `idf.py monitor` from an agent shell; it needs an interactive TTY.
- **Erasing:** do not run `idf.py erase-flash` or erase NVS without asking.
- **Commits:** small, focused Conventional Commits, in imperative mood; commit only states that build. No AI or assistant attribution anywhere. Pushing to `origin` is allowed; never force-push `main`.
- **Licence:** Apache-2.0. Adapted third-party code keeps its licence header; credit it in `THIRD_PARTY.md`, and in `NOTICE` where its licence requires.

## Review Focus

1. **Zero or several `/dev/cu.usbmodem*` ports** (board unplugged or asleep, or a second dev board attached). The tools must refuse to guess and print a clear message. They must never pick a port at random. Pinned by `PickPortTest` in Task 4.
2. **The port vanishes during a reset or USB re-enumeration.** `devlog.py` must reconnect and keep capturing, not crash. Pinned by `test_reconnects_after_usb_disconnect` in Task 4.
3. **A command is sent before the console is ready, or no fresh prompt is on screen.** `devlog.py` must nudge for a prompt and wait, and exit with code 3 if the console never answers. Pinned by `test_nudges_for_a_prompt_when_none_is_visible` and `test_prompt_never_appearing_returns_code_3` in Task 4.
4. **A fresh shell, in a different directory, where the default `python3` is Homebrew's broken 3.14.6.** `tools/idf.sh` must still work, using the Python 3.13 environment. Pinned by the Task 2 checks, which run from `/`.
5. **Factory-firmware data left in flash regions our partition table reuses** (NVS, otadata, coredump). The boot must show no `E (` error lines. If it does, erase the region, but only with the owner's approval. Pinned by the Task 5 boot-log check.

## File map

| Path | Responsibility | Task |
|---|---|---|
| `LICENSE`, `NOTICE`, `THIRD_PARTY.md` | Licence text, attribution notice, third-party register | 1 |
| `tools/idf.sh` | Run `idf.py` or any command inside the IDF environment, from any directory | 2 |
| `test/host/CMakeLists.txt` | Host test build: Unity, components under test, one executable per test | 3, 4 |
| `test/host/third_party/unity/*` | Vendored Unity v2.6.1 (MIT) | 3 |
| `test/host/test_util_crc32.c` | CRC-32 unit tests | 3 |
| `components/util/{CMakeLists.txt,include/util_crc32.h,util_crc32.c}` | Pure-C CRC-32 shared by firmware and host | 3 |
| `tools/devlog.py` | Non-interactive serial capture and console driver | 4 |
| `tools/tests/{__init__.py,test_devlog.py}` | `devlog.py` unit tests (stdlib `unittest`, no pyserial needed) | 4 |
| `CMakeLists.txt`, `version.txt`, `sdkconfig.defaults`, `partitions.csv` | ESP-IDF project definition | 5 |
| `main/{CMakeLists.txt,main.c}` | `app_main`: start-up and wiring only | 5, 6 |
| `components/diag/{CMakeLists.txt,include/diag.h,diag_internal.h,diag.c,diag_cmd_system.c}` | USB console REPL and system commands | 6 |
| `README.md` | Public project front page | 7 |
| `AGENTS.md` | Updated as each command lands | 2–7 |

---

### Task 1: Licence files

**Files:**
- Create: `LICENSE`, `NOTICE`, `THIRD_PARTY.md`

**Interfaces:**
- Produces: `THIRD_PARTY.md`, with a table that later tasks add rows to (columns: Item | Source | Licence | Where | Notes).

- [ ] **Step 1: Fetch the Apache-2.0 text**

```bash
curl -fsSL https://www.apache.org/licenses/LICENSE-2.0.txt -o LICENSE
```

- [ ] **Step 2: Verify it**

Run: `head -4 LICENSE | sed 's/^ *//' && grep -c "END OF TERMS AND CONDITIONS" LICENSE`
Expected: the lines `Apache License` and `Version 2.0, January 2004`, then `1`.

- [ ] **Step 3: Write `NOTICE`**

```text
reflbo
Copyright 2026 Dan Vybiral

This product includes third-party components. Their authors and licences are
listed in THIRD_PARTY.md.
```

- [ ] **Step 4: Write `THIRD_PARTY.md`**

```markdown
# Third-party material

Code, fonts and other assets in this repository that come from other projects.
Each item keeps its original licence, and copied or adapted files keep their
licence headers. Add a row whenever you copy or adapt anything.

| Item | Source | Licence | Where | Notes |
|---|---|---|---|---|
```

- [ ] **Step 5: Commit and push**

```bash
git add LICENSE NOTICE THIRD_PARTY.md
git commit -m "chore: add Apache-2.0 licence, NOTICE and third-party register"
git push
```

---

### Task 2: Toolchain (Python shim, ESP-IDF v5.5.5, `tools/idf.sh`)

Homebrew's Python 3.14.6 on this Mac cannot load `pyexpat`: `Symbol not found: _XML_SetAllocTrackerActivationThreshold`, because it links against macOS 26.2's older libexpat. pip and the ESP-IDF installer both break as a result. uv's CPython 3.13 is already installed and works, so ESP-IDF gets it through a shim directory that goes first on `PATH`. ESP-IDF's scripts look up `python3` on `PATH`, and they name the Python environment after that interpreter's version, so both `install.sh` and `export.sh` must see the shim.

The downloads and installs write to `~/esp` and `~/.espressif`. If the sandbox blocks those writes, re-run the command with the sandbox disabled; the owner will be asked to approve.

**Files:**
- Create: `tools/idf.sh`
- Modify: `AGENTS.md` §6 (replace the whole section)

**Interfaces:**
- Produces:
  - `tools/idf.sh <idf.py args…>`: runs `idf.py` from the repo root.
  - `tools/idf.sh exec <command…>`: runs any command inside the IDF environment, from the repo root.
  - Every later task invokes ESP-IDF through these two forms.

- [ ] **Step 1: Confirm the prerequisites**

Run: `brew list --versions cmake ninja dfu-util ccache && ~/.local/bin/uv python find 3.13`
Expected: four version lines, then a path ending in `python3.13`.

- [ ] **Step 2: Create the Python shim and check it**

```bash
PY313="$(~/.local/bin/uv python find 3.13)"
mkdir -p ~/esp/python-shim
ln -sf "$PY313" ~/esp/python-shim/python3
ln -sf "$PY313" ~/esp/python-shim/python
~/esp/python-shim/python3 -c "import sys, pyexpat, ssl; print(sys.version.split()[0])"
```

Expected: `3.13.x` with no traceback.

- [ ] **Step 3: Clone ESP-IDF v5.5.5**

Run it in the background, because it takes several minutes:

```bash
git clone -b v5.5.5 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.5
```

Then check: `git -C ~/esp/esp-idf-v5.5.5 describe --tags`
Expected: `v5.5.5`

- [ ] **Step 4: Install the esp32s3 tools and the Python environment**

Run it in the background, because it takes about 10 minutes:

```bash
PATH="$HOME/esp/python-shim:$PATH" ~/esp/esp-idf-v5.5.5/install.sh esp32s3
```

Expected: the output ends with `All done!`. A directory `~/.espressif/python_env/idf5.5_py3.13_env` exists.

- [ ] **Step 5: Write `tools/idf.sh`**

```bash
#!/usr/bin/env bash
# Run idf.py, or any command after "exec", inside the ESP-IDF environment.
# Works from any directory and any fresh shell; paths are relative to the repo root.
#
#   tools/idf.sh build
#   tools/idf.sh -p /dev/cu.usbmodem1101 flash
#   tools/idf.sh exec python tools/devlog.py --cmd version
#
# IDF_PATH defaults to ~/esp/esp-idf-v5.5.5. ~/esp/python-shim goes first on PATH because
# Homebrew's Python 3.14.6 cannot load pyexpat on macOS 26 (AGENTS.md §6).
set -eo pipefail

IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.5}"
if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "idf.sh: ESP-IDF not found at $IDF_PATH (see AGENTS.md §6)" >&2
    exit 1
fi
if [[ -d "$HOME/esp/python-shim" ]]; then
    PATH="$HOME/esp/python-shim:$PATH"
fi
export IDF_PATH PATH

set +e
# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >/dev/null
rc=$?
set -e
if [[ $rc -ne 0 ]]; then
    echo "idf.sh: $IDF_PATH/export.sh failed ($rc)" >&2
    exit "$rc"
fi

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ "${1:-}" == "exec" ]]; then
    shift
    exec "$@"
fi
exec idf.py "$@"
```

Then: `chmod +x tools/idf.sh`

- [ ] **Step 6: Verify from another directory in a fresh shell**

```bash
cd / && /Users/dan.vybiral/reflbo/tools/idf.sh --version
cd / && /Users/dan.vybiral/reflbo/tools/idf.sh exec python -c "import sys, serial, pyexpat; print(sys.version.split()[0], serial.VERSION)"
cd / && /Users/dan.vybiral/reflbo/tools/idf.sh exec pwd
```

Expected:
- `ESP-IDF v5.5.5`
- `3.13.x 3.5` (or another pyserial 3.x)
- `/Users/dan.vybiral/reflbo`

- [ ] **Step 7: Replace `AGENTS.md` §6**

Replace everything from `## 6. Environment and commands` up to (not including) `## 7. Verification` with:

````markdown
## 6. Environment and commands

One-time setup on macOS:

```sh
brew install cmake ninja dfu-util ccache   # already present on the owner's Mac
# Homebrew's Python 3.14.6 can't load pyexpat on macOS 26 (it expects a newer
# libexpat than the system's), so pip, and the ESP-IDF installer with it, fail.
# ESP-IDF uses uv's Python 3.13 through a shim directory instead:
mkdir -p ~/esp/python-shim
ln -sf "$(~/.local/bin/uv python find 3.13)" ~/esp/python-shim/python3
ln -sf "$(~/.local/bin/uv python find 3.13)" ~/esp/python-shim/python
git clone -b v5.5.5 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.5
PATH="$HOME/esp/python-shim:$PATH" ~/esp/esp-idf-v5.5.5/install.sh esp32s3
```

`tools/idf.sh` runs `idf.py`, or any command after `exec`, inside the IDF environment. Before that it puts the shim on `PATH`, loads `export.sh` and changes to the repo root. It therefore works from any fresh shell, which matters for agents because every Bash call is one. Paths passed to it are relative to the repo root.

Build, flash and observe:

```sh
tools/idf.sh set-target esp32s3            # once per clone (planned, M0 Task 5)
tools/idf.sh build                         # (planned, M0 Task 5)
ls /dev/cu.usbmodem*                       # the board's USB-Serial-JTAG port
tools/idf.sh -p /dev/cu.usbmodemXXXX flash # (planned, M0 Task 5)
tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/boot.log   # (planned, M0 Task 4)
tools/idf.sh exec python tools/devlog.py --cmd version --cmd heap                                   # (planned, M0 Task 4)
tools/idf.sh exec python tools/screenshot.py -o captures/screen.png                                 # (planned, M1)
cmake -S test/host -B build-host -G Ninja && cmake --build build-host \
  && ctest --test-dir build-host --output-on-failure                                                # (planned, M0 Task 3)
```

- `devlog.py` picks the port itself when exactly one `/dev/cu.usbmodem*` exists; otherwise pass `-p`. Exit codes: 0 ok, 2 port problem, 3 console prompt never appeared, 4 `--until` not seen in time.
- Do not run `idf.py monitor` from an agent shell; it needs an interactive TTY. Use `devlog.py`.
- Do not run `idf.py erase-flash` or erase NVS without asking. Either wipes the owner's Wi-Fi credentials and presets.
- If the port is missing, the board is probably in deep sleep. Press KEY. If it is still missing, ask the owner to enter download mode (hold BOOT while powering on).
- Power measurements follow the USB power-meter method in spec §9.4. Record them in `docs/power.md`.

````

- [ ] **Step 8: Commit and push**

```bash
git add tools/idf.sh AGENTS.md
git commit -m "build: add tools/idf.sh and document ESP-IDF setup with uv Python 3.13"
git push
```

---

### Task 3: Host test harness and `util_crc32`

`util_crc32` is the first host-buildable code. M1 uses it for frame change detection (spec §4.1) and M3 for datastore snapshots (spec §6).

**Files:**
- Create: `test/host/CMakeLists.txt`, `test/host/test_util_crc32.c`, `test/host/third_party/unity/{unity.c,unity.h,unity_internals.h,LICENSE.txt}`
- Create: `components/util/CMakeLists.txt`, `components/util/include/util_crc32.h`, `components/util/util_crc32.c`
- Modify: `THIRD_PARTY.md` (add the Unity row), `AGENTS.md` §6 (drop the Task 3 marker)

**Interfaces:**
- Produces:
  - `uint32_t util_crc32(uint32_t crc, const void *data, size_t len);` in `util_crc32.h`. It is zlib-compatible: start with `crc = 0` and chain calls to continue.
  - The host-test convention: `reflbo_host_test(<name> <libs…>)` in `test/host/CMakeLists.txt` builds `<name>.c` against Unity and registers it with ctest.

- [ ] **Step 1: Vendor Unity v2.6.1**

```bash
mkdir -p test/host/third_party/unity
for f in src/unity.c src/unity.h src/unity_internals.h LICENSE.txt; do
    curl -fsSL "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.1/$f" \
        -o "test/host/third_party/unity/$(basename "$f")"
done
ls test/host/third_party/unity
```

Expected: `LICENSE.txt  unity.c  unity.h  unity_internals.h`

- [ ] **Step 2: Write `test/host/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(reflbo_host_tests C)

# Host-side tests for the IDF-free components (spec §17). From the repo root:
#   cmake -S test/host -B build-host -G Ninja && cmake --build build-host \
#     && ctest --test-dir build-host --output-on-failure

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(REFLBO_WARNINGS -Wall -Wextra -Werror)

enable_testing()

add_library(unity STATIC third_party/unity/unity.c)
target_include_directories(unity PUBLIC third_party/unity)

# Components under test: the same sources the firmware compiles.
add_library(util STATIC ${REPO_ROOT}/components/util/util_crc32.c)
target_include_directories(util PUBLIC ${REPO_ROOT}/components/util/include)
target_compile_options(util PRIVATE ${REFLBO_WARNINGS})

# reflbo_host_test(<name> <libs...>): builds <name>.c against Unity and registers it with ctest.
function(reflbo_host_test name)
    add_executable(${name} ${name}.c)
    target_compile_options(${name} PRIVATE ${REFLBO_WARNINGS})
    target_link_libraries(${name} PRIVATE unity ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

reflbo_host_test(test_util_crc32 util)
```

- [ ] **Step 3: Write the failing test `test/host/test_util_crc32.c`**

```c
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "util_crc32.h"

void setUp(void) {}
void tearDown(void) {}

static void test_standard_check_value(void)
{
    const char *s = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, util_crc32(0, s, strlen(s)));
}

static void test_empty_input_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, util_crc32(0, NULL, 0));
}

static void test_single_byte(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xE8B7BE43u, util_crc32(0, "a", 1));
}

static void test_chained_calls_match_one_pass(void)
{
    const char *s = "123456789";
    uint32_t first = util_crc32(0, s, 4);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, util_crc32(first, s + 4, 5));
}

static void test_frame_sized_buffers(void)
{
    static uint8_t frame[15000]; /* one 400x300 1-bpp frame */
    memset(frame, 0xFF, sizeof(frame));
    TEST_ASSERT_EQUAL_HEX32(0x3C029422u, util_crc32(0, frame, sizeof(frame)));
    memset(frame, 0x00, sizeof(frame));
    TEST_ASSERT_EQUAL_HEX32(0xD17AFDBEu, util_crc32(0, frame, sizeof(frame)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_standard_check_value);
    RUN_TEST(test_empty_input_is_zero);
    RUN_TEST(test_single_byte);
    RUN_TEST(test_chained_calls_match_one_pass);
    RUN_TEST(test_frame_sized_buffers);
    return UNITY_END();
}
```

(The expected values come from Python `zlib.crc32`.)

- [ ] **Step 4: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja`
Expected: FAIL, because CMake cannot find `components/util/util_crc32.c`.

- [ ] **Step 5: Write the interface `components/util/include/util_crc32.h`**

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320), compatible with zlib's crc32().
 * Start with crc = 0 and pass the previous result to continue over more data:
 *   util_crc32(util_crc32(0, a, na), b, nb) == util_crc32(0, a||b, na + nb)
 * Pure C with no ESP-IDF dependency, so it also builds on the host.
 */
uint32_t util_crc32(uint32_t crc, const void *data, size_t len);
```

- [ ] **Step 6: Write the implementation `components/util/util_crc32.c`**

```c
#include "util_crc32.h"

/* 16-entry table, two table steps per byte: small in flash, fast enough for a 15 000-byte frame. */
static const uint32_t s_nibble_table[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
};

uint32_t util_crc32(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = data;

    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ s_nibble_table[crc & 0x0Fu];
        crc = (crc >> 4) ^ s_nibble_table[crc & 0x0Fu];
    }
    return ~crc;
}
```

- [ ] **Step 7: Write the IDF component file `components/util/CMakeLists.txt`**

```cmake
# Small pure-C helpers shared by the firmware and the host tests (no ESP-IDF headers).
idf_component_register(SRCS "util_crc32.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 8: Run the tests and confirm they pass**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`, with no compiler warnings.

- [ ] **Step 9: Register Unity in `THIRD_PARTY.md`**

Append this row to the table:

```markdown
| Unity v2.6.1 | https://github.com/ThrowTheSwitch/Unity | MIT (`test/host/third_party/unity/LICENSE.txt`) | `test/host/third_party/unity/` | Host unit-test framework; unmodified |
```

- [ ] **Step 10: Drop the Task 3 marker in `AGENTS.md` §6**

On the `ctest` line, delete `# (planned, M0 Task 3)` and the spaces before it.

- [ ] **Step 11: Commit and push**

```bash
git add test/host components/util THIRD_PARTY.md AGENTS.md
git commit -m "test: add host test harness with Unity and util_crc32"
git push
```

---

### Task 4: `tools/devlog.py`, the non-interactive serial capture

**Files:**
- Create: `tools/devlog.py`, `tools/tests/__init__.py` (empty), `tools/tests/test_devlog.py`
- Modify: `test/host/CMakeLists.txt` (run the tool tests under ctest), `AGENTS.md` §6 (drop the Task 4 markers)

**Interfaces:**
- Consumes: the `tools/idf.sh exec` form (Task 2), which supplies pyserial.
- Produces:
  - CLI: `tools/idf.sh exec python tools/devlog.py [-p PORT] [-t SECONDS] [-o FILE] [--reset] [--cmd CMD]... [--until REGEX]`.
  - Exit codes: 0 ok, 2 port problem, 3 prompt never appeared, 4 `--until` not seen.
  - `PROMPT = "reflbo> "`, which must equal `DIAG_PROMPT` in Task 6.

- [ ] **Step 1: Write the failing tests `tools/tests/test_devlog.py`**

Also create an empty `tools/tests/__init__.py`.

```python
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
```

- [ ] **Step 2: Hook the tool tests into ctest**

Append to `test/host/CMakeLists.txt`:

```cmake
# Python tool tests (stdlib unittest; pyserial is not needed).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME tools_unittests
         COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -t . -v
         WORKING_DIRECTORY ${REPO_ROOT}/tools)
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `tools_unittests` FAILS with `ModuleNotFoundError: No module named 'devlog'`. `test_util_crc32` still passes.

- [ ] **Step 4: Write `tools/devlog.py`**

```python
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


def open_port(port):
    """Open without toggling DTR/RTS, so opening the port never resets the chip."""
    import serial  # imported here so the unit tests run without pyserial

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.1
    ser.dtr = False
    ser.rts = False
    ser.open()
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
    awaiting_prompt = bool(pending)
    until_seen = until is None
    last_nudge = now()
    log = None
    if args.out:
        os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
        log = open(args.out, "w", encoding="utf-8")

    def emit(line):
        nonlocal until_seen
        out.write(line + "\n")
        out.flush()
        if log:
            log.write(line + "\n")
            log.flush()
        if until and until.search(line):
            until_seen = True

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
            prompt_visible = splitter.tail().rstrip().endswith(PROMPT.rstrip())
            if awaiting_prompt and prompt_visible:
                if pending:
                    ser.write((pending.pop(0) + "\r").encode())
                    splitter.clear_tail()
                    last_nudge = now()
                else:
                    awaiting_prompt = False
            elif awaiting_prompt and now() - last_nudge >= NUDGE_INTERVAL_S:
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
```

Then: `chmod +x tools/devlog.py`

- [ ] **Step 5: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 6: Check the CLI without a board**

Run: `tools/idf.sh exec python tools/devlog.py -t 1; echo "exit=$?"`
Expected with no board attached: `devlog: no /dev/cu.usbmodem* port found; …` and `exit=2`. With exactly one board attached, it captures for 1 s instead and prints `exit=0`.

- [ ] **Step 7: Drop the Task 4 markers in `AGENTS.md` §6**

On both `devlog.py` lines, delete `# (planned, M0 Task 4)` and the spaces before it.

- [ ] **Step 8: Commit and push**

```bash
git add tools/devlog.py tools/tests test/host/CMakeLists.txt AGENTS.md
git commit -m "feat(tools): add devlog.py for non-interactive serial capture"
git push
```

---

### Task 5: Firmware skeleton and first flash

**Files:**
- Create: `CMakeLists.txt`, `version.txt`, `sdkconfig.defaults`, `partitions.csv`, `main/CMakeLists.txt`, `main/main.c`
- Modify: `AGENTS.md` §6 (drop the Task 5 markers)

**Interfaces:**
- Consumes: `tools/idf.sh` (Task 2), `devlog.py` (Task 4), and the `util` component (Task 3, which now also compiles for the chip).
- Produces:
  - The log line `I (…) main: reflbo ready`, printed when start-up is complete. Tools and later milestones rely on it.
  - The `reflbo 0.1.0-dev starting` line.

- [ ] **Step 1: Write the project `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)

# Personal overrides (e.g. dev Wi-Fi) go in the gitignored sdkconfig.defaults.local.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reflbo)
```

- [ ] **Step 2: Write `version.txt`**

```text
0.1.0-dev
```

- [ ] **Step 3: Write `sdkconfig.defaults`**

```text
# reflbo defaults. Edit here, not in sdkconfig; personal overrides go in sdkconfig.defaults.local.
CONFIG_IDF_TARGET="esp32s3"

# Flash: 16 MB QIO (ESP32-S3-WROOM-1-N16R8)
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# Partition table (spec §14.1)
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# 8 MB octal PSRAM; skip the boot-time memory test for a faster boot (spec §5.1)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_MEMTEST=n

# Console and logs on the USB-Serial-JTAG port
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y

# Core dumps go to the coredump partition (spec §16)
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
```

- [ ] **Step 4: Write `partitions.csv`**

```text
# reflbo partition table for 16 MB flash (spec §14.1).
# storage has no image, so idf.py flash never overwrites user data in it.
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   0x10000
otadata,  data, ota,      0x19000,  0x2000
phy_init, data, phy,      0x1b000,  0x1000
ota_0,    app,  ota_0,    0x20000,  0x400000
ota_1,    app,  ota_1,    0x420000, 0x400000
coredump, data, coredump, 0x820000, 0x10000
storage,  data, littlefs, 0x830000, 0x7d0000
```

- [ ] **Step 5: Write `main/CMakeLists.txt` and `main/main.c`**

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_app_format)
```

```c
#include "esp_app_desc.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);
    ESP_LOGI(TAG, "reflbo ready");
}
```

- [ ] **Step 6: Set the target and build**

```bash
mkdir -p captures
tools/idf.sh set-target esp32s3
tools/idf.sh build 2>&1 | tee captures/build.log | tail -3
grep "warning:" captures/build.log | grep -E "/reflbo/(main|components)/" ; echo "own-warnings-exit=$?"
```

Expected:
- The build ends with `Project build complete.`
- The grep prints nothing and `own-warnings-exit=1`, meaning our code has no warnings.

If `dependencies.lock` appears, it gets committed in Step 13.

- [ ] **Step 7: Check the partition table and the image size**

```bash
tools/idf.sh partition-table | grep -E "^(nvs|otadata|phy_init|ota_0|ota_1|coredump|storage),"
tools/idf.sh size | grep -i "total image size"
```

Expected, exactly these seven lines (sizes as printed by `gen_esp32part.py`):

```text
nvs,data,nvs,0x9000,64K,
otadata,data,ota,0x19000,8K,
phy_init,data,phy,0x1b000,4K,
ota_0,app,ota_0,0x20000,4M,
ota_1,app,ota_1,0x420000,4M,
coredump,data,coredump,0x820000,64K,
storage,data,littlefs,0x830000,8000K,
```

The total image size must be far below 4 MB (a skeleton is well under 1 MB).

- [ ] **Step 8: Get the board connected**

Ask the owner to connect the board to the Mac with USB-C. If nothing enumerates, they short-press PWR. Then run: `ls /dev/cu.usbmodem*`
Expected: exactly one port. Use it as `PORT` below.

- [ ] **Step 9: Ask the owner about a one-time full erase**

Use AskUserQuestion. Recommended answer: yes.

- **Why erase:** our partition table reuses flash regions that still hold factory-firmware data.
- **What is lost:** the factory firmware and its data. It can be restored at any time from `ref/waveshare/03_Firmware/01_Factory_V1.bin`.

If the owner agrees, run `tools/idf.sh -p PORT erase-flash`.
Expected: `Chip erase completed successfully`.

- [ ] **Step 10: Flash**

Run: `tools/idf.sh -p PORT flash`
Expected: the output ends with `Hard resetting via RTS pin...` and `Done`.

If it fails with `No serial data received`, ask the owner to enter download mode (hold BOOT while powering on), then retry.

- [ ] **Step 11: Capture the boot log**

Run: `tools/idf.sh exec python tools/devlog.py -p PORT --reset --until "reflbo ready" -t 20 -o captures/boot.log; echo "exit=$?"`
Expected: `exit=0`, and the output contains `main: reflbo 0.1.0-dev starting` and `main: reflbo ready`.

- [ ] **Step 12: Check the boot log for errors and PSRAM**

```bash
grep -E "^E \(" captures/boot.log ; echo "error-lines-exit=$?"
grep -i "psram" captures/boot.log
```

Expected:
- `error-lines-exit=1`, meaning no error lines.
- PSRAM lines mention an 8 MB device (e.g. `Found 8MB PSRAM device`, and `8192K` added to the heap).

If there are error lines, stop and show them to the owner. With the owner's approval, erase only the partition the errors name, then repeat Steps 10–12:

| Errors mention | Command |
|---|---|
| nvs | `tools/idf.sh exec esptool.py -p PORT erase_region 0x9000 0x10000` |
| otadata / boot partition | `tools/idf.sh -p PORT erase-otadata` |
| core dump | `tools/idf.sh exec esptool.py -p PORT erase_region 0x820000 0x10000` |

- [ ] **Step 13: Drop the Task 5 markers, commit and push**

In `AGENTS.md` §6:
- On the `set-target` line, delete ` (planned, M0 Task 5)` and keep `# once per clone`.
- On the `build` and `flash` lines, delete `# (planned, M0 Task 5)` and the spaces before it.

```bash
git add CMakeLists.txt version.txt sdkconfig.defaults partitions.csv main AGENTS.md
git add dependencies.lock 2>/dev/null || true
git commit -m "feat: add ESP-IDF project skeleton with 16 MB partition table"
git push
```

---

### Task 6: `diag` console (`version`, `heap`, `reboot`)

**Files:**
- Create: `components/diag/CMakeLists.txt`, `components/diag/include/diag.h`, `components/diag/diag_internal.h`, `components/diag/diag.c`, `components/diag/diag_cmd_system.c`
- Modify: `main/main.c`, `main/CMakeLists.txt`, `AGENTS.md` §7

**Interfaces:**
- Consumes: the `reflbo ready` convention from Task 5, and `devlog.py`'s `PROMPT = "reflbo> "` from Task 4.
- Produces:
  - `esp_err_t diag_start(void);` in `diag.h`.
  - `#define DIAG_PROMPT "reflbo> "` in `diag_internal.h`.
  - `esp_err_t diag_register_system_commands(void);` (internal).
  - Later milestones add their command groups as further `diag_register_*()` calls inside `diag_start()`.

- [ ] **Step 1: Run the device check first and watch it fail**

Run: `tools/idf.sh exec python tools/devlog.py -p PORT --cmd version -t 6; echo "exit=$?"`
Expected: `exit=3` (`console prompt 'reflbo> ' never appeared`), because the Task 5 firmware has no console yet.

- [ ] **Step 2: Write `components/diag/include/diag.h`**

```c
#pragma once

#include "esp_err.h"

/**
 * Start the USB-Serial-JTAG console REPL and register the diagnostic commands (spec §15).
 * Call once from app_main after the rest of the system is up.
 */
esp_err_t diag_start(void);
```

- [ ] **Step 3: Write `components/diag/diag_internal.h`**

```c
#pragma once

#include "esp_err.h"

/* tools/devlog.py waits for exactly this prompt. Keep the two in sync. */
#define DIAG_PROMPT "reflbo> "

/* Registers version, heap and reboot. */
esp_err_t diag_register_system_commands(void);
```

- [ ] **Step 4: Write `components/diag/diag.c`**

```c
#include "diag.h"

#include "diag_internal.h"
#include "esp_check.h"
#include "esp_console.h"

static const char *TAG = "diag";

esp_err_t diag_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = DIAG_PROMPT;
    repl_config.max_cmdline_length = 256;

    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl),
                        TAG, "console REPL init failed");
    ESP_RETURN_ON_ERROR(esp_console_register_help_command(), TAG, "help command");
    ESP_RETURN_ON_ERROR(diag_register_system_commands(), TAG, "system commands");
    ESP_RETURN_ON_ERROR(esp_console_start_repl(repl), TAG, "console REPL start failed");
    ESP_LOGI(TAG, "console ready on USB-Serial-JTAG");
    return ESP_OK;
}
```

- [ ] **Step 5: Write `components/diag/diag_cmd_system.c`**

```c
#include <inttypes.h>
#include <stdio.h>

#include "diag_internal.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static int cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const esp_app_desc_t *app = esp_app_get_description();
    char elf_sha[17];
    esp_app_get_elf_sha256(elf_sha, sizeof(elf_sha));

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }
    size_t psram_size = esp_psram_is_initialized() ? esp_psram_get_size() : 0;

    printf("%s %s\n", app->project_name, app->version);
    printf("built %s %s, esp-idf %s, elf %s\n", app->date, app->time, app->idf_ver, elf_sha);
    printf("chip %s rev v%d.%d, %d cores, flash %" PRIu32 " MB, psram %u MB\n",
           CONFIG_IDF_TARGET, chip.revision / 100, chip.revision % 100, chip.cores,
           flash_size / (1024 * 1024), (unsigned)(psram_size / (1024 * 1024)));
    return 0;
}

static void print_heap(const char *name, uint32_t caps)
{
    printf("%-8s free %8u  min %8u  largest %8u\n", name,
           (unsigned)heap_caps_get_free_size(caps),
           (unsigned)heap_caps_get_minimum_free_size(caps),
           (unsigned)heap_caps_get_largest_free_block(caps));
}

static int cmd_heap(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    print_heap("internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    print_heap("dma", MALLOC_CAP_DMA);
    print_heap("psram", MALLOC_CAP_SPIRAM);
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("rebooting\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

esp_err_t diag_register_system_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "version", .help = "Show firmware, ESP-IDF and chip information", .func = &cmd_version },
        { .command = "heap", .help = "Show free heap per memory type", .func = &cmd_heap },
        { .command = "reboot", .help = "Restart the chip", .func = &cmd_reboot },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
```

- [ ] **Step 6: Write `components/diag/CMakeLists.txt`**

```cmake
idf_component_register(SRCS "diag.c" "diag_cmd_system.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES console esp_app_format esp_psram spi_flash)
```

- [ ] **Step 7: Wire it into `main`**

Replace `main/main.c` with:

```c
#include "diag.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);

    esp_err_t err = diag_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "reflbo ready");
}
```

Replace `main/CMakeLists.txt` with:

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES diag esp_app_format)
```

- [ ] **Step 8: Build with no warnings in our code, then flash**

```bash
tools/idf.sh build 2>&1 | tee captures/build.log | tail -3
grep "warning:" captures/build.log | grep -E "/reflbo/(main|components)/" ; echo "own-warnings-exit=$?"
tools/idf.sh -p PORT flash
```

Expected: `Project build complete.`, `own-warnings-exit=1`, then flashing ends with `Done`.

- [ ] **Step 9: Run the device check again and watch it pass**

Run: `tools/idf.sh exec python tools/devlog.py -p PORT --cmd version --cmd heap -t 15 -o captures/console.txt; echo "exit=$?"`
Expected: `exit=0`. The output contains:
- `reflbo 0.1.0-dev`
- `esp-idf v5.5.5`
- `chip esp32s3 rev v…, 2 cores, flash 16 MB, psram 8 MB`
- A `psram` heap line whose `free` value is above 7 000 000.

- [ ] **Step 10: Check `help` and `reboot`**

```bash
tools/idf.sh exec python tools/devlog.py -p PORT --cmd help -t 10 | grep -E "^(version|heap|reboot)"
tools/idf.sh exec python tools/devlog.py -p PORT --cmd reboot --until "reflbo ready" -t 25; echo "exit=$?"
```

Expected: the three command names are listed. Then `exit=0`: the board restarts, logs `reflbo ready`, and gives a prompt again.

- [ ] **Step 11: Update `AGENTS.md` §7**

Replace the paragraph that starts `**Diagnostics console** *(planned, `diag`; full list in spec §15)*:` with:

```markdown
**Diagnostics console** (`diag`; full list in spec §15). Available now: `help`, `version`, `heap`, `reboot`. Planned: `screenshot`, `btn <key|boot> <short|double|long>` (simulated presses), `sensors`, `battery`, `rtc get|set`, `field list|get|set`, `preset list|set`, `wifi status|scan`, `sync now`, `sleep stats`, `power idle <deep|light>`, `audio tone`. Drive the UI with `btn` and `screenshot` instead of asking the owner to press buttons. Inject test data with `field set`. Run commands with `tools/idf.sh exec python tools/devlog.py --cmd <command>`.
```

- [ ] **Step 12: Commit and push**

```bash
git add components/diag main AGENTS.md
git commit -m "feat(diag): add USB console with version, heap and reboot"
git push
```

---

### Task 7: README, M0 wrap-up and the M0 discussion point

**Files:**
- Create: `README.md`
- Modify: `AGENTS.md` §2 and §5.2

**Interfaces:**
- Consumes: every earlier task. The README describes only commands that exist now.

- [ ] **Step 1: Write `README.md`**

````markdown
# reflbo

Firmware for the Waveshare [ESP32-S3-RLCD-4.2](https://docs.waveshare.com/ESP32-S3-RLCD-4.2): a battery-powered desk display with watch-face-like dashboards (time, indoor climate, weather, sun times, Home Assistant values), alarms and internet radio.

**Status:** early development. See the roadmap in [AGENTS.md](AGENTS.md) and the design in [docs/specs](docs/specs/2026-09-25-firmware-design.md).

## Build and flash

Requires ESP-IDF v5.5.5. Setup is described in [AGENTS.md §6](AGENTS.md).

```sh
tools/idf.sh set-target esp32s3   # once per clone
tools/idf.sh build
tools/idf.sh -p /dev/cu.usbmodemXXXX flash
tools/idf.sh exec python tools/devlog.py --cmd version
```

Host tests:

```sh
cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host
```

## Licence

Apache-2.0. See [LICENSE](LICENSE), [NOTICE](NOTICE) and [THIRD_PARTY.md](THIRD_PARTY.md).
````

- [ ] **Step 2: Update `AGENTS.md` §2**

Replace the bullet `- **There is no firmware code yet.**` with:

```markdown
- **Status:** M0 is done: toolchain, skeleton, USB console, host tests and `devlog.py`. Next is M1, whose plan gets written before it starts.
```

- [ ] **Step 3: Update `AGENTS.md` §5.2**

In the component tree, add these two lines directly under `components/`:

```text
  util/          small pure-C helpers (CRC-32)                        [host]
```

and, under the `tools/` line:

```text
tools/tests/     unit tests for the host tools (run by ctest)
```

- [ ] **Step 4: Run the full verification**

```bash
cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure
tools/idf.sh build 2>&1 | tail -1
tools/idf.sh exec python tools/devlog.py --cmd version -t 10; echo "exit=$?"
```

Expected: `100% tests passed … out of 2`, `Project build complete.`, and `exit=0` with the version output. This covers the spec §18 M0 acceptance: clean build (level 1), host tests pass (level 2), console answers (level 3).

- [ ] **Step 5: Commit and push**

```bash
git add README.md AGENTS.md
git commit -m "docs: add README and mark M0 done"
git push
```

- [ ] **Step 6: Raise the M0 deferred proposal (spec §19)**

Ask the owner whether to add GitHub Actions CI (firmware build and host tests on every push). Build it only if they agree; it would get its own small plan.
