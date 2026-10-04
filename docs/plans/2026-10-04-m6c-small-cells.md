# M6c: Small Split Cells, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** M6c (spec §18, D33, D34): split presets with up to 24 cells as small as 40×20 px; a new size, XS, that draws a field the way the status bar does, in one line or with its symbol over its value; S cells under 150×80 that put the symbol beside the value instead of above it; and sizes that never shrink as a cell grows. The fixed layouts don't change.

**Architecture:**

- **Pure logic, host-tested:**
  - `ui`: the XS size in `ui_layout.h`, the split's tables and its new limits in `ui_split.c` and `ui_split.h`; XS drawing in `ui_widget.c` (numbers, words, the date, the battery, the Moon) and `ui_forecast.c` (the weather, the sun, air quality, pollen); S's compact forms in short cells; the published rules in `ui_catalog.c`; presets of 24 cells in `presets.json`, which may now reach 48 KB.
  - `storage`: the backup bundle's depth limit, now in its header.
  - `locale`: a date's day form ("Fri 25"), for XS.
  - `gfx`: the field icons' 16 px forms, generated.
- **Device side:**
  - `main`: the RTC snapshot's cap rises to 6 KB, its version to 9; the depth limits are asserted against each other; the presets' defaults leave the app task's stack.
  - `webui`: 64 KB requests and replies, 22 levels deep.
  - `web/`: nothing. The editor reads every limit from `GET /api/layouts`; its page tests' copy of those rules follows the device.

**Tech stack:** ESP-IDF v5.5.5, cJSON, LittleFS; Unity on the host; Node's test runner for the page; uv and Pillow for the icons. Nothing new from the registry, no new data or services.

**Spec:** `docs/specs/2026-09-25-firmware-design.md` r35 (D33, D34, the owner's approval of 2026-10-04). Task 5 brings it to r36, as built.
- Relevant: §5.2 (the split layout, sizes, the M6c heights), §5.3 (XS, S in short cells), §5.4 (split presets, validation, the file's size), §4.5 (icons), §6 (the snapshot), §10.3 (the editor, the API's limits), §17, §18 (M6c); D31, D34.
- Also `AGENTS.md` §3.4 (gotchas 11, 22, 29, 30, 33), §5.3, §6–§8.

**Research behind this plan (2026-10-04):**
- The owner reviewed the designs as panel renders from a throwaway spike (local branch `spike/m6cd`, da6abef) and approved them on 2026-10-04: the review page is `captures/spike/index.html`, published privately as https://claude.ai/artifact/XA9LY5dHweJPtKPHKwEtGi. The spike also drew M6d's solar fields; this plan takes only its small cells, and builds them again test first.
- The plan's code was built and tested task by task on the local branch `plan/m6c`, one commit per task (`plan-m6c task N: …`); the plan carries that code. A review of the whole branch and its fixes are folded into those commits (below).
- **Binary files:** the five new goldens come from `plan/m6c`, which is pushed to `origin` with this plan. Task 4 copies them with `git checkout plan/m6c -- <paths>` (in a clone without the branch, `git fetch origin plan/m6c:plan/m6c` first), renders them again, and they must match the branch's byte for byte. The icons are generated C sources, regenerated in Task 1 by `tools/gen_icons.sh`.
- **The cells a tree can make.** With parts of at least 40×20 and up to 23 splits, a legal tree makes 2451 cell sizes (336 under M6b's limits), from 43 widths and 57 heights. `test_ui_widget_fit` draws every field each of them allows, in fifteen sets of data (eight before Task 2), in about 20 s (under ASan, about 2 minutes).
- **The heights S needs, measured.** Every field was drawn at S into cells 90 to 400 px wide and 40 to 120 px tall, in the fit test's sets of data, and each kind's lowest height from which every taller cell fits is what `k_needs` keeps. The spec's estimates (r35: 50 for the weather, 56 and 49 for the sun, 46 and 40 for air quality, 46 and 42 for pollen) were the spike's; the measurements are lower:

  | Kind at S | Narrow (under 150 px) | Wide |
  |---|---|---|
  | weather now, the day's weather, air quality and UV, numbers, words | 40 (S's own) | 40 |
  | sun times | 49 | 49 |
  | pollen | 42 | 42 |

  Narrow and wide are the same for every kind, so a cell 1 px wider never draws a field smaller (spec §5.2's M6c rule). M keeps M6b's heights.
- **Sizes.** The largest `presets.json` is 37 197 bytes (16 253 under M6b): 16 split presets of 24 cells in three rows of eight columns, the most columns a tree can have, every line hidden, the longest field ids, names of control characters that cJSON writes as six bytes each, every option at its longest and 8 schedule entries. The deepest legal tree has 13 splits in a chain (seven rows, then six columns, its last cells 41×20): its file nests 17 levels, a backup of it 19. The RTC snapshot grows from 3 896 to 4 664 bytes: each preset gains 16 slots and 32 tree nodes, 16 times; RTC slow memory holds 4 816 of its 8 192 bytes with `power`'s state.
- **Not checked on the board:** everything the device does is Task 5's.
- **The review.** A fresh reviewer on the most capable model read the whole branch (2026-10-04) and found no critical issue. Its important findings, all in what a reader sees in a small cell, are fixed and folded into Tasks 1 and 2, each with a test that failed first: values cut where a short form, the unit or the symbol could have given way (a 24-hour time in a 90×40 cell, the date and the Moon in short S cells, XS numbers beside their symbol); the age mark drawn over the value in short S cells; a 48 px sky in a 40 px cell; XS losing the marks S shows (today's low and high, the charging bolt); and test data too narrow to find them. Of its minors, these are fixed too: Czech's day form without its period, `fit_tiny()`'s copies of the value, a magic 0, the backup's depth kept in three places, the presets' defaults on the app task's stack, lines over 120 characters and a `printf` in a test. Deferred: the age mark against the value in a few M cells, as before M6c.
- **The largest values** (the Review Focus's first line): a fifteenth set of data, every value at the top of its scale, found air quality's index past 100 overflowing a short S cell; Task 2 steps it down to bold 20 or 16.

**Rulings this plan makes (each costs little if wrong):**
- **S's heights are the measured ones** (Task 2): 49 for the sun, 42 for pollen, S's own 40 for the rest, narrow and wide alike. Spec r35 named the spike's estimates and left the numbers to this plan; Task 5's as-built revision records them. Cost: a field takes S in a few more cells than the spec's numbers would allow; `test_ui_widget_fit` proves each of them fits.
- **Nothing is cut before the rest gives way** (Tasks 1, 2). XS gives up the trend arrow, then (stacked) puts the unit under the number, then drops it, and drops the symbol before a number is cut. S beside the value gives up the battery's bolt, the trend arrow, the unit (a time its clock first, as AM/PM says more), then the symbol, and the number stands alone, centred ("9" for "9 µg/m³" in a 90 px cell). Words take their shorter forms (the date's "Fri 25", the Moon's short name, then its illumination), then leave their symbol behind; the Moon keeps its disc. Only then is anything cut. Cost: a cell may show a number without its unit or icon, as the status bar does.
- **XS's date is "Fri" over "25", from the date's day form** (Task 1): `LANG_DATE_DAY` in the locale ("Pá 25." in Czech), not the value's unit, as `GET /api/fields` appends a value's unit to its text.
- **Pollen in a narrow XS cell shows its level bar** (Task 1) when its word doesn't fit in sans 12, as spec §5.3 says; air quality and UV in a short S cell put their word under the number when it doesn't fit beside it, and the bar alone when there is no room for that either (Task 2).
- **The sun in a narrow XS cell** (Task 1): a row each, its icon beside its time; else each icon over its time; else the two times alone.
- **The current weather in a short S cell** (Task 2) draws a 24 px sky in a cell under 120 px wide or 52 px tall, as a 48 px one leaves no room for the temperature, or overflows.
- **No age mark in an S cell under 80 px** (Task 2), as in XS: beside the value it would sit on it; the status bar's stale warning covers the cell. Spec r35 says so for XS only; Task 5's r36 records it. Classic's and Weather's small slots (111 and 118 px tall) keep it. Cost: a stale value in a short cell is told only by the status bar.
- **Polar day and night** (Task 2): XS shows the clear sky alone where the words would be cut; S under 60 px puts the sky beside the words, on two lines if need be; a taller cell steps them down to sans 12 before a cut.
- **Today's weather in a wide cell steps down to bold 16 and sans 12 before an ellipsis** (Task 2), at every size: "102/97°" in a 150 px cell.
- **XS centres a value only when it stands alone** (Task 1): a time, or a value whose symbol had to go; a field without a symbol (battery days) starts at the left like its neighbours.
- **`presets.json` may reach 48 KB, and the web server takes 64 KB requests and replies** (Task 3), so a backup of the largest files restores (`_Static_assert` in `app_web.c`); the server's buffers were already in PSRAM. Cost: 120 KB more PSRAM for the server's request and reply and the restore's copy, 28 KB more for each of the two presets buffers: 176 KB of 8 MB.
- **JSON nests at most 20 levels in `presets.json` and 22 in a request or a backup** (Task 3), for the deepest legal tree's 17 and 19. `BACKUP_MAX_DEPTH` moves to `storage_backup.h`, and `app_web.c` asserts that each limit leaves room for the next.
- **The presets' defaults leave the app task's stack** (Task 3): `app_ui_load()` kept 2 KB of them there with 24 cells; they go to PSRAM, as the other preset buffers are. No host test can see the stack; Task 5 reads `tasks`. Cost: 2 KB of PSRAM.
- **The snapshot's cap is 6 KB and its version 9** (Task 3), as for any change to its layout: a snapshot of the old layout is never read (gotcha 29).
- **The editor keeps its code** (Task 3): it computes every limit from the catalogue, so 24 cells, 40×20 and XS need no change in `web/app.js`; its page tests' copy of the rules follows the device's and pins the editor to them.
- **The goldens use M6c's fields only** (Task 4); the spike's solar and energy cells come with M6d.
- **The fit test looks for what a reader would see** (Tasks 1, 2): an ellipsis glyph and the stale mark, matched pixel for pixel and looked for only from inked pixels, in fifteen sets of data and all 2451 cell sizes. Cost: about 20 s of the suite.
- **M6b's minors** (memory `m6b-review-deferred-minors`), fixed here as M6c touches their code: names on two lines in 81–82 px cells (Task 2: from 86 px), the sun's polar words in wide S cells of 51–52 px (Task 2), the catalogue test's sampling (Task 3: every size), `webui.h`'s comment (Task 3). Left: the battery's comma tail at M, BOOT double in a night, the split error that doesn't name its split.

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, plain HTML/CSS/JS in `web/` with no build step and no external resources.
- `[host]` code (`ui`, `locale`, `components/storage/settings.c`, `components/storage/storage_backup.c`) includes no ESP-IDF headers. cJSON counts as plain C.
- ESP-IDF style: 4-space indent, `snake_case`, a component or module prefix on public APIs, public headers in `include/`. C lines stay within 120 characters.
- The app task owns the display, storage, the settings, the presets and the syncs' state (spec §3.2, `AGENTS.md` §5.3); the web server reaches them through the app's executor.
- **The RTC snapshot is at most 6 KB** (`_Static_assert` in `main/app.c`, M6c); a change to its layout bumps `SNAP_VERSION`.
- **A widget never draws outside its cell, and in a cell the rules allow it, never within 2 px of its edges** (`test_ui_widget_fit`); a value or text is never wider than the room it is given, its ellipsis included.
- **A cell that grows never draws a field at a smaller size** (spec §5.2; `test_more_room_never_loses_a_field`).
- **Depth:** `presets.json` nests at most 20 levels (`UI_JSON_MAX_DEPTH`) and a request or a backup 22 (`WEBUI_JSON_MAX_DEPTH`, `BACKUP_MAX_DEPTH`). Every recursion over a tree takes a node a call: 14 frames at most in a legal tree, on the app task.
- **Secrets.** Never commit or log a Wi-Fi password, the AP password or the web password. The board has no web password now: a check that sets one over the device's own network clears it again with Menu ▸ Wi-Fi ▸ Reset web password, so the owner's first visit chooses theirs.
- **The board** (`AGENTS.md` §6, the owner's rules of 2026-10-03): back up its configuration with `GET /api/backup` before a check that changes it and restore it after; never erase flash or NVS, or forget the saved networks, without asking; a factory reset only with the owner's agreement. Board commands go through `tools/idf.sh` with an explicit `-p`, and the port must be confirmed as this board: Espressif `303A:1001` with the USB serial number `14:C1:9F:54:BB:94` (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'`), and `flash` prints `MAC: 14:c1:9f:54:bb:94`.
- **This Mac's Wi-Fi** may join the board's AP for checks (owner, 2026-09-30): `networksetup -setairportnetwork en0 reflbo-bb94 <password from the screen>`, removed afterwards with `networksetup -removepreferredwirelessnetwork en0 reflbo-bb94`.
- List every component source in `SRCS`. Dependencies come with ESP-IDF; nothing new from the registry.
- Small, focused Conventional Commits that each build. Push to `origin` freely; never force-push. **No attribution of any kind: no `Co-Authored-By` or other trailer in any commit** (the owner's rule, memory `no-commit-trailers`).
- Build only what the spec covers (r35: D34). Not in M6c: solar and energy (M6d), MQTT (M7), changes to the fixed layouts, built-in split presets.
- Czech text follows Czech typography, and every string's glyphs must exist in the fonts (`test_lang_glyphs`).

## Review Focus

1. **Values at the top of their scale in small cells**: an air quality index past 100 (smoke), PM at 255 µg/m³, thousands of grains of pollen, 123 days of battery. A reader expects them whole, in a smaller face, clear of the cell's edges. Pinned by Task 2's fifteenth set of data in `test_ui_widget_fit`, which found and fixed the index in a short S cell.
2. **The owner's M6b files and snapshot after the update**: `presets.json` and `settings.json` parse as they are, and a version-8 snapshot is never read, so the first wake is cold. Pinned by `test_ui_preset`'s M6b presets, which keep passing, and Task 5's Step 1 on the board.
3. **Going back to `stable-m6b`** with M6c's presets on the board: the older firmware refuses a file of 24 cells and shows its defaults, without overwriting the file, until the backup taken before the test is restored. M6b's parser isn't in this tree, so Task 5's Step 8 writes it into spec §12.10 and `AGENTS.md` §6, whose protocol restores the backup anyway.
4. **The wake that draws 24 cells**: a minute's redraw of a 24-cell preset should stay near the dashboards' (about 60 ms at M6), as the board is battery first. Pinned by Task 5's Step 5, which measures it.
5. **Building small cells in the editor on a phone**: 24 nested boxes, each reachable, splitting refused at 40×20 with the reason. Pinned by Task 3's page tests (24 cells, the ratios a 40×20 part allows, XS names), Task 5's Step 6 and the owner's acceptance.

---

### Task 1: The XS size and its widgets (`ui`, `locale`, `gfx` icons)

**Files:**
- Modify: `assets/icons/icons.txt`, `components/gfx/icons/gfx_icons.c` and `components/gfx/include/gfx_icons.h` (generated), `components/locale/include/lang.h`, `components/locale/lang_en.c`, `components/locale/lang_cs.c`, `components/ui/include/ui_layout.h`, `components/ui/ui_split.c`, `components/ui/ui_catalog.c`, `components/ui/ui_fields.c`, `components/ui/ui_widget.c`, `components/ui/ui_forecast.c`, `components/ui/ui_internal.h`
- Test: `test/host/test_lang.c`, `test/host/test_ui_split.c`, `test/host/test_ui_catalog.c`, `test/host/test_ui_widget_fit.c`

**Interfaces:**
- Consumes: `ui_split_field_size()`, `ui_split_need()`, `ui_split_min_w()`, `ui_split_min_h()` (ui_split.h, M6b); `ui_draw_cell()` (ui_dashboard.h); `ui_widget_draw()`, `ui_sky_icon()` (ui_internal.h); `lang_format_date()` (lang.h).
- Produces (Tasks 2–4):
  - `UI_SIZE_XS`, first in `ui_size_t` (`ui_layout.h`), and `UI_KINDS_XS`, the kinds S takes; `ui_split.c`'s XS row: 40 px wide, 20 tall, narrow or wide;
  - `bool ui_tiny_stacked(gfx_rect_t r)`: XS puts the symbol over the value in a cell under 120 px wide and at least 44 tall, else draws one line; `int ui_ink_above(const gfx_font_t *f, const char *text)`, `int ui_ink_below(const gfx_font_t *f, const char *text)` (ui_internal.h);
  - 16 px forms `gfx_icon_<name>_16` of `dew`, `clock`, `calendar`, `person`, `celebration`, `cloud`, `air`, `particles`, `uv`, `pollen`, `sunrise`, `sunset` and every `wx_*`;
  - `LANG_DATE_DAY` (`lang.h`): "Fri 25", "Pá 25."; `date.day`'s short form, which XS draws as "Fri" over "25";
  - in `test_ui_widget_fit.c`: `has_ellipsis(gfx_rect_t r)` (an ellipsis in any face a small value takes), `never_cut(ui_field_kind_t kind)` (a number, a time, the battery, today's high and low, the sun's times), `first_ink()` and `next_ink()` (a match is looked for only from inked pixels), and `inked()` a byte at a time.

Until Task 3, a split's parts are at least 90×40, so XS draws only where S has no room for a kind; the fit test also draws every small field at XS in cells down to 40×20, which Task 3's trees will make.

- [ ] **Step 1: Write the failing tests.** The day form of a date:

`test/host/test_lang.c`:

```diff
--- a/test/host/test_lang.c
+++ b/test/host/test_lang.c
@@ -32,6 +32,8 @@ static void test_dates_in_three_styles(void)
     TEST_ASSERT_EQUAL_STRING("Fri 25 Sep", out);
     lang_format_date(en, &tm, LANG_DATE_SHORT, out, sizeof(out));
     TEST_ASSERT_EQUAL_STRING("25 Sep", out);
+    lang_format_date(en, &tm, LANG_DATE_DAY, out, sizeof(out)); /* M6c: XS's weekday over its day */
+    TEST_ASSERT_EQUAL_STRING("Fri 25", out);
 }
 
 static void test_an_impossible_date_formats_as_empty(void)
@@ -106,6 +108,8 @@ static void test_czech_dates_numbers_and_names(void)
     TEST_ASSERT_EQUAL_STRING("Pá 25. 9.", out);
     lang_format_date(cs, &tm, LANG_DATE_SHORT, out, sizeof(out));
     TEST_ASSERT_EQUAL_STRING("25. 9.", out);
+    lang_format_date(cs, &tm, LANG_DATE_DAY, out, sizeof(out));
+    TEST_ASSERT_EQUAL_STRING("Pá 25.", out);
     tm = date(2026, 3, 2, 1);
     lang_format_date(cs, &tm, LANG_DATE_LONG, out, sizeof(out));
     TEST_ASSERT_EQUAL_STRING("Pondělí 2. března", out);
```


The size classes and the published rules:

`test/host/test_ui_split.c`:

```diff
--- a/test/host/test_ui_split.c
+++ b/test/host/test_ui_split.c
@@ -138,8 +138,8 @@ static void test_a_bad_node_is_refused(void)
     TEST_ASSERT_FALSE(lay(k_flag, sizeof(k_flag)));
 }
 
-/* XL 400 wide and at least 120 tall; L at least 200×150; M at least 130×80; S otherwise: wide from
- * 150 px with 40 px of height, narrower with 80 (spec §5.2). */
+/* XL 400 wide and at least 120 tall; L at least 200×150; M at least 130×80; S wide from 150 px with 40 px of
+ * height, narrower with 80; XS from 40×20 (spec §5.2, D34). */
 static void test_a_cells_size_follows_its_dimensions(void)
 {
     TEST_ASSERT_EQUAL_INT(UI_SIZE_XL, ui_split_cell_size(400, 120));
@@ -153,9 +153,32 @@ static void test_a_cells_size_follows_its_dimensions(void)
     TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(150, 40));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(149, 80));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(150, 79));
-    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(149, 79)); /* narrow S needs 80 */
-    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(90, 40));
-    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(150, 39));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_cell_size(149, 79)); /* narrow S needs 80 */
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_cell_size(90, 40));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_cell_size(150, 39));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_cell_size(40, 20));
+    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(39, 279));
+    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(400, 19));
+}
+
+/* XS (D34) takes every kind S takes, from 40×20 whatever the width; series and maps still need M. */
+static void test_xs_takes_the_small_kinds_from_40_by_20(void)
+{
+    TEST_ASSERT_EQUAL_INT(40, ui_split_min_w(UI_SIZE_XS));
+    TEST_ASSERT_EQUAL_INT(20, ui_split_min_h(UI_SIZE_XS, true));
+    TEST_ASSERT_EQUAL_INT(20, ui_split_min_h(UI_SIZE_XS, false));
+    static const ui_field_kind_t k_small[] = { UI_FK_TIME, UI_FK_DATE, UI_FK_NUMBER, UI_FK_BATTERY, UI_FK_MOON,
+                                               UI_FK_TEXT, UI_FK_WEATHER_NOW, UI_FK_WEATHER_DAY, UI_FK_SUN,
+                                               UI_FK_LEVEL, UI_FK_POLLEN };
+    for (size_t i = 0; i < sizeof(k_small) / sizeof(k_small[0]); i++) {
+        TEST_ASSERT_EQUAL_INT(20, ui_split_need(UI_SIZE_XS, k_small[i], true));
+        TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_field_size(k_small[i], 40, 20));
+        TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_field_size(k_small[i], 400, 39));
+    }
+    TEST_ASSERT_EQUAL_INT(-1, ui_split_need(UI_SIZE_XS, UI_FK_SERIES, true));
+    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_SERIES, 89, 279));
+    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_RAIN_MAP, 400, 79));
+    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_NUMBER, 39, 20));
 }
 
 /* A field draws at the largest size its cell allows that takes its kind and has room for it. */
@@ -174,10 +197,10 @@ static void test_a_field_draws_at_the_largest_size_with_room_for_it(void)
     TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_POLLEN, 199, 104)); /* M pollen needs 105 */
     TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_POLLEN, 199, 105));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_DAY, 199, 69)); /* wide S needs 51 */
-    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_WEATHER_DAY, 129, 98));        /* narrow S needs 99 */
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_field_size(UI_FK_WEATHER_DAY, 129, 98)); /* narrow S needs 99 */
     TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_DAY, 129, 99));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_WEATHER_DAY, 149, 94)); /* narrow M needs 94 */
-    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_NUMBER, 99, 69));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_field_size(UI_FK_NUMBER, 99, 69));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_NUMBER, 99, 80));
 }
 
@@ -185,8 +208,8 @@ static void test_a_field_draws_at_the_largest_size_with_room_for_it(void)
 static void test_more_room_never_loses_a_field(void)
 {
     for (int k = 0; k < UI_FK_COUNT; k++) {
-        for (int w = 90; w <= 400; w += 1) {
-            for (int h = 40; h <= 279; h += 7) {
+        for (int w = 40; w <= 400; w += 1) {
+            for (int h = 20; h <= 279; h += 7) {
                 if (ui_split_field_size((ui_field_kind_t)k, w, h) >= 0) {
                     TEST_ASSERT_TRUE(ui_split_field_size((ui_field_kind_t)k, w + 1 > 400 ? 400 : w + 1, h) >=
                                      ui_split_field_size((ui_field_kind_t)k, w, h));
@@ -252,6 +275,7 @@ int main(void)
     RUN_TEST(test_a_part_under_90_by_40_is_refused);
     RUN_TEST(test_a_bad_node_is_refused);
     RUN_TEST(test_a_cells_size_follows_its_dimensions);
+    RUN_TEST(test_xs_takes_the_small_kinds_from_40_by_20);
     RUN_TEST(test_a_field_draws_at_the_largest_size_with_room_for_it);
     RUN_TEST(test_more_room_never_loses_a_field);
     RUN_TEST(test_ratios_have_names);
```


`test/host/test_ui_catalog.c`:

```diff
--- a/test/host/test_ui_catalog.c
+++ b/test/host/test_ui_catalog.c
@@ -121,10 +121,18 @@ static void test_layouts_publish_the_split_rules(void)
     TEST_ASSERT_EQUAL_STRING("1/4", cJSON_GetArrayItem(ratios, 0)->valuestring);
     TEST_ASSERT_EQUAL_STRING("3/4", cJSON_GetArrayItem(ratios, 4)->valuestring);
     const cJSON *sizes = cJSON_GetObjectItemCaseSensitive(split, "sizes");
-    TEST_ASSERT_EQUAL_INT(4, cJSON_GetArraySize(sizes)); /* the largest first, as a cell takes the first that fits */
-    const cJSON *xl = cJSON_GetArrayItem(sizes, 0), *s = cJSON_GetArrayItem(sizes, 3);
+    TEST_ASSERT_EQUAL_INT(5, cJSON_GetArraySize(sizes)); /* the largest first, as a cell takes the first that fits */
+    const cJSON *xl = cJSON_GetArrayItem(sizes, 0);
+    const cJSON *s = cJSON_GetArrayItem(sizes, 3);
+    const cJSON *xs = cJSON_GetArrayItem(sizes, 4);
     TEST_ASSERT_EQUAL_STRING("XL", str(xl, "size"));
     TEST_ASSERT_EQUAL_STRING("S", str(s, "size"));
+    TEST_ASSERT_EQUAL_STRING("XS", str(xs, "size")); /* M6c, D34 */
+    TEST_ASSERT_EQUAL_INT(40, num(xs, "min_w"));
+    const cJSON *xs_min = cJSON_GetObjectItemCaseSensitive(xs, "min_h");
+    TEST_ASSERT_EQUAL_INT(20, cJSON_GetArrayItem(xs_min, 0)->valueint);
+    TEST_ASSERT_EQUAL_INT(20, cJSON_GetArrayItem(xs_min, 1)->valueint);
+    TEST_ASSERT_EQUAL_INT(11, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(xs, "kinds"))); /* S's kinds */
     TEST_ASSERT_EQUAL_INT(400, num(xl, "min_w"));
     TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(xl, "kinds"))); /* time, number */
     const cJSON *s_min = cJSON_GetObjectItemCaseSensitive(s, "min_h");
@@ -138,10 +146,10 @@ static void test_layouts_publish_the_split_rules(void)
     static const char *const k_kinds[UI_FK_COUNT] = { "time", "date", "number", "battery", "moon", "text",
                                                       "weather_now", "weather_day", "series", "sun", "level",
                                                       "pollen", "rain_map" };
-    static const char *const k_names[] = { "S", "M", "L", "XL" };
+    static const char *const k_names[] = { "XS", "S", "M", "L", "XL" };
     for (int k = 0; k < UI_FK_COUNT; k++) {
-        for (int w = 90; w <= 400; w += 7) {
-            for (int h = 40; h <= 279; h += 3) {
+        for (int w = 40; w <= 400; w += 7) {
+            for (int h = 20; h <= 279; h += 3) {
                 int size = ui_split_field_size((ui_field_kind_t)k, w, h);
                 const char *published = rule_size(split, k_kinds[k], w, h);
                 TEST_ASSERT_EQUAL_STRING(size < 0 ? NULL : k_names[size], published);
```


Every small field in every XS cell, at each threshold of the XS drawing on both sides, in the eight sets of data: it shows, stays 2 px clear of the edges, and none of a number, a time, the battery, today's high and low or the sun's times is cut. One line against the symbol over the value; and XS keeps the marks S shows (↓ and ↑ for today's low and high, a charging battery's bolt) and the Moon's disc:

`test/host/test_ui_widget_fit.c`:

```diff
--- a/test/host/test_ui_widget_fit.c
+++ b/test/host/test_ui_widget_fit.c
@@ -5,6 +5,7 @@
 
 #include "dashboard_fixtures.h"
 #include "gfx.h"
+#include "gfx_fonts.h"
 #include "ui_layout.h"
 #include "ui_split.h"
 #include "unity.h"
@@ -27,11 +28,29 @@ static void render(const char *name)
     ui_draw_dashboard(&s_fb, &ctx, &preset);
 }
 
+/* Whether any pixel in x0..x1, y0..y1 is set; a byte at a time, as the fit tests draw a million cells. */
 static bool inked(int x0, int x1, int y0, int y1)
 {
+    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
+    x1 = x1 >= s_fb.width ? s_fb.width - 1 : x1, y1 = y1 >= s_fb.height ? s_fb.height - 1 : y1;
+    if (x0 > x1) {
+        return false;
+    }
+    uint8_t first = (uint8_t)(0xFFu >> (x0 & 7)), last = (uint8_t)(0xFFu << (7 - (x1 & 7)));
     for (int y = y0; y <= y1; y++) {
-        for (int x = x0; x <= x1; x++) {
-            if (gfx_get_pixel(&s_fb, x, y)) {
+        const uint8_t *row = s_fb.buf + y * s_fb.stride;
+        int b0 = x0 >> 3, b1 = x1 >> 3;
+        if (b0 == b1) {
+            if (row[b0] & first & last) {
+                return true;
+            }
+            continue;
+        }
+        if ((row[b0] & first) || (row[b1] & last)) {
+            return true;
+        }
+        for (int b = b0 + 1; b < b1; b++) {
+            if (row[b]) {
                 return true;
             }
         }
@@ -274,6 +293,213 @@ static void test_every_field_fits_every_cell_a_split_can_make(void)
     }
 }
 
+/* XS cells (D34): narrower than 90 or lower than 40, down to 40×20, every threshold of the XS drawing on both sides
+ * (one line from 120 px of width or under 44 px of height, a 24 px symbol from 34 px, a 24 px stacked symbol from
+ * 60). */
+static const int16_t k_xs_w[] = { 40, 41, 44, 48, 50, 55, 60, 66, 70, 75, 80, 85, 89 };
+static const int16_t k_xs_h[] = { 20, 21, 22, 24, 26, 28, 30, 33, 34, 36, 39, 40, 43, 44, 45, 50, 55, 59, 60, 69,
+                                  80, 93, 100, 139, 279 };
+static const int16_t k_xs_wide_w[] = { 90, 100, 119, 120, 133, 149, 150, 199, 200, 266, 399, 400 };
+static const int16_t k_xs_low_h[] = { 20, 21, 22, 24, 26, 28, 30, 33, 34, 36, 39 };
+
+/* Whether glyph `g` of `f` is drawn with its top left at (x0, y0), a blank pixel all round it. */
+static bool glyph_at(const gfx_font_t *f, const gfx_glyph_t *g, int x0, int y0)
+{
+    int rb = (g->width + 7) / 8;
+    for (int y = -1; y <= g->height; y++) {
+        for (int x = -1; x <= g->width; x++) {
+            bool want = x >= 0 && y >= 0 && x < g->width && y < g->height &&
+                        ((f->bitmap[g->offset + y * rb + x / 8] >> (7 - x % 8)) & 1);
+            if (gfx_get_pixel(&s_fb, x0 + x, y0 + y) != want) {
+                return false;
+            }
+        }
+    }
+    return true;
+}
+
+/* The first inked pixel of a glyph's or an icon's bits, in reading order: where a match of it starts. */
+static void first_ink(const uint8_t *bits, int width, int height, int *fx, int *fy)
+{
+    int rb = (width + 7) / 8;
+    for (int i = 0; i < width * height; i++) {
+        if ((bits[(i / width) * rb + (i % width) / 8] >> (7 - (i % width) % 8)) & 1) {
+            *fx = i % width, *fy = i / width;
+            return;
+        }
+    }
+    *fx = *fy = 0;
+}
+
+/* The next inked pixel of `r` from (*x, *y) on, in reading order, a blank byte at a time; false after the last. */
+static bool next_ink(gfx_rect_t r, int *x, int *y)
+{
+    for (; *y < r.y + r.h; (*y)++, *x = r.x) {
+        const uint8_t *row = s_fb.buf + *y * s_fb.stride;
+        for (; *x < r.x + r.w; (*x)++) {
+            if (row[*x >> 3] == 0) {
+                *x |= 7; /* the rest of a blank byte */
+            } else if ((row[*x >> 3] >> (7 - (*x & 7))) & 1) {
+                return true;
+            }
+        }
+    }
+    return false;
+}
+
+/* Whether `r` shows an ellipsis ("…") in a face a small value takes: something was cut to fit. */
+static bool has_ellipsis(gfx_rect_t r)
+{
+    static const gfx_font_t *const k_faces[] = { &gfx_font_sans_12, &gfx_font_sans_16, &gfx_font_sans_bold_16,
+                                                 &gfx_font_sans_bold_20, &gfx_font_sans_bold_28 };
+    enum { FACES = sizeof(k_faces) / sizeof(k_faces[0]) };
+    const gfx_glyph_t *g[FACES];
+    int fx[FACES], fy[FACES];
+    for (int i = 0; i < FACES; i++) {
+        g[i] = gfx_font_glyph(k_faces[i], 0x2026);
+        first_ink(k_faces[i]->bitmap + g[i]->offset, g[i]->width, g[i]->height, &fx[i], &fy[i]);
+    }
+    for (int x = r.x, y = r.y; next_ink(r, &x, &y); x++) {
+        for (int i = 0; i < FACES; i++) {
+            int x0 = x - fx[i], y0 = y - fy[i];
+            if (x0 >= r.x && y0 >= r.y && x0 + g[i]->width <= r.x + r.w && y0 + g[i]->height <= r.y + r.h &&
+                glyph_at(k_faces[i], g[i], x0, y0)) {
+                return true;
+            }
+        }
+    }
+    return false;
+}
+
+/* The kinds whose value must never be cut: a number, a time, today's high and low, the sun's times. */
+static bool never_cut(ui_field_kind_t kind)
+{
+    return kind == UI_FK_NUMBER || kind == UI_FK_TIME || kind == UI_FK_BATTERY || kind == UI_FK_WEATHER_DAY ||
+           kind == UI_FK_SUN;
+}
+
+static void check_xs_cell(const ui_context_t *ctx, int w, int h, int variant)
+{
+    for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
+        const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
+        if (ui_split_field_size(info->kind, w, h) != UI_SIZE_XS) {
+            continue;
+        }
+        gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
+        gfx_fb_init(&s_fb, s_buf, 400, 300);
+        gfx_clear(&s_fb, GFX_WHITE);
+        ui_draw_cell(&s_fb, r, ctx, (ui_field_id_t)f, UI_STALE_STALE);
+        char msg[80];
+        snprintf(msg, sizeof(msg), "%s at %d×%d (XS), variant %d", info->id, w, h, variant);
+        TEST_ASSERT_TRUE_MESSAGE(inked(r.x + 2, r.x + w - 3, r.y + 2, r.y + h - 3), msg);
+        TEST_ASSERT_FALSE_MESSAGE(never_cut(info->kind) && has_ellipsis(r), msg);
+        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y, r.y + 1), msg);
+        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y + h - 2, r.y + h - 1), msg);
+        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + 1, r.y, r.y + h - 1), msg);
+        TEST_ASSERT_FALSE_MESSAGE(inked(r.x + w - 2, r.x + w - 1, r.y, r.y + h - 1), msg);
+    }
+}
+
+/* Every small field shows in every XS cell and stays clear of its edges, in every set of data. */
+static void test_every_small_field_fits_every_xs_cell(void)
+{
+    for (int variant = 0; variant < 8; variant++) {
+        ui_context_t ctx = split_context(variant);
+        for (size_t i = 0; i < sizeof(k_xs_w) / sizeof(k_xs_w[0]); i++) {
+            for (size_t j = 0; j < sizeof(k_xs_h) / sizeof(k_xs_h[0]); j++) {
+                check_xs_cell(&ctx, k_xs_w[i], k_xs_h[j], variant);
+            }
+        }
+        for (size_t i = 0; i < sizeof(k_xs_wide_w) / sizeof(k_xs_wide_w[0]); i++) {
+            for (size_t j = 0; j < sizeof(k_xs_low_h) / sizeof(k_xs_low_h[0]); j++) {
+                check_xs_cell(&ctx, k_xs_wide_w[i], k_xs_low_h[j], variant);
+            }
+        }
+    }
+}
+
+/* Runs of inked rows in `r`: the lines a widget drew, one above the other. */
+static int ink_bands(gfx_rect_t r)
+{
+    int bands = 0;
+    bool in = false;
+    for (int y = r.y; y < r.y + r.h; y++) {
+        bool row = inked(r.x, r.x + r.w - 1, y, y);
+        bands += row && !in;
+        in = row;
+    }
+    return bands;
+}
+
+static int draw_xs(const ui_context_t *ctx, ui_field_id_t field, int w, int h)
+{
+    gfx_rect_t r = { 0, 21, (int16_t)w, (int16_t)h };
+    gfx_fb_init(&s_fb, s_buf, 400, 300);
+    gfx_clear(&s_fb, GFX_WHITE);
+    ui_draw_cell(&s_fb, r, ctx, field, UI_STALE_STALE);
+    return ink_bands(r);
+}
+
+/* XS draws one line, like the status bar, where the cell is 120 px wide or more or under 44 px tall; otherwise
+ * the symbol over the value, the date as its weekday over its day (spec §5.3, D34). */
+static void test_xs_draws_one_line_or_the_symbol_over_the_value(void)
+{
+    ui_context_t ctx = split_context(0);
+    TEST_ASSERT_EQUAL_INT(1, draw_xs(&ctx, UI_FIELD_ENV_TEMP, 200, 22));
+    TEST_ASSERT_EQUAL_INT(1, draw_xs(&ctx, UI_FIELD_ENV_TEMP, 89, 43));
+    TEST_ASSERT_EQUAL_INT(1, draw_xs(&ctx, UI_FIELD_WX_NOW, 120, 60));
+    TEST_ASSERT_TRUE(draw_xs(&ctx, UI_FIELD_ENV_TEMP, 66, 69) >= 2);
+    TEST_ASSERT_TRUE(draw_xs(&ctx, UI_FIELD_WX_NOW, 66, 69) >= 2);
+    TEST_ASSERT_EQUAL_INT(2, draw_xs(&ctx, UI_FIELD_DATE_DAY, 50, 69)); /* "Fri" over "25" */
+}
+
+/* One field in a cell w × h at the bottom right of the panel, its pixels copied out: what tells two apart. */
+static void cell_bits(const ui_context_t *ctx, ui_field_id_t field, int w, int h, uint8_t *out)
+{
+    gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
+    gfx_fb_init(&s_fb, s_buf, 400, 300);
+    gfx_clear(&s_fb, GFX_WHITE);
+    ui_draw_cell(&s_fb, r, ctx, field, UI_STALE_STALE);
+    for (int y = 0; y < h; y++) {
+        for (int x = 0; x < w; x++) {
+            out[y * w + x] = (uint8_t)gfx_get_pixel(&s_fb, r.x + x, r.y + y);
+        }
+    }
+}
+
+/* XS keeps what S shows: today's low and high apart from the temperature (↓ and ↑ beside the thermometer) and
+ * a charging battery's bolt; and the Moon keeps its disc in a narrow line (D34). */
+static void test_xs_keeps_the_marks_s_shows(void)
+{
+    static uint8_t a[200 * 93], b[200 * 93], c[200 * 93];
+    static const int16_t k_cells[][2] = { { 200, 22 }, { 66, 69 }, { 50, 93 }, { 89, 30 } };
+    ui_context_t ctx = fixture_context();
+    for (size_t i = 0; i < sizeof(k_cells) / sizeof(k_cells[0]); i++) {
+        int w = k_cells[i][0], h = k_cells[i][1];
+        size_t n = (size_t)(w * h);
+        fixture_single(&s_fix_ds, 2340, 4500); /* one reading: the low and the high equal it */
+        cell_bits(&ctx, UI_FIELD_ENV_TEMP, w, h, a);
+        cell_bits(&ctx, UI_FIELD_ENV_TEMP_MIN, w, h, b);
+        cell_bits(&ctx, UI_FIELD_ENV_TEMP_MAX, w, h, c);
+        TEST_ASSERT_FALSE(memcmp(a, b, n) == 0);
+        TEST_ASSERT_FALSE(memcmp(a, c, n) == 0);
+        TEST_ASSERT_FALSE(memcmp(b, c, n) == 0);
+        ds_set_battery(&s_fix_ds, 100, 4180, DS_BAT_CHARGING, FIX_NOW);
+        cell_bits(&ctx, UI_FIELD_BAT_LEVEL, w, h, a);
+        ds_set_battery(&s_fix_ds, 100, 4180, DS_BAT_DISCHARGING, FIX_NOW);
+        cell_bits(&ctx, UI_FIELD_BAT_LEVEL, w, h, b);
+        TEST_ASSERT_FALSE(memcmp(a, b, n) == 0);
+    }
+    for (int w = 40; w <= 119; w++) { /* the disc stays, at the start of the line */
+        gfx_rect_t r = { (int16_t)(400 - w), 278, (int16_t)w, 22 };
+        gfx_fb_init(&s_fb, s_buf, 400, 300);
+        gfx_clear(&s_fb, GFX_WHITE);
+        ui_draw_cell(&s_fb, r, &ctx, UI_FIELD_MOON_PHASE, UI_STALE_STALE);
+        TEST_ASSERT_TRUE(inked(r.x + 3, r.x + 18, r.y + 3, r.y + 18));
+        TEST_ASSERT_FALSE(has_ellipsis(r));
+    }
+}
+
 /* A field with no room in its cell isn't drawn at all, rather than cut. */
 static void test_a_field_without_room_draws_nothing(void)
 {
@@ -296,5 +522,8 @@ int main(void)
     RUN_TEST(test_a_number_keeps_its_size_as_its_digits_change);
     RUN_TEST(test_every_field_fits_every_cell_a_split_can_make);
     RUN_TEST(test_a_field_without_room_draws_nothing);
+    RUN_TEST(test_every_small_field_fits_every_xs_cell);
+    RUN_TEST(test_xs_draws_one_line_or_the_symbol_over_the_value);
+    RUN_TEST(test_xs_keeps_the_marks_s_shows);
     return UNITY_END();
 }
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host -- -k 0 2>&1 | grep 'error:' | sed -E 's/.*test\/host\/([a-z_]+\.c):[0-9:]+ error: /\1: /' | sort | uniq -c; ./build-host/test_ui_catalog | grep FAIL`
Expected: three test files don't compile, and the catalogue's test fails:

```
   2 test_lang.c: use of undeclared identifier 'LANG_DATE_DAY'; did you mean 'LANG_DATE_LONG'?
  13 test_ui_split.c: use of undeclared identifier 'UI_SIZE_XS'
   1 test_ui_widget_fit.c: use of undeclared identifier 'UI_SIZE_XS'
…/test/host/test_ui_catalog.c:124:test_layouts_publish_the_split_rules:FAIL: Expected 5 Was 4
FAIL
```

- [ ] **Step 3: The 16 px icons.** Add the size to the field icons' lines, then generate:

`assets/icons/icons.txt`:

```diff
--- a/assets/icons/icons.txt
+++ b/assets/icons/icons.txt
@@ -2,41 +2,41 @@
 # C name       Material Icons name, or wi:<name> from Weather Icons   sizes (px)
 thermometer    device_thermostat     16 24 48
 drop           water_drop            16 24 48
-dew            dew_point             24 48
+dew            dew_point             16 24 48
 bolt           bolt                  16 24
 stale          sync_problem          16 24
-clock          schedule              24 48
-calendar       calendar_today        24 48
-person         person                24 48
-celebration    celebration           24 48
-cloud          cloud                 24 48
+clock          schedule              16 24 48
+calendar       calendar_today        16 24 48
+person         person                16 24 48
+celebration    celebration           16 24 48
+cloud          cloud                 16 24 48
 web            language              16
 sync           sync                  16
 sync_failed    cloud_off             16
 wifi           wifi                  16
 wifi_off       wifi_off              16
-air            air                   24 48
-particles      blur_on               24 48
-uv             light_mode            24 48
-pollen         local_florist         24 48
-# Weather codes (spec §11), day and night where they differ, and the sun's times (M5).
-wx_clear_day        wi:day-sunny                24 48
-wx_clear_night      wi:night-clear              24 48
-wx_mainly_day       wi:day-sunny-overcast       24 48
-wx_mainly_night     wi:night-alt-partly-cloudy  24 48
-wx_partly_day       wi:day-cloudy               24 48
-wx_partly_night     wi:night-alt-cloudy         24 48
-wx_overcast         wi:cloudy                   24 48
-wx_fog              wi:fog                      24 48
-wx_drizzle          wi:sprinkle                 24 48
-wx_rain             wi:rain                     24 48
-wx_freezing         wi:sleet                    24 48
-wx_snow             wi:snow                     24 48
-wx_showers_day      wi:day-showers              24 48
-wx_showers_night    wi:night-alt-showers        24 48
-wx_snow_showers_day   wi:day-snow               24 48
-wx_snow_showers_night wi:night-alt-snow         24 48
-wx_thunder          wi:thunderstorm             24 48
-wx_unknown          wi:na                       24 48
-sunrise             wi:sunrise                  24 48
-sunset              wi:sunset                   24 48
+air            air                   16 24 48
+particles      blur_on               16 24 48
+uv             light_mode            16 24 48
+pollen         local_florist         16 24 48
+# Weather codes (spec §11), day and night where they differ, and the sun's times (M5); 16 px for XS cells (M6c).
+wx_clear_day        wi:day-sunny                16 24 48
+wx_clear_night      wi:night-clear              16 24 48
+wx_mainly_day       wi:day-sunny-overcast       16 24 48
+wx_mainly_night     wi:night-alt-partly-cloudy  16 24 48
+wx_partly_day       wi:day-cloudy               16 24 48
+wx_partly_night     wi:night-alt-cloudy         16 24 48
+wx_overcast         wi:cloudy                   16 24 48
+wx_fog              wi:fog                      16 24 48
+wx_drizzle          wi:sprinkle                 16 24 48
+wx_rain             wi:rain                     16 24 48
+wx_freezing         wi:sleet                    16 24 48
+wx_snow             wi:snow                     16 24 48
+wx_showers_day      wi:day-showers              16 24 48
+wx_showers_night    wi:night-alt-showers        16 24 48
+wx_snow_showers_day   wi:day-snow               16 24 48
+wx_snow_showers_night wi:night-alt-snow         16 24 48
+wx_thunder          wi:thunderstorm             16 24 48
+wx_unknown          wi:na                       16 24 48
+sunrise             wi:sunrise                  16 24 48
+sunset              wi:sunset                   16 24 48
```


Run: `tools/gen_icons.sh && git diff --stat plan/m6c -- components/gfx`
Expected: `components/gfx/icons/gfx_icons.c: 105 icons`, and no difference from the branch (the generator and Pillow are pinned).

- [ ] **Step 4: The size.** XS comes first in `ui_size_t`, takes S's kinds from 40×20, and the size loops run down to it:

`components/ui/include/ui_layout.h`:

```diff
--- a/components/ui/include/ui_layout.h
+++ b/components/ui/include/ui_layout.h
@@ -22,6 +22,7 @@ typedef enum {
 } ui_layout_id_t;
 
 typedef enum {
+    UI_SIZE_XS, /* M6c (D34): split cells from 40×20, drawn like the status bar (spec §5.3) */
     UI_SIZE_S,
     UI_SIZE_M,
     UI_SIZE_L,
@@ -33,6 +34,7 @@ typedef enum {
     (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_DATE) | UI_KIND(UI_FK_NUMBER) | UI_KIND(UI_FK_BATTERY) |                    \
      UI_KIND(UI_FK_MOON) | UI_KIND(UI_FK_TEXT) | UI_KIND(UI_FK_WEATHER_NOW) | UI_KIND(UI_FK_WEATHER_DAY) |           \
      UI_KIND(UI_FK_SUN) | UI_KIND(UI_FK_LEVEL) | UI_KIND(UI_FK_POLLEN))
+#define UI_KINDS_XS UI_KINDS_S
 #define UI_KINDS_M (UI_KINDS_S | UI_KIND(UI_FK_SERIES) | UI_KIND(UI_FK_RAIN_MAP))
 #define UI_KINDS_L (UI_KINDS_S | UI_KIND(UI_FK_RAIN_MAP))
 #define UI_KINDS_XL (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_NUMBER))
```


`components/ui/ui_split.c`:

```diff
--- a/components/ui/ui_split.c
+++ b/components/ui/ui_split.c
@@ -19,6 +19,7 @@ static const struct {
     int16_t min_w, narrow_h, wide_h;
     uint32_t kinds;
 } k_sizes[] = {
+    [UI_SIZE_XS] = { 40, 20, 20, UI_KINDS_XS },
     [UI_SIZE_S] = { 90, 80, 40, UI_KINDS_S },
     [UI_SIZE_M] = { 130, 80, 80, UI_KINDS_M },
     [UI_SIZE_L] = { 200, 150, 150, UI_KINDS_L },
@@ -157,7 +158,7 @@ int ui_split_need(ui_size_t size, ui_field_kind_t kind, bool narrow)
 
 int ui_split_cell_size(int w, int h)
 {
-    for (int size = UI_SIZE_XL; size >= UI_SIZE_S; size--) {
+    for (int size = UI_SIZE_XL; size >= UI_SIZE_XS; size--) {
         if (w >= k_sizes[size].min_w && h >= ui_split_min_h((ui_size_t)size, w < UI_SPLIT_NARROW_W)) {
             return size;
         }
@@ -167,7 +168,7 @@ int ui_split_cell_size(int w, int h)
 
 int ui_split_field_size(ui_field_kind_t kind, int w, int h)
 {
-    for (int size = UI_SIZE_XL; size >= UI_SIZE_S; size--) {
+    for (int size = UI_SIZE_XL; size >= UI_SIZE_XS; size--) {
         int need = ui_split_need((ui_size_t)size, kind, w < UI_SPLIT_NARROW_W);
         if (w >= k_sizes[size].min_w && need >= 0 && h >= need) {
             return size;
```


`components/ui/ui_catalog.c`:

```diff
--- a/components/ui/ui_catalog.c
+++ b/components/ui/ui_catalog.c
@@ -26,7 +26,8 @@ static const char *const k_kinds[UI_FK_COUNT] = {
     [UI_FK_RAIN_MAP] = "rain_map",
 };
 _Static_assert(UI_FK_COUNT == 13, "every kind has a name in the catalogue");
-static const char *const k_sizes[] = { "S", "M", "L", "XL" };
+static const char *const k_sizes[] = { [UI_SIZE_XS] = "XS", [UI_SIZE_S] = "S", [UI_SIZE_M] = "M",
+                                       [UI_SIZE_L] = "L", [UI_SIZE_XL] = "XL" };
 
 static size_t print(cJSON *root, char *out, size_t size)
 {
@@ -55,7 +56,7 @@ static void add_split(cJSON *root)
         cJSON_AddItemToArray(ratios, cJSON_CreateString(ui_split_ratio_name(r)));
     }
     cJSON *sizes = cJSON_AddArrayToObject(split, "sizes");
-    for (int s = UI_SIZE_XL; s >= UI_SIZE_S; s--) {
+    for (int s = UI_SIZE_XL; s >= UI_SIZE_XS; s--) {
         cJSON *so = cJSON_CreateObject();
         cJSON_AddStringToObject(so, "size", k_sizes[s]);
         cJSON_AddNumberToObject(so, "min_w", ui_split_min_w((ui_size_t)s));
```


- [ ] **Step 5: The date's day form.**

`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -206,6 +206,7 @@ typedef enum {
     LANG_DATE_LONG,   /* Friday 25 September */
     LANG_DATE_MEDIUM, /* Fri 25 Sep */
     LANG_DATE_SHORT,  /* 25 Sep */
+    LANG_DATE_DAY,    /* Fri 25: the weekday and the day (M6c, XS cells) */
 } lang_date_style_t;
 
 typedef struct lang {
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -15,6 +15,9 @@ static void format_date(const lang_t *lang, const struct tm *tm, lang_date_style
     case LANG_DATE_SHORT:
         snprintf(out, size, "%d %s", tm->tm_mday, lang->months_short[tm->tm_mon]);
         break;
+    case LANG_DATE_DAY:
+        snprintf(out, size, "%s %d", lang->weekdays_short[tm->tm_wday], tm->tm_mday);
+        break;
     }
 }
 
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -19,6 +19,9 @@ static void format_date(const lang_t *lang, const struct tm *tm, lang_date_style
     case LANG_DATE_SHORT:
         snprintf(out, size, "%d. %d.", tm->tm_mday, tm->tm_mon + 1);
         break;
+    case LANG_DATE_DAY:
+        snprintf(out, size, "%s %d.", lang->weekdays_short[tm->tm_wday], tm->tm_mday);
+        break;
     }
 }
 
```


`components/ui/ui_fields.c`:

```diff
--- a/components/ui/ui_fields.c
+++ b/components/ui/ui_fields.c
@@ -169,6 +169,7 @@ static void resolve_clock(const ui_context_t *ctx, ui_field_id_t field, ui_value
     case UI_FIELD_DATE_DAY:
         lang_format_date(ctx->lang, tm, LANG_DATE_LONG, out->text, sizeof(out->text));
         lang_format_date(ctx->lang, tm, LANG_DATE_MEDIUM, out->extra, sizeof(out->extra));
+        lang_format_date(ctx->lang, tm, LANG_DATE_DAY, out->short_text, sizeof(out->short_text)); /* XS (D34) */
         out->state = out->text[0] ? UI_VALUE_FRESH : UI_VALUE_MISSING;
         break;
     case UI_FIELD_DATE_WEEK:
```


- [ ] **Step 6: XS numbers, words, the date, the battery and the Moon.** `fit_tiny()` tries the largest bold face first, with the unit and the trend arrow, then without the arrow, then (stacked) with the unit on its own line, then without the unit, each with the value whole or in its short form; it changes the caller's value in place, so the text it settles on points into the caller's value, never into a copy. A number that would only fit cut beside its symbol stands alone, centred, as a time does. Words take bold 20, bold 16 or sans 12 (`tiny_text_face()`), and are cut only when none fits; the date is "Fri 25 Sep" or "Fri 25" in one line and "Fri" over "25" stacked; the Moon keeps its disc with its name, its short name or its illumination, whichever fits. Today's low and high keep their arrow beside the thermometer, a charging battery its bolt.

`components/ui/ui_widget.c`:

```diff
--- a/components/ui/ui_widget.c
+++ b/components/ui/ui_widget.c
@@ -20,6 +20,7 @@ typedef struct {
 } ui_fonts_t;
 
 static const ui_fonts_t k_fonts[] = {
+    [UI_SIZE_XS] = { NULL, &gfx_font_sans_bold_16, &gfx_font_sans_bold_16, &gfx_font_sans_12, 16 },
     [UI_SIZE_S] = { NULL, &gfx_font_sans_bold_28, &gfx_font_sans_bold_16, &gfx_font_sans_16, 24 },
     [UI_SIZE_M] = { &gfx_font_sans_12, &gfx_font_num_cb_48, &gfx_font_sans_bold_20, &gfx_font_sans_16, 48 },
     [UI_SIZE_L] = { &gfx_font_sans_16, &gfx_font_num_cb_72, &gfx_font_sans_bold_28, &gfx_font_sans_bold_20, 48 },
@@ -43,50 +44,50 @@ static int digit_height(const gfx_font_t *f)
 
 static const gfx_bitmap_t *field_icon(ui_field_id_t field, int size)
 {
-    const gfx_bitmap_t *s24 = NULL, *s48 = NULL;
+    const gfx_bitmap_t *s16 = NULL, *s24 = NULL, *s48 = NULL;
     switch (field) {
     case UI_FIELD_ENV_TEMP:
     case UI_FIELD_ENV_TEMP_MIN:
     case UI_FIELD_ENV_TEMP_MAX:
-        s24 = &gfx_icon_thermometer_24, s48 = &gfx_icon_thermometer_48;
+        s16 = &gfx_icon_thermometer_16, s24 = &gfx_icon_thermometer_24, s48 = &gfx_icon_thermometer_48;
         break;
     case UI_FIELD_ENV_HUM:
-        s24 = &gfx_icon_drop_24, s48 = &gfx_icon_drop_48;
+        s16 = &gfx_icon_drop_16, s24 = &gfx_icon_drop_24, s48 = &gfx_icon_drop_48;
         break;
     case UI_FIELD_ENV_DEW:
-        s24 = &gfx_icon_dew_24, s48 = &gfx_icon_dew_48;
+        s16 = &gfx_icon_dew_16, s24 = &gfx_icon_dew_24, s48 = &gfx_icon_dew_48;
         break;
     case UI_FIELD_TIME_CLOCK:
-        s24 = &gfx_icon_clock_24, s48 = &gfx_icon_clock_48;
+        s16 = &gfx_icon_clock_16, s24 = &gfx_icon_clock_24, s48 = &gfx_icon_clock_48;
         break;
     case UI_FIELD_DATE_DAY:
     case UI_FIELD_DATE_WEEK:
-        s24 = &gfx_icon_calendar_24, s48 = &gfx_icon_calendar_48;
+        s16 = &gfx_icon_calendar_16, s24 = &gfx_icon_calendar_24, s48 = &gfx_icon_calendar_48;
         break;
     case UI_FIELD_DATE_NAMEDAY:
-        s24 = &gfx_icon_person_24, s48 = &gfx_icon_person_48;
+        s16 = &gfx_icon_person_16, s24 = &gfx_icon_person_24, s48 = &gfx_icon_person_48;
         break;
     case UI_FIELD_DATE_HOLIDAY:
-        s24 = &gfx_icon_celebration_24, s48 = &gfx_icon_celebration_48;
+        s16 = &gfx_icon_celebration_16, s24 = &gfx_icon_celebration_24, s48 = &gfx_icon_celebration_48;
         break;
     case UI_FIELD_WX_NOW:
     case UI_FIELD_WX_TODAY:
     case UI_FIELD_WX_HOURLY:
-    case UI_FIELD_WX_DAILY:
-        s24 = &gfx_icon_cloud_24, s48 = &gfx_icon_cloud_48; /* missing: drawn as the placeholder */
+    case UI_FIELD_WX_DAILY: /* missing: drawn as the placeholder */
+        s16 = &gfx_icon_cloud_16, s24 = &gfx_icon_cloud_24, s48 = &gfx_icon_cloud_48;
         break;
     case UI_FIELD_SUN_TIMES:
-        s24 = &gfx_icon_sunrise_24, s48 = &gfx_icon_sunrise_48;
+        s16 = &gfx_icon_sunrise_16, s24 = &gfx_icon_sunrise_24, s48 = &gfx_icon_sunrise_48;
         break;
     case UI_FIELD_AQ_INDEX:
-        s24 = &gfx_icon_air_24, s48 = &gfx_icon_air_48;
+        s16 = &gfx_icon_air_16, s24 = &gfx_icon_air_24, s48 = &gfx_icon_air_48;
         break;
     case UI_FIELD_AQ_PM25:
     case UI_FIELD_AQ_PM10:
-        s24 = &gfx_icon_particles_24, s48 = &gfx_icon_particles_48;
+        s16 = &gfx_icon_particles_16, s24 = &gfx_icon_particles_24, s48 = &gfx_icon_particles_48;
         break;
     case UI_FIELD_AQ_UV:
-        s24 = &gfx_icon_uv_24, s48 = &gfx_icon_uv_48;
+        s16 = &gfx_icon_uv_16, s24 = &gfx_icon_uv_24, s48 = &gfx_icon_uv_48;
         break;
     case UI_FIELD_POLLEN_TOP:
     case UI_FIELD_POLLEN_ALDER:
@@ -95,12 +96,12 @@ static const gfx_bitmap_t *field_icon(ui_field_id_t field, int size)
     case UI_FIELD_POLLEN_MUGWORT:
     case UI_FIELD_POLLEN_OLIVE:
     case UI_FIELD_POLLEN_RAGWEED:
-        s24 = &gfx_icon_pollen_24, s48 = &gfx_icon_pollen_48;
+        s16 = &gfx_icon_pollen_16, s24 = &gfx_icon_pollen_24, s48 = &gfx_icon_pollen_48;
         break;
     default:
         break;
     }
-    return size >= 48 ? s48 : s24;
+    return size >= 48 ? s48 : size >= 24 ? s24 : s16;
 }
 
 /* How far `text`'s ink reaches below the baseline in `f`: a comma's tail, the "g" of "µg/m³". */
@@ -116,6 +117,29 @@ static int ink_below(const gfx_font_t *f, const char *text)
     return below;
 }
 
+/* How far `text`'s ink reaches above the baseline in `f`: a capital's caron ("Čt") rises above the digits. */
+static int ink_above(const gfx_font_t *f, const char *text)
+{
+    int above = 0;
+    for (const char *s = text; *s != '\0';) {
+        const gfx_glyph_t *g = gfx_font_glyph(f, gfx_utf8_next(&s));
+        if (g != NULL && -g->y_offset > above) {
+            above = -g->y_offset;
+        }
+    }
+    return above;
+}
+
+int ui_ink_above(const gfx_font_t *f, const char *text)
+{
+    return ink_above(f, text);
+}
+
+int ui_ink_below(const gfx_font_t *f, const char *text)
+{
+    return ink_below(f, text);
+}
+
 /* The number's digits centred in max_h, with room below for its tail and its unit's, and 2 px to
  * each edge. Every digit counts as the deepest one ("5" dips 2 px in the 130 px face), so a number
  * keeps its size as its digits change. */
@@ -347,6 +371,297 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
     draw_group(fb, f, vf, &shown, value, x, r.y + (r.h + digit_height(vf)) / 2);
 }
 
+/* ---- XS (M6c, D34): the status bar's look ---- */
+
+/* The faces an XS value tries, largest first; a number's unit goes smaller beside it. */
+static const gfx_font_t *const k_fit_xs[] = { &gfx_font_sans_bold_28, &gfx_font_sans_bold_20, &gfx_font_sans_bold_16,
+                                              &gfx_font_sans_12 };
+
+static const gfx_font_t *xs_unit_font(const gfx_font_t *vf)
+{
+    return vf == &gfx_font_sans_bold_28 ? &gfx_font_sans_16 : &gfx_font_sans_12;
+}
+
+bool ui_tiny_stacked(gfx_rect_t r)
+{
+    return r.w < 120 && r.h >= 44;
+}
+
+/* A number's group in the largest of `count` faces from k_fit_xs that fits max_w and, its digits and tails,
+ * max_h (fits_height()), with the unit and the trend arrow, then without the arrow, then (with `below`, where
+ * `unit_h` more fits) with the unit under it, then without the unit; each with the value as it is or in its short
+ * form. Else sans 12 alone, cut. It changes *shown's unit and trend in place, as the fit drew them; *below says
+ * the unit goes under the value. */
+static const gfx_font_t *fit_tiny(ui_value_t *shown, const char **value, int max_w, int max_h, size_t count,
+                                  ui_fonts_t *uf, bool *below, int unit_h, char *buf, size_t size)
+{
+    char unit[sizeof(shown->unit)];
+    memcpy(unit, shown->unit, sizeof(unit));
+    int trend = shown->trend;
+    const char *full = *value;
+    if (below != NULL) {
+        *below = false;
+    }
+    for (int pass = 0; pass < 4; pass++) {
+        if (pass == 2 && (below == NULL || !unit[0] || gfx_text_width(&gfx_font_sans_12, unit) > max_w)) {
+            continue;
+        }
+        shown->trend = pass >= 1 ? 0 : trend;
+        memcpy(shown->unit, unit, sizeof(unit));
+        if (pass >= 2) {
+            shown->unit[0] = '\0';
+        }
+        for (size_t i = 0; i < count; i++) {
+            const gfx_font_t *f = k_fit_xs[i];
+            uf->unit = xs_unit_font(f);
+            const char *forms[2] = { full, shown->short_text };
+            for (int k = 0; k < 2; k++) {
+                if (forms[k][0] && group_width(uf, f, shown, forms[k]) <= max_w &&
+                    fits_height(uf, f, shown, forms[k], max_h - (pass == 2 ? unit_h : 0))) {
+                    *value = forms[k];
+                    if (below != NULL) {
+                        *below = pass == 2;
+                    }
+                    return f;
+                }
+            }
+        }
+    }
+    shown->trend = 0;
+    shown->unit[0] = '\0';
+    uf->unit = xs_unit_font(&gfx_font_sans_12);
+    gfx_text_ellipsize(&gfx_font_sans_12, shown->short_text[0] ? shown->short_text : full, max_w, buf, size);
+    *value = buf;
+    return &gfx_font_sans_12;
+}
+
+/* Words: the largest of bold 20, bold 16 and sans 12 that fits max_w, its ink 2 px clear of either edge of max_h;
+ * NULL when none does. */
+static const gfx_font_t *tiny_text_face(const char *text, int max_w, int max_h)
+{
+    static const gfx_font_t *const k_faces[] = { &gfx_font_sans_bold_20, &gfx_font_sans_bold_16, &gfx_font_sans_12 };
+    for (size_t i = 0; i < sizeof(k_faces) / sizeof(k_faces[0]); i++) {
+        const gfx_font_t *f = k_faces[i];
+        if (gfx_text_width(f, text) <= max_w && ink_above(f, text) + ink_below(f, text) + 4 <= max_h) {
+            return f;
+        }
+    }
+    return NULL;
+}
+
+/* Words as tiny_text_face() fits them, or sans 12 cut to max_w. */
+static const gfx_font_t *fit_tiny_text(const char *text, int max_w, int max_h, char *buf, size_t size)
+{
+    const gfx_font_t *f = tiny_text_face(text, max_w, max_h);
+    if (f != NULL) {
+        snprintf(buf, size, "%s", text);
+        return f;
+    }
+    gfx_text_ellipsize(&gfx_font_sans_12, text, max_w, buf, size);
+    return &gfx_font_sans_12;
+}
+
+/* What S marks beside a symbol, XS too: today's low (↓) or high (↑); a charging battery's bolt. */
+static const char *tiny_mark(const ui_value_t *v)
+{
+    return v->field == UI_FIELD_ENV_TEMP_MIN ? ARROW_DOWN : v->field == UI_FIELD_ENV_TEMP_MAX ? ARROW_UP : NULL;
+}
+
+static bool tiny_bolt(const ui_value_t *v)
+{
+    return v->kind == UI_FK_BATTERY && v->state != UI_VALUE_MISSING && v->battery == DS_BAT_CHARGING;
+}
+
+/* The symbol's size at `sym` px, its marks included: the battery's outline and bolt, the Moon, or the field's icon
+ * and its arrow; 0 × 0 for a time and for a field without one. */
+static void tiny_symbol_size(const ui_value_t *v, int sym, int *w, int *h)
+{
+    *w = *h = 0;
+    if (v->kind == UI_FK_BATTERY) {
+        *w = sym + 6 + (tiny_bolt(v) ? 2 + 16 : 0), *h = sym / 2 + 2;
+        *h = tiny_bolt(v) && *h < 16 ? 16 : *h;
+    } else if (v->kind == UI_FK_MOON) {
+        *w = *h = sym;
+    } else if (v->kind != UI_FK_TIME) {
+        const gfx_bitmap_t *icon = field_icon(v->field, sym);
+        if (icon != NULL) {
+            const char *mark = tiny_mark(v);
+            *w = icon->width + (mark != NULL ? 1 + gfx_text_width(&gfx_font_sans_12, mark) : 0), *h = icon->height;
+        }
+    }
+}
+
+/* The symbol with its top left at (x, y), `h` px tall as tiny_symbol_size() gave it. */
+static void draw_tiny_symbol(gfx_fb_t *fb, const ui_value_t *v, int x, int y, int sym, int h)
+{
+    int cy = y + h / 2;
+    if (v->kind == UI_FK_BATTERY) {
+        int bh = sym / 2 + 2;
+        ui_draw_battery(fb, x, cy - bh / 2, sym + 6, bh, v->state == UI_VALUE_MISSING ? -1 : v->percent);
+        if (tiny_bolt(v)) {
+            gfx_bitmap(fb, x + sym + 6 + 2, cy - 8, &gfx_icon_bolt_16, GFX_BLACK);
+        }
+    } else if (v->kind == UI_FK_MOON) {
+        if (v->state == UI_VALUE_MISSING) {
+            gfx_circle(fb, x + sym / 2, cy, sym / 2 - 1, GFX_BLACK);
+        } else {
+            ui_draw_moon(fb, x + sym / 2, cy, sym / 2 - 1, v->moon.age);
+        }
+    } else if (v->kind != UI_FK_TIME) {
+        const gfx_bitmap_t *icon = field_icon(v->field, sym);
+        if (icon != NULL) {
+            gfx_bitmap(fb, x, cy - icon->height / 2, icon, GFX_BLACK);
+            const char *mark = tiny_mark(v);
+            if (mark != NULL) {
+                gfx_text(fb, &gfx_font_sans_12, x + icon->width + 1, cy + icon->height / 2, mark, GFX_BLACK);
+            }
+        }
+    }
+}
+
+/* One line, like the status bar: the 16 px symbol (24 px from 34 px of height), then the value. A number that would
+ * only fit cut beside its symbol stands alone, centred, as a time does; words keep 4 px at the right for an
+ * accent's overhang. The date is "Fri 25 Sep", or "Fri 25" where that doesn't fit; the Moon keeps its disc, with
+ * its name, its short name or its illumination, whichever fits. */
+static void draw_tiny_line(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
+{
+    int sym = r.h >= 34 ? 24 : 16;
+    int sym_w, sym_h;
+    tiny_symbol_size(v, sym, &sym_w, &sym_h);
+    int cy = r.y + r.h / 2, top = cy - sym_h / 2;
+    char fit[48];
+    if (numeric(v)) {
+        for (int with = sym_w > 0; with >= 0; with--) {
+            bool centre = v->kind == UI_FK_TIME || (sym_w > 0 && !with); /* alone, or its symbol given up */
+            int x = r.x + 3 + (with ? sym_w + 3 : 0), max_w = centre ? r.w - 4 : r.x + r.w - 2 - x;
+            ui_value_t shown = *v;
+            ui_fonts_t uf = k_fonts[UI_SIZE_XS];
+            const char *value = v->text;
+            const gfx_font_t *vf = fit_tiny(&shown, &value, max_w, r.h, 4, &uf, NULL, 0, fit, sizeof(fit));
+            if (value == fit && with) {
+                continue; /* cut beside the symbol: the value alone */
+            }
+            if (with) {
+                draw_tiny_symbol(fb, v, r.x + 3, top, sym, sym_h);
+            }
+            int w = group_width(&uf, vf, &shown, value);
+            draw_group(fb, &uf, vf, &shown, value, centre ? r.x + (r.w - w) / 2 : x, cy + digit_height(vf) / 2);
+            return;
+        }
+    }
+    if (v->kind == UI_FK_MOON && v->state != UI_VALUE_MISSING) {
+        draw_tiny_symbol(fb, v, r.x + 3, top, sym, sym_h);
+        int x = r.x + 3 + sym_w + 4, max_w = r.x + r.w - 4 - x;
+        const char *forms[3] = { v->text, v->short_text, v->extra };
+        for (int k = r.w >= 120 ? 0 : 1; k < 3; k++) {
+            const gfx_font_t *tf = tiny_text_face(forms[k], max_w, r.h);
+            if (tf != NULL) {
+                gfx_text(fb, tf, x, r.y + (r.h + ink_above(tf, forms[k]) - ink_below(tf, forms[k])) / 2, forms[k],
+                         GFX_BLACK);
+                return;
+            }
+        }
+        return; /* the disc alone */
+    }
+    const char *text = v->state == UI_VALUE_MISSING ? PLACEHOLDER : v->kind == UI_FK_DATE ? v->extra : v->text;
+    for (int with = sym_w > 0; with >= 0; with--) {
+        int x = r.x + 3 + (with ? sym_w + 3 : 0), max_w = r.x + r.w - 4 - x;
+        const char *t = text;
+        if (v->kind == UI_FK_DATE && v->state != UI_VALUE_MISSING && gfx_text_width(&gfx_font_sans_12, t) > max_w) {
+            t = v->short_text;
+        }
+        if (with && gfx_text_width(&gfx_font_sans_12, t) > max_w) {
+            continue;
+        }
+        const gfx_font_t *tf = fit_tiny_text(t, max_w, r.h, fit, sizeof(fit));
+        if (with) {
+            draw_tiny_symbol(fb, v, r.x + 3, top, sym, sym_h);
+        }
+        gfx_text(fb, tf, x, r.y + (r.h + ink_above(tf, fit) - ink_below(tf, fit)) / 2, fit, GFX_BLACK);
+        return;
+    }
+}
+
+/* The symbol over the value, both centred: a 16 px symbol (24 px from 60 px of height) with its marks; the date
+ * as its weekday over its day; a time alone. A number's group fits 2 px inside either edge, words 4 px. */
+static void draw_tiny_stacked(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
+{
+    int sym = r.h >= 60 ? 24 : 16;
+    int cx = r.x + r.w / 2;
+    char fit[48];
+    if (v->kind == UI_FK_DATE && v->state != UI_VALUE_MISSING) { /* "Fri" over "25", "Pá" over "25." */
+        char wd[16];
+        snprintf(wd, sizeof(wd), "%s", v->short_text);
+        char *space = strrchr(wd, ' ');
+        const char *day = space != NULL ? space + 1 : wd;
+        if (space != NULL) {
+            *space = '\0';
+        }
+        const char *weekday = space != NULL ? wd : "";
+        const gfx_font_t *wf = &gfx_font_sans_12;
+        int above = ink_above(wf, weekday);
+        const gfx_font_t *df = &gfx_font_sans_12;
+        for (size_t i = 0; i < sizeof(k_fit_xs) / sizeof(k_fit_xs[0]); i++) {
+            if (above + 4 + digit_height(k_fit_xs[i]) + 4 <= r.h && gfx_text_width(k_fit_xs[i], day) <= r.w - 4) {
+                df = k_fit_xs[i];
+                break;
+            }
+        }
+        int block = above + 4 + digit_height(df);
+        int top = r.y + (r.h - block) / 2;
+        gfx_text(fb, wf, cx - gfx_text_width(wf, weekday) / 2, top + above, weekday, GFX_BLACK);
+        gfx_text(fb, df, cx - gfx_text_width(df, day) / 2, top + block, day, GFX_BLACK);
+        return;
+    }
+    int sym_w, sym_h;
+    tiny_symbol_size(v, sym, &sym_w, &sym_h);
+    int gap = sym_h ? 4 : 0;
+    int room = r.h - 4 - sym_h - gap;
+    const gfx_font_t *vf;
+    const char *value;
+    ui_value_t shown = *v;
+    ui_fonts_t uf = k_fonts[UI_SIZE_XS];
+    bool below = false;
+    int unit_h = 4 + ink_above(&gfx_font_sans_12, v->unit) + ink_below(&gfx_font_sans_12, v->unit);
+    int value_h; /* the value's ink: from its top to its lowest tail */
+    if (numeric(v)) {
+        value = v->text;
+        vf = fit_tiny(&shown, &value, r.w - 4, room + 4, 3, &uf, &below, unit_h, fit, sizeof(fit));
+        int tail = ink_below(vf, value);
+        int unit_tail = shown.unit[0] ? ink_below(uf.unit, shown.unit) : 0;
+        value_h = digit_height(vf) + (tail > unit_tail ? tail : unit_tail);
+    } else {
+        const char *text = v->state == UI_VALUE_MISSING ? PLACEHOLDER : v->kind == UI_FK_MOON ? v->short_text : v->text;
+        vf = fit_tiny_text(text, r.w - 8, room + 4, fit, sizeof(fit));
+        value = fit;
+        value_h = ink_above(vf, value) + ink_below(vf, value);
+    }
+    int block = sym_h + gap + value_h + (below ? unit_h : 0);
+    int top = r.y + (r.h - block) / 2;
+    draw_tiny_symbol(fb, v, cx - sym_w / 2, top, sym, sym_h);
+    int y = top + sym_h + gap;
+    if (numeric(v)) {
+        int baseline = y + digit_height(vf);
+        draw_group(fb, &uf, vf, &shown, value, cx - group_width(&uf, vf, &shown, value) / 2, baseline);
+        if (below) {
+            int base = top + block - ink_below(&gfx_font_sans_12, v->unit);
+            gfx_text(fb, &gfx_font_sans_12, cx - gfx_text_width(&gfx_font_sans_12, v->unit) / 2, base, v->unit,
+                     GFX_BLACK);
+        }
+        return;
+    }
+    gfx_text(fb, vf, cx - gfx_text_width(vf, value) / 2, y + ink_above(vf, value), value, GFX_BLACK);
+}
+
+static void draw_tiny(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
+{
+    if (ui_tiny_stacked(r)) {
+        draw_tiny_stacked(fb, r, v);
+    } else {
+        draw_tiny_line(fb, r, v);
+    }
+}
+
 static void draw_labelled(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
 {
     const ui_fonts_t *f = &k_fonts[size];
@@ -447,12 +762,14 @@ void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t
         /* the weather, air quality, pollen and sun widgets (ui_forecast.c) */
     } else if (ui_radar_widget(fb, r, size, &shown)) {
         /* rain.map (ui_radar.c) */
+    } else if (size == UI_SIZE_XS) {
+        draw_tiny(fb, r, &shown);
     } else if (size == UI_SIZE_S) {
         draw_small(fb, r, &shown);
     } else {
         draw_labelled(fb, r, size, &shown);
     }
-    if (shown.state == UI_VALUE_STALE) {
+    if (shown.state == UI_VALUE_STALE && size != UI_SIZE_XS) { /* XS has no room; the status bar marks it */
         draw_age(fb, r, &shown, lang);
     }
     fb->clip = saved;
```


`components/ui/ui_internal.h`:

```diff
--- a/components/ui/ui_internal.h
+++ b/components/ui/ui_internal.h
@@ -24,9 +24,15 @@ bool ui_radar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_
 bool ui_resolve_forecast(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
 /* Their widgets; false for a kind they don't draw. */
 bool ui_forecast_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v);
-/* A sky's icon at 24 or 48 px, its night variant where it has one. */
+/* A sky's icon at 16, 24 or 48 px, its night variant where it has one. */
 const gfx_bitmap_t *ui_sky_icon(int sky, bool night, int size);
 
+/* XS draws the symbol over the value in a cell narrower than 120 px and at least 44 tall, else one line
+ * (ui_widget.c, D34). */
+bool ui_tiny_stacked(gfx_rect_t r);
+/* How far `text`'s ink reaches above and below the baseline in `f` (ui_widget.c). */
+int ui_ink_above(const gfx_font_t *f, const char *text);
+int ui_ink_below(const gfx_font_t *f, const char *text);
 void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, ui_stale_policy_t policy,
                     const lang_t *lang);
 void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, bool any_stale);
```


- [ ] **Step 7: XS weather, sun, air quality and pollen.** One line (`tiny_row()`), or the icon over the text (`tiny_stack()`); the icon goes before the text is cut, and digits alone are centred; today's weather is "23/13°", else its high over its low, else its high; the sun stacks its two times three ways, and drops a 12-hour suffix before a time is cut; pollen too long for its word shows its level bar.

`components/ui/ui_forecast.c`:

```diff
--- a/components/ui/ui_forecast.c
+++ b/components/ui/ui_forecast.c
@@ -421,8 +421,7 @@ bool ui_resolve_forecast(const ui_context_t *ctx, ui_field_id_t field, ui_value_
 
 const gfx_bitmap_t *ui_sky_icon(int sky, bool night, int size)
 {
-    bool big = size >= 48;
-#define ICON(name) (big ? &gfx_icon_##name##_48 : &gfx_icon_##name##_24)
+#define ICON(name) (size >= 48 ? &gfx_icon_##name##_48 : size >= 24 ? &gfx_icon_##name##_24 : &gfx_icon_##name##_16)
     switch (sky) {
     case WEATHER_SKY_CLEAR:
         return night ? ICON(wx_clear_night) : ICON(wx_clear_day);
@@ -510,9 +509,171 @@ static void level_bar(gfx_fb_t *fb, int x, int y, int count, int filled, int cel
     }
 }
 
+/* ---- XS (M6c, D34): the status bar's look ---- */
+
+/* An XS icon's size: 24 px from 34 px of height in one line, from 60 px stacked; 16 px otherwise. */
+static int tiny_icon(gfx_rect_t r)
+{
+    return r.h >= (ui_tiny_stacked(r) ? 60 : 34) ? 24 : 16;
+}
+
+/* The largest of bold 28, bold 20, bold 16 and sans 12 in which `text` fits max_w, its ink 2 px clear of either
+ * edge of max_h; with `cut`, sans 12 cut to max_w when none fits, else NULL. */
+static const gfx_font_t *tiny_face(const char *text, int max_w, int max_h, bool cut, char *fit, size_t size)
+{
+    static const gfx_font_t *const k_faces[] = { &gfx_font_sans_bold_28, &gfx_font_sans_bold_20,
+                                                 &gfx_font_sans_bold_16, &gfx_font_sans_12 };
+    for (size_t i = 0; i < sizeof(k_faces) / sizeof(k_faces[0]); i++) {
+        const gfx_font_t *f = k_faces[i];
+        if (gfx_text_width(f, text) <= max_w && ui_ink_above(f, text) + ui_ink_below(f, text) + 4 <= max_h) {
+            snprintf(fit, size, "%s", text);
+            return f;
+        }
+    }
+    if (!cut) {
+        return NULL;
+    }
+    gfx_text_ellipsize(&gfx_font_sans_12, text, max_w, fit, size);
+    return &gfx_font_sans_12;
+}
+
+/* The symbol over `first` in the largest face that fits, `second` in sans 12 under it where there is room; all
+ * centred, 2 px clear of either edge, and 2 px more for words, whose accents may overhang their advance. */
+static void tiny_stack(gfx_fb_t *fb, gfx_rect_t r, const gfx_bitmap_t *icon, const char *first, const char *second,
+                       bool digits)
+{
+    int cx = r.x + r.w / 2, max_w = digits ? r.w - 4 : r.w - 8;
+    int icon_h = icon != NULL ? icon->height + 4 : 0;
+    char fit[48];
+    const gfx_font_t *f = tiny_face(first, max_w, r.h - icon_h, true, fit, sizeof(fit));
+    int fh = ui_ink_above(f, fit) + ui_ink_below(f, fit);
+    const gfx_font_t *sf = &gfx_font_sans_12;
+    int sh = second != NULL ? ui_ink_above(sf, second) + ui_ink_below(sf, second) : 0;
+    bool two = second != NULL && second[0] && gfx_text_width(sf, second) <= max_w && icon_h + fh + 4 + sh + 4 <= r.h;
+    int block = icon_h + fh + (two ? 4 + sh : 0);
+    int top = r.y + (r.h - block) / 2;
+    if (icon != NULL) {
+        gfx_bitmap(fb, cx - icon->width / 2, top, icon, GFX_BLACK);
+    }
+    int y = top + icon_h;
+    gfx_text(fb, f, cx - gfx_text_width(f, fit) / 2, y + ui_ink_above(f, fit), fit, GFX_BLACK);
+    if (two) {
+        y += fh + 4;
+        gfx_text(fb, sf, cx - gfx_text_width(sf, second) / 2, y + ui_ink_above(sf, second), second, GFX_BLACK);
+    }
+}
+
+/* One line, like the status bar: the icon, `first` in the largest face the height takes, then `second` smaller on
+ * its baseline where it fits; with `icon2`, that icon and `third` after (the sun's rise, then its set). `first`
+ * alone, centred, where it would be cut beside the icon. Digits keep 2 px from the right edge, words 4, as an
+ * accent may overhang its advance. A narrow, tall cell stacks `first` and `second` under the icon instead. */
+static void tiny_row(gfx_fb_t *fb, gfx_rect_t r, const gfx_bitmap_t *icon, const char *first, const char *second,
+                     const gfx_bitmap_t *icon2, const char *third, bool digits)
+{
+    if (ui_tiny_stacked(r)) {
+        tiny_stack(fb, r, icon, first, second, digits);
+        return;
+    }
+    int cy = r.y + r.h / 2, x = r.x + 3, right = r.x + r.w - (digits ? 2 : 4), words = r.x + r.w - 4;
+    char fit[48];
+    const gfx_font_t *f = icon != NULL ? tiny_face(first, right - x - icon->width - 3, r.h, false, fit, sizeof(fit))
+                                       : NULL;
+    if (f != NULL) { /* beside the icon, or alone where it would be cut there */
+        gfx_bitmap(fb, x, cy - icon->height / 2, icon, GFX_BLACK);
+        x += icon->width + 3;
+    } else {
+        f = tiny_face(first, digits ? r.w - 4 : right - x, r.h, true, fit, sizeof(fit));
+        x = digits ? r.x + (r.w - gfx_text_width(f, fit)) / 2 : x;
+        icon2 = NULL;
+    }
+    int base = r.y + (r.h + ui_ink_above(f, fit) - ui_ink_below(f, fit)) / 2;
+    x = gfx_text(fb, f, x, base, fit, GFX_BLACK) + 5;
+    if (second != NULL && second[0]) {
+        const gfx_font_t *sf = f == &gfx_font_sans_bold_28 ? &gfx_font_sans_16 : &gfx_font_sans_12;
+        if (gfx_text_width(sf, second) <= words - x && base - ui_ink_above(sf, second) >= r.y + 2 &&
+            base + ui_ink_below(sf, second) <= r.y + r.h - 2) {
+            x = gfx_text(fb, sf, x, base, second, GFX_BLACK) + 6;
+        }
+    }
+    if (icon2 != NULL && third != NULL && x + icon2->width + 3 + gfx_text_width(f, third) <= right) {
+        gfx_bitmap(fb, x, cy - icon2->height / 2, icon2, GFX_BLACK);
+        gfx_text(fb, f, x + icon2->width + 3, base, third, GFX_BLACK);
+    }
+}
+
+/* Whether digits `text` fit a line of `r` uncut, beside `icon` or alone, as tiny_row() places them. */
+static bool tiny_line_fits(gfx_rect_t r, const gfx_bitmap_t *icon, const char *text)
+{
+    char fit[48];
+    int beside = r.w - 2 - 3 - (icon != NULL ? icon->width + 3 : 0);
+    return (icon != NULL && tiny_face(text, beside, r.h, false, fit, sizeof(fit)) != NULL) ||
+           tiny_face(text, r.w - 4, r.h, false, fit, sizeof(fit)) != NULL;
+}
+
+/* A 12-hour time without its suffix ("6:44" for "6:44 AM"), for a cell too narrow for it. */
+static void time_short(const char *text, char *out, size_t size)
+{
+    snprintf(out, size, "%s", text);
+    char *space = strrchr(out, ' ');
+    if (space != NULL) {
+        *space = '\0';
+    }
+}
+
+/* The sun's times stacked: a row each, its icon beside the time; where that is too wide, each icon over its time;
+ * where that is too tall, the two times alone, one over the other. */
+static void tiny_sun_stack(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
+{
+    const gfx_bitmap_t *icons[2] = { &gfx_icon_sunrise_16, &gfx_icon_sunset_16 };
+    char brief[2][16];
+    time_short(v->text, brief[0], sizeof(brief[0]));
+    time_short(v->extra, brief[1], sizeof(brief[1]));
+    static const gfx_font_t *const k_faces[] = { &gfx_font_sans_bold_16, &gfx_font_sans_12 };
+    for (int form = 0; form < 2; form++) { /* the times as they are, then without a 12-hour suffix */
+        const char *times[2] = { form ? brief[0] : v->text, form ? brief[1] : v->extra };
+        for (size_t i = 0; i < sizeof(k_faces) / sizeof(k_faces[0]); i++) { /* rows */
+            const gfx_font_t *f = k_faces[i];
+            int tw = gfx_text_width(f, times[0]) > gfx_text_width(f, times[1]) ? gfx_text_width(f, times[0])
+                                                                               : gfx_text_width(f, times[1]);
+            if (19 + tw <= r.w - 4 && 2 * 18 + 4 <= r.h) { /* digits: 2 px from the edges */
+                int top = r.y + (r.h - 2 * 18) / 2, x = r.x + (r.w - 19 - tw) / 2;
+                for (int k = 0; k < 2; k++) {
+                    gfx_bitmap(fb, x, top + k * 18 + 1, icons[k], GFX_BLACK);
+                    gfx_text(fb, f, x + 19, top + k * 18 + 9 + ink_height(f) / 2, times[k], GFX_BLACK);
+                }
+                return;
+            }
+        }
+    }
+    const gfx_font_t *f = &gfx_font_sans_12;
+    bool whole = gfx_text_width(f, v->text) <= r.w - 4 && gfx_text_width(f, v->extra) <= r.w - 4;
+    const char *times[2] = { whole ? v->text : brief[0], whole ? v->extra : brief[1] };
+    int th = ink_height(f) + 3; /* a time's ink, its tails included */
+    bool icons_fit = 2 * (16 + 2 + th) + 4 + 4 <= r.h;
+    int line = (icons_fit ? 16 + 2 : 0) + th;
+    int block = 2 * line + 4;
+    int top = r.y + (r.h - block) / 2, cx = r.x + r.w / 2;
+    char fit[24];
+    for (int k = 0; k < 2; k++) {
+        int y = top + k * (line + 4);
+        if (icons_fit) {
+            gfx_bitmap(fb, cx - 8, y, icons[k], GFX_BLACK);
+            y += 16 + 2;
+        }
+        gfx_text_ellipsize(f, times[k], r.w - 4, fit, sizeof(fit));
+        gfx_text(fb, f, cx - gfx_text_width(f, fit) / 2, y + ink_height(f), fit, GFX_BLACK);
+    }
+}
+
 static void draw_weather_now(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
 {
     const gfx_bitmap_t *icon = ui_sky_icon(v->sky, v->night, 48);
+    if (size == UI_SIZE_XS) {
+        char t[sizeof(v->text) + 2];
+        snprintf(t, sizeof(t), "%s" DEGREE, v->text);
+        tiny_row(fb, r, ui_sky_icon(v->sky, v->night, tiny_icon(r)), t, v->extra, NULL, NULL, true);
+        return;
+    }
     if (size == UI_SIZE_S) {
         if (r.w < 150) { /* narrow: the sky above, the temperature below */
             gfx_bitmap(fb, r.x + (r.w - 48) / 2, r.y + 8, icon, GFX_BLACK);
@@ -586,6 +747,27 @@ static void precip(gfx_fb_t *fb, int x, int baseline, int pct)
 
 static void draw_weather_day(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
 {
+    if (size == UI_SIZE_XS) { /* "23/13°"; where it doesn't fit, "23°" over "13°", or the high alone */
+        const gfx_bitmap_t *icon = ui_sky_icon(v->sky, false, tiny_icon(r));
+        char high[sizeof(v->short_text) + 2];
+        snprintf(high, sizeof(high), "%s", v->short_text);
+        char *slash = strchr(high, '/');
+        const char *low = slash != NULL ? slash + 1 : "";
+        if (slash != NULL) {
+            *slash = '\0';
+            snprintf(slash, sizeof(high) - (size_t)(slash - high), DEGREE);
+        }
+        char fit[48];
+        bool whole = ui_tiny_stacked(r)
+                         ? tiny_face(v->short_text, r.w - 4, r.h - icon->height - 4, false, fit, sizeof(fit)) != NULL
+                         : tiny_line_fits(r, icon, v->short_text);
+        if (whole || slash == NULL) {
+            tiny_row(fb, r, icon, v->short_text, NULL, NULL, NULL, true);
+        } else {
+            tiny_row(fb, r, icon, high, ui_tiny_stacked(r) ? v->short_text + (low - high) : NULL, NULL, NULL, true);
+        }
+        return;
+    }
     if (size == UI_SIZE_S && r.w < 150) {
         gfx_bitmap(fb, r.x + (r.w - 48) / 2, r.y + 8, ui_sky_icon(v->sky, false, 48), GFX_BLACK);
         centred(fb, &gfx_font_sans_bold_20, r, r.y + 8 + 48 + 8 + ink_height(&gfx_font_sans_bold_20), v->short_text);
@@ -728,6 +910,24 @@ static void draw_rain(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
 
 static void draw_sun(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
 {
+    if (size == UI_SIZE_XS) {
+        bool big = tiny_icon(r) == 24;
+        if (v->polar) {
+            tiny_row(fb, r, ui_sky_icon(WEATHER_SKY_CLEAR, v->polar != 1, tiny_icon(r)), v->text, NULL, NULL, NULL,
+                     false);
+        } else if (ui_tiny_stacked(r)) {
+            tiny_sun_stack(fb, r, v);
+        } else {
+            const gfx_bitmap_t *rise = big ? &gfx_icon_sunrise_24 : &gfx_icon_sunrise_16;
+            char brief[2][16];
+            time_short(v->text, brief[0], sizeof(brief[0]));
+            time_short(v->extra, brief[1], sizeof(brief[1]));
+            bool whole = tiny_line_fits(r, rise, v->text);
+            tiny_row(fb, r, rise, whole ? v->text : brief[0], NULL, big ? &gfx_icon_sunset_24 : &gfx_icon_sunset_16,
+                     whole ? v->extra : brief[1], true);
+        }
+        return;
+    }
     int top = r.y;
     if (size != UI_SIZE_S) {
         top += label_line(fb, r, size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16, v->label);
@@ -767,6 +967,13 @@ static void draw_level(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_valu
 {
     const gfx_bitmap_t *icon = v->field == UI_FIELD_AQ_UV ? &gfx_icon_uv_24 : &gfx_icon_air_24;
     int bands = v->bands > 0 ? v->bands : 6;
+    if (size == UI_SIZE_XS) {
+        bool big = tiny_icon(r) == 24;
+        const gfx_bitmap_t *xs_icon = v->field == UI_FIELD_AQ_UV ? (big ? &gfx_icon_uv_24 : &gfx_icon_uv_16)
+                                                                 : (big ? &gfx_icon_air_24 : &gfx_icon_air_16);
+        tiny_row(fb, r, xs_icon, v->text, v->extra, NULL, NULL, true);
+        return;
+    }
     if (size == UI_SIZE_S) {
         bool narrow = r.w < 150;
         int y = r.y + 10;
@@ -800,6 +1007,17 @@ static void draw_level(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_valu
 /* pollen.*: a level, and its type or count (D25). */
 static void draw_pollen(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
 {
+    if (size == UI_SIZE_XS) {
+        const gfx_bitmap_t *icon = tiny_icon(r) == 24 ? &gfx_icon_pollen_24 : &gfx_icon_pollen_16;
+        if (ui_tiny_stacked(r) && gfx_text_width(&gfx_font_sans_12, v->text) > r.w - 8) { /* its level bar */
+            int top = r.y + (r.h - icon->height - 4 - 6) / 2;
+            gfx_bitmap(fb, r.x + (r.w - icon->width) / 2, top, icon, GFX_BLACK);
+            level_bar(fb, r.x + (r.w - (3 * 10 + 2 * 3)) / 2, top + icon->height + 4, 3, v->percent, 10, 6);
+            return;
+        }
+        tiny_row(fb, r, icon, v->text, v->extra[0] ? v->extra : v->label, NULL, NULL, false);
+        return;
+    }
     if (size == UI_SIZE_S) {
         int y = r.y + 10;
         if (r.w < 150) {
```


- [ ] **Step 8: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_lang && ./build-host/test_ui_split && ./build-host/test_ui_catalog && ./build-host/test_ui_widget_fit && ctest --test-dir build-host`
Expected: `8 Tests 0 Failures`, `14 Tests 0 Failures`, `4 Tests 0 Failures`, `12 Tests 0 Failures`; `100% tests passed, 0 tests failed out of 61`.

- [ ] **Step 9: The firmware builds.** `tools/idf.sh build`: clean, without a warning. The app grows by 7 KB, from 0x25ce40 to 0x25ea70 bytes (41 % of its partition free).

- [ ] **Step 10: Commit.**

```bash
git add assets/icons components/gfx components/locale components/ui test/host/test_lang.c \
  test/host/test_ui_split.c test/host/test_ui_catalog.c test/host/test_ui_widget_fit.c
git commit -m "feat(ui): the XS size, drawn like the status bar"
```

### Task 2: S in short cells (`ui`)

**Files:**
- Modify: `components/ui/ui_split.c` (S's heights), `components/ui/include/ui_split.h` (a comment), `components/ui/ui_widget.c` (`draw_small()` and `draw_small_beside()`, the age mark, XS's bolt), `components/ui/ui_forecast.c` (the weather's compact forms, air quality's and pollen's rows, the sun's polar words, today's weather in smaller faces)
- Test: `test/host/test_ui_split.c`, `test/host/test_ui_catalog.c`, `test/host/test_ui_widget_fit.c`

**Interfaces:**
- Consumes: `UI_SIZE_XS`, `ui_tiny_stacked()`, `ui_ink_above()`, `ui_ink_below()` (Task 1); `ui_split_two_lines()` (ui_internal.h); `has_ellipsis()`, `never_cut()`, `first_ink()`, `next_ink()` in the fit test (Task 1).
- Produces (Tasks 3–4): S from 40 px tall whatever the width; `k_needs` keeps only the sun's 49 and pollen's 42 at S (narrow and wide alike), M's rows unchanged; no age mark in an S cell under 80 px; the fit test's fifteen sets of data (`VARIANTS`) and `stale_mark()`.

The research's measurement (the plan's header) is the table this task writes into `k_needs`; the fit test checks it on a grid of S cells under 80 px tall, and every reachable cell (Task 3 widens that set). Seven new sets of data join the fit test: a thunderstorm by day with rain today, snow showers at night in Czech with the battery charging at 100 %, a Czech public holiday, a polar night, a polar day in Czech, a Wednesday on a 12-hour clock, and every value at the top of its scale (an air quality index of 250, PM at 255 µg/m³, 6 500 grains of pollen, 123 days of battery, 100 % humidity at −23.5 °C). They also find two things in Task 1's XS, fixed here: a charging battery's bolt that overflows a 40 px cell, and polar words cut at 40×20.

- [ ] **Step 1: Write the failing tests.** S's heights, and the published rules:

`test/host/test_ui_split.c`:

```diff
--- a/test/host/test_ui_split.c
+++ b/test/host/test_ui_split.c
@@ -138,8 +138,8 @@ static void test_a_bad_node_is_refused(void)
     TEST_ASSERT_FALSE(lay(k_flag, sizeof(k_flag)));
 }
 
-/* XL 400 wide and at least 120 tall; L at least 200×150; M at least 130×80; S wide from 150 px with 40 px of
- * height, narrower with 80; XS from 40×20 (spec §5.2, D34). */
+/* XL 400 wide and at least 120 tall; L at least 200×150; M at least 130×80; S at least 90×40; XS from 40×20
+ * (spec §5.2, D34). */
 static void test_a_cells_size_follows_its_dimensions(void)
 {
     TEST_ASSERT_EQUAL_INT(UI_SIZE_XL, ui_split_cell_size(400, 120));
@@ -153,8 +153,9 @@ static void test_a_cells_size_follows_its_dimensions(void)
     TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(150, 40));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(149, 80));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(150, 79));
-    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_cell_size(149, 79)); /* narrow S needs 80 */
-    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_cell_size(90, 40));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(149, 79)); /* M6c: the icon beside the value */
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(90, 40));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_cell_size(89, 279));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_cell_size(150, 39));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_cell_size(40, 20));
     TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(39, 279));
@@ -196,12 +197,17 @@ static void test_a_field_draws_at_the_largest_size_with_room_for_it(void)
     TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_SERIES, 199, 69));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_POLLEN, 199, 104)); /* M pollen needs 105 */
     TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_POLLEN, 199, 105));
-    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_DAY, 199, 69)); /* wide S needs 51 */
-    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_field_size(UI_FK_WEATHER_DAY, 129, 98)); /* narrow S needs 99 */
-    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_DAY, 129, 99));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_DAY, 199, 69)); /* M6c: S's own 40 px */
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_DAY, 90, 40));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_NOW, 133, 69)); /* the owner's 132×69 */
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_NOW, 90, 40));  /* narrow or wide */
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_SUN, 90, 49));          /* the sun needs 49 */
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_field_size(UI_FK_SUN, 400, 48));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_POLLEN, 90, 42));       /* pollen 42 */
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_field_size(UI_FK_POLLEN, 400, 41));
     TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_WEATHER_DAY, 149, 94)); /* narrow M needs 94 */
-    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_field_size(UI_FK_NUMBER, 99, 69));
-    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_NUMBER, 99, 80));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_NUMBER, 99, 69));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_NUMBER, 90, 40));
 }
 
 /* A larger cell never takes a field that a smaller one inside it takes: Join keeps a field. */
```


`test/host/test_ui_catalog.c`:

```diff
--- a/test/host/test_ui_catalog.c
+++ b/test/host/test_ui_catalog.c
@@ -136,11 +136,14 @@ static void test_layouts_publish_the_split_rules(void)
     TEST_ASSERT_EQUAL_INT(400, num(xl, "min_w"));
     TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(xl, "kinds"))); /* time, number */
     const cJSON *s_min = cJSON_GetObjectItemCaseSensitive(s, "min_h");
-    TEST_ASSERT_EQUAL_INT(80, cJSON_GetArrayItem(s_min, 0)->valueint); /* narrow, then wide */
+    TEST_ASSERT_EQUAL_INT(40, cJSON_GetArrayItem(s_min, 0)->valueint); /* narrow, then wide (M6c: the same) */
     TEST_ASSERT_EQUAL_INT(40, cJSON_GetArrayItem(s_min, 1)->valueint);
     const cJSON *wx = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(s, "kinds"), "weather_now");
-    TEST_ASSERT_EQUAL_INT(86, cJSON_GetArrayItem(wx, 0)->valueint);
-    TEST_ASSERT_EQUAL_INT(61, cJSON_GetArrayItem(wx, 1)->valueint);
+    TEST_ASSERT_EQUAL_INT(40, cJSON_GetArrayItem(wx, 0)->valueint); /* M6c: the sky beside the temperature */
+    TEST_ASSERT_EQUAL_INT(40, cJSON_GetArrayItem(wx, 1)->valueint);
+    const cJSON *sun = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(s, "kinds"), "sun");
+    TEST_ASSERT_EQUAL_INT(49, cJSON_GetArrayItem(sun, 0)->valueint);
+    TEST_ASSERT_EQUAL_INT(49, cJSON_GetArrayItem(sun, 1)->valueint);
     TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(s, "kinds"), "series"));
     /* the rules give what the renderer does, everywhere */
     static const char *const k_kinds[UI_FK_COUNT] = { "time", "date", "number", "battery", "moon", "text",
```


Every small field in short S cells, in the fifteen sets of data: it shows, stays clear of the edges, a number, a time, the battery, today's weather or the sun's times uncut, and no age mark (the status bar's warning stands for it under 80 px, as in XS); the date's and the Moon's short forms before a cut; the stale mark still in Classic's small slots:

`test/host/test_ui_widget_fit.c`:

```diff
--- a/test/host/test_ui_widget_fit.c
+++ b/test/host/test_ui_widget_fit.c
@@ -6,6 +6,7 @@
 #include "dashboard_fixtures.h"
 #include "gfx.h"
 #include "gfx_fonts.h"
+#include "gfx_icons.h"
 #include "ui_layout.h"
 #include "ui_split.h"
 #include "unity.h"
@@ -196,7 +197,13 @@ static void test_a_number_keeps_its_size_as_its_digits_change(void)
 /* The data a split cell's field is drawn from: 0 fresh in English, 1 fresh in Czech (its decimal
  * comma and longer words), 2 three hours old in Czech with a forecast two days old, 3 nothing yet,
  * 4 a 12-hour clock, 5 a hot day in °F (100.8 °F inside, 102 °F out), 6 frost in Czech (-12,5 °C
- * inside, -13 °C out, a dew point of -18,7 °C), 7 the clock with its seconds. */
+ * inside, -13 °C out, a dew point of -18,7 °C), 7 the clock with its seconds; from M6c 8 a thunderstorm
+ * by day and rain today, 9 snow showers at night in Czech and a battery charging at 100 %, 10 Monday
+ * 28 September in Czech, a public holiday, 11 a polar night (89.9° N), 12 a polar day (89.9° S) in Czech,
+ * 13 Wednesday 30 September on a 12-hour clock, 14 the largest values (-23.5 °C at 100 %, 123 days of battery, an
+ * air quality index of 250, a UV index of 13, PM at 255 µg/m³, pollen at 6 500 grains/m³). */
+#define VARIANTS 15
+
 static ui_context_t split_context(int variant)
 {
     ui_context_t ctx = fixture_context();
@@ -219,6 +226,48 @@ static ui_context_t split_context(int variant)
     if (variant == 5 || variant == 6) {
         fixture_forecast_shift(&s_fix_ds, variant == 5 ? 388 : -125);
     }
+    if (variant == 8 || variant == 9) { /* other skies: their icons differ in size */
+        static ds_weather_t w;
+        w = *ds_weather(&s_fix_ds);
+        w.now_code = variant == 8 ? 95 : 86;
+        w.now_is_day = variant == 8;
+        w.days[0].code = variant == 8 ? 63 : 75;
+        ds_set_weather(&s_fix_ds, &w);
+    }
+    if (variant == 9) {
+        ctx.lang = lang_get("cs");
+        ds_set_battery(&s_fix_ds, 100, 4180, DS_BAT_CHARGING, FIX_NOW);
+    }
+    if (variant == 10 || variant == 13) { /* another day: a holiday, a long weekday; the forecast doesn't cover it */
+        int days = variant == 10 ? 3 : 5;
+        ctx.lang = lang_get(variant == 10 ? "cs" : "en");
+        ctx.clock_24h = variant != 13;
+        ctx.now = FIX_NOW + days * 86400;
+        ctx.local_day = FIX_DAY + days;
+        ctx.local.tm_mday += days;
+        ctx.local.tm_wday = (ctx.local.tm_wday + days) % 7;
+        ctx.local.tm_yday += days;
+        fixture_fill(&s_fix_ds, ctx.now);
+    }
+    if (variant == 14) { /* the largest values: -23.5 °C at 100 %, 123 days of battery, the air at its worst */
+        ds_set_env(&s_fix_ds, -2350, 10000, FIX_NOW, FIX_DAY);
+        ds_set(&s_fix_ds, DS_BAT_DAYS, 1234, FIX_NOW);
+        static ds_air_t a;
+        a = *ds_air(&s_fix_ds);
+        for (int i = 0; i < DS_WX_HOURS; i++) {
+            a.aqi[i] = 250, a.pm25[i] = 255, a.pm10[i] = 255, a.uv10[i] = 125;
+        }
+        for (int d = 0; d < DS_WX_DAYS; d++) {
+            for (int t = 0; t < DS_POLLEN_TYPES; t++) {
+                a.pollen[d][t] = 65000;
+            }
+        }
+        ds_set_air(&s_fix_ds, &a);
+    }
+    if (variant == 11 || variant == 12) { /* the sun neither rises nor sets */
+        ctx.lang = lang_get(variant == 12 ? "cs" : "en");
+        ctx.lat_e4 = variant == 11 ? 899000 : -899000;
+    }
     return ctx;
 }
 
@@ -267,7 +316,7 @@ static void test_every_field_fits_every_cell_a_split_can_make(void)
     s_cell_count = 0;
     reach(400, 279, 0);
     TEST_ASSERT_EQUAL_INT(336, s_cell_count);
-    for (int variant = 0; variant < 8; variant++) {
+    for (int variant = 0; variant < VARIANTS; variant++) {
         ui_context_t ctx = split_context(variant);
         for (int i = 0; i < s_cell_count; i++) {
             int w = s_cells[i].w, h = s_cells[i].h;
@@ -371,6 +420,38 @@ static bool has_ellipsis(gfx_rect_t r)
     return false;
 }
 
+static bool icon_ink(const gfx_bitmap_t *icon, int x, int y)
+{
+    return (icon->bits[y * ((icon->width + 7) / 8) + x / 8] >> (7 - x % 8)) & 1;
+}
+
+/* Whether `r` shows the stale mark where draw_age() puts it, in the cell's bottom 26 rows: its icon's 16×16 box,
+ * ink and blank alike (a solid bar holds every pattern's ink). */
+static bool stale_mark(gfx_rect_t r)
+{
+    const gfx_bitmap_t *icon = &gfx_icon_stale_16;
+    int fx, fy;
+    first_ink(icon->bits, icon->width, icon->height, &fx, &fy);
+    gfx_rect_t bottom = { r.x, (int16_t)(r.h > 26 ? r.y + r.h - 26 : r.y), r.w, 0 };
+    bottom.h = (int16_t)(r.y + r.h - bottom.y);
+    for (int x = bottom.x, y = bottom.y; next_ink(bottom, &x, &y); x++) {
+        int x0 = x - fx, y0 = y - fy;
+        if (x0 < r.x || y0 < bottom.y || x0 + icon->width > r.x + r.w || y0 + icon->height > r.y + r.h) {
+            continue;
+        }
+        bool same = true;
+        for (int iy = 0; iy < icon->height && same; iy++) {
+            for (int ix = 0; ix < icon->width && same; ix++) {
+                same = gfx_get_pixel(&s_fb, x0 + ix, y0 + iy) == icon_ink(icon, ix, iy);
+            }
+        }
+        if (same) {
+            return true;
+        }
+    }
+    return false;
+}
+
 /* The kinds whose value must never be cut: a number, a time, today's high and low, the sun's times. */
 static bool never_cut(ui_field_kind_t kind)
 {
@@ -393,6 +474,7 @@ static void check_xs_cell(const ui_context_t *ctx, int w, int h, int variant)
         snprintf(msg, sizeof(msg), "%s at %d×%d (XS), variant %d", info->id, w, h, variant);
         TEST_ASSERT_TRUE_MESSAGE(inked(r.x + 2, r.x + w - 3, r.y + 2, r.y + h - 3), msg);
         TEST_ASSERT_FALSE_MESSAGE(never_cut(info->kind) && has_ellipsis(r), msg);
+        TEST_ASSERT_FALSE_MESSAGE(stale_mark(r), msg);
         TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y, r.y + 1), msg);
         TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y + h - 2, r.y + h - 1), msg);
         TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + 1, r.y, r.y + h - 1), msg);
@@ -403,7 +485,7 @@ static void check_xs_cell(const ui_context_t *ctx, int w, int h, int variant)
 /* Every small field shows in every XS cell and stays clear of its edges, in every set of data. */
 static void test_every_small_field_fits_every_xs_cell(void)
 {
-    for (int variant = 0; variant < 8; variant++) {
+    for (int variant = 0; variant < VARIANTS; variant++) {
         ui_context_t ctx = split_context(variant);
         for (size_t i = 0; i < sizeof(k_xs_w) / sizeof(k_xs_w[0]); i++) {
             for (size_t j = 0; j < sizeof(k_xs_h) / sizeof(k_xs_h[0]); j++) {
@@ -418,6 +500,52 @@ static void test_every_small_field_fits_every_xs_cell(void)
     }
 }
 
+/* S cells under 80 px tall (D34): the icon beside the value, narrow or wide, at every height S takes. */
+static const int16_t k_s_w[] = { 90,  91,  100, 110, 119, 120, 129, 133, 140,
+                                 149, 150, 151, 160, 199, 200, 266, 399, 400 };
+static const int16_t k_s_h[] = { 40, 41, 42, 44, 46, 48, 49, 50, 51, 53, 55, 56, 59, 60, 61, 65, 69, 70, 75, 79 };
+
+static void check_s_cell(const ui_context_t *ctx, int w, int h, int variant)
+{
+    for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
+        const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
+        if (ui_split_field_size(info->kind, w, h) != UI_SIZE_S) {
+            continue;
+        }
+        gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
+        gfx_fb_init(&s_fb, s_buf, 400, 300);
+        gfx_clear(&s_fb, GFX_WHITE);
+        ui_draw_cell(&s_fb, r, ctx, (ui_field_id_t)f, UI_STALE_STALE);
+        char msg[80];
+        snprintf(msg, sizeof(msg), "%s at %d×%d (S), variant %d", info->id, w, h, variant);
+        TEST_ASSERT_TRUE_MESSAGE(inked(r.x + 2, r.x + w - 3, r.y + 2, r.y + h - 3), msg);
+        TEST_ASSERT_FALSE_MESSAGE(never_cut(info->kind) && has_ellipsis(r), msg);
+        TEST_ASSERT_FALSE_MESSAGE(stale_mark(r), msg); /* under 80 px the status bar's warning stands for it */
+        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y, r.y + 1), msg);
+        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y + h - 2, r.y + h - 1), msg);
+        TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + 1, r.y, r.y + h - 1), msg);
+        TEST_ASSERT_FALSE_MESSAGE(inked(r.x + w - 2, r.x + w - 1, r.y, r.y + h - 1), msg);
+    }
+}
+
+/* Every small field that draws at S in a short cell shows, clear of the cell's edges, in every set of data; a
+ * field draws at S from the heights spec §5.2 gives (M6c). */
+static void test_every_small_field_fits_every_short_s_cell(void)
+{
+    for (int variant = 0; variant < VARIANTS; variant++) {
+        ui_context_t ctx = split_context(variant);
+        for (size_t i = 0; i < sizeof(k_s_w) / sizeof(k_s_w[0]); i++) {
+            for (size_t j = 0; j < sizeof(k_s_h) / sizeof(k_s_h[0]); j++) {
+                check_s_cell(&ctx, k_s_w[i], k_s_h[j], variant);
+            }
+        }
+    }
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_NOW, 90, 40)); /* the heights measured */
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_LEVEL, 90, 40));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_SUN, 90, 49));
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_POLLEN, 90, 42));
+}
+
 /* Runs of inked rows in `r`: the lines a widget drew, one above the other. */
 static int ink_bands(gfx_rect_t r)
 {
@@ -500,6 +628,35 @@ static void test_xs_keeps_the_marks_s_shows(void)
     }
 }
 
+/* A short S cell shows the date's weekday and day, or the Moon's short name, before it cuts a longer form; a
+ * stale value shows its mark where there is room for it, as in Classic's small slots (D34). */
+static void test_short_s_cells_show_a_short_form_before_cutting(void)
+{
+    static const int16_t k_cells[][2] = { { 100, 69 }, { 125, 69 }, { 133, 45 }, { 99, 41 } };
+    for (int variant = 0; variant < VARIANTS; variant++) {
+        ui_context_t ctx = split_context(variant);
+        for (size_t i = 0; i < sizeof(k_cells) / sizeof(k_cells[0]); i++) {
+            int16_t w = k_cells[i][0], h = k_cells[i][1];
+            gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), w, h };
+            static const ui_field_id_t k_fields[] = { UI_FIELD_DATE_DAY, UI_FIELD_MOON_PHASE };
+            for (size_t f = 0; f < 2; f++) {
+                gfx_fb_init(&s_fb, s_buf, 400, 300);
+                gfx_clear(&s_fb, GFX_WHITE);
+                ui_draw_cell(&s_fb, r, &ctx, k_fields[f], UI_STALE_STALE);
+                char msg[64];
+                snprintf(msg, sizeof(msg), "field %d at %d×%d, variant %d", (int)k_fields[f], r.w, r.h, variant);
+                TEST_ASSERT_FALSE_MESSAGE(has_ellipsis(r), msg);
+            }
+        }
+    }
+    ui_context_t ctx = split_context(2); /* three hours old */
+    gfx_rect_t r = { 300, 189, 100, 111 };
+    gfx_fb_init(&s_fb, s_buf, 400, 300);
+    gfx_clear(&s_fb, GFX_WHITE);
+    ui_draw_cell(&s_fb, r, &ctx, UI_FIELD_ENV_TEMP, UI_STALE_STALE);
+    TEST_ASSERT_TRUE(stale_mark(r));
+}
+
 /* A field with no room in its cell isn't drawn at all, rather than cut. */
 static void test_a_field_without_room_draws_nothing(void)
 {
@@ -525,5 +682,7 @@ int main(void)
     RUN_TEST(test_every_small_field_fits_every_xs_cell);
     RUN_TEST(test_xs_draws_one_line_or_the_symbol_over_the_value);
     RUN_TEST(test_xs_keeps_the_marks_s_shows);
+    RUN_TEST(test_every_small_field_fits_every_short_s_cell);
+    RUN_TEST(test_short_s_cells_show_a_short_form_before_cutting);
     return UNITY_END();
 }
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host && for t in test_ui_split test_ui_catalog test_ui_widget_fit; do ./build-host/$t | grep -E 'FAIL|Tests'; done`
Expected (Unity prints "×" as `\xC3\x97`):

```
…/test/host/test_ui_split.c:156:test_a_cells_size_follows_its_dimensions:FAIL: Expected 1 Was 0
…/test/host/test_ui_split.c:201:test_a_field_draws_at_the_largest_size_with_room_for_it:FAIL: Expected 1 Was 0
14 Tests 2 Failures 0 Ignored
FAIL
…/test/host/test_ui_catalog.c:139:test_layouts_publish_the_split_rules:FAIL: Expected 40 Was 80
4 Tests 1 Failures 0 Ignored
FAIL
…/test/host/test_ui_widget_fit.c:337:test_every_field_fits_every_cell_a_split_can_make:FAIL: date.holiday at 100\xC3\x9781 (size 1), variant 10
…/test/host/test_ui_widget_fit.c:480:test_every_small_field_fits_every_xs_cell:FAIL: bat.level at 40\xC3\x9744 (XS), variant 9
…/test/host/test_ui_widget_fit.c:523:test_every_small_field_fits_every_short_s_cell:FAIL: env.hum at 150\xC3\x9740 (S), variant 2
14 Tests 3 Failures 0 Ignored
FAIL
```

The holiday's name on two lines runs into an 81 px cell's bottom; the charging bolt beside the 24 px outline is wider than a 40 px cell; a stale humidity draws its age mark in a 40 px cell. `test_short_s_cells_show_a_short_form_before_cutting` passes here, as Task 1 still stacks every narrow S cell: it guards the side-by-side form this task brings.

- [ ] **Step 3: S's heights.** Narrow S cells take 40 px as wide ones do; only the sun and pollen need more:

`components/ui/ui_split.c`:

```diff
--- a/components/ui/ui_split.c
+++ b/components/ui/ui_split.c
@@ -20,7 +20,7 @@ static const struct {
     uint32_t kinds;
 } k_sizes[] = {
     [UI_SIZE_XS] = { 40, 20, 20, UI_KINDS_XS },
-    [UI_SIZE_S] = { 90, 80, 40, UI_KINDS_S },
+    [UI_SIZE_S] = { 90, 40, 40, UI_KINDS_S }, /* M6c: under 150×80 the icon goes beside the value */
     [UI_SIZE_M] = { 130, 80, 80, UI_KINDS_M },
     [UI_SIZE_L] = { 200, 150, 150, UI_KINDS_L },
     [UI_SIZE_XL] = { 400, 120, 120, UI_KINDS_XL },
@@ -34,11 +34,8 @@ static const struct {
     uint8_t size, kind;
     uint8_t narrow_h, wide_h;
 } k_needs[] = {
-    { UI_SIZE_S, UI_FK_WEATHER_NOW, 86, 61 },
-    { UI_SIZE_S, UI_FK_WEATHER_DAY, 99, 51 },
-    { UI_SIZE_S, UI_FK_SUN, 80, 49 },
-    { UI_SIZE_S, UI_FK_LEVEL, 83, 40 },
-    { UI_SIZE_S, UI_FK_POLLEN, 86, 42 },
+    { UI_SIZE_S, UI_FK_SUN, 49, 49 }, /* M6c: S's other kinds fit its own 40 px, the sky beside the value */
+    { UI_SIZE_S, UI_FK_POLLEN, 42, 42 },
     { UI_SIZE_M, UI_FK_WEATHER_NOW, 98, 80 },
     { UI_SIZE_M, UI_FK_WEATHER_DAY, 94, 80 },
     { UI_SIZE_M, UI_FK_SUN, 94, 94 },
```


`components/ui/include/ui_split.h`:

```diff
--- a/components/ui/include/ui_split.h
+++ b/components/ui/include/ui_split.h
@@ -18,7 +18,7 @@
 #define UI_SPLIT_NODES (2 * UI_SPLIT_CELLS - 1)
 #define UI_SPLIT_MIN_W 90     /* no part is smaller (spec §5.2) */
 #define UI_SPLIT_MIN_H 40
-#define UI_SPLIT_NARROW_W 150 /* a narrower cell stacks a field's symbol above its value */
+#define UI_SPLIT_NARROW_W 150 /* narrower: a kind's narrow height; S stacks from 80 px tall (D34) */
 #define UI_SPLIT_INSET 8      /* a separator stops this short of each end */
 
 typedef enum {
```


- [ ] **Step 4: The symbol beside the value in a short or wide cell** (`draw_small_beside()`). Nothing is cut before the rest gives way: words take their shorter forms (the date's "Fri 25"; the Moon's short name, then its illumination), then leave their symbol behind (the Moon keeps its disc); a number gives up the battery's bolt, its trend arrow, its unit (a time its clock first, as AM/PM says more), then its symbol, and stands alone, centred. A name stacks on two lines only from 86 px, where its tails clear the cell's bottom (M6b's minor at 81–82 px). An S cell under 80 px draws no age mark. In XS, the bolt sits 2 px from the battery by its ink, and a stacked battery whose bolt doesn't fit beside the 24 px outline takes the 16 px one.

`components/ui/ui_widget.c`:

```diff
--- a/components/ui/ui_widget.c
+++ b/components/ui/ui_widget.c
@@ -257,13 +257,14 @@ static void draw_min_max_mark(gfx_fb_t *fb, const ui_value_t *v, int x, int y)
     }
 }
 
-/* The small visual that stands for the field: an icon, a battery, or the Moon. */
-static int draw_symbol(gfx_fb_t *fb, const ui_value_t *v, int x, int y, int size)
+/* The small visual that stands for the field: an icon, a battery (its bolt while it charges, with `bolt`), or the
+ * Moon. Returns its width. */
+static int draw_symbol(gfx_fb_t *fb, const ui_value_t *v, int x, int y, int size, bool bolt)
 {
     if (v->kind == UI_FK_BATTERY) {
         int w = size * 3 / 2, h = size * 3 / 4;
         ui_draw_battery(fb, x, y + (size - h) / 2, w, h, v->state == UI_VALUE_MISSING ? -1 : v->percent);
-        if (v->battery == DS_BAT_CHARGING) {
+        if (bolt && v->battery == DS_BAT_CHARGING) {
             gfx_bitmap(fb, x + w + 2, y + (size - 16) / 2, &gfx_icon_bolt_16, GFX_BLACK);
             return w + 18;
         }
@@ -287,6 +288,19 @@ static int draw_symbol(gfx_fb_t *fb, const ui_value_t *v, int x, int y, int size
     return icon->width;
 }
 
+/* The width draw_symbol() takes at `size` px. */
+static int symbol_width(const ui_value_t *v, int size, bool bolt)
+{
+    if (v->kind == UI_FK_BATTERY) {
+        return size * 3 / 2 + (bolt && v->battery == DS_BAT_CHARGING ? 18 : 0);
+    }
+    if (v->kind == UI_FK_MOON) {
+        return size;
+    }
+    const gfx_bitmap_t *icon = field_icon(v->field, size);
+    return icon != NULL ? icon->width : 0;
+}
+
 /* The value as text: the number for most kinds, a word or name otherwise. */
 static const char *display_text(const ui_value_t *v, ui_size_t size)
 {
@@ -309,6 +323,83 @@ static bool numeric(const ui_value_t *v)
            (v->kind == UI_FK_NUMBER || v->kind == UI_FK_TIME || v->kind == UI_FK_BATTERY);
 }
 
+/* S beside the value, from 150 px of width or under 80 px of height: the symbol, then the value. Nothing is cut
+ * before the rest gives way: words take their shorter forms (the date's "Fri 25"; the Moon's short name, then its
+ * illumination), then leave their symbol behind (the Moon keeps its disc); a number gives up the battery's bolt,
+ * its trend arrow, its unit (a time its clock first, as AM/PM says more), then its symbol, and stands alone,
+ * centred. */
+static void draw_small_beside(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
+{
+    const ui_fonts_t *f = &k_fonts[UI_SIZE_S];
+    int pad = r.w < 150 ? 6 : 14, gap = r.w < 150 ? 6 : 10;
+    int sym_y = r.y + (r.h - f->icon) / 2;
+    char fit[48];
+    if (!numeric(v)) {
+        const gfx_font_t *vf = v->state == UI_VALUE_MISSING ? f->value : f->text;
+        const char *forms[3] = { display_text(v, UI_SIZE_S), "", "" };
+        if (v->state != UI_VALUE_MISSING && v->kind == UI_FK_DATE) {
+            forms[1] = v->short_text;
+        } else if (v->state != UI_VALUE_MISSING && v->kind == UI_FK_MOON) {
+            forms[1] = v->short_text, forms[2] = v->extra;
+        }
+        ui_value_t shown = *v;
+        shown.unit[0] = '\0';
+        shown.trend = 0;
+        int baseline = r.y + (r.h + digit_height(vf)) / 2;
+        for (int with = 1; with >= 0; with--) {
+            int x = with ? r.x + pad + symbol_width(v, f->icon, true) + gap : r.x + 6;
+            for (int k = 0; k < 3; k++) {
+                int w = gfx_text_width(vf, forms[k]);
+                if (forms[k][0] && w <= r.x + r.w - 6 - x) {
+                    if (with) {
+                        draw_symbol(fb, v, r.x + pad, sym_y, f->icon, true);
+                    }
+                    draw_group(fb, f, vf, &shown, forms[k], with ? x : r.x + (r.w - w) / 2, baseline);
+                    return;
+                }
+            }
+            if (v->kind == UI_FK_MOON && v->state != UI_VALUE_MISSING) {
+                break; /* the disc is the phase */
+            }
+        }
+        int x = r.x + pad + draw_symbol(fb, v, r.x + pad, sym_y, f->icon, true) + gap;
+        gfx_text_ellipsize(vf, forms[0], r.x + r.w - 6 - x, fit, sizeof(fit));
+        draw_group(fb, f, vf, &shown, fit, x, baseline);
+        return;
+    }
+    static const struct {
+        bool sym, bolt, trend, unit;
+    } k_tries[] = {
+        { true, true, true, true },    { true, false, true, true },   { true, false, false, true },
+        { true, false, false, false }, { false, false, false, true }, { false, false, false, false },
+    };
+    size_t count = sizeof(k_tries) / sizeof(k_tries[0]);
+    for (size_t i = 0; i < count; i++) {
+        if (v->kind == UI_FK_TIME && k_tries[i].sym && !k_tries[i].unit) {
+            continue;
+        }
+        ui_value_t shown = *v;
+        shown.trend = k_tries[i].trend ? v->trend : 0;
+        if (!k_tries[i].unit) {
+            shown.unit[0] = '\0';
+        }
+        int x = r.x + pad + (k_tries[i].sym ? symbol_width(v, f->icon, k_tries[i].bolt) : 0) + gap;
+        int max_w = k_tries[i].sym ? r.x + r.w - 6 - x : r.w - 12;
+        const char *value = v->text;
+        const gfx_font_t *vf = fit_number(f, k_fit_s, 3, &shown, &value, max_w, 0, 0, fit, sizeof(fit));
+        if (value == fit && i + 1 < count) {
+            continue; /* cut: give up something else first */
+        }
+        if (k_tries[i].sym) {
+            draw_symbol(fb, v, r.x + pad, sym_y, f->icon, k_tries[i].bolt);
+        } else {
+            x = r.x + (r.w - group_width(f, vf, &shown, value)) / 2;
+        }
+        draw_group(fb, f, vf, &shown, value, x, r.y + (r.h + digit_height(vf)) / 2);
+        return;
+    }
+}
+
 static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
 {
     const ui_fonts_t *f = &k_fonts[UI_SIZE_S];
@@ -320,11 +411,13 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
         shown.trend = 0;
     }
     char fit[48];
-    if (r.w < 150) { /* narrow: symbol above, value below, the trend arrow beside the symbol */
+    /* a name on two lines ends 84 px down: it stacks from 86 px, its tails 2 px clear */
+    bool two_lines = !numeric(v) && v->kind != UI_FK_MOON && gfx_text_width(vf, value) > r.w - 8;
+    if (r.w < 150 && r.h >= (two_lines ? 86 : 80)) { /* narrow and tall: symbol above, value below, the arrow beside */
         int sym_size = v->kind == UI_FK_MOON ? 28 : f->icon;
         int sym_w = v->kind == UI_FK_BATTERY ? sym_size * 3 / 2 : sym_size;
         int sym_x = r.x + (r.w - sym_w) / 2;
-        draw_symbol(fb, v, sym_x, r.y + 12, sym_size);
+        draw_symbol(fb, v, sym_x, r.y + 12, sym_size, true);
         if (shown.trend) {
             gfx_text(fb, &gfx_font_sans_bold_16, sym_x + sym_w + 4, r.y + 12 + sym_size - 4,
                      shown.trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
@@ -338,7 +431,7 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
             return;
         }
         int max_w = r.w - 8;
-        if (!numeric(v) && gfx_text_width(vf, value) > max_w) { /* a name: two lines in the regular face */
+        if (two_lines) { /* a name: two lines in the regular face */
             const gfx_font_t *tf = f->unit;
             char second[sizeof(fit)];
             ui_split_two_lines(tf, value, max_w, fit, second, sizeof(fit));
@@ -360,15 +453,7 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
         draw_group(fb, f, vf, &shown, value, r.x + (r.w - w) / 2, baseline);
         return;
     }
-    int sym_w = draw_symbol(fb, v, r.x + 14, r.y + (r.h - f->icon) / 2, f->icon); /* wide: side by side */
-    int x = r.x + 14 + sym_w + 10;
-    if (!numeric(v)) {
-        gfx_text_ellipsize(vf, value, r.x + r.w - 6 - x, fit, sizeof(fit));
-        value = fit;
-    } else {
-        vf = fit_number(f, k_fit_s, 3, &shown, &value, r.x + r.w - 6 - x, 0, 0, fit, sizeof(fit));
-    }
-    draw_group(fb, f, vf, &shown, value, x, r.y + (r.h + digit_height(vf)) / 2);
+    draw_small_beside(fb, r, v);
 }
 
 /* ---- XS (M6c, D34): the status bar's look ---- */
@@ -472,13 +557,32 @@ static bool tiny_bolt(const ui_value_t *v)
     return v->kind == UI_FK_BATTERY && v->state != UI_VALUE_MISSING && v->battery == DS_BAT_CHARGING;
 }
 
+/* The bolt's ink: the first of its 16 px icon's columns that hold any, and how many, so it sits 2 px from the
+ * battery, not its icon's box. */
+static void bolt_ink(int *x0, int *w)
+{
+    const gfx_bitmap_t *b = &gfx_icon_bolt_16;
+    int rb = (b->width + 7) / 8, lo = b->width, hi = -1;
+    for (int y = 0; y < b->height; y++) {
+        for (int x = 0; x < b->width; x++) {
+            if ((b->bits[y * rb + x / 8] >> (7 - x % 8)) & 1) {
+                lo = x < lo ? x : lo;
+                hi = x > hi ? x : hi;
+            }
+        }
+    }
+    *x0 = lo, *w = hi - lo + 1;
+}
+
 /* The symbol's size at `sym` px, its marks included: the battery's outline and bolt, the Moon, or the field's icon
  * and its arrow; 0 × 0 for a time and for a field without one. */
 static void tiny_symbol_size(const ui_value_t *v, int sym, int *w, int *h)
 {
     *w = *h = 0;
     if (v->kind == UI_FK_BATTERY) {
-        *w = sym + 6 + (tiny_bolt(v) ? 2 + 16 : 0), *h = sym / 2 + 2;
+        int bolt_x, bolt_w;
+        bolt_ink(&bolt_x, &bolt_w);
+        *w = sym + 6 + (tiny_bolt(v) ? 2 + bolt_w : 0), *h = sym / 2 + 2;
         *h = tiny_bolt(v) && *h < 16 ? 16 : *h;
     } else if (v->kind == UI_FK_MOON) {
         *w = *h = sym;
@@ -499,7 +603,9 @@ static void draw_tiny_symbol(gfx_fb_t *fb, const ui_value_t *v, int x, int y, in
         int bh = sym / 2 + 2;
         ui_draw_battery(fb, x, cy - bh / 2, sym + 6, bh, v->state == UI_VALUE_MISSING ? -1 : v->percent);
         if (tiny_bolt(v)) {
-            gfx_bitmap(fb, x + sym + 6 + 2, cy - 8, &gfx_icon_bolt_16, GFX_BLACK);
+            int bolt_x, bolt_w;
+            bolt_ink(&bolt_x, &bolt_w);
+            gfx_bitmap(fb, x + sym + 6 + 2 - bolt_x, cy - 8, &gfx_icon_bolt_16, GFX_BLACK);
         }
     } else if (v->kind == UI_FK_MOON) {
         if (v->state == UI_VALUE_MISSING) {
@@ -615,6 +721,10 @@ static void draw_tiny_stacked(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
     }
     int sym_w, sym_h;
     tiny_symbol_size(v, sym, &sym_w, &sym_h);
+    if (sym == 24 && sym_w > r.w - 4) { /* a charging battery's bolt beside the 24 px outline: the 16 px one */
+        sym = 16;
+        tiny_symbol_size(v, sym, &sym_w, &sym_h);
+    }
     int gap = sym_h ? 4 : 0;
     int room = r.h - 4 - sym_h - gap;
     const gfx_font_t *vf;
@@ -769,7 +879,8 @@ void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t
     } else {
         draw_labelled(fb, r, size, &shown);
     }
-    if (shown.state == UI_VALUE_STALE && size != UI_SIZE_XS) { /* XS has no room; the status bar marks it */
+    /* XS and a short S cell have no room beside the value; the status bar's stale warning covers them */
+    if (shown.state == UI_VALUE_STALE && size != UI_SIZE_XS && !(size == UI_SIZE_S && r.h < 80)) {
         draw_age(fb, r, &shown, lang);
     }
     fb->clip = saved;
```


- [ ] **Step 5: The forecast widgets in short cells.** The current weather beside its sky (24 px under 120 px of width or 52 px of height), its word on two lines where there is room, cut on one only where its accents and tails still fit; today's high and low beside a 24 px sky, smaller faces for °F, and in a wide cell the short form and then smaller faces before an ellipsis ("102/97°"); air quality's index in bold 20 or 16 where bold 28 is too wide (past 100), its word in a smaller face, under the number, or left to the bar; pollen's level word in bold 16 where bold 20 is too wide; the sun's polar words beside its sky under 60 px, on two lines if need be, and in sans 12 before a cut; in XS, the clear sky alone where the polar words would be cut.

`components/ui/ui_forecast.c`:

```diff
--- a/components/ui/ui_forecast.c
+++ b/components/ui/ui_forecast.c
@@ -665,6 +665,73 @@ static void tiny_sun_stack(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
     }
 }
 
+/* ---- S in a short cell (M6c, D34): the sky beside the value ---- */
+
+/* The current weather beside its sky (48 px, 24 px in a cell under 120 px wide or 52 px tall): the temperature,
+ * and under it the sky's word on one or two lines where the height allows. */
+static void weather_now_compact(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
+{
+    int sky = r.w >= 120 && r.h >= 52 ? 48 : 24;
+    const gfx_bitmap_t *icon = ui_sky_icon(v->sky, v->night, sky);
+    gfx_bitmap(fb, r.x + 4, r.y + (r.h - sky) / 2, icon, GFX_BLACK);
+    int x = r.x + 4 + sky + 4, right = r.x + r.w - 4;
+    char t[sizeof(v->text) + 2];
+    snprintf(t, sizeof(t), "%s" DEGREE, v->text);
+    const gfx_font_t *tf = gfx_text_width(&gfx_font_sans_bold_28, t) <= right - x ? &gfx_font_sans_bold_28
+                                                                                  : &gfx_font_sans_bold_20;
+    const gfx_font_t *wf = &gfx_font_sans_12;
+    char line[2][32];
+    ui_split_two_lines(wf, v->extra, right - x, line[0], line[1], sizeof(line[0]));
+    int th = ui_ink_above(tf, t), lh[2];
+    for (int i = 0; i < 2; i++) {
+        lh[i] = ui_ink_above(wf, line[i]) + ui_ink_below(wf, line[i]);
+    }
+    int lines = line[1][0] ? 2 : line[0][0] ? 1 : 0;
+    while (lines > 0 && th + 4 + lh[0] + (lines > 1 ? 2 + lh[1] : 0) + 4 > r.h) {
+        lines--;
+    }
+    if (lines == 1 && line[1][0]) { /* one line's room for two: the word cut on one */
+        gfx_text_ellipsize(wf, v->extra, right - x, line[0], sizeof(line[0]));
+        lh[0] = ui_ink_above(wf, line[0]) + ui_ink_below(wf, line[0]);
+        lines = th + 4 + lh[0] + 4 <= r.h; /* its accents and tails, as cut */
+    }
+    int block = th + (lines > 0 ? 4 + lh[0] : 0) + (lines > 1 ? 2 + lh[1] : 0);
+    int y = r.y + (r.h - block) / 2 + th;
+    gfx_text(fb, tf, x, y, t, GFX_BLACK);
+    for (int i = 0; i < lines; i++) {
+        y += (i == 0 ? ui_ink_below(tf, t) + 4 : ui_ink_below(wf, line[0]) + 2) + ui_ink_above(wf, line[i]);
+        gfx_text(fb, wf, x, y, line[i], GFX_BLACK);
+    }
+}
+
+/* Today's weather beside its 24 px sky: the high and low, and under them the chance of rain where it fits. */
+static void weather_day_compact(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
+{
+    int x = r.x + 32, right = r.x + r.w - 4;
+    static const gfx_font_t *const k_faces[] = { &gfx_font_sans_bold_20, &gfx_font_sans_bold_16, &gfx_font_sans_12 };
+    const gfx_font_t *tf = &gfx_font_sans_12;
+    for (size_t i = 0; i < sizeof(k_faces) / sizeof(k_faces[0]); i++) {
+        if (gfx_text_width(k_faces[i], v->short_text) <= right - x) {
+            tf = k_faces[i];
+            break;
+        }
+    }
+    char t[sizeof(v->short_text)];
+    gfx_text_ellipsize(tf, v->short_text, right - x, t, sizeof(t)); /* "109/97°" in a 90 px cell */
+    int th = ui_ink_above(tf, t);
+    bool rain = v->percent >= 0 && th + 4 + 16 + 4 <= r.h;
+    int block = th + (rain ? 4 + 16 : 0);
+    int top = r.y + (r.h - block) / 2;
+    gfx_bitmap(fb, r.x + 4, r.y + (r.h - 24) / 2, ui_sky_icon(v->sky, false, 24), GFX_BLACK);
+    gfx_text(fb, tf, x, top + th, t, GFX_BLACK);
+    if (rain) {
+        char text[16];
+        snprintf(text, sizeof(text), "%d %%", v->percent);
+        gfx_bitmap(fb, x, top + th + 4, &gfx_icon_drop_16, GFX_BLACK);
+        gfx_text(fb, &gfx_font_sans_12, x + 18, top + th + 4 + 12, text, GFX_BLACK);
+    }
+}
+
 static void draw_weather_now(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
 {
     const gfx_bitmap_t *icon = ui_sky_icon(v->sky, v->night, 48);
@@ -675,6 +742,10 @@ static void draw_weather_now(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const u
         return;
     }
     if (size == UI_SIZE_S) {
+        if ((r.w < 150 && r.h < 86) || r.h < 61) { /* short (M6c): the sky beside the temperature */
+            weather_now_compact(fb, r, v);
+            return;
+        }
         if (r.w < 150) { /* narrow: the sky above, the temperature below */
             gfx_bitmap(fb, r.x + (r.w - 48) / 2, r.y + 8, icon, GFX_BLACK);
             char t[sizeof(v->text) + 2];
@@ -768,6 +839,10 @@ static void draw_weather_day(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const u
         }
         return;
     }
+    if (size == UI_SIZE_S && ((r.w < 150 && r.h < 99) || r.h < 51)) { /* short (M6c): the sky beside the highs */
+        weather_day_compact(fb, r, v);
+        return;
+    }
     if (size == UI_SIZE_S && r.w < 150) {
         gfx_bitmap(fb, r.x + (r.w - 48) / 2, r.y + 8, ui_sky_icon(v->sky, false, 48), GFX_BLACK);
         centred(fb, &gfx_font_sans_bold_20, r, r.y + 8 + 48 + 8 + ink_height(&gfx_font_sans_bold_20), v->short_text);
@@ -798,13 +873,16 @@ static void draw_weather_day(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const u
     gfx_bitmap(fb, x, top + (body_h - 48) / 2, ui_sky_icon(v->sky, false, 48), GFX_BLACK);
     int tx = x + 48 + 12;
     int room = r.x + r.w - 6 - tx;
-    /* "23° / 13°" is wider than "18° / 9°": the short form, then a smaller font, before an ellipsis */
-    const char *text = v->text;
-    if (gfx_text_width(vf, text) > room) {
-        text = v->short_text;
-        if (gfx_text_width(vf, text) > room) {
-            vf = &gfx_font_sans_bold_20;
+    /* "23° / 13°" is wider than "18° / 9°": the short form, then smaller faces, before an ellipsis ("102/97°") */
+    static const gfx_font_t *const k_faces[] = { &gfx_font_sans_bold_28, &gfx_font_sans_bold_20,
+                                                 &gfx_font_sans_bold_16, &gfx_font_sans_12 };
+    const char *text = v->short_text;
+    vf = &gfx_font_sans_12;
+    for (size_t i = 0; i < sizeof(k_faces) / sizeof(k_faces[0]); i++) {
+        if (gfx_text_width(k_faces[i], v->text) <= room || gfx_text_width(k_faces[i], v->short_text) <= room) {
+            vf = k_faces[i];
             text = gfx_text_width(vf, v->text) <= room ? v->text : v->short_text;
+            break;
         }
     }
     char fit[24];
@@ -912,9 +990,16 @@ static void draw_sun(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_
 {
     if (size == UI_SIZE_XS) {
         bool big = tiny_icon(r) == 24;
-        if (v->polar) {
-            tiny_row(fb, r, ui_sky_icon(WEATHER_SKY_CLEAR, v->polar != 1, tiny_icon(r)), v->text, NULL, NULL, NULL,
-                     false);
+        if (v->polar) { /* its words; where they would be cut, the clear sky alone, by day or by night */
+            const gfx_bitmap_t *sky = ui_sky_icon(WEATHER_SKY_CLEAR, v->polar != 1, tiny_icon(r));
+            char fit[48];
+            bool stacked = ui_tiny_stacked(r);
+            if (tiny_face(v->text, r.w - (stacked ? 8 : 7), r.h - (stacked ? sky->height + 4 : 0), false, fit,
+                          sizeof(fit)) != NULL) {
+                tiny_row(fb, r, sky, v->text, NULL, NULL, NULL, false);
+            } else {
+                gfx_bitmap(fb, r.x + (r.w - sky->width) / 2, r.y + (r.h - sky->height) / 2, sky, GFX_BLACK);
+            }
         } else if (ui_tiny_stacked(r)) {
             tiny_sun_stack(fb, r, v);
         } else {
@@ -934,8 +1019,27 @@ static void draw_sun(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_
     }
     if (v->polar) {
         const gfx_bitmap_t *icon = v->polar == 1 ? &gfx_icon_wx_clear_day_24 : &gfx_icon_wx_clear_night_24;
+        if (size == UI_SIZE_S && r.h < 60) { /* a short cell (M6c): the sky beside its words, on two lines if need be */
+            int pad = r.w < 150 ? 6 : 14, x = r.x + pad + 24 + (r.w < 150 ? 6 : 10), max_w = r.x + r.w - 6 - x;
+            gfx_bitmap(fb, r.x + pad, r.y + (r.h - 24) / 2, icon, GFX_BLACK);
+            const gfx_font_t *pf = gfx_text_width(&gfx_font_sans_bold_16, v->text) <= max_w ? &gfx_font_sans_bold_16
+                                                                                            : &gfx_font_sans_12;
+            if (gfx_text_width(pf, v->text) <= max_w) {
+                gfx_text(fb, pf, x, r.y + (r.h + ui_ink_above(pf, v->text) - ui_ink_below(pf, v->text)) / 2, v->text,
+                         GFX_BLACK);
+                return;
+            }
+            char line[2][32];
+            ui_split_two_lines(pf, v->text, max_w, line[0], line[1], sizeof(line[0]));
+            int y = r.y + (r.h - 2 * pf->line_height) / 2 + pf->ascent;
+            gfx_text(fb, pf, x, y, line[0], GFX_BLACK);
+            gfx_text(fb, pf, x, y + pf->line_height, line[1], GFX_BLACK);
+            return;
+        }
         gfx_bitmap(fb, r.x + (r.w - 24) / 2, top + 8, icon, GFX_BLACK);
-        centred(fb, &gfx_font_sans_bold_16, r, top + 8 + 24 + 20, v->text);
+        const gfx_font_t *pf = gfx_text_width(&gfx_font_sans_bold_16, v->text) <= r.w - 8 ? &gfx_font_sans_bold_16
+                                                                                       : &gfx_font_sans_12;
+        centred(fb, pf, r, top + 8 + 24 + 20, v->text);
         return;
     }
     const gfx_font_t *tf = size == UI_SIZE_S ? &gfx_font_sans_bold_16 : &gfx_font_sans_bold_20;
@@ -975,7 +1079,7 @@ static void draw_level(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_valu
         return;
     }
     if (size == UI_SIZE_S) {
-        bool narrow = r.w < 150;
+        bool narrow = r.w < 150 && r.h >= 83; /* M6c: a short, narrow cell draws it side by side */
         int y = r.y + 10;
         if (narrow) {
             gfx_bitmap(fb, r.x + (r.w - 24) / 2, y, icon, GFX_BLACK);
@@ -983,14 +1087,31 @@ static void draw_level(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_valu
             centred(fb, &gfx_font_sans_12, r, y + 24 + 6 + ink_height(&gfx_font_sans_bold_28) + 18, v->extra);
             return;
         }
-        gfx_bitmap(fb, r.x + 14, r.y + (r.h - 24) / 2, icon, GFX_BLACK);
-        int x = r.x + 14 + 24 + 10;
+        int pad = r.w < 150 ? 6 : 14;
+        gfx_bitmap(fb, r.x + pad, r.y + (r.h - 24) / 2, icon, GFX_BLACK);
+        int x = r.x + pad + 24 + (r.w < 150 ? 6 : 10), right = r.x + r.w - 6;
         int base = r.y + r.h / 2 + 2;
-        int pen = gfx_text(fb, &gfx_font_sans_bold_28, x, base, v->text, GFX_BLACK);
-        char word[32];
-        gfx_text_ellipsize(&gfx_font_sans_16, v->extra, r.x + r.w - 8 - pen - 8, word, sizeof(word));
-        gfx_text(fb, &gfx_font_sans_16, pen + 8, base, word, GFX_BLACK);
-        level_bar(fb, x, base + 10, bands, v->percent + 1, 12, 6);
+        const gfx_font_t *nf = &gfx_font_sans_bold_28; /* an index past 100 in smaller faces, uncut */
+        for (int i = 0; gfx_text_width(nf, v->text) > right - x && i < 2; i++) {
+            nf = i == 0 ? &gfx_font_sans_bold_20 : &gfx_font_sans_bold_16;
+        }
+        int pen = gfx_text(fb, nf, x, base, v->text, GFX_BLACK);
+        const gfx_font_t *wf = gfx_text_width(&gfx_font_sans_16, v->extra) <= right - pen - 6 ? &gfx_font_sans_16
+                                                                                              : &gfx_font_sans_12;
+        int cell = (right - x - (bands - 1) * 3) / bands; /* the bar fits what is left of the row */
+        if (gfx_text_width(wf, v->extra) > right - pen - 6) { /* the word under the number, in the bar's place */
+            char word[32];
+            gfx_text_ellipsize(&gfx_font_sans_12, v->extra, right - x, word, sizeof(word));
+            int wb = base + 4 + ui_ink_above(&gfx_font_sans_12, word);
+            if (wb + ui_ink_below(&gfx_font_sans_12, word) <= r.y + r.h - 2) {
+                gfx_text(fb, &gfx_font_sans_12, x, wb, word, GFX_BLACK);
+            } else { /* no room under it either: the bar alone says the band */
+                level_bar(fb, x, base + 10, bands, v->percent + 1, cell > 12 ? 12 : cell, 6);
+            }
+            return;
+        }
+        gfx_text(fb, wf, pen + 6, base, v->extra, GFX_BLACK);
+        level_bar(fb, x, base + 10, bands, v->percent + 1, cell > 12 ? 12 : cell, 6);
         return;
     }
     int top = r.y + label_line(fb, r, size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16, v->label);
@@ -1020,18 +1141,22 @@ static void draw_pollen(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_val
     }
     if (size == UI_SIZE_S) {
         int y = r.y + 10;
-        if (r.w < 150) {
+        if (r.w < 150 && r.h >= 86) { /* M6c: a short, narrow cell draws it side by side */
             gfx_bitmap(fb, r.x + (r.w - 24) / 2, y, &gfx_icon_pollen_24, GFX_BLACK);
             centred(fb, &gfx_font_sans_bold_16, r, y + 24 + 6 + 14, v->text);
             centred(fb, &gfx_font_sans_12, r, y + 24 + 6 + 14 + 16, v->extra[0] ? v->extra : v->label);
             level_bar(fb, r.x + (r.w - (3 * 14 + 2 * 3)) / 2, y + 24 + 6 + 14 + 24, 3, v->percent, 14, 6);
             return;
         }
-        gfx_bitmap(fb, r.x + 14, r.y + (r.h - 24) / 2, &gfx_icon_pollen_24, GFX_BLACK);
-        int x = r.x + 14 + 24 + 10;
+        int pad = r.w < 150 ? 6 : 14;
+        gfx_bitmap(fb, r.x + pad, r.y + (r.h - 24) / 2, &gfx_icon_pollen_24, GFX_BLACK);
+        int x = r.x + pad + 24 + (r.w < 150 ? 6 : 10);
         char line[48];
-        gfx_text_ellipsize(&gfx_font_sans_bold_20, v->text, r.x + r.w - 6 - x, line, sizeof(line));
-        gfx_text(fb, &gfx_font_sans_bold_20, x, r.y + r.h / 2, line, GFX_BLACK);
+        const gfx_font_t *wf = gfx_text_width(&gfx_font_sans_bold_20, v->text) <= r.x + r.w - 6 - x
+                                   ? &gfx_font_sans_bold_20
+                                   : &gfx_font_sans_bold_16;
+        gfx_text_ellipsize(wf, v->text, r.x + r.w - 6 - x, line, sizeof(line));
+        gfx_text(fb, wf, x, r.y + r.h / 2, line, GFX_BLACK);
         gfx_text_ellipsize(&gfx_font_sans_12, v->extra[0] ? v->extra : v->label, r.x + r.w - 6 - x, line, sizeof(line));
         gfx_text(fb, &gfx_font_sans_12, x, r.y + r.h / 2 + 16, line, GFX_BLACK);
         return;
```


- [ ] **Step 6: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_ui_split && ./build-host/test_ui_catalog && ./build-host/test_ui_widget_fit && ctest --test-dir build-host`
Expected: `14 Tests 0 Failures`, `4 Tests 0 Failures`, `14 Tests 0 Failures`; `100% tests passed, 0 tests failed out of 61`.

- [ ] **Step 7: The firmware builds.** `tools/idf.sh build`: clean, without a warning. The app grows by 2.7 KB, to 0x25f510 bytes.

- [ ] **Step 8: Commit.**

```bash
git add components/ui test/host/test_ui_split.c test/host/test_ui_catalog.c test/host/test_ui_widget_fit.c
git commit -m "feat(ui): S in short cells, the symbol beside the value"
```

### Task 3: 24 cells down to 40×20 (`ui`, `storage`, `webui`, `main`)

**Files:**
- Modify: `components/ui/include/ui_split.h`, `components/ui/include/ui_layout.h` (`UI_SLOT_MAX`), `components/ui/include/ui_preset.h` (the file's size and depth), `components/ui/ui_split.c` (a comment), `components/ui/ui_forecast.c` (a word's tails in a tall narrow M cell), `components/storage/include/storage_backup.h` and `components/storage/storage_backup.c` (the backup's depth, now public), `components/webui/include/webui.h`, `main/app.c` (the snapshot), `main/app_web.c` (the limits tied together), `main/app_ui.c` (the presets' defaults off the app task's stack)
- Test: `test/host/test_ui_split.c`, `test/host/test_ui_preset.c`, `test/host/test_ui_widget_fit.c`, `test/host/test_ui_catalog.c`, `test/host/test_storage_backup.c`, `test/web/test_app.mjs`

**Interfaces:**
- Consumes: the XS size and its widgets (Task 1); S's heights (Task 2).
- Produces (Task 4): `UI_SPLIT_CELLS` 24, `UI_SPLIT_NODES` 47, `UI_SPLIT_MIN_W` 40, `UI_SPLIT_MIN_H` 20; `UI_SLOT_MAX` 24; `UI_PRESETS_JSON_MAX` 49 152; `UI_JSON_MAX_DEPTH` 20; `WEBUI_BODY_MAX` and `WEBUI_REPLY_MAX` 64 KB, `WEBUI_JSON_MAX_DEPTH` 22; `BACKUP_MAX_DEPTH` 22 in `storage_backup.h`; the snapshot's cap 6 144 bytes and `SNAP_VERSION` 9.

A tree of n cells has 2n − 1 nodes, so 47 bytes hold 24 cells. The editor (`web/app.js`) reads every limit from `GET /api/layouts`; its tests' copy of the device's rules changes, its code doesn't.

- [ ] **Step 1: Write the failing tests.** The geometry of 24 cells, and the new least part:

`test/host/test_ui_split.c`:

```diff
--- a/test/host/test_ui_split.c
+++ b/test/host/test_ui_split.c
@@ -90,26 +90,29 @@ static void test_the_spec_example_lays_out_as_the_spec_says(void)
     TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(s_g.cell[3].w, s_g.cell[3].h));
 }
 
-/* Eight cells, each at least 90×40: two rows of four columns. */
-static void test_eight_cells_fill_the_tree(void)
+/* Eight columns of halves: a row of 8 cells, 50 or 49 px wide (M6c). */
+#define EIGHT_COLUMNS                                                                                              \
+    COLS(UI_RATIO_1_2), COLS(UI_RATIO_1_2), COLS(UI_RATIO_1_2), CELL, CELL, COLS(UI_RATIO_1_2), CELL, CELL,         \
+        COLS(UI_RATIO_1_2), COLS(UI_RATIO_1_2), CELL, CELL, COLS(UI_RATIO_1_2), CELL, CELL
+
+/* 24 cells, each at least 40×20 (D34): three rows of eight columns. */
+static void test_24_cells_fill_the_tree(void)
 {
-    static const uint8_t k_tree[UI_SPLIT_NODES] = {
-        ROWS(UI_RATIO_1_2),
-        COLS(UI_RATIO_1_4), CELL, COLS(UI_RATIO_1_3), CELL, COLS(UI_RATIO_1_2), CELL, CELL,
-        COLS(UI_RATIO_1_4), CELL, COLS(UI_RATIO_1_3), CELL, COLS(UI_RATIO_1_2), CELL, CELL,
-    };
+    static const uint8_t k_tree[UI_SPLIT_NODES] = { ROWS(UI_RATIO_1_3), EIGHT_COLUMNS, ROWS(UI_RATIO_1_2),
+                                                    EIGHT_COLUMNS, EIGHT_COLUMNS };
     TEST_ASSERT_TRUE(ui_split_layout(k_tree, ui_split_area(), &s_g));
     TEST_ASSERT_EQUAL_INT(UI_SPLIT_NODES, ui_split_nodes(k_tree));
-    TEST_ASSERT_EQUAL_INT(8, s_g.cells);
-    TEST_ASSERT_EQUAL_INT(7, s_g.lines);
-    check_rect(0, 21, 100, 139, s_g.cell[0]);
-    check_rect(101, 21, 99, 139, s_g.cell[1]);
-    check_rect(201, 21, 99, 139, s_g.cell[2]);
-    check_rect(301, 21, 99, 139, s_g.cell[3]);
-    check_rect(301, 161, 99, 139, s_g.cell[7]);
+    TEST_ASSERT_EQUAL_INT(24, s_g.cells);
+    TEST_ASSERT_EQUAL_INT(23, s_g.lines);
+    check_rect(0, 21, 50, 93, s_g.cell[0]);
+    check_rect(51, 21, 49, 93, s_g.cell[1]);
+    check_rect(351, 21, 49, 93, s_g.cell[7]);
+    check_rect(0, 115, 50, 92, s_g.cell[8]);
+    check_rect(351, 208, 49, 92, s_g.cell[23]);
+    TEST_ASSERT_EQUAL_INT(UI_SIZE_XS, ui_split_cell_size(s_g.cell[23].w, s_g.cell[23].h));
 }
 
-/* A tree whose splits run past the 15 nodes has no end: there is no room for its cells. */
+/* A tree whose splits run past the 47 nodes has no end: there is no room for its cells. */
 static void test_a_tree_cut_short_is_refused(void)
 {
     uint8_t tree[UI_SPLIT_NODES];
@@ -118,16 +121,19 @@ static void test_a_tree_cut_short_is_refused(void)
     TEST_ASSERT_EQUAL_INT(0, ui_split_nodes(tree));
 }
 
-static void test_a_part_under_90_by_40_is_refused(void)
+static void test_a_part_under_40_by_20_is_refused(void)
 {
-    static const uint8_t k_narrow[] = { COLS(UI_RATIO_1_4), COLS(UI_RATIO_1_2), CELL, CELL, CELL }; /* 50 px */
-    static const uint8_t k_short[] = { ROWS(UI_RATIO_1_4), ROWS(UI_RATIO_1_2), CELL, CELL, CELL };  /* 34 px */
-    static const uint8_t k_just[] = { ROWS(UI_RATIO_1_3), ROWS(UI_RATIO_1_2), CELL, CELL, CELL };   /* 46 px */
+    static const uint8_t k_narrow[] = { COLS(UI_RATIO_1_4), COLS(UI_RATIO_1_4), CELL, CELL, CELL }; /* 25 px */
+    static const uint8_t k_short[] = { ROWS(UI_RATIO_1_4), ROWS(UI_RATIO_1_4), CELL, CELL, CELL };  /* 17 px */
+    static const uint8_t k_just[] = { ROWS(UI_RATIO_1_4), ROWS(UI_RATIO_1_3), CELL, CELL, CELL };   /* 23 px */
     TEST_ASSERT_FALSE(lay(k_narrow, sizeof(k_narrow)));
     TEST_ASSERT_FALSE(lay(k_short, sizeof(k_short)));
     TEST_ASSERT_TRUE(lay(k_just, sizeof(k_just)));
-    check_rect(0, 21, 400, 46, s_g.cell[0]);
-    check_rect(0, 68, 400, 46, s_g.cell[1]);
+    check_rect(0, 21, 400, 23, s_g.cell[0]);
+    check_rect(0, 45, 400, 45, s_g.cell[1]);
+    static const uint8_t k_just_narrow[] = { COLS(UI_RATIO_1_4), COLS(UI_RATIO_1_2), CELL, CELL, CELL }; /* 50 px */
+    TEST_ASSERT_TRUE(lay(k_just_narrow, sizeof(k_just_narrow)));
+    check_rect(0, 21, 50, 279, s_g.cell[0]);
 }
 
 static void test_a_bad_node_is_refused(void)
@@ -276,9 +282,9 @@ int main(void)
     RUN_TEST(test_each_ratio_rounds_its_first_part_down);
     RUN_TEST(test_a_single_cell_fills_the_area);
     RUN_TEST(test_the_spec_example_lays_out_as_the_spec_says);
-    RUN_TEST(test_eight_cells_fill_the_tree);
+    RUN_TEST(test_24_cells_fill_the_tree);
     RUN_TEST(test_a_tree_cut_short_is_refused);
-    RUN_TEST(test_a_part_under_90_by_40_is_refused);
+    RUN_TEST(test_a_part_under_40_by_20_is_refused);
     RUN_TEST(test_a_bad_node_is_refused);
     RUN_TEST(test_a_cells_size_follows_its_dimensions);
     RUN_TEST(test_xs_takes_the_small_kinds_from_40_by_20);
```


The largest file and the deepest tree, and the refusals at the new limits:

`test/host/test_ui_preset.c`:

```diff
--- a/test/host/test_ui_preset.c
+++ b/test/host/test_ui_preset.c
@@ -5,6 +5,7 @@
 #include "ui_preset.h"
 #include "ui_split.h"
 #include "unity.h"
+#include "util_json.h"
 
 static ui_presets_t s_p;
 static char s_err[128];
@@ -442,28 +443,34 @@ static void check_tree_rejected(const char *tree, const char *reason_part)
     check_rejected(split_file(tree), reason_part);
 }
 
+/* Eight columns of halves, a row of cells 50 or 49 px wide; `first` is the leftmost cell. */
+static void eight_columns(char *out, size_t size, const char *first)
+{
+    static char two[512], two_first[512], four[1024], four_first[1024];
+    put_split(two, sizeof(two), "columns", "1/2", "{}", "{}");
+    put_split(two_first, sizeof(two_first), "columns", "1/2", first, "{}");
+    put_split(four, sizeof(four), "columns", "1/2", two, two);
+    put_split(four_first, sizeof(four_first), "columns", "1/2", two_first, two);
+    put_split(out, size, "columns", "1/2", four_first, four);
+}
+
 static void test_bad_split_trees_are_rejected_with_a_reason(void)
 {
-    static char tree[2048], part[1024];
-    /* nine cells, each at least 90×40: two rows of four, one of them split again */
-    put_split(part, sizeof(part), "columns", "1/2", "{}", "{}");
-    char row[512];
-    snprintf(row, sizeof(row), "{\"split\": \"columns\", \"ratio\": \"1/4\", \"a\": {}, \"b\": {\"split\": \"columns\","
-             " \"ratio\": \"1/3\", \"a\": {}, \"b\": %s}}", part);
-    char split_cell[256];
-    put_split(split_cell, sizeof(split_cell), "rows", "1/2", "{}", "{}");
-    char row9[512];
-    snprintf(row9, sizeof(row9), "{\"split\": \"columns\", \"ratio\": \"1/4\", \"a\": %s,"
-             " \"b\": {\"split\": \"columns\", \"ratio\": \"1/3\", \"a\": {}, \"b\": %s}}", split_cell, part);
-    put_split(tree, sizeof(tree), "rows", "1/2", row, row9);
-    check_tree_rejected(tree, "at most 8 cells");
-    put_split(tree, sizeof(tree), "rows", "1/2", row, row); /* eight are fine */
+    static char tree[8192], part[1024], row[2048], row25[2048], rows[4096];
+    /* 25 cells, each at least 40×20: three rows of eight, one of them split again (M6c) */
+    put_split(part, sizeof(part), "rows", "1/2", "{}", "{}");
+    eight_columns(row, sizeof(row), "{}");
+    eight_columns(row25, sizeof(row25), part);
+    put_split(rows, sizeof(rows), "rows", "1/2", row, row);
+    put_split(tree, sizeof(tree), "rows", "1/3", row25, rows);
+    check_tree_rejected(tree, "at most 24 cells");
+    put_split(tree, sizeof(tree), "rows", "1/3", row, rows); /* 24 are fine */
     TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(split_file(tree), &s_p, s_err, sizeof(s_err)), s_err);
-    TEST_ASSERT_EQUAL_INT(8, ui_preset_slots(&s_p.presets[0]));
+    TEST_ASSERT_EQUAL_INT(24, ui_preset_slots(&s_p.presets[0]));
 
-    put_split(part, sizeof(part), "rows", "1/2", "{}", "{}");
-    put_split(tree, sizeof(tree), "rows", "1/4", part, "{}"); /* 69 px split in two: 34 */
-    check_tree_rejected(tree, "90×40");
+    put_split(part, sizeof(part), "rows", "1/4", "{}", "{}");
+    put_split(tree, sizeof(tree), "rows", "1/4", part, "{}"); /* 69 px, a quarter of it: 17 */
+    check_tree_rejected(tree, "40×20");
     check_tree_rejected("{\"split\": \"diagonal\", \"ratio\": \"1/2\", \"a\": {}, \"b\": {}}", "rows or columns");
     check_tree_rejected("{\"split\": \"rows\", \"ratio\": \"2/5\", \"a\": {}, \"b\": {}}", "1/4, 1/3, 1/2, 2/3 or 3/4");
     check_tree_rejected("{\"split\": \"rows\", \"ratio\": \"1/2\", \"a\": {}}", "both parts");
@@ -479,19 +486,17 @@ static void test_bad_split_trees_are_rejected_with_a_reason(void)
                    "no slot \"main\"");
 }
 
-/* The largest presets.json there can be: 16 split presets of 8 cells, in columns wherever a tree can
- * have them, with every line hidden; the longest field ids a cell takes; names of control characters,
- * which cJSON writes as six bytes each ("\u0001"); every option at its longest; 8 schedule entries
+#define C (UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE) /* a hidden column split: the longest node written */
+#define EIGHT_COLUMNS C, C, C, 0, 0, C, 0, 0, C, C, 0, 0, C, 0, 0
+
+/* The largest presets.json there can be (M6c): 16 split presets of 24 cells, three rows of eight columns, the
+ * most columns a tree can have, with every line hidden; the longest field ids a cell takes; names of control
+ * characters, which cJSON writes as six bytes each ("\u0001"); every option at its longest; 8 schedule entries
  * that switch to a preset. */
 static void test_a_full_set_of_split_presets_fits_the_save_buffer(void)
 {
-    static const uint8_t k_tree[UI_SPLIT_NODES] = {
-        UI_RATIO_1_2 | UI_SPLIT_NO_LINE,
-        UI_RATIO_1_4 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0,
-        UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0,
-        UI_RATIO_1_4 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0,
-        UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0,
-    };
+    static const uint8_t k_tree[UI_SPLIT_NODES] = { UI_RATIO_1_3 | UI_SPLIT_NO_LINE, EIGHT_COLUMNS,
+                                                    UI_RATIO_1_2 | UI_SPLIT_NO_LINE, EIGHT_COLUMNS, EIGHT_COLUMNS };
     memset(&s_p, 0, sizeof(s_p));
     for (int i = 0; i < UI_PRESET_MAX; i++) {
         ui_preset_t *p = &s_p.presets[i];
@@ -517,11 +522,46 @@ static void test_a_full_set_of_split_presets_fits_the_save_buffer(void)
     static char buf[UI_PRESETS_JSON_MAX];
     size_t n = ui_presets_to_json(&s_p, buf, sizeof(buf));
     TEST_ASSERT_TRUE_MESSAGE(n > 0, "the worst case must fit UI_PRESETS_JSON_MAX");
+    char size_msg[48];
+    snprintf(size_msg, sizeof(size_msg), "the largest presets.json: %zu bytes", n);
+    TEST_MESSAGE(size_msg);
     ui_presets_t back;
     TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(buf, &back, s_err, sizeof(s_err)), s_err);
     TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
 }
 
+/* The deepest tree there can be (M6c): 13 splits in a chain, seven rows then six columns, each leaving a cell
+ * beside the next split; its last cells are 41×20. Its file nests 17 levels, under UI_JSON_MAX_DEPTH. */
+static void test_the_deepest_tree_fits_the_nesting_limit(void)
+{
+    static const uint8_t k_tree[UI_SPLIT_NODES] = {
+        UI_RATIO_1_4, 0, UI_RATIO_1_4, 0, UI_RATIO_1_4, 0, UI_RATIO_1_4, 0, UI_RATIO_1_4, 0, UI_RATIO_1_3, 0,
+        UI_RATIO_1_2, 0,
+        UI_RATIO_1_4 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_4 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_4 | UI_SPLIT_COLUMNS, 0,
+        UI_RATIO_1_4 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_2 | UI_SPLIT_COLUMNS, 0, 0,
+    };
+    ui_split_geometry_t g;
+    TEST_ASSERT_TRUE(ui_split_layout(k_tree, ui_split_area(), &g));
+    TEST_ASSERT_EQUAL_INT(14, g.cells);
+    TEST_ASSERT_EQUAL_INT(41, g.cell[13].w);
+    TEST_ASSERT_EQUAL_INT(20, g.cell[13].h);
+    memset(&s_p, 0, sizeof(s_p));
+    ui_preset_t *p = &s_p.presets[0];
+    snprintf(p->id, sizeof(p->id), "deep");
+    snprintf(p->name, sizeof(p->name), "Deep");
+    p->layout = UI_LAYOUT_SPLIT;
+    memcpy(p->split, k_tree, sizeof(k_tree));
+    p->slots[13] = UI_FIELD_ENV_TEMP;
+    s_p.count = 1;
+    static char buf[UI_PRESETS_JSON_MAX];
+    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, buf, sizeof(buf)) > 0);
+    TEST_ASSERT_EQUAL_INT(17, util_json_depth(buf));
+    TEST_ASSERT_TRUE(17 <= UI_JSON_MAX_DEPTH);
+    ui_presets_t back;
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(buf, &back, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(k_tree, back.presets[0].split, UI_SPLIT_NODES);
+}
+
 static void test_a_preset_counts_the_slots_its_layout_uses(void)
 {
     TEST_ASSERT_EQUAL_INT(6, ui_preset_slots(&s_p.presets[0])); /* Home: Classic */
@@ -562,6 +602,7 @@ int main(void)
     RUN_TEST(test_a_split_preset_without_a_tree_is_one_empty_cell);
     RUN_TEST(test_bad_split_trees_are_rejected_with_a_reason);
     RUN_TEST(test_a_full_set_of_split_presets_fits_the_save_buffer);
+    RUN_TEST(test_the_deepest_tree_fits_the_nesting_limit);
     RUN_TEST(test_a_preset_counts_the_slots_its_layout_uses);
     return UNITY_END();
 }
```


Every cell size the new limits make, and the published rules at every width and height, so no boundary falls between two samples (M6b's minor; under half a second):

`test/host/test_ui_widget_fit.c`:

```diff
--- a/test/host/test_ui_widget_fit.c
+++ b/test/host/test_ui_widget_fit.c
@@ -272,11 +272,11 @@ static ui_context_t split_context(int variant)
 }
 
 /* Every cell size a legal split tree can make (spec §5.2): the area, then both parts of each split,
- * at most 7 splits deep, while both parts stay at least 90×40. */
+ * at most 23 splits deep, while both parts stay at least 40×20 (M6c). */
 static struct {
     int16_t w, h;
     uint8_t depth;
-} s_cells[512];
+} s_cells[4096];
 static int s_cell_count;
 
 static void reach(int w, int h, int depth)
@@ -315,7 +315,7 @@ static void test_every_field_fits_every_cell_a_split_can_make(void)
 {
     s_cell_count = 0;
     reach(400, 279, 0);
-    TEST_ASSERT_EQUAL_INT(336, s_cell_count);
+    TEST_ASSERT_EQUAL_INT(2451, s_cell_count);
     for (int variant = 0; variant < VARIANTS; variant++) {
         ui_context_t ctx = split_context(variant);
         for (int i = 0; i < s_cell_count; i++) {
```


`test/host/test_ui_catalog.c`:

```diff
--- a/test/host/test_ui_catalog.c
+++ b/test/host/test_ui_catalog.c
@@ -111,9 +111,9 @@ static void test_layouts_publish_the_split_rules(void)
     TEST_ASSERT_EQUAL_INT(21, num(split, "y"));
     TEST_ASSERT_EQUAL_INT(400, num(split, "w"));
     TEST_ASSERT_EQUAL_INT(279, num(split, "h"));
-    TEST_ASSERT_EQUAL_INT(8, num(split, "cells"));
-    TEST_ASSERT_EQUAL_INT(90, num(split, "min_w"));
-    TEST_ASSERT_EQUAL_INT(40, num(split, "min_h"));
+    TEST_ASSERT_EQUAL_INT(24, num(split, "cells")); /* M6c, D34 */
+    TEST_ASSERT_EQUAL_INT(40, num(split, "min_w"));
+    TEST_ASSERT_EQUAL_INT(20, num(split, "min_h"));
     TEST_ASSERT_EQUAL_INT(150, num(split, "narrow_w"));
     TEST_ASSERT_EQUAL_INT(8, num(split, "inset"));
     const cJSON *ratios = cJSON_GetObjectItemCaseSensitive(split, "ratios");
@@ -151,8 +151,8 @@ static void test_layouts_publish_the_split_rules(void)
                                                       "pollen", "rain_map" };
     static const char *const k_names[] = { "XS", "S", "M", "L", "XL" };
     for (int k = 0; k < UI_FK_COUNT; k++) {
-        for (int w = 40; w <= 400; w += 7) {
-            for (int h = 20; h <= 279; h += 3) {
+        for (int w = UI_SPLIT_MIN_W; w <= 400; w++) { /* every size, so no boundary falls between two */
+            for (int h = UI_SPLIT_MIN_H; h <= 279; h++) {
                 int size = ui_split_field_size((ui_field_kind_t)k, w, h);
                 const char *published = rule_size(split, k_kinds[k], w, h);
                 TEST_ASSERT_EQUAL_STRING(size < 0 ? NULL : k_names[size], published);
```


A backup holds a file as deep as `presets.json` may be, and refuses a level more:

`test/host/test_storage_backup.c`:

```diff
--- a/test/host/test_storage_backup.c
+++ b/test/host/test_storage_backup.c
@@ -81,6 +81,26 @@ static void test_deep_nesting_is_refused_before_parsing(void)
     TEST_ASSERT_NOT_NULL(strstr(s_err, "nested"));
 }
 
+/* A file nested as deep as presets.json may be (UI_JSON_MAX_DEPTH, two levels inside the bundle) restores; a level
+ * more is refused before anything parses it. */
+static void test_the_deepest_file_a_bundle_holds(void)
+{
+    static char deep[256];
+    for (int more = 0; more < 2; more++) {
+        int arrays = BACKUP_MAX_DEPTH - 2 - 1 + more; /* inside the file's own object */
+        size_t n = (size_t)snprintf(deep, sizeof(deep), "{\"reflbo_backup\": 1, \"files\": {\"a.json\": {\"x\": ");
+        for (int i = 0; i < arrays; i++) {
+            deep[n++] = '[';
+        }
+        for (int i = 0; i < arrays; i++) {
+            deep[n++] = ']';
+        }
+        snprintf(deep + n, sizeof(deep) - n, "}}}");
+        int got = backup_split(deep, s_files, 4, s_buf, sizeof(s_buf), s_err, sizeof(s_err));
+        TEST_ASSERT_EQUAL_INT_MESSAGE(more ? -1 : 1, got, s_err);
+    }
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -88,5 +108,6 @@ int main(void)
     RUN_TEST(test_a_broken_file_is_left_out);
     RUN_TEST(test_restore_refuses_anything_but_a_backup);
     RUN_TEST(test_deep_nesting_is_refused_before_parsing);
+    RUN_TEST(test_the_deepest_file_a_bundle_holds);
     return UNITY_END();
 }
```


The editor against the device's new rules: the ratios a 40×20 part allows, an XS cell's name, and a tree of 24 cells that splits no further:

`test/web/test_app.mjs`:

```diff
--- a/test/web/test_app.mjs
+++ b/test/web/test_app.mjs
@@ -507,13 +507,14 @@ test('a preset on a radar layout has no slots to fill', async () => {
 
 /* The device's rules (GET /api/layouts, ui_catalog.c) for the kinds these tests use. */
 const SPLIT = {
-  x: 0, y: 21, w: 400, h: 279, cells: 8, min_w: 90, min_h: 40, narrow_w: 150, inset: 8,
+  x: 0, y: 21, w: 400, h: 279, cells: 24, min_w: 40, min_h: 20, narrow_w: 150, inset: 8, /* M6c, D34 */
   ratios: ['1/4', '1/3', '1/2', '2/3', '3/4'],
   sizes: [
     { size: 'XL', min_w: 400, min_h: [120, 120], kinds: { time: [120, 120], number: [120, 120] } },
     { size: 'L', min_w: 200, min_h: [150, 150], kinds: { time: [150, 150], number: [150, 150] } },
     { size: 'M', min_w: 130, min_h: [80, 80], kinds: { time: [80, 80], number: [80, 80], series: [80, 80] } },
-    { size: 'S', min_w: 90, min_h: [80, 40], kinds: { time: [80, 40], number: [80, 40] } },
+    { size: 'S', min_w: 90, min_h: [40, 40], kinds: { time: [40, 40], number: [40, 40] } },
+    { size: 'XS', min_w: 40, min_h: [20, 20], kinds: { time: [20, 20], number: [20, 20] } },
   ],
 };
 const SPLIT_CATALOGUE = { ...CATALOGUE, layouts: [...CATALOGUE.layouts, { id: 'split', slots: [] }], split: SPLIT };
@@ -573,16 +574,43 @@ test('Split into rows halves a cell, its field going to the first part', async (
   assert.deepEqual(p.split, { split: 'rows', ratio: '1/2', line: true, a: { field: 'time.clock' }, b: {} });
 });
 
-test('a split offers only the ratios that leave every part 90×40', async () => {
-  const { ctx, main } = await load(splitDevice([], { split: 'rows', ratio: '1/2', line: true, a: {},
-                                                     b: { split: 'rows', ratio: '1/2', line: true, a: {}, b: {} } }));
+test('a split offers only the ratios that leave every part 40×20', async () => {
+  const { ctx, main } = await load(splitDevice([], { split: 'rows', ratio: '1/4', line: true,
+                                                     a: { split: 'rows', ratio: '1/2', line: true, a: {}, b: {} },
+                                                     b: {} }));
   await ctx.presetsPage();
   const [outer, inner] = ratioSelects(main);
   const disabled = (sel) => sel.children.filter((o) => o.disabled).map((o) => o.value);
-  assert.deepEqual(disabled(outer), ['3/4']);        /* 69 px below, split in two: 34 */
-  assert.deepEqual(disabled(inner), ['1/4', '3/4']); /* 139 px: 34 at a quarter */
+  assert.deepEqual(disabled(outer), []);             /* a quarter, 69 px, halves into 34 */
+  assert.deepEqual(disabled(inner), ['1/4', '3/4']); /* 69 px: 17 at a quarter */
   const splitButtons = below(main).filter((e) => e.tag === 'button' && /^Split into/.test(text(e)));
-  assert.deepEqual(splitButtons.map((b) => b.disabled), [false, false, true, false, true, false]); /* 400×69 */
+  assert.deepEqual(splitButtons.map((b) => b.disabled), [true, false, true, false, false, false]); /* 400×34 */
+});
+
+/* Eight columns of halves, a row of cells 50 or 49 px wide (M6c). */
+const eightColumns = () => {
+  const half = (a, b) => ({ split: 'columns', ratio: '1/2', line: true, a, b });
+  const four = () => half(half({}, {}), half({}, {}));
+  return half(four(), four());
+};
+
+test('a cell under 90 px wide is XS', async () => {
+  const { ctx, main } = await load(splitDevice([], { split: 'columns', ratio: '1/4', line: true,
+    a: { split: 'columns', ratio: '1/2', line: true, a: { field: 'env.temp' }, b: {} }, b: {} }));
+  await ctx.presetsPage();
+  assert.deepEqual(cellLabels(main), ['1 · 50×279 · XS', '2 · 49×279 · XS', '3 · 299×279 · L']);
+});
+
+test('a tree of 24 cells splits no further', async () => {
+  const rows = { split: 'rows', ratio: '1/3', line: true, a: eightColumns(),
+                 b: { split: 'rows', ratio: '1/2', line: true, a: eightColumns(), b: eightColumns() } };
+  const { ctx, main } = await load(splitDevice([], rows));
+  await ctx.presetsPage();
+  assert.equal(cellLabels(main).length, 24);
+  assert.equal(cellLabels(main)[23], '24 · 49×92 · XS');
+  const splitButtons = below(main).filter((e) => e.tag === 'button' && /^Split into/.test(text(e)));
+  assert.equal(splitButtons.length, 48);
+  assert.ok(splitButtons.every((b) => b.disabled));
 });
 
 test('a cell offers only the fields that fit it', async () => {
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host -- -k 0 2>&1 | grep 'error:' | sed -E 's/.*test\/host\/([a-z_]+\.c):[0-9:]+ error: /\1: /' | sort | uniq -c; ./build-host/test_ui_catalog | grep FAIL; ./build-host/test_ui_widget_fit | grep FAIL`
Expected: three test files don't compile, and two tests fail:

```
   1 test_storage_backup.c: use of undeclared identifier 'BACKUP_MAX_DEPTH'
   1 test_ui_preset.c: array index 13 is past the end of the array (that has type 'uint8_t[8]' (aka 'unsigned char[8]')) [-Werror,-Warray-bounds]
   2 test_ui_preset.c: excess elements in array initializer [-Werror,-Wexcess-initializers]
   3 test_ui_split.c: array index 23 is past the end of the array (that has type 'gfx_rect_t[8]') [-Werror,-Warray-bounds]
   1 test_ui_split.c: array index 8 is past the end of the array (that has type 'gfx_rect_t[8]') [-Werror,-Warray-bounds]
   1 test_ui_split.c: excess elements in array initializer [-Werror,-Wexcess-initializers]
…/test/host/test_ui_catalog.c:114:test_layouts_publish_the_split_rules:FAIL: Expected 24 Was 8
FAIL
…/test/host/test_ui_widget_fit.c:318:test_every_field_fits_every_cell_a_split_can_make:FAIL: Expected 2451 Was 336
FAIL
```

The page's tests already pass (`node --test test/web/test_app.mjs`: `# pass 36`, `# fail 0`): the editor computes every limit from the catalogue, so these pin it to the device's new rules.

- [ ] **Step 3: The limits.**

`components/ui/include/ui_split.h`:

```diff
--- a/components/ui/include/ui_split.h
+++ b/components/ui/include/ui_split.h
@@ -9,15 +9,15 @@
 
 /*
  * The split layout (spec §5.2, D31): the area under the status bar split into rows or columns, each
- * part split again, at most 8 cells. A tree is kept in preorder, a byte a node: 0 is a cell; a split
- * has its ratio in the low bits, and flags for columns and a hidden separator. The cells' fields are
- * the preset's slots, in the order the cells come. Pure C, host-buildable.
+ * part split again, at most 24 cells (M6c, D34; 8 before). A tree is kept in preorder, a byte a node:
+ * 0 is a cell; a split has its ratio in the low bits, and flags for columns and a hidden separator.
+ * The cells' fields are the preset's slots, in the order the cells come. Pure C, host-buildable.
  */
 
-#define UI_SPLIT_CELLS 8
+#define UI_SPLIT_CELLS 24
 #define UI_SPLIT_NODES (2 * UI_SPLIT_CELLS - 1)
-#define UI_SPLIT_MIN_W 90     /* no part is smaller (spec §5.2) */
-#define UI_SPLIT_MIN_H 40
+#define UI_SPLIT_MIN_W 40     /* no part is smaller (spec §5.2; M6c, D34: 90×40 before) */
+#define UI_SPLIT_MIN_H 20
 #define UI_SPLIT_NARROW_W 150 /* narrower: a kind's narrow height; S stacks from 80 px tall (D34) */
 #define UI_SPLIT_INSET 8      /* a separator stops this short of each end */
 
@@ -49,7 +49,7 @@ typedef struct {
 /* What a split preset divides: everything under the status bar and its line (400×279). */
 gfx_rect_t ui_split_area(void);
 /* Lays the tree out over `area`. False if it is cut short, has a node it doesn't know, or has a
- * part under 90×40; *out is then unspecified. */
+ * part under 40×20; *out is then unspecified. */
 bool ui_split_layout(const uint8_t tree[UI_SPLIT_NODES], gfx_rect_t area, ui_split_geometry_t *out);
 /* How many of the tree's nodes it uses: 1 to UI_SPLIT_NODES, or 0 if it is cut short. */
 int ui_split_nodes(const uint8_t tree[UI_SPLIT_NODES]);
```


`components/ui/include/ui_layout.h`:

```diff
--- a/components/ui/include/ui_layout.h
+++ b/components/ui/include/ui_layout.h
@@ -8,7 +8,7 @@
 /* Layouts (spec §5.2): fixed slot rectangles below a 20 px status bar. Pure C, host-buildable. */
 
 #define UI_STATUS_H 20
-#define UI_SLOT_MAX 8 /* the split layout's cells (ui_split.h); the fixed layouts use up to 6 */
+#define UI_SLOT_MAX 24 /* the split layout's cells (ui_split.h, M6c); the fixed layouts use up to 6 */
 
 typedef enum {
     UI_LAYOUT_CLASSIC,
```


`components/ui/ui_split.c`:

```diff
--- a/components/ui/ui_split.c
+++ b/components/ui/ui_split.c
@@ -64,7 +64,7 @@ typedef struct {
 } walk_t;
 
 /* One node and everything under it, laid over r. Each call takes a node, so it recurses at most
- * UI_SPLIT_NODES deep. */
+ * UI_SPLIT_NODES deep, and 14 deep in a tree of 40×20 parts (13 splits in a chain at most). */
 static void walk(walk_t *w, gfx_rect_t r)
 {
     if (!w->ok || w->at >= UI_SPLIT_NODES || r.w < UI_SPLIT_MIN_W || r.h < UI_SPLIT_MIN_H) {
```


- [ ] **Step 4: Room for them.** The presets file, its depth and a backup's, the server's requests and replies, and the RTC snapshot, whose layout changes with `UI_SLOT_MAX` and `UI_SPLIT_NODES`. The backup's depth moves to its header, so `app_web.c` can check that each limit leaves room for the next: a backup holds the deepest `presets.json`, and the server passes the deepest backup. The presets' defaults, 2 KB now, move off the app task's stack into PSRAM, as the other preset buffers are:

`components/ui/include/ui_preset.h`:

```diff
--- a/components/ui/include/ui_preset.h
+++ b/components/ui/include/ui_preset.h
@@ -19,8 +19,8 @@
 #define UI_CYCLE_MIN_S 10
 #define UI_CYCLE_MAX_S 3600
 #define UI_SCHEDULE_MAX 8
-#define UI_PRESETS_JSON_MAX 20480 /* presets.json at its largest is 16 253 bytes (test_ui_preset.c) */
-#define UI_JSON_MAX_DEPTH 16      /* presets.json nests 11 levels at most (a split tree's chain of 7 splits),
+#define UI_PRESETS_JSON_MAX 49152 /* presets.json at its largest: 24 cells a preset (test_ui_preset.c, M6c) */
+#define UI_JSON_MAX_DEPTH 20      /* presets.json nests 17 levels at most (a split tree's chain of 13 splits),
                                      settings.json 5; anything deeper is rejected unparsed */
 
 typedef enum {
```


`components/storage/include/storage_backup.h`:

```diff
--- a/components/storage/include/storage_backup.h
+++ b/components/storage/include/storage_backup.h
@@ -11,6 +11,7 @@
  */
 
 #define BACKUP_FORMAT 1
+#define BACKUP_MAX_DEPTH 22 /* a file's own 20 levels (presets.json's UI_JSON_MAX_DEPTH), inside the bundle's two */
 
 typedef struct {
     const char *name; /* "settings.json": a plain file name in /cfg */
```


`components/storage/storage_backup.c`:

```diff
--- a/components/storage/storage_backup.c
+++ b/components/storage/storage_backup.c
@@ -8,8 +8,6 @@
 #include "cJSON.h"
 #include "util_json.h"
 
-#define BACKUP_MAX_DEPTH 18 /* a file's own 16 levels, inside the bundle's two */
-
 static int fail(char *err, size_t size, const char *fmt, ...)
 {
     if (size > 0) {
```


`components/webui/include/webui.h`:

```diff
--- a/components/webui/include/webui.h
+++ b/components/webui/include/webui.h
@@ -41,11 +41,11 @@ typedef struct {
     void (*event)(webui_event_t event);
 } webui_config_t;
 
-#define WEBUI_BODY_MAX  (24 * 1024) /* the largest request: a backup to restore, its presets up to 16 KB */
-#define WEBUI_REPLY_MAX (24 * 1024) /* the largest reply: a backup, or a BMP (15 662 bytes) */
-/* The deepest request: a backup bundle, a file's own 16 levels inside the bundle's two
- * (storage_backup.c). Deeper ones are refused before anything parses them. */
-#define WEBUI_JSON_MAX_DEPTH 18
+#define WEBUI_BODY_MAX  (64 * 1024) /* the largest request: a backup to restore, its presets up to 48 KB (M6c) */
+#define WEBUI_REPLY_MAX (64 * 1024) /* the largest reply: a backup, or a BMP (15 662 bytes) */
+/* The deepest request: a backup bundle, a file's own 20 levels inside the bundle's two
+ * (BACKUP_MAX_DEPTH, storage_backup.h). Deeper ones are refused before anything parses them. */
+#define WEBUI_JSON_MAX_DEPTH 22
 
 esp_err_t webui_start(const webui_config_t *config);
 void webui_stop(void);
```


`main/app_web.c`:

```diff
--- a/main/app_web.c
+++ b/main/app_web.c
@@ -360,8 +360,10 @@ static void learn(const char *body, uint8_t *out, size_t size, webui_reply_t *re
 }
 
 /* A backup of the largest files restores: settings.json up to the 2 KB app_ui.c keeps, presets.json up to
- * its own limit, and the bundle around them. */
+ * its own limit, and the bundle around them; and of the deepest, as each limit leaves room for the next. */
 _Static_assert(2048 + UI_PRESETS_JSON_MAX + 512 <= WEBUI_BODY_MAX, "a backup of the largest files fits a request");
+_Static_assert(UI_JSON_MAX_DEPTH + 2 <= BACKUP_MAX_DEPTH, "a backup holds the deepest presets.json");
+_Static_assert(BACKUP_MAX_DEPTH <= WEBUI_JSON_MAX_DEPTH, "the web server passes the deepest backup");
 
 /* GET /api/backup (spec §14.4): the /cfg files as the firmware would save them now. */
 static void backup(uint8_t *out, size_t size, webui_reply_t *reply)
```


`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -110,7 +110,7 @@ void app_ui_load(void)
 {
     settings_t settings_defaults;
     default_settings(&settings_defaults);
-    ui_presets_t presets_defaults;
+    EXT_RAM_BSS_ATTR static ui_presets_t presets_defaults; /* 2 KB with 24 cells (M6c): not on the app task's stack */
     ui_presets_defaults(&presets_defaults);
     s.settings = settings_defaults;
     s.presets = presets_defaults;
```


`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -49,7 +49,8 @@
 #define TETHER_RECHECK_MS 1000
 #define RETRY_S           300  /* after a failed boot with no PC attached */
 #define SNAP_MAGIC        0x72666c62u /* "rflb" */
-#define SNAP_VERSION      8 /* 6: the weather, the air quality and the syncs' state; 7: the rain; 8: split presets */
+#define SNAP_VERSION      9 /* 6: the weather, the air quality and the syncs' state; 7: the rain; 8: split presets;
+                                  9: 24 cells (M6c) */
 #define PEEK_MS           60000 /* a button during the night shows the dashboard this long (spec §9.1) */
 #define NIGHT_RECHECK_S   60    /* a night sleep with a button held looks again this often (D16) */
 #define CRITICAL_RECHECK_S 600  /* the critical sleep checks again this often if KEY is held */
@@ -95,7 +96,7 @@ typedef struct {
     app_ui_state_t ui;
     time_t next_alarm;
 } app_snapshot_t;
-_Static_assert(sizeof(app_snapshot_t) <= 4096, "the RTC-RAM snapshot is at most 4 KB (spec §6)");
+_Static_assert(sizeof(app_snapshot_t) <= 6144, "the RTC-RAM snapshot is at most 6 KB (spec §6, M6c)");
 
 static RTC_DATA_ATTR app_snapshot_t s_snap;
 static QueueHandle_t s_queue;
```


- [ ] **Step 5: A word's tails in a tall narrow M cell.** Cells of 149 × 116, which a tree couldn't make before, showed the current weather's word with its descenders in the cell's bottom 2 px:

`components/ui/ui_forecast.c`:

```diff
--- a/components/ui/ui_forecast.c
+++ b/components/ui/ui_forecast.c
@@ -769,7 +769,7 @@ static void draw_weather_now(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const u
         int vw = value_unit_width(&gfx_font_sans_bold_28, &gfx_font_sans_16, v->text, v->unit);
         int base = top + 48 + 6 + ink_height(&gfx_font_sans_bold_28);
         value_unit(fb, &gfx_font_sans_bold_28, &gfx_font_sans_16, r.x + (r.w - vw) / 2, base, v->text, v->unit);
-        if (base + 20 <= r.y + r.h) {
+        if (base + 20 + ui_ink_below(&gfx_font_sans_16, v->extra) <= r.y + r.h - 2) { /* its tails 2 px clear */
             centred(fb, &gfx_font_sans_16, r, base + 20, v->extra);
         }
         return;
```


- [ ] **Step 6: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_ui_split && ./build-host/test_ui_preset && ./build-host/test_ui_widget_fit && ./build-host/test_storage_backup && ctest --test-dir build-host && node --test test/web/test_app.mjs`
Expected: `14 Tests 0 Failures`; `29 Tests 0 Failures`, with `INFO: the largest presets.json: 37197 bytes`; `14 Tests 0 Failures`; `5 Tests 0 Failures`; `100% tests passed, 0 tests failed out of 61`; `# pass 36`, `# fail 0`.

- [ ] **Step 7: The firmware builds, and the snapshot fits.** `tools/idf.sh build`: clean, without a warning; the static asserts in `app.c` and `app_web.c` hold. The app grows by 864 bytes, to 0x25f870 bytes (41 % of its partition free). The link map (`build/reflbo.map`): `.rtc.data` holds `app.c`'s snapshot, 4 664 bytes (0x1238), and `power.c`'s 120; `_rtc_slow_length` 0x12d0, 4 816 of RTC slow memory's 8 192 bytes.

- [ ] **Step 8: Commit.**

```bash
git add components main test
git commit -m "feat(ui): split presets of 24 cells down to 40×20"
```

### Task 4: Goldens of small cells (`test`)

**Files:**
- Modify: `test/host/dashboard_fixtures.h`
- Create (binary, from `plan/m6c`): `test/host/golden/dash_split_compact.pbm`, `dash_split_compact_cs.pbm`, `dash_split_xs_rows.pbm`, `dash_split_xs_grid.pbm`, `dash_split_xs_narrow.pbm`

**Interfaces:**
- Consumes: everything of Tasks 1–3.
- Produces (Task 5): the fixtures `split_compact`, `split_compact_cs`, `split_xs_rows`, `split_xs_grid`, `split_xs_narrow` in `k_dashboard_fixtures`; the helpers `fixture_cols()`, `fixture_rows()`, `fixture_grid_split()` and the 24 small fields `k_fixture_small24`.

The spike's approved renders (the plan's header) drew these cells with M6d's solar fields too; here they hold M6c's fields only.

- [ ] **Step 1: The fixtures.** Rows of cells made of halves and thirds, and five presets: 12 cells of 133 × 69 (the owner's size) in English and Czech; 24 of 200 × 22 without lines, the status bar's size; 24 of 66 × 69; 24 of 49 × 92.

`test/host/dashboard_fixtures.h`:

```diff
--- a/test/host/dashboard_fixtures.h
+++ b/test/host/dashboard_fixtures.h
@@ -36,6 +36,49 @@ static inline void fixture_split(ui_preset_t *p, const uint8_t *tree, size_t nod
     memcpy(p->slots, fields, cells);
 }
 
+/* A row of `n` cells (M6c): halves where n is even, a third and the rest where it is odd; `line` is 0 or
+ * UI_SPLIT_NO_LINE. Returns the next node. */
+static inline int fixture_cols(uint8_t *t, int at, int n, uint8_t line)
+{
+    if (n == 1) {
+        t[at++] = 0;
+        return at;
+    }
+    t[at++] = (uint8_t)((n % 2 == 0 ? UI_RATIO_1_2 : UI_RATIO_1_3) | UI_SPLIT_COLUMNS | line);
+    at = fixture_cols(t, at, n % 2 == 0 ? n / 2 : 1, line);
+    return fixture_cols(t, at, n % 2 == 0 ? n / 2 : n - 1, line);
+}
+
+/* `rows` rows of `cols` cells, split the same way. */
+static inline int fixture_rows(uint8_t *t, int at, int rows, int cols, uint8_t line)
+{
+    if (rows == 1) {
+        return fixture_cols(t, at, cols, line);
+    }
+    t[at++] = (uint8_t)((rows % 2 == 0 ? UI_RATIO_1_2 : UI_RATIO_1_3) | line);
+    at = fixture_rows(t, at, rows % 2 == 0 ? rows / 2 : 1, cols, line);
+    return fixture_rows(t, at, rows % 2 == 0 ? rows / 2 : rows - 1, cols, line);
+}
+
+/* A split preset of rows × cols cells (M6c), the fields in reading order. */
+static inline void fixture_grid_split(ui_preset_t *p, int rows, int cols, uint8_t line, const uint8_t *fields,
+                                      size_t cells)
+{
+    uint8_t tree[UI_SPLIT_NODES];
+    memset(tree, 0, sizeof(tree));
+    int n = fixture_rows(tree, 0, rows, cols, line);
+    fixture_split(p, tree, (size_t)n, fields, cells);
+}
+
+/* The 24 small fields of M6c's goldens, in the order the cells take them. */
+static const uint8_t k_fixture_small24[24] = {
+    UI_FIELD_TIME_CLOCK,    UI_FIELD_DATE_DAY,     UI_FIELD_ENV_TEMP,      UI_FIELD_ENV_HUM,    UI_FIELD_WX_NOW,
+    UI_FIELD_WX_TODAY,      UI_FIELD_SUN_TIMES,    UI_FIELD_AQ_INDEX,      UI_FIELD_AQ_UV,      UI_FIELD_POLLEN_TOP,
+    UI_FIELD_MOON_PHASE,    UI_FIELD_BAT_LEVEL,    UI_FIELD_DATE_WEEK,     UI_FIELD_BAT_DAYS,   UI_FIELD_ENV_DEW,
+    UI_FIELD_AQ_PM25,       UI_FIELD_AQ_PM10,      UI_FIELD_ENV_TEMP_MIN,  UI_FIELD_ENV_TEMP_MAX, UI_FIELD_POLLEN_GRASS,
+    UI_FIELD_POLLEN_BIRCH,  UI_FIELD_POLLEN_ALDER, UI_FIELD_POLLEN_MUGWORT, UI_FIELD_POLLEN_RAGWEED,
+};
+
 /* Each fixture: a context and a preset. */
 static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_preset_t *preset)
 {
@@ -278,6 +321,29 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
     } else if (strcmp(name, "home_temp_main_cs") == 0) { /* in Czech the decimals give way to the comma's tail */
         fixture_dashboard("home_temp_main", ctx, preset);
         ctx->lang = lang_get("cs");
+    } else if (strcmp(name, "split_compact") == 0) { /* M6c (D34): 4 × 3 cells of 133 × 69, S beside */
+        *preset = fixture_preset("weather");
+        static const uint8_t k_fields[12] = { UI_FIELD_TIME_CLOCK, UI_FIELD_DATE_DAY,   UI_FIELD_WX_NOW,
+                                              UI_FIELD_ENV_TEMP,   UI_FIELD_ENV_HUM,    UI_FIELD_WX_TODAY,
+                                              UI_FIELD_SUN_TIMES,  UI_FIELD_AQ_INDEX,   UI_FIELD_POLLEN_TOP,
+                                              UI_FIELD_AQ_UV,      UI_FIELD_BAT_LEVEL,  UI_FIELD_MOON_PHASE };
+        fixture_grid_split(preset, 4, 3, 0, k_fields, sizeof(k_fields));
+        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
+    } else if (strcmp(name, "split_compact_cs") == 0) { /* the same in Czech: a word under its number */
+        fixture_dashboard("split_compact", ctx, preset);
+        ctx->lang = lang_get("cs");
+    } else if (strcmp(name, "split_xs_rows") == 0) { /* 12 × 2 rows of 200 × 22: the status bar's size */
+        *preset = fixture_preset("weather");
+        fixture_grid_split(preset, 12, 2, UI_SPLIT_NO_LINE, k_fixture_small24, sizeof(k_fixture_small24));
+        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
+    } else if (strcmp(name, "split_xs_grid") == 0) { /* 4 × 6 cells of 66 × 69: XS, the symbol over the value */
+        *preset = fixture_preset("weather");
+        fixture_grid_split(preset, 4, 6, 0, k_fixture_small24, sizeof(k_fixture_small24));
+        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
+    } else if (strcmp(name, "split_xs_narrow") == 0) { /* 3 × 8 cells of 49 × 92: units under the numbers */
+        *preset = fixture_preset("weather");
+        fixture_grid_split(preset, 3, 8, 0, k_fixture_small24, sizeof(k_fixture_small24));
+        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
     } else if (strcmp(name, "grid_clock_12h") == 0) { /* a clock in a grid cell, 12-hour */
         *preset = fixture_preset("indoor");
         preset->slots[0] = UI_FIELD_TIME_CLOCK;
@@ -305,4 +371,6 @@ static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather",
                                                     "flights", "flights_100", "flights_cs", "flights_none",
                                                     "flights_failed", "flights_off", "split_weather",
                                                     "split_eight", "home_temp_main", "home_temp_main_cs",
-                                                    "weather_frost_cs", "weather_hot_f" };
+                                                    "weather_frost_cs", "weather_hot_f", "split_compact",
+                                                    "split_compact_cs", "split_xs_rows", "split_xs_grid",
+                                                    "split_xs_narrow" };
```


- [ ] **Step 2: Run the golden test to see it fail.**

Run: `cmake --build build-host && ./build-host/test_ui_dashboard_golden`
Expected: `test_every_fixture_matches_its_golden:FAIL: …/test/host/golden/dash_split_compact.pbm`, the first of the five goldens that don't exist yet.

- [ ] **Step 3: The goldens.** Copy them from the branch, then render each again; they must match byte for byte:

```bash
git checkout plan/m6c -- \
  test/host/golden/dash_split_compact.pbm \
  test/host/golden/dash_split_compact_cs.pbm \
  test/host/golden/dash_split_xs_grid.pbm \
  test/host/golden/dash_split_xs_narrow.pbm \
  test/host/golden/dash_split_xs_rows.pbm
```


```bash
for n in split_compact split_compact_cs split_xs_rows split_xs_grid split_xs_narrow; do
  ./build-host/render_dashboard $n /tmp/$n.pbm && cmp /tmp/$n.pbm test/host/golden/dash_$n.pbm
done
```

Expected: no output. Look at them: `python3 tools/render.py`, then `captures/render/dash_split_compact.png` and the four others, as the spike's review page showed them.

- [ ] **Step 4: Run the tests.**

Run: `ctest --test-dir build-host`
Expected: `100% tests passed, 0 tests failed out of 61`.

- [ ] **Step 5: Commit.**

```bash
git add test/host/dashboard_fixtures.h test/host/golden
git commit -m "test(ui): goldens of small split cells"
```

### Task 5: On the board, and the docs as built

**Files:**
- Modify (only if the checks find something): whatever they point at, each fix with its own test where one can fail first.
- Modify: `docs/specs/2026-09-25-firmware-design.md` (r36, as built), `AGENTS.md`, `README.md`, `docs/guide.md`, `tools/docs_images.py`, `docs/images/panel/` (generated)

**Needs the owner first:** the board plugged into this Mac, and a choice. These checks flash the board and use config mode over the device's own network; they set no clock, start no sync on demand and don't power the board off, so M5's RTC-trim run (memory `m5-deferred-owner-checks`: 2026-10-03 08:00 set the reference, 10-04 08:00 measured and trimmed, 10-05 08:00 shows the drift left) should go on undisturbed. The owner may still want them after the 2026-10-05 08:00 sync. The board has the home network saved and no web password: Step 2 sets a temporary one over the device's own network and Step 7 clears it again, so the owner's first visit still chooses theirs. Ask, and wait.

Before anything else, confirm the port is this board (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'` shows `14:C1:9F:54:BB:94`), and note what the checks may change, to compare in Step 7: `tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "sync status" -o captures/m6c-before.log`.

- [ ] **Step 1: Flash and boot.**

```bash
tools/idf.sh -p /dev/cu.usbmodemXXXX flash 2>&1 | grep -E 'MAC:|Hash of data verified'
tools/idf.sh exec python tools/devlog.py --cmd reboot --until "reflbo ready" -t 30 -o captures/m6c-boot.log
grep -E 'presets.json|settings.json|E \(' captures/m6c-boot.log
tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "sync status"
```

Expected: `MAC: 14:c1:9f:54:bb:94`; no `E (` line and nothing about the files being invalid (the owner's `presets.json` and `settings.json` from M6b parse as they are); `preset list` and `sync status` as in `captures/m6c-before.log`. The boot is cold: a version-8 snapshot is never read (gotcha 29). (If the board sits in download mode after flashing, leave it as gotcha 22 says.)

- [ ] **Step 2: Config mode and a backup.** Join the device's network (`btn boot long`; the password is on the screen; `networksetup -setairportnetwork en0 reflbo-bb94 <password>`), set a temporary web password, log in and save the board's configuration:

```bash
PW=$(openssl rand -hex 8) # a throwaway, never written down; Step 7 clears it
curl -s -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/setup
curl -s -c jar -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/login
curl -s -b jar http://192.168.4.1/api/backup > captures/m6c-backup.json
curl -s -b jar http://192.168.4.1/api/presets > captures/m6c-presets-before.json
curl -s -b jar http://192.168.4.1/api/layouts | python3 -c 'import json,sys; s=json.load(sys.stdin)["split"]; print(s["cells"], s["min_w"], s["min_h"], [z["size"] for z in s["sizes"]])'
```

Expected: `24 40 20 ['XL', 'L', 'M', 'S', 'XS']`; `captures/m6c-backup.json` holds `settings.json` and `presets.json`.

- [ ] **Step 3: Small cells over the web API.** Add two presets, spec §5.2's small cells as Task 4's goldens have them: 12 cells of 133×69 and 24 of 49×92, the second active.

```bash
python3 - <<'EOF'
import json
doc = json.load(open('captures/m6c-presets-before.json'))
def cols(n, cells):
    if n == 1:
        return cells.pop(0)
    k = 2 if n % 2 == 0 else 3
    a, b = (n // 2, n // 2) if k == 2 else (1, n - 1)
    return {"split": "columns", "ratio": "1/2" if k == 2 else "1/3", "line": True, "a": cols(a, cells), "b": cols(b, cells)}
def rows(r, c, cells):
    if r == 1:
        return cols(c, cells)
    k = 2 if r % 2 == 0 else 3
    a, b = (r // 2, r // 2) if k == 2 else (1, r - 1)
    return {"split": "rows", "ratio": "1/2" if k == 2 else "1/3", "line": True, "a": rows(a, c, cells), "b": rows(b, c, cells)}
small24 = ["time.clock", "date.day", "env.temp", "env.hum", "wx.now", "wx.today", "sun.times", "aq.index", "aq.uv",
           "pollen.top", "moon.phase", "bat.level", "date.week", "bat.days", "env.dew", "aq.pm25", "aq.pm10",
           "env.temp_min", "env.temp_max", "pollen.grass", "pollen.birch", "pollen.alder", "pollen.mugwort",
           "pollen.ragweed"]
compact = ["time.clock", "date.day", "wx.now", "env.temp", "env.hum", "wx.today", "sun.times", "aq.index",
           "pollen.top", "aq.uv", "bat.level", "moon.phase"]
doc["presets"].append({"id": "m6c12", "name": "Twelve cells", "layout": "split", "in_cycle": False,
                       "split": rows(4, 3, [{"field": f} for f in compact]), "options": {"status_clock": True}})
doc["presets"].append({"id": "m6c24", "name": "Small cells", "layout": "split", "in_cycle": False,
                       "split": rows(3, 8, [{"field": f} for f in small24]), "options": {"status_clock": True}})
doc["active"] = "m6c24"
json.dump(doc, open('captures/m6c-presets-small.json', 'w'))
EOF
curl -s -b jar -X PUT -H 'Content-Type: application/json' --data-binary @captures/m6c-presets-small.json \
  http://192.168.4.1/api/presets | python3 -c 'import json,sys; print([p["id"] for p in json.load(sys.stdin)["presets"]])'
curl -s -b jar -o captures/m6c-preview24.bmp 'http://192.168.4.1/api/preview.bmp?preset=m6c24'
curl -s -b jar -o captures/m6c-preview12.bmp 'http://192.168.4.1/api/preview.bmp?preset=m6c12'
tools/idf.sh exec python tools/screenshot.py -o captures/m6c-small24.png
```

Expected: the saved list ends with `m6c12`, `m6c24`; the preview BMPs and the panel's screenshot show the cells as `dash_split_xs_narrow` and `dash_split_compact` do, with today's values. Every cell shows something, and nothing touches its edges; check the screenshot's PBM against the tree's geometry:

```bash
python3 - <<'EOF'
data = open('captures/m6c-small24.pbm', 'rb').read()
w, h = 400, 300
bits = data[-w * h // 8:]
ink = lambda x, y: bits[y * (w // 8) + x // 8] >> (7 - x % 8) & 1
xs = [(0, 50), (51, 49), (101, 49), (151, 49), (201, 49), (251, 49), (301, 49), (351, 49)]
ys = [(21, 93), (115, 92), (208, 92)]
for y0, ch in ys:
    for x0, cw in xs:
        assert any(ink(x, y) for x in range(x0 + 2, x0 + cw - 2) for y in range(y0 + 2, y0 + ch - 2)), (x0, y0)
        band = [(x, y) for x in range(x0, x0 + cw) for y in (y0, y0 + 1, y0 + ch - 2, y0 + ch - 1)]
        band += [(x, y) for y in range(y0, y0 + ch) for x in (x0, x0 + 1, x0 + cw - 2, x0 + cw - 1)]
        assert not any(ink(x, y) for x, y in band), (x0, y0)
print('24 cells, each inked and clear of its edges')
EOF
```

Expected: `24 cells, each inked and clear of its edges`. Then `preset set m6c12` and a screenshot: the twelve cells as `dash_split_compact` has them.

- [ ] **Step 4: The limits.** The largest file, the deepest tree, and the refusals:

```bash
python3 - <<'EOF'
import json
doc = json.load(open('captures/m6c-presets-small.json'))
deep = {"field": "env.temp"}
for i, (kind, ratio) in enumerate([("columns", "1/2"), ("columns", "1/3"), ("columns", "1/4"), ("columns", "1/4"),
                                   ("columns", "1/4"), ("columns", "1/4"), ("rows", "1/2"), ("rows", "1/3"),
                                   ("rows", "1/4"), ("rows", "1/4"), ("rows", "1/4"), ("rows", "1/4"), ("rows", "1/4")]):
    deep = {"split": kind, "ratio": ratio, "line": True, "a": {}, "b": deep}
doc["presets"].append({"id": "m6cdeep", "name": "Deep", "layout": "split", "in_cycle": False, "split": deep})
json.dump(doc, open('captures/m6c-presets-deep.json', 'w'))
big = json.load(open('captures/m6c-presets-small.json'))
tree = big["presets"][-1]["split"]
big["presets"] = [dict(big["presets"][-1], id=f"big-{i:02d}", name="\u0001" * 23) for i in range(16)]
big["active"] = "big-00"
json.dump(big, open('captures/m6c-presets-big.json', 'w'))
bad = json.load(open('captures/m6c-presets-small.json'))
bad["presets"][-1]["split"]["a"]["a"]["a"]["a"] = {"split": "rows", "ratio": "1/2", "a": {}, "b": {}}  # 25 cells
json.dump(bad, open('captures/m6c-presets-25.json', 'w'))
EOF
wc -c captures/m6c-presets-big.json
for f in deep big 25; do
  curl -s -w ' %{http_code}\n' -b jar -X PUT -H 'Content-Type: application/json' \
    --data-binary @captures/m6c-presets-$f.json http://192.168.4.1/api/presets | tail -c 120
done
curl -s -b jar http://192.168.4.1/api/backup > captures/m6c-backup-big.json && wc -c captures/m6c-backup-big.json
curl -s -w ' %{http_code}\n' -b jar -H 'Content-Type: application/json' --data-binary @captures/m6c-backup-big.json \
  http://192.168.4.1/api/restore | tail -c 80
```

Expected: the deep and the big documents answer 200 (the deepest tree nests 17 levels; the big file is about 37 KB); the 25-cell one `{"error":"preset \"m6c24\": a split preset has at most 24 cells"} 400`; the backup of the big file, about 40 KB, restores with 200. `tasks` afterwards: the app task keeps at least 2 KB of its 8 KB stack free (M6b: 3 536 bytes).

- [ ] **Step 5: Through deep sleep.** The snapshot, 4 664 bytes at version 9, carries the presets through a deep sleep. Leave config mode (`btn boot long`) with `m6c24` active, then:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "preset set m6c24" --cmd "sleep test deep 2" -t 200 --until "sleep test done" -o captures/m6c-deep.log
tools/idf.sh exec python tools/devlog.py --cmd "sleep stats" --cmd screenshot -o captures/m6c-after-deep.log
```

Expected: two deep cycles with warm wakes in `sleep stats`; the screenshot still the 24 cells. The awake time of a wake that draws them, against the dashboards' (about 60 ms at M6), goes in the spec as built.

- [ ] **Step 6: The editor.** Headless Chrome on `http://192.168.4.1/#presets` (AGENTS §6; config mode again with `btn boot long`; leaving it in Step 5 ended the session (gotcha 42), so log in again, `curl -s -c jar … /api/auth/login` as in Step 2, and give the page the jar's session cookie with `chromium-cookies`), the `m6c24` preset selected: 24 nested boxes under the preview, the last "24 · 49×92 · XS", every Split into rows and Split into columns disabled; `m6c12`'s cells "… · 133×69 · S". A screenshot of each goes to `captures/m6c-editor-*.png` for the owner.

- [ ] **Step 7: Back to the start.** Restore the owner's configuration and compare:

```bash
curl -s -w ' %{http_code}\n' -b jar -H 'Content-Type: application/json' --data-binary @captures/m6c-backup.json \
  http://192.168.4.1/api/restore
curl -s -b jar http://192.168.4.1/api/backup > captures/m6c-backup-after.json
python3 -c 'import json; a=json.load(open("captures/m6c-backup.json"))["files"]; b=json.load(open("captures/m6c-backup-after.json"))["files"]; print(a == b)'
```

Expected: `200`; `True`. Then clear the temporary web password: Menu ▸ Wi-Fi ▸ Reset web password (`btn key long` opens the menu; `btn key short` steps to the next item and `btn key long` enters it; the confirmation asks for KEY held; a screenshot after each press shows where the menu is). Last, `networksetup -removepreferredwirelessnetwork en0 reflbo-bb94` and `rm jar captures/m6c-presets-*.json captures/m6c-backup*.json`. `preset list` and `sync status` as in `captures/m6c-before.log`.

- [ ] **Step 8: Write down what was built.**
  - Spec r36: §4.5 (the 16 px icons), §5.2 as built (the measured S heights from the plan's research; 2451 cell sizes), §5.3 as built (what gives way before anything is cut, in XS and in S beside the value, as the plan's rulings say; the date's day form, pollen's bar, the sun's three forms, polar day and night, centring; S in a short cell: the 24 px sky under 120×52, the air quality's index in a smaller face past 100 and its word under it or the bar alone, today's weather in smaller faces, no age mark under 80 px), §5.4 (the largest file, 37 197 bytes; 48 KB), §6 (the snapshot: 4 664 bytes, version 9; RTC slow memory 4 816 of 8 192 bytes), §10.3 (requests and replies up to 64 KB, 22 levels), §12.10 (`stable-m6b` reads presets of at most 8 cells: after flashing it back, restore the backup taken before the test, as the protocol does), §17 (the tests as built: fifteen sets of data), §18 (M6c built), §20 (anything Steps 1–7 found, the review's deferred minor, the M6b minors fixed and those left), §21.
  - `tools/docs_images.py`: `"split-small": "dash_split_xs_grid"` and `"split-compact": "dash_split_compact"` in `SINGLE`; run it.
  - `docs/guide.md`, "Your own layout": "up to 24 cells of at least 40×20 pixels"; a paragraph on the small sizes (a cell under 90 px wide or 40 px tall draws its field in one line like the status bar, or its icon over its value; a short cell puts the icon beside the value) with the two new images; "Coming next" loses M6c.
  - `README.md`: the roadmap's M6c row "Built".
  - `AGENTS.md`: the status (M6c built, its owner checks), the roadmap row, §6's stable-firmware note (`stable-m6b` refuses presets of more than 8 cells; restore the backup), and any gotcha the checks found.

- [ ] **Step 9: Commit and push.**

```bash
git add docs AGENTS.md README.md tools/docs_images.py
git commit -m "docs: record M6c as built (spec r36)"
git push origin main
```

## Owner acceptance

M6c is done (spec §18) once the owner has checked, with the expected results:

1. **The goldens** (`python3 tools/render.py`, `captures/render/dash_split_compact.png`, `dash_split_compact_cs.png`, `dash_split_xs_rows.png`, `dash_split_xs_grid.png`, `dash_split_xs_narrow.png`): as approved in the spike's renders, or with the changes asked for.
2. **A small-cell preset built in the web editor** on the phone (down to 40×20, or the 24 cells of Step 3): saved, it shows on the panel as the preview shows it.
3. **XS on the panel:** the 24-cell presets of Step 3 (49×92) and rows of 200×22 read at a desk's distance; a cell that doesn't goes in the spec's §20 with the owner's words.

Still open from earlier milestones, offered when the owner is at the board: M6b's acceptance (memory `m6b-design`), M6's rainy-day ČHMÚ frame and its power measurements (memory `m6-acceptance-open`), M5's trim result, sync on battery and router-off checks (memory `m5-deferred-owner-checks`), M3's night-sleep current and night peek, M4's update from the page and first run.
