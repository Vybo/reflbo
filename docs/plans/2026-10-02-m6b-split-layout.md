# M6b: The Split Layout and BOOT Double for `always`, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** M6b (spec §18, D31): a split layout beside the fixed ones, whose area the preset's own tree divides into rows and columns at 1/4, 1/3, 1/2, 2/3 or 3/4, up to 8 cells, each holding one field at the size the cell allows, each split's separator shown or hidden, built in the web editor with the live preview; and BOOT double on the dashboard, which turns sync mode `always` on and back to the mode before.

**Architecture:**

- **Pure logic, host-tested:**
  - `ui`: the split tree in preorder, a byte a node, in each preset (`ui_split.c`): its geometry, each cell's size class, and the least height each field kind needs at each size, measured on host renders; the tree in `presets.json`, validated as the spec says; its drawing (separators, then each cell's field through `ui_draw_cell()`); the rules published in `GET /api/layouts`.
  - `storage`: `sync.mode_before_always` in `settings.json`, and the toggle that uses it.
  - `locale`: the toggle's toasts.
- **Device side:**
  - `main`: BOOT double bound on the dashboard, toggling the mode with its toast (`app_sync_toggle_always()`), but not with the presses that back out of the menu; the RTC snapshot's version 8; the radar's check for presets that draw the rain map; a backup that carries the largest presets file.
  - `webui`: 24 KB requests and replies.
  - `web/`: the tree as nested boxes under the live preview, its cells numbered there and in the preview.

**Tech stack:** ESP-IDF v5.5.5, cJSON (allocating in PSRAM since M5), LittleFS; Unity on the host; Node's test runner for the page. Nothing new from the registry, no new data or services.

**Spec:** `docs/specs/2026-09-25-firmware-design.md` r32 (D31, the owner's approval of 2026-10-02). Task 6 brings it to r33, as built.
- Relevant: §5.2 (the split layout), §5.4 (split presets, validation), §5.6 (controls, BOOT double), §6 (the snapshot), §9.3 (the `always` shortcut), §10.3 (the editor, the API's limits), §14.3 (`sync.mode_before_always`), §17, §18 (M6b); D11, D31.
- Also `AGENTS.md` §3.4 (gotchas 11, 22, 29, 30, 33), §5.3, §6–§8.

**Research behind this plan (2026-10-02):**
- The plan's code was built and tested task by task on the local branch `plan/m6b`, one commit per task (`plan-m6b task N: …`); the plan carries that code. On the host the whole suite passed after every task: 61 ctest targets at the end (60 before, `test_ui_split` new), the page's 34 tests (25 before) and the 67 tool tests among them; the ASan/UBSan build passed at the end. The firmware built clean after every task, without a warning: 2 477 376 bytes (2 470 592 before).
- **Binary files:** the two new goldens come from `plan/m6b`, which is pushed to `origin` with this plan. Task 3 copies them with `git checkout plan/m6b -- <paths>` (in a clone without the branch, `git fetch origin plan/m6b:plan/m6b` first), renders them again, and they must match the branch's byte for byte.
- **What each field needs.** Every field was rendered without clipping into cells of each size at widths from 90 to 400 px and growing heights, in English and in Czech (its decimal comma and longer words), fresh and three hours stale with a forecast two days old; a field fits once no ink falls outside the cell or within 2 px of its edges. Numbers, times and the battery fit their digits to the cell's height as well as its width (Task 3), so they need no more than the spec's minimums: S from 40 px tall when 150 px wide or more, from 80 px when narrower; M from 130 × 80; L from 200 × 150; XL 400 wide from 120 px. These kinds need more:

  | Size | Kind | Narrow (under 150 px) | Wide |
  |---|---|---|---|
  | S | weather now | 86 | 61 |
  | S | the day's weather | 99 | 51 |
  | S | sun times | 80 | 49 |
  | S | air quality, UV | 83 | 40 |
  | S | pollen | 86 | 42 |
  | M | weather now | 98 | 80 |
  | M | the day's weather | 94 | 80 |
  | M | sun times | 94 | 94 |
  | M | air quality, UV | 93 | 93 |
  | M | pollen | 105 | 105 |

  The Weather example's bottom cells (spec §5.2, 69 px) hold every small field; its right cells (199 × 104) hold everything at M but pollen, which draws there at S. Task 3's test then draws every field the table allows in each of the 336 cell sizes a legal tree can make, clipped as the device draws, in eight sets of data: English, Czech, stale, no data, a 12-hour clock, 100.8 °F, frost in Czech, and seconds.
- **Sizes.** The largest `presets.json` is 16 253 bytes (the limit was 8 KB): 16 split presets of 8 cells with the longest field ids, names of 23 control characters (cJSON writes each as six bytes), every option at its longest and 8 schedule entries that switch presets. A backup bundles it with `settings.json` (up to 2 KB). `GET /api/layouts` grows to 4 473 bytes. The RTC snapshot grows from 3 616 to 3 896 bytes of its 4 096: each preset's tree (15 bytes) and two more slots, 16 times, and one byte of settings.
- **The editor** was looked at in headless Chrome against a fake device serving the real catalogue and fields: nested boxes, the split buttons of a 69 px cell disabled, "3 · 199×104 · M (Pollen at S)", the cells' numbers at their corners in the preview.
- **Not checked on the board:** everything the device does is Task 6's.
- **A review of the whole branch** (2026-10-02, opus), before this plan was written, found one important problem and six smaller ones. Their fixes are in the tasks that own the code, so every task carries its fixed version:
  - A number in a full-width cell 120–161 px tall fell from XL to M (48 px digits): XL numbers needed 162 px for a Czech decimal comma's tail, and L needs 150. Numbers, times and the battery now fit their digits to the height too (Task 3), the table loses its XL and battery rows (Task 1), and the same fix ends a cut in a fixed layout: in Classic's `main` slot (400 × 125) the comma's tail of "23,4" was cut at the slot's edge and read "23.4".
  - Smaller ones: `presets.json`'s size test missed the true worst case, 131 bytes under the 16 KB limit (Task 2: the worst case, and 20 KB); BOOT pressed fast to back out of the menu could turn `always` on with the presses after the menu closed, and turning `always` on in quiet hours or a night toasted "Wi-Fi stays on" while Wi-Fi stays off until they end, and the toggle didn't check the critical battery itself (Task 5); a page's or a backup's own `mode_before_always` was overwritten (Task 5); Split into rows or columns dropped a field without a word, and a cell didn't say when its field draws smaller than the cell's class (Task 4); the depth comment in `ui_preset.h` (Task 2). The spec's as-built revision is Task 6's.

**Rulings this plan makes (each costs little if wrong):**
- **Each kind's least height per size, from the renders above** (Task 1): the spec's minimums are each size's own; where a kind's widget needs more, a cell needs that height to draw it at that size, else the field draws at the next size down, or the cell can't hold it. Device validation, drawing, the published rules and the editor all read the one table; `test_ui_widget_fit` draws every field in every cell a tree can make. Cost: a field is offered in fewer cells than the spec's numbers alone would allow.
- **Numbers fit their cell's height as well as its width** (Task 3), in spec §5.3's order: at each font, largest first, the value, then without its decimals. Where a Czech comma's tail is what doesn't fit, the decimals go before the digits shrink: a full-width cell under 163 px shows "23 °C" in Czech where English shows "23.4 °C" (the 130 px digits are 98 px tall, the comma's tail 18 more). Cost: one decimal in such cells; keeping it instead (smaller digits) is a swap of two checks in `fit_number()`.
- **The sun widget steps down its face** (Task 3) when a 12-hour time doesn't fit: "7:01 AM" ran past both edges of a 100 px cell. No golden changes.
- **`presets.json` may reach 20 KB, and the web server takes 24 KB requests and replies** (Task 2), so a backup of the largest files restores (a `_Static_assert` in `app_web.c`). Cost: 4 KB more PSRAM for each of the two presets buffers, 8 KB for the server's.
- **The snapshot's version goes to 8** (Task 2), as for any change to its layout: a snapshot of the old layout is never read (gotcha 29). A flash or an update resets the chip anyway, so the first boot after one is cold.
- **A split preset is written with its tree and without `slots`, and every split with its `line`** (Task 2); a missing `line` reads as shown, as spec §5.4 says.
- **A split preset without a tree is one empty cell** (Task 2), as lenient as missing `slots`; a fixed layout's `split` key is ignored.
- **Cells are counted in preorder** (Tasks 2, 4): the device's errors ("cell 4 (400×69) can't show rain.map"), the editor's boxes and the preview's tags number them alike.
- **A ratio change or a split that leaves a field without room empties its cell, with a toast** (Task 4): spec §10.3 disables only the ratios that make a part too small; this keeps the tree valid without blocking the change.
- **A cell whose field draws smaller than the cell's class says so** (Task 4): "3 · 199×104 · M (Pollen at S)"; spec §10.3 names only the size and class.
- **Every change into `always` remembers the mode it left** (Task 5): the menu's and a page's too, not only BOOT double's, so BOOT double returns to the mode before however `always` came on; a page or a backup that names its own `mode_before_always` keeps it.
- **BOOT double within a second of the menu closing does nothing** (Task 5): BOOT pressed fast to back out of the menu would otherwise turn `always` on with the presses after it closed.
- **The toggle's toasts** (Task 5): "Always on: Wi-Fi stays on" as spec §5.6 says, but "Always on: Wi-Fi from 06:00" in quiet hours or a night, which keep Wi-Fi off until they end; leaving `always`, "Sync: <mode>" in the menu's words ("Sync: At set times", "Synchronizace: V daný čas").
- **No built-in split preset**: the owner builds them in the editor; spec §5.4's built-ins stay.
- **M6's review minors** stay in their memory for M7's planning: none lies in code M6b changes.

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, plain HTML/CSS/JS in `web/` with no build step and no external resources.
- `[host]` code (`ui`, `locale`, `components/storage/settings.c`) includes no ESP-IDF headers. cJSON counts as plain C.
- ESP-IDF style: 4-space indent, `snake_case`, a component or module prefix on public APIs, public headers in `include/`. C lines stay within 120 characters.
- The app task owns the display, storage, the settings, the presets and the syncs' state (spec §3.2, `AGENTS.md` §5.3); the web server reaches them through the app's executor.
- **The RTC snapshot is at most 4 KB** (`_Static_assert` in `main/app.c`); a change to its layout bumps `SNAP_VERSION`.
- **Depth:** `presets.json` nests at most 16 levels (`UI_JSON_MAX_DEPTH`) and a request 18 (`WEBUI_JSON_MAX_DEPTH`); the deepest legal tree, seven splits in a chain, puts its last cell at level 11 of the file and 13 of a backup. Every recursion over a tree takes a node a call: at most 15 frames, on the app task (the API runs there through the app's executor).
- **Secrets.** Never commit or log a Wi-Fi password, the AP password or the web password. The board has no web password now (cleared at the end of M6's checks, with the owner's agreement): a check that sets one over the device's own network clears it again with Menu ▸ Wi-Fi ▸ Reset web password, so the owner's first visit chooses theirs.
- **This Mac's Wi-Fi** may join the board's AP for checks (owner, 2026-09-30): `networksetup -setairportnetwork en0 reflbo-XXXX <password from the screen>`, removed afterwards with `networksetup -removepreferredwirelessnetwork en0 reflbo-XXXX`.
- Never erase flash or NVS, or forget the saved networks, without asking. A factory reset only with the owner's agreement.
  - Board commands go through `tools/idf.sh` with an explicit `-p`.
  - The port must be confirmed as this board: Espressif `303A:1001` with the USB serial number `14:C1:9F:54:BB:94` (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'`), and `flash` prints `MAC: 14:c1:9f:54:bb:94`.
- List every component source in `SRCS`. Dependencies come with ESP-IDF; nothing new from the registry.
- Small, focused Conventional Commits that each build. Push to `origin` freely; never force-push. **No attribution of any kind: no `Co-Authored-By` or other trailer in any commit** (the owner's rule, memory `no-commit-trailers`).
- Build only what the spec covers (r32: D31). Not in M6b: built-in split presets, changes to the fixed layouts, MQTT (M7), web UI translations (spec §5.8).
- Czech text follows Czech typography, and every string's glyphs must exist in the fonts (`test_lang_glyphs`).
- A widget never draws outside its cell, and in a cell the rules allow it, never against its edges (`test_ui_widget_fit`).

## Review Focus

1. **Files and snapshots from M6.** The owner's `presets.json` (Classic, Grid, Weather, the radars; `slots` objects) and `settings.json` meet the new firmware, and RTC RAM holds a version-7 snapshot. Expected: both files parse as they are and save the same, `settings.json` gaining `mode_before_always`; the boot after the flash is cold and never reads the old snapshot; nothing crashes. Pinned by:
   - `test_ui_preset`'s round trips and the full set that fits the save buffer (Task 2);
   - the board's first boot over M6 in Task 6, with the owner's presets.
2. **Trees at the limits, from the page or a file.** Eight cells of the narrowest legal parts (90 × 40), seven splits in a chain, every line hidden, a field that fits only at S. Expected: the editor builds only trees the device accepts; the device refuses the rest with the cell's number and size; nothing it accepts draws outside its cells. Pinned by:
   - `test_ui_split`'s geometry and `test_ui_preset`'s refusals (Tasks 1, 2), the editor's tests (Task 4), the catalogue's rules checked against `ui_split_field_size()` everywhere (Task 4);
   - Task 6's refusals over the API.
3. **BOOT double outside the dashboard.** The menu and the moment after it closes, config mode, the first-run screen, the critical-battery screen, a night's peek, quiet hours, the radar's loop playing, a press that wakes the board, no network saved. Expected: the dashboard and the Radar layout toggle; the menu and config mode bind no BOOT double, and a double within a second of the menu closing does nothing; the critical screen and the first-run screen change nothing; the loop stops; in quiet hours or a night the toast says when Wi-Fi comes; with no network saved, "No Wi-Fi network saved" and the mode stays. Pinned by:
   - the code's order in `handle_button()` and `app_sync_toggle_always()` (Task 5);
   - Task 6's `btn boot double` in each context the board can reach without erasing anything.
4. **BOOT short after the double-press window.** Expected: BOOT short still refreshes the sensors (on the Radar layout, plays the hour or syncs), 300 ms later; BOOT long still enters config mode at 3 s. Pinned by:
   - `test_gesture`'s double-press window, unchanged;
   - Task 6's presses.
5. **Long values in small cells.** Czech words and decimal commas, a 12-hour clock, seconds, °F, frost, stale marks, no data, in every cell a tree can make. Expected: inside the cell, clear of its edges, a number's comma whole. Pinned by:
   - `test_ui_widget_fit`'s split cells and Classic's main slot (Task 3), the two goldens (Task 3).

---

### Task 1: The split tree's geometry and its cells' sizes (`ui`)

**Files:**
- Create: `components/ui/include/ui_split.h`, `components/ui/ui_split.c`, `test/host/test_ui_split.c`
- Modify: `components/ui/include/ui_layout.h` (the kind sets each size takes), `components/ui/ui_layout.c`, `components/ui/CMakeLists.txt`, `test/host/CMakeLists.txt`, `AGENTS.md` (§5.2's `ui` line)

**Interfaces:**
- Consumes: `gfx_rect_t` (gfx.h); `ui_field_kind_t`, `UI_KIND()` (ui_fields.h); `ui_size_t`, `UI_STATUS_H` (ui_layout.h).
- Produces (Tasks 2–4), in `ui_split.h`:
  - `UI_SPLIT_CELLS` 8, `UI_SPLIT_NODES` 15, `UI_SPLIT_MIN_W` 90, `UI_SPLIT_MIN_H` 40, `UI_SPLIT_NARROW_W` 150, `UI_SPLIT_INSET` 8;
  - `ui_ratio_t`: `UI_RATIO_1_4` = 1, `UI_RATIO_1_3`, `UI_RATIO_1_2`, `UI_RATIO_2_3`, `UI_RATIO_3_4`; a node's bits `UI_SPLIT_RATIO` 0x07 (0: a cell), `UI_SPLIT_COLUMNS` 0x08, `UI_SPLIT_NO_LINE` 0x10;
  - `ui_split_line_t { gfx_rect_t rect; uint8_t node; int16_t at; }`, `ui_split_geometry_t { uint8_t cells; gfx_rect_t cell[8]; uint8_t lines; ui_split_line_t line[7]; }`;
  - `gfx_rect_t ui_split_area(void)`; `bool ui_split_layout(const uint8_t tree[UI_SPLIT_NODES], gfx_rect_t area, ui_split_geometry_t *out)`; `int ui_split_nodes(const uint8_t tree[UI_SPLIT_NODES])`; `int ui_split_first(int length, int ratio)`;
  - `int ui_split_cell_size(int w, int h)`, `int ui_split_field_size(ui_field_kind_t kind, int w, int h)` (a `ui_size_t`, or -1);
  - `int ui_split_min_w(ui_size_t size)`, `int ui_split_min_h(ui_size_t size, bool narrow)`, `int ui_split_need(ui_size_t size, ui_field_kind_t kind, bool narrow)`;
  - `const char *ui_split_ratio_name(int ratio)`, `int ui_split_ratio_by_name(const char *name)`.
- In `ui_layout.h`: `UI_KINDS_S`, `UI_KINDS_M`, `UI_KINDS_L`, `UI_KINDS_XL`, the kinds each size takes (until now `ui_layout.c`'s private `K_*`).

A tree is kept in preorder: a split's node, then its first part `a` and everything under it, then its second part `b`. A cell is 0; its field sits in the preset's slots at the cell's place among the cells. A tree of n cells has 2n − 1 nodes, so 15 bytes hold 8 cells.

- [ ] **Step 1: Write the failing test.**

`test/host/test_ui_split.c`:

```c
#include <string.h>

#include "ui_split.h"
#include "unity.h"

/* The split layout (spec §5.2, D31): its geometry, and the sizes its cells give their fields. */

#define ROWS(r) ((uint8_t)(r))
#define COLS(r) ((uint8_t)((r) | UI_SPLIT_COLUMNS))
#define NO_LINE UI_SPLIT_NO_LINE
#define CELL 0

static ui_split_geometry_t s_g;

void setUp(void)
{
    memset(&s_g, 0xA5, sizeof(s_g));
}

void tearDown(void) {}

static void check_rect(int x, int y, int w, int h, gfx_rect_t r)
{
    TEST_ASSERT_EQUAL_INT(x, r.x);
    TEST_ASSERT_EQUAL_INT(y, r.y);
    TEST_ASSERT_EQUAL_INT(w, r.w);
    TEST_ASSERT_EQUAL_INT(h, r.h);
}

static bool lay(const uint8_t *nodes, int count)
{
    uint8_t tree[UI_SPLIT_NODES] = { 0 };
    memcpy(tree, nodes, (size_t)count);
    return ui_split_layout(tree, ui_split_area(), &s_g);
}

static void test_the_area_is_everything_under_the_status_bar(void)
{
    check_rect(0, 21, 400, 279, ui_split_area());
}

static void test_each_ratio_rounds_its_first_part_down(void)
{
    static const int k_279[] = { 69, 93, 139, 186, 209 }, k_400[] = { 100, 133, 200, 266, 300 };
    for (int r = UI_RATIO_1_4; r <= UI_RATIO_3_4; r++) {
        TEST_ASSERT_EQUAL_INT(k_279[r - 1], ui_split_first(279, r));
        TEST_ASSERT_EQUAL_INT(k_400[r - 1], ui_split_first(400, r));
    }
}

static void test_a_single_cell_fills_the_area(void)
{
    static const uint8_t k_tree[] = { CELL };
    TEST_ASSERT_TRUE(lay(k_tree, 1));
    TEST_ASSERT_EQUAL_INT(1, s_g.cells);
    TEST_ASSERT_EQUAL_INT(0, s_g.lines);
    check_rect(0, 21, 400, 279, s_g.cell[0]);
    TEST_ASSERT_EQUAL_INT(1, ui_split_nodes((const uint8_t[UI_SPLIT_NODES]){ CELL }));
}

/* Spec §5.2's example: Weather with smaller bottom cells. */
static void test_the_spec_example_lays_out_as_the_spec_says(void)
{
    static const uint8_t k_tree[] = { ROWS(UI_RATIO_3_4),
                                      COLS(UI_RATIO_1_2), CELL, ROWS(UI_RATIO_1_2), CELL, CELL,
                                      COLS(UI_RATIO_1_2) | NO_LINE, CELL, CELL };
    TEST_ASSERT_TRUE(lay(k_tree, sizeof(k_tree)));
    TEST_ASSERT_EQUAL_INT(9, ui_split_nodes((const uint8_t[UI_SPLIT_NODES]){ ROWS(UI_RATIO_3_4), COLS(UI_RATIO_1_2),
        CELL, ROWS(UI_RATIO_1_2), CELL, CELL, COLS(UI_RATIO_1_2) | NO_LINE, CELL, CELL }));
    TEST_ASSERT_EQUAL_INT(5, s_g.cells);
    check_rect(0, 21, 200, 209, s_g.cell[0]);   /* L */
    check_rect(201, 21, 199, 104, s_g.cell[1]); /* M */
    check_rect(201, 126, 199, 104, s_g.cell[2]);
    check_rect(0, 231, 200, 69, s_g.cell[3]);   /* S, drawn wide */
    check_rect(201, 231, 199, 69, s_g.cell[4]);
    TEST_ASSERT_EQUAL_INT(4, s_g.lines); /* every split, in preorder: the gap after the first part */
    check_rect(0, 21, 400, 279, s_g.line[0].rect);
    TEST_ASSERT_EQUAL_INT(230, s_g.line[0].at);
    TEST_ASSERT_EQUAL_INT(200, s_g.line[1].at);
    TEST_ASSERT_EQUAL_INT(125, s_g.line[2].at);
    check_rect(201, 21, 199, 209, s_g.line[2].rect);
    TEST_ASSERT_EQUAL_INT(200, s_g.line[3].at);
    TEST_ASSERT_TRUE(s_g.line[3].node & UI_SPLIT_NO_LINE);
    TEST_ASSERT_FALSE(s_g.line[0].node & UI_SPLIT_NO_LINE);
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_cell_size(s_g.cell[0].w, s_g.cell[0].h));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(s_g.cell[1].w, s_g.cell[1].h));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(s_g.cell[3].w, s_g.cell[3].h));
}

/* Eight cells, each at least 90×40: two rows of four columns. */
static void test_eight_cells_fill_the_tree(void)
{
    static const uint8_t k_tree[UI_SPLIT_NODES] = {
        ROWS(UI_RATIO_1_2),
        COLS(UI_RATIO_1_4), CELL, COLS(UI_RATIO_1_3), CELL, COLS(UI_RATIO_1_2), CELL, CELL,
        COLS(UI_RATIO_1_4), CELL, COLS(UI_RATIO_1_3), CELL, COLS(UI_RATIO_1_2), CELL, CELL,
    };
    TEST_ASSERT_TRUE(ui_split_layout(k_tree, ui_split_area(), &s_g));
    TEST_ASSERT_EQUAL_INT(UI_SPLIT_NODES, ui_split_nodes(k_tree));
    TEST_ASSERT_EQUAL_INT(8, s_g.cells);
    TEST_ASSERT_EQUAL_INT(7, s_g.lines);
    check_rect(0, 21, 100, 139, s_g.cell[0]);
    check_rect(101, 21, 99, 139, s_g.cell[1]);
    check_rect(201, 21, 99, 139, s_g.cell[2]);
    check_rect(301, 21, 99, 139, s_g.cell[3]);
    check_rect(301, 161, 99, 139, s_g.cell[7]);
}

/* A tree whose splits run past the 15 nodes has no end: there is no room for its cells. */
static void test_a_tree_cut_short_is_refused(void)
{
    uint8_t tree[UI_SPLIT_NODES];
    memset(tree, COLS(UI_RATIO_1_2), sizeof(tree));
    TEST_ASSERT_FALSE(ui_split_layout(tree, ui_split_area(), &s_g));
    TEST_ASSERT_EQUAL_INT(0, ui_split_nodes(tree));
}

static void test_a_part_under_90_by_40_is_refused(void)
{
    static const uint8_t k_narrow[] = { COLS(UI_RATIO_1_4), COLS(UI_RATIO_1_2), CELL, CELL, CELL }; /* 50 px */
    static const uint8_t k_short[] = { ROWS(UI_RATIO_1_4), ROWS(UI_RATIO_1_2), CELL, CELL, CELL };  /* 34 px */
    static const uint8_t k_just[] = { ROWS(UI_RATIO_1_3), ROWS(UI_RATIO_1_2), CELL, CELL, CELL };   /* 46 px */
    TEST_ASSERT_FALSE(lay(k_narrow, sizeof(k_narrow)));
    TEST_ASSERT_FALSE(lay(k_short, sizeof(k_short)));
    TEST_ASSERT_TRUE(lay(k_just, sizeof(k_just)));
    check_rect(0, 21, 400, 46, s_g.cell[0]);
    check_rect(0, 68, 400, 46, s_g.cell[1]);
}

static void test_a_bad_node_is_refused(void)
{
    static const uint8_t k_ratio[] = { 6, CELL, CELL };
    static const uint8_t k_flag[] = { ROWS(UI_RATIO_1_2) | 0x20, CELL, CELL };
    TEST_ASSERT_FALSE(lay(k_ratio, sizeof(k_ratio)));
    TEST_ASSERT_FALSE(lay(k_flag, sizeof(k_flag)));
}

/* XL 400 wide and at least 120 tall; L at least 200×150; M at least 130×80; S otherwise: wide from
 * 150 px with 40 px of height, narrower with 80 (spec §5.2). */
static void test_a_cells_size_follows_its_dimensions(void)
{
    TEST_ASSERT_EQUAL_INT(UI_SIZE_XL, ui_split_cell_size(400, 120));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(400, 119));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_cell_size(399, 279));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_cell_size(200, 150));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(199, 150));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(200, 149));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(130, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(129, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(150, 40));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(149, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(150, 79));
    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(149, 79)); /* narrow S needs 80 */
    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(90, 40));
    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(150, 39));
}

/* A field draws at the largest size its cell allows that takes its kind and has room for it. */
static void test_a_field_draws_at_the_largest_size_with_room_for_it(void)
{
    TEST_ASSERT_EQUAL_INT(UI_SIZE_XL, ui_split_field_size(UI_FK_TIME, 400, 279));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_field_size(UI_FK_WEATHER_NOW, 400, 279)); /* XL takes no weather */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_field_size(UI_FK_DATE, 400, 279));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_XL, ui_split_field_size(UI_FK_NUMBER, 400, 120)); /* its digits fit the height */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_NUMBER, 400, 119));  /* L needs 150 */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_BATTERY, 130, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_SERIES, 200, 209));  /* L takes no series */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_field_size(UI_FK_RAIN_MAP, 200, 209));
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_RAIN_MAP, 199, 69)); /* series and maps need M */
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_SERIES, 199, 69));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_POLLEN, 199, 104)); /* M pollen needs 105 */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_POLLEN, 199, 105));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_DAY, 199, 69)); /* wide S needs 51 */
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_WEATHER_DAY, 129, 98));        /* narrow S needs 99 */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_DAY, 129, 99));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_WEATHER_DAY, 149, 94)); /* narrow M needs 94 */
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_NUMBER, 99, 69));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_NUMBER, 99, 80));
}

/* A larger cell never takes a field that a smaller one inside it takes: Join keeps a field. */
static void test_more_room_never_loses_a_field(void)
{
    for (int k = 0; k < UI_FK_COUNT; k++) {
        for (int w = 90; w <= 400; w += 1) {
            for (int h = 40; h <= 279; h += 7) {
                if (ui_split_field_size((ui_field_kind_t)k, w, h) >= 0) {
                    TEST_ASSERT_TRUE(ui_split_field_size((ui_field_kind_t)k, w + 1 > 400 ? 400 : w + 1, h) >=
                                     ui_split_field_size((ui_field_kind_t)k, w, h));
                    TEST_ASSERT_TRUE(ui_split_field_size((ui_field_kind_t)k, w, h + 7 > 279 ? 279 : h + 7) >=
                                     ui_split_field_size((ui_field_kind_t)k, w, h));
                }
            }
        }
    }
}

static void test_ratios_have_names(void)
{
    static const char *const k_names[] = { "1/4", "1/3", "1/2", "2/3", "3/4" };
    for (int r = UI_RATIO_1_4; r <= UI_RATIO_3_4; r++) {
        TEST_ASSERT_EQUAL_STRING(k_names[r - 1], ui_split_ratio_name(r));
        TEST_ASSERT_EQUAL_INT(r, ui_split_ratio_by_name(k_names[r - 1]));
    }
    TEST_ASSERT_EQUAL_INT(0, ui_split_ratio_by_name("2/4"));
    TEST_ASSERT_EQUAL_INT(0, ui_split_ratio_by_name(NULL));
    TEST_ASSERT_NULL(ui_split_ratio_name(0));
    TEST_ASSERT_NULL(ui_split_ratio_name(6));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_area_is_everything_under_the_status_bar);
    RUN_TEST(test_each_ratio_rounds_its_first_part_down);
    RUN_TEST(test_a_single_cell_fills_the_area);
    RUN_TEST(test_the_spec_example_lays_out_as_the_spec_says);
    RUN_TEST(test_eight_cells_fill_the_tree);
    RUN_TEST(test_a_tree_cut_short_is_refused);
    RUN_TEST(test_a_part_under_90_by_40_is_refused);
    RUN_TEST(test_a_bad_node_is_refused);
    RUN_TEST(test_a_cells_size_follows_its_dimensions);
    RUN_TEST(test_a_field_draws_at_the_largest_size_with_room_for_it);
    RUN_TEST(test_more_room_never_loses_a_field);
    RUN_TEST(test_ratios_have_names);
    return UNITY_END();
}
```


Register it:

`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -249,6 +249,7 @@ reflbo_host_test(test_sync_ntp sync_logic)
 target_compile_definitions(test_weather PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/open-meteo")
 reflbo_host_test(test_ui_fields ui)
 reflbo_host_test(test_ui_preset ui)
+reflbo_host_test(test_ui_split ui)
 reflbo_host_test(test_ui_schedule ui)
 reflbo_host_test(test_ui_widget_fit ui)
 reflbo_host_test(test_ui_menu ui)
```


- [ ] **Step 2: Run it to see it fail.**

Run: `cmake --build build-host --target test_ui_split 2>&1 | grep -m1 'error:'`
Expected: `test_ui_split.c:3:10: fatal error: 'ui_split.h' file not found`.

- [ ] **Step 3: The kinds each size takes move to `ui_layout.h`,** where the split's sizes share them with the fixed slots:

`components/ui/include/ui_layout.h`:

```diff
--- a/components/ui/include/ui_layout.h
+++ b/components/ui/include/ui_layout.h
@@ -3,6 +3,7 @@
 #include <stdint.h>
 
 #include "gfx.h"
+#include "ui_fields.h"
 
 /* Layouts (spec §5.2): fixed slot rectangles below a 20 px status bar. Pure C, host-buildable. */
 
@@ -26,6 +27,15 @@ typedef enum {
     UI_SIZE_XL,
 } ui_size_t;
 
+/* The field kinds each size takes (spec §5.1): the fixed layouts' slots and the split layout's cells. */
+#define UI_KINDS_S                                                                                                   \
+    (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_DATE) | UI_KIND(UI_FK_NUMBER) | UI_KIND(UI_FK_BATTERY) |                    \
+     UI_KIND(UI_FK_MOON) | UI_KIND(UI_FK_TEXT) | UI_KIND(UI_FK_WEATHER_NOW) | UI_KIND(UI_FK_WEATHER_DAY) |           \
+     UI_KIND(UI_FK_SUN) | UI_KIND(UI_FK_LEVEL) | UI_KIND(UI_FK_POLLEN))
+#define UI_KINDS_M (UI_KINDS_S | UI_KIND(UI_FK_SERIES) | UI_KIND(UI_FK_RAIN_MAP))
+#define UI_KINDS_L (UI_KINDS_S | UI_KIND(UI_FK_RAIN_MAP))
+#define UI_KINDS_XL (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_NUMBER))
+
 typedef struct {
     const char *name; /* as in presets.json: "main", "s1" */
     gfx_rect_t rect;
```


`components/ui/ui_layout.c`:

```diff
--- a/components/ui/ui_layout.c
+++ b/components/ui/ui_layout.c
@@ -2,48 +2,38 @@
 
 #include <string.h>
 
-#include "ui_fields.h"
-
 /* Slot rectangles for 400x300 below the 20 px status bar (spec §5.2), tuned on host renders. */
 
-#define K_ANY_SMALL                                                                                                  \
-    (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_DATE) | UI_KIND(UI_FK_NUMBER) | UI_KIND(UI_FK_BATTERY) |                    \
-     UI_KIND(UI_FK_MOON) | UI_KIND(UI_FK_TEXT) | UI_KIND(UI_FK_WEATHER_NOW) | UI_KIND(UI_FK_WEATHER_DAY) |           \
-     UI_KIND(UI_FK_SUN) | UI_KIND(UI_FK_LEVEL) | UI_KIND(UI_FK_POLLEN))
-#define K_ANY_MEDIUM (K_ANY_SMALL | UI_KIND(UI_FK_SERIES) | UI_KIND(UI_FK_RAIN_MAP))
-#define K_LARGE (K_ANY_SMALL | UI_KIND(UI_FK_RAIN_MAP))
-#define K_XL (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_NUMBER))
-
 static const ui_slot_t k_classic[] = {
-    { "main", { 0, 21, 400, 125 }, UI_SIZE_XL, K_XL },
+    { "main", { 0, 21, 400, 125 }, UI_SIZE_XL, UI_KINDS_XL },
     { "sub", { 0, 146, 400, 40 }, UI_SIZE_M, UI_KIND(UI_FK_DATE) | UI_KIND(UI_FK_TEXT) },
-    { "s1", { 0, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
-    { "s2", { 100, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
-    { "s3", { 200, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
-    { "s4", { 300, 189, 100, 111 }, UI_SIZE_S, K_ANY_SMALL },
+    { "s1", { 0, 189, 100, 111 }, UI_SIZE_S, UI_KINDS_S },
+    { "s2", { 100, 189, 100, 111 }, UI_SIZE_S, UI_KINDS_S },
+    { "s3", { 200, 189, 100, 111 }, UI_SIZE_S, UI_KINDS_S },
+    { "s4", { 300, 189, 100, 111 }, UI_SIZE_S, UI_KINDS_S },
 };
 
 static const ui_slot_t k_weather[] = {
-    { "now", { 0, 21, 200, 160 }, UI_SIZE_L, K_LARGE },
-    { "today", { 200, 21, 200, 80 }, UI_SIZE_M, K_ANY_MEDIUM },
-    { "hourly", { 200, 101, 200, 80 }, UI_SIZE_M, K_ANY_MEDIUM },
-    { "s1", { 0, 182, 200, 118 }, UI_SIZE_S, K_ANY_SMALL },
-    { "s2", { 200, 182, 200, 118 }, UI_SIZE_S, K_ANY_SMALL },
+    { "now", { 0, 21, 200, 160 }, UI_SIZE_L, UI_KINDS_L },
+    { "today", { 200, 21, 200, 80 }, UI_SIZE_M, UI_KINDS_M },
+    { "hourly", { 200, 101, 200, 80 }, UI_SIZE_M, UI_KINDS_M },
+    { "s1", { 0, 182, 200, 118 }, UI_SIZE_S, UI_KINDS_S },
+    { "s2", { 200, 182, 200, 118 }, UI_SIZE_S, UI_KINDS_S },
 };
 
 static const ui_slot_t k_grid[] = {
-    { "g1", { 0, 21, 133, 139 }, UI_SIZE_M, K_ANY_MEDIUM },
-    { "g2", { 133, 21, 134, 139 }, UI_SIZE_M, K_ANY_MEDIUM },
-    { "g3", { 267, 21, 133, 139 }, UI_SIZE_M, K_ANY_MEDIUM },
-    { "g4", { 0, 160, 133, 140 }, UI_SIZE_M, K_ANY_MEDIUM },
-    { "g5", { 133, 160, 134, 140 }, UI_SIZE_M, K_ANY_MEDIUM },
-    { "g6", { 267, 160, 133, 140 }, UI_SIZE_M, K_ANY_MEDIUM },
+    { "g1", { 0, 21, 133, 139 }, UI_SIZE_M, UI_KINDS_M },
+    { "g2", { 133, 21, 134, 139 }, UI_SIZE_M, UI_KINDS_M },
+    { "g3", { 267, 21, 133, 139 }, UI_SIZE_M, UI_KINDS_M },
+    { "g4", { 0, 160, 133, 140 }, UI_SIZE_M, UI_KINDS_M },
+    { "g5", { 133, 160, 134, 140 }, UI_SIZE_M, UI_KINDS_M },
+    { "g6", { 267, 160, 133, 140 }, UI_SIZE_M, UI_KINDS_M },
 };
 
 static const ui_slot_t k_focus[] = {
-    { "main", { 0, 21, 400, 190 }, UI_SIZE_XL, K_XL },
-    { "s1", { 0, 212, 200, 88 }, UI_SIZE_M, K_ANY_MEDIUM },
-    { "s2", { 200, 212, 200, 88 }, UI_SIZE_M, K_ANY_MEDIUM },
+    { "main", { 0, 21, 400, 190 }, UI_SIZE_XL, UI_KINDS_XL },
+    { "s1", { 0, 212, 200, 88 }, UI_SIZE_M, UI_KINDS_M },
+    { "s2", { 200, 212, 200, 88 }, UI_SIZE_M, UI_KINDS_M },
 };
 
 static const ui_layout_t k_layouts[UI_LAYOUT_COUNT] = {
```


- [ ] **Step 4: The split layout.** The heights in `k_needs` are the research's table (the plan's header): where a kind's widget needs more than its size's own least cell. Numbers, times and the battery need no entry: Task 3 fits their digits to the height. Task 3's `test_ui_widget_fit` checks the table by drawing every field it allows in every cell size a tree can make.

`components/ui/include/ui_split.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "ui_fields.h"
#include "ui_layout.h"

/*
 * The split layout (spec §5.2, D31): the area under the status bar split into rows or columns, each
 * part split again, at most 8 cells. A tree is kept in preorder, a byte a node: 0 is a cell; a split
 * has its ratio in the low bits, and flags for columns and a hidden separator. The cells' fields are
 * the preset's slots, in the order the cells come. Pure C, host-buildable.
 */

#define UI_SPLIT_CELLS 8
#define UI_SPLIT_NODES (2 * UI_SPLIT_CELLS - 1)
#define UI_SPLIT_MIN_W 90     /* no part is smaller (spec §5.2) */
#define UI_SPLIT_MIN_H 40
#define UI_SPLIT_NARROW_W 150 /* a narrower cell stacks a field's symbol above its value */
#define UI_SPLIT_INSET 8      /* a separator stops this short of each end */

typedef enum {
    UI_RATIO_1_4 = 1,
    UI_RATIO_1_3,
    UI_RATIO_1_2,
    UI_RATIO_2_3,
    UI_RATIO_3_4,
} ui_ratio_t;

#define UI_SPLIT_RATIO 0x07   /* a split's ratio (ui_ratio_t); 0 makes the node a cell */
#define UI_SPLIT_COLUMNS 0x08 /* part a left of part b; without it, a over b */
#define UI_SPLIT_NO_LINE 0x10 /* the separator hidden; its 1 px gap stays */

typedef struct {
    gfx_rect_t rect; /* the split's own rectangle */
    uint8_t node;    /* its code */
    int16_t at;      /* the gap between its parts: an x for columns, a y for rows */
} ui_split_line_t;

typedef struct {
    uint8_t cells;
    gfx_rect_t cell[UI_SPLIT_CELLS]; /* in preorder, as their fields come in the slots */
    uint8_t lines;
    ui_split_line_t line[UI_SPLIT_CELLS - 1]; /* every split, in preorder, its separator shown or not */
} ui_split_geometry_t;

/* What a split preset divides: everything under the status bar and its line (400×279). */
gfx_rect_t ui_split_area(void);
/* Lays the tree out over `area`. False if it is cut short, has a node it doesn't know, or has a
 * part under 90×40; *out is then unspecified. */
bool ui_split_layout(const uint8_t tree[UI_SPLIT_NODES], gfx_rect_t area, ui_split_geometry_t *out);
/* How many of the tree's nodes it uses: 1 to UI_SPLIT_NODES, or 0 if it is cut short. */
int ui_split_nodes(const uint8_t tree[UI_SPLIT_NODES]);
/* The first part's length when a split of `length` px gives it `ratio`, rounded down. */
int ui_split_first(int length, int ratio);

/* The size class a cell of w×h has (spec §5.2), or -1 when it is too small for any field. */
int ui_split_cell_size(int w, int h);
/* The size a field of `kind` draws at in a cell of w×h: the largest size the cell is wide enough
 * for, that takes the kind, and whose height for the kind the cell has; -1 if there is none. */
int ui_split_field_size(ui_field_kind_t kind, int w, int h);
/* What GET /api/layouts publishes (ui_catalog.c): a size's least width and height, in a cell
 * narrower than 150 px or not; and the least height a cell needs to draw `kind` at `size`, at
 * least the size's own, or -1 when the size doesn't take the kind. */
int ui_split_min_w(ui_size_t size);
int ui_split_min_h(ui_size_t size, bool narrow);
int ui_split_need(ui_size_t size, ui_field_kind_t kind, bool narrow);

/* "3/4" for UI_RATIO_3_4 and back; NULL and 0 for anything else. */
const char *ui_split_ratio_name(int ratio);
int ui_split_ratio_by_name(const char *name);
```


`components/ui/ui_split.c`:

```c
#include "ui_split.h"

#include <string.h>

/* The split layout's geometry and its cells' sizes (spec §5.2, D31). */

#define PANEL_W 400
#define PANEL_H 300

static const char *const k_ratio_names[] = { [UI_RATIO_1_4] = "1/4", [UI_RATIO_1_3] = "1/3", [UI_RATIO_1_2] = "1/2",
                                             [UI_RATIO_2_3] = "2/3", [UI_RATIO_3_4] = "3/4" };
static const uint8_t k_ratio_num[] = { [UI_RATIO_1_4] = 1, [UI_RATIO_1_3] = 1, [UI_RATIO_1_2] = 1,
                                       [UI_RATIO_2_3] = 2, [UI_RATIO_3_4] = 3 };
static const uint8_t k_ratio_den[] = { [UI_RATIO_1_4] = 4, [UI_RATIO_1_3] = 3, [UI_RATIO_1_2] = 2,
                                       [UI_RATIO_2_3] = 3, [UI_RATIO_3_4] = 4 };

/* Each size's least cell (spec §5.2), and the kinds it takes (spec §5.1). */
static const struct {
    int16_t min_w, narrow_h, wide_h;
    uint32_t kinds;
} k_sizes[] = {
    [UI_SIZE_S] = { 90, 80, 40, UI_KINDS_S },
    [UI_SIZE_M] = { 130, 80, 80, UI_KINDS_M },
    [UI_SIZE_L] = { 200, 150, 150, UI_KINDS_L },
    [UI_SIZE_XL] = { 400, 120, 120, UI_KINDS_XL },
};

/* Where a kind's widget needs more height than its size's least cell: the least height at which it
 * stays clear of the cell's edges, measured on host renders of every field with Czech text and
 * stale marks; test_ui_widget_fit.c checks each one. Numbers, times and the battery fit their
 * digits to the height (ui_widget.c), so they need no more than the size's own. */
static const struct {
    uint8_t size, kind;
    uint8_t narrow_h, wide_h;
} k_needs[] = {
    { UI_SIZE_S, UI_FK_WEATHER_NOW, 86, 61 },
    { UI_SIZE_S, UI_FK_WEATHER_DAY, 99, 51 },
    { UI_SIZE_S, UI_FK_SUN, 80, 49 },
    { UI_SIZE_S, UI_FK_LEVEL, 83, 40 },
    { UI_SIZE_S, UI_FK_POLLEN, 86, 42 },
    { UI_SIZE_M, UI_FK_WEATHER_NOW, 98, 80 },
    { UI_SIZE_M, UI_FK_WEATHER_DAY, 94, 80 },
    { UI_SIZE_M, UI_FK_SUN, 94, 94 },
    { UI_SIZE_M, UI_FK_LEVEL, 93, 93 },
    { UI_SIZE_M, UI_FK_POLLEN, 105, 105 },
};

gfx_rect_t ui_split_area(void)
{
    return (gfx_rect_t){ 0, UI_STATUS_H + 1, PANEL_W, PANEL_H - UI_STATUS_H - 1 };
}

int ui_split_first(int length, int ratio)
{
    if (ratio < UI_RATIO_1_4 || ratio > UI_RATIO_3_4) {
        return 0;
    }
    return length * k_ratio_num[ratio] / k_ratio_den[ratio];
}

typedef struct {
    const uint8_t *tree;
    int at; /* the next node */
    ui_split_geometry_t *out;
    bool ok;
} walk_t;

/* One node and everything under it, laid over r. Each call takes a node, so it recurses at most
 * UI_SPLIT_NODES deep. */
static void walk(walk_t *w, gfx_rect_t r)
{
    if (!w->ok || w->at >= UI_SPLIT_NODES || r.w < UI_SPLIT_MIN_W || r.h < UI_SPLIT_MIN_H) {
        w->ok = false;
        return;
    }
    uint8_t node = w->tree[w->at++];
    int ratio = node & UI_SPLIT_RATIO;
    if (ratio == 0) {
        if (node != 0 || w->out->cells >= UI_SPLIT_CELLS) {
            w->ok = false;
            return;
        }
        w->out->cell[w->out->cells++] = r;
        return;
    }
    if (ratio > UI_RATIO_3_4 || (node & ~(UI_SPLIT_RATIO | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE)) ||
        w->out->lines >= UI_SPLIT_CELLS - 1) {
        w->ok = false;
        return;
    }
    bool columns = node & UI_SPLIT_COLUMNS;
    int first = ui_split_first(columns ? r.w : r.h, ratio);
    gfx_rect_t a = r, b = r;
    if (columns) {
        a.w = (int16_t)first;
        b.x = (int16_t)(r.x + first + 1);
        b.w = (int16_t)(r.w - first - 1);
    } else {
        a.h = (int16_t)first;
        b.y = (int16_t)(r.y + first + 1);
        b.h = (int16_t)(r.h - first - 1);
    }
    w->out->line[w->out->lines++] =
        (ui_split_line_t){ .rect = r, .node = node, .at = (int16_t)(columns ? r.x + first : r.y + first) };
    walk(w, a);
    walk(w, b);
}

bool ui_split_layout(const uint8_t tree[UI_SPLIT_NODES], gfx_rect_t area, ui_split_geometry_t *out)
{
    memset(out, 0, sizeof(*out));
    walk_t w = { .tree = tree, .out = out, .ok = true };
    walk(&w, area);
    return w.ok;
}

/* The nodes a tree uses, from its shape alone: each split needs two more. */
int ui_split_nodes(const uint8_t tree[UI_SPLIT_NODES])
{
    int open = 1;
    for (int i = 0; i < UI_SPLIT_NODES; i++) {
        open += (tree[i] & UI_SPLIT_RATIO) ? 1 : -1;
        if (open == 0) {
            return i + 1;
        }
    }
    return 0;
}

int ui_split_min_w(ui_size_t size)
{
    return (unsigned)size <= UI_SIZE_XL ? k_sizes[size].min_w : -1;
}

int ui_split_min_h(ui_size_t size, bool narrow)
{
    if ((unsigned)size > UI_SIZE_XL) {
        return -1;
    }
    return narrow ? k_sizes[size].narrow_h : k_sizes[size].wide_h;
}

int ui_split_need(ui_size_t size, ui_field_kind_t kind, bool narrow)
{
    if ((unsigned)size > UI_SIZE_XL || (unsigned)kind >= UI_FK_COUNT || !(k_sizes[size].kinds & UI_KIND(kind))) {
        return -1;
    }
    int need = ui_split_min_h(size, narrow);
    for (size_t i = 0; i < sizeof(k_needs) / sizeof(k_needs[0]); i++) {
        if (k_needs[i].size == size && k_needs[i].kind == kind) {
            int h = narrow ? k_needs[i].narrow_h : k_needs[i].wide_h;
            need = h > need ? h : need;
        }
    }
    return need;
}

int ui_split_cell_size(int w, int h)
{
    for (int size = UI_SIZE_XL; size >= UI_SIZE_S; size--) {
        if (w >= k_sizes[size].min_w && h >= ui_split_min_h((ui_size_t)size, w < UI_SPLIT_NARROW_W)) {
            return size;
        }
    }
    return -1;
}

int ui_split_field_size(ui_field_kind_t kind, int w, int h)
{
    for (int size = UI_SIZE_XL; size >= UI_SIZE_S; size--) {
        int need = ui_split_need((ui_size_t)size, kind, w < UI_SPLIT_NARROW_W);
        if (w >= k_sizes[size].min_w && need >= 0 && h >= need) {
            return size;
        }
    }
    return -1;
}

const char *ui_split_ratio_name(int ratio)
{
    return ratio >= UI_RATIO_1_4 && ratio <= UI_RATIO_3_4 ? k_ratio_names[ratio] : NULL;
}

int ui_split_ratio_by_name(const char *name)
{
    for (int r = UI_RATIO_1_4; name != NULL && r <= UI_RATIO_3_4; r++) {
        if (strcmp(k_ratio_names[r], name) == 0) {
            return r;
        }
    }
    return 0;
}
```


- [ ] **Step 5: The firmware's sources, and AGENTS.md's component list.**

`components/ui/CMakeLists.txt`:

```diff
--- a/components/ui/CMakeLists.txt
+++ b/components/ui/CMakeLists.txt
@@ -2,7 +2,7 @@
 idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                             "ui_status.c" "ui_dashboard.c" "ui_schedule.c" "ui_menu.c" "ui_menu_draw.c"
                             "ui_screens.c" "ui_config.c" "ui_catalog.c" "ui_forecast.c" "ui_radar.c"
-                            "ui_flights.c"
+                            "ui_flights.c" "ui_split.c"
                        INCLUDE_DIRS "include"
                        REQUIRES gfx locale datastore util map radar adsb
                        PRIV_REQUIRES json scheduler weather astro)
```


`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -220,7 +220,7 @@ components/
   locale/        language packs: en, cs; API prefix lang_             [host]
   astro/         sunrise/sunset, day length                           [host]
   datastore/     fields, freshness, change events, snapshot           [host]
-  ui/            layouts, widgets, presets, screens, menu             [host]
+  ui/            layouts, split trees, widgets, presets, screens, menu [host]
   scheduler/     next-wake computation (display, alarms, sync)        [host]
   sensors/       SHTC3, battery gauge
   rtc/           PCF85063 driver
```


- [ ] **Step 6: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_ui_split && ctest --test-dir build-host`
Expected: `12 Tests 0 Failures 0 Ignored`; `100% tests passed, 0 tests failed out of 61`.

- [ ] **Step 7: The firmware builds.** `tools/idf.sh build`: clean, without a warning. (Nothing calls the new code yet, so the image doesn't grow.)

- [ ] **Step 8: Commit.**

```bash
git add components/ui test/host/test_ui_split.c test/host/CMakeLists.txt AGENTS.md
git commit -m "feat(ui): the split layout's geometry and its cells' sizes"
```

### Task 2: Split presets in `presets.json` (`ui`, `main`, `webui`)

**Files:**
- Modify: `components/ui/include/ui_layout.h` (`UI_LAYOUT_SPLIT`, `UI_SLOT_MAX`), `components/ui/ui_layout.c`, `components/ui/include/ui_preset.h`, `components/ui/ui_preset.c`, `components/ui/ui_preset_json.c`, `main/app.c` (the snapshot's version), `main/app_radar.c`, `components/webui/include/webui.h`, `main/app_web.c`, `test/host/test_ui_preset.c`, `test/host/test_ui_catalog.c`

**Interfaces:**
- Consumes (Task 1): `ui_split_layout()`, `ui_split_area()`, `ui_split_nodes()`, `ui_split_field_size()`, `ui_split_ratio_name()`, `ui_split_ratio_by_name()`, `UI_SPLIT_*`, `UI_RATIO_*`.
- Produces (Tasks 3–5):
  - `UI_LAYOUT_SPLIT` (layout id 6, `"split"` in `presets.json`, after `UI_LAYOUT_FLIGHTS`), `UI_SLOT_MAX` 8;
  - `ui_preset_t.split[UI_SPLIT_NODES]`: the tree, all 0 for the fixed layouts; the cells' fields in `slots[]`, in preorder;
  - `int ui_preset_slots(const ui_preset_t *p)`: the slots its layout uses (a split tree's cells, 0 for a tree cut short);
  - `UI_PRESETS_JSON_MAX` 20480 (the largest file is 16 253 bytes); `WEBUI_BODY_MAX` and `WEBUI_REPLY_MAX` 24 KB;
  - in `presets.json` (spec §5.4): `"layout": "split"` with `"split": { "split": "rows" | "columns", "ratio": "1/4" … "3/4", "line": true | false, "a": {…}, "b": {…} }`, a cell `{ "field": "<id>" }` or `{}`.

The layout and the preset grow first; then the codec reads the tree depth first, as the cells come, and checks the whole: at most 8 cells, every part at least 90 × 40 (the geometry), and each field one its cell can show. Validation stays where `presets.json`, `PUT /api/presets`, the editor's preview and a restored backup all meet it: `ui_presets_from_json()`.

- [ ] **Step 1: Write the failing tests.** The size test builds the largest file there can be: names of control characters, which cJSON writes as six bytes each, and every option, id and schedule entry at its longest. The catalogue lists every layout, so its count grows too.

`test/host/test_ui_preset.c`:

```diff
--- a/test/host/test_ui_preset.c
+++ b/test/host/test_ui_preset.c
@@ -3,11 +3,12 @@
 
 #include "ui_fields.h"
 #include "ui_preset.h"
+#include "ui_split.h"
 #include "unity.h"
 
 static ui_presets_t s_p;
 static char s_err[128];
-static char s_json[8192];
+static char s_json[UI_PRESETS_JSON_MAX];
 
 void setUp(void)
 {
@@ -368,6 +369,169 @@ static void test_a_full_set_fits_the_save_buffer(void)
     TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
 }
 
+
+/* Spec §5.2's example as presets.json has it (spec §5.4): Weather with smaller bottom cells. */
+#define SPLIT_WEATHER                                                                                              \
+    "{\"schema\": 1, \"presets\": [{\"id\": \"wx\", \"name\": \"Weather\", \"layout\": \"split\", \"split\": "        \
+    "{\"split\": \"rows\", \"ratio\": \"3/4\", \"line\": true,"                                                      \
+    " \"a\": {\"split\": \"columns\", \"ratio\": \"1/2\","                                                           \
+    "        \"a\": {\"field\": \"wx.now\"},"                                                                        \
+    "        \"b\": {\"split\": \"rows\", \"ratio\": \"1/2\", \"a\": {\"field\": \"wx.today\"},"                      \
+    "               \"b\": {\"field\": \"wx.hourly\"}}},"                                                            \
+    " \"b\": {\"split\": \"columns\", \"ratio\": \"1/2\", \"line\": false, \"a\": {\"field\": \"env.temp\"},"         \
+    "        \"b\": {\"field\": \"env.hum\"}}}}]}"
+
+static void test_the_spec_split_example_parses(void)
+{
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(SPLIT_WEATHER, &s_p, s_err, sizeof(s_err)), s_err);
+    const ui_preset_t *p = &s_p.presets[0];
+    TEST_ASSERT_EQUAL(UI_LAYOUT_SPLIT, p->layout);
+    static const uint8_t k_tree[UI_SPLIT_NODES] = { UI_RATIO_3_4, UI_RATIO_1_2 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_2, 0,
+                                                    0, UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0 };
+    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_tree, p->split, UI_SPLIT_NODES); /* a missing line is shown */
+    static const uint8_t k_fields[UI_SLOT_MAX] = { UI_FIELD_WX_NOW, UI_FIELD_WX_TODAY, UI_FIELD_WX_HOURLY,
+                                                   UI_FIELD_ENV_TEMP, UI_FIELD_ENV_HUM };
+    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_fields, p->slots, UI_SLOT_MAX); /* the cells' fields, in preorder */
+    TEST_ASSERT_EQUAL_INT(5, ui_preset_slots(p));
+}
+
+static void test_a_split_preset_survives_a_round_trip(void)
+{
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(SPLIT_WEATHER, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"layout\":\"split\",\"in_cycle\":true,\"split\":{\"split\":\"rows\","
+                                                "\"ratio\":\"3/4\",\"line\":true,\"a\":{\"split\":\"columns\""),
+                                 s_json);
+    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"line\":false,\"a\":{\"field\":\"env.temp\"},\"b\":{\"field\":"
+                                                "\"env.hum\"}}},\"options\""),
+                                 s_json);
+    TEST_ASSERT_NULL_MESSAGE(strstr(s_json, "\"slots\""), s_json); /* a split preset has its tree instead */
+    ui_presets_t back;
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
+}
+
+static void test_a_split_preset_without_a_tree_is_one_empty_cell(void)
+{
+    const char *json = "{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"split\"},"
+                       " {\"id\": \"b\", \"layout\": \"split\", \"split\": {\"field\": \"time.clock\"}},"
+                       " {\"id\": \"c\", \"layout\": \"split\", \"split\": {}}]}";
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(json, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_INT(1, ui_preset_slots(&s_p.presets[0]));
+    TEST_ASSERT_EQUAL(UI_FIELD_NONE, s_p.presets[0].slots[0]);
+    TEST_ASSERT_EQUAL(UI_FIELD_TIME_CLOCK, s_p.presets[1].slots[0]); /* 400×279: the clock at XL */
+    TEST_ASSERT_EQUAL(UI_FIELD_NONE, s_p.presets[2].slots[0]);
+}
+
+/* A cell of the tree as JSON: `field` or empty; a split of two parts. */
+static int put_split(char *out, size_t size, const char *split, const char *ratio, const char *a, const char *b)
+{
+    return snprintf(out, size, "{\"split\": \"%s\", \"ratio\": \"%s\", \"a\": %s, \"b\": %s}", split, ratio, a, b);
+}
+
+/* presets.json with one split preset of `tree`, in s_json. */
+static const char *split_file(const char *tree)
+{
+    snprintf(s_json, sizeof(s_json),
+             "{\"schema\": 1, \"presets\": [{\"id\": \"t\", \"layout\": \"split\", \"split\": %s}]}", tree);
+    return s_json;
+}
+
+static void check_tree_rejected(const char *tree, const char *reason_part)
+{
+    check_rejected(split_file(tree), reason_part);
+}
+
+static void test_bad_split_trees_are_rejected_with_a_reason(void)
+{
+    static char tree[2048], part[1024];
+    /* nine cells, each at least 90×40: two rows of four, one of them split again */
+    put_split(part, sizeof(part), "columns", "1/2", "{}", "{}");
+    char row[512];
+    snprintf(row, sizeof(row), "{\"split\": \"columns\", \"ratio\": \"1/4\", \"a\": {}, \"b\": {\"split\": \"columns\","
+             " \"ratio\": \"1/3\", \"a\": {}, \"b\": %s}}", part);
+    char split_cell[256];
+    put_split(split_cell, sizeof(split_cell), "rows", "1/2", "{}", "{}");
+    char row9[512];
+    snprintf(row9, sizeof(row9), "{\"split\": \"columns\", \"ratio\": \"1/4\", \"a\": %s,"
+             " \"b\": {\"split\": \"columns\", \"ratio\": \"1/3\", \"a\": {}, \"b\": %s}}", split_cell, part);
+    put_split(tree, sizeof(tree), "rows", "1/2", row, row9);
+    check_tree_rejected(tree, "at most 8 cells");
+    put_split(tree, sizeof(tree), "rows", "1/2", row, row); /* eight are fine */
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(split_file(tree), &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_INT(8, ui_preset_slots(&s_p.presets[0]));
+
+    put_split(part, sizeof(part), "rows", "1/2", "{}", "{}");
+    put_split(tree, sizeof(tree), "rows", "1/4", part, "{}"); /* 69 px split in two: 34 */
+    check_tree_rejected(tree, "90×40");
+    check_tree_rejected("{\"split\": \"diagonal\", \"ratio\": \"1/2\", \"a\": {}, \"b\": {}}", "rows or columns");
+    check_tree_rejected("{\"split\": \"rows\", \"ratio\": \"2/5\", \"a\": {}, \"b\": {}}", "1/4, 1/3, 1/2, 2/3 or 3/4");
+    check_tree_rejected("{\"split\": \"rows\", \"ratio\": \"1/2\", \"a\": {}}", "both parts");
+    check_tree_rejected("{\"split\": \"rows\", \"ratio\": \"1/2\", \"a\": {}, \"b\": \"env.temp\"}", "both parts");
+    check_tree_rejected("{\"field\": \"env.cold\"}", "unknown field");
+    check_tree_rejected("{\"field\": 7}", "field id");
+    check_tree_rejected("[]", "split must be");
+    /* the rain map needs M: the bottom cell is 400×69 */
+    put_split(tree, sizeof(tree), "rows", "3/4", "{\"field\": \"time.clock\"}", "{\"field\": \"rain.map\"}");
+    check_tree_rejected(tree, "cell 2 (400×69) can't show rain.map");
+    check_rejected("{\"schema\": 1, \"presets\": [{\"id\": \"a\", \"layout\": \"split\","
+                   " \"slots\": {\"main\": \"env.temp\"}}]}",
+                   "no slot \"main\"");
+}
+
+/* The largest presets.json there can be: 16 split presets of 8 cells, in columns wherever a tree can
+ * have them, with every line hidden; the longest field ids a cell takes; names of control characters,
+ * which cJSON writes as six bytes each ("\u0001"); every option at its longest; 8 schedule entries
+ * that switch to a preset. */
+static void test_a_full_set_of_split_presets_fits_the_save_buffer(void)
+{
+    static const uint8_t k_tree[UI_SPLIT_NODES] = {
+        UI_RATIO_1_2 | UI_SPLIT_NO_LINE,
+        UI_RATIO_1_4 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0,
+        UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0,
+        UI_RATIO_1_4 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0,
+        UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0,
+    };
+    memset(&s_p, 0, sizeof(s_p));
+    for (int i = 0; i < UI_PRESET_MAX; i++) {
+        ui_preset_t *p = &s_p.presets[i];
+        snprintf(p->id, sizeof(p->id), "preset-%08d", i);
+        memset(p->name, 0x01, UI_PRESET_NAME_LEN - 1);
+        p->layout = UI_LAYOUT_SPLIT;
+        memcpy(p->split, k_tree, sizeof(k_tree));
+        for (int k = 0; k < UI_SPLIT_CELLS; k++) {
+            p->slots[k] = (uint8_t)(k % 2 ? UI_FIELD_POLLEN_MUGWORT : UI_FIELD_POLLEN_RAGWEED); /* fit narrow S */
+        }
+        p->clock = UI_CLOCK_12H;
+        p->stale_policy = UI_STALE_PLACEHOLDER;
+        p->status_battery = UI_STATUS_BAT_PERCENT | UI_STATUS_BAT_VOLTAGE | UI_STATUS_BAT_DAYS;
+    }
+    s_p.count = UI_PRESET_MAX;
+    s_p.cycle_interval_s = UI_CYCLE_MAX_S;
+    s_p.offered = UI_OFFERED_ALL;
+    s_p.schedule.count = UI_SCHEDULE_MAX;
+    for (int i = 0; i < UI_SCHEDULE_MAX; i++) {
+        s_p.schedule.entries[i] = (ui_schedule_entry_t){ .at_min = 600, .days = 0x7F, .action = UI_SCHED_PRESET,
+                                                          .preset = (uint8_t)i };
+    }
+    static char buf[UI_PRESETS_JSON_MAX];
+    size_t n = ui_presets_to_json(&s_p, buf, sizeof(buf));
+    TEST_ASSERT_TRUE_MESSAGE(n > 0, "the worst case must fit UI_PRESETS_JSON_MAX");
+    ui_presets_t back;
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(buf, &back, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
+}
+
+static void test_a_preset_counts_the_slots_its_layout_uses(void)
+{
+    TEST_ASSERT_EQUAL_INT(6, ui_preset_slots(&s_p.presets[0])); /* Home: Classic */
+    TEST_ASSERT_EQUAL_INT(0, ui_preset_slots(&s_p.presets[ui_presets_find(&s_p, "rain")]));
+    ui_preset_t split = { .layout = UI_LAYOUT_SPLIT };
+    TEST_ASSERT_EQUAL_INT(1, ui_preset_slots(&split));
+    memset(split.split, UI_RATIO_1_2, sizeof(split.split)); /* cut short: no cells */
+    TEST_ASSERT_EQUAL_INT(0, ui_preset_slots(&split));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -393,5 +557,11 @@ int main(void)
     RUN_TEST(test_slots_given_as_a_list_are_rejected);
     RUN_TEST(test_deep_nesting_is_rejected_before_parsing);
     RUN_TEST(test_a_full_set_fits_the_save_buffer);
+    RUN_TEST(test_the_spec_split_example_parses);
+    RUN_TEST(test_a_split_preset_survives_a_round_trip);
+    RUN_TEST(test_a_split_preset_without_a_tree_is_one_empty_cell);
+    RUN_TEST(test_bad_split_trees_are_rejected_with_a_reason);
+    RUN_TEST(test_a_full_set_of_split_presets_fits_the_save_buffer);
+    RUN_TEST(test_a_preset_counts_the_slots_its_layout_uses);
     return UNITY_END();
 }
```


`test/host/test_ui_catalog.c`:

```diff
--- a/test/host/test_ui_catalog.c
+++ b/test/host/test_ui_catalog.c
@@ -54,11 +54,14 @@ static void test_layouts_list_their_slots_with_rectangles_sizes_and_kinds(void)
     TEST_ASSERT_EQUAL_INT(400, num(s_root, "width"));
     TEST_ASSERT_EQUAL_INT(300, num(s_root, "height"));
     const cJSON *layouts = cJSON_GetObjectItemCaseSensitive(s_root, "layouts");
-    TEST_ASSERT_EQUAL_INT(6, cJSON_GetArraySize(layouts));
+    TEST_ASSERT_EQUAL_INT(7, cJSON_GetArraySize(layouts));
     const cJSON *radar = by_id(layouts, "radar"); /* M6: the radars draw their own map, without slots */
     TEST_ASSERT_NOT_NULL(radar);
     TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(radar, "slots")));
     TEST_ASSERT_NOT_NULL(by_id(layouts, "flights"));
+    const cJSON *split = by_id(layouts, "split"); /* M6b: its cells come from each preset's tree */
+    TEST_ASSERT_NOT_NULL(split);
+    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(split, "slots")));
     const cJSON *classic = by_id(layouts, "classic");
     TEST_ASSERT_NOT_NULL(classic);
     const cJSON *slots = cJSON_GetObjectItemCaseSensitive(classic, "slots");
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host --target test_ui_preset 2>&1 | grep error: | head -3; cmake --build build-host --target test_ui_catalog >/dev/null && ./build-host/test_ui_catalog | grep FAIL`
Expected: `use of undeclared identifier 'UI_LAYOUT_SPLIT'`, `no member named 'split' in 'ui_preset_t'`, `call to undeclared function 'ui_preset_slots'`; then `test_layouts_list_their_slots_with_rectangles_sizes_and_kinds:FAIL: Expected 7 Was 6`.

- [ ] **Step 3: The split layout and the preset's tree.** Every slots array grows to 8: the defaults' compound literals fill the rest with `UI_FIELD_NONE`. The file's limit grows to 20 KB, a quarter above the largest file, for what M7's fields may add.

`components/ui/include/ui_layout.h`:

```diff
--- a/components/ui/include/ui_layout.h
+++ b/components/ui/include/ui_layout.h
@@ -8,7 +8,7 @@
 /* Layouts (spec §5.2): fixed slot rectangles below a 20 px status bar. Pure C, host-buildable. */
 
 #define UI_STATUS_H 20
-#define UI_SLOT_MAX 6
+#define UI_SLOT_MAX 8 /* the split layout's cells (ui_split.h); the fixed layouts use up to 6 */
 
 typedef enum {
     UI_LAYOUT_CLASSIC,
@@ -17,6 +17,7 @@ typedef enum {
     UI_LAYOUT_FOCUS,
     UI_LAYOUT_RADAR,   /* M6: the weather radar's map, without slots (spec §5.2) */
     UI_LAYOUT_FLIGHTS, /* M6: the flight radar's map and panel, without slots */
+    UI_LAYOUT_SPLIT,   /* M6b: cells from the preset's own tree (ui_split.h, D31), without fixed slots */
     UI_LAYOUT_COUNT,
 } ui_layout_id_t;
 
```


`components/ui/ui_layout.c`:

```diff
--- a/components/ui/ui_layout.c
+++ b/components/ui/ui_layout.c
@@ -43,6 +43,7 @@ static const ui_layout_t k_layouts[UI_LAYOUT_COUNT] = {
     [UI_LAYOUT_FOCUS] = { "focus", k_focus, sizeof(k_focus) / sizeof(k_focus[0]) },
     [UI_LAYOUT_RADAR] = { "radar", NULL, 0 },
     [UI_LAYOUT_FLIGHTS] = { "flights", NULL, 0 },
+    [UI_LAYOUT_SPLIT] = { "split", NULL, 0 },
 };
 
 const ui_layout_t *ui_layout(ui_layout_id_t id)
```


`components/ui/include/ui_preset.h`:

```diff
--- a/components/ui/include/ui_preset.h
+++ b/components/ui/include/ui_preset.h
@@ -6,6 +6,7 @@
 #include <time.h>
 
 #include "ui_layout.h"
+#include "ui_split.h"
 
 /*
  * Presets (spec §5.4): a layout, its slot bindings and options, stored in /cfg/presets.json.
@@ -18,8 +19,9 @@
 #define UI_CYCLE_MIN_S 10
 #define UI_CYCLE_MAX_S 3600
 #define UI_SCHEDULE_MAX 8
-#define UI_PRESETS_JSON_MAX 8192 /* presets.json at its largest: 16 presets and 8 schedule entries */
-#define UI_JSON_MAX_DEPTH 16     /* the files nest 5 levels; anything deeper is rejected unparsed */
+#define UI_PRESETS_JSON_MAX 20480 /* presets.json at its largest is 16 253 bytes (test_ui_preset.c) */
+#define UI_JSON_MAX_DEPTH 16      /* presets.json nests 11 levels at most (a split tree's chain of 7 splits),
+                                     settings.json 5; anything deeper is rejected unparsed */
 
 typedef enum {
     UI_STALE_STALE,       /* show the value with its age (the default) */
@@ -45,7 +47,8 @@ typedef struct {
     char name[UI_PRESET_NAME_LEN];
     uint8_t layout; /* ui_layout_id_t */
     bool in_cycle;
-    uint8_t slots[UI_SLOT_MAX]; /* ui_field_id_t per slot, in the layout's slot order */
+    uint8_t slots[UI_SLOT_MAX]; /* ui_field_id_t per slot, in the layout's slot order, or per cell */
+    uint8_t split[UI_SPLIT_NODES]; /* the split layout's tree (ui_split.h); all 0 for the others */
     uint8_t clock;              /* ui_clock_mode_t */
     bool seconds;
     bool invert;
@@ -107,6 +110,10 @@ typedef struct {
     uint8_t offered; /* UI_OFFERED_* */
 } ui_presets_t;
 
+/* How many of `p`'s slots its layout uses: a fixed layout's slots, or the cells of its split tree
+ * (0 for a tree cut short). */
+int ui_preset_slots(const ui_preset_t *p);
+
 /* The built-in presets (spec §5.4): used when presets.json is missing or invalid. */
 void ui_presets_defaults(ui_presets_t *p);
 int ui_presets_find(const ui_presets_t *p, const char *id); /* index, or -1 */
```


`components/ui/ui_preset.c`:

```diff
--- a/components/ui/ui_preset.c
+++ b/components/ui/ui_preset.c
@@ -72,6 +72,16 @@ bool ui_presets_offer_builtins(ui_presets_t *p)
     return changed;
 }
 
+int ui_preset_slots(const ui_preset_t *p)
+{
+    if (p->layout == UI_LAYOUT_SPLIT) {
+        int nodes = ui_split_nodes(p->split);
+        return nodes > 0 ? (nodes + 1) / 2 : 0; /* a tree of n cells has 2n - 1 nodes */
+    }
+    const ui_layout_t *layout = ui_layout((ui_layout_id_t)p->layout);
+    return layout != NULL ? layout->slot_count : 0;
+}
+
 int ui_presets_find(const ui_presets_t *p, const char *id)
 {
     for (int i = 0; id != NULL && i < p->count; i++) {
```


- [ ] **Step 4: The tree in `presets.json`.** A cell's error names it by its place in preorder and its size ("cell 2 (400×69) can't show rain.map"), as the editor numbers the cells (Task 4).

`components/ui/ui_preset_json.c`:

```diff
--- a/components/ui/ui_preset_json.c
+++ b/components/ui/ui_preset_json.c
@@ -5,6 +5,7 @@
 #include "cJSON.h"
 #include "ui_fields.h"
 #include "ui_preset.h"
+#include "ui_split.h"
 #include "util_json.h"
 
 #define SCHEMA 1
@@ -111,6 +112,86 @@ static bool parse_slots(const cJSON *slots, const ui_layout_t *layout, ui_preset
     return true;
 }
 
+typedef struct {
+    ui_preset_t *out;
+    int nodes; /* taken so far, in preorder */
+    int cells;
+    char *err;
+    size_t size;
+} tree_t;
+
+/* A split tree's node and everything under it (spec §5.4). The file's depth is checked before it
+ * parses, so the recursion is at most UI_JSON_MAX_DEPTH deep. */
+static bool parse_node(tree_t *t, const cJSON *node)
+{
+    const char *id = t->out->id;
+    if (t->nodes >= UI_SPLIT_NODES) { /* a tree of n cells has 2n - 1 nodes */
+        return fail(t->err, t->size, "preset \"%s\": a split preset has at most %d cells", id, UI_SPLIT_CELLS);
+    }
+    const cJSON *split = cJSON_GetObjectItemCaseSensitive(node, "split");
+    if (split == NULL) {
+        t->out->split[t->nodes++] = 0;
+        const cJSON *field = cJSON_GetObjectItemCaseSensitive(node, "field");
+        if (field != NULL && !cJSON_IsNull(field) && !(cJSON_IsString(field) && field->valuestring[0] == '\0')) {
+            if (!cJSON_IsString(field)) {
+                return fail(t->err, t->size, "preset \"%s\": a cell's field must be a field id", id);
+            }
+            ui_field_id_t f = ui_field_by_name(field->valuestring);
+            if (f == UI_FIELD_NONE) {
+                return fail(t->err, t->size, "preset \"%s\": unknown field \"%s\"", id, field->valuestring);
+            }
+            t->out->slots[t->cells] = (uint8_t)f;
+        }
+        t->cells++;
+        return true;
+    }
+    const char *dir = cJSON_IsString(split) ? split->valuestring : "";
+    bool columns = strcmp(dir, "columns") == 0;
+    if (!columns && strcmp(dir, "rows") != 0) {
+        return fail(t->err, t->size, "preset \"%s\": a split is rows or columns", id);
+    }
+    const cJSON *ratio = cJSON_GetObjectItemCaseSensitive(node, "ratio");
+    int r = cJSON_IsString(ratio) ? ui_split_ratio_by_name(ratio->valuestring) : 0;
+    if (r == 0) {
+        return fail(t->err, t->size, "preset \"%s\": a split's ratio is 1/4, 1/3, 1/2, 2/3 or 3/4", id);
+    }
+    const cJSON *a = cJSON_GetObjectItemCaseSensitive(node, "a"), *b = cJSON_GetObjectItemCaseSensitive(node, "b");
+    if (!cJSON_IsObject(a) || !cJSON_IsObject(b)) {
+        return fail(t->err, t->size, "preset \"%s\": a split needs both parts, a and b, as objects", id);
+    }
+    t->out->split[t->nodes++] =
+        (uint8_t)(r | (columns ? UI_SPLIT_COLUMNS : 0) | (optional_bool(node, "line", true) ? 0 : UI_SPLIT_NO_LINE));
+    return parse_node(t, a) && parse_node(t, b);
+}
+
+/* The split layout's tree, then its geometry and what each cell can show (spec §5.2). */
+static bool parse_split(const cJSON *split, ui_preset_t *out, char *err, size_t size)
+{
+    if (split == NULL || cJSON_IsNull(split)) {
+        return true; /* one empty cell */
+    }
+    if (!cJSON_IsObject(split)) {
+        return fail(err, size, "preset \"%s\": split must be a tree of splits and cells", out->id);
+    }
+    tree_t t = { .out = out, .err = err, .size = size };
+    if (!parse_node(&t, split)) {
+        return false;
+    }
+    ui_split_geometry_t g;
+    if (!ui_split_layout(out->split, ui_split_area(), &g)) {
+        return fail(err, size, "preset \"%s\": a split's parts must be at least %d×%d", out->id, UI_SPLIT_MIN_W,
+                    UI_SPLIT_MIN_H);
+    }
+    for (int i = 0; i < g.cells; i++) {
+        const ui_field_info_t *info = ui_field_info((ui_field_id_t)out->slots[i]);
+        if (info != NULL && ui_split_field_size(info->kind, g.cell[i].w, g.cell[i].h) < 0) {
+            return fail(err, size, "preset \"%s\": cell %d (%d×%d) can't show %s", out->id, i + 1, g.cell[i].w,
+                        g.cell[i].h, info->id);
+        }
+    }
+    return true;
+}
+
 static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t size)
 {
     memset(out, 0, sizeof(*out));
@@ -137,6 +218,9 @@ static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t
         !parse_slots(slots, ui_layout((ui_layout_id_t)layout), out, err, size)) {
         return false;
     }
+    if (layout == UI_LAYOUT_SPLIT && !parse_split(cJSON_GetObjectItemCaseSensitive(item, "split"), out, err, size)) {
+        return false;
+    }
     const cJSON *options = cJSON_GetObjectItemCaseSensitive(item, "options");
     const cJSON *h24 = cJSON_GetObjectItemCaseSensitive(options, "clock_24h");
     out->clock = cJSON_IsBool(h24) ? (cJSON_IsTrue(h24) ? UI_CLOCK_24H : UI_CLOCK_12H) : UI_CLOCK_DEFAULT;
@@ -294,6 +378,26 @@ bool ui_presets_from_json(const char *json, ui_presets_t *out, char *err, size_t
     return ok;
 }
 
+/* A split tree's node and everything under it, in preorder: `at` the node, `cell` its first cell. */
+static cJSON *node_json(const ui_preset_t *p, int *at, int *cell)
+{
+    cJSON *obj = cJSON_CreateObject();
+    uint8_t node = p->split[(*at)++];
+    if ((node & UI_SPLIT_RATIO) == 0) {
+        const ui_field_info_t *info = ui_field_info((ui_field_id_t)p->slots[(*cell)++]);
+        if (info != NULL) {
+            cJSON_AddStringToObject(obj, "field", info->id);
+        }
+        return obj;
+    }
+    cJSON_AddStringToObject(obj, "split", node & UI_SPLIT_COLUMNS ? "columns" : "rows");
+    cJSON_AddStringToObject(obj, "ratio", ui_split_ratio_name(node & UI_SPLIT_RATIO));
+    cJSON_AddBoolToObject(obj, "line", !(node & UI_SPLIT_NO_LINE));
+    cJSON_AddItemToObject(obj, "a", node_json(p, at, cell));
+    cJSON_AddItemToObject(obj, "b", node_json(p, at, cell));
+    return obj;
+}
+
 static cJSON *preset_json(const ui_preset_t *p)
 {
     const ui_layout_t *layout = ui_layout((ui_layout_id_t)p->layout);
@@ -302,11 +406,17 @@ static cJSON *preset_json(const ui_preset_t *p)
     cJSON_AddStringToObject(obj, "name", p->name);
     cJSON_AddStringToObject(obj, "layout", layout->id);
     cJSON_AddBoolToObject(obj, "in_cycle", p->in_cycle);
-    cJSON *slots = cJSON_AddObjectToObject(obj, "slots");
-    for (int i = 0; i < layout->slot_count; i++) {
-        const ui_field_info_t *info = ui_field_info((ui_field_id_t)p->slots[i]);
-        if (info != NULL) {
-            cJSON_AddStringToObject(slots, layout->slots[i].name, info->id);
+    if (p->layout == UI_LAYOUT_SPLIT) {
+        int at = 0, cell = 0;
+        bool whole = ui_split_nodes(p->split) > 0; /* a tree cut short can't be walked: one empty cell */
+        cJSON_AddItemToObject(obj, "split", whole ? node_json(p, &at, &cell) : cJSON_CreateObject());
+    } else {
+        cJSON *slots = cJSON_AddObjectToObject(obj, "slots");
+        for (int i = 0; i < layout->slot_count; i++) {
+            const ui_field_info_t *info = ui_field_info((ui_field_id_t)p->slots[i]);
+            if (info != NULL) {
+                cJSON_AddStringToObject(slots, layout->slots[i].name, info->id);
+            }
         }
     }
     cJSON *options = cJSON_AddObjectToObject(obj, "options");
```


- [ ] **Step 5: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_ui_preset && ./build-host/test_ui_catalog && ctest --test-dir build-host`
Expected: `28 Tests 0 Failures 0 Ignored`; `3 Tests 0 Failures 0 Ignored`; `out of 61`, all passing.

- [ ] **Step 6: The device.** The snapshot holds 16 presets, so its layout changed (gotcha 29: a snapshot of another layout must not be read). The radar opens its map and file for presets that draw the rain map: a split preset's cells count. The web server's buffers take the largest backup.

`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -49,7 +49,7 @@
 #define TETHER_RECHECK_MS 1000
 #define RETRY_S           300  /* after a failed boot with no PC attached */
 #define SNAP_MAGIC        0x72666c62u /* "rflb" */
-#define SNAP_VERSION      7 /* 6: the weather, the air quality and the syncs' state; 7: the rain */
+#define SNAP_VERSION      8 /* 6: the weather, the air quality and the syncs' state; 7: the rain; 8: split presets */
 #define PEEK_MS           60000 /* a button during the night shows the dashboard this long (spec §9.1) */
 #define NIGHT_RECHECK_S   60    /* a night sleep with a button held looks again this often (D16) */
 #define CRITICAL_RECHECK_S 600  /* the critical sleep checks again this often if KEY is held */
```


`main/app_radar.c`:

```diff
--- a/main/app_radar.c
+++ b/main/app_radar.c
@@ -198,14 +198,13 @@ void app_radar_status(app_radar_status_t *out)
     out->source = source_now();
 }
 
-/* The preset draws the weather radar: the Radar layout, or a slot's map. */
+/* The preset draws the weather radar: the Radar layout, or a slot's or a cell's map. */
 static bool shows_weather(const ui_preset_t *p)
 {
     if (p->layout == UI_LAYOUT_RADAR) {
         return true;
     }
-    const ui_layout_t *layout = ui_layout((ui_layout_id_t)p->layout);
-    for (int i = 0; layout != NULL && i < layout->slot_count; i++) {
+    for (int i = 0; i < ui_preset_slots(p); i++) {
         if (p->slots[i] == UI_FIELD_RAIN_MAP) {
             return true;
         }
```


`components/webui/include/webui.h`:

```diff
--- a/components/webui/include/webui.h
+++ b/components/webui/include/webui.h
@@ -41,8 +41,8 @@ typedef struct {
     void (*event)(webui_event_t event);
 } webui_config_t;
 
-#define WEBUI_BODY_MAX  (16 * 1024) /* the largest request: a backup to restore */
-#define WEBUI_REPLY_MAX (20 * 1024) /* the largest reply: a BMP (15 662 bytes) or a backup */
+#define WEBUI_BODY_MAX  (24 * 1024) /* the largest request: a backup to restore, its presets up to 16 KB */
+#define WEBUI_REPLY_MAX (24 * 1024) /* the largest reply: a backup, or a BMP (15 662 bytes) */
 /* The deepest request: a backup bundle, a file's own 16 levels inside the bundle's two
  * (storage_backup.c). Deeper ones are refused before anything parses them. */
 #define WEBUI_JSON_MAX_DEPTH 18
```


`main/app_web.c`:

```diff
--- a/main/app_web.c
+++ b/main/app_web.c
@@ -359,6 +359,10 @@ static void learn(const char *body, uint8_t *out, size_t size, webui_reply_t *re
     reply_cjson(reply, out, size, o);
 }
 
+/* A backup of the largest files restores: settings.json up to the 2 KB app_ui.c keeps, presets.json up to
+ * its own limit, and the bundle around them. */
+_Static_assert(2048 + UI_PRESETS_JSON_MAX + 512 <= WEBUI_BODY_MAX, "a backup of the largest files fits a request");
+
 /* GET /api/backup (spec §14.4): the /cfg files as the firmware would save them now. */
 static void backup(uint8_t *out, size_t size, webui_reply_t *reply)
 {
```


- [ ] **Step 7: The firmware builds,** and the snapshot fits.

Run: `tools/idf.sh build 2>&1 | grep -c 'warning:'; tools/idf.sh exec xtensa-esp32s3-elf-nm -S build/reflbo.elf | grep ' s_snap$'`
Expected: `0`; `50000000 00000f30 d s_snap` (3 888 bytes; 3 616 before, at most 4 096).

- [ ] **Step 8: Commit.**

```bash
git add components/ui components/webui main test/host/test_ui_preset.c test/host/test_ui_catalog.c
git commit -m "feat(ui): split presets in presets.json"
```

### Task 3: Drawing split presets (`ui`)

**Files:**
- Modify: `components/ui/include/ui_dashboard.h`, `components/ui/ui_dashboard.c`, `components/ui/ui_widget.c` (numbers fit the height), `components/ui/ui_forecast.c` (the sun widget's faces), `test/host/dashboard_fixtures.h`, `test/host/test_ui_split.c`, `test/host/test_ui_widget_fit.c`
- Create: `test/host/golden/dash_split_weather.pbm`, `test/host/golden/dash_split_eight.pbm` (copied from `plan/m6b`, rendered again)

**Interfaces:**
- Consumes (Tasks 1, 2): `ui_split_layout()`, `ui_split_area()`, `ui_split_field_size()`, `ui_split_first()`, `UI_SPLIT_INSET`, `UI_SPLIT_MIN_W`, `UI_SPLIT_MIN_H`, `UI_SPLIT_CELLS`, `UI_SPLIT_COLUMNS`, `UI_SPLIT_NO_LINE`; `UI_LAYOUT_SPLIT`, `ui_preset_t.split`.
- Produces: `bool ui_draw_cell(gfx_fb_t *fb, gfx_rect_t cell, const ui_context_t *ctx, ui_field_id_t field, ui_stale_policy_t policy)` (true if the value shown is stale); the dashboard fixtures `split_weather` (spec §5.2's example, its bottom line hidden) and `split_eight` (eight cells: every ratio, rows and columns, a hidden line); `fixture_split()`.

The separators come first, 1 px in each split's gap and 8 px short of each end, then each cell's field at the size `ui_split_field_size()` gives it. A cell with no room for its field draws nothing rather than a cut widget; `presets.json` refuses such a tree anyway (Task 2).

The fit test draws every field the table allows in each of the 336 cell sizes a legal tree can make (the area, then both parts of each split, at most seven deep, while both parts stay at least 90 × 40), in eight sets of data. It finds two widgets that need fixing: a number's digits chosen by width alone, so in a short cell they, or a Czech comma's tail, run past its bottom edge; and the sun widget's 12-hour times in a 100 px cell. The first is also a fixed layout's bug: in Classic's `main` slot (400 × 125) the comma's tail of "23,4" is cut at the slot's edge, and the number reads "23.4".

- [ ] **Step 1: Write the failing tests.** The separators' exact pixels; Classic's main slot with a Czech temperature; every field in every cell a tree can make; and the goldens' fixtures:

`test/host/test_ui_split.c`:

```diff
--- a/test/host/test_ui_split.c
+++ b/test/host/test_ui_split.c
@@ -1,5 +1,8 @@
+#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */
+
 #include <string.h>
 
+#include "dashboard_fixtures.h"
 #include "ui_split.h"
 #include "unity.h"
 
@@ -208,6 +211,35 @@ static void test_ratios_have_names(void)
     TEST_ASSERT_NULL(ui_split_ratio_name(6));
 }
 
+/* A separator is a 1 px line in its split's gap, 8 px short of each end; a hidden one leaves the gap
+ * white, and the cells don't move (spec §5.2). */
+static void test_separators_are_drawn_unless_hidden(void)
+{
+    static uint8_t buf[400 * 300 / 8];
+    ui_context_t ctx = fixture_context();
+    ui_preset_t p = { .layout = UI_LAYOUT_SPLIT, .split = { ROWS(UI_RATIO_1_2), COLS(UI_RATIO_1_2) | NO_LINE, CELL,
+                                                            CELL, COLS(UI_RATIO_1_3), CELL, CELL } };
+    gfx_fb_t fb;
+    gfx_fb_init(&fb, buf, 400, 300);
+    ui_draw_dashboard(&fb, &ctx, &p); /* every cell empty: only the lines below the status bar */
+    for (int x = 0; x < 400; x++) { /* the rows' gap at y 160 */
+        TEST_ASSERT_EQUAL_INT_MESSAGE(x >= 8 && x < 392, gfx_get_pixel(&fb, x, 160), "the rows' line");
+    }
+    for (int y = 21; y < 160; y++) { /* the top columns' gap at x 200: hidden */
+        TEST_ASSERT_FALSE_MESSAGE(gfx_get_pixel(&fb, 200, y), "a hidden line");
+    }
+    for (int y = 161; y < 300; y++) { /* the bottom columns' gap at x 133, over 139 px */
+        TEST_ASSERT_EQUAL_INT_MESSAGE(y >= 169 && y < 292, gfx_get_pixel(&fb, 133, y), "the columns' line");
+    }
+    int ink = 0;
+    for (int y = 21; y < 300; y++) {
+        for (int x = 0; x < 400; x++) {
+            ink += gfx_get_pixel(&fb, x, y);
+        }
+    }
+    TEST_ASSERT_EQUAL_INT(384 + 123, ink); /* the two lines and nothing else */
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -223,5 +255,6 @@ int main(void)
     RUN_TEST(test_a_field_draws_at_the_largest_size_with_room_for_it);
     RUN_TEST(test_more_room_never_loses_a_field);
     RUN_TEST(test_ratios_have_names);
+    RUN_TEST(test_separators_are_drawn_unless_hidden);
     return UNITY_END();
 }
```


`test/host/test_ui_widget_fit.c`:

```diff
--- a/test/host/test_ui_widget_fit.c
+++ b/test/host/test_ui_widget_fit.c
@@ -6,6 +6,7 @@
 #include "dashboard_fixtures.h"
 #include "gfx.h"
 #include "ui_layout.h"
+#include "ui_split.h"
 #include "unity.h"
 
 /* Values too wide for their slot shrink to fit (spec §5.3): clipping would cut them at the
@@ -126,6 +127,128 @@ static void test_todays_two_digit_high_and_low_show_whole(void)
     check_low_shows(-124, -186, -156); /* the widest in °C */
 }
 
+/* A number fits its slot's height too (spec §5.3): in Czech the comma's tail of "23,4" in Classic's
+ * main slot (400×125, XL) reached its bottom edge and was cut there, so it read "23.4". */
+static void test_a_czech_number_fits_classics_main_slot(void)
+{
+    ui_context_t ctx;
+    ui_preset_t preset;
+    TEST_ASSERT_TRUE(fixture_dashboard("home_cs", &ctx, &preset));
+    preset.slots[0] = UI_FIELD_ENV_TEMP;
+    gfx_fb_init(&s_fb, s_buf, 400, 300);
+    ui_draw_dashboard(&s_fb, &ctx, &preset);
+    check_fits("home_cs, a temperature", UI_LAYOUT_CLASSIC, "main");
+    gfx_rect_t r = ui_layout(UI_LAYOUT_CLASSIC)->slots[0].rect;
+    TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + r.w - 1, r.y + r.h - 2, r.y + r.h - 1), "the slot's bottom edge");
+}
+
+/* The data a split cell's field is drawn from: 0 fresh in English, 1 fresh in Czech (its decimal
+ * comma and longer words), 2 three hours old in Czech with a forecast two days old, 3 nothing yet,
+ * 4 a 12-hour clock, 5 a hot day in °F (100.8 °F), 6 frost in Czech (-12,5 °C, a dew point of
+ * -18,7 °C), 7 the clock with its seconds. */
+static ui_context_t split_context(int variant)
+{
+    ui_context_t ctx = fixture_context();
+    ctx.lang = lang_get(variant == 1 || variant == 2 || variant == 6 ? "cs" : "en");
+    ctx.clock_24h = variant != 4;
+    ctx.fahrenheit = variant == 5;
+    ctx.seconds = variant == 7;
+    ctx.local = fixture_local(variant == 4 ? 12 : 20, variant == 4 ? 58 : 48, 37);
+    if (variant == 2) {
+        fixture_fill(&s_fix_ds, FIX_NOW - 3 * 3600);
+    } else if (variant == 5 || variant == 6) {
+        fixture_single(&s_fix_ds, variant == 5 ? 3820 : -1250, variant == 5 ? 3000 : 6000);
+    }
+    if (variant == 3) {
+        ds_init(&s_fix_ds);
+    } else {
+        fixture_forecast(&s_fix_ds, variant == 2 ? FIX_NOW - 50 * 3600 : FIX_NOW - 3600);
+        fixture_rain_now(&s_fix_ds);
+    }
+    return ctx;
+}
+
+/* Every cell size a legal split tree can make (spec §5.2): the area, then both parts of each split,
+ * at most 7 splits deep, while both parts stay at least 90×40. */
+static struct {
+    int16_t w, h;
+    uint8_t depth;
+} s_cells[512];
+static int s_cell_count;
+
+static void reach(int w, int h, int depth)
+{
+    int i = 0;
+    while (i < s_cell_count && (s_cells[i].w != w || s_cells[i].h != h)) {
+        i++;
+    }
+    if (i < s_cell_count && s_cells[i].depth <= depth) {
+        return; /* found before, with as many splits left */
+    }
+    if (i == s_cell_count) {
+        TEST_ASSERT_TRUE(s_cell_count < (int)(sizeof(s_cells) / sizeof(s_cells[0])));
+        s_cell_count++;
+    }
+    s_cells[i].w = (int16_t)w;
+    s_cells[i].h = (int16_t)h;
+    s_cells[i].depth = (uint8_t)depth;
+    for (int r = UI_RATIO_1_4; depth < UI_SPLIT_CELLS - 1 && r <= UI_RATIO_3_4; r++) {
+        int a = ui_split_first(w, r), b = w - a - 1; /* columns */
+        if (a >= UI_SPLIT_MIN_W && b >= UI_SPLIT_MIN_W) {
+            reach(a, h, depth + 1);
+            reach(b, h, depth + 1);
+        }
+        a = ui_split_first(h, r), b = h - a - 1; /* rows */
+        if (a >= UI_SPLIT_MIN_H && b >= UI_SPLIT_MIN_H) {
+            reach(w, a, depth + 1);
+            reach(w, b, depth + 1);
+        }
+    }
+}
+
+/* Split cells (spec §5.2, D31): in every cell a tree can make, every field it can show draws at the
+ * size ui_split.c gives it, shows, and stays clear of the cell's edges. The rain map fills its cell. */
+static void test_every_field_fits_every_cell_a_split_can_make(void)
+{
+    s_cell_count = 0;
+    reach(400, 279, 0);
+    TEST_ASSERT_EQUAL_INT(336, s_cell_count);
+    for (int variant = 0; variant < 8; variant++) {
+        ui_context_t ctx = split_context(variant);
+        for (int i = 0; i < s_cell_count; i++) {
+            int w = s_cells[i].w, h = s_cells[i].h;
+            for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
+                const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
+                int size = ui_split_field_size(info->kind, w, h);
+                if (size < 0 || info->kind == UI_FK_RAIN_MAP) {
+                    continue;
+                }
+                gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
+                gfx_fb_init(&s_fb, s_buf, 400, 300);
+                gfx_clear(&s_fb, GFX_WHITE);
+                ui_draw_cell(&s_fb, r, &ctx, (ui_field_id_t)f, UI_STALE_STALE);
+                char msg[80];
+                snprintf(msg, sizeof(msg), "%s at %d×%d (size %d), variant %d", info->id, w, h, size, variant);
+                TEST_ASSERT_TRUE_MESSAGE(inked(r.x + 2, r.x + w - 3, r.y + 2, r.y + h - 3), msg);
+                TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y, r.y + 1), msg);
+                TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + w - 1, r.y + h - 2, r.y + h - 1), msg);
+                TEST_ASSERT_FALSE_MESSAGE(inked(r.x, r.x + 1, r.y, r.y + h - 1), msg);
+                TEST_ASSERT_FALSE_MESSAGE(inked(r.x + w - 2, r.x + w - 1, r.y, r.y + h - 1), msg);
+            }
+        }
+    }
+}
+
+/* A field with no room in its cell isn't drawn at all, rather than cut. */
+static void test_a_field_without_room_draws_nothing(void)
+{
+    ui_context_t ctx = split_context(0);
+    gfx_fb_init(&s_fb, s_buf, 400, 300);
+    gfx_clear(&s_fb, GFX_WHITE);
+    ui_draw_cell(&s_fb, (gfx_rect_t){ 0, 21, 199, 69 }, &ctx, UI_FIELD_WX_HOURLY, UI_STALE_STALE); /* needs M */
+    TEST_ASSERT_FALSE(inked(0, 399, 0, 299));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -134,5 +257,8 @@ int main(void)
     RUN_TEST(test_a_negative_temperature_fits_a_small_cell);
     RUN_TEST(test_a_12_hour_clock_fits_a_grid_cell);
     RUN_TEST(test_todays_two_digit_high_and_low_show_whole);
+    RUN_TEST(test_a_czech_number_fits_classics_main_slot);
+    RUN_TEST(test_every_field_fits_every_cell_a_split_can_make);
+    RUN_TEST(test_a_field_without_room_draws_nothing);
     return UNITY_END();
 }
```


`test/host/dashboard_fixtures.h`:

```diff
--- a/test/host/dashboard_fixtures.h
+++ b/test/host/dashboard_fixtures.h
@@ -26,6 +26,16 @@ static inline ui_preset_t fixture_preset(const char *id)
     return all.presets[i < 0 ? 0 : i];
 }
 
+/* A preset on the split layout (M6b): its tree in preorder, and its cells' fields in their order. */
+static inline void fixture_split(ui_preset_t *p, const uint8_t *tree, size_t nodes, const uint8_t *fields, size_t cells)
+{
+    p->layout = UI_LAYOUT_SPLIT;
+    memset(p->split, 0, sizeof(p->split));
+    memcpy(p->split, tree, nodes);
+    memset(p->slots, 0, sizeof(p->slots));
+    memcpy(p->slots, fields, cells);
+}
+
 /* Each fixture: a context and a preset. */
 static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_preset_t *preset)
 {
@@ -234,6 +244,24 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
         radar->fl_always = false;
         radar->fl_updated = 0;
         ctx->radar = radar;
+    } else if (strcmp(name, "split_weather") == 0) { /* M6b: spec §5.2's example, its bottom line hidden */
+        *preset = fixture_preset("weather");
+        static const uint8_t k_tree[] = { UI_RATIO_3_4, UI_RATIO_1_2 | UI_SPLIT_COLUMNS, 0, UI_RATIO_1_2, 0, 0,
+                                          UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0 };
+        static const uint8_t k_fields[] = { UI_FIELD_WX_NOW, UI_FIELD_WX_TODAY, UI_FIELD_WX_HOURLY, UI_FIELD_ENV_TEMP,
+                                            UI_FIELD_ENV_HUM };
+        fixture_split(preset, k_tree, sizeof(k_tree), k_fields, sizeof(k_fields));
+        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
+    } else if (strcmp(name, "split_eight") == 0) { /* eight cells: each ratio, both ways, a hidden line */
+        *preset = fixture_preset("home");
+        static const uint8_t k_tree[] = { UI_RATIO_1_3, UI_RATIO_3_4 | UI_SPLIT_COLUMNS, 0, 0,
+                                          UI_RATIO_1_4 | UI_SPLIT_COLUMNS, UI_RATIO_1_2 | UI_SPLIT_NO_LINE, 0, 0,
+                                          UI_RATIO_2_3 | UI_SPLIT_COLUMNS, UI_RATIO_1_2, 0, 0, UI_RATIO_1_2, 0, 0 };
+        static const uint8_t k_fields[] = { UI_FIELD_TIME_CLOCK, UI_FIELD_DATE_DAY, UI_FIELD_ENV_TEMP,
+                                            UI_FIELD_ENV_HUM,    UI_FIELD_WX_NOW,   UI_FIELD_WX_HOURLY,
+                                            UI_FIELD_MOON_PHASE, UI_FIELD_BAT_LEVEL };
+        fixture_split(preset, k_tree, sizeof(k_tree), k_fields, sizeof(k_fields));
+        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
     } else if (strcmp(name, "grid_clock_12h") == 0) { /* a clock in a grid cell, 12-hour */
         *preset = fixture_preset("indoor");
         preset->slots[0] = UI_FIELD_TIME_CLOCK;
@@ -259,4 +287,5 @@ static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather",
                                                     "radar_stale_cs", "radar_loop", "radar_none",
                                                     "radar_rainviewer", "grid_rain_map", "weather_rain_map",
                                                     "flights", "flights_100", "flights_cs", "flights_none",
-                                                    "flights_failed", "flights_off" };
+                                                    "flights_failed", "flights_off", "split_weather",
+                                                    "split_eight" };
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host --target test_ui_widget_fit 2>&1 | grep -m1 error:; cmake --build build-host --target test_ui_split test_ui_dashboard_golden >/dev/null; ./build-host/test_ui_split | grep FAIL; ./build-host/test_ui_dashboard_golden | grep FAIL`
Expected: `call to undeclared function 'ui_draw_cell'`; `test_separators_are_drawn_unless_hidden:FAIL: Expected 1 Was 0. the rows' line`; the golden test's failure names `dash_split_weather.pbm`, which doesn't exist yet.

- [ ] **Step 3: The split layout on the dashboard.**

`components/ui/include/ui_dashboard.h`:

```diff
--- a/components/ui/include/ui_dashboard.h
+++ b/components/ui/include/ui_dashboard.h
@@ -7,3 +7,7 @@
 /* The dashboard screen (spec §5.5): the active preset's layout under the status bar. Rendering
  * is a pure function of the context and the preset. Pure C, host-buildable. */
 void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset);
+/* One field in a cell of the split layout, at the size the cell allows it (spec §5.2); nothing when
+ * the cell has no room for it. True if what it shows is stale. */
+bool ui_draw_cell(gfx_fb_t *fb, gfx_rect_t cell, const ui_context_t *ctx, ui_field_id_t field,
+                  ui_stale_policy_t policy);
```


`components/ui/ui_dashboard.c`:

```diff
--- a/components/ui/ui_dashboard.c
+++ b/components/ui/ui_dashboard.c
@@ -2,6 +2,7 @@
 
 #include "ui_internal.h"
 #include "ui_radar.h"
+#include "ui_split.h"
 
 static void draw_separators(gfx_fb_t *fb, ui_layout_id_t layout)
 {
@@ -32,6 +33,47 @@ static void draw_separators(gfx_fb_t *fb, ui_layout_id_t layout)
     }
 }
 
+bool ui_draw_cell(gfx_fb_t *fb, gfx_rect_t cell, const ui_context_t *ctx, ui_field_id_t field,
+                  ui_stale_policy_t policy)
+{
+    const ui_field_info_t *info = ui_field_info(field);
+    int size = info != NULL ? ui_split_field_size(info->kind, cell.w, cell.h) : -1;
+    if (size < 0) {
+        return false;
+    }
+    ui_value_t v;
+    ui_resolve(ctx, field, &v);
+    ui_widget_draw(fb, cell, (ui_size_t)size, &v, policy, ctx->lang);
+    return v.state == UI_VALUE_STALE;
+}
+
+/* The split layout (spec §5.2): each split's separator unless it is hidden, then each cell's field.
+ * Returns whether any value shown is stale. */
+static bool draw_split(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset)
+{
+    ui_split_geometry_t g;
+    if (!ui_split_layout(preset->split, ui_split_area(), &g)) {
+        return false; /* presets.json checks its trees: nothing to draw for one cut short */
+    }
+    for (int i = 0; i < g.lines; i++) {
+        const ui_split_line_t *l = &g.line[i];
+        if (l->node & UI_SPLIT_NO_LINE) {
+            continue;
+        }
+        if (l->node & UI_SPLIT_COLUMNS) {
+            gfx_vline(fb, l->at, l->rect.y + UI_SPLIT_INSET, l->rect.h - 2 * UI_SPLIT_INSET, GFX_BLACK);
+        } else {
+            gfx_hline(fb, l->rect.x + UI_SPLIT_INSET, l->at, l->rect.w - 2 * UI_SPLIT_INSET, GFX_BLACK);
+        }
+    }
+    bool any_stale = false;
+    for (int i = 0; i < g.cells; i++) {
+        any_stale |= ui_draw_cell(fb, g.cell[i], ctx, (ui_field_id_t)preset->slots[i],
+                                  (ui_stale_policy_t)preset->stale_policy);
+    }
+    return any_stale;
+}
+
 void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset)
 {
     ui_context_t c = *ctx;
@@ -49,6 +91,8 @@ void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t
         ui_draw_radar_view(fb, below, &c);
     } else if (preset->layout == UI_LAYOUT_FLIGHTS) {
         ui_draw_flights_view(fb, below, &c);
+    } else if (preset->layout == UI_LAYOUT_SPLIT) {
+        any_stale = draw_split(fb, &c, preset);
     } else if (layout != NULL) {
         draw_separators(fb, (ui_layout_id_t)preset->layout);
         for (int i = 0; i < layout->slot_count; i++) {
```


Run: `cmake --build build-host --target test_ui_widget_fit test_ui_split && ./build-host/test_ui_split | tail -1 && ./build-host/test_ui_widget_fit | grep -E 'FAIL|Tests'`
Expected: `OK` for `test_ui_split`; two failures in `test_ui_widget_fit` (Unity escapes the ×): `test_a_czech_number_fits_classics_main_slot:FAIL: the slot's bottom edge` and `test_every_field_fits_every_cell_a_split_can_make:FAIL: date.week at 400\xC3\x97123 (size 3), variant 0`.

- [ ] **Step 4: Numbers fit the height too.** At each font, largest first, the value as it is, then without its decimals, as spec §5.3 has it for the width; now each also needs room for its digits, centred as before, its tail below (a comma's, or its unit's: the "g" of "µg/m³") and 2 px to each edge. The fixed layouts' goldens don't change: every number in them already fits.

`components/ui/ui_widget.c`:

```diff
--- a/components/ui/ui_widget.c
+++ b/components/ui/ui_widget.c
@@ -103,6 +103,31 @@ static const gfx_bitmap_t *field_icon(ui_field_id_t field, int size)
     return size >= 48 ? s48 : s24;
 }
 
+/* How far `text`'s ink reaches below the baseline in `f`: a comma's tail, the "g" of "µg/m³". */
+static int ink_below(const gfx_font_t *f, const char *text)
+{
+    int below = 0;
+    for (const char *s = text; *s != '\0';) {
+        const gfx_glyph_t *g = gfx_font_glyph(f, gfx_utf8_next(&s));
+        if (g != NULL && g->y_offset + g->height > below) {
+            below = g->y_offset + g->height;
+        }
+    }
+    return below;
+}
+
+/* The number's digits centred in max_h, with room below for its tail and its unit's, and 2 px to
+ * each edge. */
+static bool fits_height(const ui_fonts_t *f, const gfx_font_t *vf, const ui_value_t *v, const char *value, int max_h)
+{
+    if (max_h <= 0) {
+        return true;
+    }
+    int below = ink_below(vf, value);
+    int unit_below = v->unit[0] ? ink_below(f->unit, v->unit) : 0;
+    return digit_height(vf) + 2 * (below > unit_below ? below : unit_below) + 4 <= max_h;
+}
+
 /* Draws value, unit and trend arrow as one group centred on cx; returns the group's width. */
 static int group_width(const ui_fonts_t *f, const gfx_font_t *vf, const ui_value_t *v, const char *value)
 {
@@ -128,18 +153,20 @@ static void draw_group(gfx_fb_t *fb, const ui_fonts_t *f, const gfx_font_t *vf,
     }
 }
 
-/* Picks the font and text for a number group so that it fits `max_w` (with `extra_w` beside it):
- * at each font, largest first, the value as it is, then without its decimals ("101" for "100.8").
+/* Picks the font and text for a number group so that it fits `max_w` (with `extra_w` beside it)
+ * and `max_h` (0: any height): at each font, largest first, the value as it is, then without its
+ * decimals ("101" for "100.8"; in Czech without the comma, whose tail reaches below the digits).
  * If even the smallest font is too wide, the value is cut with an ellipsis into `buf`. */
 static const gfx_font_t *fit_number(const ui_fonts_t *f, const gfx_font_t *const *fonts, int count,
-                                    const ui_value_t *v, const char **value, int max_w, int extra_w, char *buf,
-                                    size_t size)
+                                    const ui_value_t *v, const char **value, int max_w, int extra_w, int max_h,
+                                    char *buf, size_t size)
 {
     for (int i = 0; i < count; i++) {
-        if (group_width(f, fonts[i], v, *value) + extra_w <= max_w) {
+        if (group_width(f, fonts[i], v, *value) + extra_w <= max_w && fits_height(f, fonts[i], v, *value, max_h)) {
             return fonts[i];
         }
-        if (v->short_text[0] && group_width(f, fonts[i], v, v->short_text) + extra_w <= max_w) {
+        if (v->short_text[0] && group_width(f, fonts[i], v, v->short_text) + extra_w <= max_w &&
+            fits_height(f, fonts[i], v, v->short_text, max_h)) {
             *value = v->short_text;
             return fonts[i];
         }
@@ -298,7 +325,7 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
             gfx_text_ellipsize(vf, value, max_w, fit, sizeof(fit));
             value = fit;
         } else {
-            vf = fit_number(f, k_fit_s, 3, &shown, &value, max_w, 0, fit, sizeof(fit));
+            vf = fit_number(f, k_fit_s, 3, &shown, &value, max_w, 0, 0, fit, sizeof(fit));
         }
         int w = group_width(f, vf, &shown, value);
         int baseline = r.y + 12 + f->icon + 14 + digit_height(vf);
@@ -311,7 +338,7 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
         gfx_text_ellipsize(vf, value, r.x + r.w - 6 - x, fit, sizeof(fit));
         value = fit;
     } else {
-        vf = fit_number(f, k_fit_s, 3, &shown, &value, r.x + r.w - 6 - x, 0, fit, sizeof(fit));
+        vf = fit_number(f, k_fit_s, 3, &shown, &value, r.x + r.w - 6 - x, 0, 0, fit, sizeof(fit));
     }
     draw_group(fb, f, vf, &shown, value, x, r.y + (r.h + digit_height(vf)) / 2);
 }
@@ -372,11 +399,11 @@ static void draw_labelled(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_v
         const gfx_font_t *const *fonts;
         int count;
     } k_fit[] = { [UI_SIZE_M] = { k_fit_m, 3 }, [UI_SIZE_L] = { k_fit_l, 3 }, [UI_SIZE_XL] = { k_fit_xl, 4 } };
-    const gfx_font_t *vf =
-        fit_number(f, k_fit[size].fonts, k_fit[size].count, &shown, &value, body.w - 6, extra_w, fit, sizeof(fit));
+    int extra_lines = v->kind == UI_FK_BATTERY ? f->unit->line_height : 0;
+    const gfx_font_t *vf = fit_number(f, k_fit[size].fonts, k_fit[size].count, &shown, &value, body.w - 6, extra_w,
+                                      body.h - extra_lines, fit, sizeof(fit));
     int w = group_width(f, vf, &shown, value);
     int x = body.x + (body.w - w - extra_w) / 2;
-    int extra_lines = v->kind == UI_FK_BATTERY ? f->unit->line_height : 0;
     int baseline = body.y + (body.h + digit_height(vf) - extra_lines) / 2;
     draw_group(fb, f, vf, &shown, value, x, baseline);
     if (v->kind == UI_FK_TIME) {
```


Run: `cmake --build build-host --target test_ui_widget_fit && ./build-host/test_ui_widget_fit | grep -E 'FAIL|Tests'`
Expected: one failure, `sun.times at 100\xC3\x97279 (size 0), variant 4`: with a 12-hour clock "7:01 AM" runs past both edges of a 100 px cell.

- [ ] **Step 5: The sun widget steps down its face** until both times fit, before anything reaches the cell's edges. Classic's small slots (100 px) gain the same fix; it changes no golden, as no fixture puts the sun in one with a 12-hour clock.

`components/ui/ui_forecast.c`:

```diff
--- a/components/ui/ui_forecast.c
+++ b/components/ui/ui_forecast.c
@@ -718,9 +718,17 @@ static void draw_sun(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_
     const gfx_font_t *tf = size == UI_SIZE_S ? &gfx_font_sans_bold_16 : &gfx_font_sans_bold_20;
     int rows = 2 * 28 + (v->detail[0] && size != UI_SIZE_S ? 18 : 0);
     int y = top + (r.y + r.h - top - rows) / 2;
-    int w = 24 + 6 + gfx_text_width(tf, v->text);
-    int w2 = 24 + 6 + gfx_text_width(tf, v->extra);
-    w = w > w2 ? w : w2;
+    /* a 12-hour time ("7:01 AM") in a narrow split cell: smaller faces before it reaches the edges */
+    static const gfx_font_t *const k_smaller[] = { &gfx_font_sans_16, &gfx_font_sans_12 };
+    int w = 0;
+    for (int i = 0; i <= 2; i++) {
+        int w1 = 24 + 6 + gfx_text_width(tf, v->text), w2 = 24 + 6 + gfx_text_width(tf, v->extra);
+        w = w1 > w2 ? w1 : w2;
+        if (w <= r.w - 8 || i == 2) {
+            break;
+        }
+        tf = k_smaller[i];
+    }
     int x = r.x + (r.w - w) / 2;
     gfx_bitmap(fb, x, y, &gfx_icon_sunrise_24, GFX_BLACK);
     gfx_text(fb, tf, x + 30, y + 12 + ink_height(tf) / 2, v->text, GFX_BLACK);
```


Run: `cmake --build build-host --target test_ui_widget_fit && ./build-host/test_ui_widget_fit | tail -1`
Expected: `OK` (8 tests, about 2 s).

- [ ] **Step 6: The two goldens.** Copy them from the branch, render them again, and compare:

```bash
git checkout plan/m6b -- \
  test/host/golden/dash_split_eight.pbm \
  test/host/golden/dash_split_weather.pbm
```


```bash
cmake --build build-host --target render_dashboard
for f in split_weather split_eight; do
  ./build-host/render_dashboard $f /tmp/dash_$f.pbm && cmp /tmp/dash_$f.pbm test/host/golden/dash_$f.pbm
done
python3 tools/render.py >/dev/null   # then look at captures/render/dash_split_weather.png and dash_split_eight.png
```

Expected: `cmp` prints nothing. The Weather example: Weather now large on the left (200 × 209), today and the next hours on the right (199 × 104 each), the temperature and humidity in the slim bottom cells (69 px), drawn wide, with no line between them; the status bar's clock. The eight cells: the clock at M (300 × 93) beside the date, temperature over humidity without a line between them, Weather now over the next hours, the Moon over the battery.

- [ ] **Step 7: Run the tests.**

Run: `cmake --build build-host && ctest --test-dir build-host`
Expected: `out of 61`, all passing (the goldens of every other fixture unchanged).

- [ ] **Step 8: The firmware builds.** `tools/idf.sh build`: clean, without a warning.

- [ ] **Step 9: Commit.**

```bash
git add components/ui test/host/dashboard_fixtures.h test/host/test_ui_split.c test/host/test_ui_widget_fit.c \
        test/host/golden/dash_split_weather.pbm test/host/golden/dash_split_eight.pbm
git commit -m "feat(ui): draw split presets; numbers fit their slot's height"
```

### Task 4: The split rules in `GET /api/layouts`, and the editor (`ui`, `web`)

**Files:**
- Modify: `components/ui/include/ui_catalog.h`, `components/ui/ui_catalog.c`, `test/host/test_ui_catalog.c`, `web/app.js`, `web/style.css`, `test/web/test_app.mjs`

**Interfaces:**
- Consumes (Tasks 1, 2): `ui_split_area()`, `ui_split_min_w()`, `ui_split_min_h()`, `ui_split_need()`, `ui_split_ratio_name()`, `UI_SPLIT_*`; the split tree's JSON (Task 2).
- Produces: in `GET /api/layouts`, beside `layouts` (which now lists `split` without slots):
  ```json
  "split": { "x": 0, "y": 21, "w": 400, "h": 279, "cells": 8, "min_w": 90, "min_h": 40, "narrow_w": 150,
             "inset": 8, "ratios": ["1/4", "1/3", "1/2", "2/3", "3/4"],
             "sizes": [ { "size": "XL", "min_w": 400, "min_h": [120, 120], "kinds": { "time": [120, 120], … } },
                        …, { "size": "S", "min_w": 90, "min_h": [80, 40], "kinds": { "weather_now": [86, 61], … } } ] }
  ```
  `sizes` comes largest first; each pair is [narrower than `narrow_w`, not]; a kind a size doesn't take is absent. A cell's size is the first whose `min_w` and `min_h` it has; a field's, the first whose `min_w` and the kind's height it has.
- In `web/app.js`: `splitParts(node, r)`, `splitCells(node, r)`, `splitFits(node, r)`, `splitSize(w, h, kind)`, `slotsOf(p)`, `splitEditor(ed, p)`.

The editor computes what the device does, from the device's own numbers: the same rounding (the first part's share rounded down, a 1 px gap) and the same table, so it offers exactly the trees `presets.json` accepts. The C test checks the published rules against `ui_split_field_size()` over the whole range of cells.

- [ ] **Step 1: Write the failing catalogue test.**

`test/host/test_ui_catalog.c`:

```diff
--- a/test/host/test_ui_catalog.c
+++ b/test/host/test_ui_catalog.c
@@ -5,9 +5,10 @@
 #include "cJSON.h"
 #include "context_fixtures.h"
 #include "ui_catalog.h"
+#include "ui_split.h"
 #include "unity.h"
 
-static char s_out[8192];
+static char s_out[8192]; /* the layouts take 4 473 bytes with the split rules */
 static cJSON *s_root;
 
 void setUp(void)
@@ -83,6 +84,72 @@ static void test_layouts_list_their_slots_with_rectangles_sizes_and_kinds(void)
     TEST_ASSERT_EQUAL_STRING("rain_map", last->valuestring); /* a medium slot takes the rain map */
 }
 
+/* The size a field draws at by the published rules, as the editor reads them (web/app.js). */
+static const char *rule_size(const cJSON *split, const char *kind, int w, int h)
+{
+    int wide = w >= num(split, "narrow_w");
+    const cJSON *size;
+    cJSON_ArrayForEach(size, cJSON_GetObjectItemCaseSensitive(split, "sizes"))
+    {
+        const cJSON *need = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(size, "kinds"), kind);
+        if (w >= num(size, "min_w") && need != NULL && h >= cJSON_GetArrayItem(need, wide)->valueint) {
+            return str(size, "size");
+        }
+    }
+    return NULL;
+}
+
+/* M6b (spec §5.2, D31): the split layout's rules, so the editor offers each cell only the fields
+ * that fit it. */
+static void test_layouts_publish_the_split_rules(void)
+{
+    TEST_ASSERT_TRUE(ui_catalog_layouts_json(s_out, sizeof(s_out)) > 0);
+    s_root = cJSON_Parse(s_out);
+    const cJSON *split = cJSON_GetObjectItemCaseSensitive(s_root, "split");
+    TEST_ASSERT_NOT_NULL(split);
+    TEST_ASSERT_EQUAL_INT(0, num(split, "x"));
+    TEST_ASSERT_EQUAL_INT(21, num(split, "y"));
+    TEST_ASSERT_EQUAL_INT(400, num(split, "w"));
+    TEST_ASSERT_EQUAL_INT(279, num(split, "h"));
+    TEST_ASSERT_EQUAL_INT(8, num(split, "cells"));
+    TEST_ASSERT_EQUAL_INT(90, num(split, "min_w"));
+    TEST_ASSERT_EQUAL_INT(40, num(split, "min_h"));
+    TEST_ASSERT_EQUAL_INT(150, num(split, "narrow_w"));
+    TEST_ASSERT_EQUAL_INT(8, num(split, "inset"));
+    const cJSON *ratios = cJSON_GetObjectItemCaseSensitive(split, "ratios");
+    TEST_ASSERT_EQUAL_INT(5, cJSON_GetArraySize(ratios));
+    TEST_ASSERT_EQUAL_STRING("1/4", cJSON_GetArrayItem(ratios, 0)->valuestring);
+    TEST_ASSERT_EQUAL_STRING("3/4", cJSON_GetArrayItem(ratios, 4)->valuestring);
+    const cJSON *sizes = cJSON_GetObjectItemCaseSensitive(split, "sizes");
+    TEST_ASSERT_EQUAL_INT(4, cJSON_GetArraySize(sizes)); /* the largest first, as a cell takes the first that fits */
+    const cJSON *xl = cJSON_GetArrayItem(sizes, 0), *s = cJSON_GetArrayItem(sizes, 3);
+    TEST_ASSERT_EQUAL_STRING("XL", str(xl, "size"));
+    TEST_ASSERT_EQUAL_STRING("S", str(s, "size"));
+    TEST_ASSERT_EQUAL_INT(400, num(xl, "min_w"));
+    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(xl, "kinds"))); /* time, number */
+    const cJSON *s_min = cJSON_GetObjectItemCaseSensitive(s, "min_h");
+    TEST_ASSERT_EQUAL_INT(80, cJSON_GetArrayItem(s_min, 0)->valueint); /* narrow, then wide */
+    TEST_ASSERT_EQUAL_INT(40, cJSON_GetArrayItem(s_min, 1)->valueint);
+    const cJSON *wx = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(s, "kinds"), "weather_now");
+    TEST_ASSERT_EQUAL_INT(86, cJSON_GetArrayItem(wx, 0)->valueint);
+    TEST_ASSERT_EQUAL_INT(61, cJSON_GetArrayItem(wx, 1)->valueint);
+    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(s, "kinds"), "series"));
+    /* the rules give what the renderer does, everywhere */
+    static const char *const k_kinds[UI_FK_COUNT] = { "time", "date", "number", "battery", "moon", "text",
+                                                      "weather_now", "weather_day", "series", "sun", "level",
+                                                      "pollen", "rain_map" };
+    static const char *const k_names[] = { "S", "M", "L", "XL" };
+    for (int k = 0; k < UI_FK_COUNT; k++) {
+        for (int w = 90; w <= 400; w += 7) {
+            for (int h = 40; h <= 279; h += 3) {
+                int size = ui_split_field_size((ui_field_kind_t)k, w, h);
+                const char *published = rule_size(split, k_kinds[k], w, h);
+                TEST_ASSERT_EQUAL_STRING(size < 0 ? NULL : k_names[size], published);
+            }
+        }
+    }
+}
+
 /* GET /api/fields: the catalogue with values right now, labels in English (spec §5.8). */
 static void test_fields_carry_their_kind_label_and_current_value(void)
 {
@@ -115,6 +182,7 @@ int main(void)
 {
     UNITY_BEGIN();
     RUN_TEST(test_layouts_list_their_slots_with_rectangles_sizes_and_kinds);
+    RUN_TEST(test_layouts_publish_the_split_rules);
     RUN_TEST(test_fields_carry_their_kind_label_and_current_value);
     RUN_TEST(test_a_buffer_too_small_gives_nothing);
     return UNITY_END();
```


Run: `cmake --build build-host --target test_ui_catalog >/dev/null && ./build-host/test_ui_catalog | grep FAIL`
Expected: `test_layouts_publish_the_split_rules:FAIL: Expected Non-NULL`.

- [ ] **Step 2: The rules.**

`components/ui/include/ui_catalog.h`:

```diff
--- a/components/ui/include/ui_catalog.h
+++ b/components/ui/include/ui_catalog.h
@@ -7,7 +7,8 @@
 /* What the web UI's preset editor needs to know (spec §10.3), as JSON. Pure C, host-buildable. */
 
 /* GET /api/layouts: the panel size and every layout's slots, with their rectangles, size classes
- * and the field kinds each one takes. Returns the length written, or 0 if `size` is too small. */
+ * and the field kinds each one takes; and the split layout's rules (M6b). Returns the length
+ * written, or 0 if `size` is too small. */
 size_t ui_catalog_layouts_json(char *out, size_t size);
 /* GET /api/fields: every field with its kind, its label in English (the web UI's language, spec
  * §5.8) and its value right now in the device's language. 0 if `size` is too small. */
```


`components/ui/ui_catalog.c`:

```diff
--- a/components/ui/ui_catalog.c
+++ b/components/ui/ui_catalog.c
@@ -5,6 +5,7 @@
 
 #include "cJSON.h"
 #include "ui_layout.h"
+#include "ui_split.h"
 
 #define PANEL_W 400
 #define PANEL_H 300
@@ -34,6 +35,44 @@ static size_t print(cJSON *root, char *out, size_t size)
     return ok ? strlen(out) : 0;
 }
 
+/* The split layout's rules (spec §5.2): its area and limits, its ratios, and per size, largest first,
+ * the least cell and the least height each kind it takes needs, narrower than narrow_w or not. */
+static void add_split(cJSON *root)
+{
+    gfx_rect_t area = ui_split_area();
+    cJSON *split = cJSON_AddObjectToObject(root, "split");
+    cJSON_AddNumberToObject(split, "x", area.x);
+    cJSON_AddNumberToObject(split, "y", area.y);
+    cJSON_AddNumberToObject(split, "w", area.w);
+    cJSON_AddNumberToObject(split, "h", area.h);
+    cJSON_AddNumberToObject(split, "cells", UI_SPLIT_CELLS);
+    cJSON_AddNumberToObject(split, "min_w", UI_SPLIT_MIN_W);
+    cJSON_AddNumberToObject(split, "min_h", UI_SPLIT_MIN_H);
+    cJSON_AddNumberToObject(split, "narrow_w", UI_SPLIT_NARROW_W);
+    cJSON_AddNumberToObject(split, "inset", UI_SPLIT_INSET);
+    cJSON *ratios = cJSON_AddArrayToObject(split, "ratios");
+    for (int r = UI_RATIO_1_4; r <= UI_RATIO_3_4; r++) {
+        cJSON_AddItemToArray(ratios, cJSON_CreateString(ui_split_ratio_name(r)));
+    }
+    cJSON *sizes = cJSON_AddArrayToObject(split, "sizes");
+    for (int s = UI_SIZE_XL; s >= UI_SIZE_S; s--) {
+        cJSON *so = cJSON_CreateObject();
+        cJSON_AddStringToObject(so, "size", k_sizes[s]);
+        cJSON_AddNumberToObject(so, "min_w", ui_split_min_w((ui_size_t)s));
+        const int min_h[2] = { ui_split_min_h((ui_size_t)s, true), ui_split_min_h((ui_size_t)s, false) };
+        cJSON_AddItemToObject(so, "min_h", cJSON_CreateIntArray(min_h, 2));
+        cJSON *kinds = cJSON_AddObjectToObject(so, "kinds");
+        for (int k = 0; k < UI_FK_COUNT; k++) {
+            const int need[2] = { ui_split_need((ui_size_t)s, (ui_field_kind_t)k, true),
+                                  ui_split_need((ui_size_t)s, (ui_field_kind_t)k, false) };
+            if (need[1] >= 0) {
+                cJSON_AddItemToObject(kinds, k_kinds[k], cJSON_CreateIntArray(need, 2));
+            }
+        }
+        cJSON_AddItemToArray(sizes, so);
+    }
+}
+
 size_t ui_catalog_layouts_json(char *out, size_t size)
 {
     cJSON *root = cJSON_CreateObject();
@@ -65,6 +104,7 @@ size_t ui_catalog_layouts_json(char *out, size_t size)
         }
         cJSON_AddItemToArray(layouts, lo);
     }
+    add_split(root);
     return print(root, out, size);
 }
 
```


Run: `cmake --build build-host --target test_ui_catalog && ./build-host/test_ui_catalog | tail -1`
Expected: `OK` (4 tests; the layouts take 4 473 bytes).

- [ ] **Step 3: Write the failing page tests,** with the device's rules for the kinds they use:

`test/web/test_app.mjs`:

```diff
--- a/test/web/test_app.mjs
+++ b/test/web/test_app.mjs
@@ -502,3 +502,145 @@ test('a preset on a radar layout has no slots to fill', async () => {
   assert.match(text(main), /Rain radar Radar/); /* the list names its layout */
   assert.match(text(main), /This layout draws the weather radar/);
 });
+
+/* ---- the split layout's editor (M6b, spec §5.2, §10.3, D31) ---- */
+
+/* The device's rules (GET /api/layouts, ui_catalog.c) for the kinds these tests use. */
+const SPLIT = {
+  x: 0, y: 21, w: 400, h: 279, cells: 8, min_w: 90, min_h: 40, narrow_w: 150, inset: 8,
+  ratios: ['1/4', '1/3', '1/2', '2/3', '3/4'],
+  sizes: [
+    { size: 'XL', min_w: 400, min_h: [120, 120], kinds: { time: [120, 120], number: [120, 120] } },
+    { size: 'L', min_w: 200, min_h: [150, 150], kinds: { time: [150, 150], number: [150, 150] } },
+    { size: 'M', min_w: 130, min_h: [80, 80], kinds: { time: [80, 80], number: [80, 80], series: [80, 80] } },
+    { size: 'S', min_w: 90, min_h: [80, 40], kinds: { time: [80, 40], number: [80, 40] } },
+  ],
+};
+const SPLIT_CATALOGUE = { ...CATALOGUE, layouts: [...CATALOGUE.layouts, { id: 'split', slots: [] }], split: SPLIT };
+const SPLIT_FIELDS = { fields: [...FIELDS.fields, { id: 'wx.hourly', kind: 'series', label: 'Next hours', value: '' }] };
+
+/* A device with one preset, Home: Classic, or on the split layout with `tree`. */
+function splitDevice(saved, tree) {
+  const home = tree ? { id: 'home', name: 'Home', layout: 'split', in_cycle: true, split: tree, options: {} }
+                    : preset('home', 'Home', true);
+  const doc = { schema: 1, active: 'home', presets: [home], cycle: { enabled: false, interval_s: 60 },
+                schedule: { enabled: false, entries: [] } };
+  return {
+    'GET /api/layouts': () => reply(200, SPLIT_CATALOGUE),
+    'GET /api/presets': () => reply(200, JSON.parse(JSON.stringify(doc))),
+    'GET /api/fields': () => reply(200, SPLIT_FIELDS),
+    'POST /api/preview.bmp': () => reply(200, 'BM', 'image/bmp'),
+    'PUT /api/presets': (init) => { saved.push(JSON.parse(init.body)); return reply(200, JSON.parse(init.body)); },
+  };
+}
+
+async function change(el, value) {
+  if (el.attrs.type === 'checkbox') el.checked = value;
+  else el.value = value;
+  await Promise.all((el.listeners.change || []).map((fn) => fn({ target: el })));
+}
+
+const ratioSelects = (root) => below(root).filter((e) => e.tag === 'select' && options(e).includes('1/4'));
+const cellLabels = (root) => below(root).filter((e) => e.className === 'cell-name').map(text);
+
+async function savedTree(main, saved) {
+  await buttonNamed(main, 'Save').click();
+  await settle();
+  return saved.at(-1).presets[0];
+}
+
+test('switching a preset to Split starts from one cell holding its first field', async () => {
+  const saved = [];
+  const { ctx, main } = await load(splitDevice(saved));
+  await ctx.presetsPage();
+  await change(control(main, 'Layout'), 'split');
+  assert.deepEqual(cellLabels(main), ['1 · 400×279 · XL']);
+  const p = await savedTree(main, saved);
+  assert.deepEqual(p.split, { field: 'time.clock' });
+  assert.equal(p.slots, undefined, 'a split preset has its tree instead of slots');
+});
+
+test('Split into rows halves a cell, its field going to the first part', async () => {
+  const saved = [];
+  const { ctx, main } = await load(splitDevice(saved, { field: 'time.clock' }));
+  await ctx.presetsPage();
+  await buttonNamed(main, 'Split into rows').click();
+  assert.deepEqual(cellLabels(main), ['1 · 400×139 · XL', '2 · 400×139 · XL']); /* XL from 120 px */
+  const tags = below(main).filter((e) => e.className.split(' ').includes('slot-tag'));
+  assert.deepEqual(tags.map((e) => [text(e), e.style.left, e.style.top]),
+                   [['1', '100%', '7%'], ['2', '100%', '53.667%']]); /* each cell's number at its top right */
+  const p = await savedTree(main, saved);
+  assert.deepEqual(p.split, { split: 'rows', ratio: '1/2', line: true, a: { field: 'time.clock' }, b: {} });
+});
+
+test('a split offers only the ratios that leave every part 90×40', async () => {
+  const { ctx, main } = await load(splitDevice([], { split: 'rows', ratio: '1/2', line: true, a: {},
+                                                     b: { split: 'rows', ratio: '1/2', line: true, a: {}, b: {} } }));
+  await ctx.presetsPage();
+  const [outer, inner] = ratioSelects(main);
+  const disabled = (sel) => sel.children.filter((o) => o.disabled).map((o) => o.value);
+  assert.deepEqual(disabled(outer), ['3/4']);        /* 69 px below, split in two: 34 */
+  assert.deepEqual(disabled(inner), ['1/4', '3/4']); /* 139 px: 34 at a quarter */
+  const splitButtons = below(main).filter((e) => e.tag === 'button' && /^Split into/.test(text(e)));
+  assert.deepEqual(splitButtons.map((b) => b.disabled), [false, false, true, false, true, false]); /* 400×69 */
+});
+
+test('a cell offers only the fields that fit it', async () => {
+  const { ctx, main } = await load(splitDevice([], { split: 'rows', ratio: '3/4', line: true, a: {}, b: {} }));
+  await ctx.presetsPage();
+  const fieldSelects = below(main).filter((e) => e.tag === 'select' && options(e).includes(''));
+  assert.deepEqual(options(fieldSelects[0]), ['', 'time.clock', 'env.temp', 'wx.hourly']); /* 400×209 */
+  assert.deepEqual(options(fieldSelects[1]), ['', 'time.clock', 'env.temp']); /* 400×69: no series */
+});
+
+test('Join keeps the first field found inside the split', async () => {
+  const saved = [];
+  const { ctx, main } = await load(splitDevice(saved, { split: 'columns', ratio: '1/2', line: true, a: {},
+    b: { split: 'rows', ratio: '1/2', line: true, a: { field: 'env.temp' }, b: { field: 'time.clock' } } }));
+  await ctx.presetsPage();
+  await buttonNamed(main, 'Join').click(); /* the outer split's, the first */
+  const p = await savedTree(main, saved);
+  assert.deepEqual(p.split, { field: 'env.temp' });
+});
+
+test('each split keeps its own separator', async () => {
+  const saved = [];
+  const { ctx, main } = await load(splitDevice(saved, { split: 'rows', ratio: '1/2', line: true, a: {},
+                                                        b: { split: 'columns', ratio: '1/2', line: true, a: {}, b: {} } }));
+  await ctx.presetsPage();
+  const boxes = below(main).filter((e) => e.tag === 'label' && text(e) === 'Separator').map((l) => l.children[0]);
+  assert.equal(boxes.length, 2);
+  await change(boxes[1], false);
+  const p = await savedTree(main, saved);
+  assert.equal(p.split.line, true);
+  assert.equal(p.split.b.line, false);
+});
+
+test('a ratio that leaves a field without room empties its cell, and says so', async () => {
+  const saved = [];
+  const { ctx, main, byId } = await load(splitDevice(saved, { split: 'rows', ratio: '1/2', line: true, a: {},
+                                                              b: { field: 'wx.hourly' } }));
+  await ctx.presetsPage();
+  await change(ratioSelects(main)[0], '3/4'); /* the bottom cell: 400×69, too low for a series */
+  assert.equal(text(byId.toast), 'No room for Next hours');
+  const p = await savedTree(main, saved);
+  assert.deepEqual(p.split.b, {});
+});
+
+test('a split that leaves no room for the cell\'s field says so', async () => {
+  const saved = [];
+  const { ctx, main, byId } = await load(splitDevice(saved, { split: 'rows', ratio: '1/2', line: true,
+                                                              a: { field: 'wx.hourly' }, b: {} }));
+  await ctx.presetsPage();
+  await buttonNamed(main, 'Split into rows').click(); /* the first cell's: 400×69 halves, too low for a series */
+  assert.equal(text(byId.toast), 'No room for Next hours');
+  const p = await savedTree(main, saved);
+  assert.deepEqual(p.split.a, { split: 'rows', ratio: '1/2', line: true, a: {}, b: {} });
+});
+
+test('a cell says when its field draws at a smaller size than the cell\'s', async () => {
+  const { ctx, main } = await load(splitDevice([], { split: 'rows', ratio: '3/4', line: true,
+                                                     a: { field: 'wx.hourly' }, b: { field: 'env.temp' } }));
+  await ctx.presetsPage();
+  assert.deepEqual(cellLabels(main), ['1 · 400×209 · XL (Next hours at M)', '2 · 400×69 · S']);
+});
```


Run: `node --test test/web/test_app.mjs 2>&1 | grep -E '^# (pass|fail)'`
Expected: `# pass 25`, `# fail 9`.

- [ ] **Step 4: The editor.** Switching to Split starts from one cell holding the first field. A cell names its size and class, and its field's size when that is smaller ("3 · 199×104 · M (Pollen at S)"); it offers only the fields that fit, and splits at 1/2 with its line on, its field going to the first part if it fits there, else with "No room for …". A split offers only the ratios that leave every part 90 × 40, its separator and Join, which keeps the first field found inside it (it had room in a part, so it has room in the whole); a ratio that leaves a field without room empties that cell and says so. The preview numbers the cells as the boxes do.

`web/app.js`:

```diff
--- a/web/app.js
+++ b/web/app.js
@@ -842,7 +842,7 @@ async function devicePage() {
 /* ---- Presets (spec §5.4) ---- */
 
 const LAYOUT_NAMES = { classic: 'Classic', weather: 'Weather', grid: 'Grid', focus: 'Focus', radar: 'Radar',
-                       flights: 'Flights' };
+                       flights: 'Flights', split: 'Split' };
 const NO_SLOTS = { radar: 'This layout draws the weather radar: its centre and zoom are on the Radar page.',
                    flights: 'This layout draws the flight radar: its centre, range and filters are on the Radar page.' };
 const CYCLE_S = [10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600];
@@ -873,10 +873,10 @@ async function presetsPage() {
 }
 
 /* The preview, with each slot's name at its top right corner, as the slot fields below call them
- * (the renderer puts captions top left). */
-function previewBox(ed, layout) {
+ * (the renderer puts captions top left); a split preset's cells by their numbers. */
+function previewBox(ed, slots) {
   const pct = (v, of) => `${+(100 * v / of).toFixed(3)}%`;
-  return h('div', { class: 'preview' }, ed.img, layout.slots.map((slot) => {
+  return h('div', { class: 'preview' }, ed.img, slots.map((slot) => {
     const tag = h('span', { class: 'slot-tag', text: slot.id });
     tag.style.left = pct(slot.x + slot.w, catalogue.width);
     tag.style.top = pct(slot.y, catalogue.height);
@@ -888,6 +888,118 @@ function layoutOf(id) {
   return catalogue.layouts.find((l) => l.id === id) || catalogue.layouts[0];
 }
 
+/* ---- the split layout (spec §5.2, D31): its tree, by the rules GET /api/layouts publishes ---- */
+
+const splitArea = () => ({ x: catalogue.split.x, y: catalogue.split.y, w: catalogue.split.w, h: catalogue.split.h });
+
+/* A split's two parts of r: the first gets its ratio, rounded down, then a 1 px gap. */
+function splitParts(node, r) {
+  const [num, den] = node.ratio.split('/').map(Number);
+  if (node.split === 'columns') {
+    const first = Math.floor(r.w * num / den);
+    return [{ x: r.x, y: r.y, w: first, h: r.h }, { x: r.x + first + 1, y: r.y, w: r.w - first - 1, h: r.h }];
+  }
+  const first = Math.floor(r.h * num / den);
+  return [{ x: r.x, y: r.y, w: r.w, h: first }, { x: r.x, y: r.y + first + 1, w: r.w, h: r.h - first - 1 }];
+}
+
+/* The cells under `node` with their rectangles, in the device's order: its error messages count them so. */
+function splitCells(node, r, out = []) {
+  if (!node.split) out.push({ node, r });
+  else splitParts(node, r).forEach((part, i) => splitCells(i ? node.b : node.a, part, out));
+  return out;
+}
+
+/* Every part of the tree is at least min_w × min_h. */
+function splitFits(node, r) {
+  if (r.w < catalogue.split.min_w || r.h < catalogue.split.min_h) return false;
+  if (!node.split) return true;
+  const [a, b] = splitParts(node, r);
+  return splitFits(node.a, a) && splitFits(node.b, b);
+}
+
+/* A cell's size class, or the size a field of `kind` draws at in it: the first size, largest first, whose
+ * least width and height it has; null if none. */
+function splitSize(w, h, kind) {
+  const wide = w >= catalogue.split.narrow_w ? 1 : 0;
+  const size = catalogue.split.sizes.find((s) => {
+    const need = kind ? s.kinds[kind] : s.min_h;
+    return w >= s.min_w && need && h >= need[wide];
+  });
+  return size ? size.size : null;
+}
+
+const cellCount = (node) => (node.split ? cellCount(node.a) + cellCount(node.b) : 1);
+const firstField = (node) => (node.split ? firstField(node.a) || firstField(node.b) : node.field);
+
+/* The preview's tags: each cell's number where a slot's name goes. */
+function slotsOf(p) {
+  if (p.layout !== 'split') return layoutOf(p.layout).slots;
+  return splitCells(p.split, splitArea()).map(({ r }, i) => ({ id: String(i + 1), ...r }));
+}
+
+/* The tree as nested boxes (spec §10.3): a cell's size, class (and its field's, when smaller) and the
+ * fields that fit it, Split into rows or columns; a split's ratio, Separator and Join. */
+function splitEditor(ed, p) {
+  const fieldOf = (id) => ed.fields.find((f) => f.id === id);
+  let number = 0;
+  const box = (node, r, set) => {
+    if (!node.split) {
+      number++;
+      const fits = ed.fields.filter((f) => splitSize(r.w, r.h, f.kind));
+      const select = h('select', { onchange: (ev) => {
+        if (ev.target.value) node.field = ev.target.value;
+        else delete node.field;
+        changed(ed, false);
+      } }, h('option', { value: '' }, '(empty)'), fits.map((f) => h('option',
+        { value: f.id, selected: node.field === f.id }, `${f.label} — ${f.value || 'no data yet'}`)));
+      const splitButton = (dir, text) => {
+        const half = { split: dir, ratio: '1/2', line: true, a: {}, b: {} }; /* the field goes to the first part */
+        const b = button(text, () => {
+          const f = fieldOf(node.field), [a] = splitParts(half, r);
+          if (f && splitSize(a.w, a.h, f.kind)) half.a.field = f.id;
+          else if (f) toast(`No room for ${f.label}`);
+          set(half);
+          changed(ed, true);
+        });
+        b.disabled = cellCount(p.split) >= catalogue.split.cells || !splitFits(half, r);
+        return b;
+      };
+      const size = splitSize(r.w, r.h), f = fieldOf(node.field), at = f && splitSize(r.w, r.h, f.kind);
+      const smaller = at && at !== size ? ` (${f.label} at ${at})` : '';
+      const name = `${number} · ${r.w}×${r.h} · ${size || 'too small'}${smaller}`;
+      return h('div', { class: 'cell' }, h('label', { class: 'cell-name', text: name }),
+        select, actions(splitButton('rows', 'Split into rows'), splitButton('columns', 'Split into columns')));
+    }
+    const [ra, rb] = splitParts(node, r);
+    const ratio = h('select', { onchange: (ev) => {
+      node.ratio = ev.target.value;
+      const dropped = []; /* a smaller cell may lose its field */
+      for (const { node: cell, r: cr } of splitCells(p.split, splitArea())) {
+        const f = fieldOf(cell.field);
+        if (f && !splitSize(cr.w, cr.h, f.kind)) {
+          delete cell.field;
+          dropped.push(f.label);
+        }
+      }
+      if (dropped.length) toast(`No room for ${dropped.join(', ')}`);
+      changed(ed, true);
+    } }, catalogue.split.ratios.map((q) => h('option', { value: q, selected: node.ratio === q,
+                                                         disabled: !splitFits({ ...node, ratio: q }, r) }, q)));
+    return h('div', { class: 'split' }, h('div', { class: 'row' },
+      h('span', { class: 'muted', text: node.split === 'columns' ? 'Columns' : 'Rows' }), ratio,
+      h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: node.line !== false,
+        onchange: (ev) => { node.line = ev.target.checked; changed(ed, false); } }), 'Separator'),
+      button('Join', () => {
+        const f = firstField(node); /* it had room in a part, so it has room in the whole */
+        set(f ? { field: f } : {});
+        changed(ed, true);
+      })),
+    box(node.a, ra, (x) => { node.a = x; }), box(node.b, rb, (x) => { node.b = x; }));
+  };
+  return h('div', { class: 'tree' }, box(p.split, splitArea(), (x) => { p.split = x; }));
+}
+
 /* The device renders the preset as it stands in the editor (POST /api/preview.bmp). */
 function preview(ed) {
   clearTimeout(ed.timer);
@@ -939,8 +1051,17 @@ function renderPresets(ed) {
   const name = h('input', { type: 'text', maxlength: 23, value: p.name,
                             onchange: (ev) => { p.name = ev.target.value.trim() || p.id; changed(ed, true); } });
   const layout = h('select', { onchange: (ev) => {
-    const old = p.slots;
+    const old = p.slots || {};
+    const first = p.layout === 'split' ? firstField(p.split)
+                                       : layoutOf(p.layout).slots.map((s) => old[s.id]).find(Boolean);
     p.layout = ev.target.value;
+    if (p.layout === 'split') { /* spec §10.3: one cell, holding the first field */
+      const f = ed.fields.find((x) => x.id === first), all = splitArea();
+      delete p.slots;
+      p.split = f && splitSize(all.w, all.h, f.kind) ? { field: f.id } : {};
+      return changed(ed, true);
+    }
+    delete p.split;
     p.slots = {};
     for (const slot of layoutOf(p.layout).slots) { /* keep what still fits */
       const f = old[slot.id] && ed.fields.find((x) => x.id === old[slot.id]);
@@ -948,7 +1069,8 @@ function renderPresets(ed) {
     }
     changed(ed, true);
   } }, catalogue.layouts.map((l) => h('option', { value: l.id, selected: l.id === p.layout }, LAYOUT_NAMES[l.id] || l.id)));
-  const slots = h('div', { class: 'slots' }, layoutOf(p.layout).slots.map((slot) => h('div', { class: 'slot' },
+  const slots = p.layout === 'split' ? splitEditor(ed, p)
+    : h('div', { class: 'slots' }, layoutOf(p.layout).slots.map((slot) => h('div', { class: 'slot' },
     h('label', { text: `${slot.id} · ${slot.size}` }),
     h('select', { onchange: (ev) => {
       if (ev.target.value) p.slots[slot.id] = ev.target.value;
@@ -978,7 +1100,7 @@ function renderPresets(ed) {
       o.status_battery = ['percent', 'voltage', 'days'].filter((k) => battery.has(k));
       changed(ed, false);
     } }), text);
-  const editCard = card(`Edit ${p.name}`, previewBox(ed, layoutOf(p.layout)), ed.previewNote,
+  const editCard = card(`Edit ${p.name}`, previewBox(ed, slotsOf(p)), ed.previewNote,
     field('Name', name), field('Layout', layout), slots,
     NO_SLOTS[p.layout] ? h('p', { class: 'muted small', text: NO_SLOTS[p.layout] }) : null,
     field('Time format', clock), check('seconds', 'Show seconds'),
```


`web/style.css`:

```diff
--- a/web/style.css
+++ b/web/style.css
@@ -105,6 +105,10 @@ a { color: var(--accent); }
 }
 .slots { display: grid; grid-template-columns: 1fr; gap: 8px; }
 .slot label { margin: 0 0 4px; font-weight: 400; color: var(--muted); }
+.tree .split, .tree .cell { border: 1px solid var(--line); border-radius: 8px; padding: 8px 10px; margin: 8px 0 0; }
+.tree .split .split, .tree .split .cell { margin-left: 6px; }
+.tree .cell-name { margin: 0 0 4px; font-weight: 400; color: var(--muted); }
+.tree .actions { margin-top: 8px; }
 .days { display: flex; gap: 2px; flex-wrap: wrap; }
 .days label { display: flex; flex-direction: column; align-items: center; margin: 0; font-weight: 400; font-size: .8rem; }
 .entry { border-top: 1px solid var(--line); padding: 10px 0; }
```


Run: `node --test test/web/test_app.mjs 2>&1 | grep -E '^# (pass|fail)'`
Expected: `# pass 34`, `# fail 0`.

- [ ] **Step 5: Run the tests.**

Run: `cmake --build build-host && ctest --test-dir build-host`
Expected: `out of 61`, all passing (`web_unittests` runs the page's tests).

- [ ] **Step 6: The firmware builds** with the page embedded: `tools/idf.sh build`, clean, without a warning.

- [ ] **Step 7: Commit.**

```bash
git add components/ui/include/ui_catalog.h components/ui/ui_catalog.c test/host/test_ui_catalog.c
git commit -m "feat(ui): publish the split layout's rules in /api/layouts"
git add web/app.js web/style.css test/web/test_app.mjs
git commit -m "feat(web): build split presets in the preset editor"
```

### Task 5: BOOT double toggles sync mode `always` (`storage`, `locale`, `main`)

**Files:**
- Modify: `components/storage/include/settings.h`, `components/storage/settings.c`, `test/host/test_settings.c`, `components/locale/include/lang.h`, `components/locale/lang_en.c`, `components/locale/lang_cs.c`, `main/app_menu.c`, `main/app_internal.h`, `main/app_sync.c`, `main/app.c`, `main/app_ui.c`

**Interfaces:**
- Consumes: M5's settings codec and `k_sync_modes`; `app_sync.c`'s `networks_saved()` and `always_wanted()`; `app_ui_save_settings()`, `app_clock_moved()`, `app_ui_toast()`, `app_ui_night()`, `app_uptime_ms()`.
- Produces:
  - `settings_t.sync_mode_before_always` (a `settings_sync_mode_t`, never `always`; default times), `sync.mode_before_always` in `settings.json`;
  - `void settings_remember_mode(settings_t *s, uint8_t prev_mode)`: after `sync_mode` changed from `prev_mode`, entering `always` remembers `prev_mode`;
  - `void settings_replaced(settings_t *s, const settings_t *before)`: the same for a whole document, unless it names its own `mode_before_always`;
  - `void settings_toggle_always(settings_t *s)`;
  - `LS_T_ALWAYS_ON` ("Always on: Wi-Fi stays on"), `LS_T_ALWAYS_FROM` ("Always on: Wi-Fi from", followed by " 06:00"), `LS_T_SYNC_MODE` ("Sync", followed by ": <mode>");
  - `void app_sync_toggle_always(void)`: the toggle with its toast; `bool app_menu_closed_within(int64_t ms)`.

The default lives in `components/storage` (`settings_sync_defaults()`), where the host tests see it (AGENTS §8). The toggle saves and reschedules exactly as the menu's mode edit does (`app_clock_moved(0)`): turning `always` on, the next tick starts a sync, which brings Wi-Fi up, unless quiet hours or a night hold it off, as the toast then says; turning it off, `app_sync_wifi_check()` stops Wi-Fi once nothing needs it. The dashboard's gesture config now binds BOOT double, so the gesture engine waits its 300 ms window before a BOOT short (spec §5.6); the menu and config mode keep their own configs, without it, and a BOOT double within a second of the menu closing is the presses that backed out of it, so it does nothing. On the critical-battery screen and the first-run screen `handle_button()` takes every gesture before the dashboard's; the toggle refuses a critical battery itself too.

- [ ] **Step 1: Write the failing tests.**

`test/host/test_settings.c`:

```diff
--- a/test/host/test_settings.c
+++ b/test/host/test_settings.c
@@ -328,6 +328,7 @@ static void test_the_sync_defaults_are_the_specs(void)
     TEST_ASSERT_EQUAL_UINT16(360, defaults.quiet_to);
     TEST_ASSERT_EQUAL_STRING("cz.pool.ntp.org", defaults.ntp[0]);
     TEST_ASSERT_EQUAL_STRING("pool.ntp.org", defaults.ntp[1]);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_TIMES, defaults.sync_mode_before_always); /* M6b (D31) */
     /* A file saved before M5 has neither sync.* nor time.ntp: it syncs at 05:30 */
     const char *json = "{\"schema\":1,\"language\":\"cs\",\"time\":{\"clock_24h\":false}}";
     TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &defaults, &s_out, s_err, sizeof(s_err)), s_err);
@@ -421,6 +422,73 @@ static void test_a_centre_that_follows_the_location_is_not_saved(void)
     TEST_ASSERT_EQUAL_UINT8(28, s_out.wx_zoom_q);
 }
 
+/* sync.mode_before_always (spec §14.3, D31): what BOOT double turns `always` back into; never `always`. */
+static void test_the_mode_before_always_parses_and_round_trips(void)
+{
+    const char *json = "{\"schema\":1,\"sync\":{\"mode\":\"always\",\"mode_before_always\":\"interval\"}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_ALWAYS, s_out.sync_mode);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_INTERVAL, s_out.sync_mode_before_always);
+    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"mode_before_always\":\t\"interval\""), s_json);
+    settings_t again;
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(&s_out, &again, sizeof(again));
+    static const char *const k_ignored[] = { "\"always\"", "\"sometimes\"", "3" };
+    for (size_t i = 0; i < sizeof(k_ignored) / sizeof(k_ignored[0]); i++) {
+        snprintf(s_json, sizeof(s_json), "{\"schema\":1,\"sync\":{\"mode_before_always\":%s}}", k_ignored[i]);
+        TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+        TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_SYNC_TIMES, s_out.sync_mode_before_always, k_ignored[i]);
+    }
+}
+
+/* BOOT double on the dashboard (spec §5.6, D31): `always` on, remembering the mode before, then back. */
+static void test_boot_double_toggles_always_and_back(void)
+{
+    settings_t s = s_defaults;
+    s.sync_mode = SETTINGS_SYNC_INTERVAL;
+    settings_toggle_always(&s);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_ALWAYS, s.sync_mode);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_INTERVAL, s.sync_mode_before_always);
+    settings_toggle_always(&s);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_INTERVAL, s.sync_mode);
+    s.sync_mode = SETTINGS_SYNC_MANUAL;
+    settings_toggle_always(&s);
+    settings_toggle_always(&s);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, s.sync_mode);
+}
+
+/* The menu or a page that turns `always` on remembers the mode it left too, so BOOT double returns to it. */
+static void test_entering_always_remembers_the_mode_it_left(void)
+{
+    settings_t s = s_defaults;
+    s.sync_mode = SETTINGS_SYNC_ALWAYS;
+    settings_remember_mode(&s, SETTINGS_SYNC_MANUAL);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, s.sync_mode_before_always);
+    settings_remember_mode(&s, SETTINGS_SYNC_ALWAYS); /* `always` again: nothing left */
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, s.sync_mode_before_always);
+    s.sync_mode = SETTINGS_SYNC_INTERVAL; /* leaving `always` keeps what it remembers */
+    settings_remember_mode(&s, SETTINGS_SYNC_ALWAYS);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, s.sync_mode_before_always);
+}
+
+/* A page's settings or a restored backup replace them whole (spec §14.3): entering `always` remembers
+ * the mode left, unless the document names a mode_before_always of its own. */
+static void test_settings_that_replace_others_remember_the_mode_left(void)
+{
+    settings_t before = s_defaults;
+    before.sync_mode = SETTINGS_SYNC_INTERVAL;
+    settings_t next = before; /* the Sync page: the mode alone, merged into the file */
+    next.sync_mode = SETTINGS_SYNC_ALWAYS;
+    settings_replaced(&next, &before);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_INTERVAL, next.sync_mode_before_always);
+    next = before; /* a backup taken in `always`, with the mode it had left */
+    next.sync_mode = SETTINGS_SYNC_ALWAYS;
+    next.sync_mode_before_always = SETTINGS_SYNC_MANUAL;
+    settings_replaced(&next, &before);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, next.sync_mode_before_always);
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -441,6 +509,10 @@ int main(void)
     RUN_TEST(test_a_learned_battery_curve_parses_and_round_trips);
     RUN_TEST(test_learned_without_a_usable_curve_keeps_the_built_in_one);
     RUN_TEST(test_the_sync_settings_parse_and_round_trip);
+    RUN_TEST(test_the_mode_before_always_parses_and_round_trips);
+    RUN_TEST(test_boot_double_toggles_always_and_back);
+    RUN_TEST(test_entering_always_remembers_the_mode_it_left);
+    RUN_TEST(test_settings_that_replace_others_remember_the_mode_left);
     RUN_TEST(test_sync_times_are_sorted_without_repeats_or_bad_ones);
     RUN_TEST(test_sync_settings_fall_back_one_by_one);
     RUN_TEST(test_the_sync_defaults_are_the_specs);
```


Run: `cmake --build build-host --target test_settings 2>&1 | grep -o "error: [^;]*" | sort | uniq -c`
Expected: `10 error: no member named 'sync_mode_before_always' in 'settings_t'`, and one `call to undeclared function` each for `settings_remember_mode`, `settings_replaced` and `settings_toggle_always`.

- [ ] **Step 2: The remembered mode.**

`components/storage/include/settings.h`:

```diff
--- a/components/storage/include/settings.h
+++ b/components/storage/include/settings.h
@@ -57,6 +57,7 @@ typedef struct {
     uint16_t bat_learned_mv[SETTINGS_BAT_CURVE_POINTS]; /* all 0 until a discharge was learned */
     uint32_t bat_learned_at;                            /* UTC seconds, 0 for never */
     uint8_t sync_mode;                                  /* settings_sync_mode_t */
+    uint8_t sync_mode_before_always;                    /* what BOOT double returns to; never `always` (D31) */
     uint8_t sync_time_count;                            /* 1..SETTINGS_SYNC_TIMES_MAX */
     uint16_t sync_times[SETTINGS_SYNC_TIMES_MAX];       /* minutes after midnight, ascending, no repeats */
     uint16_t sync_interval_min;                         /* 15..1440 */
@@ -75,10 +76,20 @@ typedef struct {
 } settings_t;
 
 /* The sync's and the NTP servers' defaults (spec §14.3): times mode at 05:30, a 60 min interval,
- * quiet hours 23:00-06:00 but off, cz.pool.ntp.org and pool.ntp.org. The app's defaults include
- * them, so a settings.json from before M5 syncs. */
+ * quiet hours 23:00-06:00 but off, cz.pool.ntp.org and pool.ntp.org; times again after `always`.
+ * The app's defaults include them, so a settings.json from before M5 syncs. */
 void settings_sync_defaults(settings_t *out);
 
+/* sync.mode just changed from `prev_mode` (the menu, a page): entering `always` from another mode
+ * remembers that one in sync_mode_before_always (D31). */
+void settings_remember_mode(settings_t *s, uint8_t prev_mode);
+/* `s` replaces `before` whole (a page's settings, a restored backup): entering `always` remembers
+ * the mode left, unless `s` names a mode_before_always of its own. */
+void settings_replaced(settings_t *s, const settings_t *before);
+/* BOOT double on the dashboard (spec §5.6, D31): sync mode `always` on, remembering the mode
+ * before, or off, back to the mode it remembers. */
+void settings_toggle_always(settings_t *s);
+
 /* The radars' defaults (spec §14.3): zoom 6.5; a range of 50 km, every altitude, none on the ground,
  * 100 aircraft; both centres on out->lat_e4 and lon_e4, so set the location first. */
 void settings_radar_defaults(settings_t *out);
```


`components/storage/settings.c`:

```diff
--- a/components/storage/settings.c
+++ b/components/storage/settings.c
@@ -189,6 +189,12 @@ static void read_sync(const cJSON *sync, settings_t *out)
             out->sync_mode = (uint8_t)i;
         }
     }
+    const cJSON *before = child(sync, "mode_before_always");
+    for (size_t i = 0; cJSON_IsString(before) && i < sizeof(k_sync_modes) / sizeof(k_sync_modes[0]); i++) {
+        if (i != SETTINGS_SYNC_ALWAYS && strcmp(before->valuestring, k_sync_modes[i]) == 0) {
+            out->sync_mode_before_always = (uint8_t)i;
+        }
+    }
     read_times(child(sync, "times"), out);
     out->sync_interval_min = (uint16_t)read_scaled(sync, "interval_min", out->sync_interval_min, 1, 15, 1440);
     const cJSON *quiet = child(sync, "quiet");
@@ -237,6 +243,7 @@ void settings_radar_defaults(settings_t *out)
 void settings_sync_defaults(settings_t *out)
 {
     out->sync_mode = SETTINGS_SYNC_TIMES;
+    out->sync_mode_before_always = SETTINGS_SYNC_TIMES;
     out->sync_time_count = 1;
     memset(out->sync_times, 0, sizeof(out->sync_times));
     out->sync_times[0] = 5 * 60 + 30;
@@ -249,6 +256,28 @@ void settings_sync_defaults(settings_t *out)
     snprintf(out->ntp[1], SETTINGS_HOST_LEN, "%s", "pool.ntp.org");
 }
 
+void settings_remember_mode(settings_t *s, uint8_t prev_mode)
+{
+    bool from_other = prev_mode != SETTINGS_SYNC_ALWAYS && prev_mode <= SETTINGS_SYNC_MANUAL;
+    if (s->sync_mode == SETTINGS_SYNC_ALWAYS && from_other) {
+        s->sync_mode_before_always = prev_mode;
+    }
+}
+
+void settings_replaced(settings_t *s, const settings_t *before)
+{
+    if (s->sync_mode_before_always == before->sync_mode_before_always) {
+        settings_remember_mode(s, before->sync_mode);
+    }
+}
+
+void settings_toggle_always(settings_t *s)
+{
+    uint8_t prev = s->sync_mode;
+    s->sync_mode = prev == SETTINGS_SYNC_ALWAYS ? s->sync_mode_before_always : SETTINGS_SYNC_ALWAYS;
+    settings_remember_mode(s, prev);
+}
+
 bool settings_from_json(const char *json, const settings_t *defaults, settings_t *out, char *err, size_t err_size)
 {
     if (util_json_depth(json) > SETTINGS_JSON_MAX_DEPTH) {
@@ -396,6 +425,9 @@ size_t settings_to_json(const settings_t *s, const char *base_json, char *out, s
     cJSON *sync = object_at(root, "sync");
     put(sync, "mode", cJSON_CreateString(k_sync_modes[s->sync_mode < SETTINGS_SYNC_MANUAL ? s->sync_mode
                                                                                          : SETTINGS_SYNC_MANUAL]));
+    uint8_t before = s->sync_mode_before_always;
+    bool known = before <= SETTINGS_SYNC_MANUAL && before != SETTINGS_SYNC_ALWAYS;
+    put(sync, "mode_before_always", cJSON_CreateString(k_sync_modes[known ? before : SETTINGS_SYNC_TIMES]));
     cJSON *times = cJSON_CreateArray();
     for (int i = 0; i < s->sync_time_count && i < SETTINGS_SYNC_TIMES_MAX; i++) {
         cJSON_AddItemToArray(times, hhmm_json(s->sync_times[i]));
```


Run: `cmake --build build-host --target test_settings && ./build-host/test_settings | tail -1`
Expected: `OK` (27 tests).

- [ ] **Step 3: The toasts,** in both packs:

`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -168,6 +168,9 @@ typedef enum {
     LS_T_SYNC_DONE,
     LS_T_SYNC_FAILED,
     LS_T_NO_NETWORK,
+    LS_T_ALWAYS_ON,   /* BOOT double turned sync mode `always` on (D31) */
+    LS_T_ALWAYS_FROM, /* the same in quiet hours or a night: followed by " 06:00", when Wi-Fi comes */
+    LS_T_SYNC_MODE, /* followed by ": <mode>", the mode BOOT double returned to */
     LS_UV_INDEX,
     LS_UV_LOW, /* the UV bands, in weather_uv_band_t order (D26) */
     LS_UV_MODERATE,
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -176,6 +176,9 @@ const lang_t lang_en = {
         [LS_T_SYNC_DONE] = "Sync done",
         [LS_T_SYNC_FAILED] = "Sync failed",
         [LS_T_NO_NETWORK] = "No Wi-Fi network saved",
+        [LS_T_ALWAYS_ON] = "Always on: Wi-Fi stays on",
+        [LS_T_ALWAYS_FROM] = "Always on: Wi-Fi from",
+        [LS_T_SYNC_MODE] = "Sync",
         [LS_UV_INDEX] = "UV index",
         [LS_UV_LOW] = "Low",
         [LS_UV_MODERATE] = "Moderate",
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -216,6 +216,9 @@ const lang_t lang_cs = {
         [LS_T_SYNC_DONE] = "Synchronizováno",
         [LS_T_SYNC_FAILED] = "Synchronizace selhala",
         [LS_T_NO_NETWORK] = "Žádná uložená síť Wi-Fi",
+        [LS_T_ALWAYS_ON] = "Stále: Wi-Fi zůstává zapnutá",
+        [LS_T_ALWAYS_FROM] = "Stále: Wi-Fi od",
+        [LS_T_SYNC_MODE] = "Synchronizace",
         [LS_UV_INDEX] = "UV index",
         [LS_UV_LOW] = "Nízký",
         [LS_UV_MODERATE] = "Střední",
```


Run: `cmake --build build-host && ctest --test-dir build-host -R lang`
Expected: both `test_lang` and `test_lang_glyphs` pass (the glyphs of "zůstává" and "Synchronizace" exist).

- [ ] **Step 4: BOOT double in the app.** The menu's mode edit and a page's settings remember the mode left as BOOT double does, so BOOT double returns to it however `always` came on. In quiet hours or a night the toast gives the time Wi-Fi comes, in the 24-hour form the night's toast uses.

`main/app_menu.c`:

```diff
--- a/main/app_menu.c
+++ b/main/app_menu.c
@@ -26,11 +26,13 @@
 
 static const char *TAG = "app_menu";
 
-/* Gesture timings (spec §5.6): the dashboard binds KEY double and a 3 s BOOT long press; the menu
- * binds neither, so KEY short answers at once. */
+static int64_t s_closed_ms; /* app_uptime_ms() when the menu last closed */
+
+/* Gesture timings (spec §5.6): the dashboard binds both double presses (BOOT's since M6b, D31) and
+ * a 3 s BOOT long press; the menu binds neither double, so KEY short answers at once. */
 const gesture_config_t k_app_dashboard_buttons[BOARD_BUTTON_COUNT] = {
     [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = true },
-    [BOARD_BUTTON_BOOT] = { .long_ms = 3000, .double_enabled = false },
+    [BOARD_BUTTON_BOOT] = { .long_ms = 3000, .double_enabled = true },
 };
 static const gesture_config_t k_menu_buttons[BOARD_BUTTON_COUNT] = {
     [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = false },
@@ -281,10 +283,13 @@ static void apply(const ui_menu_intent_t *in)
             set->display_every_min = (uint8_t)in->value;
             save_settings = retime = true;
             break;
-        case UI_MI_SYNC_MODE:
+        case UI_MI_SYNC_MODE: {
+            uint8_t prev = set->sync_mode;
             set->sync_mode = (uint8_t)(in->value >= 0 && in->value <= SETTINGS_SYNC_MANUAL ? in->value : 0);
-            save_settings = retime = true; /* retime schedules the next sync */
+            settings_remember_mode(set, prev); /* BOOT double returns to it (D31) */
+            save_settings = retime = true;     /* retime schedules the next sync */
             break;
+        }
         case UI_MI_SYNC_INTERVAL:
             set->sync_interval_min = k_sync_min[in->value >= 0 && in->value < SYNC_STEPS ? in->value : 2];
             save_settings = retime = true;
@@ -408,6 +413,7 @@ void app_menu_close(void)
         return;
     }
     s_open = false;
+    s_closed_ms = app_uptime_ms();
     board_buttons_set_config(k_app_dashboard_buttons);
     esp_err_t err = st7305_set_mode(ST7305_MODE_LPM);
     if (err != ESP_OK) {
@@ -427,6 +433,11 @@ int64_t app_menu_deadline_ms(void)
     return s_open ? s_deadline_ms : 0;
 }
 
+bool app_menu_closed_within(int64_t ms)
+{
+    return s_closed_ms != 0 && app_uptime_ms() - s_closed_ms < ms;
+}
+
 void app_menu_key(ui_menu_key_t key)
 {
     if (!s_open) {
```


`main/app_internal.h`:

```diff
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -114,6 +114,7 @@ extern const gesture_config_t k_app_dashboard_buttons[BOARD_BUTTON_COUNT];
 void app_menu_open(void);
 void app_menu_close(void);
 bool app_menu_is_open(void);
+bool app_menu_closed_within(int64_t ms); /* the menu closed less than `ms` ago */
 void app_menu_key(ui_menu_key_t key);
 void app_menu_render(void);
 int64_t app_menu_deadline_ms(void); /* the menu closes at this time without input */
@@ -129,6 +130,10 @@ time_t app_sync_due(void);       /* for the wake scheduler; 0 = none */
 void app_sync_tick(void);        /* starts a sync that is due; quiet hours in sync mode `always` */
 esp_err_t app_sync_now(void);    /* on demand: ESP_ERR_NOT_FOUND with no network saved */
 void app_sync_now_toast(void);   /* the same, with the menu's toast: Sync now, and BOOT on the Radar layout (D30) */
+/* BOOT double on the dashboard (spec §5.6, D31): sync mode `always` on, or back to the mode before,
+ * saved and scheduled, with a toast; refused with no network saved ("No Wi-Fi network saved") and on a
+ * critical battery. */
+void app_sync_toggle_always(void);
 bool app_sync_active(void);      /* a sync or a radar-only refresh runs */
 bool app_sync_refreshing(void);  /* what runs is a radar-only refresh (spec §9.3): not shown as a sync */
 bool app_sync_running(void);     /* a sync runs, or waits for a refresh to end: what the screens show */
```


`main/app_sync.c`:

```diff
--- a/main/app_sync.c
+++ b/main/app_sync.c
@@ -308,6 +308,40 @@ void app_sync_now_toast(void)
     app_ui_toast(lang_str(lang, err == ESP_ERR_NOT_FOUND ? LS_T_NO_NETWORK : LS_T_SYNC_STARTED));
 }
 
+void app_sync_toggle_always(void)
+{
+    const lang_t *lang = lang_get(app_settings()->language);
+    settings_t *set = &app_state()->settings;
+    bool on = set->sync_mode != SETTINGS_SYNC_ALWAYS;
+    if (app_state()->critical) {
+        return; /* spec §8: nothing may drain the battery */
+    }
+    if (on && !networks_saved()) {
+        app_ui_toast(lang_str(lang, LS_T_NO_NETWORK));
+        return;
+    }
+    settings_toggle_always(set);
+    app_ui_save_settings();
+    app_clock_moved(0); /* as the menu's mode does: `always` takes Wi-Fi at once; leaving it, it goes */
+    ESP_LOGI(TAG, "BOOT double: sync mode always %s", on ? "on" : "off");
+    char text[64];
+    if (!on) { /* "Sync: At set times": the packs keep the modes in settings_sync_mode_t order */
+        snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_SYNC_MODE),
+                 lang_str(lang, (lang_str_t)(LS_SYNC_TIMES + set->sync_mode)));
+    } else if (always_wanted(time(NULL))) {
+        snprintf(text, sizeof(text), "%s", lang_str(lang, LS_T_ALWAYS_ON));
+    } else { /* quiet hours (D25) or a night (spec §9.1) keep Wi-Fi off until they end */
+        int from = set->quiet_to;
+        if (app_ui_night()) {
+            struct tm local;
+            localtime_r(&app_state()->night_until, &local);
+            from = local.tm_hour * 60 + local.tm_min;
+        }
+        snprintf(text, sizeof(text), "%s %02d:%02d", lang_str(lang, LS_T_ALWAYS_FROM), from / 60 % 24, from % 60);
+    }
+    app_ui_toast(text);
+}
+
 esp_err_t app_sync_now(void)
 {
     if (app_state()->critical) {
```


`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -54,6 +54,7 @@
 #define NIGHT_RECHECK_S   60    /* a night sleep with a button held looks again this often (D16) */
 #define CRITICAL_RECHECK_S 600  /* the critical sleep checks again this often if KEY is held */
 #define OTA_VERIFY_MS     60000 /* spec §10.5: a new image is valid after this long without a panic */
+#define MENU_SETTLE_MS    1000  /* BOOT pressed fast to back out of the menu makes no double after it */
 #define RESTART_MS        1500  /* a restart's toast stays this long; it also lets a web reply go out */
 #define NVS_KEY_RESUME    "resume_cfg" /* in `sys`: come back in config mode (spec §10.2) */
 
@@ -322,6 +323,12 @@ static void handle_button(board_button_t button, gesture_t gesture)
         app_ui_sample(time(NULL));
         app_ui_render();
         ESP_LOGI(TAG, "BOOT short: sensors refreshed");
+    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_DOUBLE) { /* spec §5.6, D31 */
+        if (app_menu_closed_within(MENU_SETTLE_MS)) {
+            ESP_LOGI(TAG, "BOOT double just after the menu closed: ignored"); /* the presses that backed out */
+        } else {
+            app_sync_toggle_always();
+        }
     } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_LONG) {
         app_config_enter(); /* 3 s on the dashboard (spec §5.6) */
     }
```


`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -684,6 +684,7 @@ esp_err_t app_ui_replace_settings(const char *json, char *err, size_t err_size)
         snprintf(err, err_size, "settings.json is larger than %u bytes", (unsigned)sizeof(s_settings_base) - 1);
         return ESP_ERR_INVALID_SIZE;
     }
+    settings_replaced(&parsed, &s.settings); /* a page that turns `always` on: BOOT double returns */
     s.settings = parsed;
     memcpy(s_settings_base, json, n + 1); /* keys this firmware doesn't know stay, as in the file */
     esp_err_t e = app_ui_save_settings();
```


- [ ] **Step 5: Run the tests, and build.**

Run: `cmake --build build-host && ctest --test-dir build-host; tools/idf.sh build 2>&1 | grep -c 'warning:'; tools/idf.sh exec xtensa-esp32s3-elf-nm -S build/reflbo.elf | grep ' s_snap$'`
Expected: `out of 61`, all passing; `0`; `50000000 00000f38 d s_snap` (3 896 bytes: the settings' new byte, padded; the version Task 2 set covers it).

- [ ] **Step 6: Commit.**

```bash
git add components/storage components/locale main test/host/test_settings.c
git commit -m "feat(app): BOOT double toggles sync mode always (D31)"
```

### Task 6: On the board, and the docs as built

**Files:**
- Modify (only if the checks find something): whatever they point at, each fix with its own test where one can fail first.
- Modify: `docs/specs/2026-09-25-firmware-design.md` (r33, as built), `AGENTS.md`

**Needs the owner first:** the board plugged into this Mac, and a choice: Steps 2–5 turn sync mode `always` on, and `always` syncs, which restarts M5's RTC-trim count over three daily syncs (memory `m5-deferred-owner-checks`: the next clean run is 2026-10-03 to 10-05 at 05:30), so the checks run now or after the 2026-10-05 05:30 sync. The board has the home network saved and no web password: Step 3 sets a temporary one over the device's own network and Step 6 clears it again, so the owner's first visit still chooses theirs. Ask, and wait.

Before anything else, confirm the port is this board (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'` shows `14:C1:9F:54:BB:94`), and note what the checks change, to restore it in Step 6: `tools/idf.sh exec python tools/devlog.py --cmd "sync status" --cmd "preset list" -o captures/m6b-before.log`.

- [ ] **Step 1: Flash and boot.**

```bash
tools/idf.sh -p /dev/cu.usbmodemXXXX flash 2>&1 | grep -E 'MAC:|Hash of data verified'
tools/idf.sh exec python tools/devlog.py --cmd reboot --until "reflbo ready" -t 30 -o captures/m6b-boot.log
grep -E 'presets.json|settings.json|E \(' captures/m6b-boot.log
tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "sync status"
```

Expected: `MAC: 14:c1:9f:54:bb:94`; no `E (` line and nothing about the files being invalid (the owner's `presets.json` and `settings.json` from M6 parse as they are); `preset list` and `sync status` as in `captures/m6b-before.log`. (If the board sits in download mode after flashing, leave it as gotcha 22 says.)

- [ ] **Step 2: BOOT double on the dashboard.** A toast lasts 3 s, so the press and the screenshot go in one call (AGENTS §7):

```bash
tools/idf.sh exec python tools/devlog.py --cmd "btn boot double" --cmd screenshot -t 8 -o captures/m6b-always-on.log
tools/idf.sh exec python tools/devlog.py --cmd "sync status" -t 40 --until "app_sync: sync (done|failed)"
tools/idf.sh exec python tools/devlog.py --cmd "btn boot double" --cmd screenshot -t 8 -o captures/m6b-always-off.log
tools/idf.sh exec python tools/devlog.py --cmd "sync status" -t 10
```

Expected: the first log has `BOOT double: sync mode always on`, and its screenshot (decoded with `screenshot.extract_pbm()`) the toast "Always on: Wi-Fi stays on"; `sync status` says mode always, a sync follows and ends `done`, and the status bar shows the Wi-Fi mark. The second press: `BOOT double: sync mode always off`, the toast "Sync: At set times", `sync status` mode times again, and `Wi-Fi off: nothing needs it` within a few seconds.

Then the presses that back out of the menu: `--cmd "btn key long" --cmd "btn boot short" --cmd "btn boot double"` in one call (the menu opens; BOOT short at its root closes it; the double comes within the second after). Expected: `menu closed`, then `BOOT double just after the menu closed: ignored`; `sync status` still mode times.

- [ ] **Step 3: A split preset over the web API.** Join the device's network (`btn boot long`; the password is on the screen; `networksetup -setairportnetwork en0 reflbo-bb94 <password>`), set a temporary web password and log in:

```bash
PW=$(openssl rand -hex 8) # a throwaway, never written down; Step 6 clears it
curl -s -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/setup
curl -s -c jar -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/login
curl -s -b jar http://192.168.4.1/api/presets > captures/m6b-presets-before.json
curl -s -b jar http://192.168.4.1/api/layouts | python3 -c 'import json,sys; s=json.load(sys.stdin)["split"]; print(s["cells"], [z["size"] for z in s["sizes"]])'
```

Expected: `8 ['XL', 'L', 'M', 'S']`. Then add spec §5.2's example as a new preset, active, and look at it:

```bash
python3 - <<'EOF'
import json
doc = json.load(open('captures/m6b-presets-before.json'))
tree = {"split": "rows", "ratio": "3/4", "line": True,
        "a": {"split": "columns", "ratio": "1/2", "line": True, "a": {"field": "wx.now"},
              "b": {"split": "rows", "ratio": "1/2", "line": True, "a": {"field": "wx.today"}, "b": {"field": "wx.hourly"}}},
        "b": {"split": "columns", "ratio": "1/2", "line": False, "a": {"field": "env.temp"}, "b": {"field": "env.hum"}}}
doc["presets"].append({"id": "m6b", "name": "Weather split", "layout": "split", "in_cycle": False, "split": tree,
                       "options": {"status_clock": True}})
doc["active"] = "m6b"
json.dump(doc, open('captures/m6b-presets-split.json', 'w'))
EOF
curl -s -b jar -X PUT -H 'Content-Type: application/json' --data-binary @captures/m6b-presets-split.json \
  http://192.168.4.1/api/presets | python3 -c 'import json,sys; print([p["id"] for p in json.load(sys.stdin)["presets"]])'
curl -s -b jar -o captures/m6b-preview.bmp 'http://192.168.4.1/api/preview.bmp?preset=m6b'
tools/idf.sh exec python tools/screenshot.py -o captures/m6b-split.png
```

Expected: the saved list ends with `m6b`; the preview BMP and the panel's screenshot both show the Weather example as `dash_split_weather` does, with today's values. The separators sit in their gaps, which nothing else inks: check the screenshot's PBM:

```bash
python3 - <<'EOF'
data = open('captures/m6b-split.pbm', 'rb').read()
w, h = 400, 300
bits = data[-w * h // 8:]
ink = lambda x, y: bits[y * (w // 8) + x // 8] >> (7 - x % 8) & 1
assert all(ink(x, 230) for x in range(8, 392)), 'the rows line at y 230'
assert all(ink(200, y) for y in range(29, 222)), 'the top columns line at x 200'
assert all(ink(x, 125) for x in range(209, 392)), 'the right rows line at y 125'
assert not any(ink(200, y) for y in range(231, 300)), 'the bottom line is hidden'
print('separators as spec §5.2 says')
EOF
```

Expected: `separators as spec §5.2 says`. Headless Chrome on `http://192.168.4.1/#presets` (AGENTS §6), logged in with the jar's session: the tree as nested boxes under the preview; cells "1 · 200×209 · L", "2 · 199×104 · M", "3 · 199×104 · M", "4 · 200×69 · S", "5 · 199×69 · S"; the bottom cells' Split into rows disabled; the preview's tags 1–5 at the cells' corners.

- [ ] **Step 4: The refusals.**

```bash
python3 - <<'EOF'
import json
doc = json.load(open('captures/m6b-presets-split.json'))
tree = doc["presets"][-1]["split"]
tree["b"] = {"field": "rain.map"}  # 400×69: the map needs M
json.dump(doc, open('captures/m6b-bad-map.json', 'w'))
cell = lambda: {"split": "rows", "ratio": "1/2", "a": {}, "b": {}}
row = lambda: {"split": "columns", "ratio": "1/4", "a": cell(), "b": {"split": "columns", "ratio": "1/3", "a": {},
               "b": {"split": "columns", "ratio": "1/2", "a": {}, "b": {}}}}
doc["presets"][-1]["split"] = {"split": "rows", "ratio": "1/2", "a": row(), "b": row()}  # ten cells
json.dump(doc, open('captures/m6b-bad-cells.json', 'w'))
EOF
for f in bad-map bad-cells; do
  curl -s -w ' %{http_code}\n' -b jar -X PUT -H 'Content-Type: application/json' \
    --data-binary @captures/m6b-$f.json http://192.168.4.1/api/presets
done
```

Expected: `{"error":"preset \"m6b\": cell 4 (400×69) can't show rain.map"} 400` (the cells in preorder: Weather now, today, the next hours, then the bottom) and `{"error":"preset \"m6b\": a split preset has at most 8 cells"} 400`; `preset list` unchanged.

- [ ] **Step 5: The mode a page leaves, and quiet hours.** Still in config mode, over the device's network:

```bash
curl -s -b jar -X PATCH -H 'Content-Type: application/json' -d '{"sync":{"mode":"interval"}}' http://192.168.4.1/api/settings >/dev/null
curl -s -b jar -X PATCH -H 'Content-Type: application/json' -d '{"sync":{"mode":"always"}}' http://192.168.4.1/api/settings \
  | python3 -c 'import json,sys; s=json.load(sys.stdin)["sync"]; print(s["mode"], s["mode_before_always"])'
tools/idf.sh exec python tools/devlog.py --cmd "btn boot long" -t 5   # config mode ends; `always` keeps Wi-Fi
tools/idf.sh exec python tools/devlog.py --cmd "btn boot double" --cmd screenshot -t 8 -o captures/m6b-back.log
tools/idf.sh exec python tools/devlog.py --cmd "sync status" -t 5
```

Expected: `always interval`; the screenshot's toast "Sync: Every interval"; `sync status` mode interval.

Then quiet hours from a minute ago to ten minutes ahead, set in config mode again (`btn boot long`, log in again over the device's network), and BOOT double on the dashboard after it:

```bash
FROM=$(date -v-1M +%H:%M); TO=$(date -v+10M +%H:%M)
curl -s -b jar -X PATCH -H 'Content-Type: application/json' \
  -d "{\"sync\":{\"quiet\":{\"enabled\":true,\"from\":\"$FROM\",\"to\":\"$TO\"}}}" http://192.168.4.1/api/settings >/dev/null
tools/idf.sh exec python tools/devlog.py --cmd "btn boot long" -t 5   # config mode ends
tools/idf.sh exec python tools/devlog.py --cmd "btn boot double" --cmd screenshot -t 8 -o captures/m6b-quiet.log
tools/idf.sh exec python tools/devlog.py --cmd "btn boot double" -t 5
```

Expected: the toast "Always on: Wi-Fi from <TO>", and no sync starts (`always` waits for the span's end); the second press returns to interval, "Sync: Every interval". Step 6 puts the quiet hours back.

- [ ] **Step 6: Back to the start.** Config mode again (`btn boot long`), log in again over the device's network, and restore what `captures/m6b-before.log` had: `PUT /api/presets` with `captures/m6b-presets-before.json` (the owner's presets, and their active one), and `PATCH /api/settings` with the sync mode and quiet hours from the log. Then clear the temporary web password: Menu ▸ Wi-Fi ▸ Reset web password (`btn key long` opens the menu; `btn key short` steps to the next item and `btn key long` enters it; the confirmation asks for KEY held; a screenshot after each press shows where the menu is). Last, `networksetup -removepreferredwirelessnetwork en0 reflbo-bb94` and `rm jar captures/m6b-*.json`.

- [ ] **Step 7: Write down what was built.**
  - Spec r33: §5.2 as built (the table of least heights each kind needs, from the plan's research; a cell with no room for its field draws nothing), §5.3 (numbers fit the height as well as the width, in the same order; the sun widget's smaller faces), §5.4 (a split preset is written with its tree and every split's `line`, without `slots`; one without a tree is one empty cell; errors count cells in preorder; `presets.json` up to 20 KB), §5.6 (the toasts: "Always on: Wi-Fi stays on", "Always on: Wi-Fi from 06:00" in quiet hours or a night, and "Sync: <mode>"; a BOOT double within a second of the menu closing does nothing), §6 (the snapshot: 3 896 bytes, version 8), §10.3 (requests and replies up to 24 KB; the editor's numbered cells, a field's own size when smaller than its cell's class, and a ratio change or a split that empties a cell left too small, with a toast), §14.3 (any change into `always` remembers the mode it left, unless the document names its own), §17 (the tests as built), §20 (anything Steps 1–5 found), §21.
  - `AGENTS.md`: the status (M6b built), and any gotcha the checks found.

- [ ] **Step 8: Commit and push.**

```bash
git add docs AGENTS.md
git commit -m "docs: record M6b as built (spec r33)"
git push origin main
```

## Owner acceptance

M6b is done (spec §18) once the owner has checked, with the expected results:

1. **The goldens** (`python3 tools/render.py`, `captures/render/dash_split_weather.png` and `dash_split_eight.png`): as wanted, or with the changes asked for.
2. **A split preset built in the web editor** on the phone (the Weather example, or one of the owner's own): saved, it shows on the panel as the preview shows it.
3. **BOOT double with the buttons:** on the dashboard, "Always on: Wi-Fi stays on" and the Wi-Fi mark; again, back to the mode before with its toast. BOOT short still refreshes the sensors, after a short pause; BOOT long still enters config mode at 3 s.

Still open from earlier milestones, offered when the owner is at the board: M6's rainy-day ČHMÚ frame and its power measurements (memory `m6-acceptance-open`), M5's trim over days, sync on battery and router-off checks (memory `m5-deferred-owner-checks`), M3's night-sleep current and night peek, M4's update from the page and first run.
