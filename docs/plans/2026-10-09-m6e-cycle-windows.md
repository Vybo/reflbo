# M6e: Cycle Windows per Preset, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** M6e (spec §18, D41): a preset may have a cycle window, from and until, each a local time or sunrise or sunset with an offset of up to 3 h, crossing midnight when until comes first, and the days it opens on; the auto-cycle skips a preset whose window is closed, while KEY short, the schedule, the menu and the web page still reach every preset; the built-in Solar preset comes with sunrise to sunset; windows are edited on the Presets page and with `preset window` on the console.

**Architecture:**

- **Pure logic, host-tested:** `ui_window` (new, in `ui`): the bounds in two bytes, a window's rules, whether it is open at a time and when that changes, with the sun from `astro` and local times through `scheduler`'s §9.2 rules; the console's texts. `ui_preset`: the window in each preset and in `presets.json`, the auto-cycle's next preset, Solar's window.
- **Device side:** `main/app_ui.c`'s cycle step asks with the clock (0 when not valid) and the location saved now; `main/app_cmds.c`'s `preset list` and `preset window`; `main/app.c`'s snapshot version.
- **Web:** the Presets page's "In the cycle" with its ends and days, the list's summary, and the page's own checks before it saves.

**Tech stack:** ESP-IDF v5.5.5, cJSON, LittleFS; Unity on the host; Node's test runner for the page. Nothing new from the registry.

**Spec:** `docs/specs/2026-09-25-firmware-design.md` r45 (D41, written 2026-10-09 from the owner's answers that day; the owner then said "Continue with m6e"). Task 5 brings it to r46, as built.
- Relevant: §5.4 (Cycle windows, the Validation list, Switching, the built-in Solar preset), §6 (the snapshot), §9.2 (local times across DST), §10.3 (Presets, M6e), §11 (`astro`), §15 (`preset`), §17 (M6e), §18 (M6e), §1.2's D41.
- Also `AGENTS.md` §3.4 (gotchas 22, 29, 43), §5.3, §6 (the stable firmware and board tests), §7, §8.

**Research behind this plan (2026-10-09):**
- **How it was built.** The plan's code was built and tested task by task on the branch `plan/m6e` from main (25fcde3), one commit per task (`plan-m6e task N: …`); the plan carries that code. A review of the whole branch and its fixes are folded into those commits (below).
- **Tests and sizes as built.** On the host the whole suite passed after every task: 67 ctest targets at the end (66 before: `test_ui_window` is new), and the page's 58 tests (54 before); `test_ui_window` and `test_ui_preset` passed under ASan and UBSan. The firmware built clean after every task, without a warning: 2 542 848 bytes at the end (2 537 536 before). The RTC snapshot grows by 112 bytes to 5 968 of its 6 144 (`SNAP_VERSION` 12); the largest `presets.json`, 16 split presets of 24 cells each with a window at its longest, is 38 238 bytes of 48 KB.
- **What was there to build on.** `astro_sun()` gives a local day's sunrise and sunset, or a polar day or night, from the zone's offset at local noon, as `ui_forecast.c` uses it for the sun widget; `sched_local_to_utc()` turns a local minute into a time with §9.2's rules (a minute a DST change skips is the next valid one, a repeated one its first occurrence). The cycle already steps in `app_ui_tick()` at `cycle_interval_s` (10 s to 1 h), and `ui_presets_next()` already skips Flights outside sync mode `always` (D28). `presets.json`'s parser looks up only the keys it knows, so `stable-m6d` reads a file with windows and ignores them; its next save drops them.
- **Not checked on the board:** everything the device does is Task 5's: the console's windows and refusals, a window closing while the cycle runs, a new location, the page against the board, a restart, a deep sleep, and the stable firmware reading a file with windows.
- **The review (2026-10-09, opus):** a fresh reviewer read the whole branch before this plan was generated, ran the suites, and probed `ui_window_open()` with 4 000 random windows at each of eight places from Sydney to Longyearbyen and around both DST changes, checking that each state holds until the change it reports, and the page's checks against the device's on 483 strings (no difference). No Critical finding; two were fixed test first in Task 1:
  - **Important:** a window whose start falls before midnight of the day it opens (`sunrise-180` at Reykjavik in June, a polar day's `sunrise-N`) read closed until midnight, and `preset list` printed a time at which it was still closed (the windows opened tomorrow are now looked at too).
  - **Re-graded Important:** 02:00–02:30 on the spring-forward night resolved both ends to 03:00 and became a day-long window (two clock ends now cross midnight on the clock face, so it is empty that night).
  - **Left for later, in spec r46 §20 (Task 5):**
    - Polar edges as ruled: the first day after a polar night opens sunrise → sunset at midnight, and mixed ends open in a polar night.
    - A closed window still scans ahead for its opening when the cycle step asks no change time.
    - `ui_window.c` repeats `ui_forecast.c`'s sun cache, keyed without the zone.
    - The page shows a stored end it can't read (an offset of 1.5, a cleared time) as 07:00 after a re-render, while Save refuses it.
    - While any window is bad, every preview fails.
    - "In the cycle: All day" shows on presets outside the cycle, and `preset window … off` says "always in the cycle" for one outside it.
    - A refusal for a 15-character id is cut at 96 bytes in the restore's and the boot's messages.
    - Host tests of a location change, and page tests of a copied windowed preset and an offset of 1.5.
    - Two compound literals leave a padding byte unspecified where a test compares memory.
    - The console reads days in octal from a leading zero, as `schedule add` does.

**Rulings this plan makes (each costs little if wrong):**

- **A polar night puts sunrise at the day's end and sunset at its start** (Task 1), not spec r45's "both at 12:00": with the rule that a window whose `until` comes at or before its `from` closes the next day, 12:00 and 12:00 made sunrise → sunset a 24-hour window. Now sunrise → sunset is closed all polar night and open all polar day, and sunset → sunrise the opposite. Task 5 corrects the spec. Cost: none south of the polar circles.
- **Two clock ends cross midnight on the clock face** (Task 1, the review's): 02:00–02:30 on the spring-forward night resolves both ends to 03:00, and is empty that night rather than a day long; ends with a sun bound compare their resolved times, as the spec says.
- **A window can open the evening before its day** (Task 1, the review's): `sunrise-180` falls before midnight in a northern June, so the window opened on tomorrow is looked for too.
- **A bound is two bytes** (Task 1): minutes after midnight, or 2000 (sunrise) or 3000 (sunset) plus the offset, so 16 windows cost the RTC snapshot 112 bytes rather than twice that; M7, which comes next, leaves the snapshot about 40 bytes under its 7 KB cap.
- **One check for the file and the console** (Tasks 1–3): `ui_window_make()` names the rule a window breaks, and both prefix it, so they refuse the same things in the same words; the page checks the same rules before it sends (Task 4).
- **`days` must be a whole number from 1 to 127** (Task 2), stricter than the schedule's entries, whose `days` are cut to seven bits: a window that opens on no day is a mistake, not a choice.
- **`SNAP_VERSION` moves with the presets' layout** (Task 2), not with the app's glue in Task 3, so every commit is a consistent image.
- **Only the cycle step looks at windows** (Task 3): no wake-up is added for a window's opening or closing; a closed preset on screen stays until the next step, as spec §5.4 says, at most the cycle's interval.
- **A window is judged with the location and the zone saved now** (Task 3), at each step, so a new place counts from the next step; sun times are cached by day and place.
- **The page shows an offset as it is written** (Task 4), even one out of range (200), and names it at Save rather than clamping it in the editor: what the device refuses never goes out, and a value in a file is shown as stored.

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, plain HTML/CSS/JS in `web/` with no build step and no external resources.
- `[host]` code (`ui_window.c`, `ui_preset.c`, `ui_preset_json.c`, all of `ui`) includes no ESP-IDF headers. cJSON counts as plain C.
- ESP-IDF style: 4-space indent, `snake_case`, a component or module prefix on public APIs, public headers in `include/`. C lines stay within 120 characters; the page's files keep their own style.
- The app task owns the presets, the display and storage (spec §3.2, `AGENTS.md` §5.3); console commands that touch them run on it through `diag_on_owner()`.
- **The RTC snapshot is at most 6 KB** (`_Static_assert` in `main/app.c`); a change to its layout bumps `SNAP_VERSION`.
- **Input from outside is checked before cJSON recurses** (gotcha 30): `presets.json` and requests keep their depth limits; a window adds one level.
- **Never commit or log secrets.**
- **Board tests** (memory `board-test-protocol`): back up the configuration first (`GET /api/backup`) and restore it after (`POST /api/restore`), then compare. Never erase flash, NVS or the storage partition without asking. Board commands go through `tools/idf.sh` with an explicit `-p`; the port must be confirmed as this board (USB serial `14:C1:9F:54:BB:94`, `flash` prints `MAC: 14:c1:9f:54:bb:94`). The web password may be reset from the menu at any time (the owner, 2026-10-02); a temporary one is set over the device's own network and cleared again at the end.
- List every component source in `SRCS`. Dependencies come with ESP-IDF; nothing new from the registry.
- Small, focused Conventional Commits that each build. Push to `origin` freely; never force-push `main`. **No attribution of any kind: no `Co-Authored-By` or other trailer in any commit** (the owner's rule, memory `no-commit-trailers`).
- Build only what the spec covers (r45: D41). Not in M6e: windows for HA's `cmd/next` or select (M7 keeps them out, D41), a window's state on the panel or the page, more than one window a preset.

## Review Focus

1. **The clock, the zone and the place change under a window.** A power-off without the backup cell (D9) and the sync that sets the clock; a new time zone or location saved on the web page; a DST change between two cycle steps. Expected: every window counts as open until the clock is valid, then follows the zone and the place saved now, at the next cycle step; sun times are never those of the old place. Pinned by:
   - `test_ui_window` (DST changes, polar days and nights, a sun window that follows the sun) (Task 1), `test_ui_preset`'s cycle with no valid time (Task 2);
   - Task 5 Step 4, the location changed on the board.
2. **The cycle's timing around a window.** A window that closes between two cycle steps, an interval of up to 1 h, the board asleep in light or deep sleep, a night sleep that ends inside or outside a window, the auto-cycle switched off and on. Expected: a closed preset on screen stays until the next step (spec §5.4), the step then moves to the next open preset, and with none open the screen stays; no extra wake-ups for windows. Pinned by:
   - `test_ui_preset`'s cycle tests (Task 2);
   - Task 5 Step 3, a window that closes a minute after it is set.
3. **Files with windows going through every path.** The web page's save, the preview, a backup and its restore, a copied preset, the console's `preset window`, a reboot and a routine wake, and the stable firmware, which reads a file with windows and drops them on its next save. Expected: a window survives each path on M6e; the stable firmware still boots the file. Pinned by:
   - `test_ui_preset`'s round trip and the largest file with windows (Task 2), the page's tests (Task 4);
   - Task 5 Steps 2, 5 and 6.
4. **What the console takes.** `preset window` with a missing or extra argument, days as `0x7f`, `abc`, `-1` or `300`, bounds with capitals, spaces or a leading zero, an unknown preset, `off` on a preset without a window. Expected: a sentence naming the rule, nothing saved; a good window saved and printed with its state. Pinned by:
   - `test_ui_window`'s bounds and refusals (Task 1);
   - Task 5 Step 3's refusals.
5. **The page against the device's rules.** An offset of 200 or 1.5, from equal to until, no day ticked, a window copied with a new preset, All day and back. Expected: the page refuses each with a sentence before sending, and what it sends the device takes. Pinned by:
   - the page's tests (Task 4), `test_ui_preset`'s refusals (Task 2);
   - Task 5 Step 5, the page against the board.

---

### Task 1: The cycle windows (`ui`)

**Files:**
- Create: `components/ui/include/ui_window.h`, `components/ui/ui_window.c`, `test/host/test_ui_window.c`
- Modify: `components/ui/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `astro_sun()` and `astro_sun_t` (`astro.h`); `sched_local_to_utc()` (`scheduler.h`, spec §9.2's rules for a time a DST change skips or repeats); `util_days_from_civil()`, `util_civil_from_days()` (`util_time.h`); the host build's `reflbo_host_test()` and its `ui` and `astro` libraries. `ui` already has `astro` and `scheduler` among its private requirements, on the board and on the host.
- Produces (`ui_window.h`, pure C):
  - `UI_BOUND_OFFSET_MAX 180`, `UI_BOUND_TEXT_LEN 12`, `UI_BOUND_SUNRISE 2000`, `UI_BOUND_SUNSET 3000`;
  - `typedef struct { uint8_t days; int16_t from, until; } ui_window_t;` (6 bytes; `days` 0: no window);
  - `bool ui_bound_parse(const char *text, int16_t *out)`, `void ui_bound_format(int16_t bound, char *out, size_t size)`;
  - `bool ui_window_make(const char *from, const char *until, int days, ui_window_t *out, char *err, size_t err_size)`;
  - `bool ui_window_open(const ui_window_t *w, time_t now, int32_t lat_e4, int32_t lon_e4, time_t *change)`;
  - `void ui_window_text(const ui_window_t *w, char *out, size_t size)`, `void ui_window_state_text(const ui_window_t *w, time_t now, int32_t lat_e4, int32_t lon_e4, char *out, size_t size)`.

A preset's cycle window (spec §5.4, D41), as pure logic the presets, the console and the host tests share. A bound is a local time, `"HH:MM"`, or `"sunrise"` or `"sunset"` with an optional `+N` or `-N` of 1–180 minutes, no leading zero; it is kept in two bytes, as the RTC snapshot keeps the presets: minutes after midnight, or 2000 (sunrise) or 3000 (sunset) plus the offset. `ui_window_make()` checks a window as `presets.json` and the console give it, and names the rule it breaks: a bad `from` or `until`, the two the same, or `days` outside 1–127.

When it is open:
- A window opens on each day its `days` name (bit 0 Monday) at `from` and closes at `until` that day, or the next day when `until` comes at or before `from`. Two clock ends are compared on the clock face, so 02:00–02:30, both 03:00 in the spring DST gap, is empty that night rather than a day long.
- A clock bound is that day's local minute through `sched_local_to_utc()`: one a DST change skips counts from the next valid minute, a repeated one from its first occurrence (spec §9.2).
- A sun bound is that day's sunrise or sunset at the location, with its offset, from `astro_sun()` with the zone's offset at local noon, as `ui_forecast.c` computes the sun widget's. Where the sun doesn't rise or set, a polar day runs from midnight to midnight (sunrise 00:00, sunset 24:00) and a polar night the other way round (sunrise at the day's end, sunset at its start), so sunrise → sunset is open all polar day and closed all polar night, and sunset → sunrise the opposite.
- `ui_window_open()` looks at the windows opened from two days back (one opened last evening closes this morning; a bound 3 h past a polar sunset falls on the next day) to tomorrow (a bound up to 3 h before sunrise falls this evening, as at Reykjavik in June), and joins a window to the next day's when that starts before it ends, so a polar day reports no change. `*change` is the closing time when open, the next opening within 8 days when closed, 0 when nothing changes in that time.
- A few days' sun times are kept, as the S3 computes them in software doubles.

For `preset list` (Task 3): `ui_window_text()` gives "sunrise - sunset-30", with " on Mo Fr" unless every day; `ui_window_state_text()` gives "open until 18:21", "closed until Sat 07:00" (the weekday when not today), "open" or "closed" without a change in sight, and "open: the clock isn't set" for `now` 0.

- [ ] **Step 1: Write the failing test.** Bounds read and written back, and refused; a window made or refused with its reason; no window; a clock window, one across midnight, the days it opens on; a sun window with its offsets, sunset to sunrise; a window that opens the evening before its day (Reykjavik, a polar day); both DST changes and a window inside the spring gap; polar days and nights at Longyearbyen; the console's texts:

`test/host/test_ui_window.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/test/host/test_ui_window.c
@@ -0,0 +1,279 @@
+#define _POSIX_C_SOURCE 200809L /* setenv */
+
+#include <stdlib.h>
+#include <string.h>
+#include <time.h>
+
+#include "astro.h"
+#include "ui_window.h"
+#include "unity.h"
+#include "util_time.h"
+
+/* Europe/Prague, the default time zone (AGENTS.md §1), and Brno, the default location. */
+#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"
+#define BRNO_LAT 491951
+#define BRNO_LON 166068
+#define SVALBARD_LAT 782232 /* Longyearbyen: a polar day in June, a polar night in December */
+#define SVALBARD_LON 156267
+#define REYKJAVIK_LAT 641466 /* sunrise before 03:00 in June */
+#define REYKJAVIK_LON -219426
+
+static char s_err[96];
+
+void setUp(void)
+{
+    setenv("TZ", TZ_PRAGUE, 1);
+    tzset();
+    s_err[0] = '\0';
+}
+
+void tearDown(void) {}
+
+/* A local time that exists once (not in a DST change's hour). */
+static time_t local_time(int y, int mo, int d, int h, int mi)
+{
+    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi, .tm_isdst = -1 };
+    return mktime(&t);
+}
+
+/* UTC seconds for a UTC calendar time. */
+static time_t utc(int y, int mo, int d, int h, int mi)
+{
+    return (time_t)(util_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60);
+}
+
+/* The sun at a place on a local date, as the device computes it. */
+static astro_sun_t sun(int y, int mo, int d, int32_t lat, int32_t lon)
+{
+    int32_t offset = (int32_t)(utc(y, mo, d, 12, 0) - local_time(y, mo, d, 12, 0));
+    astro_sun_t s;
+    astro_sun(y, mo, d, offset, lat, lon, &s);
+    return s;
+}
+
+static ui_window_t window(const char *from, const char *until, int days)
+{
+    ui_window_t w;
+    TEST_ASSERT_TRUE_MESSAGE(ui_window_make(from, until, days, &w, s_err, sizeof(s_err)), s_err);
+    return w;
+}
+
+/* Open or not at `now` at a place, and when that changes. */
+static void assert_state_at(const ui_window_t *w, time_t now, int32_t lat, int32_t lon, bool open, time_t change)
+{
+    time_t got = -1;
+    TEST_ASSERT_EQUAL(open, ui_window_open(w, now, lat, lon, &got));
+    TEST_ASSERT_EQUAL_INT64((int64_t)change, (int64_t)got);
+}
+
+static void assert_state(const ui_window_t *w, time_t now, bool open, time_t change)
+{
+    assert_state_at(w, now, BRNO_LAT, BRNO_LON, open, change);
+}
+
+/* A bound is a local time, or sunrise or sunset with an offset of 1-180 minutes, and writes back as it came. */
+static void test_bounds_read_and_write_back(void)
+{
+    static const struct {
+        const char *text;
+        int16_t bound;
+    } k_cases[] = {
+        { "00:00", 0 }, { "07:00", 420 }, { "23:59", 1439 },
+        { "sunrise", UI_BOUND_SUNRISE }, { "sunset", UI_BOUND_SUNSET },
+        { "sunrise-15", UI_BOUND_SUNRISE - 15 }, { "sunset+30", UI_BOUND_SUNSET + 30 },
+        { "sunrise+180", UI_BOUND_SUNRISE + 180 }, { "sunset-180", UI_BOUND_SUNSET - 180 },
+    };
+    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
+        int16_t b = -1;
+        TEST_ASSERT_TRUE_MESSAGE(ui_bound_parse(k_cases[i].text, &b), k_cases[i].text);
+        TEST_ASSERT_EQUAL_INT16(k_cases[i].bound, b);
+        char back[UI_BOUND_TEXT_LEN];
+        ui_bound_format(b, back, sizeof(back));
+        TEST_ASSERT_EQUAL_STRING(k_cases[i].text, back);
+    }
+}
+
+static void test_bad_bounds_are_refused(void)
+{
+    static const char *const k_bad[] = { "", "7:00", "24:00", "12:60", "07:00+5", "dawn", "Sunrise", "sunrise+0",
+                                         "sunrise+", "sunset+181", "sunrise-181", "sunset 30", "sunset+3x",
+                                         "sunrise--5", "sunset+0030" };
+    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
+        int16_t b;
+        TEST_ASSERT_FALSE_MESSAGE(ui_bound_parse(k_bad[i], &b), k_bad[i]);
+    }
+}
+
+/* A window is made from its texts as presets.json and the console give them, or refused with a reason. */
+static void test_a_window_is_made_or_refused(void)
+{
+    ui_window_t w;
+    TEST_ASSERT_TRUE(ui_window_make("sunrise", "sunset-30", 127, &w, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_UINT8(127, w.days);
+    TEST_ASSERT_EQUAL_INT16(UI_BOUND_SUNRISE, w.from);
+    TEST_ASSERT_EQUAL_INT16(UI_BOUND_SUNSET - 30, w.until);
+    static const struct {
+        const char *from, *until;
+        int days;
+        const char *why;
+    } k_bad[] = {
+        { "dawn", "sunset", 127, "from must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes" },
+        { "07:00", "sunset+200", 127, "until must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes" },
+        { "sunset", "sunset", 127, "from and until can't be the same" },
+        { "07:00", "07:00", 127, "from and until can't be the same" },
+        { "07:00", "22:00", 0, "days must be 1-127" },
+        { "07:00", "22:00", 128, "days must be 1-127" },
+        { "07:00", "22:00", -1, "days must be 1-127" },
+    };
+    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
+        TEST_ASSERT_FALSE(ui_window_make(k_bad[i].from, k_bad[i].until, k_bad[i].days, &w, s_err, sizeof(s_err)));
+        TEST_ASSERT_EQUAL_STRING(k_bad[i].why, s_err);
+    }
+}
+
+/* No window (days 0): always open, nothing ever changes. */
+static void test_no_window_is_always_open(void)
+{
+    ui_window_t none = { 0 };
+    assert_state(&none, local_time(2026, 10, 9, 3, 0), true, 0);
+}
+
+static void test_a_clock_window(void)
+{
+    ui_window_t w = window("07:00", "22:00", 127);
+    assert_state(&w, local_time(2026, 10, 9, 6, 59), false, local_time(2026, 10, 9, 7, 0));
+    assert_state(&w, local_time(2026, 10, 9, 7, 0), true, local_time(2026, 10, 9, 22, 0));
+    assert_state(&w, local_time(2026, 10, 9, 22, 0), false, local_time(2026, 10, 10, 7, 0));
+}
+
+/* Until at or before from: the window closes the next day. */
+static void test_a_window_across_midnight(void)
+{
+    ui_window_t w = window("22:00", "06:00", 127);
+    assert_state(&w, local_time(2026, 10, 9, 23, 30), true, local_time(2026, 10, 10, 6, 0));
+    assert_state(&w, local_time(2026, 10, 10, 5, 59), true, local_time(2026, 10, 10, 6, 0));
+    assert_state(&w, local_time(2026, 10, 10, 6, 0), false, local_time(2026, 10, 10, 22, 0));
+}
+
+/* Days are the days a window opens on: Friday's night runs into Saturday morning. */
+static void test_days_are_the_days_it_opens_on(void)
+{
+    ui_window_t w = window("22:00", "06:00", 1 << 4); /* Fridays; 2026-10-09 is one */
+    assert_state(&w, local_time(2026, 10, 9, 23, 0), true, local_time(2026, 10, 10, 6, 0));
+    assert_state(&w, local_time(2026, 10, 10, 5, 0), true, local_time(2026, 10, 10, 6, 0));
+    assert_state(&w, local_time(2026, 10, 10, 23, 0), false, local_time(2026, 10, 16, 22, 0));
+}
+
+/* Sun bounds are that day's sunrise and sunset at the location, with their offsets. */
+static void test_a_sun_window_follows_the_sun(void)
+{
+    astro_sun_t today = sun(2026, 10, 9, BRNO_LAT, BRNO_LON), tomorrow = sun(2026, 10, 10, BRNO_LAT, BRNO_LON);
+    TEST_ASSERT_EQUAL(ASTRO_NORMAL, today.kind);
+    ui_window_t day = window("sunrise", "sunset", 127);
+    assert_state(&day, (time_t)today.sunrise - 1, false, (time_t)today.sunrise);
+    assert_state(&day, (time_t)today.sunrise, true, (time_t)today.sunset);
+    assert_state(&day, (time_t)today.sunset, false, (time_t)tomorrow.sunrise);
+    ui_window_t shifted = window("sunrise+30", "sunset-45", 127);
+    assert_state(&shifted, (time_t)today.sunrise + 1799, false, (time_t)today.sunrise + 1800);
+    assert_state(&shifted, (time_t)today.sunrise + 1800, true, (time_t)today.sunset - 2700);
+}
+
+static void test_sunset_to_sunrise_spans_the_night(void)
+{
+    astro_sun_t today = sun(2026, 10, 9, BRNO_LAT, BRNO_LON), tomorrow = sun(2026, 10, 10, BRNO_LAT, BRNO_LON);
+    ui_window_t night = window("sunset", "sunrise", 127);
+    assert_state(&night, local_time(2026, 10, 9, 23, 0), true, (time_t)tomorrow.sunrise);
+    assert_state(&night, local_time(2026, 10, 9, 12, 0), false, (time_t)today.sunset);
+}
+
+/* A sun bound can fall on the day before the window's: the window opened on tomorrow is open late today. */
+static void test_a_window_can_start_the_evening_before_its_day(void)
+{
+    setenv("TZ", "GMT0", 1); /* Iceland */
+    tzset();
+    astro_sun_t day13 = sun(2026, 6, 13, REYKJAVIK_LAT, REYKJAVIK_LON);
+    TEST_ASSERT_EQUAL(ASTRO_NORMAL, day13.kind);
+    time_t opens = (time_t)day13.sunrise - 180 * 60; /* before midnight, on the 12th */
+    TEST_ASSERT_TRUE(opens < local_time(2026, 6, 13, 0, 0));
+    ui_window_t w = window("sunrise-180", "03:00", 127);
+    assert_state_at(&w, local_time(2026, 6, 12, 8, 34), REYKJAVIK_LAT, REYKJAVIK_LON, false, opens);
+    assert_state_at(&w, opens, REYKJAVIK_LAT, REYKJAVIK_LON, true, local_time(2026, 6, 13, 3, 0));
+    setenv("TZ", TZ_PRAGUE, 1);
+    tzset();
+    ui_window_t polar = window("sunrise-180", "22:00", 127); /* a polar day's sunrise is 00:00: 21:00 the day before */
+    time_t change = -1;
+    TEST_ASSERT_TRUE(ui_window_open(&polar, local_time(2026, 6, 21, 23, 0), SVALBARD_LAT, SVALBARD_LON, &change));
+    TEST_ASSERT_EQUAL_INT64(0, (int64_t)change); /* each day's window starts before the last one closes */
+}
+
+/* A time a DST change skips counts from the next valid minute; a repeated one from its first occurrence (§9.2). */
+static void test_dst_changes(void)
+{
+    ui_window_t w = window("02:30", "03:30", 127);
+    /* 2026-03-29: 02:00 CET becomes 03:00 CEST, so 02:30 is 03:00 (01:00 UTC); 03:30 CEST is 01:30 UTC. */
+    assert_state(&w, utc(2026, 3, 29, 0, 59), false, utc(2026, 3, 29, 1, 0));
+    assert_state(&w, utc(2026, 3, 29, 1, 0), true, utc(2026, 3, 29, 1, 30));
+    /* 2026-10-25: 03:00 CEST becomes 02:00 CET; 02:30 is its first (00:30 UTC), 03:30 is CET (02:30 UTC). */
+    assert_state(&w, utc(2026, 10, 25, 0, 30), true, utc(2026, 10, 25, 2, 30));
+    /* A window inside the spring gap doesn't exist that night: both ends are 03:00 CEST, it is empty, not a day
+     * long; the next night it is 02:00-02:30 CEST (00:00-00:30 UTC). */
+    ui_window_t gap = window("02:00", "02:30", 127);
+    assert_state(&gap, utc(2026, 3, 29, 1, 30), false, utc(2026, 3, 30, 0, 0));
+}
+
+/* Where the sun doesn't rise or set, sunrise and sunset fall back: a polar day is all sunrise-to-sunset, a polar
+ * night all sunset-to-sunrise. A window open for longer than the days it can see reports no change. */
+static void test_polar_days_and_nights(void)
+{
+    TEST_ASSERT_EQUAL(ASTRO_POLAR_DAY, sun(2026, 6, 21, SVALBARD_LAT, SVALBARD_LON).kind);
+    TEST_ASSERT_EQUAL(ASTRO_POLAR_NIGHT, sun(2026, 12, 21, SVALBARD_LAT, SVALBARD_LON).kind);
+    ui_window_t day = window("sunrise", "sunset", 127), night = window("sunset", "sunrise", 127);
+    time_t june = local_time(2026, 6, 21, 3, 0), december = local_time(2026, 12, 21, 12, 0), change = -1;
+    TEST_ASSERT_TRUE(ui_window_open(&day, june, SVALBARD_LAT, SVALBARD_LON, &change));
+    TEST_ASSERT_EQUAL_INT64(0, (int64_t)change);
+    TEST_ASSERT_FALSE(ui_window_open(&night, june, SVALBARD_LAT, SVALBARD_LON, &change));
+    TEST_ASSERT_EQUAL_INT64(0, (int64_t)change);
+    TEST_ASSERT_FALSE(ui_window_open(&day, december, SVALBARD_LAT, SVALBARD_LON, &change));
+    TEST_ASSERT_EQUAL_INT64(0, (int64_t)change);
+    TEST_ASSERT_TRUE(ui_window_open(&night, december, SVALBARD_LAT, SVALBARD_LON, &change));
+    TEST_ASSERT_EQUAL_INT64(0, (int64_t)change);
+}
+
+/* What `preset list` prints: the window, its days when not every day, and its state now. */
+static void test_the_console_text(void)
+{
+    char text[64];
+    ui_window_t solar = window("sunrise", "sunset-30", 127), fridays = window("22:00", "06:00", (1 << 4) | 1);
+    ui_window_text(&solar, text, sizeof(text));
+    TEST_ASSERT_EQUAL_STRING("sunrise - sunset-30", text);
+    ui_window_text(&fridays, text, sizeof(text));
+    TEST_ASSERT_EQUAL_STRING("22:00 - 06:00 on Mo Fr", text);
+    ui_window_t clock = window("07:00", "22:00", 127);
+    ui_window_state_text(&clock, local_time(2026, 10, 9, 12, 0), BRNO_LAT, BRNO_LON, text, sizeof(text));
+    TEST_ASSERT_EQUAL_STRING("open until 22:00", text);
+    ui_window_state_text(&clock, local_time(2026, 10, 9, 23, 0), BRNO_LAT, BRNO_LON, text, sizeof(text));
+    TEST_ASSERT_EQUAL_STRING("closed until Sat 07:00", text);
+    ui_window_state_text(&fridays, local_time(2026, 10, 10, 12, 0), BRNO_LAT, BRNO_LON, text, sizeof(text));
+    TEST_ASSERT_EQUAL_STRING("closed until Mon 22:00", text);
+    ui_window_state_text(&clock, 0, BRNO_LAT, BRNO_LON, text, sizeof(text));
+    TEST_ASSERT_EQUAL_STRING("open: the clock isn't set", text);
+}
+
+int main(void)
+{
+    UNITY_BEGIN();
+    RUN_TEST(test_bounds_read_and_write_back);
+    RUN_TEST(test_bad_bounds_are_refused);
+    RUN_TEST(test_a_window_is_made_or_refused);
+    RUN_TEST(test_no_window_is_always_open);
+    RUN_TEST(test_a_clock_window);
+    RUN_TEST(test_a_window_across_midnight);
+    RUN_TEST(test_days_are_the_days_it_opens_on);
+    RUN_TEST(test_a_sun_window_follows_the_sun);
+    RUN_TEST(test_sunset_to_sunrise_spans_the_night);
+    RUN_TEST(test_a_window_can_start_the_evening_before_its_day);
+    RUN_TEST(test_dst_changes);
+    RUN_TEST(test_polar_days_and_nights);
+    RUN_TEST(test_the_console_text);
+    return UNITY_END();
+}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -270,6 +270,7 @@ reflbo_host_test(test_ui_fields ui)
 reflbo_host_test(test_ui_preset ui)
 reflbo_host_test(test_ui_split ui)
 reflbo_host_test(test_ui_schedule ui)
+reflbo_host_test(test_ui_window ui astro)
 reflbo_host_test(test_ui_widget_fit ui)
 target_include_directories(test_ui_widget_fit PRIVATE ${REPO_ROOT}/components/ui) # ui_widget_draw(), fresh and stale
 reflbo_host_test(test_ui_menu ui)
```


- [ ] **Step 2: Run it to see it fail.**

Run: `cmake -S test/host -B build-host -G Ninja >/dev/null && cmake --build build-host --target test_ui_window 2>&1 | grep -E 'error:' | sed -E 's/.*error: //' | sort | uniq -c | sort -rn | head -4`
Expected:

```
   1 'ui_window.h' file not found
```

- [ ] **Step 3: The windows.** The host build finds the new source by its glob; the firmware's component lists it.

`components/ui/include/ui_window.h`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ui/include/ui_window.h
@@ -0,0 +1,45 @@
+#pragma once
+
+#include <stdbool.h>
+#include <stddef.h>
+#include <stdint.h>
+#include <time.h>
+
+/*
+ * A preset's cycle window (spec §5.4, M6e, D41): the auto-cycle visits the preset only while it is open. It
+ * opens at `from` on each day `days` names and closes at `until` that day, or the next day when `until` comes
+ * at or before `from` (two clock ends compared on the clock face, so one inside a DST gap is empty that night).
+ * Pure C, host-buildable.
+ */
+
+#define UI_BOUND_OFFSET_MAX 180 /* minutes either side of sunrise or sunset */
+#define UI_BOUND_TEXT_LEN 12    /* "sunrise-180" and its NUL */
+
+/* A bound in two bytes, as the RTC snapshot keeps presets: minutes after local midnight, 0-1439, or sunrise or
+ * sunset plus an offset of -180 to 180 minutes. */
+enum {
+    UI_BOUND_SUNRISE = 2000,
+    UI_BOUND_SUNSET = 3000,
+};
+
+typedef struct {
+    uint8_t days; /* bit 0 Monday ... bit 6 Sunday; 0: no window, always open */
+    int16_t from, until;
+} ui_window_t;
+
+/* "HH:MM", "sunrise", "sunset", or a sun bound with "+N" or "-N" minutes, N 1-180 without a leading zero. */
+bool ui_bound_parse(const char *text, int16_t *out);
+void ui_bound_format(int16_t bound, char *out, size_t size);
+/* A window from presets.json's or the console's texts; false with the reason in `err` (spec §5.4). */
+bool ui_window_make(const char *from, const char *until, int days, ui_window_t *out, char *err, size_t err_size);
+
+/* Whether `w` is open at `now`, with sunrise and sunset at the location (1e-4 degrees, as settings_t keeps it)
+ * and local times as TZ says. *change, if given, gets when that next changes: the closing time when open, the
+ * opening time when closed; 0 when nothing changes within the next 8 days (a polar day or night, no window). */
+bool ui_window_open(const ui_window_t *w, time_t now, int32_t lat_e4, int32_t lon_e4, time_t *change);
+
+/* For `preset list` (spec §15): "sunrise - sunset-30", with " on Mo Fr" unless every day. */
+void ui_window_text(const ui_window_t *w, char *out, size_t size);
+/* "open until 18:21", "closed until Sat 07:00" (the weekday when not today), "open", "closed"; `now` 0, the
+ * clock not set: "open: the clock isn't set", as every window counts as open then. */
+void ui_window_state_text(const ui_window_t *w, time_t now, int32_t lat_e4, int32_t lon_e4, char *out, size_t size);
```


`components/ui/ui_window.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ui/ui_window.c
@@ -0,0 +1,273 @@
+#define _POSIX_C_SOURCE 200809L /* localtime_r */
+
+#include "ui_window.h"
+
+#include <stdio.h>
+#include <string.h>
+
+#include "astro.h"
+#include "scheduler.h"
+#include "util_time.h"
+
+/* The windows that can be open now opened from two days back (one opened last evening closes this morning; a
+ * bound 3 h past a polar day's sunset falls on the next day) to tomorrow (a bound up to 3 h before sunrise, or a
+ * polar sunrise, falls this evening). A week's mask opens within 8 days. */
+#define LOOK_BACK_DAYS 2
+#define LOOK_AHEAD_DAYS 8
+
+static const char *const k_day_names[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" }; /* bit 0 is Monday */
+static const char *const k_weekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" }; /* tm_wday */
+
+static bool is_sun(int16_t b)
+{
+    return b >= UI_BOUND_SUNRISE - UI_BOUND_OFFSET_MAX;
+}
+
+static bool is_sunrise(int16_t b)
+{
+    return b < UI_BOUND_SUNSET - UI_BOUND_OFFSET_MAX;
+}
+
+bool ui_bound_parse(const char *text, int16_t *out)
+{
+    bool rise = strncmp(text, "sunrise", 7) == 0;
+    if (rise || strncmp(text, "sunset", 6) == 0) {
+        const char *p = text + (rise ? 7 : 6);
+        int base = rise ? UI_BOUND_SUNRISE : UI_BOUND_SUNSET;
+        if (*p == '\0') {
+            *out = (int16_t)base;
+            return true;
+        }
+        if ((*p != '+' && *p != '-') || p[1] < '1' || p[1] > '9') {
+            return false;
+        }
+        int sign = *p == '-' ? -1 : 1, value = 0, digits = 0;
+        for (p++; *p >= '0' && *p <= '9' && digits < 4; p++, digits++) {
+            value = value * 10 + (*p - '0');
+        }
+        if (*p != '\0' || value > UI_BOUND_OFFSET_MAX) {
+            return false;
+        }
+        *out = (int16_t)(base + sign * value);
+        return true;
+    }
+    if (strlen(text) != 5 || text[2] != ':') {
+        return false;
+    }
+    for (int i = 0; i < 5; i++) {
+        if (i != 2 && (text[i] < '0' || text[i] > '9')) {
+            return false;
+        }
+    }
+    int h = (text[0] - '0') * 10 + (text[1] - '0'), m = (text[3] - '0') * 10 + (text[4] - '0');
+    if (h > 23 || m > 59) {
+        return false;
+    }
+    *out = (int16_t)(h * 60 + m);
+    return true;
+}
+
+void ui_bound_format(int16_t bound, char *out, size_t size)
+{
+    if (!is_sun(bound)) {
+        snprintf(out, size, "%02d:%02d", bound / 60 % 24, bound % 60);
+        return;
+    }
+    bool rise = is_sunrise(bound);
+    int offset = bound - (rise ? UI_BOUND_SUNRISE : UI_BOUND_SUNSET);
+    if (offset == 0) {
+        snprintf(out, size, "%s", rise ? "sunrise" : "sunset");
+    } else {
+        snprintf(out, size, "%s%+d", rise ? "sunrise" : "sunset", offset);
+    }
+}
+
+bool ui_window_make(const char *from, const char *until, int days, ui_window_t *out, char *err, size_t err_size)
+{
+    static const char k_bound_rule[] = "must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes";
+    ui_window_t w;
+    if (!ui_bound_parse(from, &w.from)) {
+        snprintf(err, err_size, "from %s", k_bound_rule);
+        return false;
+    }
+    if (!ui_bound_parse(until, &w.until)) {
+        snprintf(err, err_size, "until %s", k_bound_rule);
+        return false;
+    }
+    if (w.from == w.until) {
+        snprintf(err, err_size, "from and until can't be the same");
+        return false;
+    }
+    if (days < 1 || days > 0x7F) {
+        snprintf(err, err_size, "days must be 1-127");
+        return false;
+    }
+    out->days = (uint8_t)days; /* field by field: a preset's padding stays as its memset left it */
+    out->from = w.from;
+    out->until = w.until;
+    return true;
+}
+
+/* The zone's UTC offset at local noon of a date, as astro_sun() takes it. */
+static int32_t noon_offset(int year, int month, int day)
+{
+    struct tm noon = { .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = 12, .tm_isdst = -1 };
+    time_t t = mktime(&noon);
+    return (int32_t)(util_days_from_civil(year, month, day) * 86400 + 12 * 3600 - (int64_t)t);
+}
+
+/* The sun on a local day at a place; a few days are kept, as the S3 computes in software doubles. */
+static astro_sun_t sun_on(int64_t day, int32_t lat_e4, int32_t lon_e4)
+{
+    static struct {
+        int64_t day;
+        int32_t lat, lon;
+        astro_sun_t sun;
+        bool used;
+    } cache[4];
+    static int next;
+    for (int i = 0; i < 4; i++) {
+        if (cache[i].used && cache[i].day == day && cache[i].lat == lat_e4 && cache[i].lon == lon_e4) {
+            return cache[i].sun;
+        }
+    }
+    int y, m, d;
+    util_civil_from_days(day, &y, &m, &d);
+    astro_sun_t sun;
+    astro_sun(y, m, d, noon_offset(y, m, d), lat_e4, lon_e4, &sun);
+    cache[next].day = day;
+    cache[next].lat = lat_e4;
+    cache[next].lon = lon_e4;
+    cache[next].sun = sun;
+    cache[next].used = true;
+    next = (next + 1) % 4;
+    return sun;
+}
+
+static time_t at_minute(int64_t day, int minute)
+{
+    int y, m, d;
+    util_civil_from_days(day, &y, &m, &d);
+    return sched_local_to_utc(y, m, d, minute); /* a skipped minute is the next valid one (spec §9.2) */
+}
+
+/* A bound on a local day. Without a sunrise or sunset, a polar day runs from midnight to midnight and a polar
+ * night from midnight to midnight the other way round: sunrise at the day's end, sunset at its start. */
+static time_t bound_at(int16_t b, int64_t day, int32_t lat_e4, int32_t lon_e4)
+{
+    if (!is_sun(b)) {
+        return at_minute(day, b);
+    }
+    bool rise = is_sunrise(b);
+    int offset = b - (rise ? UI_BOUND_SUNRISE : UI_BOUND_SUNSET);
+    astro_sun_t sun = sun_on(day, lat_e4, lon_e4);
+    time_t base;
+    if (sun.kind == ASTRO_NORMAL) {
+        base = (time_t)(rise ? sun.sunrise : sun.sunset);
+    } else if (sun.kind == ASTRO_POLAR_DAY) {
+        base = at_minute(rise ? day : day + 1, 0);
+    } else {
+        base = at_minute(rise ? day + 1 : day, 0);
+    }
+    return base + (time_t)offset * 60;
+}
+
+static int64_t local_day(time_t t)
+{
+    struct tm local;
+    localtime_r(&t, &local);
+    return util_days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
+}
+
+/* The window that opens on `day`, [*start, *end); false when it doesn't open that day or would be empty. */
+static bool span(const ui_window_t *w, int64_t day, int32_t lat_e4, int32_t lon_e4, time_t *start, time_t *end)
+{
+    int monday_first = (int)((day % 7 + 7 + 3) % 7); /* day 0, 1970-01-01, was a Thursday */
+    if (!(w->days & (1u << monday_first))) {
+        return false;
+    }
+    *start = bound_at(w->from, day, lat_e4, lon_e4);
+    *end = bound_at(w->until, day, lat_e4, lon_e4);
+    /* Two clock ends cross midnight by the clock face: 02:00-02:30 in a spring gap, both 03:00, is empty that
+     * night rather than a day long. */
+    bool clock = !is_sun(w->from) && !is_sun(w->until);
+    if (clock ? w->until <= w->from : *end <= *start) {
+        *end = bound_at(w->until, day + 1, lat_e4, lon_e4);
+    }
+    return *end > *start;
+}
+
+bool ui_window_open(const ui_window_t *w, time_t now, int32_t lat_e4, int32_t lon_e4, time_t *change)
+{
+    if (change != NULL) {
+        *change = 0;
+    }
+    if (w->days == 0) {
+        return true;
+    }
+    int64_t today = local_day(now);
+    for (int64_t d = today - LOOK_BACK_DAYS; d <= today + 1; d++) {
+        time_t start, end;
+        if (!span(w, d, lat_e4, lon_e4, &start, &end) || now < start || now >= end) {
+            continue;
+        }
+        bool joined = true; /* the next day's window starts where this one ends: they are one */
+        for (int64_t e = d + 1; joined && e <= today + LOOK_AHEAD_DAYS; e++) {
+            time_t s2, e2;
+            joined = span(w, e, lat_e4, lon_e4, &s2, &e2) && s2 <= end;
+            if (joined && e2 > end) {
+                end = e2;
+            }
+        }
+        if (change != NULL && !joined) {
+            *change = end;
+        }
+        return true;
+    }
+    for (int64_t d = today - LOOK_BACK_DAYS; d <= today + LOOK_AHEAD_DAYS; d++) {
+        time_t start, end;
+        if (span(w, d, lat_e4, lon_e4, &start, &end) && start > now) {
+            if (change != NULL) {
+                *change = start;
+            }
+            break;
+        }
+    }
+    return false;
+}
+
+void ui_window_text(const ui_window_t *w, char *out, size_t size)
+{
+    char from[UI_BOUND_TEXT_LEN], until[UI_BOUND_TEXT_LEN];
+    ui_bound_format(w->from, from, sizeof(from));
+    ui_bound_format(w->until, until, sizeof(until));
+    size_t n = (size_t)snprintf(out, size, "%s - %s", from, until);
+    if ((w->days & 0x7F) != 0x7F && n < size) {
+        n += (size_t)snprintf(out + n, size - n, " on");
+        for (int bit = 0; bit < 7 && n < size; bit++) {
+            if (w->days & (1u << bit)) {
+                n += (size_t)snprintf(out + n, size - n, " %s", k_day_names[bit]);
+            }
+        }
+    }
+}
+
+void ui_window_state_text(const ui_window_t *w, time_t now, int32_t lat_e4, int32_t lon_e4, char *out, size_t size)
+{
+    if (now == 0) {
+        snprintf(out, size, "open: the clock isn't set");
+        return;
+    }
+    time_t change;
+    bool open = ui_window_open(w, now, lat_e4, lon_e4, &change);
+    if (change == 0) {
+        snprintf(out, size, "%s", open ? "open" : "closed");
+        return;
+    }
+    struct tm a, b;
+    localtime_r(&now, &a);
+    localtime_r(&change, &b);
+    bool same_day = a.tm_year == b.tm_year && a.tm_yday == b.tm_yday;
+    snprintf(out, size, "%s until %s%s%02d:%02d", open ? "open" : "closed", same_day ? "" : k_weekdays[b.tm_wday],
+             same_day ? "" : " ", b.tm_hour, b.tm_min);
+}
```


`components/ui/CMakeLists.txt`:

```diff
--- a/components/ui/CMakeLists.txt
+++ b/components/ui/CMakeLists.txt
@@ -2,7 +2,7 @@
 idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                             "ui_status.c" "ui_dashboard.c" "ui_schedule.c" "ui_menu.c" "ui_menu_draw.c"
                             "ui_screens.c" "ui_config.c" "ui_catalog.c" "ui_forecast.c" "ui_radar.c"
-                            "ui_flights.c" "ui_split.c" "ui_solar.c"
+                            "ui_flights.c" "ui_split.c" "ui_solar.c" "ui_window.c"
                        INCLUDE_DIRS "include"
                        REQUIRES gfx locale datastore util map radar adsb solar energy
                        PRIV_REQUIRES json scheduler weather astro)
```


- [ ] **Step 4: Run the tests.**

Run: `cmake -S test/host -B build-host -G Ninja >/dev/null && cmake --build build-host >/dev/null 2>&1 && ./build-host/test_ui_window | tail -2 && ctest --test-dir build-host | tail -3`
Expected:

```
13 Tests 0 Failures 0 Ignored
OK
100% tests passed, 0 tests failed out of 67
```

- [ ] **Step 5: The firmware builds:** `tools/idf.sh build`, clean, without a warning. The image doesn't grow yet: nothing calls the windows until Task 2.

- [ ] **Step 6: Commit.**

```bash
git add components/ui test/host/CMakeLists.txt test/host/test_ui_window.c
git commit -m "feat(ui): cycle windows: bounds, days and when they are open (spec §5.4, D41)"
```

### Task 2: Windows in `presets.json` and the auto-cycle (`ui`, `main`)

**Files:**
- Modify: `components/ui/include/ui_preset.h`, `components/ui/ui_preset.c`, `components/ui/ui_preset_json.c`, `main/app.c`
- Test: `test/host/test_ui_preset.c`

**Interfaces:**
- Consumes: Task 1's `ui_window_t`, `ui_window_make()`, `ui_window_open()`, `ui_bound_format()`, `UI_BOUND_SUNRISE`, `UI_BOUND_SUNSET`, `UI_BOUND_TEXT_LEN`.
- Produces:
  - `ui_preset_t.window` (`ui_window_t`, after `in_cycle`; `days` 0: always open);
  - `int ui_presets_cycle_next(const ui_presets_t *p, bool always, time_t now, int32_t lat_e4, int32_t lon_e4)`: as `ui_presets_next()`, skipping presets whose window is closed at `now`; the active one when no other is open; `now` 0 (the clock not set): every window open. `ui_presets_next()`, KEY short's, becomes its `now` 0 case;
  - `presets.json`'s `"window": { "from": …, "until": …, "days": … }` per preset, written only when set;
  - `SNAP_VERSION` 12.

The presets carry their windows (spec §5.4): the parser reads an optional `"window"` beside `in_cycle`, through Task 1's `ui_window_make()`, so the file and the console refuse the same things with the same words, prefixed with the preset: "preset \"a\": window: from and until can't be the same". `null` is no window; `days` missing is every day; `days` that isn't a whole number from 1 to 127 is refused, as is a window that isn't an object. The writer writes a window only where there is one, so a file without windows stays as it was.

Only the auto-cycle looks at windows: `ui_presets_cycle_next()` steps to the next preset in cycle order that is in the cycle, on the Flights layout only in sync mode `always` (D28), and open at `now`; with none, the active one stays. KEY short, `cmd/next` (M7), schedule entries, the menu and the web UI keep using `ui_presets_next()` or select a preset directly, so they reach every preset. The built-in Solar preset, offered to new setups and to files from before M6d, has sunrise → sunset; it stays outside the cycle (M6d).

The presets ride in the RTC snapshot, so its layout changes: `SNAP_VERSION` goes to 12, and the snapshot grows by 112 bytes to 5 968 of its 6 144. A flash or an update resets the chip anyway, so the first boot after one is cold. The largest `presets.json`, M6c's 16 split presets of 24 cells, each with a window at its longest, is 38 238 bytes of its 48 KB.

- [ ] **Step 1: Write the failing tests.** The auto-cycle skipping a closed window while KEY short doesn't, without a valid clock, and with Flights; the one on screen staying when none is open; Solar's daylight window; a window's round trip, a file without one, days missing, `null`; each refusal; the largest file with windows:

`test/host/test_ui_preset.c`:

```diff
--- a/test/host/test_ui_preset.c
+++ b/test/host/test_ui_preset.c
@@ -1,5 +1,9 @@
+#define _POSIX_C_SOURCE 200809L /* setenv */
+
 #include <stdio.h>
+#include <stdlib.h>
 #include <string.h>
+#include <time.h>
 
 #include "ui_fields.h"
 #include "ui_preset.h"
@@ -75,6 +79,124 @@ static void test_the_cycle_visits_flights_only_in_sync_mode_always(void)
     TEST_ASSERT_EQUAL_INT(5, ui_presets_next(&s_p, true));
 }
 
+/* Europe/Prague and Brno, the defaults (AGENTS.md §1), for the cycle windows (spec §5.4, D41). */
+#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"
+#define BRNO_LAT 491951
+#define BRNO_LON 166068
+
+static time_t local_time(int y, int mo, int d, int h, int mi)
+{
+    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi, .tm_isdst = -1 };
+    return mktime(&t);
+}
+
+static int cycle_next(bool always, time_t now)
+{
+    return ui_presets_cycle_next(&s_p, always, now, BRNO_LAT, BRNO_LON);
+}
+
+/* The auto-cycle skips a preset whose window is closed; KEY short (ui_presets_next) doesn't (D41). */
+static void test_the_auto_cycle_skips_a_closed_window(void)
+{
+    setenv("TZ", TZ_PRAGUE, 1);
+    tzset();
+    TEST_ASSERT_TRUE(ui_window_make("07:00", "08:00", 127, &s_p.presets[1].window, s_err, sizeof(s_err)));
+    time_t noon = local_time(2026, 10, 9, 12, 0), morning = local_time(2026, 10, 9, 7, 30);
+    TEST_ASSERT_EQUAL_INT(2, cycle_next(true, noon));  /* home -> weather, past indoor's closed window */
+    TEST_ASSERT_EQUAL_INT(1, cycle_next(true, morning)); /* open: home -> indoor */
+    TEST_ASSERT_EQUAL_INT(1, ui_presets_next(&s_p, true)); /* KEY short still reaches it */
+    TEST_ASSERT_EQUAL_INT(1, cycle_next(true, 0));       /* no valid time: every window open */
+    s_p.active = 4;                                      /* rain: flights still only in sync mode always */
+    TEST_ASSERT_EQUAL_INT(5, cycle_next(true, noon));
+    TEST_ASSERT_EQUAL_INT(0, cycle_next(false, noon));
+}
+
+/* With no other preset in the cycle open, the one on screen stays, its own window open or not. */
+static void test_the_auto_cycle_keeps_the_preset_when_none_is_open(void)
+{
+    setenv("TZ", TZ_PRAGUE, 1);
+    tzset();
+    for (int i = 0; i < s_p.count; i++) {
+        TEST_ASSERT_TRUE(ui_window_make("07:00", "08:00", 127, &s_p.presets[i].window, s_err, sizeof(s_err)));
+    }
+    s_p.active = 2;
+    TEST_ASSERT_EQUAL_INT(2, cycle_next(true, local_time(2026, 10, 9, 12, 0)));
+    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p, true));
+}
+
+/* The built-in Solar preset has a window from sunrise to sunset; the others none (spec §5.4, M6e). */
+static void test_the_built_in_solar_preset_has_a_daylight_window(void)
+{
+    for (int i = 0; i < s_p.count; i++) {
+        const ui_window_t *w = &s_p.presets[i].window;
+        if (strcmp(s_p.presets[i].id, "solar") == 0) {
+            TEST_ASSERT_EQUAL_UINT8(0x7F, w->days);
+            TEST_ASSERT_EQUAL_INT16(UI_BOUND_SUNRISE, w->from);
+            TEST_ASSERT_EQUAL_INT16(UI_BOUND_SUNSET, w->until);
+        } else {
+            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, w->days, s_p.presets[i].id);
+        }
+    }
+}
+
+static void test_a_window_survives_a_round_trip(void)
+{
+    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"window\": "
+                       "{\"from\": \"sunrise+30\", \"until\": \"22:00\", \"days\": 31}}, "
+                       "{\"id\": \"b\", \"layout\": \"grid\"}]}";
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(31, s_p.presets[0].window.days);
+    TEST_ASSERT_EQUAL_INT16(UI_BOUND_SUNRISE + 30, s_p.presets[0].window.from);
+    TEST_ASSERT_EQUAL_INT16(22 * 60, s_p.presets[0].window.until);
+    TEST_ASSERT_EQUAL_UINT8(0, s_p.presets[1].window.days); /* none: always open */
+    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"window\":{\"from\":\"sunrise+30\",\"until\":\"22:00\",\"days\":31}"));
+    TEST_ASSERT_NULL(strstr(strstr(s_json, "\"id\":\"b\""), "\"window\"")); /* not for b */
+    ui_presets_t back;
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
+}
+
+/* A window without days opens every day; `null` is no window. */
+static void test_a_window_without_days_opens_every_day(void)
+{
+    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"window\": "
+                       "{\"from\": \"sunset\", \"until\": \"sunrise\"}}, "
+                       "{\"id\": \"b\", \"layout\": \"grid\", \"window\": null}]}";
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(0x7F, s_p.presets[0].window.days);
+    TEST_ASSERT_EQUAL_UINT8(0, s_p.presets[1].window.days);
+}
+
+static void test_bad_windows_are_rejected_with_a_reason(void)
+{
+    static const struct {
+        const char *window, *why;
+    } k_cases[] = {
+        { "\"sunrise\"", "preset \"a\": window must be an object" },
+        { "{\"from\": \"dawn\", \"until\": \"sunset\"}",
+          "preset \"a\": window: from must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes" },
+        { "{\"from\": \"07:00\"}",
+          "preset \"a\": window: until must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes" },
+        { "{\"from\": \"sunset+0\", \"until\": \"23:00\"}",
+          "preset \"a\": window: from must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes" },
+        { "{\"from\": \"07:00\", \"until\": \"07:00\"}", "preset \"a\": window: from and until can't be the same" },
+        { "{\"from\": \"07:00\", \"until\": \"22:00\", \"days\": 0}", "preset \"a\": window: days must be 1-127" },
+        { "{\"from\": \"07:00\", \"until\": \"22:00\", \"days\": 255}", "preset \"a\": window: days must be 1-127" },
+        { "{\"from\": \"07:00\", \"until\": \"22:00\", \"days\": \"Mo\"}", "preset \"a\": window: days must be 1-127" },
+        { "{\"from\": \"07:00\", \"until\": \"22:00\", \"days\": 3.5}", "preset \"a\": window: days must be 1-127" },
+    };
+    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
+        char json[256];
+        snprintf(json, sizeof(json),
+                 "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"grid\", \"window\": %s}]}",
+                 k_cases[i].window);
+        s_err[0] = '\0';
+        TEST_ASSERT_FALSE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), json);
+        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
+    }
+}
+
 /* A presets.json saved by M5: the four presets of its day, no marker. */
 static const char k_m5_file[] =
     "{\"schema\":1,\"active\":\"weather\",\"presets\":["
@@ -560,6 +682,7 @@ static void test_a_full_set_of_split_presets_fits_the_save_buffer(void)
         p->clock = UI_CLOCK_12H;
         p->stale_policy = UI_STALE_PLACEHOLDER;
         p->status_battery = UI_STATUS_BAT_PERCENT | UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
+        TEST_ASSERT_TRUE(ui_window_make("sunrise-180", "sunset-180", 0x7E, &p->window, s_err, sizeof(s_err))); /* M6e */
     }
     s_p.count = UI_PRESET_MAX;
     s_p.cycle_interval_s = UI_CYCLE_MAX_S;
@@ -628,6 +751,12 @@ int main(void)
     RUN_TEST(test_defaults_are_the_eight_built_ins);
     RUN_TEST(test_next_follows_cycle_order_and_skips_presets_out_of_it);
     RUN_TEST(test_the_cycle_visits_flights_only_in_sync_mode_always);
+    RUN_TEST(test_the_auto_cycle_skips_a_closed_window);
+    RUN_TEST(test_the_auto_cycle_keeps_the_preset_when_none_is_open);
+    RUN_TEST(test_the_built_in_solar_preset_has_a_daylight_window);
+    RUN_TEST(test_a_window_survives_a_round_trip);
+    RUN_TEST(test_a_window_without_days_opens_every_day);
+    RUN_TEST(test_bad_windows_are_rejected_with_a_reason);
     RUN_TEST(test_a_file_from_before_m6_gains_the_radars_once);
     RUN_TEST(test_a_file_from_m6c_gains_solar_and_energy_once);
     RUN_TEST(test_the_radars_need_room_and_a_free_id);
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host --target test_ui_preset 2>&1 | grep -E 'error:' | sed -E 's/.*error: //' | sort | uniq -c | sort -rn | head -6`
Expected:

```
  10 no member named 'window' in 'ui_preset_t'
   3 call to undeclared function 'ui_window_make'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
   2 use of undeclared identifier 'UI_BOUND_SUNRISE'
   1 use of undeclared identifier 'UI_BOUND_SUNSET'
   1 unknown type name 'ui_window_t'
   1 call to undeclared function 'ui_presets_cycle_next'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
```

- [ ] **Step 3: The window in a preset, the auto-cycle, Solar's window.**

`components/ui/include/ui_preset.h`:

```diff
--- a/components/ui/include/ui_preset.h
+++ b/components/ui/include/ui_preset.h
@@ -7,6 +7,7 @@
 
 #include "ui_layout.h"
 #include "ui_split.h"
+#include "ui_window.h"
 
 /*
  * Presets (spec §5.4): a layout, its slot bindings and options, stored in /cfg/presets.json.
@@ -47,6 +48,7 @@ typedef struct {
     char name[UI_PRESET_NAME_LEN];
     uint8_t layout; /* ui_layout_id_t */
     bool in_cycle;
+    ui_window_t window; /* the auto-cycle skips the preset while it is closed (spec §5.4, D41); days 0: none */
     uint8_t slots[UI_SLOT_MAX]; /* ui_field_id_t per slot, in the layout's slot order, or per cell */
     uint8_t split[UI_SPLIT_NODES]; /* the split layout's tree (ui_split.h); all 0 for the others */
     uint8_t clock;              /* ui_clock_mode_t */
@@ -123,6 +125,10 @@ int ui_presets_find(const ui_presets_t *p, const char *id); /* index, or -1 */
  * preset is in the cycle (spec §5.4, KEY short). Presets on the Flights layout are in it only in
  * sync mode `always` (D28). */
 int ui_presets_next(const ui_presets_t *p, bool always);
+/* The auto-cycle's next preset (spec §5.4, D41): as ui_presets_next(), skipping presets whose cycle window is
+ * closed at `now` at the location (1e-4 degrees); the active one when no other is open. `now` 0, the clock not
+ * set: every window counts as open. */
+int ui_presets_cycle_next(const ui_presets_t *p, bool always, time_t now, int32_t lat_e4, int32_t lon_e4);
 /* Offers the built-ins `offered` doesn't name yet: each joins at the end if its id is free and
  * there is room, and is marked offered either way. True if anything changed: save the file. */
 bool ui_presets_offer_builtins(ui_presets_t *p);
```


`components/ui/ui_preset.c`:

```diff
--- a/components/ui/ui_preset.c
+++ b/components/ui/ui_preset.c
@@ -44,17 +44,17 @@ void ui_presets_defaults(ui_presets_t *p)
 }
 
 /* The built-ins added after M5, whose layouts have no slots: the two radars (D28) in the cycle, Solar and Energy
- * (D35, D36) outside it. */
+ * (D35, D36) outside it; Solar's cycle window runs from sunrise to sunset (M6e, D41). */
 static const struct {
     uint8_t bit;
     const char *id, *name;
     ui_layout_id_t layout;
-    bool in_cycle;
+    bool in_cycle, daylight;
 } k_offered[] = {
-    { UI_OFFERED_RAIN, "rain", "Rain radar", UI_LAYOUT_RADAR, true },
-    { UI_OFFERED_FLIGHTS, "flights", "Flights", UI_LAYOUT_FLIGHTS, true },
-    { UI_OFFERED_SOLAR, "solar", "Solar", UI_LAYOUT_SOLAR, false },
-    { UI_OFFERED_ENERGY, "energy", "Energy", UI_LAYOUT_ENERGY, false },
+    { UI_OFFERED_RAIN, "rain", "Rain radar", UI_LAYOUT_RADAR, true, false },
+    { UI_OFFERED_FLIGHTS, "flights", "Flights", UI_LAYOUT_FLIGHTS, true, false },
+    { UI_OFFERED_SOLAR, "solar", "Solar", UI_LAYOUT_SOLAR, false, true },
+    { UI_OFFERED_ENERGY, "energy", "Energy", UI_LAYOUT_ENERGY, false, false },
 };
 
 bool ui_presets_offer_builtins(ui_presets_t *p)
@@ -69,6 +69,9 @@ bool ui_presets_offer_builtins(ui_presets_t *p)
             *added = make(k_offered[i].id, k_offered[i].name, k_offered[i].layout, k_offered[i].in_cycle,
                           (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_NONE });
             added->status_clock = true; /* the view fills the screen: the time goes to the status bar */
+            if (k_offered[i].daylight) {
+                added->window = (ui_window_t){ .days = 0x7F, .from = UI_BOUND_SUNRISE, .until = UI_BOUND_SUNSET };
+            }
         }
         p->offered |= k_offered[i].bit;
         changed = true;
@@ -97,10 +100,17 @@ int ui_presets_find(const ui_presets_t *p, const char *id)
 }
 
 int ui_presets_next(const ui_presets_t *p, bool always)
+{
+    return ui_presets_cycle_next(p, always, 0, 0, 0);
+}
+
+int ui_presets_cycle_next(const ui_presets_t *p, bool always, time_t now, int32_t lat_e4, int32_t lon_e4)
 {
     for (int step = 1; step < p->count; step++) {
         int i = (p->active + step) % p->count;
-        if (p->presets[i].in_cycle && (always || p->presets[i].layout != UI_LAYOUT_FLIGHTS)) {
+        const ui_preset_t *q = &p->presets[i];
+        if (q->in_cycle && (always || q->layout != UI_LAYOUT_FLIGHTS) &&
+            (now == 0 || ui_window_open(&q->window, now, lat_e4, lon_e4, NULL))) {
             return i;
         }
     }
```


- [ ] **Step 4: `presets.json`.**

`components/ui/ui_preset_json.c`:

```diff
--- a/components/ui/ui_preset_json.c
+++ b/components/ui/ui_preset_json.c
@@ -192,6 +192,27 @@ static bool parse_split(const cJSON *split, ui_preset_t *out, char *err, size_t
     return true;
 }
 
+/* A preset's cycle window (spec §5.4, D41): from, until and days, checked as the console checks them. */
+static bool parse_window(const cJSON *window, ui_preset_t *out, char *err, size_t size)
+{
+    if (!cJSON_IsObject(window)) {
+        return fail(err, size, "preset \"%s\": window must be an object", out->id);
+    }
+    const cJSON *from = cJSON_GetObjectItemCaseSensitive(window, "from");
+    const cJSON *until = cJSON_GetObjectItemCaseSensitive(window, "until");
+    const cJSON *days = cJSON_GetObjectItemCaseSensitive(window, "days");
+    int mask = 0x7F;
+    if (days != NULL) {
+        mask = cJSON_IsNumber(days) && days->valuedouble == (double)days->valueint ? days->valueint : -1;
+    }
+    char why[96];
+    if (!ui_window_make(cJSON_IsString(from) ? from->valuestring : "", cJSON_IsString(until) ? until->valuestring : "",
+                        mask, &out->window, why, sizeof(why))) {
+        return fail(err, size, "preset \"%s\": window: %s", out->id, why);
+    }
+    return true;
+}
+
 static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t size)
 {
     memset(out, 0, sizeof(*out));
@@ -213,6 +234,10 @@ static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t
     }
     out->layout = (uint8_t)layout;
     out->in_cycle = optional_bool(item, "in_cycle", true);
+    const cJSON *window = cJSON_GetObjectItemCaseSensitive(item, "window");
+    if (window != NULL && !cJSON_IsNull(window) && !parse_window(window, out, err, size)) {
+        return false;
+    }
     const cJSON *slots = cJSON_GetObjectItemCaseSensitive(item, "slots");
     if (slots != NULL && !cJSON_IsNull(slots) &&
         !parse_slots(slots, ui_layout((ui_layout_id_t)layout), out, err, size)) {
@@ -406,6 +431,15 @@ static cJSON *preset_json(const ui_preset_t *p)
     cJSON_AddStringToObject(obj, "name", p->name);
     cJSON_AddStringToObject(obj, "layout", layout->id);
     cJSON_AddBoolToObject(obj, "in_cycle", p->in_cycle);
+    if (p->window.days != 0) {
+        char from[UI_BOUND_TEXT_LEN], until[UI_BOUND_TEXT_LEN];
+        ui_bound_format(p->window.from, from, sizeof(from));
+        ui_bound_format(p->window.until, until, sizeof(until));
+        cJSON *window = cJSON_AddObjectToObject(obj, "window");
+        cJSON_AddStringToObject(window, "from", from);
+        cJSON_AddStringToObject(window, "until", until);
+        cJSON_AddNumberToObject(window, "days", p->window.days);
+    }
     if (p->layout == UI_LAYOUT_SPLIT) {
         int at = 0, cell = 0;
         bool whole = ui_split_nodes(p->split) > 0; /* a tree cut short can't be walked: one empty cell */
```


- [ ] **Step 5: The snapshot's version.**

`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -49,9 +49,9 @@
 #define TETHER_RECHECK_MS 1000
 #define RETRY_S           300  /* after a failed boot with no PC attached */
 #define SNAP_MAGIC        0x72666c62u /* "rflb" */
-#define SNAP_VERSION      11 /* 6: the weather, the air quality and the syncs' state; 7: the rain; 8: split presets;
+#define SNAP_VERSION      12 /* 6: the weather, the air quality and the syncs' state; 7: the rain; 8: split presets;
                                    9: 24 cells (M6c); 10: the solar state and the sync's two steps (M6d);
-                                   11: the Developer API's plant (D37) */
+                                   11: the Developer API's plant (D37); 12: the presets' cycle windows (D41) */
 #define PEEK_MS           60000 /* a button during the night shows the dashboard this long (spec §9.1) */
 #define NIGHT_RECHECK_S   60    /* a night sleep with a button held looks again this often (D16) */
 #define CRITICAL_RECHECK_S 600  /* the critical sleep checks again this often if KEY is held */
```


- [ ] **Step 6: Run the tests.**

Run: `cmake --build build-host >/dev/null 2>&1 && ./build-host/test_ui_preset | grep -E 'largest presets|Tests' && ctest --test-dir build-host | tail -3`
Expected:

```
…/test/host/test_ui_preset.c:700:test_a_full_set_of_split_presets_fits_the_save_buffer:INFO: the largest presets.json: 38238 bytes
37 Tests 0 Failures 0 Ignored
100% tests passed, 0 tests failed out of 67
```

- [ ] **Step 7: The firmware builds:** `tools/idf.sh build`, clean, without a warning; the snapshot's `_Static_assert` (6 KB) holds.

- [ ] **Step 8: Commit.**

```bash
git add components/ui main/app.c test/host/test_ui_preset.c
git commit -m "feat(ui): presets' cycle windows in presets.json and the auto-cycle (spec §5.4, D41)"
```

### Task 3: The auto-cycle's windows and the console (`main`)

**Files:**
- Modify: `main/app_ui.c`, `main/app_cmds.c`, `AGENTS.md`

**Interfaces:**
- Consumes: Task 2's `ui_presets_cycle_next()` and `ui_preset_t.window`; Task 1's `ui_window_make()`, `ui_window_text()`, `ui_window_state_text()`; `timekeeping_valid()`, `app_settings()` (`lat_e4`, `lon_e4`), `app_presets()`, `app_ui_save_presets()`, `ui_presets_find()`.
- Produces: the console's `preset window <id> <from> <until> [days]` and `preset window <id> off`; `preset list`'s window lines.

The app's cycle step (`app_ui_tick()`) asks `ui_presets_cycle_next()` with the time when the clock is valid, else 0, and the location saved now, so a new place or zone counts from the next step. No wake-up is added: a window is looked at only when the cycle steps, and a preset on screen when its window closes stays until then (spec §5.4).

The console (spec §15), on the app task through `diag_on_owner()` as the other `preset` commands:
- `preset list` prints, under a preset with a window, `    window sunrise - sunset: open until 18:21`.
- `preset window solar sunrise sunset` sets one, every day; a fifth word gives the days, the Mon–Sun mask as `schedule add` takes it (`31`, `0x1f`). `preset window solar off` removes it. Either is saved as the web page saves it, and printed with its state; a window `ui_window_make()` refuses prints its reason ("preset: from and until can't be the same") and changes nothing.

- [ ] **Step 1: The cycle step.**

`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -617,7 +617,9 @@ void app_ui_tick(bool force)
         s.cycle_at = now + s.presets.cycle_interval_s; /* the first tick, or the clock moved back */
     }
     if (s.presets.cycle_enabled && now >= s.cycle_at) {
-        app_ui_select(ui_presets_next(&s.presets, s.settings.sync_mode == SETTINGS_SYNC_ALWAYS),
+        time_t at = timekeeping_valid() ? now : 0; /* without a valid clock every window is open (spec §5.4) */
+        app_ui_select(ui_presets_cycle_next(&s.presets, s.settings.sync_mode == SETTINGS_SYNC_ALWAYS, at,
+                                            s.settings.lat_e4, s.settings.lon_e4),
                       false); /* renders; not saved, the cycle will move on */
         return;
     }
```


- [ ] **Step 2: The console.**

`main/app_cmds.c`:

```diff
--- a/main/app_cmds.c
+++ b/main/app_cmds.c
@@ -9,6 +9,7 @@
 #include "esp_log.h"
 #include "lang.h"
 #include "netmgr.h"
+#include "timekeeping.h"
 #include "ui_fields.h"
 #include "ui_layout.h"
 #include "util_time.h"
@@ -97,15 +98,32 @@ static int field_body(int argc, char **argv)
     return 0;
 }
 
+/* A preset's cycle window and whether it is open now (spec §5.4, §15, D41); nothing without one. */
+static void print_window(const ui_preset_t *pr)
+{
+    if (pr->window.days == 0) {
+        return;
+    }
+    const settings_t *set = app_settings();
+    char text[48], state[48];
+    ui_window_text(&pr->window, text, sizeof(text));
+    ui_window_state_text(&pr->window, timekeeping_valid() ? time(NULL) : 0, set->lat_e4, set->lon_e4, state,
+                         sizeof(state));
+    printf("    window %s: %s\n", text, state);
+}
+
 static int preset_body(int argc, char **argv)
 {
-    static const char *const k_usage = "preset list | preset set <id>";
+    static const char *const k_usage = "preset list | preset set <id> | preset window <id> <from> <until> [days] | "
+                                       "preset window <id> off  (from, until: HH:MM, sunrise or sunset, +-1-180 "
+                                       "minutes; days: bit 0 Monday, default 127)";
     ui_presets_t *p = app_presets();
     if (argc == 2 && strcmp(argv[1], "list") == 0) {
         for (int i = 0; i < p->count; i++) {
             const ui_preset_t *pr = &p->presets[i];
             printf("%c %-15s %-23s %-8s%s\n", i == p->active ? '*' : ' ', pr->id, pr->name,
                    ui_layout((ui_layout_id_t)pr->layout)->id, pr->in_cycle ? "" : " (not in the cycle)");
+            print_window(pr);
         }
         printf("auto-cycle %s, every %u s\n", p->cycle_enabled ? "on" : "off", (unsigned)p->cycle_interval_s);
         return 0;
@@ -120,6 +138,38 @@ static int preset_body(int argc, char **argv)
         printf("preset: %s\n", p->presets[index].id);
         return 0;
     }
+    if ((argc == 4 || argc == 5 || argc == 6) && strcmp(argv[1], "window") == 0) {
+        int index = ui_presets_find(p, argv[2]);
+        if (index < 0) {
+            printf("preset: no preset \"%s\" (see `preset list`)\n", argv[2]);
+            return 1;
+        }
+        ui_preset_t *pr = &p->presets[index];
+        if (argc == 4 && strcmp(argv[3], "off") == 0) {
+            pr->window = (ui_window_t){ 0 };
+        } else if (argc >= 5) {
+            char *end = NULL;
+            long days = argc == 6 ? strtol(argv[5], &end, 0) : 0x7F;
+            if (argc == 6 && (*end != '\0' || days < 0 || days > 0x7F)) {
+                days = -1; /* ui_window_make() names the rule */
+            }
+            char why[96];
+            if (!ui_window_make(argv[3], argv[4], (int)days, &pr->window, why, sizeof(why))) {
+                printf("preset: %s\n", why);
+                return 1;
+            }
+        } else {
+            return usage(k_usage);
+        }
+        app_ui_save_presets(); /* as the web page saves it (spec §15) */
+        if (pr->window.days == 0) {
+            printf("preset: %s has no window: always in the cycle\n", pr->id);
+        } else {
+            printf("preset: %s\n", pr->id);
+            print_window(pr);
+        }
+        return 0;
+    }
     return usage(k_usage);
 }
 
@@ -521,7 +571,8 @@ void app_register_commands(void)
 {
     const esp_console_cmd_t cmds[] = {
         { .command = "field", .help = "field list | get <id> | set <id> <value> | clear <id>", .func = &cmd_field },
-        { .command = "preset", .help = "preset list | set <id>", .func = &cmd_preset },
+        { .command = "preset", .help = "preset list | set <id> | window <id> <from> <until> [days] | window <id> off",
+          .func = &cmd_preset },
         { .command = "night", .help = "night <minutes>: night sleep now (spec §9.1)", .func = &cmd_night },
         { .command = "schedule", .help = "schedule list | on | off | clear | add <HH:MM> preset <id> [days] | "
                                          "add <HH:MM> night <HH:MM> [days]", .func = &cmd_schedule },
```


- [ ] **Step 3: `AGENTS.md`'s commands and the console's list.**

`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -301,6 +301,7 @@ tools/idf.sh exec python tools/devlog.py --cmd "rtc set $(date -u +%Y-%m-%dT%H:%
 tools/idf.sh exec python tools/devlog.py --cmd "power idle deep"   # or light; kept in NVS (sys/idle)
 tools/idf.sh exec python tools/devlog.py --cmd "sleep test deep 2" # sleep cycles while tethered; then: sleep stats
 tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "field set env.temp -5.5"   # redraws at once
+tools/idf.sh exec python tools/devlog.py --cmd "preset window solar sunrise sunset" --cmd "preset list"   # a cycle window (D41)
 tools/idf.sh exec python tools/devlog.py --cmd "btn key long"    # the menu: then KEY and BOOT, short or long
 tools/idf.sh exec python tools/devlog.py --cmd "schedule add 23:00 night 06:00" --cmd "schedule on"
 tools/idf.sh exec python tools/devlog.py --cmd "night 60"        # night sleep now; the console drops
@@ -364,7 +365,7 @@ Use the cheapest level that proves the change. Any UI change needs at least leve
 
 **Screenshots**: the `screenshot` console command prints the canonical framebuffer as base64 PBM between `-----BEGIN RLCD PBM-----` and `-----END RLCD PBM-----`. `tools/screenshot.py` turns that into a PNG using only pyserial and the standard library. In config mode the web UI serves `/api/screenshot.bmp`, and `/api/preview.bmp` renders any preset with live data. A screenshot shows what the firmware drew, not what the panel shows, because the ST7305 is write-only. After any display-driver change, have the owner confirm the test pattern.
 
-**Diagnostics console** (`diag`; full list in spec §15). Available now: `help`, `version`, `heap`, `reboot`, `screenshot`, `panel status|test|clear|mode <hpm|lpm>|rate <0.25|0.5|1|2|4|8>|fps [s]|sleep|wake|init <factory|xiaozhi>`, `btn <key|boot> <short|double|long>` (simulated presses), `sensors`, `battery [learn start|stop]`, `rtc get|set <ISO 8601>`, `tasks`, `power idle [deep|light]`, `sleep stats [reset]|test <deep|light> <n>`, `field list|get <id>|set <id> <value>|clear <id>`, `preset list|set <id>`, `night <minutes>`, `schedule list|on|off|clear|add <HH:MM> preset <id> [days]|add <HH:MM> night <HH:MM> [days]`, `wifi status|scan`, `sync now|status`, `radar status|loop`, `solar status|demo on|demo off`, `energy raw`; `rtc get` also prints the trim and the last drift. Planned: `audio tone`. Drive the UI, the menu included, with `btn` and `screenshot` instead of asking the owner to press buttons. A toast lasts 3 s, less than two port sessions take, so send `--cmd "btn key short" --cmd screenshot` in one devlog call and decode the log with `screenshot.extract_pbm()`. A `btn` gesture reaches the app through the buttons task, so a command in the same devlog call can run before it has taken effect: check its result in a separate call, and end a call that sends one with a slower command (`sync status`, `screenshot`), as a devlog call ends when its last command answers, before the app logs what the gesture did. Inject test data with `field set`. Run commands with `tools/idf.sh exec python tools/devlog.py --cmd <command>`. The console runs in plain line mode on purpose: no history, arrow keys or tab completion, even in a terminal. It never sends escape-code queries that a script can't answer (spec §15, `components/diag/diag.c`).
+**Diagnostics console** (`diag`; full list in spec §15). Available now: `help`, `version`, `heap`, `reboot`, `screenshot`, `panel status|test|clear|mode <hpm|lpm>|rate <0.25|0.5|1|2|4|8>|fps [s]|sleep|wake|init <factory|xiaozhi>`, `btn <key|boot> <short|double|long>` (simulated presses), `sensors`, `battery [learn start|stop]`, `rtc get|set <ISO 8601>`, `tasks`, `power idle [deep|light]`, `sleep stats [reset]|test <deep|light> <n>`, `field list|get <id>|set <id> <value>|clear <id>`, `preset list|set <id>|window <id> <from> <until> [days]|window <id> off`, `night <minutes>`, `schedule list|on|off|clear|add <HH:MM> preset <id> [days]|add <HH:MM> night <HH:MM> [days]`, `wifi status|scan`, `sync now|status`, `radar status|loop`, `solar status|demo on|demo off`, `energy raw`; `rtc get` also prints the trim and the last drift. Planned: `audio tone`. Drive the UI, the menu included, with `btn` and `screenshot` instead of asking the owner to press buttons. A toast lasts 3 s, less than two port sessions take, so send `--cmd "btn key short" --cmd screenshot` in one devlog call and decode the log with `screenshot.extract_pbm()`. A `btn` gesture reaches the app through the buttons task, so a command in the same devlog call can run before it has taken effect: check its result in a separate call, and end a call that sends one with a slower command (`sync status`, `screenshot`), as a devlog call ends when its last command answers, before the app logs what the gesture did. Inject test data with `field set`. Run commands with `tools/idf.sh exec python tools/devlog.py --cmd <command>`. The console runs in plain line mode on purpose: no history, arrow keys or tab completion, even in a terminal. It never sends escape-code queries that a script can't answer (spec §15, `components/diag/diag.c`).
 
 **Done** means: the acceptance criteria pass at the right level, new logic has tests, power-affecting changes have measurements in `docs/power.md`, and this file and `docs/` are updated, the user guide (`docs/guide.md`, its images and `README.md`) included.
 
```


- [ ] **Step 4: The host suite is unchanged, and the firmware builds.** Device-only; Task 5 checks it on the board.

Run: `cmake --build build-host >/dev/null 2>&1 && ctest --test-dir build-host | tail -3`
Expected:

```
100% tests passed, 0 tests failed out of 67
```

Then `tools/idf.sh build`: clean, without a warning.

- [ ] **Step 5: Commit.**

```bash
git add main/app_ui.c main/app_cmds.c AGENTS.md
git commit -m "feat(app): the auto-cycle skips closed windows; preset window on the console (spec §5.4, §15, D41)"
```

### Task 4: The window on the Presets page (`web`)

**Files:**
- Modify: `web/app.js`
- Test: `test/web/test_app.mjs`

**Interfaces:**
- Consumes: Task 2's `presets.json` `window` (`from`, `until`, `days`) through `GET` and `PUT /api/presets` and the preview, which already take whole presets; the page's `choose()`, `field()`, `changed()`, `busy()`, `ApiError`, `DAYS`.
- Produces: `parseBound()`, `boundOk()`, `windowText()`, `windowError()`; the edit card's "In the cycle", "From", "Until" and "Days"; the list's window beside each cycle box.

The edit card (spec §10.3) gets "In the cycle": All day, or Within a window, which starts at sunrise to sunset every day. Each end is At a time, Sunrise or Sunset, with a time or an offset in minutes; changing the kind keeps the offset typed so far. Days are ticked as the schedule's are. A hint says the auto-cycle skips the preset while its window is closed and KEY still reaches it; another, that sunrise and sunset are the location's and an end before the start runs past midnight. The preset list shows "sunrise – sunset", with the days when not every day, beside the cycle box.

Save checks every window as the device does before sending (Task 1's rules), and says what's wrong in a sentence naming the preset: an end that isn't a time or a sun bound within 180 minutes (an offset of 200 or 1.5 is shown as typed, then refused here), the two ends the same, no day ticked. So a save the page allows is one the device takes. A copied preset keeps its window.

- [ ] **Step 1: Write the failing page tests.** The window from sunrise to sunset by default and the list's summary; ends as a time and a sun bound with an offset, and the days; the refusals; All day removing the window. The fake device's `presetDevice()` takes a hook to start with a window:

`test/web/test_app.mjs`:

```diff
--- a/test/web/test_app.mjs
+++ b/test/web/test_app.mjs
@@ -177,10 +177,11 @@ const FIELDS = { fields: [{ id: 'time.clock', kind: 'time', name: 'Time', value:
 const preset = (id, name, inCycle) => ({ id, name, layout: 'classic', in_cycle: inCycle,
                                          slots: { main: 'time.clock', s1: 'env.temp' }, options: {} });
 
-/* A device with presets: Home in the cycle, Weather out of it; PUTs are kept in `saved`. */
-function presetDevice(saved) {
+/* A device with presets: Home in the cycle, Weather out of it; PUTs are kept in `saved`; `edit` may change the doc. */
+function presetDevice(saved, edit = () => {}) {
   const doc = { schema: 1, active: 'weather', presets: [preset('home', 'Home', true), preset('weather', 'Weather', false)],
                 cycle: { enabled: false, interval_s: 60 }, schedule: { enabled: false, entries: [] } };
+  edit(doc);
   return {
     'GET /api/layouts': () => reply(200, CATALOGUE),
     'GET /api/presets': () => reply(200, JSON.parse(JSON.stringify(doc))),
@@ -224,6 +225,75 @@ test('the preview names each slot where the layout puts it', async () => {
   assert.deepEqual(tags.map((e) => [e.style.left, e.style.top]), [['100%', '7%'], ['50%', '57.333%']]); /* top right */
 });
 
+/* ---- a preset's cycle window (spec §5.4, §10.3, D41) ---- */
+
+test('a preset gets a cycle window, sunrise to sunset by default, and the list shows it (D41)', async () => {
+  const saved = [];
+  const { ctx, main } = await load(presetDevice(saved));
+  await ctx.presetsPage(); /* Weather, the active one, is selected */
+  assert.equal(control(main, 'In the cycle').value, 'all');
+  await type(control(main, 'In the cycle'), 'window');
+  await buttonNamed(main, 'Save').click();
+  await settle();
+  assert.deepEqual(saved.at(-1).presets[1].window, { from: 'sunrise', until: 'sunset', days: 127 });
+  assert.equal(saved.at(-1).presets[0].window, undefined);
+  assert.match(text(main), /sunrise – sunset/);
+  assert.match(text(main), /skips this preset while its window is closed; KEY still reaches it/);
+});
+
+test('a window\'s ends take a time, or sunrise or sunset with an offset, and its days (D41)', async () => {
+  const saved = [];
+  const { ctx, main } = await load(presetDevice(saved));
+  await ctx.presetsPage();
+  await type(control(main, 'In the cycle'), 'window');
+  await type(control(main, 'From').children[0], 'time');
+  await type(control(main, 'From').children[1], '22:00');
+  await type(control(main, 'Until').children[1], '-30'); /* sunset, 30 min before */
+  await type(control(main, 'Until').children[0], 'sunrise'); /* the offset stays */
+  const days = below(control(main, 'Days')).filter((e) => e.tag === 'input');
+  for (const i of [5, 6]) { days[i].checked = false; await Promise.all(days[i].listeners.change.map((fn) => fn({ target: days[i] }))); }
+  await buttonNamed(main, 'Save').click();
+  await settle();
+  assert.deepEqual(saved.at(-1).presets[1].window, { from: '22:00', until: 'sunrise-30', days: 31 });
+  assert.match(text(main), /22:00 – sunrise-30, Mo Tu We Th Fr/);
+});
+
+test('the page refuses a window the device would refuse (D41)', async () => {
+  const saved = [];
+  const { ctx, main } = await load(presetDevice(saved, (doc) => {
+    doc.presets[1].window = { from: 'sunset', until: 'sunset+200', days: 127 };
+  }));
+  await ctx.presetsPage();
+  await type(control(main, 'Until').children[1], '200'); /* the page changed nothing yet: a save is due */
+  await buttonNamed(main, 'Save').click();
+  await settle();
+  assert.match(text(main), /Weather: a window's ends are times, or sunrise or sunset within 180 minutes\./);
+  await type(control(main, 'Until').children[1], '0');
+  await buttonNamed(main, 'Save').click();
+  await settle();
+  assert.match(text(main), /Weather: the window's from and until can't be the same\./);
+  await type(control(main, 'Until').children[1], '15');
+  const days = below(control(main, 'Days')).filter((e) => e.tag === 'input');
+  for (const d of days) { d.checked = false; await Promise.all(d.listeners.change.map((fn) => fn({ target: d }))); }
+  await buttonNamed(main, 'Save').click();
+  await settle();
+  assert.match(text(main), /Weather: a window opens on one day at least\./);
+  assert.equal(saved.length, 0);
+});
+
+test('All day removes the window (D41)', async () => {
+  const saved = [];
+  const { ctx, main } = await load(presetDevice(saved, (doc) => {
+    doc.presets[1].window = { from: 'sunrise', until: 'sunset', days: 127 };
+  }));
+  await ctx.presetsPage();
+  assert.equal(control(main, 'In the cycle').value, 'window');
+  await type(control(main, 'In the cycle'), 'all');
+  await buttonNamed(main, 'Save').click();
+  await settle();
+  assert.equal(saved.at(-1).presets[1].window, undefined);
+});
+
 /* ---- the Device page's battery calibration (owner, 2026-09-30) ---- */
 
 /* The control a field() label introduces: the element after it. */
```


- [ ] **Step 2: Run them to see them fail.**

Run: `node --test test/web/test_app.mjs 2>&1 | grep -E '^# (pass|fail)|^not ok'`
Expected:

```
not ok 9 - a preset gets a cycle window, sunrise to sunset by default, and the list shows it (D41)
not ok 10 - a window's ends take a time, or sunrise or sunset with an offset, and its days (D41)
not ok 11 - the page refuses a window the device would refuse (D41)
not ok 12 - All day removes the window (D41)
# pass 54
# fail 4
```

- [ ] **Step 3: The page.**

`web/app.js`:

```diff
--- a/web/app.js
+++ b/web/app.js
@@ -1048,6 +1048,28 @@ function fieldOptions(ed, fits, selected) {
 const CYCLE_S = [10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600];
 const cycleLabel = (s) => (s < 60 ? `${s} s` : s < 3600 ? `${s / 60} min` : `${s / 3600} h`);
 const DAYS = ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']; /* bit 0 is Monday */
+
+/* A cycle window's end (spec §5.4, D41): "HH:MM", or sunrise or sunset with "+N" or "-N" minutes. Read as it is
+ * written, offsets out of range included, so the editor shows it and windowError() names what's wrong. */
+function parseBound(text) {
+  const m = /^(?:(\d\d:\d\d)|(sunrise|sunset)(?:([+-]\d+))?)$/.exec(text || '');
+  if (!m) return { kind: 'time', time: '07:00' };
+  return m[1] ? { kind: 'time', time: m[1] } : { kind: m[2], offset: Number(m[3] || 0) };
+}
+const boundOk = (text) => /^(?:([01]\d|2[0-3]):[0-5]\d|(sunrise|sunset)([+-](?:[1-9]\d?|1[0-7]\d|180))?)$/.test(text || '');
+const windowText = (w) => `${w.from} – ${w.until}${(w.days ?? 127) === 127 ? ''
+  : `, ${DAYS.filter((d, bit) => (w.days ?? 127) & (1 << bit)).join(' ')}`}`;
+
+/* What the device would refuse in a preset's window (spec §5.4), as a sentence; null when it takes it. */
+function windowError(p) {
+  const w = p.window;
+  if (!w) return null;
+  if (!boundOk(w.from) || !boundOk(w.until)) return `${p.name}: a window's ends are times, or sunrise or sunset within 180 minutes.`;
+  if (w.from === w.until) return `${p.name}: the window's from and until can't be the same.`;
+  const days = w.days ?? 127;
+  if (!Number.isInteger(days) || days < 1 || days > 127) return `${p.name}: a window opens on one day at least.`;
+  return null;
+}
 let catalogue = null; /* GET /api/layouts, cached: it doesn't change while the page is open */
 
 function uniqueId(doc, base) {
@@ -1236,7 +1258,8 @@ function renderPresets(ed) {
       h('b', { text: q.name }), ' ', h('span', { class: 'muted small', text: LAYOUT_NAMES[q.layout] || q.layout })),
     h('label', { class: 'check small', title: 'KEY and the auto-cycle step through these' },
       h('input', { type: 'checkbox', checked: q.in_cycle, onchange: (ev) => { q.in_cycle = ev.target.checked; changed(ed, false); } }),
-      'cycle'))));
+      'cycle'),
+    q.window ? h('span', { class: 'muted small', text: ` ${windowText(q.window)}` }) : null)));
   const listCard = card('Presets', list, h('p', { class: 'muted small', text: 'Tap a preset to edit it. The dot marks ' +
     'the one on the screen now; "cycle" puts a preset in the order KEY and the auto-cycle step through.' }), actions(
     button('New preset', () => {
@@ -1300,9 +1323,44 @@ function renderPresets(ed) {
       o.status_battery = ['percent', 'voltage', 'days'].filter((k) => battery.has(k));
       changed(ed, false);
     } }), text);
+  /* The cycle window (spec §5.4, D41): each end a time, or sunrise or sunset with an offset, and the days. */
+  const bound = (key) => {
+    const b = parseBound(p.window[key]);
+    const kind = choose([['time', 'At a time'], ['sunrise', 'Sunrise'], ['sunset', 'Sunset']], b.kind);
+    kind.addEventListener('change', (ev) => {
+      const k = ev.target.value, now = parseBound(p.window[key]); /* the offset as typed since the last render */
+      p.window[key] = k === 'time' ? '07:00' : `${k}${now.offset ? (now.offset > 0 ? '+' : '') + now.offset : ''}`;
+      changed(ed, true);
+    });
+    const value = b.kind === 'time'
+      ? h('input', { type: 'time', value: b.time, onchange: (ev) => { p.window[key] = ev.target.value; changed(ed, false); } })
+      : h('input', { type: 'number', min: -180, max: 180, step: 1, value: b.offset, title: 'Minutes after (+) or before (-)',
+                     onchange: (ev) => {
+                       const n = Number(ev.target.value);
+                       p.window[key] = n ? `${b.kind}${n > 0 ? '+' : ''}${n}` : b.kind; /* 1.5 or 200: refused at Save */
+                       changed(ed, false);
+                     } });
+    return h('div', { class: 'row' }, kind, value);
+  };
+  const cycleWindow = choose([['all', 'All day'], ['window', 'Within a window']], p.window ? 'window' : 'all');
+  cycleWindow.addEventListener('change', (ev) => {
+    if (ev.target.value === 'window') p.window = { from: 'sunrise', until: 'sunset', days: 127 };
+    else delete p.window;
+    changed(ed, true);
+  });
+  const windowDays = () => h('div', { class: 'days' }, DAYS.map((d, bit) => h('label', {}, h('input', { type: 'checkbox',
+    checked: (p.window.days ?? 127) & (1 << bit), onchange: (ev) => {
+      p.window.days = ((p.window.days ?? 127) & ~(1 << bit)) | (ev.target.checked ? 1 << bit : 0);
+      changed(ed, false);
+    } }), d)));
+
   const editCard = card(`Edit ${p.name}`, previewBox(ed, slotsOf(p)), ed.previewNote,
     field('Name', name), field('Layout', layout), slots,
     NO_SLOTS[p.layout] ? h('p', { class: 'muted small', text: NO_SLOTS[p.layout] }) : null,
+    field('In the cycle', cycleWindow, 'The auto-cycle skips this preset while its window is closed; KEY still reaches it.'),
+    p.window ? [field('From', bound('from')), field('Until', bound('until')), field('Days', windowDays()),
+                h('p', { class: 'muted small', text: 'Sunrise and sunset are the location\'s, with minutes after (+) or ' +
+                  'before (-). An end before the start runs past midnight.' })] : null,
     field('Time format', clock), check('seconds', 'Show seconds'),
     o.seconds ? h('p', { class: 'bad small', text: 'Seconds wake the device every second: the battery lasts far less.' }) : null,
     check('invert', 'White on black'), field('Old or missing data', stale),
@@ -1365,6 +1423,8 @@ function renderPresets(ed) {
 
   ed.saveBar = card(null, ed.saveNote, actions(
     button('Save', () => busy(ed.saveBar, ed.saveNote, async () => {
+      const bad = ed.doc.presets.map(windowError).find(Boolean);
+      if (bad) throw new ApiError(bad);
       ed.doc = await api('PUT', '/api/presets', ed.doc);
       if (!ed.doc.schedule) ed.doc.schedule = { enabled: false, entries: [] };
       ed.dirty = false;
```


- [ ] **Step 4: Run the tests.**

Run: `node --test test/web/test_app.mjs 2>&1 | grep -E '^# (pass|fail)' && cmake --build build-host >/dev/null 2>&1 && ctest --test-dir build-host | tail -3`
Expected:

```
# pass 58
# fail 0
100% tests passed, 0 tests failed out of 67
```

- [ ] **Step 5: The firmware builds** with the page embedded: `tools/idf.sh build`, clean, without a warning.

- [ ] **Step 6: Commit.**

```bash
git add web/app.js test/web/test_app.mjs
git commit -m "feat(web): a preset's cycle window on the Presets page (spec §10.3, D41)"
```

### Task 5: On the board, and the docs as built

**Files:**
- Modify (only if the checks find something): whatever they point at, each fix with its own test where one can fail first.
- Modify: `docs/specs/2026-09-25-firmware-design.md` (r46, as built), `AGENTS.md`, `docs/guide.md`, `docs/images/web/presets.png`, `docs/images/web/presets-edit.png`

**Needs the owner first:** the board plugged into this Mac, and agreement that the checks may replace the presets for a while (the backup brings them back). They need no sync, so M5's RTC-trim count goes on. Step 1 resets the owner's web password, as the owner allows (memory `web-password-reset-ok`), and Step 9 clears the temporary one. Ask, and wait.

Before anything else, confirm the port is this board (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'` shows `14:C1:9F:54:BB:94`), and note what it has now:

```bash
tools/idf.sh exec python tools/devlog.py --cmd version --cmd "preset list" -o captures/m6e-before.log
grep -E 'elf|auto-cycle' captures/m6e-before.log
```

Expected: `elf 112aa30d3` (the stable firmware, `stable-m6d`), and the presets and auto-cycle to compare with in Step 7.

- [ ] **Step 1: Config mode and a temporary web password.** As M6d's checks did (AGENTS §6): `btn boot long`, a screenshot for the device's network and its password, Menu ▸ Wi-Fi ▸ Reset web password first if the page asks for the owner's, then join the network and set a throwaway password, in one script that restores the home Wi-Fi whatever happens:

```bash
networksetup -setairportnetwork en0 reflbo-bb94 '<the password on the screen>'
PW=$(openssl rand -hex 8) # never written down; Step 9 clears it
curl -s -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/setup
curl -s -c captures/m6e-jar -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/login
```

Expected: both answer 200. Config mode also joins the home network (gotcha 43), whose address `wifi status` prints; the same session works there.

- [ ] **Step 2: Back up, on the stable firmware.**

```bash
curl -s -b captures/m6e-jar http://192.168.4.1/api/backup > captures/m6e-backup.json
python3 -c 'import json; b=json.load(open("captures/m6e-backup.json")); print(sorted(b["files"]), b["firmware"])'
```

Expected: `['presets.json', 'settings.json']` and the stable firmware's version.

- [ ] **Step 3: Flash M6e, and a window that closes in two minutes.**

```bash
tools/idf.sh build && tools/idf.sh -p /dev/cu.usbmodemXXXX flash 2>&1 | grep -E 'MAC:|Hash of data verified'
tools/idf.sh exec python tools/devlog.py --cmd reboot --until "reflbo ready" -t 30 -o captures/m6e-boot.log
grep -E 'presets.json|E \(' captures/m6e-boot.log
tools/idf.sh exec python tools/devlog.py --cmd version --cmd "preset list"
```

Expected: `MAC: 14:c1:9f:54:bb:94`; no `E (` line and nothing about an invalid `presets.json` (the owner's file has no windows; the snapshot's version moved, so the first boot is cold); `preset list` as in `captures/m6e-before.log`, with no window lines. (A board left in download mode after flashing: gotcha 22.)

The console's refusals, each with its reason and nothing saved:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "preset window solar dawn sunset" --cmd "preset window solar 07:00 07:00" \
  --cmd "preset window solar 07:00 22:00 abc" --cmd "preset window solar sunset+181 sunrise" --cmd "preset window nope 07:00 08:00"
```

Expected, in order: `preset: from must be HH:MM, or sunrise or sunset with an offset of 1-180 minutes`; `preset: from and until can't be the same`; `preset: days must be 1-127`; the `from` sentence again; `preset: no preset "nope" (see \`preset list\`)`.

A test set of presets, over the session from Step 1 (back in config mode with `btn boot long` and the login, if it ended): Home and Indoor in the cycle, Indoor's window closing two minutes from now, the cycle every 10 s; then config mode off, so the dashboard cycles:

```bash
A=$(date -v-1M +%H:%M); B=$(date -v+2M +%H:%M)
curl -s -b captures/m6e-jar -H 'Content-Type: application/json' -X PUT -d "{\"schema\":1,\"active\":\"home\",\"cycle\":{\"enabled\":true,\"interval_s\":10},\"presets\":[{\"id\":\"home\",\"layout\":\"classic\"},{\"id\":\"indoor\",\"layout\":\"grid\",\"window\":{\"from\":\"$A\",\"until\":\"$B\"}},{\"id\":\"solar\",\"layout\":\"solar\",\"in_cycle\":false,\"window\":{\"from\":\"sunrise\",\"until\":\"sunset\"}}]}" http://192.168.4.1/api/presets | head -c 200; echo
tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "btn boot long" -t 8
tools/idf.sh exec python tools/devlog.py -t 200 -o captures/m6e-cycle.log
grep -E 'app_ui: preset' captures/m6e-cycle.log
tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "btn key short" --cmd "sync status"
```

Expected: the PUT echoes the doc with Indoor's window; `preset list` shows `    window HH:MM - HH:MM: open until HH:MM` under Indoor and Solar's state by today's sun ("open until 17:58" by day, "closed until 07:06" by night). In the capture, `app_ui: preset indoor` and `app_ui: preset home` take turns about every 10 s until Indoor's window closes, then only `app_ui: preset home` (the cycle keeps Home: no other preset is open; Indoor, on screen at the moment it closed, stayed until the next step). The last call's `preset list` shows Indoor `closed until <tomorrow> HH:MM` and KEY short then logs `app_ui: preset indoor`: KEY still reaches it.

- [ ] **Step 4: The place moves the sun.** Config mode again, log in; the location to Sydney, then back is the restore's job:

```bash
curl -s -b captures/m6e-jar -H 'Content-Type: application/json' -X PATCH -d '{"location":{"lat":-33.8688,"lon":151.2093}}' http://192.168.4.1/api/settings | head -c 80; echo
tools/idf.sh exec python tools/devlog.py --cmd "preset list"
```

Expected: Solar's state turns over (by Brno's day it is Sydney's night: "closed until …" at Sydney's sunrise, in Prague time), from the next look, without a reboot.

- [ ] **Step 5: The page.** Headless Chrome on `http://192.168.4.1/#presets` (AGENTS §6) with the jar's session: the list shows "sunrise – sunset" beside Solar; selecting Solar, the edit card has In the cycle "Within a window", From Sunrise, Until Sunset, every day ticked. Set Until to Sunset with -30 and Save: `preset list` then shows `window sunrise - sunset-30`. An offset of 200 is refused with "Solar: a window's ends are times, or sunrise or sunset within 180 minutes." and nothing is sent. Save the page's two screenshots for the guide over `docs/images/web/presets.png` and `presets-edit.png` (the docs kit's 500 px window, AGENTS §6), Solar selected in the second.

- [ ] **Step 6: A restart, a deep sleep, and the stable firmware.**

```bash
tools/idf.sh exec python tools/devlog.py --cmd reboot --until "reflbo ready" -t 30
tools/idf.sh exec python tools/devlog.py --cmd "sleep test deep 1" -t 10
tools/idf.sh exec python tools/devlog.py --cmd "sleep stats" --cmd "preset list"
```

Expected: the windows survive both: the restart reads them from `presets.json`, the deep sleep's warm wake from the snapshot (`sleep: 0 light, 1 deep`).

Then the stable firmware with this file, touching nothing that saves (no KEY, no cycle toggle):

```bash
captures/stable/stable-m6d/flash.sh /dev/cu.usbmodemXXXX
tools/idf.sh exec python tools/devlog.py --cmd reboot --until "reflbo ready" -t 30 -o captures/m6e-stable.log
grep -E 'presets.json|E \(' captures/m6e-stable.log; tools/idf.sh exec python tools/devlog.py --cmd version --cmd "preset list"
```

Expected: `elf 112aa30d3`; no error and nothing about an invalid file: `stable-m6d` reads the presets and ignores their windows (its next save would drop them, spec §5.4). Flash M6e again (`tools/idf.sh -p /dev/cu.usbmodemXXXX flash`, then the reboot as in Step 3); `preset list` still shows the windows, as nothing saved meanwhile.

- [ ] **Step 7: The owner's configuration back.** Config mode, log in, restore, compare:

```bash
curl -s -b captures/m6e-jar -H 'Content-Type: application/json' --data-binary @captures/m6e-backup.json http://192.168.4.1/api/restore
curl -s -b captures/m6e-jar http://192.168.4.1/api/backup > captures/m6e-after.json
python3 -c 'import json; a,b=(json.load(open(f))["files"] for f in ("captures/m6e-backup.json","captures/m6e-after.json")); print("same" if a==b else "differs")'
```

Expected: `{"ok":true,"skipped":[]}` and `same`: the owner's presets, auto-cycle and location as they were. `preset list` matches `captures/m6e-before.log`.

- [ ] **Step 8: The owner's own window.** Ask the owner whether Solar should join the cycle from sunrise to sunset for the acceptance below, and with which ends. On a yes: `preset window solar sunrise sunset` (or the owner's ends) on the console and, if Solar is outside the cycle, its "cycle" box on the Presets page. The board stays on M6e for the acceptance.

- [ ] **Step 9: Tidy up.** Clear the temporary web password (Menu ▸ Wi-Fi ▸ Reset web password, as in Step 1), leave config mode, `networksetup -removepreferredwirelessnetwork en0 reflbo-bb94`, `rm captures/m6e-jar captures/m6e-*.json`.

- [ ] **Step 10: Write down what was built.**
  - Spec r46: §5.4's cycle windows as built: a polar night puts sunrise at the day's end and sunset at its start, not both at 12:00, which made sunrise → sunset a 24 h window, so the first day after a polar night opens sunrise → sunset at midnight and mixed ends open in a polar night; two clock ends cross midnight on the clock face, so a window inside the spring DST gap is empty that night; a window can open the evening before its day; the bounds' two bytes; the console's and the file's shared words; the largest `presets.json` 38 238 bytes with windows; the snapshot 5 968 bytes, version 12. Also §15 (`preset list`'s window lines), §17 (`test_ui_window`), §18 (M6e built), §20 (anything the checks found, and the review's ten minors this plan's header lists), §21.
  - `AGENTS.md`: the status (M6e built; the owner's acceptance waits), and a gotcha if the checks taught one.
  - `docs/guide.md` § Presets: the cycle window (In the cycle on the page, sun or clock ends, days, KEY still reaching the preset) and `preset window` among the console's commands; the two new screenshots.

- [ ] **Step 11: Commit and push.**

```bash
git add docs AGENTS.md
git commit -m "docs: record M6e as built (spec r46)"
git push origin main
```

## Owner acceptance

M6e is done (spec §18) once the owner has checked, with the expected results:

1. **Solar leaves the cycle after sunset:** with Solar in the cycle and its window sunrise → sunset, the auto-cycle stops showing it after sunset (`preset list` says "closed until <sunrise>").
2. **And comes back in the morning:** after sunrise the cycle shows Solar again.
3. **KEY still reaches it:** at night, KEY short steps to Solar as before.

After the acceptance, M6e's build may become the stable firmware (`stable-m6e`), which M7's board tests then flash back; M7's plan is refreshed first (D41).
