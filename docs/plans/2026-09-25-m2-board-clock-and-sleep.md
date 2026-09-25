# M2: Board Services, Clock Screen and Sleep, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Read the RTC, the SHTC3, the battery and the buttons, and show a live clock screen that updates every minute. Support both idle strategies, light and deep sleep, so the owner can measure them and pick one (D3).

**Architecture:**

- **Pure logic first, host-tested:**
  - the gesture recogniser (`board`)
  - the battery model and SHTC3 codec (`sensors`)
  - the PCF85063 codec (`rtc`)
  - the ISO 8601 parser (`timekeeping`)
  - the wake scheduler
  - the sleep policy (`power`)
  - snapshot sealing and civil dates (`util`)
  - the clock screen (`ui`), checked by golden renders
- **Device services around them:**
  - board bring-up: I²C bus, GPIO ISR service, codec standby
  - a buttons task
  - the PCF85063 driver, timekeeping and the sensors
- **The app task (`main`)** owns the hardware. It handles an event queue (RTC alarm INT, gestures, console calls), renders the clock, and programs the next RTC alarm.
- **`power`:**
  - light sleep through `esp_light_sleep_start()` with GPIO wake
  - deep sleep with ext1 wake, an RTC-RAM snapshot and a panel attach without reset
  - a tethered board stays awake, because either sleep drops the USB console
- **Console commands** run on the app task through a diag executor.

**Tech stack:**
- ESP-IDF v5.5.5: `i2c_master`, `adc_oneshot` with curve-fitting calibration, `esp_sleep`, GPIO hold, RTC RAM.
- Unity for host tests; Python 3 stdlib for tools.

**Spec:** `docs/specs/2026-09-25-firmware-design.md`.
- Relevant sections: §2, §3.1–§3.4, §4.2, §5.1–§5.3, §5.6, §7, §8, §9.1–§9.2, §9.4, §14.2, §15–§17, and the M2 row of §18.
- Also `AGENTS.md` §3 (gotchas 4, 7, 8, 10, 11, 13), §5.3 and §6–§8.

**Research behind this plan (2026-09-25):**
- ESP-IDF v5.5.5 source: sleep, USB-Serial-JTAG, I²C, ADC and GPIO hold.
- Vendor code, schematic and datasheets for the PCF85063A, SHTC3, battery divider, buttons and codecs.
- A spike on the board, in a throwaway worktree, confirmed:
  - the clock renders
  - two light-sleep and two deep-sleep cycles each wake on the RTC alarm
  - after deep sleep the panel still runs at 1.00 Hz (TE pulses), so it was not reset
  - a deep-sleep wake costs 168 ms of app time
  - the plan's code comes from that spike

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, Python 3 host tools.
- Components marked `[host]`, and the pure files named in each task, include no ESP-IDF headers.
- ESP-IDF style:
  - 4-space indent and `snake_case`.
  - Public APIs carry a component prefix and live in `include/`.
  - The PCF85063 driver uses `pcf85063_`, because ESP-IDF already defines `rtc_init` and other `rtc_*` names.
- Return `esp_err_t` and use `ESP_RETURN_ON_ERROR`. No `ESP_ERROR_CHECK` on recoverable paths.
- The app task owns the display, `st7305`, the I²C devices and sleep (spec §3.2, AGENTS §5.3). Console commands that touch them run through the diag executor.
- **Timeouts and delays at the 100 Hz tick:**
  - I²C timeouts are at least 20 ms (we use 50 ms), because timeouts under 10 ms round down to 0 ticks.
  - Datasheet minimum delays use `util_ticks_at_least()`.
- Deep-sleep entry runs from a task whose stack is in internal RAM (`xTaskCreate` default).
- Never erase flash or NVS. Board commands go through `tools/idf.sh` with an explicit `-p`, and the port must be confirmed as this board (Espressif `303A:1001`, MAC `14:c1:9f:54:bb:94`).
- List every component source in `SRCS`. After adding a component directory, run `tools/idf.sh reconfigure`. After changing `sdkconfig.defaults`, delete `sdkconfig` and reconfigure.
- Small, focused Conventional Commits that each build. Push to `origin` freely; never force-push. No AI or assistant attribution anywhere.
- Build only what the spec covers. Menus, presets, Wi-Fi and alarms stay unbound.

## Review Focus

1. **A board on battery that never sleeps.** A stale tether flag, a grace period that never ends, or a gesture stuck as busy would drain the 18650 in hours. Pinned by:
   - `test_tethered_board_stays_awake` and `test_holding_awake_wins_even_during_a_sleep_test` (Task 7)
   - `test_tap_shorter_than_debounce_is_caught_at_recheck` (Task 3)
   - the device sleep tests (Task 13)
2. **A missed or stale RTC alarm**, for example after `rtc set` moves the clock, or when the INT edge is lost. The board must wake again within 5 s, and a wake time must never be at or before now. Pinned by:
   - `test_a_wake_exactly_on_a_minute_moves_to_the_next_one` (Task 2)
   - the backup-tick check after `rtc set` (Task 12)
3. **The RTC oscillator stopped** (no backup cell, PWR off). The screen shows "--:--" and "Set time", minute wakes continue, and `rtc set` clears the flag. Pinned by:
   - `test_reports_the_oscillator_stop_flag` (Task 5)
   - the `invalid` golden render (Task 8)
   - `pcf85063_write` checking that the flag cleared (Task 11)
4. **A deep-sleep snapshot from another firmware version, or corrupt RTC RAM.** The board must take the cold path without resetting a panel that is running. Pinned by:
   - `test_other_magic_version_or_size_is_rejected` and `test_a_changed_byte_is_detected` (Task 1)
   - the fallback warm attach in `app.c` (Task 13)
5. **DST changes and local alignment** of 5- and 15-minute slots. Pinned by `test_spring_forward_skips_the_missing_hour` and `test_fall_back_keeps_quarter_hours` (Task 2).

## File map

| Path | Responsibility | Task |
|---|---|---|
| `components/util/{include/util_snapshot.h,util_snapshot.c,include/util_time.h,util_time.c}` | Sealed RTC-RAM blocks; civil dates | 1 |
| `components/scheduler/*` | Next wake from local wall-clock slots | 2 |
| `components/board/{include/gesture.h,gesture.c}` | Short, double and long presses from debounced edges | 3 |
| `components/sensors/{include/shtc3_codec.h,shtc3_codec.c,include/battery_model.h,battery_model.c}` | SHTC3 CRC and conversion; OCV curve, smoothing, charging inference | 4 |
| `components/rtc/{include/pcf85063_regs.h,pcf85063_regs.c}` | PCF85063 time and alarm registers ↔ UTC | 5 |
| `components/timekeeping/{include/timekeeping_iso.h,timekeeping_iso.c}` | ISO 8601 parsing for `rtc set` | 6 |
| `components/power/{include/power_policy.h,power_policy.c}` | Awake, light or deep, and when | 7 |
| `components/ui/*`, fonts `sans_bold_28`, `num_cb_130` | The Classic clock screen; goldens | 8 |
| `tools/devlog.py` | Recognises a prompt with a log line glued on | 9 |
| `components/board/{board.c,board_buttons.c,include/*}`, `components/diag/diag_cmd_buttons.c` | I²C bus, ISR service, codec standby, buttons task; `btn` | 10 |
| `components/rtc/pcf85063.c`, `components/timekeeping/timekeeping.c`, `components/sensors/sensors.c`, `components/diag/diag_cmd_sensors.c`, `main/Kconfig.projbuild` | Drivers and `rtc`, `sensors`, `battery` | 11 |
| `main/app.c`, `main/main.c`, diag executor | The app task, clock on the panel, `tasks` | 12 |
| `components/power/power.c`, st7305/display warm attach, `components/diag/diag_cmd_power.c`, `docs/power.md` | Light and deep sleep; `power`, `sleep` | 13 |
| `AGENTS.md`, spec, `docs/power.md` | Owner checks, D3 | 14 |

Host test count (ctest entries): 9 at the start. Each task states the new total.

---

### Task 1: `util` sealed snapshots and civil dates

**Files:**
- Create: `components/util/include/util_snapshot.h`, `components/util/util_snapshot.c`, `components/util/include/util_time.h`, `components/util/util_time.c`, `test/host/test_util_snapshot.c`, `test/host/test_util_time.c`
- Modify: `components/util/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `util_snapshot_hdr_t {uint32_t magic; uint16_t version; uint16_t size; uint32_t crc}`.
  - `void util_snapshot_seal(void *block, size_t size, uint32_t magic, uint16_t version)` and `bool util_snapshot_valid(const void *block, size_t size, uint32_t magic, uint16_t version)`. The block starts with the header, and the CRC covers the bytes after it.
  - `int64_t util_days_from_civil(int y, int m, int d)` and `void util_civil_from_days(int64_t days, int *y, int *m, int *d)`.

- [ ] **Step 1: Write the failing tests**

`test/host/test_util_snapshot.c`:

```c
#include <string.h>

#include "unity.h"
#include "util_snapshot.h"

typedef struct {
    util_snapshot_hdr_t hdr;
    uint32_t counter;
    char note[12];
} demo_t;

#define MAGIC 0x72666c62u /* "rflb" */

static demo_t s_demo;

void setUp(void)
{
    memset(&s_demo, 0, sizeof(s_demo));
    s_demo.counter = 42;
    strcpy(s_demo.note, "hello");
    util_snapshot_seal(&s_demo, sizeof(s_demo), MAGIC, 3);
}

void tearDown(void) {}

static void test_sealed_block_is_valid(void)
{
    TEST_ASSERT_TRUE(util_snapshot_valid(&s_demo, sizeof(s_demo), MAGIC, 3));
}

static void test_zeroed_memory_is_invalid(void)
{
    demo_t zero;
    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_FALSE(util_snapshot_valid(&zero, sizeof(zero), MAGIC, 3));
}

static void test_a_changed_byte_is_detected(void)
{
    s_demo.note[0] = 'j';
    TEST_ASSERT_FALSE(util_snapshot_valid(&s_demo, sizeof(s_demo), MAGIC, 3));
}

static void test_other_magic_version_or_size_is_rejected(void)
{
    TEST_ASSERT_FALSE(util_snapshot_valid(&s_demo, sizeof(s_demo), MAGIC + 1, 3));
    TEST_ASSERT_FALSE(util_snapshot_valid(&s_demo, sizeof(s_demo), MAGIC, 4));
    TEST_ASSERT_FALSE(util_snapshot_valid(&s_demo, sizeof(s_demo) - 4, MAGIC, 3));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sealed_block_is_valid);
    RUN_TEST(test_zeroed_memory_is_invalid);
    RUN_TEST(test_a_changed_byte_is_detected);
    RUN_TEST(test_other_magic_version_or_size_is_rejected);
    return UNITY_END();
}
```

`test/host/test_util_time.c`:

```c
#include "unity.h"
#include "util_time.h"

void setUp(void) {}
void tearDown(void) {}

static void test_known_days_since_the_epoch(void)
{
    TEST_ASSERT_EQUAL_INT64(0, util_days_from_civil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(10957, util_days_from_civil(2000, 1, 1));
    TEST_ASSERT_EQUAL_INT64(20721, util_days_from_civil(2026, 9, 25));
    TEST_ASSERT_EQUAL_INT64(21243, util_days_from_civil(2028, 2, 29));
    TEST_ASSERT_EQUAL_INT64(-1, util_days_from_civil(1969, 12, 31));
}

static void test_round_trips_every_day_from_2000_to_2100(void)
{
    for (int64_t z = util_days_from_civil(2000, 1, 1); z <= util_days_from_civil(2100, 12, 31); z++) {
        int y, m, d;
        util_civil_from_days(z, &y, &m, &d);
        TEST_ASSERT_EQUAL_INT64(z, util_days_from_civil(y, m, d));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_days_since_the_epoch);
    RUN_TEST(test_round_trips_every_day_from_2000_to_2100);
    return UNITY_END();
}
```

Add both to the unit-test list in `test/host/CMakeLists.txt`. These are the `reflbo_host_test` lines above `reflbo_host_test(test_test_pattern_golden gfx)`:

```cmake
reflbo_host_test(test_util_snapshot util)
reflbo_host_test(test_util_time util)
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL, with `'util_snapshot.h' file not found`.

- [ ] **Step 3: Write the implementation**

`components/util/include/util_snapshot.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Sealed state blocks, e.g. the RTC-RAM snapshot that survives deep sleep (spec §3.3): a header
 * with magic, version, size and a CRC-32 of everything after it. Pure C, host-buildable.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size; /* whole block, header included */
    uint32_t crc;  /* util_crc32 of the bytes after the header */
} util_snapshot_hdr_t;

/* `block` starts with a util_snapshot_hdr_t and is `size` bytes long. */
void util_snapshot_seal(void *block, size_t size, uint32_t magic, uint16_t version);
bool util_snapshot_valid(const void *block, size_t size, uint32_t magic, uint16_t version);
```

`components/util/util_snapshot.c`:

```c
#include "util_snapshot.h"

#include "util_crc32.h"

static uint32_t body_crc(const void *block, size_t size)
{
    const uint8_t *bytes = block;
    return util_crc32(0, bytes + sizeof(util_snapshot_hdr_t), size - sizeof(util_snapshot_hdr_t));
}

void util_snapshot_seal(void *block, size_t size, uint32_t magic, uint16_t version)
{
    util_snapshot_hdr_t *hdr = block;
    hdr->magic = magic;
    hdr->version = version;
    hdr->size = (uint16_t)size;
    hdr->crc = body_crc(block, size);
}

bool util_snapshot_valid(const void *block, size_t size, uint32_t magic, uint16_t version)
{
    const util_snapshot_hdr_t *hdr = block;
    if (size < sizeof(*hdr) || size > UINT16_MAX) {
        return false;
    }
    return hdr->magic == magic && hdr->version == version && hdr->size == size && hdr->crc == body_crc(block, size);
}
```

`components/util/include/util_time.h`:

```c
#pragma once

#include <stdint.h>

/* Proleptic Gregorian calendar <-> days since 1970-01-01 (Howard Hinnant's algorithms). Pure C. */
int64_t util_days_from_civil(int year, int month, int day);
void util_civil_from_days(int64_t days, int *year, int *month, int *day);
```

`components/util/util_time.c`:

```c
#include "util_time.h"

int64_t util_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void util_civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}
```

`components/util/CMakeLists.txt`:

```cmake
# Small pure-C helpers shared by the firmware and the host tests (no ESP-IDF headers).
idf_component_register(SRCS "util_crc32.c" "util_base64.c" "util_snapshot.c" "util_ticks.c" "util_time.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 11`.

- [ ] **Step 5: Build the firmware, then commit and push**

Run: `tools/idf.sh build`
Expected: `Project build complete`, with no warnings in `main/` or `components/`.

```bash
git add components/util test/host/test_util_snapshot.c test/host/test_util_time.c test/host/CMakeLists.txt
git commit -m "feat(util): add sealed state snapshots and civil date conversion"
git push
```

---

### Task 2: `scheduler`: next wake from local wall-clock slots

**Files:**
- Create: `components/scheduler/CMakeLists.txt`, `components/scheduler/include/scheduler.h`, `components/scheduler/scheduler.c`, `test/host/test_scheduler.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `sched_reason_t` (`SCHED_DISPLAY`, `SCHED_SENSORS`).
  - `sched_input_t {time_t now; int display_every_min; int sensors_every_min}` and `sched_wake_t {time_t when; unsigned reasons}`.
  - `sched_wake_t scheduler_next_wake(const sched_input_t *in)`: `when` is always a whole minute after `now`.
  - `int scheduler_is_slot(time_t t, int every_min)`.
- Local time comes from the TZ variable.

- [ ] **Step 1: Write the failing test `test/host/test_scheduler.c`**

```c
#define _POSIX_C_SOURCE 200809L /* setenv */

#include <stdlib.h>
#include <time.h>

#include "scheduler.h"
#include "unity.h"

/* Europe/Prague, the default time zone (AGENTS.md §1). */
#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"

void setUp(void)
{
    setenv("TZ", TZ_PRAGUE, 1);
    tzset();
}

void tearDown(void) {}

/* UTC seconds for a UTC calendar time (timegm is not standard C). */
static time_t utc(int y, int mo, int d, int h, int mi, int s)
{
    long days = 0;
    for (int yy = 1970; yy < y; yy++) {
        days += (yy % 4 == 0 && (yy % 100 != 0 || yy % 400 == 0)) ? 366 : 365;
    }
    static const int cum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    days += cum[mo - 1] + d - 1;
    if (mo > 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) {
        days++;
    }
    return (time_t)(((days * 24 + h) * 60 + mi) * 60 + s);
}

static sched_wake_t next(time_t now, int display, int sensors)
{
    sched_input_t in = { .now = now, .display_every_min = display, .sensors_every_min = sensors };
    return scheduler_next_wake(&in);
}

static void test_next_minute_for_every_minute_updates(void)
{
    sched_wake_t w = next(utc(2026, 9, 25, 8, 0, 30), 1, 5); /* 10:00:30 CEST */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 8, 1, 0), w.when);
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY, w.reasons);
}

static void test_a_wake_exactly_on_a_minute_moves_to_the_next_one(void)
{
    sched_wake_t w = next(utc(2026, 9, 25, 8, 1, 0), 1, 5);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 8, 2, 0), w.when);
}

static void test_sensor_slots_align_to_local_five_minutes(void)
{
    sched_wake_t w = next(utc(2026, 9, 25, 8, 4, 10), 1, 5);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 8, 5, 0), w.when);
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY | SCHED_SENSORS, w.reasons);
}

static void test_fifteen_minute_updates(void)
{
    sched_wake_t w = next(utc(2026, 9, 25, 8, 7, 0), 15, 30);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 8, 15, 0), w.when);
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY, w.reasons);
}

static void test_spring_forward_skips_the_missing_hour(void)
{
    /* 2026-03-29 01:50 CET = 00:50 UTC; 02:00 local does not exist, 01:00 UTC is 03:00 CEST. */
    sched_wake_t w = next(utc(2026, 3, 29, 0, 50, 0), 15, 30);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 29, 1, 0, 0), w.when);
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY | SCHED_SENSORS, w.reasons);
}

static void test_fall_back_keeps_quarter_hours(void)
{
    /* 2026-10-25 02:50 CEST = 00:50 UTC; at 01:00 UTC it is 02:00 CET again. */
    sched_wake_t w = next(utc(2026, 10, 25, 0, 50, 0), 15, 30);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 1, 0, 0), w.when);
    w = next(utc(2026, 10, 25, 1, 0, 0), 15, 30);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 1, 15, 0), w.when);
}

static void test_is_slot_needs_a_whole_minute(void)
{
    TEST_ASSERT_TRUE(scheduler_is_slot(utc(2026, 9, 25, 8, 5, 0), 5));
    TEST_ASSERT_FALSE(scheduler_is_slot(utc(2026, 9, 25, 8, 5, 1), 5));
    TEST_ASSERT_FALSE(scheduler_is_slot(utc(2026, 9, 25, 8, 6, 0), 5));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_next_minute_for_every_minute_updates);
    RUN_TEST(test_a_wake_exactly_on_a_minute_moves_to_the_next_one);
    RUN_TEST(test_sensor_slots_align_to_local_five_minutes);
    RUN_TEST(test_fifteen_minute_updates);
    RUN_TEST(test_spring_forward_skips_the_missing_hour);
    RUN_TEST(test_fall_back_keeps_quarter_hours);
    RUN_TEST(test_is_slot_needs_a_whole_minute);
    return UNITY_END();
}
```

In `test/host/CMakeLists.txt`, add this library before the `# reflbo_host_test(<name> <libs...>)` comment:

```cmake
# scheduler: pure C.
add_library(scheduler STATIC ${REPO_ROOT}/components/scheduler/scheduler.c)
target_include_directories(scheduler PUBLIC ${REPO_ROOT}/components/scheduler/include)
target_compile_options(scheduler PRIVATE ${REFLBO_WARNINGS})
```

Add `reflbo_host_test(test_scheduler scheduler)` to the unit-test list.

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL, with CMake `Cannot find source file` for `scheduler.c`.

- [ ] **Step 3: Write the component**

`components/scheduler/include/scheduler.h`:

```c
#pragma once

#include <time.h>

/*
 * Wake scheduler (spec §9.2): when to wake next and why. Pure C, host-buildable. Periodic jobs run
 * at local wall-clock slots (minute of day divisible by their period), so DST shifts nothing.
 * Local time comes from the TZ environment variable (tzset()). M3+ adds cycle switches and
 * timeouts, M5 syncs and M7 user alarms.
 */

typedef enum {
    SCHED_DISPLAY = 1u << 0, /* display update */
    SCHED_SENSORS = 1u << 1, /* SHTC3 and battery sample */
} sched_reason_t;

typedef struct {
    time_t now;            /* UTC seconds */
    int display_every_min; /* 1..15 */
    int sensors_every_min; /* 1..30 */
} sched_input_t;

typedef struct {
    time_t when;      /* UTC; a whole minute after `now` */
    unsigned reasons; /* sched_reason_t bits due at `when` */
} sched_wake_t;

sched_wake_t scheduler_next_wake(const sched_input_t *in);
/* True if `t` is a local slot of a job that runs every `every_min` minutes. */
int scheduler_is_slot(time_t t, int every_min);
```

`components/scheduler/scheduler.c`:

```c
#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include "scheduler.h"

int scheduler_is_slot(time_t t, int every_min)
{
    struct tm local;
    if (every_min <= 0 || t % 60 != 0 || localtime_r(&t, &local) == NULL) {
        return 0;
    }
    return (local.tm_hour * 60 + local.tm_min) % every_min == 0;
}

/* First slot strictly after `now`. A slot every <= 1440 min exists within a day plus a DST hour. */
static time_t next_slot(time_t now, int every_min)
{
    time_t t = now - now % 60 + 60;
    for (int i = 0; i < 25 * 60; i++, t += 60) {
        if (scheduler_is_slot(t, every_min)) {
            return t;
        }
    }
    return t;
}

sched_wake_t scheduler_next_wake(const sched_input_t *in)
{
    time_t display = next_slot(in->now, in->display_every_min);
    time_t sensors = next_slot(in->now, in->sensors_every_min);
    sched_wake_t wake = { .when = display < sensors ? display : sensors, .reasons = 0 };
    if (display == wake.when) {
        wake.reasons |= SCHED_DISPLAY;
    }
    if (sensors == wake.when) {
        wake.reasons |= SCHED_SENSORS;
    }
    return wake;
}
```

`components/scheduler/CMakeLists.txt`:

```cmake
# Wake scheduler (spec §9.2). Pure C: also built on the host by test/host.
idf_component_register(SRCS "scheduler.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 12`.

- [ ] **Step 5: Build the firmware, then commit and push**

Run: `tools/idf.sh reconfigure >/dev/null && tools/idf.sh build`
Expected: `Project build complete`, no warnings in our code, and `__idf_scheduler` in the build log.

```bash
git add components/scheduler test/host/test_scheduler.c test/host/CMakeLists.txt
git commit -m "feat(scheduler): add wake scheduling on local wall-clock slots"
git push
```

---

### Task 3: `board` gesture recogniser

**Files:**
- Create: `components/board/CMakeLists.txt` (only `gesture.c` for now), `components/board/include/gesture.h`, `components/board/gesture.c`, `test/host/test_gesture.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `gesture_t` (`GESTURE_NONE`, `GESTURE_SHORT`, `GESTURE_DOUBLE`, `GESTURE_LONG`).
  - `gesture_config_t {uint32_t long_ms; bool double_enabled}` and `gesture_recogniser_t`.
  - `void gesture_init(gesture_recogniser_t *, gesture_config_t)` and `void gesture_set_config(gesture_recogniser_t *, gesture_config_t)`.
  - `gesture_t gesture_update(gesture_recogniser_t *, bool pressed, uint32_t now_ms)`: call it on every edge and at every deadline, always with the current level.
  - `gesture_t gesture_tap(gesture_recogniser_t *, uint32_t now_ms)`.
  - `uint32_t gesture_deadline(const gesture_recogniser_t *)` (`GESTURE_NO_DEADLINE` when none) and `bool gesture_busy(const gesture_recogniser_t *)`.
  - `GESTURE_DEBOUNCE_MS` 30 and `GESTURE_DOUBLE_MS` 300.

- [ ] **Step 1: Write the failing test `test/host/test_gesture.c`**

```c
#include "gesture.h"
#include "unity.h"

static gesture_recogniser_t s_g;
static const gesture_config_t k_plain = { .long_ms = 1000, .double_enabled = false };
static const gesture_config_t k_double = { .long_ms = 1000, .double_enabled = true };

void setUp(void)
{
    gesture_init(&s_g, k_plain);
}

void tearDown(void) {}

static void test_short_press_fires_on_release_without_double(void)
{
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 1000));
    TEST_ASSERT_TRUE(gesture_busy(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1100));
    TEST_ASSERT_FALSE(gesture_busy(&s_g));
    TEST_ASSERT_EQUAL_UINT32(GESTURE_NO_DEADLINE, gesture_deadline(&s_g));
}

static void test_short_press_waits_out_the_double_window(void)
{
    gesture_set_config(&s_g, k_double);
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 1100));
    TEST_ASSERT_EQUAL_UINT32(1400, gesture_deadline(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 1399));
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1400));
    TEST_ASSERT_FALSE(gesture_busy(&s_g));
}

static void test_second_press_in_the_window_is_a_double(void)
{
    gesture_set_config(&s_g, k_double);
    gesture_update(&s_g, true, 1000);
    gesture_update(&s_g, false, 1100);
    TEST_ASSERT_EQUAL(GESTURE_DOUBLE, gesture_update(&s_g, true, 1250));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 1300));
    TEST_ASSERT_FALSE(gesture_busy(&s_g));
}

static void test_two_presses_without_double_are_two_shorts(void)
{
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1100));
    gesture_update(&s_g, true, 1250);
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1300));
}

static void test_long_press_fires_while_held_and_release_is_silent(void)
{
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL_UINT32(2000, gesture_deadline(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 1999));
    TEST_ASSERT_EQUAL(GESTURE_LONG, gesture_update(&s_g, true, 2000));
    TEST_ASSERT_EQUAL_UINT32(GESTURE_NO_DEADLINE, gesture_deadline(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 2500));
    TEST_ASSERT_FALSE(gesture_busy(&s_g));
}

static void test_boot_long_press_can_take_three_seconds(void)
{
    gesture_set_config(&s_g, (gesture_config_t){ .long_ms = 3000, .double_enabled = false });
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 2000));
    TEST_ASSERT_EQUAL(GESTURE_LONG, gesture_update(&s_g, true, 4000));
}

static void test_bounces_inside_the_debounce_time_are_ignored(void)
{
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 1005));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 1008));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 1030)); /* recheck: still pressed */
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1200));
}

static void test_tap_shorter_than_debounce_is_caught_at_recheck(void)
{
    gesture_update(&s_g, true, 1000);
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, false, 1020));
    TEST_ASSERT_EQUAL_UINT32(1030, gesture_deadline(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 1030));
}

static void test_times_wrap_around(void)
{
    gesture_update(&s_g, true, UINT32_MAX - 500);
    TEST_ASSERT_EQUAL_UINT32(499, gesture_deadline(&s_g));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_update(&s_g, true, 100));
    TEST_ASSERT_EQUAL(GESTURE_LONG, gesture_update(&s_g, true, 499));
}

static void test_wake_tap_is_a_short_press(void)
{
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_tap(&s_g, 250));
    gesture_set_config(&s_g, k_double);
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_tap(&s_g, 5000));
    TEST_ASSERT_EQUAL(GESTURE_SHORT, gesture_update(&s_g, false, 5300));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_short_press_fires_on_release_without_double);
    RUN_TEST(test_short_press_waits_out_the_double_window);
    RUN_TEST(test_second_press_in_the_window_is_a_double);
    RUN_TEST(test_two_presses_without_double_are_two_shorts);
    RUN_TEST(test_long_press_fires_while_held_and_release_is_silent);
    RUN_TEST(test_boot_long_press_can_take_three_seconds);
    RUN_TEST(test_bounces_inside_the_debounce_time_are_ignored);
    RUN_TEST(test_tap_shorter_than_debounce_is_caught_at_recheck);
    RUN_TEST(test_times_wrap_around);
    RUN_TEST(test_wake_tap_is_a_short_press);
    return UNITY_END();
}
```

In `test/host/CMakeLists.txt`, add this before the `# reflbo_host_test(<name> <libs...>)` comment:

```cmake
# board: only the pure gesture recogniser builds on the host.
add_library(board_logic STATIC ${REPO_ROOT}/components/board/gesture.c)
target_include_directories(board_logic PUBLIC ${REPO_ROOT}/components/board/include)
target_compile_options(board_logic PRIVATE ${REFLBO_WARNINGS})
```

Add `reflbo_host_test(test_gesture board_logic)` to the unit-test list.

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL, with `Cannot find source file` for `gesture.c`.

- [ ] **Step 3: Write the recogniser**

`components/board/include/gesture.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Button gesture recogniser (spec §5.6). Pure C, host-buildable.
 *
 * Call gesture_update() on every raw edge and whenever gesture_deadline() passes, always with the
 * button's current level. It debounces, times the press and returns at most one gesture per call.
 * Times are milliseconds from any monotonic clock; wrap-around is handled.
 */

#define GESTURE_DEBOUNCE_MS 30
#define GESTURE_DOUBLE_MS   300
#define GESTURE_NO_DEADLINE UINT32_MAX

typedef enum {
    GESTURE_NONE,
    GESTURE_SHORT,
    GESTURE_DOUBLE,
    GESTURE_LONG,
} gesture_t;

typedef struct {
    uint32_t long_ms;    /* hold time for a long press: 1000, or 3000 for BOOT on the dashboard */
    bool double_enabled; /* wait GESTURE_DOUBLE_MS for a second press before reporting a short one */
} gesture_config_t;

typedef struct {
    gesture_config_t config;
    int state;            /* internal: gesture.c */
    bool level;           /* debounced level: true = pressed */
    bool recheck;         /* an edge arrived inside the debounce time */
    bool have_edge;       /* edge_ms is valid */
    uint32_t edge_ms;     /* last accepted edge */
    uint32_t mark_ms;     /* press time (pressed) or release time (waiting for a second press) */
} gesture_recogniser_t;

void gesture_init(gesture_recogniser_t *g, gesture_config_t config);
void gesture_set_config(gesture_recogniser_t *g, gesture_config_t config);
gesture_t gesture_update(gesture_recogniser_t *g, bool pressed, uint32_t now_ms);
/* A press that ended before it could be timed, e.g. the one that woke the chip (spec §3.3). */
gesture_t gesture_tap(gesture_recogniser_t *g, uint32_t now_ms);
uint32_t gesture_deadline(const gesture_recogniser_t *g); /* absolute ms, or GESTURE_NO_DEADLINE */
bool gesture_busy(const gesture_recogniser_t *g);         /* a gesture is still being timed */
```

`components/board/gesture.c`:

```c
#include "gesture.h"

enum {
    ST_IDLE,
    ST_PRESSED,     /* first press held, timing a long press */
    ST_WAIT_SECOND, /* released, waiting for a second press */
    ST_IGNORE,      /* a gesture fired while pressed; ignore input until release */
};

static bool reached(uint32_t now_ms, uint32_t since_ms, uint32_t span_ms)
{
    return (uint32_t)(now_ms - since_ms) >= span_ms;
}

void gesture_init(gesture_recogniser_t *g, gesture_config_t config)
{
    *g = (gesture_recogniser_t){ .config = config, .state = ST_IDLE };
}

void gesture_set_config(gesture_recogniser_t *g, gesture_config_t config)
{
    g->config = config;
}

static gesture_t on_edge(gesture_recogniser_t *g, bool pressed, uint32_t now_ms)
{
    if (pressed) {
        if (g->state == ST_WAIT_SECOND) {
            g->state = ST_IGNORE;
            return GESTURE_DOUBLE;
        }
        g->state = ST_PRESSED;
        g->mark_ms = now_ms;
        return GESTURE_NONE;
    }
    if (g->state == ST_PRESSED) {
        if (g->config.double_enabled) {
            g->state = ST_WAIT_SECOND;
            g->mark_ms = now_ms;
            return GESTURE_NONE;
        }
        g->state = ST_IDLE;
        return GESTURE_SHORT;
    }
    g->state = ST_IDLE; /* release after a long or double press */
    return GESTURE_NONE;
}

gesture_t gesture_update(gesture_recogniser_t *g, bool pressed, uint32_t now_ms)
{
    gesture_t fired = GESTURE_NONE;
    if (pressed != g->level) {
        if (g->have_edge && !reached(now_ms, g->edge_ms, GESTURE_DEBOUNCE_MS)) {
            g->recheck = true; /* bounce, or a press shorter than the debounce time: look again later */
        } else {
            g->level = pressed;
            g->have_edge = true;
            g->edge_ms = now_ms;
            g->recheck = false;
            fired = on_edge(g, pressed, now_ms);
        }
    } else if (g->recheck && reached(now_ms, g->edge_ms, GESTURE_DEBOUNCE_MS)) {
        g->recheck = false; /* the level settled where it was */
    }
    if (fired != GESTURE_NONE) {
        return fired;
    }
    if (g->state == ST_PRESSED && reached(now_ms, g->mark_ms, g->config.long_ms)) {
        g->state = ST_IGNORE;
        return GESTURE_LONG;
    }
    if (g->state == ST_WAIT_SECOND && reached(now_ms, g->mark_ms, GESTURE_DOUBLE_MS)) {
        g->state = ST_IDLE;
        return GESTURE_SHORT;
    }
    return GESTURE_NONE;
}

gesture_t gesture_tap(gesture_recogniser_t *g, uint32_t now_ms)
{
    g->state = ST_PRESSED;
    g->level = false;
    g->have_edge = true;
    g->edge_ms = now_ms;
    g->recheck = false;
    return on_edge(g, false, now_ms);
}

/* A real deadline never equals GESTURE_NO_DEADLINE; one that would lands 1 ms early. */
static uint32_t at(uint32_t ms)
{
    return ms == GESTURE_NO_DEADLINE ? ms - 1 : ms;
}

uint32_t gesture_deadline(const gesture_recogniser_t *g)
{
    uint32_t deadline = GESTURE_NO_DEADLINE;
    if (g->state == ST_PRESSED) {
        deadline = at(g->mark_ms + g->config.long_ms);
    } else if (g->state == ST_WAIT_SECOND) {
        deadline = at(g->mark_ms + GESTURE_DOUBLE_MS);
    }
    if (g->recheck) {
        uint32_t recheck = at(g->edge_ms + GESTURE_DEBOUNCE_MS);
        if (deadline == GESTURE_NO_DEADLINE || (int32_t)(recheck - deadline) < 0) {
            deadline = recheck;
        }
    }
    return deadline;
}

bool gesture_busy(const gesture_recogniser_t *g)
{
    return g->state != ST_IDLE || g->recheck;
}
```

`components/board/CMakeLists.txt` (Task 10 adds the device sources):

```cmake
# Board bring-up, pins, buttons (spec §3.1). gesture.c is pure C and also built on the host.
idf_component_register(SRCS "gesture.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 13`.

- [ ] **Step 5: Build the firmware, then commit and push**

Run: `tools/idf.sh reconfigure >/dev/null && tools/idf.sh build`
Expected: `Project build complete`, with no warnings in our code.

```bash
git add components/board test/host/test_gesture.c test/host/CMakeLists.txt
git commit -m "feat(board): add the button gesture recogniser"
git push
```

---

### Task 4: `sensors` SHTC3 codec and battery model

**Files:**
- Create: `components/sensors/CMakeLists.txt` (pure sources for now), `components/sensors/include/shtc3_codec.h`, `components/sensors/shtc3_codec.c`, `components/sensors/include/battery_model.h`, `components/sensors/battery_model.c`, `test/host/test_shtc3_codec.c`, `test/host/test_battery_model.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `uint8_t shtc3_crc8(const uint8_t *, size_t)` and `bool shtc3_parse(const uint8_t raw[6], int *temp_c100, int *hum_pct100)`.
  - `battery_state_t` (`BATTERY_UNKNOWN`, `BATTERY_DISCHARGING`, `BATTERY_CHARGING`, `BATTERY_FULL`).
  - `battery_gauge_t`, a plain struct that is safe to copy into RTC RAM.
  - `int battery_percent_from_mv(int)`.
  - `battery_gauge_init`, `battery_gauge_add(g, now_s, mv)`, `battery_gauge_mv`, `battery_gauge_level` (-1 before the first sample) and `battery_gauge_state(g, now_s)`.
- Facts behind it (datasheet v1.1, Table 16, Fig. 7):
  - The SHTC3 CRC uses polynomial 0x31, init 0xFF.
  - The worked example 0x648B/0xA133 decodes to 23.73 °C / 62.97 %RH.

- [ ] **Step 1: Write the failing tests**

`test/host/test_shtc3_codec.c`:

```c
#include "shtc3_codec.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_crc_matches_the_datasheet_vectors(void)
{
    const uint8_t zero[] = { 0x00 };
    const uint8_t beef[] = { 0xBE, 0xEF };
    TEST_ASSERT_EQUAL_HEX8(0xAC, shtc3_crc8(zero, 1));
    TEST_ASSERT_EQUAL_HEX8(0x92, shtc3_crc8(beef, 2));
}

static void test_parses_the_datasheet_example(void)
{
    /* Datasheet Fig. 7: T 0x648B (CRC 0xC7) = 23.73 °C, RH 0xA133 (CRC 0x1C) = 62.97 %RH. */
    const uint8_t raw[6] = { 0x64, 0x8B, 0xC7, 0xA1, 0x33, 0x1C };
    int t, rh;
    TEST_ASSERT_TRUE(shtc3_parse(raw, &t, &rh));
    TEST_ASSERT_EQUAL_INT(2373, t);
    TEST_ASSERT_EQUAL_INT(6297, rh);
}

static void test_rejects_a_bad_crc(void)
{
    const uint8_t raw[6] = { 0x64, 0x8B, 0xC6, 0xA1, 0x33, 0x1C };
    int t = 1, rh = 1;
    TEST_ASSERT_FALSE(shtc3_parse(raw, &t, &rh));
    TEST_ASSERT_EQUAL_INT(1, t);
}

static void test_converts_the_range_ends(void)
{
    uint8_t raw[6] = { 0x00, 0x00, 0, 0xFF, 0xFF, 0 };
    raw[2] = shtc3_crc8(raw, 2);
    raw[5] = shtc3_crc8(raw + 3, 2);
    int t, rh;
    TEST_ASSERT_TRUE(shtc3_parse(raw, &t, &rh));
    TEST_ASSERT_EQUAL_INT(-4500, t);
    TEST_ASSERT_EQUAL_INT(10000, rh); /* 99.998 %RH rounds to 100.00 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_matches_the_datasheet_vectors);
    RUN_TEST(test_parses_the_datasheet_example);
    RUN_TEST(test_rejects_a_bad_crc);
    RUN_TEST(test_converts_the_range_ends);
    return UNITY_END();
}
```

`test/host/test_battery_model.c`:

```c
#include "battery_model.h"
#include "unity.h"

static battery_gauge_t s_g;

void setUp(void)
{
    battery_gauge_init(&s_g);
}

void tearDown(void) {}

/* Adds one sample every 5 min from t0, all at the same voltage. */
static uint32_t add_steady(uint32_t t0, int mv, int count)
{
    for (int i = 0; i < count; i++) {
        battery_gauge_add(&s_g, t0 + (uint32_t)i * 300u, mv);
    }
    return t0 + (uint32_t)(count - 1) * 300u;
}

static void test_ocv_curve_interpolates_and_clamps(void)
{
    TEST_ASSERT_EQUAL_INT(100, battery_percent_from_mv(4250));
    TEST_ASSERT_EQUAL_INT(100, battery_percent_from_mv(4200));
    TEST_ASSERT_EQUAL_INT(50, battery_percent_from_mv(3840));
    TEST_ASSERT_EQUAL_INT(48, battery_percent_from_mv(3830));
    TEST_ASSERT_EQUAL_INT(0, battery_percent_from_mv(3270));
    TEST_ASSERT_EQUAL_INT(0, battery_percent_from_mv(3000));
}

static void test_no_sample_means_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(-1, battery_gauge_level(&s_g));
    TEST_ASSERT_EQUAL_INT(0, battery_gauge_mv(&s_g));
    TEST_ASSERT_EQUAL(BATTERY_UNKNOWN, battery_gauge_state(&s_g, 1000));
}

static void test_first_sample_sets_voltage_and_level(void)
{
    battery_gauge_add(&s_g, 1000, 3840);
    TEST_ASSERT_EQUAL_INT(3840, battery_gauge_mv(&s_g));
    TEST_ASSERT_EQUAL_INT(50, battery_gauge_level(&s_g));
    TEST_ASSERT_EQUAL(BATTERY_UNKNOWN, battery_gauge_state(&s_g, 1000));
}

static void test_samples_are_smoothed(void)
{
    battery_gauge_add(&s_g, 1000, 4000);
    battery_gauge_add(&s_g, 1300, 3900);
    TEST_ASSERT_EQUAL_INT(3975, battery_gauge_mv(&s_g));
}

static void test_level_does_not_rise_while_not_charging(void)
{
    battery_gauge_add(&s_g, 1000, 3800);
    TEST_ASSERT_EQUAL_INT(40, battery_gauge_level(&s_g));
    battery_gauge_add(&s_g, 1300, 3900); /* a recovery bump after load, not a charge */
    TEST_ASSERT_EQUAL_INT(40, battery_gauge_level(&s_g));
}

static void test_steady_voltage_after_30_min_is_discharging(void)
{
    uint32_t t = add_steady(1000, 3840, 7);
    TEST_ASSERT_EQUAL(BATTERY_DISCHARGING, battery_gauge_state(&s_g, t));
}

static void test_rising_voltage_over_30_min_is_charging_and_raises_the_level(void)
{
    uint32_t t = 1000;
    for (int i = 0; i < 8; i++, t += 300) {
        battery_gauge_add(&s_g, t, 3800 + i * 10); /* +70 mV over 35 min */
    }
    TEST_ASSERT_EQUAL(BATTERY_CHARGING, battery_gauge_state(&s_g, t - 300));
    TEST_ASSERT_TRUE(battery_gauge_level(&s_g) > 40);
}

static void test_high_steady_voltage_is_full(void)
{
    uint32_t t = add_steady(1000, 4180, 8);
    TEST_ASSERT_EQUAL(BATTERY_FULL, battery_gauge_state(&s_g, t));
    TEST_ASSERT_EQUAL_INT(98, battery_gauge_level(&s_g));
}

static void test_extra_samples_do_not_crowd_the_history(void)
{
    battery_gauge_add(&s_g, 1000, 3840);
    for (int i = 1; i <= 20; i++) {
        battery_gauge_add(&s_g, 1000 + (uint32_t)i * 10u, 3840); /* e.g. BOOT refreshes */
    }
    TEST_ASSERT_EQUAL_UINT8(1, s_g.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ocv_curve_interpolates_and_clamps);
    RUN_TEST(test_no_sample_means_unknown);
    RUN_TEST(test_first_sample_sets_voltage_and_level);
    RUN_TEST(test_samples_are_smoothed);
    RUN_TEST(test_level_does_not_rise_while_not_charging);
    RUN_TEST(test_steady_voltage_after_30_min_is_discharging);
    RUN_TEST(test_rising_voltage_over_30_min_is_charging_and_raises_the_level);
    RUN_TEST(test_high_steady_voltage_is_full);
    RUN_TEST(test_extra_samples_do_not_crowd_the_history);
    return UNITY_END();
}
```

In `test/host/CMakeLists.txt`, add this before the `# reflbo_host_test(<name> <libs...>)` comment:

```cmake
# sensors: only the SHTC3 codec and the battery model build on the host.
add_library(sensors_logic STATIC ${REPO_ROOT}/components/sensors/shtc3_codec.c
                                 ${REPO_ROOT}/components/sensors/battery_model.c)
target_include_directories(sensors_logic PUBLIC ${REPO_ROOT}/components/sensors/include)
target_compile_options(sensors_logic PRIVATE ${REFLBO_WARNINGS})
```

Add `reflbo_host_test(test_shtc3_codec sensors_logic)` and `reflbo_host_test(test_battery_model sensors_logic)` to the unit-test list.

- [ ] **Step 2: Run them and confirm they fail**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL, with `Cannot find source file` for `shtc3_codec.c`.

- [ ] **Step 3: Write the codec and the model**

`components/sensors/include/shtc3_codec.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SHTC3 data handling (datasheet §5.6, §5.9, §5.11): CRC and conversion. Pure C, host-buildable. */

uint8_t shtc3_crc8(const uint8_t *data, size_t len); /* poly 0x31, init 0xFF */
/* Parses a T-first measurement: T msb, lsb, crc, RH msb, lsb, crc. False on a CRC mismatch. */
bool shtc3_parse(const uint8_t raw[6], int *temp_c100, int *hum_pct100);
```

`components/sensors/shtc3_codec.c`:

```c
#include "shtc3_codec.h"

uint8_t shtc3_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1);
        }
    }
    return crc;
}

bool shtc3_parse(const uint8_t raw[6], int *temp_c100, int *hum_pct100)
{
    if (shtc3_crc8(raw, 2) != raw[2] || shtc3_crc8(raw + 3, 2) != raw[5]) {
        return false;
    }
    int32_t t = (raw[0] << 8) | raw[1];
    int32_t rh = (raw[3] << 8) | raw[4];
    *temp_c100 = (int)(-4500 + (17500 * t + 32768) / 65536); /* T = -45 + 175 * S / 2^16 */
    *hum_pct100 = (int)((10000 * rh + 32768) / 65536);        /* RH = 100 * S / 2^16 */
    return true;
}
```

`components/sensors/include/battery_model.h`:

```c
#pragma once

#include <stdint.h>

/*
 * Battery gauge logic (spec §8): open-circuit-voltage curve, smoothing and inferred charging
 * state. Pure C, host-buildable; the ADC reading happens in battery.c.
 */

#define BATTERY_HISTORY 8 /* samples kept for charging inference, at least 4 min apart */

typedef enum {
    BATTERY_UNKNOWN,     /* less than 30 min of history */
    BATTERY_DISCHARGING,
    BATTERY_CHARGING,    /* rose >= 30 mV over the last 30 min */
    BATTERY_FULL,        /* >= 4150 mV and steady */
} battery_state_t;

typedef struct {
    uint32_t time_s;
    uint16_t mv;
} battery_point_t;

typedef struct {
    uint32_t ema_mv16;  /* smoothed voltage, mV x 16; 0 = no sample yet */
    uint8_t level;      /* %, never rises unless charging or full */
    uint8_t count;      /* valid history points */
    uint8_t head;       /* next history slot */
    battery_point_t history[BATTERY_HISTORY];
} battery_gauge_t;

int battery_percent_from_mv(int mv); /* OCV table, 0..100 */
void battery_gauge_init(battery_gauge_t *g);
void battery_gauge_add(battery_gauge_t *g, uint32_t now_s, int mv);
int battery_gauge_mv(const battery_gauge_t *g);    /* smoothed; 0 before the first sample */
int battery_gauge_level(const battery_gauge_t *g); /* %; -1 before the first sample */
battery_state_t battery_gauge_state(const battery_gauge_t *g, uint32_t now_s);
```

`components/sensors/battery_model.c`:

```c
#include "battery_model.h"

#include <stdbool.h>
#include <stddef.h>

#define HISTORY_SPACING_S 240  /* keep one history point per ~5 min sample, skip extra samples */
#define TREND_WINDOW_S    1800 /* 30 min */
#define CHARGE_RISE_MV    30
#define FULL_MV           4150
#define STEADY_MV         10

/* Li-ion open-circuit voltage vs state of charge, NCR18650B-like (approximate; tune with data). */
static const struct {
    uint16_t mv;
    uint8_t pct;
} k_ocv[] = {
    { 3270, 0 },  { 3610, 5 },  { 3690, 10 }, { 3710, 15 }, { 3730, 20 }, { 3750, 25 }, { 3770, 30 },
    { 3790, 35 }, { 3800, 40 }, { 3820, 45 }, { 3840, 50 }, { 3850, 55 }, { 3870, 60 }, { 3910, 65 },
    { 3950, 70 }, { 3980, 75 }, { 4020, 80 }, { 4080, 85 }, { 4110, 90 }, { 4150, 95 }, { 4200, 100 },
};
#define OCV_POINTS (sizeof(k_ocv) / sizeof(k_ocv[0]))

int battery_percent_from_mv(int mv)
{
    if (mv <= k_ocv[0].mv) {
        return 0;
    }
    for (unsigned i = 1; i < OCV_POINTS; i++) {
        if (mv <= k_ocv[i].mv) {
            int dv = k_ocv[i].mv - k_ocv[i - 1].mv;
            int dp = k_ocv[i].pct - k_ocv[i - 1].pct;
            return k_ocv[i - 1].pct + ((mv - k_ocv[i - 1].mv) * dp + dv / 2) / dv;
        }
    }
    return 100;
}

void battery_gauge_init(battery_gauge_t *g)
{
    *g = (battery_gauge_t){ 0 };
}

static const battery_point_t *newest(const battery_gauge_t *g)
{
    return &g->history[(g->head + BATTERY_HISTORY - 1) % BATTERY_HISTORY];
}

/* The newest point at least TREND_WINDOW_S older than `now`, or NULL. */
static const battery_point_t *window_start(const battery_gauge_t *g, uint32_t now_s)
{
    for (unsigned k = 0; k < g->count; k++) {
        const battery_point_t *p = &g->history[(g->head + BATTERY_HISTORY - 1 - k) % BATTERY_HISTORY];
        if (now_s - p->time_s >= TREND_WINDOW_S) {
            return p;
        }
    }
    return NULL;
}

battery_state_t battery_gauge_state(const battery_gauge_t *g, uint32_t now_s)
{
    if (g->ema_mv16 == 0) {
        return BATTERY_UNKNOWN;
    }
    const battery_point_t *start = window_start(g, now_s);
    if (start == NULL) {
        return BATTERY_UNKNOWN;
    }
    int now_mv = battery_gauge_mv(g);
    int rise = now_mv - start->mv;
    if (now_mv >= FULL_MV && rise < STEADY_MV && rise > -STEADY_MV) {
        return BATTERY_FULL;
    }
    return rise >= CHARGE_RISE_MV ? BATTERY_CHARGING : BATTERY_DISCHARGING;
}

void battery_gauge_add(battery_gauge_t *g, uint32_t now_s, int mv)
{
    if (mv < 1) {
        mv = 1; /* ema_mv16 == 0 means "no sample yet" */
    }
    bool first = g->ema_mv16 == 0;
    if (first) {
        g->ema_mv16 = (uint32_t)mv * 16u;
    } else {
        int32_t ema = (int32_t)g->ema_mv16;
        ema += ((int32_t)mv * 16 - ema) / 4; /* alpha = 1/4 per sample */
        g->ema_mv16 = (uint32_t)ema;
    }
    if (g->count == 0 || now_s - newest(g)->time_s >= HISTORY_SPACING_S) {
        g->history[g->head] = (battery_point_t){ .time_s = now_s, .mv = (uint16_t)battery_gauge_mv(g) };
        g->head = (uint8_t)((g->head + 1) % BATTERY_HISTORY);
        if (g->count < BATTERY_HISTORY) {
            g->count++;
        }
    }
    int pct = battery_percent_from_mv(battery_gauge_mv(g));
    battery_state_t state = battery_gauge_state(g, now_s);
    if (first || state == BATTERY_CHARGING || state == BATTERY_FULL || pct < g->level) {
        g->level = (uint8_t)pct;
    }
}

int battery_gauge_mv(const battery_gauge_t *g)
{
    return (int)((g->ema_mv16 + 8) / 16);
}

int battery_gauge_level(const battery_gauge_t *g)
{
    return g->ema_mv16 == 0 ? -1 : g->level;
}
```

`components/sensors/CMakeLists.txt` (Task 11 adds the drivers):

```cmake
# SHTC3 and battery gauge (spec §8). shtc3_codec.c and battery_model.c are pure C and also built on the host.
idf_component_register(SRCS "shtc3_codec.c" "battery_model.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 15`.

- [ ] **Step 5: Build the firmware, then commit and push**

Run: `tools/idf.sh reconfigure >/dev/null && tools/idf.sh build`
Expected: `Project build complete`, with no warnings in our code.

```bash
git add components/sensors test/host/test_shtc3_codec.c test/host/test_battery_model.c test/host/CMakeLists.txt
git commit -m "feat(sensors): add the SHTC3 codec and the battery gauge model"
git push
```

---

### Task 5: `rtc` PCF85063 register codec

**Files:**
- Create: `components/rtc/CMakeLists.txt` (codec only for now), `components/rtc/include/pcf85063_regs.h`, `components/rtc/pcf85063_regs.c`, `test/host/test_pcf85063_regs.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `util_days_from_civil`, `util_civil_from_days` (Task 1).
- Produces:
  - `PCF85063_ADDR` (0x51) and the register and length constants.
  - `PCF85063_CONTROL_1_RUN` (0x00), `PCF85063_CONTROL_2_RUN` (0x8F), `PCF85063_TIMER_MODE_OFF` (0x18).
  - `PCF85063_MIN_TIME`, `PCF85063_MAX_TIME`.
  - `bool pcf85063_decode_time(const uint8_t regs[7], time_t *utc, bool *stopped)`.
  - `void pcf85063_encode_time(time_t utc, uint8_t regs[7])`.
  - `void pcf85063_encode_alarm(time_t wake, uint8_t regs[5])`.
- Datasheet facts (PCF85063A Rev. 6):
  - The alarm enable bits are active low; 0x80 disables a field.
  - Writes to the Control_2 flags are ANDed, so 0x8F clears AF only.
  - COF = 111 turns off CLKOUT, which is on after power-up.
  - The oscillator-stop flag is Seconds bit 7.
  - The time registers freeze during one multi-byte access.

- [ ] **Step 1: Write the failing test `test/host/test_pcf85063_regs.c`**

```c
#include "pcf85063_regs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define FRI_2026_09_25_204805 ((time_t)1790369285)

static void test_encodes_a_known_time(void)
{
    uint8_t r[7];
    pcf85063_encode_time(FRI_2026_09_25_204805, r);
    const uint8_t expected[7] = { 0x05, 0x48, 0x20, 0x25, 5 /* Friday */, 0x09, 0x26 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r, 7);
}

static void test_decodes_a_known_time(void)
{
    const uint8_t r[7] = { 0x05, 0x48, 0x20, 0x25, 5, 0x09, 0x26 };
    time_t t = 0;
    bool stopped = true;
    TEST_ASSERT_TRUE(pcf85063_decode_time(r, &t, &stopped));
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
    TEST_ASSERT_FALSE(stopped);
}

static void test_reports_the_oscillator_stop_flag(void)
{
    const uint8_t r[7] = { 0x80, 0x00, 0x00, 0x01, 6, 0x01, 0x00 }; /* power-on reset state */
    time_t t = 0;
    bool stopped = false;
    TEST_ASSERT_TRUE(pcf85063_decode_time(r, &t, &stopped));
    TEST_ASSERT_TRUE(stopped);
    TEST_ASSERT_EQUAL_INT64(PCF85063_MIN_TIME, t);
}

static void test_round_trips_leap_day_and_range_ends(void)
{
    const time_t cases[] = { 1835438400 /* 2028-02-29T12:00Z */, PCF85063_MIN_TIME, PCF85063_MAX_TIME };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t r[7];
        time_t back = 0;
        bool stopped = true;
        pcf85063_encode_time(cases[i], r);
        TEST_ASSERT_TRUE(pcf85063_decode_time(r, &back, &stopped));
        TEST_ASSERT_EQUAL_INT64(cases[i], back);
    }
}

static void test_rejects_impossible_register_values(void)
{
    const uint8_t bad_bcd[7] = { 0x5A, 0x00, 0x00, 0x01, 6, 0x01, 0x00 };
    const uint8_t feb_30[7] = { 0x00, 0x00, 0x00, 0x30, 0, 0x02, 0x26 };
    time_t t;
    bool stopped;
    TEST_ASSERT_FALSE(pcf85063_decode_time(bad_bcd, &t, &stopped));
    TEST_ASSERT_FALSE(pcf85063_decode_time(feb_30, &t, &stopped));
}

static void test_clamps_times_outside_the_rtc_range(void)
{
    uint8_t r[7];
    time_t t;
    bool stopped;
    pcf85063_encode_time(0, r);
    TEST_ASSERT_TRUE(pcf85063_decode_time(r, &t, &stopped));
    TEST_ASSERT_EQUAL_INT64(PCF85063_MIN_TIME, t);
}

static void test_alarm_matches_minute_hour_and_day(void)
{
    uint8_t a[5];
    pcf85063_encode_alarm((time_t)1790369340 /* 2026-09-25T20:49:00Z */, a);
    const uint8_t expected[5] = { 0x80, 0x49, 0x20, 0x25, 0x80 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, a, 5);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encodes_a_known_time);
    RUN_TEST(test_decodes_a_known_time);
    RUN_TEST(test_reports_the_oscillator_stop_flag);
    RUN_TEST(test_round_trips_leap_day_and_range_ends);
    RUN_TEST(test_rejects_impossible_register_values);
    RUN_TEST(test_clamps_times_outside_the_rtc_range);
    RUN_TEST(test_alarm_matches_minute_hour_and_day);
    return UNITY_END();
}
```

In `test/host/CMakeLists.txt`, add this before the `# reflbo_host_test(<name> <libs...>)` comment:

```cmake
# rtc: only the PCF85063 register codec builds on the host.
add_library(rtc_logic STATIC ${REPO_ROOT}/components/rtc/pcf85063_regs.c)
target_include_directories(rtc_logic PUBLIC ${REPO_ROOT}/components/rtc/include)
target_compile_options(rtc_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(rtc_logic PUBLIC util)
```

Add `reflbo_host_test(test_pcf85063_regs rtc_logic)` to the unit-test list.

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL, with `Cannot find source file` for `pcf85063_regs.c`.

- [ ] **Step 3: Write the codec**

`components/rtc/include/pcf85063_regs.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * PCF85063A register codec (datasheet Rev. 6, §8): time and alarm registers <-> UTC seconds. The
 * RTC stores UTC (spec §7). Pure C, host-buildable; the I²C transfers live in rtc.c.
 */

#define PCF85063_ADDR           0x51
#define PCF85063_REG_CONTROL_1  0x00
#define PCF85063_REG_CONTROL_2  0x01
#define PCF85063_REG_SECONDS    0x04 /* 04h-0Ah: seconds .. years, read and written in one transfer */
#define PCF85063_REG_ALARM      0x0B /* 0Bh-0Fh: second, minute, hour, day, weekday alarms */
#define PCF85063_REG_TIMER_MODE 0x11

#define PCF85063_TIME_LEN  7
#define PCF85063_ALARM_LEN 5

/* Control_1: 24-hour mode, CAP_SEL = 7 pF (the vendor default), clock running. */
#define PCF85063_CONTROL_1_RUN 0x00
/* Control_2: AIE on, AF = 0 (clears the alarm flag; flag writes are ANDed), MI/HMI off,
 * TF = 1 (left as is), COF = 111 (CLKOUT off; it is on after power-up). */
#define PCF85063_CONTROL_2_RUN 0x8F
#define PCF85063_CONTROL_2_AF  0x40
/* Timer_mode: timer off, clock 1/60 Hz, the low-power setting (Table 36). */
#define PCF85063_TIMER_MODE_OFF 0x18

/* 2000-01-01T00:00:00Z .. 2099-12-31T23:59:59Z */
#define PCF85063_MIN_TIME ((time_t)946684800)
#define PCF85063_MAX_TIME ((time_t)4102444799)

/* Decodes 04h-0Ah. `stopped` reports the oscillator-stop flag (time not trustworthy). False if a
 * register holds an impossible value. */
bool pcf85063_decode_time(const uint8_t regs[PCF85063_TIME_LEN], time_t *utc, bool *stopped);
/* Encodes 04h-0Ah with the oscillator-stop flag cleared. `utc` is clamped to the RTC's range. */
void pcf85063_encode_time(time_t utc, uint8_t regs[PCF85063_TIME_LEN]);
/* Encodes 0Bh-0Fh for an alarm at `wake` (a whole minute, within the next 28 days): minute, hour
 * and day enabled, second and weekday disabled. AF sets when the time reaches it. */
void pcf85063_encode_alarm(time_t wake, uint8_t regs[PCF85063_ALARM_LEN]);
```

`components/rtc/pcf85063_regs.c`:

```c
#include "pcf85063_regs.h"

#include "util_time.h"

#define AEN_DISABLED 0x80 /* alarm enable bits are active low */

static uint8_t to_bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static int from_bcd(uint8_t b, uint8_t mask, int max)
{
    b &= mask;
    int hi = b >> 4;
    int lo = b & 0x0F;
    if (lo > 9) {
        return -1;
    }
    int v = hi * 10 + lo;
    return v > max ? -1 : v;
}

static int days_in_month(int y, int m)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = y % 4 == 0; /* 2000..2099: every fourth year, 2000 included */
    return m == 2 && leap ? 29 : days[m - 1];
}

bool pcf85063_decode_time(const uint8_t regs[PCF85063_TIME_LEN], time_t *utc, bool *stopped)
{
    int sec = from_bcd(regs[0], 0x7F, 59);
    int min = from_bcd(regs[1], 0x7F, 59);
    int hour = from_bcd(regs[2], 0x3F, 23);
    int day = from_bcd(regs[3], 0x3F, 31);
    int month = from_bcd(regs[5], 0x1F, 12);
    int year = from_bcd(regs[6], 0xFF, 99);
    if (sec < 0 || min < 0 || hour < 0 || day < 1 || month < 1 || year < 0 ||
        day > days_in_month(2000 + year, month)) {
        return false;
    }
    *stopped = (regs[0] & 0x80) != 0;
    int64_t days = util_days_from_civil(2000 + year, month, day);
    *utc = (time_t)(((days * 24 + hour) * 60 + min) * 60 + sec);
    return true;
}

void pcf85063_encode_time(time_t utc, uint8_t regs[PCF85063_TIME_LEN])
{
    if (utc < PCF85063_MIN_TIME) {
        utc = PCF85063_MIN_TIME;
    } else if (utc > PCF85063_MAX_TIME) {
        utc = PCF85063_MAX_TIME;
    }
    int64_t days = utc / 86400;
    int secs = (int)(utc % 86400);
    int y, m, d;
    util_civil_from_days(days, &y, &m, &d);
    regs[0] = to_bcd(secs % 60); /* OS = 0 */
    regs[1] = to_bcd(secs / 60 % 60);
    regs[2] = to_bcd(secs / 3600);
    regs[3] = to_bcd(d);
    regs[4] = (uint8_t)((days + 4) % 7); /* 1970-01-01 was a Thursday; 0 = Sunday */
    regs[5] = to_bcd(m);
    regs[6] = to_bcd(y - 2000);
}

void pcf85063_encode_alarm(time_t wake, uint8_t regs[PCF85063_ALARM_LEN])
{
    uint8_t t[PCF85063_TIME_LEN];
    pcf85063_encode_time(wake, t);
    regs[0] = AEN_DISABLED;        /* second: any (the match starts at :00 of the minute) */
    regs[1] = t[1];                /* minute */
    regs[2] = t[2];                /* hour */
    regs[3] = t[3];                /* day */
    regs[4] = AEN_DISABLED;        /* weekday: any */
}
```

`components/rtc/CMakeLists.txt` (Task 11 adds the driver):

```cmake
# PCF85063A RTC (spec §7). pcf85063_regs.c is pure C and also built on the host.
idf_component_register(SRCS "pcf85063_regs.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES util)
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 16`.

- [ ] **Step 5: Build the firmware, then commit and push**

Run: `tools/idf.sh reconfigure >/dev/null && tools/idf.sh build`
Expected: `Project build complete`, with no warnings in our code.

```bash
git add components/rtc test/host/test_pcf85063_regs.c test/host/CMakeLists.txt
git commit -m "feat(rtc): add the PCF85063 time and alarm register codec"
git push
```

---

### Task 6: `timekeeping` ISO 8601 parser

**Files:**
- Create: `components/timekeeping/CMakeLists.txt` (parser only for now), `components/timekeeping/include/timekeeping_iso.h`, `components/timekeeping/timekeeping_iso.c`, `test/host/test_timekeeping_iso.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `util_days_from_civil` (Task 1).
- Produces: `bool timekeeping_parse_iso8601(const char *text, time_t *utc)`.
  - It accepts `YYYY-MM-DDTHH:MM[:SS]` followed by `Z`, `±HH:MM` or nothing.
  - Nothing means local time from the TZ variable.
  - A space may replace the `T`.

- [ ] **Step 1: Write the failing test `test/host/test_timekeeping_iso.c`**

```c
#define _POSIX_C_SOURCE 200809L /* setenv */

#include <stdlib.h>
#include <time.h>

#include "timekeeping_iso.h"
#include "unity.h"

#define FRI_2026_09_25_204805 ((time_t)1790369285)

void setUp(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

void tearDown(void) {}

static void test_parses_utc(void)
{
    time_t t = 0;
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25T20:48:05Z", &t));
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
}

static void test_parses_an_offset(void)
{
    time_t t = 0;
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25T22:48:05+02:00", &t));
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25T15:18:05-05:30", &t));
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
}

static void test_parses_local_time_with_the_tz_rules(void)
{
    time_t t = 0;
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25 22:48:05", &t)); /* CEST, UTC+2 */
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-01-15T12:00", &t)); /* CET, UTC+1 */
    TEST_ASSERT_EQUAL_INT64((time_t)1768474800, t);
}

static void test_seconds_are_optional(void)
{
    time_t t = 0;
    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25T20:48Z", &t));
    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805 - 5, t);
}

static void test_rejects_malformed_or_impossible_input(void)
{
    const char *bad[] = { "", "garbage", "2026-13-01T00:00Z", "2026-02-30T00:00Z", "2026-09-25T25:00Z",
                          "2026-09-25T20:48:05ZZ", "2026-09-25T20:48+2", "26-09-25T20:48Z", "2026-09-25" };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        time_t t = 0;
        TEST_ASSERT_FALSE_MESSAGE(timekeeping_parse_iso8601(bad[i], &t), bad[i]);
    }
    time_t t = 0;
    TEST_ASSERT_FALSE(timekeeping_parse_iso8601(NULL, &t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_utc);
    RUN_TEST(test_parses_an_offset);
    RUN_TEST(test_parses_local_time_with_the_tz_rules);
    RUN_TEST(test_seconds_are_optional);
    RUN_TEST(test_rejects_malformed_or_impossible_input);
    return UNITY_END();
}
```

In `test/host/CMakeLists.txt`, add this before the `# reflbo_host_test(<name> <libs...>)` comment:

```cmake
# timekeeping: only the ISO 8601 parser builds on the host.
add_library(timekeeping_logic STATIC ${REPO_ROOT}/components/timekeeping/timekeeping_iso.c)
target_include_directories(timekeeping_logic PUBLIC ${REPO_ROOT}/components/timekeeping/include)
target_compile_options(timekeeping_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(timekeeping_logic PUBLIC util)
```

Add `reflbo_host_test(test_timekeeping_iso timekeeping_logic)` to the unit-test list.

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL, with `Cannot find source file` for `timekeeping_iso.c`.

- [ ] **Step 3: Write the parser**

`components/timekeeping/include/timekeeping_iso.h`:

```c
#pragma once

#include <stdbool.h>
#include <time.h>

/*
 * Parses an ISO 8601 date and time for `rtc set`: "YYYY-MM-DDTHH:MM[:SS]" followed by "Z", an
 * offset "+HH:MM" / "-HH:MM", or nothing for local time (the TZ variable). A space may replace the
 * "T". Pure C, host-buildable.
 */
bool timekeeping_parse_iso8601(const char *text, time_t *utc);
```

`components/timekeeping/timekeeping_iso.c`:

```c
#define _POSIX_C_SOURCE 200809L /* mktime with tzset semantics */

#include "timekeeping_iso.h"

#include <ctype.h>
#include <stddef.h>

#include "util_time.h"

/* Reads exactly `digits` decimal digits. */
static bool number(const char **p, int digits, int *out)
{
    int v = 0;
    for (int i = 0; i < digits; i++) {
        if (!isdigit((unsigned char)(*p)[i])) {
            return false;
        }
        v = v * 10 + ((*p)[i] - '0');
    }
    *p += digits;
    *out = v;
    return true;
}

static bool expect(const char **p, char c)
{
    if (**p != c) {
        return false;
    }
    (*p)++;
    return true;
}

static int days_in_month(int y, int m)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : days[m - 1];
}

bool timekeeping_parse_iso8601(const char *text, time_t *utc)
{
    const char *p = text;
    int y, mo, d, h, mi, s = 0;
    if (text == NULL || !number(&p, 4, &y) || !expect(&p, '-') || !number(&p, 2, &mo) || !expect(&p, '-') ||
        !number(&p, 2, &d) || (*p != 'T' && *p != 't' && *p != ' ')) {
        return false;
    }
    p++;
    if (!number(&p, 2, &h) || !expect(&p, ':') || !number(&p, 2, &mi)) {
        return false;
    }
    if (*p == ':' && (p++, !number(&p, 2, &s))) {
        return false;
    }
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) || h > 23 || mi > 59 || s > 59) {
        return false;
    }

    if (*p == '\0') { /* local time */
        struct tm local = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi,
                            .tm_sec = s, .tm_isdst = -1 };
        time_t t = mktime(&local);
        if (t == (time_t)-1) {
            return false;
        }
        *utc = t;
        return true;
    }

    int offset_min = 0;
    if (*p == 'Z' || *p == 'z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        int sign = *p == '-' ? -1 : 1;
        int oh, om;
        p++;
        if (!number(&p, 2, &oh) || !expect(&p, ':') || !number(&p, 2, &om) || oh > 14 || om > 59) {
            return false;
        }
        offset_min = sign * (oh * 60 + om);
    } else {
        return false;
    }
    if (*p != '\0') {
        return false;
    }
    int64_t days = util_days_from_civil(y, mo, d);
    *utc = (time_t)(((days * 24 + h) * 60 + mi - offset_min) * 60 + s);
    return true;
}
```

`components/timekeeping/CMakeLists.txt` (Task 11 adds the service):

```cmake
# System time, time zone and manual set (spec §7). timekeeping_iso.c is pure C and also built on the host.
idf_component_register(SRCS "timekeeping_iso.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES util)
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 17`.

- [ ] **Step 5: Build the firmware, then commit and push**

Run: `tools/idf.sh reconfigure >/dev/null && tools/idf.sh build`
Expected: `Project build complete`, with no warnings in our code.

```bash
git add components/timekeeping test/host/test_timekeeping_iso.c test/host/CMakeLists.txt
git commit -m "feat(timekeeping): add an ISO 8601 parser for setting the clock"
git push
```

---

### Task 7: `power` sleep policy

**Files:**
- Create: `components/power/CMakeLists.txt` (policy only for now), `components/power/include/power_policy.h`, `components/power/power_policy.c`, `test/host/test_power_policy.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `power_idle_t` (`POWER_IDLE_LIGHT`, `POWER_IDLE_DEEP`) and `power_plan_t` (`POWER_PLAN_AWAKE`, `POWER_PLAN_LIGHT`, `POWER_PLAN_DEEP`).
  - `power_policy_input_t {strategy, tethered, hold_awake, test_cycles, test_mode}`.
  - `power_plan_t power_policy(const power_policy_input_t *)`.
- Rules:
  - `hold_awake` means awake.
  - While `sleep test` cycles are left, sleep in the test mode.
  - A tethered board stays awake.
  - Otherwise use the strategy.

- [ ] **Step 1: Write the failing test `test/host/test_power_policy.c`**

```c
#include "power_policy.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static power_plan_t plan(power_idle_t strategy, bool tethered, bool hold, int test_cycles, power_idle_t test_mode)
{
    power_policy_input_t in = { .strategy = strategy, .tethered = tethered, .hold_awake = hold,
                                .test_cycles = test_cycles, .test_mode = test_mode };
    return power_policy(&in);
}

static void test_untethered_board_sleeps_with_its_strategy(void)
{
    TEST_ASSERT_EQUAL(POWER_PLAN_LIGHT, plan(POWER_IDLE_LIGHT, false, false, 0, POWER_IDLE_LIGHT));
    TEST_ASSERT_EQUAL(POWER_PLAN_DEEP, plan(POWER_IDLE_DEEP, false, false, 0, POWER_IDLE_LIGHT));
}

static void test_tethered_board_stays_awake(void)
{
    /* Light and deep sleep both drop the USB console (AGENTS.md gotcha 11). */
    TEST_ASSERT_EQUAL(POWER_PLAN_AWAKE, plan(POWER_IDLE_LIGHT, true, false, 0, POWER_IDLE_LIGHT));
    TEST_ASSERT_EQUAL(POWER_PLAN_AWAKE, plan(POWER_IDLE_DEEP, true, false, 0, POWER_IDLE_LIGHT));
}

static void test_holding_awake_wins_even_during_a_sleep_test(void)
{
    TEST_ASSERT_EQUAL(POWER_PLAN_AWAKE, plan(POWER_IDLE_DEEP, false, true, 0, POWER_IDLE_LIGHT));
    TEST_ASSERT_EQUAL(POWER_PLAN_AWAKE, plan(POWER_IDLE_LIGHT, true, true, 3, POWER_IDLE_DEEP));
}

static void test_sleep_test_cycles_sleep_even_when_tethered(void)
{
    TEST_ASSERT_EQUAL(POWER_PLAN_DEEP, plan(POWER_IDLE_LIGHT, true, false, 2, POWER_IDLE_DEEP));
    TEST_ASSERT_EQUAL(POWER_PLAN_LIGHT, plan(POWER_IDLE_DEEP, true, false, 1, POWER_IDLE_LIGHT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_untethered_board_sleeps_with_its_strategy);
    RUN_TEST(test_tethered_board_stays_awake);
    RUN_TEST(test_holding_awake_wins_even_during_a_sleep_test);
    RUN_TEST(test_sleep_test_cycles_sleep_even_when_tethered);
    return UNITY_END();
}
```

In `test/host/CMakeLists.txt`, add this before the `# reflbo_host_test(<name> <libs...>)` comment:

```cmake
# power: only the sleep policy builds on the host.
add_library(power_logic STATIC ${REPO_ROOT}/components/power/power_policy.c)
target_include_directories(power_logic PUBLIC ${REPO_ROOT}/components/power/include)
target_compile_options(power_logic PRIVATE ${REFLBO_WARNINGS})
```

Add `reflbo_host_test(test_power_policy power_logic)` to the unit-test list.

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL, with `Cannot find source file` for `power_policy.c`.

- [ ] **Step 3: Write the policy**

`components/power/include/power_policy.h`:

```c
#pragma once

#include <stdbool.h>

/* Sleep policy (spec §3.4, §9.1): whether and how to sleep now. Pure C, host-buildable. */

typedef enum {
    POWER_IDLE_LIGHT,
    POWER_IDLE_DEEP,
} power_idle_t;

typedef enum {
    POWER_PLAN_AWAKE, /* keep running; wait for events */
    POWER_PLAN_LIGHT,
    POWER_PLAN_DEEP,
} power_plan_t;

typedef struct {
    power_idle_t strategy;  /* the configured idle strategy */
    bool tethered;          /* a USB host is sending frames: sleeping would drop the console */
    bool hold_awake;        /* grace period after boot or a button, pending work, a gesture in progress */
    int test_cycles;        /* > 0: `sleep test` cycles left; they sleep even when tethered */
    power_idle_t test_mode;
} power_policy_input_t;

power_plan_t power_policy(const power_policy_input_t *in);
```

`components/power/power_policy.c`:

```c
#include "power_policy.h"

static power_plan_t plan_for(power_idle_t idle)
{
    return idle == POWER_IDLE_DEEP ? POWER_PLAN_DEEP : POWER_PLAN_LIGHT;
}

power_plan_t power_policy(const power_policy_input_t *in)
{
    if (in->hold_awake) {
        return POWER_PLAN_AWAKE;
    }
    if (in->test_cycles > 0) {
        return plan_for(in->test_mode);
    }
    if (in->tethered) {
        return POWER_PLAN_AWAKE;
    }
    return plan_for(in->strategy);
}
```

`components/power/CMakeLists.txt` (Task 13 adds sleep entry):

```cmake
# Power states, sleep entry and policy (spec §3.4, §9). power_policy.c is pure C and also built on the host.
idf_component_register(SRCS "power_policy.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 18`.

- [ ] **Step 5: Build the firmware, then commit and push**

Run: `tools/idf.sh reconfigure >/dev/null && tools/idf.sh build`
Expected: `Project build complete`, with no warnings in our code.

```bash
git add components/power test/host/test_power_policy.c test/host/CMakeLists.txt
git commit -m "feat(power): add the sleep policy"
git push
```

---

### Task 8: Clock screen (`ui`), its fonts and golden renders

**Files:**
- Create: `assets/fonts/DejaVuSansCondensed-Bold.ttf`, `components/gfx/fonts/gfx_font_sans_bold_28.c` and `gfx_font_num_cb_130.c` (generated), `components/ui/CMakeLists.txt`, `components/ui/include/ui_clock.h`, `components/ui/ui_clock.c`, `test/host/ui_fixtures.h`, `test/host/test_ui_clock_golden.c`, `test/host/render_clock.c`, `test/host/golden/clock_{valid,invalid,cold}.pbm` (generated, then reviewed)
- Modify: `tools/gen_fonts.sh`, `components/gfx/include/gfx_fonts.h`, `components/gfx/CMakeLists.txt`, `test/host/test_gfx_fonts.c`, `test/host/CMakeLists.txt`, `tools/render.py`, `THIRD_PARTY.md`

**Interfaces:**
- Consumes:
  - `gfx_*` and fonts (M1).
- Produces:
  - `extern const gfx_font_t gfx_font_sans_bold_28, gfx_font_num_cb_130`. The latter is DejaVu Sans Condensed Bold at 130 px, digits charset only.
  - `ui_battery_t` (`UI_BATTERY_UNKNOWN`, `UI_BATTERY_DISCHARGING`, `UI_BATTERY_CHARGING`, `UI_BATTERY_FULL`).
  - `ui_clock_t {bool time_valid; struct tm local; bool env_valid; int temp_c10; int hum_pct; bool battery_valid; int battery_pct; int battery_mv; ui_battery_t battery_state}`.
  - `void ui_draw_clock(gfx_fb_t *fb, const ui_clock_t *clock)`.
- Layout (400×300):
  - A 20 px status bar: "Set time" white on black when the time is invalid; battery % and an icon at the right, with "↑" while charging.
  - The time: `HH:MM` in `num_cb_130`, or `--:--`.
  - The date: "Friday 25 September" in `sans_20`.
  - A bottom row of four cells: Temperature, Humidity, Battery and Voltage, each a `sans_12` label over a `sans_bold_28` value with a `sans_16` unit. A missing value shows "—".
  - The owner reviews layouts at M3 (spec §5.2).

- [ ] **Step 1: Add the fonts**

```bash
cp .cache/dejavu-fonts-ttf-2.37/ttf/DejaVuSansCondensed-Bold.ttf assets/fonts/
# (missing .cache? re-run the curl/tar lines of M1 Task 3 Step 5 first)
```

Append to `tools/gen_fonts.sh`:

```bash
fontgen --ttf assets/fonts/DejaVuSans-Bold.ttf --size 28 --charset text --name sans_bold_28 --licence assets/fonts/LICENSE-DejaVu.txt
fontgen --ttf assets/fonts/DejaVuSansCondensed-Bold.ttf --size 130 --charset digits --name num_cb_130 --licence assets/fonts/LICENSE-DejaVu.txt
```

Run: `tools/gen_fonts.sh && git status --short components/gfx/fonts`
Expected:
- The script prints `gfx_font_sans_bold_28.c: 327 glyphs, ascent 26, line 33` and `gfx_font_num_cb_130.c: 20 glyphs, ascent 121, line 152`.
- Only the two new files appear as untracked; the other four fonts are unchanged.

Append to `components/gfx/include/gfx_fonts.h`:

```c
extern const gfx_font_t gfx_font_sans_bold_28;
extern const gfx_font_t gfx_font_num_cb_130; /* DejaVu Sans Condensed Bold; digits, : . - ° % only */
```

In `components/gfx/CMakeLists.txt`, add `"fonts/gfx_font_sans_bold_28.c" "fonts/gfx_font_num_cb_130.c"` to `SRCS`.

In `test/host/test_gfx_fonts.c`, add `&gfx_font_sans_bold_28` to `s_fonts[]`, the text fonts whose Czech coverage is checked.

In `THIRD_PARTY.md`, change the DejaVu row's first cell to `DejaVu Sans 2.37 (Regular, Bold, Condensed Bold)`.

- [ ] **Step 2: Write the failing golden test and its fixtures**

`test/host/ui_fixtures.h`:

```c
#pragma once

#include "ui_clock.h"

/* Fixed inputs for the clock screen's golden renders (test_ui_clock_golden.c, render_clock.c). */

static inline ui_clock_t fixture_clock_valid(void)
{
    ui_clock_t c = { .time_valid = true, .env_valid = true, .temp_c10 = 234, .hum_pct = 45, .battery_valid = true,
                     .battery_pct = 87, .battery_mv = 3921, .battery_state = UI_BATTERY_DISCHARGING };
    c.local.tm_hour = 20;
    c.local.tm_min = 48;
    c.local.tm_wday = 5; /* Friday */
    c.local.tm_mday = 25;
    c.local.tm_mon = 8; /* September */
    c.local.tm_year = 126;
    return c;
}

/* RTC oscillator stopped: "--:--", "Set time", no readings yet (spec §5.3). */
static inline ui_clock_t fixture_clock_invalid(void)
{
    return (ui_clock_t){ .time_valid = false };
}

/* Below zero and charging: the sign of -0.5 °C, the charging arrow, a full battery. */
static inline ui_clock_t fixture_clock_cold(void)
{
    ui_clock_t c = { .time_valid = true, .env_valid = true, .temp_c10 = -5, .hum_pct = 100, .battery_valid = true,
                     .battery_pct = 100, .battery_mv = 4180, .battery_state = UI_BATTERY_CHARGING };
    c.local.tm_hour = 0;
    c.local.tm_min = 5;
    c.local.tm_wday = 0; /* Sunday */
    c.local.tm_mday = 1;
    c.local.tm_mon = 2; /* March */
    c.local.tm_year = 126;
    return c;
}
```

`test/host/test_ui_clock_golden.c`:

```c
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "ui_fixtures.h"
#include "unity.h"

/* Each fixture must match test/host/golden/clock_<name>.pbm byte for byte. After an intentional
 * change: build-host/render_clock <name> test/host/golden/clock_<name>.pbm for each fixture, look
 * at the PNGs (python3 tools/render.py), commit. */

static uint8_t s_buf[400 * 300 / 8];
static uint8_t s_pbm[16000];
static uint8_t s_golden[16000];

void setUp(void) {}
void tearDown(void) {}

static void check(const char *name, ui_clock_t clock)
{
    gfx_fb_t fb;
    gfx_fb_init(&fb, s_buf, 400, 300);
    ui_draw_clock(&fb, &clock);
    size_t n = gfx_pbm_encode(&fb, s_pbm, sizeof(s_pbm));

    char path[256];
    snprintf(path, sizeof(path), "%s/clock_%s.pbm", GOLDEN_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    size_t golden = fread(s_golden, 1, sizeof(s_golden), f);
    fclose(f);
    if (golden != n || memcmp(s_golden, s_pbm, n) != 0) {
        char actual[64];
        snprintf(actual, sizeof(actual), "clock_%s.actual.pbm", name);
        FILE *out = fopen(actual, "wb");
        if (out != NULL) {
            fwrite(s_pbm, 1, n, out);
            fclose(out);
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)n, (int)golden, path);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_golden, s_pbm, n, path);
}

static void test_valid_time_with_readings(void)
{
    check("valid", fixture_clock_valid());
}

static void test_invalid_time_without_readings(void)
{
    check("invalid", fixture_clock_invalid());
}

static void test_below_zero_and_charging(void)
{
    check("cold", fixture_clock_cold());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_time_with_readings);
    RUN_TEST(test_invalid_time_without_readings);
    RUN_TEST(test_below_zero_and_charging);
    return UNITY_END();
}
```

`test/host/render_clock.c`:

```c
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "ui_fixtures.h"

/* Writes a clock-screen fixture as PBM: render_clock <valid|invalid|cold> OUT.pbm */
int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <valid|invalid|cold> OUT.pbm\n", argv[0]);
        return 2;
    }
    ui_clock_t clock;
    if (strcmp(argv[1], "valid") == 0) {
        clock = fixture_clock_valid();
    } else if (strcmp(argv[1], "invalid") == 0) {
        clock = fixture_clock_invalid();
    } else if (strcmp(argv[1], "cold") == 0) {
        clock = fixture_clock_cold();
    } else {
        fprintf(stderr, "unknown fixture %s\n", argv[1]);
        return 2;
    }
    static uint8_t buf[400 * 300 / 8];
    static uint8_t pbm[16000];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    ui_draw_clock(&fb, &clock);
    size_t n = gfx_pbm_encode(&fb, pbm, sizeof(pbm));
    FILE *f = fopen(argv[2], "wb");
    if (f == NULL || fwrite(pbm, 1, n, f) != n) {
        perror(argv[2]);
        return 1;
    }
    fclose(f);
    return 0;
}
```

In `test/host/CMakeLists.txt`, add this before the `# reflbo_host_test(<name> <libs...>)` comment:

```cmake
# ui: pure C on top of gfx.
add_library(ui STATIC ${REPO_ROOT}/components/ui/ui_clock.c)
target_include_directories(ui PUBLIC ${REPO_ROOT}/components/ui/include)
target_compile_options(ui PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(ui PUBLIC gfx)
```

Add this after the `render_test_pattern` executable block:

```cmake
reflbo_host_test(test_ui_clock_golden ui)
target_compile_definitions(test_ui_clock_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(render_clock render_clock.c)
target_compile_options(render_clock PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(render_clock PRIVATE ui)
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL, with `Cannot find source file` for `ui_clock.c`.

- [ ] **Step 4: Write the screen**

`components/ui/include/ui_clock.h`:

```c
#pragma once

#include <stdbool.h>
#include <time.h>

#include "gfx.h"

/*
 * The Classic clock screen (spec §5.2, M2 version): status bar, large time, date and a bottom row
 * with temperature, humidity, battery level and voltage. Rendering is a pure function of this
 * struct. Pure C, host-buildable.
 */

typedef enum {
    UI_BATTERY_UNKNOWN,
    UI_BATTERY_DISCHARGING,
    UI_BATTERY_CHARGING,
    UI_BATTERY_FULL,
} ui_battery_t;

typedef struct {
    bool time_valid;     /* false: "--:--" and "Set time" (spec §5.3) */
    struct tm local;     /* local time and date */
    bool env_valid;
    int temp_c10;        /* 0.1 °C */
    int hum_pct;
    bool battery_valid;
    int battery_pct;
    int battery_mv;
    ui_battery_t battery_state;
} ui_clock_t;

void ui_draw_clock(gfx_fb_t *fb, const ui_clock_t *clock);
```

`components/ui/ui_clock.c`:

```c
#include "ui_clock.h"

#include <stdio.h>

#include "gfx_fonts.h"

#define STATUS_H   20
#define TIME_BASE  132 /* baseline of the large time */
#define DATE_BASE  170
#define ROW_TOP    188 /* separator above the bottom row */
#define CELLS      4

static const char *const k_weekdays[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                                          "Saturday" };
static const char *const k_months[] = { "January", "February", "March", "April", "May", "June", "July",
                                        "August", "September", "October", "November", "December" };

static void draw_battery_icon(gfx_fb_t *fb, int x, int y, const ui_clock_t *c)
{
    gfx_rect(fb, (gfx_rect_t){ (int16_t)x, (int16_t)y, 22, 11 }, GFX_BLACK);
    gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x + 22), (int16_t)(y + 3), 2, 5 }, GFX_BLACK);
    if (c->battery_valid) {
        int fill = c->battery_pct * 18 / 100;
        gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x + 2), (int16_t)(y + 2), (int16_t)fill, 7 }, GFX_BLACK);
    }
}

static void draw_status_bar(gfx_fb_t *fb, const ui_clock_t *c)
{
    const gfx_font_t *f = &gfx_font_sans_12;
    if (!c->time_valid) {
        gfx_fill_rect(fb, (gfx_rect_t){ 0, 0, 84, STATUS_H }, GFX_BLACK);
        gfx_text_in_rect(fb, &gfx_font_sans_16, (gfx_rect_t){ 6, 0, 78, STATUS_H }, GFX_ALIGN_LEFT, "Set time",
                         GFX_WHITE);
    }
    char text[16];
    if (!c->battery_valid) {
        snprintf(text, sizeof(text), "—");
    } else if (c->battery_state == UI_BATTERY_CHARGING) {
        snprintf(text, sizeof(text), "↑ %d%%", c->battery_pct);
    } else {
        snprintf(text, sizeof(text), "%d%%", c->battery_pct);
    }
    int icon_x = fb->width - 6 - 24;
    gfx_text_in_rect(fb, f, (gfx_rect_t){ (int16_t)(icon_x - 106), 0, 100, STATUS_H }, GFX_ALIGN_RIGHT, text,
                     GFX_BLACK);
    draw_battery_icon(fb, icon_x, 5, c);
    gfx_hline(fb, 0, STATUS_H, fb->width, GFX_BLACK);
}

static void draw_cell(gfx_fb_t *fb, int index, const char *label, const char *value, const char *unit)
{
    int w = fb->width / CELLS;
    int x = index * w;
    gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ (int16_t)x, ROW_TOP + 10, (int16_t)w, 16 }, GFX_ALIGN_CENTER,
                     label, GFX_BLACK);
    const gfx_font_t *vf = &gfx_font_sans_bold_28;
    const gfx_font_t *uf = &gfx_font_sans_16;
    int vw = gfx_text_width(vf, value);
    int uw = unit[0] ? gfx_text_width(uf, unit) + 2 : 0;
    int start = x + (w - vw - uw) / 2;
    int base = ROW_TOP + 64;
    int pen = gfx_text(fb, vf, start, base, value, GFX_BLACK);
    if (unit[0]) {
        gfx_text(fb, uf, pen + 2, base, unit, GFX_BLACK);
    }
    if (index > 0) {
        gfx_vline(fb, x, ROW_TOP + 10, 64, GFX_BLACK);
    }
}

void ui_draw_clock(gfx_fb_t *fb, const ui_clock_t *c)
{
    char buf[40];
    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    draw_status_bar(fb, c);

    if (c->time_valid) {
        snprintf(buf, sizeof(buf), "%02d:%02d", c->local.tm_hour, c->local.tm_min);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    const gfx_font_t *tf = &gfx_font_num_cb_130;
    gfx_text(fb, tf, (fb->width - gfx_text_width(tf, buf)) / 2, TIME_BASE, buf, GFX_BLACK);

    if (c->time_valid && c->local.tm_wday >= 0 && c->local.tm_wday < 7 && c->local.tm_mon >= 0 &&
        c->local.tm_mon < 12) {
        snprintf(buf, sizeof(buf), "%s %d %s", k_weekdays[c->local.tm_wday], c->local.tm_mday,
                 k_months[c->local.tm_mon]);
        const gfx_font_t *df = &gfx_font_sans_20;
        gfx_text(fb, df, (fb->width - gfx_text_width(df, buf)) / 2, DATE_BASE, buf, GFX_BLACK);
    }

    gfx_hline(fb, 12, ROW_TOP, fb->width - 24, GFX_BLACK);
    char value[16];
    if (c->env_valid) {
        int magnitude = c->temp_c10 < 0 ? -c->temp_c10 : c->temp_c10;
        snprintf(value, sizeof(value), "%s%d.%d", c->temp_c10 < 0 ? "-" : "", magnitude / 10, magnitude % 10);
        draw_cell(fb, 0, "Temperature", value, "°C");
        snprintf(value, sizeof(value), "%d", c->hum_pct);
        draw_cell(fb, 1, "Humidity", value, "%");
    } else {
        draw_cell(fb, 0, "Temperature", "—", "");
        draw_cell(fb, 1, "Humidity", "—", "");
    }
    if (c->battery_valid) {
        snprintf(value, sizeof(value), "%d", c->battery_pct);
        draw_cell(fb, 2, "Battery", value, "%");
        snprintf(value, sizeof(value), "%d.%02d", c->battery_mv / 1000, (c->battery_mv % 1000) / 10);
        draw_cell(fb, 3, "Voltage", value, "V");
    } else {
        draw_cell(fb, 2, "Battery", "—", "");
        draw_cell(fb, 3, "Voltage", "—", "");
    }
}
```

`components/ui/CMakeLists.txt`:

```cmake
# Screens and layouts (spec §5). Pure C: also built on the host by test/host.
idf_component_register(SRCS "ui_clock.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx)
```

- [ ] **Step 5: Run it and confirm the golden test fails only for the missing goldens**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: only `test_ui_clock_golden` fails, with `.../golden/clock_valid.pbm` among the failed paths.

- [ ] **Step 6: Render the goldens, look at them, and confirm the test passes**

`tools/render.py`:

```python
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
RENDERERS = {
    "test_pattern": ["render_test_pattern"],
    "clock_valid": ["render_clock", "valid"],
    "clock_invalid": ["render_clock", "invalid"],
    "clock_cold": ["render_clock", "cold"],
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
```

```bash
for f in valid invalid cold; do build-host/render_clock $f test/host/golden/clock_$f.pbm; done
python3 tools/render.py
```

Look at `captures/render/clock_{valid,invalid,cold}.png` (Read tool) and check each of these:
- valid:
  - "20:48" fills most of the width
  - "Friday 25 September"
  - the four cells read 23.4 °C, 45 %, 87 %, 3.92 V
  - "87%" and a partly filled battery icon at the top right
- invalid:
  - "Set time" white on black at the top left
  - `--:--`
  - no date
  - "—" in all four cells
- cold:
  - "00:05" and "Sunday 1 March"
  - "-0.5 °C" with its minus sign, 100 %, 100 %, 4.18 V
  - "↑ 100%" and a full icon
- nothing overlaps

Run: `ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 19`.

- [ ] **Step 7: Build the firmware, then commit and push**

Run: `tools/idf.sh reconfigure >/dev/null && tools/idf.sh build`
Expected: `Project build complete`, with no warnings in our code.

```bash
git add assets/fonts components/gfx components/ui test/host tools/gen_fonts.sh tools/render.py THIRD_PARTY.md
git commit -m "feat(ui): add the Classic clock screen with golden renders"
git push
```

---

### Task 9: `devlog.py`: a prompt with a log line glued on ends the command

The app task logs whenever it wants, so a line can land right after the console prints its prompt, as in `reflbo> W (20052) app: …`. `devlog.py` then waited for a bare prompt that never came, and gave up with exit 3. The spike hit this on the first `rtc set`. This is the M0 deferred minor about lines glued to the prompt.

**Files:**
- Modify: `tools/devlog.py`, `tools/tests/test_devlog.py`

**Interfaces:**
- Produces: `devlog.run()` treats a completed line that starts with `PROMPT` as the prompt, once the current command's echo has been seen (or before the first command).

- [ ] **Step 1: Write the failing test**

Add to `class RunTest` in `tools/tests/test_devlog.py`, before `test_nudges_again_for_the_prompt_after_a_reboot`:

```python
    def test_prompt_with_a_log_line_glued_on_still_ends_the_command(self):
        # The app task can log right after the console prints its prompt: "reflbo> W (20052) app: ...".
        code, out, fake, _ = self.run_tool(
            [b"reflbo> ",
             (after_sending("rtc get"), b"rtc get\r\nrtc: 2026-09-25T19:44:48Z\r\nreflbo> W (20052) app: tick\r\n"),
             (after_sending("sensors"), b"sensors\r\nsensors: 23.41 C, 45.20 %RH\r\nreflbo> ")],
            cmd=["rtc get", "sensors"], seconds=10.0)
        self.assertEqual(code, 0)
        self.assertEqual(fake.written, [b"rtc get\r", b"sensors\r"])
        self.assertIn("sensors: 23.41 C", out)
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cd tools && python3 -m unittest tests.test_devlog -v 2>&1 | tail -5; cd ..`
Expected: `FAIL: test_prompt_with_a_log_line_glued_on_still_ends_the_command`, with `AssertionError: 3 != 0`.

- [ ] **Step 3: Make the fix**

Make these changes in `tools/devlog.py`:
- after `until_seen = until is None`, add `prompt_line = False  # a prompt arrived with other output glued on, e.g. "reflbo> W (20) app: ..."`
- add `prompt_line` to `emit()`'s `nonlocal` list
- after the `echo_seen = True` branch in `emit()`, add `elif line.startswith(PROMPT) and (current is None or echo_seen): prompt_line = True`
- in the loop, make it `prompt_visible = prompt_line or splitter.tail()...`
- reset `prompt_line = False` as the first statement of the `if awaiting_prompt and prompt_visible ...` branch

The whole file afterwards:

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
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 19`, and `tools_unittests` runs 38 Python tests (37 before).

- [ ] **Step 5: Commit and push**

```bash
git add tools/devlog.py tools/tests/test_devlog.py
git commit -m "fix(tools): end devlog commands on a prompt that has a log line glued on"
git push
```

---

### Task 10: Board bring-up, buttons and `btn`

**Files:**
- Create: `components/board/include/board_pins.h`, `components/board/include/board.h`, `components/board/include/board_buttons.h`, `components/board/board.c`, `components/board/board_buttons.c`, `components/diag/diag_cmd_buttons.c`
- Modify: `components/board/CMakeLists.txt`, `components/st7305/st7305.c`, `components/st7305/CMakeLists.txt`, `components/diag/diag_internal.h`, `components/diag/diag.c`, `components/diag/CMakeLists.txt`, `main/main.c`, `main/CMakeLists.txt`, `AGENTS.md`

**Interfaces:**
- Consumes: `gesture_*` (Task 3), `util_ticks_at_least` (M1).
- Produces:
  - `board_pins.h`: `BOARD_PIN_*`. From here on, `st7305` takes its pins from it.
  - `esp_err_t board_init(bool cold)` and `i2c_master_bus_handle_t board_i2c(void)`.
  - `board_button_t` (`BOARD_BUTTON_KEY`, `BOARD_BUTTON_BOOT`) and `board_button_cb_t`.
  - `board_buttons_start(cb, const gesture_config_t cfg[2])`, `board_buttons_inject(b, g)`, `board_buttons_woke(b)`, `board_buttons_resync()`, `board_buttons_busy()`, `board_buttons_pressed(b)`.
  - `board_button_name` and `board_gesture_name`.
  - The console command `btn <key|boot> <short|double|long>`.
- `board` owns:
  - the I²C bus: external 2.2 kΩ pull-ups, and the internal ones too, which silences ESP-IDF's pull-up warning
  - the GPIO ISR service: `st7305_count_frames` already tolerates an earlier install
  - codec standby at cold boot: the esp_codec_dev ES8311 suspend and ES7210 stop register lists
  - PA_CTRL driven low
- The buttons task runs at priority 6 with a 3 KB stack. It gets edges from an ANYEDGE ISR and reports each gesture through the callback.

- [ ] **Step 1: Watch the device check fail**

Run: `tools/idf.sh exec python tools/devlog.py -p PORT --cmd "btn key short" -t 10 | grep -E "btn|unknown"`
Expected: `unknown command: btn`.

- [ ] **Step 2: Write the board component**

`components/board/include/board_pins.h`:

```c
#pragma once

/* Waveshare ESP32-S3-RLCD-4.2 pin map (AGENTS.md §3.2, checked against the schematic). */

#define BOARD_PIN_BOOT     0  /* BOOT button: active low, external 10k pull-up and 100 nF */
#define BOARD_PIN_BAT_ADC  4  /* ADC1_CH3 = VBAT x 1/3 */
#define BOARD_PIN_LCD_DC   5
#define BOARD_PIN_LCD_TE   6
#define BOARD_PIN_LCD_SCK  11
#define BOARD_PIN_LCD_MOSI 12
#define BOARD_PIN_I2C_SDA  13 /* external 2.2k pull-ups */
#define BOARD_PIN_I2C_SCL  14
#define BOARD_PIN_RTC_INT  15 /* PCF85063 INT: open drain, active low, no external pull-up */
#define BOARD_PIN_KEY      18 /* KEY button: active low, external 10k pull-up, no capacitor */
#define BOARD_PIN_LCD_CS   40 /* digital-only pad */
#define BOARD_PIN_LCD_RST  41 /* digital-only pad; low resets the panel */
#define BOARD_PIN_PA_CTRL  46 /* speaker amp enable: external 10k pull-down */
```

`components/board/include/board.h`:

```c
#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * Board bring-up (spec §3.1). Owns the I²C bus and the GPIO ISR service. Call board_init() once,
 * first. `cold`: after power-on, also report the I²C devices and put the audio codecs into standby
 * (they keep that state through deep sleep).
 */
esp_err_t board_init(bool cold);
i2c_master_bus_handle_t board_i2c(void);
```

`components/board/board.c`:

```c
#include "board.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

#define I2C_TIMEOUT_MS 50 /* at least two 10 ms ticks; shorter timeouts round down to zero */

static const char *TAG = "board";

static i2c_master_bus_handle_t s_i2c;

/* Standby writes from esp_codec_dev: es8311 suspend and es7210 stop (AGENTS.md gotcha 8). */
static const uint8_t k_es8311_standby[][2] = {
    { 0x32, 0x00 }, { 0x17, 0x00 }, { 0x0E, 0xFF }, { 0x12, 0x02 }, { 0x14, 0x00 },
    { 0x0D, 0xFA }, { 0x15, 0x00 }, { 0x02, 0x10 }, { 0x00, 0x00 }, { 0x00, 0x1F },
    { 0x01, 0x30 }, { 0x01, 0x00 }, { 0x45, 0x00 }, { 0x0D, 0xFC }, { 0x02, 0x00 },
};
static const uint8_t k_es7210_standby[][2] = {
    { 0x47, 0xFF }, { 0x48, 0xFF }, { 0x49, 0xFF }, { 0x4A, 0xFF }, { 0x4B, 0xFF },
    { 0x4C, 0xFF }, { 0x40, 0xC0 }, { 0x01, 0x7F }, { 0x06, 0x07 },
};

static esp_err_t write_codec(uint16_t addr, const uint8_t (*regs)[2], size_t count)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &cfg, &dev), TAG, "add codec");
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < count && err == ESP_OK; i++) {
        err = i2c_master_transmit(dev, regs[i], 2, I2C_TIMEOUT_MS);
    }
    i2c_master_bus_rm_device(dev);
    return err;
}

static void report_devices(void)
{
    static const struct {
        uint16_t addr;
        const char *name;
    } k_devices[] = { { 0x18, "ES8311" }, { 0x40, "ES7210" }, { 0x51, "PCF85063" }, { 0x70, "SHTC3" } };
    for (size_t i = 0; i < sizeof(k_devices) / sizeof(k_devices[0]); i++) {
        esp_err_t err = i2c_master_probe(s_i2c, k_devices[i].addr, I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "I2C 0x%02x %s present", k_devices[i].addr, k_devices[i].name);
        } else {
            ESP_LOGW(TAG, "I2C 0x%02x %s missing: %s", k_devices[i].addr, k_devices[i].name, esp_err_to_name(err));
        }
    }
}

esp_err_t board_init(bool cold)
{
    gpio_config_t pa = { .pin_bit_mask = 1ULL << BOARD_PIN_PA_CTRL, .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&pa), TAG, "PA_CTRL");
    gpio_set_level(BOARD_PIN_PA_CTRL, 0); /* speaker amp off */

    i2c_master_bus_config_t bus = {
        .i2c_port = -1,
        .sda_io_num = BOARD_PIN_I2C_SDA,
        .scl_io_num = BOARD_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, /* besides the external 2.2k, as the vendor does; also
                                               * silences ESP-IDF's missing-pull-up warning */
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c), TAG, "I2C bus");

    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "GPIO ISR service");

    if (cold) {
        report_devices();
        err = write_codec(0x18, k_es8311_standby, sizeof(k_es8311_standby) / sizeof(k_es8311_standby[0]));
        if (err == ESP_OK) {
            err = write_codec(0x40, k_es7210_standby, sizeof(k_es7210_standby) / sizeof(k_es7210_standby[0]));
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "codec standby failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "audio codecs in standby");
        }
    }
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c(void)
{
    return s_i2c;
}
```

`components/board/include/board_buttons.h`:

```c
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "gesture.h"

/*
 * KEY and BOOT buttons -> gestures (spec §5.6). Edges arrive by interrupt; a task times the
 * gestures and reports them through the callback, from that task.
 */

typedef enum {
    BOARD_BUTTON_KEY,
    BOARD_BUTTON_BOOT,
    BOARD_BUTTON_COUNT,
} board_button_t;

typedef void (*board_button_cb_t)(board_button_t button, gesture_t gesture);

esp_err_t board_buttons_start(board_button_cb_t cb, const gesture_config_t config[BOARD_BUTTON_COUNT]);
/* The `btn` console command: reports a gesture as if the button had made it. */
void board_buttons_inject(board_button_t button, gesture_t gesture);
/* A button woke the chip: time it from now if it is still held, else count a tap (spec §3.3). */
void board_buttons_woke(board_button_t button);
/* Re-reads both levels, e.g. after light sleep, when edge interrupts may have been missed. */
void board_buttons_resync(void);
/* True while a gesture is being timed or an edge is queued; sleeping then would lose it. */
bool board_buttons_busy(void);
bool board_buttons_pressed(board_button_t button);
const char *board_button_name(board_button_t button);
const char *board_gesture_name(gesture_t gesture);
```

`components/board/board_buttons.c`:

```c
#include "board_buttons.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "util_ticks.h"

#define TASK_STACK    3072
#define TASK_PRIORITY 6 /* above the app task: gestures are timed while it renders */
#define QUEUE_DEPTH   16

static const char *TAG = "buttons";

typedef enum {
    MSG_EDGE,
    MSG_INJECT,
    MSG_WOKE,
    MSG_RESYNC,
} msg_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t button;
    uint8_t gesture;
} msg_t;

static const gpio_num_t k_pins[BOARD_BUTTON_COUNT] = { BOARD_PIN_KEY, BOARD_PIN_BOOT };
static QueueHandle_t s_queue;
static board_button_cb_t s_cb;
static gesture_recogniser_t s_rec[BOARD_BUTTON_COUNT];
static volatile bool s_busy;

static void IRAM_ATTR on_edge(void *arg)
{
    msg_t msg = { .kind = MSG_EDGE, .button = (uint8_t)(uintptr_t)arg };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &msg, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool board_buttons_pressed(board_button_t button)
{
    return gpio_get_level(k_pins[button]) == 0;
}

static void report(board_button_t button, gesture_t gesture)
{
    if (gesture != GESTURE_NONE) {
        ESP_LOGI(TAG, "%s %s", board_button_name(button), board_gesture_name(gesture));
        s_cb(button, gesture);
    }
}

static TickType_t next_wait(void)
{
    TickType_t wait = portMAX_DELAY;
    uint32_t now = now_ms();
    for (int b = 0; b < BOARD_BUTTON_COUNT; b++) {
        uint32_t deadline = gesture_deadline(&s_rec[b]);
        if (deadline == GESTURE_NO_DEADLINE) {
            continue;
        }
        int32_t left = (int32_t)(deadline - now);
        TickType_t ticks = left <= 0 ? 0 : util_ticks_at_least((uint32_t)left, portTICK_PERIOD_MS);
        if (ticks < wait) {
            wait = ticks;
        }
    }
    return wait;
}

static void buttons_task(void *arg)
{
    (void)arg;
    for (;;) {
        msg_t msg;
        if (xQueueReceive(s_queue, &msg, next_wait()) == pdTRUE) {
            board_button_t b = (board_button_t)msg.button;
            switch (msg.kind) {
            case MSG_EDGE:
                report(b, gesture_update(&s_rec[b], board_buttons_pressed(b), now_ms()));
                break;
            case MSG_INJECT:
                report(b, (gesture_t)msg.gesture);
                break;
            case MSG_WOKE:
                if (board_buttons_pressed(b)) {
                    report(b, gesture_update(&s_rec[b], true, now_ms()));
                } else {
                    report(b, gesture_tap(&s_rec[b], now_ms()));
                }
                break;
            case MSG_RESYNC:
                for (int i = 0; i < BOARD_BUTTON_COUNT; i++) {
                    report((board_button_t)i, gesture_update(&s_rec[i], board_buttons_pressed(i), now_ms()));
                }
                break;
            }
        }
        bool busy = false;
        for (int b = 0; b < BOARD_BUTTON_COUNT; b++) {
            report((board_button_t)b, gesture_update(&s_rec[b], board_buttons_pressed(b), now_ms()));
            busy |= gesture_busy(&s_rec[b]);
        }
        s_busy = busy;
    }
}

static void post(msg_t msg)
{
    s_busy = true;
    xQueueSend(s_queue, &msg, pdMS_TO_TICKS(100));
}

esp_err_t board_buttons_start(board_button_cb_t cb, const gesture_config_t config[BOARD_BUTTON_COUNT])
{
    s_cb = cb;
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(msg_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue");
    for (int b = 0; b < BOARD_BUTTON_COUNT; b++) {
        gesture_init(&s_rec[b], config[b]);
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << k_pins[b],
            .mode = GPIO_MODE_INPUT,
            .intr_type = GPIO_INTR_ANYEDGE, /* both buttons have external pull-ups */
        };
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "button pin");
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(k_pins[b], on_edge, (void *)(uintptr_t)b), TAG, "button ISR");
    }
    BaseType_t ok = xTaskCreatePinnedToCore(buttons_task, "buttons", TASK_STACK, NULL, TASK_PRIORITY, NULL,
                                            tskNO_AFFINITY);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

void board_buttons_inject(board_button_t button, gesture_t gesture)
{
    post((msg_t){ .kind = MSG_INJECT, .button = (uint8_t)button, .gesture = (uint8_t)gesture });
}

void board_buttons_woke(board_button_t button)
{
    post((msg_t){ .kind = MSG_WOKE, .button = (uint8_t)button });
}

void board_buttons_resync(void)
{
    post((msg_t){ .kind = MSG_RESYNC });
}

bool board_buttons_busy(void)
{
    return s_busy || uxQueueMessagesWaiting(s_queue) > 0;
}

const char *board_button_name(board_button_t button)
{
    return button == BOARD_BUTTON_KEY ? "KEY" : "BOOT";
}

const char *board_gesture_name(gesture_t gesture)
{
    switch (gesture) {
    case GESTURE_SHORT:
        return "short";
    case GESTURE_DOUBLE:
        return "double";
    case GESTURE_LONG:
        return "long";
    default:
        return "none";
    }
}
```

`components/board/CMakeLists.txt`:

```cmake
# Board bring-up, pins, buttons (spec §3.1). gesture.c is pure C and also built on the host.
idf_component_register(SRCS "board.c" "board_buttons.c" "gesture.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES esp_driver_gpio esp_timer util)
```

- [ ] **Step 3: Take the panel pins from `board_pins.h`**

In `components/st7305/st7305.c`:
- Add `#include "board_pins.h"` above `#include "driver/gpio.h"`.
- Replace the six `#define PIN_…` lines with:

```c
#define PIN_MOSI    BOARD_PIN_LCD_MOSI
#define PIN_SCLK    BOARD_PIN_LCD_SCK
#define PIN_DC      BOARD_PIN_LCD_DC
#define PIN_CS      BOARD_PIN_LCD_CS
#define PIN_RST     BOARD_PIN_LCD_RST
#define PIN_TE      BOARD_PIN_LCD_TE
```

In `components/st7305/CMakeLists.txt`, change `PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_lcd util` to `PRIV_REQUIRES board esp_driver_gpio esp_driver_spi esp_lcd util`.

- [ ] **Step 4: Add `btn`, and start the board and the buttons in `main`**

`components/diag/diag_cmd_buttons.c`:

```c
#include <stdio.h>
#include <string.h>

#include "board_buttons.h"
#include "diag_internal.h"
#include "esp_console.h"

static int usage(const char *text)
{
    printf("usage: %s\n", text);
    return 1;
}

/* btn <key|boot> <short|double|long>: runs on the console task; the buttons task reports it. */
static int cmd_btn(int argc, char **argv)
{
    static const char *const k_usage = "btn <key|boot> <short|double|long>";
    if (argc != 3) {
        return usage(k_usage);
    }
    board_button_t button;
    if (strcmp(argv[1], "key") == 0) {
        button = BOARD_BUTTON_KEY;
    } else if (strcmp(argv[1], "boot") == 0) {
        button = BOARD_BUTTON_BOOT;
    } else {
        return usage(k_usage);
    }
    gesture_t gesture;
    if (strcmp(argv[2], "short") == 0) {
        gesture = GESTURE_SHORT;
    } else if (strcmp(argv[2], "double") == 0) {
        gesture = GESTURE_DOUBLE;
    } else if (strcmp(argv[2], "long") == 0) {
        gesture = GESTURE_LONG;
    } else {
        return usage(k_usage);
    }
    board_buttons_inject(button, gesture);
    printf("btn: %s %s\n", board_button_name(button), board_gesture_name(gesture));
    return 0;
}

esp_err_t diag_register_button_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "btn", .help = "btn <key|boot> <short|double|long>: inject a button gesture", .func = &cmd_btn },
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

In `components/diag/diag_internal.h`, add `esp_err_t diag_register_button_commands(void);  /* btn */`. In `components/diag/diag.c`, after the display-commands line, add `ESP_RETURN_ON_ERROR(diag_register_button_commands(), TAG, "button commands");`.

`components/diag/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "diag.c" "diag_cmd_buttons.c" "diag_cmd_display.c" "diag_cmd_system.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board console display esp_app_format esp_driver_usb_serial_jtag esp_psram gfx
                                     spi_flash util vfs)
```

`main/main.c`:

```c
#include "board.h"
#include "board_buttons.h"
#include "diag.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gfx_test_pattern.h"

static const char *TAG = "main";

/* The owner's pick at the M1 panel check (2026-09-25): the factory sequence has the better contrast.
 * The LPM refresh rate stays at the st7305 default of 1 Hz, which showed no contrast loss. */
#define DISPLAY_VARIANT ST7305_VARIANT_FACTORY

/* Dashboard bindings (spec §5.6): KEY double toggles auto-cycle, BOOT long needs 3 s. */
static const gesture_config_t k_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = true },
    [BOARD_BUTTON_BOOT] = { .long_ms = 3000, .double_enabled = false },
};

static void on_button(board_button_t button, gesture_t gesture)
{
    (void)button;
    (void)gesture; /* the buttons task logs each gesture; the app task binds them (M2 Task 12) */
}

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);

    esp_err_t err = board_init(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(err));
    }

    err = display_init(DISPLAY_VARIANT);
    if (err == ESP_OK) {
        gfx_draw_test_pattern(display_fb());
        err = display_commit(false);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display failed: %s", esp_err_to_name(err));
    }

    err = board_buttons_start(on_button, k_buttons);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "buttons failed: %s", esp_err_to_name(err));
    }

    err = diag_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "reflbo ready");
}
```

In `main/CMakeLists.txt`, change `PRIV_REQUIRES diag display esp_app_format gfx` to `PRIV_REQUIRES board diag display esp_app_format gfx`.

- [ ] **Step 5: Build, flash and watch the boot log**

```bash
tools/idf.sh reconfigure >/dev/null && tools/idf.sh build 2>&1 | tee captures/build.log | tail -1
grep "warning:" captures/build.log | grep -cE "/reflbo/(main|components)/"
tools/idf.sh -p PORT flash
tools/idf.sh exec python tools/devlog.py -p PORT --reset --until "reflbo ready" -t 20 -o captures/boot.log >/dev/null; echo "exit=$?"
grep -E "board:|(^|reflbo> )[EW] \(" captures/boot.log
```

Expected:
- `Project build complete` and `0` own warnings.
- `exit=0`.
- The log shows `board: I2C 0x18 ES8311 present`, `0x40 ES7210`, `0x51 PCF85063`, `0x70 SHTC3` and `board: audio codecs in standby`.
- No `E (` or `W (` lines.

- [ ] **Step 6: Inject gestures and check the panel still works**

```bash
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "btn key short" --cmd "btn boot long" -t 15 | grep -E "buttons:|^btn:"
tools/idf.sh exec python tools/screenshot.py -p PORT --compare test/host/golden/test_pattern.pbm; echo "exit=$?"
```

Expected:
- `buttons: KEY short`, `btn: KEY short`, `buttons: BOOT long`, `btn: BOOT long`.
- `screenshot: identical to test/host/golden/test_pattern.pbm`, `exit=0`: `main` still draws the test pattern.

- [ ] **Step 7: Record the hardware facts**

In `AGENTS.md` §3.4:
- Extend gotcha 8 with: "The vendor firmware never puts the codecs into standby and keeps the amp enabled; `board_init()` writes the esp_codec_dev standby sequences at every cold boot (they keep that state through deep sleep)."
- Add gotcha 16: "**KEY (GPIO18) has no debounce capacitor** (BOOT has 100 nF). Buttons are debounced in software (30 ms, spec §5.6)."
- Add gotcha 17: "**Battery drain the firmware cannot remove** (schematic estimate): the BAT_ADC divider, 10–14 µA, and the ideal-diode bias (Q6 into R16), about 30–36 µA, both straight from VBAT, even with PWR off."
- Add gotcha 18: "**ESP-IDF timeouts in ms become ticks rounded down.** At the 100 Hz tick, an I²C timeout below 10 ms is 0 ticks and fails at once; use 20 ms or more (the drivers use 50). Datasheet minimum delays go through `util_ticks_at_least()`."

In AGENTS §5.3 "Shared resources": add "`board` owns the I²C bus (GPIO 13/14) and the GPIO ISR service."

In §7 "Available now": add `btn <key|boot> <short|double|long>`, and remove `btn` from the planned list.

- [ ] **Step 8: Commit and push**

```bash
git add components/board components/st7305 components/diag main AGENTS.md
git commit -m "feat(board): add board bring-up, the button task and the btn command"
git push
```

---

### Task 11: RTC, timekeeping and sensors; `rtc`, `sensors`, `battery`

**Files:**
- Create: `components/rtc/include/pcf85063.h`, `components/rtc/pcf85063.c`, `components/timekeeping/include/timekeeping.h`, `components/timekeeping/timekeeping.c`, `components/sensors/include/sensors.h`, `components/sensors/sensors.c`, `components/diag/diag_cmd_sensors.c`, `main/Kconfig.projbuild`
- Modify: the `rtc`, `timekeeping` and `sensors` `CMakeLists.txt`, `components/diag/include/diag.h`, `components/diag/diag.c`, `components/diag/diag_internal.h`, `components/diag/CMakeLists.txt`, `main/main.c`, `main/CMakeLists.txt`, `AGENTS.md`

**Interfaces:**
- Consumes: `board_i2c()` (Task 10), `pcf85063_*` codec (Task 5), `timekeeping_parse_iso8601` (Task 6), `shtc3_*` and `battery_gauge_*` (Task 4), `util_ticks_at_least`.
- Produces:
  - `pcf85063_init(bus)`, `pcf85063_read(&utc, &valid)`, `pcf85063_write(utc)`, `pcf85063_set_alarm(wake)`, `pcf85063_clear_alarm()`.
    - `pcf85063_write` retries until the oscillator-stop flag clears, for up to 2 s.
    - `pcf85063_set_alarm` also clears AF, which releases INT.
  - `timekeeping_init(tz)`, `timekeeping_load_from_rtc()`, `timekeeping_valid()`, `timekeeping_set_utc(utc)`.
  - `sensors_env_t`, `sensors_battery_t`, `sensors_state_t`.
  - `sensors_init(bus, cold)`, `sensors_sample_env(now)`, `sensors_sample_battery(now)`, `sensors_env()`, `sensors_battery(now)`, `sensors_export()`, `sensors_import()`.
  - `diag_set_executor(diag_executor_t)` and the internal `diag_on_owner(body, argc, argv)`. With no executor, a body runs on the console task.
  - Kconfig: `REFLBO_TZ`, `REFLBO_TEMP_OFFSET_C10`, `REFLBO_HUM_OFFSET_PCT10`, `REFLBO_BATTERY_FACTOR_PERMILLE`.
  - The console commands `rtc get`, `rtc set <ISO 8601>`, `sensors` and `battery`.
- SHTC3 handling:
  - normal mode, T first, clock stretching off (0x7866), then a 13 ms wait
  - sleep (0xB098) after every read, because it idles at 45 µA otherwise
  - one retry (spec §16)
  - Ruling: normal mode, not low power. The datasheet gives no accuracy for low-power mode, and the energy difference is about 0.04 mAh/day at 5-minute samples.
- Battery: one discarded sample, then 16 samples at 12 dB with curve-fitting calibration, ×3 for the divider, times the per-device factor.

- [ ] **Step 1: Watch the device check fail**

Run: `tools/idf.sh exec python tools/devlog.py -p PORT --cmd "rtc get" -t 10 | grep -E "^rtc|unknown"`
Expected: `unknown command: rtc`.

- [ ] **Step 2: Write the drivers**

`components/rtc/include/pcf85063.h`:

```c
#pragma once

#include <stdbool.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * PCF85063A real-time clock on the shared I²C bus (spec §7). Stores UTC. Its alarm drives the
 * minute wakes: AF latches and holds INT (GPIO15) low until pcf85063_set_alarm() or pcf85063_clear_alarm().
 * Call from the app task only.
 */

/* Configures the chip: 24 h, CLKOUT off, alarm interrupt on, timer off. Keeps the time. */
esp_err_t pcf85063_init(i2c_master_bus_handle_t bus);
/* `valid` is false while the oscillator-stop flag is set (time lost, e.g. at PWR-off). */
esp_err_t pcf85063_read(time_t *utc, bool *valid);
/* Sets the time and clears the oscillator-stop flag; fails if the oscillator does not run. */
esp_err_t pcf85063_write(time_t utc);
/* Arms the alarm for `wake` (a whole minute) and clears a pending alarm flag. */
esp_err_t pcf85063_set_alarm(time_t wake);
esp_err_t pcf85063_clear_alarm(void);
```

`components/rtc/pcf85063.c`:

```c
#include "pcf85063.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pcf85063_regs.h"

#define I2C_TIMEOUT_MS  50 /* at least two 10 ms ticks; shorter timeouts round down to zero */
#define OS_CLEAR_TRIES  20 /* the oscillator can take up to 2 s to start (datasheet §8.3.1.1) */

static const char *TAG = "pcf85063";

static i2c_master_dev_handle_t s_dev;

static esp_err_t write_regs(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[1 + PCF85063_TIME_LEN];
    ESP_RETURN_ON_FALSE(len <= PCF85063_TIME_LEN, ESP_ERR_INVALID_SIZE, TAG, "write too long");
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) {
        buf[1 + i] = data[i];
    }
    return i2c_master_transmit(s_dev, buf, len + 1, I2C_TIMEOUT_MS);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
}

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus)
{
    if (s_dev == NULL) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = PCF85063_ADDR,
            .scl_speed_hz = 400000,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");
    }
    const uint8_t control[] = { PCF85063_CONTROL_1_RUN, PCF85063_CONTROL_2_RUN };
    ESP_RETURN_ON_ERROR(write_regs(PCF85063_REG_CONTROL_1, control, sizeof(control)), TAG, "control");
    const uint8_t timer = PCF85063_TIMER_MODE_OFF;
    return write_regs(PCF85063_REG_TIMER_MODE, &timer, 1);
}

esp_err_t pcf85063_read(time_t *utc, bool *valid)
{
    uint8_t regs[PCF85063_TIME_LEN];
    ESP_RETURN_ON_ERROR(read_regs(PCF85063_REG_SECONDS, regs, sizeof(regs)), TAG, "read time");
    bool stopped;
    ESP_RETURN_ON_FALSE(pcf85063_decode_time(regs, utc, &stopped), ESP_ERR_INVALID_RESPONSE, TAG,
                        "impossible time %02x %02x %02x %02x", regs[0], regs[1], regs[2], regs[3]);
    *valid = !stopped;
    return ESP_OK;
}

esp_err_t pcf85063_write(time_t utc)
{
    for (int attempt = 0; attempt < OS_CLEAR_TRIES; attempt++) {
        uint8_t regs[PCF85063_TIME_LEN];
        pcf85063_encode_time(utc, regs);
        ESP_RETURN_ON_ERROR(write_regs(PCF85063_REG_SECONDS, regs, sizeof(regs)), TAG, "write time");
        uint8_t seconds;
        ESP_RETURN_ON_ERROR(read_regs(PCF85063_REG_SECONDS, &seconds, 1), TAG, "read back");
        if ((seconds & 0x80) == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); /* oscillator not stable yet (only after power-on): retry */
    }
    ESP_LOGE(TAG, "oscillator-stop flag stays set: the RTC oscillator is not running");
    return ESP_ERR_INVALID_STATE;
}

esp_err_t pcf85063_set_alarm(time_t wake)
{
    uint8_t regs[PCF85063_ALARM_LEN];
    pcf85063_encode_alarm(wake, regs);
    ESP_RETURN_ON_ERROR(write_regs(PCF85063_REG_ALARM, regs, sizeof(regs)), TAG, "alarm");
    return pcf85063_clear_alarm();
}

esp_err_t pcf85063_clear_alarm(void)
{
    const uint8_t control_2 = PCF85063_CONTROL_2_RUN; /* AF = 0 clears it; the other flags stay */
    return write_regs(PCF85063_REG_CONTROL_2, &control_2, 1);
}
```

`components/rtc/CMakeLists.txt`:

```cmake
# PCF85063A RTC (spec §7). pcf85063_regs.c is pure C and also built on the host.
idf_component_register(SRCS "pcf85063.c" "pcf85063_regs.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES util)
```

`components/timekeeping/include/timekeeping.h`:

```c
#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

/* System time from the RTC, time zone, manual set (spec §7). Call from the app task only. */

esp_err_t timekeeping_init(const char *tz_posix);
/* Reads the RTC and sets the system time. The time counts as invalid while the RTC's
 * oscillator-stop flag is set, until timekeeping_set_utc(). */
esp_err_t timekeeping_load_from_rtc(void);
bool timekeeping_valid(void);
esp_err_t timekeeping_set_utc(time_t utc);
```

`components/timekeeping/timekeeping.c`:

```c
#include "timekeeping.h"

#include <stdlib.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "pcf85063.h"

static const char *TAG = "timekeeping";

static bool s_valid;

esp_err_t timekeeping_init(const char *tz_posix)
{
    ESP_RETURN_ON_FALSE(setenv("TZ", tz_posix, 1) == 0, ESP_ERR_NO_MEM, TAG, "TZ");
    tzset();
    return ESP_OK;
}

esp_err_t timekeeping_load_from_rtc(void)
{
    time_t utc;
    bool valid;
    ESP_RETURN_ON_ERROR(pcf85063_read(&utc, &valid), TAG, "RTC read");
    struct timeval tv = { .tv_sec = utc };
    settimeofday(&tv, NULL);
    s_valid = valid;
    return ESP_OK;
}

bool timekeeping_valid(void)
{
    return s_valid;
}

esp_err_t timekeeping_set_utc(time_t utc)
{
    ESP_RETURN_ON_ERROR(pcf85063_write(utc), TAG, "RTC write");
    struct timeval tv = { .tv_sec = utc };
    settimeofday(&tv, NULL);
    s_valid = true;
    ESP_LOGI(TAG, "time set to %lld", (long long)utc);
    return ESP_OK;
}
```

`components/timekeeping/CMakeLists.txt`:

```cmake
# System time, time zone and manual set (spec §7). timekeeping_iso.c is pure C and also built on the host.
idf_component_register(SRCS "timekeeping.c" "timekeeping_iso.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES rtc util)
```

`components/sensors/include/sensors.h`:

```c
#pragma once

#include <stdbool.h>
#include <time.h>

#include "battery_model.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * SHTC3 climate sensor and battery gauge (spec §8). Keeps the latest readings, which the display
 * and the console both show. Call from the app task only.
 */

typedef struct {
    bool valid;
    int temp_c100;  /* 0.01 °C, calibration offset applied */
    int hum_pct100; /* 0.01 %RH */
    time_t time;    /* when it was read (UTC) */
} sensors_env_t;

typedef struct {
    bool valid;
    int last_mv;     /* latest reading */
    int smoothed_mv; /* gauge voltage */
    int level;       /* % */
    battery_state_t state;
    time_t time;     /* latest reading (UTC) */
} sensors_battery_t;

/* State that must survive deep sleep (it lives in the app's RTC-RAM snapshot). */
typedef struct {
    sensors_env_t env;
    battery_gauge_t gauge;
    int last_mv;
    time_t battery_time;
} sensors_state_t;

/* `cold`: after power-on; checks the SHTC3's id and sends it to sleep. */
esp_err_t sensors_init(i2c_master_bus_handle_t bus, bool cold);
esp_err_t sensors_sample_env(time_t now);
esp_err_t sensors_sample_battery(time_t now);
sensors_env_t sensors_env(void);
sensors_battery_t sensors_battery(time_t now);
void sensors_export(sensors_state_t *out);
void sensors_import(const sensors_state_t *in);
```

`components/sensors/sensors.c`:

```c
#include "sensors.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "shtc3_codec.h"
#include "util_ticks.h"

#define SHTC3_ADDR          0x70
#define SHTC3_CMD_WAKEUP    0x3517
#define SHTC3_CMD_SLEEP     0xB098
#define SHTC3_CMD_READ_ID   0xEFC8
#define SHTC3_CMD_MEASURE   0x7866 /* normal mode, T first, no clock stretching */
#define SHTC3_WAKEUP_US     300    /* datasheet: 240 µs max */
#define SHTC3_MEASURE_MS    13     /* datasheet: 12.1 ms max in normal mode */
#define I2C_TIMEOUT_MS      50     /* at least two 10 ms ticks; shorter timeouts round down to zero */

#define BAT_CHANNEL         ADC_CHANNEL_3 /* GPIO4 */
#define BAT_SAMPLES         16
#define BAT_DIVIDER         3             /* 200k / 100k */

static const char *TAG = "sensors";

static i2c_master_dev_handle_t s_shtc3;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static sensors_state_t s_state;

static esp_err_t shtc3_command(uint16_t cmd)
{
    const uint8_t bytes[2] = { (uint8_t)(cmd >> 8), (uint8_t)cmd };
    return i2c_master_transmit(s_shtc3, bytes, sizeof(bytes), I2C_TIMEOUT_MS);
}

static esp_err_t shtc3_wake(void)
{
    esp_err_t err = shtc3_command(SHTC3_CMD_WAKEUP);
    esp_rom_delay_us(SHTC3_WAKEUP_US);
    return err;
}

static esp_err_t shtc3_check_id(void)
{
    ESP_RETURN_ON_ERROR(shtc3_wake(), TAG, "wakeup");
    uint8_t id[3];
    esp_err_t err = shtc3_command(SHTC3_CMD_READ_ID);
    if (err == ESP_OK) {
        err = i2c_master_receive(s_shtc3, id, sizeof(id), I2C_TIMEOUT_MS);
    }
    shtc3_command(SHTC3_CMD_SLEEP);
    ESP_RETURN_ON_ERROR(err, TAG, "read id");
    ESP_RETURN_ON_FALSE(shtc3_crc8(id, 2) == id[2], ESP_ERR_INVALID_CRC, TAG, "id CRC");
    uint16_t value = (uint16_t)((id[0] << 8) | id[1]);
    ESP_RETURN_ON_FALSE((value & 0x083F) == 0x0807, ESP_ERR_NOT_FOUND, TAG, "unexpected id 0x%04x", value);
    return ESP_OK;
}

static esp_err_t adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &s_adc), TAG, "ADC unit");
    adc_oneshot_chan_cfg_t chan = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BAT_CHANNEL, &chan), TAG, "ADC channel");
    adc_cali_curve_fitting_config_t cali = {
        .unit_id = ADC_UNIT_1,
        .chan = BAT_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC calibration in eFuse; battery readings are approximate");
        s_cali = NULL;
    }
    return ESP_OK;
}

esp_err_t sensors_init(i2c_master_bus_handle_t bus, bool cold)
{
    if (s_shtc3 == NULL) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = SHTC3_ADDR,
            .scl_speed_hz = 400000,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_shtc3), TAG, "add SHTC3");
    }
    if (cold) {
        battery_gauge_init(&s_state.gauge);
        esp_err_t err = shtc3_check_id();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SHTC3 not ready: %s", esp_err_to_name(err));
        }
    }
    return adc_init();
}

static esp_err_t read_env_once(int *temp_c100, int *hum_pct100)
{
    ESP_RETURN_ON_ERROR(shtc3_wake(), TAG, "wakeup");
    esp_err_t err = shtc3_command(SHTC3_CMD_MEASURE);
    uint8_t raw[6];
    if (err == ESP_OK) {
        vTaskDelay(util_ticks_at_least(SHTC3_MEASURE_MS, portTICK_PERIOD_MS));
        err = i2c_master_receive(s_shtc3, raw, sizeof(raw), I2C_TIMEOUT_MS);
    }
    shtc3_command(SHTC3_CMD_SLEEP); /* 45 µA idle otherwise */
    ESP_RETURN_ON_ERROR(err, TAG, "measure");
    ESP_RETURN_ON_FALSE(shtc3_parse(raw, temp_c100, hum_pct100), ESP_ERR_INVALID_CRC, TAG, "CRC");
    return ESP_OK;
}

esp_err_t sensors_sample_env(time_t now)
{
    int t, h;
    esp_err_t err = read_env_once(&t, &h);
    if (err != ESP_OK) {
        err = read_env_once(&t, &h); /* spec §16: retry once */
    }
    ESP_RETURN_ON_ERROR(err, TAG, "SHTC3");
    s_state.env = (sensors_env_t){
        .valid = true,
        .temp_c100 = t + CONFIG_REFLBO_TEMP_OFFSET_C10 * 10,
        .hum_pct100 = h + CONFIG_REFLBO_HUM_OFFSET_PCT10 * 10,
        .time = now,
    };
    return ESP_OK;
}

esp_err_t sensors_sample_battery(time_t now)
{
    int raw, sum = 0, count = 0;
    adc_oneshot_read(s_adc, BAT_CHANNEL, &raw); /* the first sample after a pause reads low */
    for (int i = 0; i < BAT_SAMPLES; i++) {
        if (adc_oneshot_read(s_adc, BAT_CHANNEL, &raw) != ESP_OK) {
            continue;
        }
        int mv = raw * 3100 / 4095; /* rough 12 dB range without calibration */
        if (s_cali != NULL) {
            adc_cali_raw_to_voltage(s_cali, raw, &mv);
        }
        sum += mv;
        count++;
    }
    ESP_RETURN_ON_FALSE(count > 0, ESP_FAIL, TAG, "ADC read");
    int mv = sum / count * BAT_DIVIDER * CONFIG_REFLBO_BATTERY_FACTOR_PERMILLE / 1000;
    s_state.last_mv = mv;
    s_state.battery_time = now;
    battery_gauge_add(&s_state.gauge, (uint32_t)now, mv);
    return ESP_OK;
}

sensors_env_t sensors_env(void)
{
    return s_state.env;
}

sensors_battery_t sensors_battery(time_t now)
{
    return (sensors_battery_t){
        .valid = battery_gauge_level(&s_state.gauge) >= 0,
        .last_mv = s_state.last_mv,
        .smoothed_mv = battery_gauge_mv(&s_state.gauge),
        .level = battery_gauge_level(&s_state.gauge),
        .state = battery_gauge_state(&s_state.gauge, (uint32_t)now),
        .time = s_state.battery_time,
    };
}

void sensors_export(sensors_state_t *out)
{
    *out = s_state;
}

void sensors_import(const sensors_state_t *in)
{
    s_state = *in;
}
```

`components/sensors/CMakeLists.txt`:

```cmake
# SHTC3 and battery gauge (spec §8). shtc3_codec.c and battery_model.c are pure C and also built on the host.
idf_component_register(SRCS "sensors.c" "shtc3_codec.c" "battery_model.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES esp_adc util)
```

`main/Kconfig.projbuild`:

```kconfig
menu "reflbo"

    config REFLBO_TZ
        string "Time zone (POSIX TZ string)"
        default "CET-1CEST,M3.5.0,M10.5.0/3"
        help
            Default time zone, Europe/Prague (spec §7). Settings override it from M3.

    config REFLBO_TEMP_OFFSET_C10
        int "SHTC3 temperature offset (0.1 °C)"
        range -100 100
        default 0
        help
            Added to every reading; the board warms the sensor (AGENTS.md gotcha 10). Calibrate
            against a reference thermometer (spec §8).

    config REFLBO_HUM_OFFSET_PCT10
        int "SHTC3 humidity offset (0.1 %RH)"
        range -200 200
        default 0

    config REFLBO_BATTERY_FACTOR_PERMILLE
        int "Battery voltage calibration factor (per mille)"
        range 900 1100
        default 1000
        help
            Per-device correction for the ADC and the 200k/100k divider (spec §8).

endmenu
```

- [ ] **Step 3: Add the executor hook and the commands**

`components/diag/include/diag.h`:

```c
#pragma once

#include "esp_err.h"

/**
 * Start the USB-Serial-JTAG console REPL and register the diagnostic commands (spec §15).
 * Call once from app_main after the rest of the system is up.
 */
esp_err_t diag_start(void);

/* Runs fn(arg) on the task that owns the hardware (the app task, spec §3.2) and waits for it. */
typedef esp_err_t (*diag_executor_t)(void (*fn)(void *arg), void *arg);
/* Commands that touch the display, I²C devices or sleep state run through this; set it before
 * diag_start(). Without one they run on the console task. */
void diag_set_executor(diag_executor_t executor);
```

In `components/diag/diag.c`, insert this above `static void diag_repl_task(void *arg)`:

```c
static diag_executor_t s_executor;

void diag_set_executor(diag_executor_t executor)
{
    s_executor = executor;
}

typedef struct {
    int (*body)(int argc, char **argv);
    int argc;
    char **argv;
    int ret;
} diag_call_t;

static void call_body(void *arg)
{
    diag_call_t *call = arg;
    call->ret = call->body(call->argc, call->argv);
}

int diag_on_owner(int (*body)(int argc, char **argv), int argc, char **argv)
{
    diag_call_t call = { .body = body, .argc = argc, .argv = argv, .ret = 1 };
    if (s_executor == NULL) {
        call_body(&call);
        return call.ret;
    }
    esp_err_t err = s_executor(call_body, &call);
    if (err != ESP_OK) {
        printf("%s: %s\n", argv[0], esp_err_to_name(err));
        return 1;
    }
    return call.ret;
}
```

Also add `ESP_RETURN_ON_ERROR(diag_register_sensor_commands(), TAG, "sensor commands");` after the button-commands line.

In `components/diag/diag_internal.h`, add:

```c
esp_err_t diag_register_sensor_commands(void);  /* sensors, battery, rtc */

/* Runs a command body on the hardware owner's task (see diag_set_executor) and returns its result. */
int diag_on_owner(int (*body)(int argc, char **argv), int argc, char **argv);
```

`components/diag/diag_cmd_sensors.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "diag_internal.h"
#include "esp_console.h"
#include "pcf85063.h"
#include "sensors.h"
#include "timekeeping.h"
#include "timekeeping_iso.h"

static int usage(const char *text)
{
    printf("usage: %s\n", text);
    return 1;
}

static void format_local(time_t t, char *out, size_t size)
{
    struct tm local;
    localtime_r(&t, &local);
    strftime(out, size, "%a %Y-%m-%d %H:%M:%S %Z", &local);
}

static int sensors_body(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = sensors_sample_env(time(NULL));
    if (err != ESP_OK) {
        printf("sensors: SHTC3 read failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    sensors_env_t env = sensors_env();
    printf("sensors: %d.%02d C, %d.%02d %%RH\n", env.temp_c100 / 100, abs(env.temp_c100 % 100), env.hum_pct100 / 100,
           env.hum_pct100 % 100);
    return 0;
}

static const char *battery_state_name(battery_state_t state)
{
    switch (state) {
    case BATTERY_DISCHARGING:
        return "discharging";
    case BATTERY_CHARGING:
        return "charging";
    case BATTERY_FULL:
        return "full";
    default:
        return "unknown";
    }
}

static int battery_body(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    time_t now = time(NULL);
    esp_err_t err = sensors_sample_battery(now);
    if (err != ESP_OK) {
        printf("battery: ADC read failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    sensors_battery_t b = sensors_battery(now);
    printf("battery: %d mV now, %d mV smoothed, %d %%, %s\n", b.last_mv, b.smoothed_mv, b.level,
           battery_state_name(b.state));
    return 0;
}

static int rtc_body(int argc, char **argv)
{
    static const char *const k_usage = "rtc get | rtc set <YYYY-MM-DDTHH:MM[:SS][Z|+HH:MM]>";
    if (argc == 2 && strcmp(argv[1], "get") == 0) {
        time_t utc;
        bool valid;
        esp_err_t err = pcf85063_read(&utc, &valid);
        if (err != ESP_OK) {
            printf("rtc: %s\n", esp_err_to_name(err));
            return 1;
        }
        char iso[32], local[48];
        struct tm tm_utc;
        gmtime_r(&utc, &tm_utc);
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        format_local(utc, local, sizeof(local));
        printf("rtc: %s (%s), local %s\n", iso, valid ? "valid" : "INVALID: oscillator stopped, set the time",
               local);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "set") == 0) {
        time_t utc;
        if (!timekeeping_parse_iso8601(argv[2], &utc)) {
            return usage(k_usage);
        }
        esp_err_t err = timekeeping_set_utc(utc);
        if (err != ESP_OK) {
            printf("rtc: %s\n", esp_err_to_name(err));
            return 1;
        }
        char local[48];
        format_local(utc, local, sizeof(local));
        printf("rtc: set, local %s\n", local);
        return 0;
    }
    return usage(k_usage);
}

static int cmd_sensors(int argc, char **argv)
{
    return diag_on_owner(sensors_body, argc, argv);
}

static int cmd_battery(int argc, char **argv)
{
    return diag_on_owner(battery_body, argc, argv);
}

static int cmd_rtc(int argc, char **argv)
{
    return diag_on_owner(rtc_body, argc, argv);
}

esp_err_t diag_register_sensor_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "sensors", .help = "Read the SHTC3 now", .func = &cmd_sensors },
        { .command = "battery", .help = "Read the battery now and show the gauge", .func = &cmd_battery },
        { .command = "rtc", .help = "rtc get | rtc set <ISO 8601 time; Z, +HH:MM or local>", .func = &cmd_rtc },
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

`components/diag/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "diag.c" "diag_cmd_buttons.c" "diag_cmd_display.c" "diag_cmd_sensors.c"
                            "diag_cmd_system.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board console display esp_app_format esp_driver_usb_serial_jtag esp_psram gfx
                                     rtc sensors spi_flash timekeeping util vfs)
```

`main/main.c`:

```c
#include "board.h"
#include "board_buttons.h"
#include "diag.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gfx_test_pattern.h"
#include "pcf85063.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "timekeeping.h"

static const char *TAG = "main";

/* The owner's pick at the M1 panel check (2026-09-25): the factory sequence has the better contrast.
 * The LPM refresh rate stays at the st7305 default of 1 Hz, which showed no contrast loss. */
#define DISPLAY_VARIANT ST7305_VARIANT_FACTORY

/* Dashboard bindings (spec §5.6): KEY double toggles auto-cycle, BOOT long needs 3 s. */
static const gesture_config_t k_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = true },
    [BOARD_BUTTON_BOOT] = { .long_ms = 3000, .double_enabled = false },
};

static void on_button(board_button_t button, gesture_t gesture)
{
    (void)button;
    (void)gesture; /* the buttons task logs each gesture; the app task binds them (M2 Task 12) */
}

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);

    esp_err_t err = board_init(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(err));
    }

    err = pcf85063_init(board_i2c());
    if (err == ESP_OK) {
        err = timekeeping_init(CONFIG_REFLBO_TZ);
    }
    if (err == ESP_OK) {
        err = timekeeping_load_from_rtc();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC failed: %s", esp_err_to_name(err));
    }
    err = sensors_init(board_i2c(), true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sensors failed: %s", esp_err_to_name(err));
    }

    err = display_init(DISPLAY_VARIANT);
    if (err == ESP_OK) {
        gfx_draw_test_pattern(display_fb());
        err = display_commit(false);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display failed: %s", esp_err_to_name(err));
    }

    err = board_buttons_start(on_button, k_buttons);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "buttons failed: %s", esp_err_to_name(err));
    }

    err = diag_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "reflbo ready");
}
```

In `main/CMakeLists.txt`, change `PRIV_REQUIRES board diag display esp_app_format gfx` to `PRIV_REQUIRES board diag display esp_app_format gfx rtc sensors timekeeping`.

- [ ] **Step 4: Build, flash and exercise the commands**

```bash
tools/idf.sh build 2>&1 | tee captures/build.log | tail -1
grep "warning:" captures/build.log | grep -cE "/reflbo/(main|components)/"
tools/idf.sh -p PORT flash
tools/idf.sh exec python tools/devlog.py -p PORT --reset --until "reflbo ready" -t 20 -o captures/boot.log >/dev/null; echo "exit=$?"
grep -E "(^|reflbo> )[EW] \(" captures/boot.log
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "rtc get" --cmd "rtc set $(date -u +%Y-%m-%dT%H:%M:%SZ)" --cmd "rtc get" --cmd "sensors" --cmd "battery" -t 25 | grep -E "^(rtc|sensors|battery):"; date -u +%H:%M:%S
```

Expected:
- `0` own warnings, `exit=0`, and no `E (` or `W (` lines.
- `rtc: …Z (valid)` or `(INVALID: oscillator stopped, set the time)`.
- `rtc: set, local …`.
- A second `rtc get` within 2 s of the Mac's UTC printed after it.
- `sensors: T C, H %RH`, with T between 10 and 45 and H between 5 and 95.
- `battery: N mV now, N mV smoothed, P %, unknown`, with N between 3000 and 4300 (4.0–4.2 V on USB with a charged cell).

- [ ] **Step 5: The time survives a reboot**

Run: `tools/idf.sh exec python tools/devlog.py -p PORT --cmd reboot --until "reflbo ready" -t 20 >/dev/null; tools/idf.sh exec python tools/devlog.py -p PORT --cmd "rtc get" -t 10 | grep "^rtc:"; date -u +%H:%M:%S`
Expected: `rtc: …Z (valid)`, within 2 s of the Mac.

- [ ] **Step 6: Record the RTC and SHTC3 facts**

In `AGENTS.md` §3.4:
- Replace gotcha 7 with: "**Use the PCF85063 for timing.** The ESP32 has no 32 kHz crystal, so its sleep timer drifts; the PCF85063 is the time source. Wake scheduling uses its alarm: the alarm flag (AF) latches and INT stays low until firmware clears it, which ext1 catches reliably. Don't use the minute interrupt. Depending on TI_TP it is a 1/64 s pulse or a level held until TF is cleared (datasheet §8.2.2.3, unverified here), and it shares INT with the alarm. **CLKOUT is on after power-up, and CLKOE (pin 3) is not connected** on the schematic, so firmware writes COF = 111 at every boot. The board puts 22 pF on each crystal pin, above the usual load rating, so expect the RTC to run slow (estimate 2–9 s/day); trim the Offset register against NTP at M5. The vendor firmware overwrote the time on every boot and never checked the oscillator-stop flag."
- Extend gotcha 10 with: "The SHTC3 idles at 45 µA unless sent to sleep, which the vendor code never did; `sensors.c` sleeps it after every read."

In §6, add to the command block:

```sh
tools/idf.sh exec python tools/devlog.py --cmd "rtc set $(date -u +%Y-%m-%dT%H:%M:%SZ)"   # set the RTC from this Mac
```

In §7 "Available now", add `sensors`, `battery`, `rtc get|set`, and remove them from the planned list.

- [ ] **Step 7: Commit and push**

```bash
git add components/rtc components/timekeeping components/sensors components/diag main AGENTS.md
git commit -m "feat(sensors): add the RTC, timekeeping and sensor drivers with console commands"
git push
```

---

### Task 12: The app task: live clock on the panel

**Files:**
- Create: `main/app.h`, `main/app.c`
- Modify: `main/main.c`, `main/CMakeLists.txt`, `main/Kconfig.projbuild`, `sdkconfig.defaults`, `components/diag/diag_cmd_display.c`, `components/diag/diag_cmd_system.c`, `AGENTS.md`

**Interfaces:**
- Consumes: everything above; `ui_draw_clock` (Task 8); `scheduler_*` (Task 2); `display_*` (M1).
- Produces:
  - `esp_err_t app_start(void)` and `esp_err_t app_execute(void (*fn)(void *), void *arg)`, the diag executor.
  - The app task: 8 KB stack, priority 5, core 1.
  - Events: RTC alarm (GPIO15 NEGEDGE ISR), buttons, console calls.
  - A backup tick 5 s after a missed alarm.
  - A rescheduling guard when the clock moves back.
  - Kconfig: `REFLBO_PANEL_INIT_{FACTORY,XIAOZHI}`, which replaces `DISPLAY_VARIANT` (an M1 deferred minor), plus `REFLBO_DISPLAY_UPDATE_MIN` and `REFLBO_SENSOR_INTERVAL_MIN`.
  - The console command `tasks`.
  - `screenshot` and `panel` now run on the app task.
- The loop stays awake; sleep arrives in Task 13.

- [ ] **Step 1: Watch the device checks fail**

Run: `tools/idf.sh exec python tools/devlog.py -p PORT --cmd tasks -t 10 | grep -E "unknown|Name"`
Expected: `unknown command: tasks`.

- [ ] **Step 2: Write the app task**

`main/app.h`:

```c
#pragma once

#include "esp_err.h"

/* The app task (spec §3.2): owns the display, the I²C devices and sleep. */
esp_err_t app_start(void);
/* diag executor: runs fn(arg) on the app task and waits for it. */
esp_err_t app_execute(void (*fn)(void *arg), void *arg);
```

`main/app.c`:

```c
#include "app.h"

#include <time.h>

#include "board.h"
#include "board_buttons.h"
#include "board_pins.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pcf85063.h"
#include "scheduler.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "timekeeping.h"
#include "ui_clock.h"
#include "util_ticks.h"

#define APP_STACK         8192
#define APP_PRIORITY      5
#define APP_CORE          1
#define QUEUE_DEPTH       16
#define BACKUP_S          5    /* wake anyway this long after a missed RTC alarm (spec §9.2) */

#if CONFIG_REFLBO_PANEL_INIT_XIAOZHI
#define PANEL_VARIANT ST7305_VARIANT_XIAOZHI
#else
#define PANEL_VARIANT ST7305_VARIANT_FACTORY
#endif

static const char *TAG = "app";

typedef enum {
    EV_RTC_ALARM,
    EV_BUTTON,
    EV_CALL,
} ev_type_t;

typedef struct {
    ev_type_t type;
    union {
        struct {
            board_button_t button;
            gesture_t gesture;
        } button;
        struct {
            void (*fn)(void *);
            void *arg;
            SemaphoreHandle_t done;
        } call;
    };
} app_event_t;

static QueueHandle_t s_queue;
static time_t s_next_wake;

/* Dashboard bindings (spec §5.6): KEY double toggles auto-cycle, BOOT long needs 3 s. */
static const gesture_config_t k_dashboard_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = true },
    [BOARD_BUTTON_BOOT] = { .long_ms = 3000, .double_enabled = false },
};

static void IRAM_ATTR on_rtc_int(void *arg)
{
    (void)arg;
    app_event_t ev = { .type = EV_RTC_ALARM };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &ev, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void on_button(board_button_t button, gesture_t gesture)
{
    app_event_t ev = { .type = EV_BUTTON, .button = { .button = button, .gesture = gesture } };
    xQueueSend(s_queue, &ev, pdMS_TO_TICKS(100));
}

esp_err_t app_execute(void (*fn)(void *arg), void *arg)
{
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(done != NULL, ESP_ERR_NO_MEM, TAG, "semaphore");
    app_event_t ev = { .type = EV_CALL, .call = { .fn = fn, .arg = arg, .done = done } };
    if (xQueueSend(s_queue, &ev, pdMS_TO_TICKS(1000)) != pdTRUE) {
        vSemaphoreDelete(done);
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    return ESP_OK;
}

static ui_battery_t ui_battery_state(battery_state_t state)
{
    switch (state) {
    case BATTERY_DISCHARGING:
        return UI_BATTERY_DISCHARGING;
    case BATTERY_CHARGING:
        return UI_BATTERY_CHARGING;
    case BATTERY_FULL:
        return UI_BATTERY_FULL;
    default:
        return UI_BATTERY_UNKNOWN;
    }
}

static void render(void)
{
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        return;
    }
    time_t now = time(NULL);
    ui_clock_t clock = { .time_valid = timekeeping_valid() };
    localtime_r(&now, &clock.local);
    sensors_env_t env = sensors_env();
    clock.env_valid = env.valid;
    clock.temp_c10 = (env.temp_c100 + (env.temp_c100 >= 0 ? 5 : -5)) / 10;
    clock.hum_pct = (env.hum_pct100 + 50) / 100;
    sensors_battery_t bat = sensors_battery(now);
    clock.battery_valid = bat.valid;
    clock.battery_pct = bat.level;
    clock.battery_mv = bat.smoothed_mv;
    clock.battery_state = ui_battery_state(bat.state);
    ui_draw_clock(fb, &clock);
    esp_err_t err = display_commit(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display: %s", esp_err_to_name(err));
    }
}

static void sample_sensors(time_t now)
{
    esp_err_t err = sensors_sample_env(now);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SHTC3: %s; keeping the last reading", esp_err_to_name(err));
    }
    err = sensors_sample_battery(now);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "battery: %s", esp_err_to_name(err));
    }
}

static void schedule_next(void)
{
    sched_input_t in = {
        .now = time(NULL),
        .display_every_min = CONFIG_REFLBO_DISPLAY_UPDATE_MIN,
        .sensors_every_min = CONFIG_REFLBO_SENSOR_INTERVAL_MIN,
    };
    s_next_wake = scheduler_next_wake(&in).when;
    esp_err_t err = pcf85063_set_alarm(s_next_wake); /* also clears the alarm flag, which releases INT */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC alarm: %s; the backup timer takes over", esp_err_to_name(err));
    }
}

/* A scheduled minute, from the RTC alarm or its backup. `force` also samples and renders. */
static void on_tick(bool force)
{
    esp_err_t err = timekeeping_load_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC read: %s", esp_err_to_name(err));
    }
    time_t now = time(NULL);
    time_t slot = now - now % 60;
    if (force || scheduler_is_slot(slot, CONFIG_REFLBO_SENSOR_INTERVAL_MIN)) {
        sample_sensors(now);
    }
    if (force || scheduler_is_slot(slot, CONFIG_REFLBO_DISPLAY_UPDATE_MIN)) {
        render();
    }
    schedule_next();
}

static void handle_button(board_button_t button, gesture_t gesture)
{
    if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT) {
        sample_sensors(time(NULL)); /* spec §5.6: refresh sensors */
        render();
        ESP_LOGI(TAG, "BOOT short: sensors refreshed");
        return;
    }
    ESP_LOGI(TAG, "%s %s is not bound yet (menu and presets come in M3, config mode in M4)",
             board_button_name(button), board_gesture_name(gesture));
}

/* The next wake must be at most one update interval away. If the clock moved back (`rtc set`,
 * later SNTP), the pending alarm and its backup timer are too far off: schedule again. A clock
 * that moved forward is caught by the backup tick instead. */
static void check_clock_jump(void)
{
    if (s_next_wake - time(NULL) > (CONFIG_REFLBO_DISPLAY_UPDATE_MIN + 1) * 60) {
        ESP_LOGW(TAG, "clock moved back; scheduling again");
        on_tick(true);
    }
}

static void handle_event(const app_event_t *ev)
{
    switch (ev->type) {
    case EV_RTC_ALARM:
        on_tick(false);
        break;
    case EV_BUTTON:
        handle_button(ev->button.button, ev->button.gesture);
        break;
    case EV_CALL:
        ev->call.fn(ev->call.arg);
        xSemaphoreGive(ev->call.done);
        break;
    }
}

static esp_err_t start_rtc_int(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_PIN_RTC_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, /* open drain, no external pull-up */
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "RTC INT pin");
    return gpio_isr_handler_add(BOARD_PIN_RTC_INT, on_rtc_int, NULL);
}

static esp_err_t boot(void)
{
    ESP_RETURN_ON_ERROR(board_init(true), TAG, "board");
    ESP_RETURN_ON_ERROR(pcf85063_init(board_i2c()), TAG, "RTC");
    ESP_RETURN_ON_ERROR(timekeeping_init(CONFIG_REFLBO_TZ), TAG, "time zone");
    esp_err_t err = timekeeping_load_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC read: %s", esp_err_to_name(err));
    }
    ESP_RETURN_ON_ERROR(sensors_init(board_i2c(), true), TAG, "sensors");
    ESP_RETURN_ON_ERROR(display_init(PANEL_VARIANT), TAG, "display");
    ESP_RETURN_ON_ERROR(board_buttons_start(on_button, k_dashboard_buttons), TAG, "buttons");
    ESP_RETURN_ON_ERROR(start_rtc_int(), TAG, "RTC INT");
    on_tick(true);
    ESP_LOGI(TAG, "reflbo ready");
    return ESP_OK;
}

/* Awake-only loop: sleep arrives with the power component (M2 Task 13). */
static void app_task(void *arg)
{
    (void)arg;
    esp_err_t err = boot();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "boot failed: %s; staying awake for the console", esp_err_to_name(err));
    }
    for (;;) {
        check_clock_jump();
        int64_t backup_ms = ((int64_t)(s_next_wake + BACKUP_S) - time(NULL)) * 1000;
        TickType_t wait = backup_ms <= 0 ? 0 : util_ticks_at_least((uint32_t)backup_ms, portTICK_PERIOD_MS);
        app_event_t ev;
        if (xQueueReceive(s_queue, &ev, wait) == pdTRUE) {
            handle_event(&ev);
        } else if (err == ESP_OK && time(NULL) >= s_next_wake + BACKUP_S) {
            ESP_LOGW(TAG, "RTC alarm missed; backup tick");
            on_tick(false);
        }
    }
}

esp_err_t app_start(void)
{
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(app_event_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue");
    BaseType_t ok = xTaskCreatePinnedToCore(app_task, "app", APP_STACK, NULL, APP_PRIORITY, NULL, APP_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}
```

`main/main.c`:

```c
#include "app.h"
#include "diag.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);

    esp_err_t err = app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "app failed to start: %s", esp_err_to_name(err));
    }

    diag_set_executor(app_execute);
    err = diag_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }
}
```

`main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.c" "app.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES board diag display esp_app_format esp_driver_gpio gfx rtc scheduler sensors
                                     timekeeping ui util)
```

In `main/Kconfig.projbuild`, add this before `endmenu`:

```cmake
    choice REFLBO_PANEL_INIT
        prompt "Panel init sequence"
        default REFLBO_PANEL_INIT_FACTORY
        help
            ST7305 vendor init sequence (decision D12). XiaoZhi's stays available for diagnostics.
        config REFLBO_PANEL_INIT_FACTORY
            bool "Factory firmware (better contrast)"
        config REFLBO_PANEL_INIT_XIAOZHI
            bool "XiaoZhi firmware"
    endchoice

    config REFLBO_DISPLAY_UPDATE_MIN
        int "Display update interval (minutes)"
        range 1 15
        default 1
        help
            Compile-time default for display.update_min (spec §9.2). Updates run at local wall-clock
            slots: every N minutes counted from midnight.

    config REFLBO_SENSOR_INTERVAL_MIN
        int "Sensor sample interval (minutes)"
        range 1 30
        default 5
```

Append to `sdkconfig.defaults`:

```text

# `tasks` console command
CONFIG_FREERTOS_USE_TRACE_FACILITY=y
CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS=y
```

- [ ] **Step 3: Route the display commands through the app task, and add `tasks`**

In `components/diag/diag_cmd_display.c`, rename `cmd_screenshot` to `screenshot_body` and `cmd_panel` to `panel_body`. Insert these wrappers above `diag_register_display_commands`:

```c
static int cmd_screenshot(int argc, char **argv)
{
    return diag_on_owner(screenshot_body, argc, argv);
}

static int cmd_panel(int argc, char **argv)
{
    return diag_on_owner(panel_body, argc, argv);
}
```

In `components/diag/diag_cmd_system.c`, insert this above `cmd_reboot`:

```c
static int cmd_tasks(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static char buf[1024];
    vTaskList(buf);
    printf("Name          State Prio Stack  Num Core\n%s", buf);
    return 0;
}
```

Also add `{ .command = "tasks", .help = "List FreeRTOS tasks", .func = &cmd_tasks },` to its command table, and update the header comment in `diag_internal.h` to `/* version, heap, reboot, tasks */`.

- [ ] **Step 4: Build with the new defaults, flash, boot**

```bash
rm -f sdkconfig && tools/idf.sh reconfigure >/dev/null
grep -E "^CONFIG_FREERTOS_USE_TRACE_FACILITY=y|^CONFIG_REFLBO_PANEL_INIT_FACTORY=y" sdkconfig
tools/idf.sh build 2>&1 | tee captures/build.log | tail -1
grep "warning:" captures/build.log | grep -cE "/reflbo/(main|components)/"
tools/idf.sh -p PORT flash
tools/idf.sh exec python tools/devlog.py -p PORT --reset --until "app: reflbo ready" -t 20 -o captures/boot.log >/dev/null; echo "exit=$?"
grep -E "(^|reflbo> )[EW] \(" captures/boot.log
tools/idf.sh exec python tools/devlog.py -p PORT --cmd tasks -t 10 | grep -E "^(app|buttons|diag_repl) "
```

Expected:
- Both config lines are present, and there are `0` own warnings.
- `exit=0`, with no `E (` or `W (` lines.
- `tasks` lists `app`, `buttons` and `diag_repl` with their free stack.

- [ ] **Step 5: The screen shows the time and the console's values**

```bash
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "btn boot short" --cmd "rtc get" --cmd "battery" -t 15 | grep -E "^(rtc|battery):|app: BOOT"
tools/idf.sh exec python tools/screenshot.py -p PORT -o captures/clock.png; date "+%H:%M"
```

Look at `captures/clock.png`. Expected:
- `app: BOOT short: sensors refreshed`.
- The large time is the Mac's local `HH:MM`, and the date is today.
- The voltage cell is within 0.02 V of `battery`'s smoothed value, and the level matches.
- The temperature and humidity are plausible. The screen shows the reading from the BOOT refresh; `sensors` would take a newer one.

- [ ] **Step 6: The minute updates, and clock jumps are handled**

```bash
tools/idf.sh exec python tools/screenshot.py -p PORT -o captures/clock1.png >/dev/null; T1=$(date +%H:%M)
sleep 62; tools/idf.sh exec python tools/screenshot.py -p PORT -o captures/clock2.png >/dev/null; T2=$(date +%H:%M); echo "$T1 -> $T2"
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "rtc set $(date -u -v-1H +%Y-%m-%dT%H:%M:%SZ)" -t 15 | grep -E "^rtc:|app:"
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "rtc set $(date -u +%Y-%m-%dT%H:%M:%SZ)" -t 15 | grep -E "^rtc:|app:"
```

Expected:
- `clock1.png` shows `T1` and `clock2.png` shows `T2`.
- Setting the clock back logs `app: clock moved back; scheduling again`. That is Review Focus 2.
- Setting it forward again logs `app: RTC alarm missed; backup tick` within 5 s.
- After both, the screen shows the current minute.

- [ ] **Step 7: Update the docs**

In `AGENTS.md`:
- §5.3 "Shared resources": the app task (`main/app.c`) now owns the display, `st7305`, the I²C devices and sleep. Console commands run on it through `diag_set_executor(app_execute)`; drop "until M2".
- §7 "Available now": add `tasks`.
- §8: in the Kconfig bullet, name the `reflbo` menu (`main/Kconfig.projbuild`).

- [ ] **Step 8: Commit and push**

```bash
git add main components/diag sdkconfig.defaults AGENTS.md
git commit -m "feat(app): run the clock screen from an app task that owns the hardware"
git push
```

---

### Task 13: Sleep: light and deep, tether policy, warm panel attach; `power`, `sleep`

**Files:**
- Create: `components/power/include/power.h`, `components/power/power.c`, `components/diag/diag_cmd_power.c`, `docs/power.md`
- Modify: `components/power/CMakeLists.txt`, `components/st7305/st7305.c`, `components/st7305/include/st7305.h`, `components/display/display.c`, `components/display/include/display.h`, `main/app.c`, `main/main.c`, `main/CMakeLists.txt`, `main/Kconfig.projbuild`, `sdkconfig.defaults`, `components/diag/diag.c`, `components/diag/diag_internal.h`, `components/diag/CMakeLists.txt`, `AGENTS.md`, `docs/specs/2026-09-25-firmware-design.md`

**Interfaces:**
- Consumes: `power_policy` (Task 7), `util_snapshot_*` (Task 1), `board_buttons_*` (Task 10), `pcf85063_set_alarm` (Task 11), the app task (Task 12).
- Produces:
  - `power_wake_t` (`POWER_WAKE_COLD`, `RTC`, `KEY`, `BOOT`, `TIMER`, `OTHER`) and `power_stats_t`.
  - `power_init()`, `power_boot_wake()`, `power_wake_name()`, `power_tethered()`, `power_hold_awake_ms()`, `power_plan(work_pending)`.
  - `power_sleep_light(until_utc)`, which returns the wake cause, and `power_sleep_deep(until_utc)`, which never returns.
  - `power_idle_strategy()` and `power_set_idle_strategy()`, persisted in NVS `sys/idle`.
  - `power_start_test(mode, cycles)` (resets the stats), `power_test_cycles_left()`, `power_stats()`, `power_reset_stats()`.
  - `st7305_init_warm(variant, mode, rate)` and `st7305_prepare_deep_sleep()`.
  - `display_state_t`, `display_init_warm(&state)`, `display_export(&state)`, `display_prepare_deep_sleep()`.
  - The console commands `power idle [deep|light]` and `sleep stats [reset] | sleep test <deep|light> <n>`.
  - Kconfig: `REFLBO_IDLE_DEFAULT_{LIGHT,DEEP}`, default light until D3.
- Facts from the ESP-IDF v5.5.5 source and the spike:
  - Light sleep disables the USB-Serial-JTAG pad. On the owner's Mac the port stayed present, but the console works only while awake.
  - Deep sleep powers the USB PHY off.
  - ext1 wake (ANY_LOW) works for GPIO0, 15 and 18. GPIO15 needs `rtc_gpio_pullup_en`.
  - Light sleep floats every pin unless `gpio_sleep_sel_dis()` is called for it.
  - `gpio_wakeup_enable()` switches a pin to a level interrupt; the edge type is restored after wake.
  - Deep-sleep holds use `gpio_hold_en()` + `gpio_deep_sleep_hold_en()`. The release is glitch-free: drive the level, configure the pin, then `gpio_hold_dis()`.
- Rulings made while planning:
  - Light sleep is entered by `power_sleep_light()` (`esp_light_sleep_start()` with GPIO wake), not by esp_pm automatic light sleep. The console drops in either kind of light sleep, and the automatic kind would need level-type interrupts on the button and RTC pins all the time.
  - A tethered board stays awake instead of light sleeping (spec §3.4 assumed the console survives light sleep).
  - `st7305_reinit` records HPM right after the reset. This M1 deferred minor matters now that `set_mode` returns early when the mode is unchanged.

- [ ] **Step 1: Watch the device check fail**

Run: `tools/idf.sh exec python tools/devlog.py -p PORT --cmd "sleep stats" -t 10 | grep -E "^sleep|unknown"`
Expected: `unknown command: sleep`.

- [ ] **Step 2: Write the power component**

`components/power/include/power.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "power_policy.h"

/*
 * Power states and sleep entry (spec §3.4, §9). Light sleep is entered by power_sleep_light();
 * deep sleep by power_sleep_deep(), which reboots into app_main on wake. Either one drops the
 * USB console, so the policy keeps a tethered board awake. Call from the app task only.
 */

typedef enum {
    POWER_WAKE_COLD,  /* power-on or reset, not a wake from sleep */
    POWER_WAKE_RTC,   /* PCF85063 alarm (INT low) */
    POWER_WAKE_KEY,
    POWER_WAKE_BOOT,
    POWER_WAKE_TIMER, /* backup timer: the RTC alarm did not arrive */
    POWER_WAKE_OTHER,
    POWER_WAKE_COUNT,
} power_wake_t;

typedef struct {
    uint32_t light_sleeps;
    uint32_t deep_sleeps;
    uint32_t wakes[POWER_WAKE_COUNT];
    uint64_t awake_ms_total; /* app time between sleeps; ROM and bootloader time are not included */
    uint32_t awake_ms_last;
    uint32_t awake_ms_max;
} power_stats_t;

/* Call once at boot, after nvs_flash_init(): decodes and counts the wake cause. */
esp_err_t power_init(void);
power_wake_t power_boot_wake(void); /* why this boot happened */
const char *power_wake_name(power_wake_t wake);
bool power_tethered(void);
/* Stay awake at least this long from now: lets a PC find the board after boot or a button. */
void power_hold_awake_ms(uint32_t ms);
power_plan_t power_plan(bool work_pending);
/* Sleeps until `until_utc` or a button or the RTC alarm; returns what woke it. */
power_wake_t power_sleep_light(time_t until_utc);
/* Never returns: the chip reboots on wake. Seal RTC-RAM state and hold the panel pins first. */
void power_sleep_deep(time_t until_utc);
power_idle_t power_idle_strategy(void);
esp_err_t power_set_idle_strategy(power_idle_t idle); /* persisted in NVS */
/* `sleep test`: the next `cycles` sleeps use `mode` even when tethered. Resets the stats. */
void power_start_test(power_idle_t mode, int cycles);
int power_test_cycles_left(void);
power_stats_t power_stats(void);
void power_reset_stats(void);
```

`components/power/power.c`:

```c
#include "power.h"

#include <stdio.h>
#include <unistd.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "util_snapshot.h"

#define STATE_MAGIC   0x72666c70u /* "rflp" */
#define STATE_VERSION 1
#define NVS_NAMESPACE "sys"
#define NVS_KEY_IDLE  "idle"
#define TEST_END_HOLD_MS 3000

static const char *TAG = "power";

typedef struct {
    util_snapshot_hdr_t hdr;
    power_stats_t stats;
    int32_t test_cycles;
    uint8_t test_mode;
    uint8_t test_ended; /* the last test cycle just ran: stay awake so the PC finds the board */
} power_state_t;

static RTC_DATA_ATTR power_state_t s_rtc; /* survives deep sleep only */
static power_idle_t s_strategy;
static power_wake_t s_boot_wake;
static int64_t s_hold_until_us;
static int64_t s_awake_since_us;

static const gpio_num_t k_wake_pins[] = { BOARD_PIN_RTC_INT, BOARD_PIN_KEY, BOARD_PIN_BOOT };

static void seal(void)
{
    util_snapshot_seal(&s_rtc, sizeof(s_rtc), STATE_MAGIC, STATE_VERSION);
}

static void count_wake(power_wake_t wake);
static power_wake_t decode_boot_wake(void);
static void finish_test(void);

esp_err_t power_init(void)
{
    s_boot_wake = decode_boot_wake();
    if (!util_snapshot_valid(&s_rtc, sizeof(s_rtc), STATE_MAGIC, STATE_VERSION)) {
        s_rtc = (power_state_t){ 0 };
        seal();
    }
    if (s_boot_wake != POWER_WAKE_COLD) {
        count_wake(s_boot_wake);
        finish_test();
    }
#if CONFIG_REFLBO_IDLE_DEFAULT_DEEP
    s_strategy = POWER_IDLE_DEEP;
#else
    s_strategy = POWER_IDLE_LIGHT;
#endif
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(nvs, NVS_KEY_IDLE, &v) == ESP_OK && v <= POWER_IDLE_DEEP) {
            s_strategy = (power_idle_t)v;
        }
        nvs_close(nvs);
    }
    s_awake_since_us = 0; /* esp_timer starts at 0 on every boot */
    ESP_RETURN_ON_ERROR(esp_sleep_cpu_pd_low_init(), TAG, "CPU power-down in light sleep");
    return ESP_OK;
}

static power_wake_t decode_boot_wake(void)
{
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        return POWER_WAKE_COLD;
    }
    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & BIT(ESP_SLEEP_WAKEUP_EXT1)) {
        uint64_t pins = esp_sleep_get_ext1_wakeup_status();
        if (pins & BIT64(BOARD_PIN_KEY)) {
            return POWER_WAKE_KEY;
        }
        if (pins & BIT64(BOARD_PIN_BOOT)) {
            return POWER_WAKE_BOOT;
        }
        if (pins & BIT64(BOARD_PIN_RTC_INT)) {
            return POWER_WAKE_RTC;
        }
    }
    return causes & BIT(ESP_SLEEP_WAKEUP_TIMER) ? POWER_WAKE_TIMER : POWER_WAKE_OTHER;
}

power_wake_t power_boot_wake(void)
{
    return s_boot_wake;
}

const char *power_wake_name(power_wake_t wake)
{
    static const char *const names[POWER_WAKE_COUNT] = { "cold", "rtc", "key", "boot", "timer", "other" };
    return (unsigned)wake < POWER_WAKE_COUNT ? names[wake] : "?";
}

bool power_tethered(void)
{
    return usb_serial_jtag_is_connected();
}

void power_hold_awake_ms(uint32_t ms)
{
    int64_t until = esp_timer_get_time() + (int64_t)ms * 1000;
    if (until > s_hold_until_us) {
        s_hold_until_us = until;
    }
}

power_plan_t power_plan(bool work_pending)
{
    power_policy_input_t in = {
        .strategy = s_strategy,
        .tethered = power_tethered(),
        .hold_awake = work_pending || esp_timer_get_time() < s_hold_until_us,
        .test_cycles = s_rtc.test_cycles,
        .test_mode = (power_idle_t)s_rtc.test_mode,
    };
    return power_policy(&in);
}

static void count_sleep(bool deep)
{
    uint32_t awake_ms = (uint32_t)((esp_timer_get_time() - s_awake_since_us) / 1000);
    power_stats_t *st = &s_rtc.stats;
    if (deep) {
        st->deep_sleeps++;
    } else {
        st->light_sleeps++;
    }
    st->awake_ms_total += awake_ms;
    st->awake_ms_last = awake_ms;
    if (awake_ms > st->awake_ms_max) {
        st->awake_ms_max = awake_ms;
    }
    if (s_rtc.test_cycles > 0 && --s_rtc.test_cycles == 0) {
        s_rtc.test_ended = 1;
    }
    seal();
}

/* After the last `sleep test` cycle, give the USB host time to enumerate the board again. */
static void finish_test(void)
{
    if (s_rtc.test_ended) {
        s_rtc.test_ended = 0;
        seal();
        power_hold_awake_ms(TEST_END_HOLD_MS);
        ESP_LOGI(TAG, "sleep test finished");
    }
}

static void count_wake(power_wake_t wake)
{
    s_rtc.stats.wakes[wake]++;
    seal();
}

static uint64_t sleep_us_until(time_t until_utc)
{
    time_t now = time(NULL);
    time_t left = until_utc > now ? until_utc - now : 1;
    return (uint64_t)left * 1000000u;
}

power_wake_t power_sleep_light(time_t until_utc)
{
    count_sleep(false);
    for (size_t i = 0; i < sizeof(k_wake_pins) / sizeof(k_wake_pins[0]); i++) {
        gpio_intr_disable(k_wake_pins[i]);
        gpio_wakeup_enable(k_wake_pins[i], GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(sleep_us_until(until_utc));
    esp_err_t err = esp_light_sleep_start();
    for (size_t i = 0; i < sizeof(k_wake_pins) / sizeof(k_wake_pins[0]); i++) {
        gpio_wakeup_disable(k_wake_pins[i]);
        gpio_set_intr_type(k_wake_pins[i], k_wake_pins[i] == BOARD_PIN_RTC_INT ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE);
        gpio_intr_enable(k_wake_pins[i]);
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    s_awake_since_us = esp_timer_get_time();

    power_wake_t wake = POWER_WAKE_OTHER;
    if (gpio_get_level(BOARD_PIN_KEY) == 0) {
        wake = POWER_WAKE_KEY;
    } else if (gpio_get_level(BOARD_PIN_BOOT) == 0) {
        wake = POWER_WAKE_BOOT;
    } else if (gpio_get_level(BOARD_PIN_RTC_INT) == 0) {
        wake = POWER_WAKE_RTC;
    } else if (err == ESP_OK && (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER))) {
        wake = POWER_WAKE_TIMER;
    }
    count_wake(wake);
    finish_test();
    return wake;
}

void power_sleep_deep(time_t until_utc)
{
    count_sleep(true);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    rtc_gpio_pullup_en(BOARD_PIN_RTC_INT); /* INT has no external pull-up */
    rtc_gpio_pulldown_dis(BOARD_PIN_RTC_INT);
    esp_sleep_enable_ext1_wakeup_io(BIT64(BOARD_PIN_RTC_INT) | BIT64(BOARD_PIN_KEY) | BIT64(BOARD_PIN_BOOT),
                                    ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_enable_timer_wakeup(sleep_us_until(until_utc));
    fflush(stdout);
    fsync(fileno(stdout));
    esp_deep_sleep_disable_rom_logging();
    esp_deep_sleep_start();
}


power_idle_t power_idle_strategy(void)
{
    return s_strategy;
}

esp_err_t power_set_idle_strategy(power_idle_t idle)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "NVS open");
    esp_err_t err = nvs_set_u8(nvs, NVS_KEY_IDLE, (uint8_t)idle);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    ESP_RETURN_ON_ERROR(err, TAG, "NVS write");
    s_strategy = idle;
    return ESP_OK;
}

void power_start_test(power_idle_t mode, int cycles)
{
    s_rtc.stats = (power_stats_t){ 0 }; /* the test's numbers only */
    s_rtc.test_cycles = cycles;
    s_rtc.test_mode = (uint8_t)mode;
    seal();
}

int power_test_cycles_left(void)
{
    return s_rtc.test_cycles;
}

power_stats_t power_stats(void)
{
    return s_rtc.stats;
}

void power_reset_stats(void)
{
    s_rtc.stats = (power_stats_t){ 0 };
    seal();
}
```

`components/power/CMakeLists.txt`:

```cmake
# Power states, sleep entry and policy (spec §3.4, §9). power_policy.c is pure C and also built on the host.
idf_component_register(SRCS "power.c" "power_policy.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board esp_driver_gpio esp_driver_usb_serial_jtag esp_timer nvs_flash util)
```

- [ ] **Step 3: Warm panel attach and pin handling**

`components/st7305/include/st7305.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "st7305_frame.h"

/*
 * ST7305 driver. It owns SPI3_HOST and GPIO 5 (D/C), 6 (TE), 11 (SCK), 12 (MOSI), 40 (CS) and
 * 41 (RESET). Not thread-safe: only one task may call it at a time. That is the display service's
 * caller (see display.h).
 */

/* The two vendor init sequences (AGENTS.md gotcha 6). They differ in the source high voltages
 * (contrast), the oscillator (HPM frame rate) and the LPM frame rate. The driver replaces the
 * sequence's LPM rate (factory 8 Hz, XiaoZhi 1 Hz) with st7305_lpm_rate(). */
typedef enum {
    ST7305_VARIANT_FACTORY, /* factory firmware: SPI 10 MHz, VSHP/VSHN 0x41, HPM 16 Hz */
    ST7305_VARIANT_XIAOZHI, /* XiaoZhi firmware: SPI 40 MHz, VSHP 0x69, VSHN 0x4B, HPM 25.5 Hz */
} st7305_variant_t;

typedef enum {
    ST7305_MODE_HPM, /* high power mode: fast refresh, for interaction */
    ST7305_MODE_LPM, /* low power mode: slow refresh, for idle */
} st7305_mode_t;

/* LPM refresh rate. The values are the LFRA codes of FRCTRL (B2h), datasheet §8.2.3. Idle stays in
 * LPM, so this sets the panel's idle power draw. */
typedef enum {
    ST7305_LPM_0_25HZ,
    ST7305_LPM_0_5HZ,
    ST7305_LPM_1HZ, /* default (spec §4.2) */
    ST7305_LPM_2HZ,
    ST7305_LPM_4HZ,
    ST7305_LPM_8HZ,
} st7305_lpm_rate_t;

/* Cold start: SPI bus, hardware reset, init sequence. The panel RAM is undefined until the first push. */
esp_err_t st7305_init(st7305_variant_t variant);
/* After a deep-sleep wake: attaches the SPI bus without resetting the panel, which kept its
 * image, mode and rate (AGENTS.md gotcha 4). Releases the pin holds set before sleeping. */
esp_err_t st7305_init_warm(st7305_variant_t variant, st7305_mode_t mode, st7305_lpm_rate_t rate);
/* Holds CS and RESET through deep sleep. Call last, just before sleeping. */
esp_err_t st7305_prepare_deep_sleep(void);
/* Resets the panel and runs another init sequence (the SPI clock follows the variant). */
esp_err_t st7305_reinit(st7305_variant_t variant);
/* Converts and sends a canonical frame (spec §4.1); blocks until the DMA transfer has finished. */
esp_err_t st7305_push(const uint8_t *canonical);
/* Switches the power mode with the datasheet §7.11 sequence: about 120 ms into LPM, 320 ms into HPM. */
esp_err_t st7305_set_mode(st7305_mode_t mode);
/* Sets the LPM refresh rate. Before st7305_init it is only stored; every init applies it. */
esp_err_t st7305_set_lpm_rate(st7305_lpm_rate_t rate);
st7305_variant_t st7305_variant(void);
st7305_mode_t st7305_mode(void);
st7305_lpm_rate_t st7305_lpm_rate(void);
const char *st7305_lpm_rate_name(st7305_lpm_rate_t rate); /* "0.25" ... "8" (Hz) */
/* Counts the panel's tearing-effect pulses (TE, GPIO6: one per panel frame) for window_ms. */
esp_err_t st7305_count_frames(uint32_t window_ms, uint32_t *frames);
```

`components/st7305/st7305.c`:

```c
#include "st7305.h"

#include <stddef.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "util_ticks.h"

#define PIN_MOSI    BOARD_PIN_LCD_MOSI
#define PIN_SCLK    BOARD_PIN_LCD_SCK
#define PIN_DC      BOARD_PIN_LCD_DC
#define PIN_CS      BOARD_PIN_LCD_CS
#define PIN_RST     BOARD_PIN_LCD_RST
#define PIN_TE      BOARD_PIN_LCD_TE
#define SPI_HOST_ID SPI3_HOST

static const char *TAG = "st7305";

/* Datasheet delays are minimums, so sleep at least `ms` (see util_ticks.h). */
static void delay_at_least_ms(uint32_t ms)
{
    vTaskDelay(util_ticks_at_least(ms, portTICK_PERIOD_MS));
}

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[10];
    uint16_t delay_ms;
} init_cmd_t;

/* From ref/waveshare: 10_FactoryProgram port_bsp/display_bsp.cpp and the XiaoZhi board's
 * custom_lcd_display.cc. Identical except C1, C4, D8 and B2. */
static const init_cmd_t s_init_factory[] = {
    { 0xD6, 2, { 0x17, 0x02 }, 0 },                  /* NVMLOADCTRL */
    { 0xD1, 1, { 0x01 }, 0 },                        /* BSTEN: booster on */
    { 0xC0, 2, { 0x11, 0x04 }, 0 },                  /* GCTRL: gate voltages */
    { 0xC1, 4, { 0x41, 0x41, 0x41, 0x41 }, 0 },      /* VSHPCTRL: source high, positive */
    { 0xC2, 4, { 0x19, 0x19, 0x19, 0x19 }, 0 },      /* VSLPCTRL: source low, positive */
    { 0xC4, 4, { 0x41, 0x41, 0x41, 0x41 }, 0 },      /* VSHNCTRL: source high, negative */
    { 0xC5, 4, { 0x19, 0x19, 0x19, 0x19 }, 0 },      /* VSLNCTRL: source low, negative */
    { 0xD8, 2, { 0xA6, 0xE9 }, 0 },                  /* OSCSET */
    { 0xB2, 1, { 0x05 }, 0 },                        /* FRCTRL: HPM 16 Hz, LPM 8 Hz */
    { 0xB3, 10, { 0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45 }, 0 }, /* GTUPEQH */
    { 0xB4, 8, { 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45 }, 0 },             /* GTUPEQL */
    { 0x62, 3, { 0x32, 0x03, 0x1F }, 0 },
    { 0xB7, 1, { 0x13 }, 0 },                        /* SOUEQ */
    { 0xB0, 1, { 0x64 }, 0 },                        /* GATESET */
    { 0x11, 0, { 0 }, 200 },                         /* SLPOUT */
    { 0xC9, 1, { 0x00 }, 0 },                        /* VSHLSEL */
    { 0x36, 1, { 0x48 }, 0 },                        /* MADCTL */
    { 0x3A, 1, { 0x11 }, 0 },                        /* DTFORM */
    { 0xB9, 1, { 0x20 }, 0 },                        /* GAMAMS: mono */
    { 0xB8, 1, { 0x29 }, 0 },                        /* PNLSET */
    { 0x21, 0, { 0 }, 0 },                           /* INVON */
    { 0x2A, 2, { 0x12, 0x2A }, 0 },                  /* CASET */
    { 0x2B, 2, { 0x00, 0xC7 }, 0 },                  /* RASET */
    { 0x35, 1, { 0x00 }, 0 },                        /* TEON */
    { 0xD0, 1, { 0xFF }, 0 },                        /* AUTOPWRCTRL */
    { 0x38, 0, { 0 }, 0 },                           /* HPM */
    { 0x29, 0, { 0 }, 0 },                           /* DISPON */
};

static const init_cmd_t s_init_xiaozhi[] = {
    { 0xD6, 2, { 0x17, 0x02 }, 0 },
    { 0xD1, 1, { 0x01 }, 0 },
    { 0xC0, 2, { 0x11, 0x04 }, 0 },
    { 0xC1, 4, { 0x69, 0x69, 0x69, 0x69 }, 0 },      /* VSHPCTRL */
    { 0xC2, 4, { 0x19, 0x19, 0x19, 0x19 }, 0 },
    { 0xC4, 4, { 0x4B, 0x4B, 0x4B, 0x4B }, 0 },      /* VSHNCTRL */
    { 0xC5, 4, { 0x19, 0x19, 0x19, 0x19 }, 0 },
    { 0xD8, 2, { 0x80, 0xE9 }, 0 },                  /* OSCSET */
    { 0xB2, 1, { 0x02 }, 0 },                        /* FRCTRL: HPM 25.5 Hz, LPM 1 Hz */
    { 0xB3, 10, { 0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45 }, 0 },
    { 0xB4, 8, { 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45 }, 0 },
    { 0x62, 3, { 0x32, 0x03, 0x1F }, 0 },
    { 0xB7, 1, { 0x13 }, 0 },
    { 0xB0, 1, { 0x64 }, 0 },
    { 0x11, 0, { 0 }, 200 },
    { 0xC9, 1, { 0x00 }, 0 },
    { 0x36, 1, { 0x48 }, 0 },
    { 0x3A, 1, { 0x11 }, 0 },
    { 0xB9, 1, { 0x20 }, 0 },
    { 0xB8, 1, { 0x29 }, 0 },
    { 0x21, 0, { 0 }, 0 },
    { 0x2A, 2, { 0x12, 0x2A }, 0 },
    { 0x2B, 2, { 0x00, 0xC7 }, 0 },
    { 0x35, 1, { 0x00 }, 0 },
    { 0xD0, 1, { 0xFF }, 0 },
    { 0x38, 0, { 0 }, 0 },
    { 0x29, 0, { 0 }, 0 },
};

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;
static uint8_t *s_panel; /* DMA-capable internal RAM */
static bool s_bus_ready;
static st7305_variant_t s_variant;
static st7305_mode_t s_mode;
static st7305_lpm_rate_t s_lpm_rate = ST7305_LPM_1HZ;
static volatile uint32_t s_te_pulses;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t create_io(uint32_t pclk_hz)
{
    if (s_io != NULL) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_del(s_io), TAG, "panel IO delete failed");
        s_io = NULL;
    }
    esp_lcd_panel_io_spi_config_t cfg = {
        .cs_gpio_num = PIN_CS,
        .dc_gpio_num = PIN_DC,
        .spi_mode = 0,
        .pclk_hz = pclk_hz,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST_ID, &cfg, &s_io), TAG, "IO");
    /* Light sleep would float the pad and could select the panel; keep CS driven (idles high). */
    return gpio_sleep_sel_dis(PIN_CS);
}

static void hardware_reset(void)
{
    gpio_set_level(PIN_RST, 1);
    delay_at_least_ms(50);
    gpio_set_level(PIN_RST, 0);
    delay_at_least_ms(20);
    gpio_set_level(PIN_RST, 1);
    /* Datasheet §12.1.4: a reset in sleep-out mode (reflash, reboot, `panel init`) takes up to 120 ms
     * to cancel, and SLPOUT must wait that long. The vendor's 50 ms only covers a power-on reset. */
    delay_at_least_ms(120);
}

static esp_err_t run_init(const init_cmd_t *cmds, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, cmds[i].cmd, cmds[i].len ? cmds[i].data : NULL,
                                                      cmds[i].len),
                            TAG, "init command 0x%02X failed", cmds[i].cmd);
        if (cmds[i].delay_ms) {
            delay_at_least_ms(cmds[i].delay_ms);
        }
    }
    return ESP_OK;
}

/* FRCTRL (B2h) = HFRA << 4 | LFRA. HFRA stays 0, as in both vendor sequences: HPM 16 Hz with the
 * factory oscillator setting, 25.5 Hz with XiaoZhi's. */
static esp_err_t write_frame_rate(void)
{
    const uint8_t frctrl = (uint8_t)s_lpm_rate;
    return esp_lcd_panel_io_tx_param(s_io, 0xB2, &frctrl, 1);
}

static esp_err_t bus_init(void)
{
    s_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_done != NULL, ESP_ERR_NO_MEM, TAG, "semaphore");
    s_panel = heap_caps_malloc(ST7305_FRAME_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_NO_MEM, TAG, "panel buffer");
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ST7305_FRAME_BYTES,
    };
    return spi_bus_initialize(SPI_HOST_ID, &bus, SPI_DMA_CH_AUTO);
}

static esp_err_t reset_pin_init(void)
{
    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "reset pin");
    return gpio_sleep_sel_dis(PIN_RST); /* RESET must stay high in light sleep, or the panel resets */
}

esp_err_t st7305_init(st7305_variant_t variant)
{
    ESP_RETURN_ON_FALSE(!s_bus_ready, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    ESP_RETURN_ON_ERROR(bus_init(), TAG, "SPI bus");
    ESP_RETURN_ON_ERROR(reset_pin_init(), TAG, "reset pin");
    s_bus_ready = true;
    return st7305_reinit(variant);
}

esp_err_t st7305_init_warm(st7305_variant_t variant, st7305_mode_t mode, st7305_lpm_rate_t rate)
{
    ESP_RETURN_ON_FALSE(!s_bus_ready, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    ESP_RETURN_ON_ERROR(bus_init(), TAG, "SPI bus");
    /* The pads are still held from before deep sleep. Drive the same levels, then release them. */
    gpio_set_level(PIN_RST, 1);
    ESP_RETURN_ON_ERROR(reset_pin_init(), TAG, "reset pin");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(PIN_RST), TAG, "release RESET");
    s_bus_ready = true;
    const bool xiaozhi = variant == ST7305_VARIANT_XIAOZHI;
    ESP_RETURN_ON_ERROR(create_io(xiaozhi ? 40 * 1000 * 1000 : 10 * 1000 * 1000), TAG, "panel IO");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(PIN_CS), TAG, "release CS");
    s_variant = variant;
    s_mode = mode;
    s_lpm_rate = rate;
    ESP_LOGI(TAG, "attached without reset (%s, %s, LPM %s Hz)", xiaozhi ? "XiaoZhi" : "factory",
             mode == ST7305_MODE_LPM ? "LPM" : "HPM", st7305_lpm_rate_name(rate));
    return ESP_OK;
}

esp_err_t st7305_prepare_deep_sleep(void)
{
    ESP_RETURN_ON_FALSE(s_io != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    /* CS (idle high) and RESET are digital pads: they lose their level in deep sleep unless held
     * (AGENTS.md gotcha 4). The hold takes effect at once, so this is the last panel access. */
    gpio_set_level(PIN_RST, 1);
    ESP_RETURN_ON_ERROR(gpio_hold_en(PIN_CS), TAG, "hold CS");
    ESP_RETURN_ON_ERROR(gpio_hold_en(PIN_RST), TAG, "hold RESET");
    gpio_deep_sleep_hold_en();
    return ESP_OK;
}

esp_err_t st7305_reinit(st7305_variant_t variant)
{
    ESP_RETURN_ON_FALSE(s_bus_ready, ESP_ERR_INVALID_STATE, TAG, "call st7305_init first");
    const bool xiaozhi = variant == ST7305_VARIANT_XIAOZHI;
    ESP_RETURN_ON_ERROR(create_io(xiaozhi ? 40 * 1000 * 1000 : 10 * 1000 * 1000), TAG, "panel IO");
    hardware_reset();
    s_mode = ST7305_MODE_HPM; /* the panel is in HPM after a reset, even if the init below fails */
    if (xiaozhi) {
        ESP_RETURN_ON_ERROR(run_init(s_init_xiaozhi, sizeof(s_init_xiaozhi) / sizeof(s_init_xiaozhi[0])), TAG,
                            "XiaoZhi init");
    } else {
        ESP_RETURN_ON_ERROR(run_init(s_init_factory, sizeof(s_init_factory) / sizeof(s_init_factory[0])), TAG,
                            "factory init");
    }
    ESP_RETURN_ON_ERROR(write_frame_rate(), TAG, "frame rate");
    s_variant = variant;
    ESP_LOGI(TAG, "%s init sequence, SPI %d MHz, LPM %s Hz", xiaozhi ? "XiaoZhi" : "factory", xiaozhi ? 40 : 10,
             st7305_lpm_rate_name(s_lpm_rate));
    return ESP_OK;
}

esp_err_t st7305_push(const uint8_t *canonical)
{
    static const uint8_t columns[] = { 0x12, 0x2A };
    static const uint8_t rows[] = { 0x00, 0xC7 };

    ESP_RETURN_ON_FALSE(s_io != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    st7305_frame_to_panel(canonical, s_panel);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x2A, columns, sizeof(columns)), TAG, "column window");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x2B, rows, sizeof(rows)), TAG, "row window");
    xSemaphoreTake(s_done, 0); /* drop a stale completion */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(s_io, 0x2C, s_panel, ST7305_FRAME_BYTES), TAG, "frame");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_done, pdMS_TO_TICKS(500)) == pdTRUE, ESP_ERR_TIMEOUT, TAG,
                        "frame transfer timed out");
    return ESP_OK;
}

/* Datasheet §7.11 sets the source voltages for the new mode during a switch. Both vendor sequences
 * load the same values into all four voltage sets of C1h/C2h/C4h/C5h, so that step reselects set 1. */
static esp_err_t select_source_voltages(void)
{
    const uint8_t set = 0x00;
    return esp_lcd_panel_io_tx_param(s_io, 0xC9, &set, 1);
}

esp_err_t st7305_set_mode(st7305_mode_t mode)
{
    ESP_RETURN_ON_FALSE(s_io != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    if (mode == s_mode) {
        return ESP_OK;
    }
    if (mode == ST7305_MODE_LPM) { /* HPM => LPM */
        ESP_RETURN_ON_ERROR(select_source_voltages(), TAG, "LPM voltages");
        delay_at_least_ms(20);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x39, NULL, 0), TAG, "LPM");
        delay_at_least_ms(100);
    } else { /* LPM => HPM */
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x38, NULL, 0), TAG, "HPM");
        delay_at_least_ms(300);
        ESP_RETURN_ON_ERROR(select_source_voltages(), TAG, "HPM voltages");
        delay_at_least_ms(20);
    }
    s_mode = mode;
    return ESP_OK;
}

esp_err_t st7305_set_lpm_rate(st7305_lpm_rate_t rate)
{
    ESP_RETURN_ON_FALSE((unsigned)rate <= ST7305_LPM_8HZ, ESP_ERR_INVALID_ARG, TAG, "LPM rate");
    s_lpm_rate = rate;
    if (s_io == NULL) {
        return ESP_OK; /* st7305_init applies it */
    }
    return write_frame_rate(); /* applies at once, also in LPM (measured with panel fps) */
}

st7305_variant_t st7305_variant(void)
{
    return s_variant;
}

st7305_mode_t st7305_mode(void)
{
    return s_mode;
}

st7305_lpm_rate_t st7305_lpm_rate(void)
{
    return s_lpm_rate;
}

static void IRAM_ATTR on_te(void *arg)
{
    (void)arg;
    s_te_pulses++;
}

esp_err_t st7305_count_frames(uint32_t window_ms, uint32_t *frames)
{
    ESP_RETURN_ON_FALSE(s_io != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    gpio_config_t te = {
        .pin_bit_mask = 1ULL << PIN_TE,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&te), TAG, "TE pin");
    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "GPIO ISR service");
    s_te_pulses = 0;
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(PIN_TE, on_te, NULL), TAG, "TE handler");
    vTaskDelay(pdMS_TO_TICKS(window_ms));
    gpio_isr_handler_remove(PIN_TE);
    gpio_set_intr_type(PIN_TE, GPIO_INTR_DISABLE);
    *frames = s_te_pulses;
    return ESP_OK;
}

const char *st7305_lpm_rate_name(st7305_lpm_rate_t rate)
{
    static const char *const names[] = { "0.25", "0.5", "1", "2", "4", "8" };
    return (unsigned)rate < sizeof(names) / sizeof(names[0]) ? names[rate] : "?";
}
```

`components/display/include/display.h`:

```c
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "gfx.h"
#include "st7305.h"

/* Display service (spec §4.1): owns the canonical framebuffer (in PSRAM) and pushes it to the panel.
 *
 * Not thread-safe: the display and st7305 belong to the app task (spec §3.2). Console commands
 * that draw or push run there through the diag executor. */

/* What must survive deep sleep to re-attach the panel without a reset (app RTC-RAM snapshot). */
typedef struct {
    st7305_variant_t variant;
    st7305_mode_t mode;
    st7305_lpm_rate_t lpm_rate;
    uint32_t last_crc; /* CRC of the frame the panel shows */
    bool pushed;
} display_state_t;

esp_err_t display_init(st7305_variant_t variant); /* cold start: white frame, then LPM */
/* Deep-sleep wake: the panel still shows the last frame; attach without reset or clear. */
esp_err_t display_init_warm(const display_state_t *state);
void display_export(display_state_t *out);
/* Holds the panel pins for deep sleep. The last display call before sleeping. */
esp_err_t display_prepare_deep_sleep(void);
gfx_fb_t *display_fb(void); /* NULL until display_init has allocated the framebuffer */
/* Pushes the framebuffer if it changed since the last push (CRC32), or always when force is set. */
esp_err_t display_commit(bool force);
/* Re-initialises the panel with another init sequence and pushes the current frame. */
esp_err_t display_set_variant(st7305_variant_t variant);
```

`components/display/display.c`:

```c
#include "display.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "util_crc32.h"

static const char *TAG = "display";

static gfx_fb_t s_fb; /* s_fb.buf stays NULL until display_init */
static uint32_t s_last_crc;
static bool s_pushed;

static esp_err_t alloc_fb(void)
{
    ESP_RETURN_ON_FALSE(s_fb.buf == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    uint8_t *buf = heap_caps_calloc(1, ST7305_FRAME_BYTES, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(buf != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer");
    gfx_fb_init(&s_fb, buf, ST7305_WIDTH, ST7305_HEIGHT);
    return ESP_OK;
}

esp_err_t display_init_warm(const display_state_t *state)
{
    ESP_RETURN_ON_ERROR(alloc_fb(), TAG, "framebuffer");
    ESP_RETURN_ON_ERROR(st7305_init_warm(state->variant, state->mode, state->lpm_rate), TAG, "panel attach");
    s_last_crc = state->last_crc;
    s_pushed = state->pushed;
    return ESP_OK;
}

void display_export(display_state_t *out)
{
    *out = (display_state_t){
        .variant = st7305_variant(),
        .mode = st7305_mode(),
        .lpm_rate = st7305_lpm_rate(),
        .last_crc = s_last_crc,
        .pushed = s_pushed,
    };
}

esp_err_t display_prepare_deep_sleep(void)
{
    return st7305_prepare_deep_sleep();
}

esp_err_t display_init(st7305_variant_t variant)
{
    ESP_RETURN_ON_ERROR(alloc_fb(), TAG, "framebuffer");
    ESP_RETURN_ON_ERROR(st7305_init(variant), TAG, "panel init");
    int64_t start = esp_timer_get_time();
    ESP_RETURN_ON_ERROR(display_commit(true), TAG, "first frame");
    ESP_LOGI(TAG, "first frame pushed in %lld us", (long long)(esp_timer_get_time() - start));
    return st7305_set_mode(ST7305_MODE_LPM);
}

gfx_fb_t *display_fb(void)
{
    return s_fb.buf != NULL ? &s_fb : NULL;
}

esp_err_t display_commit(bool force)
{
    ESP_RETURN_ON_FALSE(s_fb.buf != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    uint32_t crc = util_crc32(0, s_fb.buf, ST7305_FRAME_BYTES);
    if (!force && s_pushed && crc == s_last_crc) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(st7305_push(s_fb.buf), TAG, "push");
    s_last_crc = crc;
    s_pushed = true;
    return ESP_OK;
}

esp_err_t display_set_variant(st7305_variant_t variant)
{
    ESP_RETURN_ON_ERROR(st7305_reinit(variant), TAG, "reinit");
    ESP_RETURN_ON_ERROR(display_commit(true), TAG, "frame");
    return st7305_set_mode(ST7305_MODE_LPM);
}
```

- [ ] **Step 4: Sleep in the app task, and add `power` and `sleep`**

`main/app.c`:

```c
#include "app.h"

#include <time.h>

#include "board.h"
#include "board_buttons.h"
#include "board_pins.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "power.h"
#include "pcf85063.h"
#include "scheduler.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "timekeeping.h"
#include "ui_clock.h"
#include "util_snapshot.h"
#include "util_ticks.h"

#define APP_STACK         8192 /* internal RAM: deep-sleep entry requires it */
#define APP_PRIORITY      5
#define APP_CORE          1
#define QUEUE_DEPTH       16
#define BACKUP_S          5    /* wake anyway this long after a missed RTC alarm (spec §9.2) */
#define GRACE_MS          2000 /* stay awake after boot or a button so a PC can find the board */
#define TETHER_RECHECK_MS 1000
#define SNAP_MAGIC        0x72666c62u /* "rflb" */
#define SNAP_VERSION      1

#if CONFIG_REFLBO_PANEL_INIT_XIAOZHI
#define PANEL_VARIANT ST7305_VARIANT_XIAOZHI
#else
#define PANEL_VARIANT ST7305_VARIANT_FACTORY
#endif

static const char *TAG = "app";

typedef enum {
    EV_RTC_ALARM,
    EV_BUTTON,
    EV_CALL,
} ev_type_t;

typedef struct {
    ev_type_t type;
    union {
        struct {
            board_button_t button;
            gesture_t gesture;
        } button;
        struct {
            void (*fn)(void *);
            void *arg;
            SemaphoreHandle_t done;
        } call;
    };
} app_event_t;

/* Kept in RTC RAM through deep sleep; invalid after any other reset (spec §3.3). */
typedef struct {
    util_snapshot_hdr_t hdr;
    sensors_state_t sensors;
    display_state_t display;
} app_snapshot_t;

static RTC_DATA_ATTR app_snapshot_t s_snap;
static QueueHandle_t s_queue;
static time_t s_next_wake;

/* Dashboard bindings (spec §5.6): KEY double toggles auto-cycle, BOOT long needs 3 s. */
static const gesture_config_t k_dashboard_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = true },
    [BOARD_BUTTON_BOOT] = { .long_ms = 3000, .double_enabled = false },
};

static void IRAM_ATTR on_rtc_int(void *arg)
{
    (void)arg;
    app_event_t ev = { .type = EV_RTC_ALARM };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &ev, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void on_button(board_button_t button, gesture_t gesture)
{
    app_event_t ev = { .type = EV_BUTTON, .button = { .button = button, .gesture = gesture } };
    xQueueSend(s_queue, &ev, pdMS_TO_TICKS(100));
}

esp_err_t app_execute(void (*fn)(void *arg), void *arg)
{
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(done != NULL, ESP_ERR_NO_MEM, TAG, "semaphore");
    app_event_t ev = { .type = EV_CALL, .call = { .fn = fn, .arg = arg, .done = done } };
    if (xQueueSend(s_queue, &ev, pdMS_TO_TICKS(1000)) != pdTRUE) {
        vSemaphoreDelete(done);
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    return ESP_OK;
}

static ui_battery_t ui_battery_state(battery_state_t state)
{
    switch (state) {
    case BATTERY_DISCHARGING:
        return UI_BATTERY_DISCHARGING;
    case BATTERY_CHARGING:
        return UI_BATTERY_CHARGING;
    case BATTERY_FULL:
        return UI_BATTERY_FULL;
    default:
        return UI_BATTERY_UNKNOWN;
    }
}

static void render(void)
{
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        return;
    }
    time_t now = time(NULL);
    ui_clock_t clock = { .time_valid = timekeeping_valid() };
    localtime_r(&now, &clock.local);
    sensors_env_t env = sensors_env();
    clock.env_valid = env.valid;
    clock.temp_c10 = (env.temp_c100 + (env.temp_c100 >= 0 ? 5 : -5)) / 10;
    clock.hum_pct = (env.hum_pct100 + 50) / 100;
    sensors_battery_t bat = sensors_battery(now);
    clock.battery_valid = bat.valid;
    clock.battery_pct = bat.level;
    clock.battery_mv = bat.smoothed_mv;
    clock.battery_state = ui_battery_state(bat.state);
    ui_draw_clock(fb, &clock);
    esp_err_t err = display_commit(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display: %s", esp_err_to_name(err));
    }
}

static void sample_sensors(time_t now)
{
    esp_err_t err = sensors_sample_env(now);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SHTC3: %s; keeping the last reading", esp_err_to_name(err));
    }
    err = sensors_sample_battery(now);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "battery: %s", esp_err_to_name(err));
    }
}

static void schedule_next(void)
{
    sched_input_t in = {
        .now = time(NULL),
        .display_every_min = CONFIG_REFLBO_DISPLAY_UPDATE_MIN,
        .sensors_every_min = CONFIG_REFLBO_SENSOR_INTERVAL_MIN,
    };
    s_next_wake = scheduler_next_wake(&in).when;
    esp_err_t err = pcf85063_set_alarm(s_next_wake); /* also clears the alarm flag, which releases INT */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC alarm: %s; the backup timer takes over", esp_err_to_name(err));
    }
}

/* A scheduled minute, from the RTC alarm or its backup. `force` also samples and renders. */
static void on_tick(bool force)
{
    esp_err_t err = timekeeping_load_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC read: %s", esp_err_to_name(err));
    }
    time_t now = time(NULL);
    time_t slot = now - now % 60;
    if (force || scheduler_is_slot(slot, CONFIG_REFLBO_SENSOR_INTERVAL_MIN)) {
        sample_sensors(now);
    }
    if (force || scheduler_is_slot(slot, CONFIG_REFLBO_DISPLAY_UPDATE_MIN)) {
        render();
    }
    schedule_next();
}

static void handle_button(board_button_t button, gesture_t gesture)
{
    power_hold_awake_ms(GRACE_MS);
    if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT) {
        sample_sensors(time(NULL)); /* spec §5.6: refresh sensors */
        render();
        ESP_LOGI(TAG, "BOOT short: sensors refreshed");
        return;
    }
    ESP_LOGI(TAG, "%s %s is not bound yet (menu and presets come in M3, config mode in M4)",
             board_button_name(button), board_gesture_name(gesture));
}

/* The next wake must be at most one update interval away. If the clock moved back (`rtc set`,
 * later SNTP), the pending alarm and its backup timer are too far off: schedule again. A clock
 * that moved forward is caught by the backup tick instead. */
static void check_clock_jump(void)
{
    if (s_next_wake - time(NULL) > (CONFIG_REFLBO_DISPLAY_UPDATE_MIN + 1) * 60) {
        ESP_LOGW(TAG, "clock moved back; scheduling again");
        on_tick(true);
    }
}

static void handle_event(const app_event_t *ev)
{
    switch (ev->type) {
    case EV_RTC_ALARM:
        on_tick(false);
        break;
    case EV_BUTTON:
        handle_button(ev->button.button, ev->button.gesture);
        break;
    case EV_CALL:
        ev->call.fn(ev->call.arg);
        xSemaphoreGive(ev->call.done);
        power_hold_awake_ms(GRACE_MS);
        break;
    }
}

static void handle_wake(power_wake_t wake)
{
    switch (wake) {
    case POWER_WAKE_RTC:
        on_tick(false);
        break;
    case POWER_WAKE_TIMER:
        ESP_LOGW(TAG, "RTC alarm missed; backup wake");
        on_tick(false);
        break;
    case POWER_WAKE_KEY:
    case POWER_WAKE_BOOT:
        board_buttons_resync(); /* edges during sleep raised no interrupt */
        power_hold_awake_ms(GRACE_MS);
        break;
    default:
        break;
    }
}

static void enter_deep_sleep(void)
{
    sensors_export(&s_snap.sensors);
    display_export(&s_snap.display);
    util_snapshot_seal(&s_snap, sizeof(s_snap), SNAP_MAGIC, SNAP_VERSION);
    esp_err_t err = display_prepare_deep_sleep();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel pins: %s", esp_err_to_name(err));
    }
    power_sleep_deep(s_next_wake + BACKUP_S);
}

static esp_err_t start_rtc_int(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_PIN_RTC_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, /* open drain, no external pull-up */
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "RTC INT pin");
    return gpio_isr_handler_add(BOARD_PIN_RTC_INT, on_rtc_int, NULL);
}

static esp_err_t boot(void)
{
    ESP_RETURN_ON_ERROR(power_init(), TAG, "power");
    power_wake_t wake = power_boot_wake();
    bool warm = wake != POWER_WAKE_COLD && util_snapshot_valid(&s_snap, sizeof(s_snap), SNAP_MAGIC, SNAP_VERSION);

    ESP_RETURN_ON_ERROR(board_init(wake == POWER_WAKE_COLD), TAG, "board");
    ESP_RETURN_ON_ERROR(pcf85063_init(board_i2c()), TAG, "RTC");
    ESP_RETURN_ON_ERROR(timekeeping_init(CONFIG_REFLBO_TZ), TAG, "time zone");
    esp_err_t err = timekeeping_load_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC read: %s", esp_err_to_name(err));
    }
    ESP_RETURN_ON_ERROR(sensors_init(board_i2c(), !warm), TAG, "sensors");
    if (warm) {
        sensors_import(&s_snap.sensors);
        ESP_RETURN_ON_ERROR(display_init_warm(&s_snap.display), TAG, "display");
    } else if (wake != POWER_WAKE_COLD) {
        /* Woke from deep sleep without a valid snapshot: the panel still runs, don't reset it. */
        display_state_t fallback = { .variant = PANEL_VARIANT, .mode = ST7305_MODE_LPM, .lpm_rate = ST7305_LPM_1HZ };
        ESP_RETURN_ON_ERROR(display_init_warm(&fallback), TAG, "display");
    } else {
        ESP_RETURN_ON_ERROR(display_init(PANEL_VARIANT), TAG, "display");
    }
    ESP_RETURN_ON_ERROR(board_buttons_start(on_button, k_dashboard_buttons), TAG, "buttons");
    ESP_RETURN_ON_ERROR(start_rtc_int(), TAG, "RTC INT");

    if (wake == POWER_WAKE_KEY) {
        board_buttons_woke(BOARD_BUTTON_KEY);
    } else if (wake == POWER_WAKE_BOOT) {
        board_buttons_woke(BOARD_BUTTON_BOOT);
    }
    if (wake == POWER_WAKE_COLD || wake == POWER_WAKE_KEY || wake == POWER_WAKE_BOOT) {
        power_hold_awake_ms(GRACE_MS);
    }
    on_tick(!warm);
    ESP_LOGI(TAG, "reflbo ready (%s wake%s)", power_wake_name(wake), warm ? ", warm" : "");
    return ESP_OK;
}

static void app_task(void *arg)
{
    (void)arg;
    esp_err_t err = boot();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "boot failed: %s; staying awake for the console", esp_err_to_name(err));
    }
    for (;;) {
        check_clock_jump();
        bool pending = uxQueueMessagesWaiting(s_queue) > 0 || board_buttons_busy();
        power_plan_t plan = err == ESP_OK ? power_plan(pending) : POWER_PLAN_AWAKE;
        if (plan == POWER_PLAN_LIGHT) {
            handle_wake(power_sleep_light(s_next_wake + BACKUP_S));
            continue;
        }
        if (plan == POWER_PLAN_DEEP) {
            enter_deep_sleep();
        }
        int64_t backup_ms = ((int64_t)(s_next_wake + BACKUP_S) - time(NULL)) * 1000;
        int64_t wait_ms = backup_ms < TETHER_RECHECK_MS ? backup_ms : TETHER_RECHECK_MS;
        TickType_t wait = wait_ms <= 0 ? 0 : util_ticks_at_least((uint32_t)wait_ms, portTICK_PERIOD_MS);
        app_event_t ev;
        if (xQueueReceive(s_queue, &ev, wait) == pdTRUE) {
            handle_event(&ev);
        } else if (err == ESP_OK && time(NULL) >= s_next_wake + BACKUP_S) {
            ESP_LOGW(TAG, "RTC alarm missed; backup tick");
            on_tick(false);
        }
    }
}

esp_err_t app_start(void)
{
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(app_event_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue");
    BaseType_t ok = xTaskCreatePinnedToCore(app_task, "app", APP_STACK, NULL, APP_PRIORITY, NULL, APP_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}
```

`main/main.c`:

```c
#include "app.h"
#include "diag.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        /* Never erase NVS on our own (AGENTS.md quick rule 3): settings fall back to defaults. */
        ESP_LOGE(TAG, "NVS unavailable (%s); settings use their defaults", esp_err_to_name(err));
    }

    err = app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "app failed to start: %s", esp_err_to_name(err));
    }

    diag_set_executor(app_execute);
    err = diag_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }
}
```

`main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.c" "app.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES board diag display esp_app_format esp_driver_gpio gfx nvs_flash power rtc
                                     scheduler sensors timekeeping ui util)
```

In `main/Kconfig.projbuild`, add this before `endmenu`:

```cmake
    choice REFLBO_IDLE_DEFAULT
        prompt "Idle strategy until the owner picks one (D3)"
        default REFLBO_IDLE_DEFAULT_LIGHT
        help
            `power idle deep|light` overrides this at runtime and keeps the choice in NVS.
        config REFLBO_IDLE_DEFAULT_LIGHT
            bool "Light sleep"
        config REFLBO_IDLE_DEFAULT_DEEP
            bool "Deep sleep"
    endchoice
```

Append to `sdkconfig.defaults`:

```text

# Faster deep-sleep wakes: skip image validation, quiet bootloader (spec §9.4 optimisation candidates)
CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP=y
CONFIG_BOOTLOADER_LOG_LEVEL_WARN=y
```

`components/diag/diag_cmd_power.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag_internal.h"
#include "esp_console.h"
#include "power.h"

static int usage(const char *text)
{
    printf("usage: %s\n", text);
    return 1;
}

static int power_body(int argc, char **argv)
{
    static const char *const k_usage = "power idle [deep|light]";
    if (argc < 2 || strcmp(argv[1], "idle") != 0 || argc > 3) {
        return usage(k_usage);
    }
    if (argc == 3) {
        power_idle_t idle;
        if (strcmp(argv[2], "deep") == 0) {
            idle = POWER_IDLE_DEEP;
        } else if (strcmp(argv[2], "light") == 0) {
            idle = POWER_IDLE_LIGHT;
        } else {
            return usage(k_usage);
        }
        esp_err_t err = power_set_idle_strategy(idle);
        if (err != ESP_OK) {
            printf("power: %s\n", esp_err_to_name(err));
            return 1;
        }
    }
    printf("power: idle %s%s\n", power_idle_strategy() == POWER_IDLE_DEEP ? "deep" : "light",
           power_tethered() ? " (tethered: stays awake while a USB host is connected)" : "");
    return 0;
}

static int sleep_body(int argc, char **argv)
{
    static const char *const k_usage = "sleep stats [reset] | sleep test <deep|light> <cycles>";
    if (argc >= 2 && strcmp(argv[1], "stats") == 0) {
        if (argc == 3 && strcmp(argv[2], "reset") == 0) {
            power_reset_stats();
        } else if (argc != 2) {
            return usage(k_usage);
        }
        power_stats_t st = power_stats();
        uint32_t sleeps = st.light_sleeps + st.deep_sleeps;
        printf("sleep: %lu light, %lu deep; wakes: rtc %lu, key %lu, boot %lu, timer %lu, other %lu\n",
               (unsigned long)st.light_sleeps, (unsigned long)st.deep_sleeps,
               (unsigned long)st.wakes[POWER_WAKE_RTC], (unsigned long)st.wakes[POWER_WAKE_KEY],
               (unsigned long)st.wakes[POWER_WAKE_BOOT], (unsigned long)st.wakes[POWER_WAKE_TIMER],
               (unsigned long)st.wakes[POWER_WAKE_OTHER]);
        printf("sleep: awake per cycle: last %lu ms, max %lu ms, mean %lu ms; test cycles left %d\n",
               (unsigned long)st.awake_ms_last, (unsigned long)st.awake_ms_max,
               (unsigned long)(sleeps ? st.awake_ms_total / sleeps : 0), power_test_cycles_left());
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "test") == 0) {
        power_idle_t mode;
        if (strcmp(argv[2], "deep") == 0) {
            mode = POWER_IDLE_DEEP;
        } else if (strcmp(argv[2], "light") == 0) {
            mode = POWER_IDLE_LIGHT;
        } else {
            return usage(k_usage);
        }
        int cycles = atoi(argv[3]);
        if (cycles < 1 || cycles > 60) {
            return usage(k_usage);
        }
        power_start_test(mode, cycles);
        printf("sleep: %d %s sleep cycles follow; the USB console drops and returns after them\n", cycles, argv[2]);
        return 0;
    }
    return usage(k_usage);
}

static int cmd_power(int argc, char **argv)
{
    return diag_on_owner(power_body, argc, argv);
}

static int cmd_sleep(int argc, char **argv)
{
    return diag_on_owner(sleep_body, argc, argv);
}

esp_err_t diag_register_power_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "power", .help = "power idle [deep|light]: show or set the idle strategy", .func = &cmd_power },
        { .command = "sleep", .help = "sleep stats [reset] | sleep test <deep|light> <cycles>", .func = &cmd_sleep },
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

In `components/diag/diag_internal.h`, add `esp_err_t diag_register_power_commands(void);   /* power, sleep */`. In `components/diag/diag.c`, after the sensor-commands line, add `ESP_RETURN_ON_ERROR(diag_register_power_commands(), TAG, "power commands");`.

`components/diag/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "diag.c" "diag_cmd_buttons.c" "diag_cmd_display.c" "diag_cmd_power.c"
                            "diag_cmd_sensors.c" "diag_cmd_system.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board console display esp_app_format esp_driver_usb_serial_jtag esp_psram gfx
                                     power rtc sensors spi_flash timekeeping util vfs)
```

`docs/power.md`:

```markdown
# Power measurements

Measured by the owner with a USB power meter, following spec §9.4:

- Power the board from a USB charger, not a PC. With no USB host, the firmware follows its sleep policy.
- Use a fully charged 18650, or remove it, so charging current doesn't distort the reading.
- Read the meter's mAh counter over at least 1 h per scenario.
- Battery-side current ≈ 5 V current × 5 V / V_bat. This ignores converter losses.

Many USB meters are inaccurate below 1 mA, so the long windows matter.

Baseline to beat: the vendor factory firmware draws about 90 mA at 5.3 V (AGENTS.md gotcha 12).

## Firmware facts that shape the numbers

Firmware at M2, measured with `sleep stats`:
- About 170 ms of app time per deep-sleep wake, plus ROM and bootloader time.
- About 60 ms per light-sleep wake.
- One wake per minute: the display updates every minute, and the sensors are sampled on every fifth.
- The panel stays in LPM at 1 Hz throughout.

## Results

| Date | Commit | Scenario | Settings | Meter | Window | mAh | Mean at 5 V | Est. battery current | Notes |
|---|---|---|---|---|---|---|---|---|---|
```

- [ ] **Step 5: Build with the new defaults, flash, boot**

```bash
rm -f sdkconfig && tools/idf.sh reconfigure >/dev/null
grep -E "^CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP=y|^CONFIG_REFLBO_IDLE_DEFAULT_LIGHT=y" sdkconfig
tools/idf.sh build 2>&1 | tee captures/build.log | tail -1
grep "warning:" captures/build.log | grep -cE "/reflbo/(main|components)/"
tools/idf.sh -p PORT flash
tools/idf.sh exec python tools/devlog.py -p PORT --reset --until "reflbo ready" -t 20 -o captures/boot.log >/dev/null; echo "exit=$?"
grep -E "app: reflbo ready|(^|reflbo> )[EW] \(" captures/boot.log
```

Expected:
- Both config lines are present, and there are `0` own warnings.
- `exit=0`, `app: reflbo ready (cold wake)`, and no `E (` or `W (` lines.

- [ ] **Step 6: A tethered board stays awake**

```bash
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "power idle" --cmd "sleep stats reset" -t 15 | grep -E "^(power|sleep):"
sleep 70
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "sleep stats" -t 10 | grep -E "^sleep:"
```

Expected: `power: idle light (tethered: stays awake while a USB host is connected)`, and after 70 s, `sleep: 0 light, 0 deep`.

- [ ] **Step 7: Light-sleep cycles**

```bash
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "sleep test light 2" --until "sleep test finished" -t 200 -o captures/light.log >/dev/null; echo "exit=$?"
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "sleep stats" --cmd "panel fps 4" -t 15 | grep -E "^(sleep|panel fps):"
tools/idf.sh exec python tools/screenshot.py -p PORT -o captures/after-light.png >/dev/null; date +%H:%M
```

Expected:
- `exit=0`, possibly after reconnects.
- `sleep: 2 light, 0 deep; wakes: rtc 2, key 0, boot 0, timer 0, other 0`.
- A last awake time around 60 ms.
- `panel fps: 4 frames in 4 s = 1.00 Hz`: the panel was never reset.
- The screenshot shows the Mac's current minute.

- [ ] **Step 8: Deep-sleep cycles**

```bash
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "sleep test deep 2" -t 10 | grep -E "^sleep:"
sleep 150
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "sleep stats" --cmd "panel fps 4" -t 30 | grep -E "^(sleep|panel fps):"
tools/idf.sh exec python tools/screenshot.py -p PORT -o captures/after-deep.png >/dev/null; date +%H:%M
```

Expected:
- `sleep: 0 light, 2 deep; wakes: rtc 2, key 0, boot 0, timer 0, other 0`.
- A last awake time of about 170 ms.
- `panel fps: … = 1.00 Hz`: the panel kept running through deep sleep and was never reset.
- The screenshot shows the current minute, and the values match `battery`.

Record the awake times in `docs/power.md` under "Firmware facts".

- [ ] **Step 9: Update the docs**

In `AGENTS.md` §3.4:
- **Gotcha 4.** Add: "The hold latches at once, so `st7305_prepare_deep_sleep()` is the last panel access before `esp_deep_sleep_start()`. On wake, `st7305_init_warm()` drives the same levels, configures the pins, then calls `gpio_hold_dis()`. Checked at M2: the panel still ran at 1.00 Hz (`panel fps`) after deep sleep, so it kept its image."
- **Gotcha 11.** Replace it with: "**The USB console works only while the chip is awake.** Deep sleep powers the USB PHY off, so the host sees a detach. Light sleep disables the USB pad; on the owner's Mac the port stays present, but nothing gets through until the chip wakes. So a *tethered* board, one with a USB host sending frames, never sleeps. After a cold boot or a button wake the board stays awake 2 s so a PC can find it. To catch a deep-sleeping board, press KEY. `sleep test <deep|light> <n>` runs sleep cycles while tethered, for testing."
- **New gotcha 19.** "**Light sleep floats every pin** unless told otherwise (`ESP_SLEEP_GPIO_RESET_WORKAROUND`): panel CS and RESET call `gpio_sleep_sel_dis()` so they keep driving; the three wake pins keep theirs through `gpio_wakeup_enable()`, which also switches them to level interrupts until `power_sleep_light()` restores their edge type."
- **New gotcha 20.** "`usb_serial_jtag_is_connected()` watches for SOF frames on every tick while awake. It starts true and turns false after one 10 ms tick without an SOF. A PC needs roughly 0.1–1 s to enumerate the board again after a boot or wake."
- **New gotcha 21.** "**GPIO15 (RTC INT) needs the RTC-domain pull-up in deep sleep** (`rtc_gpio_pullup_en`); `gpio_pullup_en` does not apply there. Clear the alarm flag before sleeping, or ext1 wakes at once."

Elsewhere in `AGENTS.md`:
- **§5.1.** Replace "`CONFIG_PM_ENABLE`; tickless idle; app rollback; `CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`; quiet bootloader logs" with "`CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`; bootloader logs at warning level; `tasks` statistics; app rollback *(planned, M4)*". Automatic light sleep (`CONFIG_PM_ENABLE`) is not used; see D14.
- **§5.3.** Change the tethered sentence to "Tethered mode, with a USB host connected, stays awake (gotcha 11)."
- **§6.** Add to the command block:

  ```sh
  tools/idf.sh exec python tools/devlog.py --cmd "power idle deep"   # or light; kept in NVS (sys/idle)
  tools/idf.sh exec python tools/devlog.py --cmd "sleep test deep 2" # sleep cycles while tethered; then: sleep stats
  ```
- **§7 "Available now".** Add `power idle [deep|light]` and `sleep stats [reset]|test <deep|light> <n>`. Remove `sleep stats` and `power idle <deep|light>` from the planned list.
- **§9.** Add D14: "Light sleep is entered explicitly by the app (`power_sleep_light()`), not by esp_pm automatic light sleep; a tethered board stays awake."

In the spec:
- **§3.4.**
  - Change the light-sleep column heading to "Light sleep (`power_sleep_light()`)".
  - USB console row: "Unusable while asleep: the pad is disabled; the Mac keeps the port".
  - Wake latency row: "About 170 ms of app time per wake (M2)" for deep and "About 1 ms; about 60 ms of app time per wake (M2)" for light.
  - Replace the tethered bullet with gotcha 11's rule and `sleep test`.
- **§3.1.**
  - The `board` row reads "Pin map, I²C bus, GPIO setup and ISR service, buttons → gestures, codec standby".
  - The `power` row reads "Power states, idle strategy, wake sources and wake cause, sleep entry, sleep statistics".
  - The `rtc` row notes that the API uses the `pcf85063_` prefix.
- **§1.2.** Add D14.
- **§14.2.** Change the `sys` row to "Device id, AP password, schema version, idle strategy override (`idle`)".
- **§15.** Add `sleep test <deep|light> <n>` and `sleep stats reset`.
- **§21.** Add r5: "M2: the board stays awake while tethered, light sleep is entered explicitly (D14), measured wake costs, NVS `sys/idle`, sleep test".

- [ ] **Step 10: Commit and push**

```bash
git add components/power components/st7305 components/display components/diag main sdkconfig.defaults docs AGENTS.md
git commit -m "feat(power): add light and deep sleep with a warm panel attach and sleep diagnostics"
git push
```

---

### Task 14: Owner checks and the idle-strategy decision (D3)

**Files:**
- Modify: `docs/power.md`, `AGENTS.md` (§2, §9), the spec (§1.2 D3, §3.4, §21), `main/Kconfig.projbuild` (the idle default)

The owner needs to be present for these checks. Give them exact instructions, one question at a time, using AskUserQuestion.

- [ ] **Step 1: Physical buttons**

Start the capture: `tools/idf.sh exec python tools/devlog.py -p PORT -t 90 -o captures/buttons.log >/dev/null` (in the background).

Ask the owner to do, about 3 s apart:
1. press KEY once
2. press KEY twice quickly
3. hold KEY for 2 s
4. press BOOT once
5. hold BOOT for 4 s

Run: `grep -E "buttons:|app: BOOT" captures/buttons.log`
Expected, in order:
- `buttons: KEY short`
- `buttons: KEY double`
- `buttons: KEY long`
- `buttons: BOOT short`, then `app: BOOT short: sensors refreshed`
- `buttons: BOOT long`

If a press is missing or doubled, debug it with superpowers:systematic-debugging before going on (debounce, pull-ups).

- [ ] **Step 2: BOOT and KEY wake from deep sleep, and the panel through deep sleep**

Run `sleep test deep 4`. Ask the owner to:
- watch the panel: the image must stay, and the time must move on each minute
- press BOOT once during the second minute
- press KEY once during the fourth minute

After about 5 minutes, run `sleep stats` and ask the owner what they saw.

Expected:
- `wakes: … key 1, boot 1`.
- The owner saw the image stay and the minutes advance.
- After BOOT the screen refreshed normally.

Download mode would leave the panel frozen, with no further wakes counted, and would need a power cycle. If that happens, follow the spec §20 fallback: wake only on KEY and the RTC. Remove GPIO0 from the ext1 mask, and record it as a gotcha.

- [ ] **Step 3: Current measurements (about 2 h of the owner's time)**

Give the owner this checklist:
1. With the board on the Mac, run `power idle deep`.
2. Move the USB cable to a charger through the USB power meter. The 18650 must be fully charged or removed. Leave it for at least 1 h, then note the mAh and the meter's model.
3. Back on the Mac, run `power idle light` and repeat.

Also measure the M1 firmware idle current for comparison, if the owner is willing.

Record each run as a row in `docs/power.md`.

- [ ] **Step 4: Decide D3, and mark M2 done**

Decision rule (spec §3.4): choose deep sleep if its average is at least 20 % lower than light sleep's; otherwise choose light sleep.
- Set that default in `main/Kconfig.projbuild` (`REFLBO_IDLE_DEFAULT`).
- Record D3 in `AGENTS.md` §9 and spec §1.2 with the numbers, and add spec r6 in §21.
- In `AGENTS.md` §2, set the status to M2 done, and point "Latest plan" at this plan.
- Build, flash, and run the host suite and a boot check once more.

```bash
git add docs/power.md AGENTS.md docs/specs/2026-09-25-firmware-design.md main/Kconfig.projbuild
git commit -m "docs: record the M2 power measurements and the idle-strategy decision (D3)"
git push
```
