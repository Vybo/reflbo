# M3a: Dashboards, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the M2 clock screen with configurable dashboards. Four layouts show optional fields through presets that KEY switches and cycles, and that survive a reboot in LittleFS. The first half of M3 (spec §18, D15).

**Architecture:**

- **Pure logic, host-tested:**
  - `util` calendar helpers: ISO week, moon phase from Meeus's low-precision terms.
  - `gfx`: fixes and the missing primitives (bitmaps, circles, ellipsis).
  - `locale`: language packs, prefix `lang_` because libc owns `locale_t`; `en` only.
  - `datastore`: measured values with freshness, derived dew point, today's min and max, hour trends.
  - The battery gauge's days-left estimate.
  - Scheduler wakes for auto-cycling and the seconds display.
  - `ui`: field resolution, widgets, layouts, status bar, presets and their JSON codec, and the dashboard renderer, checked by golden renders the owner reviewed.
  - The `storage` settings codec and config-file logic (atomic writes, the `.bak` fallback).
- **Device side:**
  - `storage` mounts LittleFS on the `storage` partition and wraps the config-file logic for the app.
  - `main/app_ui.c` owns the dashboard state: settings, presets, datastore, cycling.
  - `main/app.c` keeps orchestrating the hardware; the snapshot now carries the dashboard, so deep-sleep wakes skip storage.
  - `field` and `preset` console commands.
- **Fields that follow from the clock** (time, date, week, moon, name day, holiday) are computed in `ui`; the datastore holds only measured and fetched values.

**Tech stack:** ESP-IDF v5.5.5 with its bundled cJSON (`json`) and `joltwallet/littlefs` 1.22.3 from the component registry; Unity on the host, where the same cJSON source is compiled from the IDF tree; Python 3 with pinned Pillow for the font and icon generators.

**Spec:** `docs/specs/2026-09-25-firmware-design.md` r8.
- Relevant: §3.1–§3.3, §4.3–§4.5, §5.1–§5.6, §6, §7, §8, §9.1–§9.2, §14.1–§14.3, §15, §17, §18 (M3), §19 and D15.
- Also `AGENTS.md` §3 (gotchas 4, 7, 11, 20), §5.3, §6–§8.

**Research behind this plan (2026-09-28):**
- A spike in a throwaway worktree built every task below. On the board it confirmed:
  - LittleFS formats and mounts (24 of 8000 KB used).
  - KEY short switches presets, and the choice survives a reboot.
  - Auto-cycle switches after its interval.
  - `field set` renders at once.
  - A backward `rtc set` redraws at once.
  - Two deep-sleep cycles keep the preset and the data (64 ms awake per wake).
  - The panel still runs at 1.00 Hz.
- Host renders of all fixtures went to the owner, who approved them on 2026-09-28 and asked for two status-bar options: the battery's level, voltage and days left, and a clock in the middle for data-first presets. The spike added both; the goldens below include them.
- Two changes came after those board checks and are so far checked on the host and by building only. Task 13's board checks cover both:
  - The config-file logic moved out of `storage.c` into the host-tested `storage_file.c`.
  - Auto-cycling is now armed by the first tick after boot rather than at load. Before the RTC was read, load armed it from a clock near 1970, so with cycling on, a reboot switched presets at once.
- Reference moon phases for the tests come from PyEphem.
- The icon font is Google's Material Icons (Apache-2.0), pinned at upstream commit `f7bd4f25f3764883717c09a1fd867f560c9a9581` (see Task 3).

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, Python 3 host tools.
- `[host]` components (`util`, `gfx`, `locale`, `datastore`, `ui`, `scheduler`) and the pure files named per task include no ESP-IDF headers. cJSON counts as plain C.
- ESP-IDF style: 4-space indent, `snake_case`, a component or module prefix on public APIs, public headers in `include/`.
- The app task owns the display, `st7305`, the I²C devices, storage, the datastore and sleep (spec §3.2, AGENTS §5.3). Console commands that touch them run through `diag_on_owner()`.
- Never erase flash or NVS. Formatting a blank `storage` partition at mount is by design (spec §14.1). Board commands go through `tools/idf.sh` with an explicit `-p`, and the port must be confirmed as this board (Espressif `303A:1001`, MAC `14:c1:9f:54:bb:94`).
- List every component source in `SRCS`. After adding a component directory, run `tools/idf.sh reconfigure`. Pin registry dependencies with `==` and commit `dependencies.lock`; never edit `managed_components/`.
- Small, focused Conventional Commits that each build. Push to `origin` freely; never force-push. No AI or assistant attribution anywhere.
- Build only what the spec covers (spec r8 and D15). The menu, toast and critical-battery screens, settings editing, the schedule and night sleep, and the `cs` pack are M3b.

## Review Focus

1. **A corrupt or hand-edited `presets.json` or `settings.json`.** The firmware must fall back to `.bak`, then to defaults, never crash or show a half-parsed preset. Pinned by:
   - `test_invalid_files_are_rejected_with_a_reason` and `test_lenient_parts_fall_back_to_defaults` (Task 9)
   - `test_bad_values_are_clamped_or_ignored_one_by_one` and `test_not_json_or_another_schema_fails` (Task 11)
   - `test_a_rejected_file_falls_back_to_the_backup`, `test_power_lost_between_the_renames_loads_the_previous_file` and `test_both_rejected_is_invalid` (Task 11)
   - `load_one()` restoring the defaults when a parse fails part-way (Task 13)
2. **Values that don't fit a slot**: long names, °F with three digits, negative temperatures, a clock with seconds in 12-hour mode. Text must never run into a neighbouring slot. Pinned by:
   - the ellipsis tests (Task 2)
   - the `indoor_cold`, `home_12h_charging` and `focus_seconds` goldens (Task 10)
   - the widgets clip to their slot
3. **Stale and missing data** after a sensor failure or a long deep sleep. The status bar must warn, and slots must follow the stale policy. Pinned by:
   - `test_values_go_stale_after_their_ttl` (Task 5)
   - `test_old_readings_are_stale_with_their_age` (Task 8)
   - the `home_stale` and `home_invalid` goldens (Task 10)
4. **Sub-minute wakes on battery**: auto-cycle and seconds. They must not re-sample the sensors every wake or starve the RTC alarm, and a routine deep wake must not touch storage. Pinned by:
   - the scheduler tests (Task 7)
   - `done_slot` in `app_ui_tick()` (Task 13)
   - the device deep-sleep check (Task 13)
5. **The clock moving** (`rtc set` now, SNTP from M5). The screen must follow at once, and the alarm must be rescheduled exactly, across DST too. Pinned by:
   - the EV_CALL time-jump check and the exact `check_clock_jump()` (Task 13)
   - auto-cycling armed at the first tick and re-armed when the clock moves back (Task 13)
   - the device `rtc set` check (Task 13)


---

### Task 1: Calendar helpers (`util`)

**Files:**
- Create: `components/util/include/util_calendar.h`, `components/util/util_calendar.c`, `test/host/test_util_calendar.c`
- Modify: `components/util/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `util_days_from_civil()` (`util_time.h`).
- Produces: `int util_iso_week(int year, int month, int day)`; `util_moon_t { int index; int illumination; double age; }` and `util_moon_t util_moon_phase(time_t utc)` — index 0 new … 4 full … 7 waning crescent, illumination in %, age 0–1 through the cycle (Tasks 8, 10).

- [ ] **Step 1: Write the failing test.** Reference instants come from PyEphem (UTC).

```c
#include "unity.h"
#include "util_calendar.h"
#include "util_time.h"

void setUp(void) {}
void tearDown(void) {}

static time_t utc(int y, int mo, int d, int h, int mi, int s)
{
    return (time_t)(util_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s);
}

static void test_iso_weeks_including_year_boundaries(void)
{
    TEST_ASSERT_EQUAL_INT(39, util_iso_week(2026, 9, 25));
    TEST_ASSERT_EQUAL_INT(1, util_iso_week(2026, 1, 1));   /* a Thursday */
    TEST_ASSERT_EQUAL_INT(53, util_iso_week(2027, 1, 1));  /* 2026 has 53 weeks */
    TEST_ASSERT_EQUAL_INT(53, util_iso_week(2026, 12, 31));
    TEST_ASSERT_EQUAL_INT(52, util_iso_week(2023, 1, 1));  /* a Sunday, still in 2022's last week */
    TEST_ASSERT_EQUAL_INT(1, util_iso_week(2024, 12, 30)); /* a Monday, already week 1 of 2025 */
    TEST_ASSERT_EQUAL_INT(53, util_iso_week(2021, 1, 3));  /* 2020 is a leap year starting on a Wednesday */
}

/* Reference instants from PyEphem (UTC). */
static void check_phase(time_t t, int index, int min_illum, int max_illum)
{
    util_moon_t m = util_moon_phase(t);
    TEST_ASSERT_EQUAL_INT(index, m.index);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(min_illum, m.illumination);
    TEST_ASSERT_LESS_OR_EQUAL_INT(max_illum, m.illumination);
}

static void test_new_quarter_and_full_moons_of_2026(void)
{
    check_phase(utc(2026, 1, 18, 19, 51, 55), 0, 0, 1);
    check_phase(utc(2026, 2, 17, 12, 1, 5), 0, 0, 1);
    check_phase(utc(2026, 1, 26, 4, 47, 21), 2, 47, 53);
    check_phase(utc(2026, 2, 24, 12, 27, 33), 2, 47, 53);
    check_phase(utc(2026, 1, 3, 10, 2, 51), 4, 99, 100);
    check_phase(utc(2026, 3, 3, 11, 37, 50), 4, 99, 100);
    check_phase(utc(2026, 1, 10, 15, 48, 21), 6, 47, 53);
    check_phase(utc(2026, 2, 9, 12, 43, 4), 6, 47, 53);
}

static void test_illumination_between_phases(void)
{
    TEST_ASSERT_INT_WITHIN(2, 96, util_moon_phase(utc(2026, 9, 28, 12, 0, 0)).illumination);
    TEST_ASSERT_INT_WITHIN(2, 99, util_moon_phase(utc(2026, 12, 25, 0, 0, 0)).illumination);
    TEST_ASSERT_INT_WITHIN(2, 18, util_moon_phase(utc(2027, 6, 1, 0, 0, 0)).illumination);
}

static void test_waxing_before_full_and_waning_after(void)
{
    util_moon_t before = util_moon_phase(utc(2026, 2, 28, 0, 0, 0));
    util_moon_t after = util_moon_phase(utc(2026, 3, 7, 0, 0, 0));
    TEST_ASSERT_EQUAL_INT(3, before.index);
    TEST_ASSERT_EQUAL_INT(5, after.index);
    TEST_ASSERT_TRUE(before.age < 0.5 && after.age > 0.5);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_iso_weeks_including_year_boundaries);
    RUN_TEST(test_new_quarter_and_full_moons_of_2026);
    RUN_TEST(test_illumination_between_phases);
    RUN_TEST(test_waxing_before_full_and_waning_after);
    return UNITY_END();
}
```

- [ ] **Step 2: Register it and watch it fail.** In `test/host/CMakeLists.txt`, link `util` with libm and add the test after `test_util_time`:

```cmake
add_library(util STATIC ${UTIL_SOURCES})
target_link_libraries(util PUBLIC m)
```

```cmake
reflbo_host_test(test_util_time util)
reflbo_host_test(test_util_calendar util)
```

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL to compile, `util_calendar.h` not found.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <time.h>

/* Calendar helpers for the date fields (spec §5.1). Pure C, host-buildable. */

/* ISO 8601 week number (1–53) of a civil date. */
int util_iso_week(int year, int month, int day);

typedef struct {
    int index;        /* 0 new, 1 waxing crescent, 2 first quarter, 3 waxing gibbous, 4 full,
                         5 waning gibbous, 6 last quarter, 7 waning crescent */
    int illumination; /* lit fraction of the disc, % */
    double age;       /* position in the cycle: 0 new, 0.5 full, towards 1 the next new moon */
} util_moon_t;

/* Moon phase at a UTC time, from Meeus's low-precision terms (Astronomical Algorithms,
 * chapter 48): within an hour or so of the true phase, plenty for a desk display. */
util_moon_t util_moon_phase(time_t utc);
```

```c
#include "util_calendar.h"

#include <math.h>
#include <stdbool.h>

#include "util_time.h"

#define PI 3.14159265358979323846

/* Monday = 1 … Sunday = 7; 1970-01-01 was a Thursday. */
static int iso_weekday(int64_t days)
{
    return (int)(((days % 7) + 10) % 7) + 1;
}

static bool is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static bool has_week_53(int year)
{
    int jan1 = iso_weekday(util_days_from_civil(year, 1, 1));
    return jan1 == 4 || (jan1 == 3 && is_leap(year));
}

int util_iso_week(int year, int month, int day)
{
    int64_t days = util_days_from_civil(year, month, day);
    int ordinal = (int)(days - util_days_from_civil(year, 1, 1)) + 1;
    int week = (ordinal - iso_weekday(days) + 10) / 7;
    if (week < 1) {
        return has_week_53(year - 1) ? 53 : 52;
    }
    if (week == 53 && !has_week_53(year)) {
        return 1;
    }
    return week;
}

static double norm360(double deg)
{
    deg = fmod(deg, 360.0);
    return deg < 0 ? deg + 360.0 : deg;
}

util_moon_t util_moon_phase(time_t utc)
{
    const double r = PI / 180.0;
    double jd = (double)utc / 86400.0 + 2440587.5;
    double t = (jd - 2451545.0) / 36525.0;
    double d = norm360(297.8501921 + 445267.1114034 * t);  /* mean elongation */
    double m = norm360(357.5291092 + 35999.0502909 * t);   /* the Sun's mean anomaly */
    double mp = norm360(134.9633964 + 477198.8675055 * t); /* the Moon's mean anomaly */
    double phase_angle = 180.0 - d - 6.289 * sin(mp * r) + 2.100 * sin(m * r) - 1.274 * sin((2 * d - mp) * r) -
                         0.658 * sin(2 * d * r) - 0.214 * sin(2 * mp * r) - 0.110 * sin(d * r);
    double elongation = norm360(180.0 - phase_angle); /* 0 new, 180 full */

    util_moon_t moon;
    moon.age = elongation / 360.0;
    moon.index = (int)floor(moon.age * 8.0 + 0.5) % 8;
    moon.illumination = (int)floor((1.0 + cos(phase_angle * r)) / 2.0 * 100.0 + 0.5);
    return moon;
}
```

```cmake
# Small pure-C helpers shared by the firmware and the host tests (no ESP-IDF headers).
idf_component_register(SRCS "util_crc32.c" "util_base64.c" "util_snapshot.c" "util_ticks.c" "util_time.c" "util_calendar.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_util_calendar && ctest --test-dir build-host --output-on-failure`
Expected: `4 Tests 0 Failures`; the whole suite passes.

- [ ] **Step 5: Commit.**

```bash
git add components/util test/host/test_util_calendar.c test/host/CMakeLists.txt
git commit -m "feat(util): add ISO week and moon phase helpers"
```

---

### Task 2: gfx fixes and new primitives

**Files:**
- Modify: `components/gfx/include/gfx.h`, `components/gfx/gfx.c`, `components/gfx/gfx_text.c`, `test/host/test_gfx.c`, `test/host/test_gfx_text.c`

**Interfaces:**
- Produces:
  - `gfx_bitmap_t { const uint8_t *bits; uint8_t width, height; }` and `gfx_bitmap(fb, x, y, bm, color)`: glyph-format bitmaps, ink only (Task 3's icons, Task 8).
  - `gfx_circle(fb, cx, cy, r, color)`, `gfx_fill_circle(...)`.
  - `int gfx_text_ellipsize(font, utf8, max_width, out, out_size)`: cuts at a codepoint and ends with "…" (or "..." if the font lacks it).
  - `const gfx_glyph_t *gfx_font_glyph(font, cp)`.
  - Text functions treat NULL as "".
- Fixes (M1 review minors): NULL strings; the missing-glyph box drawn with int coordinates, since a `gfx_rect_t` wrapped at x > 32767; `gfx_pixel` checking the buffer bounds as well as the clip; `gfx_line` clipped with Cohen–Sutherland in 64-bit before stepping; the hollow-box test; the malformed-UTF-8 test asserting where the pointer stops.

- [ ] **Step 1: Write the failing tests** (both files replace the old ones).

```c
#include <string.h>

#include "gfx.h"
#include "unity.h"

#define GUARD 0xA5

/* 16x4 framebuffer (8 bytes) with two guard bytes on each side to catch out-of-bounds writes. */
static uint8_t s_mem[2 + 8 + 2];
static gfx_fb_t s_fb;

void setUp(void)
{
    memset(s_mem, GUARD, sizeof(s_mem));
    memset(s_mem + 2, 0, 8);
    gfx_fb_init(&s_fb, s_mem + 2, 16, 4);
}

void tearDown(void) {}

static void assert_guards_intact(void)
{
    TEST_ASSERT_EQUAL_HEX8(GUARD, s_mem[0]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, s_mem[1]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, s_mem[10]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, s_mem[11]);
}

static int count_black(void)
{
    int n = 0;
    for (int y = 0; y < s_fb.height; y++) {
        for (int x = 0; x < s_fb.width; x++) {
            n += gfx_get_pixel(&s_fb, x, y) ? 1 : 0;
        }
    }
    return n;
}

static void test_pixel_bits_are_msb_first_row_major(void)
{
    gfx_pixel(&s_fb, 0, 0, GFX_BLACK);
    gfx_pixel(&s_fb, 7, 0, GFX_BLACK);
    gfx_pixel(&s_fb, 8, 1, GFX_BLACK);
    TEST_ASSERT_EQUAL_HEX8(0x81, s_fb.buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_fb.buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_fb.buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x80, s_fb.buf[3]);
    TEST_ASSERT_EQUAL_INT(3, count_black());
}

static void test_white_clears_and_invert_toggles(void)
{
    gfx_pixel(&s_fb, 3, 2, GFX_BLACK);
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 3, 2));
    gfx_pixel(&s_fb, 3, 2, GFX_WHITE);
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 3, 2));
    gfx_pixel(&s_fb, 3, 2, GFX_INVERT);
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 3, 2));
    gfx_pixel(&s_fb, 3, 2, GFX_INVERT);
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 3, 2));
}

static void test_drawing_outside_the_framebuffer_changes_nothing(void)
{
    gfx_pixel(&s_fb, -1, 0, GFX_BLACK);
    gfx_pixel(&s_fb, 16, 0, GFX_BLACK);
    gfx_pixel(&s_fb, 0, -1, GFX_BLACK);
    gfx_pixel(&s_fb, 0, 4, GFX_BLACK);
    gfx_pixel(&s_fb, 1000, 1000, GFX_BLACK);
    gfx_fill_rect(&s_fb, (gfx_rect_t){ -100, -100, 20, 20 }, GFX_BLACK);
    gfx_hline(&s_fb, -5, 1, 3, GFX_BLACK);
    gfx_vline(&s_fb, 20, 0, 4, GFX_BLACK);
    gfx_hline(&s_fb, 0, 1, -7, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(0, count_black());
    assert_guards_intact();
}

static void test_huge_rectangles_are_clipped_to_the_framebuffer(void)
{
    gfx_fill_rect(&s_fb, (gfx_rect_t){ -1000, -1000, 30000, 30000 }, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(64, count_black());
    assert_guards_intact();
}

static void test_clip_limits_drawing(void)
{
    gfx_set_clip(&s_fb, (gfx_rect_t){ 2, 1, 3, 2 });
    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 16, 4 }, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(6, count_black());
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 1));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 4, 2));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 5, 2));
    gfx_reset_clip(&s_fb);
    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 16, 4 }, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(64, count_black());
}

static void test_clip_is_intersected_with_the_framebuffer(void)
{
    gfx_set_clip(&s_fb, (gfx_rect_t){ 10, 2, 100, 100 });
    TEST_ASSERT_EQUAL_INT(10, s_fb.clip.x);
    TEST_ASSERT_EQUAL_INT(2, s_fb.clip.y);
    TEST_ASSERT_EQUAL_INT(6, s_fb.clip.w);
    TEST_ASSERT_EQUAL_INT(2, s_fb.clip.h);
    gfx_set_clip(&s_fb, (gfx_rect_t){ 20, 20, 5, 5 });
    TEST_ASSERT_EQUAL_INT(0, s_fb.clip.w);
    TEST_ASSERT_EQUAL_INT(0, s_fb.clip.h);
    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 16, 4 }, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(0, count_black());
}

static void test_rect_outline_draws_each_pixel_once(void)
{
    gfx_rect(&s_fb, (gfx_rect_t){ 0, 0, 4, 3 }, GFX_INVERT);
    TEST_ASSERT_EQUAL_INT(10, count_black());
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 0, 0));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 3, 2));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 1, 1));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 2, 1));
}

static void test_line_includes_both_endpoints_in_either_direction(void)
{
    gfx_line(&s_fb, 3, 3, 0, 0, GFX_BLACK);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, i, i));
    }
    gfx_line(&s_fb, 15, 0, 15, 3, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(8, count_black());
}

static void test_pbm_has_header_and_raster(void)
{
    uint8_t out[16];
    gfx_pixel(&s_fb, 0, 0, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(16, (int)gfx_pbm_size(&s_fb));
    TEST_ASSERT_EQUAL_INT(16, (int)gfx_pbm_encode(&s_fb, out, sizeof(out)));
    TEST_ASSERT_EQUAL_MEMORY("P4\n16 4\n", out, 8);
    TEST_ASSERT_EQUAL_HEX8(0x80, out[8]);
    TEST_ASSERT_EQUAL_INT(0, (int)gfx_pbm_encode(&s_fb, out, 15));
}

/* A clip assigned by hand may reach past the buffer; pixels there must still be dropped. */
static void test_pixel_ignores_a_clip_that_reaches_outside_the_buffer(void)
{
    s_fb.clip = (gfx_rect_t){ -8, -8, 64, 64 };
    gfx_pixel(&s_fb, -1, 0, GFX_BLACK);
    gfx_pixel(&s_fb, 16, 3, GFX_BLACK);
    gfx_pixel(&s_fb, 0, 4, GFX_BLACK);
    assert_guards_intact();
    TEST_ASSERT_EQUAL_INT(0, count_black());
}

/* The review found that lines stepped their whole unclipped length and 2*err could overflow. */
static void test_huge_line_draws_only_its_visible_part(void)
{
    gfx_line(&s_fb, -2000000000, 2, 2000000000, 2, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(16, count_black());
    for (int x = 0; x < 16; x++) {
        TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, x, 2));
    }
    gfx_line(&s_fb, -50, -50, -10, -40, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(16, count_black());
    assert_guards_intact();
}

static uint8_t s_big[16 * 16 / 8];
static gfx_fb_t s_big_fb;

static int count_big(void)
{
    int n = 0;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            n += gfx_get_pixel(&s_big_fb, x, y) ? 1 : 0;
        }
    }
    return n;
}

static void big_setup(void)
{
    memset(s_big, 0, sizeof(s_big));
    gfx_fb_init(&s_big_fb, s_big, 16, 16);
}

static void test_circle_is_symmetric_and_draws_each_pixel_once(void)
{
    big_setup();
    gfx_circle(&s_big_fb, 8, 8, 5, GFX_INVERT);
    int n = count_big();
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_big_fb, 13, 8));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_big_fb, 3, 8));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_big_fb, 8, 3));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_big_fb, 8, 13));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_big_fb, 8, 8));
    for (int y = 1; y < 16; y++) {
        for (int x = 1; x < 16; x++) { /* mirror images around (8, 8) */
            TEST_ASSERT_EQUAL(gfx_get_pixel(&s_big_fb, x, y), gfx_get_pixel(&s_big_fb, 16 - x, y));
            TEST_ASSERT_EQUAL(gfx_get_pixel(&s_big_fb, x, y), gfx_get_pixel(&s_big_fb, y, x));
        }
    }
    gfx_circle(&s_big_fb, 8, 8, 5, GFX_INVERT); /* a pixel drawn twice would survive the second pass */
    TEST_ASSERT_EQUAL_INT(0, count_big());
    TEST_ASSERT_TRUE(n > 20);
}

static void test_filled_circle_covers_its_outline(void)
{
    big_setup();
    gfx_circle(&s_big_fb, 8, 8, 5, GFX_BLACK);
    int outline = count_big();
    big_setup();
    gfx_fill_circle(&s_big_fb, 8, 8, 5, GFX_BLACK);
    int filled = count_big();
    TEST_ASSERT_INT_WITHIN(8, 95, filled); /* pi * 5.5^2: the fill reaches the outline's rim */
    gfx_circle(&s_big_fb, 8, 8, 5, GFX_INVERT);
    TEST_ASSERT_EQUAL_INT(filled - outline, count_big()); /* every outline pixel was inside the fill */
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_big_fb, 8, 8));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_big_fb, 14, 8));
}

static void test_bitmap_draws_ink_only_and_clips(void)
{
    static const uint8_t bits[] = { 0xA0, 0x40, 0xA0 }; /* #.# / .#. / #.# */
    const gfx_bitmap_t x_mark = { bits, 3, 3 };
    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 16, 4 }, GFX_BLACK);
    gfx_bitmap(&s_fb, 1, 0, &x_mark, GFX_WHITE);
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 1, 0));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 0)); /* not ink: left alone */
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 2, 1));
    TEST_ASSERT_EQUAL_INT(64 - 5, count_black());
    gfx_bitmap(&s_fb, 14, 2, &x_mark, GFX_WHITE); /* runs off the right and bottom edges */
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 14, 2));
    assert_guards_intact();
    gfx_bitmap(&s_fb, 0, 0, NULL, GFX_WHITE);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pixel_bits_are_msb_first_row_major);
    RUN_TEST(test_white_clears_and_invert_toggles);
    RUN_TEST(test_drawing_outside_the_framebuffer_changes_nothing);
    RUN_TEST(test_huge_rectangles_are_clipped_to_the_framebuffer);
    RUN_TEST(test_clip_limits_drawing);
    RUN_TEST(test_clip_is_intersected_with_the_framebuffer);
    RUN_TEST(test_rect_outline_draws_each_pixel_once);
    RUN_TEST(test_line_includes_both_endpoints_in_either_direction);
    RUN_TEST(test_pbm_has_header_and_raster);
    RUN_TEST(test_pixel_ignores_a_clip_that_reaches_outside_the_buffer);
    RUN_TEST(test_huge_line_draws_only_its_visible_part);
    RUN_TEST(test_circle_is_symmetric_and_draws_each_pixel_once);
    RUN_TEST(test_filled_circle_covers_its_outline);
    RUN_TEST(test_bitmap_draws_ink_only_and_clips);
    return UNITY_END();
}
```

```c
#include <string.h>

#include "gfx.h"
#include "unity.h"

/* Hand-made font: 'A' is a 3x3 arch, 'Ž' a 2x2 block, space has no ink. */
static const uint8_t s_bitmap[] = {
    0x40, 0xA0, 0xE0, /* 'A': .#. / #.# / ### */
    0xC0, 0xC0,       /* 'Ž': ## / ## */
};
static const gfx_glyph_t s_glyphs[] = {
    { 0x0020, 0, 0, 0, 0, 0, 2 },
    { 0x0041, 0, 3, 3, 0, -3, 4 },
    { 0x017D, 3, 2, 2, 0, -2, 3 },
};
static const gfx_font_t s_font = { s_bitmap, s_glyphs, 3, 3, 4 };

static uint8_t s_buf[16]; /* 16x8 */
static gfx_fb_t s_fb;

void setUp(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    gfx_fb_init(&s_fb, s_buf, 16, 8);
}

void tearDown(void) {}

static int count_black(void)
{
    int n = 0;
    for (int y = 0; y < s_fb.height; y++) {
        for (int x = 0; x < s_fb.width; x++) {
            n += gfx_get_pixel(&s_fb, x, y) ? 1 : 0;
        }
    }
    return n;
}

static void test_utf8_decodes_multibyte_characters(void)
{
    const char *s = "Ž°€A";
    TEST_ASSERT_EQUAL_HEX32(0x017D, gfx_utf8_next(&s));
    TEST_ASSERT_EQUAL_HEX32(0x00B0, gfx_utf8_next(&s));
    TEST_ASSERT_EQUAL_HEX32(0x20AC, gfx_utf8_next(&s));
    TEST_ASSERT_EQUAL_HEX32(0x0041, gfx_utf8_next(&s));
    TEST_ASSERT_EQUAL_HEX32(0, gfx_utf8_next(&s));
}

static void test_malformed_utf8_becomes_replacement_and_stops_at_nul(void)
{
    const char *cases[] = { "\xFF", "\xC5", "\xE2\x82", "\xC0\x80", "\xED\xA0\x80" };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char *s = cases[i];
        TEST_ASSERT_EQUAL_HEX32(0xFFFD, gfx_utf8_next(&s));
        while (*s != '\0') {
            TEST_ASSERT_EQUAL_HEX32(0xFFFD, gfx_utf8_next(&s));
        }
        TEST_ASSERT_EQUAL_HEX32(0, gfx_utf8_next(&s));
        TEST_ASSERT_EQUAL_PTR(cases[i] + strlen(cases[i]), s); /* stopped on the NUL, not past it */
    }
}

static void test_text_width_sums_advances(void)
{
    TEST_ASSERT_EQUAL_INT(10, gfx_text_width(&s_font, "A A"));
    TEST_ASSERT_EQUAL_INT(7, gfx_text_width(&s_font, "AŽ"));
    TEST_ASSERT_EQUAL_INT(0, gfx_text_width(&s_font, ""));
}

static void test_missing_glyph_uses_box_advance(void)
{
    TEST_ASSERT_TRUE(gfx_font_has_glyph(&s_font, 'A'));
    TEST_ASSERT_FALSE(gfx_font_has_glyph(&s_font, 'B'));
    TEST_ASSERT_EQUAL_INT(4, gfx_text_width(&s_font, "B"));
}

static void test_glyph_is_placed_relative_to_pen_and_baseline(void)
{
    TEST_ASSERT_EQUAL_INT(5, gfx_text(&s_fb, &s_font, 1, 4, "A", GFX_BLACK));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 1));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 1, 2));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 3, 2));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 1, 3));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 3));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 3, 3));
    TEST_ASSERT_EQUAL_INT(6, count_black());
}

/* No ink at all: a tall line box, so the fallback box is big enough to be hollow (6x6). */
static const gfx_font_t s_tall = { s_bitmap, s_glyphs, 3, 9, 12 };

static void test_missing_glyph_draws_a_hollow_box(void)
{
    TEST_ASSERT_EQUAL_INT(8, gfx_text(&s_fb, &s_tall, 0, 7, "B", GFX_BLACK));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 1, 1));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 6, 6));
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 3, 3));
    TEST_ASSERT_EQUAL_INT(20, count_black());
}

/* The review found the box's x was cut to int16_t, so far off-screen text drew phantom boxes. */
static void test_missing_glyph_far_right_draws_nothing(void)
{
    gfx_text(&s_fb, &s_font, 65536, 4, "B", GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(0, count_black());
}

static void test_null_strings_are_empty(void)
{
    TEST_ASSERT_EQUAL_INT(0, gfx_text_width(&s_font, NULL));
    TEST_ASSERT_EQUAL_INT(3, gfx_text(&s_fb, &s_font, 3, 4, NULL, GFX_BLACK));
    gfx_text_in_rect(&s_fb, &s_font, (gfx_rect_t){ 0, 0, 16, 8 }, GFX_ALIGN_CENTER, NULL, GFX_BLACK);
    char out[8];
    TEST_ASSERT_EQUAL_INT(0, gfx_text_ellipsize(&s_font, NULL, 10, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_INT(0, count_black());
}

/* 'A' (advance 4) and the ellipsis (advance 3). */
static const gfx_glyph_t s_dot_glyphs[] = {
    { 0x0041, 0, 3, 3, 0, -3, 4 },
    { 0x2026, 0, 1, 1, 0, -1, 3 },
};
static const gfx_font_t s_dots = { s_bitmap, s_dot_glyphs, 2, 3, 4 };

static void test_ellipsize_keeps_text_that_fits_and_cuts_the_rest(void)
{
    char out[16];
    TEST_ASSERT_EQUAL_INT(8, gfx_text_ellipsize(&s_dots, "AA", 13, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("AA", out);
    TEST_ASSERT_EQUAL_INT(11, gfx_text_ellipsize(&s_dots, "AAAAAA", 13, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("AA\xE2\x80\xA6", out);
    TEST_ASSERT_EQUAL_INT(3, gfx_text_ellipsize(&s_dots, "AAAAAA", 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("\xE2\x80\xA6", out);
    TEST_ASSERT_EQUAL_INT(4, gfx_text_ellipsize(&s_dots, "AAAAAA", 100, out, 2)); /* one A fits the buffer */
    TEST_ASSERT_EQUAL_STRING("A", out);
}

static void test_text_in_rect_centres_horizontally_and_vertically(void)
{
    gfx_text_in_rect(&s_fb, &s_font, (gfx_rect_t){ 0, 0, 16, 8 }, GFX_ALIGN_CENTER, "A", GFX_BLACK);
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 7, 2));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 6, 4));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 8, 4));
    TEST_ASSERT_EQUAL_INT(6, count_black());
}

static void test_text_in_rect_aligns_right(void)
{
    gfx_text_in_rect(&s_fb, &s_font, (gfx_rect_t){ 0, 0, 16, 8 }, GFX_ALIGN_RIGHT, "A", GFX_BLACK);
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 13, 2));
    TEST_ASSERT_EQUAL_INT(6, count_black());
}

static void test_text_in_rect_clips_to_the_rect_and_restores_the_clip(void)
{
    gfx_text_in_rect(&s_fb, &s_font, (gfx_rect_t){ 0, 0, 5, 8 }, GFX_ALIGN_LEFT, "AA", GFX_BLACK);
    for (int y = 0; y < 8; y++) {
        for (int x = 5; x < 16; x++) {
            TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, x, y));
        }
    }
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 4, 4));
    TEST_ASSERT_EQUAL_INT(16, s_fb.clip.w);
    TEST_ASSERT_EQUAL_INT(8, s_fb.clip.h);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_utf8_decodes_multibyte_characters);
    RUN_TEST(test_malformed_utf8_becomes_replacement_and_stops_at_nul);
    RUN_TEST(test_text_width_sums_advances);
    RUN_TEST(test_missing_glyph_uses_box_advance);
    RUN_TEST(test_glyph_is_placed_relative_to_pen_and_baseline);
    RUN_TEST(test_missing_glyph_draws_a_hollow_box);
    RUN_TEST(test_missing_glyph_far_right_draws_nothing);
    RUN_TEST(test_null_strings_are_empty);
    RUN_TEST(test_ellipsize_keeps_text_that_fits_and_cuts_the_rest);
    RUN_TEST(test_text_in_rect_centres_horizontally_and_vertically);
    RUN_TEST(test_text_in_rect_aligns_right);
    RUN_TEST(test_text_in_rect_clips_to_the_rect_and_restores_the_clip);
    return UNITY_END();
}
```

- [ ] **Step 2: Watch them fail.**

Run: `cmake --build build-host`
Expected: FAIL to compile: `gfx_circle`, `gfx_bitmap`, `gfx_bitmap_t` and `gfx_text_ellipsize` undeclared.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx_font.h"

/*
 * 1-bpp drawing (spec §4.3). Framebuffer: row-major, MSB = leftmost pixel, bit 1 = black,
 * which is the PBM P4 raster layout. Pure C with no ESP-IDF headers, so it builds on the host.
 */

typedef enum {
    GFX_WHITE = 0,
    GFX_BLACK = 1,
    GFX_INVERT = 2,
} gfx_color_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} gfx_rect_t;

typedef struct {
    uint8_t *buf;
    int16_t width;
    int16_t height;
    int16_t stride;  /* bytes per row: (width + 7) / 8 */
    gfx_rect_t clip; /* drawing is limited to this rectangle */
} gfx_fb_t;

size_t gfx_fb_size(int16_t width, int16_t height);
void gfx_fb_init(gfx_fb_t *fb, uint8_t *buf, int16_t width, int16_t height);
void gfx_clear(gfx_fb_t *fb, gfx_color_t color); /* whole buffer; ignores the clip */

gfx_rect_t gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b); /* empty result has w = h = 0 */
void gfx_set_clip(gfx_fb_t *fb, gfx_rect_t clip);           /* intersected with the framebuffer */
void gfx_reset_clip(gfx_fb_t *fb);

void gfx_pixel(gfx_fb_t *fb, int x, int y, gfx_color_t color);
bool gfx_get_pixel(const gfx_fb_t *fb, int x, int y); /* true = black; false outside the buffer */
void gfx_hline(gfx_fb_t *fb, int x, int y, int w, gfx_color_t color);
void gfx_vline(gfx_fb_t *fb, int x, int y, int h, gfx_color_t color);
void gfx_line(gfx_fb_t *fb, int x0, int y0, int x1, int y1, gfx_color_t color);
void gfx_rect(gfx_fb_t *fb, gfx_rect_t r, gfx_color_t color); /* outline, each pixel drawn once */
void gfx_fill_rect(gfx_fb_t *fb, gfx_rect_t r, gfx_color_t color);
void gfx_circle(gfx_fb_t *fb, int cx, int cy, int r, gfx_color_t color); /* outline, each pixel drawn once */
void gfx_fill_circle(gfx_fb_t *fb, int cx, int cy, int r, gfx_color_t color);

/* 1-bpp image in the glyph format: rows MSB first, each row padded to whole bytes, 1 = ink. */
typedef struct {
    const uint8_t *bits;
    uint8_t width;
    uint8_t height;
} gfx_bitmap_t;

/* Draws the bitmap's ink in `color` with its top-left corner at (x, y); other pixels are left
 * alone. For an opaque image, fill its rectangle first. */
void gfx_bitmap(gfx_fb_t *fb, int x, int y, const gfx_bitmap_t *bm, gfx_color_t color);

/* PBM P4 image of the framebuffer: header "P4\n<w> <h>\n" followed by the raster. */
size_t gfx_pbm_size(const gfx_fb_t *fb);
size_t gfx_pbm_encode(const gfx_fb_t *fb, uint8_t *out, size_t out_size); /* 0 if out_size is too small */


typedef enum {
    GFX_ALIGN_LEFT,
    GFX_ALIGN_CENTER,
    GFX_ALIGN_RIGHT,
} gfx_align_t;

/* Decodes one UTF-8 codepoint and advances *s. Returns 0 at the terminating NUL and U+FFFD for
 * malformed input; never reads past the NUL. */
uint32_t gfx_utf8_next(const char **s);

bool gfx_font_has_glyph(const gfx_font_t *font, uint32_t codepoint);
const gfx_glyph_t *gfx_font_glyph(const gfx_font_t *font, uint32_t codepoint); /* NULL if missing */
int gfx_text_width(const gfx_font_t *font, const char *utf8);
/* Draws one line with its baseline at `baseline`; returns the pen x after the text.
 * A codepoint missing from the font is drawn as a hollow box. */
int gfx_text(gfx_fb_t *fb, const gfx_font_t *font, int x, int baseline, const char *utf8, gfx_color_t color);
/* One line aligned in r, vertically centred on the font's line box, clipped to r. */
void gfx_text_in_rect(gfx_fb_t *fb, const gfx_font_t *font, gfx_rect_t r, gfx_align_t align, const char *utf8,
                      gfx_color_t color);
/* Copies `utf8` into `out`. If it is wider than max_width, cuts it at a codepoint and ends it
 * with an ellipsis ("…", or "..." if the font lacks it). Returns the width of the result. */
int gfx_text_ellipsize(const gfx_font_t *font, const char *utf8, int max_width, char *out, size_t out_size);

/* The text functions treat a NULL string as empty. */
```

```c
#include "gfx.h"

#include <stdbool.h>
#include <string.h>

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

size_t gfx_fb_size(int16_t width, int16_t height)
{
    return (size_t)((width + 7) / 8) * (size_t)height;
}

void gfx_fb_init(gfx_fb_t *fb, uint8_t *buf, int16_t width, int16_t height)
{
    fb->buf = buf;
    fb->width = width;
    fb->height = height;
    fb->stride = (int16_t)((width + 7) / 8);
    gfx_reset_clip(fb);
}

void gfx_clear(gfx_fb_t *fb, gfx_color_t color)
{
    size_t size = gfx_fb_size(fb->width, fb->height);
    if (color == GFX_INVERT) {
        for (size_t i = 0; i < size; i++) {
            fb->buf[i] ^= 0xFF;
        }
        return;
    }
    memset(fb->buf, color == GFX_BLACK ? 0xFF : 0x00, size);
}

gfx_rect_t gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b)
{
    int x0 = max_int(a.x, b.x);
    int y0 = max_int(a.y, b.y);
    int x1 = min_int(a.x + a.w, b.x + b.w);
    int y1 = min_int(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) {
        return (gfx_rect_t){ (int16_t)x0, (int16_t)y0, 0, 0 };
    }
    return (gfx_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0) };
}

void gfx_set_clip(gfx_fb_t *fb, gfx_rect_t clip)
{
    fb->clip = gfx_rect_intersect(clip, (gfx_rect_t){ 0, 0, fb->width, fb->height });
}

void gfx_reset_clip(gfx_fb_t *fb)
{
    fb->clip = (gfx_rect_t){ 0, 0, fb->width, fb->height };
}

void gfx_pixel(gfx_fb_t *fb, int x, int y, gfx_color_t color)
{
    const gfx_rect_t *c = &fb->clip;
    if (x < c->x || y < c->y || x >= c->x + c->w || y >= c->y + c->h) {
        return;
    }
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) {
        return; /* a clip set by hand, not through gfx_set_clip(), may reach outside the buffer */
    }
    uint8_t *byte = &fb->buf[y * fb->stride + (x >> 3)];
    uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    switch (color) {
    case GFX_BLACK:
        *byte |= mask;
        break;
    case GFX_WHITE:
        *byte &= (uint8_t)~mask;
        break;
    case GFX_INVERT:
        *byte ^= mask;
        break;
    }
}

bool gfx_get_pixel(const gfx_fb_t *fb, int x, int y)
{
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) {
        return false;
    }
    return (fb->buf[y * fb->stride + (x >> 3)] & (0x80u >> (x & 7))) != 0;
}

void gfx_hline(gfx_fb_t *fb, int x, int y, int w, gfx_color_t color)
{
    int x0 = max_int(x, fb->clip.x);
    int x1 = min_int(x + w, fb->clip.x + fb->clip.w);
    for (int i = x0; i < x1; i++) {
        gfx_pixel(fb, i, y, color);
    }
}

void gfx_vline(gfx_fb_t *fb, int x, int y, int h, gfx_color_t color)
{
    int y0 = max_int(y, fb->clip.y);
    int y1 = min_int(y + h, fb->clip.y + fb->clip.h);
    for (int i = y0; i < y1; i++) {
        gfx_pixel(fb, x, i, color);
    }
}

enum { OUT_LEFT = 1, OUT_RIGHT = 2, OUT_TOP = 4, OUT_BOTTOM = 8 };

static int outcode(const gfx_rect_t *c, int64_t x, int64_t y)
{
    int code = 0;
    if (x < c->x) {
        code |= OUT_LEFT;
    } else if (x >= c->x + c->w) {
        code |= OUT_RIGHT;
    }
    if (y < c->y) {
        code |= OUT_TOP;
    } else if (y >= c->y + c->h) {
        code |= OUT_BOTTOM;
    }
    return code;
}

/* Cohen-Sutherland: trims the segment to the clip so a huge line costs no more than its visible part. */
static bool clip_line(const gfx_rect_t *c, int64_t *x0, int64_t *y0, int64_t *x1, int64_t *y1)
{
    int64_t left = c->x, right = c->x + c->w - 1, top = c->y, bottom = c->y + c->h - 1;
    int code0 = outcode(c, *x0, *y0);
    int code1 = outcode(c, *x1, *y1);
    for (;;) {
        if ((code0 | code1) == 0) {
            return true;
        }
        if (code0 & code1) {
            return false;
        }
        int code = code0 ? code0 : code1;
        int64_t dx = *x1 - *x0, dy = *y1 - *y0, x, y;
        if (code & OUT_BOTTOM) {
            y = bottom;
            x = *x0 + dx * (bottom - *y0) / dy;
        } else if (code & OUT_TOP) {
            y = top;
            x = *x0 + dx * (top - *y0) / dy;
        } else if (code & OUT_RIGHT) {
            x = right;
            y = *y0 + dy * (right - *x0) / dx;
        } else {
            x = left;
            y = *y0 + dy * (left - *x0) / dx;
        }
        if (code == code0) {
            *x0 = x;
            *y0 = y;
            code0 = outcode(c, x, y);
        } else {
            *x1 = x;
            *y1 = y;
            code1 = outcode(c, x, y);
        }
    }
}

void gfx_line(gfx_fb_t *fb, int x0, int y0, int x1, int y1, gfx_color_t color)
{
    int64_t ax = x0, ay = y0, bx = x1, by = y1;
    if (fb->clip.w <= 0 || fb->clip.h <= 0 || !clip_line(&fb->clip, &ax, &ay, &bx, &by)) {
        return;
    }
    int64_t dx = bx > ax ? bx - ax : ax - bx;
    int64_t dy = by > ay ? ay - by : by - ay; /* negative */
    int sx = ax < bx ? 1 : -1;
    int sy = ay < by ? 1 : -1;
    int64_t err = dx + dy;
    for (;;) {
        gfx_pixel(fb, (int)ax, (int)ay, color);
        if (ax == bx && ay == by) {
            break;
        }
        int64_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            ax += sx;
        }
        if (e2 <= dx) {
            err += dx;
            ay += sy;
        }
    }
}

void gfx_rect(gfx_fb_t *fb, gfx_rect_t r, gfx_color_t color)
{
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    gfx_hline(fb, r.x, r.y, r.w, color);
    if (r.h > 1) {
        gfx_hline(fb, r.x, r.y + r.h - 1, r.w, color);
    }
    if (r.h > 2) {
        gfx_vline(fb, r.x, r.y + 1, r.h - 2, color);
        if (r.w > 1) {
            gfx_vline(fb, r.x + r.w - 1, r.y + 1, r.h - 2, color);
        }
    }
}

void gfx_fill_rect(gfx_fb_t *fb, gfx_rect_t r, gfx_color_t color)
{
    gfx_rect_t area = gfx_rect_intersect(r, fb->clip);
    for (int y = area.y; y < area.y + area.h; y++) {
        gfx_hline(fb, area.x, y, area.w, color);
    }
}

void gfx_circle(gfx_fb_t *fb, int cx, int cy, int r, gfx_color_t color)
{
    if (r < 0) {
        return;
    }
    if (r == 0) {
        gfx_pixel(fb, cx, cy, color);
        return;
    }
    /* Midpoint circle; the octant loop visits each pixel once except where octants meet, so
     * collect the points of one step and skip duplicates before drawing (INVERT must hit each once). */
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        int px[8] = { cx + x, cx + y, cx - y, cx - x, cx - x, cx - y, cx + y, cx + x };
        int py[8] = { cy + y, cy + x, cy + x, cy + y, cy - y, cy - x, cy - x, cy - y };
        for (int i = 0; i < 8; i++) {
            bool seen = false;
            for (int j = 0; j < i; j++) {
                seen |= px[j] == px[i] && py[j] == py[i];
            }
            if (!seen) {
                gfx_pixel(fb, px[i], py[i], color);
            }
        }
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_fill_circle(gfx_fb_t *fb, int cx, int cy, int r, gfx_color_t color)
{
    if (r < 0) {
        return;
    }
    for (int dy = -r; dy <= r; dy++) {
        int half = 0;
        while ((half + 1) * (half + 1) + dy * dy <= r * r + r) { /* r*r + r rounds the rim like the outline */
            half++;
        }
        gfx_hline(fb, cx - half, cy + dy, 2 * half + 1, color);
    }
}

void gfx_bitmap(gfx_fb_t *fb, int x, int y, const gfx_bitmap_t *bm, gfx_color_t color)
{
    if (bm == NULL || bm->bits == NULL) {
        return;
    }
    int row_bytes = (bm->width + 7) / 8;
    for (int r = 0; r < bm->height; r++) {
        for (int c = 0; c < bm->width; c++) {
            if (bm->bits[r * row_bytes + (c >> 3)] & (0x80u >> (c & 7))) {
                gfx_pixel(fb, x + c, y + r, color);
            }
        }
    }
}
```

```c
#include "gfx.h"

#include <string.h>

uint32_t gfx_utf8_next(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = p[0];
    int extra = 0;    /* initialised for GCC's -Wmaybe-uninitialized; every path below sets them */
    uint32_t min = 0;

    if (c == 0) {
        return 0;
    }
    if (c < 0x80) {
        *s += 1;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        extra = 1;
        c &= 0x1F;
        min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        c &= 0x0F;
        min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        c &= 0x07;
        min = 0x10000;
    } else {
        *s += 1;
        return 0xFFFD;
    }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) { /* also stops at the NUL of a truncated sequence */
            *s += i;
            return 0xFFFD;
        }
        c = (c << 6) | (p[i] & 0x3Fu);
    }
    *s += extra + 1;
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        return 0xFFFD;
    }
    return c;
}

static const gfx_glyph_t *find_glyph(const gfx_font_t *font, uint32_t codepoint)
{
    int lo = 0;
    int hi = (int)font->glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t c = font->glyphs[mid].codepoint;
        if (c == codepoint) {
            return &font->glyphs[mid];
        }
        if (c < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

static void missing_box(const gfx_font_t *font, int *w, int *h)
{
    *w = font->line_height / 2 < 2 ? 2 : font->line_height / 2;
    *h = font->ascent * 2 / 3 < 2 ? 2 : font->ascent * 2 / 3;
}

bool gfx_font_has_glyph(const gfx_font_t *font, uint32_t codepoint)
{
    return find_glyph(font, codepoint) != NULL;
}

const gfx_glyph_t *gfx_font_glyph(const gfx_font_t *font, uint32_t codepoint)
{
    return find_glyph(font, codepoint);
}

int gfx_text_width(const gfx_font_t *font, const char *utf8)
{
    if (utf8 == NULL) {
        return 0;
    }
    int width = 0;
    uint32_t cp;
    while ((cp = gfx_utf8_next(&utf8)) != 0) {
        const gfx_glyph_t *g = find_glyph(font, cp);
        if (g != NULL) {
            width += g->advance;
        } else {
            int w, h;
            missing_box(font, &w, &h);
            width += w + 2;
        }
    }
    return width;
}

static void draw_glyph(gfx_fb_t *fb, const gfx_font_t *font, const gfx_glyph_t *g, int x, int baseline,
                       gfx_color_t color)
{
    int row_bytes = (g->width + 7) / 8;
    const uint8_t *rows = font->bitmap + g->offset;
    for (int r = 0; r < g->height; r++) {
        for (int c = 0; c < g->width; c++) {
            if (rows[r * row_bytes + (c >> 3)] & (0x80u >> (c & 7))) {
                gfx_pixel(fb, x + g->x_offset + c, baseline + g->y_offset + r, color);
            }
        }
    }
}

/* The fallback box, drawn with int coordinates: a gfx_rect_t would wrap at x > 32767. */
static void draw_missing_box(gfx_fb_t *fb, int x, int baseline, int w, int h, gfx_color_t color)
{
    int top = baseline - h;
    gfx_hline(fb, x, top, w, color);
    gfx_hline(fb, x, baseline - 1, w, color);
    gfx_vline(fb, x, top + 1, h - 2, color);
    gfx_vline(fb, x + w - 1, top + 1, h - 2, color);
}

int gfx_text(gfx_fb_t *fb, const gfx_font_t *font, int x, int baseline, const char *utf8, gfx_color_t color)
{
    if (utf8 == NULL) {
        return x;
    }
    uint32_t cp;
    while ((cp = gfx_utf8_next(&utf8)) != 0) {
        const gfx_glyph_t *g = find_glyph(font, cp);
        if (g != NULL) {
            draw_glyph(fb, font, g, x, baseline, color);
            x += g->advance;
        } else {
            int w, h;
            missing_box(font, &w, &h);
            draw_missing_box(fb, x + 1, baseline, w, h, color);
            x += w + 2;
        }
    }
    return x;
}

void gfx_text_in_rect(gfx_fb_t *fb, const gfx_font_t *font, gfx_rect_t r, gfx_align_t align, const char *utf8,
                      gfx_color_t color)
{
    if (utf8 == NULL) {
        return;
    }
    gfx_rect_t saved = fb->clip;
    fb->clip = gfx_rect_intersect(saved, r);

    int x = r.x;
    if (align != GFX_ALIGN_LEFT) {
        int width = gfx_text_width(font, utf8);
        x = align == GFX_ALIGN_CENTER ? r.x + (r.w - width) / 2 : r.x + r.w - width;
    }
    int baseline = r.y + (r.h - font->line_height) / 2 + font->ascent;
    gfx_text(fb, font, x, baseline, utf8, color);

    fb->clip = saved;
}

int gfx_text_ellipsize(const gfx_font_t *font, const char *utf8, int max_width, char *out, size_t out_size)
{
    if (out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (utf8 == NULL) {
        return 0;
    }
    const char *dots = gfx_font_has_glyph(font, 0x2026) ? "\xE2\x80\xA6" : "...";
    int full = gfx_text_width(font, utf8);
    int dots_w = gfx_text_width(font, dots);
    int budget = full <= max_width ? full : max_width - dots_w;
    const char *s = utf8, *end = utf8;
    int width = 0;
    for (;;) {
        const char *before = s;
        uint32_t cp = gfx_utf8_next(&s);
        if (cp == 0) {
            break;
        }
        char one[5] = { 0 };
        memcpy(one, before, (size_t)(s - before));
        int w = gfx_text_width(font, one);
        if (width + w > budget || (size_t)(s - utf8) + (full <= max_width ? 0 : strlen(dots)) >= out_size) {
            break;
        }
        width += w;
        end = s;
    }
    size_t n = (size_t)(end - utf8);
    memcpy(out, utf8, n);
    out[n] = '\0';
    if (*end != '\0') {
        size_t room = out_size - 1 - n;
        size_t d = strlen(dots);
        if (d <= room) {
            memcpy(out + n, dots, d + 1);
            width += dots_w;
        }
    }
    return width;
}
```

- [ ] **Step 4: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_gfx && ./build-host/test_gfx_text && ctest --test-dir build-host --output-on-failure`
Expected: `14 Tests 0 Failures` and `12 Tests 0 Failures`; the goldens are unchanged, so the suite passes.

- [ ] **Step 5: Commit.**

```bash
git add components/gfx/include/gfx.h components/gfx/gfx.c components/gfx/gfx_text.c test/host/test_gfx.c test/host/test_gfx_text.c
git commit -m "feat(gfx): add bitmaps, circles and ellipsis, and harden text and lines"
```

---

### Task 3: Widget fonts and icons

**Files:**
- Modify: `tools/fontgen.py`, `tools/tests/test_fontgen.py`, `tools/gen_fonts.sh`, `components/gfx/include/gfx_fonts.h`, `components/gfx/CMakeLists.txt`, `components/gfx/fonts/*` (regenerated), `test/host/CMakeLists.txt`, `test/host/golden/test_pattern.pbm`, `THIRD_PARTY.md`, `AGENTS.md` §6
- Create: `tools/imggen.py`, `tools/tests/test_imggen.py`, `tools/gen_icons.sh`, `assets/icons/MaterialIcons-Regular.ttf`, `assets/icons/MaterialIcons-Regular.codepoints`, `assets/icons/LICENSE-MaterialIcons.txt`, `assets/icons/icons.txt`, `components/gfx/icons/gfx_icons.c` and `components/gfx/include/gfx_icons.h` (generated)

**Interfaces:**
- Consumes: `gfx_bitmap_t` (Task 2).
- Produces:
  - Fonts `gfx_font_sans_bold_16`, `gfx_font_num_cb_48`, `gfx_font_num_cb_72` and `gfx_font_num_cb_110`, next to the existing ones.
  - Icons `gfx_icon_<name>_<size>`: thermometer 16/24/48, drop 16/24/48, dew 24/48, bolt 16/24, stale 16/24, clock 24/48, calendar 24/48, person 24/48, celebration 24/48, cloud 24/48 (Task 8).
- fontgen fixes (M1 review minors): Pillow's BASIC layout engine is pinned; blank glyph rows and columns are trimmed (about 12.7 KB of bitmaps saved); ascent and line height grow to fit every glyph's ink, which fixes the sans_12 ĺ clipping.

- [ ] **Step 1: Write the failing tool tests.** `test_fontgen.py` gains `TrimGlyphTest` and `LineMetricsTest`; `test_imggen.py` is new.

```python
import unittest

import fontgen


class ParseCharsetTest(unittest.TestCase):
    def test_ascii_is_the_printable_range(self):
        cps = fontgen.parse_charset("ascii")
        self.assertEqual(cps[0], 0x20)
        self.assertEqual(cps[-1], 0x7E)
        self.assertEqual(len(cps), 95)

    def test_text_covers_czech_and_symbols(self):
        cps = set(fontgen.parse_charset("text"))
        for ch in "ŽžŮůĚěŘřŤťĎďŇň°µ²€–…→":
            self.assertIn(ord(ch), cps, ch)

    def test_ranges_and_single_codepoints_combine_sorted_without_duplicates(self):
        self.assertEqual(fontgen.parse_charset("0x41-0x43,0x20AC,0x41"), [0x41, 0x42, 0x43, 0x20AC])


class PackRowsTest(unittest.TestCase):
    def test_rows_are_msb_first_and_padded_to_bytes(self):
        self.assertEqual(fontgen.pack_rows([[1, 0, 1], [0, 1, 0]]), bytes([0b10100000, 0b01000000]))

    def test_rows_wider_than_a_byte_use_two_bytes(self):
        self.assertEqual(fontgen.pack_rows([[1] * 9]), bytes([0xFF, 0x80]))


class TrimGlyphTest(unittest.TestCase):
    def test_blank_rows_and_columns_go_and_the_offsets_follow(self):
        glyph = dict(cp=0x41, width=4, height=4, x=1, y=-4, advance=5,
                     rows=[[0, 0, 0, 0], [0, 1, 1, 0], [0, 1, 0, 0], [0, 0, 0, 0]])
        trimmed = fontgen.trim_glyph(glyph)
        self.assertEqual((trimmed["width"], trimmed["height"], trimmed["x"], trimmed["y"]), (2, 2, 2, -3))
        self.assertEqual(trimmed["rows"], [[1, 1], [1, 0]])
        self.assertEqual(trimmed["advance"], 5)

    def test_a_glyph_without_ink_keeps_only_its_advance(self):
        glyph = dict(cp=0x20, width=3, height=2, x=0, y=-2, advance=4, rows=[[0, 0, 0], [0, 0, 0]])
        self.assertEqual(fontgen.trim_glyph(glyph), dict(cp=0x20, width=0, height=0, x=0, y=0, advance=4, rows=[]))


class LineMetricsTest(unittest.TestCase):
    def test_grows_to_fit_ink_above_the_ascent_and_below_the_descent(self):
        glyphs = [dict(width=2, height=13, y=-13), dict(width=2, height=4, y=1), dict(width=0, height=0, y=0)]
        self.assertEqual(fontgen.line_metrics(glyphs, 12, 3), (13, 18))
        self.assertEqual(fontgen.line_metrics([], 12, 3), (12, 15))


class EmitCTest(unittest.TestCase):
    def test_emits_glyph_table_with_offsets(self):
        glyphs = [
            dict(cp=0x20, width=0, height=0, x=0, y=0, advance=5, rows=[]),
            dict(cp=0x41, width=3, height=2, x=1, y=-2, advance=6, rows=[[1, 1, 1], [1, 0, 1]]),
        ]
        text = fontgen.emit_c("tiny", "Tiny.ttf", 8, glyphs, ascent=7, line_height=9)
        self.assertIn("{ 0x0020, 0, 0, 0, 0, 0, 5 },", text)
        self.assertIn("{ 0x0041, 0, 3, 2, 1, -2, 6 },", text)
        self.assertIn("0xE0, 0xA0,", text)
        self.assertIn("const gfx_font_t gfx_font_tiny = { s_bitmap, s_glyphs, 2, 7, 9 };", text)

    def test_names_the_font_licence_in_the_header(self):
        text = fontgen.emit_c("tiny", "Tiny.ttf", 8, [], ascent=7, line_height=9,
                              licence="assets/fonts/LICENSE-Tiny.txt")
        self.assertIn("Font licence: assets/fonts/LICENSE-Tiny.txt", text.splitlines()[1])

    def test_rejects_glyphs_that_do_not_fit_the_c_types(self):
        glyph = dict(cp=0x41, width=300, height=1, x=0, y=0, advance=1, rows=[[1] * 300])
        with self.assertRaises(ValueError):
            fontgen.emit_c("big", "Big.ttf", 400, [glyph], ascent=1, line_height=1)


if __name__ == "__main__":
    unittest.main()
```

```python
import unittest

import imggen


class ParseManifestTest(unittest.TestCase):
    def test_reads_names_and_sizes_and_skips_comments(self):
        text = "# header\nthermometer device_thermostat 16 24  # trailing\n\ndrop water_drop 48\n"
        self.assertEqual(imggen.parse_manifest(text),
                         [("thermometer", "device_thermostat", [16, 24]), ("drop", "water_drop", [48])])

    def test_rejects_a_line_without_sizes_or_with_a_bad_c_name(self):
        with self.assertRaises(ValueError):
            imggen.parse_manifest("thermometer device_thermostat\n")
        with self.assertRaises(ValueError):
            imggen.parse_manifest("2x water_drop 16\n")


class ParseCodepointsTest(unittest.TestCase):
    def test_maps_names_to_codepoints(self):
        self.assertEqual(imggen.parse_codepoints("bolt ea0b\nwater_drop e798\n"), {"bolt": 0xEA0B, "water_drop": 0xE798})


class EmitTest(unittest.TestCase):
    def test_c_defines_one_square_bitmap_per_icon_and_size(self):
        rows = [[1, 0, 0, 0, 0, 0, 0, 0, 1], [0] * 9]
        text = imggen.emit_c([("x_9", 9, rows)], "Icons.ttf", licence="assets/icons/LICENSE.txt")
        self.assertIn("Icon licence: assets/icons/LICENSE.txt", text.splitlines()[1])
        self.assertIn("0x80, 0x80, 0x00, 0x00,", text)
        self.assertIn("const gfx_bitmap_t gfx_icon_x_9 = { s_x_9, 9, 9 };", text)

    def test_header_declares_every_icon(self):
        text = imggen.emit_h([("a_16", 16, []), ("b_24", 24, [])], "Icons.ttf")
        self.assertIn("extern const gfx_bitmap_t gfx_icon_a_16;", text)
        self.assertIn("extern const gfx_bitmap_t gfx_icon_b_24;", text)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Watch them fail.**

Run: `cd tools && python3 -m unittest tests.test_fontgen tests.test_imggen; cd ..`
Expected: FAIL: `fontgen` has no `trim_glyph`, and `No module named 'imggen'`.

- [ ] **Step 3: Implement the generators.**

```python
#!/usr/bin/env python3
"""Render a TrueType font into a 1-bpp bitmap font for components/gfx (spec §4.4).

Run it through tools/gen_fonts.sh, which supplies the pinned Pillow and fontTools via uv:
  uv run --python 3.13 --with-requirements tools/requirements.txt \
      tools/fontgen.py --ttf assets/fonts/DejaVuSans.ttf --size 16 --charset text --name sans_16
Writes components/gfx/fonts/gfx_font_<name>.c, which defines `const gfx_font_t gfx_font_<name>`.
Glyphs are rendered with FreeType's monochrome hinting, the mode Pillow uses for "1" images.
"""
import argparse
import pathlib
import sys

CHARSETS = {
    "ascii": [(0x20, 0x7E)],
    # ASCII, Latin-1, Latin Extended-A, dashes, ellipsis, euro, arrows
    "text": [(0x20, 0x7E), (0xA0, 0xFF), (0x100, 0x17F), (0x2013, 0x2014), (0x2026, 0x2026),
             (0x20AC, 0x20AC), (0x2190, 0x2193)],
    # large numerals: space % + , - . / 0-9 : ° minus
    "digits": [(0x20, 0x20), (0x25, 0x25), (0x2B, 0x3A), (0xB0, 0xB0), (0x2212, 0x2212)],
}


def parse_charset(spec):
    """'text' or 'ascii,0x20AC,0x2190-0x2193' -> sorted list of codepoints."""
    cps = set()
    for part in (p.strip() for p in spec.split(",")):
        if part in CHARSETS:
            for lo, hi in CHARSETS[part]:
                cps.update(range(lo, hi + 1))
        elif "-" in part:
            lo, hi = (int(v, 0) for v in part.split("-", 1))
            cps.update(range(lo, hi + 1))
        else:
            cps.add(int(part, 0))
    return sorted(cps)


def pack_rows(rows):
    """Rows of 0/1 pixels -> bytes, MSB first, each row padded to whole bytes."""
    out = bytearray()
    for row in rows:
        for start in range(0, len(row), 8):
            byte = 0
            for i, bit in enumerate(row[start:start + 8]):
                if bit:
                    byte |= 0x80 >> i
            out.append(byte)
    return bytes(out)


def render_glyph(font, cp):
    """One glyph as FreeType renders it in monochrome, positioned relative to the pen at the baseline."""
    from PIL import Image, ImageDraw

    ch = chr(cp)
    advance = round(font.getlength(ch, mode="1"))
    left, top, right, bottom = font.getbbox(ch, mode="1", anchor="ls")
    width, height = right - left, bottom - top
    if width <= 0 or height <= 0:
        return dict(cp=cp, width=0, height=0, x=0, y=0, advance=advance, rows=[])
    img = Image.new("1", (width, height), 0)
    draw = ImageDraw.Draw(img)
    draw.fontmode = "1"
    draw.text((-left, -top), ch, font=font, fill=1, anchor="ls")
    px = img.load()
    rows = [[1 if px[x, y] else 0 for x in range(width)] for y in range(height)]
    return trim_glyph(dict(cp=cp, width=width, height=height, x=left, y=top, advance=advance, rows=rows))


def trim_glyph(glyph):
    """Drops blank rows and columns around the ink; the offsets move so nothing shifts on screen."""
    rows = glyph["rows"]
    ink_rows = [i for i, row in enumerate(rows) if any(row)]
    if not ink_rows:
        return dict(glyph, width=0, height=0, x=0, y=0, rows=[])
    ink_cols = [c for c in range(glyph["width"]) if any(row[c] for row in rows)]
    top, bottom, left, right = ink_rows[0], ink_rows[-1], ink_cols[0], ink_cols[-1]
    return dict(glyph, width=right - left + 1, height=bottom - top + 1, x=glyph["x"] + left, y=glyph["y"] + top,
                rows=[row[left:right + 1] for row in rows[top:bottom + 1]])


def line_metrics(glyphs, ascent, descent):
    """The font's ascent and line height, grown so that every glyph's ink fits the line box."""
    for glyph in glyphs:
        if glyph["height"]:
            ascent = max(ascent, -glyph["y"])
            descent = max(descent, glyph["y"] + glyph["height"])
    return ascent, ascent + descent


def _check(glyph):
    ok = (0 <= glyph["width"] <= 255 and 0 <= glyph["height"] <= 255 and -128 <= glyph["x"] <= 127
          and -128 <= glyph["y"] <= 127 and 0 <= glyph["advance"] <= 255)
    if not ok:
        raise ValueError(f"glyph U+{glyph['cp']:04X} does not fit gfx_glyph_t: {glyph}")


def emit_c(name, source, size, glyphs, ascent, line_height, licence=None):
    data = bytearray()
    offsets = []
    for glyph in glyphs:
        _check(glyph)
        offsets.append(len(data))
        data += pack_rows(glyph["rows"])
    lines = [
        f"/* Generated by tools/fontgen.py from {source} at {size} px. Do not edit; run tools/gen_fonts.sh. */",
        *([f"/* Font licence: {licence} (it also covers these bitmaps). */"] if licence else []),
        '#include "gfx_font.h"',
        "",
        "static const uint8_t s_bitmap[] = {",
    ]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    if not data:
        lines.append("    0x00,")
    lines += ["};", "", "static const gfx_glyph_t s_glyphs[] = {"]
    for glyph, offset in zip(glyphs, offsets):
        lines.append(f"    {{ 0x{glyph['cp']:04X}, {offset}, {glyph['width']}, {glyph['height']}, "
                     f"{glyph['x']}, {glyph['y']}, {glyph['advance']} }},")
    lines += [
        "};",
        "",
        f"const gfx_font_t gfx_font_{name} = {{ s_bitmap, s_glyphs, {len(glyphs)}, {ascent}, {line_height} }};",
        "",
    ]
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ttf", required=True, help="TrueType/OpenType font file")
    parser.add_argument("--size", required=True, type=int, help="pixel size (em)")
    parser.add_argument("--charset", default="text", help="preset name and/or codepoints, comma separated")
    parser.add_argument("--name", required=True, help="C name suffix: gfx_font_<name>")
    parser.add_argument("--out", help="output .c (default: components/gfx/fonts/gfx_font_<name>.c)")
    parser.add_argument("--licence", help="licence file of the font, named in the generated header")
    args = parser.parse_args(argv)

    from fontTools.ttLib import TTFont
    from PIL import ImageFont

    cmap = TTFont(args.ttf).getBestCmap()
    font = ImageFont.truetype(args.ttf, args.size, layout_engine=ImageFont.Layout.BASIC)  # same with or without raqm
    wanted = parse_charset(args.charset)
    glyphs = [render_glyph(font, cp) for cp in wanted if cp in cmap]
    missing = [f"U+{cp:04X}" for cp in wanted if cp not in cmap]
    ascent, line_height = line_metrics(glyphs, *font.getmetrics())
    out = pathlib.Path(args.out or f"components/gfx/fonts/gfx_font_{args.name}.c")
    out.write_text(emit_c(args.name, pathlib.Path(args.ttf).name, args.size, glyphs, ascent, line_height,
                          args.licence),
                   encoding="utf-8")
    print(f"{out}: {len(glyphs)} glyphs, ascent {ascent}, line {line_height}"
          + (f", not in font: {' '.join(missing)}" if missing else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

```python
#!/usr/bin/env python3
"""Render icons from an icon font into 1-bpp C bitmaps for components/gfx (spec §4.5).

Run it through tools/gen_icons.sh, which supplies the pinned Pillow via uv:
  uv run --python 3.13 --with-requirements tools/requirements.txt tools/imggen.py \\
      --ttf assets/icons/MaterialIcons-Regular.ttf --codepoints assets/icons/MaterialIcons-Regular.codepoints \\
      --manifest assets/icons/icons.txt --licence assets/icons/LICENSE-MaterialIcons.txt
Writes components/gfx/icons/gfx_icons.c and components/gfx/include/gfx_icons.h. Each icon is a
square bitmap of its size (the font's em box), so icons of one size line up. Glyphs are rendered
with FreeType's monochrome hinting, like tools/fontgen.py.
"""
import argparse
import pathlib
import sys


def parse_manifest(text):
    """'c_name source_name size...' lines -> [(c_name, source_name, [sizes])]; '#' starts a comment."""
    icons = []
    for number, line in enumerate(text.splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 3 or not parts[0].isidentifier():
            raise ValueError(f"manifest line {number}: expected 'c_name source_name size...': {line!r}")
        icons.append((parts[0], parts[1], [int(s) for s in parts[2:]]))
    return icons


def parse_codepoints(text):
    """Material Icons' 'name hex' lines -> {name: codepoint}."""
    return {name: int(hexcp, 16) for name, hexcp in (line.split() for line in text.splitlines() if line.strip())}


def pack_rows(rows):
    """Rows of 0/1 pixels -> bytes, MSB first, each row padded to whole bytes."""
    out = bytearray()
    for row in rows:
        for start in range(0, len(row), 8):
            byte = 0
            for i, bit in enumerate(row[start:start + 8]):
                if bit:
                    byte |= 0x80 >> i
            out.append(byte)
    return bytes(out)


def render_icon(font, codepoint, size):
    """size x size rows of 0/1, the glyph drawn at the em box's top-left corner."""
    from PIL import Image, ImageDraw

    img = Image.new("1", (size, size), 0)
    draw = ImageDraw.Draw(img)
    draw.fontmode = "1"
    draw.text((0, 0), chr(codepoint), font=font, fill=1)
    px = img.load()
    return [[1 if px[x, y] else 0 for x in range(size)] for y in range(size)]


def emit_c(icons, source, licence=None):
    """icons: [(symbol, size, rows)] -> the .c file text."""
    lines = [
        f"/* Generated by tools/imggen.py from {source}. Do not edit; run tools/gen_icons.sh. */",
        *([f"/* Icon licence: {licence} (it also covers these bitmaps). */"] if licence else []),
        '#include "gfx_icons.h"',
        "",
    ]
    for symbol, size, rows in icons:
        data = pack_rows(rows)
        lines.append(f"static const uint8_t s_{symbol}[] = {{")
        for i in range(0, len(data), 16):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
        lines += ["};", f"const gfx_bitmap_t gfx_icon_{symbol} = {{ s_{symbol}, {size}, {size} }};", ""]
    return "\n".join(lines)


def emit_h(icons, source):
    lines = [
        "#pragma once",
        "",
        '#include "gfx.h"',
        "",
        f"/* Icons generated by tools/imggen.py from {source} (see THIRD_PARTY.md). */",
    ]
    lines += [f"extern const gfx_bitmap_t gfx_icon_{symbol};" for symbol, _size, _rows in icons]
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ttf", required=True, help="icon font")
    parser.add_argument("--codepoints", required=True, help="the font's 'name hex' codepoint list")
    parser.add_argument("--manifest", required=True, help="icons to render: 'c_name source_name size...'")
    parser.add_argument("--licence", help="licence file of the icons, named in the generated source")
    parser.add_argument("--out-c", default="components/gfx/icons/gfx_icons.c")
    parser.add_argument("--out-h", default="components/gfx/include/gfx_icons.h")
    args = parser.parse_args(argv)

    from PIL import ImageFont

    codepoints = parse_codepoints(pathlib.Path(args.codepoints).read_text())
    rendered = []
    for c_name, source_name, sizes in parse_manifest(pathlib.Path(args.manifest).read_text()):
        if source_name not in codepoints:
            raise SystemExit(f"{source_name}: not in {args.codepoints}")
        for size in sizes:
            font = ImageFont.truetype(args.ttf, size, layout_engine=ImageFont.Layout.BASIC)
            rendered.append((f"{c_name}_{size}", size, render_icon(font, codepoints[source_name], size)))
    source = pathlib.Path(args.ttf).name
    pathlib.Path(args.out_c).write_text(emit_c(rendered, source, args.licence), encoding="utf-8")
    pathlib.Path(args.out_h).write_text(emit_h(rendered, source), encoding="utf-8")
    print(f"{args.out_c}: {len(rendered)} icons")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

```bash
#!/usr/bin/env bash
# Regenerate components/gfx/icons from the icon font in assets/icons (spec §4.5). Needs uv; the
# pinned Pillow comes from tools/requirements.txt.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

uv run --quiet --python 3.13 --with-requirements tools/requirements.txt tools/imggen.py \
    --ttf assets/icons/MaterialIcons-Regular.ttf --codepoints assets/icons/MaterialIcons-Regular.codepoints \
    --manifest assets/icons/icons.txt --licence assets/icons/LICENSE-MaterialIcons.txt
```

```bash
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
fontgen --ttf assets/fonts/DejaVuSans-Bold.ttf --size 28 --charset text --name sans_bold_28 --licence assets/fonts/LICENSE-DejaVu.txt
fontgen --ttf assets/fonts/DejaVuSans-Bold.ttf --size 16 --charset text --name sans_bold_16 --licence assets/fonts/LICENSE-DejaVu.txt
fontgen --ttf assets/fonts/DejaVuSansCondensed-Bold.ttf --size 48 --charset digits --name num_cb_48 --licence assets/fonts/LICENSE-DejaVu.txt
fontgen --ttf assets/fonts/DejaVuSansCondensed-Bold.ttf --size 72 --charset digits --name num_cb_72 --licence assets/fonts/LICENSE-DejaVu.txt
fontgen --ttf assets/fonts/DejaVuSansCondensed-Bold.ttf --size 110 --charset digits --name num_cb_110 --licence assets/fonts/LICENSE-DejaVu.txt
fontgen --ttf assets/fonts/DejaVuSansCondensed-Bold.ttf --size 130 --charset digits --name num_cb_130 --licence assets/fonts/LICENSE-DejaVu.txt
```

Run: `chmod +x tools/gen_icons.sh && cd tools && python3 -m unittest tests.test_fontgen tests.test_imggen; cd ..`
Expected: OK.

- [ ] **Step 4: Add the icon font, pinned.** Material Icons from `google/material-design-icons` at commit `f7bd4f25f3764883717c09a1fd867f560c9a9581` (the last change to the font, 2022-09-19), Apache-2.0:

```bash
mkdir -p assets/icons
base=https://raw.githubusercontent.com/google/material-design-icons/f7bd4f25f3764883717c09a1fd867f560c9a9581
curl -sL -o assets/icons/MaterialIcons-Regular.ttf $base/font/MaterialIcons-Regular.ttf
curl -sL -o assets/icons/MaterialIcons-Regular.codepoints $base/font/MaterialIcons-Regular.codepoints
curl -sL -o assets/icons/LICENSE-MaterialIcons.txt $base/LICENSE
shasum -a 256 assets/icons/*
```

Expected hashes:

```text
ef149f08bdd2ff09a4e2c8573476b7b0f3fbb15b623954ade59899e7175bedda  assets/icons/MaterialIcons-Regular.ttf
530f25bf7b2d71c8e1da9476d53f9a9bb6b7e187bff69bb7128bb679b8194894  assets/icons/MaterialIcons-Regular.codepoints
58d1e17ffe5109a7ae296caafcadfdbe6a7d176f0bc4ab01e12a689b0499d8bd  assets/icons/LICENSE-MaterialIcons.txt
```

The manifest:

```text
# Icons rendered into components/gfx/icons by tools/gen_icons.sh (spec §4.5).
# C name       Material Icons name   sizes (px)
thermometer    device_thermostat     16 24 48
drop           water_drop            16 24 48
dew            dew_point             24 48
bolt           bolt                  16 24
stale          sync_problem          16 24
clock          schedule              24 48
calendar       calendar_today        24 48
person         person                24 48
celebration    celebration           24 48
cloud          cloud                 24 48
```

- [ ] **Step 5: Generate the fonts and icons.**

Run: `tools/gen_fonts.sh && tools/gen_icons.sh`
Expected:
- the fonts print their glyph counts, including `components/gfx/fonts/gfx_font_num_cb_110.c: 20 glyphs, ascent 103, line 129` and `components/gfx/fonts/gfx_font_sans_12.c: 327 glyphs, ascent 13, line 16`;
- the icons print `components/gfx/icons/gfx_icons.c: 22 icons`.

Declare the new fonts and list the new sources:

```c
#pragma once

#include "gfx_font.h"

/* Bitmap fonts rendered from DejaVu Sans 2.37 by tools/gen_fonts.sh (see THIRD_PARTY.md). */
extern const gfx_font_t gfx_font_sans_12;
extern const gfx_font_t gfx_font_sans_16;
extern const gfx_font_t gfx_font_sans_20;
extern const gfx_font_t gfx_font_sans_bold_16;
extern const gfx_font_t gfx_font_sans_bold_20;
extern const gfx_font_t gfx_font_sans_bold_28;
/* DejaVu Sans Condensed Bold; digits, space, % + , - . / : ° and the minus sign only */
extern const gfx_font_t gfx_font_num_cb_48;
extern const gfx_font_t gfx_font_num_cb_72;
extern const gfx_font_t gfx_font_num_cb_110;
extern const gfx_font_t gfx_font_num_cb_130;
```

```cmake
# 1-bpp drawing, fonts and icons (spec §4.3–4.5). Pure C: also built on the host by test/host.
# Sources are listed, not globbed: ESP-IDF globs SRC_DIRS only at configure time.
idf_component_register(SRCS "gfx.c" "gfx_pbm.c" "gfx_test_pattern.c" "gfx_text.c"
                            "fonts/gfx_font_sans_12.c" "fonts/gfx_font_sans_16.c"
                            "fonts/gfx_font_sans_20.c" "fonts/gfx_font_sans_bold_20.c"
                            "fonts/gfx_font_sans_bold_16.c" "fonts/gfx_font_sans_bold_28.c"
                            "fonts/gfx_font_num_cb_48.c" "fonts/gfx_font_num_cb_72.c" "fonts/gfx_font_num_cb_110.c"
                            "fonts/gfx_font_num_cb_130.c"
                            "icons/gfx_icons.c"
                       INCLUDE_DIRS "include")
```

In `test/host/CMakeLists.txt`, the gfx glob also takes the icons:

```cmake
# gfx: every source in the component, generated fonts and icons included.
file(GLOB GFX_SOURCES CONFIGURE_DEPENDS
     ${REPO_ROOT}/components/gfx/*.c ${REPO_ROOT}/components/gfx/fonts/*.c ${REPO_ROOT}/components/gfx/icons/*.c)
```

- [ ] **Step 6: Check the goldens.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `test_test_pattern_golden` and `test_ui_clock_golden` FAIL, because sans_12's line box grew by one pixel.

Confirm the test pattern changed only by that shift:

```bash
./build-host/render_test_pattern build-host/tp_new.pbm
python3 - <<'PY'
import pathlib
def raster(p):
    b = pathlib.Path(p).read_bytes(); return b[b.index(b"\n", b.index(b"\n") + 1) + 1:]
a, b = raster("test/host/golden/test_pattern.pbm"), raster("build-host/tp_new.pbm")
rows = sorted({i // 50 for i in range(len(a)) if a[i] != b[i]})
print(rows[0], rows[-1], all(b[(r + 1) * 50:(r + 2) * 50] == a[r * 50:(r + 1) * 50] for r in range(88, 104)))
PY
```

Expected: `91 101 True`: only the glyph-sample line moved, down one row.

Regenerate both:

```bash
./build-host/render_test_pattern test/host/golden/test_pattern.pbm
for n in valid invalid cold; do ./build-host/render_clock $n test/host/golden/clock_$n.pbm; done
shasum -a 256 test/host/golden/test_pattern.pbm
ctest --test-dir build-host --output-on-failure
```

Expected:
- `6a4ab7a22a974cb71001aaaf79c500bfba89006896c6d7d42d689bd04e99dfbc`, the test pattern the spike rendered with the same fonts.
- The suite passes.

- [ ] **Step 7: Credit the icons and document the command.** `THIRD_PARTY.md` gains a row:

```markdown
| Material Icons (Regular), commit `f7bd4f25f3764883717c09a1fd867f560c9a9581` | https://github.com/google/material-design-icons | Apache-2.0 (`assets/icons/LICENSE-MaterialIcons.txt`) | `assets/icons/`; bitmaps rendered into `components/gfx/icons/` | Rasterised by `tools/gen_icons.sh` from the names in `assets/icons/icons.txt`; font and codepoint list unmodified |
```

In `AGENTS.md` §6, after the `gen_fonts.sh` line:

```sh
tools/gen_icons.sh                          # regenerate components/gfx/icons from assets/icons (needs uv)
```

- [ ] **Step 8: Commit.** Two commits:

```bash
git add tools/fontgen.py tools/imggen.py tools/gen_icons.sh tools/gen_fonts.sh tools/tests/test_fontgen.py tools/tests/test_imggen.py
git commit -m "feat(tools): trim glyphs, fit the line box to the ink, and add the icon generator"
git add assets/icons components/gfx test/host/CMakeLists.txt test/host/golden THIRD_PARTY.md AGENTS.md
git commit -m "feat(gfx): add widget fonts and Material icons"
```


---

### Task 4: Language packs (`locale`)

**Files:**
- Create: `components/locale/CMakeLists.txt`, `components/locale/include/lang.h`, `components/locale/lang.c`, `components/locale/lang_en.c`, `test/host/test_lang.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `lang_t` holds the strings indexed by `lang_str_t`, the weekday, month and moon-phase names (long and short), the decimal separator, the first weekday, and the `format_date`, `name_day` and `holiday` hooks (M3b adds `cs`).
  - `const lang_t *lang_get(const char *code)`: English for NULL or unknown.
  - `lang_str()`; `lang_format_date(lang, tm, LANG_DATE_LONG|MEDIUM|SHORT, out, size)`.
  - `lang_format_time(hour, minute, second, h24, seconds, out, size, &suffix)`.
  - `int lang_format_decimal(lang, value, decimals, out, size)`: the sign survives below 1, the M2 review's −0.50 finding.
- The header is `lang.h`, not `locale.h`, and the prefix is `lang_`: libc's `<locale.h>` owns `locale_t`, and a `locale.h` on the include path would shadow it.

- [ ] **Step 1: Write the failing test.**

```c
#include <string.h>

#include "lang.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static struct tm date(int year, int month, int day, int wday)
{
    return (struct tm){ .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_wday = wday };
}

static void test_english_is_the_default_and_the_fallback(void)
{
    TEST_ASSERT_EQUAL_STRING("en", lang_get("en")->code);
    TEST_ASSERT_EQUAL_STRING("en", lang_get("xx")->code);
    TEST_ASSERT_EQUAL_STRING("en", lang_get(NULL)->code);
    TEST_ASSERT_EQUAL_STRING("Set time", lang_str(lang_get("en"), LS_SET_TIME));
    TEST_ASSERT_EQUAL_STRING("", lang_str(lang_get("en"), LS_COUNT));
}

static void test_dates_in_three_styles(void)
{
    const lang_t *en = lang_get("en");
    struct tm tm = date(2026, 9, 25, 5);
    char out[40];
    lang_format_date(en, &tm, LANG_DATE_LONG, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Friday 25 September", out);
    lang_format_date(en, &tm, LANG_DATE_MEDIUM, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Fri 25 Sep", out);
    lang_format_date(en, &tm, LANG_DATE_SHORT, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("25 Sep", out);
}

static void test_an_impossible_date_formats_as_empty(void)
{
    struct tm tm = date(2026, 13, 1, 0);
    char out[16] = "x";
    lang_format_date(lang_get("en"), &tm, LANG_DATE_LONG, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_times_in_24_and_12_hour_modes(void)
{
    char out[16];
    const char *suffix;
    lang_format_time(20, 48, 5, true, false, out, sizeof(out), &suffix);
    TEST_ASSERT_EQUAL_STRING("20:48", out);
    TEST_ASSERT_EQUAL_STRING("", suffix);
    lang_format_time(7, 5, 9, true, true, out, sizeof(out), &suffix);
    TEST_ASSERT_EQUAL_STRING("07:05:09", out);
    lang_format_time(20, 48, 0, false, false, out, sizeof(out), &suffix);
    TEST_ASSERT_EQUAL_STRING("8:48", out);
    TEST_ASSERT_EQUAL_STRING("PM", suffix);
    lang_format_time(0, 5, 0, false, false, out, sizeof(out), &suffix);
    TEST_ASSERT_EQUAL_STRING("12:05", out);
    TEST_ASSERT_EQUAL_STRING("AM", suffix);
    lang_format_time(12, 0, 0, false, false, out, sizeof(out), &suffix);
    TEST_ASSERT_EQUAL_STRING("12:00", out);
    TEST_ASSERT_EQUAL_STRING("PM", suffix);
}

/* The M2 review found the console printing -0.50 as "0.50"; the sign must survive below 1. */
static void test_decimals_keep_the_sign_below_one(void)
{
    const lang_t *en = lang_get("en");
    char out[16];
    lang_format_decimal(en, 234, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("23.4", out);
    lang_format_decimal(en, -5, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-0.5", out);
    lang_format_decimal(en, -50, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-0.50", out);
    lang_format_decimal(en, 45, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("45", out);
    lang_format_decimal(en, 0, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("0.0", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_english_is_the_default_and_the_fallback);
    RUN_TEST(test_dates_in_three_styles);
    RUN_TEST(test_an_impossible_date_formats_as_empty);
    RUN_TEST(test_times_in_24_and_12_hour_modes);
    RUN_TEST(test_decimals_keep_the_sign_below_one);
    return UNITY_END();
}
```

- [ ] **Step 2: Register it and watch it fail.** In `test/host/CMakeLists.txt`, before the `ui` library:

```cmake
# locale: pure C language packs.
add_library(locale STATIC ${REPO_ROOT}/components/locale/lang.c ${REPO_ROOT}/components/locale/lang_en.c)
target_include_directories(locale PUBLIC ${REPO_ROOT}/components/locale/include)
target_compile_options(locale PRIVATE ${REFLBO_WARNINGS})
```

and after `test_power_policy`:

```cmake
reflbo_host_test(test_lang locale)
```

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL: `components/locale/lang.c` does not exist.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/*
 * Language packs (spec §5.8): UI strings, weekday, month and moon-phase names, and the date, time
 * and number formats. The component is `locale`; the prefix is `lang_` because libc's <locale.h>
 * already owns `locale_t`. Pure C, host-buildable.
 */

typedef enum {
    LS_SET_TIME,
    LS_TIME,
    LS_DATE,
    LS_TEMPERATURE,
    LS_HUMIDITY,
    LS_DEW_POINT,
    LS_TODAY_MIN,
    LS_TODAY_MAX,
    LS_BATTERY,
    LS_BATTERY_DAYS,
    LS_WEEK,
    LS_MOON,
    LS_NAME_DAY,
    LS_HOLIDAY,
    LS_WEATHER,
    LS_TODAY,
    LS_FORECAST,
    LS_SUN,
    LS_DAYS_UNIT,    /* after a number of days: "d" */
    LS_HOURS_UNIT,   /* "h" */
    LS_MINUTES_UNIT, /* "min" */
    LS_COUNT,
} lang_str_t;

typedef enum {
    LANG_DATE_LONG,   /* Friday 25 September */
    LANG_DATE_MEDIUM, /* Fri 25 Sep */
    LANG_DATE_SHORT,  /* 25 Sep */
} lang_date_style_t;

typedef struct lang {
    const char *code; /* "en" */
    const char *name; /* in the language itself: "English" */
    const char *strings[LS_COUNT];
    const char *weekdays[7]; /* struct tm order: Sunday first */
    const char *weekdays_short[7];
    const char *months[12];
    const char *months_short[12];
    const char *moon_phases[8];       /* util_moon_t.index order */
    const char *moon_phases_short[8]; /* for small slots; the disc shows waxing or waning */
    char decimal_sep;
    int first_weekday; /* 0 Sunday, 1 Monday */
    void (*format_date)(const struct lang *lang, const struct tm *tm, lang_date_style_t style, char *out,
                        size_t size);
    const char *(*name_day)(int month, int day);         /* NULL: the pack has no calendar */
    const char *(*holiday)(int year, int month, int day); /* NULL, or NULL result: not a holiday */
} lang_t;

/* The pack for a code such as "en"; English for NULL or an unknown code. */
const lang_t *lang_get(const char *code);
const char *lang_str(const lang_t *lang, lang_str_t id);
/* An out-of-range tm writes an empty string. */
void lang_format_date(const lang_t *lang, const struct tm *tm, lang_date_style_t style, char *out, size_t size);
/* "20:48", "20:48:05", or "8:48" with *suffix "PM" in 12-hour mode (*suffix is "" otherwise). */
void lang_format_time(int hour, int minute, int second, bool h24, bool seconds, char *out, size_t size,
                      const char **suffix);
/* `value` scaled by 10^decimals: (-5, 1) is "-0.5" and (234, 1) is "23.4", with the pack's
 * decimal separator. Returns the length written. */
int lang_format_decimal(const lang_t *lang, long value, int decimals, char *out, size_t size);
```

```c
#include "lang.h"

#include <stdio.h>
#include <string.h>

extern const lang_t lang_en;

static const lang_t *const k_packs[] = { &lang_en };

const lang_t *lang_get(const char *code)
{
    for (size_t i = 0; code != NULL && i < sizeof(k_packs) / sizeof(k_packs[0]); i++) {
        if (strcmp(k_packs[i]->code, code) == 0) {
            return k_packs[i];
        }
    }
    return &lang_en;
}

const char *lang_str(const lang_t *lang, lang_str_t id)
{
    return (unsigned)id < LS_COUNT && lang->strings[id] != NULL ? lang->strings[id] : "";
}

void lang_format_date(const lang_t *lang, const struct tm *tm, lang_date_style_t style, char *out, size_t size)
{
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (tm->tm_wday < 0 || tm->tm_wday > 6 || tm->tm_mon < 0 || tm->tm_mon > 11 || tm->tm_mday < 1 ||
        tm->tm_mday > 31) {
        return;
    }
    lang->format_date(lang, tm, style, out, size);
}

void lang_format_time(int hour, int minute, int second, bool h24, bool seconds, char *out, size_t size,
                      const char **suffix)
{
    *suffix = "";
    if (!h24) {
        *suffix = hour < 12 ? "AM" : "PM";
        hour = hour % 12 == 0 ? 12 : hour % 12;
    }
    if (seconds) {
        snprintf(out, size, h24 ? "%02d:%02d:%02d" : "%d:%02d:%02d", hour, minute, second);
    } else {
        snprintf(out, size, h24 ? "%02d:%02d" : "%d:%02d", hour, minute);
    }
}

int lang_format_decimal(const lang_t *lang, long value, int decimals, char *out, size_t size)
{
    long scale = 1;
    for (int i = 0; i < decimals; i++) {
        scale *= 10;
    }
    const char *sign = value < 0 ? "-" : "";
    unsigned long magnitude = value < 0 ? 0ul - (unsigned long)value : (unsigned long)value;
    if (decimals <= 0) {
        return snprintf(out, size, "%s%lu", sign, magnitude);
    }
    return snprintf(out, size, "%s%lu%c%0*lu", sign, magnitude / (unsigned long)scale, lang->decimal_sep, decimals,
                    magnitude % (unsigned long)scale);
}
```

```c
#include <stdio.h>

#include "lang.h"

static void format_date(const lang_t *lang, const struct tm *tm, lang_date_style_t style, char *out, size_t size)
{
    switch (style) {
    case LANG_DATE_LONG:
        snprintf(out, size, "%s %d %s", lang->weekdays[tm->tm_wday], tm->tm_mday, lang->months[tm->tm_mon]);
        break;
    case LANG_DATE_MEDIUM:
        snprintf(out, size, "%s %d %s", lang->weekdays_short[tm->tm_wday], tm->tm_mday,
                 lang->months_short[tm->tm_mon]);
        break;
    case LANG_DATE_SHORT:
        snprintf(out, size, "%d %s", tm->tm_mday, lang->months_short[tm->tm_mon]);
        break;
    }
}

const lang_t lang_en = {
    .code = "en",
    .name = "English",
    .strings = {
        [LS_SET_TIME] = "Set time",
        [LS_TIME] = "Time",
        [LS_DATE] = "Date",
        [LS_TEMPERATURE] = "Temperature",
        [LS_HUMIDITY] = "Humidity",
        [LS_DEW_POINT] = "Dew point",
        [LS_TODAY_MIN] = "Today's low",
        [LS_TODAY_MAX] = "Today's high",
        [LS_BATTERY] = "Battery",
        [LS_BATTERY_DAYS] = "Battery left",
        [LS_WEEK] = "Week",
        [LS_MOON] = "Moon",
        [LS_NAME_DAY] = "Name day",
        [LS_HOLIDAY] = "Holiday",
        [LS_WEATHER] = "Weather",
        [LS_TODAY] = "Today",
        [LS_FORECAST] = "Forecast",
        [LS_SUN] = "Sunrise and sunset",
        [LS_DAYS_UNIT] = "d",
        [LS_HOURS_UNIT] = "h",
        [LS_MINUTES_UNIT] = "min",
    },
    .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
    .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
    .months = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
                "November", "December" },
    .months_short = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" },
    .moon_phases = { "New moon", "Waxing crescent", "First quarter", "Waxing gibbous", "Full moon", "Waning gibbous",
                     "Last quarter", "Waning crescent" },
    .moon_phases_short = { "New", "Crescent", "First qtr", "Gibbous", "Full", "Gibbous", "Last qtr", "Crescent" },
    .decimal_sep = '.',
    .first_weekday = 1,
    .format_date = format_date,
    .name_day = NULL,
    .holiday = NULL,
};
```

```cmake
# Language packs (spec §5.8). Pure C: also built on the host by test/host.
idf_component_register(SRCS "lang.c" "lang_en.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ./build-host/test_lang && ctest --test-dir build-host --output-on-failure`
Expected: `5 Tests 0 Failures`; the suite passes.

- [ ] **Step 5: Commit.**

```bash
git add components/locale test/host/test_lang.c test/host/CMakeLists.txt
git commit -m "feat(locale): add language packs with English"
```

---

### Task 5: Field store (`datastore`)

**Files:**
- Create: `components/datastore/CMakeLists.txt`, `components/datastore/include/datastore.h`, `components/datastore/datastore.c`, `test/host/test_datastore.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `ds_t`: plain data, kept in the app's RTC-RAM snapshot.
  - `ds_field_t`: `DS_ENV_TEMP`, `DS_ENV_HUM`, `DS_ENV_DEW`, `DS_ENV_TEMP_MIN`, `DS_ENV_TEMP_MAX`, `DS_BAT_LEVEL`, `DS_BAT_DAYS`.
  - `ds_entry_t { value, trend, updated, mv, bat_state }`; `ds_bat_state_t`; `ds_freshness_t` (missing, fresh, stale); `DS_NO_TREND`.
  - `ds_init`, `ds_set_ttl`.
  - `ds_set_env(ds, temp_c100, hum_pct100, now, local_day)`: also sets the dew point (Magnus), today's min and max (reset when `local_day` changes) and the hour trends (the newest reading 60–90 min old).
  - `ds_set_battery`, `ds_set`, `ds_clear`, `ds_get`, `ds_freshness(ds, field, now, local_day)`, `ds_take_changes`.
- Fields that follow from the clock are not stored (Task 8 computes them).

- [ ] **Step 1: Write the failing test.**

```c
#include "datastore.h"
#include "unity.h"

static ds_t s_ds;
#define T0  1790000000 /* a UTC time in 2026 */
#define DAY 20720

void setUp(void)
{
    ds_init(&s_ds);
}

void tearDown(void) {}

static ds_entry_t get(ds_field_t f)
{
    ds_entry_t e = { 0 };
    TEST_ASSERT_TRUE(ds_get(&s_ds, f, &e));
    return e;
}

static void test_fields_start_missing(void)
{
    ds_entry_t e;
    for (int f = 0; f < DS_FIELD_COUNT; f++) {
        TEST_ASSERT_FALSE(ds_get(&s_ds, (ds_field_t)f, &e));
        TEST_ASSERT_EQUAL(DS_MISSING, ds_freshness(&s_ds, (ds_field_t)f, T0, DAY));
    }
    TEST_ASSERT_EQUAL_HEX32(0, ds_take_changes(&s_ds));
}

static void test_env_reading_sets_temperature_humidity_and_dew_point(void)
{
    ds_set_env(&s_ds, 2500, 5000, T0, DAY);
    TEST_ASSERT_EQUAL_INT32(2500, get(DS_ENV_TEMP).value);
    TEST_ASSERT_EQUAL_INT32(5000, get(DS_ENV_HUM).value);
    TEST_ASSERT_INT32_WITHIN(10, 1386, get(DS_ENV_DEW).value); /* 25 °C at 50 % → 13.86 °C */
    TEST_ASSERT_EQUAL(DS_FRESH, ds_freshness(&s_ds, DS_ENV_TEMP, T0, DAY));
    uint32_t changes = ds_take_changes(&s_ds);
    TEST_ASSERT_TRUE(changes & (1u << DS_ENV_TEMP));
    TEST_ASSERT_TRUE(changes & (1u << DS_ENV_DEW));
    TEST_ASSERT_EQUAL_HEX32(0, ds_take_changes(&s_ds));
}

static void test_dew_point_below_zero_and_at_saturation(void)
{
    ds_set_env(&s_ds, -500, 8000, T0, DAY);
    TEST_ASSERT_INT32_WITHIN(5, -792, get(DS_ENV_DEW).value); /* -5 °C at 80 % → -7.92 °C */
    ds_set_env(&s_ds, 1000, 10000, T0 + 300, DAY);
    TEST_ASSERT_INT32_WITHIN(1, 1000, get(DS_ENV_DEW).value);
}

static void test_no_dew_point_below_one_percent_humidity(void)
{
    ds_set_env(&s_ds, 2500, 50, T0, DAY);
    ds_entry_t e;
    TEST_ASSERT_FALSE(ds_get(&s_ds, DS_ENV_DEW, &e));
}

static void test_values_go_stale_after_their_ttl(void)
{
    ds_set_env(&s_ds, 2100, 4000, T0, DAY);
    TEST_ASSERT_EQUAL(DS_FRESH, ds_freshness(&s_ds, DS_ENV_TEMP, T0 + 900, DAY));
    TEST_ASSERT_EQUAL(DS_STALE, ds_freshness(&s_ds, DS_ENV_TEMP, T0 + 901, DAY));
    ds_set_ttl(&s_ds, DS_ENV_TEMP, 3600);
    TEST_ASSERT_EQUAL(DS_FRESH, ds_freshness(&s_ds, DS_ENV_TEMP, T0 + 901, DAY));
}

static void test_min_and_max_follow_the_day_and_restart_at_midnight(void)
{
    ds_set_env(&s_ds, 2100, 4000, T0, DAY);
    ds_set_env(&s_ds, 1900, 4000, T0 + 600, DAY);
    ds_set_env(&s_ds, 2300, 4000, T0 + 1200, DAY);
    TEST_ASSERT_EQUAL_INT32(1900, get(DS_ENV_TEMP_MIN).value);
    TEST_ASSERT_EQUAL_INT32(2300, get(DS_ENV_TEMP_MAX).value);
    TEST_ASSERT_EQUAL(DS_MISSING, ds_freshness(&s_ds, DS_ENV_TEMP_MAX, T0 + 1300, DAY + 1));
    ds_set_env(&s_ds, 2200, 4000, T0 + 1800, DAY + 1);
    TEST_ASSERT_EQUAL_INT32(2200, get(DS_ENV_TEMP_MIN).value);
    TEST_ASSERT_EQUAL_INT32(2200, get(DS_ENV_TEMP_MAX).value);
    TEST_ASSERT_EQUAL(DS_FRESH, ds_freshness(&s_ds, DS_ENV_TEMP_MIN, T0 + 1800, DAY + 1));
}

static void test_trend_compares_with_the_reading_an_hour_ago(void)
{
    for (int i = 0; i <= 12; i++) { /* every 5 min for an hour, rising 0.1 °C and 0.5 % each */
        ds_set_env(&s_ds, 2000 + 10 * i, 4000 + 50 * i, T0 + 300 * i, DAY);
    }
    TEST_ASSERT_EQUAL_INT32(120, get(DS_ENV_TEMP).trend);
    TEST_ASSERT_EQUAL_INT32(600, get(DS_ENV_HUM).trend);
}

static void test_no_trend_without_an_hour_of_history(void)
{
    ds_set_env(&s_ds, 2000, 4000, T0, DAY);
    ds_set_env(&s_ds, 2100, 4000, T0 + 3000, DAY); /* 50 min */
    TEST_ASSERT_EQUAL_INT32(DS_NO_TREND, get(DS_ENV_TEMP).trend);
    ds_set_env(&s_ds, 2200, 4000, T0 + 9000, DAY); /* only readings older than 90 min: too old */
    TEST_ASSERT_EQUAL_INT32(DS_NO_TREND, get(DS_ENV_TEMP).trend);
}

static void test_frequent_readings_do_not_crowd_the_history(void)
{
    for (int i = 0; i <= 60; i++) { /* every minute for an hour */
        ds_set_env(&s_ds, 2000 + i, 4000, T0 + 60 * i, DAY);
    }
    TEST_ASSERT_EQUAL_INT32(60, get(DS_ENV_TEMP).trend);
    TEST_ASSERT_TRUE(s_ds.env_count <= 13);
}

static void test_battery_carries_voltage_and_state(void)
{
    ds_set_battery(&s_ds, 83, 4051, DS_BAT_DISCHARGING, T0);
    ds_entry_t e = get(DS_BAT_LEVEL);
    TEST_ASSERT_EQUAL_INT32(83, e.value);
    TEST_ASSERT_EQUAL_INT(4051, e.mv);
    TEST_ASSERT_EQUAL(DS_BAT_DISCHARGING, e.bat_state);
}

static void test_set_and_clear_any_field(void)
{
    ds_set(&s_ds, DS_BAT_DAYS, 92, T0);
    TEST_ASSERT_EQUAL_INT32(92, get(DS_BAT_DAYS).value);
    ds_take_changes(&s_ds);
    ds_clear(&s_ds, DS_BAT_DAYS);
    TEST_ASSERT_EQUAL(DS_MISSING, ds_freshness(&s_ds, DS_BAT_DAYS, T0, DAY));
    TEST_ASSERT_EQUAL_HEX32(1u << DS_BAT_DAYS, ds_take_changes(&s_ds));
    ds_clear(&s_ds, DS_BAT_DAYS); /* already clear: no change */
    TEST_ASSERT_EQUAL_HEX32(0, ds_take_changes(&s_ds));
    ds_set(&s_ds, DS_FIELD_COUNT, 1, T0); /* out of range: ignored */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fields_start_missing);
    RUN_TEST(test_env_reading_sets_temperature_humidity_and_dew_point);
    RUN_TEST(test_dew_point_below_zero_and_at_saturation);
    RUN_TEST(test_no_dew_point_below_one_percent_humidity);
    RUN_TEST(test_values_go_stale_after_their_ttl);
    RUN_TEST(test_min_and_max_follow_the_day_and_restart_at_midnight);
    RUN_TEST(test_trend_compares_with_the_reading_an_hour_ago);
    RUN_TEST(test_no_trend_without_an_hour_of_history);
    RUN_TEST(test_frequent_readings_do_not_crowd_the_history);
    RUN_TEST(test_battery_carries_voltage_and_state);
    RUN_TEST(test_set_and_clear_any_field);
    return UNITY_END();
}
```

- [ ] **Step 2: Register it and watch it fail.** In `test/host/CMakeLists.txt`, after the `locale` library:

```cmake
# datastore: pure C field store.
add_library(datastore STATIC ${REPO_ROOT}/components/datastore/datastore.c)
target_include_directories(datastore PUBLIC ${REPO_ROOT}/components/datastore/include)
target_compile_options(datastore PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(datastore PUBLIC m)
```

and after `test_lang`:

```cmake
reflbo_host_test(test_datastore datastore)
```

Run: `cmake -S test/host -B build-host -G Ninja`
Expected: FAIL: `components/datastore/datastore.c` does not exist.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * Field store (spec §6): measured and fetched values with their age and freshness. Fields that
 * follow from the clock (time, date, week, moon phase, name day, holiday) are computed by `ui`
 * from the time and the language pack, so they are not stored. A ds_t is plain data: the app
 * keeps it in its RTC-RAM snapshot, so it survives deep sleep. Pure C, host-buildable. The app
 * task owns it; a lock arrives with the first other writer (the sync task, M5).
 */

typedef enum {
    DS_ENV_TEMP,     /* 0.01 °C, calibration offset applied */
    DS_ENV_HUM,      /* 0.01 %RH */
    DS_ENV_DEW,      /* 0.01 °C, from temperature and humidity */
    DS_ENV_TEMP_MIN, /* 0.01 °C, since local midnight */
    DS_ENV_TEMP_MAX,
    DS_BAT_LEVEL, /* %; the entry also carries the voltage and the charging state */
    DS_BAT_DAYS,  /* 0.1 days of battery left */
    DS_FIELD_COUNT,
} ds_field_t;

typedef enum {
    DS_BAT_UNKNOWN,
    DS_BAT_DISCHARGING,
    DS_BAT_CHARGING,
    DS_BAT_FULL,
} ds_bat_state_t;

typedef enum {
    DS_MISSING, /* never set, cleared, or from another day */
    DS_FRESH,
    DS_STALE,   /* older than the field's time to live */
} ds_freshness_t;

#define DS_NO_TREND INT32_MIN
#define DS_ENV_HISTORY 16

typedef struct {
    int32_t value;    /* in the unit listed above */
    int32_t trend;    /* change over about the last hour, same unit; DS_NO_TREND if unknown */
    uint32_t updated; /* UTC seconds; 0 = not set */
    int16_t mv;       /* DS_BAT_LEVEL: smoothed battery voltage */
    uint8_t bat_state; /* DS_BAT_LEVEL: ds_bat_state_t */
} ds_entry_t;

typedef struct {
    uint32_t time;
    int32_t temp;
    int32_t hum;
} ds_env_point_t;

typedef struct {
    ds_entry_t entries[DS_FIELD_COUNT];
    uint32_t ttl_s[DS_FIELD_COUNT];
    ds_env_point_t env_history[DS_ENV_HISTORY];
    uint8_t env_head;
    uint8_t env_count;
    int32_t min_max_day; /* local day of the min and max */
    uint32_t changes;    /* one bit per ds_field_t since the last ds_take_changes() */
} ds_t;

void ds_init(ds_t *ds);
/* The time after which a value counts as stale (spec §5.1: 15 min for the sensors). */
void ds_set_ttl(ds_t *ds, ds_field_t field, uint32_t ttl_s);
/* An SHTC3 reading. `local_day` numbers the local calendar day; the min and max restart when
 * it changes. Also updates the dew point and the trends. */
void ds_set_env(ds_t *ds, int temp_c100, int hum_pct100, time_t now, int32_t local_day);
void ds_set_battery(ds_t *ds, int level_pct, int mv, ds_bat_state_t state, time_t now);
/* Any field to a plain value, e.g. from the `field set` console command. */
void ds_set(ds_t *ds, ds_field_t field, int32_t value, time_t now);
void ds_clear(ds_t *ds, ds_field_t field);
/* False, and *out untouched, if the field is not set. */
bool ds_get(const ds_t *ds, ds_field_t field, ds_entry_t *out);
ds_freshness_t ds_freshness(const ds_t *ds, ds_field_t field, time_t now, int32_t local_day);
/* The fields changed since the previous call, as a bit mask of ds_field_t. */
uint32_t ds_take_changes(ds_t *ds);
```

```c
#include "datastore.h"

#include <math.h>
#include <string.h>

#define DEFAULT_TTL_S       900  /* spec §5.1: stale after 15 min without a reading */
#define HISTORY_SPACING_S   300  /* at most one trend point per 5 min */
#define TREND_MIN_AGE_S     3600 /* the trend compares with the newest reading 60–90 min old */
#define TREND_MAX_AGE_S     5400

void ds_init(ds_t *ds)
{
    memset(ds, 0, sizeof(*ds));
    for (int f = 0; f < DS_FIELD_COUNT; f++) {
        ds->entries[f].trend = DS_NO_TREND;
        ds->ttl_s[f] = DEFAULT_TTL_S;
    }
    ds->min_max_day = INT32_MIN;
}

void ds_set_ttl(ds_t *ds, ds_field_t field, uint32_t ttl_s)
{
    if ((unsigned)field < DS_FIELD_COUNT) {
        ds->ttl_s[field] = ttl_s;
    }
}

static void store(ds_t *ds, ds_field_t field, int32_t value, int32_t trend, time_t now)
{
    ds_entry_t *e = &ds->entries[field];
    e->value = value;
    e->trend = trend;
    e->updated = (uint32_t)now;
    ds->changes |= 1u << field;
}

/* Magnus formula (Sonntag 1990 constants); NaN below 1 %RH, where it breaks down. */
static int32_t dew_point_c100(int temp_c100, int hum_pct100)
{
    double t = temp_c100 / 100.0;
    double rh = hum_pct100 / 100.0;
    double gamma = log(rh / 100.0) + 17.62 * t / (243.12 + t);
    return (int32_t)lround(243.12 * gamma / (17.62 - gamma) * 100.0);
}

static const ds_env_point_t *point_ago(const ds_t *ds, uint32_t now)
{
    for (int k = 0; k < ds->env_count; k++) {
        const ds_env_point_t *p = &ds->env_history[(ds->env_head + DS_ENV_HISTORY - 1 - k) % DS_ENV_HISTORY];
        uint32_t age = now - p->time;
        if (age >= TREND_MIN_AGE_S) {
            return age <= TREND_MAX_AGE_S ? p : NULL;
        }
    }
    return NULL;
}

void ds_set_env(ds_t *ds, int temp_c100, int hum_pct100, time_t now, int32_t local_day)
{
    uint32_t t = (uint32_t)now;
    const ds_env_point_t *old = point_ago(ds, t);
    store(ds, DS_ENV_TEMP, temp_c100, old ? temp_c100 - old->temp : DS_NO_TREND, now);
    store(ds, DS_ENV_HUM, hum_pct100, old ? hum_pct100 - old->hum : DS_NO_TREND, now);
    if (hum_pct100 >= 100) {
        store(ds, DS_ENV_DEW, dew_point_c100(temp_c100, hum_pct100), DS_NO_TREND, now);
    } else {
        ds_clear(ds, DS_ENV_DEW);
    }

    if (ds->min_max_day != local_day || ds->entries[DS_ENV_TEMP_MIN].updated == 0) {
        ds->min_max_day = local_day;
        store(ds, DS_ENV_TEMP_MIN, temp_c100, DS_NO_TREND, now);
        store(ds, DS_ENV_TEMP_MAX, temp_c100, DS_NO_TREND, now);
    } else {
        if (temp_c100 < ds->entries[DS_ENV_TEMP_MIN].value) {
            store(ds, DS_ENV_TEMP_MIN, temp_c100, DS_NO_TREND, now);
        }
        if (temp_c100 > ds->entries[DS_ENV_TEMP_MAX].value) {
            store(ds, DS_ENV_TEMP_MAX, temp_c100, DS_NO_TREND, now);
        }
        ds->entries[DS_ENV_TEMP_MIN].updated = t; /* still today's: fresh as long as readings keep coming */
        ds->entries[DS_ENV_TEMP_MAX].updated = t;
    }

    const ds_env_point_t *newest =
        ds->env_count ? &ds->env_history[(ds->env_head + DS_ENV_HISTORY - 1) % DS_ENV_HISTORY] : NULL;
    if (newest == NULL || t - newest->time >= HISTORY_SPACING_S) {
        ds->env_history[ds->env_head] = (ds_env_point_t){ t, temp_c100, hum_pct100 };
        ds->env_head = (uint8_t)((ds->env_head + 1) % DS_ENV_HISTORY);
        if (ds->env_count < DS_ENV_HISTORY) {
            ds->env_count++;
        }
    }
}

void ds_set_battery(ds_t *ds, int level_pct, int mv, ds_bat_state_t state, time_t now)
{
    store(ds, DS_BAT_LEVEL, level_pct, DS_NO_TREND, now);
    ds->entries[DS_BAT_LEVEL].mv = (int16_t)mv;
    ds->entries[DS_BAT_LEVEL].bat_state = (uint8_t)state;
}

void ds_set(ds_t *ds, ds_field_t field, int32_t value, time_t now)
{
    if ((unsigned)field < DS_FIELD_COUNT) {
        store(ds, field, value, DS_NO_TREND, now);
    }
}

void ds_clear(ds_t *ds, ds_field_t field)
{
    if ((unsigned)field < DS_FIELD_COUNT && ds->entries[field].updated != 0) {
        ds->entries[field] = (ds_entry_t){ .trend = DS_NO_TREND };
        ds->changes |= 1u << field;
    }
}

bool ds_get(const ds_t *ds, ds_field_t field, ds_entry_t *out)
{
    if ((unsigned)field >= DS_FIELD_COUNT || ds->entries[field].updated == 0) {
        return false;
    }
    *out = ds->entries[field];
    return true;
}

ds_freshness_t ds_freshness(const ds_t *ds, ds_field_t field, time_t now, int32_t local_day)
{
    ds_entry_t e;
    if (!ds_get(ds, field, &e)) {
        return DS_MISSING;
    }
    if ((field == DS_ENV_TEMP_MIN || field == DS_ENV_TEMP_MAX) && ds->min_max_day != local_day) {
        return DS_MISSING; /* yesterday's extremes */
    }
    return (uint32_t)now - e.updated > ds->ttl_s[field] ? DS_STALE : DS_FRESH;
}

uint32_t ds_take_changes(ds_t *ds)
{
    uint32_t changes = ds->changes;
    ds->changes = 0;
    return changes;
}
```

```cmake
# Field store (spec §6). Pure C: also built on the host by test/host.
idf_component_register(SRCS "datastore.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ./build-host/test_datastore && ctest --test-dir build-host --output-on-failure`
Expected: `11 Tests 0 Failures`; the suite passes. The dew point at −5 °C and 80 % is −7.92 °C. By hand: γ = ln 0.8 + 17.62·(−5)/238.12 = −0.5931; Td = 243.12·γ/(17.62 − γ) = −7.92.

- [ ] **Step 5: Commit.**

```bash
git add components/datastore test/host/test_datastore.c test/host/CMakeLists.txt
git commit -m "feat(datastore): add the field store with dew point, daily extremes and trends"
```

---

### Task 6: Battery days left and runtime offsets (`sensors`)

**Files:**
- Modify: `components/sensors/include/battery_model.h`, `components/sensors/battery_model.c`, `test/host/test_battery_model.c`, `components/sensors/include/sensors.h`, `components/sensors/sensors.c`

**Interfaces:**
- Produces:
  - `int battery_percent10_from_mv(int mv)` (0.1 %).
  - `int battery_gauge_days_left10(const battery_gauge_t *g, uint32_t now_s)`: -1 unless discharging with at least 6 h of hourly history, which charging restarts.
  - `battery_gauge_t` gains `levels[25]`, `level_count` and `level_head`: the sensors snapshot grows by 152 bytes.
  - `void sensors_set_offsets(int temp_c100, int hum_pct100)`: the Kconfig offsets are now only the defaults.
  - `int sensors_battery_days_left10(time_t now)`.

- [ ] **Step 1: Write the failing tests** (the file replaces the old one: four new tests after the M2 ones).

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

/* The review found that a 240 s history spacing with 8 slots spans only 28 min, so charging was
 * never seen at 4-minute samples and flickered at 1 and 2 minutes. */
static void test_charging_is_seen_at_every_sample_interval(void)
{
    const uint32_t intervals[] = { 60, 120, 180, 240, 300, 600 };
    for (unsigned i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++) {
        battery_gauge_init(&s_g);
        int missed = 0;
        for (uint32_t elapsed = 0; elapsed <= 3 * 3600; elapsed += intervals[i]) {
            uint32_t now = 1000 + elapsed;
            battery_gauge_add(&s_g, now, 3700 + (int)(elapsed * 2 / 60)); /* +2 mV per minute */
            if (elapsed >= 40 * 60 && battery_gauge_state(&s_g, now) != BATTERY_CHARGING) {
                missed++;
            }
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, missed, "a sample interval hid the charging trend");
    }
}

static void test_percent_in_tenths_matches_the_curve(void)
{
    TEST_ASSERT_EQUAL_INT(500, battery_percent10_from_mv(3840));
    TEST_ASSERT_EQUAL_INT(475, battery_percent10_from_mv(3830));
    TEST_ASSERT_EQUAL_INT(1000, battery_percent10_from_mv(4300));
    TEST_ASSERT_EQUAL_INT(0, battery_percent10_from_mv(3000));
}

/* A 5-min sample every step, falling `mv_per_hour` steadily from `mv0`. */
static uint32_t discharge(uint32_t t0, int mv0, int mv_per_hour, int hours)
{
    uint32_t t = t0;
    for (int i = 0; i <= hours * 12; i++) {
        t = t0 + (uint32_t)i * 300u;
        battery_gauge_add(&s_g, t, mv0 - mv_per_hour * i / 12);
    }
    return t;
}

static void test_days_left_after_six_hours_of_discharge(void)
{
    /* 3.79 V down 2 mV an hour, where the curve is 2.5 % per 10 mV throughout: 0.5 % an hour. After
     * 8 h the level is 31 %, so 31 / 0.5 / 24 = 2.6 days are left. */
    uint32_t t = discharge(1000, 3790, 2, 8);
    TEST_ASSERT_INT_WITHIN(3, 26, battery_gauge_days_left10(&s_g, t));
}

static void test_no_days_left_before_six_hours_or_without_a_drop(void)
{
    uint32_t t = discharge(1000, 3900, 10, 5);
    TEST_ASSERT_EQUAL_INT(-1, battery_gauge_days_left10(&s_g, t));
    battery_gauge_init(&s_g);
    t = add_steady(1000, 3800, 12 * 8); /* flat for 8 h: no measurable drop */
    TEST_ASSERT_EQUAL_INT(-1, battery_gauge_days_left10(&s_g, t));
}

static void test_charging_restarts_the_days_left_history(void)
{
    uint32_t t = discharge(1000, 3790, 2, 8);
    for (int i = 1; i <= 12; i++) { /* an hour on the charger: +100 mV */
        battery_gauge_add(&s_g, t + (uint32_t)i * 300u, 3774 + 100 * i / 12);
    }
    t += 3600;
    TEST_ASSERT_EQUAL_INT(-1, battery_gauge_days_left10(&s_g, t));
    TEST_ASSERT_EQUAL_INT(0, s_g.level_count);
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
    RUN_TEST(test_charging_is_seen_at_every_sample_interval);
    RUN_TEST(test_percent_in_tenths_matches_the_curve);
    RUN_TEST(test_days_left_after_six_hours_of_discharge);
    RUN_TEST(test_no_days_left_before_six_hours_or_without_a_drop);
    RUN_TEST(test_charging_restarts_the_days_left_history);
    return UNITY_END();
}
```

- [ ] **Step 2: Watch them fail.**

Run: `cmake --build build-host`
Expected: FAIL to compile: `battery_percent10_from_mv`, `battery_gauge_days_left10` and `level_count` undeclared.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stdint.h>

/*
 * Battery gauge logic (spec §8): open-circuit-voltage curve, smoothing and inferred charging
 * state. Pure C, host-buildable; the ADC reading happens in battery.c.
 */

#define BATTERY_HISTORY 8 /* samples kept for charging inference, at least 4.5 min apart */

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

#define BATTERY_LEVEL_HISTORY 25 /* hourly levels for the days-left estimate: a day's worth */

typedef struct {
    uint32_t time_s;
    uint16_t pct10; /* 0.1 % */
} battery_level_point_t;

typedef struct {
    uint32_t ema_mv16;  /* smoothed voltage, mV x 16; 0 = no sample yet */
    uint8_t level;      /* %, never rises unless charging or full */
    uint8_t count;      /* valid history points */
    uint8_t head;       /* next history slot */
    battery_point_t history[BATTERY_HISTORY];
    battery_level_point_t levels[BATTERY_LEVEL_HISTORY]; /* restart whenever charging is seen */
    uint8_t level_count;
    uint8_t level_head;
} battery_gauge_t;

int battery_percent_from_mv(int mv);   /* OCV table, 0..100 */
int battery_percent10_from_mv(int mv); /* the same in 0.1 %, 0..1000 */
void battery_gauge_init(battery_gauge_t *g);
void battery_gauge_add(battery_gauge_t *g, uint32_t now_s, int mv);
int battery_gauge_mv(const battery_gauge_t *g);    /* smoothed; 0 before the first sample */
int battery_gauge_level(const battery_gauge_t *g); /* %; -1 before the first sample */
battery_state_t battery_gauge_state(const battery_gauge_t *g, uint32_t now_s);
/* Days of battery left, in 0.1 days: the level divided by the discharge rate over up to the
 * last 24 h (spec §5.1 `bat.days`). -1 unless discharging with at least 6 h of history. */
int battery_gauge_days_left10(const battery_gauge_t *g, uint32_t now_s);
```

```c
#include "battery_model.h"

#include <stdbool.h>
#include <stddef.h>

#define HISTORY_SPACING_S 270  /* at most one history point per 4.5 min; extra samples are skipped */
#define TREND_WINDOW_S    1800 /* 30 min */
#define CHARGE_RISE_MV    30
#define FULL_MV           4150
#define STEADY_MV         10
#define LEVEL_SPACING_S   3600  /* one days-left point per hour */
#define DAYS_MIN_SPAN_S   21600 /* 6 h of discharge before estimating */

/* Right after a point is added, the oldest one must already be a full window old, at any sample
 * interval, or the state falls back to UNKNOWN until the next point arrives. */
_Static_assert((BATTERY_HISTORY - 1) * HISTORY_SPACING_S >= TREND_WINDOW_S, "history too short for the trend window");

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

int battery_percent10_from_mv(int mv)
{
    if (mv <= k_ocv[0].mv) {
        return 0;
    }
    for (unsigned i = 1; i < OCV_POINTS; i++) {
        if (mv <= k_ocv[i].mv) {
            int dv = k_ocv[i].mv - k_ocv[i - 1].mv;
            int dp = k_ocv[i].pct - k_ocv[i - 1].pct;
            return k_ocv[i - 1].pct * 10 + ((mv - k_ocv[i - 1].mv) * dp * 10 + dv / 2) / dv;
        }
    }
    return 1000;
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

    if (state == BATTERY_CHARGING || state == BATTERY_FULL) {
        g->level_count = 0; /* the discharge rate starts over once the charger lets go */
        return;
    }
    const battery_level_point_t *last =
        g->level_count ? &g->levels[(g->level_head + BATTERY_LEVEL_HISTORY - 1) % BATTERY_LEVEL_HISTORY] : NULL;
    if (last == NULL || now_s - last->time_s >= LEVEL_SPACING_S) {
        g->levels[g->level_head] =
            (battery_level_point_t){ .time_s = now_s, .pct10 = (uint16_t)battery_percent10_from_mv(battery_gauge_mv(g)) };
        g->level_head = (uint8_t)((g->level_head + 1) % BATTERY_LEVEL_HISTORY);
        if (g->level_count < BATTERY_LEVEL_HISTORY) {
            g->level_count++;
        }
    }
}

int battery_gauge_days_left10(const battery_gauge_t *g, uint32_t now_s)
{
    if (g->level_count == 0 || battery_gauge_state(g, now_s) != BATTERY_DISCHARGING) {
        return -1;
    }
    const battery_level_point_t *oldest =
        &g->levels[(g->level_head + BATTERY_LEVEL_HISTORY - g->level_count) % BATTERY_LEVEL_HISTORY];
    uint32_t span = now_s - oldest->time_s;
    int now_pct10 = battery_percent10_from_mv(battery_gauge_mv(g));
    int drop = oldest->pct10 - now_pct10;
    if (span < DAYS_MIN_SPAN_S || drop <= 0) {
        return -1;
    }
    int64_t days10 = (int64_t)now_pct10 * span * 10 / ((int64_t)drop * 86400);
    return days10 > 9999 ? 9999 : (int)days10;
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
/* Calibration offsets added to every reading (settings sensors.temp_offset_c, hum_offset_pct). */
void sensors_set_offsets(int temp_c100, int hum_pct100);
sensors_env_t sensors_env(void);
sensors_battery_t sensors_battery(time_t now);
/* 0.1 days of battery left, or -1 (battery_gauge_days_left10). */
int sensors_battery_days_left10(time_t now);
void sensors_export(sensors_state_t *out);
void sensors_import(const sensors_state_t *in);
```

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
static int s_temp_offset_c100 = CONFIG_REFLBO_TEMP_OFFSET_C10 * 10;
static int s_hum_offset_pct100 = CONFIG_REFLBO_HUM_OFFSET_PCT10 * 10;

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
        .temp_c100 = t + s_temp_offset_c100,
        .hum_pct100 = h + s_hum_offset_pct100,
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

void sensors_set_offsets(int temp_c100, int hum_pct100)
{
    s_temp_offset_c100 = temp_c100;
    s_hum_offset_pct100 = hum_pct100;
}

int sensors_battery_days_left10(time_t now)
{
    return battery_gauge_days_left10(&s_state.gauge, (uint32_t)now);
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

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_battery_model && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `14 Tests 0 Failures`; the suite passes; the firmware builds without warnings. At 3.79 V falling 2 mV an hour, the curve gives 0.5 % an hour, so after 8 h at 31 % about 2.6 days are left.

- [ ] **Step 5: Commit.**

```bash
git add components/sensors test/host/test_battery_model.c
git commit -m "feat(sensors): estimate battery days left and take calibration offsets at runtime"
```

---

### Task 7: Auto-cycle and seconds wakes (`scheduler`)

**Files:**
- Modify: `components/scheduler/include/scheduler.h`, `components/scheduler/scheduler.c`, `test/host/test_scheduler.c`

**Interfaces:**
- Produces:
  - `sched_input_t` gains `time_t cycle_at` (0 = off) and `bool every_second`.
  - `sched_wake_t` gains `time_t alarm`, the next minute-aligned slot for the RTC. `when` is the earliest wake of any kind.
  - New reasons `SCHED_CYCLE` and `SCHED_SECOND`. An overdue cycle switch is due in the next second.
- Existing callers that fill the first three fields keep working (the new fields are zero).

- [ ] **Step 1: Write the failing tests** (the file replaces the old one: three new tests after the M2 ones).

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

static void test_seconds_display_wakes_every_second_and_keeps_the_minute_alarm(void)
{
    time_t now = utc(2026, 9, 25, 18, 48, 20);
    sched_wake_t w = scheduler_next_wake(&(sched_input_t){ now, 1, 5, 0, true });
    TEST_ASSERT_EQUAL_INT64(now + 1, w.when);
    TEST_ASSERT_EQUAL_HEX(SCHED_SECOND, w.reasons);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 18, 49, 0), w.alarm);
}

static void test_cycle_switch_before_the_next_minute_comes_first(void)
{
    time_t now = utc(2026, 9, 25, 18, 48, 20);
    sched_wake_t w = scheduler_next_wake(&(sched_input_t){ now, 1, 5, now + 15, false });
    TEST_ASSERT_EQUAL_INT64(now + 15, w.when);
    TEST_ASSERT_EQUAL_HEX(SCHED_CYCLE, w.reasons);
    w = scheduler_next_wake(&(sched_input_t){ now, 1, 5, utc(2026, 9, 25, 18, 49, 0), false });
    TEST_ASSERT_EQUAL_HEX(SCHED_DISPLAY | SCHED_CYCLE, w.reasons); /* same second: both */
    w = scheduler_next_wake(&(sched_input_t){ now, 1, 5, now + 3600, false });
    TEST_ASSERT_EQUAL_INT64(w.alarm, w.when);
}

static void test_an_overdue_cycle_switch_runs_in_the_next_second(void)
{
    time_t now = utc(2026, 9, 25, 18, 48, 20);
    sched_wake_t w = scheduler_next_wake(&(sched_input_t){ now, 1, 5, now - 30, false });
    TEST_ASSERT_EQUAL_INT64(now + 1, w.when);
    TEST_ASSERT_EQUAL_HEX(SCHED_CYCLE, w.reasons);
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
    RUN_TEST(test_seconds_display_wakes_every_second_and_keeps_the_minute_alarm);
    RUN_TEST(test_cycle_switch_before_the_next_minute_comes_first);
    RUN_TEST(test_an_overdue_cycle_switch_runs_in_the_next_second);
    return UNITY_END();
}
```

- [ ] **Step 2: Watch them fail.**

Run: `cmake --build build-host`
Expected: FAIL to compile: `sched_input_t` has no member `cycle_at`, `SCHED_SECOND` undeclared.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stdbool.h>
#include <time.h>

/*
 * Wake scheduler (spec §9.2): when to wake next and why. Pure C, host-buildable. Periodic jobs run
 * at local wall-clock slots (minute of day divisible by their period), so DST shifts nothing.
 * Local time comes from the TZ environment variable (tzset()). Preset cycling and the seconds
 * display add wakes between minutes; M5 adds syncs, M7 user alarms.
 */

typedef enum {
    SCHED_DISPLAY = 1u << 0, /* display update */
    SCHED_SENSORS = 1u << 1, /* SHTC3 and battery sample */
    SCHED_CYCLE = 1u << 2,   /* the next auto-cycle preset switch */
    SCHED_SECOND = 1u << 3,  /* the active preset shows seconds */
} sched_reason_t;

typedef struct {
    time_t now;            /* UTC seconds */
    int display_every_min; /* 1..15 */
    int sensors_every_min; /* 1..30 */
    time_t cycle_at;       /* next auto-cycle switch (UTC); 0 = cycling is off */
    bool every_second;
} sched_input_t;

typedef struct {
    time_t when;      /* UTC; the earliest wake, after `now` */
    unsigned reasons; /* sched_reason_t bits due at `when` */
    time_t alarm;     /* the next minute-aligned wake (display or sensors), for the RTC alarm */
} sched_wake_t;

sched_wake_t scheduler_next_wake(const sched_input_t *in);
/* True if `t` is a local slot of a job that runs every `every_min` minutes. */
int scheduler_is_slot(time_t t, int every_min);
```

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
    time_t cycle = in->cycle_at == 0 ? 0 : in->cycle_at > in->now ? in->cycle_at : in->now + 1; /* overdue: now */
    time_t second = in->every_second ? in->now + 1 : 0;

    sched_wake_t wake = { .alarm = display < sensors ? display : sensors };
    wake.when = wake.alarm;
    if (cycle != 0 && cycle < wake.when) {
        wake.when = cycle;
    }
    if (second != 0 && second < wake.when) {
        wake.when = second;
    }
    wake.reasons = (display == wake.when ? SCHED_DISPLAY : 0) | (sensors == wake.when ? SCHED_SENSORS : 0) |
                   (cycle == wake.when ? SCHED_CYCLE : 0) | (second == wake.when ? SCHED_SECOND : 0);
    return wake;
}
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_scheduler && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `10 Tests 0 Failures`; the suite passes; the firmware builds (the app still reads `.when` for the RTC alarm, which equals `.alarm` without the new inputs).

- [ ] **Step 5: Commit.**

```bash
git add components/scheduler test/host/test_scheduler.c
git commit -m "feat(scheduler): add wakes for auto-cycling and the seconds display"
```


---

### Task 8: The field catalogue (`ui` fields)

**Files:**
- Create: `components/ui/include/ui_fields.h`, `components/ui/ui_fields.c`, `test/host/test_ui_fields.c`, `test/host/context_fixtures.h`
- Modify: `components/ui/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `ds_t`, `ds_get()`, `ds_freshness()` and the `DS_*` fields (Task 5).
  - `lang_t` and its functions (Task 4).
  - `util_iso_week()` and `util_moon_phase()` (Task 1).
- Produces:
  - `ui_field_id_t`: the spec §5.1 catalogue. The weather and sun fields exist but resolve to missing until M5.
  - `ui_field_kind_t` and `UI_KIND(k)` bits (layouts accept kinds per slot, Task 9).
  - `ui_field_info()` and `ui_field_by_name()`.
  - `ui_context_t`: everything one render reads.
  - `ui_value_t`: what a widget shows. It holds the state (missing, fresh, stale) with the age, the label, `text`, `unit`, `extra`, `short_text`, `trend`, `percent`, the battery state and the moon.
  - `void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)`.
- `context_fixtures.h` is the fixed test context: Friday 25 September 2026, 20:48 CEST. Task 10's dashboard fixtures build on it.
- `ui_clock.c` stays until Task 13, so the firmware keeps its M2 screen until the app switches over.

- [ ] **Step 1: Write the failing test.**

```c
#pragma once

#include "datastore.h"
#include "lang.h"
#include "ui_fields.h"
#include "util_time.h"

/* Fixed inputs for the ui tests (test_ui_fields.c, dashboard_fixtures.h): Friday 25 September
 * 2026, 20:48 CEST (18:48 UTC). */

#define FIX_DAY ((int32_t)20721) /* util_days_from_civil(2026, 9, 25) */
#define FIX_NOW ((time_t)FIX_DAY * 86400 + 18 * 3600 + 48 * 60)

static ds_t s_fix_ds;

static inline struct tm fixture_local(int hour, int minute, int second)
{
    struct tm tm = { .tm_year = 126, .tm_mon = 8, .tm_mday = 25, .tm_wday = 5, .tm_hour = hour,
                     .tm_min = minute, .tm_sec = second, .tm_yday = 267 };
    return tm;
}

/* An hour of readings, warming 0.7 °C, then battery and days left: everything fresh. */
static inline void fixture_fill(ds_t *ds, time_t now)
{
    ds_init(ds);
    for (int i = 0; i <= 12; i++) {
        time_t t = now - 3600 + 300 * i;
        int temp = 2270 + 6 * i - (i == 3 ? 250 : 0); /* one cold reading makes the day's low */
        ds_set_env(ds, i == 12 ? 2340 : temp, 4500, t, FIX_DAY);
    }
    ds_set_env(ds, 2410, 4500, now - 1200, FIX_DAY); /* the day's high, then back down */
    ds_set_env(ds, 2340, 4500, now, FIX_DAY);
    ds_set_battery(ds, 87, 3921, DS_BAT_DISCHARGING, now);
    ds_set(ds, DS_BAT_DAYS, 85, now);
}

static inline ui_context_t fixture_context(void)
{
    fixture_fill(&s_fix_ds, FIX_NOW);
    ui_context_t ctx = { .now = FIX_NOW, .local = fixture_local(20, 48, 0), .time_valid = true,
                         .local_day = FIX_DAY, .ds = &s_fix_ds, .lang = lang_get("en"), .clock_24h = true };
    return ctx;
}
```

```c
#include <string.h>

#include "context_fixtures.h"
#include "ui_fields.h"
#include "unity.h"

static ui_context_t s_ctx;

void setUp(void)
{
    s_ctx = fixture_context();
}

void tearDown(void) {}

static ui_value_t resolve(ui_field_id_t f)
{
    ui_value_t v;
    ui_resolve(&s_ctx, f, &v);
    return v;
}

static void test_names_map_both_ways(void)
{
    for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
        const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
        TEST_ASSERT_NOT_NULL(info);
        TEST_ASSERT_EQUAL(f, ui_field_by_name(info->id));
    }
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_field_by_name("env.nope"));
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, ui_field_by_name(NULL));
    TEST_ASSERT_NULL(ui_field_info(UI_FIELD_NONE));
    TEST_ASSERT_NULL(ui_field_info(UI_FIELD_COUNT));
}

static void test_temperature_rounds_to_tenths_with_unit_and_trend(void)
{
    ui_value_t v = resolve(UI_FIELD_ENV_TEMP);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("23.4", v.text);
    TEST_ASSERT_EQUAL_STRING("°C", v.unit);
    TEST_ASSERT_EQUAL_INT(1, v.trend); /* +0.7 °C in the last hour, over the 0.5 °C threshold */
    TEST_ASSERT_EQUAL_STRING("Temperature", v.label);
}

static void test_fahrenheit_converts_values(void)
{
    s_ctx.fahrenheit = true;
    ui_value_t v = resolve(UI_FIELD_ENV_TEMP);
    TEST_ASSERT_EQUAL_STRING("74.1", v.text);
    TEST_ASSERT_EQUAL_STRING("°F", v.unit);
}

static void test_humidity_battery_and_days_left(void)
{
    TEST_ASSERT_EQUAL_STRING("45", resolve(UI_FIELD_ENV_HUM).text);
    ui_value_t bat = resolve(UI_FIELD_BAT_LEVEL);
    TEST_ASSERT_EQUAL_STRING("87", bat.text);
    TEST_ASSERT_EQUAL_STRING("3.92 V", bat.extra);
    TEST_ASSERT_EQUAL(DS_BAT_DISCHARGING, bat.battery);
    ui_value_t days = resolve(UI_FIELD_BAT_DAYS);
    TEST_ASSERT_EQUAL_STRING("8.5", days.text);
    TEST_ASSERT_EQUAL_STRING("d", days.unit);
    ds_set(&s_fix_ds, DS_BAT_DAYS, 123, FIX_NOW);
    TEST_ASSERT_EQUAL_STRING("12", resolve(UI_FIELD_BAT_DAYS).text); /* whole days from 10 on */
}

static void test_clock_fields_follow_the_time_and_language(void)
{
    ui_value_t t = resolve(UI_FIELD_TIME_CLOCK);
    TEST_ASSERT_EQUAL_STRING("20:48", t.text);
    TEST_ASSERT_EQUAL_STRING("", t.unit);
    s_ctx.clock_24h = false;
    t = resolve(UI_FIELD_TIME_CLOCK);
    TEST_ASSERT_EQUAL_STRING("8:48", t.text);
    TEST_ASSERT_EQUAL_STRING("PM", t.unit);
    ui_value_t d = resolve(UI_FIELD_DATE_DAY);
    TEST_ASSERT_EQUAL_STRING("Friday 25 September", d.text);
    TEST_ASSERT_EQUAL_STRING("Fri 25 Sep", d.extra);
    TEST_ASSERT_EQUAL_STRING("39", resolve(UI_FIELD_DATE_WEEK).text);
    ui_value_t m = resolve(UI_FIELD_MOON_PHASE);
    TEST_ASSERT_EQUAL_INT(4, m.moon.index); /* full moon on 26 September 2026 */
    TEST_ASSERT_EQUAL_STRING("Full moon", m.text);
    TEST_ASSERT_EQUAL_STRING("Full", m.short_text);
}

static void test_invalid_time_shows_dashes_and_hides_the_date(void)
{
    s_ctx.time_valid = false;
    ui_value_t t = resolve(UI_FIELD_TIME_CLOCK);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, t.state);
    TEST_ASSERT_EQUAL_STRING("--:--", t.text);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_DATE_DAY).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_DATE_WEEK).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_MOON_PHASE).state);
}

static void test_english_has_no_name_days_or_holidays_and_weather_waits_for_m5(void)
{
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_DATE_NAMEDAY).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_DATE_HOLIDAY).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_WX_NOW).state);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_SUN_TIMES).state);
}

static void test_old_readings_are_stale_with_their_age(void)
{
    s_ctx.now = FIX_NOW + 3 * 3600;
    ui_value_t v = resolve(UI_FIELD_ENV_TEMP);
    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
    TEST_ASSERT_EQUAL_UINT32(3 * 3600, v.age_s);
    TEST_ASSERT_EQUAL_STRING("23.4", v.text);
}

static void test_none_resolves_to_missing(void)
{
    ui_value_t v = resolve(UI_FIELD_NONE);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, v.field);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_names_map_both_ways);
    RUN_TEST(test_temperature_rounds_to_tenths_with_unit_and_trend);
    RUN_TEST(test_fahrenheit_converts_values);
    RUN_TEST(test_humidity_battery_and_days_left);
    RUN_TEST(test_clock_fields_follow_the_time_and_language);
    RUN_TEST(test_invalid_time_shows_dashes_and_hides_the_date);
    RUN_TEST(test_english_has_no_name_days_or_holidays_and_weather_waits_for_m5);
    RUN_TEST(test_old_readings_are_stale_with_their_age);
    RUN_TEST(test_none_resolves_to_missing);
    return UNITY_END();
}
```

- [ ] **Step 2: Register it and watch it fail.** In `test/host/CMakeLists.txt`, replace the `ui` library:

```cmake
# ui: pure C on top of gfx, locale and the datastore.
file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
add_library(ui STATIC ${UI_SOURCES})
target_include_directories(ui PUBLIC ${REPO_ROOT}/components/ui/include)
target_compile_options(ui PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(ui PUBLIC gfx locale datastore util)
```

and add after `test_datastore`:

```cmake
reflbo_host_test(test_ui_fields ui)
```

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL to compile: `ui_fields.h` not found.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "datastore.h"
#include "gfx.h"
#include "lang.h"
#include "util_calendar.h"

/*
 * The field catalogue (spec §5.1) and what a widget shows for a field right now. Fields from
 * the datastore carry their freshness; the rest follow from the clock and the language pack.
 * Pure C, host-buildable.
 */

typedef enum {
    UI_FK_TIME,
    UI_FK_DATE,
    UI_FK_NUMBER,
    UI_FK_BATTERY,
    UI_FK_MOON,
    UI_FK_TEXT,
    UI_FK_WEATHER_NOW,
    UI_FK_WEATHER_DAY,
    UI_FK_SERIES,
    UI_FK_SUN,
    UI_FK_COUNT,
} ui_field_kind_t;

#define UI_KIND(k) (1u << (k))

typedef enum {
    UI_FIELD_NONE, /* an empty slot */
    UI_FIELD_TIME_CLOCK,
    UI_FIELD_DATE_DAY,
    UI_FIELD_DATE_WEEK,
    UI_FIELD_ENV_TEMP,
    UI_FIELD_ENV_HUM,
    UI_FIELD_ENV_DEW,
    UI_FIELD_ENV_TEMP_MIN,
    UI_FIELD_ENV_TEMP_MAX,
    UI_FIELD_BAT_LEVEL,
    UI_FIELD_BAT_DAYS,
    UI_FIELD_MOON_PHASE,
    UI_FIELD_DATE_NAMEDAY,
    UI_FIELD_DATE_HOLIDAY,
    UI_FIELD_WX_NOW,    /* M5 */
    UI_FIELD_WX_TODAY,  /* M5 */
    UI_FIELD_WX_HOURLY, /* M5 */
    UI_FIELD_WX_DAILY,  /* M5 */
    UI_FIELD_SUN_TIMES, /* M5 */
    UI_FIELD_COUNT,
} ui_field_id_t;

typedef struct {
    const char *id; /* as in presets.json: "env.temp" */
    ui_field_kind_t kind;
    lang_str_t label;
    int ds_field; /* ds_field_t, or -1 for a field computed from the clock */
} ui_field_info_t;

/* NULL for UI_FIELD_NONE and out-of-range values. */
const ui_field_info_t *ui_field_info(ui_field_id_t field);
/* UI_FIELD_NONE for an unknown id. */
ui_field_id_t ui_field_by_name(const char *id);

/* Everything the dashboard reads, gathered by the app for one render. */
typedef struct {
    time_t now;          /* UTC */
    struct tm local;     /* local time at `now` */
    bool time_valid;     /* false: the RTC oscillator stopped and nobody set the time (spec §5.3) */
    int32_t local_day;   /* days since 1970-01-01 in local time */
    const ds_t *ds;
    const lang_t *lang;
    bool clock_24h;
    bool seconds;
    bool fahrenheit;
} ui_context_t;

typedef enum {
    UI_VALUE_MISSING,
    UI_VALUE_FRESH,
    UI_VALUE_STALE,
} ui_value_state_t;

typedef struct {
    ui_field_id_t field;
    ui_field_kind_t kind;
    ui_value_state_t state;
    uint32_t age_s;    /* how old a stale value is */
    const char *label; /* from the language pack */
    char text[48];     /* the value: "23.4", "20:48", "Friday 25 September", a name */
    char unit[8];      /* "°C", "%", "d", or the AM/PM suffix of a time */
    char extra[24];    /* secondary text: the seconds, the medium date, the illumination, the voltage */
    char short_text[16]; /* a shorter form for small slots: the short phase name */
    int trend;         /* -1 falling, 0 steady or unknown, 1 rising */
    int percent;       /* battery level or moon illumination */
    ds_bat_state_t battery;
    util_moon_t moon;
} ui_value_t;

void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
```

```c
#include "ui_fields.h"

#include <stdio.h>
#include <string.h>

#define TEMP_TREND_C100 50 /* spec §5.1: arrows beyond 0.5 °C or 3 % an hour */
#define HUM_TREND_PCT100 300

static const ui_field_info_t k_fields[UI_FIELD_COUNT] = {
    [UI_FIELD_TIME_CLOCK] = { "time.clock", UI_FK_TIME, LS_TIME, -1 },
    [UI_FIELD_DATE_DAY] = { "date.day", UI_FK_DATE, LS_DATE, -1 },
    [UI_FIELD_DATE_WEEK] = { "date.week", UI_FK_NUMBER, LS_WEEK, -1 },
    [UI_FIELD_ENV_TEMP] = { "env.temp", UI_FK_NUMBER, LS_TEMPERATURE, DS_ENV_TEMP },
    [UI_FIELD_ENV_HUM] = { "env.hum", UI_FK_NUMBER, LS_HUMIDITY, DS_ENV_HUM },
    [UI_FIELD_ENV_DEW] = { "env.dew", UI_FK_NUMBER, LS_DEW_POINT, DS_ENV_DEW },
    [UI_FIELD_ENV_TEMP_MIN] = { "env.temp_min", UI_FK_NUMBER, LS_TODAY_MIN, DS_ENV_TEMP_MIN },
    [UI_FIELD_ENV_TEMP_MAX] = { "env.temp_max", UI_FK_NUMBER, LS_TODAY_MAX, DS_ENV_TEMP_MAX },
    [UI_FIELD_BAT_LEVEL] = { "bat.level", UI_FK_BATTERY, LS_BATTERY, DS_BAT_LEVEL },
    [UI_FIELD_BAT_DAYS] = { "bat.days", UI_FK_NUMBER, LS_BATTERY_DAYS, DS_BAT_DAYS },
    [UI_FIELD_MOON_PHASE] = { "moon.phase", UI_FK_MOON, LS_MOON, -1 },
    [UI_FIELD_DATE_NAMEDAY] = { "date.nameday", UI_FK_TEXT, LS_NAME_DAY, -1 },
    [UI_FIELD_DATE_HOLIDAY] = { "date.holiday", UI_FK_TEXT, LS_HOLIDAY, -1 },
    [UI_FIELD_WX_NOW] = { "wx.now", UI_FK_WEATHER_NOW, LS_WEATHER, -1 },
    [UI_FIELD_WX_TODAY] = { "wx.today", UI_FK_WEATHER_DAY, LS_TODAY, -1 },
    [UI_FIELD_WX_HOURLY] = { "wx.hourly", UI_FK_SERIES, LS_FORECAST, -1 },
    [UI_FIELD_WX_DAILY] = { "wx.daily", UI_FK_SERIES, LS_FORECAST, -1 },
    [UI_FIELD_SUN_TIMES] = { "sun.times", UI_FK_SUN, LS_SUN, -1 },
};

const ui_field_info_t *ui_field_info(ui_field_id_t field)
{
    if (field <= UI_FIELD_NONE || field >= UI_FIELD_COUNT) {
        return NULL;
    }
    return &k_fields[field];
}

ui_field_id_t ui_field_by_name(const char *id)
{
    for (int f = UI_FIELD_NONE + 1; id != NULL && f < UI_FIELD_COUNT; f++) {
        if (strcmp(k_fields[f].id, id) == 0) {
            return (ui_field_id_t)f;
        }
    }
    return UI_FIELD_NONE;
}

static int trend_sign(int32_t trend, int32_t threshold)
{
    if (trend == DS_NO_TREND) {
        return 0;
    }
    return trend > threshold ? 1 : trend < -threshold ? -1 : 0;
}

/* 0.01 °C in the display unit, rounded to 0.1 (half away from zero). */
static long temp_tenths(const ui_context_t *ctx, int32_t c100)
{
    long v = ctx->fahrenheit ? (long)c100 * 9 / 5 + 3200 : c100;
    return (v + (v >= 0 ? 5 : -5)) / 10;
}

static bool from_store(const ui_context_t *ctx, ds_field_t f, ui_value_t *out, ds_entry_t *e)
{
    ds_freshness_t fresh = ds_freshness(ctx->ds, f, ctx->now, ctx->local_day);
    if (fresh == DS_MISSING || !ds_get(ctx->ds, f, e)) {
        return false;
    }
    out->state = fresh == DS_STALE ? UI_VALUE_STALE : UI_VALUE_FRESH;
    out->age_s = (uint32_t)ctx->now > e->updated ? (uint32_t)ctx->now - e->updated : 0;
    return true;
}

static void resolve_temperature(const ui_context_t *ctx, ds_field_t f, ui_value_t *out)
{
    ds_entry_t e;
    if (!from_store(ctx, f, out, &e)) {
        return;
    }
    lang_format_decimal(ctx->lang, temp_tenths(ctx, e.value), 1, out->text, sizeof(out->text));
    snprintf(out->unit, sizeof(out->unit), "%s", ctx->fahrenheit ? "°F" : "°C");
    out->trend = trend_sign(e.trend, TEMP_TREND_C100);
}

static void resolve_store(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    ds_entry_t e;
    switch (field) {
    case UI_FIELD_ENV_TEMP:
    case UI_FIELD_ENV_DEW:
    case UI_FIELD_ENV_TEMP_MIN:
    case UI_FIELD_ENV_TEMP_MAX:
        resolve_temperature(ctx, (ds_field_t)ui_field_info(field)->ds_field, out);
        break;
    case UI_FIELD_ENV_HUM:
        if (from_store(ctx, DS_ENV_HUM, out, &e)) {
            snprintf(out->text, sizeof(out->text), "%ld", (long)(e.value + 50) / 100);
            snprintf(out->unit, sizeof(out->unit), "%%");
            out->trend = trend_sign(e.trend, HUM_TREND_PCT100);
        }
        break;
    case UI_FIELD_BAT_LEVEL:
        if (from_store(ctx, DS_BAT_LEVEL, out, &e)) {
            out->percent = e.value;
            out->battery = (ds_bat_state_t)e.bat_state;
            snprintf(out->text, sizeof(out->text), "%ld", (long)e.value);
            snprintf(out->unit, sizeof(out->unit), "%%");
            char volts[12];
            lang_format_decimal(ctx->lang, (e.mv + 5) / 10, 2, volts, sizeof(volts));
            snprintf(out->extra, sizeof(out->extra), "%s V", volts);
        }
        break;
    case UI_FIELD_BAT_DAYS:
        if (from_store(ctx, DS_BAT_DAYS, out, &e)) {
            if (e.value >= 100) { /* whole days from 10 on */
                snprintf(out->text, sizeof(out->text), "%ld", (long)(e.value + 5) / 10);
            } else {
                lang_format_decimal(ctx->lang, e.value, 1, out->text, sizeof(out->text));
            }
            snprintf(out->unit, sizeof(out->unit), "%s", lang_str(ctx->lang, LS_DAYS_UNIT));
        }
        break;
    default:
        break;
    }
}

static void resolve_clock(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    const struct tm *tm = &ctx->local;
    if (field == UI_FIELD_TIME_CLOCK) {
        out->state = UI_VALUE_FRESH; /* shown even when invalid: "--:--" (spec §5.3) */
        if (!ctx->time_valid) {
            snprintf(out->text, sizeof(out->text), "--:--");
            return;
        }
        const char *suffix;
        lang_format_time(tm->tm_hour, tm->tm_min, tm->tm_sec, ctx->clock_24h, false, out->text, sizeof(out->text),
                         &suffix);
        snprintf(out->unit, sizeof(out->unit), "%s", suffix);
        if (ctx->seconds) {
            snprintf(out->extra, sizeof(out->extra), "%02d", tm->tm_sec);
        }
        return;
    }
    if (!ctx->time_valid) {
        return; /* everything else needs a valid date */
    }
    int year = tm->tm_year + 1900, month = tm->tm_mon + 1, day = tm->tm_mday;
    switch (field) {
    case UI_FIELD_DATE_DAY:
        lang_format_date(ctx->lang, tm, LANG_DATE_LONG, out->text, sizeof(out->text));
        lang_format_date(ctx->lang, tm, LANG_DATE_MEDIUM, out->extra, sizeof(out->extra));
        out->state = out->text[0] ? UI_VALUE_FRESH : UI_VALUE_MISSING;
        break;
    case UI_FIELD_DATE_WEEK:
        snprintf(out->text, sizeof(out->text), "%d", util_iso_week(year, month, day));
        out->state = UI_VALUE_FRESH;
        break;
    case UI_FIELD_MOON_PHASE:
        out->moon = util_moon_phase(ctx->now);
        out->percent = out->moon.illumination;
        snprintf(out->text, sizeof(out->text), "%s", ctx->lang->moon_phases[out->moon.index]);
        snprintf(out->short_text, sizeof(out->short_text), "%s", ctx->lang->moon_phases_short[out->moon.index]);
        snprintf(out->extra, sizeof(out->extra), "%d%%", out->moon.illumination);
        out->state = UI_VALUE_FRESH;
        break;
    case UI_FIELD_DATE_NAMEDAY:
    case UI_FIELD_DATE_HOLIDAY: {
        const char *name = NULL;
        if (field == UI_FIELD_DATE_NAMEDAY && ctx->lang->name_day != NULL) {
            name = ctx->lang->name_day(month, day);
        } else if (field == UI_FIELD_DATE_HOLIDAY && ctx->lang->holiday != NULL) {
            name = ctx->lang->holiday(year, month, day);
        }
        if (name != NULL && name[0] != '\0') {
            snprintf(out->text, sizeof(out->text), "%s", name);
            out->state = UI_VALUE_FRESH;
        }
        break;
    }
    default:
        break;
    }
}

void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    memset(out, 0, sizeof(*out));
    out->field = field;
    const ui_field_info_t *info = ui_field_info(field);
    if (info == NULL) {
        return;
    }
    out->kind = info->kind;
    out->label = lang_str(ctx->lang, info->label);
    if (info->ds_field >= 0) {
        resolve_store(ctx, field, out);
    } else {
        resolve_clock(ctx, field, out); /* weather and sun stay missing until M5 */
    }
}
```

`components/ui/CMakeLists.txt`:

```cmake
# Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
idf_component_register(SRCS "ui_clock.c" "ui_fields.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx locale datastore util)
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ./build-host/test_ui_fields && ctest --test-dir build-host --output-on-failure && tools/idf.sh reconfigure && tools/idf.sh build`
Expected:
- `9 Tests 0 Failures`, and the suite passes.
- The firmware builds with no warnings. `reconfigure` picks up the new `locale` and `datastore` components.

- [ ] **Step 5: Commit.**

```bash
git add components/ui test/host/test_ui_fields.c test/host/context_fixtures.h test/host/CMakeLists.txt
git commit -m "feat(ui): add the field catalogue and resolve fields for widgets"
```

---

### Task 9: Layouts and presets (`ui` presets)

**Files:**
- Create: `components/ui/include/ui_layout.h`, `components/ui/ui_layout.c`, `components/ui/include/ui_preset.h`, `components/ui/ui_preset.c`, `components/ui/ui_preset_json.c`, `test/host/test_ui_preset.c`
- Modify: `components/ui/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `ui_field_id_t`, `ui_field_by_name()`, `ui_field_info()` and `UI_KIND()` (Task 8).
- Produces:
  - Layouts (spec §5.2): `ui_layout_id_t` (classic, weather, grid, focus), `ui_size_t` (S, M, L, XL), `ui_slot_t { name, rect, size, kinds }` and `ui_layout_t`.
  - `ui_layout()`, `ui_layout_by_name()`, `ui_slot_by_name()`, `UI_STATUS_H` (20) and `UI_SLOT_MAX` (6).
  - Presets (spec §5.4): `ui_preset_t` holds the id, name, layout, `in_cycle`, the slots and the options: `clock`, `seconds`, `invert`, `stale_policy`, `status_clock` and `status_battery`.
  - `ui_presets_t { count, active, cycle_enabled, cycle_interval_s, presets[16] }`.
  - `ui_presets_defaults()`: Home, Indoor, Weather (out of the cycle until M5) and Focus clock. Indoor and Weather have the status clock.
  - `ui_presets_find()`, `ui_presets_next()` (cycle order, wrapping), `ui_presets_from_json()` (strict on structure, lenient per option, with a reason on failure) and `ui_presets_to_json()` (0 if the buffer is too small).
- cJSON comes from ESP-IDF (component `json`). The host compiles the same `cJSON.c` from the IDF tree, so both parse alike.

- [ ] **Step 1: Write the failing test.**

```c
#include <stdio.h>
#include <string.h>

#include "ui_fields.h"
#include "ui_preset.h"
#include "unity.h"

static ui_presets_t s_p;
static char s_err[128];
static char s_json[8192];

void setUp(void)
{
    ui_presets_defaults(&s_p);
    s_err[0] = '\0';
}

void tearDown(void) {}

static void test_defaults_are_home_indoor_weather_and_focus(void)
{
    TEST_ASSERT_EQUAL_INT(4, s_p.count);
    TEST_ASSERT_EQUAL_STRING("home", s_p.presets[s_p.active].id);
    TEST_ASSERT_EQUAL(UI_LAYOUT_CLASSIC, s_p.presets[0].layout);
    TEST_ASSERT_EQUAL(UI_FIELD_TIME_CLOCK, s_p.presets[0].slots[0]);
    TEST_ASSERT_EQUAL_INT(2, ui_presets_find(&s_p, "weather"));
    TEST_ASSERT_FALSE(s_p.presets[2].in_cycle);
    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "nope"));
}

static void test_next_follows_cycle_order_and_skips_presets_out_of_it(void)
{
    TEST_ASSERT_EQUAL_INT(1, ui_presets_next(&s_p)); /* home -> indoor */
    s_p.active = 1;
    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p)); /* indoor -> focus, skipping weather */
    s_p.active = 3;
    TEST_ASSERT_EQUAL_INT(0, ui_presets_next(&s_p)); /* wraps */
    for (int i = 0; i < s_p.count; i++) {
        s_p.presets[i].in_cycle = i == 3;
    }
    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p)); /* the only one: stays */
}

static void test_defaults_survive_a_json_round_trip(void)
{
    s_p.active = 3;
    s_p.cycle_enabled = true;
    s_p.cycle_interval_s = 120;
    s_p.presets[3].clock = UI_CLOCK_12H;
    s_p.presets[3].seconds = true;
    s_p.presets[3].stale_policy = UI_STALE_HIDE;
    s_p.presets[3].status_battery = UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
    s_p.presets[3].status_clock = true;
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
}

static void test_the_spec_example_parses(void)
{
    const char *json = "{ \"schema\": 1, \"active\": \"home\", \"cycle\": { \"enabled\": false, \"interval_s\": 60 },"
                       "  \"presets\": [ { \"id\": \"home\", \"name\": \"Home\", \"layout\": \"classic\", \"in_cycle\": true,"
                       "    \"slots\": { \"main\": \"time.clock\", \"sub\": \"date.day\", \"s1\": \"env.temp\","
                       "                 \"s2\": \"env.hum\", \"s3\": \"wx.now\", \"s4\": \"bat.level\" },"
                       "    \"options\": { \"clock_24h\": true, \"seconds\": false, \"invert\": false,"
                       "                   \"stale_policy\": \"stale\" } } ] }";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT(1, s_p.count);
    TEST_ASSERT_EQUAL(UI_FIELD_WX_NOW, s_p.presets[0].slots[4]);
    TEST_ASSERT_EQUAL(UI_CLOCK_24H, s_p.presets[0].clock);
}

static void check_rejected(const char *json, const char *reason_part)
{
    s_err[0] = '\0';
    TEST_ASSERT_FALSE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), json);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_err, reason_part), s_err);
}

static void test_invalid_files_are_rejected_with_a_reason(void)
{
    check_rejected("not json", "not valid JSON");
    check_rejected("{\"schema\": 2, \"presets\": []}", "schema");
    check_rejected("{\"schema\": 1, \"presets\": []}", "1-16");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"round\"}]}", "unknown layout");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"Big Id\", \"layout\": \"grid\"}]}", "preset id");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"slots\": {\"g7\": \"env.temp\"}}]}",
                   "no slot \"g7\"");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"slots\": {\"g1\": \"env.cold\"}}]}",
                   "unknown field");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"classic\", \"slots\": {\"main\": \"bat.level\"}}]}",
                   "can't show");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\"}, {\"id\": \"a\", \"layout\": \"grid\"}]}",
                   "duplicate");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"options\": {\"stale_policy\": \"x\"}}]}",
                   "stale_policy");
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"options\": {\"status_battery\": [\"amps\"]}}]}",
                   "status_battery");
}

static void test_too_many_presets_are_rejected(void)
{
    size_t n = (size_t)snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"presets\": [");
    for (int i = 0; i < UI_PRESET_MAX + 1; i++) {
        n += (size_t)snprintf(s_json + n, sizeof(s_json) - n, "%s{\"id\": \"p%d\", \"layout\": \"grid\"}", i ? "," : "", i);
    }
    snprintf(s_json + n, sizeof(s_json) - n, "]}");
    check_rejected(s_json, "1-16");
}

static void test_lenient_parts_fall_back_to_defaults(void)
{
    const char *json = "{\"schema\": 1, \"active\": \"gone\", \"cycle\": {\"enabled\": true, \"interval_s\": 3},"
                       " \"presets\": [{\"id\": \"a\", \"layout\": \"focus\", \"slots\": {\"s1\": null, \"s2\": \"\"}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_INT(0, s_p.active);                 /* unknown active: the first */
    TEST_ASSERT_EQUAL_UINT16(UI_CYCLE_MIN_S, s_p.cycle_interval_s); /* clamped to 10 s */
    TEST_ASSERT_TRUE(s_p.presets[0].in_cycle);
    TEST_ASSERT_EQUAL_STRING("a", s_p.presets[0].name);  /* no name: the id */
    TEST_ASSERT_EQUAL(UI_CLOCK_DEFAULT, s_p.presets[0].clock);
    TEST_ASSERT_EQUAL(UI_FIELD_NONE, s_p.presets[0].slots[1]);
    TEST_ASSERT_FALSE(s_p.presets[0].status_clock);
    TEST_ASSERT_EQUAL_HEX8(UI_STATUS_BAT_PERCENT, s_p.presets[0].status_battery);
}

static void test_status_bar_options_parse(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"options\":"
                       " {\"status_clock\": true, \"status_battery\": [\"days\", \"voltage\"]}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_TRUE(s_p.presets[0].status_clock);
    TEST_ASSERT_EQUAL_HEX8(UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS, s_p.presets[0].status_battery);
}

static void test_json_that_does_not_fit_the_buffer_returns_zero(void)
{
    TEST_ASSERT_EQUAL_UINT(0, ui_presets_to_json(&s_p, s_json, 64));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_home_indoor_weather_and_focus);
    RUN_TEST(test_next_follows_cycle_order_and_skips_presets_out_of_it);
    RUN_TEST(test_defaults_survive_a_json_round_trip);
    RUN_TEST(test_the_spec_example_parses);
    RUN_TEST(test_invalid_files_are_rejected_with_a_reason);
    RUN_TEST(test_too_many_presets_are_rejected);
    RUN_TEST(test_lenient_parts_fall_back_to_defaults);
    RUN_TEST(test_status_bar_options_parse);
    RUN_TEST(test_json_that_does_not_fit_the_buffer_returns_zero);
    return UNITY_END();
}
```

- [ ] **Step 2: Register it and watch it fail.** In `test/host/CMakeLists.txt`, before the `ui` library:

```cmake
# cJSON: the copy bundled with ESP-IDF, so the host tests parse exactly like the firmware.
set(REFLBO_IDF_PATH "$ENV{REFLBO_IDF_PATH}")
if(NOT REFLBO_IDF_PATH)
    set(REFLBO_IDF_PATH "$ENV{HOME}/esp/esp-idf-v5.5.5")
endif()
add_library(cjson STATIC ${REFLBO_IDF_PATH}/components/json/cJSON/cJSON.c)
target_include_directories(cjson PUBLIC ${REFLBO_IDF_PATH}/components/json/cJSON)
target_compile_options(cjson PRIVATE -Wno-deprecated-declarations) # its sprintf calls; third-party code
target_link_libraries(cjson PUBLIC m)
```

The `ui` library links it:

```cmake
target_link_libraries(ui PUBLIC gfx locale datastore util PRIVATE cjson m)
```

and after `test_ui_fields`:

```cmake
reflbo_host_test(test_ui_preset ui)
```

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL to compile: `ui_preset.h` not found.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stdint.h>

#include "gfx.h"

/* Layouts (spec §5.2): fixed slot rectangles below a 20 px status bar. Pure C, host-buildable. */

#define UI_STATUS_H 20
#define UI_SLOT_MAX 6

typedef enum {
    UI_LAYOUT_CLASSIC,
    UI_LAYOUT_WEATHER,
    UI_LAYOUT_GRID,
    UI_LAYOUT_FOCUS,
    UI_LAYOUT_COUNT,
} ui_layout_id_t;

typedef enum {
    UI_SIZE_S,
    UI_SIZE_M,
    UI_SIZE_L,
    UI_SIZE_XL,
} ui_size_t;

typedef struct {
    const char *name; /* as in presets.json: "main", "s1" */
    gfx_rect_t rect;
    ui_size_t size;
    uint32_t kinds; /* UI_KIND() bits of the field kinds the slot accepts */
} ui_slot_t;

typedef struct {
    const char *id; /* "classic" */
    const ui_slot_t *slots;
    int slot_count;
} ui_layout_t;

const ui_layout_t *ui_layout(ui_layout_id_t id); /* NULL if out of range */
int ui_layout_by_name(const char *id);           /* -1 if unknown */
int ui_slot_by_name(const ui_layout_t *layout, const char *name);
```

```c
#include "ui_layout.h"

#include <string.h>

#include "ui_fields.h"

/* Slot rectangles for 400x300 below the 20 px status bar (spec §5.2), tuned on host renders. */

#define K_ANY_SMALL                                                                                                  \
    (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_DATE) | UI_KIND(UI_FK_NUMBER) | UI_KIND(UI_FK_BATTERY) |                    \
     UI_KIND(UI_FK_MOON) | UI_KIND(UI_FK_TEXT) | UI_KIND(UI_FK_WEATHER_NOW) | UI_KIND(UI_FK_WEATHER_DAY) |           \
     UI_KIND(UI_FK_SUN))
#define K_ANY_MEDIUM (K_ANY_SMALL | UI_KIND(UI_FK_SERIES))
#define K_LARGE (K_ANY_SMALL)
#define K_XL (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_NUMBER))

static const ui_slot_t k_classic[] = {
    { "main", { 0, 21, 400, 125 }, UI_SIZE_XL, K_XL },
    { "sub", { 0, 146, 400, 40 }, UI_SIZE_M, UI_KIND(UI_FK_DATE) | UI_KIND(UI_FK_TEXT) },
    { "s1", { 0, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
    { "s2", { 100, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
    { "s3", { 200, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
    { "s4", { 300, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
};

static const ui_slot_t k_weather[] = {
    { "now", { 0, 21, 200, 160 }, UI_SIZE_L, K_LARGE },
    { "today", { 200, 21, 200, 80 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "hourly", { 200, 101, 200, 80 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "s1", { 0, 182, 200, 118 }, UI_SIZE_S, K_ANY_SMALL },
    { "s2", { 200, 182, 200, 118 }, UI_SIZE_S, K_ANY_SMALL },
};

static const ui_slot_t k_grid[] = {
    { "g1", { 0, 21, 133, 139 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "g2", { 133, 21, 134, 139 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "g3", { 267, 21, 133, 139 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "g4", { 0, 160, 133, 140 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "g5", { 133, 160, 134, 140 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "g6", { 267, 160, 133, 140 }, UI_SIZE_M, K_ANY_MEDIUM },
};

static const ui_slot_t k_focus[] = {
    { "main", { 0, 21, 400, 190 }, UI_SIZE_XL, K_XL },
    { "s1", { 0, 212, 200, 88 }, UI_SIZE_M, K_ANY_MEDIUM },
    { "s2", { 200, 212, 200, 88 }, UI_SIZE_M, K_ANY_MEDIUM },
};

static const ui_layout_t k_layouts[UI_LAYOUT_COUNT] = {
    [UI_LAYOUT_CLASSIC] = { "classic", k_classic, sizeof(k_classic) / sizeof(k_classic[0]) },
    [UI_LAYOUT_WEATHER] = { "weather", k_weather, sizeof(k_weather) / sizeof(k_weather[0]) },
    [UI_LAYOUT_GRID] = { "grid", k_grid, sizeof(k_grid) / sizeof(k_grid[0]) },
    [UI_LAYOUT_FOCUS] = { "focus", k_focus, sizeof(k_focus) / sizeof(k_focus[0]) },
};

const ui_layout_t *ui_layout(ui_layout_id_t id)
{
    return (unsigned)id < UI_LAYOUT_COUNT ? &k_layouts[id] : NULL;
}

int ui_layout_by_name(const char *id)
{
    for (int i = 0; id != NULL && i < UI_LAYOUT_COUNT; i++) {
        if (strcmp(k_layouts[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int ui_slot_by_name(const ui_layout_t *layout, const char *name)
{
    for (int i = 0; layout != NULL && name != NULL && i < layout->slot_count; i++) {
        if (strcmp(layout->slots[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}
```

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_layout.h"

/*
 * Presets (spec §5.4): a layout, its slot bindings and options, stored in /cfg/presets.json.
 * Pure C, host-buildable.
 */

#define UI_PRESET_MAX 16
#define UI_PRESET_ID_LEN 16   /* with the terminator */
#define UI_PRESET_NAME_LEN 24
#define UI_CYCLE_MIN_S 10
#define UI_CYCLE_MAX_S 3600

typedef enum {
    UI_STALE_STALE,       /* show the value with its age (the default) */
    UI_STALE_PLACEHOLDER, /* show "—" */
    UI_STALE_HIDE,        /* leave the slot empty */
} ui_stale_policy_t;

/* What the status bar shows beside the battery icon (option status_battery). */
enum {
    UI_STATUS_BAT_PERCENT = 1u << 0,
    UI_STATUS_BAT_VOLTAGE = 1u << 1,
    UI_STATUS_BAT_DAYS = 1u << 2,
};

typedef enum {
    UI_CLOCK_DEFAULT, /* follow the time.clock_24h setting */
    UI_CLOCK_24H,
    UI_CLOCK_12H,
} ui_clock_mode_t;

typedef struct {
    char id[UI_PRESET_ID_LEN];
    char name[UI_PRESET_NAME_LEN];
    uint8_t layout; /* ui_layout_id_t */
    bool in_cycle;
    uint8_t slots[UI_SLOT_MAX]; /* ui_field_id_t per slot, in the layout's slot order */
    uint8_t clock;              /* ui_clock_mode_t */
    bool seconds;
    bool invert;
    uint8_t stale_policy;   /* ui_stale_policy_t */
    bool status_clock;      /* a small clock in the middle of the status bar */
    uint8_t status_battery; /* UI_STATUS_BAT_* bits */
} ui_preset_t;

typedef struct {
    uint8_t count;
    uint8_t active; /* index into presets */
    bool cycle_enabled;
    uint16_t cycle_interval_s;
    ui_preset_t presets[UI_PRESET_MAX];
} ui_presets_t;

/* The built-in presets (spec §5.4): used when presets.json is missing or invalid. */
void ui_presets_defaults(ui_presets_t *p);
int ui_presets_find(const ui_presets_t *p, const char *id); /* index, or -1 */
/* The next preset in cycle order after the active one, wrapping; the active one if no other
 * preset is in the cycle (spec §5.4, KEY short). */
int ui_presets_next(const ui_presets_t *p);
/* Parses and validates presets.json. On failure returns false with a reason in `err` and leaves
 * *out unspecified. */
bool ui_presets_from_json(const char *json, ui_presets_t *out, char *err, size_t err_size);
/* Serialises; returns the length written, or 0 if `size` is too small. */
size_t ui_presets_to_json(const ui_presets_t *p, char *out, size_t size);
```

```c
#include "ui_preset.h"

#include <stdio.h>
#include <string.h>

#include "ui_fields.h"

static ui_preset_t make(const char *id, const char *name, ui_layout_id_t layout, bool in_cycle,
                        const ui_field_id_t slots[UI_SLOT_MAX])
{
    ui_preset_t p = { .layout = (uint8_t)layout, .in_cycle = in_cycle, .stale_policy = UI_STALE_STALE,
                      .status_battery = UI_STATUS_BAT_PERCENT };
    snprintf(p.id, sizeof(p.id), "%s", id);
    snprintf(p.name, sizeof(p.name), "%s", name);
    for (int i = 0; i < UI_SLOT_MAX; i++) {
        p.slots[i] = (uint8_t)slots[i];
    }
    return p;
}

void ui_presets_defaults(ui_presets_t *p)
{
    memset(p, 0, sizeof(*p));
    /* Weather has nothing to show until M5, so it stays out of the cycle until then. */
    p->presets[0] = make("home", "Home", UI_LAYOUT_CLASSIC, true,
                         (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_TIME_CLOCK, UI_FIELD_DATE_DAY, UI_FIELD_ENV_TEMP,
                                                       UI_FIELD_ENV_HUM, UI_FIELD_MOON_PHASE, UI_FIELD_BAT_LEVEL });
    p->presets[1] = make("indoor", "Indoor", UI_LAYOUT_GRID, true,
                         (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_ENV_TEMP, UI_FIELD_ENV_HUM, UI_FIELD_ENV_DEW,
                                                       UI_FIELD_ENV_TEMP_MIN, UI_FIELD_ENV_TEMP_MAX,
                                                       UI_FIELD_BAT_DAYS });
    p->presets[2] = make("weather", "Weather", UI_LAYOUT_WEATHER, false,
                         (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_WX_NOW, UI_FIELD_WX_TODAY, UI_FIELD_WX_HOURLY,
                                                       UI_FIELD_ENV_TEMP, UI_FIELD_ENV_HUM, UI_FIELD_NONE });
    p->presets[3] = make("focus", "Focus clock", UI_LAYOUT_FOCUS, true,
                         (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_TIME_CLOCK, UI_FIELD_DATE_DAY, UI_FIELD_ENV_TEMP,
                                                       UI_FIELD_NONE, UI_FIELD_NONE, UI_FIELD_NONE });
    p->presets[1].status_clock = true; /* data first: the time goes to the status bar */
    p->presets[2].status_clock = true;
    p->count = 4;
    p->active = 0;
    p->cycle_enabled = false;
    p->cycle_interval_s = 60;
}

int ui_presets_find(const ui_presets_t *p, const char *id)
{
    for (int i = 0; id != NULL && i < p->count; i++) {
        if (strcmp(p->presets[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int ui_presets_next(const ui_presets_t *p)
{
    for (int step = 1; step < p->count; step++) {
        int i = (p->active + step) % p->count;
        if (p->presets[i].in_cycle) {
            return i;
        }
    }
    return p->active;
}
```

```c
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "ui_fields.h"
#include "ui_preset.h"

#define SCHEMA 1

static const char *const k_policies[] = { [UI_STALE_STALE] = "stale", [UI_STALE_PLACEHOLDER] = "placeholder",
                                          [UI_STALE_HIDE] = "hide" };
static const char *const k_battery_parts[] = { "percent", "voltage", "days" }; /* UI_STATUS_BAT_* bit order */

static bool fail(char *err, size_t size, const char *fmt, ...)
{
    if (size > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, size, fmt, ap);
        va_end(ap);
    }
    return false;
}

/* Ids travel in console commands and HA select options: keep them short and plain. */
static bool valid_id(const char *id)
{
    size_t n = strlen(id);
    if (n == 0 || n >= UI_PRESET_ID_LEN) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

static bool optional_bool(const cJSON *obj, const char *key, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

static bool parse_slots(const cJSON *slots, const ui_layout_t *layout, ui_preset_t *out, char *err, size_t size)
{
    const cJSON *slot;
    cJSON_ArrayForEach(slot, slots)
    {
        int index = ui_slot_by_name(layout, slot->string);
        if (index < 0) {
            return fail(err, size, "preset \"%s\": layout %s has no slot \"%s\"", out->id, layout->id, slot->string);
        }
        if (cJSON_IsNull(slot) || (cJSON_IsString(slot) && slot->valuestring[0] == '\0')) {
            continue; /* explicitly empty */
        }
        if (!cJSON_IsString(slot)) {
            return fail(err, size, "preset \"%s\": slot %s needs a field id", out->id, slot->string);
        }
        ui_field_id_t field = ui_field_by_name(slot->valuestring);
        if (field == UI_FIELD_NONE) {
            return fail(err, size, "preset \"%s\": unknown field \"%s\"", out->id, slot->valuestring);
        }
        if (!(layout->slots[index].kinds & UI_KIND(ui_field_info(field)->kind))) {
            return fail(err, size, "preset \"%s\": slot %s can't show %s", out->id, slot->string, slot->valuestring);
        }
        out->slots[index] = (uint8_t)field;
    }
    return true;
}

static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t size)
{
    memset(out, 0, sizeof(*out));
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    if (!cJSON_IsString(id) || !valid_id(id->valuestring)) {
        return fail(err, size, "a preset id must be 1-%d characters of a-z, 0-9, - or _", UI_PRESET_ID_LEN - 1);
    }
    snprintf(out->id, sizeof(out->id), "%s", id->valuestring);
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
    if (cJSON_IsString(name)) {
        if (strlen(name->valuestring) >= UI_PRESET_NAME_LEN) {
            return fail(err, size, "preset \"%s\": name longer than %d bytes", out->id, UI_PRESET_NAME_LEN - 1);
        }
        snprintf(out->name, sizeof(out->name), "%s", name->valuestring);
    } else {
        snprintf(out->name, sizeof(out->name), "%s", out->id);
    }
    const cJSON *layout_name = cJSON_GetObjectItemCaseSensitive(item, "layout");
    int layout = cJSON_IsString(layout_name) ? ui_layout_by_name(layout_name->valuestring) : -1;
    if (layout < 0) {
        return fail(err, size, "preset \"%s\": unknown layout", out->id);
    }
    out->layout = (uint8_t)layout;
    out->in_cycle = optional_bool(item, "in_cycle", true);
    const cJSON *slots = cJSON_GetObjectItemCaseSensitive(item, "slots");
    if (slots != NULL && !parse_slots(slots, ui_layout((ui_layout_id_t)layout), out, err, size)) {
        return false;
    }
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(item, "options");
    const cJSON *h24 = cJSON_GetObjectItemCaseSensitive(options, "clock_24h");
    out->clock = cJSON_IsBool(h24) ? (cJSON_IsTrue(h24) ? UI_CLOCK_24H : UI_CLOCK_12H) : UI_CLOCK_DEFAULT;
    out->seconds = optional_bool(options, "seconds", false);
    out->invert = optional_bool(options, "invert", false);
    out->stale_policy = UI_STALE_STALE;
    const cJSON *policy = cJSON_GetObjectItemCaseSensitive(options, "stale_policy");
    if (policy != NULL) {
        int found = -1;
        for (int i = 0; cJSON_IsString(policy) && i < (int)(sizeof(k_policies) / sizeof(k_policies[0])); i++) {
            if (strcmp(policy->valuestring, k_policies[i]) == 0) {
                found = i;
            }
        }
        if (found < 0) {
            return fail(err, size, "preset \"%s\": stale_policy must be stale, placeholder or hide", out->id);
        }
        out->stale_policy = (uint8_t)found;
    }
    out->status_clock = optional_bool(options, "status_clock", false);
    out->status_battery = UI_STATUS_BAT_PERCENT;
    const cJSON *battery = cJSON_GetObjectItemCaseSensitive(options, "status_battery");
    if (battery != NULL) {
        if (!cJSON_IsArray(battery)) {
            return fail(err, size, "preset \"%s\": status_battery must be a list", out->id);
        }
        out->status_battery = 0;
        const cJSON *part;
        cJSON_ArrayForEach(part, battery)
        {
            int bit = -1;
            for (int i = 0; cJSON_IsString(part) && i < 3; i++) {
                if (strcmp(part->valuestring, k_battery_parts[i]) == 0) {
                    bit = i;
                }
            }
            if (bit < 0) {
                return fail(err, size, "preset \"%s\": status_battery takes percent, voltage and days", out->id);
            }
            out->status_battery |= (uint8_t)(1u << bit);
        }
    }
    return true;
}

static bool parse(const cJSON *root, ui_presets_t *out, char *err, size_t size)
{
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    if (!cJSON_IsNumber(schema) || schema->valueint != SCHEMA) {
        return fail(err, size, "schema must be %d", SCHEMA);
    }
    const cJSON *presets = cJSON_GetObjectItemCaseSensitive(root, "presets");
    int n = cJSON_IsArray(presets) ? cJSON_GetArraySize(presets) : 0;
    if (n < 1 || n > UI_PRESET_MAX) {
        return fail(err, size, "presets must hold 1-%d entries", UI_PRESET_MAX);
    }
    memset(out, 0, sizeof(*out));
    const cJSON *item;
    cJSON_ArrayForEach(item, presets)
    {
        ui_preset_t *p = &out->presets[out->count];
        if (!parse_preset(item, p, err, size)) {
            return false;
        }
        if (ui_presets_find(out, p->id) >= 0) {
            return fail(err, size, "duplicate preset id \"%s\"", p->id);
        }
        out->count++;
    }
    const cJSON *active = cJSON_GetObjectItemCaseSensitive(root, "active");
    int index = cJSON_IsString(active) ? ui_presets_find(out, active->valuestring) : -1;
    out->active = (uint8_t)(index < 0 ? 0 : index);
    const cJSON *cycle = cJSON_GetObjectItemCaseSensitive(root, "cycle");
    out->cycle_enabled = optional_bool(cycle, "enabled", false);
    const cJSON *interval = cJSON_GetObjectItemCaseSensitive(cycle, "interval_s");
    int seconds = cJSON_IsNumber(interval) ? interval->valueint : 60;
    out->cycle_interval_s = (uint16_t)(seconds < UI_CYCLE_MIN_S ? UI_CYCLE_MIN_S
                                       : seconds > UI_CYCLE_MAX_S ? UI_CYCLE_MAX_S
                                                                  : seconds);
    return true;
}

bool ui_presets_from_json(const char *json, ui_presets_t *out, char *err, size_t err_size)
{
    cJSON *root = json != NULL ? cJSON_Parse(json) : NULL;
    if (root == NULL) {
        return fail(err, err_size, "not valid JSON");
    }
    bool ok = parse(root, out, err, err_size);
    cJSON_Delete(root);
    return ok;
}

static cJSON *preset_json(const ui_preset_t *p)
{
    const ui_layout_t *layout = ui_layout((ui_layout_id_t)p->layout);
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id", p->id);
    cJSON_AddStringToObject(obj, "name", p->name);
    cJSON_AddStringToObject(obj, "layout", layout->id);
    cJSON_AddBoolToObject(obj, "in_cycle", p->in_cycle);
    cJSON *slots = cJSON_AddObjectToObject(obj, "slots");
    for (int i = 0; i < layout->slot_count; i++) {
        const ui_field_info_t *info = ui_field_info((ui_field_id_t)p->slots[i]);
        if (info != NULL) {
            cJSON_AddStringToObject(slots, layout->slots[i].name, info->id);
        }
    }
    cJSON *options = cJSON_AddObjectToObject(obj, "options");
    if (p->clock != UI_CLOCK_DEFAULT) {
        cJSON_AddBoolToObject(options, "clock_24h", p->clock == UI_CLOCK_24H);
    }
    cJSON_AddBoolToObject(options, "seconds", p->seconds);
    cJSON_AddBoolToObject(options, "invert", p->invert);
    cJSON_AddStringToObject(options, "stale_policy", k_policies[p->stale_policy < 3 ? p->stale_policy : 0]);
    cJSON_AddBoolToObject(options, "status_clock", p->status_clock);
    cJSON *battery = cJSON_AddArrayToObject(options, "status_battery");
    for (int i = 0; i < 3; i++) {
        if (p->status_battery & (1u << i)) {
            cJSON_AddItemToArray(battery, cJSON_CreateString(k_battery_parts[i]));
        }
    }
    return obj;
}

size_t ui_presets_to_json(const ui_presets_t *p, char *out, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema", SCHEMA);
    cJSON_AddStringToObject(root, "active", p->count ? p->presets[p->active].id : "");
    cJSON *cycle = cJSON_AddObjectToObject(root, "cycle");
    cJSON_AddBoolToObject(cycle, "enabled", p->cycle_enabled);
    cJSON_AddNumberToObject(cycle, "interval_s", p->cycle_interval_s);
    cJSON *presets = cJSON_AddArrayToObject(root, "presets");
    for (int i = 0; i < p->count; i++) {
        cJSON_AddItemToArray(presets, preset_json(&p->presets[i]));
    }
    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, true);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}
```

`components/ui/CMakeLists.txt`:

```cmake
# Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
idf_component_register(SRCS "ui_clock.c" "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx locale datastore util
                       PRIV_REQUIRES json)
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_ui_preset && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `9 Tests 0 Failures`; the suite passes; the firmware builds with no warnings.

- [ ] **Step 5: Commit.**

```bash
git add components/ui test/host/test_ui_preset.c test/host/CMakeLists.txt
git commit -m "feat(ui): add the four layouts, the built-in presets and their JSON codec"
```

---

### Task 10: Widgets, status bar and dashboard (`ui` rendering)

**Files:**
- Create:
  - `components/ui/ui_internal.h`, `components/ui/ui_widget.c`, `components/ui/ui_status.c`, `components/ui/include/ui_dashboard.h`, `components/ui/ui_dashboard.c`
  - `test/host/dashboard_fixtures.h`, `test/host/test_ui_dashboard_golden.c`, `test/host/render_dashboard.c`
  - `test/host/golden/dash_*.pbm` (generated)
- Modify: `components/ui/CMakeLists.txt`, `test/host/CMakeLists.txt`, `tools/render.py`

**Interfaces:**
- Consumes:
  - Fields and the context (Task 8); layouts and presets (Task 9).
  - The fonts and icons (Task 3); `gfx_bitmap()`, `gfx_fill_circle()` and `gfx_text_ellipsize()` (Task 2).
- Produces: `void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset)`. It is a pure function of its inputs (spec §5.5): the status bar, then each slot's widget in its rectangle, inverted if the preset says so.
- Widgets, per slot size:
  - S: bold 28 over a sans 16 label, with a 24 px icon.
  - M: `num_cb_48` with a sans_12 label.
  - L: `num_cb_72`.
  - XL: `num_cb_130`, stepping down to 110, 72 and 48 until the text fits the slot, including the seconds column.
- Widget details:
  - Text that doesn't fit is ellipsized. A widget never draws outside its rectangle (clipped).
  - The trend arrow goes beside the label; in narrow S cells, beside the icon.
  - A stale value shows the stale icon and its age at the bottom right, or follows the preset's `stale_policy`.
- The status bar (20 px):
  - "Set time" on the left while the clock is invalid, otherwise a stale warning when a shown value is stale.
  - An optional clock in the middle (`status_clock`).
  - The charging bolt and the battery, with the percentage, voltage and days left that `status_battery` selects.

- [ ] **Step 1: Write the failing test and the renderer.** The fixtures cover the four built-in presets and seven edge cases: invalid time, stale data, 12 h and charging, below zero in °F with the placeholder policy, seconds, the full battery details, and inverted.

```c
#pragma once

#include <string.h>

#include "context_fixtures.h"
#include "ui_dashboard.h"

/* The dashboard fixtures for the golden renders (test_ui_dashboard_golden.c, render_dashboard.c):
 * a context and a preset each, on top of context_fixtures.h. */

static inline ui_preset_t fixture_preset(const char *id)
{
    ui_presets_t all;
    ui_presets_defaults(&all);
    int i = ui_presets_find(&all, id);
    return all.presets[i < 0 ? 0 : i];
}

/* Each fixture: a context and a preset. */
static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_preset_t *preset)
{
    *ctx = fixture_context();
    if (strcmp(name, "home") == 0 || strcmp(name, "indoor") == 0 || strcmp(name, "weather") == 0 ||
        strcmp(name, "focus") == 0) {
        *preset = fixture_preset(name);
    } else if (strcmp(name, "home_invalid") == 0) { /* RTC stopped, no readings yet (spec §5.3) */
        *preset = fixture_preset("home");
        ctx->time_valid = false;
        ds_init(&s_fix_ds);
    } else if (strcmp(name, "home_stale") == 0) { /* readings 3 h old */
        *preset = fixture_preset("home");
        fixture_fill(&s_fix_ds, FIX_NOW - 3 * 3600);
    } else if (strcmp(name, "home_12h_charging") == 0) {
        *preset = fixture_preset("home");
        ctx->clock_24h = false;
        ds_set_battery(&s_fix_ds, 64, 4012, DS_BAT_CHARGING, FIX_NOW);
    } else if (strcmp(name, "indoor_cold") == 0) { /* below zero, °F, placeholder policy for stale */
        *preset = fixture_preset("indoor");
        ds_init(&s_fix_ds);
        ds_set_env(&s_fix_ds, -50, 8000, FIX_NOW, FIX_DAY);
        ctx->fahrenheit = true;
        preset->stale_policy = UI_STALE_PLACEHOLDER;
    } else if (strcmp(name, "focus_seconds") == 0) {
        *preset = fixture_preset("focus");
        preset->seconds = true;
        ctx->local = fixture_local(20, 48, 37);
    } else if (strcmp(name, "home_battery_details") == 0) { /* level, voltage and days in the status bar */
        *preset = fixture_preset("home");
        preset->status_clock = true;
        preset->status_battery = UI_STATUS_BAT_PERCENT | UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
    } else if (strcmp(name, "home_inverted") == 0) {
        *preset = fixture_preset("home");
        preset->invert = true;
    } else {
        return false;
    }
    return true;
}

static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather", "focus", "home_invalid",
                                                    "home_stale", "home_12h_charging", "indoor_cold",
                                                    "focus_seconds", "home_battery_details", "home_inverted" };
```

```c
#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "unity.h"

/* Each fixture must match test/host/golden/dash_<name>.pbm byte for byte. After an intentional
 * change: build-host/render_dashboard <name> test/host/golden/dash_<name>.pbm for each fixture,
 * look at the PNGs (python3 tools/render.py), commit. */

static uint8_t s_buf[400 * 300 / 8];
static uint8_t s_pbm[16000];
static uint8_t s_golden[16000];

void setUp(void) {}
void tearDown(void) {}

static void check(const char *name)
{
    ui_context_t ctx;
    ui_preset_t preset;
    TEST_ASSERT_TRUE_MESSAGE(fixture_dashboard(name, &ctx, &preset), name);
    gfx_fb_t fb;
    gfx_fb_init(&fb, s_buf, 400, 300);
    ui_draw_dashboard(&fb, &ctx, &preset);
    size_t n = gfx_pbm_encode(&fb, s_pbm, sizeof(s_pbm));

    char path[256];
    snprintf(path, sizeof(path), "%s/dash_%s.pbm", GOLDEN_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    size_t golden = fread(s_golden, 1, sizeof(s_golden), f);
    fclose(f);
    if (golden != n || memcmp(s_golden, s_pbm, n) != 0) {
        char actual[64];
        snprintf(actual, sizeof(actual), "dash_%s.actual.pbm", name);
        FILE *out = fopen(actual, "wb");
        if (out != NULL) {
            fwrite(s_pbm, 1, n, out);
            fclose(out);
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)n, (int)golden, path);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_golden, s_pbm, n, path);
}

static void test_every_fixture_matches_its_golden(void)
{
    for (size_t i = 0; i < sizeof(k_dashboard_fixtures) / sizeof(k_dashboard_fixtures[0]); i++) {
        check(k_dashboard_fixtures[i]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_fixture_matches_its_golden);
    return UNITY_END();
}
```

```c
#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"

/* build-host/render_dashboard <fixture> <out.pbm>: one dashboard fixture as PBM (tools/render.py). */
int main(int argc, char **argv)
{
    static uint8_t buf[400 * 300 / 8];
    static uint8_t pbm[16000];
    ui_context_t ctx;
    ui_preset_t preset;
    if (argc != 3 || !fixture_dashboard(argv[1], &ctx, &preset)) {
        fprintf(stderr, "usage: render_dashboard <fixture> <out.pbm>\n");
        return 2;
    }
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    ui_draw_dashboard(&fb, &ctx, &preset);
    size_t n = gfx_pbm_encode(&fb, pbm, sizeof(pbm));
    FILE *f = fopen(argv[2], "wb");
    if (f == NULL || fwrite(pbm, 1, n, f) != n) {
        return 1;
    }
    fclose(f);
    return 0;
}
```

In `test/host/CMakeLists.txt`, after the `render_clock` executable:

```cmake
reflbo_host_test(test_ui_dashboard_golden ui)
target_compile_definitions(test_ui_dashboard_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(render_dashboard render_dashboard.c)
target_compile_options(render_dashboard PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(render_dashboard PRIVATE ui)
```

- [ ] **Step 2: Watch it fail.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL to compile: `ui_dashboard.h` not found.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include "gfx.h"
#include "ui_fields.h"
#include "ui_layout.h"
#include "ui_preset.h"

/* Shared by the ui sources; not part of the component's API. */

void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, ui_stale_policy_t policy,
                    const lang_t *lang);
void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, bool any_stale);
/* A battery outline w x h with a nub, filled to `pct` (no fill if pct < 0). */
void ui_draw_battery(gfx_fb_t *fb, int x, int y, int w, int h, int pct);
/* The Moon's disc in a thin outline, its shadow inked. */
void ui_draw_moon(gfx_fb_t *fb, int cx, int cy, int r, double age);
```

```c
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "gfx_icons.h"
#include "ui_internal.h"

#define PLACEHOLDER "\xE2\x80\x94" /* em dash */
#define ARROW_UP "\xE2\x86\x91"
#define ARROW_DOWN "\xE2\x86\x93"
#define PI 3.14159265358979323846

typedef struct {
    const gfx_font_t *label; /* NULL: no label */
    const gfx_font_t *value; /* numbers */
    const gfx_font_t *text;  /* words, and "—" */
    const gfx_font_t *unit;
    int icon; /* icon size in px */
} ui_fonts_t;

static const ui_fonts_t k_fonts[] = {
    [UI_SIZE_S] = { NULL, &gfx_font_sans_bold_28, &gfx_font_sans_bold_16, &gfx_font_sans_16, 24 },
    [UI_SIZE_M] = { &gfx_font_sans_12, &gfx_font_num_cb_48, &gfx_font_sans_bold_20, &gfx_font_sans_16, 48 },
    [UI_SIZE_L] = { &gfx_font_sans_16, &gfx_font_num_cb_72, &gfx_font_sans_bold_28, &gfx_font_sans_bold_20, 48 },
    [UI_SIZE_XL] = { &gfx_font_sans_16, &gfx_font_num_cb_130, &gfx_font_sans_bold_28, &gfx_font_sans_bold_28, 48 },
};

/* Height of a digit's ink, for centring numbers on what shows rather than on the line box. */
static int digit_height(const gfx_font_t *f)
{
    const gfx_glyph_t *g = gfx_font_glyph(f, '0');
    return g != NULL ? g->height : f->ascent;
}

static const gfx_bitmap_t *field_icon(ui_field_id_t field, int size)
{
    const gfx_bitmap_t *s24 = NULL, *s48 = NULL;
    switch (field) {
    case UI_FIELD_ENV_TEMP:
    case UI_FIELD_ENV_TEMP_MIN:
    case UI_FIELD_ENV_TEMP_MAX:
        s24 = &gfx_icon_thermometer_24, s48 = &gfx_icon_thermometer_48;
        break;
    case UI_FIELD_ENV_HUM:
        s24 = &gfx_icon_drop_24, s48 = &gfx_icon_drop_48;
        break;
    case UI_FIELD_ENV_DEW:
        s24 = &gfx_icon_dew_24, s48 = &gfx_icon_dew_48;
        break;
    case UI_FIELD_TIME_CLOCK:
        s24 = &gfx_icon_clock_24, s48 = &gfx_icon_clock_48;
        break;
    case UI_FIELD_DATE_DAY:
    case UI_FIELD_DATE_WEEK:
        s24 = &gfx_icon_calendar_24, s48 = &gfx_icon_calendar_48;
        break;
    case UI_FIELD_DATE_NAMEDAY:
        s24 = &gfx_icon_person_24, s48 = &gfx_icon_person_48;
        break;
    case UI_FIELD_DATE_HOLIDAY:
        s24 = &gfx_icon_celebration_24, s48 = &gfx_icon_celebration_48;
        break;
    case UI_FIELD_WX_NOW:
    case UI_FIELD_WX_TODAY:
    case UI_FIELD_WX_HOURLY:
    case UI_FIELD_WX_DAILY:
    case UI_FIELD_SUN_TIMES:
        s24 = &gfx_icon_cloud_24, s48 = &gfx_icon_cloud_48;
        break;
    default:
        break;
    }
    return size >= 48 ? s48 : s24;
}

/* Draws value, unit and trend arrow as one group centred on cx; returns the group's width. */
static int group_width(const ui_fonts_t *f, const gfx_font_t *vf, const ui_value_t *v, const char *value)
{
    int w = gfx_text_width(vf, value);
    if (v->unit[0]) {
        w += 2 + gfx_text_width(f->unit, v->unit);
    }
    if (v->trend) {
        w += 2 + gfx_text_width(f->unit, ARROW_UP);
    }
    return w;
}

static void draw_group(gfx_fb_t *fb, const ui_fonts_t *f, const gfx_font_t *vf, const ui_value_t *v,
                       const char *value, int x, int baseline)
{
    int pen = gfx_text(fb, vf, x, baseline, value, GFX_BLACK);
    if (v->unit[0]) {
        pen = gfx_text(fb, f->unit, pen + 2, baseline, v->unit, GFX_BLACK);
    }
    if (v->trend) {
        gfx_text(fb, f->unit, pen + 2, baseline, v->trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
    }
}

static void format_age(const lang_t *lang, uint32_t age_s, char *out, size_t size)
{
    if (age_s < 3600) {
        snprintf(out, size, "%lu %s", (unsigned long)(age_s / 60), lang_str(lang, LS_MINUTES_UNIT));
    } else if (age_s < 86400) {
        snprintf(out, size, "%lu %s", (unsigned long)(age_s / 3600), lang_str(lang, LS_HOURS_UNIT));
    } else {
        snprintf(out, size, "%lu %s", (unsigned long)(age_s / 86400), lang_str(lang, LS_DAYS_UNIT));
    }
}

/* "⟲ 2 h" in the rect's top-right corner (spec §5.3). */
static void draw_age(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v, const lang_t *lang)
{
    char age[16];
    format_age(lang, v->age_s, age, sizeof(age));
    const gfx_font_t *f = &gfx_font_sans_12;
    int w = gfx_text_width(f, age);
    int x = r.x + r.w - 6 - w;
    int baseline = r.y + r.h - 6 - (f->line_height - f->ascent);
    gfx_text(fb, f, x, baseline, age, GFX_BLACK);
    gfx_bitmap(fb, x - 18, baseline - 13, &gfx_icon_stale_16, GFX_BLACK);
}

static void draw_min_max_mark(gfx_fb_t *fb, const ui_value_t *v, int x, int y)
{
    if (v->field == UI_FIELD_ENV_TEMP_MIN || v->field == UI_FIELD_ENV_TEMP_MAX) {
        gfx_text(fb, &gfx_font_sans_bold_16, x, y, v->field == UI_FIELD_ENV_TEMP_MIN ? ARROW_DOWN : ARROW_UP,
                 GFX_BLACK);
    }
}

/* The small visual that stands for the field: an icon, a battery, or the Moon. */
static int draw_symbol(gfx_fb_t *fb, const ui_value_t *v, int x, int y, int size)
{
    if (v->kind == UI_FK_BATTERY) {
        int w = size * 3 / 2, h = size * 3 / 4;
        ui_draw_battery(fb, x, y + (size - h) / 2, w, h, v->state == UI_VALUE_MISSING ? -1 : v->percent);
        if (v->battery == DS_BAT_CHARGING) {
            gfx_bitmap(fb, x + w + 2, y + (size - 16) / 2, &gfx_icon_bolt_16, GFX_BLACK);
            return w + 18;
        }
        return w;
    }
    if (v->kind == UI_FK_MOON) {
        int r = size / 2 - 1;
        if (v->state == UI_VALUE_MISSING) {
            gfx_circle(fb, x + size / 2, y + size / 2, r, GFX_BLACK);
        } else {
            ui_draw_moon(fb, x + size / 2, y + size / 2, r, v->moon.age);
        }
        return size;
    }
    const gfx_bitmap_t *icon = field_icon(v->field, size);
    if (icon == NULL) {
        return 0;
    }
    gfx_bitmap(fb, x, y, icon, GFX_BLACK);
    draw_min_max_mark(fb, v, x + icon->width - 6, y + icon->height);
    return icon->width;
}

/* The value as text: the number for most kinds, a word or name otherwise. */
static const char *display_text(const ui_value_t *v, ui_size_t size)
{
    if (v->state == UI_VALUE_MISSING) {
        return PLACEHOLDER;
    }
    switch (v->kind) {
    case UI_FK_DATE:
        return size == UI_SIZE_S ? v->extra : v->text;
    case UI_FK_MOON:
        return v->text;
    default:
        return v->text;
    }
}

static bool numeric(const ui_value_t *v)
{
    return v->state != UI_VALUE_MISSING &&
           (v->kind == UI_FK_NUMBER || v->kind == UI_FK_TIME || v->kind == UI_FK_BATTERY);
}

static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
{
    const ui_fonts_t *f = &k_fonts[UI_SIZE_S];
    const gfx_font_t *vf = numeric(v) ? f->value : v->state == UI_VALUE_MISSING ? f->value : f->text;
    const char *value = display_text(v, UI_SIZE_S);
    ui_value_t shown = *v;
    if (!numeric(v)) {
        shown.unit[0] = '\0';
        shown.trend = 0;
    }
    char fit[48];
    if (r.w < 150) { /* narrow: symbol above, value below, the trend arrow beside the symbol */
        int sym_size = v->kind == UI_FK_MOON ? 28 : f->icon;
        int sym_w = v->kind == UI_FK_BATTERY ? sym_size * 3 / 2 : sym_size;
        int sym_x = r.x + (r.w - sym_w) / 2;
        draw_symbol(fb, v, sym_x, r.y + 12, sym_size);
        if (shown.trend) {
            gfx_text(fb, &gfx_font_sans_bold_16, sym_x + sym_w + 4, r.y + 12 + sym_size - 4,
                     shown.trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
            shown.trend = 0;
        }
        if (v->kind == UI_FK_MOON && v->state != UI_VALUE_MISSING) { /* the phase name, small */
            const char *name = gfx_text_width(&gfx_font_sans_12, v->text) <= r.w - 8 ? v->text : v->short_text;
            gfx_text_ellipsize(&gfx_font_sans_12, name, r.w - 8, fit, sizeof(fit));
            gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ r.x, (int16_t)(r.y + 12 + sym_size + 14), r.w, 20 },
                             GFX_ALIGN_CENTER, fit, GFX_BLACK);
            return;
        }
        int max_w = r.w - 8;
        if (!numeric(v)) {
            gfx_text_ellipsize(vf, value, max_w, fit, sizeof(fit));
            value = fit;
        }
        int w = group_width(f, vf, &shown, value);
        int baseline = r.y + 12 + f->icon + 14 + digit_height(vf);
        draw_group(fb, f, vf, &shown, value, r.x + (r.w - w) / 2, baseline);
        return;
    }
    int sym_w = draw_symbol(fb, v, r.x + 14, r.y + (r.h - f->icon) / 2, f->icon); /* wide: side by side */
    int x = r.x + 14 + sym_w + 10;
    if (!numeric(v)) {
        gfx_text_ellipsize(vf, value, r.x + r.w - 6 - x, fit, sizeof(fit));
        value = fit;
    }
    draw_group(fb, f, vf, &shown, value, x, r.y + (r.h + digit_height(vf)) / 2);
}

static void draw_labelled(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    const ui_fonts_t *f = &k_fonts[size];
    int top = r.y + 6;
    if (f->label != NULL && v->kind != UI_FK_TIME && !(size == UI_SIZE_M && v->kind == UI_FK_DATE)) {
        char label[40];
        int arrow_w = v->trend ? gfx_text_width(f->label, ARROW_UP) + 4 : 0;
        gfx_text_ellipsize(f->label, v->label, r.w - 12 - arrow_w, label, sizeof(label));
        int pen = gfx_text(fb, f->label, r.x + 6, top + f->label->ascent, label, GFX_BLACK);
        if (v->trend) { /* beside the label, where it doesn't widen the value */
            gfx_text(fb, f->label, pen + 4, top + f->label->ascent, v->trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
        }
        top += f->label->line_height;
    }
    gfx_rect_t body = { r.x, (int16_t)top, r.w, (int16_t)(r.y + r.h - top) };
    char fit[48];
    const char *value = display_text(v, size);

    if (v->kind == UI_FK_MOON && v->state != UI_VALUE_MISSING) { /* disc, then the phase name */
        int d = size == UI_SIZE_M ? 40 : 64;
        int cx = body.x + 10 + d / 2, cy = body.y + body.h / 2;
        ui_draw_moon(fb, cx, cy, d / 2 - 1, v->moon.age);
        int x = body.x + 20 + d;
        gfx_text_ellipsize(&gfx_font_sans_16, v->text, body.x + body.w - 6 - x, fit, sizeof(fit));
        gfx_text(fb, &gfx_font_sans_16, x, cy - 2, fit, GFX_BLACK);
        gfx_text(fb, &gfx_font_sans_bold_20, x, cy + 22, v->extra, GFX_BLACK);
        return;
    }
    if (!numeric(v)) { /* words: centred, cut to fit */
        const gfx_font_t *tf = v->state == UI_VALUE_MISSING ? &gfx_font_sans_bold_28 : f->text;
        if (v->kind == UI_FK_DATE && size == UI_SIZE_M) {
            tf = &gfx_font_sans_20;
            if (gfx_text_width(tf, value) > body.w - 12) {
                value = v->extra; /* the medium form: "Fri 25 Sep" */
            }
        }
        gfx_text_ellipsize(tf, value, body.w - 12, fit, sizeof(fit));
        gfx_text_in_rect(fb, tf, body, GFX_ALIGN_CENTER, fit, GFX_BLACK);
        return;
    }
    const gfx_font_t *vf = f->value;
    ui_value_t shown = *v;
    shown.trend = 0; /* drawn beside the label */
    if (v->kind == UI_FK_TIME) {
        shown.unit[0] = '\0'; /* AM/PM and seconds go beside the digits, smaller */
    }
    const gfx_font_t *side = size == UI_SIZE_XL ? &gfx_font_sans_bold_28 : &gfx_font_sans_bold_20;
    int extra_w = 0;
    if (v->kind == UI_FK_TIME) { /* AM/PM and the seconds share one column beside the digits */
        int unit_w = v->unit[0] ? gfx_text_width(side, v->unit) : 0;
        int sec_w = v->extra[0] ? gfx_text_width(side, v->extra) : 0;
        extra_w = unit_w || sec_w ? 6 + (unit_w > sec_w ? unit_w : sec_w) : 0;
    }
    int w = group_width(f, vf, &shown, value);
    while (w + extra_w > body.w - 8 && vf != &gfx_font_num_cb_48) { /* too wide: step down a size */
        vf = vf == &gfx_font_num_cb_130 ? &gfx_font_num_cb_110
             : vf == &gfx_font_num_cb_110 ? &gfx_font_num_cb_72
                                          : &gfx_font_num_cb_48;
        w = group_width(f, vf, &shown, value);
    }
    int x = body.x + (body.w - w - extra_w) / 2;
    int extra_lines = v->kind == UI_FK_BATTERY ? f->unit->line_height : 0;
    int baseline = body.y + (body.h + digit_height(vf) - extra_lines) / 2;
    draw_group(fb, f, vf, &shown, value, x, baseline);
    if (v->kind == UI_FK_TIME) {
        int sx = x + w + 6;
        if (v->extra[0]) { /* seconds, level with the top of the digits */
            gfx_text(fb, side, sx, baseline - digit_height(vf) + side->ascent, v->extra, GFX_BLACK);
        }
        if (v->unit[0]) {
            gfx_text(fb, side, sx, baseline, v->unit, GFX_BLACK);
        }
    }
    if (v->kind == UI_FK_BATTERY) {
        int tw = gfx_text_width(f->unit, v->extra);
        gfx_text(fb, f->unit, body.x + (body.w - tw) / 2, baseline + f->unit->line_height, v->extra, GFX_BLACK);
    }
}

void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, ui_stale_policy_t policy,
                    const lang_t *lang)
{
    if (v->field == UI_FIELD_NONE) {
        return;
    }
    ui_value_t shown = *v;
    if (v->state == UI_VALUE_STALE && policy != UI_STALE_STALE) {
        shown.state = UI_VALUE_MISSING;
    }
    if (shown.state == UI_VALUE_MISSING && policy == UI_STALE_HIDE) {
        return;
    }
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, r));
    if (size == UI_SIZE_S) {
        draw_small(fb, r, &shown);
    } else {
        draw_labelled(fb, r, size, &shown);
    }
    if (shown.state == UI_VALUE_STALE) {
        draw_age(fb, r, &shown, lang);
    }
    fb->clip = saved;
}

void ui_draw_battery(gfx_fb_t *fb, int x, int y, int w, int h, int pct)
{
    int nub = w / 10 < 2 ? 2 : w / 10;
    gfx_rect(fb, (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)(w - nub), (int16_t)h }, GFX_BLACK);
    gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x + w - nub), (int16_t)(y + h / 4), (int16_t)nub, (int16_t)(h - h / 2) },
                  GFX_BLACK);
    if (pct >= 0) {
        int inner = w - nub - 4;
        int fill = (pct > 100 ? 100 : pct) * inner / 100;
        gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x + 2), (int16_t)(y + 2), (int16_t)fill, (int16_t)(h - 4) },
                      GFX_BLACK);
    }
}

void ui_draw_moon(gfx_fb_t *fb, int cx, int cy, int r, double age)
{
    /* The shadow is inked, like the monochrome moon symbols: new is a black disc, full an empty
     * circle. Row by row, the terminator sits at half-width * cos(2 pi age); waxing, the lit part
     * is on the right (as seen from the northern hemisphere). */
    double c = cos(2.0 * PI * age);
    bool waxing = age < 0.5;
    for (int dy = -r; dy <= r; dy++) {
        double half = sqrt((double)(r * r - dy * dy));
        double t = half * c;
        int x0 = (int)lround(waxing ? cx - half : cx - t);
        int x1 = (int)lround(waxing ? cx + t : cx + half);
        if (x1 > x0) {
            gfx_hline(fb, x0, cy + dy, x1 - x0 + 1, GFX_BLACK);
        }
    }
    gfx_circle(fb, cx, cy, r, GFX_BLACK);
}
```

```c
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "gfx_icons.h"
#include "ui_internal.h"

/* Status bar (spec §5.2): "Set time" or a stale warning on the left, an optional clock in the
 * middle, the battery on the right with its level, voltage or days left as the preset asks. */
void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, bool any_stale)
{
    if (!ctx->time_valid) {
        const gfx_font_t *f = &gfx_font_sans_bold_16;
        const char *text = lang_str(ctx->lang, LS_SET_TIME);
        int w = gfx_text_width(f, text) + 12;
        gfx_fill_rect(fb, (gfx_rect_t){ 0, 0, (int16_t)w, UI_STATUS_H }, GFX_BLACK);
        gfx_text_in_rect(fb, f, (gfx_rect_t){ 6, 0, (int16_t)(w - 6), UI_STATUS_H }, GFX_ALIGN_LEFT, text, GFX_WHITE);
    } else if (any_stale) {
        gfx_bitmap(fb, 4, 2, &gfx_icon_stale_16, GFX_BLACK);
    }

    if (preset->status_clock && ctx->time_valid) {
        ui_value_t t;
        ui_resolve(ctx, UI_FIELD_TIME_CLOCK, &t);
        char clock[sizeof(t.text) + sizeof(t.unit) + 1];
        snprintf(clock, sizeof(clock), "%s%s%s", t.text, t.unit[0] ? " " : "", t.unit);
        gfx_text_in_rect(fb, &gfx_font_sans_bold_16, (gfx_rect_t){ 120, 0, 160, UI_STATUS_H }, GFX_ALIGN_CENTER,
                         clock, GFX_BLACK);
    }

    ui_value_t bat, days;
    ui_resolve(ctx, UI_FIELD_BAT_LEVEL, &bat);
    ui_resolve(ctx, UI_FIELD_BAT_DAYS, &days);
    int x = fb->width - 6 - 26;
    ui_draw_battery(fb, x, 5, 26, 11, bat.state == UI_VALUE_MISSING ? -1 : bat.percent);
    if (bat.battery == DS_BAT_CHARGING) {
        x -= 16;
        gfx_bitmap(fb, x, 2, &gfx_icon_bolt_16, GFX_BLACK);
    }
    char text[sizeof(bat.text) + sizeof(bat.extra) + sizeof(days.text) + sizeof(days.unit) + 12] = "";
    size_t n = 0;
    uint8_t parts = preset->status_battery;
    if (bat.state == UI_VALUE_MISSING) {
        snprintf(text, sizeof(text), "\xE2\x80\x94");
    } else {
        if (parts & UI_STATUS_BAT_PERCENT) {
            n += (size_t)snprintf(text + n, sizeof(text) - n, "%s%%", bat.text);
        }
        if ((parts & UI_STATUS_BAT_VOLTAGE) && n < sizeof(text)) {
            n += (size_t)snprintf(text + n, sizeof(text) - n, "%s%s", n ? "  " : "", bat.extra);
        }
        if ((parts & UI_STATUS_BAT_DAYS) && days.state != UI_VALUE_MISSING && n < sizeof(text)) {
            snprintf(text + n, sizeof(text) - n, "%s%s %s", n ? "  " : "", days.text, days.unit);
        }
    }
    gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ (int16_t)(x - 124), 0, 120, UI_STATUS_H }, GFX_ALIGN_RIGHT,
                     text, GFX_BLACK);
    gfx_hline(fb, 0, UI_STATUS_H, fb->width, GFX_BLACK);
}
```

```c
#pragma once

#include "gfx.h"
#include "ui_fields.h"
#include "ui_preset.h"

/* The dashboard screen (spec §5.5): the active preset's layout under the status bar. Rendering
 * is a pure function of the context and the preset. Pure C, host-buildable. */
void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset);
```

```c
#include "ui_dashboard.h"

#include "ui_internal.h"

static void draw_separators(gfx_fb_t *fb, ui_layout_id_t layout)
{
    switch (layout) {
    case UI_LAYOUT_CLASSIC:
        gfx_hline(fb, 12, 188, 376, GFX_BLACK);
        for (int i = 1; i < 4; i++) {
            gfx_vline(fb, i * 100, 199, 90, GFX_BLACK);
        }
        break;
    case UI_LAYOUT_WEATHER:
        gfx_vline(fb, 200, 29, 144, GFX_BLACK);
        gfx_hline(fb, 208, 101, 184, GFX_BLACK);
        gfx_hline(fb, 12, 181, 376, GFX_BLACK);
        gfx_vline(fb, 200, 190, 102, GFX_BLACK);
        break;
    case UI_LAYOUT_GRID:
        gfx_vline(fb, 133, 29, 263, GFX_BLACK);
        gfx_vline(fb, 267, 29, 263, GFX_BLACK);
        gfx_hline(fb, 8, 160, 384, GFX_BLACK);
        break;
    case UI_LAYOUT_FOCUS:
        gfx_hline(fb, 12, 211, 376, GFX_BLACK);
        gfx_vline(fb, 200, 220, 72, GFX_BLACK);
        break;
    default:
        break;
    }
}

void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset)
{
    ui_context_t c = *ctx;
    if (preset->clock != UI_CLOCK_DEFAULT) {
        c.clock_24h = preset->clock == UI_CLOCK_24H;
    }
    c.seconds = preset->seconds;

    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    const ui_layout_t *layout = ui_layout((ui_layout_id_t)preset->layout);
    bool any_stale = false;
    if (layout != NULL) {
        draw_separators(fb, (ui_layout_id_t)preset->layout);
        for (int i = 0; i < layout->slot_count; i++) {
            ui_value_t v;
            ui_resolve(&c, (ui_field_id_t)preset->slots[i], &v);
            any_stale |= v.state == UI_VALUE_STALE;
            ui_widget_draw(fb, layout->slots[i].rect, layout->slots[i].size, &v,
                           (ui_stale_policy_t)preset->stale_policy, c.lang);
        }
    }
    ui_status_draw(fb, &c, preset, any_stale);
    if (preset->invert) {
        gfx_clear(fb, GFX_INVERT);
    }
}
```

`components/ui/CMakeLists.txt`:

```cmake
# Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
idf_component_register(SRCS "ui_clock.c" "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                            "ui_status.c" "ui_dashboard.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx locale datastore util
                       PRIV_REQUIRES json)
```

- [ ] **Step 4: Watch the golden test fail for the right reason.**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure -R dashboard`
Expected: FAIL: `.../golden/dash_home.pbm` could not be opened. There are no goldens yet.

- [ ] **Step 5: Generate the goldens and check them against the spike's renders.** The owner approved the renders on 2026-09-28 and asked for the status-bar options; `home_battery_details` and the status clocks on Indoor and Weather were added afterwards, and they go to the owner with the M3a report. The renderer is deterministic, so the files must match byte for byte:

```bash
for n in home indoor weather focus home_invalid home_stale home_12h_charging indoor_cold focus_seconds \
         home_battery_details home_inverted; do
    ./build-host/render_dashboard $n test/host/golden/dash_$n.pbm
done
shasum -a 256 test/host/golden/dash_*.pbm
```

Expected:

```text
c9f96b5bcb70d25bb77398c9a770a0e13b0c4f1fd0f97b8ee8d65d8e26691ff2  test/host/golden/dash_focus_seconds.pbm
b131388c40cc8a449cd0cad5af60e4e24bc137ceea693c6804b953625443f521  test/host/golden/dash_focus.pbm
9f47926ac14f7d0510b5a97b8b7d0b1f6bb2ae01d166e8e7782c6efa3a9c87ba  test/host/golden/dash_home_12h_charging.pbm
814c22371a925eed9f8815065c5539afb56d8e35cbce6cbccfc9b8d76eae64ab  test/host/golden/dash_home_battery_details.pbm
d5e0fcc7c4a65d557bc10b916261070b9efbab4f76dab6d39b25013884829b8b  test/host/golden/dash_home_invalid.pbm
3f8b45da084df8eea57b4377aabcf9cc4f359f68f82e77374c9741e28c85e418  test/host/golden/dash_home_inverted.pbm
ee7af6c2044104568ee664458295469e2ebbed9e20d7e09286ae3c8184a0a5d1  test/host/golden/dash_home_stale.pbm
26cf16104617a05c87279f5d2092e2de307dc043428d3740e3b7fef852f7a80e  test/host/golden/dash_home.pbm
73a8e998ff10ff4e79f7761f6f2df4a72010651dbf4000ed8439a4efa3f6748f  test/host/golden/dash_indoor_cold.pbm
eababed772ec57c7440880f42a854d4b44dfdf6ab0ba0e996c8bfd1076ff388c  test/host/golden/dash_indoor.pbm
8a2ad049f0d4e74a7efd2b4cc27797c1e1e723c14a6588b424674bb6267385ab  test/host/golden/dash_weather.pbm
```

A mismatch means a font, icon or rendering difference. Find it before going on: these renders are what the owner reviews.

- [ ] **Step 6: Add the renders to `tools/render.py`, then look at them.** Until Task 13 removes the clock screen, the table keeps both:

```python
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
```

Run: `python3 tools/render.py && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected:
- `captures/render/dash_*.png` are written. Open each one and check that no text crosses a slot border.
- The suite passes and the firmware builds.

- [ ] **Step 7: Commit.**

```bash
git add components/ui test/host/dashboard_fixtures.h test/host/test_ui_dashboard_golden.c test/host/render_dashboard.c \
        test/host/golden/dash_*.pbm test/host/CMakeLists.txt tools/render.py
git commit -m "feat(ui): render dashboards: widgets, status bar and golden fixtures"
```


---

### Task 11: Config files (`storage`)

**Files:**
- Create:
  - `components/storage/CMakeLists.txt`, `components/storage/idf_component.yml`
  - `components/storage/include/settings.h`, `components/storage/settings.c`
  - `components/storage/include/storage_file.h`, `components/storage/storage_file.c`
  - `components/storage/include/storage.h`, `components/storage/storage.c`
  - `test/host/test_settings.c`, `test/host/test_storage_file.c`
  - `dependencies.lock` (generated)
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: cJSON (Task 9's host library; IDF component `json`).
- Produces:
  - `settings_t`: language, `clock_24h`, `tz_posix`, `tz_iana`, `fahrenheit`, `sensors_every_min`, `temp_offset_c100`, `hum_offset_pct100`, `display_every_min`, `lpm_quarter_hz`.
  - `settings_from_json(json, defaults, out, err, size)`: fails only on text that isn't a JSON object with `"schema": 1`. A missing or mistyped key takes its default, and out-of-range numbers are clamped.
  - `settings_to_json(s, base_json, out, size)`: keeps the keys this firmware doesn't know.
  - `storage_parse_t`; `storage_file_load()` with its `.bak` fallback and `STORAGE_REJECTED_*` bits; `storage_file_write_atomic()` (spec §14.3). They are plain POSIX, so the host tests them in a scratch directory.
  - `storage_init()`: mounts LittleFS at `/fs` and formats a blank partition (spec §14.1); `storage_ready()`.
  - `storage_load()` and `storage_write_atomic()`: the file functions on the mounted partition, returning `esp_err_t` and logging rejected files.
  - `STORAGE_SETTINGS_PATH` (`/fs/cfg/settings.json`) and `STORAGE_PRESETS_PATH` (`/fs/cfg/presets.json`).
- LittleFS comes from the ESP Component Registry: `joltwallet/littlefs`, pinned `==1.22.3`.

- [ ] **Step 1: Write the failing settings test.**

```c
#include <string.h>

#include "settings.h"
#include "unity.h"

static settings_t s_defaults, s_out;
static char s_err[96];
static char s_json[2048];

void setUp(void)
{
    s_defaults = (settings_t){ .language = "en", .clock_24h = true, .tz_posix = "CET-1CEST,M3.5.0,M10.5.0/3",
                               .tz_iana = "Europe/Prague", .sensors_every_min = 5, .display_every_min = 1,
                               .lpm_quarter_hz = 4 };
    memset(&s_out, 0xAA, sizeof(s_out));
    s_err[0] = '\0';
}

void tearDown(void) {}

static void test_the_spec_sketch_parses(void)
{
    const char *json =
        "{ \"schema\": 1, \"language\": \"en\","
        "  \"location\": { \"name\": \"Brno\", \"lat\": 49.1951, \"lon\": 16.6068 },"
        "  \"time\": { \"tz_iana\": \"Europe/London\", \"tz_posix\": \"GMT0BST,M3.5.0/1,M10.5.0\","
        "            \"clock_24h\": false, \"ntp\": [\"cz.pool.ntp.org\"] },"
        "  \"units\": { \"temp\": \"F\" },"
        "  \"sensors\": { \"interval_min\": 10, \"temp_offset_c\": -3.5, \"hum_offset_pct\": 2.25 },"
        "  \"display\": { \"contrast\": \"default\", \"update_min\": 2, \"lpm_hz\": 0.25 } }";
    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_STRING("Europe/London", s_out.tz_iana);
    TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", s_out.tz_posix);
    TEST_ASSERT_FALSE(s_out.clock_24h);
    TEST_ASSERT_TRUE(s_out.fahrenheit);
    TEST_ASSERT_EQUAL_UINT8(10, s_out.sensors_every_min);
    TEST_ASSERT_EQUAL_INT16(-350, s_out.temp_offset_c100);
    TEST_ASSERT_EQUAL_INT16(225, s_out.hum_offset_pct100);
    TEST_ASSERT_EQUAL_UINT8(2, s_out.display_every_min);
    TEST_ASSERT_EQUAL_UINT8(1, s_out.lpm_quarter_hz);
}

static void test_missing_keys_take_the_defaults(void)
{
    TEST_ASSERT_TRUE(settings_from_json("{\"schema\": 1}", &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_MEMORY(&s_defaults, &s_out, sizeof(s_out));
}

static void test_bad_values_are_clamped_or_ignored_one_by_one(void)
{
    const char *json = "{\"schema\": 1, \"time\": {\"clock_24h\": \"yes\", \"tz_posix\": 5},"
                       " \"sensors\": {\"interval_min\": 99, \"temp_offset_c\": -50},"
                       " \"display\": {\"update_min\": 0, \"lpm_hz\": 3}, \"language\": \"much too long\"}";
    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(s_out.clock_24h);                      /* wrong type: default */
    TEST_ASSERT_EQUAL_STRING(s_defaults.tz_posix, s_out.tz_posix);
    TEST_ASSERT_EQUAL_UINT8(30, s_out.sensors_every_min);   /* clamped */
    TEST_ASSERT_EQUAL_INT16(-1000, s_out.temp_offset_c100); /* clamped to -10 °C */
    TEST_ASSERT_EQUAL_UINT8(1, s_out.display_every_min);
    TEST_ASSERT_EQUAL_UINT8(4, s_out.lpm_quarter_hz);       /* 3 Hz is not a panel rate */
    TEST_ASSERT_EQUAL_STRING("en", s_out.language);
}

static void test_not_json_or_another_schema_fails(void)
{
    TEST_ASSERT_FALSE(settings_from_json("{", &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("not a JSON object", s_err);
    TEST_ASSERT_FALSE(settings_from_json("[1]", &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_FALSE(settings_from_json("{\"schema\": 2}", &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("schema must be 1", s_err);
}

static void test_saving_keeps_keys_this_firmware_does_not_know(void)
{
    const char *base = "{\"schema\": 1, \"mqtt\": {\"host\": \"ha.local\"}, \"time\": {\"ntp\": [\"a\"], \"clock_24h\": true}}";
    settings_t s = s_defaults;
    s.clock_24h = false;
    s.lpm_quarter_hz = 32;
    TEST_ASSERT_TRUE(settings_to_json(&s, base, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_json, "ha.local"));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"ntp\""));
    TEST_ASSERT_TRUE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_FALSE(s_out.clock_24h);
    TEST_ASSERT_EQUAL_UINT8(32, s_out.lpm_quarter_hz);
}

static void test_saving_without_a_base_round_trips(void)
{
    settings_t s = s_defaults;
    s.fahrenheit = true;
    s.temp_offset_c100 = -125;
    TEST_ASSERT_TRUE(settings_to_json(&s, NULL, s_json, sizeof(s_json)) > 0);
    TEST_ASSERT_TRUE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_MEMORY(&s, &s_out, sizeof(s));
    TEST_ASSERT_EQUAL_UINT(0, settings_to_json(&s, NULL, s_json, 16));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_spec_sketch_parses);
    RUN_TEST(test_missing_keys_take_the_defaults);
    RUN_TEST(test_bad_values_are_clamped_or_ignored_one_by_one);
    RUN_TEST(test_not_json_or_another_schema_fails);
    RUN_TEST(test_saving_keeps_keys_this_firmware_does_not_know);
    RUN_TEST(test_saving_without_a_base_round_trips);
    return UNITY_END();
}
```

- [ ] **Step 2: Register it and watch it fail.** In `test/host/CMakeLists.txt`, after the `cjson` library:

```cmake
# storage: the settings codec and the config-file logic build on the host; the mount does not.
add_library(storage_logic STATIC ${REPO_ROOT}/components/storage/settings.c)
target_include_directories(storage_logic PUBLIC ${REPO_ROOT}/components/storage/include)
target_compile_options(storage_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(storage_logic PRIVATE cjson m)
```

and after `test_ui_preset`:

```cmake
reflbo_host_test(test_settings storage_logic)
```

Run: `cmake -S test/host -B build-host -G Ninja`
Expected: FAIL: `components/storage/settings.c` does not exist.

- [ ] **Step 3: Implement the codec.**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Non-secret settings in /cfg/settings.json (spec §14.3). Missing or mistyped keys take the
 * defaults the caller passes (built from Kconfig), and out-of-range numbers are clamped, so one
 * bad value never resets the rest. Keys this firmware doesn't know are kept when saving. Pure C
 * on cJSON, host-buildable.
 */

#define SETTINGS_TZ_POSIX_LEN 64
#define SETTINGS_TZ_IANA_LEN 40

typedef struct {
    char language[4];                    /* "en" */
    bool clock_24h;
    char tz_posix[SETTINGS_TZ_POSIX_LEN]; /* "CET-1CEST,M3.5.0,M10.5.0/3" */
    char tz_iana[SETTINGS_TZ_IANA_LEN];   /* "Europe/Prague", shown to the user */
    bool fahrenheit;
    uint8_t sensors_every_min; /* 1..30 */
    int16_t temp_offset_c100;  /* -1000..1000 */
    int16_t hum_offset_pct100; /* -2000..2000 */
    uint8_t display_every_min; /* 1..15 */
    uint8_t lpm_quarter_hz;    /* panel refresh in LPM, 0.25 Hz steps: 1, 2, 4, 8, 16 or 32 */
} settings_t;

/* Fails only if the text is not a JSON object with "schema": 1. */
bool settings_from_json(const char *json, const settings_t *defaults, settings_t *out, char *err, size_t err_size);
/* `base_json` (the file as read, or NULL) with the known keys replaced by `s`. Returns the
 * length written, or 0 if `size` is too small. */
size_t settings_to_json(const settings_t *s, const char *base_json, char *out, size_t size);
```

```c
#include "settings.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#define SCHEMA 1

static bool fail(char *err, size_t size, const char *fmt, ...)
{
    if (size > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, size, fmt, ap);
        va_end(ap);
    }
    return false;
}

static const cJSON *child(const cJSON *obj, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(obj, key);
}

static void read_string(const cJSON *obj, const char *key, char *out, size_t size)
{
    const cJSON *item = child(obj, key);
    if (cJSON_IsString(item) && strlen(item->valuestring) < size && item->valuestring[0] != '\0') {
        snprintf(out, size, "%s", item->valuestring);
    }
}

static void read_bool(const cJSON *obj, const char *key, bool *out)
{
    const cJSON *item = child(obj, key);
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
    }
}

/* A number scaled by `scale`, rounded and clamped to [lo, hi]. */
static long read_scaled(const cJSON *obj, const char *key, long fallback, double scale, long lo, long hi)
{
    const cJSON *item = child(obj, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return fallback;
    }
    long v = lround(item->valuedouble * scale);
    return v < lo ? lo : v > hi ? hi : v;
}

static uint8_t lpm_from_hz(const cJSON *display, uint8_t fallback)
{
    const cJSON *item = child(display, "lpm_hz");
    if (!cJSON_IsNumber(item)) {
        return fallback;
    }
    for (int q = 1; q <= 32; q *= 2) {
        if (fabs(item->valuedouble - q / 4.0) < 0.01) {
            return (uint8_t)q;
        }
    }
    return fallback;
}

bool settings_from_json(const char *json, const settings_t *defaults, settings_t *out, char *err, size_t err_size)
{
    cJSON *root = json != NULL ? cJSON_Parse(json) : NULL;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return fail(err, err_size, "not a JSON object");
    }
    const cJSON *schema = child(root, "schema");
    if (!cJSON_IsNumber(schema) || schema->valueint != SCHEMA) {
        cJSON_Delete(root);
        return fail(err, err_size, "schema must be %d", SCHEMA);
    }
    *out = *defaults;
    read_string(root, "language", out->language, sizeof(out->language));
    const cJSON *time = child(root, "time");
    read_string(time, "tz_posix", out->tz_posix, sizeof(out->tz_posix));
    read_string(time, "tz_iana", out->tz_iana, sizeof(out->tz_iana));
    read_bool(time, "clock_24h", &out->clock_24h);
    const cJSON *temp_unit = child(child(root, "units"), "temp");
    if (cJSON_IsString(temp_unit)) {
        out->fahrenheit = strcmp(temp_unit->valuestring, "F") == 0;
    }
    const cJSON *sensors = child(root, "sensors");
    out->sensors_every_min = (uint8_t)read_scaled(sensors, "interval_min", out->sensors_every_min, 1, 1, 30);
    out->temp_offset_c100 = (int16_t)read_scaled(sensors, "temp_offset_c", out->temp_offset_c100, 100, -1000, 1000);
    out->hum_offset_pct100 = (int16_t)read_scaled(sensors, "hum_offset_pct", out->hum_offset_pct100, 100, -2000, 2000);
    const cJSON *display = child(root, "display");
    out->display_every_min = (uint8_t)read_scaled(display, "update_min", out->display_every_min, 1, 1, 15);
    out->lpm_quarter_hz = lpm_from_hz(display, out->lpm_quarter_hz);
    cJSON_Delete(root);
    return true;
}

/* obj[key], created as an empty object if it is missing or not an object. */
static cJSON *object_at(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsObject(item)) {
        cJSON_DeleteItemFromObjectCaseSensitive(obj, key);
        item = cJSON_AddObjectToObject(obj, key);
    }
    return item;
}

static void put(cJSON *obj, const char *key, cJSON *value)
{
    if (cJSON_GetObjectItemCaseSensitive(obj, key) != NULL) {
        cJSON_ReplaceItemInObjectCaseSensitive(obj, key, value);
    } else {
        cJSON_AddItemToObject(obj, key, value);
    }
}

size_t settings_to_json(const settings_t *s, const char *base_json, char *out, size_t size)
{
    cJSON *root = base_json != NULL ? cJSON_Parse(base_json) : NULL;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    put(root, "schema", cJSON_CreateNumber(SCHEMA));
    put(root, "language", cJSON_CreateString(s->language));
    cJSON *time = object_at(root, "time");
    put(time, "tz_iana", cJSON_CreateString(s->tz_iana));
    put(time, "tz_posix", cJSON_CreateString(s->tz_posix));
    put(time, "clock_24h", cJSON_CreateBool(s->clock_24h));
    put(object_at(root, "units"), "temp", cJSON_CreateString(s->fahrenheit ? "F" : "C"));
    cJSON *sensors = object_at(root, "sensors");
    put(sensors, "interval_min", cJSON_CreateNumber(s->sensors_every_min));
    put(sensors, "temp_offset_c", cJSON_CreateNumber(s->temp_offset_c100 / 100.0));
    put(sensors, "hum_offset_pct", cJSON_CreateNumber(s->hum_offset_pct100 / 100.0));
    cJSON *display = object_at(root, "display");
    put(display, "update_min", cJSON_CreateNumber(s->display_every_min));
    put(display, "lpm_hz", cJSON_CreateNumber(s->lpm_quarter_hz / 4.0));
    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, true);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}
```

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ./build-host/test_settings`
Expected: `6 Tests 0 Failures`.

- [ ] **Step 4: Commit.**

```bash
git add components/storage/include/settings.h components/storage/settings.c test/host/test_settings.c test/host/CMakeLists.txt
git commit -m "feat(storage): add the settings.json codec"
```

- [ ] **Step 5: Write the failing file test.** It runs in `build-host/storage_tmp` and covers:
  - the backup kept on each write;
  - the fallback to `.bak` when the file is rejected, too big, or missing after a power cut between the renames;
  - a failed write leaving the old file alone.

```c
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage_file.h"
#include "unity.h"

/* Every test runs in an empty TEST_TMP_DIR (in the build directory), with a path as short as the
 * device's: the build directory's own path may not fit STORAGE_PATH_MAX. */
#define PATH "cfg.json"

static int s_parse_calls;
static char s_parsed[64];

/* Accepts text that starts like a JSON object, and keeps it. */
static bool parse_object(const char *text, void *ctx)
{
    (void)ctx;
    s_parse_calls++;
    if (text[0] != '{') {
        return false;
    }
    snprintf(s_parsed, sizeof(s_parsed), "%s", text);
    return true;
}

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    fputs(text, f);
    fclose(f);
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static const char *contents(const char *path)
{
    static char text[64];
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    return text;
}

static storage_file_result_t load(char *buf, size_t size, bool *from_backup, unsigned *rejected)
{
    return storage_file_load(PATH, buf, size, parse_object, NULL, from_backup, rejected);
}

void setUp(void)
{
    mkdir(TEST_TMP_DIR, 0775);
    TEST_ASSERT_EQUAL(0, chdir(TEST_TMP_DIR));
    unlink(PATH);
    unlink(PATH ".bak");
    unlink(PATH ".tmp");
    rmdir(PATH ".tmp");
    s_parse_calls = 0;
    s_parsed[0] = '\0';
}

void tearDown(void) {}

static void test_nothing_there_is_missing(void)
{
    char buf[64];
    bool from_backup = false;
    unsigned rejected = 99;
    TEST_ASSERT_EQUAL(STORAGE_FILE_MISSING, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_EQUAL(0, s_parse_calls);
    TEST_ASSERT_EQUAL(0, rejected);
}

static void test_the_first_write_leaves_no_backup(void)
{
    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, storage_file_write_atomic(PATH, "{\"v\":1}", 7));
    TEST_ASSERT_EQUAL_STRING("{\"v\":1}", contents(PATH));
    TEST_ASSERT_FALSE(exists(PATH ".bak"));
    TEST_ASSERT_FALSE(exists(PATH ".tmp"));
}

static void test_each_write_keeps_the_previous_file_as_the_backup(void)
{
    storage_file_write_atomic(PATH, "{\"v\":1}", 7);
    storage_file_write_atomic(PATH, "{\"v\":2}", 7);
    TEST_ASSERT_EQUAL_STRING("{\"v\":2}", contents(PATH));
    TEST_ASSERT_EQUAL_STRING("{\"v\":1}", contents(PATH ".bak"));
    storage_file_write_atomic(PATH, "{\"v\":3}", 7);
    TEST_ASSERT_EQUAL_STRING("{\"v\":3}", contents(PATH));
    TEST_ASSERT_EQUAL_STRING("{\"v\":2}", contents(PATH ".bak"));
    TEST_ASSERT_FALSE(exists(PATH ".tmp"));
}

static void test_a_good_file_is_used_without_reading_the_backup(void)
{
    put(PATH, "{\"main\":1}");
    put(PATH ".bak", "{\"backup\":1}");
    char buf[64];
    bool from_backup = true;
    unsigned rejected = 99;
    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_FALSE(from_backup);
    TEST_ASSERT_EQUAL_STRING("{\"main\":1}", s_parsed);
    TEST_ASSERT_EQUAL(1, s_parse_calls);
    TEST_ASSERT_EQUAL(0, rejected);
}

static void test_a_rejected_file_falls_back_to_the_backup(void)
{
    storage_file_write_atomic(PATH, "{\"good\":1}", 10);
    storage_file_write_atomic(PATH, "garbage", 7);
    char buf[64];
    bool from_backup = false;
    unsigned rejected = 0;
    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_TRUE(from_backup);
    TEST_ASSERT_EQUAL_STRING("{\"good\":1}", s_parsed);
    TEST_ASSERT_EQUAL(STORAGE_REJECTED_MAIN, rejected);
}

static void test_power_lost_between_the_renames_loads_the_previous_file(void)
{
    /* <path> already became <path>.bak; the new file is still <path>.tmp. */
    put(PATH ".bak", "{\"previous\":1}");
    put(PATH ".tmp", "{\"next\":1}");
    char buf[64];
    bool from_backup = false;
    unsigned rejected = 99;
    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_TRUE(from_backup);
    TEST_ASSERT_EQUAL_STRING("{\"previous\":1}", s_parsed);
    TEST_ASSERT_EQUAL(0, rejected); /* a missing file is not a rejected one */
}

static void test_a_file_too_big_for_the_buffer_is_rejected_unparsed(void)
{
    char buf[16];
    bool from_backup = false;
    unsigned rejected = 0;
    put(PATH, "{\"v\":\"01234567\"}"); /* 16 bytes: one too many */
    TEST_ASSERT_EQUAL(STORAGE_FILE_INVALID, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_EQUAL(0, s_parse_calls);
    TEST_ASSERT_EQUAL(STORAGE_REJECTED_MAIN, rejected);
    put(PATH, "{\"v\":\"0123456\"}"); /* 15 bytes fit */
    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_EQUAL_STRING("{\"v\":\"0123456\"}", s_parsed);
}

static void test_both_rejected_is_invalid(void)
{
    put(PATH, "nope");
    put(PATH ".bak", "nor this");
    char buf[64];
    bool from_backup = false;
    unsigned rejected = 0;
    TEST_ASSERT_EQUAL(STORAGE_FILE_INVALID, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_EQUAL(2, s_parse_calls);
    TEST_ASSERT_EQUAL(STORAGE_REJECTED_MAIN | STORAGE_REJECTED_BACKUP, rejected);
}

static void test_a_failed_write_changes_nothing(void)
{
    put(PATH, "{\"old\":1}");
    TEST_ASSERT_EQUAL(0, mkdir(PATH ".tmp", 0775)); /* now .tmp can't be opened for writing */
    TEST_ASSERT_EQUAL(STORAGE_FILE_IO_ERROR, storage_file_write_atomic(PATH, "{\"new\":1}", 9));
    TEST_ASSERT_EQUAL(EISDIR, errno);
    TEST_ASSERT_EQUAL_STRING("{\"old\":1}", contents(PATH));
    TEST_ASSERT_FALSE(exists(PATH ".bak"));
}

static void test_bad_arguments_are_refused(void)
{
    char path[STORAGE_PATH_MAX];
    memset(path, 'a', sizeof(path) - 4);
    path[sizeof(path) - 4] = '\0'; /* fits, but not with ".bak" */
    char buf[64];
    bool from_backup = false;
    TEST_ASSERT_EQUAL(STORAGE_FILE_BAD_ARG, storage_file_write_atomic(path, "{}", 2));
    TEST_ASSERT_EQUAL(STORAGE_FILE_BAD_ARG,
                      storage_file_load(path, buf, sizeof(buf), parse_object, NULL, &from_backup, NULL));
    TEST_ASSERT_EQUAL(STORAGE_FILE_BAD_ARG, storage_file_load(PATH, buf, 1, parse_object, NULL, &from_backup, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_there_is_missing);
    RUN_TEST(test_the_first_write_leaves_no_backup);
    RUN_TEST(test_each_write_keeps_the_previous_file_as_the_backup);
    RUN_TEST(test_a_good_file_is_used_without_reading_the_backup);
    RUN_TEST(test_a_rejected_file_falls_back_to_the_backup);
    RUN_TEST(test_power_lost_between_the_renames_loads_the_previous_file);
    RUN_TEST(test_a_file_too_big_for_the_buffer_is_rejected_unparsed);
    RUN_TEST(test_both_rejected_is_invalid);
    RUN_TEST(test_a_failed_write_changes_nothing);
    RUN_TEST(test_bad_arguments_are_refused);
    return UNITY_END();
}
```

In `test/host/CMakeLists.txt`, add `storage_file.c` to the library:

```cmake
add_library(storage_logic STATIC
            ${REPO_ROOT}/components/storage/settings.c ${REPO_ROOT}/components/storage/storage_file.c)
```

and register the test after `test_settings`:

```cmake
reflbo_host_test(test_storage_file storage_logic)
target_compile_definitions(test_storage_file PRIVATE TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/storage_tmp")
```

Run: `cmake -S test/host -B build-host -G Ninja`
Expected: FAIL: `components/storage/storage_file.c` does not exist.

- [ ] **Step 6: Implement the file logic.**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Config files kept with a backup (spec §14.3). Plain POSIX file calls, so this logic is tested
 * on the host; storage.c mounts LittleFS and wraps it for the app. Not thread-safe.
 */

#define STORAGE_PATH_MAX 64 /* with the terminator, and room for ".bak" / ".tmp" */

/* Parses a file's text; false means the content is invalid. */
typedef bool (*storage_parse_t)(const char *text, void *ctx);

typedef enum {
    STORAGE_FILE_OK,
    STORAGE_FILE_MISSING,  /* load: neither <path> nor <path>.bak exists */
    STORAGE_FILE_INVALID,  /* load: a file exists, but none was accepted */
    STORAGE_FILE_IO_ERROR, /* write: open, write, sync or rename failed; errno says why */
    STORAGE_FILE_BAD_ARG,  /* the path with its suffix exceeds STORAGE_PATH_MAX, or the buffer is under 2 bytes */
} storage_file_result_t;

/* Bits of `rejected` in storage_file_load(): files that exist but were not accepted. */
enum {
    STORAGE_REJECTED_MAIN = 1u << 0,
    STORAGE_REJECTED_BACKUP = 1u << 1,
};

/*
 * Reads `path` into buf (NUL-terminated, at most size - 1 bytes) and hands it to `parse`. A file
 * that is missing, unreadable, larger than the buffer or rejected by `parse` falls back to
 * <path>.bak. `*from_backup` says which one parsed; `*rejected` (may be NULL) gets the
 * STORAGE_REJECTED_* bits.
 */
storage_file_result_t storage_file_load(const char *path, char *buf, size_t size, storage_parse_t parse, void *ctx,
                                        bool *from_backup, unsigned *rejected);

/*
 * Writes <path>.tmp and syncs it, renames the old <path> to <path>.bak, then renames .tmp into
 * place. Wherever power is lost, <path> or <path>.bak holds a complete file. On failure <path> is
 * unchanged, unless the last rename failed: then only <path>.bak remains, and it loads.
 */
storage_file_result_t storage_file_write_atomic(const char *path, const char *data, size_t len);
```

```c
#include "storage_file.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
    READ_OK,
    READ_MISSING,
    READ_FAILED, /* exists but can't be read whole: too big for the buffer, or an I/O error */
} read_result_t;

static bool with_suffix(char *out, const char *path, const char *suffix)
{
    int n = snprintf(out, STORAGE_PATH_MAX, "%s%s", path, suffix);
    return n > 0 && n < STORAGE_PATH_MAX;
}

static read_result_t read_file(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return errno == ENOENT ? READ_MISSING : READ_FAILED;
    }
    size_t n = fread(buf, 1, size - 1, f);
    bool whole = !ferror(f) && (n < size - 1 || fgetc(f) == EOF);
    fclose(f);
    buf[n] = '\0';
    return whole ? READ_OK : READ_FAILED;
}

storage_file_result_t storage_file_load(const char *path, char *buf, size_t size, storage_parse_t parse, void *ctx,
                                        bool *from_backup, unsigned *rejected)
{
    unsigned bits = 0;
    if (rejected != NULL) {
        *rejected = 0;
    }
    char bak[STORAGE_PATH_MAX];
    if (size < 2 || !with_suffix(bak, path, ".bak")) {
        return STORAGE_FILE_BAD_ARG;
    }
    const char *const candidates[] = { path, bak };
    storage_file_result_t result = STORAGE_FILE_MISSING;
    for (int i = 0; i < 2; i++) {
        read_result_t r = read_file(candidates[i], buf, size);
        if (r == READ_MISSING) {
            continue;
        }
        if (r == READ_OK && parse(buf, ctx)) {
            *from_backup = i == 1;
            result = STORAGE_FILE_OK;
            break;
        }
        bits |= 1u << i;
        result = STORAGE_FILE_INVALID;
    }
    if (rejected != NULL) {
        *rejected = bits;
    }
    return result;
}

/* Removes the half-written .tmp, keeping errno from the step that failed. */
static storage_file_result_t fail(const char *tmp)
{
    int saved = errno;
    unlink(tmp);
    errno = saved;
    return STORAGE_FILE_IO_ERROR;
}

storage_file_result_t storage_file_write_atomic(const char *path, const char *data, size_t len)
{
    char tmp[STORAGE_PATH_MAX], bak[STORAGE_PATH_MAX];
    if (!with_suffix(tmp, path, ".tmp") || !with_suffix(bak, path, ".bak")) {
        return STORAGE_FILE_BAD_ARG;
    }
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) {
        return STORAGE_FILE_IO_ERROR;
    }
    bool written = fwrite(data, 1, len, f) == len && fflush(f) == 0 && fsync(fileno(f)) == 0;
    int saved = errno;
    bool closed = fclose(f) == 0;
    if (!written) {
        errno = saved;
    }
    if (!written || !closed) {
        return fail(tmp);
    }
    struct stat st;
    if (stat(path, &st) == 0 && rename(path, bak) != 0) { /* rename replaces an older .bak */
        return fail(tmp);
    }
    if (rename(tmp, path) != 0) {
        return fail(tmp);
    }
    return STORAGE_FILE_OK;
}
```

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ./build-host/test_storage_file && ctest --test-dir build-host --output-on-failure`
Expected: `10 Tests 0 Failures`; the suite passes.

- [ ] **Step 7: Commit.**

```bash
git add components/storage/include/storage_file.h components/storage/storage_file.c test/host/test_storage_file.c \
        test/host/CMakeLists.txt
git commit -m "feat(storage): write config files atomically and fall back to the backup"
```

- [ ] **Step 8: Add the LittleFS mount.**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "storage_file.h"

/*
 * LittleFS on the `storage` partition (spec §14.1, §14.3), mounted at /fs. `idf.py flash` never
 * writes it; a blank or unreadable partition is formatted at mount. Call from the app task only.
 */

#define STORAGE_SETTINGS_PATH "/fs/cfg/settings.json"
#define STORAGE_PRESETS_PATH "/fs/cfg/presets.json"

esp_err_t storage_init(void);
bool storage_ready(void);

/*
 * storage_file_load() on the mounted partition, logging rejected files (spec §14.3). ESP_OK if
 * <path> or <path>.bak parsed (*from_backup says which), ESP_ERR_NOT_FOUND if neither exists,
 * ESP_ERR_INVALID_RESPONSE if none parsed.
 */
esp_err_t storage_load(const char *path, char *buf, size_t size, storage_parse_t parse, void *ctx,
                       bool *from_backup);
/* storage_file_write_atomic() on the mounted partition: <path>.tmp, then the old file to .bak. */
esp_err_t storage_write_atomic(const char *path, const char *data, size_t len);
```

```c
#include "storage.h"

#include <errno.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_littlefs.h"
#include "esp_log.h"

#define BASE_PATH "/fs"

static const char *TAG = "storage";
static bool s_ready;

esp_err_t storage_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    esp_vfs_littlefs_conf_t conf = {
        .base_path = BASE_PATH,
        .partition_label = "storage",
        .format_if_mount_failed = true, /* the first boot finds a blank partition (spec §14.1) */
    };
    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&conf), TAG, "mount");
    static const char *const k_dirs[] = { BASE_PATH "/cfg", BASE_PATH "/state" };
    for (size_t i = 0; i < sizeof(k_dirs) / sizeof(k_dirs[0]); i++) {
        if (mkdir(k_dirs[i], 0775) != 0 && errno != EEXIST) {
            ESP_LOGW(TAG, "mkdir %s: errno %d", k_dirs[i], errno);
        }
    }
    size_t total = 0, used = 0;
    esp_littlefs_info("storage", &total, &used);
    ESP_LOGI(TAG, "LittleFS: %u of %u KB used", (unsigned)(used / 1024), (unsigned)(total / 1024));
    s_ready = true;
    return ESP_OK;
}

bool storage_ready(void)
{
    return s_ready;
}

esp_err_t storage_load(const char *path, char *buf, size_t size, storage_parse_t parse, void *ctx, bool *from_backup)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not mounted");
    unsigned rejected = 0;
    storage_file_result_t r = storage_file_load(path, buf, size, parse, ctx, from_backup, &rejected);
    if (rejected & STORAGE_REJECTED_MAIN) {
        ESP_LOGW(TAG, "%s rejected", path);
    }
    if (rejected & STORAGE_REJECTED_BACKUP) {
        ESP_LOGW(TAG, "%s.bak rejected", path);
    }
    switch (r) {
    case STORAGE_FILE_OK:
        return ESP_OK;
    case STORAGE_FILE_MISSING:
        return ESP_ERR_NOT_FOUND;
    case STORAGE_FILE_INVALID:
        return ESP_ERR_INVALID_RESPONSE;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t storage_write_atomic(const char *path, const char *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not mounted");
    storage_file_result_t r = storage_file_write_atomic(path, data, len);
    ESP_RETURN_ON_FALSE(r != STORAGE_FILE_BAD_ARG, ESP_ERR_INVALID_ARG, TAG, "path %s", path);
    ESP_RETURN_ON_FALSE(r == STORAGE_FILE_OK, ESP_FAIL, TAG, "write %s: errno %d", path, errno);
    return ESP_OK;
}
```

```yaml
dependencies:
  joltwallet/littlefs: "==1.22.3"
```

```cmake
# LittleFS config files and the settings codec (spec §14). settings.c and storage_file.c are pure
# C (cJSON and POSIX files) and are also built on the host by test/host.
idf_component_register(SRCS "storage.c" "storage_file.c" "settings.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json vfs joltwallet__littlefs)
```

Run: `tools/idf.sh reconfigure && tools/idf.sh build && git diff --stat -- dependencies.lock && grep -A2 "joltwallet/littlefs:" dependencies.lock`
Expected:
- The component manager downloads `joltwallet/littlefs` 1.22.3 into `managed_components/` (gitignored). The build passes with no warnings.
- `dependencies.lock` is new and records `component_hash: 3af20a3d75c49db9dc2286fd50862c34055c9f578419ad1f2bf7ed3ed01391fe`.

- [ ] **Step 9: Commit.**

```bash
git add components/storage dependencies.lock
git commit -m "feat(storage): mount LittleFS on the storage partition"
```

---

### Task 12: Platform fixes from the M2 review

**Files:**
- Modify:
  - `components/power/include/power.h`, `components/power/power.c`
  - `components/st7305/include/st7305.h`, `components/st7305/st7305.c`, `components/display/include/display.h`, `components/display/display.c`
  - `components/diag/include/diag.h`, `components/diag/diag_internal.h`, `components/diag/diag_cmd_power.c`, `components/diag/diag_cmd_sensors.c`
  - `tools/tests/test_devlog.py`, `test/host/CMakeLists.txt`

**Interfaces:**
- Produces (Task 13 uses them):
  - `int diag_on_owner(int (*body)(int, char **), int argc, char **argv)`, now in the public `diag.h`, for console commands registered outside `diag`.
  - `void display_cancel_deep_sleep(void)` and `void st7305_cancel_deep_sleep(void)`: release the pin holds after `display_prepare_deep_sleep()` failed.
  - `power_sleep_light()` and `power_sleep_deep()` sleep to the microsecond, so a wake set for a whole second (the seconds display) lands on it.
- Fixes (the owner chose "only where M3 touches"):
  - A rejected light sleep is neither counted nor uses a test cycle.
  - `power_set_idle_strategy()` applies the strategy even when NVS can't keep it; `power idle` then says so.
  - A failed LPM-rate write leaves the stored rate alone.
  - `panel fps` no longer installs the GPIO ISR service, which the board component owns. That removes the "already installed" error.
  - `sensors` prints −0.50 °C with its sign.
  - `rtc set` refuses years outside 2000–2099, which the RTC can't hold.
- The host harness gains `-DREFLBO_SANITIZE=ON` (ASan and UBSan), a Python 3.9 floor and no `tools/__pycache__`. A new test keeps `devlog.PROMPT` equal to the firmware's `DIAG_PROMPT`.

These changes are device code, checked on the board (AGENTS §7, level 3). Only the prompt test runs on the host, and it passes as soon as it exists: it guards against later drift.

- [ ] **Step 1: The prompt test and the harness.** Apply:

```diff
diff --git a/tools/tests/test_devlog.py b/tools/tests/test_devlog.py
index 09e816d..c45250b 100644
--- a/tools/tests/test_devlog.py
+++ b/tools/tests/test_devlog.py
@@ -1,4 +1,6 @@
 import io
+import pathlib
+import re
 import unittest
 from types import SimpleNamespace
 
@@ -124,6 +126,14 @@ class OpenPortTest(unittest.TestCase):
         self.assertEqual(ser.states[-1], (False, False))
 
 
+class PromptTest(unittest.TestCase):
+    def test_prompt_matches_the_firmware(self):
+        header = pathlib.Path(__file__).resolve().parents[2] / "components/diag/diag_internal.h"
+        match = re.search(r'#define DIAG_PROMPT "([^"]*)"', header.read_text())
+        self.assertIsNotNone(match, "DIAG_PROMPT not found")
+        self.assertEqual(devlog.PROMPT, match.group(1))
+
+
 class StripAnsiTest(unittest.TestCase):
     def test_removes_color_codes(self):
         self.assertEqual(devlog.strip_ansi("\x1b[0;32mI (12) main: ok\x1b[0m"), "I (12) main: ok")
```

In `test/host/CMakeLists.txt`, after `set(REFLBO_WARNINGS ...)`:

```cmake
# cmake -DREFLBO_SANITIZE=ON: everything with AddressSanitizer and UndefinedBehaviorSanitizer.
option(REFLBO_SANITIZE "Build the host tests with ASan and UBSan" OFF)
if(REFLBO_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()
```

and at the end, replace the Python lines with:

```cmake
# Python tool tests (stdlib unittest; pyserial is not needed).
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
add_test(NAME tools_unittests
         COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -t . -v
         WORKING_DIRECTORY ${REPO_ROOT}/tools)
set_tests_properties(tools_unittests PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1") # no tools/__pycache__
```

Run:

```bash
rm -rf tools/__pycache__ tools/tests/__pycache__
cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure
cmake -S test/host -B build-host-asan -G Ninja -DREFLBO_SANITIZE=ON && cmake --build build-host-asan \
  && ctest --test-dir build-host-asan --output-on-failure
ls tools/__pycache__ 2>&1 | head -1
```

Expected:
- Both suites pass, and `test_prompt_matches_the_firmware` is listed as ok.
- The sanitizer build reports nothing.
- `ls` reports `No such file or directory`.

```bash
git add tools/tests/test_devlog.py test/host/CMakeLists.txt
git commit -m "test: check the console prompt against the firmware and add a sanitizer build"
```

- [ ] **Step 2: Power.** Apply:

```diff
diff --git a/components/power/include/power.h b/components/power/include/power.h
index 6b13d82..6de78ff 100644
--- a/components/power/include/power.h
+++ b/components/power/include/power.h
@@ -62,7 +62,8 @@ void power_sleep_deep(time_t until_utc);
 /* Never returns: deep sleep for `seconds` or until KEY or BOOT, then boot from scratch. */
 void power_sleep_retry(uint32_t seconds);
 power_idle_t power_idle_strategy(void);
-esp_err_t power_set_idle_strategy(power_idle_t idle); /* persisted in NVS */
+/* Applies at once; an error means only that NVS didn't keep it for the next boot. */
+esp_err_t power_set_idle_strategy(power_idle_t idle);
 /* `sleep test`: the next `cycles` sleeps use `mode` even when tethered. Resets the stats. */
 void power_start_test(power_idle_t mode, int cycles);
 int power_test_cycles_left(void);
diff --git a/components/power/power.c b/components/power/power.c
index 2e6ec94..7120ed4 100644
--- a/components/power/power.c
+++ b/components/power/power.c
@@ -1,6 +1,7 @@
 #include "power.h"
 
 #include <stdio.h>
+#include <sys/time.h>
 #include <unistd.h>
 
 #include "board_pins.h"
@@ -175,7 +176,15 @@ power_plan_t power_plan(bool work_pending)
     return power_policy(&in);
 }
 
+/* The awake phase ended at `at_us` (esp_timer). */
+static void count_sleep_at(bool deep, int64_t at_us);
+
 static void count_sleep(bool deep)
+{
+    count_sleep_at(deep, esp_timer_get_time());
+}
+
+static void count_sleep_at(bool deep, int64_t at_us)
 {
     power_stats_t *st = &s_rtc.stats;
     if (deep) {
@@ -184,7 +193,7 @@ static void count_sleep(bool deep)
         st->light_sleeps++;
     }
     if (s_awake_since_us >= 0) {
-        uint32_t awake_ms = (uint32_t)((esp_timer_get_time() - s_awake_since_us) / 1000);
+        uint32_t awake_ms = (uint32_t)((at_us - s_awake_since_us) / 1000);
         st->awake_count++;
         st->awake_ms_total += awake_ms;
         st->awake_ms_last = awake_ms;
@@ -226,11 +235,13 @@ static void count_wake(power_wake_t wake, int64_t slept_ms)
     seal();
 }
 
+/* To the microsecond, so a wake set for a whole second (the seconds display) lands on it. */
 static uint64_t sleep_us_until(time_t until_utc)
 {
-    time_t now = time(NULL);
-    time_t left = until_utc > now ? until_utc - now : 1;
-    return (uint64_t)left * 1000000u;
+    struct timeval tv;
+    gettimeofday(&tv, NULL);
+    int64_t left = (int64_t)until_utc * 1000000 - ((int64_t)tv.tv_sec * 1000000 + tv.tv_usec);
+    return left > 1000 ? (uint64_t)left : 1000u;
 }
 
 power_wake_t power_sleep_light(time_t until_utc)
@@ -242,7 +253,6 @@ power_wake_t power_sleep_light(time_t until_utc)
             ESP_LOGW(TAG, "CPU stays powered in light sleep: %s", esp_err_to_name(err));
         }
     }
-    count_sleep(false);
     for (size_t i = 0; i < sizeof(k_wake_pins) / sizeof(k_wake_pins[0]); i++) {
         gpio_intr_disable(k_wake_pins[i]);
         gpio_wakeup_enable(k_wake_pins[i], GPIO_INTR_LOW_LEVEL);
@@ -258,7 +268,10 @@ power_wake_t power_sleep_light(time_t until_utc)
         gpio_intr_enable(k_wake_pins[i]);
     }
     esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
-    s_awake_since_us = woke_us;
+    if (err == ESP_OK) {
+        count_sleep_at(false, slept_from_us);
+        s_awake_since_us = woke_us;
+    }
 
     power_wake_t wake = POWER_WAKE_OTHER;
     if (gpio_get_level(BOARD_PIN_KEY) == 0) {
@@ -270,6 +283,9 @@ power_wake_t power_sleep_light(time_t until_utc)
     } else if (err == ESP_OK && (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER))) {
         wake = POWER_WAKE_TIMER;
     }
+    if (err != ESP_OK) {
+        return wake; /* rejected (a wake source was already active): not a sleep, no test cycle used */
+    }
     count_wake(wake, (woke_us - slept_from_us) / 1000);
     finish_test();
     return wake;
@@ -315,6 +331,8 @@ power_idle_t power_idle_strategy(void)
 
 esp_err_t power_set_idle_strategy(power_idle_t idle)
 {
+    s_strategy = idle; /* applies for this session even if NVS can't keep it */
+    cache_strategy();
     nvs_handle_t nvs;
     ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "NVS open");
     esp_err_t err = nvs_set_u8(nvs, NVS_KEY_IDLE, (uint8_t)idle);
@@ -323,8 +341,6 @@ esp_err_t power_set_idle_strategy(power_idle_t idle)
     }
     nvs_close(nvs);
     ESP_RETURN_ON_ERROR(err, TAG, "NVS write");
-    s_strategy = idle;
-    cache_strategy();
     return ESP_OK;
 }
 
```

Run: `tools/idf.sh build`
Expected: passes with no warnings.

```bash
git add components/power
git commit -m "fix(power): sleep to the microsecond, count only real light sleeps, keep the idle strategy without NVS"
```

- [ ] **Step 3: Panel.** Apply:

```diff
diff --git a/components/display/display.c b/components/display/display.c
index b142ff6..a9f0534 100644
--- a/components/display/display.c
+++ b/components/display/display.c
@@ -46,6 +46,11 @@ esp_err_t display_prepare_deep_sleep(void)
     return st7305_prepare_deep_sleep();
 }
 
+void display_cancel_deep_sleep(void)
+{
+    st7305_cancel_deep_sleep();
+}
+
 esp_err_t display_init(st7305_variant_t variant)
 {
     ESP_RETURN_ON_ERROR(alloc_fb(), TAG, "framebuffer");
diff --git a/components/display/include/display.h b/components/display/include/display.h
index 13931b7..41dd4b7 100644
--- a/components/display/include/display.h
+++ b/components/display/include/display.h
@@ -26,6 +26,7 @@ esp_err_t display_init_warm(const display_state_t *state);
 void display_export(display_state_t *out);
 /* Holds the panel pins for deep sleep. The last display call before sleeping. */
 esp_err_t display_prepare_deep_sleep(void);
+void display_cancel_deep_sleep(void); /* after display_prepare_deep_sleep() failed */
 gfx_fb_t *display_fb(void); /* NULL until display_init has allocated the framebuffer */
 /* Pushes the framebuffer if it changed since the last push (CRC32), or always when force is set. */
 esp_err_t display_commit(bool force);
diff --git a/components/st7305/include/st7305.h b/components/st7305/include/st7305.h
index 2d463e6..4ea072e 100644
--- a/components/st7305/include/st7305.h
+++ b/components/st7305/include/st7305.h
@@ -51,6 +51,8 @@ esp_err_t st7305_push(const uint8_t *canonical);
 esp_err_t st7305_set_mode(st7305_mode_t mode);
 /* Sets the LPM refresh rate. Before st7305_init it is only stored; every init applies it. */
 esp_err_t st7305_set_lpm_rate(st7305_lpm_rate_t rate);
+/* Undoes st7305_prepare_deep_sleep() after it failed part-way, so the panel can be used again. */
+void st7305_cancel_deep_sleep(void);
 st7305_variant_t st7305_variant(void);
 st7305_mode_t st7305_mode(void);
 st7305_lpm_rate_t st7305_lpm_rate(void);
diff --git a/components/st7305/st7305.c b/components/st7305/st7305.c
index 9f90212..ff56504 100644
--- a/components/st7305/st7305.c
+++ b/components/st7305/st7305.c
@@ -242,6 +242,13 @@ esp_err_t st7305_prepare_deep_sleep(void)
     return ESP_OK;
 }
 
+void st7305_cancel_deep_sleep(void)
+{
+    gpio_deep_sleep_hold_dis();
+    gpio_hold_dis(PIN_CS);
+    gpio_hold_dis(PIN_RST);
+}
+
 esp_err_t st7305_reinit(st7305_variant_t variant)
 {
     ESP_RETURN_ON_FALSE(s_bus_ready, ESP_ERR_INVALID_STATE, TAG, "call st7305_init first");
@@ -311,11 +318,16 @@ esp_err_t st7305_set_mode(st7305_mode_t mode)
 esp_err_t st7305_set_lpm_rate(st7305_lpm_rate_t rate)
 {
     ESP_RETURN_ON_FALSE((unsigned)rate <= ST7305_LPM_8HZ, ESP_ERR_INVALID_ARG, TAG, "LPM rate");
-    s_lpm_rate = rate;
+    st7305_lpm_rate_t previous = s_lpm_rate;
+    s_lpm_rate = rate; /* write_frame_rate() sends s_lpm_rate */
     if (s_io == NULL) {
         return ESP_OK; /* st7305_init applies it */
     }
-    return write_frame_rate(); /* applies at once, also in LPM (measured with panel fps) */
+    esp_err_t err = write_frame_rate(); /* applies at once, also in LPM (measured with panel fps) */
+    if (err != ESP_OK) {
+        s_lpm_rate = previous; /* the panel still runs at the old rate */
+    }
+    return err;
 }
 
 st7305_variant_t st7305_variant(void)
@@ -348,9 +360,7 @@ esp_err_t st7305_count_frames(uint32_t window_ms, uint32_t *frames)
         .intr_type = GPIO_INTR_POSEDGE,
     };
     ESP_RETURN_ON_ERROR(gpio_config(&te), TAG, "TE pin");
-    esp_err_t err = gpio_install_isr_service(0);
-    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "GPIO ISR service");
-    s_te_pulses = 0;
+    s_te_pulses = 0; /* the board component installed the GPIO ISR service (AGENTS.md §5.3) */
     ESP_RETURN_ON_ERROR(gpio_isr_handler_add(PIN_TE, on_te, NULL), TAG, "TE handler");
     vTaskDelay(pdMS_TO_TICKS(window_ms));
     gpio_isr_handler_remove(PIN_TE);
```

Run: `tools/idf.sh build`
Expected: passes with no warnings.

```bash
git add components/st7305 components/display
git commit -m "fix(st7305): keep the LPM rate on a failed write, undo deep-sleep holds, leave the ISR service to board"
```

- [ ] **Step 4: Console.** Apply:

```diff
diff --git a/components/diag/diag_cmd_power.c b/components/diag/diag_cmd_power.c
index cc801f7..3183c7e 100644
--- a/components/diag/diag_cmd_power.c
+++ b/components/diag/diag_cmd_power.c
@@ -29,8 +29,7 @@ static int power_body(int argc, char **argv)
         }
         esp_err_t err = power_set_idle_strategy(idle);
         if (err != ESP_OK) {
-            printf("power: %s\n", esp_err_to_name(err));
-            return 1;
+            printf("power: set for this session, not saved: %s\n", esp_err_to_name(err));
         }
     }
     printf("power: idle %s%s\n", power_idle_strategy() == POWER_IDLE_DEEP ? "deep" : "light",
diff --git a/components/diag/diag_cmd_sensors.c b/components/diag/diag_cmd_sensors.c
index 4d667ea..2026857 100644
--- a/components/diag/diag_cmd_sensors.c
+++ b/components/diag/diag_cmd_sensors.c
@@ -33,7 +33,8 @@ static int sensors_body(int argc, char **argv)
         return 1;
     }
     sensors_env_t env = sensors_env();
-    printf("sensors: %d.%02d C, %d.%02d %%RH\n", env.temp_c100 / 100, abs(env.temp_c100 % 100), env.hum_pct100 / 100,
+    int t = abs(env.temp_c100);
+    printf("sensors: %s%d.%02d C, %d.%02d %%RH\n", env.temp_c100 < 0 ? "-" : "", t / 100, t % 100, env.hum_pct100 / 100,
            env.hum_pct100 % 100);
     return 0;
 }
@@ -90,7 +91,9 @@ static int rtc_body(int argc, char **argv)
     }
     if (argc == 3 && strcmp(argv[1], "set") == 0) {
         time_t utc;
-        if (!timekeeping_parse_iso8601(argv[2], &utc)) {
+        struct tm year_check;
+        if (!timekeeping_parse_iso8601(argv[2], &utc) || gmtime_r(&utc, &year_check) == NULL ||
+            year_check.tm_year < 100 || year_check.tm_year > 199) { /* the RTC holds 2000-2099 */
             return usage(k_usage);
         }
         esp_err_t err = timekeeping_set_utc(utc);
diff --git a/components/diag/diag_internal.h b/components/diag/diag_internal.h
index 492a228..1ed40ce 100644
--- a/components/diag/diag_internal.h
+++ b/components/diag/diag_internal.h
@@ -1,5 +1,6 @@
 #pragma once
 
+#include "diag.h"
 #include "esp_err.h"
 
 /* tools/devlog.py waits for exactly this prompt. Keep the two in sync. */
@@ -11,5 +12,3 @@ esp_err_t diag_register_button_commands(void);  /* btn */
 esp_err_t diag_register_sensor_commands(void);  /* sensors, battery, rtc */
 esp_err_t diag_register_power_commands(void);   /* power, sleep */
 
-/* Runs a command body on the hardware owner's task (see diag_set_executor) and returns its result. */
-int diag_on_owner(int (*body)(int argc, char **argv), int argc, char **argv);
diff --git a/components/diag/include/diag.h b/components/diag/include/diag.h
index 41b6731..f7e31e1 100644
--- a/components/diag/include/diag.h
+++ b/components/diag/include/diag.h
@@ -13,3 +13,6 @@ typedef esp_err_t (*diag_executor_t)(void (*fn)(void *arg), void *arg);
 /* Commands that touch the display, I²C devices or sleep state run through this; set it before
  * diag_start(). Without one they run on the console task. */
 void diag_set_executor(diag_executor_t executor);
+/* Runs a command body on the executor's task and returns its result: for commands registered
+ * outside this component (main registers `field` and `preset`) after diag_start(). */
+int diag_on_owner(int (*body)(int argc, char **argv), int argc, char **argv);
```

Run: `tools/idf.sh build`
Expected: passes with no warnings.

```bash
git add components/diag
git commit -m "fix(diag): sign negative temperatures, keep rtc set within 2000-2099, share diag_on_owner"
```

- [ ] **Step 5: Check on the board.** Confirm the port first: `ls /dev/cu.usbmodem*` shows one port, and `flash` must print `MAC: 14:c1:9f:54:bb:94`. The screen is still the M2 clock at this point.

```bash
tools/idf.sh -p /dev/cu.usbmodem2101 flash
tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/m3a-t12-boot.log
tools/idf.sh exec python tools/devlog.py --cmd "rtc set 2100-01-01T00:00:00Z" --cmd "rtc set 1999-12-31T23:59:59Z" \
  --cmd "rtc set $(date -u +%Y-%m-%dT%H:%M:%SZ)" --cmd sensors --cmd "panel fps 5" --cmd "power idle" \
  -o captures/m3a-t12-cmds.log
grep -E "(^|reflbo> )[EW] \(" captures/m3a-t12-boot.log captures/m3a-t12-cmds.log
```

Expected:
- The boot log reaches `reflbo ready (cold wake)`.
- Both out-of-range `rtc set` commands print `usage: rtc get | rtc set <YYYY-MM-DDTHH:MM[:SS][Z|+HH:MM]>`, and the third sets the clock.
- `sensors` prints the reading.
- `panel fps` prints `5 frames in 5 s = 1.00 Hz`.
- `power idle` prints the strategy.
- `grep` finds no `E` or `W` lines; in particular, no `GPIO isr service already installed`.

Then one light sleep (the port drops for up to a minute), read with a fresh session afterwards:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "sleep test light 1" -t 5
sleep 75
tools/idf.sh exec python tools/devlog.py --cmd "sleep stats"
```

Expected: `sleep: 1 light, 0 deep`, one wake, and `test cycles left 0`.


---

### Task 13: The app shows dashboards (`main`)

**Files:**
- Create: `main/app_internal.h`, `main/app_ui.c`, `main/app_cmds.c`
- Modify: `main/app.c`, `main/CMakeLists.txt`, `main/Kconfig.projbuild`, `components/ui/CMakeLists.txt`, `test/host/CMakeLists.txt`, `tools/render.py`
- Delete: `components/ui/ui_clock.c`, `components/ui/include/ui_clock.h`, `test/host/test_ui_clock_golden.c`, `test/host/render_clock.c`, `test/host/ui_fixtures.h`, `test/host/golden/clock_*.pbm`

**Interfaces:**
- Consumes:
  - Everything from Tasks 1–12. The main entry points:
    - `ui_draw_dashboard()`, `ui_presets_*` and `ui_resolve()`;
    - `ds_*`;
    - `settings_from_json()` and `settings_to_json()`;
    - `storage_init()`, `storage_load()` and `storage_write_atomic()`;
    - `scheduler_next_wake()` with `cycle_at` and `every_second`;
    - `sensors_set_offsets()` and `sensors_battery_days_left10()`;
    - `diag_on_owner()` and `display_cancel_deep_sleep()`.
- Produces (`main/app_internal.h`): `app_ui_state_t { ds, settings, presets, cycle_at, done_slot }`, part of the deep-sleep snapshot (`SNAP_VERSION` 2). Around it, the `app_ui_*` functions, which belong to the app task.
- Behaviour:
  - **Boot.** A cold boot loads `settings.json` and `presets.json` (falling back to `.bak`, then to the defaults). A warm deep wake takes everything from RTC RAM and never touches storage.
  - **Settings applied at boot.** The time zone, the sensor offsets, the freshness (15 min, or twice the sensor interval if longer) and the panel's LPM rate.
  - **KEY short** makes the next preset in the cycle active and saves `presets.json`.
  - **KEY double** turns auto-cycling on or off and saves it. Cycle switches are not saved.
  - **Auto-cycle timing.** The first tick after a boot arms the switch, once the RTC has set the clock. A clock that moved back re-arms it.
  - **Waking.** The RTC alarm stays on minute slots. A cycle switch or a seconds tick before the next slot uses the ESP timer (`sleep_until()`).
  - **Moving the clock.** A console call that moves the clock (`rtc set`) renders at once.
  - **Clock jumps.** `check_clock_jump()` compares the pending alarm with a fresh schedule, which also holds across DST.
  - **A failed deep-sleep pin hold** now sleeps light for that cycle instead of letting the panel reset.
  - **Warm wake without a valid snapshot.** The panel rate is re-sent.
  - **Console.** `field list|get|set|clear` and `preset list|set` run on the app task.

- [ ] **Step 1: The Kconfig name for the default time zone.**

```kconfig
menu "reflbo"

    config REFLBO_TZ
        string "Time zone (POSIX TZ string)"
        default "CET-1CEST,M3.5.0,M10.5.0/3"
        help
            Default time zone, Europe/Prague (spec §7). settings.json overrides it.

    config REFLBO_TZ_NAME
        string "Time zone name (IANA)"
        default "Europe/Prague"
        help
            The name shown for REFLBO_TZ; settings.json keeps both (spec §7).

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

    choice REFLBO_IDLE_DEFAULT
        prompt "Default idle strategy (D3: light sleep, chosen at M2)"
        default REFLBO_IDLE_DEFAULT_LIGHT
        help
            `power idle deep|light` overrides this at runtime and keeps the choice in NVS.
        config REFLBO_IDLE_DEFAULT_LIGHT
            bool "Light sleep"
        config REFLBO_IDLE_DEFAULT_DEEP
            bool "Deep sleep"
    endchoice

endmenu
```

- [ ] **Step 2: The dashboard state and the console commands.**

```c
#pragma once

#include <stdbool.h>
#include <time.h>

#include "datastore.h"
#include "scheduler.h"
#include "settings.h"
#include "ui_fields.h"
#include "ui_preset.h"

/* The dashboard's state and behaviour (main/app_ui.c). All of it belongs to the app task. */

typedef struct {
    ds_t ds;
    settings_t settings;
    ui_presets_t presets;
    time_t cycle_at;  /* next auto-cycle switch; 0 = not armed (cycling off, or no tick yet) */
    time_t done_slot; /* the last minute slot that was sampled and rendered */
} app_ui_state_t;

/* Kconfig settings, the built-in presets and an empty datastore. */
void app_ui_defaults(void);
/* Mounts storage and reads settings.json and presets.json, keeping defaults for what fails. */
void app_ui_load(void);
void app_ui_export(app_ui_state_t *out); /* for the deep-sleep snapshot */
void app_ui_import(const app_ui_state_t *in);

const settings_t *app_settings(void);
ds_t *app_ds(void);
ui_presets_t *app_presets(void);

/* Applies the settings that live outside the app: time zone, sensor offsets, freshness, panel rate. */
void app_ui_apply_settings(void);
void app_ui_render(void);
/* The context a render uses right now (also for the `field` command). */
void app_ui_context(ui_context_t *ctx);
void app_ui_sample(time_t now); /* SHTC3 and battery into the datastore */
/* Makes preset `index` active and renders it. `persist` saves presets.json (manual switches). */
void app_ui_select(int index, bool persist);
void app_ui_toggle_cycle(void);
/* Samples and renders what is due now: minute slots, the cycle switch, the seconds display.
 * `force` samples and renders anyway. Call after reading the RTC. */
void app_ui_tick(bool force);
sched_wake_t app_ui_next_wake(time_t now);

/* The `field` and `preset` console commands (main/app_cmds.c); call after diag_start(). */
void app_register_commands(void);
```

```c
#include <stdio.h>
#include <string.h>

#include "app_internal.h"
#include "display.h"
#include "esp_log.h"
#include "lang.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "st7305.h"
#include "storage.h"
#include "timekeeping.h"
#include "ui_dashboard.h"
#include "util_time.h"

static const char *TAG = "app_ui";

static app_ui_state_t s;
static char s_file[6144]; /* presets.json with 16 presets is about 5 KB */
static char s_err[96];

static void default_settings(settings_t *out)
{
    *out = (settings_t){
        .language = "en",
        .clock_24h = true,
        .sensors_every_min = CONFIG_REFLBO_SENSOR_INTERVAL_MIN,
        .temp_offset_c100 = CONFIG_REFLBO_TEMP_OFFSET_C10 * 10,
        .hum_offset_pct100 = CONFIG_REFLBO_HUM_OFFSET_PCT10 * 10,
        .display_every_min = CONFIG_REFLBO_DISPLAY_UPDATE_MIN,
        .lpm_quarter_hz = 4, /* 1 Hz (D12) */
    };
    snprintf(out->tz_posix, sizeof(out->tz_posix), "%s", CONFIG_REFLBO_TZ);
    snprintf(out->tz_iana, sizeof(out->tz_iana), "%s", CONFIG_REFLBO_TZ_NAME);
}

void app_ui_defaults(void)
{
    memset(&s, 0, sizeof(s));
    ds_init(&s.ds);
    default_settings(&s.settings);
    ui_presets_defaults(&s.presets);
}

static bool parse_settings(const char *text, void *ctx)
{
    settings_t defaults;
    default_settings(&defaults);
    bool ok = settings_from_json(text, &defaults, ctx, s_err, sizeof(s_err));
    if (!ok) {
        ESP_LOGW(TAG, "settings.json: %s", s_err);
    }
    return ok;
}

static bool parse_presets(const char *text, void *ctx)
{
    bool ok = ui_presets_from_json(text, ctx, s_err, sizeof(s_err));
    if (!ok) {
        ESP_LOGW(TAG, "presets.json: %s", s_err);
    }
    return ok;
}

static void load_one(const char *path, storage_parse_t parse, void *target, size_t size, const void *defaults)
{
    bool from_backup = false;
    esp_err_t err = storage_load(path, s_file, sizeof(s_file), parse, target, &from_backup);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s loaded%s", path, from_backup ? " from the backup" : "");
        return;
    }
    memcpy(target, defaults, size); /* a failed parse may have left it half-written */
    ESP_LOGI(TAG, "%s: %s; using defaults", path, err == ESP_ERR_NOT_FOUND ? "not there yet" : "invalid");
}

void app_ui_load(void)
{
    settings_t settings_defaults;
    default_settings(&settings_defaults);
    ui_presets_t presets_defaults;
    ui_presets_defaults(&presets_defaults);
    s.settings = settings_defaults;
    s.presets = presets_defaults;
    esp_err_t err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage: %s; settings and presets use their defaults", esp_err_to_name(err));
        return;
    }
    load_one(STORAGE_SETTINGS_PATH, parse_settings, &s.settings, sizeof(s.settings), &settings_defaults);
    load_one(STORAGE_PRESETS_PATH, parse_presets, &s.presets, sizeof(s.presets), &presets_defaults);
    s.cycle_at = 0; /* armed by the first tick, once the RTC has set the clock */
}

void app_ui_export(app_ui_state_t *out)
{
    *out = s;
}

void app_ui_import(const app_ui_state_t *in)
{
    s = *in;
}

const settings_t *app_settings(void)
{
    return &s.settings;
}

ds_t *app_ds(void)
{
    return &s.ds;
}

ui_presets_t *app_presets(void)
{
    return &s.presets;
}

void app_ui_apply_settings(void)
{
    timekeeping_init(s.settings.tz_posix);
    sensors_set_offsets(s.settings.temp_offset_c100, s.settings.hum_offset_pct100);
    /* Spec §5.1: stale after 15 min, but never before the next reading is due. */
    uint32_t ttl = (uint32_t)s.settings.sensors_every_min * 120u;
    ttl = ttl < 900 ? 900 : ttl;
    for (int f = 0; f < DS_FIELD_COUNT; f++) {
        ds_set_ttl(&s.ds, (ds_field_t)f, ttl);
    }
    int quarter_hz = s.settings.lpm_quarter_hz, rate = 0;
    while (quarter_hz > 1 && rate < ST7305_LPM_8HZ) {
        quarter_hz /= 2;
        rate++;
    }
    if (display_fb() != NULL && st7305_lpm_rate() != (st7305_lpm_rate_t)rate) {
        esp_err_t err = st7305_set_lpm_rate((st7305_lpm_rate_t)rate);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "panel rate: %s", esp_err_to_name(err));
        }
    }
}

static int32_t local_day(const struct tm *local)
{
    return (int32_t)util_days_from_civil(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);
}

void app_ui_context(ui_context_t *ctx)
{
    time_t now = time(NULL);
    *ctx = (ui_context_t){ .now = now, .time_valid = timekeeping_valid(), .ds = &s.ds,
                           .lang = lang_get(s.settings.language), .clock_24h = s.settings.clock_24h,
                           .fahrenheit = s.settings.fahrenheit };
    localtime_r(&now, &ctx->local);
    ctx->local_day = local_day(&ctx->local);
}

void app_ui_render(void)
{
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        return;
    }
    ui_context_t ctx;
    app_ui_context(&ctx);
    ui_draw_dashboard(fb, &ctx, &s.presets.presets[s.presets.active]);
    esp_err_t err = display_commit(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display: %s", esp_err_to_name(err));
    }
}

static ds_bat_state_t ds_battery_state(battery_state_t state)
{
    switch (state) {
    case BATTERY_DISCHARGING:
        return DS_BAT_DISCHARGING;
    case BATTERY_CHARGING:
        return DS_BAT_CHARGING;
    case BATTERY_FULL:
        return DS_BAT_FULL;
    default:
        return DS_BAT_UNKNOWN;
    }
}

void app_ui_sample(time_t now)
{
    struct tm local;
    localtime_r(&now, &local);
    esp_err_t err = sensors_sample_env(now);
    if (err == ESP_OK) {
        sensors_env_t env = sensors_env();
        ds_set_env(&s.ds, env.temp_c100, env.hum_pct100, env.time, local_day(&local));
    } else {
        ESP_LOGW(TAG, "SHTC3: %s; keeping the last reading", esp_err_to_name(err));
    }
    err = sensors_sample_battery(now);
    if (err == ESP_OK) {
        sensors_battery_t bat = sensors_battery(now);
        ds_set_battery(&s.ds, bat.level, bat.smoothed_mv, ds_battery_state(bat.state), now);
        int days10 = sensors_battery_days_left10(now);
        if (days10 >= 0) {
            ds_set(&s.ds, DS_BAT_DAYS, days10, now);
        } else {
            ds_clear(&s.ds, DS_BAT_DAYS);
        }
    } else {
        ESP_LOGW(TAG, "battery: %s", esp_err_to_name(err));
    }
    ds_take_changes(&s.ds); /* every sample is followed by a render anyway */
}

static void save_presets(void)
{
    size_t n = ui_presets_to_json(&s.presets, s_file, sizeof(s_file));
    if (n == 0) {
        ESP_LOGE(TAG, "presets.json does not fit %u bytes", (unsigned)sizeof(s_file));
        return;
    }
    esp_err_t err = storage_init(); /* not mounted yet after a routine deep-sleep wake */
    if (err == ESP_OK) {
        err = storage_write_atomic(STORAGE_PRESETS_PATH, s_file, n);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving presets: %s", esp_err_to_name(err));
    }
}

void app_ui_select(int index, bool persist)
{
    if (index < 0 || index >= s.presets.count) {
        return;
    }
    s.presets.active = (uint8_t)index;
    if (s.presets.cycle_enabled) {
        s.cycle_at = time(NULL) + s.presets.cycle_interval_s; /* a switch restarts the interval */
    }
    ESP_LOGI(TAG, "preset %s", s.presets.presets[index].id);
    app_ui_render();
    if (persist) {
        save_presets();
    }
}

void app_ui_toggle_cycle(void)
{
    s.presets.cycle_enabled = !s.presets.cycle_enabled;
    s.cycle_at = s.presets.cycle_enabled ? time(NULL) + s.presets.cycle_interval_s : 0;
    ESP_LOGI(TAG, "auto-cycle %s", s.presets.cycle_enabled ? "on" : "off");
    save_presets();
}

void app_ui_tick(bool force)
{
    time_t now = time(NULL);
    time_t slot = now - now % 60;
    bool new_slot = slot != s.done_slot;
    bool render = force || s.presets.presets[s.presets.active].seconds;
    if (force || (new_slot && scheduler_is_slot(slot, s.settings.sensors_every_min))) {
        app_ui_sample(now);
        render = true;
    }
    if (new_slot && scheduler_is_slot(slot, s.settings.display_every_min)) {
        render = true;
    }
    s.done_slot = slot;
    if (s.presets.cycle_enabled && (s.cycle_at == 0 || s.cycle_at - now > s.presets.cycle_interval_s)) {
        s.cycle_at = now + s.presets.cycle_interval_s; /* the first tick, or the clock moved back */
    }
    if (s.presets.cycle_enabled && now >= s.cycle_at) {
        app_ui_select(ui_presets_next(&s.presets), false); /* renders; not saved, the cycle will move on */
        return;
    }
    if (render) {
        app_ui_render();
    }
}

sched_wake_t app_ui_next_wake(time_t now)
{
    sched_input_t in = {
        .now = now,
        .display_every_min = s.settings.display_every_min,
        .sensors_every_min = s.settings.sensors_every_min,
        .cycle_at = s.presets.cycle_enabled ? s.cycle_at : 0,
        .every_second = s.presets.presets[s.presets.active].seconds,
    };
    return scheduler_next_wake(&in);
}
```

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_internal.h"
#include "diag.h"
#include "esp_console.h"
#include "esp_log.h"
#include "ui_fields.h"
#include "ui_layout.h"

/* `field` and `preset` (spec §15): inspect the dashboard and inject test data on the device. */

static const char *TAG = "app_cmds";

static int usage(const char *text)
{
    printf("usage: %s\n", text);
    return 1;
}

static void print_field(const ui_context_t *ctx, ui_field_id_t field)
{
    ui_value_t v;
    ui_resolve(ctx, field, &v);
    const char *state = v.state == UI_VALUE_FRESH ? "fresh" : v.state == UI_VALUE_STALE ? "stale" : "missing";
    printf("%-13s %-7s %s%s%s", ui_field_info(field)->id, state, v.text, v.unit[0] ? " " : "", v.unit);
    if (v.extra[0]) {
        printf(" (%s)", v.extra);
    }
    if (v.state == UI_VALUE_STALE) {
        printf(", %lu s old", (unsigned long)v.age_s);
    }
    if (v.trend) {
        printf(", %s", v.trend > 0 ? "rising" : "falling");
    }
    printf("\n");
}

/* Datastore units per console unit: 0.01 °C, 0.01 %, %, 0.1 d. */
static int scale(ds_field_t field)
{
    return field == DS_BAT_LEVEL ? 1 : field == DS_BAT_DAYS ? 10 : 100;
}

static int field_body(int argc, char **argv)
{
    static const char *const k_usage = "field list | field get <id> | field set <id> <value> | field clear <id>";
    ui_context_t ctx;
    app_ui_context(&ctx);
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
            print_field(&ctx, (ui_field_id_t)f);
        }
        return 0;
    }
    if (argc < 3) {
        return usage(k_usage);
    }
    ui_field_id_t field = ui_field_by_name(argv[2]);
    if (field == UI_FIELD_NONE) {
        printf("field: no field \"%s\" (see `field list`)\n", argv[2]);
        return 1;
    }
    if (argc == 3 && strcmp(argv[1], "get") == 0) {
        print_field(&ctx, field);
        return 0;
    }
    int ds_field = ui_field_info(field)->ds_field;
    bool set = argc == 4 && strcmp(argv[1], "set") == 0;
    bool clear = argc == 3 && strcmp(argv[1], "clear") == 0;
    if (!set && !clear) {
        return usage(k_usage);
    }
    if (ds_field < 0) {
        printf("field: %s follows the clock and can't be set\n", argv[2]);
        return 1;
    }
    if (set) {
        char *end;
        double value = strtod(argv[3], &end);
        if (end == argv[3] || *end != '\0') {
            return usage(k_usage);
        }
        long scaled = (long)(value * scale((ds_field_t)ds_field) + (value < 0 ? -0.5 : 0.5));
        ds_set(app_ds(), (ds_field_t)ds_field, (int32_t)scaled, ctx.now);
    } else {
        ds_clear(app_ds(), (ds_field_t)ds_field);
    }
    app_ui_render();
    app_ui_context(&ctx);
    print_field(&ctx, field);
    return 0;
}

static int preset_body(int argc, char **argv)
{
    static const char *const k_usage = "preset list | preset set <id>";
    ui_presets_t *p = app_presets();
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        for (int i = 0; i < p->count; i++) {
            const ui_preset_t *pr = &p->presets[i];
            printf("%c %-15s %-23s %-8s%s\n", i == p->active ? '*' : ' ', pr->id, pr->name,
                   ui_layout((ui_layout_id_t)pr->layout)->id, pr->in_cycle ? "" : " (not in the cycle)");
        }
        printf("auto-cycle %s, every %u s\n", p->cycle_enabled ? "on" : "off", (unsigned)p->cycle_interval_s);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "set") == 0) {
        int index = ui_presets_find(p, argv[2]);
        if (index < 0) {
            printf("preset: no preset \"%s\" (see `preset list`)\n", argv[2]);
            return 1;
        }
        app_ui_select(index, true);
        printf("preset: %s\n", p->presets[index].id);
        return 0;
    }
    return usage(k_usage);
}

static int cmd_field(int argc, char **argv)
{
    return diag_on_owner(field_body, argc, argv);
}

static int cmd_preset(int argc, char **argv)
{
    return diag_on_owner(preset_body, argc, argv);
}

void app_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "field", .help = "field list | get <id> | set <id> <value> | clear <id>", .func = &cmd_field },
        { .command = "preset", .help = "preset list | set <id>", .func = &cmd_preset },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: %s", cmds[i].command, esp_err_to_name(err));
        }
    }
}
```

- [ ] **Step 3: The app.**

```c
#include "app.h"

#include <sys/time.h>
#include <time.h>

#include "app_internal.h"

#include "board.h"
#include "board_buttons.h"
#include "board_pins.h"
#include "diag.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "power.h"
#include "pcf85063.h"
#include "scheduler.h"
#include "st7305.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "timekeeping.h"
#include "util_snapshot.h"
#include "util_ticks.h"

#define APP_STACK         8192 /* internal RAM: deep-sleep entry requires it */
#define APP_PRIORITY      5
#define APP_CORE          1
#define QUEUE_DEPTH       16
#define BACKUP_S          5    /* wake anyway this long after a missed RTC alarm (spec §9.2) */
#define GRACE_MS          2000 /* stay awake after boot or a button so a PC can find the board */
#define TETHER_RECHECK_MS 1000
#define RETRY_S           300  /* after a failed boot with no PC attached */
#define SNAP_MAGIC        0x72666c62u /* "rflb" */
#define SNAP_VERSION      2

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
    app_ui_state_t ui;
    time_t next_alarm;
} app_snapshot_t;

static RTC_DATA_ATTR app_snapshot_t s_snap;
static QueueHandle_t s_queue;
static time_t s_next_alarm; /* the RTC alarm: the next minute slot */
static time_t s_wake_at;    /* the earliest wake: the alarm, or a cycle switch or seconds tick before it */

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

static void schedule_next(void)
{
    sched_wake_t wake = app_ui_next_wake(time(NULL));
    s_wake_at = wake.when;
    if (wake.alarm != s_next_alarm) {
        s_next_alarm = wake.alarm;
        esp_err_t err = pcf85063_set_alarm(s_next_alarm); /* also clears the alarm flag, which releases INT */
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RTC alarm: %s; the backup timer takes over", esp_err_to_name(err));
        }
    }
}

/* The timer wake: a cycle switch or seconds tick if one comes before the alarm, else the alarm's backup. */
static time_t sleep_until(void)
{
    return s_wake_at < s_next_alarm ? s_wake_at : s_next_alarm + BACKUP_S;
}

/* A scheduled wake: the RTC alarm, its backup, a cycle switch or a seconds tick. `force` also
 * samples and renders. */
static void on_tick(bool force)
{
    esp_err_t err = timekeeping_load_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC read: %s", esp_err_to_name(err));
    }
    if (time(NULL) >= s_next_alarm) {
        s_next_alarm = 0; /* fired: program the next one, even if it is the same minute again */
    }
    app_ui_tick(force);
    schedule_next();
}

/* Dashboard bindings (spec §5.6). */
static void handle_button(board_button_t button, gesture_t gesture)
{
    power_hold_awake_ms(GRACE_MS);
    if (button == BOARD_BUTTON_KEY && gesture == GESTURE_SHORT) {
        app_ui_select(ui_presets_next(app_presets()), true);
    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_DOUBLE) {
        app_ui_toggle_cycle();
    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT) {
        app_ui_sample(time(NULL));
        app_ui_render();
        ESP_LOGI(TAG, "BOOT short: sensors refreshed");
    } else {
        ESP_LOGI(TAG, "%s %s is not bound yet (the menu comes in M3b, config mode in M4)", board_button_name(button),
                 board_gesture_name(gesture));
    }
    schedule_next();
}

/* If the clock moved back (`rtc set`, later SNTP), the pending alarm is further off than the next
 * slot from now: schedule again. Comparing with a fresh schedule stays right across DST changes
 * and for any update interval. A clock that moved forward is caught by the backup tick. */
static void check_clock_jump(void)
{
    if (s_next_alarm != 0 && app_ui_next_wake(time(NULL)).alarm < s_next_alarm) {
        ESP_LOGW(TAG, "clock moved back; scheduling again");
        on_tick(true);
    }
}

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
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
    case EV_CALL: {
        bool was_valid = timekeeping_valid();
        int64_t before = now_ms();
        ev->call.fn(ev->call.arg);
        xSemaphoreGive(ev->call.done);
        int64_t moved = now_ms() - before;
        if (timekeeping_valid() != was_valid || moved < 0 || moved > 2000) {
            on_tick(true); /* `rtc set` and friends: show the new time now, not at the next slot */
        }
        power_hold_awake_ms(GRACE_MS);
        break;
    }
    }
}

static void handle_wake(power_wake_t wake)
{
    switch (wake) {
    case POWER_WAKE_RTC:
        on_tick(false);
        break;
    case POWER_WAKE_TIMER: /* a cycle switch or seconds tick, or the alarm's backup */
        if (time(NULL) >= s_next_alarm + BACKUP_S) {
            ESP_LOGW(TAG, "RTC alarm missed; backup wake");
        }
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
    app_ui_export(&s_snap.ui);
    s_snap.next_alarm = s_next_alarm;
    util_snapshot_seal(&s_snap, sizeof(s_snap), SNAP_MAGIC, SNAP_VERSION);
    esp_err_t err = display_prepare_deep_sleep();
    if (err != ESP_OK) {
        /* Without the holds the panel would reset in deep sleep: sleep light this time instead. */
        ESP_LOGE(TAG, "panel pins: %s; light sleep this time", esp_err_to_name(err));
        display_cancel_deep_sleep();
        s_snap.hdr.magic = 0;
        handle_wake(power_sleep_light(sleep_until()));
        return;
    }
    power_sleep_deep(sleep_until());
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

/* The RTC alarm or its backup timer: back to sleep in a fraction of a second, unless the board
 * then decides to stay awake. */
static bool routine_wake(power_wake_t wake)
{
    return wake == POWER_WAKE_RTC || wake == POWER_WAKE_TIMER;
}

/* Logs, settings and the console: what a board needs once it stays awake. Routine wakes skip
 * them, because they cost time on every wake and nobody can use them (spec §3.3). */
static void come_alive(void)
{
    static bool s_alive;
    if (s_alive) {
        return;
    }
    s_alive = true;
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) {
        err = power_load_settings();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "power settings: %s", esp_err_to_name(err));
        }
    } else {
        /* Never erase NVS on our own (AGENTS.md quick rule 3): settings fall back to defaults. */
        ESP_LOGE(TAG, "NVS unavailable (%s); settings use their defaults", esp_err_to_name(err));
    }
    err = diag_start();
    if (err == ESP_OK) {
        app_register_commands();
    } else {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }
    size_t dump_addr, dump_size; /* spec §16; IDF's own boot check is off, it would run every wake */
    if (esp_core_dump_image_get(&dump_addr, &dump_size) == ESP_OK) {
        ESP_LOGW(TAG, "a %u-byte core dump is in flash; read it with `idf.py coredump-info`", (unsigned)dump_size);
    }
}

static esp_err_t boot(void)
{
    esp_err_t err = power_init();
    power_wake_t wake = power_boot_wake();
    if (!routine_wake(wake)) {
        come_alive();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "power");
    bool warm = wake != POWER_WAKE_COLD && util_snapshot_valid(&s_snap, sizeof(s_snap), SNAP_MAGIC, SNAP_VERSION);
    if (warm) {
        app_ui_import(&s_snap.ui); /* routine wakes skip storage: everything is in RTC RAM */
        s_next_alarm = s_snap.next_alarm;
    } else {
        app_ui_defaults();
        app_ui_load();
    }

    ESP_RETURN_ON_ERROR(board_init(wake == POWER_WAKE_COLD), TAG, "board");
    ESP_RETURN_ON_ERROR(pcf85063_init(board_i2c()), TAG, "RTC");
    ESP_RETURN_ON_ERROR(timekeeping_init(app_settings()->tz_posix), TAG, "time zone");
    err = timekeeping_load_from_rtc();
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
        st7305_set_lpm_rate(ST7305_LPM_1HZ); /* assumed, so send it; safe without a reset */
    } else {
        ESP_RETURN_ON_ERROR(display_init(PANEL_VARIANT), TAG, "display");
    }
    app_ui_apply_settings(); /* offsets, freshness and the panel rate, now that the panel is up */
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
    if (wake == POWER_WAKE_TIMER && time(NULL) >= s_next_alarm + BACKUP_S) {
        ESP_LOGW(TAG, "RTC alarm missed; backup wake");
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
        ESP_LOGE(TAG, "boot failed: %s; the console stays up while a PC is attached, otherwise the "
                 "board sleeps and boots again in %d s", esp_err_to_name(err), RETRY_S);
        power_boot_failed();
        power_hold_awake_ms(GRACE_MS);
    }
    for (;;) {
        if (err == ESP_OK) {
            check_clock_jump();
        }
        bool pending = uxQueueMessagesWaiting(s_queue) > 0 || board_buttons_busy();
        switch (power_plan(pending)) {
        case POWER_PLAN_LIGHT:
            handle_wake(power_sleep_light(sleep_until()));
            continue;
        case POWER_PLAN_DEEP:
            enter_deep_sleep(); /* returns only if it had to sleep light instead */
            continue;
        case POWER_PLAN_RETRY:
            power_sleep_retry(RETRY_S);
            break;
        case POWER_PLAN_AWAKE:
            come_alive();
            break;
        }
        int64_t wait_ms = TETHER_RECHECK_MS; /* a failed boot has no schedule to wait for */
        if (err == ESP_OK) {
            int64_t due_ms = (int64_t)sleep_until() * 1000 - now_ms();
            wait_ms = due_ms < wait_ms ? due_ms : wait_ms;
        }
        TickType_t wait = wait_ms <= 0 ? 0 : util_ticks_at_least((uint32_t)wait_ms, portTICK_PERIOD_MS);
        app_event_t ev;
        if (xQueueReceive(s_queue, &ev, wait) == pdTRUE) {
            handle_event(&ev);
        } else if (err == ESP_OK && time(NULL) >= s_next_alarm + BACKUP_S) {
            ESP_LOGW(TAG, "RTC alarm missed; backup tick");
            on_tick(false);
        } else if (err == ESP_OK && time(NULL) >= s_wake_at) {
            on_tick(false); /* a cycle switch or seconds tick */
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

```cmake
idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_cmds.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES board console datastore diag display esp_app_format esp_driver_gpio espcoredump
                                     gfx locale nvs_flash power rtc scheduler sensors st7305 storage timekeeping ui
                                     util)
```

Run: `tools/idf.sh reconfigure && tools/idf.sh build`
Expected: passes with no warnings. `sdkconfig` gains `CONFIG_REFLBO_TZ_NAME="Europe/Prague"`: a new option takes its default, so deleting `sdkconfig` is not needed.

- [ ] **Step 4: Commit.**

```bash
git add main
git commit -m "feat(main): show dashboards with presets, cycling and saved settings"
```

- [ ] **Step 5: Remove the M2 clock screen.**

```bash
git rm components/ui/ui_clock.c components/ui/include/ui_clock.h test/host/test_ui_clock_golden.c \
       test/host/render_clock.c test/host/ui_fixtures.h test/host/golden/clock_valid.pbm \
       test/host/golden/clock_invalid.pbm test/host/golden/clock_cold.pbm
```

```cmake
# Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                            "ui_status.c" "ui_dashboard.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx locale datastore util
                       PRIV_REQUIRES json)
```

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
DASHBOARDS = ["home", "indoor", "weather", "focus", "home_invalid", "home_stale", "home_12h_charging", "indoor_cold",
              "focus_seconds", "home_battery_details", "home_inverted"]  # test/host/dashboard_fixtures.h
RENDERERS = {
    "test_pattern": ["render_test_pattern"],
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
```

The host build in its final form:

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

# cmake -DREFLBO_SANITIZE=ON: everything with AddressSanitizer and UndefinedBehaviorSanitizer.
option(REFLBO_SANITIZE "Build the host tests with ASan and UBSan" OFF)
if(REFLBO_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()

add_library(unity STATIC third_party/unity/unity.c)
target_include_directories(unity PUBLIC third_party/unity)

# Components under test: the same sources the firmware compiles.
file(GLOB UTIL_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/util/*.c)
add_library(util STATIC ${UTIL_SOURCES})
target_link_libraries(util PUBLIC m)
target_include_directories(util PUBLIC ${REPO_ROOT}/components/util/include)
target_compile_options(util PRIVATE ${REFLBO_WARNINGS})

# gfx: every source in the component, generated fonts and icons included.
file(GLOB GFX_SOURCES CONFIGURE_DEPENDS
     ${REPO_ROOT}/components/gfx/*.c ${REPO_ROOT}/components/gfx/fonts/*.c ${REPO_ROOT}/components/gfx/icons/*.c)
add_library(gfx STATIC ${GFX_SOURCES})
target_include_directories(gfx PUBLIC ${REPO_ROOT}/components/gfx/include)
target_compile_options(gfx PRIVATE ${REFLBO_WARNINGS})

# st7305: only the pure-C frame conversion builds on the host.
add_library(st7305_frame STATIC ${REPO_ROOT}/components/st7305/st7305_frame.c)
target_include_directories(st7305_frame PUBLIC ${REPO_ROOT}/components/st7305/include)
target_compile_options(st7305_frame PRIVATE ${REFLBO_WARNINGS})

# scheduler: pure C.
add_library(scheduler STATIC ${REPO_ROOT}/components/scheduler/scheduler.c)
target_include_directories(scheduler PUBLIC ${REPO_ROOT}/components/scheduler/include)
target_compile_options(scheduler PRIVATE ${REFLBO_WARNINGS})

# board: only the pure gesture recogniser builds on the host.
add_library(board_logic STATIC ${REPO_ROOT}/components/board/gesture.c)
target_include_directories(board_logic PUBLIC ${REPO_ROOT}/components/board/include)
target_compile_options(board_logic PRIVATE ${REFLBO_WARNINGS})

# sensors: only the SHTC3 codec and the battery model build on the host.
add_library(sensors_logic STATIC ${REPO_ROOT}/components/sensors/shtc3_codec.c
                                 ${REPO_ROOT}/components/sensors/battery_model.c)
target_include_directories(sensors_logic PUBLIC ${REPO_ROOT}/components/sensors/include)
target_compile_options(sensors_logic PRIVATE ${REFLBO_WARNINGS})

# rtc: only the PCF85063 register codec builds on the host.
add_library(rtc_logic STATIC ${REPO_ROOT}/components/rtc/pcf85063_regs.c)
target_include_directories(rtc_logic PUBLIC ${REPO_ROOT}/components/rtc/include)
target_compile_options(rtc_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(rtc_logic PUBLIC util)

# timekeeping: only the ISO 8601 parser builds on the host.
add_library(timekeeping_logic STATIC ${REPO_ROOT}/components/timekeeping/timekeeping_iso.c)
target_include_directories(timekeeping_logic PUBLIC ${REPO_ROOT}/components/timekeeping/include)
target_compile_options(timekeeping_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(timekeeping_logic PUBLIC util)

# power: only the sleep policy builds on the host.
add_library(power_logic STATIC ${REPO_ROOT}/components/power/power_policy.c)
target_include_directories(power_logic PUBLIC ${REPO_ROOT}/components/power/include)
target_compile_options(power_logic PRIVATE ${REFLBO_WARNINGS})

# locale: pure C language packs.
add_library(locale STATIC ${REPO_ROOT}/components/locale/lang.c ${REPO_ROOT}/components/locale/lang_en.c)
target_include_directories(locale PUBLIC ${REPO_ROOT}/components/locale/include)
target_compile_options(locale PRIVATE ${REFLBO_WARNINGS})

# datastore: pure C field store.
add_library(datastore STATIC ${REPO_ROOT}/components/datastore/datastore.c)
target_include_directories(datastore PUBLIC ${REPO_ROOT}/components/datastore/include)
target_compile_options(datastore PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(datastore PUBLIC m)

# cJSON: the copy bundled with ESP-IDF, so the host tests parse exactly like the firmware.
set(REFLBO_IDF_PATH "$ENV{REFLBO_IDF_PATH}")
if(NOT REFLBO_IDF_PATH)
    set(REFLBO_IDF_PATH "$ENV{HOME}/esp/esp-idf-v5.5.5")
endif()
add_library(cjson STATIC ${REFLBO_IDF_PATH}/components/json/cJSON/cJSON.c)
target_include_directories(cjson PUBLIC ${REFLBO_IDF_PATH}/components/json/cJSON)
target_compile_options(cjson PRIVATE -Wno-deprecated-declarations) # its sprintf calls; third-party code
target_link_libraries(cjson PUBLIC m)

# storage: the settings codec and the config-file logic build on the host; the mount does not.
add_library(storage_logic STATIC
            ${REPO_ROOT}/components/storage/settings.c ${REPO_ROOT}/components/storage/storage_file.c)
target_include_directories(storage_logic PUBLIC ${REPO_ROOT}/components/storage/include)
target_compile_options(storage_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(storage_logic PRIVATE cjson m)

# ui: pure C on top of gfx, locale and the datastore.
file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
add_library(ui STATIC ${UI_SOURCES})
target_include_directories(ui PUBLIC ${REPO_ROOT}/components/ui/include)
target_compile_options(ui PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(ui PUBLIC gfx locale datastore util PRIVATE cjson m)

# reflbo_host_test(<name> <libs...>): builds <name>.c against Unity and registers it with ctest.
function(reflbo_host_test name)
    add_executable(${name} ${name}.c)
    target_compile_options(${name} PRIVATE ${REFLBO_WARNINGS})
    target_link_libraries(${name} PRIVATE unity ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

reflbo_host_test(test_util_crc32 util)
reflbo_host_test(test_util_base64 util)
reflbo_host_test(test_util_ticks util)
reflbo_host_test(test_gfx gfx)
reflbo_host_test(test_gfx_text gfx)
reflbo_host_test(test_gfx_fonts gfx)
reflbo_host_test(test_st7305_frame st7305_frame)
reflbo_host_test(test_util_snapshot util)
reflbo_host_test(test_util_time util)
reflbo_host_test(test_util_calendar util)
reflbo_host_test(test_scheduler scheduler)
reflbo_host_test(test_gesture board_logic)
reflbo_host_test(test_shtc3_codec sensors_logic)
reflbo_host_test(test_battery_model sensors_logic)
reflbo_host_test(test_pcf85063_regs rtc_logic)
reflbo_host_test(test_timekeeping_iso timekeeping_logic)
reflbo_host_test(test_power_policy power_logic)
reflbo_host_test(test_lang locale)
reflbo_host_test(test_datastore datastore)
reflbo_host_test(test_ui_fields ui)
reflbo_host_test(test_ui_preset ui)
reflbo_host_test(test_settings storage_logic)
reflbo_host_test(test_storage_file storage_logic)
target_compile_definitions(test_storage_file PRIVATE TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/storage_tmp")

reflbo_host_test(test_test_pattern_golden gfx)
target_compile_definitions(test_test_pattern_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(render_test_pattern render_test_pattern.c)
target_compile_options(render_test_pattern PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(render_test_pattern PRIVATE gfx)

reflbo_host_test(test_ui_dashboard_golden ui)
target_compile_definitions(test_ui_dashboard_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(render_dashboard render_dashboard.c)
target_compile_options(render_dashboard PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(render_dashboard PRIVATE ui)

# Python tool tests (stdlib unittest; pyserial is not needed).
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
add_test(NAME tools_unittests
         COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -t . -v
         WORKING_DIRECTORY ${REPO_ROOT}/tools)
set_tests_properties(tools_unittests PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1") # no tools/__pycache__
```

Run:

```bash
rm -rf build-host && cmake -S test/host -B build-host -G Ninja && cmake --build build-host \
  && ctest --test-dir build-host --output-on-failure
cmake -S test/host -B build-host-asan -G Ninja -DREFLBO_SANITIZE=ON && cmake --build build-host-asan \
  && ctest --test-dir build-host-asan --output-on-failure
python3 tools/render.py && tools/idf.sh build
```

Expected:
- `100% tests passed, 0 tests failed out of 26`, in both builds.
- `render.py` writes the test pattern and the 11 dashboards.
- The firmware builds with no warnings.

```bash
git add components/ui/CMakeLists.txt tools/render.py test/host/CMakeLists.txt
git commit -m "refactor(ui): remove the M2 clock screen"
```

- [ ] **Step 6: Check on the board (M3a acceptance, spec §18).** Confirm the port as in Task 12, then flash and capture the cold boot:

```bash
tools/idf.sh -p /dev/cu.usbmodem2101 flash
tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/m3a-boot.log
grep -E "storage:|app_ui:|reflbo ready|(^|reflbo> )[EW] \(" captures/m3a-boot.log
```

Expected:
- `storage: LittleFS: <n> of 8000 KB used`.
- For each of `/fs/cfg/settings.json` and `/fs/cfg/presets.json`, one line: `… loaded`, or `…: not there yet; using defaults`. A file written by an earlier build loads; missing option keys take their defaults.
- `reflbo ready (cold wake)`, and no `E` or `W` lines.

Set the clock, show Home and look at it:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "rtc set $(date -u +%Y-%m-%dT%H:%M:%SZ)" --cmd "preset set home" \
  --cmd "preset list" --cmd "field list" -o captures/m3a-home.log
tools/idf.sh exec python tools/screenshot.py -o captures/m3a-home.png
```

Expected:
- `preset list`:
  - four rows: `* home … classic`, `indoor … grid`, `weather … weather (not in the cycle)` and `focus … focus`;
  - then `auto-cycle off, every 60 s`, unless a saved file says otherwise.
- `field list` shows `env.temp`, `env.hum`, `env.dew`, `env.temp_min`, `env.temp_max` and `bat.level` as fresh. The weather and sun fields are missing.
- The screenshot is the Classic layout:
  - the Mac's local time, today's date;
  - temperature and humidity as in `field list`;
  - the moon phase and the battery;
  - the date and battery in the status bar.

KEY switches presets, and the choice survives a reboot:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "btn key short" --cmd "preset list" -o captures/m3a-key.log
tools/idf.sh exec python tools/screenshot.py -o captures/m3a-indoor.png
tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/m3a-reboot.log
tools/idf.sh exec python tools/devlog.py --cmd "preset list"
```

Expected:
- After `btn key short`: `app_ui: preset indoor` and `* indoor`.
- The screenshot is the six-cell Grid:
  - temperature, humidity, dew point, today's low and high, days left;
  - the clock in the middle of the status bar, if the loaded file has `status_clock` for Indoor or no file existed.
- After the reset: `/fs/cfg/presets.json loaded`, and `preset list` still shows `* indoor`.

Injected data renders at once:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "preset set home" --cmd "field set env.temp -5.5"
tools/idf.sh exec python tools/screenshot.py -o captures/m3a-field.png
tools/idf.sh exec python tools/devlog.py --cmd "btn boot short" --cmd "field get env.temp"
```

Expected:
- `field set` answers with a line starting `env.temp      fresh   -5.5 °C`.
- The screenshot's temperature cell shows −5.5.
- BOOT short samples the SHTC3 again, and `field get` shows the room temperature.

Auto-cycle switches after its interval, and a reboot doesn't switch at once:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "preset set indoor" --cmd "btn key double" --until "app_ui: preset focus" \
  -t 90 -o captures/m3a-cycle.log
tools/idf.sh exec python tools/devlog.py --reset -t 40 -o captures/m3a-cycle-boot.log
grep -c "app_ui: preset " captures/m3a-cycle-boot.log
tools/idf.sh exec python tools/devlog.py --until "app_ui: preset focus" -t 60
tools/idf.sh exec python tools/devlog.py --cmd "btn key double" --cmd "preset set home" --cmd "preset list"
```

Expected:
- `app_ui: auto-cycle on`, then within 60–65 s `app_ui: preset focus`; devlog exits 0.
- The 40 s after the reset have no switch: `grep -c` prints `0`.
- The next switch, from the saved `indoor` to `focus`, arrives within the following 60 s: devlog exits 0.
- Finally `auto-cycle off` and `* home`.

A clock moved back redraws at once:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "rtc set $(date -u -v-3H +%Y-%m-%dT%H:%M:%SZ)"
tools/idf.sh exec python tools/screenshot.py -o captures/m3a-jump.png
tools/idf.sh exec python tools/devlog.py --cmd "rtc set $(date -u +%Y-%m-%dT%H:%M:%SZ)"
```

Expected: the screenshot, taken seconds later, shows the local time three hours back.

Deep sleep keeps the preset and the data. `env.temp_min` makes a good probe: new readings never raise a low.

```bash
tools/idf.sh exec python tools/devlog.py --cmd "power idle" --cmd "field set env.temp_min -20" --cmd "sleep test deep 2"
sleep 150   # each cycle sleeps to the next minute slot
tools/idf.sh exec python tools/devlog.py --cmd "sleep stats" --cmd "preset list" --cmd "field get env.temp_min" \
  --cmd "panel fps 5" -o captures/m3a-deep.log
tools/idf.sh exec python tools/devlog.py --cmd "field clear env.temp_min" --cmd "btn boot short"
```

Expected:
- `sleep: 0 light, 2 deep; wakes: rtc 2`, with the last awake phase under 100 ms.
- `* home`.
- `env.temp_min  fresh   -20.0 °C`: the datastore came through RTC RAM.
- `panel fps` prints `1.00 Hz`.

Leave the idle strategy as `power idle` printed it at the start.


---

### Task 14: Documentation

**Files:**
- Modify: `AGENTS.md`, `docs/specs/2026-09-25-firmware-design.md`

**Interfaces:** none. This task records what Tasks 1–13 built, as AGENTS.md rule 6 requires. The M2 review's doc minors are included: spec §3.2, gotcha 4, gotcha 7, and the M0 notes on `uv python install 3.13`, `reconfigure` and the stale *(planned)*.

The changes:
- **AGENTS.md**
  - §2: status.
  - §3.4: gotchas 4 and 7.
  - §3.5: the stale *(planned)*.
  - §5.2: `locale`.
  - §6: the Python 3.13 install, the sanitizer build, the dashboard commands, LittleFS and goldens.
  - §7: the `field` and `preset` commands are available.
  - §8: `sdkconfig.defaults`.
- **Spec, revision r9**
  - §3.1 and §3.2.
  - §4.4 and §4.5: fonts and icons.
  - §5.2–§5.4: status bar, widgets, presets and validation.
  - §6: the datastore as built.
  - §14.3: the settings keys read, and `/fs`.
  - §15, §17 and §21.

- [ ] **Step 1: Apply the edits.** Save this script outside the repository (a scratch directory) and run it from the repo root. Each edit must match exactly once, or the script stops and changes nothing on disk for that file.

```python
"""M3a documentation updates (Task 14). Every edit must match exactly once."""
import pathlib

EDITS = {
    "AGENTS.md": [
        # §2: status
        ("M3 is in progress as two plans: M3a (storage, datastore, locale, layouts, widgets, presets) and then M3b "
         "(menu, screens, settings, schedule and night sleep, Czech pack).",
         "M3 runs as two plans. M3a is done: LittleFS config files, the datastore with the extra fields, the English "
         "pack, four layouts with widgets and a status bar, and presets that KEY switches and auto-cycles and that "
         "survive a reboot. M3b is next (menu, screens, settings, schedule and night sleep, Czech pack); its plan "
         "gets written before it starts."),
        # §3.4 gotcha 4: the owner watched the image through deep sleep (M2 Task 14)
        ("Checked at M2: the panel still ran at 1.00 Hz (`panel fps`) after deep sleep, so it kept its image.",
         "Checked at M2: the owner watched six deep-sleep cycles and the image stayed, updating every minute; "
         "afterwards `panel fps` still measured 1.00 Hz."),
        # §3.4 gotcha 7: the measured drift
        ("so expect the RTC to run slow (estimate 2–9 s/day); trim the Offset register against NTP at M5.",
         "so the RTC runs slow: about 3.4 s/day at M2 (9 s in 2.6 days, against an estimate of 2–9 s/day). Trim the "
         "Offset register against NTP at M5."),
        # §3.5
        ("gets listed in `THIRD_PARTY.md` *(planned)*.", "gets listed in `THIRD_PARTY.md`."),
        # §5.2
        ("  locale/        language packs (en in v1)                            [host]",
         "  locale/        language packs: en, cs (M3b); API prefix lang_       [host]"),
        # §6: a fresh Mac needs uv's Python 3.13 before the shim links
        ("mkdir -p ~/esp/python-shim\n",
         "~/.local/bin/uv python install 3.13        # a fresh Mac has none yet\nmkdir -p ~/esp/python-shim\n"),
        ("tools/idf.sh exec python tools/devlog.py --cmd \"sleep test deep 2\" # sleep cycles while tethered; then: sleep stats\n",
         "tools/idf.sh exec python tools/devlog.py --cmd \"sleep test deep 2\" # sleep cycles while tethered; then: sleep stats\n"
         "tools/idf.sh exec python tools/devlog.py --cmd \"preset list\" --cmd \"field set env.temp -5.5\"   # redraws at once\n"),
        ("cmake -S test/host -B build-host -G Ninja && cmake --build build-host \\\n"
         "  && ctest --test-dir build-host --output-on-failure\n",
         "cmake -S test/host -B build-host -G Ninja && cmake --build build-host \\\n"
         "  && ctest --test-dir build-host --output-on-failure\n"
         "cmake -S test/host -B build-host-asan -G Ninja -DREFLBO_SANITIZE=ON && cmake --build build-host-asan \\\n"
         "  && ctest --test-dir build-host-asan --output-on-failure   # the same tests with ASan and UBSan\n"),
        ("silently leaves the new component out.\n",
         "silently leaves the new component out.\n"
         "- Config files live on LittleFS, mounted at `/fs`: `/fs/cfg/settings.json` and `/fs/cfg/presets.json`, each "
         "with a `.bak` of the previous version (spec §14.3). The first boot formats a blank `storage` partition; "
         "`idf.py flash` never writes it.\n"
         "- Golden renders: after an intentional UI change, rewrite a golden with `build-host/render_dashboard <fixture> "
         "test/host/golden/dash_<fixture>.pbm` (fixtures in `test/host/dashboard_fixtures.h`), look at the PNGs from "
         "`tools/render.py`, then commit.\n"),
        # §7: the console
        ("`sleep stats [reset]|test <deep|light> <n>`. Planned: `field list|get|set`, `preset list|set`, "
         "`wifi status|scan`, `sync now`, `audio tone`.",
         "`sleep stats [reset]|test <deep|light> <n>`, `field list|get <id>|set <id> <value>|clear <id>`, "
         "`preset list|set <id>`. Planned: `wifi status|scan`, `sync now`, `audio tone`."),
        # §8: reconfigure doesn't re-apply sdkconfig.defaults (M0 review)
        ("- Change configuration through `sdkconfig.defaults`, then run `idf.py reconfigure` or delete `sdkconfig`.",
         "- Change configuration through `sdkconfig.defaults`, then delete `sdkconfig` and build. An existing "
         "`sdkconfig` keeps the values it has, so `reconfigure` only adds options that are new."),
    ],
    "docs/specs/2026-09-25-firmware-design.md": [
        # §3.1
        ("| `locale` | Language packs: strings, date and number formats | — | ✓ |",
         "| `locale` | Language packs (API prefix `lang_`, because libc owns `locale_t`): strings, date and number "
         "formats, name days and holidays | — | ✓ |"),
        ("| `datastore` | Field registry, values, freshness, change events, snapshot | — | ✓ |",
         "| `datastore` | Measured and fetched values, freshness, derived values and trends, change mask (§6) | — | ✓ |"),
        ("| `ui` | Layouts, widgets, presets, cycler, screens, menu, input handling | gfx, locale, datastore | ✓ |",
         "| `ui` | Field catalogue, layouts, widgets, status bar, presets and their JSON codec, cycle order, screens, "
         "menu, input handling | gfx, locale, datastore, util | ✓ |"),
        ("| `storage` | NVS (identity, secrets), LittleFS config files, microSD mount | IDF | JSON schemas |",
         "| `storage` | NVS (identity, secrets), LittleFS config files, microSD mount | IDF | settings codec, "
         "config-file backup logic |"),
        # §3.2 (M2 review)
        ("3. When the queue is empty and the power state allows it, the app task calls `power_idle()` (§3.4).",
         "3. When the queue is empty, the app task asks `power_plan()` whether and how to sleep, then calls "
         "`power_sleep_light()` or `power_sleep_deep()` (§3.4)."),
        # §4.4
        ("- **Sizes.** Initial: text 12, 16, 20, 28 px; numeric 48, 72, 110 px. Tuned in M1/M3.\n"
         "- **Choice.** Specific fonts are picked in M1, for example a pixel font for small sizes and a clean sans for "
         "large digits. Every font needs a licence that allows redistribution in this repository.",
         "- **Sizes (M3a).** Text: DejaVu Sans 12, 16 and 20 px, and Sans Bold 16, 20 and 28 px. Numeric: DejaVu Sans "
         "Condensed Bold 48, 72, 110 and 130 px. `fontgen` trims blank glyph rows and columns, and grows the ascent "
         "and line height to fit every glyph's ink.\n"
         "- **Choice.** DejaVu 2.37, picked at M1 (`THIRD_PARTY.md`). Every font needs a licence that allows "
         "redistribution in this repository."),
        # §4.5
        ("- A 1-bpp icon set generated into C bitmaps by `tools/imggen.py`, at sizes 16, 24, 48 and 64 px.\n"
         "- Contents: weather codes (day and night variants), battery levels, charging, Wi-Fi, sync/stale, alarm bell, "
         "thermometer, humidity, sunrise, sunset.\n",
         "- A 1-bpp icon set generated into C bitmaps by `tools/imggen.py` (`tools/gen_icons.sh`) from Google's "
         "Material Icons font (Apache-2.0, pinned to one upstream commit). `assets/icons/icons.txt` lists each icon's "
         "name and sizes.\n"
         "- M3a: thermometer, drop, dew, bolt (charging), stale, clock, calendar, person, celebration and cloud, at 16, "
         "24 or 48 px as the list says. The battery and the moon are drawn with primitives. Weather codes (day and "
         "night), Wi-Fi, sync, the alarm bell, sunrise and sunset join with their milestones.\n"),
        # §5.2: the status bar the owner asked for at the M3a render review
        ("Every layout can show a status bar (top 20 px). It carries the battery icon and %, charging state, "
         "Wi-Fi/sync state or a stale warning, and the next alarm (M7).",
         "Every layout has a status bar (top 20 px):\n\n"
         "- Left: \"Set time\" while the time is invalid (§5.3); otherwise a stale warning when a shown value is "
         "stale.\n"
         "- Middle: a small clock, if the preset sets `status_clock`. It is meant for data-first presets (owner "
         "request, 2026-09-28).\n"
         "- Right: the charging bolt and the battery icon, with the parts `status_battery` lists: level %, voltage, "
         "days left. The default is the level.\n"
         "- Later: Wi-Fi and sync state (M4, M5) and the next alarm (M7)."),
        ("- Slot rectangles are fixed per layout and defined in code. They are finalised in M3 from host renders the "
         "owner reviews.",
         "- Slot rectangles are fixed per layout and defined in code (`components/ui/ui_layout.c`). The owner "
         "approved them from the M3a host renders on 2026-09-28."),
        # §5.3
        ("- There is one renderer per field kind and size class (XL/L/M/S). For example, a number widget shows label, "
         "value and unit, while the S size shows only an icon and the value.",
         "- There is one renderer per field kind and size class (XL/L/M/S). For example, a number widget in M shows "
         "label, value and unit; in S it shows an icon, the value and a short label. XL steps its font down (130, "
         "110, 72, 48 px) until the value fits. Text that still doesn't fit ends in an ellipsis, and a widget never "
         "draws outside its slot."),
        ("  - `stale`: show the value with an age marker, e.g. \"⟲ 2 d\".",
         "  - `stale`: show the value with an age marker: the stale icon and the age, e.g. \"2 d\", at the slot's "
         "bottom right."),
        # §5.4
        ("        \"s1\": \"env.temp\", \"s2\": \"env.hum\", \"s3\": \"wx.now\", \"s4\": \"bat.level\"\n"
         "      },\n"
         "      \"options\": { \"clock_24h\": true, \"seconds\": false, \"invert\": false, \"stale_policy\": \"stale\" }\n",
         "        \"s1\": \"env.temp\", \"s2\": \"env.hum\", \"s3\": \"moon.phase\", \"s4\": \"bat.level\"\n"
         "      },\n"
         "      \"options\": { \"clock_24h\": true, \"seconds\": false, \"invert\": false, \"stale_policy\": \"stale\",\n"
         "                   \"status_clock\": false, \"status_battery\": [\"percent\"] }\n"),
        ("- **Built-in defaults.** Home (Classic), Weather and Focus clock are compiled in. They are used when the file "
         "is missing or invalid. There can be at most 16 presets.",
         "- **Built-in defaults.** Home (Classic), Indoor (Grid, with the status clock), Weather and Focus clock are "
         "compiled in. They are used when the file is missing or invalid. Weather stays out of the cycle until M5 "
         "brings weather data. There can be at most 16 presets.\n"
         "- **Options.** `clock_24h` overrides the time setting when present. `status_clock` and `status_battery` "
         "shape the status bar (§5.2).\n"
         "- **Validation.** A file with a structural error is rejected as a whole, and the error names it. Structural "
         "errors:\n"
         "  - not JSON, or another schema;\n"
         "  - no presets, or more than 16;\n"
         "  - a bad or duplicate id;\n"
         "  - an unknown layout, slot or field, or a field the slot can't show;\n"
         "  - an unknown `stale_policy` or `status_battery` value.\n\n"
         "  Everything else is lenient:\n"
         "  - an unknown `active` selects the first preset;\n"
         "  - the cycle interval is clamped to 10 s–1 h;\n"
         "  - a missing name takes the id;\n"
         "  - missing options take their defaults;\n"
         "  - `null` or `\"\"` leaves a slot empty."),
        ("  - The web UI or the menu.\n",
         "  - The web UI or the menu.\n\n"
         "  A manual switch is saved in `presets.json`, so it survives a reboot. An auto-cycle switch is not saved.\n"),
        # §6: the datastore as built
        ("- **Table.** An entry for each built-in field, plus up to 32 dynamic `mqtt.<key>` entries.\n"
         "- **Entry contents.**\n"
         "  - Id and kind.\n"
         "  - Value: a number (float plus precision), short text (at most 48 bytes of UTF-8), a time, or a weather "
         "struct/series.\n"
         "  - Unit and label.\n"
         "  - `updated_at` (UTC) and `ttl_s`.\n"
         "  - Flags.\n",
         "- **Table.** An entry for each measured or fetched field: `env.*` and `bat.*` since M3a, weather from M5, and "
         "up to 32 dynamic `mqtt.<key>` entries from M6. Fields that follow from the clock (`time.*`, `date.*`, "
         "`moon.phase`) are computed by `ui` at render time and never stored.\n"
         "- **Entry contents.**\n"
         "  - A fixed-point value: 0.01 °C, 0.01 %, whole %, or 0.1 days. Short text (at most 48 bytes of UTF-8), "
         "times and weather structs arrive with the fields that need them.\n"
         "  - The trend and `updated` (UTC); a `ttl_s` per field. The battery entry also holds the voltage and the "
         "charging state.\n"
         "  - Units and labels live in the `ui` field catalogue.\n"
         "- **Derived values.** Each SHTC3 reading also sets:\n"
         "  - `env.dew`, from the Magnus formula (17.62 and 243.12 °C);\n"
         "  - today's `env.temp_min` and `env.temp_max`, restarted at local midnight;\n"
         "  - the trends: the change since the newest reading that is 60–90 min old, from a 16-point history spaced "
         "at least 5 min apart.\n"),
        ("- **API.** Typed setters and getters, a freshness check, and a change mask posted as `DATA_CHANGED`. A mutex "
         "protects the table; getters copy values out.\n"
         "- **Snapshot.** Binary format: magic, version, CRC32, entries, at most 4 KB in total. It is written:\n"
         "  - To RTC RAM before every idle.\n"
         "  - To `/state/datastore.bin` after every sync, so data survives a power-off and reappears marked as stale.",
         "- **API.** Setters and getters, a freshness check (missing, fresh, stale) and a change mask. The app task owns "
         "the datastore (`AGENTS.md` §5.3). Other tasks reach it through events, and console commands through the "
         "app's executor, so it needs no mutex.\n"
         "- **Snapshot.** The datastore is plain data inside the app's RTC-RAM snapshot (magic, version, CRC32, at "
         "most 4 KB in total), sealed before every deep sleep. From M5 it is also written to `/state/datastore.bin` "
         "after every sync, so data survives a power-off and reappears marked as stale."),
        # §14.3
        ("- If a file is invalid, the firmware uses `*.bak`; if that is invalid too, it uses defaults and shows a "
         "toast.",
         "- If a file is invalid, the firmware uses `*.bak`; if that is invalid too, it uses defaults and shows a "
         "toast (from M3b; M3a logs it).\n"
         "- The partition is mounted at `/fs`, so the files are `/fs/cfg/settings.json` and so on."),
        ("            \"discovery_prefix\": \"homeassistant\", \"discovery\": true }\n}\n```\n",
         "            \"discovery_prefix\": \"homeassistant\", \"discovery\": true }\n}\n```\n\n"
         "M3a reads `language`, `time.tz_iana`, `time.tz_posix`, `time.clock_24h`, `units.temp`, `sensors.*`, "
         "`display.update_min` and `display.lpm_hz`. The file must be a JSON object with `\"schema\": 1`; beyond that, "
         "a missing or mistyped key takes its default and an out-of-range number is clamped, so one bad value never "
         "resets the rest. Saving keeps the keys the firmware doesn't know.\n"),
        # §15
        ("| `field list` · `field get <id>` · `field set <id> <value>` | Inspect and inject data, e.g. fixtures on the "
         "device |",
         "| `field list` · `field get <id>` · `field set <id> <value>` · `field clear <id>` | Inspect and inject data, "
         "e.g. fixtures on the device |"),
        ("| `tools/fontgen.py` · `tools/imggen.py` | Turn fonts and icons into C sources | Pillow (tools venv) |",
         "| `tools/fontgen.py` · `tools/imggen.py`, run by `tools/gen_fonts.sh` · `tools/gen_icons.sh` | Turn fonts and "
         "icons into C sources | uv, Pillow |"),
        ("pyserial comes from the ESP-IDF Python environment. Pillow lives in a separate tools venv "
         "(`tools/requirements.txt`).",
         "pyserial comes from the ESP-IDF Python environment. The generators run through `uv` with the pinned versions "
         "in `tools/requirements.txt`."),
        # §17
        ("  - Preset and settings JSON: validation and migrations.\n",
         "  - Preset and settings JSON: validation and migrations.\n"
         "  - Config files: the atomic write and the `.bak` fallback, in a scratch directory.\n"),
        ("Each render is compared with `test/host/golden/*.pbm`. After the owner reviews the PNGs, `--update` rewrites "
         "the goldens.",
         "Each render is compared with `test/host/golden/*.pbm`. After an intentional change, the renderer rewrites "
         "the golden (`build-host/render_dashboard <fixture> <file>`), and the owner reviews the PNGs from "
         "`tools/render.py`.\n"
         "- **Sanitizers.** `-DREFLBO_SANITIZE=ON` builds the host tests with AddressSanitizer and "
         "UndefinedBehaviorSanitizer."),
        # §21
        ("(§5.7); M3 split into M3a and M3b (§18) |\n",
         "(§5.7); M3 split into M3a and M3b (§18) |\n"
         "| r9 | 2026-09-29 | M3a as built: the status bar's `status_clock` and `status_battery` options and the "
         "built-in Indoor preset, from the owner's render review (§5.2, §5.4); preset validation (§5.4); widget sizing "
         "and ellipsis (§5.3); the datastore's scope, ownership and snapshot (§6); fonts and Material icons (§4.4, "
         "§4.5); the `lang_` prefix and storage's host-tested parts (§3.1); `power_plan()` in the runtime model "
         "(§3.2); the settings keys read and the `/fs` mount (§14.3); `field clear` (§15); golden updates and the "
         "sanitizer build (§17) |\n"),
    ],
}

for name, edits in EDITS.items():
    path = pathlib.Path(name)
    text = path.read_text()
    for old, new in edits:
        count = text.count(old)
        assert count == 1, f"{name}: {count} matches for {old[:70]!r}"
        text = text.replace(old, new)
    path.write_text(text)
    print(f"{name}: {len(edits)} edits")
```

Run: `python3 <scratch>/m3a_docs.py && git diff --stat`
Expected:
- `AGENTS.md: 11 edits` and `docs/specs/2026-09-25-firmware-design.md: 24 edits`.
- The diff touches only those two files.

- [ ] **Step 2: Read the result.** Read both diffs whole. Check that:
  - every command they name exists (`field clear`, `render_dashboard`, `-DREFLBO_SANITIZE`, `tools/gen_icons.sh`);
  - the §6 block in AGENTS.md still renders as one code block;
  - nothing still calls `field`, `preset` or the icons *(planned)*.

Run: `grep -n "planned" AGENTS.md | grep -E "field|preset|THIRD_PARTY"`
Expected: no output.

- [ ] **Step 3: Commit.**

```bash
git add AGENTS.md docs/specs/2026-09-25-firmware-design.md
git commit -m "docs: record M3a as built in AGENTS.md and the spec (r9)"
```
