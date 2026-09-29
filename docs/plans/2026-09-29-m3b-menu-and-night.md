# M3b: Menu, Night Sleep and Czech, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish M3 (spec §18) with the second plan, M3b:
- the on-device menu, with settings editing;
- toasts and the critical-battery screen;
- the preset schedule, with timed night sleep;
- the LPM rate as a setting;
- the Czech pack with public holidays (D16);
- held buttons left out of the wake sources (D16).

**Architecture:**

- **Pure logic, host-tested:**
  - `util`: Easter Sunday, for the Czech holidays; a JSON nesting check that runs before cJSON.
  - `locale`: the `cs` pack (dates, numbers, moon names, public holidays, no name days) and the strings for the menu, toasts and screens in both packs.
  - `timekeeping`: the menu's short list of time zones.
  - `scheduler`: the local-time rules of spec §9.2 and the schedule entry wake.
  - `ui`:
    - the schedule in `presets.json` and the order its entries run in;
    - stricter `presets.json` parsing;
    - the menu state machine;
    - the menu, toast and critical screens;
    - the low-battery mark;
    - two-line names in narrow slots.
  - `storage`: a nesting limit for `settings.json`, and a fallback load that keeps the good backup.
  - `sensors`: the humidity offset clamped to 0–100 %, and the critical-battery threshold with hysteresis.
- **Device side:**
  - `st7305` and `display`: panel sleep-in (through HPM), and waking it with the init sequence.
  - `storage`: the factory erase, which formats the partition.
  - `power` and `board`:
    - a held button left out of the wake sources;
    - the critical sleep, woken by KEY only;
    - gesture timings per context;
    - a button ignored until it is released.
  - `main`:
    - `app_menu.c`: the menu's model and intents, the panel in HPM while it is open, the 60 s timeout;
    - toasts;
    - settings saved over the file's unknown keys;
    - the schedule;
    - night sleep with its 60 s peek;
    - the critical screen;
    - the `night` and `schedule` console commands.

**Tech stack:** as in M3a: ESP-IDF v5.5.5 with its bundled cJSON and `joltwallet/littlefs` 1.22.3; Unity on the host, where cJSON is compiled from the IDF tree. No new dependencies.

**Spec:** `docs/specs/2026-09-25-firmware-design.md` r10. Task 13 brings it to r11.
- Relevant: §4.2, §5.4–§5.8, §8, §9.1, §9.2, §14.3, §14.4, §15, §17, §18 (M3), D15 and D16.
- Also `AGENTS.md` §3.4 (gotchas 4, 11, 16, 19, 21, 22), §5.3, §6–§8.

**Research behind this plan (2026-09-29):**
- A spike in a throwaway worktree built every task below. On the board it confirmed:
  - **The panel.**
    - `panel sleep` stops the frames: `panel fps` counts none.
    - A plain SLPOUT brought the panel back at 2 Hz, because both init sequences set NRDSLP (D6h), which reloads the NVM defaults at sleep-out. Waking therefore runs the init sequence again, and the panel settles at 1.00 Hz.
  - **The menu, driven with `btn`.**
    - The panel is in HPM while the menu is open.
    - The temperature offset edits in 0.1 °C steps.
    - Switching the language redraws the menu in Czech at once.
    - `settings.json` survives a reset.
  - **Toasts.** One shows for 3 s, then clears.
  - **The schedule.**
    - A preset entry switched at its minute.
    - Two entries in one minute, a night listed before a preset, both ran.
    - A night entry deep-slept for 2 min while the console was gone, ended with the RTC alarm, and the panel came back at 1.00 Hz.
  - **Console nights.** `night 2` behaved the same, and `sleep stats` counted the RTC wakes.
- The owner reviewed the host renders and approved them on 2026-09-29: the Czech dashboards, the menu and the English screens. Rulings:
  - Display ▸ Contrast stays hidden for now.
  - The temperature offset steps by 0.1 °C.
- Changes made after that review, checked on the host (sanitizers too) and on the board. The goldens did not change.
  - The schedule's run order moved into `ui` with host tests.
  - A night that ends when it starts is rejected.
  - A press during a night peek keeps the board awake.
  - A night sleep with a button held checks again every minute.
  - The critical-battery hysteresis moved into the battery model.
- **Not checked on the board:**
  - physical buttons: the D16 mask and the night peek;
  - factory reset, which erases storage, so the owner is asked first;
  - the critical screen on a really low battery.

  The owner's checks cover the buttons and the reset (Owner acceptance, at the end).

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, Python 3 host tools.
- `[host]` components (`util`, `gfx`, `locale`, `datastore`, `ui`, `scheduler`) and the pure files named per task include no ESP-IDF headers. cJSON counts as plain C.
- ESP-IDF style: 4-space indent, `snake_case`, a component or module prefix on public APIs, public headers in `include/`. Lines stay within 120 characters.
- The app task owns:
  - the display and `st7305`;
  - the I²C devices, storage and the datastore;
  - sleep;
  - the menu and the toasts.

  Console commands that touch them run through `diag_on_owner()` (spec §3.2, AGENTS §5.3).
- Never erase flash or NVS without asking.
  - Factory reset erases the `storage` partition and the NVS namespaces `wifi`, `secrets` and `ctr` (spec §14.4). Run it on the board only after the owner agrees.
  - Board commands go through `tools/idf.sh` with an explicit `-p`.
  - The port must be confirmed as this board: Espressif `303A:1001`, and `flash` prints `MAC: 14:c1:9f:54:bb:94`.
- List every component source in `SRCS`; M3b adds no component directory. Never edit `managed_components/`.
- Small, focused Conventional Commits that each build. Push to `origin` freely; never force-push. No AI or assistant attribution anywhere.
- Build only what the spec covers (r10, D15 and D16). The menu hides what doesn't exist yet (spec §5.7):
  - Alarms and Radio (M7);
  - Wi-Fi (M4) and Sync (M5);
  - Info ▸ IP/MAC and Last sync;
  - Display ▸ Contrast (owner, 2026-09-29).
- Czech text follows Czech typography:
  - decimal comma;
  - dates like "Pátek 25. září", "Pá 25. 9.", "25. 9.";
  - hints with the en dash "–", as the text fonts have no U+2212.

  Every string's glyphs must exist in the fonts (`test_lang_glyphs`).

## Review Focus

1. **A button held or stuck when the board goes to sleep**: a finger still on KEY as a night starts, or the board lying on BOOT. Expected behaviour:
   - no stream of wakes;
   - the button makes no gesture until it is released;
   - once released, it works again within a minute, on the dashboard and during a night.

   Pinned by:
   - the masks in `power` and `board` (Task 11);
   - the night's one-minute recheck and `ignore_held_buttons()` (Task 12);
   - owner check 2 (Owner acceptance).
2. **Settings edited in the menu after a routine deep-sleep wake**, when `settings.json` hasn't been read since boot, and then a reboot. Expected: the file's unknown keys are kept, and the edit persists. Pinned by:
   - `test_saving_keeps_keys_this_firmware_does_not_know` (M3a);
   - `app_ui_save_settings()` reading the file before merging (Task 12);
   - the board check: offset edit, reset, still there (Task 12).
3. **The clock moving or DST changing around schedule entries and nights**:
   - the spring gap and the autumn repeat;
   - `rtc set` or the menu's editor moving the clock over an entry.

   Expected: each entry runs once, at its local minute; a jump never runs the entries it skipped late; a night ends at its wall-clock end. Pinned by:
   - `test_local_times_follow_the_dst_rules` and `test_a_weekly_entry_in_the_spring_gap_fires_at_the_first_valid_minute` (Task 4);
   - `test_dst_changes_run_an_entry_once` (Task 5);
   - `app_clock_moved()` restarting the schedule checks (Task 12);
   - the board's `rtc set` check (Task 12).
4. **Night edge cases**:
   - an end on the next day;
   - a night due while the menu is open or a toast shows;
   - several entries in one minute;
   - a press during the night;
   - a missed RTC alarm.

   Expected:
   - the night waits until the menu closes;
   - the presets of that minute run before the night;
   - a press shows the dashboard until 60 s after the last press;
   - the backup timer ends a night whose alarm was missed.

   Pinned by:
   - `test_weekly_entries_find_their_next_day`, with "a night's end" (Task 4);
   - `test_due_entries_run_by_time_with_a_night_last` (Task 5);
   - the board's schedule and `night` checks (Task 12);
   - owner check 3.
5. **Czech text that doesn't fit**:
   - long holiday names in S cells;
   - menu labels, values and hints;
   - toasts with a long preset name;
   - the confirmation question.

   Expected: two lines or an ellipsis, never drawn into a neighbour. Pinned by:
   - the goldens `dash_home_holiday_cs`, `screen_menu_*_cs` and `screen_toast_preset_cs` (Task 9);
   - `test_every_pack_text_has_its_glyphs` (Task 2);
   - `ui_split_two_lines()` (Task 9).

---


### Task 1: Easter and a JSON nesting check (`util`)

**Files:**
- Create: `components/util/include/util_json.h`, `components/util/util_json.c`, `test/host/test_util_json.c`
- Modify: `components/util/include/util_calendar.h`, `components/util/util_calendar.c`, `components/util/CMakeLists.txt`, `test/host/test_util_calendar.c`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `void util_easter(int year, int *month, int *day)`: Easter Sunday of a Gregorian year, month 1–12 (Task 2's holidays).
  - `int util_json_depth(const char *text)`: how deeply the text nests objects and lists, ignoring brackets inside strings; 0 for NULL or a bare value (Tasks 5 and 6 reject deeper than 16 before calling cJSON, whose recursion would otherwise go as deep as the file asks).

- [ ] **Step 1: Write the failing tests.** `test_util_calendar.c` gains the Easter test (the file replaces the old one); `test_util_json.c` is new. The Easter dates include the earliest (22 March) and latest (25 April) possible.

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

static void check_easter(int year, int month, int day)
{
    int m = 0, d = 0;
    util_easter(year, &m, &d);
    TEST_ASSERT_EQUAL_INT_MESSAGE(month, m, "month");
    TEST_ASSERT_EQUAL_INT_MESSAGE(day, d, "day");
}

static void test_easter_sunday_including_the_earliest_and_latest_dates(void)
{
    check_easter(2024, 3, 31);
    check_easter(2025, 4, 20);
    check_easter(2026, 4, 5);
    check_easter(2027, 3, 28);
    check_easter(2008, 3, 23);
    check_easter(2038, 4, 25); /* the latest possible date */
    check_easter(2285, 3, 22); /* the earliest possible date */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_iso_weeks_including_year_boundaries);
    RUN_TEST(test_new_quarter_and_full_moons_of_2026);
    RUN_TEST(test_illumination_between_phases);
    RUN_TEST(test_waxing_before_full_and_waning_after);
    RUN_TEST(test_easter_sunday_including_the_earliest_and_latest_dates);
    return UNITY_END();
}
```

```c
#include "unity.h"
#include "util_json.h"

void setUp(void) {}
void tearDown(void) {}

static void test_depth_counts_objects_and_lists(void)
{
    TEST_ASSERT_EQUAL_INT(0, util_json_depth("42"));
    TEST_ASSERT_EQUAL_INT(1, util_json_depth("{}"));
    TEST_ASSERT_EQUAL_INT(3, util_json_depth("{\"a\": [1, {\"b\": 2}], \"c\": {}}"));
    TEST_ASSERT_EQUAL_INT(0, util_json_depth(NULL));
}

static void test_brackets_inside_strings_do_not_count(void)
{
    TEST_ASSERT_EQUAL_INT(1, util_json_depth("{\"a\": \"[[[{{\"}"));
    TEST_ASSERT_EQUAL_INT(1, util_json_depth("{\"a\": \"quote \\\" [[[\"}"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_depth_counts_objects_and_lists);
    RUN_TEST(test_brackets_inside_strings_do_not_count);
    return UNITY_END();
}
```

- [ ] **Step 2: Register the new test and watch both fail.** In `test/host/CMakeLists.txt`, after `reflbo_host_test(test_util_calendar util)`:

```cmake
reflbo_host_test(test_util_json util)
```

Run: `cmake --build build-host`
Expected: FAIL to compile: `util_json.h` not found, and `util_easter` undeclared in `test_util_calendar.c`.

- [ ] **Step 3: Implement.** The header replaces the old one; `util_calendar.c` gains `util_easter()` at the end; the CMake file replaces the old one.

```c
#pragma once

#include <time.h>

/* Calendar helpers for the date fields (spec §5.1). Pure C, host-buildable. */

/* ISO 8601 week number (1–53) of a civil date. */
int util_iso_week(int year, int month, int day);

/* Easter Sunday of a Gregorian year (the anonymous Gregorian algorithm, Meeus chapter 8). */
void util_easter(int year, int *month, int *day);

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

```diff
diff --git a/components/util/util_calendar.c b/components/util/util_calendar.c
index 8d4bf03..46aca54 100644
--- a/components/util/util_calendar.c
+++ b/components/util/util_calendar.c
@@ -62,3 +62,16 @@ util_moon_t util_moon_phase(time_t utc)
     moon.illumination = (int)floor((1.0 + cos(phase_angle * r)) / 2.0 * 100.0 + 0.5);
     return moon;
 }
+
+void util_easter(int year, int *month, int *day)
+{
+    int a = year % 19, b = year / 100, c = year % 100;
+    int d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
+    int h = (19 * a + b - d - g + 15) % 30;
+    int i = c / 4, k = c % 4;
+    int l = (32 + 2 * e + 2 * i - h - k) % 7;
+    int m = (a + 11 * h + 22 * l) / 451;
+    int n = h + l - 7 * m + 114;
+    *month = n / 31;
+    *day = n % 31 + 1;
+}
```

```c
#pragma once

/* How deeply a JSON text nests objects and arrays, ignoring brackets inside strings. Parsers
 * check it before cJSON, whose recursion would otherwise run as deep as the text asks. Pure C. */
int util_json_depth(const char *text);
```

```c
#include "util_json.h"

#include <stdbool.h>
#include <stddef.h>

int util_json_depth(const char *text)
{
    int depth = 0, max = 0;
    bool in_string = false, escaped = false;
    for (const char *p = text; p != NULL && *p != '\0'; p++) {
        char c = *p;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
        } else if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            if (++depth > max) {
                max = depth;
            }
        } else if ((c == '}' || c == ']') && depth > 0) {
            depth--;
        }
    }
    return max;
}
```

```cmake
# Small pure-C helpers shared by the firmware and the host tests (no ESP-IDF headers).
idf_component_register(SRCS "util_crc32.c" "util_base64.c" "util_snapshot.c" "util_ticks.c" "util_time.c"
                            "util_calendar.c" "util_json.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_util_calendar && ./build-host/test_util_json && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `5 Tests 0 Failures`, then `2 Tests 0 Failures`; the suite passes 28 of 28; the firmware builds without warnings.

- [ ] **Step 5: Commit.**

```bash
git add components/util test/host/test_util_calendar.c test/host/test_util_json.c test/host/CMakeLists.txt
git commit -m "feat(util): add Easter dates and a JSON nesting check"
```

---


### Task 2: The Czech pack and the M3b strings (`locale`)

**Files:**
- Create: `components/locale/lang_cs.c`, `test/host/test_lang_glyphs.c`
- Modify: `components/locale/include/lang.h`, `components/locale/lang_en.c`, `components/locale/lang.c`, `components/locale/CMakeLists.txt`, `test/host/test_lang.c`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `util_easter()` (Task 1).
- Produces:
  - `lang_get("cs")`, a pack named "Čeština". It has:
    - Czech dates: "Pátek 25. září" (genitive months), "Pá 25. 9.", "25. 9.";
    - a decimal comma, and Monday as the first weekday;
    - moon phase names;
    - `holiday(year, month, day)` for the public holidays of zákon č. 245/2000 Sb. (spec §5.8), with Good Friday from 2016 and Easter computed;
    - `name_day == NULL` (D16).
  - New `lang_str_t` ids, in both packs:
    - the menu labels `LS_MENU` … `LS_M_FACTORY_RESET`;
    - `LS_ON`, `LS_OFF`;
    - the button hints `LS_HINT_BROWSE`, `LS_HINT_EDIT`, `LS_HINT_DATETIME`, `LS_HINT_CONFIRM`;
    - `LS_CONFIRM_FACTORY_RESET`;
    - the toasts `LS_T_PRESET`, `LS_T_CYCLE_ON`, `LS_T_CYCLE_OFF`, `LS_T_DEFAULTS`, `LS_T_NIGHT_UNTIL`, `LS_T_REBOOTING`, `LS_T_RESETTING`;
    - the critical screen's `LS_BATTERY_EMPTY` and `LS_CHARGE_ME`.

    Tasks 8, 9 and 12 use them.

- [ ] **Step 1: Write the failing tests.** `test_lang.c` gains three tests (the file replaces the old one): every string exists in both packs; Czech dates, numbers and names; the Czech holidays. `test_lang_glyphs.c` is new: every text of both packs, and every Czech holiday name, must have its glyphs in the six text fonts, since a missing glyph draws a hollow box (spec §4.3).

```c
#include <stdio.h>
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

static void test_every_string_exists_in_every_pack(void)
{
    const char *const codes[] = { "en", "cs" };
    for (int c = 0; c < 2; c++) {
        const lang_t *lang = lang_get(codes[c]);
        TEST_ASSERT_EQUAL_STRING(codes[c], lang->code);
        for (int id = 0; id < LS_COUNT; id++) {
            char msg[32];
            snprintf(msg, sizeof(msg), "%s string %d", codes[c], id);
            TEST_ASSERT_NOT_NULL_MESSAGE(lang->strings[id], msg);
            TEST_ASSERT_TRUE_MESSAGE(lang->strings[id][0] != '\0', msg);
        }
    }
}

static void test_czech_dates_numbers_and_names(void)
{
    const lang_t *cs = lang_get("cs");
    TEST_ASSERT_EQUAL_STRING("Čeština", cs->name);
    struct tm tm = date(2026, 9, 25, 5);
    char out[40];
    lang_format_date(cs, &tm, LANG_DATE_LONG, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Pátek 25. září", out);
    lang_format_date(cs, &tm, LANG_DATE_MEDIUM, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Pá 25. 9.", out);
    lang_format_date(cs, &tm, LANG_DATE_SHORT, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("25. 9.", out);
    tm = date(2026, 3, 2, 1);
    lang_format_date(cs, &tm, LANG_DATE_LONG, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Pondělí 2. března", out);
    lang_format_decimal(cs, -125, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-12,5", out);
    TEST_ASSERT_EQUAL_STRING("Úplněk", cs->moon_phases[4]);
    TEST_ASSERT_EQUAL_STRING("Nastavte čas", lang_str(cs, LS_SET_TIME));
    TEST_ASSERT_EQUAL_INT(1, cs->first_weekday);
}

static void test_czech_public_holidays_including_easter(void)
{
    const lang_t *cs = lang_get("cs");
    TEST_ASSERT_NOT_NULL(cs->holiday);
    TEST_ASSERT_NULL(cs->name_day); /* D16: no name-day calendar until a clean source turns up */
    TEST_ASSERT_EQUAL_STRING("Velký pátek", cs->holiday(2026, 4, 3));        /* Easter is 5 April 2026 */
    TEST_ASSERT_EQUAL_STRING("Velikonoční pondělí", cs->holiday(2026, 4, 6));
    TEST_ASSERT_EQUAL_STRING("Velký pátek", cs->holiday(2027, 3, 26));       /* Easter is 28 March 2027 */
    TEST_ASSERT_NULL(cs->holiday(2026, 4, 5));                               /* Easter Sunday is not a day off */
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 1, 1));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 5, 1));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 5, 8));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 7, 5));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 7, 6));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 9, 28));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 10, 28));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 11, 17));
    TEST_ASSERT_EQUAL_STRING("Štědrý den", cs->holiday(2026, 12, 24));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 12, 25));
    TEST_ASSERT_NOT_NULL(cs->holiday(2026, 12, 26));
    TEST_ASSERT_NULL(cs->holiday(2026, 9, 29));
    TEST_ASSERT_NULL(cs->holiday(2015, 4, 3)); /* Good Friday became a holiday in 2016 */
    TEST_ASSERT_NOT_NULL(cs->holiday(2016, 3, 25));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_english_is_the_default_and_the_fallback);
    RUN_TEST(test_dates_in_three_styles);
    RUN_TEST(test_an_impossible_date_formats_as_empty);
    RUN_TEST(test_times_in_24_and_12_hour_modes);
    RUN_TEST(test_decimals_keep_the_sign_below_one);
    RUN_TEST(test_every_string_exists_in_every_pack);
    RUN_TEST(test_czech_dates_numbers_and_names);
    RUN_TEST(test_czech_public_holidays_including_easter);
    return UNITY_END();
}
```

```c
#include <stdio.h>

#include "gfx.h"
#include "gfx_fonts.h"
#include "lang.h"
#include "unity.h"

/* Every text of every language pack must render in the text fonts without the missing-glyph box:
 * a string may only use characters the fonts carry (spec §4.4). */

void setUp(void) {}
void tearDown(void) {}

static void check_text(const lang_t *lang, const char *what, int index, const char *text)
{
    static const struct {
        const char *name;
        const gfx_font_t *font;
    } k_fonts[] = {
        { "sans_12", &gfx_font_sans_12 },           { "sans_16", &gfx_font_sans_16 },
        { "sans_20", &gfx_font_sans_20 },           { "sans_bold_16", &gfx_font_sans_bold_16 },
        { "sans_bold_20", &gfx_font_sans_bold_20 }, { "sans_bold_28", &gfx_font_sans_bold_28 },
    };
    for (size_t f = 0; f < sizeof(k_fonts) / sizeof(k_fonts[0]); f++) {
        const char *p = text;
        for (uint32_t cp = gfx_utf8_next(&p); cp != 0; cp = gfx_utf8_next(&p)) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s %s %d: U+%04X missing in %s", lang->code, what, index, (unsigned)cp,
                     k_fonts[f].name);
            TEST_ASSERT_TRUE_MESSAGE(gfx_font_has_glyph(k_fonts[f].font, cp), msg);
        }
    }
}

static void test_every_pack_text_has_its_glyphs(void)
{
    const char *const codes[] = { "en", "cs" };
    for (int c = 0; c < 2; c++) {
        const lang_t *lang = lang_get(codes[c]);
        for (int id = 0; id < LS_COUNT; id++) {
            check_text(lang, "string", id, lang_str(lang, (lang_str_t)id));
        }
        for (int i = 0; i < 7; i++) {
            check_text(lang, "weekday", i, lang->weekdays[i]);
            check_text(lang, "weekday_short", i, lang->weekdays_short[i]);
        }
        for (int i = 0; i < 12; i++) {
            check_text(lang, "month", i, lang->months[i]);
            check_text(lang, "month_short", i, lang->months_short[i]);
        }
        for (int i = 0; i < 8; i++) {
            check_text(lang, "moon", i, lang->moon_phases[i]);
            check_text(lang, "moon_short", i, lang->moon_phases_short[i]);
        }
    }
}

static void test_every_holiday_name_has_its_glyphs(void)
{
    const lang_t *cs = lang_get("cs");
    for (int month = 1; month <= 12; month++) {
        for (int day = 1; day <= 31; day++) {
            const char *name = cs->holiday(2026, month, day);
            if (name != NULL) {
                check_text(cs, "holiday", month * 100 + day, name);
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_pack_text_has_its_glyphs);
    RUN_TEST(test_every_holiday_name_has_its_glyphs);
    return UNITY_END();
}
```

- [ ] **Step 2: Register the new test and watch both fail.** In `test/host/CMakeLists.txt`, the `locale` library takes the new pack and links `util` (for Easter):

```cmake
# locale: pure C language packs.
add_library(locale STATIC ${REPO_ROOT}/components/locale/lang.c ${REPO_ROOT}/components/locale/lang_en.c
            ${REPO_ROOT}/components/locale/lang_cs.c)
target_include_directories(locale PUBLIC ${REPO_ROOT}/components/locale/include)
target_compile_options(locale PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(locale PRIVATE util)
```

and after `reflbo_host_test(test_lang locale)`:

```cmake
reflbo_host_test(test_lang_glyphs locale gfx)
```

Run: `cmake --build build-host`
Expected: FAIL: CMake can't find `lang_cs.c`. Create it empty (`touch components/locale/lang_cs.c`), then run `cmake --build build-host; ./build-host/test_lang; ./build-host/test_lang_glyphs`. Expected:
- `test_lang` fails `test_czech_dates_numbers_and_names` with `Expected 'Čeština' Was 'English'`: `lang_get()` falls back to English.
- `test_lang_glyphs` crashes in `test_every_holiday_name_has_its_glyphs`, because English has no holiday table.

- [ ] **Step 3: Implement.** `lang.h` and `lang_en.c` replace the old files; `lang_cs.c` is new; `lang.c` registers it; the CMake file replaces the old one. The hints use the en dash "–" for minus, because the text fonts have no U+2212.

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
    /* The menu (spec §5.7) */
    LS_MENU,
    LS_M_PRESETS,
    LS_M_ACTIVE_PRESET,
    LS_M_AUTO_CYCLE,
    LS_M_CYCLE_INTERVAL,
    LS_M_SCHEDULE,
    LS_M_TIME,
    LS_M_SET_DATETIME,
    LS_M_CLOCK_24H,
    LS_M_TIME_ZONE,
    LS_M_DISPLAY,
    LS_M_UPDATE_INTERVAL,
    LS_M_REFRESH_RATE,
    LS_M_SENSORS,
    LS_M_TEMP_OFFSET,
    LS_M_HUM_OFFSET,
    LS_M_UNITS,
    LS_M_INFO,
    LS_M_FIRMWARE,
    LS_M_DEVICE,
    LS_M_UPTIME,
    LS_M_FREE_MEMORY,
    LS_M_SYSTEM,
    LS_M_LANGUAGE,
    LS_M_REBOOT,
    LS_M_FACTORY_RESET,
    LS_ON,
    LS_OFF,
    LS_HINT_BROWSE,   /* the button hints at the bottom of the menu (spec §5.6) */
    LS_HINT_EDIT,
    LS_HINT_DATETIME,
    LS_HINT_CONFIRM,
    LS_CONFIRM_FACTORY_RESET,
    /* Toasts and special screens (spec §5.5) */
    LS_T_PRESET, /* followed by ": <preset name>" */
    LS_T_CYCLE_ON,
    LS_T_CYCLE_OFF,
    LS_T_DEFAULTS, /* a config file was invalid, so the defaults are in use */
    LS_T_NIGHT_UNTIL, /* followed by " 06:00" */
    LS_T_REBOOTING,
    LS_T_RESETTING,
    LS_BATTERY_EMPTY,
    LS_CHARGE_ME,
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
        [LS_MENU] = "Menu",
        [LS_M_PRESETS] = "Presets",
        [LS_M_ACTIVE_PRESET] = "Active preset",
        [LS_M_AUTO_CYCLE] = "Auto-cycle",
        [LS_M_CYCLE_INTERVAL] = "Interval",
        [LS_M_SCHEDULE] = "Schedule",
        [LS_M_TIME] = "Time",
        [LS_M_SET_DATETIME] = "Set date and time",
        [LS_M_CLOCK_24H] = "24-hour clock",
        [LS_M_TIME_ZONE] = "Time zone",
        [LS_M_DISPLAY] = "Display",
        [LS_M_UPDATE_INTERVAL] = "Update interval",
        [LS_M_REFRESH_RATE] = "Refresh rate",
        [LS_M_SENSORS] = "Sensors",
        [LS_M_TEMP_OFFSET] = "Temperature offset",
        [LS_M_HUM_OFFSET] = "Humidity offset",
        [LS_M_UNITS] = "Units",
        [LS_M_INFO] = "Info",
        [LS_M_FIRMWARE] = "Firmware",
        [LS_M_DEVICE] = "Device",
        [LS_M_UPTIME] = "Uptime",
        [LS_M_FREE_MEMORY] = "Free memory",
        [LS_M_SYSTEM] = "System",
        [LS_M_LANGUAGE] = "Language",
        [LS_M_REBOOT] = "Reboot",
        [LS_M_FACTORY_RESET] = "Factory reset",
        [LS_ON] = "On",
        [LS_OFF] = "Off",
        [LS_HINT_BROWSE] = "KEY next · hold: open     BOOT back · hold: close",
        [LS_HINT_EDIT] = "KEY + · hold: save     BOOT – · hold: cancel",
        [LS_HINT_DATETIME] = "KEY + · hold: next     BOOT – · hold: cancel",
        [LS_HINT_CONFIRM] = "Hold KEY to confirm · BOOT cancels",
        [LS_CONFIRM_FACTORY_RESET] = "Erase all settings and presets?",
        [LS_T_PRESET] = "Preset",
        [LS_T_CYCLE_ON] = "Auto-cycle on",
        [LS_T_CYCLE_OFF] = "Auto-cycle off",
        [LS_T_DEFAULTS] = "Using default settings",
        [LS_T_NIGHT_UNTIL] = "Night until",
        [LS_T_REBOOTING] = "Rebooting…",
        [LS_T_RESETTING] = "Factory reset…",
        [LS_BATTERY_EMPTY] = "Battery empty",
        [LS_CHARGE_ME] = "Please charge me",
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

```c
#include <stdio.h>

#include "lang.h"
#include "util_calendar.h"
#include "util_time.h"

/* Czech (spec §5.8). Dates use the genitive month: "Pátek 25. září". No name-day calendar yet:
 * it waits for a source whose licence allows redistribution (D16). */

static void format_date(const lang_t *lang, const struct tm *tm, lang_date_style_t style, char *out, size_t size)
{
    switch (style) {
    case LANG_DATE_LONG:
        snprintf(out, size, "%s %d. %s", lang->weekdays[tm->tm_wday], tm->tm_mday, lang->months[tm->tm_mon]);
        break;
    case LANG_DATE_MEDIUM:
        snprintf(out, size, "%s %d. %d.", lang->weekdays_short[tm->tm_wday], tm->tm_mday, tm->tm_mon + 1);
        break;
    case LANG_DATE_SHORT:
        snprintf(out, size, "%d. %d.", tm->tm_mday, tm->tm_mon + 1);
        break;
    }
}

/* Public holidays and days off, zákon č. 245/2000 Sb. as amended (Good Friday from 2016). */
static const char *holiday(int year, int month, int day)
{
    static const struct {
        unsigned char month, day;
        const char *name;
    } k_fixed[] = {
        { 1, 1, "Nový rok" },
        { 5, 1, "Svátek práce" },
        { 5, 8, "Den vítězství" },
        { 7, 5, "Cyril a Metoděj" },
        { 7, 6, "Mistr Jan Hus" },
        { 9, 28, "Den české státnosti" },
        { 10, 28, "Vznik Československa" },
        { 11, 17, "Den boje za svobodu" },
        { 12, 24, "Štědrý den" },
        { 12, 25, "1. svátek vánoční" },
        { 12, 26, "2. svátek vánoční" },
    };
    for (size_t i = 0; i < sizeof(k_fixed) / sizeof(k_fixed[0]); i++) {
        if (k_fixed[i].month == month && k_fixed[i].day == day) {
            return k_fixed[i].name;
        }
    }
    int em, ed;
    util_easter(year, &em, &ed);
    int64_t easter = util_days_from_civil(year, em, ed), today = util_days_from_civil(year, month, day);
    if (today == easter + 1) {
        return "Velikonoční pondělí";
    }
    if (today == easter - 2 && year >= 2016) {
        return "Velký pátek";
    }
    return NULL;
}

const lang_t lang_cs = {
    .code = "cs",
    .name = "Čeština",
    .strings = {
        [LS_SET_TIME] = "Nastavte čas",
        [LS_TIME] = "Čas",
        [LS_DATE] = "Datum",
        [LS_TEMPERATURE] = "Teplota",
        [LS_HUMIDITY] = "Vlhkost",
        [LS_DEW_POINT] = "Rosný bod",
        [LS_TODAY_MIN] = "Dnešní minimum",
        [LS_TODAY_MAX] = "Dnešní maximum",
        [LS_BATTERY] = "Baterie",
        [LS_BATTERY_DAYS] = "Výdrž baterie",
        [LS_WEEK] = "Týden",
        [LS_MOON] = "Měsíc",
        [LS_NAME_DAY] = "Jmeniny",
        [LS_HOLIDAY] = "Svátek",
        [LS_WEATHER] = "Počasí",
        [LS_TODAY] = "Dnes",
        [LS_FORECAST] = "Předpověď",
        [LS_SUN] = "Východ a západ slunce",
        [LS_DAYS_UNIT] = "d",
        [LS_HOURS_UNIT] = "h",
        [LS_MINUTES_UNIT] = "min",
        [LS_MENU] = "Nabídka",
        [LS_M_PRESETS] = "Předvolby",
        [LS_M_ACTIVE_PRESET] = "Aktivní předvolba",
        [LS_M_AUTO_CYCLE] = "Střídání",
        [LS_M_CYCLE_INTERVAL] = "Interval střídání",
        [LS_M_SCHEDULE] = "Rozvrh",
        [LS_M_TIME] = "Čas",
        [LS_M_SET_DATETIME] = "Nastavit datum a čas",
        [LS_M_CLOCK_24H] = "24hodinový čas",
        [LS_M_TIME_ZONE] = "Časové pásmo",
        [LS_M_DISPLAY] = "Displej",
        [LS_M_UPDATE_INTERVAL] = "Interval obnovy",
        [LS_M_REFRESH_RATE] = "Obnovovací frekvence",
        [LS_M_SENSORS] = "Senzory",
        [LS_M_TEMP_OFFSET] = "Korekce teploty",
        [LS_M_HUM_OFFSET] = "Korekce vlhkosti",
        [LS_M_UNITS] = "Jednotky",
        [LS_M_INFO] = "Informace",
        [LS_M_FIRMWARE] = "Firmware",
        [LS_M_DEVICE] = "Zařízení",
        [LS_M_UPTIME] = "Doba běhu",
        [LS_M_FREE_MEMORY] = "Volná paměť",
        [LS_M_SYSTEM] = "Systém",
        [LS_M_LANGUAGE] = "Jazyk",
        [LS_M_REBOOT] = "Restartovat",
        [LS_M_FACTORY_RESET] = "Tovární nastavení",
        [LS_ON] = "Zapnuto",
        [LS_OFF] = "Vypnuto",
        [LS_HINT_BROWSE] = "KEY další · podržet: otevřít     BOOT zpět · podržet: zavřít",
        [LS_HINT_EDIT] = "KEY + · podržet: uložit     BOOT – · podržet: zrušit",
        [LS_HINT_DATETIME] = "KEY + · podržet: další     BOOT – · podržet: zrušit",
        [LS_HINT_CONFIRM] = "Podržte KEY pro potvrzení · BOOT zruší",
        [LS_CONFIRM_FACTORY_RESET] = "Smazat všechna nastavení a předvolby?",
        [LS_T_PRESET] = "Předvolba",
        [LS_T_CYCLE_ON] = "Střídání zapnuto",
        [LS_T_CYCLE_OFF] = "Střídání vypnuto",
        [LS_T_DEFAULTS] = "Výchozí nastavení",
        [LS_T_NIGHT_UNTIL] = "Noc do",
        [LS_T_REBOOTING] = "Restartuji…",
        [LS_T_RESETTING] = "Obnovuji tovární nastavení…",
        [LS_BATTERY_EMPTY] = "Baterie je vybitá",
        [LS_CHARGE_ME] = "Nabijte mě, prosím",
    },
    .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
    .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
    /* genitive, the form dates use */
    .months = { "ledna", "února", "března", "dubna", "května", "června", "července", "srpna", "září", "října",
                "listopadu", "prosince" },
    .months_short = { "led", "úno", "bře", "dub", "kvě", "čvn", "čvc", "srp", "zář", "říj", "lis", "pro" },
    .moon_phases = { "Nov", "Dorůstající srpek", "První čtvrť", "Dorůstající měsíc", "Úplněk", "Couvající měsíc",
                     "Poslední čtvrť", "Couvající srpek" },
    .moon_phases_short = { "Nov", "Srpek", "1. čtvrť", "Dorůstá", "Úplněk", "Couvá", "Posl. čtvrť", "Srpek" },
    .decimal_sep = ',',
    .first_weekday = 1,
    .format_date = format_date,
    .name_day = NULL,
    .holiday = holiday,
};
```

```diff
diff --git a/components/locale/lang.c b/components/locale/lang.c
index 058dfc3..6cde1d6 100644
--- a/components/locale/lang.c
+++ b/components/locale/lang.c
@@ -4,8 +4,9 @@
 #include <string.h>
 
 extern const lang_t lang_en;
+extern const lang_t lang_cs;
 
-static const lang_t *const k_packs[] = { &lang_en };
+static const lang_t *const k_packs[] = { &lang_en, &lang_cs };
 
 const lang_t *lang_get(const char *code)
 {
```

```cmake
# Language packs (spec §5.8). Pure C: also built on the host by test/host.
idf_component_register(SRCS "lang.c" "lang_en.c" "lang_cs.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES util)
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_lang && ./build-host/test_lang_glyphs && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `8 Tests 0 Failures`, then `2 Tests 0 Failures`; the suite passes 29 of 29, the dashboard goldens included (English is unchanged); the firmware builds without warnings.

- [ ] **Step 5: Commit.**

```bash
git add components/locale test/host/test_lang.c test/host/test_lang_glyphs.c test/host/CMakeLists.txt
git commit -m "feat(locale): add the Czech pack with public holidays, and the menu strings"
```

---


### Task 3: The time zone short list (`timekeeping`)

**Files:**
- Create: `components/timekeeping/include/timekeeping_zones.h`, `components/timekeeping/timekeeping_zones.c`, `test/host/test_timekeeping_zones.c`
- Modify: `components/timekeeping/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing new.
- Produces: `timekeeping_zone_t { const char *iana; const char *posix; }`; `const timekeeping_zone_t *timekeeping_zones(int *count)`, 14 zones in menu order with Europe/Prague at index 2; `int timekeeping_zone_find(const char *iana)`, the index or -1 (Tasks 9 and 12). The full picker is the web UI's (spec §5.7, M4), so a zone set there that isn't on this list stays selectable in the menu (Task 12).

- [ ] **Step 1: Write the failing test.** The C library's own rules check each POSIX string: its UTC offset in January and in July.

```c
#define _POSIX_C_SOURCE 200809L /* setenv, localtime_r */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "timekeeping_zones.h"
#include "unity.h"
#include "util_time.h"

void setUp(void) {}
void tearDown(void) {}

/* UTC offset in minutes of a zone's POSIX string at a UTC instant, using the C library's rules
 * (newlib on the device reads the same strings). */
static int offset_min(const char *posix, time_t t)
{
    setenv("TZ", posix, 1);
    tzset();
    struct tm local;
    localtime_r(&t, &local);
    int64_t local_s = util_days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) * 86400 +
                      local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    return (int)((local_s - (int64_t)t) / 60);
}

static void test_each_zone_has_its_winter_and_summer_offset(void)
{
    static const struct {
        const char *iana;
        int january, july; /* minutes east of UTC */
    } k_expected[] = {
        { "UTC", 0, 0 },
        { "Europe/London", 0, 60 },
        { "Europe/Prague", 60, 120 },
        { "Europe/Helsinki", 120, 180 },
        { "Europe/Moscow", 180, 180 },
        { "Asia/Dubai", 240, 240 },
        { "Asia/Kolkata", 330, 330 },
        { "Asia/Shanghai", 480, 480 },
        { "Asia/Tokyo", 540, 540 },
        { "Australia/Sydney", 660, 600 },
        { "America/New_York", -300, -240 },
        { "America/Chicago", -360, -300 },
        { "America/Denver", -420, -360 },
        { "America/Los_Angeles", -480, -420 },
    };
    int count = 0;
    const timekeeping_zone_t *zones = timekeeping_zones(&count);
    TEST_ASSERT_EQUAL_INT(sizeof(k_expected) / sizeof(k_expected[0]), count);
    time_t jan = (time_t)(util_days_from_civil(2026, 1, 15) * 86400 + 12 * 3600);
    time_t jul = (time_t)(util_days_from_civil(2026, 7, 15) * 86400 + 12 * 3600);
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_STRING(k_expected[i].iana, zones[i].iana);
        TEST_ASSERT_EQUAL_INT_MESSAGE(k_expected[i].january, offset_min(zones[i].posix, jan), zones[i].iana);
        TEST_ASSERT_EQUAL_INT_MESSAGE(k_expected[i].july, offset_min(zones[i].posix, jul), zones[i].iana);
    }
}

static void test_zones_are_found_by_name(void)
{
    TEST_ASSERT_EQUAL_INT(2, timekeeping_zone_find("Europe/Prague"));
    TEST_ASSERT_EQUAL_INT(-1, timekeeping_zone_find("Mars/Olympus"));
    TEST_ASSERT_EQUAL_INT(-1, timekeeping_zone_find(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_zone_has_its_winter_and_summer_offset);
    RUN_TEST(test_zones_are_found_by_name);
    return UNITY_END();
}
```

- [ ] **Step 2: Register it and watch it fail.** In `test/host/CMakeLists.txt`, the `timekeeping_logic` library takes the new source:

```cmake
# timekeeping: the ISO 8601 parser and the zone list build on the host.
add_library(timekeeping_logic STATIC ${REPO_ROOT}/components/timekeeping/timekeeping_iso.c
            ${REPO_ROOT}/components/timekeeping/timekeeping_zones.c)
```

and after `reflbo_host_test(test_timekeeping_iso timekeeping_logic)`:

```cmake
reflbo_host_test(test_timekeeping_zones timekeeping_logic)
```

Run: `cmake --build build-host`
Expected: FAIL: `timekeeping_zones.c` doesn't exist.

- [ ] **Step 3: Implement.** Angle-bracket zone names (`<+04>-4`) are avoided, because older newlib versions don't parse them.

```c
#pragma once

/* The short list of time zones the menu offers (spec §5.7); the web UI (M4) maps any IANA name.
 * Pure data, host-buildable. */

typedef struct {
    const char *iana;  /* shown to the user and kept in settings.json: "Europe/Prague" */
    const char *posix; /* what the C library reads: "CET-1CEST,M3.5.0,M10.5.0/3" */
} timekeeping_zone_t;

const timekeeping_zone_t *timekeeping_zones(int *count);
int timekeeping_zone_find(const char *iana); /* index, or -1 */
```

```c
#include "timekeeping_zones.h"

#include <string.h>

/* West to east from UTC, then the Americas. Rules as of 2026; angle-bracket names are avoided
 * because older newlib versions don't parse them. */
static const timekeeping_zone_t k_zones[] = {
    { "UTC", "UTC0" },
    { "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Moscow", "MSK-3" },
    { "Asia/Dubai", "GST-4" },
    { "Asia/Kolkata", "IST-5:30" },
    { "Asia/Shanghai", "CST-8" },
    { "Asia/Tokyo", "JST-9" },
    { "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "America/New_York", "EST5EDT,M3.2.0,M11.1.0" },
    { "America/Chicago", "CST6CDT,M3.2.0,M11.1.0" },
    { "America/Denver", "MST7MDT,M3.2.0,M11.1.0" },
    { "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0" },
};

const timekeeping_zone_t *timekeeping_zones(int *count)
{
    *count = (int)(sizeof(k_zones) / sizeof(k_zones[0]));
    return k_zones;
}

int timekeeping_zone_find(const char *iana)
{
    for (int i = 0; iana != NULL && i < (int)(sizeof(k_zones) / sizeof(k_zones[0])); i++) {
        if (strcmp(k_zones[i].iana, iana) == 0) {
            return i;
        }
    }
    return -1;
}
```

```cmake
# System time, time zone and manual set (spec §7). timekeeping_iso.c and timekeeping_zones.c are
# pure C and also built on the host.
idf_component_register(SRCS "timekeeping.c" "timekeeping_iso.c" "timekeeping_zones.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES rtc util)
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_timekeeping_zones && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `2 Tests 0 Failures`; the suite passes 30 of 30; the firmware builds without warnings.

- [ ] **Step 5: Commit.**

```bash
git add components/timekeeping test/host/test_timekeeping_zones.c test/host/CMakeLists.txt
git commit -m "feat(timekeeping): add the menu's time zone list"
```

---


### Task 4: Local-time rules and the entry wake (`scheduler`)

**Files:**
- Modify: `components/scheduler/include/scheduler.h`, `components/scheduler/scheduler.c`, `test/host/test_scheduler.c`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `time_t sched_local_to_utc(int year, int month, int day, int minute)`: when the local clock first reads that minute of that date. The spec §9.2 rules:
    - a time that doesn't exist (spring forward) maps to the first valid minute after it;
    - a repeated time (fall back) maps to its first occurrence.

    Task 12's date-time editor uses it.
  - `time_t sched_next_weekly(time_t after, int at_min, unsigned days)`: the first time strictly after `after` when the local clock reads `at_min` on one of `days` (bit 0 Monday … bit 6 Sunday); 0 when `days` is empty. Task 5 and Task 12 (a night's end) use it.
  - `sched_input_t.schedule_at`: the next schedule entry, UTC on a minute, 0 = none. It joins the RTC alarm (reason `SCHED_ENTRY`), so an entry wakes the board at its minute whatever the update interval.
- Both read the C library's local time, which `timekeeping_init()` sets up from the settings' POSIX string.

- [ ] **Step 1: Write the failing tests.** The file replaces the old one:
  - four new tests after the M2 and M3a ones;
  - the old positional `sched_input_t` initialisers become designated, since a new field would otherwise trip `-Wmissing-field-initializers`.

  Prague springs forward on 29 March 2026 at 02:00 and falls back on 25 October 2026 at 03:00.

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
    sched_wake_t w = scheduler_next_wake(
        &(sched_input_t){ .now = now, .display_every_min = 1, .sensors_every_min = 5, .every_second = true });
    TEST_ASSERT_EQUAL_INT64(now + 1, w.when);
    TEST_ASSERT_EQUAL_HEX(SCHED_SECOND, w.reasons);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 18, 49, 0), w.alarm);
}

static void test_cycle_switch_before_the_next_minute_comes_first(void)
{
    time_t now = utc(2026, 9, 25, 18, 48, 20);
    sched_wake_t w = scheduler_next_wake(
        &(sched_input_t){ .now = now, .display_every_min = 1, .sensors_every_min = 5, .cycle_at = now + 15 });
    TEST_ASSERT_EQUAL_INT64(now + 15, w.when);
    TEST_ASSERT_EQUAL_HEX(SCHED_CYCLE, w.reasons);
    w = scheduler_next_wake(&(sched_input_t){ .now = now, .display_every_min = 1, .sensors_every_min = 5,
                                                       .cycle_at = utc(2026, 9, 25, 18, 49, 0) });
    TEST_ASSERT_EQUAL_HEX(SCHED_DISPLAY | SCHED_CYCLE, w.reasons); /* same second: both */
    w = scheduler_next_wake(
        &(sched_input_t){ .now = now, .display_every_min = 1, .sensors_every_min = 5, .cycle_at = now + 3600 });
    TEST_ASSERT_EQUAL_INT64(w.alarm, w.when);
}

static void test_an_overdue_cycle_switch_runs_in_the_next_second(void)
{
    time_t now = utc(2026, 9, 25, 18, 48, 20);
    sched_wake_t w = scheduler_next_wake(
        &(sched_input_t){ .now = now, .display_every_min = 1, .sensors_every_min = 5, .cycle_at = now - 30 });
    TEST_ASSERT_EQUAL_INT64(now + 1, w.when);
    TEST_ASSERT_EQUAL_HEX(SCHED_CYCLE, w.reasons);
}

/* Spec §9.2: a local time that doesn't exist fires at the first valid minute after it; a repeated
 * one fires once, at its first occurrence. Prague springs forward on 29 March 2026 at 02:00 and
 * falls back on 25 October 2026 at 03:00. */
static void test_local_times_follow_the_dst_rules(void)
{
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 20, 30, 0), sched_local_to_utc(2026, 9, 25, 22 * 60 + 30));
    TEST_ASSERT_EQUAL_INT64(utc(2026, 1, 15, 22, 0, 0), sched_local_to_utc(2026, 1, 15, 23 * 60));
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 29, 1, 0, 0), sched_local_to_utc(2026, 3, 29, 2 * 60 + 30)); /* 03:00 CEST */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 0, 30, 0), sched_local_to_utc(2026, 10, 25, 2 * 60 + 30)); /* CEST */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 2, 0, 0), sched_local_to_utc(2026, 10, 25, 3 * 60));
}

static void test_weekly_entries_find_their_next_day(void)
{
    const unsigned weekdays = 0x1F; /* Monday to Friday */
    time_t friday_2330 = utc(2026, 9, 25, 21, 30, 0);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 28, 21, 0, 0), sched_next_weekly(friday_2330, 23 * 60, weekdays));
    time_t friday_2200 = utc(2026, 9, 25, 20, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 21, 0, 0), sched_next_weekly(friday_2200, 23 * 60, weekdays));
    time_t friday_2300 = utc(2026, 9, 25, 21, 0, 0); /* strictly after: this one is past */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 28, 21, 0, 0), sched_next_weekly(friday_2300, 23 * 60, weekdays));
    /* a night's end */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 26, 4, 0, 0), sched_next_weekly(friday_2330, 6 * 60, 0x7F));
    TEST_ASSERT_EQUAL_INT64(0, sched_next_weekly(friday_2330, 23 * 60, 0)); /* no days: never */
}

static void test_a_weekly_entry_in_the_spring_gap_fires_at_the_first_valid_minute(void)
{
    time_t saturday_noon = utc(2026, 3, 28, 11, 0, 0);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 29, 1, 0, 0), sched_next_weekly(saturday_noon, 2 * 60 + 30, 0x7F));
    time_t after_it = utc(2026, 3, 29, 1, 0, 0); /* then the next day's 02:30 CEST */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 30, 0, 30, 0), sched_next_weekly(after_it, 2 * 60 + 30, 0x7F));
}

static void test_the_next_schedule_entry_sets_the_alarm(void)
{
    time_t now = utc(2026, 9, 25, 20, 16, 0); /* 22:16 local, display every 15 min: next slot 22:30 */
    sched_input_t in = { .now = now, .display_every_min = 15, .sensors_every_min = 30,
                         .schedule_at = utc(2026, 9, 25, 20, 20, 0) };
    sched_wake_t w = scheduler_next_wake(&in);
    TEST_ASSERT_EQUAL_INT64(in.schedule_at, w.alarm);
    TEST_ASSERT_EQUAL_INT64(in.schedule_at, w.when);
    TEST_ASSERT_EQUAL_UINT(SCHED_ENTRY, w.reasons);
    in.schedule_at = utc(2026, 9, 25, 21, 0, 0); /* after the slot: the slot comes first */
    w = scheduler_next_wake(&in);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 20, 30, 0), w.alarm);
    TEST_ASSERT_EQUAL_UINT(SCHED_DISPLAY | SCHED_SENSORS, w.reasons);
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
    RUN_TEST(test_local_times_follow_the_dst_rules);
    RUN_TEST(test_weekly_entries_find_their_next_day);
    RUN_TEST(test_a_weekly_entry_in_the_spring_gap_fires_at_the_first_valid_minute);
    RUN_TEST(test_the_next_schedule_entry_sets_the_alarm);
    return UNITY_END();
}
```

- [ ] **Step 2: Watch them fail.**

Run: `cmake --build build-host`
Expected: FAIL to compile: `sched_input_t` has no member `schedule_at`, and `sched_local_to_utc`, `sched_next_weekly` and `SCHED_ENTRY` are undeclared.

- [ ] **Step 3: Implement.** Both files replace the old ones.
  - `sched_local_to_utc()` asks `mktime()`, then checks the answer with `localtime_r()`, because the C standard leaves open what `mktime()` returns for a time inside a gap. An hour earlier with the same reading means a repeated time; an answer that doesn't read back means a gap, whose end is found minute by minute.
  - `sched_next_weekly()` walks up to 8 local days from noon, so a DST change never skips or repeats a date.

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
    SCHED_ENTRY = 1u << 4,   /* a schedule entry (spec §5.4) */
} sched_reason_t;

typedef struct {
    time_t now;            /* UTC seconds */
    int display_every_min; /* 1..15 */
    int sensors_every_min; /* 1..30 */
    time_t cycle_at;       /* next auto-cycle switch (UTC); 0 = cycling is off */
    bool every_second;
    time_t schedule_at;    /* next schedule entry (UTC, on a minute); 0 = none */
} sched_input_t;

typedef struct {
    time_t when;      /* UTC; the earliest wake, after `now` */
    unsigned reasons; /* sched_reason_t bits due at `when` */
    time_t alarm;     /* the next minute-aligned wake (display, sensors or an entry), for the RTC alarm */
} sched_wake_t;

sched_wake_t scheduler_next_wake(const sched_input_t *in);
/* True if `t` is a local slot of a job that runs every `every_min` minutes. */
int scheduler_is_slot(time_t t, int every_min);
/* When the local clock first reads `minute` (after midnight) on a local date (spec §9.2): a time
 * that doesn't exist (spring forward) maps to the first valid minute after it, and a repeated one
 * (fall back) to its first occurrence. */
time_t sched_local_to_utc(int year, int month, int day, int minute);
/* The first time strictly after `after` when the local clock reads `at_min` on one of `days`
 * (bit 0 Monday ... bit 6 Sunday); 0 if `days` is empty. */
time_t sched_next_weekly(time_t after, int at_min, unsigned days);
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

/* Local date and minute as one number that grows with local time. */
static long long local_key(const struct tm *t)
{
    return (((long long)t->tm_year * 13 + t->tm_mon) * 32 + t->tm_mday) * 1440 + t->tm_hour * 60 + t->tm_min;
}

time_t sched_local_to_utc(int year, int month, int day, int minute)
{
    struct tm want = { .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = minute / 60,
                       .tm_min = minute % 60, .tm_isdst = -1 };
    long long key = local_key(&want);
    struct tm probe = want, local;
    time_t t = mktime(&probe);
    if (localtime_r(&t, &local) != NULL && local_key(&local) == key) {
        time_t earlier = t - 3600; /* fall back: the same reading an hour before is its first occurrence */
        return localtime_r(&earlier, &local) != NULL && local_key(&local) == key ? earlier : t;
    }
    for (time_t s = t - 3 * 3600; s <= t + 3 * 3600; s += 60) { /* the reading doesn't exist: the gap's end */
        if (localtime_r(&s, &local) != NULL && local_key(&local) >= key) {
            return s;
        }
    }
    return t;
}

time_t sched_next_weekly(time_t after, int at_min, unsigned days)
{
    struct tm local;
    if ((days & 0x7F) == 0 || localtime_r(&after, &local) == NULL) {
        return 0;
    }
    for (int d = 0; d <= 8; d++) {
        struct tm day = { .tm_year = local.tm_year, .tm_mon = local.tm_mon, .tm_mday = local.tm_mday + d,
                          .tm_hour = 12, .tm_isdst = -1 };
        mktime(&day); /* normalises the date and sets tm_wday */
        int monday_first = (day.tm_wday + 6) % 7;
        if (!(days & (1u << monday_first))) {
            continue;
        }
        time_t t = sched_local_to_utc(day.tm_year + 1900, day.tm_mon + 1, day.tm_mday, at_min);
        if (t > after) {
            return t;
        }
    }
    return 0;
}

sched_wake_t scheduler_next_wake(const sched_input_t *in)
{
    time_t display = next_slot(in->now, in->display_every_min);
    time_t sensors = next_slot(in->now, in->sensors_every_min);
    time_t entry = in->schedule_at > in->now ? in->schedule_at : 0;
    time_t cycle = in->cycle_at == 0 ? 0 : in->cycle_at > in->now ? in->cycle_at : in->now + 1; /* overdue: now */
    time_t second = in->every_second ? in->now + 1 : 0;

    sched_wake_t wake = { .alarm = display < sensors ? display : sensors };
    if (entry != 0 && entry < wake.alarm) {
        wake.alarm = entry;
    }
    wake.when = wake.alarm;
    if (cycle != 0 && cycle < wake.when) {
        wake.when = cycle;
    }
    if (second != 0 && second < wake.when) {
        wake.when = second;
    }
    wake.reasons = (display == wake.when ? SCHED_DISPLAY : 0) | (sensors == wake.when ? SCHED_SENSORS : 0) |
                   (cycle == wake.when ? SCHED_CYCLE : 0) | (second == wake.when ? SCHED_SECOND : 0) |
                   (entry == wake.when ? SCHED_ENTRY : 0);
    return wake;
}
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_scheduler && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `14 Tests 0 Failures`; the suite passes 30 of 30; the firmware builds without warnings (`main/app_ui.c` builds its input with designated initialisers, so `schedule_at` is 0 there until Task 12).

- [ ] **Step 5: Commit.**

```bash
git add components/scheduler test/host/test_scheduler.c
git commit -m "feat(scheduler): add local-time rules and the schedule entry wake"
```

---


### Task 5: The schedule in `presets.json`, and stricter parsing (`ui` presets)

**Files:**
- Create: `components/ui/ui_schedule.c`, `test/host/test_ui_schedule.c`
- Modify: `components/ui/include/ui_preset.h`, `components/ui/ui_preset_json.c`, `components/ui/CMakeLists.txt`, `test/host/test_ui_preset.c`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `util_json_depth()` (Task 1); `sched_next_weekly()` (Task 4).
- Produces (`ui_preset.h`):
  - `UI_SCHEDULE_MAX` 8; `UI_PRESETS_JSON_MAX` 8192, the save buffer for 16 presets and 8 entries; `UI_JSON_MAX_DEPTH` 16.
  - `ui_sched_action_t { UI_SCHED_PRESET, UI_SCHED_NIGHT }`.
  - `ui_schedule_entry_t { uint16_t at_min; uint8_t days; uint8_t action; uint8_t preset; uint16_t until_min; }`. Times are local minutes after midnight, and `days` has bit 0 for Monday. `preset` indexes `presets[]`, and `until_min` never equals `at_min`.
  - `ui_schedule_t { bool enabled; uint8_t count; ui_schedule_entry_t entries[8]; }`, as `ui_presets_t.schedule`.
  - `time_t ui_schedule_next(const ui_schedule_t *, time_t after, int *index)`: the next run after `after`, with its entry's index; 0 if none will run.
  - `int ui_schedule_due(const ui_schedule_t *, time_t after, time_t now, int order[8])`: the entries due in (after, now], in run order. That is by time; within a minute the presets run first, in list order, then a night, since nothing runs once a night has started.
  - Task 12 runs them.
- `presets.json` (spec §5.4):
  - `"schedule": { "enabled": …, "entries": [ { "at": "HH:MM", "days": 0–127, "action": "preset", "preset": "<id>" } or { …, "action": "night", "until": "HH:MM" } ] }`.
  - A missing `days` means every day. `enabled` defaults to false.
  - Rejected, as structural errors, with the entry's number in the message:
    - a bad `at` or `until`;
    - an unknown action or preset id;
    - a night that ends when it starts;
    - more than 8 entries;
    - `entries` that isn't a list.
- **Hardening**, from the M3a review:
  - A file nested deeper than 16 levels is rejected before cJSON parses it.
  - A name over 23 bytes is cut at a UTF-8 character boundary, rather than failing the file (spec §5.4 lists long names as lenient).
  - `null` options take their defaults.
  - `slots` given as a list is rejected.
  - The file is written unformatted, so 16 presets and 8 entries fit 8 KB.

- [ ] **Step 1: Write the failing tests.** `test_ui_preset.c` gains nine tests (the file replaces the old one); `test_ui_schedule.c` is new and covers:
  - the next entry on its days;
  - the run order within a minute;
  - an entry that ran not coming due again;
  - both DST changes.

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

static const char *k_with_schedule =
    "{ \"schema\": 1, \"presets\": [ { \"id\": \"home\", \"layout\": \"classic\" },"
    "                                { \"id\": \"focus\", \"layout\": \"focus\" } ],"
    "  \"schedule\": { \"enabled\": true, \"entries\": ["
    "    { \"at\": \"22:30\", \"days\": 127, \"action\": \"preset\", \"preset\": \"focus\" },"
    "    { \"at\": \"23:00\", \"days\": 31, \"action\": \"night\", \"until\": \"06:00\" } ] } }";

static void test_the_spec_schedule_parses(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_with_schedule, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_TRUE(s_p.schedule.enabled);
    TEST_ASSERT_EQUAL_INT(2, s_p.schedule.count);
    const ui_schedule_entry_t *e = &s_p.schedule.entries[0];
    TEST_ASSERT_EQUAL_INT(22 * 60 + 30, e->at_min);
    TEST_ASSERT_EQUAL_HEX8(0x7F, e->days);
    TEST_ASSERT_EQUAL(UI_SCHED_PRESET, e->action);
    TEST_ASSERT_EQUAL_INT(1, e->preset);
    e = &s_p.schedule.entries[1];
    TEST_ASSERT_EQUAL(UI_SCHED_NIGHT, e->action);
    TEST_ASSERT_EQUAL_INT(23 * 60, e->at_min);
    TEST_ASSERT_EQUAL_INT(6 * 60, e->until_min);
    TEST_ASSERT_EQUAL_HEX8(0x1F, e->days); /* Monday to Friday */
}

static void test_a_schedule_survives_a_round_trip(void)
{
    TEST_ASSERT_TRUE(ui_presets_from_json(k_with_schedule, &s_p, s_err, sizeof(s_err)));
    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
}

static void test_bad_schedules_are_rejected_with_a_reason(void)
{
    const char *base = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\"}], \"schedule\": ";
    const struct {
        const char *schedule, *reason;
    } k_cases[] = {
        { "{\"entries\": [{\"at\": \"25:00\", \"action\": \"night\", \"until\": \"06:00\"}]}", "at" },
        { "{\"entries\": [{\"at\": \"7:5\", \"action\": \"night\", \"until\": \"06:00\"}]}", "at" },
        { "{\"entries\": [{\"at\": \"22:00\", \"action\": \"dance\"}]}", "action" },
        { "{\"entries\": [{\"at\": \"22:00\", \"action\": \"preset\", \"preset\": \"gone\"}]}", "preset" },
        { "{\"entries\": [{\"at\": \"22:00\", \"action\": \"night\"}]}", "until" },
        { "{\"entries\": [{\"at\": \"22:00\", \"action\": \"night\", \"until\": \"22:00\"}]}", "another time" },
        { "{\"entries\": {}}", "list" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        snprintf(s_json, sizeof(s_json), "%s%s}", base, k_cases[i].schedule);
        check_rejected(s_json, k_cases[i].reason);
    }
    size_t n = (size_t)snprintf(s_json, sizeof(s_json), "%s{\"entries\": [", base);
    for (int i = 0; i <= UI_SCHEDULE_MAX; i++) {
        n += (size_t)snprintf(s_json + n, sizeof(s_json) - n,
                              "%s{\"at\": \"0%d:00\", \"action\": \"night\", \"until\": \"09:00\"}", i ? "," : "", i);
    }
    snprintf(s_json + n, sizeof(s_json) - n, "]}}");
    check_rejected(s_json, "at most");
}

static void test_a_missing_days_mask_means_every_day(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\"}],"
                       " \"schedule\": {\"entries\": [{\"at\": \"23:00\", \"action\": \"night\","
                       " \"until\": \"06:00\"}]}}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_FALSE(s_p.schedule.enabled); /* entries alone don't switch it on */
    TEST_ASSERT_EQUAL_HEX8(0x7F, s_p.schedule.entries[0].days);
}

static void test_a_long_name_is_cut_at_a_character(void)
{
    /* 24 bytes: the 23-byte limit falls inside the last "ř" (2 bytes), which must not be split */
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\","
                       " \"name\": \"Obývák a pracovna ř\\u0159\"}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    size_t n = strlen(s_p.presets[0].name);
    TEST_ASSERT_TRUE(n <= UI_PRESET_NAME_LEN - 1);
    TEST_ASSERT_TRUE((s_p.presets[0].name[n - 1] & 0xC0) != 0xC0); /* no dangling lead byte */
    TEST_ASSERT_EQUAL_STRING_LEN("Obývák a pracovna", s_p.presets[0].name, 18);
}

static void test_null_options_take_their_defaults(void)
{
    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\","
                       " \"options\": {\"stale_policy\": null, \"status_battery\": null}}]}";
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL(UI_STALE_STALE, s_p.presets[0].stale_policy);
    TEST_ASSERT_EQUAL_HEX8(UI_STATUS_BAT_PERCENT, s_p.presets[0].status_battery);
}

static void test_slots_given_as_a_list_are_rejected(void)
{
    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"slots\": [\"env.temp\"]}]}",
                   "slots");
}

static void test_deep_nesting_is_rejected_before_parsing(void)
{
    size_t n = (size_t)snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"deep\": ");
    for (int i = 0; i < 40; i++) {
        s_json[n++] = '[';
    }
    for (int i = 0; i < 40; i++) {
        s_json[n++] = ']';
    }
    snprintf(s_json + n, sizeof(s_json) - n, ", \"presets\": [{\"id\": \"a\", \"layout\": \"grid\"}]}");
    check_rejected(s_json, "nested");
}

static void test_a_full_set_fits_the_save_buffer(void)
{
    memset(&s_p, 0, sizeof(s_p));
    for (int i = 0; i < UI_PRESET_MAX; i++) {
        ui_preset_t *p = &s_p.presets[i];
        snprintf(p->id, sizeof(p->id), "preset-%08d", i);
        snprintf(p->name, sizeof(p->name), "Předvolba číslo %04d", i); /* 23 bytes: the longest name */
        p->layout = UI_LAYOUT_GRID;
        for (int k = 0; k < 6; k++) {
            p->slots[k] = (uint8_t)(UI_FIELD_ENV_TEMP + k);
        }
        p->clock = UI_CLOCK_12H;
        p->status_battery = UI_STATUS_BAT_PERCENT | UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
    }
    s_p.count = UI_PRESET_MAX;
    s_p.cycle_interval_s = 60;
    s_p.schedule.count = UI_SCHEDULE_MAX;
    for (int i = 0; i < UI_SCHEDULE_MAX; i++) {
        s_p.schedule.entries[i] = (ui_schedule_entry_t){ .at_min = 600, .days = 0x7F, .action = UI_SCHED_NIGHT,
                                                          .until_min = 700 };
    }
    static char buf[UI_PRESETS_JSON_MAX];
    size_t n = ui_presets_to_json(&s_p, buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "the worst case must fit UI_PRESETS_JSON_MAX");
    ui_presets_t back;
    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(buf, &back, s_err, sizeof(s_err)), s_err);
    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
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
    RUN_TEST(test_the_spec_schedule_parses);
    RUN_TEST(test_a_schedule_survives_a_round_trip);
    RUN_TEST(test_bad_schedules_are_rejected_with_a_reason);
    RUN_TEST(test_a_missing_days_mask_means_every_day);
    RUN_TEST(test_a_long_name_is_cut_at_a_character);
    RUN_TEST(test_null_options_take_their_defaults);
    RUN_TEST(test_slots_given_as_a_list_are_rejected);
    RUN_TEST(test_deep_nesting_is_rejected_before_parsing);
    RUN_TEST(test_a_full_set_fits_the_save_buffer);
    return UNITY_END();
}
```

```c
#define _POSIX_C_SOURCE 200809L /* setenv */

#include <stdlib.h>
#include <time.h>

#include "ui_preset.h"
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
    return (time_t)days * 86400 + h * 3600 + mi * 60 + s;
}

static ui_schedule_entry_t preset_at(int hour, int minute, uint8_t days, uint8_t preset)
{
    return (ui_schedule_entry_t){ .at_min = (uint16_t)(hour * 60 + minute), .days = days,
                                  .action = UI_SCHED_PRESET, .preset = preset };
}

static ui_schedule_entry_t night_at(int hour, int minute, int until_hour, int until_minute)
{
    return (ui_schedule_entry_t){ .at_min = (uint16_t)(hour * 60 + minute), .days = 0x7F,
                                  .action = UI_SCHED_NIGHT, .until_min = (uint16_t)(until_hour * 60 + until_minute) };
}

/* Friday 25 September 2026; Prague is on CEST (UTC+2). */
static void test_the_next_entry_is_the_earliest_on_its_days(void)
{
    ui_schedule_t s = { .enabled = true, .count = 3 };
    s.entries[0] = preset_at(23, 0, 0x7F, 1);
    s.entries[1] = preset_at(22, 30, 0x60, 2); /* weekends only */
    s.entries[2] = preset_at(22, 45, 0x00, 3); /* no days: never */
    int index = -1;
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 25, 21, 0, 0), ui_schedule_next(&s, utc(2026, 9, 25, 20, 0, 0), &index));
    TEST_ASSERT_EQUAL_INT(0, index);
    /* after Friday's 23:00, Saturday's 22:30 comes before Saturday's 23:00 */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 26, 20, 30, 0), ui_schedule_next(&s, utc(2026, 9, 25, 21, 0, 0), &index));
    TEST_ASSERT_EQUAL_INT(1, index);
    ui_schedule_t empty = { .enabled = true };
    TEST_ASSERT_EQUAL_INT64(0, ui_schedule_next(&empty, utc(2026, 9, 25, 20, 0, 0), &index));
}

/* Spec §5.4: every entry runs at its minute, even when several share one. At the same minute the
 * presets run first, in list order, and a night last, as nothing runs once it has started. */
static void test_due_entries_run_by_time_with_a_night_last(void)
{
    ui_schedule_t s = { .enabled = true, .count = 5 };
    s.entries[0] = night_at(22, 30, 6, 0);
    s.entries[1] = preset_at(22, 30, 0x7F, 1);
    s.entries[2] = preset_at(22, 0, 0x7F, 2);
    s.entries[3] = preset_at(22, 30, 0x7F, 3);
    s.entries[4] = preset_at(22, 31, 0x7F, 3); /* after `now`: not yet */
    int order[UI_SCHEDULE_MAX];
    int n = ui_schedule_due(&s, utc(2026, 9, 25, 19, 59, 30), utc(2026, 9, 25, 20, 30, 5), order);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_INT(2, order[0]); /* 22:00 */
    TEST_ASSERT_EQUAL_INT(1, order[1]); /* 22:30, the presets in list order */
    TEST_ASSERT_EQUAL_INT(3, order[2]);
    TEST_ASSERT_EQUAL_INT(0, order[3]); /* 22:30, the night */
}

static void test_an_entry_that_ran_is_not_due_again(void)
{
    ui_schedule_t s = { .enabled = true, .count = 1 };
    s.entries[0] = preset_at(22, 30, 0x7F, 1);
    int order[UI_SCHEDULE_MAX];
    time_t ran = utc(2026, 9, 25, 20, 30, 5); /* checked 5 s after 22:30 */
    TEST_ASSERT_EQUAL_INT(0, ui_schedule_due(&s, ran, utc(2026, 9, 25, 20, 31, 0), order));
    TEST_ASSERT_EQUAL_INT(0, ui_schedule_due(&s, ran, ran, order));
    /* the next day, it is due once */
    TEST_ASSERT_EQUAL_INT(1, ui_schedule_due(&s, utc(2026, 9, 26, 20, 29, 0), utc(2026, 9, 26, 20, 30, 0), order));
}

/* Spec §9.2 through the schedule: 02:30 doesn't exist on 29 March 2026 and runs at 03:00 CEST; on
 * 25 October it happens twice and runs once. */
static void test_dst_changes_run_an_entry_once(void)
{
    ui_schedule_t s = { .enabled = true, .count = 1 };
    s.entries[0] = preset_at(2, 30, 0x7F, 1);
    int order[UI_SCHEDULE_MAX];
    time_t before_gap = utc(2026, 3, 29, 0, 59, 0); /* 01:59 CET */
    TEST_ASSERT_EQUAL_INT(1, ui_schedule_due(&s, before_gap, utc(2026, 3, 29, 1, 0, 10), order)); /* 03:00:10 CEST */
    time_t first = utc(2026, 10, 25, 0, 30, 0); /* 02:30 CEST */
    TEST_ASSERT_EQUAL_INT(1, ui_schedule_due(&s, first - 60, first + 5, order));
    time_t second = utc(2026, 10, 25, 1, 30, 0); /* 02:30 CET, an hour later */
    TEST_ASSERT_EQUAL_INT(0, ui_schedule_due(&s, first + 5, second + 5, order));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_next_entry_is_the_earliest_on_its_days);
    RUN_TEST(test_due_entries_run_by_time_with_a_night_last);
    RUN_TEST(test_an_entry_that_ran_is_not_due_again);
    RUN_TEST(test_dst_changes_run_an_entry_once);
    return UNITY_END();
}
```

- [ ] **Step 2: Register the new test and watch both fail.** In `test/host/CMakeLists.txt`, `ui` links the scheduler for its local-time rules:

```cmake
# ui: pure C on top of gfx, locale, the datastore and the scheduler's local-time rules.
file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
add_library(ui STATIC ${UI_SOURCES})
target_include_directories(ui PUBLIC ${REPO_ROOT}/components/ui/include)
target_compile_options(ui PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(ui PUBLIC gfx locale datastore util PRIVATE cjson scheduler m)
```

and after `reflbo_host_test(test_ui_preset ui)`:

```cmake
reflbo_host_test(test_ui_schedule ui)
```

Run: `cmake --build build-host`
Expected: FAIL to compile: `ui_schedule_entry_t`, `UI_SCHEDULE_MAX` and `ui_schedule_due` are undeclared.

- [ ] **Step 3: Implement.** The header replaces the old one; apply the patch to `ui_preset_json.c`; `ui_schedule.c` is new; the CMake file replaces the old one.

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

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
#define UI_SCHEDULE_MAX 8
#define UI_PRESETS_JSON_MAX 8192 /* presets.json at its largest: 16 presets and 8 schedule entries */
#define UI_JSON_MAX_DEPTH 16     /* the files nest 5 levels; anything deeper is rejected unparsed */

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

typedef enum {
    UI_SCHED_PRESET, /* make a preset active */
    UI_SCHED_NIGHT,  /* night sleep until `until_min` (spec §9.1) */
} ui_sched_action_t;

/* A schedule entry (spec §5.4, D15). Times are local minutes after midnight. */
typedef struct {
    uint16_t at_min;
    uint8_t days;   /* bit 0 Monday ... bit 6 Sunday */
    uint8_t action; /* ui_sched_action_t */
    uint8_t preset; /* index into presets, for UI_SCHED_PRESET */
    uint16_t until_min; /* for UI_SCHED_NIGHT, never `at_min`: before it means the next day */
} ui_schedule_entry_t;

typedef struct {
    bool enabled;
    uint8_t count;
    ui_schedule_entry_t entries[UI_SCHEDULE_MAX];
} ui_schedule_t;

/* When the entries run (spec §5.4), with the local-time rules of spec §9.2. The first time after
 * `after` that an entry runs, with that entry's index in *index; 0 if none ever will. */
time_t ui_schedule_next(const ui_schedule_t *schedule, time_t after, int *index);
/* The entries due in (after, now], in the order they run: by time, and within a minute the presets
 * in list order before a night. Returns how many; `order` gets their indexes. */
int ui_schedule_due(const ui_schedule_t *schedule, time_t after, time_t now, int order[UI_SCHEDULE_MAX]);

typedef struct {
    uint8_t count;
    uint8_t active; /* index into presets */
    bool cycle_enabled;
    uint16_t cycle_interval_s;
    ui_preset_t presets[UI_PRESET_MAX];
    ui_schedule_t schedule;
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

```diff
diff --git a/components/ui/ui_preset_json.c b/components/ui/ui_preset_json.c
index 384dd79..8e17ddd 100644
--- a/components/ui/ui_preset_json.c
+++ b/components/ui/ui_preset_json.c
@@ -5,6 +5,7 @@
 #include "cJSON.h"
 #include "ui_fields.h"
 #include "ui_preset.h"
+#include "util_json.h"
 
 #define SCHEMA 1
 
@@ -45,8 +46,45 @@ static bool optional_bool(const cJSON *obj, const char *key, bool fallback)
     return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
 }
 
+/* Copies a name, cut to fit at a character boundary (a limit inside "ř" would split it). */
+static void copy_name(char *out, size_t size, const char *name)
+{
+    size_t n = strlen(name);
+    if (n >= size) {
+        n = size - 1;
+        while (n > 0 && ((unsigned char)name[n] & 0xC0) == 0x80) { /* name[n] continues a sequence */
+            n--;
+        }
+    }
+    memcpy(out, name, n);
+    out[n] = '\0';
+}
+
+/* "22:30" -> minutes after midnight. */
+static bool parse_hhmm(const cJSON *item, uint16_t *out)
+{
+    const char *s = cJSON_IsString(item) ? item->valuestring : "";
+    if (strlen(s) != 5 || s[2] != ':') {
+        return false;
+    }
+    for (int i = 0; i < 5; i++) {
+        if (i != 2 && (s[i] < '0' || s[i] > '9')) {
+            return false;
+        }
+    }
+    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
+    if (h > 23 || m > 59) {
+        return false;
+    }
+    *out = (uint16_t)(h * 60 + m);
+    return true;
+}
+
 static bool parse_slots(const cJSON *slots, const ui_layout_t *layout, ui_preset_t *out, char *err, size_t size)
 {
+    if (!cJSON_IsObject(slots)) {
+        return fail(err, size, "preset \"%s\": slots must be an object of slot names", out->id);
+    }
     const cJSON *slot;
     cJSON_ArrayForEach(slot, slots)
     {
@@ -82,10 +120,7 @@ static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t
     snprintf(out->id, sizeof(out->id), "%s", id->valuestring);
     const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
     if (cJSON_IsString(name)) {
-        if (strlen(name->valuestring) >= UI_PRESET_NAME_LEN) {
-            return fail(err, size, "preset \"%s\": name longer than %d bytes", out->id, UI_PRESET_NAME_LEN - 1);
-        }
-        snprintf(out->name, sizeof(out->name), "%s", name->valuestring);
+        copy_name(out->name, sizeof(out->name), name->valuestring);
     } else {
         snprintf(out->name, sizeof(out->name), "%s", out->id);
     }
@@ -97,7 +132,8 @@ static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t
     out->layout = (uint8_t)layout;
     out->in_cycle = optional_bool(item, "in_cycle", true);
     const cJSON *slots = cJSON_GetObjectItemCaseSensitive(item, "slots");
-    if (slots != NULL && !parse_slots(slots, ui_layout((ui_layout_id_t)layout), out, err, size)) {
+    if (slots != NULL && !cJSON_IsNull(slots) &&
+        !parse_slots(slots, ui_layout((ui_layout_id_t)layout), out, err, size)) {
         return false;
     }
     const cJSON *options = cJSON_GetObjectItemCaseSensitive(item, "options");
@@ -107,7 +143,7 @@ static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t
     out->invert = optional_bool(options, "invert", false);
     out->stale_policy = UI_STALE_STALE;
     const cJSON *policy = cJSON_GetObjectItemCaseSensitive(options, "stale_policy");
-    if (policy != NULL) {
+    if (policy != NULL && !cJSON_IsNull(policy)) {
         int found = -1;
         for (int i = 0; cJSON_IsString(policy) && i < (int)(sizeof(k_policies) / sizeof(k_policies[0])); i++) {
             if (strcmp(policy->valuestring, k_policies[i]) == 0) {
@@ -122,7 +158,7 @@ static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t
     out->status_clock = optional_bool(options, "status_clock", false);
     out->status_battery = UI_STATUS_BAT_PERCENT;
     const cJSON *battery = cJSON_GetObjectItemCaseSensitive(options, "status_battery");
-    if (battery != NULL) {
+    if (battery != NULL && !cJSON_IsNull(battery)) {
         if (!cJSON_IsArray(battery)) {
             return fail(err, size, "preset \"%s\": status_battery must be a list", out->id);
         }
@@ -145,6 +181,55 @@ static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t
     return true;
 }
 
+static bool parse_schedule(const cJSON *schedule, ui_presets_t *out, char *err, size_t size)
+{
+    out->schedule.enabled = optional_bool(schedule, "enabled", false);
+    const cJSON *entries = cJSON_GetObjectItemCaseSensitive(schedule, "entries");
+    if (entries == NULL || cJSON_IsNull(entries)) {
+        return true;
+    }
+    if (!cJSON_IsArray(entries)) {
+        return fail(err, size, "schedule: entries must be a list");
+    }
+    if (cJSON_GetArraySize(entries) > UI_SCHEDULE_MAX) {
+        return fail(err, size, "schedule: at most %d entries", UI_SCHEDULE_MAX);
+    }
+    const cJSON *e;
+    cJSON_ArrayForEach(e, entries)
+    {
+        int n = out->schedule.count + 1;
+        ui_schedule_entry_t *se = &out->schedule.entries[out->schedule.count];
+        if (!parse_hhmm(cJSON_GetObjectItemCaseSensitive(e, "at"), &se->at_min)) {
+            return fail(err, size, "schedule entry %d: \"at\" must be HH:MM", n);
+        }
+        const cJSON *days = cJSON_GetObjectItemCaseSensitive(e, "days");
+        se->days = cJSON_IsNumber(days) ? (uint8_t)(days->valueint & 0x7F) : 0x7F;
+        const cJSON *action = cJSON_GetObjectItemCaseSensitive(e, "action");
+        const char *a = cJSON_IsString(action) ? action->valuestring : "";
+        if (strcmp(a, "preset") == 0) {
+            const cJSON *id = cJSON_GetObjectItemCaseSensitive(e, "preset");
+            int index = cJSON_IsString(id) ? ui_presets_find(out, id->valuestring) : -1;
+            if (index < 0) {
+                return fail(err, size, "schedule entry %d: unknown preset", n);
+            }
+            se->action = UI_SCHED_PRESET;
+            se->preset = (uint8_t)index;
+        } else if (strcmp(a, "night") == 0) {
+            if (!parse_hhmm(cJSON_GetObjectItemCaseSensitive(e, "until"), &se->until_min)) {
+                return fail(err, size, "schedule entry %d: night needs \"until\" as HH:MM", n);
+            }
+            if (se->until_min == se->at_min) {
+                return fail(err, size, "schedule entry %d: a night must end at another time than it starts", n);
+            }
+            se->action = UI_SCHED_NIGHT;
+        } else {
+            return fail(err, size, "schedule entry %d: action must be preset or night", n);
+        }
+        out->schedule.count++;
+    }
+    return true;
+}
+
 static bool parse(const cJSON *root, ui_presets_t *out, char *err, size_t size)
 {
     const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
@@ -179,11 +264,15 @@ static bool parse(const cJSON *root, ui_presets_t *out, char *err, size_t size)
     out->cycle_interval_s = (uint16_t)(seconds < UI_CYCLE_MIN_S ? UI_CYCLE_MIN_S
                                        : seconds > UI_CYCLE_MAX_S ? UI_CYCLE_MAX_S
                                                                   : seconds);
-    return true;
+    const cJSON *schedule = cJSON_GetObjectItemCaseSensitive(root, "schedule");
+    return schedule == NULL || cJSON_IsNull(schedule) || parse_schedule(schedule, out, err, size);
 }
 
 bool ui_presets_from_json(const char *json, ui_presets_t *out, char *err, size_t err_size)
 {
+    if (util_json_depth(json) > UI_JSON_MAX_DEPTH) {
+        return fail(err, err_size, "nested more than %d levels", UI_JSON_MAX_DEPTH);
+    }
     cJSON *root = json != NULL ? cJSON_Parse(json) : NULL;
     if (root == NULL) {
         return fail(err, err_size, "not valid JSON");
@@ -237,7 +326,30 @@ size_t ui_presets_to_json(const ui_presets_t *p, char *out, size_t size)
     for (int i = 0; i < p->count; i++) {
         cJSON_AddItemToArray(presets, preset_json(&p->presets[i]));
     }
-    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, true);
+    if (p->schedule.enabled || p->schedule.count) {
+        cJSON *schedule = cJSON_AddObjectToObject(root, "schedule");
+        cJSON_AddBoolToObject(schedule, "enabled", p->schedule.enabled);
+        cJSON *entries = cJSON_AddArrayToObject(schedule, "entries");
+        for (int i = 0; i < p->schedule.count && i < UI_SCHEDULE_MAX; i++) {
+            const ui_schedule_entry_t *e = &p->schedule.entries[i];
+            cJSON *obj = cJSON_CreateObject();
+            char hhmm[8];
+            snprintf(hhmm, sizeof(hhmm), "%02d:%02d", e->at_min / 60 % 24, e->at_min % 60);
+            cJSON_AddStringToObject(obj, "at", hhmm);
+            cJSON_AddNumberToObject(obj, "days", e->days);
+            if (e->action == UI_SCHED_NIGHT) {
+                cJSON_AddStringToObject(obj, "action", "night");
+                snprintf(hhmm, sizeof(hhmm), "%02d:%02d", e->until_min / 60 % 24, e->until_min % 60);
+                cJSON_AddStringToObject(obj, "until", hhmm);
+            } else {
+                cJSON_AddStringToObject(obj, "action", "preset");
+                cJSON_AddStringToObject(obj, "preset", e->preset < p->count ? p->presets[e->preset].id : "");
+            }
+            cJSON_AddItemToArray(entries, obj);
+        }
+    }
+    /* unformatted: the device reads it, and 16 presets must fit UI_PRESETS_JSON_MAX */
+    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
     cJSON_Delete(root);
     return ok ? strlen(out) : 0;
 }
```

```c
#include "scheduler.h"
#include "ui_preset.h"

time_t ui_schedule_next(const ui_schedule_t *schedule, time_t after, int *index)
{
    time_t best = 0;
    for (int i = 0; i < schedule->count && i < UI_SCHEDULE_MAX; i++) {
        const ui_schedule_entry_t *e = &schedule->entries[i];
        time_t t = sched_next_weekly(after, e->at_min, e->days);
        if (t != 0 && (best == 0 || t < best)) {
            best = t;
            *index = i;
        }
    }
    return best;
}

/* An entry is due at most once: the app checks at every entry's minute, and a clock that jumps
 * restarts the checks (main/app.c), so a span never covers a day. */
int ui_schedule_due(const ui_schedule_t *schedule, time_t after, time_t now, int order[UI_SCHEDULE_MAX])
{
    long long key[UI_SCHEDULE_MAX]; /* the time, with a night after the presets of its minute */
    int n = 0;
    for (int i = 0; i < schedule->count && i < UI_SCHEDULE_MAX; i++) {
        const ui_schedule_entry_t *e = &schedule->entries[i];
        time_t t = sched_next_weekly(after, e->at_min, e->days);
        if (t == 0 || t > now) {
            continue;
        }
        long long k = (long long)t * 2 + (e->action == UI_SCHED_NIGHT);
        int j = n++;
        for (; j > 0 && key[j - 1] > k; j--) { /* equal keys keep their list order */
            key[j] = key[j - 1];
            order[j] = order[j - 1];
        }
        key[j] = k;
        order[j] = i;
    }
    return n;
}
```

```cmake
# Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                            "ui_status.c" "ui_dashboard.c" "ui_schedule.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx locale datastore util
                       PRIV_REQUIRES json scheduler)
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_ui_preset && ./build-host/test_ui_schedule && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `18 Tests 0 Failures`, then `4 Tests 0 Failures`; the suite passes 31 of 31; the firmware builds without warnings. The app keeps the schedule it loads and saves it back, but runs nothing until Task 12.

- [ ] **Step 5: Commit.**

```bash
git add components/ui test/host/test_ui_preset.c test/host/test_ui_schedule.c test/host/CMakeLists.txt
git commit -m "feat(ui): add the preset schedule and parse presets.json more strictly"
```

---


### Task 6: A nesting limit, a kept backup and the factory erase (`storage`)

**Files:**
- Modify:
  - `components/storage/settings.c`, `components/storage/storage_file.c`, `components/storage/include/storage_file.h`, `components/storage/storage.c`, `components/storage/include/storage.h`, `components/storage/CMakeLists.txt`
  - `test/host/test_settings.c`, `test/host/test_storage_file.c`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `util_json_depth()` (Task 1).
- Produces:
  - `settings_from_json()` rejects a file nested deeper than 16 levels, with an error containing "nested", before cJSON sees it. The M3a review found the recursion unbounded on the app task's 8 KB stack.
  - `storage_file_load()`: when the backup parsed because the main file was read whole but rejected, the main file is removed. The next save's rename would otherwise move the rejected file over the good backup (M3a review). A file too big for the buffer stays, since its content was never judged.
  - `esp_err_t storage_erase(void)`: unmounts and formats the `storage` partition, so the next boot starts empty (spec §14.4; Task 12's factory reset).

- [ ] **Step 1: Write the failing tests.** Apply the patch: one test in each file.

```diff
diff --git a/test/host/test_settings.c b/test/host/test_settings.c
index 613bfb8..1f5227e 100644
--- a/test/host/test_settings.c
+++ b/test/host/test_settings.c
@@ -1,3 +1,4 @@
+#include <stdio.h>
 #include <string.h>
 
 #include "settings.h"
@@ -70,6 +71,21 @@ static void test_not_json_or_another_schema_fails(void)
     TEST_ASSERT_EQUAL_STRING("schema must be 1", s_err);
 }
 
+static void test_deep_nesting_is_rejected_before_parsing(void)
+{
+    static char json[256];
+    size_t n = (size_t)snprintf(json, sizeof(json), "{\"schema\": 1, \"x\": ");
+    for (int i = 0; i < 40; i++) {
+        json[n++] = '[';
+    }
+    for (int i = 0; i < 40; i++) {
+        json[n++] = ']';
+    }
+    snprintf(json + n, sizeof(json) - n, "}");
+    TEST_ASSERT_FALSE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_NOT_NULL(strstr(s_err, "nested"));
+}
+
 static void test_saving_keeps_keys_this_firmware_does_not_know(void)
 {
     const char *base = "{\"schema\": 1, \"mqtt\": {\"host\": \"ha.local\"}, \"time\": {\"ntp\": [\"a\"], \"clock_24h\": true}}";
@@ -102,6 +118,7 @@ int main(void)
     RUN_TEST(test_missing_keys_take_the_defaults);
     RUN_TEST(test_bad_values_are_clamped_or_ignored_one_by_one);
     RUN_TEST(test_not_json_or_another_schema_fails);
+    RUN_TEST(test_deep_nesting_is_rejected_before_parsing);
     RUN_TEST(test_saving_keeps_keys_this_firmware_does_not_know);
     RUN_TEST(test_saving_without_a_base_round_trips);
     return UNITY_END();
```

```diff
diff --git a/test/host/test_storage_file.c b/test/host/test_storage_file.c
index f3f0729..74e8809 100644
--- a/test/host/test_storage_file.c
+++ b/test/host/test_storage_file.c
@@ -127,6 +127,19 @@ static void test_a_rejected_file_falls_back_to_the_backup(void)
     TEST_ASSERT_EQUAL(STORAGE_REJECTED_MAIN, rejected);
 }
 
+static void test_a_fallback_load_keeps_the_good_backup_for_the_next_save(void)
+{
+    storage_file_write_atomic(PATH, "{\"good\":1}", 10);
+    storage_file_write_atomic(PATH, "garbage", 7);
+    char buf[64];
+    bool from_backup = false;
+    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, load(buf, sizeof(buf), &from_backup, NULL));
+    TEST_ASSERT_TRUE(from_backup);
+    storage_file_write_atomic(PATH, "{\"new\":1}", 9); /* the save after the fallback */
+    TEST_ASSERT_EQUAL_STRING("{\"new\":1}", contents(PATH));
+    TEST_ASSERT_EQUAL_STRING("{\"good\":1}", contents(PATH ".bak")); /* not the rejected file */
+}
+
 static void test_power_lost_between_the_renames_loads_the_previous_file(void)
 {
     /* <path> already became <path>.bak; the new file is still <path>.tmp. */
@@ -198,6 +211,7 @@ int main(void)
     RUN_TEST(test_each_write_keeps_the_previous_file_as_the_backup);
     RUN_TEST(test_a_good_file_is_used_without_reading_the_backup);
     RUN_TEST(test_a_rejected_file_falls_back_to_the_backup);
+    RUN_TEST(test_a_fallback_load_keeps_the_good_backup_for_the_next_save);
     RUN_TEST(test_power_lost_between_the_renames_loads_the_previous_file);
     RUN_TEST(test_a_file_too_big_for_the_buffer_is_rejected_unparsed);
     RUN_TEST(test_both_rejected_is_invalid);
```

- [ ] **Step 2: Watch them fail.** In `test/host/CMakeLists.txt`, `storage_logic` links `util` (for the nesting check):

```cmake
target_link_libraries(storage_logic PRIVATE cjson util m)
```

Run: `cmake --build build-host && ./build-host/test_settings; ./build-host/test_storage_file`
Expected:
- `test_deep_nesting_is_rejected_before_parsing` fails: cJSON parses 40 levels, and the unknown key is ignored.
- `test_a_fallback_load_keeps_the_good_backup_for_the_next_save` fails: `Expected '{"good":1}' Was 'garbage'`.

- [ ] **Step 3: Implement.** Apply the patches; the CMake file replaces the old one.

```diff
diff --git a/components/storage/settings.c b/components/storage/settings.c
index dd0c061..8f9bff4 100644
--- a/components/storage/settings.c
+++ b/components/storage/settings.c
@@ -6,8 +6,10 @@
 #include <string.h>
 
 #include "cJSON.h"
+#include "util_json.h"
 
 #define SCHEMA 1
+#define SETTINGS_JSON_MAX_DEPTH 16 /* the sketch nests 3 levels */
 
 static bool fail(char *err, size_t size, const char *fmt, ...)
 {
@@ -68,6 +70,9 @@ static uint8_t lpm_from_hz(const cJSON *display, uint8_t fallback)
 
 bool settings_from_json(const char *json, const settings_t *defaults, settings_t *out, char *err, size_t err_size)
 {
+    if (util_json_depth(json) > SETTINGS_JSON_MAX_DEPTH) {
+        return fail(err, err_size, "nested more than %d levels", SETTINGS_JSON_MAX_DEPTH);
+    }
     cJSON *root = json != NULL ? cJSON_Parse(json) : NULL;
     if (!cJSON_IsObject(root)) {
         cJSON_Delete(root);
```

```diff
diff --git a/components/storage/include/storage_file.h b/components/storage/include/storage_file.h
index b23f930..dffbe00 100644
--- a/components/storage/include/storage_file.h
+++ b/components/storage/include/storage_file.h
@@ -31,7 +31,8 @@ enum {
  * Reads `path` into buf (NUL-terminated, at most size - 1 bytes) and hands it to `parse`. A file
  * that is missing, unreadable, larger than the buffer or rejected by `parse` falls back to
  * <path>.bak. `*from_backup` says which one parsed; `*rejected` (may be NULL) gets the
- * STORAGE_REJECTED_* bits.
+ * STORAGE_REJECTED_* bits. When the backup parsed because <path> was read whole but rejected,
+ * <path> is removed, so the next save keeps the good backup (a file too big for the buffer stays).
  */
 storage_file_result_t storage_file_load(const char *path, char *buf, size_t size, storage_parse_t parse, void *ctx,
                                         bool *from_backup, unsigned *rejected);
```

```diff
diff --git a/components/storage/storage_file.c b/components/storage/storage_file.c
index 47046bb..b82bae6 100644
--- a/components/storage/storage_file.c
+++ b/components/storage/storage_file.c
@@ -43,6 +43,7 @@ storage_file_result_t storage_file_load(const char *path, char *buf, size_t size
     }
     const char *const candidates[] = { path, bak };
     storage_file_result_t result = STORAGE_FILE_MISSING;
+    bool main_invalid = false; /* read whole and rejected: its content is no use to anyone */
     for (int i = 0; i < 2; i++) {
         read_result_t r = read_file(candidates[i], buf, size);
         if (r == READ_MISSING) {
@@ -53,9 +54,13 @@ storage_file_result_t storage_file_load(const char *path, char *buf, size_t size
             result = STORAGE_FILE_OK;
             break;
         }
+        main_invalid |= i == 0 && r == READ_OK;
         bits |= 1u << i;
         result = STORAGE_FILE_INVALID;
     }
+    if (result == STORAGE_FILE_OK && *from_backup && main_invalid) {
+        unlink(path); /* else the next save would rename it over the good backup */
+    }
     if (rejected != NULL) {
         *rejected = bits;
     }
```

```diff
diff --git a/components/storage/include/storage.h b/components/storage/include/storage.h
index 5edc286..3e806c8 100644
--- a/components/storage/include/storage.h
+++ b/components/storage/include/storage.h
@@ -16,6 +16,8 @@
 
 esp_err_t storage_init(void);
 bool storage_ready(void);
+/* Factory reset (spec §14.4): unmounts and formats the partition. The next boot starts empty. */
+esp_err_t storage_erase(void);
 
 /*
  * storage_file_load() on the mounted partition, logging rejected files (spec §14.3). ESP_OK if
```

```diff
diff --git a/components/storage/storage.c b/components/storage/storage.c
index d09dfc8..30cfb24 100644
--- a/components/storage/storage.c
+++ b/components/storage/storage.c
@@ -41,6 +41,15 @@ bool storage_ready(void)
     return s_ready;
 }
 
+esp_err_t storage_erase(void)
+{
+    if (s_ready) {
+        esp_vfs_littlefs_unregister("storage");
+        s_ready = false;
+    }
+    return esp_littlefs_format("storage");
+}
+
 esp_err_t storage_load(const char *path, char *buf, size_t size, storage_parse_t parse, void *ctx, bool *from_backup)
 {
     ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not mounted");
```

```cmake
# LittleFS config files and the settings codec (spec §14). settings.c and storage_file.c are pure
# C (cJSON and POSIX files) and are also built on the host by test/host.
idf_component_register(SRCS "storage.c" "storage_file.c" "settings.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json util vfs joltwallet__littlefs)
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_settings && ./build-host/test_storage_file && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `7 Tests 0 Failures`, then `11 Tests 0 Failures`; the suite passes 31 of 31; the firmware builds without warnings.

- [ ] **Step 5: Commit.**

```bash
git add components/storage test/host/test_settings.c test/host/test_storage_file.c test/host/CMakeLists.txt
git commit -m "fix(storage): limit JSON nesting, keep a good backup, add the factory erase"
```

---


### Task 7: The humidity clamp and the critical threshold (`sensors`)

**Files:**
- Modify:
  - `components/sensors/include/shtc3_codec.h`, `components/sensors/shtc3_codec.c`, `components/sensors/sensors.c`
  - `components/sensors/include/battery_model.h`, `components/sensors/battery_model.c`
  - `test/host/test_shtc3_codec.c`, `test/host/test_battery_model.c`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `int shtc3_offset_humidity(int hum_pct100, int offset_pct100)`: the reading plus its offset, clamped to 0–10000 (0.01 %RH). `sensors_sample_env()` uses it, so an offset edited in the menu can't show 104 % (an M3a carry-over note).
  - `BATTERY_CRITICAL_MV` 3300 and `BATTERY_RECOVER_MV` 3400.
  - `bool battery_critical(bool was_critical, int mv, battery_state_t state)` implements spec §8, with hysteresis so the screen doesn't flicker at the threshold. The rules:
    - critical at 3300 mV or below while not charging;
    - it stays critical until 3400 mV, or until the gauge sees charging or full;
    - 0 mV (no reading yet) changes nothing.

    Task 12 uses it.

- [ ] **Step 1: Write the failing tests.** Apply the patch: one test in each file.

```diff
diff --git a/test/host/test_shtc3_codec.c b/test/host/test_shtc3_codec.c
index de2504d..0b5742e 100644
--- a/test/host/test_shtc3_codec.c
+++ b/test/host/test_shtc3_codec.c
@@ -41,6 +41,13 @@ static void test_converts_the_range_ends(void)
     TEST_ASSERT_EQUAL_INT(10000, rh); /* 99.998 %RH rounds to 100.00 */
 }
 
+static void test_offsets_keep_humidity_within_0_and_100_percent(void)
+{
+    TEST_ASSERT_EQUAL_INT(4750, shtc3_offset_humidity(4500, 250));
+    TEST_ASSERT_EQUAL_INT(10000, shtc3_offset_humidity(9800, 500)); /* a +5 % offset near saturation */
+    TEST_ASSERT_EQUAL_INT(0, shtc3_offset_humidity(300, -500));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -48,5 +55,6 @@ int main(void)
     RUN_TEST(test_parses_the_datasheet_example);
     RUN_TEST(test_rejects_a_bad_crc);
     RUN_TEST(test_converts_the_range_ends);
+    RUN_TEST(test_offsets_keep_humidity_within_0_and_100_percent);
     return UNITY_END();
 }
```

```diff
diff --git a/test/host/test_battery_model.c b/test/host/test_battery_model.c
index 6a7e767..f8dd612 100644
--- a/test/host/test_battery_model.c
+++ b/test/host/test_battery_model.c
@@ -190,6 +190,21 @@ static void test_a_clock_moved_back_restarts_the_history(void)
     TEST_ASSERT_EQUAL_INT(-1, battery_gauge_days_left10(&s_g, t - 3 * 3600));
 }
 
+/* Spec §8: critical at 3.3 V or below while not charging; leaving takes 3.4 V, or charging. */
+static void test_critical_has_hysteresis_and_ignores_missing_readings(void)
+{
+    TEST_ASSERT_FALSE(battery_critical(false, 3301, BATTERY_DISCHARGING));
+    TEST_ASSERT_TRUE(battery_critical(false, 3300, BATTERY_DISCHARGING));
+    TEST_ASSERT_TRUE(battery_critical(false, 3300, BATTERY_UNKNOWN));
+    TEST_ASSERT_FALSE(battery_critical(false, 3200, BATTERY_CHARGING));
+    TEST_ASSERT_TRUE(battery_critical(true, 3399, BATTERY_DISCHARGING)); /* not recovered yet */
+    TEST_ASSERT_FALSE(battery_critical(true, 3400, BATTERY_DISCHARGING));
+    TEST_ASSERT_FALSE(battery_critical(true, 3350, BATTERY_CHARGING));
+    TEST_ASSERT_FALSE(battery_critical(true, 3350, BATTERY_FULL));
+    TEST_ASSERT_TRUE(battery_critical(true, 0, BATTERY_UNKNOWN)); /* no reading: no change */
+    TEST_ASSERT_FALSE(battery_critical(false, 0, BATTERY_UNKNOWN));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -210,5 +225,6 @@ int main(void)
     RUN_TEST(test_a_shifted_history_keeps_the_estimate_across_a_clock_set);
     RUN_TEST(test_a_clock_moved_forward_gives_no_estimate_rather_than_a_wrong_one);
     RUN_TEST(test_a_clock_moved_back_restarts_the_history);
+    RUN_TEST(test_critical_has_hysteresis_and_ignores_missing_readings);
     return UNITY_END();
 }
```

- [ ] **Step 2: Watch them fail.**

Run: `cmake --build build-host`
Expected: FAIL to compile: `shtc3_offset_humidity` and `battery_critical` are undeclared.

- [ ] **Step 3: Implement.** Apply the patches.

```diff
diff --git a/components/sensors/include/shtc3_codec.h b/components/sensors/include/shtc3_codec.h
index 91453d5..ea047d3 100644
--- a/components/sensors/include/shtc3_codec.h
+++ b/components/sensors/include/shtc3_codec.h
@@ -9,3 +9,5 @@
 uint8_t shtc3_crc8(const uint8_t *data, size_t len); /* poly 0x31, init 0xFF */
 /* Parses a T-first measurement: T msb, lsb, crc, RH msb, lsb, crc. False on a CRC mismatch. */
 bool shtc3_parse(const uint8_t raw[6], int *temp_c100, int *hum_pct100);
+/* A humidity with its calibration offset, kept within 0-100 %RH (0.01 % units). */
+int shtc3_offset_humidity(int hum_pct100, int offset_pct100);
```

```diff
diff --git a/components/sensors/shtc3_codec.c b/components/sensors/shtc3_codec.c
index 774183c..2900a46 100644
--- a/components/sensors/shtc3_codec.c
+++ b/components/sensors/shtc3_codec.c
@@ -23,3 +23,9 @@ bool shtc3_parse(const uint8_t raw[6], int *temp_c100, int *hum_pct100)
     *hum_pct100 = (int)((10000 * rh + 32768) / 65536);        /* RH = 100 * S / 2^16 */
     return true;
 }
+
+int shtc3_offset_humidity(int hum_pct100, int offset_pct100)
+{
+    int h = hum_pct100 + offset_pct100;
+    return h < 0 ? 0 : h > 10000 ? 10000 : h;
+}
```

```diff
diff --git a/components/sensors/sensors.c b/components/sensors/sensors.c
index 4c5c1a2..e088df2 100644
--- a/components/sensors/sensors.c
+++ b/components/sensors/sensors.c
@@ -128,7 +128,7 @@ esp_err_t sensors_sample_env(time_t now)
     s_state.env = (sensors_env_t){
         .valid = true,
         .temp_c100 = t + s_temp_offset_c100,
-        .hum_pct100 = h + s_hum_offset_pct100,
+        .hum_pct100 = shtc3_offset_humidity(h, s_hum_offset_pct100),
         .time = now,
     };
     return ESP_OK;
```

```diff
diff --git a/components/sensors/include/battery_model.h b/components/sensors/include/battery_model.h
index 1104899..0b6cda8 100644
--- a/components/sensors/include/battery_model.h
+++ b/components/sensors/include/battery_model.h
@@ -1,5 +1,6 @@
 #pragma once
 
+#include <stdbool.h>
 #include <stdint.h>
 
 /*
@@ -53,3 +54,10 @@ int battery_gauge_days_left10(const battery_gauge_t *g, uint32_t now_s);
  * its spacing on the new clock; a board without the RTC cell starts in 2000 (spec §7). A clock
  * that moves back without it restarts the history instead. */
 void battery_gauge_shift_time(battery_gauge_t *g, int64_t delta_s);
+
+#define BATTERY_CRITICAL_MV 3300 /* spec §8: at or below, the critical screen */
+#define BATTERY_RECOVER_MV  3400 /* the critical screen stays up until then, or until charging */
+/* Whether the battery is critical after a reading of `mv` (smoothed), given whether it was before:
+ * spec §8 with some hysteresis, so the screen doesn't flicker at the threshold. 0 mV means no
+ * reading yet and changes nothing. */
+bool battery_critical(bool was_critical, int mv, battery_state_t state);
```

```diff
diff --git a/components/sensors/battery_model.c b/components/sensors/battery_model.c
index 9babc04..7c2b32b 100644
--- a/components/sensors/battery_model.c
+++ b/components/sensors/battery_model.c
@@ -196,3 +196,15 @@ int battery_gauge_level(const battery_gauge_t *g)
 {
     return g->ema_mv16 == 0 ? -1 : g->level;
 }
+
+bool battery_critical(bool was_critical, int mv, battery_state_t state)
+{
+    bool charging = state == BATTERY_CHARGING || state == BATTERY_FULL;
+    if (mv <= 0) {
+        return was_critical;
+    }
+    if (was_critical) {
+        return !charging && mv < BATTERY_RECOVER_MV;
+    }
+    return !charging && mv <= BATTERY_CRITICAL_MV;
+}
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_shtc3_codec && ./build-host/test_battery_model && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `5 Tests 0 Failures`, then `18 Tests 0 Failures`; the suite passes 31 of 31; the firmware builds without warnings.

- [ ] **Step 5: Commit.**

```bash
git add components/sensors test/host/test_shtc3_codec.c test/host/test_battery_model.c
git commit -m "feat(sensors): clamp offset humidity and add the critical-battery threshold"
```

---


### Task 8: The menu state machine (`ui` menu)

**Files:**
- Create: `components/ui/include/ui_menu.h`, `components/ui/ui_menu.c`, `test/host/test_ui_menu.c`
- Modify: `components/ui/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: the menu strings (Task 2): `LS_MENU`, `LS_M_*`, `LS_ON`, `LS_OFF`; `lang_format_decimal()`.
- Produces (`ui_menu.h`):
  - `ui_menu_item_t`: every section and item of spec §5.7 in display order, each item after its section:
    - Presets: Active preset, Auto-cycle, Interval, Schedule.
    - Time: Set date and time, 24-hour clock, Time zone.
    - Display: Update interval (1–15 min), Refresh rate.
    - Sensors: Temperature offset (0.1 °C steps, ±10 °C), Humidity offset (0.5 % steps, ±20 %), Units.
    - Info: Battery, Firmware, Device, Uptime, Free memory.
    - System: Language, Reboot, Factory reset.

    Display ▸ Contrast is left out (owner, 2026-09-29).
  - `ui_menu_key_t`, from the gestures of spec §5.6:
    - `NEXT`, KEY short: the next item, or + while editing;
    - `SELECT`, KEY long: open, edit, save or confirm;
    - `BACK`, BOOT short: up, or −;
    - `EXIT`, BOOT long: close, or cancel.
  - `ui_menu_model_t`: what the app shows and lets the user edit:
    - per-item values, in each item's own unit;
    - choice labels;
    - info texts;
    - hidden flags;
    - the local time for the editor.
  - `ui_menu_intent_t`, what the app must carry out:
    - `NONE`: redraw only;
    - `SET` item = value;
    - `SET_TIME` with a local `struct tm`;
    - `ACTION` (reboot, factory reset);
    - `CLOSE`.
  - `ui_menu_t` holds the state: section, cursor, mode (browse, edit, date-time, confirm), the value being edited, the date and time being edited, and its field.
  - Functions:
    - `ui_menu_open()`, `ui_menu_input()`;
    - `ui_menu_visible()`, `ui_menu_current()`;
    - `ui_menu_label()`, `ui_menu_value_text()`.
  - Behaviour:
    - A toggle flips at once.
    - A choice or number edits in place and saves with SELECT.
    - The date-time editor walks day, month, year (2000–2099, the RTC's range), hour and minute, keeping the day valid for the month.
    - Factory reset asks first, and only a long press confirms it; reboot doesn't ask.
    - `ui_draw_menu()` is declared here and implemented in Task 9.
  - Tasks 9 and 12 use all of it. The menu changes nothing outside its own state: the app applies the intents.

- [ ] **Step 1: Write the failing test.**

```c
#include <string.h>

#include "ui_menu.h"
#include "unity.h"

static ui_menu_t s_m;
static ui_menu_model_t s_model;
static const char *const k_presets[] = { "Home", "Indoor", "Weather", "Focus clock" };

void setUp(void)
{
    memset(&s_model, 0, sizeof(s_model));
    s_model.choices[UI_MI_ACTIVE_PRESET] = k_presets;
    s_model.choice_count[UI_MI_ACTIVE_PRESET] = 4;
    s_model.value[UI_MI_ACTIVE_PRESET] = 1;
    s_model.value[UI_MI_UPDATE_INTERVAL] = 1;
    s_model.local = (struct tm){ .tm_year = 126, .tm_mon = 8, .tm_mday = 25, .tm_hour = 20, .tm_min = 48 };
    ui_menu_open(&s_m);
}

void tearDown(void) {}

static ui_menu_intent_t press(ui_menu_key_t key)
{
    return ui_menu_input(&s_m, &s_model, key);
}

/* Moves the cursor to `item` in the current list and selects it. */
static ui_menu_intent_t open_item(ui_menu_item_t item)
{
    for (int i = 0; i < UI_MI_COUNT && ui_menu_current(&s_m, &s_model) != item; i++) {
        press(UI_MENU_KEY_NEXT);
    }
    TEST_ASSERT_EQUAL_INT(item, ui_menu_current(&s_m, &s_model));
    return press(UI_MENU_KEY_SELECT);
}

static void test_the_root_lists_the_sections_in_order(void)
{
    const ui_menu_item_t expected[] = { UI_MI_PRESETS, UI_MI_TIME, UI_MI_DISPLAY, UI_MI_SENSORS, UI_MI_INFO,
                                        UI_MI_SYSTEM };
    ui_menu_item_t items[UI_MI_COUNT];
    int n = ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT);
    TEST_ASSERT_EQUAL_INT(6, n);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 6);
    TEST_ASSERT_EQUAL_INT(UI_MI_PRESETS, ui_menu_current(&s_m, &s_model));
}

static void test_next_wraps_select_enters_and_back_returns_to_the_section(void)
{
    for (int i = 0; i < 6; i++) {
        press(UI_MENU_KEY_NEXT);
    }
    TEST_ASSERT_EQUAL_INT(UI_MI_PRESETS, ui_menu_current(&s_m, &s_model)); /* wrapped */
    open_item(UI_MI_SENSORS);
    TEST_ASSERT_EQUAL_INT(UI_MI_SENSORS, s_m.section);
    TEST_ASSERT_EQUAL_INT(UI_MI_TEMP_OFFSET, ui_menu_current(&s_m, &s_model));
    TEST_ASSERT_EQUAL(UI_MENU_NONE, press(UI_MENU_KEY_BACK).kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_ROOT, s_m.section);
    TEST_ASSERT_EQUAL_INT(UI_MI_SENSORS, ui_menu_current(&s_m, &s_model)); /* the cursor stays on it */
}

static void test_back_at_the_root_and_exit_anywhere_close_the_menu(void)
{
    TEST_ASSERT_EQUAL(UI_MENU_CLOSE, press(UI_MENU_KEY_BACK).kind);
    ui_menu_open(&s_m);
    open_item(UI_MI_TIME);
    TEST_ASSERT_EQUAL(UI_MENU_CLOSE, press(UI_MENU_KEY_EXIT).kind);
}

static void test_a_toggle_flips_at_once(void)
{
    open_item(UI_MI_PRESETS);
    ui_menu_intent_t in = open_item(UI_MI_AUTO_CYCLE);
    TEST_ASSERT_EQUAL(UI_MENU_SET, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_AUTO_CYCLE, in.item);
    TEST_ASSERT_EQUAL_INT(1, in.value);
    TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
}

static void test_a_choice_is_saved_with_select_or_dropped_with_exit(void)
{
    open_item(UI_MI_PRESETS);
    open_item(UI_MI_ACTIVE_PRESET);
    TEST_ASSERT_EQUAL(UI_MENU_EDIT, s_m.mode);
    press(UI_MENU_KEY_NEXT);
    press(UI_MENU_KEY_NEXT);
    press(UI_MENU_KEY_NEXT); /* 1 -> 2 -> 3 -> 0: wraps */
    TEST_ASSERT_EQUAL_INT(0, s_m.edit);
    press(UI_MENU_KEY_BACK); /* back to 3 */
    ui_menu_intent_t in = press(UI_MENU_KEY_SELECT);
    TEST_ASSERT_EQUAL(UI_MENU_SET, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_ACTIVE_PRESET, in.item);
    TEST_ASSERT_EQUAL_INT(3, in.value);
    press(UI_MENU_KEY_SELECT); /* edit again, then cancel */
    press(UI_MENU_KEY_NEXT);
    TEST_ASSERT_EQUAL(UI_MENU_NONE, press(UI_MENU_KEY_EXIT).kind);
    TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
    TEST_ASSERT_EQUAL_INT(UI_MI_PRESETS, s_m.section); /* cancelling an edit doesn't leave the section */
}

static void test_numbers_step_and_stop_at_their_limits(void)
{
    open_item(UI_MI_DISPLAY);
    open_item(UI_MI_UPDATE_INTERVAL); /* 1-15 min */
    press(UI_MENU_KEY_BACK);          /* already at the minimum */
    TEST_ASSERT_EQUAL_INT(1, s_m.edit);
    for (int i = 0; i < 20; i++) {
        press(UI_MENU_KEY_NEXT);
    }
    TEST_ASSERT_EQUAL_INT(15, s_m.edit);
    TEST_ASSERT_EQUAL_INT(15, press(UI_MENU_KEY_SELECT).value);
    press(UI_MENU_KEY_EXIT);
    ui_menu_open(&s_m);
    open_item(UI_MI_SENSORS);
    open_item(UI_MI_TEMP_OFFSET); /* 0.1 degree steps */
    press(UI_MENU_KEY_BACK);
    press(UI_MENU_KEY_BACK);
    TEST_ASSERT_EQUAL_INT(-2, press(UI_MENU_KEY_SELECT).value);
}

static void test_the_date_time_editor_walks_its_fields_and_keeps_the_day_valid(void)
{
    s_model.local.tm_mday = 31;
    s_model.local.tm_mon = 0; /* 31 January */
    open_item(UI_MI_TIME);
    open_item(UI_MI_SET_DATETIME);
    TEST_ASSERT_EQUAL(UI_MENU_DATETIME, s_m.mode);
    TEST_ASSERT_EQUAL_INT(0, s_m.dt_field);        /* the day first */
    press(UI_MENU_KEY_NEXT);                       /* 31 wraps to 1 */
    TEST_ASSERT_EQUAL_INT(1, s_m.dt.tm_mday);
    press(UI_MENU_KEY_BACK);                       /* and back to 31 */
    TEST_ASSERT_EQUAL(UI_MENU_NONE, press(UI_MENU_KEY_SELECT).kind);
    press(UI_MENU_KEY_NEXT);                       /* February: the day drops to 28 */
    TEST_ASSERT_EQUAL_INT(1, s_m.dt.tm_mon);
    TEST_ASSERT_EQUAL_INT(28, s_m.dt.tm_mday);
    press(UI_MENU_KEY_SELECT);                     /* the year */
    press(UI_MENU_KEY_SELECT);                     /* the hour */
    press(UI_MENU_KEY_BACK);                       /* 20 -> 19 */
    press(UI_MENU_KEY_SELECT);                     /* the minute */
    press(UI_MENU_KEY_NEXT);                       /* 48 -> 49 */
    ui_menu_intent_t in = press(UI_MENU_KEY_SELECT);
    TEST_ASSERT_EQUAL(UI_MENU_SET_TIME, in.kind);
    TEST_ASSERT_EQUAL_INT(126, in.local.tm_year);
    TEST_ASSERT_EQUAL_INT(1, in.local.tm_mon);
    TEST_ASSERT_EQUAL_INT(28, in.local.tm_mday);
    TEST_ASSERT_EQUAL_INT(19, in.local.tm_hour);
    TEST_ASSERT_EQUAL_INT(49, in.local.tm_min);
    TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
}

static void test_factory_reset_asks_first(void)
{
    open_item(UI_MI_SYSTEM);
    TEST_ASSERT_EQUAL(UI_MENU_NONE, open_item(UI_MI_FACTORY_RESET).kind);
    TEST_ASSERT_EQUAL(UI_MENU_CONFIRM, s_m.mode);
    TEST_ASSERT_EQUAL(UI_MENU_NONE, press(UI_MENU_KEY_BACK).kind); /* cancelled */
    TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
    press(UI_MENU_KEY_SELECT);
    ui_menu_intent_t in = press(UI_MENU_KEY_SELECT);
    TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_FACTORY_RESET, in.item);
    ui_menu_open(&s_m);
    open_item(UI_MI_SYSTEM);
    in = open_item(UI_MI_REBOOT); /* rebooting loses nothing: no question */
    TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_REBOOT, in.item);
}

static void test_hidden_items_are_skipped_and_info_does_nothing(void)
{
    s_model.hidden[UI_MI_DISPLAY] = true;
    ui_menu_item_t items[UI_MI_COUNT];
    TEST_ASSERT_EQUAL_INT(5, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
    press(UI_MENU_KEY_NEXT);
    press(UI_MENU_KEY_NEXT);
    TEST_ASSERT_EQUAL_INT(UI_MI_SENSORS, ui_menu_current(&s_m, &s_model));
    open_item(UI_MI_INFO);
    TEST_ASSERT_EQUAL(UI_MENU_NONE, open_item(UI_MI_INFO_UPTIME).kind);
    TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_root_lists_the_sections_in_order);
    RUN_TEST(test_next_wraps_select_enters_and_back_returns_to_the_section);
    RUN_TEST(test_back_at_the_root_and_exit_anywhere_close_the_menu);
    RUN_TEST(test_a_toggle_flips_at_once);
    RUN_TEST(test_a_choice_is_saved_with_select_or_dropped_with_exit);
    RUN_TEST(test_numbers_step_and_stop_at_their_limits);
    RUN_TEST(test_the_date_time_editor_walks_its_fields_and_keeps_the_day_valid);
    RUN_TEST(test_factory_reset_asks_first);
    RUN_TEST(test_hidden_items_are_skipped_and_info_does_nothing);
    return UNITY_END();
}
```

- [ ] **Step 2: Register it and watch it fail.** In `test/host/CMakeLists.txt`, after `reflbo_host_test(test_ui_widget_fit ui)`:

```cmake
reflbo_host_test(test_ui_menu ui)
```

Run: `cmake --build build-host`
Expected: FAIL to compile: `ui_menu.h` not found.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "gfx.h"
#include "lang.h"

/*
 * The on-device menu (spec §5.7) as a state machine. The app maps gestures to keys (spec §5.6),
 * passes the current values in a model, and carries out the intents the menu returns; the menu
 * itself changes nothing outside its own state. Pure C, host-buildable.
 */

/* Every section and item, in display order: a section's children follow it. */
typedef enum {
    UI_MI_ROOT,
    UI_MI_PRESETS,
    UI_MI_ACTIVE_PRESET,  /* choice: the presets' names */
    UI_MI_AUTO_CYCLE,     /* toggle */
    UI_MI_CYCLE_INTERVAL, /* choice: interval labels */
    UI_MI_SCHEDULE,       /* toggle */
    UI_MI_TIME,
    UI_MI_SET_DATETIME, /* the date-time editor */
    UI_MI_CLOCK_24H,    /* toggle */
    UI_MI_TIME_ZONE,    /* choice: the zone short list */
    UI_MI_DISPLAY,
    UI_MI_UPDATE_INTERVAL, /* number, minutes 1-15 */
    UI_MI_REFRESH_RATE,    /* choice: 0.25-8 Hz */
    UI_MI_SENSORS,
    UI_MI_TEMP_OFFSET, /* number, 0.1 °C, -10.0 to +10.0 */
    UI_MI_HUM_OFFSET,  /* number, 0.1 %, -20.0 to +20.0 in 0.5 steps */
    UI_MI_UNITS,       /* choice: °C, °F */
    UI_MI_INFO,
    UI_MI_INFO_BATTERY, /* info texts */
    UI_MI_INFO_FIRMWARE,
    UI_MI_INFO_DEVICE,
    UI_MI_INFO_UPTIME,
    UI_MI_INFO_MEMORY,
    UI_MI_SYSTEM,
    UI_MI_LANGUAGE,      /* choice: the language packs */
    UI_MI_REBOOT,        /* action */
    UI_MI_FACTORY_RESET, /* action, confirmed first */
    UI_MI_COUNT,
} ui_menu_item_t;

typedef enum {
    UI_MENU_KEY_NEXT,   /* KEY short: the next item, or + while editing */
    UI_MENU_KEY_SELECT, /* KEY long: open, edit, save, or confirm */
    UI_MENU_KEY_BACK,   /* BOOT short: up a level, or - while editing */
    UI_MENU_KEY_EXIT,   /* BOOT long: close the menu, or cancel an edit */
} ui_menu_key_t;

/* What the app shows and lets the user edit. Values are in each item's own unit (above). */
typedef struct {
    int32_t value[UI_MI_COUNT];
    const char *const *choices[UI_MI_COUNT]; /* labels of choice items, built by the app */
    uint8_t choice_count[UI_MI_COUNT];
    const char *info[UI_MI_COUNT]; /* texts of info items */
    bool hidden[UI_MI_COUNT];      /* features that don't exist yet */
    struct tm local;               /* the date-time editor starts from this */
} ui_menu_model_t;

typedef enum {
    UI_MENU_NONE,     /* only the menu changed: redraw it */
    UI_MENU_SET,      /* item = value */
    UI_MENU_SET_TIME, /* the local date and time in `local` */
    UI_MENU_ACTION,   /* run item: reboot, factory reset */
    UI_MENU_CLOSE,
} ui_menu_intent_kind_t;

typedef struct {
    ui_menu_intent_kind_t kind;
    ui_menu_item_t item;
    int32_t value;
    struct tm local;
} ui_menu_intent_t;

typedef enum {
    UI_MENU_BROWSE,
    UI_MENU_EDIT,     /* a choice or number */
    UI_MENU_DATETIME, /* the date-time editor */
    UI_MENU_CONFIRM,  /* an action that asks first */
} ui_menu_mode_t;

typedef struct {
    uint8_t section; /* the list shown: UI_MI_ROOT or a section */
    uint8_t cursor;  /* index among the section's visible items */
    uint8_t mode;    /* ui_menu_mode_t */
    int32_t edit;    /* the value being edited */
    struct tm dt;    /* the date and time being edited */
    uint8_t dt_field; /* 0 day, 1 month, 2 year, 3 hour, 4 minute */
} ui_menu_t;

void ui_menu_open(ui_menu_t *m);
ui_menu_intent_t ui_menu_input(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_key_t key);
/* The visible items of the current section, in order; returns their number. */
int ui_menu_visible(const ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t *out, int max);
ui_menu_item_t ui_menu_current(const ui_menu_t *m, const ui_menu_model_t *model);
/* The label of an item in the pack's language. */
const char *ui_menu_label(ui_menu_item_t item, const lang_t *lang);
/* An item's value as the list shows it: "On", "Europe/Prague", "+0,5 °C". */
void ui_menu_value_text(ui_menu_item_t item, int32_t value, const ui_menu_model_t *model, const lang_t *lang,
                        char *out, size_t size);

/* The menu screen: the current list, or the editor or question it shows (spec §5.7). */
void ui_draw_menu(gfx_fb_t *fb, const ui_menu_t *m, const ui_menu_model_t *model, const lang_t *lang);
```

```c
#include "ui_menu.h"

#include <stdio.h>
#include <string.h>

/* The menu tree (spec §5.7). Items belong to the nearest section before them in ui_menu_item_t. */

typedef enum {
    K_SECTION,
    K_TOGGLE,
    K_CHOICE,
    K_NUMBER,
    K_DATETIME,
    K_INFO,
    K_ACTION,
} kind_t;

typedef struct {
    lang_str_t label;
    uint8_t kind;
    uint8_t parent;
    int16_t min, max, step; /* numbers */
    uint8_t decimals;
    const char *unit;
    bool confirm; /* actions */
} node_t;

static const node_t k_nodes[UI_MI_COUNT] = {
    [UI_MI_ROOT] = { .label = LS_MENU, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_PRESETS] = { .label = LS_M_PRESETS, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_ACTIVE_PRESET] = { .label = LS_M_ACTIVE_PRESET, .kind = K_CHOICE, .parent = UI_MI_PRESETS },
    [UI_MI_AUTO_CYCLE] = { .label = LS_M_AUTO_CYCLE, .kind = K_TOGGLE, .parent = UI_MI_PRESETS },
    [UI_MI_CYCLE_INTERVAL] = { .label = LS_M_CYCLE_INTERVAL, .kind = K_CHOICE, .parent = UI_MI_PRESETS },
    [UI_MI_SCHEDULE] = { .label = LS_M_SCHEDULE, .kind = K_TOGGLE, .parent = UI_MI_PRESETS },
    [UI_MI_TIME] = { .label = LS_M_TIME, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_SET_DATETIME] = { .label = LS_M_SET_DATETIME, .kind = K_DATETIME, .parent = UI_MI_TIME },
    [UI_MI_CLOCK_24H] = { .label = LS_M_CLOCK_24H, .kind = K_TOGGLE, .parent = UI_MI_TIME },
    [UI_MI_TIME_ZONE] = { .label = LS_M_TIME_ZONE, .kind = K_CHOICE, .parent = UI_MI_TIME },
    [UI_MI_DISPLAY] = { .label = LS_M_DISPLAY, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_UPDATE_INTERVAL] = { .label = LS_M_UPDATE_INTERVAL, .kind = K_NUMBER, .parent = UI_MI_DISPLAY, .min = 1,
                                .max = 15, .step = 1, .unit = "min" },
    [UI_MI_REFRESH_RATE] = { .label = LS_M_REFRESH_RATE, .kind = K_CHOICE, .parent = UI_MI_DISPLAY },
    [UI_MI_SENSORS] = { .label = LS_M_SENSORS, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_TEMP_OFFSET] = { .label = LS_M_TEMP_OFFSET, .kind = K_NUMBER, .parent = UI_MI_SENSORS, .min = -100,
                            .max = 100, .step = 1, .decimals = 1, .unit = "°C" },
    [UI_MI_HUM_OFFSET] = { .label = LS_M_HUM_OFFSET, .kind = K_NUMBER, .parent = UI_MI_SENSORS, .min = -200,
                           .max = 200, .step = 5, .decimals = 1, .unit = "%" },
    [UI_MI_UNITS] = { .label = LS_M_UNITS, .kind = K_CHOICE, .parent = UI_MI_SENSORS },
    [UI_MI_INFO] = { .label = LS_M_INFO, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_INFO_BATTERY] = { .label = LS_BATTERY, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_FIRMWARE] = { .label = LS_M_FIRMWARE, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_DEVICE] = { .label = LS_M_DEVICE, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_UPTIME] = { .label = LS_M_UPTIME, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_MEMORY] = { .label = LS_M_FREE_MEMORY, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_SYSTEM] = { .label = LS_M_SYSTEM, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_LANGUAGE] = { .label = LS_M_LANGUAGE, .kind = K_CHOICE, .parent = UI_MI_SYSTEM },
    [UI_MI_REBOOT] = { .label = LS_M_REBOOT, .kind = K_ACTION, .parent = UI_MI_SYSTEM },
    [UI_MI_FACTORY_RESET] = { .label = LS_M_FACTORY_RESET, .kind = K_ACTION, .parent = UI_MI_SYSTEM,
                              .confirm = true },
};

static const ui_menu_intent_t k_none = { .kind = UI_MENU_NONE };

void ui_menu_open(ui_menu_t *m)
{
    memset(m, 0, sizeof(*m));
    m->section = UI_MI_ROOT;
    m->mode = UI_MENU_BROWSE;
}

int ui_menu_visible(const ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t *out, int max)
{
    int n = 0;
    for (int i = UI_MI_ROOT + 1; i < UI_MI_COUNT; i++) {
        if (k_nodes[i].parent == m->section && !model->hidden[i] && n < max) {
            out[n++] = (ui_menu_item_t)i;
        }
    }
    return n;
}

ui_menu_item_t ui_menu_current(const ui_menu_t *m, const ui_menu_model_t *model)
{
    ui_menu_item_t items[UI_MI_COUNT];
    int n = ui_menu_visible(m, model, items, UI_MI_COUNT);
    return n == 0 ? UI_MI_ROOT : items[m->cursor < n ? m->cursor : n - 1];
}

const char *ui_menu_label(ui_menu_item_t item, const lang_t *lang)
{
    return (unsigned)item < UI_MI_COUNT ? lang_str(lang, k_nodes[item].label) : "";
}

void ui_menu_value_text(ui_menu_item_t item, int32_t value, const ui_menu_model_t *model, const lang_t *lang,
                        char *out, size_t size)
{
    out[0] = '\0';
    if ((unsigned)item >= UI_MI_COUNT) {
        return;
    }
    const node_t *n = &k_nodes[item];
    switch (n->kind) {
    case K_TOGGLE:
        snprintf(out, size, "%s", lang_str(lang, value ? LS_ON : LS_OFF));
        break;
    case K_CHOICE:
        if (model->choices[item] != NULL && value >= 0 && value < model->choice_count[item]) {
            snprintf(out, size, "%s", model->choices[item][value]);
        }
        break;
    case K_NUMBER: {
        char num[16];
        lang_format_decimal(lang, value, n->decimals, num, sizeof(num));
        bool signed_offset = n->min < 0;
        snprintf(out, size, "%s%s %s", signed_offset && value > 0 ? "+" : "", num, n->unit);
        break;
    }
    case K_INFO:
        snprintf(out, size, "%s", model->info[item] != NULL ? model->info[item] : "");
        break;
    case K_DATETIME:
        snprintf(out, size, "%02d:%02d", model->local.tm_hour, model->local.tm_min);
        break;
    default:
        break;
    }
}

static int days_in_month(int year, int month) /* month 0-11 */
{
    static const int k_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 1 && leap ? 29 : k_days[month];
}

/* Steps a date-time field by delta, wrapping within its range; the day stays valid. */
static void step_datetime(ui_menu_t *m, int delta)
{
    struct tm *t = &m->dt;
    int year = t->tm_year + 1900;
    switch (m->dt_field) {
    case 0: {
        int days = days_in_month(year, t->tm_mon);
        t->tm_mday = (t->tm_mday - 1 + delta + days) % days + 1;
        break;
    }
    case 1:
        t->tm_mon = (t->tm_mon + delta + 12) % 12;
        break;
    case 2:
        year = 2000 + (year - 2000 + delta + 100) % 100; /* the RTC holds 2000-2099 */
        t->tm_year = year - 1900;
        break;
    case 3:
        t->tm_hour = (t->tm_hour + delta + 24) % 24;
        break;
    default:
        t->tm_min = (t->tm_min + delta + 60) % 60;
        break;
    }
    int days = days_in_month(t->tm_year + 1900, t->tm_mon);
    if (t->tm_mday > days) {
        t->tm_mday = days;
    }
}

static void start_datetime(ui_menu_t *m, const struct tm *local)
{
    m->dt = (struct tm){ .tm_year = local->tm_year, .tm_mon = local->tm_mon, .tm_mday = local->tm_mday,
                         .tm_hour = local->tm_hour, .tm_min = local->tm_min, .tm_isdst = -1 };
    if (m->dt.tm_year < 100 || m->dt.tm_year > 199) {
        m->dt.tm_year = 126; /* an unset clock: start from 2026 */
    }
    if (m->dt.tm_mon < 0 || m->dt.tm_mon > 11) {
        m->dt.tm_mon = 0;
    }
    int days = days_in_month(m->dt.tm_year + 1900, m->dt.tm_mon);
    if (m->dt.tm_mday < 1 || m->dt.tm_mday > days) {
        m->dt.tm_mday = 1;
    }
    m->dt_field = 0;
    m->mode = UI_MENU_DATETIME;
}

static ui_menu_intent_t select_item(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t item)
{
    const node_t *n = &k_nodes[item];
    switch (n->kind) {
    case K_SECTION:
        m->section = (uint8_t)item;
        m->cursor = 0;
        return k_none;
    case K_TOGGLE:
        return (ui_menu_intent_t){ .kind = UI_MENU_SET, .item = item, .value = !model->value[item] };
    case K_CHOICE:
        if (model->choice_count[item] == 0) {
            return k_none;
        }
        m->edit = model->value[item];
        m->mode = UI_MENU_EDIT;
        return k_none;
    case K_NUMBER:
        m->edit = model->value[item];
        m->mode = UI_MENU_EDIT;
        return k_none;
    case K_DATETIME:
        start_datetime(m, &model->local);
        return k_none;
    case K_ACTION:
        if (n->confirm) {
            m->mode = UI_MENU_CONFIRM;
            return k_none;
        }
        return (ui_menu_intent_t){ .kind = UI_MENU_ACTION, .item = item };
    default:
        return k_none; /* info: nothing to do */
    }
}

static void go_up(ui_menu_t *m, const ui_menu_model_t *model)
{
    uint8_t from = m->section;
    m->section = k_nodes[from].parent;
    ui_menu_item_t items[UI_MI_COUNT];
    int n = ui_menu_visible(m, model, items, UI_MI_COUNT);
    m->cursor = 0;
    for (int i = 0; i < n; i++) {
        if (items[i] == from) {
            m->cursor = (uint8_t)i;
        }
    }
}

static void step_edit(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t item, int delta)
{
    const node_t *n = &k_nodes[item];
    if (n->kind == K_CHOICE) {
        int count = model->choice_count[item];
        m->edit = count ? (m->edit + delta + count) % count : 0;
        return;
    }
    int32_t v = m->edit + delta * n->step;
    m->edit = v < n->min ? n->min : v > n->max ? n->max : v;
}

ui_menu_intent_t ui_menu_input(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_key_t key)
{
    ui_menu_item_t item = ui_menu_current(m, model);
    switch (m->mode) {
    case UI_MENU_EDIT:
        if (key == UI_MENU_KEY_NEXT || key == UI_MENU_KEY_BACK) {
            step_edit(m, model, item, key == UI_MENU_KEY_NEXT ? 1 : -1);
            return k_none;
        }
        m->mode = UI_MENU_BROWSE;
        if (key == UI_MENU_KEY_SELECT) {
            return (ui_menu_intent_t){ .kind = UI_MENU_SET, .item = item, .value = m->edit };
        }
        return k_none; /* cancelled */
    case UI_MENU_DATETIME:
        if (key == UI_MENU_KEY_NEXT || key == UI_MENU_KEY_BACK) {
            step_datetime(m, key == UI_MENU_KEY_NEXT ? 1 : -1);
            return k_none;
        }
        if (key == UI_MENU_KEY_SELECT && m->dt_field < 4) {
            m->dt_field++;
            return k_none;
        }
        m->mode = UI_MENU_BROWSE;
        if (key == UI_MENU_KEY_SELECT) {
            return (ui_menu_intent_t){ .kind = UI_MENU_SET_TIME, .item = item, .local = m->dt };
        }
        return k_none;
    case UI_MENU_CONFIRM:
        if (key == UI_MENU_KEY_NEXT) {
            return k_none; /* only a long press confirms */
        }
        m->mode = UI_MENU_BROWSE;
        if (key == UI_MENU_KEY_SELECT) {
            return (ui_menu_intent_t){ .kind = UI_MENU_ACTION, .item = item };
        }
        return k_none;
    default:
        break;
    }
    switch (key) {
    case UI_MENU_KEY_NEXT: {
        ui_menu_item_t items[UI_MI_COUNT];
        int n = ui_menu_visible(m, model, items, UI_MI_COUNT);
        m->cursor = n ? (uint8_t)((m->cursor + 1) % n) : 0;
        return k_none;
    }
    case UI_MENU_KEY_SELECT:
        return item == UI_MI_ROOT ? k_none : select_item(m, model, item);
    case UI_MENU_KEY_BACK:
        if (m->section == UI_MI_ROOT) {
            return (ui_menu_intent_t){ .kind = UI_MENU_CLOSE };
        }
        go_up(m, model);
        return k_none;
    default:
        return (ui_menu_intent_t){ .kind = UI_MENU_CLOSE };
    }
}
```

```cmake
# Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                            "ui_status.c" "ui_dashboard.c" "ui_schedule.c" "ui_menu.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx locale datastore util
                       PRIV_REQUIRES json scheduler)
```

- [ ] **Step 4: Run the tests and build the firmware.**

Run: `cmake --build build-host && ./build-host/test_ui_menu && ctest --test-dir build-host --output-on-failure && tools/idf.sh build`
Expected: `9 Tests 0 Failures`; the suite passes 32 of 32; the firmware builds without warnings.

- [ ] **Step 5: Commit.**

```bash
git add components/ui test/host/test_ui_menu.c test/host/CMakeLists.txt
git commit -m "feat(ui): add the menu state machine"
```

---


### Task 9: The menu, toast and critical screens (`ui` rendering)

**Files:**
- Create:
  - `components/ui/ui_menu_draw.c`, `components/ui/include/ui_screens.h`, `components/ui/ui_screens.c`
  - `test/host/screen_fixtures.h`, `test/host/test_ui_screens_golden.c`, `test/host/render_screen.c`
  - `test/host/golden/screen_*.pbm` (12), `test/host/golden/dash_{home_cs,indoor_cs,home_holiday_cs,home_low_battery}.pbm`
- Modify:
  - `components/ui/ui_internal.h`, `components/ui/ui_status.c`, `components/ui/ui_widget.c`, `components/ui/CMakeLists.txt`
  - `test/host/dashboard_fixtures.h`, `test/host/render_dashboard.c`, `test/host/CMakeLists.txt`, `test/host/golden/dash_home_stale.pbm`
  - `tools/render.py`

**Interfaces:**
- Consumes: the Task 2 strings and the `cs` pack; `timekeeping_zones()` (Task 3, in the fixtures only); the Task 8 menu.
- Produces:
  - `void ui_draw_menu(gfx_fb_t *, const ui_menu_t *, const ui_menu_model_t *, const lang_t *)`:
    - a 30 px title bar;
    - up to seven 34 px rows, the cursor row inverted;
    - the value being edited inverted in a box;
    - the button hints at the bottom;
    - the date-time editor in the 48 px digits;
    - the confirmation question on up to two lines.
  - `ui_screens.h`:
    - `void ui_draw_toast(gfx_fb_t *, const char *text)`: a black box with a white rim near the bottom, over whatever is on screen.
    - `void ui_draw_critical(gfx_fb_t *, const ui_context_t *)`: a large empty battery, "Battery empty" and "Please charge me", and the time and date the screen was drawn, since nothing updates after it (spec §5.5, §8).
  - Status bar and widgets (spec §5.2, §8, and the M3a review):
    - a stale battery reading counts as stale data;
    - a battery at 15 % or below gets a "!" before its icon;
    - right-hand text wider than its 120 px ends in an ellipsis, rather than losing its first glyphs;
    - a stale value shows no trend arrow (an M3a carry-over note);
    - a name too wide for a narrow S cell, such as a Czech holiday, goes on two lines in the regular face.
  - `ui_split_two_lines()` in `ui_internal.h`.
  - The renderers print their fixtures with `--list`, and `tools/render.py` asks them, so the names live only in the fixture headers (M3a review).
- The goldens were reviewed by the owner and approved on 2026-09-29. Two exceptions:
  - `dash_home_stale` loses the "↑" beside a 3-hour-old temperature, the one intended change to an M3a golden. It goes to the owner with the M3b report.
  - `dash_home_low_battery` is new since that review.

- [ ] **Step 1: Write the failing test and the fixtures.** The screen fixtures sit on top of the dashboard fixtures:
  - the menu at the root in both languages;
  - Presets and System in Czech;
  - the time zone and the temperature offset being edited;
  - Info in English;
  - the date-time editor on its month;
  - the factory-reset question;
  - a toast over the Czech Home;
  - the critical screen in both languages.

  `dashboard_fixtures.h` gains four dashboards (apply the patch):
  - Home and Indoor in Czech;
  - Home on 28 September 2026, with the holiday in an S cell;
  - Home at 12 %.

```c
#pragma once

#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "timekeeping_zones.h"
#include "ui_menu.h"
#include "ui_screens.h"

/* Fixed inputs for the menu and the special screens (test_ui_screens_golden.c, render_screen.c),
 * on top of the dashboard fixtures. */

static const char *const k_fix_presets[] = { "Home", "Indoor", "Weather", "Focus clock" };
static const char *const k_fix_intervals[] = { "10 s", "15 s", "30 s", "1 min", "2 min",
                                               "5 min", "10 min", "15 min", "30 min", "1 h" };
static const char *const k_fix_units[] = { "°C", "°F" };
static const char *const k_fix_languages[] = { "English", "Čeština" };
static const char *s_fix_zones[16];
static char s_fix_rate_text[6][12];
static const char *s_fix_rates[6];

/* The model the app would build: Indoor active, Prague, 1 Hz, a -1.5 °C offset. */
static inline void fixture_menu_model(ui_menu_model_t *m, const lang_t *lang)
{
    memset(m, 0, sizeof(*m));
    int zones = 0;
    const timekeeping_zone_t *z = timekeeping_zones(&zones);
    for (int i = 0; i < zones && i < 16; i++) {
        s_fix_zones[i] = z[i].iana;
    }
    static const int k_quarter_hz[] = { 1, 2, 4, 8, 16, 32 };
    for (int i = 0; i < 6; i++) {
        char num[8];
        lang_format_decimal(lang, k_quarter_hz[i] * 25, 2, num, sizeof(num));
        size_t n = strlen(num);
        while (n > 1 && num[n - 1] == '0') { /* 0.25, 0.5, 1 */
            num[--n] = '\0';
        }
        if (num[n - 1] == lang->decimal_sep) {
            num[n - 1] = '\0';
        }
        snprintf(s_fix_rate_text[i], sizeof(s_fix_rate_text[i]), "%s Hz", num);
        s_fix_rates[i] = s_fix_rate_text[i];
    }
    m->choices[UI_MI_ACTIVE_PRESET] = k_fix_presets;
    m->choice_count[UI_MI_ACTIVE_PRESET] = 4;
    m->value[UI_MI_ACTIVE_PRESET] = 1;
    m->choices[UI_MI_CYCLE_INTERVAL] = k_fix_intervals;
    m->choice_count[UI_MI_CYCLE_INTERVAL] = 10;
    m->value[UI_MI_CYCLE_INTERVAL] = 3;
    m->value[UI_MI_CLOCK_24H] = 1;
    m->choices[UI_MI_TIME_ZONE] = s_fix_zones;
    m->choice_count[UI_MI_TIME_ZONE] = (uint8_t)zones;
    m->value[UI_MI_TIME_ZONE] = 2;
    m->value[UI_MI_UPDATE_INTERVAL] = 1;
    m->choices[UI_MI_REFRESH_RATE] = s_fix_rates;
    m->choice_count[UI_MI_REFRESH_RATE] = 6;
    m->value[UI_MI_REFRESH_RATE] = 2;
    m->value[UI_MI_TEMP_OFFSET] = -15;
    m->choices[UI_MI_UNITS] = k_fix_units;
    m->choice_count[UI_MI_UNITS] = 2;
    m->choices[UI_MI_LANGUAGE] = k_fix_languages;
    m->choice_count[UI_MI_LANGUAGE] = 2;
    m->value[UI_MI_LANGUAGE] = strcmp(lang->code, "cs") == 0;
    m->info[UI_MI_INFO_BATTERY] = "82 % · 4.04 V";
    m->info[UI_MI_INFO_FIRMWARE] = "0.3.0 (0b21fcd)";
    m->info[UI_MI_INFO_DEVICE] = "reflbo-bb94";
    m->info[UI_MI_INFO_UPTIME] = "2 d 3 h";
    m->info[UI_MI_INFO_MEMORY] = "7.9 MB";
    m->local = fixture_local(20, 48, 0);
}

/* Moves the menu's cursor to `item` in the current section. */
static inline void fixture_menu_to(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t item)
{
    for (int i = 0; i < UI_MI_COUNT && ui_menu_current(m, model) != item; i++) {
        ui_menu_input(m, model, UI_MENU_KEY_NEXT);
    }
}

static inline void fixture_menu_open(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t section,
                                     ui_menu_item_t item)
{
    ui_menu_open(m);
    if (section != UI_MI_ROOT) {
        fixture_menu_to(m, model, section);
        ui_menu_input(m, model, UI_MENU_KEY_SELECT);
    }
    fixture_menu_to(m, model, item);
}

/* Draws screen fixture `name` into fb; false for an unknown name. */
static inline bool fixture_screen(const char *name, gfx_fb_t *fb)
{
    static ui_menu_model_t model;
    static ui_menu_t menu;
    const lang_t *lang = lang_get(strstr(name, "_cs") != NULL ? "cs" : "en");
    fixture_menu_model(&model, lang);
    if (strncmp(name, "menu_root", 9) == 0) {
        fixture_menu_open(&menu, &model, UI_MI_ROOT, UI_MI_PRESETS);
    } else if (strcmp(name, "menu_presets_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_PRESETS, UI_MI_ACTIVE_PRESET);
    } else if (strcmp(name, "menu_edit_zone_en") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_TIME, UI_MI_TIME_ZONE);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
        ui_menu_input(&menu, &model, UI_MENU_KEY_BACK); /* Prague -> London */
    } else if (strcmp(name, "menu_edit_offset_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_SENSORS, UI_MI_TEMP_OFFSET);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
    } else if (strcmp(name, "menu_info_en") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_INFO, UI_MI_INFO_BATTERY);
    } else if (strcmp(name, "menu_system_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_SYSTEM, UI_MI_LANGUAGE);
    } else if (strcmp(name, "menu_datetime_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_TIME, UI_MI_SET_DATETIME);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT); /* on to the month */
    } else if (strcmp(name, "menu_confirm_cs") == 0) {
        fixture_menu_open(&menu, &model, UI_MI_SYSTEM, UI_MI_FACTORY_RESET);
        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
    } else if (strcmp(name, "toast_preset_cs") == 0) {
        ui_context_t ctx;
        ui_preset_t preset;
        fixture_dashboard("home_cs", &ctx, &preset);
        ui_draw_dashboard(fb, &ctx, &preset);
        char text[64];
        snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_PRESET), "Indoor");
        ui_draw_toast(fb, text);
        return true;
    } else if (strncmp(name, "critical", 8) == 0) {
        ui_context_t ctx = fixture_context();
        ctx.lang = lang;
        ui_draw_critical(fb, &ctx);
        return true;
    } else {
        return false;
    }
    ui_draw_menu(fb, &menu, &model, lang);
    return true;
}

static const char *const k_screen_fixtures[] = { "menu_root_en", "menu_root_cs", "menu_presets_cs",
                                                 "menu_edit_zone_en", "menu_edit_offset_cs", "menu_info_en",
                                                 "menu_system_cs", "menu_datetime_cs", "menu_confirm_cs",
                                                 "toast_preset_cs", "critical_en", "critical_cs" };
```

```c
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "screen_fixtures.h"
#include "unity.h"

/* Each screen fixture must match test/host/golden/screen_<name>.pbm byte for byte. After an
 * intentional change: build-host/render_screen <name> test/host/golden/screen_<name>.pbm for
 * each fixture, look at the PNGs (python3 tools/render.py), commit. */

static uint8_t s_buf[400 * 300 / 8];
static uint8_t s_pbm[16000];
static uint8_t s_golden[16000];

void setUp(void) {}
void tearDown(void) {}

static void check(const char *name)
{
    gfx_fb_t fb;
    gfx_fb_init(&fb, s_buf, 400, 300);
    TEST_ASSERT_TRUE_MESSAGE(fixture_screen(name, &fb), name);
    size_t n = gfx_pbm_encode(&fb, s_pbm, sizeof(s_pbm));
    char path[256];
    snprintf(path, sizeof(path), "%s/screen_%s.pbm", GOLDEN_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    size_t golden = fread(s_golden, 1, sizeof(s_golden), f);
    fclose(f);
    if (golden != n || memcmp(s_golden, s_pbm, n) != 0) {
        char actual[64];
        snprintf(actual, sizeof(actual), "screen_%s.actual.pbm", name);
        FILE *out = fopen(actual, "wb");
        if (out != NULL) {
            fwrite(s_pbm, 1, n, out);
            fclose(out);
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)n, (int)golden, path);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_golden, s_pbm, n, path);
}

static void test_every_screen_matches_its_golden(void)
{
    for (size_t i = 0; i < sizeof(k_screen_fixtures) / sizeof(k_screen_fixtures[0]); i++) {
        check(k_screen_fixtures[i]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_screen_matches_its_golden);
    return UNITY_END();
}
```

```c
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "screen_fixtures.h"

/* build-host/render_screen <fixture> <out.pbm>: one menu or special-screen fixture as PBM
 * (tools/render.py). `--list` prints the fixture names. */
int main(int argc, char **argv)
{
    static uint8_t buf[400 * 300 / 8];
    static uint8_t pbm[16000];
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (size_t i = 0; i < sizeof(k_screen_fixtures) / sizeof(k_screen_fixtures[0]); i++) {
            printf("%s\n", k_screen_fixtures[i]);
        }
        return 0;
    }
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    if (argc != 3 || !fixture_screen(argv[1], &fb)) {
        fprintf(stderr, "usage: render_screen <fixture> <out.pbm> | --list\n");
        return 2;
    }
    size_t n = gfx_pbm_encode(&fb, pbm, sizeof(pbm));
    FILE *f = fopen(argv[2], "wb");
    if (f == NULL || fwrite(pbm, 1, n, f) != n) {
        return 1;
    }
    fclose(f);
    return 0;
}
```

```diff
diff --git a/test/host/dashboard_fixtures.h b/test/host/dashboard_fixtures.h
index fc01c17..815f75b 100644
--- a/test/host/dashboard_fixtures.h
+++ b/test/host/dashboard_fixtures.h
@@ -70,6 +70,25 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
     } else if (strcmp(name, "home_frost") == 0) {
         *preset = fixture_preset("home");
         fixture_single(&s_fix_ds, -1250, 6000);
+    } else if (strcmp(name, "home_cs") == 0) { /* the Czech pack */
+        *preset = fixture_preset("home");
+        ctx->lang = lang_get("cs");
+    } else if (strcmp(name, "indoor_cs") == 0) {
+        *preset = fixture_preset("indoor");
+        ctx->lang = lang_get("cs");
+    } else if (strcmp(name, "home_holiday_cs") == 0) { /* Monday 28 September 2026: Den české státnosti */
+        *preset = fixture_preset("home");
+        preset->slots[4] = UI_FIELD_DATE_HOLIDAY;
+        ctx->lang = lang_get("cs");
+        ctx->now = FIX_NOW + 3 * 86400;
+        ctx->local.tm_mday = 28;
+        ctx->local.tm_wday = 1;
+        ctx->local.tm_yday = 270;
+        ctx->local_day = FIX_DAY + 3;
+        fixture_fill(&s_fix_ds, ctx->now);
+    } else if (strcmp(name, "home_low_battery") == 0) { /* 12 %: the status bar marks it */
+        *preset = fixture_preset("home");
+        ds_set_battery(&s_fix_ds, 12, 3650, DS_BAT_DISCHARGING, FIX_NOW);
     } else if (strcmp(name, "grid_clock_12h") == 0) { /* a clock in a grid cell, 12-hour */
         *preset = fixture_preset("indoor");
         preset->slots[0] = UI_FIELD_TIME_CLOCK;
@@ -84,4 +103,5 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
 static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather", "focus", "home_invalid",
                                                     "home_stale", "home_12h_charging", "indoor_cold",
                                                     "focus_seconds", "home_battery_details", "home_inverted",
-                                                    "indoor_hot_f", "indoor_frost", "home_frost", "grid_clock_12h" };
+                                                    "indoor_hot_f", "indoor_frost", "home_frost", "grid_clock_12h",
+                                                    "home_cs", "indoor_cs", "home_holiday_cs", "home_low_battery" };
```

```diff
diff --git a/test/host/render_dashboard.c b/test/host/render_dashboard.c
index 0616df4..686a0c5 100644
--- a/test/host/render_dashboard.c
+++ b/test/host/render_dashboard.c
@@ -4,15 +4,22 @@
 #include "dashboard_fixtures.h"
 #include "gfx.h"
 
-/* build-host/render_dashboard <fixture> <out.pbm>: one dashboard fixture as PBM (tools/render.py). */
+/* build-host/render_dashboard <fixture> <out.pbm>: one dashboard fixture as PBM (tools/render.py).
+ * `--list` prints the fixture names. */
 int main(int argc, char **argv)
 {
     static uint8_t buf[400 * 300 / 8];
     static uint8_t pbm[16000];
+    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
+        for (size_t i = 0; i < sizeof(k_dashboard_fixtures) / sizeof(k_dashboard_fixtures[0]); i++) {
+            printf("%s\n", k_dashboard_fixtures[i]);
+        }
+        return 0;
+    }
     ui_context_t ctx;
     ui_preset_t preset;
     if (argc != 3 || !fixture_dashboard(argv[1], &ctx, &preset)) {
-        fprintf(stderr, "usage: render_dashboard <fixture> <out.pbm>\n");
+        fprintf(stderr, "usage: render_dashboard <fixture> <out.pbm> | --list\n");
         return 2;
     }
     gfx_fb_t fb;
```

- [ ] **Step 2: Register them and watch them fail.** In `test/host/CMakeLists.txt`, after the `render_dashboard` executable:

```cmake
reflbo_host_test(test_ui_screens_golden ui timekeeping_logic)
target_compile_definitions(test_ui_screens_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(render_screen render_screen.c)
target_compile_options(render_screen PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(render_screen PRIVATE ui timekeeping_logic)
```

Run: `cmake --build build-host`
Expected: FAIL to compile: `ui_screens.h` not found.

- [ ] **Step 3: Implement.** Apply the patches; the three sources are new; the CMake file replaces the old one.

```diff
diff --git a/components/ui/ui_internal.h b/components/ui/ui_internal.h
index 97e6662..49efcaf 100644
--- a/components/ui/ui_internal.h
+++ b/components/ui/ui_internal.h
@@ -7,9 +7,15 @@
 
 /* Shared by the ui sources; not part of the component's API. */
 
+#define UI_BATTERY_LOW_PCT 15 /* spec §8: the status bar marks a low battery */
+
 void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, ui_stale_policy_t policy,
                     const lang_t *lang);
 void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, bool any_stale);
+/* Splits text that is wider than max_w into two lines at the last space that lets the first
+ * line fit; each line is then cut with an ellipsis if it is still too wide. line2 is empty when
+ * the text fits on one line. */
+void ui_split_two_lines(const gfx_font_t *font, const char *text, int max_w, char *line1, char *line2, size_t size);
 /* A battery outline w x h with a nub, filled to `pct` (no fill if pct < 0). */
 void ui_draw_battery(gfx_fb_t *fb, int x, int y, int w, int h, int pct);
 /* The Moon's disc in a thin outline, its shadow inked. */
```

```diff
diff --git a/components/ui/ui_status.c b/components/ui/ui_status.c
index 45fc559..faf5455 100644
--- a/components/ui/ui_status.c
+++ b/components/ui/ui_status.c
@@ -9,6 +9,10 @@
  * middle, the battery on the right with its level, voltage or days left as the preset asks. */
 void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, bool any_stale)
 {
+    ui_value_t bat, days;
+    ui_resolve(ctx, UI_FIELD_BAT_LEVEL, &bat);
+    ui_resolve(ctx, UI_FIELD_BAT_DAYS, &days);
+    any_stale |= bat.state == UI_VALUE_STALE; /* the battery shown here counts too */
     if (!ctx->time_valid) {
         const gfx_font_t *f = &gfx_font_sans_bold_16;
         const char *text = lang_str(ctx->lang, LS_SET_TIME);
@@ -28,14 +32,14 @@ void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *pr
                          clock, GFX_BLACK);
     }
 
-    ui_value_t bat, days;
-    ui_resolve(ctx, UI_FIELD_BAT_LEVEL, &bat);
-    ui_resolve(ctx, UI_FIELD_BAT_DAYS, &days);
     int x = fb->width - 6 - 26;
     ui_draw_battery(fb, x, 5, 26, 11, bat.state == UI_VALUE_MISSING ? -1 : bat.percent);
     if (bat.battery == DS_BAT_CHARGING) {
         x -= 16;
         gfx_bitmap(fb, x, 2, &gfx_icon_bolt_16, GFX_BLACK);
+    } else if (bat.state != UI_VALUE_MISSING && bat.percent <= UI_BATTERY_LOW_PCT) { /* spec §8: low */
+        x -= 10;
+        gfx_text(fb, &gfx_font_sans_bold_16, x + 2, 16, "!", GFX_BLACK);
     }
     char text[sizeof(bat.text) + sizeof(bat.extra) + sizeof(days.text) + sizeof(days.unit) + 12] = "";
     size_t n = 0;
@@ -53,7 +57,9 @@ void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *pr
             snprintf(text + n, sizeof(text) - n, "%s%s %s", n ? "  " : "", days.text, days.unit);
         }
     }
+    char fit[sizeof(text)];
+    gfx_text_ellipsize(&gfx_font_sans_12, text, 120, fit, sizeof(fit)); /* cut at the end, not the start */
     gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ (int16_t)(x - 124), 0, 120, UI_STATUS_H }, GFX_ALIGN_RIGHT,
-                     text, GFX_BLACK);
+                     fit, GFX_BLACK);
     gfx_hline(fb, 0, UI_STATUS_H, fb->width, GFX_BLACK);
 }
```

```diff
diff --git a/components/ui/ui_widget.c b/components/ui/ui_widget.c
index 50253eb..906f4cb 100644
--- a/components/ui/ui_widget.c
+++ b/components/ui/ui_widget.c
@@ -130,6 +130,25 @@ static const gfx_font_t *fit_number(const ui_fonts_t *f, const gfx_font_t *const
     return vf;
 }
 
+void ui_split_two_lines(const gfx_font_t *font, const char *text, int max_w, char *line1, char *line2, size_t size)
+{
+    line2[0] = '\0';
+    if (gfx_text_width(font, text) <= max_w) {
+        gfx_text_ellipsize(font, text, max_w, line1, size);
+        return;
+    }
+    char first[96];
+    snprintf(first, sizeof(first), "%s", text ? text : "");
+    for (char *space = strrchr(first, ' '); space != NULL; space = strrchr(first, ' ')) {
+        *space = '\0';
+        if (gfx_text_width(font, first) <= max_w) {
+            gfx_text_ellipsize(font, text + (space - first) + 1, max_w, line2, size);
+            break;
+        }
+    }
+    gfx_text_ellipsize(font, first, max_w, line1, size); /* no space that helps: one cut line */
+}
+
 static void format_age(const lang_t *lang, uint32_t age_s, char *out, size_t size)
 {
     if (age_s < 3600) {
@@ -141,7 +160,7 @@ static void format_age(const lang_t *lang, uint32_t age_s, char *out, size_t siz
     }
 }
 
-/* "⟲ 2 h" in the rect's top-right corner (spec §5.3). */
+/* "⟲ 2 h" in the rect's bottom-right corner (spec §5.3). */
 static void draw_age(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v, const lang_t *lang)
 {
     char age[16];
@@ -243,6 +262,17 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
             return;
         }
         int max_w = r.w - 8;
+        if (!numeric(v) && gfx_text_width(vf, value) > max_w) { /* a name: two lines in the regular face */
+            const gfx_font_t *tf = f->unit;
+            char second[sizeof(fit)];
+            ui_split_two_lines(tf, value, max_w, fit, second, sizeof(fit));
+            int top = r.y + 12 + f->icon + 10;
+            gfx_text_in_rect(fb, tf, (gfx_rect_t){ r.x, (int16_t)top, r.w, tf->line_height }, GFX_ALIGN_CENTER, fit,
+                             GFX_BLACK);
+            gfx_text_in_rect(fb, tf, (gfx_rect_t){ r.x, (int16_t)(top + tf->line_height), r.w, tf->line_height },
+                             GFX_ALIGN_CENTER, second, GFX_BLACK);
+            return;
+        }
         if (!numeric(v)) {
             gfx_text_ellipsize(vf, value, max_w, fit, sizeof(fit));
             value = fit;
@@ -356,6 +386,9 @@ void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t
     if (shown.state == UI_VALUE_MISSING && policy == UI_STALE_HIDE) {
         return;
     }
+    if (shown.state == UI_VALUE_STALE) {
+        shown.trend = 0; /* an old reading's trend says nothing about now */
+    }
     gfx_rect_t saved = fb->clip;
     gfx_set_clip(fb, gfx_rect_intersect(saved, r));
     if (size == UI_SIZE_S) {
```

```c
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "ui_menu.h"

/* The menu screen (spec §5.7): a title bar, up to seven rows, and the button hints. */

#define HEADER_H 30
#define ROW_Y0 36
#define ROW_H 34
#define ROWS 7
#define FOOTER_Y 282

static void draw_header(gfx_fb_t *fb, const char *title)
{
    gfx_fill_rect(fb, (gfx_rect_t){ 0, 0, fb->width, HEADER_H }, GFX_BLACK);
    char fit[64];
    gfx_text_ellipsize(&gfx_font_sans_bold_20, title, fb->width - 24, fit, sizeof(fit));
    gfx_text_in_rect(fb, &gfx_font_sans_bold_20, (gfx_rect_t){ 12, 0, (int16_t)(fb->width - 24), HEADER_H },
                     GFX_ALIGN_LEFT, fit, GFX_WHITE);
}

static void draw_hints(gfx_fb_t *fb, const char *hints)
{
    gfx_hline(fb, 0, FOOTER_Y - 4, fb->width, GFX_BLACK);
    char fit[96];
    gfx_text_ellipsize(&gfx_font_sans_12, hints, fb->width - 12, fit, sizeof(fit));
    gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ 0, FOOTER_Y, fb->width, (int16_t)(fb->height - FOOTER_Y) },
                     GFX_ALIGN_CENTER, fit, GFX_BLACK);
}

static void draw_list(gfx_fb_t *fb, const ui_menu_t *m, const ui_menu_model_t *model, const lang_t *lang)
{
    ui_menu_item_t items[UI_MI_COUNT];
    int n = ui_menu_visible(m, model, items, UI_MI_COUNT);
    int first = m->cursor >= ROWS ? m->cursor - ROWS + 1 : 0;
    const gfx_font_t *f = &gfx_font_sans_20;
    for (int row = 0; row < ROWS && first + row < n; row++) {
        int i = first + row;
        ui_menu_item_t item = items[i];
        gfx_rect_t r = { 6, (int16_t)(ROW_Y0 + row * ROW_H), (int16_t)(fb->width - 12), ROW_H - 2 };
        bool cursor = i == m->cursor;
        bool editing = cursor && m->mode == UI_MENU_EDIT;
        gfx_color_t ink = cursor && !editing ? GFX_WHITE : GFX_BLACK;
        if (cursor && !editing) {
            gfx_fill_rect(fb, r, GFX_BLACK);
        }
        char value[48];
        if (item == UI_MI_PRESETS || item == UI_MI_TIME || item == UI_MI_DISPLAY || item == UI_MI_SENSORS ||
            item == UI_MI_INFO || item == UI_MI_SYSTEM) {
            snprintf(value, sizeof(value), "\xE2\x86\x92"); /* a section: → */
        } else {
            ui_menu_value_text(item, editing ? m->edit : model->value[item], model, lang, value, sizeof(value));
        }
        int value_w = value[0] ? gfx_text_width(f, value) : 0;
        char label[48];
        gfx_text_ellipsize(f, ui_menu_label(item, lang), r.w - 20 - value_w - 16, label, sizeof(label));
        gfx_text_in_rect(fb, f, (gfx_rect_t){ (int16_t)(r.x + 8), r.y, (int16_t)(r.w - 16), r.h }, GFX_ALIGN_LEFT,
                         label, ink);
        if (value[0] == '\0') {
            continue;
        }
        gfx_rect_t vr = { (int16_t)(r.x + r.w - 8 - value_w), r.y, (int16_t)value_w, r.h };
        if (editing) { /* the value being edited, inverted in a box */
            gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(vr.x - 8), (int16_t)(r.y + 2), (int16_t)(value_w + 16),
                                            (int16_t)(r.h - 4) },
                          GFX_BLACK);
            gfx_text_in_rect(fb, f, vr, GFX_ALIGN_LEFT, value, GFX_WHITE);
        } else {
            gfx_text_in_rect(fb, f, vr, GFX_ALIGN_LEFT, value, ink);
        }
    }
    draw_hints(fb, lang_str(lang, m->mode == UI_MENU_EDIT ? LS_HINT_EDIT : LS_HINT_BROWSE));
}

/* One field of the date-time editor, inverted when it is the one being edited. */
static int draw_field(gfx_fb_t *fb, const gfx_font_t *f, int x, int baseline, const char *text, bool active)
{
    int w = gfx_text_width(f, text);
    if (active) {
        gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x - 4), (int16_t)(baseline - f->ascent - 2), (int16_t)(w + 8),
                                        (int16_t)(f->line_height + 2) },
                      GFX_BLACK);
    }
    gfx_text(fb, f, x, baseline, text, active ? GFX_WHITE : GFX_BLACK);
    return w;
}

static void draw_datetime(gfx_fb_t *fb, const ui_menu_t *m, const lang_t *lang)
{
    const gfx_font_t *f = &gfx_font_num_cb_48;
    char day[12], month[12], year[12], hour[12], minute[12]; /* room for any int, as GCC checks */
    snprintf(day, sizeof(day), "%02d", m->dt.tm_mday);
    snprintf(month, sizeof(month), "%02d", m->dt.tm_mon + 1);
    snprintf(year, sizeof(year), "%04d", m->dt.tm_year + 1900);
    snprintf(hour, sizeof(hour), "%02d", m->dt.tm_hour);
    snprintf(minute, sizeof(minute), "%02d", m->dt.tm_min);
    int gap = 12, dot = gfx_text_width(f, ".");
    int date_w = gfx_text_width(f, day) + gfx_text_width(f, month) + gfx_text_width(f, year) + 2 * dot + 4 * gap;
    int x = (fb->width - date_w) / 2, baseline = 128;
    x += draw_field(fb, f, x, baseline, day, m->dt_field == 0) + gap;
    x = gfx_text(fb, f, x, baseline, ".", GFX_BLACK) + gap;
    x += draw_field(fb, f, x, baseline, month, m->dt_field == 1) + gap;
    x = gfx_text(fb, f, x, baseline, ".", GFX_BLACK) + gap;
    draw_field(fb, f, x, baseline, year, m->dt_field == 2);
    int colon = gfx_text_width(f, ":");
    int time_w = gfx_text_width(f, hour) + gfx_text_width(f, minute) + colon + 2 * gap;
    x = (fb->width - time_w) / 2;
    baseline = 220;
    x += draw_field(fb, f, x, baseline, hour, m->dt_field == 3) + gap;
    x = gfx_text(fb, f, x, baseline, ":", GFX_BLACK) + gap;
    draw_field(fb, f, x, baseline, minute, m->dt_field == 4);
    draw_hints(fb, lang_str(lang, LS_HINT_DATETIME));
}

/* The question on up to two centred lines, split at a space. */
static void draw_question(gfx_fb_t *fb, const char *text, int y)
{
    const gfx_font_t *f = &gfx_font_sans_bold_20;
    int max_w = fb->width - 24;
    char line1[96], line2[96];
    snprintf(line1, sizeof(line1), "%s", text);
    line2[0] = '\0';
    if (gfx_text_width(f, line1) > max_w) {
        for (char *space = strrchr(line1, ' '); space != NULL; space = strrchr(line1, ' ')) {
            *space = '\0';
            if (gfx_text_width(f, line1) <= max_w) {
                snprintf(line2, sizeof(line2), "%s", text + (space - line1) + 1);
                break;
            }
        }
    }
    char fit[96];
    gfx_text_ellipsize(f, line1, max_w, fit, sizeof(fit));
    gfx_text_in_rect(fb, f, (gfx_rect_t){ 0, (int16_t)y, fb->width, 28 }, GFX_ALIGN_CENTER, fit, GFX_BLACK);
    if (line2[0]) {
        gfx_text_ellipsize(f, line2, max_w, fit, sizeof(fit));
        gfx_text_in_rect(fb, f, (gfx_rect_t){ 0, (int16_t)(y + 30), fb->width, 28 }, GFX_ALIGN_CENTER, fit,
                         GFX_BLACK);
    }
}

void ui_draw_menu(gfx_fb_t *fb, const ui_menu_t *m, const ui_menu_model_t *model, const lang_t *lang)
{
    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    ui_menu_item_t item = ui_menu_current(m, model);
    switch (m->mode) {
    case UI_MENU_DATETIME:
        draw_header(fb, ui_menu_label(item, lang));
        draw_datetime(fb, m, lang);
        break;
    case UI_MENU_CONFIRM:
        draw_header(fb, ui_menu_label(item, lang));
        draw_question(fb, lang_str(lang, LS_CONFIRM_FACTORY_RESET), 110);
        draw_hints(fb, lang_str(lang, LS_HINT_CONFIRM));
        break;
    default:
        draw_header(fb, ui_menu_label((ui_menu_item_t)m->section, lang));
        draw_list(fb, m, model, lang);
        break;
    }
}
```

```c
#pragma once

#include "gfx.h"
#include "ui_fields.h"

/* Special screens (spec §5.5). Pure C, host-buildable. */

/* A short message over whatever is on screen, for about 3 s: "Preset: Indoor". */
void ui_draw_toast(gfx_fb_t *fb, const char *text);
/* The last screen before the battery gives out (spec §8): nothing else updates after it. */
void ui_draw_critical(gfx_fb_t *fb, const ui_context_t *ctx);
```

```c
#include "ui_screens.h"

#include <stdio.h>

#include "gfx_fonts.h"
#include "ui_internal.h"

void ui_draw_toast(gfx_fb_t *fb, const char *text)
{
    const gfx_font_t *f = &gfx_font_sans_bold_20;
    char fit[96];
    int w = gfx_text_ellipsize(f, text, fb->width - 60, fit, sizeof(fit)) + 40;
    gfx_rect_t box = { (int16_t)((fb->width - w) / 2), (int16_t)(fb->height - 62), (int16_t)w, 44 };
    gfx_reset_clip(fb);
    /* a white rim keeps the box visible over black content, such as an inverted preset */
    gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(box.x - 3), (int16_t)(box.y - 3), (int16_t)(box.w + 6),
                                    (int16_t)(box.h + 6) },
                  GFX_WHITE);
    gfx_fill_rect(fb, box, GFX_BLACK);
    gfx_text_in_rect(fb, f, box, GFX_ALIGN_CENTER, fit, GFX_WHITE);
}

void ui_draw_critical(gfx_fb_t *fb, const ui_context_t *ctx)
{
    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    ui_draw_battery(fb, 130, 44, 140, 64, 0);
    gfx_text_in_rect(fb, &gfx_font_sans_bold_28, (gfx_rect_t){ 0, 132, fb->width, 36 }, GFX_ALIGN_CENTER,
                     lang_str(ctx->lang, LS_BATTERY_EMPTY), GFX_BLACK);
    gfx_text_in_rect(fb, &gfx_font_sans_20, (gfx_rect_t){ 0, 172, fb->width, 28 }, GFX_ALIGN_CENTER,
                     lang_str(ctx->lang, LS_CHARGE_ME), GFX_BLACK);
    if (ctx->time_valid) { /* when the screen was drawn: it stays up while the battery recovers */
        ui_value_t t, d;
        ui_resolve(ctx, UI_FIELD_TIME_CLOCK, &t);
        ui_resolve(ctx, UI_FIELD_DATE_DAY, &d);
        char line[sizeof(t.text) + sizeof(t.unit) + sizeof(d.extra) + 8];
        snprintf(line, sizeof(line), "%s%s%s \xC2\xB7 %s", t.text, t.unit[0] ? " " : "", t.unit, d.extra);
        gfx_text_in_rect(fb, &gfx_font_sans_16, (gfx_rect_t){ 0, 244, fb->width, 24 }, GFX_ALIGN_CENTER, line,
                         GFX_BLACK);
    }
}
```

```cmake
# Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                            "ui_status.c" "ui_dashboard.c" "ui_schedule.c" "ui_menu.c" "ui_menu_draw.c"
                            "ui_screens.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx locale datastore util
                       PRIV_REQUIRES json scheduler)
```

- [ ] **Step 4: Watch the golden tests fail for the right reason.**

Run: `cmake --build build-host && ./build-host/test_ui_dashboard_golden; ./build-host/test_ui_screens_golden`
Expected:
- The dashboard test fails at `dash_home_stale.pbm` with a memory mismatch: the trend arrow is gone.
- The screen test fails because `screen_menu_root_en.pbm` doesn't exist.

- [ ] **Step 5: Generate the goldens and check them against the spike's.** The renderer is deterministic, so the files must match byte for byte:

```bash
for n in home_stale home_cs indoor_cs home_holiday_cs home_low_battery; do
  ./build-host/render_dashboard "$n" "test/host/golden/dash_$n.pbm"
done
for n in $(./build-host/render_screen --list); do
  ./build-host/render_screen "$n" "test/host/golden/screen_$n.pbm"
done
shasum -a 256 test/host/golden/dash_home_stale.pbm test/host/golden/dash_*_cs.pbm \
  test/host/golden/dash_home_low_battery.pbm test/host/golden/screen_*.pbm
```

Expected:

```text
6c60b788a8733666ebca7ad9a4907ee45ed0f61fe694f5f11573d85122ec5164  test/host/golden/dash_home_stale.pbm
0bd976fa8125007a2469c586cb0a2a1fb3fca45acca1ed117dcb83831e67df6f  test/host/golden/dash_home_cs.pbm
e7183612389b4abfd4ff462186ccc1431a3e910170f5f0361c93245f85e7a0e7  test/host/golden/dash_home_holiday_cs.pbm
ef1a0007119077a555fbdfc6d804ff7726b8f391182ec1507ea6add040087798  test/host/golden/dash_indoor_cs.pbm
0dcc7fc0f551be4d7aaf3d087ac4921d4cdcad1d79c14e5b07f09dba67cdfad2  test/host/golden/dash_home_low_battery.pbm
1f117027524914c0c802c9d1191037a90bf32456792d76d5b423ae3255c07922  test/host/golden/screen_critical_cs.pbm
53d90c829adf46911220552cd7d1101ce0300b77977e2d89d2e43c05508c67f5  test/host/golden/screen_critical_en.pbm
c8a31325b7675909b3b387beea2fce311727d8c83480960896de8cb3bc855f6b  test/host/golden/screen_menu_confirm_cs.pbm
891a2280b1e68c0f225b7fb71ff59f9b29e6169ce53094e3bd27ef1d9633d564  test/host/golden/screen_menu_datetime_cs.pbm
0df5d1c2c6d596d7bd666d1878558ded1ef34e08dd84aea5c9bbb05fd0088286  test/host/golden/screen_menu_edit_offset_cs.pbm
545f2f16a036f53542ecbcdc51a42efa0fe5cdebd8b416bf7631b3528186e489  test/host/golden/screen_menu_edit_zone_en.pbm
6f1ea98c8ee647168982be9f04c5d09de70c9f0cb847f48d05fb54885fa79856  test/host/golden/screen_menu_info_en.pbm
16799d0960a62e9b0edbebe7b6b5bdbcddb87fd9115ba7893748634d89130b40  test/host/golden/screen_menu_presets_cs.pbm
0c1a452624059b6f21e6789ac3ee6d19dd86c79d88d0055d83a82074c7d253ec  test/host/golden/screen_menu_root_cs.pbm
7a3f1f8634c73b306a16d45221b8e9491f78dea097296d9e801e4161ee2af32c  test/host/golden/screen_menu_root_en.pbm
0673d0973b7b1f326d56215a1d5439eedfb5cf36bf407ef74efe8567cec5ae73  test/host/golden/screen_menu_system_cs.pbm
d063e596d7e4a244a33d922b76351d4d8ed0838d5d779099bdc3ae5cc503d3b8  test/host/golden/screen_toast_preset_cs.pbm
```

If a hash differs, the code differs from this plan: find the difference before going on.

- [ ] **Step 6: Update `tools/render.py`, then look at the renders.** Apply the patch, then run it:

```diff
diff --git a/tools/render.py b/tools/render.py
index fe0d3af..da9a26d 100755
--- a/tools/render.py
+++ b/tools/render.py
@@ -11,14 +11,18 @@ import sys
 sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
 import pbm_png  # noqa: E402
 
-# name -> renderer executable in the build directory and its arguments (the output path comes last)
-DASHBOARDS = ["home", "indoor", "weather", "focus", "home_invalid", "home_stale", "home_12h_charging", "indoor_cold",
-              "focus_seconds", "home_battery_details", "home_inverted", "indoor_hot_f", "indoor_frost", "home_frost",
-              "grid_clock_12h"]  # test/host/dashboard_fixtures.h
-RENDERERS = {
-    "test_pattern": ["render_test_pattern"],
-    **{f"dash_{name}": ["render_dashboard", name] for name in DASHBOARDS},
-}
+# Renderers that take a fixture name; `--list` prints their fixtures (test/host/*_fixtures.h).
+LISTED = {"dash": "render_dashboard", "screen": "render_screen"}
+
+
+def renderers(build_dir):
+    """name -> the command to run, without the output path that comes last."""
+    commands = {"test_pattern": [str(build_dir / "render_test_pattern")]}
+    for prefix, exe in LISTED.items():
+        path = str(build_dir / exe)
+        names = subprocess.run([path, "--list"], check=True, capture_output=True, text=True).stdout.split()
+        commands.update({f"{prefix}_{name}": [path, name] for name in names})
+    return commands
 
 
 def main(argv=None):
@@ -28,10 +32,9 @@ def main(argv=None):
     args = parser.parse_args(argv)
     out_dir = pathlib.Path(args.out_dir)
     out_dir.mkdir(parents=True, exist_ok=True)
-    for name, command in RENDERERS.items():
+    for name, command in renderers(pathlib.Path(args.build_dir)).items():
         pbm = out_dir / f"{name}.pbm"
-        exe = pathlib.Path(args.build_dir) / command[0]
-        subprocess.run([str(exe), *command[1:], str(pbm)], check=True)
+        subprocess.run([*command, str(pbm)], check=True)
         (out_dir / f"{name}.png").write_bytes(pbm_png.png_from_pbm(pbm.read_bytes()))
         print(f"{name}: {pbm} and {pbm.with_suffix('.png')}")
     return 0
```

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure && python3 tools/render.py && tools/idf.sh build`
Expected:
- The suite passes 33 of 33.
- `render.py` writes 32 renders to `captures/render/`: the test pattern, 19 dashboards and 12 screens.
- The firmware builds without warnings.

Look at every `screen_*.png` and the four new `dash_*.png`. Check that:
- Czech diacritics render, and no hollow boxes appear.
- The holiday name in `dash_home_holiday_cs` sits on two lines inside its cell.
- The menu's cursor row and the edited value are inverted.
- The toast has its white rim.
- `dash_home_stale` shows its temperature without the arrow.

- [ ] **Step 7: Commit.**

```bash
git add components/ui test/host tools/render.py
git commit -m "feat(ui): add the menu, toast and critical-battery screens"
```

---


### Task 10: Panel sleep (`st7305`, `display`, `diag`)

**Files:**
- Modify: `components/st7305/include/st7305.h`, `components/st7305/st7305.c`, `components/display/include/display.h`, `components/display/display.c`, `components/diag/diag_cmd_display.c`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `esp_err_t st7305_sleep_in(void)`: datasheet §7.10.
    - From LPM it goes through HPM (`38h`, then 300 ms), then `SLPIN` (`10h`, then 100 ms).
    - The panel stops scanning, so its image fades, while its RAM stays. Night sleep uses it (spec §9.1).
  - `esp_err_t st7305_sleep_out(void)`: the panel reset and the init sequence, as at a cold boot.
    - Both init sequences set NRDSLP (D6h), so a plain `SLPOUT` reloads the NVM defaults. The spike's panel came back at 2 Hz that way.
    - Afterwards the panel is in HPM with a cleared RAM.
  - `bool st7305_asleep(void)`.
  - `st7305_init_warm(variant, mode, rate, bool asleep)`: a deep-sleep wake keeps a sleeping panel asleep.
  - `display_state_t.asleep`, kept in the snapshot.
  - `display_sleep()`; `display_wake()` (sleep-out, LPM, then the frame pushed again); `display_asleep()`.
  - `panel sleep` and `panel wake` on the console. `panel status` ends in `, asleep` while the panel sleeps.
  - Task 12's night uses all of it.

- [ ] **Step 1: Implement.** Apply the patches. The panel is write-only, so there is nothing to test on the host: Step 3 checks it on the board.

```diff
diff --git a/components/st7305/include/st7305.h b/components/st7305/include/st7305.h
index 4ea072e..5c8b158 100644
--- a/components/st7305/include/st7305.h
+++ b/components/st7305/include/st7305.h
@@ -40,7 +40,7 @@ typedef enum {
 esp_err_t st7305_init(st7305_variant_t variant);
 /* After a deep-sleep wake: attaches the SPI bus without resetting the panel, which kept its
  * image, mode and rate (AGENTS.md gotcha 4). Releases the pin holds set before sleeping. */
-esp_err_t st7305_init_warm(st7305_variant_t variant, st7305_mode_t mode, st7305_lpm_rate_t rate);
+esp_err_t st7305_init_warm(st7305_variant_t variant, st7305_mode_t mode, st7305_lpm_rate_t rate, bool asleep);
 /* Holds CS and RESET through deep sleep. Call last, just before sleeping. */
 esp_err_t st7305_prepare_deep_sleep(void);
 /* Resets the panel and runs another init sequence (the SPI clock follows the variant). */
@@ -53,6 +53,14 @@ esp_err_t st7305_set_mode(st7305_mode_t mode);
 esp_err_t st7305_set_lpm_rate(st7305_lpm_rate_t rate);
 /* Undoes st7305_prepare_deep_sleep() after it failed part-way, so the panel can be used again. */
 void st7305_cancel_deep_sleep(void);
+/* Sleep-in (datasheet §7.10): from LPM through HPM (300 ms), then SLPIN and 100 ms. The panel stops
+ * scanning, so its image fades; RAM and settings are kept. Night sleep (spec §9.1). */
+esp_err_t st7305_sleep_in(void);
+/* Wakes the panel from sleep-in with a reset and the init sequence, as a plain SLPOUT would reload
+ * the NVM defaults (NRDSLP is set). The panel is then in HPM with the RAM cleared: push a frame and
+ * set LPM. */
+esp_err_t st7305_sleep_out(void);
+bool st7305_asleep(void);
 st7305_variant_t st7305_variant(void);
 st7305_mode_t st7305_mode(void);
 st7305_lpm_rate_t st7305_lpm_rate(void);
```

```diff
diff --git a/components/st7305/st7305.c b/components/st7305/st7305.c
index ff56504..926f044 100644
--- a/components/st7305/st7305.c
+++ b/components/st7305/st7305.c
@@ -106,6 +106,7 @@ static uint8_t *s_panel; /* DMA-capable internal RAM */
 static bool s_bus_ready;
 static st7305_variant_t s_variant;
 static st7305_mode_t s_mode;
+static bool s_asleep;
 static st7305_lpm_rate_t s_lpm_rate = ST7305_LPM_1HZ;
 static volatile uint32_t s_te_pulses;
 
@@ -210,7 +211,7 @@ esp_err_t st7305_init(st7305_variant_t variant)
     return st7305_reinit(variant);
 }
 
-esp_err_t st7305_init_warm(st7305_variant_t variant, st7305_mode_t mode, st7305_lpm_rate_t rate)
+esp_err_t st7305_init_warm(st7305_variant_t variant, st7305_mode_t mode, st7305_lpm_rate_t rate, bool asleep)
 {
     ESP_RETURN_ON_FALSE(!s_bus_ready, ESP_ERR_INVALID_STATE, TAG, "already initialised");
     ESP_RETURN_ON_ERROR(bus_init(), TAG, "SPI bus");
@@ -225,8 +226,9 @@ esp_err_t st7305_init_warm(st7305_variant_t variant, st7305_mode_t mode, st7305_
     s_variant = variant;
     s_mode = mode;
     s_lpm_rate = rate;
-    ESP_LOGI(TAG, "attached without reset (%s, %s, LPM %s Hz)", xiaozhi ? "XiaoZhi" : "factory",
-             mode == ST7305_MODE_LPM ? "LPM" : "HPM", st7305_lpm_rate_name(rate));
+    s_asleep = asleep;
+    ESP_LOGI(TAG, "attached without reset (%s, %s, LPM %s Hz%s)", xiaozhi ? "XiaoZhi" : "factory",
+             mode == ST7305_MODE_LPM ? "LPM" : "HPM", st7305_lpm_rate_name(rate), asleep ? ", asleep" : "");
     return ESP_OK;
 }
 
@@ -256,6 +258,7 @@ esp_err_t st7305_reinit(st7305_variant_t variant)
     ESP_RETURN_ON_ERROR(create_io(xiaozhi ? 40 * 1000 * 1000 : 10 * 1000 * 1000), TAG, "panel IO");
     hardware_reset();
     s_mode = ST7305_MODE_HPM; /* the panel is in HPM after a reset, even if the init below fails */
+    s_asleep = false;
     if (xiaozhi) {
         ESP_RETURN_ON_ERROR(run_init(s_init_xiaozhi, sizeof(s_init_xiaozhi) / sizeof(s_init_xiaozhi[0])), TAG,
                             "XiaoZhi init");
@@ -315,6 +318,40 @@ esp_err_t st7305_set_mode(st7305_mode_t mode)
     return ESP_OK;
 }
 
+esp_err_t st7305_sleep_in(void)
+{
+    ESP_RETURN_ON_FALSE(s_io != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
+    if (s_asleep) {
+        return ESP_OK;
+    }
+    if (s_mode == ST7305_MODE_LPM) { /* datasheet §7.10: sleep-in from LPM goes through HPM */
+        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x38, NULL, 0), TAG, "HPM");
+        s_mode = ST7305_MODE_HPM;
+        delay_at_least_ms(300);
+    }
+    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x10, NULL, 0), TAG, "SLPIN");
+    delay_at_least_ms(100);
+    s_asleep = true;
+    return ESP_OK;
+}
+
+esp_err_t st7305_sleep_out(void)
+{
+    ESP_RETURN_ON_FALSE(s_io != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
+    if (!s_asleep) {
+        return ESP_OK;
+    }
+    /* Both init sequences set NRDSLP (D6h 2nd parameter 0x02): a plain SLPOUT reloads the NVM
+     * defaults, and the panel came back at 2 Hz (M3b). Start over as at a cold boot instead; the
+     * sequence has its own SLPOUT and wait. */
+    return st7305_reinit(s_variant);
+}
+
+bool st7305_asleep(void)
+{
+    return s_asleep;
+}
+
 esp_err_t st7305_set_lpm_rate(st7305_lpm_rate_t rate)
 {
     ESP_RETURN_ON_FALSE((unsigned)rate <= ST7305_LPM_8HZ, ESP_ERR_INVALID_ARG, TAG, "LPM rate");
```

```diff
diff --git a/components/display/include/display.h b/components/display/include/display.h
index 41dd4b7..ce749ef 100644
--- a/components/display/include/display.h
+++ b/components/display/include/display.h
@@ -18,6 +18,7 @@ typedef struct {
     st7305_lpm_rate_t lpm_rate;
     uint32_t last_crc; /* CRC of the frame the panel shows */
     bool pushed;
+    bool asleep; /* sleep-in: night sleep (spec §9.1) */
 } display_state_t;
 
 esp_err_t display_init(st7305_variant_t variant); /* cold start: white frame, then LPM */
@@ -28,6 +29,11 @@ void display_export(display_state_t *out);
 esp_err_t display_prepare_deep_sleep(void);
 void display_cancel_deep_sleep(void); /* after display_prepare_deep_sleep() failed */
 gfx_fb_t *display_fb(void); /* NULL until display_init has allocated the framebuffer */
+/* Night sleep (spec §9.1): the panel stops scanning and its image fades; waking pushes the frame
+ * again and returns the panel to LPM. */
+esp_err_t display_sleep(void);
+esp_err_t display_wake(void);
+bool display_asleep(void);
 /* Pushes the framebuffer if it changed since the last push (CRC32), or always when force is set. */
 esp_err_t display_commit(bool force);
 /* Re-initialises the panel with another init sequence and pushes the current frame. */
```

```diff
diff --git a/components/display/display.c b/components/display/display.c
index a9f0534..f880f7c 100644
--- a/components/display/display.c
+++ b/components/display/display.c
@@ -24,7 +24,8 @@ static esp_err_t alloc_fb(void)
 esp_err_t display_init_warm(const display_state_t *state)
 {
     ESP_RETURN_ON_ERROR(alloc_fb(), TAG, "framebuffer");
-    ESP_RETURN_ON_ERROR(st7305_init_warm(state->variant, state->mode, state->lpm_rate), TAG, "panel attach");
+    ESP_RETURN_ON_ERROR(st7305_init_warm(state->variant, state->mode, state->lpm_rate, state->asleep), TAG,
+                        "panel attach");
     s_last_crc = state->last_crc;
     s_pushed = state->pushed;
     return ESP_OK;
@@ -38,9 +39,30 @@ void display_export(display_state_t *out)
         .lpm_rate = st7305_lpm_rate(),
         .last_crc = s_last_crc,
         .pushed = s_pushed,
+        .asleep = st7305_asleep(),
     };
 }
 
+esp_err_t display_sleep(void)
+{
+    return st7305_sleep_in();
+}
+
+esp_err_t display_wake(void)
+{
+    if (!st7305_asleep()) {
+        return ESP_OK;
+    }
+    ESP_RETURN_ON_ERROR(st7305_sleep_out(), TAG, "sleep out");
+    ESP_RETURN_ON_ERROR(st7305_set_mode(ST7305_MODE_LPM), TAG, "LPM");
+    return display_commit(true); /* the image faded while the panel slept */
+}
+
+bool display_asleep(void)
+{
+    return st7305_asleep();
+}
+
 esp_err_t display_prepare_deep_sleep(void)
 {
     return st7305_prepare_deep_sleep();
```

```diff
diff --git a/components/diag/diag_cmd_display.c b/components/diag/diag_cmd_display.c
index 1765db2..c6c3892 100644
--- a/components/diag/diag_cmd_display.c
+++ b/components/diag/diag_cmd_display.c
@@ -42,8 +42,9 @@ static int screenshot_body(int argc, char **argv)
 
 static int print_status(void)
 {
-    printf("panel %s, mode %s, lpm rate %s Hz\n", st7305_variant() == ST7305_VARIANT_XIAOZHI ? "xiaozhi" : "factory",
-           st7305_mode() == ST7305_MODE_LPM ? "lpm" : "hpm", st7305_lpm_rate_name(st7305_lpm_rate()));
+    printf("panel %s, mode %s, lpm rate %s Hz%s\n", st7305_variant() == ST7305_VARIANT_XIAOZHI ? "xiaozhi" : "factory",
+           st7305_mode() == ST7305_MODE_LPM ? "lpm" : "hpm", st7305_lpm_rate_name(st7305_lpm_rate()),
+           st7305_asleep() ? ", asleep" : "");
     return 0;
 }
 
@@ -113,12 +114,17 @@ static int panel_body(int argc, char **argv)
         err = st7305_set_mode(ST7305_MODE_LPM);
     } else if (argc == 3 && strcmp(argv[1], "rate") == 0 && parse_lpm_rate(argv[2], &rate)) {
         err = st7305_set_lpm_rate(rate);
+    } else if (argc == 2 && strcmp(argv[1], "sleep") == 0) {
+        err = display_sleep();
+    } else if (argc == 2 && strcmp(argv[1], "wake") == 0) {
+        err = display_wake();
     } else if (argc == 3 && strcmp(argv[1], "init") == 0 && strcmp(argv[2], "factory") == 0) {
         err = display_set_variant(ST7305_VARIANT_FACTORY);
     } else if (argc == 3 && strcmp(argv[1], "init") == 0 && strcmp(argv[2], "xiaozhi") == 0) {
         err = display_set_variant(ST7305_VARIANT_XIAOZHI);
     } else {
-        printf("usage: panel status | test | clear | mode <hpm|lpm> | rate <0.25|0.5|1|2|4|8> | fps [s] | init <factory|xiaozhi>\n");
+        printf("usage: panel status | test | clear | mode <hpm|lpm> | rate <0.25|0.5|1|2|4|8> | fps [s] | sleep |"
+               " wake | init <factory|xiaozhi>\n");
         return 1;
     }
     if (err != ESP_OK) {
```

- [ ] **Step 2: Build.**

Run: `tools/idf.sh build`
Expected: the firmware builds without warnings.

- [ ] **Step 3: Check on the board.** Confirm the port first: `ls /dev/cu.usbmodem*` shows one port, and `flash` must print `MAC: 14:c1:9f:54:bb:94`.

```bash
tools/idf.sh -p /dev/cu.usbmodem2101 flash
tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/m3b-t10-boot.log
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-t10-before.png
tools/idf.sh exec python tools/devlog.py --cmd "panel sleep" --cmd "panel status" --cmd "panel fps 3" \
  -o captures/m3b-t10-sleep.log
tools/idf.sh exec python tools/devlog.py --cmd "panel wake" --cmd "panel status" -o captures/m3b-t10-wake.log
sleep 5
tools/idf.sh exec python tools/devlog.py --cmd "panel fps 5" -o captures/m3b-t10-fps.log
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-t10-after.png --compare captures/m3b-t10-before.pbm
```

Expected:
- `panel status` after the sleep: `panel factory, mode hpm, lpm rate 1 Hz, asleep`.
- `panel fps: 0 frames in 3 s = 0.00 Hz`.
- After the wake: `panel factory, mode lpm, lpm rate 1 Hz`.
- `panel fps: 5 frames in 5 s = 1.00 Hz`. The first seconds after a wake can read higher, while the panel settles; that is why the check waits.
- `screenshot.py` exits 0: the frame is unchanged. If a minute passed in between, the clock differs; take both screenshots again within the same minute.
- No `E` or `W` lines in the logs.

- [ ] **Step 4: Commit.**

```bash
git add components/st7305 components/display components/diag
git commit -m "feat(st7305): add panel sleep for the night, waking with the init sequence"
```

---


### Task 11: Held buttons and the critical sleep (`power`, `board`)

**Files:**
- Modify: `components/power/include/power.h`, `components/power/power.c`, `components/board/include/board_buttons.h`, `components/board/board_buttons.c`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - **Masking (D16, spec §9.2).** A button that is low when the chip goes to sleep is left out of that sleep's wake sources. This holds for:
    - light sleep: no `gpio_wakeup_enable()` for it, and the decode after waking ignores it;
    - deep sleep: no ext1 bit;
    - the retry sleep after a failed boot.

    The RTC alarm and the timer still wake the chip.
  - **The mask record.**
    - `POWER_BUTTON_KEY`, `POWER_BUTTON_BOOT` and `unsigned power_masked_buttons(void)`: the buttons the sleep that just ended left out.
    - It is kept in RTC RAM (`power_state_t.masked`, `STATE_VERSION` 5) for deep sleep.
    - The app ignores those buttons until they are released (Task 12).
  - **`void power_sleep_critical(uint32_t recheck_s)`.** The critical-battery deep sleep (spec §8): KEY is the only wake source. If KEY is held, a timer wakes the chip after `recheck_s` instead, so the board can't sleep for good.
  - **`void board_buttons_ignore_until_released(board_button_t)`.** A held button makes no gesture until it has been released. The gesture being timed for it is dropped. A released button is unaffected.
  - **`void board_buttons_set_config(const gesture_config_t config[BOARD_BUTTON_COUNT])`.** New gesture timings for the context (spec §5.6), applied on the buttons task; `config` must stay valid. The menu uses no KEY double and a 1 s BOOT long press (Task 12).
- Nothing calls these until Task 12, so behaviour doesn't change yet.

- [ ] **Step 1: Implement.** Apply the patches. These are pin-level paths with no host build (the pure gesture recogniser and power policy are unchanged): Step 3 checks that sleeping still works, and owner check 2 covers the held buttons.

```diff
diff --git a/components/power/include/power.h b/components/power/include/power.h
index 6de78ff..29397db 100644
--- a/components/power/include/power.h
+++ b/components/power/include/power.h
@@ -59,8 +59,17 @@ power_plan_t power_plan(bool work_pending);
 power_wake_t power_sleep_light(time_t until_utc);
 /* Never returns: the chip reboots on wake. Seal RTC-RAM state and hold the panel pins first. */
 void power_sleep_deep(time_t until_utc);
+/* Never returns: the critical-battery sleep (spec §8), woken by KEY only. If KEY is held, a timer
+ * wakes the chip after `recheck_s` instead, so it can't sleep for good. */
+void power_sleep_critical(uint32_t recheck_s);
 /* Never returns: deep sleep for `seconds` or until KEY or BOOT, then boot from scratch. */
 void power_sleep_retry(uint32_t seconds);
+/* A button held when the chip goes to sleep is left out of that sleep's wake sources (D16), so a
+ * stuck KEY or BOOT can't keep the board awake. These bits say which were, for the sleep that just
+ * ended: the app ignores such a button until it is released. */
+#define POWER_BUTTON_KEY  (1u << 0)
+#define POWER_BUTTON_BOOT (1u << 1)
+unsigned power_masked_buttons(void);
 power_idle_t power_idle_strategy(void);
 /* Applies at once; an error means only that NVS didn't keep it for the next boot. */
 esp_err_t power_set_idle_strategy(power_idle_t idle);
```

```diff
diff --git a/components/power/power.c b/components/power/power.c
index 7120ed4..a547349 100644
--- a/components/power/power.c
+++ b/components/power/power.c
@@ -20,7 +20,7 @@
 #include "util_snapshot.h"
 
 #define STATE_MAGIC   0x72666c70u /* "rflp" */
-#define STATE_VERSION 4
+#define STATE_VERSION 5
 #define NVS_NAMESPACE "sys"
 #define NVS_KEY_IDLE  "idle"
 #define TEST_END_HOLD_MS 3000
@@ -37,6 +37,7 @@ typedef struct {
     uint8_t idle_valid; /* `idle` mirrors NVS, so routine wakes need not open it */
     uint8_t idle;
     uint8_t retry;      /* boot failed: boot from scratch at the next wake */
+    uint8_t masked;     /* POWER_BUTTON_* left out of the deep sleep's wake sources (D16) */
 } power_state_t;
 
 static RTC_DATA_ATTR power_state_t s_rtc; /* survives deep sleep only */
@@ -46,6 +47,7 @@ static bool s_boot_failed;
 static bool s_cpu_pd_ready;
 static int64_t s_hold_until_us;
 static int64_t s_awake_since_us; /* esp_timer time this awake phase began; -1 after a stats reset */
+static unsigned s_masked;        /* POWER_BUTTON_* the sleep that just ended left out */
 
 static const gpio_num_t k_wake_pins[] = { BOARD_PIN_RTC_INT, BOARD_PIN_KEY, BOARD_PIN_BOOT };
 
@@ -55,6 +57,7 @@ static void seal(void)
 }
 
 static void count_wake(power_wake_t wake, int64_t slept_ms);
+static unsigned held_buttons(void);
 static power_wake_t decode_boot_wake(void);
 static void finish_test(void);
 
@@ -66,6 +69,7 @@ esp_err_t power_init(void)
         s_rtc = (power_state_t){ 0 };
         seal();
     }
+    s_masked = s_boot_wake != POWER_WAKE_COLD ? s_rtc.masked : 0;
     if (s_boot_wake != POWER_WAKE_COLD && s_rtc.retry) {
         s_rtc.retry = 0;
         seal();
@@ -253,9 +257,14 @@ power_wake_t power_sleep_light(time_t until_utc)
             ESP_LOGW(TAG, "CPU stays powered in light sleep: %s", esp_err_to_name(err));
         }
     }
+    unsigned held = held_buttons();
     for (size_t i = 0; i < sizeof(k_wake_pins) / sizeof(k_wake_pins[0]); i++) {
         gpio_intr_disable(k_wake_pins[i]);
-        gpio_wakeup_enable(k_wake_pins[i], GPIO_INTR_LOW_LEVEL);
+        bool masked = (k_wake_pins[i] == BOARD_PIN_KEY && (held & POWER_BUTTON_KEY)) ||
+                      (k_wake_pins[i] == BOARD_PIN_BOOT && (held & POWER_BUTTON_BOOT));
+        if (!masked) {
+            gpio_wakeup_enable(k_wake_pins[i], GPIO_INTR_LOW_LEVEL);
+        }
     }
     esp_sleep_enable_gpio_wakeup();
     esp_sleep_enable_timer_wakeup(sleep_us_until(until_utc));
@@ -273,10 +282,11 @@ power_wake_t power_sleep_light(time_t until_utc)
         s_awake_since_us = woke_us;
     }
 
-    power_wake_t wake = POWER_WAKE_OTHER;
-    if (gpio_get_level(BOARD_PIN_KEY) == 0) {
+    s_masked = held;
+    power_wake_t wake = POWER_WAKE_OTHER; /* a button held since before the sleep didn't wake it */
+    if (!(held & POWER_BUTTON_KEY) && gpio_get_level(BOARD_PIN_KEY) == 0) {
         wake = POWER_WAKE_KEY;
-    } else if (gpio_get_level(BOARD_PIN_BOOT) == 0) {
+    } else if (!(held & POWER_BUTTON_BOOT) && gpio_get_level(BOARD_PIN_BOOT) == 0) {
         wake = POWER_WAKE_BOOT;
     } else if (gpio_get_level(BOARD_PIN_RTC_INT) == 0) {
         wake = POWER_WAKE_RTC;
@@ -301,14 +311,34 @@ static void start_deep_sleep(void)
     esp_deep_sleep_start();
 }
 
+/* POWER_BUTTON_* for the buttons held right now. */
+static unsigned held_buttons(void)
+{
+    return (gpio_get_level(BOARD_PIN_KEY) == 0 ? POWER_BUTTON_KEY : 0) |
+           (gpio_get_level(BOARD_PIN_BOOT) == 0 ? POWER_BUTTON_BOOT : 0);
+}
+
+/* ext1 bits for the buttons that are not held (D16). */
+static uint64_t button_wake_bits(unsigned held)
+{
+    return (held & POWER_BUTTON_KEY ? 0 : BIT64(BOARD_PIN_KEY)) |
+           (held & POWER_BUTTON_BOOT ? 0 : BIT64(BOARD_PIN_BOOT));
+}
+
+unsigned power_masked_buttons(void)
+{
+    return s_masked;
+}
+
 void power_sleep_deep(time_t until_utc)
 {
     count_sleep(true);
     esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
     rtc_gpio_pullup_en(BOARD_PIN_RTC_INT); /* INT has no external pull-up */
     rtc_gpio_pulldown_dis(BOARD_PIN_RTC_INT);
-    esp_sleep_enable_ext1_wakeup_io(BIT64(BOARD_PIN_RTC_INT) | BIT64(BOARD_PIN_KEY) | BIT64(BOARD_PIN_BOOT),
-                                    ESP_EXT1_WAKEUP_ANY_LOW);
+    unsigned held = held_buttons();
+    s_rtc.masked = (uint8_t)held;
+    esp_sleep_enable_ext1_wakeup_io(BIT64(BOARD_PIN_RTC_INT) | button_wake_bits(held), ESP_EXT1_WAKEUP_ANY_LOW);
     esp_sleep_enable_timer_wakeup(sleep_us_until(until_utc));
     start_deep_sleep();
 }
@@ -318,11 +348,30 @@ void power_sleep_retry(uint32_t seconds)
     s_rtc.retry = 1;
     esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
     /* Not RTC_INT: whatever broke the boot may leave it low, which would wake the chip at once. */
-    esp_sleep_enable_ext1_wakeup_io(BIT64(BOARD_PIN_KEY) | BIT64(BOARD_PIN_BOOT), ESP_EXT1_WAKEUP_ANY_LOW);
+    unsigned held = held_buttons();
+    s_rtc.masked = (uint8_t)held;
+    uint64_t bits = button_wake_bits(held);
+    if (bits != 0) {
+        esp_sleep_enable_ext1_wakeup_io(bits, ESP_EXT1_WAKEUP_ANY_LOW);
+    }
     esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000u);
     start_deep_sleep();
 }
 
+void power_sleep_critical(uint32_t recheck_s)
+{
+    count_sleep(true);
+    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
+    unsigned held = held_buttons() & POWER_BUTTON_KEY;
+    s_rtc.masked = (uint8_t)held;
+    if (held) {
+        esp_sleep_enable_timer_wakeup((uint64_t)recheck_s * 1000000u);
+    } else {
+        esp_sleep_enable_ext1_wakeup_io(BIT64(BOARD_PIN_KEY), ESP_EXT1_WAKEUP_ANY_LOW);
+    }
+    start_deep_sleep();
+}
+
 
 power_idle_t power_idle_strategy(void)
 {
```

```diff
diff --git a/components/board/include/board_buttons.h b/components/board/include/board_buttons.h
index 9a779ca..b54ef5f 100644
--- a/components/board/include/board_buttons.h
+++ b/components/board/include/board_buttons.h
@@ -25,6 +25,11 @@ void board_buttons_inject(board_button_t button, gesture_t gesture);
 void board_buttons_woke(board_button_t button);
 /* Re-reads both levels, e.g. after light sleep, when edge interrupts may have been missed. */
 void board_buttons_resync(void);
+/* A button held since before the last sleep (D16): no gestures until it has been released. */
+void board_buttons_ignore_until_released(board_button_t button);
+/* New gesture timings for the context (spec §5.6): the menu has no double press on KEY and a 1 s
+ * BOOT long press. `config` must stay valid; it is applied on the buttons task. */
+void board_buttons_set_config(const gesture_config_t config[BOARD_BUTTON_COUNT]);
 /* True while a gesture is being timed or an edge is queued; sleeping then would lose it. */
 bool board_buttons_busy(void);
 bool board_buttons_pressed(board_button_t button);
```

```diff
diff --git a/components/board/board_buttons.c b/components/board/board_buttons.c
index 357ca14..d080112 100644
--- a/components/board/board_buttons.c
+++ b/components/board/board_buttons.c
@@ -22,6 +22,8 @@ typedef enum {
     MSG_INJECT,
     MSG_WOKE,
     MSG_RESYNC,
+    MSG_IGNORE,
+    MSG_CONFIG,
 } msg_kind_t;
 
 typedef struct {
@@ -35,6 +37,8 @@ static QueueHandle_t s_queue;
 static board_button_cb_t s_cb;
 static gesture_recogniser_t s_rec[BOARD_BUTTON_COUNT];
 static volatile bool s_busy;
+static bool s_ignore[BOARD_BUTTON_COUNT];          /* held since before the sleep: wait for release */
+static const gesture_config_t *volatile s_config; /* the next gesture timings, for MSG_CONFIG */
 
 static void IRAM_ATTR on_edge(void *arg)
 {
@@ -105,13 +109,33 @@ static void buttons_task(void *arg)
                 break;
             case MSG_RESYNC:
                 for (int i = 0; i < BOARD_BUTTON_COUNT; i++) {
-                    report((board_button_t)i, gesture_update(&s_rec[i], board_buttons_pressed(i), now_ms()));
+                    if (!s_ignore[i]) {
+                        report((board_button_t)i, gesture_update(&s_rec[i], board_buttons_pressed(i), now_ms()));
+                    }
+                }
+                break;
+            case MSG_IGNORE:
+                if (board_buttons_pressed(b)) {
+                    s_ignore[b] = true;
+                    gesture_init(&s_rec[b], s_rec[b].config); /* forget any press being timed */
+                }
+                break;
+            case MSG_CONFIG:
+                for (int i = 0; s_config != NULL && i < BOARD_BUTTON_COUNT; i++) {
+                    gesture_set_config(&s_rec[i], s_config[i]);
                 }
                 break;
             }
         }
         bool busy = false;
         for (int b = 0; b < BOARD_BUTTON_COUNT; b++) {
+            if (s_ignore[b]) {
+                if (board_buttons_pressed(b)) {
+                    continue; /* still held */
+                }
+                s_ignore[b] = false;
+                ESP_LOGI(TAG, "%s released", board_button_name(b));
+            }
             report((board_button_t)b, gesture_update(&s_rec[b], board_buttons_pressed(b), now_ms()));
             busy |= gesture_busy(&s_rec[b]);
         }
@@ -164,6 +188,17 @@ void board_buttons_resync(void)
     post((msg_t){ .kind = MSG_RESYNC });
 }
 
+void board_buttons_ignore_until_released(board_button_t button)
+{
+    post((msg_t){ .kind = MSG_IGNORE, .button = (uint8_t)button });
+}
+
+void board_buttons_set_config(const gesture_config_t config[BOARD_BUTTON_COUNT])
+{
+    s_config = config;
+    post((msg_t){ .kind = MSG_CONFIG });
+}
+
 bool board_buttons_busy(void)
 {
     return s_busy || (s_queue != NULL && uxQueueMessagesWaiting(s_queue) > 0);
```

- [ ] **Step 2: Build and run the host suite.**

Run: `tools/idf.sh build && ctest --test-dir build-host --output-on-failure`
Expected: the firmware builds without warnings; the suite passes 33 of 33.

- [ ] **Step 3: Check on the board that sleep still works.** Confirm the port as in Task 10. `STATE_VERSION` changed, so the first boot starts the power state afresh.

```bash
tools/idf.sh -p /dev/cu.usbmodem2101 flash
tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/m3b-t11-boot.log
tools/idf.sh exec python tools/devlog.py --cmd "sleep stats reset" --cmd "sleep test light 1" -t 5
sleep 75
tools/idf.sh exec python tools/devlog.py --cmd "sleep test deep 1" -t 5
sleep 75
tools/idf.sh exec python tools/devlog.py --cmd "sleep stats" -o captures/m3b-t11-stats.log
```

Expected:
- `sleep: 1 light, 1 deep; wakes: rtc 2, key 0, boot 0, timer 0, other 0`. With no button held, both sleeps kept every wake source, and the RTC alarm woke them.
- `test cycles left 0`.
- No `E` or `W` lines.

Start a fresh devlog after each test, since a session opened before a sleep never sees the console come back (tooling note, M2).

- [ ] **Step 4: Commit.** Two commits:

```bash
git add components/power
git commit -m "feat(power): leave held buttons out of the wake sources and add the critical sleep (D16)"
git add components/board
git commit -m "feat(board): ignore a held button until release, and switch gesture timings per context"
```

---


### Task 12: The app: menu, toasts, schedule, night and critical battery (`main`)

**Files:**
- Create: `main/app_menu.c`
- Modify: `main/app.c`, `main/app_ui.c`, `main/app_internal.h`, `main/app_cmds.c`, `main/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1–11. The main entry points:
  - `ui_menu_*` and `ui_draw_menu()`; `ui_draw_toast()` and `ui_draw_critical()`;
  - `ui_schedule_due()` and `ui_schedule_next()`; `sched_local_to_utc()` and `sched_next_weekly()`;
  - `timekeeping_zones()`;
  - `battery_critical()`;
  - `display_sleep()`, `display_wake()` and `display_asleep()`;
  - `power_masked_buttons()`, `power_sleep_critical()`, `board_buttons_ignore_until_released()` and `board_buttons_set_config()`;
  - `storage_erase()`.
- Produces (`main/app_internal.h`):
  - `app_ui_state_t` gains `night_until`, `sched_checked`, `cold_boot_at` and `critical`. It is part of the deep-sleep snapshot: `SNAP_VERSION` 3, asserted at 4 KB or less (1808 bytes now; M3a review).
  - Around it: the toast, night and menu functions, `app_state()`, `app_clock_moved()` and `app_uptime_ms()`, all for the app task only.
- Behaviour:
  - **Dashboard buttons** (spec §5.6):
    - KEY short: the next preset, with the toast "Preset: <name>";
    - KEY double: auto-cycle on or off, with a toast;
    - KEY long: the menu;
    - BOOT short: the sensors again;
    - BOOT long (3 s): stays unbound until config mode (M4).
  - **The menu** (spec §5.7):
    - The panel is in HPM while it is open, with the menu's gesture timings; it closes after 60 s without input.
    - Presets, Auto-cycle, Interval and Schedule save `presets.json`. The other settings save `settings.json`, merged over the file's unknown keys; after a routine deep-sleep wake the file is read first.
    - The offsets take effect at once, with a fresh sample. The zone, the update interval and the refresh rate apply at once.
    - The date-time editor sets the RTC through `sched_local_to_utc()`.
    - Info shows:
      - the battery;
      - the firmware version and the first bytes of its ELF hash;
      - `reflbo-XXXX`;
      - the uptime since the first valid clock after a cold boot;
      - the free heap.
    - Reboot shows a toast, then restarts.
    - Factory reset erases `storage` and the NVS namespaces `wifi`, `secrets` and `ctr`, keeps `sys` (spec §14.4), then restarts.
  - **Toasts**: 3 s over the screen, holding the board awake so they don't linger through a sleep. A config file that fell back to its defaults shows "Using default settings" at boot (spec §14.3).
  - **The schedule** (spec §5.4):
    - Entries run at their minute, while the clock is valid and no night is on.
    - The presets of a minute run before its night.
    - Entries skipped by a clock jump or a night don't run late.
    - A preset entry's switch isn't saved, like an auto-cycle switch.
    - A cold boot doesn't run the entries it missed.
  - **Night** (spec §9.1):
    - The panel sleeps. The chip deep-sleeps whatever the idle strategy and even with a PC attached, until the RTC alarm at the end (plus the backup timer) or a button.
    - A button wakes the panel, which shows the dashboard. The waking press does nothing else. The night resumes 60 s after the last press, and the board stays awake meanwhile. KEY long still opens the menu, and the night waits for the menu to close.
    - A night that starts with a button held checks again every minute, so the button can wake it once released (D16).
    - The entries inside the night are skipped.
    - `night <1-1440>` starts one from the console, for measuring.
  - **Critical battery** (spec §8):
    - At the threshold, the critical screen is drawn and the chip deep-sleeps with KEY as its only wake source (a 600 s recheck if KEY is held).
    - A press checks the battery again, and a recovered battery returns the dashboard.
  - **Held buttons**: after any wake, a button the sleep left out is ignored until released.
  - **Clock moves**: `app_clock_moved()` runs after `rtc set` and the menu's editor. It:
    - shifts the battery history;
    - restarts the schedule checks;
    - restarts the cycle interval, so a forward move doesn't switch presets at once (M3a review);
    - renders at once.
  - **Console**:
    - `night <minutes>`;
    - `schedule list | on | off | clear | add <HH:MM> preset <id> [days] | add <HH:MM> night <HH:MM> [days]`, since until the M4 web UI the console sets the entries (spec §5.7);
    - `field set` rejects NaN, infinity and values beyond ±100000 (M3a review).

- [ ] **Step 1: Write the app.** The four files replace the old ones; `app_menu.c` is new; apply the CMake patch.

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "board_buttons.h"
#include "datastore.h"
#include "esp_err.h"
#include "scheduler.h"
#include "settings.h"
#include "ui_fields.h"
#include "ui_menu.h"
#include "ui_preset.h"

/* The dashboard's state and behaviour (main/app_ui.c, main/app_menu.c). All of it belongs to the
 * app task. */

typedef struct {
    ds_t ds;
    settings_t settings;
    ui_presets_t presets;
    time_t cycle_at;      /* next auto-cycle switch; 0 = not armed (cycling off, or no tick yet) */
    time_t done_slot;     /* the last minute slot that was sampled and rendered */
    time_t night_until;   /* night sleep ends here (UTC, on a minute); 0 = no night (spec §9.1) */
    time_t sched_checked; /* schedule entries up to this time have run (spec §5.4) */
    time_t cold_boot_at;  /* for Info > Uptime; 0 while the clock is unset */
    bool critical;        /* the critical-battery screen is up (spec §8) */
} app_ui_state_t;

/* Kconfig settings, the built-in presets and an empty datastore. */
void app_ui_defaults(void);
/* Mounts storage and reads settings.json and presets.json, keeping defaults for what fails. */
void app_ui_load(void);
void app_ui_export(app_ui_state_t *out); /* for the deep-sleep snapshot */
void app_ui_import(const app_ui_state_t *in);
app_ui_state_t *app_state(void); /* the menu edits it (main/app_menu.c) */

const settings_t *app_settings(void);
ds_t *app_ds(void);
ui_presets_t *app_presets(void);

/* Applies the settings that live outside the app: time zone, sensor offsets, freshness, panel rate. */
void app_ui_apply_settings(void);
/* Draws what the screen shows now (the menu, the critical screen or the dashboard, and a toast
 * over it) and pushes it. */
void app_ui_render(void);
/* The context a render uses right now (also for the `field` command). */
void app_ui_context(ui_context_t *ctx);
void app_ui_sample(time_t now); /* SHTC3 and battery into the datastore */
/* Makes preset `index` active and renders it. `persist` saves presets.json (manual switches). */
void app_ui_select(int index, bool persist);
void app_ui_toggle_cycle(void);
void app_ui_set_cycle(bool enabled);
/* Samples and renders what is due now: minute slots, the cycle switch, the seconds display and
 * the schedule. `force` samples and renders anyway. Call after reading the RTC. */
void app_ui_tick(bool force);
sched_wake_t app_ui_next_wake(time_t now);
esp_err_t app_ui_save_settings(void);
esp_err_t app_ui_save_presets(void);

/* A short message over the screen for about 3 s (spec §5.5). */
void app_ui_toast(const char *text);
bool app_ui_toast_active(void);
int64_t app_ui_toast_until_ms(void);
/* The toast's time is up: draws the screen without it. */
void app_ui_toast_expire(void);

/* Night sleep (spec §9.1): starts now and ends at `until` (UTC, on a minute). */
void app_ui_start_night(time_t until);
void app_ui_end_night(void);
bool app_ui_night(void);

/* The on-device menu (main/app_menu.c, spec §5.7). The dashboard's gesture timings (spec §5.6)
 * live there too, since the menu swaps them for its own. */
extern const gesture_config_t k_app_dashboard_buttons[BOARD_BUTTON_COUNT];
void app_menu_open(void);
void app_menu_close(void);
bool app_menu_is_open(void);
void app_menu_key(ui_menu_key_t key);
void app_menu_render(void);
int64_t app_menu_deadline_ms(void); /* the menu closes at this time without input */

/* Implemented in main/app.c for the menu: the clock was set, and moved by delta_s. */
void app_clock_moved(int64_t delta_s);
int64_t app_uptime_ms(void); /* milliseconds since boot, unmoved by clock changes: toasts, menu */

/* The `field`, `preset` and `night` console commands (main/app_cmds.c); call after diag_start(). */
void app_register_commands(void);
```

```c
#include <stdio.h>
#include <string.h>

#include "app_internal.h"
#include "display.h"
#include "esp_log.h"
#include "lang.h"
#include "power.h"
#include "sdkconfig.h"
#include "sensors.h"
#include "st7305.h"
#include "storage.h"
#include "timekeeping.h"
#include "ui_dashboard.h"
#include "ui_screens.h"
#include "util_time.h"

#define TOAST_MS 3000

static const char *TAG = "app_ui";

static app_ui_state_t s;
static char s_file[UI_PRESETS_JSON_MAX];
static char s_settings_base[2048]; /* settings.json as read: keys this firmware doesn't know stay */
static char s_err[96];
static char s_toast[64];
static int64_t s_toast_until_ms;

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

/* True if the file had to fall back to the defaults (it existed but nothing in it parsed). */
static bool load_one(const char *path, char *buf, size_t buf_size, storage_parse_t parse, void *target, size_t size,
                     const void *defaults)
{
    bool from_backup = false;
    esp_err_t err = storage_load(path, buf, buf_size, parse, target, &from_backup);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s loaded%s", path, from_backup ? " from the backup" : "");
        return false;
    }
    memcpy(target, defaults, size); /* a failed parse may have left it half-written */
    ESP_LOGI(TAG, "%s: %s; using defaults", path, err == ESP_ERR_NOT_FOUND ? "not there yet" : "invalid");
    return err != ESP_ERR_NOT_FOUND;
}

void app_ui_load(void)
{
    settings_t settings_defaults;
    default_settings(&settings_defaults);
    ui_presets_t presets_defaults;
    ui_presets_defaults(&presets_defaults);
    s.settings = settings_defaults;
    s.presets = presets_defaults;
    s.cycle_at = 0; /* armed by the first tick, once the RTC has set the clock */
    s_settings_base[0] = '\0';
    esp_err_t err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage: %s; settings and presets use their defaults", esp_err_to_name(err));
        return;
    }
    bool fell_back = load_one(STORAGE_SETTINGS_PATH, s_settings_base, sizeof(s_settings_base), parse_settings,
                              &s.settings, sizeof(s.settings), &settings_defaults);
    if (fell_back) {
        s_settings_base[0] = '\0'; /* nothing worth keeping in it */
    }
    fell_back |= load_one(STORAGE_PRESETS_PATH, s_file, sizeof(s_file), parse_presets, &s.presets, sizeof(s.presets),
                          &presets_defaults);
    if (fell_back) { /* spec §14.3: say so */
        app_ui_toast(lang_str(lang_get(s.settings.language), LS_T_DEFAULTS));
    }
}

void app_ui_export(app_ui_state_t *out)
{
    *out = s;
}

void app_ui_import(const app_ui_state_t *in)
{
    s = *in;
}

app_ui_state_t *app_state(void)
{
    return &s;
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
    if (s.cold_boot_at == 0 && timekeeping_valid()) {
        s.cold_boot_at = time(NULL); /* Info > Uptime counts from the first valid clock after a cold boot */
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
    if (fb == NULL || display_asleep()) {
        return; /* night sleep: nothing is drawn (spec §9.1) */
    }
    if (app_menu_is_open()) {
        app_menu_render();
        return;
    }
    ui_context_t ctx;
    app_ui_context(&ctx);
    if (s.critical) {
        ui_draw_critical(fb, &ctx);
    } else {
        ui_draw_dashboard(fb, &ctx, &s.presets.presets[s.presets.active]);
    }
    if (app_ui_toast_active()) {
        ui_draw_toast(fb, s_toast);
    }
    esp_err_t err = display_commit(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display: %s", esp_err_to_name(err));
    }
}

void app_ui_toast(const char *text)
{
    snprintf(s_toast, sizeof(s_toast), "%s", text);
    s_toast_until_ms = app_uptime_ms() + TOAST_MS;
    power_hold_awake_ms(TOAST_MS); /* a sleep would leave it on screen until the next wake */
    ESP_LOGI(TAG, "toast: %s", text);
    app_ui_render();
}

bool app_ui_toast_active(void)
{
    return s_toast[0] != '\0' && app_uptime_ms() < s_toast_until_ms;
}

int64_t app_ui_toast_until_ms(void)
{
    return s_toast[0] != '\0' ? s_toast_until_ms : 0;
}

void app_ui_toast_expire(void)
{
    if (s_toast[0] != '\0' && !app_ui_toast_active()) {
        s_toast[0] = '\0';
        app_ui_render();
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
        bool critical = battery_critical(s.critical, bat.smoothed_mv, bat.state); /* spec §8 */
        if (critical && !s.critical) {
            ESP_LOGW(TAG, "battery critical: %d mV", bat.smoothed_mv);
        } else if (!critical && s.critical) {
            ESP_LOGI(TAG, "battery recovered: %d mV", bat.smoothed_mv);
        }
        s.critical = critical;
    } else {
        ESP_LOGW(TAG, "battery: %s", esp_err_to_name(err));
    }
    ds_take_changes(&s.ds); /* every sample is followed by a render anyway */
}

esp_err_t app_ui_save_presets(void)
{
    size_t n = ui_presets_to_json(&s.presets, s_file, sizeof(s_file));
    if (n == 0) {
        ESP_LOGE(TAG, "presets.json does not fit %u bytes", (unsigned)sizeof(s_file));
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = storage_init(); /* not mounted yet after a routine deep-sleep wake */
    if (err == ESP_OK) {
        err = storage_write_atomic(STORAGE_PRESETS_PATH, s_file, n);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving presets: %s", esp_err_to_name(err));
    }
    return err;
}

static bool keep_text(const char *text, void *ctx)
{
    (void)text;
    (void)ctx;
    return true; /* storage_load left it in s_settings_base */
}

esp_err_t app_ui_save_settings(void)
{
    esp_err_t err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving settings: %s", esp_err_to_name(err));
        return err;
    }
    if (s_settings_base[0] == '\0') { /* a deep-sleep wake doesn't read the file: read it now */
        bool from_backup = false;
        if (storage_load(STORAGE_SETTINGS_PATH, s_settings_base, sizeof(s_settings_base), keep_text, NULL,
                         &from_backup) != ESP_OK) {
            s_settings_base[0] = '\0';
        }
    }
    static char out[sizeof(s_settings_base)];
    size_t n = settings_to_json(&s.settings, s_settings_base[0] ? s_settings_base : NULL, out, sizeof(out));
    if (n == 0) {
        ESP_LOGE(TAG, "settings.json does not fit %u bytes", (unsigned)sizeof(out));
        return ESP_ERR_INVALID_SIZE;
    }
    err = storage_write_atomic(STORAGE_SETTINGS_PATH, out, n);
    if (err == ESP_OK) {
        memcpy(s_settings_base, out, n + 1); /* the next save builds on what is now in the file */
    } else {
        ESP_LOGE(TAG, "saving settings: %s", esp_err_to_name(err));
    }
    return err;
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
        app_ui_save_presets();
    }
}

void app_ui_set_cycle(bool enabled)
{
    s.presets.cycle_enabled = enabled;
    s.cycle_at = enabled ? time(NULL) + s.presets.cycle_interval_s : 0;
    ESP_LOGI(TAG, "auto-cycle %s", enabled ? "on" : "off");
    app_ui_save_presets();
}

void app_ui_toggle_cycle(void)
{
    app_ui_set_cycle(!s.presets.cycle_enabled);
}

void app_ui_start_night(time_t until)
{
    s.night_until = until - until % 60;
    ESP_LOGI(TAG, "night until %lld", (long long)s.night_until);
}

void app_ui_end_night(void)
{
    s.night_until = 0;
    s.sched_checked = time(NULL); /* entries that fell inside the night don't run late */
    ESP_LOGI(TAG, "night over");
}

bool app_ui_night(void)
{
    return s.night_until != 0;
}

static bool schedule_runs(void)
{
    return s.presets.schedule.enabled && s.presets.schedule.count > 0 && timekeeping_valid() && s.night_until == 0;
}

/* Runs the entries that came due since the last check, in order; a night entry ends the run, as
 * the entries after it fall inside the night. */
static void run_schedule(time_t now)
{
    if (!schedule_runs()) {
        s.sched_checked = now;
        return;
    }
    if (s.sched_checked == 0 || s.sched_checked > now) {
        s.sched_checked = now; /* the first run, or the clock moved back: nothing is overdue */
        return;
    }
    int order[UI_SCHEDULE_MAX];
    int n = ui_schedule_due(&s.presets.schedule, s.sched_checked, now, order);
    for (int i = 0; i < n; i++) {
        const ui_schedule_entry_t *e = &s.presets.schedule.entries[order[i]];
        if (e->action == UI_SCHED_NIGHT) {
            char until[8], text[48];
            time_t end = sched_next_weekly(now, e->until_min, 0x7F);
            snprintf(until, sizeof(until), "%02d:%02d", e->until_min / 60, e->until_min % 60);
            snprintf(text, sizeof(text), "%s %s", lang_str(lang_get(s.settings.language), LS_T_NIGHT_UNTIL), until);
            app_ui_start_night(end);
            app_ui_toast(text);
            break;
        }
        ESP_LOGI(TAG, "schedule: preset %s", s.presets.presets[e->preset].id);
        app_ui_select(e->preset, false);
    }
    s.sched_checked = now;
}

void app_ui_tick(bool force)
{
    time_t now = time(NULL);
    if (s.night_until != 0 && now < s.night_until && display_asleep()) {
        return; /* night sleep: nothing is sampled or drawn (spec §9.1) */
    }
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
    run_schedule(now);
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
    int index = -1;
    sched_input_t in = {
        .now = now,
        .display_every_min = s.settings.display_every_min,
        .sensors_every_min = s.settings.sensors_every_min,
        .cycle_at = s.presets.cycle_enabled ? s.cycle_at : 0,
        .every_second = s.presets.presets[s.presets.active].seconds,
        .schedule_at = schedule_runs() ? ui_schedule_next(&s.presets.schedule, now, &index) : 0,
    };
    return scheduler_next_wake(&in);
}
```

```c
#include <stdio.h>
#include <string.h>

#include "app_internal.h"
#include "board_buttons.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lang.h"
#include "nvs.h"
#include "power.h"
#include "st7305.h"
#include "storage.h"
#include "timekeeping.h"
#include "timekeeping_zones.h"

/* The menu on the device (spec §5.7): builds the model from the app's state, carries out what the
 * menu asks for, and keeps the panel in HPM while it is open (spec §4.2). */

#define MENU_TIMEOUT_MS 60000 /* spec §5.7: the menu closes after 60 s without input */
#define ZONES_MAX       20

static const char *TAG = "app_menu";

/* Gesture timings (spec §5.6): the dashboard binds KEY double and a 3 s BOOT long press; the menu
 * binds neither, so KEY short answers at once. */
const gesture_config_t k_app_dashboard_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = true },
    [BOARD_BUTTON_BOOT] = { .long_ms = 3000, .double_enabled = false },
};
static const gesture_config_t k_menu_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = false },
    [BOARD_BUTTON_BOOT] = { .long_ms = 1000, .double_enabled = false },
};

static const uint16_t k_cycle_s[] = { 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
static const char *const k_cycle_labels[] = { "10 s", "15 s", "30 s", "1 min", "2 min",
                                              "5 min", "10 min", "15 min", "30 min", "1 h" };
#define CYCLE_COUNT ((int)(sizeof(k_cycle_s) / sizeof(k_cycle_s[0])))
static const uint8_t k_quarter_hz[] = { 1, 2, 4, 8, 16, 32 }; /* 0.25 to 8 Hz (D12) */
#define RATE_COUNT ((int)(sizeof(k_quarter_hz) / sizeof(k_quarter_hz[0])))
static const char *const k_units[] = { "°C", "°F" };
static const char *const k_languages[] = { "en", "cs" };
#define LANGUAGE_COUNT ((int)(sizeof(k_languages) / sizeof(k_languages[0])))

static ui_menu_t s_menu;
static ui_menu_model_t s_model;
static bool s_open;
static int64_t s_deadline_ms;
static const char *s_preset_names[UI_PRESET_MAX];
static const char *s_zone_names[ZONES_MAX];
static const char *s_language_names[LANGUAGE_COUNT];
static char s_rate_text[RATE_COUNT][12];
static const char *s_rates[RATE_COUNT];
static char s_info[5][80];

static const lang_t *lang(void)
{
    return lang_get(app_settings()->language);
}

/* "0,25 Hz", "1 Hz": the pack's decimal separator, no trailing zeros. */
static void rate_label(const lang_t *l, int quarter_hz, char *out, size_t size)
{
    char num[12];
    lang_format_decimal(l, quarter_hz * 25, 2, num, sizeof(num));
    size_t n = strlen(num);
    while (n > 1 && num[n - 1] == '0') {
        num[--n] = '\0';
    }
    if (n > 0 && num[n - 1] == l->decimal_sep) {
        num[--n] = '\0';
    }
    snprintf(out, size, "%s Hz", num);
}

static int nearest_index(const uint16_t *values, int count, int value)
{
    int best = 0;
    for (int i = 0; i < count; i++) {
        if (values[i] <= value) {
            best = i;
        }
    }
    return best;
}

static void format_uptime(const lang_t *l, int64_t s, char *out, size_t size)
{
    if (s >= 86400) {
        snprintf(out, size, "%lld %s %lld %s", (long long)(s / 86400), lang_str(l, LS_DAYS_UNIT),
                 (long long)(s % 86400 / 3600), lang_str(l, LS_HOURS_UNIT));
    } else if (s >= 3600) {
        snprintf(out, size, "%lld %s %lld %s", (long long)(s / 3600), lang_str(l, LS_HOURS_UNIT),
                 (long long)(s % 3600 / 60), lang_str(l, LS_MINUTES_UNIT));
    } else {
        snprintf(out, size, "%lld %s", (long long)(s / 60), lang_str(l, LS_MINUTES_UNIT));
    }
}

static int zone_list(int *current)
{
    const settings_t *set = app_settings();
    int count = 0;
    const timekeeping_zone_t *zones = timekeeping_zones(&count);
    for (int i = 0; i < count && i < ZONES_MAX; i++) {
        s_zone_names[i] = zones[i].iana;
    }
    *current = timekeeping_zone_find(set->tz_iana);
    if (*current < 0 && count < ZONES_MAX) { /* a zone set elsewhere (the web UI, M4) stays selectable */
        s_zone_names[count] = set->tz_iana;
        *current = count++;
    }
    return count;
}

static void build_model(void)
{
    const app_ui_state_t *st = app_state();
    const settings_t *set = &st->settings;
    const lang_t *l = lang();
    ui_menu_model_t *m = &s_model;
    memset(m, 0, sizeof(*m));

    for (int i = 0; i < st->presets.count; i++) {
        s_preset_names[i] = st->presets.presets[i].name;
    }
    m->choices[UI_MI_ACTIVE_PRESET] = s_preset_names;
    m->choice_count[UI_MI_ACTIVE_PRESET] = st->presets.count;
    m->value[UI_MI_ACTIVE_PRESET] = st->presets.active;
    m->value[UI_MI_AUTO_CYCLE] = st->presets.cycle_enabled;
    m->choices[UI_MI_CYCLE_INTERVAL] = k_cycle_labels;
    m->choice_count[UI_MI_CYCLE_INTERVAL] = CYCLE_COUNT;
    m->value[UI_MI_CYCLE_INTERVAL] = nearest_index(k_cycle_s, CYCLE_COUNT, st->presets.cycle_interval_s);
    m->value[UI_MI_SCHEDULE] = st->presets.schedule.enabled;

    time_t now = time(NULL);
    localtime_r(&now, &m->local);
    m->value[UI_MI_CLOCK_24H] = set->clock_24h;
    int zone = -1;
    m->choice_count[UI_MI_TIME_ZONE] = (uint8_t)zone_list(&zone);
    m->choices[UI_MI_TIME_ZONE] = s_zone_names;
    m->value[UI_MI_TIME_ZONE] = zone;

    m->value[UI_MI_UPDATE_INTERVAL] = set->display_every_min;
    int rate = 0;
    for (int i = 0; i < RATE_COUNT; i++) {
        rate_label(l, k_quarter_hz[i], s_rate_text[i], sizeof(s_rate_text[i]));
        s_rates[i] = s_rate_text[i];
        if (k_quarter_hz[i] <= set->lpm_quarter_hz) {
            rate = i;
        }
    }
    m->choices[UI_MI_REFRESH_RATE] = s_rates;
    m->choice_count[UI_MI_REFRESH_RATE] = RATE_COUNT;
    m->value[UI_MI_REFRESH_RATE] = rate;

    m->value[UI_MI_TEMP_OFFSET] = set->temp_offset_c100 / 10;
    m->value[UI_MI_HUM_OFFSET] = set->hum_offset_pct100 / 10;
    m->choices[UI_MI_UNITS] = k_units;
    m->choice_count[UI_MI_UNITS] = 2;
    m->value[UI_MI_UNITS] = set->fahrenheit;

    for (int i = 0; i < LANGUAGE_COUNT; i++) {
        s_language_names[i] = lang_get(k_languages[i])->name;
        if (strcmp(set->language, k_languages[i]) == 0) {
            m->value[UI_MI_LANGUAGE] = i;
        }
    }
    m->choices[UI_MI_LANGUAGE] = s_language_names;
    m->choice_count[UI_MI_LANGUAGE] = LANGUAGE_COUNT;

    ui_context_t ctx;
    app_ui_context(&ctx);
    ui_value_t bat;
    ui_resolve(&ctx, UI_FIELD_BAT_LEVEL, &bat);
    if (bat.state == UI_VALUE_MISSING) {
        snprintf(s_info[0], sizeof(s_info[0]), "\xE2\x80\x94");
    } else {
        snprintf(s_info[0], sizeof(s_info[0]), "%s %% \xC2\xB7 %s", bat.text, bat.extra);
    }
    char sha[8];
    esp_app_get_elf_sha256(sha, sizeof(sha));
    snprintf(s_info[1], sizeof(s_info[1]), "%.24s (%s)", esp_app_get_description()->version, sha);
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA); /* the device id (spec §5.5) */
    snprintf(s_info[2], sizeof(s_info[2]), "reflbo-%02x%02x", mac[4], mac[5]);
    if (st->cold_boot_at != 0 && timekeeping_valid() && now >= st->cold_boot_at) {
        format_uptime(l, now - st->cold_boot_at, s_info[3], sizeof(s_info[3]));
    } else {
        snprintf(s_info[3], sizeof(s_info[3]), "\xE2\x80\x94");
    }
    char mb[12];
    lang_format_decimal(l, (long)(esp_get_free_heap_size() / (1024 * 1024 / 10)), 1, mb, sizeof(mb));
    snprintf(s_info[4], sizeof(s_info[4]), "%s MB", mb);
    const ui_menu_item_t info_items[] = { UI_MI_INFO_BATTERY, UI_MI_INFO_FIRMWARE, UI_MI_INFO_DEVICE,
                                          UI_MI_INFO_UPTIME, UI_MI_INFO_MEMORY };
    for (int i = 0; i < 5; i++) {
        m->info[info_items[i]] = s_info[i];
    }
}

/* Shows `message` over the dashboard for a moment, then restarts the chip. */
static void restart_after(lang_str_t message)
{
    app_menu_close();
    app_ui_toast(lang_str(lang(), message));
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

/* Spec §14.4: erases storage and the NVS namespaces wifi, secrets and ctr; keeps sys. */
static void factory_reset(void)
{
    app_menu_close();
    app_ui_toast(lang_str(lang(), LS_T_RESETTING));
    esp_err_t err = storage_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erasing storage: %s", esp_err_to_name(err));
    }
    static const char *const k_namespaces[] = { "wifi", "secrets", "ctr" };
    for (size_t i = 0; i < sizeof(k_namespaces) / sizeof(k_namespaces[0]); i++) {
        nvs_handle_t nvs;
        if (nvs_open(k_namespaces[i], NVS_READONLY, &nvs) != ESP_OK) {
            continue; /* never written: nothing to erase */
        }
        nvs_close(nvs);
        if (nvs_open(k_namespaces[i], NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_erase_all(nvs);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
    ESP_LOGW(TAG, "factory reset done; restarting");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static void set_zone(int index)
{
    int count = 0;
    const timekeeping_zone_t *zones = timekeeping_zones(&count);
    if (index < 0 || index >= count) {
        return; /* the zone set elsewhere: unchanged */
    }
    settings_t *set = &app_state()->settings;
    snprintf(set->tz_iana, sizeof(set->tz_iana), "%s", zones[index].iana);
    snprintf(set->tz_posix, sizeof(set->tz_posix), "%s", zones[index].posix);
}

static void apply(const ui_menu_intent_t *in)
{
    app_ui_state_t *st = app_state();
    settings_t *set = &st->settings;
    bool save_settings = false, save_presets = false, apply_settings = false, resample = false, retime = false;
    switch (in->kind) {
    case UI_MENU_SET:
        switch (in->item) {
        case UI_MI_ACTIVE_PRESET:
            app_ui_select(in->value, true);
            break;
        case UI_MI_AUTO_CYCLE:
            app_ui_set_cycle(in->value != 0);
            break;
        case UI_MI_CYCLE_INTERVAL:
            st->presets.cycle_interval_s = k_cycle_s[in->value < CYCLE_COUNT ? in->value : 0];
            if (st->presets.cycle_enabled) {
                st->cycle_at = time(NULL) + st->presets.cycle_interval_s;
            }
            save_presets = true;
            break;
        case UI_MI_SCHEDULE:
            st->presets.schedule.enabled = in->value != 0;
            st->sched_checked = time(NULL);
            save_presets = true;
            break;
        case UI_MI_CLOCK_24H:
            set->clock_24h = in->value != 0;
            save_settings = true;
            break;
        case UI_MI_TIME_ZONE:
            set_zone(in->value);
            apply_settings = save_settings = retime = true;
            break;
        case UI_MI_UPDATE_INTERVAL:
            set->display_every_min = (uint8_t)in->value;
            save_settings = retime = true;
            break;
        case UI_MI_REFRESH_RATE:
            set->lpm_quarter_hz = k_quarter_hz[in->value < RATE_COUNT ? in->value : 2];
            apply_settings = save_settings = true;
            break;
        case UI_MI_TEMP_OFFSET:
            set->temp_offset_c100 = (int16_t)(in->value * 10);
            apply_settings = save_settings = resample = true;
            break;
        case UI_MI_HUM_OFFSET:
            set->hum_offset_pct100 = (int16_t)(in->value * 10);
            apply_settings = save_settings = resample = true;
            break;
        case UI_MI_UNITS:
            set->fahrenheit = in->value == 1;
            save_settings = true;
            break;
        case UI_MI_LANGUAGE:
            snprintf(set->language, sizeof(set->language), "%s",
                     k_languages[in->value < LANGUAGE_COUNT ? in->value : 0]);
            save_settings = true;
            break;
        default:
            break;
        }
        break;
    case UI_MENU_SET_TIME: {
        time_t before = time(NULL);
        time_t utc = sched_local_to_utc(in->local.tm_year + 1900, in->local.tm_mon + 1, in->local.tm_mday,
                                        in->local.tm_hour * 60 + in->local.tm_min);
        esp_err_t err = timekeeping_set_utc(utc);
        if (err == ESP_OK) {
            app_clock_moved((int64_t)utc - (int64_t)before);
        } else {
            ESP_LOGE(TAG, "setting the time: %s", esp_err_to_name(err));
        }
        break;
    }
    case UI_MENU_ACTION:
        if (in->item == UI_MI_REBOOT) {
            restart_after(LS_T_REBOOTING);
        } else if (in->item == UI_MI_FACTORY_RESET) {
            factory_reset();
        }
        break;
    case UI_MENU_CLOSE:
        app_menu_close();
        return;
    default:
        break;
    }
    if (apply_settings) {
        app_ui_apply_settings();
    }
    if (resample) {
        app_ui_sample(time(NULL));
    }
    if (save_settings) {
        app_ui_save_settings();
    }
    if (save_presets) {
        app_ui_save_presets();
    }
    if (retime) {
        app_clock_moved(0); /* new slots or a new zone: schedule the wakes again */
    }
}

void app_menu_open(void)
{
    if (s_open) {
        return;
    }
    ui_menu_open(&s_menu);
    s_open = true;
    s_deadline_ms = app_uptime_ms() + MENU_TIMEOUT_MS;
    power_hold_awake_ms(MENU_TIMEOUT_MS);
    board_buttons_set_config(k_menu_buttons);
    esp_err_t err = st7305_set_mode(ST7305_MODE_HPM); /* new frames appear at once (spec §4.2) */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HPM: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "menu open");
    app_menu_render();
}

void app_menu_close(void)
{
    if (!s_open) {
        return;
    }
    s_open = false;
    board_buttons_set_config(k_app_dashboard_buttons);
    esp_err_t err = st7305_set_mode(ST7305_MODE_LPM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LPM: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "menu closed");
    app_ui_render();
}

bool app_menu_is_open(void)
{
    return s_open;
}

int64_t app_menu_deadline_ms(void)
{
    return s_open ? s_deadline_ms : 0;
}

void app_menu_key(ui_menu_key_t key)
{
    if (!s_open) {
        return;
    }
    s_deadline_ms = app_uptime_ms() + MENU_TIMEOUT_MS;
    power_hold_awake_ms(MENU_TIMEOUT_MS);
    build_model();
    ui_menu_intent_t in = ui_menu_input(&s_menu, &s_model, key);
    apply(&in);
    if (s_open) {
        app_menu_render();
    }
}

void app_menu_render(void)
{
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        return;
    }
    build_model();
    ui_draw_menu(fb, &s_menu, &s_model, lang());
    esp_err_t err = display_commit(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display: %s", esp_err_to_name(err));
    }
}
```

```c
#include "app.h"

#include <sys/time.h>
#include <time.h>

#include "app_internal.h"

#include "board.h"
#include "board_buttons.h"
#include "board_pins.h"
#include "diag.h"
#include "lang.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_timer.h"
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
#include "ui_screens.h"
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
#define SNAP_VERSION      3
#define PEEK_MS           60000 /* a button during the night shows the dashboard this long (spec §9.1) */
#define NIGHT_RECHECK_S   60    /* a night sleep with a button held looks again this often (D16) */
#define CRITICAL_RECHECK_S 600  /* the critical sleep checks again this often if KEY is held */

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
_Static_assert(sizeof(app_snapshot_t) <= 4096, "the RTC-RAM snapshot is at most 4 KB (spec §6)");

static RTC_DATA_ATTR app_snapshot_t s_snap;
static QueueHandle_t s_queue;
static time_t s_next_alarm; /* the RTC alarm: the next minute slot */
static time_t s_wake_at;    /* the earliest wake: the alarm, or a cycle switch or seconds tick before it */
static int64_t s_peek_until_ms; /* night: the dashboard shows until then (app_uptime_ms) */

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

int64_t app_uptime_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* A scheduled wake: the RTC alarm, its backup, a cycle switch or a seconds tick. `force` also
 * samples and renders. */
static void on_tick(bool force)
{
    esp_err_t err = timekeeping_load_from_rtc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC read: %s", esp_err_to_name(err));
    }
    time_t now = time(NULL);
    if (now >= s_next_alarm) {
        s_next_alarm = 0; /* fired: program the next one, even if it is the same minute again */
    }
    if (app_ui_night() && now >= app_state()->night_until) { /* spec §9.1: the dashboard is back */
        app_ui_end_night();
        err = display_wake();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "panel wake: %s", esp_err_to_name(err));
        }
        force = true;
    }
    app_ui_tick(force);
    schedule_next();
}

void app_clock_moved(int64_t delta_s)
{
    sensors_shift_time(delta_s); /* the battery history keeps its spacing on the new clock */
    app_state()->sched_checked = time(NULL); /* entries the jump skipped don't run late */
    app_state()->cycle_at = 0; /* the next tick starts the cycle interval again, rather than switching at once */
    on_tick(true); /* show the new time now, not at the next slot */
}

static const char *preset_name(void)
{
    const ui_presets_t *p = app_presets();
    return p->presets[p->active].name;
}

/* Dashboard bindings (spec §5.6); the menu has its own while it is open. */
static void handle_button(board_button_t button, gesture_t gesture)
{
    power_hold_awake_ms(GRACE_MS);
    if (app_ui_night()) { /* any press keeps the night's dashboard up, awake, as the peek lives in RAM */
        s_peek_until_ms = app_uptime_ms() + PEEK_MS;
        power_hold_awake_ms(PEEK_MS);
    }
    const lang_t *lang = lang_get(app_settings()->language);
    char text[64];
    if (app_menu_is_open()) {
        bool held = gesture == GESTURE_LONG;
        app_menu_key(button == BOARD_BUTTON_KEY ? (held ? UI_MENU_KEY_SELECT : UI_MENU_KEY_NEXT)
                                                : (held ? UI_MENU_KEY_EXIT : UI_MENU_KEY_BACK));
    } else if (app_state()->critical) {
        app_ui_sample(time(NULL)); /* only a recovered battery leaves this screen (spec §8) */
        app_ui_render();
    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_SHORT) {
        app_ui_select(ui_presets_next(app_presets()), true);
        snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_PRESET), preset_name());
        app_ui_toast(text);
    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_DOUBLE) {
        app_ui_toggle_cycle();
        app_ui_toast(lang_str(lang, app_presets()->cycle_enabled ? LS_T_CYCLE_ON : LS_T_CYCLE_OFF));
    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_LONG) {
        app_menu_open();
    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT) {
        app_ui_sample(time(NULL));
        app_ui_render();
        ESP_LOGI(TAG, "BOOT short: sensors refreshed");
    } else {
        ESP_LOGI(TAG, "%s %s is not bound yet (config mode comes in M4)", board_button_name(button),
                 board_gesture_name(gesture));
    }
    schedule_next();
}

/* A button held since before the sleep (D16) makes no gesture until it has been released. */
static void ignore_held_buttons(void)
{
    unsigned masked = power_masked_buttons();
    if (masked & POWER_BUTTON_KEY) {
        board_buttons_ignore_until_released(BOARD_BUTTON_KEY);
    }
    if (masked & POWER_BUTTON_BOOT) {
        board_buttons_ignore_until_released(BOARD_BUTTON_BOOT);
    }
}

/* A button woke the chip in the night: the panel wakes and shows the dashboard for a while; the
 * press itself does nothing else (spec §9.1). */
static void night_peek(board_button_t button)
{
    board_buttons_ignore_until_released(button);
    esp_err_t err = display_wake();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel wake: %s", esp_err_to_name(err));
    }
    s_peek_until_ms = app_uptime_ms() + PEEK_MS;
    power_hold_awake_ms(PEEK_MS);
    on_tick(true);
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
            app_clock_moved(moved / 1000); /* `rtc set` and friends */
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
        if (display_asleep()) { /* a night that had to sleep light: the press only wakes the panel */
            night_peek(wake == POWER_WAKE_KEY ? BOARD_BUTTON_KEY : BOARD_BUTTON_BOOT);
            break;
        }
        board_buttons_resync(); /* edges during sleep raised no interrupt */
        power_hold_awake_ms(GRACE_MS);
        break;
    default:
        break;
    }
    ignore_held_buttons();
}

/* Seals the RTC-RAM snapshot and holds the panel pins; false if the holds failed, in which case
 * the chip must not deep-sleep (the panel would reset). */
static bool prepare_deep_sleep(void)
{
    sensors_export(&s_snap.sensors);
    display_export(&s_snap.display);
    app_ui_export(&s_snap.ui);
    s_snap.next_alarm = s_next_alarm;
    util_snapshot_seal(&s_snap, sizeof(s_snap), SNAP_MAGIC, SNAP_VERSION);
    esp_err_t err = display_prepare_deep_sleep();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel pins: %s; light sleep this time", esp_err_to_name(err));
        display_cancel_deep_sleep();
        s_snap.hdr.magic = 0;
        return false;
    }
    return true;
}

/* Deep sleep until `wake` (the timer) or the RTC alarm or a button. */
static void enter_deep_sleep(time_t wake)
{
    if (!prepare_deep_sleep()) {
        handle_wake(power_sleep_light(wake));
        return;
    }
    power_sleep_deep(wake);
}

/* Night sleep (spec §9.1): the panel sleeps, and the chip deep-sleeps whatever the idle strategy
 * and even while tethered, until the end time or a button. */
static void enter_night_sleep(void)
{
    time_t until = app_state()->night_until;
    esp_err_t err = display_sleep();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel sleep: %s", esp_err_to_name(err));
    }
    s_next_alarm = until;
    s_wake_at = until;
    err = pcf85063_set_alarm(until); /* also clears the alarm flag */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC alarm: %s; the backup timer takes over", esp_err_to_name(err));
    }
    time_t wake = until + BACKUP_S;
    if (board_buttons_pressed(BOARD_BUTTON_KEY) || board_buttons_pressed(BOARD_BUTTON_BOOT)) {
        /* D16 leaves a held button out of this sleep's wake sources: look again soon, so that
         * once it is released, a press can still show the dashboard */
        time_t recheck = time(NULL) + NIGHT_RECHECK_S;
        wake = recheck < wake ? recheck : wake;
    }
    enter_deep_sleep(wake);
}

/* Spec §8: the critical screen stays up, and only KEY wakes the chip to check the battery again. */
static void enter_critical_sleep(void)
{
    app_ui_render();
    if (!prepare_deep_sleep()) {
        handle_wake(power_sleep_light(time(NULL) + CRITICAL_RECHECK_S));
        return;
    }
    power_sleep_critical(CRITICAL_RECHECK_S);
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
        /* Woke from deep sleep without a valid snapshot: the panel still runs, don't reset it. It
         * may have been asleep for the night, so wake it anyway (SLPOUT is harmless otherwise). */
        display_state_t fallback = { .variant = PANEL_VARIANT, .mode = ST7305_MODE_LPM, .lpm_rate = ST7305_LPM_1HZ,
                                     .asleep = true };
        ESP_RETURN_ON_ERROR(display_init_warm(&fallback), TAG, "display");
        st7305_set_lpm_rate(ST7305_LPM_1HZ); /* assumed, so send it; safe without a reset */
        err = display_wake();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "panel wake: %s", esp_err_to_name(err));
        }
    } else {
        ESP_RETURN_ON_ERROR(display_init(PANEL_VARIANT), TAG, "display");
    }
    app_ui_apply_settings(); /* offsets, freshness and the panel rate, now that the panel is up */
    ESP_RETURN_ON_ERROR(board_buttons_start(on_button, k_app_dashboard_buttons), TAG, "buttons");
    ESP_RETURN_ON_ERROR(start_rtc_int(), TAG, "RTC INT");
    ignore_held_buttons();

    bool button_wake = wake == POWER_WAKE_KEY || wake == POWER_WAKE_BOOT;
    board_button_t woke_by = wake == POWER_WAKE_KEY ? BOARD_BUTTON_KEY : BOARD_BUTTON_BOOT;
    if (wake == POWER_WAKE_COLD || button_wake) {
        power_hold_awake_ms(GRACE_MS);
    }
    if (wake == POWER_WAKE_TIMER && time(NULL) >= s_next_alarm + BACKUP_S) {
        ESP_LOGW(TAG, "RTC alarm missed; backup wake");
    }
    if (button_wake && app_ui_night() && time(NULL) < app_state()->night_until) {
        night_peek(woke_by);
    } else {
        if (button_wake && app_state()->critical) {
            board_buttons_ignore_until_released(woke_by); /* it only asks for a battery check */
        } else if (button_wake) {
            board_buttons_woke(woke_by);
        }
        on_tick(!warm || app_state()->critical);
    }
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
        bool pending = uxQueueMessagesWaiting(s_queue) > 0 || board_buttons_busy();
        if (err == ESP_OK) {
            check_clock_jump();
            int64_t mono = app_uptime_ms();
            if (app_menu_is_open() && mono >= app_menu_deadline_ms()) {
                app_menu_close(); /* 60 s without input (spec §5.7) */
            }
            app_ui_toast_expire();
            bool busy = pending || app_menu_is_open() || app_ui_toast_active();
            if (!busy && app_ui_night() && mono >= s_peek_until_ms) {
                enter_night_sleep(); /* returns only if it had to sleep light instead */
                continue;
            }
            if (!busy && app_state()->critical) {
                enter_critical_sleep();
                continue;
            }
        }
        switch (power_plan(pending)) {
        case POWER_PLAN_LIGHT:
            handle_wake(power_sleep_light(sleep_until()));
            continue;
        case POWER_PLAN_DEEP:
            enter_deep_sleep(sleep_until()); /* returns only if it had to sleep light instead */
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
            int64_t mono = app_uptime_ms();
            const int64_t deadlines[] = { app_menu_deadline_ms(), app_ui_toast_until_ms(),
                                          app_ui_night() ? s_peek_until_ms : 0 };
            for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++) {
                if (deadlines[i] != 0 && deadlines[i] - mono < wait_ms) {
                    wait_ms = deadlines[i] - mono;
                }
            }
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

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_internal.h"
#include "diag.h"
#include "esp_console.h"
#include "esp_log.h"
#include "lang.h"
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
        if (end == argv[3] || *end != '\0' || !(value >= -100000.0 && value <= 100000.0)) { /* NaN and inf too */
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

/* `night <minutes>` (spec §9.1): night sleep now, for measuring what it saves. */
static int night_body(int argc, char **argv)
{
    static const char *const k_usage = "night <minutes, 1-1440>";
    char *end;
    long minutes = argc == 2 ? strtol(argv[1], &end, 10) : 0;
    if (argc != 2 || *end != '\0' || minutes < 1 || minutes > 1440) {
        return usage(k_usage);
    }
    time_t until = time(NULL) + minutes * 60;
    until += (60 - until % 60) % 60; /* the RTC alarm fires on whole minutes */
    struct tm local;
    localtime_r(&until, &local);
    char text[48];
    snprintf(text, sizeof(text), "%s %02d:%02d", lang_str(lang_get(app_settings()->language), LS_T_NIGHT_UNTIL),
             local.tm_hour, local.tm_min);
    app_ui_start_night(until);
    app_ui_toast(text);
    printf("night: until %02d:%02d local; the board sleeps in 3 s, even while a PC is attached, and the console "
           "drops\n",
           local.tm_hour, local.tm_min);
    return 0;
}

static int cmd_night(int argc, char **argv)
{
    return diag_on_owner(night_body, argc, argv);
}

/* "22:30" -> minutes after midnight, or -1. */
static int parse_hhmm(const char *s)
{
    int h, m;
    char tail;
    if (strlen(s) != 5 || sscanf(s, "%2d:%2d%c", &h, &m, &tail) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return -1;
    }
    return h * 60 + m;
}

static void print_schedule(const ui_presets_t *p)
{
    printf("schedule %s, %d of %d entries\n", p->schedule.enabled ? "on" : "off", p->schedule.count, UI_SCHEDULE_MAX);
    for (int i = 0; i < p->schedule.count; i++) {
        const ui_schedule_entry_t *e = &p->schedule.entries[i];
        printf("%d: %02d:%02d days 0x%02x ", i + 1, e->at_min / 60, e->at_min % 60, e->days);
        if (e->action == UI_SCHED_NIGHT) {
            printf("night until %02d:%02d\n", e->until_min / 60, e->until_min % 60);
        } else {
            printf("preset %s\n", p->presets[e->preset].id);
        }
    }
}

/* `schedule` (spec §5.7: until the M4 web UI, presets.json or the console sets the entries). */
static int schedule_body(int argc, char **argv)
{
    static const char *const k_usage = "schedule list | on | off | clear | add <HH:MM> preset <id> [days] | "
                                       "add <HH:MM> night <HH:MM> [days]  (days: bit 0 Monday, default 127)";
    ui_presets_t *p = app_presets();
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        print_schedule(p);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "off") == 0)) {
        p->schedule.enabled = strcmp(argv[1], "on") == 0;
        app_state()->sched_checked = time(NULL);
    } else if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        p->schedule.count = 0;
    } else if ((argc == 5 || argc == 6) && strcmp(argv[1], "add") == 0) {
        if (p->schedule.count >= UI_SCHEDULE_MAX) {
            printf("schedule: full (%d entries)\n", UI_SCHEDULE_MAX);
            return 1;
        }
        ui_schedule_entry_t e = { .days = 0x7F };
        int at = parse_hhmm(argv[2]);
        char *end = NULL;
        long days = argc == 6 ? strtol(argv[5], &end, 0) : 0x7F;
        if (at < 0 || (argc == 6 && (*end != '\0' || days < 0 || days > 0x7F))) {
            return usage(k_usage);
        }
        e.at_min = (uint16_t)at;
        e.days = (uint8_t)days;
        if (strcmp(argv[3], "preset") == 0) {
            int index = ui_presets_find(p, argv[4]);
            if (index < 0) {
                printf("schedule: no preset \"%s\" (see `preset list`)\n", argv[4]);
                return 1;
            }
            e.action = UI_SCHED_PRESET;
            e.preset = (uint8_t)index;
        } else if (strcmp(argv[3], "night") == 0 && parse_hhmm(argv[4]) >= 0) {
            if (parse_hhmm(argv[4]) == at) {
                printf("schedule: a night must end at another time than it starts\n");
                return 1;
            }
            e.action = UI_SCHED_NIGHT;
            e.until_min = (uint16_t)parse_hhmm(argv[4]);
        } else {
            return usage(k_usage);
        }
        p->schedule.entries[p->schedule.count++] = e;
    } else {
        return usage(k_usage);
    }
    if (app_ui_save_presets() != ESP_OK) {
        printf("schedule: changed for this session, not saved\n");
    }
    print_schedule(p);
    return 0;
}

static int cmd_schedule(int argc, char **argv)
{
    return diag_on_owner(schedule_body, argc, argv);
}

void app_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "field", .help = "field list | get <id> | set <id> <value> | clear <id>", .func = &cmd_field },
        { .command = "preset", .help = "preset list | set <id>", .func = &cmd_preset },
        { .command = "night", .help = "night <minutes>: night sleep now (spec §9.1)", .func = &cmd_night },
        { .command = "schedule", .help = "schedule list | on | off | clear | add <HH:MM> preset <id> [days] | "
                                         "add <HH:MM> night <HH:MM> [days]", .func = &cmd_schedule },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: %s", cmds[i].command, esp_err_to_name(err));
        }
    }
}
```

```diff
diff --git a/main/CMakeLists.txt b/main/CMakeLists.txt
index ce1093f..d4ee902 100644
--- a/main/CMakeLists.txt
+++ b/main/CMakeLists.txt
@@ -1,5 +1,5 @@
-idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_cmds.c"
+idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_menu.c" "app_cmds.c"
                        INCLUDE_DIRS "."
-                       PRIV_REQUIRES board console datastore diag display esp_app_format esp_driver_gpio espcoredump
-                                     gfx locale nvs_flash power rtc scheduler sensors st7305 storage timekeeping ui
-                                     util)
+                       PRIV_REQUIRES board console datastore diag display esp_app_format esp_driver_gpio esp_timer
+                                     espcoredump gfx locale nvs_flash power rtc scheduler sensors st7305 storage
+                                     timekeeping ui util)
```

- [ ] **Step 2: Build.**

Run: `tools/idf.sh build && ctest --test-dir build-host --output-on-failure`
Expected: the firmware builds without warnings; the suite passes 33 of 33.

- [ ] **Step 3: Commit.**

```bash
git add main
git commit -m "feat(app): add the menu, toasts, schedule, night sleep and the critical-battery screen"
```

- [ ] **Step 4: Check on the board (M3b acceptance, level 3).** Confirm the port as in Task 10, flash, capture the cold boot and set the clock:

```bash
tools/idf.sh -p /dev/cu.usbmodem2101 flash
tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/m3b-boot.log
tools/idf.sh exec python tools/devlog.py --cmd "rtc set $(date -u +%Y-%m-%dT%H:%M:%SZ)" --cmd "power idle" \
  --cmd "preset set home" --cmd "schedule list"
grep -E "storage:|app_ui:|reflbo ready|(^|reflbo> )[EW] \(" captures/m3b-boot.log
```

Expected:
- `storage: LittleFS: …`.
- For both config files, `… loaded` or `…: not there yet; using defaults`.
- `reflbo ready (cold wake)`, and no `E` or `W` lines.
- `schedule off, 0 of 8 entries`, unless the file holds some.
- Note the idle strategy `power idle` prints.

**Toasts.** Take the first screenshot straight away:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "btn key short"
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-toast.png
sleep 4
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-toast-gone.png
```

Expected:
- `app_ui: preset indoor` and `app_ui: toast: Preset: Indoor`.
- The first screenshot shows the black toast box near the bottom; the second shows the Indoor grid without it.

**The menu.** Open it, then edit the temperature offset to −0.5 °C:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "btn key long"
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-menu-root.png
tools/idf.sh exec python tools/devlog.py --cmd "panel status"
tools/idf.sh exec python tools/devlog.py --cmd "btn key short" --cmd "btn key short" --cmd "btn key short" \
  --cmd "btn key long" --cmd "btn key long" --cmd "btn boot short" --cmd "btn boot short" --cmd "btn boot short" \
  --cmd "btn boot short" --cmd "btn boot short" --cmd "btn key long"
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-menu-offset.png
```

Expected:
- `app_menu: menu open`.
- `panel factory, mode hpm, …`. A `btn` gesture reaches the app through the buttons task, so a command sent in the same devlog call can run before the menu has opened; hence the separate call.
- The root screenshot lists Presets, Time, Display, Sensors, Info and System, with Presets inverted.
- The second screenshot shows Sensors with the temperature offset at "-0.5 °C".
- The log shows a LittleFS line as `settings.json` is saved.

The offset survives a reset:

```bash
tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/m3b-reboot.log
tools/idf.sh exec python tools/devlog.py --cmd "btn key long" --cmd "btn key short" --cmd "btn key short" \
  --cmd "btn key short" --cmd "btn key long"
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-menu-offset-kept.png
```

Expected: `/fs/cfg/settings.json loaded`, and the screenshot again shows −0.5 °C.

Set it back, and switch the language to Czech from the menu:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "btn key long" --cmd "btn key short" --cmd "btn key short" \
  --cmd "btn key short" --cmd "btn key short" --cmd "btn key short" --cmd "btn key long" --cmd "btn boot short" \
  --cmd "btn key short" --cmd "btn key short" --cmd "btn key long" --cmd "btn key long" --cmd "btn key short" \
  --cmd "btn key long"
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-menu-cs.png
tools/idf.sh exec python tools/devlog.py --cmd "btn boot long"
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-dash-cs.png
tools/idf.sh exec python tools/devlog.py --cmd "panel status"
```

The sequence runs:
- the offset back up to 0.0, saved;
- BOOT short, up to the root;
- two KEY shorts to System;
- open it;
- edit Language;
- KEY short to Čeština;
- save.

Expected:
- The menu redraws in Czech at once: "Systém", "Jazyk" and "Čeština".
- After BOOT long: `app_menu: menu closed` and `mode lpm`.
- The dashboard is in Czech: the date as "Úterý 29. září" (today's), with decimal commas.

Switch back: `btn key long`, five `btn key short`, `btn key long`, `btn key long`, `btn key short`, `btn key long`, `btn boot long`. The dashboard is English again.

The menu closes by itself:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "btn key long" --until "menu closed" -t 75
```

Expected: devlog exits 0 about 60 s after `menu open`.

**The schedule and a night**:
- a preset and a night in the same minute, the night listed first;
- a night that ends when it starts, which is refused.

```bash
at=$(date -v+2M +%H:%M); until=$(date -v+4M +%H:%M)
tools/idf.sh exec python tools/devlog.py --cmd "schedule add $at night $at" --cmd "schedule add $at night $until" \
  --cmd "schedule add $at preset focus" --cmd "schedule on" -o captures/m3b-schedule-add.log
tools/idf.sh exec python tools/devlog.py --until "night until" -t 200 -o captures/m3b-schedule.log
```

Wait until the port has gone and come back (about two minutes), then:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "panel status" --cmd "sleep stats" \
  -o captures/m3b-night-end.log
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-night-end.png
```

Expected:
- The first `add` prints `schedule: a night must end at another time than it starts`.
- The other two are listed.
- At the minute: `app_ui: schedule: preset focus`, then `app_ui: night until …` and the toast `Night until HH:MM`.
- The port drops about 3 s later and returns at the end time.
- After the night:
  - `* focus`;
  - `panel factory, mode lpm, lpm rate 1 Hz`, without `asleep`;
  - `sleep stats` with a new deep sleep and an `rtc` wake;
  - the screenshot is the Focus clock at the current time.

A clock jump doesn't run the entries it skipped:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "schedule clear" --cmd "schedule add $(date -v+2M +%H:%M) preset weather" \
  --cmd "rtc set $(date -u -v+10M +%Y-%m-%dT%H:%M:%SZ)" -t 30 -o captures/m3b-jump.log
grep -c "schedule: preset" captures/m3b-jump.log
tools/idf.sh exec python tools/devlog.py --cmd "rtc set $(date -u +%Y-%m-%dT%H:%M:%SZ)" --cmd "schedule clear" \
  --cmd "schedule off" --cmd "preset set home"
```

Expected: `grep -c` prints `0`, and the clock is back.

**A console night** (`night 1`): the port drops about 3 s after the command and returns one to two minutes later, on the next whole minute after it. `sleep stats` then counts one more deep sleep with an `rtc` wake, and `panel status` shows `mode lpm, lpm rate 1 Hz`.

**Factory reset. Ask the owner first**: it erases this board's settings and presets (AGENTS quick rule 3). Only if they agree:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "btn key long" --cmd "btn key short" --cmd "btn key short" \
  --cmd "btn key short" --cmd "btn key short" --cmd "btn key short" --cmd "btn key long" --cmd "btn key short" \
  --cmd "btn key short" --cmd "btn key long"
tools/idf.sh exec python tools/screenshot.py -o captures/m3b-reset-ask.png
tools/idf.sh exec python tools/devlog.py --cmd "btn key long" --until "reflbo ready" -t 30 -o captures/m3b-reset.log
tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "power idle"
```

Expected:
- The screenshot asks the reset question, with "Hold KEY to confirm · BOOT cancels".
- The log shows `factory reset done; restarting`.
- The next boot says `not there yet; using defaults` for both files.
- `preset list` shows the four built-ins, with `* home`.
- `power idle` prints the same strategy as before (`sys` is kept).

If the owner declines, skip it and say so in the report.

Finally, restore what the checks changed:
- English;
- a 0.0 offset;
- `preset set home`;
- the schedule cleared and off;
- the idle strategy noted at the start.

---


### Task 13: Documentation

**Files:**
- Modify: `AGENTS.md`, `docs/specs/2026-09-25-firmware-design.md`, `docs/power.md`

**Interfaces:** none. This task records what Tasks 1–12 built, as AGENTS.md rule 6 requires, and folds in the M3a review's documentation minors:
- the spec's sensor TTL;
- AGENTS §6's "blank partition";
- the M3a per-wake figure in `docs/power.md`.

The changes:
- **AGENTS.md**
  - §2: status and the M3 row.
  - §3.4: gotcha 24 (sleep-out needs the init sequence, because of NRDSLP).
  - §5.2: `locale` and `timekeeping`.
  - §6:
    - the menu, `schedule` and `night` commands;
    - how the partition gets formatted;
    - the screen goldens;
    - night sleep dropping the console.
  - §7: the console list, and driving the menu with `btn`.
  - §9: D17, the owner's M3b render-review rulings.
- **Spec r11**
  - D17 (§1.2).
  - Panel sleep and wake (§4.2).
  - The TTL (§5.1).
  - Validation and the schedule as built (§5.4).
  - Toasts and the critical screen (§5.5).
  - The menu's gesture timings (§5.6) and the menu as built (§5.7).
  - The humidity clamp and the thresholds (§8).
  - Night sleep (§9.1).
  - The fallback toast and the kept backup (§14.3).
  - Factory reset (§14.4).
  - The console and `render.py` (§15), and the goldens (§17).
  - A critical-battery risk (§20), and the revision row (§21).
- **docs/power.md**: the M3a per-wake figure.

- [ ] **Step 1: Apply the edits.** Save this script outside the repository (a scratch directory) and run it from the repo root. Each edit must match exactly once, or the script stops and changes nothing on disk for that file.

```python
"""M3b documentation updates (Task 13). Every edit must match exactly once."""
import pathlib

EDITS = {
    "AGENTS.md": [
        # §2: status
        ("M3b is next (menu, screens, settings, schedule and night sleep, Czech pack); its plan gets written before "
         "it starts.",
         "M3b is built: the on-device menu with settings editing, toasts and the critical-battery screen, the preset "
         "schedule with timed night sleep, and the Czech pack with its public holidays. M3 is done once the owner "
         "has measured night sleep and checked the buttons (Owner acceptance in the M3b plan)."),
        ("| M3 | Data store, layouts, presets, cycling, status bar, on-device menu | KEY switches presets, and the "
         "choice survives a reboot |",
         "| M3 | Data store, layouts, presets, cycling, status bar, on-device menu, schedule and night sleep, Czech "
         "pack | KEY switches presets, and the choice survives a reboot. The owner measures night sleep |"),
        # §3.4: the panel's sleep-out (M3b)
        ("The DSJ package is a 3×4 mm VSON with the pins underneath.\n",
         "The DSJ package is a 3×4 mm VSON with the pins underneath.\n"
         "24. **Sleep-out needs the init sequence again.** Both vendor init sequences set NRDSLP (`D6h`, second "
         "parameter `0x02`), which makes `SLPOUT` reload the NVM defaults: at M3b a plain SLPOUT brought the panel "
         "back at 2 Hz. So `st7305_sleep_out()` resets the panel and runs the init sequence, and `display_wake()` then "
         "pushes the frame and returns to LPM. `panel fps` reads high for a few seconds while the panel settles, then "
         "1.00 Hz. Sleep-in from LPM goes through HPM first (datasheet §7.10), about 400 ms in all. Night sleep uses "
         "both (spec §9.1); `panel sleep` and `panel wake` try them by hand.\n"),
        # §5.2
        ("  locale/        language packs: en, cs (M3b); API prefix lang_       [host]",
         "  locale/        language packs: en, cs; API prefix lang_             [host]"),
        ("  timekeeping/   system time from RTC, TZ, SNTP, manual set",
         "  timekeeping/   system time from RTC, TZ and its short list, SNTP, manual set"),
        # §6: commands
        ('tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "field set env.temp -5.5"   # redraws at once\n',
         'tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "field set env.temp -5.5"   # redraws at once\n'
         'tools/idf.sh exec python tools/devlog.py --cmd "btn key long"    # the menu: then KEY and BOOT, short or long\n'
         'tools/idf.sh exec python tools/devlog.py --cmd "schedule add 23:00 night 06:00" --cmd "schedule on"\n'
         'tools/idf.sh exec python tools/devlog.py --cmd "night 60"        # night sleep now; the console drops\n'),
        # §6: the partition is formatted whenever it doesn't mount (M3a review)
        ("The first boot formats a blank `storage` partition; `idf.py flash` never writes it.",
         "A boot formats the `storage` partition whenever it doesn't mount: when it is blank, but also when it is "
         "corrupted. The menu's factory reset formats it too. `idf.py flash` never writes it."),
        ("- Golden renders: after an intentional UI change, rewrite a golden with `build-host/render_dashboard "
         "<fixture> test/host/golden/dash_<fixture>.pbm` (fixtures in `test/host/dashboard_fixtures.h`), look at "
         "the PNGs from `tools/render.py`, then commit.",
         "- Golden renders: after an intentional UI change, rewrite a golden with `build-host/render_dashboard "
         "<fixture> test/host/golden/dash_<fixture>.pbm` or `build-host/render_screen <fixture> "
         "test/host/golden/screen_<fixture>.pbm` (fixtures in `test/host/dashboard_fixtures.h` and "
         "`screen_fixtures.h`; `--list` prints them), look at the PNGs from `tools/render.py`, then commit.\n"
         "- **Night sleep deep-sleeps even while a PC is attached** (spec §9.1). After `night <minutes>` or a "
         "schedule's night entry, the port is gone until the end time or a KEY or BOOT press."),
        # §7: console
        ("`panel status|test|clear|mode <hpm|lpm>|rate <0.25|0.5|1|2|4|8>|fps [s]|init <factory|xiaozhi>`",
         "`panel status|test|clear|mode <hpm|lpm>|rate <0.25|0.5|1|2|4|8>|fps [s]|sleep|wake|init <factory|xiaozhi>`"),
        ("`field list|get <id>|set <id> <value>|clear <id>`, `preset list|set <id>`. Planned:",
         "`field list|get <id>|set <id> <value>|clear <id>`, `preset list|set <id>`, `night <minutes>`, "
         "`schedule list|on|off|clear|add <HH:MM> preset <id> [days]|add <HH:MM> night <HH:MM> [days]`. Planned:"),
        ("Drive the UI with `btn` and `screenshot` instead of asking the owner to press buttons.",
         "Drive the UI, the menu included, with `btn` and `screenshot` instead of asking the owner to press buttons. "
         "A `btn` gesture reaches the app through the buttons task, so a command in the same devlog call can run "
         "before it has taken effect: check its result in a separate call."),
        # §9: the owner's M3b rulings
        ("A button still held at sleep time is left out of that sleep's wake sources |\n",
         "A button still held at sleep time is left out of that sleep's wake sources |\n"
         "| D17 | Owner, 2026-09-29 (M3b render review): the menu leaves out Display ▸ Contrast for now, and the "
         "temperature offset steps by 0.1 °C |\n"),
    ],
    "docs/specs/2026-09-25-firmware-design.md": [
        # §1.2
        ("A stuck KEY or BOOT would otherwise wake the board again and again |\n",
         "A stuck KEY or BOOT would otherwise wake the board again and again |\n"
         "| D17 | Owner, 2026-09-29 (M3b render review): the menu leaves out Display ▸ Contrast for now, and the "
         "temperature offset steps by 0.1 °C (§5.7) | No contrast levels besides the factory sequence's (D12) have "
         "been checked on the panel |\n"),
        # §4.2: sleep-in and wake
        ("- **Before deep sleep.** Drive CS high and RESET high,",
         "- **Sleep-in and wake** (night sleep, §9.1).\n"
         "  - Sleep-in follows datasheet §7.10: from LPM through HPM (`38h`, 300 ms), then `SLPIN` (`10h`, 100 ms). "
         "The panel stops scanning, and its image fades.\n"
         "  - Both init sequences set NRDSLP (`D6h`), so `SLPOUT` would reload the NVM defaults; at M3b the panel "
         "came back at 2 Hz that way. Waking therefore resets the panel and runs the init sequence again, then "
         "pushes the frame and returns to LPM.\n"
         "  - A deep-sleep wake keeps a sleeping panel asleep: the snapshot records it.\n"
         "- **Before deep sleep.** Drive CS high and RESET high,"),
        # §5.1: the TTL as built (M3a review)
        ("| `env.temp` | number, °C | SHTC3 | Stale after 15 min without a reading |",
         "| `env.temp` | number, °C | SHTC3 | Stale after 15 min without a reading, or after twice the sample "
         "interval if that is longer |"),
        # §5.4: validation as built
        ("  - an unknown `stale_policy` or `status_battery` value.\n",
         "  - an unknown `stale_policy` or `status_battery` value;\n"
         "  - nesting deeper than 16 levels, or `slots` that isn't an object;\n"
         "  - a bad schedule: more than 8 entries, or an entry with a bad time, action or preset, or a night that "
         "ends at the minute it starts.\n"),
        ("  - a missing name takes the id;\n  - missing options take their defaults;\n",
         "  - a missing name takes the id, and a name longer than 23 bytes is cut at a character boundary;\n"
         "  - missing or `null` options take their defaults;\n"),
        # §5.4: the schedule as built
        ("  - `night` starts night sleep (§9.1) until `until`, which may be on the next day.\n",
         "  - `night` starts night sleep (§9.1) until `until`, which may be on the next day.\n"
         "  - Entries of the same minute run in list order, presets before a night, because nothing runs once a "
         "night has started.\n"
         "  - Entries run only while the time is valid. They don't run late: not those a night covers or a clock "
         "change skips, and not those missed while the board was off.\n"
         "  - A preset entry's switch is not saved, like an auto-cycle switch.\n"
         "  - Until the web UI (M4), the `schedule` console command edits the entries (§15).\n"),
        # §5.5: screens as built
        ("| Critical battery | \"Battery empty — charge me\" and the last known time. Nothing else updates |",
         "| Critical battery | A large empty battery, \"Battery empty\" and \"Please charge me\", with the time and date "
         "it was drawn. Nothing else updates |"),
        ("| Toast | A short overlay (e.g. \"Preset: Weather\", \"Sync failed\") shown for about 3 s |",
         "| Toast | A short overlay near the bottom, in a black box (e.g. \"Preset: Weather\", \"Sync failed\"), shown "
         "for 3 s. The board stays awake meanwhile |"),
        # §5.6: the menu's timings
        ("  - BOOT on the dashboard: long press fires at 3 s instead, so Wi-Fi doesn't switch on by accident.\n",
         "  - BOOT on the dashboard: long press fires at 3 s instead, so Wi-Fi doesn't switch on by accident.\n"
         "  - In the menu KEY has no double press, so a short press answers at once, and BOOT's long press fires "
         "at 1 s.\n"),
        # §5.7: the menu as built
        ("- Schedule entries are edited in the web UI (M4); until then `presets.json` or the console sets them.\n",
         "- Schedule entries are edited in the web UI (M4); until then `presets.json` or the console sets them.\n"
         "- Display ▸ Contrast is hidden for now (D17). Info ▸ IP/MAC and Last sync result join with M4 and M5.\n"
         "- As built (M3b):\n"
         "  - A toggle flips at once. A choice or a number is edited in place and saved with KEY long.\n"
         "  - The temperature offset steps by 0.1 °C (±10 °C, D17), the humidity offset by 0.5 % (±20 %).\n"
         "  - The time zone list holds 14 zones. A zone set elsewhere stays selectable.\n"
         "  - Info shows the device id `reflbo-XXXX`, the firmware version with the first bytes of its ELF hash, "
         "and the uptime since the first valid clock after a cold boot.\n"
         "  - Reboot doesn't ask. Factory reset asks, and only KEY held confirms it.\n"
         "  - The panel is in HPM while the menu is open.\n"),
        # §8: the humidity clamp and the thresholds as built
        ("- Temperature and humidity offsets are settings.",
         "- Temperature and humidity offsets are settings. The humidity with its offset stays within 0–100 %."),
        ("  - Low, 15 %: status icon; sync retries are skipped.\n"
         "  - Critical, 3.3 V or below: critical screen. Only KEY wakes the device.\n",
         "  - Low, 15 %: a \"!\" beside the status bar's battery; sync retries are skipped.\n"
         "  - Critical, 3.3 V or below while not charging: the critical screen. Only KEY wakes the device; if KEY "
         "is held, a timer checks again every 10 min instead (D16). The screen stays until the battery reads "
         "3.4 V, or until charging is seen, so it doesn't flicker at the threshold.\n"),
        # §9.1: night sleep as built
        ("- The chip deep-sleeps whatever the idle strategy, woken only by KEY, BOOT or the end time (the RTC alarm, "
         "with the usual backup timer). Nothing is sampled or drawn.\n",
         "- The chip deep-sleeps whatever the idle strategy, woken only by KEY, BOOT or the end time (the RTC alarm, "
         "with the usual backup timer). Nothing is sampled or drawn. It sleeps even while a PC is attached, so the "
         "console drops.\n"),
        ("- A button press wakes the panel (`SLPOUT`, then the datasheet's wait) and shows the dashboard. After 60 s "
         "without input, night sleep resumes. KEY long still opens the menu.\n",
         "- A button press wakes the panel (with the init sequence again, §4.2) and shows the dashboard.\n"
         "  - The press that woke it does nothing else.\n"
         "  - The board stays awake until 60 s after the last press, then night sleep resumes.\n"
         "  - KEY long still opens the menu. A night that comes due while the menu is open waits until it closes.\n"
         "  - A night that starts with a button held checks again every minute, so the button can wake it once "
         "released (D16).\n"),
        # §14.3: the fallback as built
        ("- If a file is invalid, the firmware uses `*.bak`; if that is invalid too, it uses defaults and shows a toast "
         "(from M3b; M3a logs it).\n",
         "- If a file is invalid, the firmware uses `*.bak`; if that is invalid too, it uses defaults and shows the "
         "toast \"Using default settings\".\n"
         "  - A file rejected in favour of its `.bak` is removed, so the next save keeps the good backup.\n"
         "  - A file nested deeper than 16 levels is rejected before it is parsed.\n"),
        # §14.4
        ("- **Factory reset.** From the menu (with confirmation) or the web UI.",
         "- **Factory reset.** From the menu (System ▸ Factory reset, confirmed by holding KEY), or from M4 the web "
         "UI. The board restarts afterwards."),
        # §15: console
        ("| `panel status` · `panel test` · `panel clear` · `panel mode <hpm\\|lpm>` · `panel rate <Hz>` · "
         "`panel fps [s]` · `panel init <factory\\|xiaozhi>` | Panel diagnostics: test pattern, power mode, LPM rate, "
         "measured frame rate, init sequence |",
         "| `panel status` · `panel test` · `panel clear` · `panel mode <hpm\\|lpm>` · `panel rate <Hz>` · "
         "`panel fps [s]` · `panel sleep` · `panel wake` · `panel init <factory\\|xiaozhi>` | Panel diagnostics: test "
         "pattern, power mode, LPM rate, measured frame rate, sleep-in and wake, init sequence |"),
        ("| `preset list` · `preset set <id>` | Presets |\n",
         "| `preset list` · `preset set <id>` | Presets |\n"
         "| `schedule list` · `schedule on\\|off\\|clear` · `schedule add <HH:MM> preset <id> [days]` · "
         "`schedule add <HH:MM> night <HH:MM> [days]` | The preset schedule (§5.4); `days` is the Mon–Sun mask, "
         "default 127 |\n"
         "| `night <minutes>` | Night sleep now (§9.1), for measuring; the console drops until it ends |\n"),
        ("| `tools/render.py` | Run the host renderer on presets and fixtures, writing PNGs | Host build |",
         "| `tools/render.py` | Run the host renderers on every fixture, writing PNGs; `render_dashboard --list` and "
         "`render_screen --list` name them | Host build |"),
        # §17
        ("After an intentional change, the renderer rewrites the golden (`build-host/render_dashboard <fixture> "
         "<file>`),",
         "After an intentional change, the renderer rewrites the golden (`build-host/render_dashboard <fixture> "
         "<file>`, or `render_screen` for the menu and the special screens),"),
        # §20
        ("| No RTC backup cell (D9): the time is lost at every PWR-off |",
         "| The critical-battery path has not met a really low battery: its thresholds are host-tested and its "
         "screen is a golden, but its KEY-only sleep has not run on the board | Watch the first time the board runs "
         "flat on battery (M5 power work) |\n"
         "| No RTC backup cell (D9): the time is lost at every PWR-off |"),
        # §21
        ("| r10 | 2026-09-29 | Owner decisions (D16): the `cs` pack ships the public holidays only for now (§5.8, "
         "§20); a held button is left out of the wake sources (§9.2) |\n",
         "| r10 | 2026-09-29 | Owner decisions (D16): the `cs` pack ships the public holidays only for now (§5.8, "
         "§20); a held button is left out of the wake sources (§9.2) |\n"
         "| r11 | 2026-09-29 | M3b as built. The menu without Contrast and with the offset steps (D17, §5.7), and the "
         "menu's gesture timings (§5.6). Toasts and the critical screen (§5.5), the critical hysteresis and the "
         "humidity clamp (§8). The schedule's order and validation (§5.4), and night sleep (§9.1). Panel sleep and "
         "wake with NRDSLP (§4.2). The fallback toast, the kept backup and the nesting limit (§14.3, §5.4), and "
         "factory reset (§14.4). The `night`, `schedule` and `panel sleep\\|wake` commands, and `--list` for the "
         "renderers (§15, §17). The sensor TTL (§5.1) and a critical-battery risk (§20) |\n"),
    ],
    "docs/power.md": [
        ("- Full cycles slept 60.0 s, so nothing woke the board early.\n",
         "- Full cycles slept 60.0 s, so nothing woke the board early.\n\n"
         "Firmware at M3a (the dashboards, 2026-09-28): 64 ms of app time per routine deep wake, measured on the M3a "
         "spike with `sleep test deep 2`. That is about the M2 figure, since a dashboard draws and pushes in about the "
         "time the clock screen did.\n"),
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

Run: `python3 <scratch>/m3b_docs.py && git diff --stat`
Expected:
- `AGENTS.md: 12 edits`, `docs/specs/2026-09-25-firmware-design.md: 22 edits` and `docs/power.md: 1 edits`.
- The diff touches only those three files.

- [ ] **Step 2: Read the result.** Read the three diffs whole. Check that:
  - every command they name exists: `panel sleep`, `panel wake`, `night`, `schedule`, `render_screen --list`;
  - the §6 block in AGENTS.md still renders as one code block;
  - the gotchas are numbered 1–24 without a gap;
  - D17 appears in both decision tables and says the same thing.

Run: `grep -c "^| D17 " AGENTS.md docs/specs/2026-09-25-firmware-design.md && grep -n "^24\. " AGENTS.md`
Expected: `1` for each file, and one line for gotcha 24.

- [ ] **Step 3: Commit.**

```bash
git add AGENTS.md docs/specs/2026-09-25-firmware-design.md docs/power.md
git commit -m "docs: record M3b as built in AGENTS.md and the spec (r11)"
```

---


## Owner acceptance (M3, after the final review)

These checks need the owner's hands, eyes or meter (AGENTS §7, level 4). Once the final review's fixes are in, flash `main`, set the clock, and send the owner this list.
- The 18650 stays in and charged, so moving the USB cable between the PC and the charger never cuts the power. Without a backup cell, the clock would be lost (D9).
- Read `sleep stats` over USB before anything switches the board off, because PWR-off wipes RTC RAM (M2 note).

1. **Night-sleep current** (D15, spec §9.4). This is the M3 acceptance measurement.
   - From the PC, set a night that starts in 10 minutes and lasts at least 90 minutes, then switch the schedule on:

     ```bash
     tools/idf.sh exec python tools/devlog.py --cmd "schedule add $(date -v+10M +%H:%M) night $(date -v+100M +%H:%M)" \
       --cmd "schedule on"
     ```

     Move the cable to the charger and meter before the night starts.
   - **Expected:** at the start the toast "Night until HH:MM" shows for 3 s, then the panel blanks.
   - **Measure:** read the meter's mAh over at least 1 h inside the night. Also read an idle hour with the same firmware and settings: Home, the defaults, light sleep.
   - **Record:** both go into `docs/power.md` with the date, commit, settings, meter and window. The forced-PWM converter's ~60 mW floor (gotcha 23) is likely to dominate both, so the saving may be small. Whether night sleep stays is the owner's call (D15).
   - Afterwards, back on the PC: `schedule clear`, then `schedule off`.
2. **A held button** (D16). On the charger, hold KEY down for three minutes. Expected:
   - The menu opens after 1 s and closes by itself about a minute later.
   - The clock keeps updating every minute while KEY is still held.
   - After release, a short press switches the preset, with its toast.

   Back on the PC, `sleep stats` shows the hold as `rtc` wakes, not as a run of `key` wakes. Hold BOOT the same way; its long press (3 s) does nothing on the dashboard. Never power the board on with BOOT held: that enters download mode (gotcha 22).
3. **The night peek** (spec §9.1). During a night (check 1, or `night 10`), press KEY briefly. Expected:
   - Within about a second the dashboard comes up. The press doesn't switch the preset.
   - The dashboard stays until about 60 s after the last press, then the panel blanks again.
   - A KEY long press during the peek opens the menu; after it closes, the night resumes a minute later.
   - At the end time the dashboard comes back by itself.
4. **The Czech panel.** Choose Čeština in the menu (System ▸ Language). Expected:
   - The dashboard and the menu read well on the panel, diacritics (ř, ů, ě) included.
   - The hints fit the bottom line.
   - Switch back if wanted.
5. **Menu buttons** (spec §5.6):
   - Open the menu with a 1 s KEY press.
   - Browse with KEY short, enter with KEY long, go back with BOOT short, and close with a 1 s BOOT press.

   Expected:
   - Every press answers at once, with no double-press wait, and the panel redraws promptly (HPM).
   - Left alone, the menu closes after 60 s.

Record the results in the M3b report. When all five pass, M3 is done: update AGENTS §2 in a small `docs:` commit. A failed check is a finding to fix before that, with its test first.
