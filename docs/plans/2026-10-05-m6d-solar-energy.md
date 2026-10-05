# M6d: Solar and the House's Energy, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** M6d (spec §18, D33, D35, D36): a PV forecast from Open-Meteo through our own model, from Forecast.Solar or from Solcast, for up to two roof planes; the house's energy from SolaX Cloud, the battery shown when there is one; both as `pv.*` and `energy.*` fields at every size, a chart and a flow, two layouts with built-in presets; a switch for each data step of the sync; the Solar web page, `/api/solar/check`, `solar.bin` and the `solar` commands.

**Architecture:**

- **Pure logic, host-tested:**
  - `solar` (new): our PV model; a reply's power by local quarter hour (`solar_acc_*`), over three days from the fetch's local day, with DST and the midnight roll-over; the forecast as kept (`solar_forecast_t`: two days of quarter hours in tens of W, three days' totals) and what the fields read of it (today, what is left, the peak); Open-Meteo's, Forecast.Solar's and Solcast's requests and replies; Solcast's budget; how long a forecast stays fresh.
  - `energy` (new): SolaX Cloud's request and reply in our signs; the battery's rule; the day the readings build (`energy_day_t`: the reading nearest midnight, each quarter hour's mean output) and today's totals.
  - `storage`: `sync.steps`, `solar.*` and `energy.*` in `settings.json`; the keys taken out of a PATCH (`settings_take_secrets()`), checked and kept out of the file; a second Forecast.Solar plane only with a key.
  - `ui`: the view (`ui_solar.h`) and the fields at every size; the chart and the flow; the Solar and Energy layouts and their built-in presets; Sync ▸ Steps; the sample day (`ui_solar_demo()`) the goldens and `solar demo` share.
  - `sync`: which steps fail a sync (`sync_report_failed()`); `util`: a URL's host (`util_url_host()`), all `fetch` logs of a URL.
- **Device side:**
  - `sync`: the Solar and Energy steps, the steps' switches, a step that keeps what it has (`SYNC_STEP_KEPT`), three kinds of run: a sync, sync mode `always`'s refresh (the radar, the house's reading or both), and the Solar page's check; its buffers in PSRAM.
  - `fetch`: Solcast's bearer header; a failed GET logs its host alone.
  - `main`: the solar state (`app_solar.c`) in the RTC snapshot and `/fs/state/solar.bin`; the keys in NVS `secrets` (`app_secrets.c`); the requests and the reports; the energy refresh every 5 min in `always`; `solar status|demo`; the API's status, settings, keys and check.
  - `web/`: the Solar page; the Sync page's steps; the editor's Solar and Energy fields, grouped, marked when their step is off.

**Tech stack:** ESP-IDF v5.5.5, cJSON, LittleFS, NVS; Unity on the host; Node's test runner for the page; uv and Pillow for the icons. New services: Open-Meteo's tilted irradiance (no key), Forecast.Solar (optional key), Solcast (key), SolaX Cloud (token). Nothing new from the registry.

**Spec:** `docs/specs/2026-09-25-firmware-design.md` r37 (D33, D35, D36, the owner's approval of 2026-10-04). Task 12 brings it to r38, as built.
- Relevant: §5.1 (the `pv.*` and `energy.*` rows, the solar and energy fields), §5.2 (the Solar and Energy layouts), §5.3 (the chart and the flow), §5.4 (built-in presets), §5.7 (Sync ▸ Steps), §6 (the snapshot), §9.3 (the steps' switches, the Solar and Energy steps, the energy in `always`, which failures fail a sync), §10.3, §10.4 (the Solar page, the API, write-only keys), §11.5, §11.6, §14.2–§14.4, §15, §17, §18 (M6d); D35, D36.
- Also `AGENTS.md` §3.4 (gotchas 11, 22, 29, 30, 31, 33, 38, 41), §5.3, §6–§8.

**Research behind this plan (2026-10-04 and 05):**
- The owner approved the designs as panel renders from a throwaway spike (local branch `spike/m6cd`, da6abef; the review page `captures/spike/index.html`, published privately as https://claude.ai/artifact/XA9LY5dHweJPtKPHKwEtGi). This plan builds them again test first. Their twelve renders are its goldens: `home_energy` and the five Energy layouts match the spike byte for byte; the Solar layout and the chart in Grid, Weather and Focus differ by a few pixels of bar tops and of the forecast's line, as the device keeps power in tens of W.
- The plan's code was built and tested task by task on the local branch `plan/m6d`, one commit per task (`plan-m6d task N: …`); the plan carries that code. A review of the whole branch and its fixes are folded into those commits (below).
- **Files copied from the branch:** the twelve goldens, and the JSON fixtures (recorded from the live services, or built in their documented formats). The tasks that add them copy them with `git checkout plan/m6d -- <paths>` (in a clone without the branch, `git fetch origin plan/m6d:plan/m6d` first). The icons are generated C sources, regenerated in Task 5 by `tools/gen_icons.sh`.
- **The services, checked on 2026-10-05:**
  - Open-Meteo's `global_tilted_irradiance` for Brno, a plane tilted 35° to the south: 288 values from local midnight, 6.3 KB, each the mean of the 15 minutes before its time (`test/host/fixtures/solar/open-meteo-brno.json`). Through our model at 5 kWp and 14 % losses: 16.6 kWh that day, against Forecast.Solar's 15.0 kWh for the same roof.
  - Forecast.Solar's `watts` are the power **at** each time, not the mean of the period before it as spec r35 said: its `watt_hours_period` is the trapezoid between two points (231 Wh at 06:00 = (110 + 352) ÷ 2), and integrating the line gives its own day totals within 3 Wh (15 022 against 15 025). Its zero points sit at sunrise and sunset, to the second. Its `/estimate/watts/` reply carries only those points: 1.2 KB, against 3.1 KB for the whole estimate (`forecast-solar-brno.json`; the rate limit's address in it replaced by 192.0.2.1).
  - No account was at hand for Solcast, for Forecast.Solar with a key or for SolaX Cloud. Their fixtures are built in the documented formats: Solcast's `forecasts` for 72 h (about 18 KB), Forecast.Solar's 15-minute points for three days, and SolaX Cloud's V4.0 reply — the example in its PDF (the documented night reading of an X1-Boost) and three built from it: a string inverter by day, an X3-Hybrid charging its battery, and a refusal (`success: false`).
- **Sizes:** the largest Solcast reply is about 18 KB, so the sync's body buffer is 32 KB, in PSRAM, with the sync's request and report and the Solar step's reply (2.3 KB in all) moved there too, and `solar.bin`'s buffers and the demo's. The largest `settings.json` is 1690 bytes; the app keeps 4 KB of it (2 KB before). The RTC snapshot grows from 4 664 to 5 680 bytes (version 10): RTC slow memory holds 5 832 of its 8 192 bytes with `power`'s state.
- **Tests:** `test_ui_widget_fit` now draws the 13 small solar fields and the chart and the flow in every cell size and fifteen sets of data, one of them at the largest values (200 kW from the roof, 1599 kWh in a day): about 40 s, 4 minutes under ASan. It found three overflows the spike had: the chart's axis labels at 200 kW, the flow's values in narrow columns, and the "now" mark at the plot's edge. The layouts' own test at those values found the rest (Task 7).
- **The review (2026-10-05):** a fresh reviewer read the whole branch before this plan was generated: 1 Critical, 3 Important and 12 Minor findings, all fixed test first in the tasks that own their code (rulings below), but four minors left for later (spec §20, Task 12).
  - **Critical:** with the Weather step off and no forecast stored, every successful sync started the next at once, without end: the board never slept and asked every service every few seconds (Task 10's `sync_need()`).
  - **Important:** a restore saved a bundle's text as it was when one of its keys was of a form `settings_take_secrets()` refused, so the key reached the file (Task 11); provider times out of all reason could keep the sync task busy for minutes, an Open-Meteo time of `1e300` for ever (Task 1); Solcast's budget, counting a failed call, held Check now back for hours after the owner fixed a key, and a kept step hid the error (Tasks 4, 9–11).
  - **Minors fixed with them:** a key named twice in a PATCH (re-graded Important: it reached the file), the layouts' text over text from about 100 kWh a day (re-graded Important), values out of range (a wrapped total, a negative output shown as solar, a cast of an out-of-range double), the radar's loop stopped by a refresh for the house alone, the demo keeping real data out of `solar.bin`, Solcast's sites missing from it, 2.3 KB of internal RAM, Check now with nothing to check, details cut inside a character, long lines, a restore without the second-plane check, `solar.bin` rewritten by every kept step.

**Rulings this plan makes (each costs little if wrong):**
- **Power is kept in tens of W** (Task 1): two planes of 100 kWp pass a `uint16_t` of watts; 655 kW at most. Cost: 10 W steps, a pixel on a chart bar now and then.
- **Forecast.Solar's `watts` are integrated as a line** (Task 2), as they are the power at each point (checked live; spec r35 said the mean of the period before it), from its `/estimate/watts/` reply, a quarter the size of the whole estimate; with a key, both planes go in one request, without one the first alone. Cost: if a keyed account's reply differs, the owner's acceptance shows it.
- **Today's and tomorrow's totals are their quarter hours' sums** (Task 1), so `pv.today`, `pv.left` and the chart agree; the day after's comes from the reply. Cost: up to a few Wh against a provider's own total.
- **A reply's gaps keep the last forecast's quarter hours of the same local day** (Task 1): Solcast sends nothing before now, so the morning keeps what the last forecast said.
- **A provider's numbers are cut to reason, not refused** (Tasks 1, 3): a period or line to the three days and a day to spare, power to 0–655.35 kW, an Open-Meteo time outside 1970–2286 left out; SolaX's output never below 0. Cost: a nonsense reply shows bounded nonsense instead of an error.
- **A refusal's detail is the provider's own words** (Tasks 1, 2, 10), without its name, cut between characters to the sync's 24 bytes. Cost: the Sync page names the step, not the service; the Solar page names the source.
- **Solcast's budget** (Tasks 2, 4, 9): it is asked when it never was, when the clock went back, or 3 h (one site) or 6 h (two sites) after its last call, failed calls included; between, the step is "kept". A new key or site starts it over, so Check now asks at once; there is no daily count. Cost: a clock that jumps back, or many key changes in a day, ask more than 10 times, and Solcast's 429 then keeps the forecast.
- **`solar` and `energy` need `timekeeping`** (Tasks 2, 3) for its ISO 8601 parser, which now drops a fraction of a second; spec §3.1 named `util` alone.
- **The fixtures without an account** (Tasks 2, 3) are built in the documented formats: Solcast, Forecast.Solar with a key, and SolaX Cloud's string inverter by day, hybrid and refusal beside the PDF's own example. Cost: a real reply that differs is found in the owner's acceptance.
- **The reading taken as midnight's** (Task 3) is the nearest within an hour either side; one before midnight waits for the day to turn; a day without one has no totals to or from the grid, and own use needs both. An upload counts once.
- **Keys are checked when saved** (Task 4) against what their requests carry as they are: letters and digits (Solcast's key also `-` and `_`, its sites `-`), 63 at most; `null` or `""` clears one; `[]` clears both sites; a key named twice counts its last, as the merge does. The page asks for both site ids when one is set and the other changes.
- **`settings.json` may be 4 KB** (Task 4; 2 KB before): the largest is 1690 bytes, and the test keeps half the room for M7.
- **`ui` needs `solar` and `energy`** (Task 5), as it needs `radar` and `adsb`; the sample day (`ui_solar_demo()`) lives in `ui`, where both the goldens and `solar demo` reach it.
- **The grid's way** (Task 5): "Export" or "Import" as its label in M and up, without an arrow; ↑ or ↓ in S and XS; under 20 W "Grid" and nothing. Today's totals to and from the grid carry their arrows in S and XS too.
- **The fit test's fixes** (Task 6): the axis labels' margin, the "now" mark and the heading's total at 200 kW, and the flow's values in narrow columns; the spike had none of them.
- **Goldens re-rendered where the tens of W move a pixel** (Tasks 6, 7): `grid_solar`, `weather_solar`, `focus_solar` and the three Solar layouts; `home_energy` and the five Energy layouts are the spike's to the byte.
- **The layouts raise the status bar's stale warning** (Task 7) when what they show is stale, as a stale slot does (spec §5.2); a day without a total and a total without a reading near midnight show a dash.
- **The layouts' numbers take short forms from 100 kWh a day** (Task 7): whole kWh, and a total in the row of four starts under its icon from 1 MWh; nothing moves at a house's usual values. Cost: a big site's day reads "1599 kWh" without its tenth.
- **Sync ▸ Steps is a section** (Task 8) of five switches, labelled with the steps' names that Info ▸ Last sync uses.
- **The app's state before the sync's steps** (Tasks 9, 10), which call into it; `SNAP_VERSION` 10 covers both, as they ship together.
- **The sample day's battery follows `energy.battery` `on`** (Task 9); the demo draws from its own buffers and is never saved, while the real state is saved as ever; a restart ends it.
- **Solcast's site count is kept in the state and in `solar.bin`** (Task 9) at each Solar step, so the forecast's freshness needs no NVS at a render (a routine wake has none, gotcha 31), after a cold boot too.
- **A check runs both steps whatever the switches say** (Task 10), asked for on the Solar page; it is no sync: no history, no retries, no mark in the status bar. Wi-Fi is released after it as after a sync.
- **A kept step keeps the last call's error** (Tasks 9–11): the Solar page shows why the forecast was kept (Solcast's budget, with when it is asked again, or a 429) beside the last call's error; the Sync page shows every step's detail; a kept step writes nothing to `solar.bin`.
- **The forecast's need follows the Weather step** (Task 10): with Weather off, a missing forecast starts no sync, as no sync would bring one; the schedule decides.
- **The radar is applied only when its step ran or frames came** (Task 10), so a refresh for the house alone or a check leaves its loop playing.
- **Check now refuses with both sources off** (Tasks 10, 11): 409, "nothing to check"; a check that can't join shows why as both steps' errors on the Solar page.
- **Info ▸ Last sync names the first step that failed, the house's energy too** (Task 10; spec: its failures show in Info), while the status bar's mark and the retries follow `sync_report_failed()`.
- **The sync's buffers move to PSRAM** (Task 10): the request with its keys, the report, the Solar step's reply and the 32 KB body; nothing there is DMA.
- **A restore refuses a bundle whose keys can't be taken out, or with a second Forecast.Solar plane and no key** (Task 11), as a PATCH refuses them; one whose keys can be taken out drops them.
- **The review's cheap minors are fixed in the plan**, not deferred: the plan was still being written, and the owner's rule fixes minors where a milestone touches their code (D25, D27). Cost: a few more tests in the plan.
- **The docs follow in Task 12**, as in M6c: `AGENTS.md`, the spec's as-built revision, the guide and the README in one commit after the board checks.

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, plain HTML/CSS/JS in `web/` with no build step and no external resources.
- `[host]` code (`solar`, `energy`, `ui`, `locale`, `components/storage/settings.c`, `components/sync/sync_plan.c`, `util`) includes no ESP-IDF headers. cJSON counts as plain C.
- ESP-IDF style: 4-space indent, `snake_case`, a component or module prefix on public APIs, public headers in `include/`. C lines stay within 120 characters. GCC's `-Wformat-truncation` is an error (gotcha 33): size a buffer for the widest value its format can print.
- The host build is clean too, but for one linker note from before M6d: `ld: warning: ignoring duplicate libraries: 'libcjson.a'` (`test_ui_catalog`, as on `main`). The Expected blocks leave it out, with CMake's `--` status lines and ctest's timings.
- The app task owns the display, storage, the settings, the presets, the syncs' and the solar state (spec §3.2, `AGENTS.md` §5.3); the web server reaches them through the app's executor; the sync task fetches and reports.
- **The RTC snapshot is at most 6 KB** (`_Static_assert` in `main/app.c`); a change to its layout bumps `SNAP_VERSION`.
- **JSON from outside** is checked with `util_json_depth()` before cJSON parses it (gotcha 30).
- **Secrets.** Forecast.Solar's key, Solcast's key and site ids, and SolaX's token and registration number live in NVS `secrets` only: never in `settings.json`, a backup, a reply, a log or the console. `fetch` logs a URL's host alone. Never commit or log a Wi-Fi password, the AP password, the web password or any of these keys.
- **The board** (`AGENTS.md` §6, the owner's rules of 2026-10-03): back up its configuration with `GET /api/backup` before a check that changes it and restore it after; never erase flash or NVS, or forget the saved networks, without asking; a factory reset only with the owner's agreement. Board commands go through `tools/idf.sh` with an explicit `-p`, and the port must be confirmed as this board: Espressif `303A:1001` with the USB serial number `14:C1:9F:54:BB:94` (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'`), and `flash` prints `MAC: 14:c1:9f:54:bb:94`.
- **This Mac's Wi-Fi** may join the board's AP for checks (owner, 2026-09-30): `networksetup -setairportnetwork en0 reflbo-bb94 <password from the screen>`, removed afterwards with `networksetup -removepreferredwirelessnetwork en0 reflbo-bb94`. The board has no web password now; a check that sets one clears it again (Menu ▸ Wi-Fi ▸ Reset web password).
- List every component source in `SRCS`, and run `tools/idf.sh reconfigure` after adding a component (`AGENTS.md` §6). Dependencies come with ESP-IDF; nothing new from the registry.
- Small, focused Conventional Commits that each build. Push to `origin` freely; never force-push. **No attribution of any kind: no `Co-Authored-By` or other trailer in any commit** (the owner's rule, memory `no-commit-trailers`).
- Build only what the spec covers (r37: D35, D36). Not in M6d: MQTT (M7, and its MQTT source for the house's energy), the SolaX dongle's local API, changes to other layouts.
- Czech text follows Czech typography, and every string's glyphs must exist in the fonts (`test_lang_glyphs`).

## Review Focus

1. **Real replies that differ from the fixtures**: Solcast's, Forecast.Solar's with a key and SolaX Cloud's hybrid replies were built from documentation. A reasonable owner expects a forecast or a reading, or a step that fails with a reason on the Sync page, never a crash, a hang or a garbled number. Pinned by Tasks 1–3's refusals (nulls, missing members, error bodies, a 200 that says it failed), their times and values out of all reason (a line from 1970, a time of `1e300`, −5 kW, 1e30 W, an out-of-range inverter type, also under UBSan) and the owner's acceptance with the real keys.
2. **The heap during a Solar step**: Solcast's 18 KB reply makes several hundred cJSON nodes while Wi-Fi is up, in config mode or sync mode `always`; the device should stay as responsive as with the weather's replies. Pinned by Task 12's Step 9 (`heap` after a check) and the owner's acceptance 3 (after Solcast's reply).
3. **Keys never leave NVS**: not in `settings.json`, a backup, a reply, a log or the console, and only characters their requests can carry. Pinned by Task 4's tests (the patch without them, a key named twice or in a second object, the echoed flags, the refusals), Task 10's `util_url_host()` tests, Task 11's page tests (write-only fields) and Task 12's Step 6 on the device (a restore with a key it can't take out is refused).
4. **Midnight and DST**: after midnight the forecast's tomorrow is today and the house's totals start over; without a reading near midnight the totals to and from the grid are dashes, not wrong numbers; a DST night keeps a day's energy. Pinned by Task 1's DST tests, Task 3's day tests, Task 5's "after midnight tomorrow is today" and Task 7's dashes.
5. **The owner's M6c files after the update**: `presets.json` gains Solar and Energy once, `settings.json` runs every step, the first wake is cold. Going back to `stable-m6b` before `stable-m6d` exists, its parser refuses a `presets.json` with the Solar or Energy layout (as it refuses M6c's 24 cells) and starts from its `.bak` or built-ins until a backup from before is restored. Pinned by Task 4's file from before M6d, Task 7's M6c file, Task 12's Step 0 (the backup taken before flashing, kept until `stable-m6d`), Steps 1 and 8, and its Step 12, which writes the way back into spec §12.10 and `AGENTS.md` §6.

---

### Task 1: The PV model, the local quarter hours and Open-Meteo's forecast (`solar`)

**Files:**
- Create: `components/solar/CMakeLists.txt`, `components/solar/include/solar.h`, `components/solar/solar.c`, `components/solar/solar_url.c`, `components/solar/solar_parse.c`
- Modify: `components/util/include/util_json.h`, `components/util/util_json.c`, `test/host/CMakeLists.txt`
- Test: `test/host/test_solar.c`, `test/host/test_util_json.c`; fixture `test/host/fixtures/solar/open-meteo-brno.json` (copied from the branch)

**Interfaces:**
- Consumes: `util_days_from_civil()` (util_time.h), `util_json_depth()` (util_json.h), cJSON.
- Produces (Tasks 2, 5–10):
  - `SOLAR_STEPS` (96), `SOLAR_DAYS` (3), `SOLAR_UNIT_W` (10: a quarter hour's power is kept in tens of W), `SOLAR_WH_NONE`, `SOLAR_PLANES_MAX` (2), `SOLAR_URL_MAX`;
  - `solar_plane_t { float kwp; int tilt; int azimuth; }`;
  - `solar_forecast_t { int32_t day; uint32_t fetched; uint16_t q[2][SOLAR_STEPS]; uint32_t wh[SOLAR_DAYS]; }`: local day `day` and the next by quarter hour, three days' totals (the first two the sums of their quarter hours);
  - `solar_acc_t`, a reply on its way: `solar_acc_init(acc, now)`, `solar_acc_period(acc, end, seconds, watts)`, `solar_acc_line(acc, t0, w0, t1, w1)`, `solar_acc_cap(acc, watts)`, `solar_acc_finish(acc, old, fetched, out)` (quarter hours the reply didn't cover keep `old`'s of the same local day; `out` may be `old`);
  - `float solar_model_w(float kwp, float gti_w_m2, float t_air_c, int losses_pct)`;
  - `const uint16_t *solar_day(f, day)`, `uint32_t solar_day_wh(f, day)`, `uint32_t solar_left_wh(f, day, quarter, seconds)`, `bool solar_peak(f, day, &w, &quarter)`;
  - `size_t solar_open_meteo_url(out, size, lat_e4, lon_e4, plane)`, `bool solar_parse_open_meteo(json, len, plane, losses_pct, acc, err, err_size)`;
  - `size_t util_json_text(out, size, text)` (util_json.h): a provider's words cut where they don't fit, between UTF-8 characters, never inside one (Tasks 2, 10).

Local quarter hours follow the TZ variable: a UTC quarter-hour mark is a local one in every time zone, as every offset is a whole number of quarter hours. The night the clocks go back, both 02:00–03:00 land on the same quarter hours (no output then); the night they go forward, 02:00–03:00 gets nothing.

A provider's numbers are kept within reason before anything is integrated (the review's finding): a period or a line is cut to the accumulator's three local days with a day to spare on either side, so a time years away costs nothing; power below 0, not a number or above 655.35 kW (what a quarter hour keeps) is held within those bounds; an Open-Meteo time outside 1970–2286 is left out before its cast. A refusal carries the provider's own words without its name, as the step and its source name it.

- [ ] **Step 1: Write the failing tests.** The model against the formula; a period and a line by quarter hour; three days and no more; times far off (a line from 1970 must take under 50 ms) and power out of all reason; both DST nights; the inverter's cap; a reply's gaps keeping the last forecast's quarter hours; what the fields read; Open-Meteo's request and its recorded reply, with the totals worked out by hand from the reply (16 644, 22 216 and 18 652 Wh), and its refusals, a time of `1e300` among them, in the sync's 24-byte detail; a provider's words cut between characters:

`test/host/test_solar.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/test/host/test_solar.c
@@ -0,0 +1,444 @@
+#define _POSIX_C_SOURCE 200809L /* setenv, tzset */
+
+#include <math.h>
+#include <stdio.h>
+#include <stdlib.h>
+#include <string.h>
+#include <time.h>
+
+#include "solar.h"
+#include "unity.h"
+#include "util_time.h"
+
+/* The PV forecast (spec §11.5, D35): our model, the local quarter hours and the replies. The
+ * Open-Meteo fixture was fetched on 2026-10-05 for Brno (49.1951, 16.6068), a plane tilted 35°
+ * facing south. */
+
+#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"
+
+static char s_json[32768];
+static size_t s_len;
+static char s_err[96];
+
+void setUp(void)
+{
+    setenv("TZ", TZ_PRAGUE, 1);
+    tzset();
+}
+
+void tearDown(void) {}
+
+static const char *fixture(const char *name)
+{
+    char path[256];
+    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
+    FILE *f = fopen(path, "rb");
+    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
+    s_len = fread(s_json, 1, sizeof(s_json) - 1, f);
+    fclose(f);
+    s_json[s_len] = '\0';
+    return s_json;
+}
+
+/* A local time in Prague; `dst` 1 or 0 picks the summer or winter one of an hour that comes twice. */
+static time_t local_at(int y, int mo, int d, int h, int mi, int dst)
+{
+    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi,
+                    .tm_isdst = dst };
+    return mktime(&t);
+}
+
+static time_t local(int y, int mo, int d, int h, int mi)
+{
+    return local_at(y, mo, d, h, mi, -1);
+}
+
+static int32_t day_of(int y, int mo, int d)
+{
+    return (int32_t)util_days_from_civil(y, mo, d);
+}
+
+static const solar_plane_t k_south = { .kwp = 5.0f, .tilt = 35, .azimuth = 0 };
+
+/* --- the model --- */
+
+static void test_the_model_follows_the_formula(void)
+{
+    /* 5 kWp, 800 W/m², 20 °C, 14 % losses: the cell at 44.8 °C, 0.9208 for the heat, 3167.6 W */
+    TEST_ASSERT_FLOAT_WITHIN(0.5f, 3167.6f, solar_model_w(5.0f, 800.0f, 20.0f, 14));
+}
+
+static void test_the_model_gains_in_the_cold(void)
+{
+    /* 1000 W/m² at -10 °C: the cell at 21 °C gives 1.6 % more than the rating, less the losses */
+    TEST_ASSERT_FLOAT_WITHIN(0.5f, 4368.8f, solar_model_w(5.0f, 1000.0f, -10.0f, 14));
+}
+
+static void test_no_light_no_power(void)
+{
+    TEST_ASSERT_EQUAL_FLOAT(0.0f, solar_model_w(5.0f, 0.0f, 30.0f, 14));
+    TEST_ASSERT_EQUAL_FLOAT(0.0f, solar_model_w(5.0f, -3.0f, 30.0f, 14)); /* a negative irradiance is none */
+    TEST_ASSERT_EQUAL_FLOAT(0.0f, solar_model_w(5.0f, 1000.0f, 300.0f, 0)); /* never below nothing */
+}
+
+/* --- the local quarter hours --- */
+
+static void test_a_quarter_hour_period_fills_its_own(void)
+{
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
+    solar_acc_period(&acc, local(2026, 10, 5, 12, 15), 900, 2000.0f); /* 12:00-12:15: quarter 48 */
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 1234, &f);
+    TEST_ASSERT_EQUAL_INT32(day_of(2026, 10, 5), f.day);
+    TEST_ASSERT_EQUAL_UINT32(1234, f.fetched);
+    TEST_ASSERT_EQUAL_UINT16(200, f.q[0][48]);
+    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][47]);
+    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][49]);
+    TEST_ASSERT_EQUAL_UINT32(500, f.wh[0]); /* 2 kW for a quarter hour */
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[1]); /* nothing came for tomorrow */
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[2]);
+}
+
+static void test_a_half_hour_fills_two_quarter_hours(void)
+{
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
+    solar_acc_period(&acc, local(2026, 10, 5, 12, 30), 1800, 3000.0f); /* Solcast's 12:00-12:30 */
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_EQUAL_UINT16(300, f.q[0][48]);
+    TEST_ASSERT_EQUAL_UINT16(300, f.q[0][49]);
+    TEST_ASSERT_EQUAL_UINT32(1500, f.wh[0]);
+}
+
+static void test_a_line_is_integrated_by_quarter_hour(void)
+{
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
+    solar_acc_line(&acc, local(2026, 10, 5, 12, 0), 0.0f, local(2026, 10, 5, 13, 0), 4000.0f); /* Forecast.Solar */
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_EQUAL_UINT16(50, f.q[0][48]); /* the mean of each quarter hour of the line */
+    TEST_ASSERT_EQUAL_UINT16(150, f.q[0][49]);
+    TEST_ASSERT_EQUAL_UINT16(250, f.q[0][50]);
+    TEST_ASSERT_EQUAL_UINT16(350, f.q[0][51]);
+    TEST_ASSERT_EQUAL_UINT32(2000, f.wh[0]);
+}
+
+static void test_periods_beyond_the_three_days_are_left_out(void)
+{
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
+    solar_acc_period(&acc, local(2026, 10, 5, 0, 0), 900, 1000.0f); /* yesterday's 23:45-24:00 */
+    solar_acc_period(&acc, local(2026, 10, 7, 12, 15), 900, 1000.0f); /* the day after tomorrow */
+    solar_acc_period(&acc, local(2026, 10, 8, 12, 15), 900, 1000.0f); /* a fourth day */
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[0]);
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[1]);
+    TEST_ASSERT_EQUAL_UINT32(250, f.wh[2]);
+}
+
+static void test_the_night_the_clocks_go_back_keeps_both_hours(void)
+{
+    /* 2026-10-25: 02:00-03:00 comes twice, in summer time and then in winter time; both land on
+     * quarters 8-11, so the day keeps its energy */
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 25, 1, 0));
+    time_t summer = local_at(2026, 10, 25, 2, 15, 1);
+    solar_acc_period(&acc, summer, 900, 400.0f);
+    solar_acc_period(&acc, summer + 3600, 900, 400.0f); /* 02:00-02:15 again, in winter time */
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_EQUAL_UINT16(80, f.q[0][8]);
+    TEST_ASSERT_EQUAL_UINT32(200, f.wh[0]);
+}
+
+static void test_the_night_the_clocks_go_forward_skips_an_hour(void)
+{
+    /* 2026-03-29: 02:00 becomes 03:00; the hour after 01:00 UTC is 03:00-04:00 local */
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 3, 29, 1, 0));
+    time_t t = local(2026, 3, 29, 1, 45);
+    for (int i = 1; i <= 8; i++) {
+        solar_acc_period(&acc, t + i * 900, 900, 400.0f); /* 01:45-03:45 UTC+1 ... */
+    }
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_EQUAL_UINT16(40, f.q[0][7]);  /* 01:45 */
+    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][8]);   /* 02:00 never came */
+    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][11]);
+    TEST_ASSERT_EQUAL_UINT16(40, f.q[0][12]); /* 03:00 */
+    TEST_ASSERT_EQUAL_UINT16(40, f.q[0][18]);
+    TEST_ASSERT_EQUAL_UINT32(800, f.wh[0]);
+}
+
+static void test_the_inverter_caps_each_quarter_hour(void)
+{
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
+    solar_acc_period(&acc, local(2026, 10, 5, 12, 15), 900, 2500.0f); /* two planes */
+    solar_acc_period(&acc, local(2026, 10, 5, 12, 15), 900, 2500.0f);
+    solar_acc_period(&acc, local(2026, 10, 5, 12, 30), 900, 1000.0f);
+    solar_acc_cap(&acc, 4000.0f);
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_EQUAL_UINT16(400, f.q[0][48]);
+    TEST_ASSERT_EQUAL_UINT16(100, f.q[0][49]);
+}
+
+/* A provider's times out of all reason count only within the three days, and fast: a line or a period that reaches
+ * years away is cut to them before it is integrated (Review Focus). */
+static void test_times_far_off_count_only_within_the_three_days(void)
+{
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
+    clock_t c0 = clock();
+    solar_acc_line(&acc, 0, 1000.0f, local(2026, 10, 6, 0, 0), 1000.0f); /* from 1970 */
+    TEST_ASSERT_TRUE_MESSAGE(clock() - c0 < CLOCKS_PER_SEC / 20, "a line from 1970");
+    solar_acc_line(&acc, local(2026, 10, 7, 0, 0), 400.0f, (time_t)253402300799, 400.0f); /* to the year 9999 */
+    solar_acc_period(&acc, local(2026, 10, 6, 0, 0), INT32_MAX, 1000.0f);                /* from 1958 */
+    TEST_ASSERT_TRUE_MESSAGE(clock() - c0 < CLOCKS_PER_SEC / 20, "a line to 9999");
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_EQUAL_UINT32(48000, f.wh[0]); /* the line's and the period's 24 h of 1 kW */
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[1]);
+    TEST_ASSERT_EQUAL_UINT32(9600, f.wh[2]);
+}
+
+/* Power below zero, not a number, or beyond what a quarter hour keeps (655 kW) stays within those bounds, so no
+ * day's total wraps (Review Focus). */
+static void test_power_out_of_reason_stays_within_bounds(void)
+{
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 9, 0));
+    solar_acc_line(&acc, local(2026, 10, 5, 12, 0), -100.0f, local(2026, 10, 5, 12, 15), -100.0f);
+    solar_acc_period(&acc, local(2026, 10, 6, 12, 15), 900, 1e30f);
+    solar_acc_period(&acc, local(2026, 10, 6, 12, 30), 900, NAN);
+    solar_acc_period(&acc, local(2026, 10, 7, 12, 15), 900, -5000.0f);
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_EQUAL_UINT32(0, f.wh[0]);
+    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, f.q[1][48]);
+    TEST_ASSERT_EQUAL_UINT16(0, f.q[1][49]);
+    TEST_ASSERT_EQUAL_UINT32(163838, f.wh[1]); /* 655.35 kW for a quarter hour */
+    TEST_ASSERT_EQUAL_UINT32(0, f.wh[2]);
+}
+
+static void test_quarter_hours_a_reply_leaves_out_keep_the_last_forecast(void)
+{
+    /* Solcast sends nothing before now: the morning keeps what the last forecast said */
+    solar_forecast_t old;
+    memset(&old, 0, sizeof(old));
+    old.day = day_of(2026, 10, 5);
+    old.q[0][40] = 150;
+    old.q[0][48] = 200;
+    old.wh[0] = 875;
+    old.wh[1] = SOLAR_WH_NONE;
+    old.wh[2] = SOLAR_WH_NONE;
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 0));
+    solar_acc_period(&acc, local(2026, 10, 5, 12, 15), 900, 3000.0f);
+    solar_forecast_t f;
+    solar_acc_finish(&acc, &old, 0, &f);
+    TEST_ASSERT_EQUAL_UINT16(150, f.q[0][40]); /* kept */
+    TEST_ASSERT_EQUAL_UINT16(300, f.q[0][48]); /* replaced */
+    TEST_ASSERT_EQUAL_UINT32(1125, f.wh[0]);
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[1]);
+}
+
+static void test_yesterdays_forecast_gives_today_its_tomorrow(void)
+{
+    solar_forecast_t old;
+    memset(&old, 0, sizeof(old));
+    old.day = day_of(2026, 10, 4);
+    old.q[1][40] = 120; /* 5 October, 10:00 */
+    old.wh[0] = 0;
+    old.wh[1] = 300;
+    old.wh[2] = 9000;
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 0));
+    solar_acc_period(&acc, local(2026, 10, 5, 12, 15), 900, 3000.0f);
+    solar_forecast_t f;
+    solar_acc_finish(&acc, &old, 0, &f); /* finishing into the old one is allowed */
+    TEST_ASSERT_EQUAL_UINT16(120, f.q[0][40]);
+    TEST_ASSERT_EQUAL_UINT32(1050, f.wh[0]);
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[1]); /* old's day after is no quarter hours to keep */
+    solar_acc_finish(&acc, &old, 0, &old);
+    TEST_ASSERT_EQUAL_UINT16(120, old.q[0][40]);
+}
+
+/* --- what the fields read --- */
+
+static solar_forecast_t two_quarters(void)
+{
+    solar_forecast_t f;
+    memset(&f, 0, sizeof(f));
+    f.day = day_of(2026, 10, 5);
+    f.q[0][48] = 200; /* 2 kW at 12:00 */
+    f.q[0][49] = 400; /* 4 kW at 12:15 */
+    f.q[1][50] = 100;
+    f.wh[0] = 1500;
+    f.wh[1] = 250;
+    f.wh[2] = 9000;
+    return f;
+}
+
+static void test_today_and_tomorrow_follow_the_local_day(void)
+{
+    solar_forecast_t f = two_quarters();
+    int32_t d = f.day;
+    TEST_ASSERT_EQUAL_PTR(f.q[0], solar_day(&f, d));
+    TEST_ASSERT_EQUAL_PTR(f.q[1], solar_day(&f, d + 1)); /* after midnight, tomorrow is today */
+    TEST_ASSERT_NULL(solar_day(&f, d + 2));
+    TEST_ASSERT_NULL(solar_day(&f, d - 1));
+    TEST_ASSERT_EQUAL_UINT32(9000, solar_day_wh(&f, d + 2));
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, solar_day_wh(&f, d + 3));
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, solar_day_wh(&f, d - 1));
+    f.wh[1] = SOLAR_WH_NONE;
+    TEST_ASSERT_NULL(solar_day(&f, d + 1)); /* no data for that day */
+    solar_forecast_t none;
+    memset(&none, 0, sizeof(none));
+    TEST_ASSERT_NULL(solar_day(&none, d));
+    TEST_ASSERT_NULL(solar_day(NULL, d));
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, solar_day_wh(NULL, d));
+}
+
+static void test_what_is_left_counts_the_rest_of_the_day(void)
+{
+    solar_forecast_t f = two_quarters();
+    TEST_ASSERT_EQUAL_UINT32(1500, solar_left_wh(&f, f.day, 0, 0));
+    TEST_ASSERT_EQUAL_UINT32(1250, solar_left_wh(&f, f.day, 48, 450)); /* half of 12:00's, all of 12:15's */
+    TEST_ASSERT_EQUAL_UINT32(0, solar_left_wh(&f, f.day, 50, 0));
+    TEST_ASSERT_EQUAL_UINT32(250, solar_left_wh(&f, f.day + 1, 0, 0));
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, solar_left_wh(&f, f.day + 2, 0, 0));
+}
+
+static void test_the_peak_is_the_highest_quarter_hour(void)
+{
+    solar_forecast_t f = two_quarters();
+    uint32_t w;
+    int quarter;
+    TEST_ASSERT_TRUE(solar_peak(&f, f.day, &w, &quarter));
+    TEST_ASSERT_EQUAL_UINT32(4000, w);
+    TEST_ASSERT_EQUAL_INT(49, quarter);
+    memset(f.q[0], 0, sizeof(f.q[0]));
+    TEST_ASSERT_FALSE(solar_peak(&f, f.day, &w, &quarter)); /* no output: no peak */
+    TEST_ASSERT_FALSE(solar_peak(&f, f.day + 2, &w, &quarter));
+}
+
+/* --- Open-Meteo --- */
+
+static void test_open_meteo_asks_for_one_plane(void)
+{
+    char url[SOLAR_URL_MAX];
+    TEST_ASSERT_TRUE(solar_open_meteo_url(url, sizeof(url), 491951, 166068, &k_south) > 0);
+    TEST_ASSERT_EQUAL_STRING("https://api.open-meteo.com/v1/forecast?latitude=49.1951&longitude=16.6068"
+                             "&minutely_15=global_tilted_irradiance,temperature_2m&tilt=35&azimuth=0"
+                             "&timezone=auto&timeformat=unixtime&forecast_days=3",
+                             url);
+    const solar_plane_t east = { .kwp = 2.4f, .tilt = 20, .azimuth = -90 };
+    TEST_ASSERT_TRUE(solar_open_meteo_url(url, sizeof(url), -338688, -1512093, &east) > 0);
+    TEST_ASSERT_NOT_NULL(strstr(url, "latitude=-33.8688&longitude=-151.2093"));
+    TEST_ASSERT_NOT_NULL(strstr(url, "&tilt=20&azimuth=-90&"));
+    TEST_ASSERT_EQUAL(0, solar_open_meteo_url(url, 40, 491951, 166068, &k_south)); /* never half a URL */
+    TEST_ASSERT_EQUAL_STRING("", url);
+}
+
+static solar_forecast_t open_meteo_brno(int planes)
+{
+    const char *json = fixture("open-meteo-brno.json");
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
+    for (int i = 0; i < planes; i++) {
+        TEST_ASSERT_TRUE_MESSAGE(solar_parse_open_meteo(json, s_len, &k_south, 14, &acc, s_err, sizeof(s_err)),
+                                 s_err);
+    }
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    return f;
+}
+
+static void test_open_meteo_gives_three_days(void)
+{
+    solar_forecast_t f = open_meteo_brno(1);
+    TEST_ASSERT_EQUAL_INT32(day_of(2026, 10, 5), f.day);
+    TEST_ASSERT_UINT32_WITHIN(30, 16644, f.wh[0]); /* the model over the reply, worked out by hand */
+    TEST_ASSERT_UINT32_WITHIN(30, 22216, f.wh[1]);
+    TEST_ASSERT_UINT32_WITHIN(30, 18652, f.wh[2]);
+    uint32_t w;
+    int quarter;
+    TEST_ASSERT_TRUE(solar_peak(&f, f.day, &w, &quarter));
+    TEST_ASSERT_EQUAL_INT(49, quarter); /* 12:15-12:30 */
+    TEST_ASSERT_UINT32_WITHIN(10, 2870, w);
+    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][0]); /* night */
+}
+
+static void test_two_planes_add_up(void)
+{
+    solar_forecast_t f = open_meteo_brno(2);
+    TEST_ASSERT_UINT32_WITHIN(60, 2 * 16644, f.wh[0]);
+}
+
+static void test_open_meteo_leaves_out_what_it_has_no_value_for(void)
+{
+    const char *json = "{\"minutely_15\":{\"time\":[1791195300,1791196200],"
+                       "\"global_tilted_irradiance\":[null,500.0],\"temperature_2m\":[12.0,null]}}";
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
+    TEST_ASSERT_FALSE(solar_parse_open_meteo(json, strlen(json), &k_south, 14, &acc, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("no data", s_err);
+}
+
+static void test_open_meteo_refusals(void)
+{
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
+    static const struct {
+        const char *json, *why;
+    } k_cases[] = {
+        { "not json", "not a JSON object" },
+        { "{}", "no data" },
+        { "{\"minutely_15\":{\"time\":[1791195300],\"global_tilted_irradiance\":[],\"temperature_2m\":[1]}}",
+          "no data" },
+        { "{\"error\":true,\"reason\":\"Latitude must be in range of -90 to 90°.\"}", "Latitude must be in ran" },
+        { "{\"error\":true,\"reason\":\"Latitude out of range:°\"}", "Latitude out of range:" }, /* never half a ° */
+        { "{\"minutely_15\":{\"time\":[[[[1]]]]}}", "nested too deeply" },
+        { "{\"minutely_15\":{\"time\":[1e300],\"global_tilted_irradiance\":[500],\"temperature_2m\":[10]}}",
+          "no data" }, /* a time no clock has: left out, never cast */
+    };
+    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
+        TEST_ASSERT_FALSE(solar_parse_open_meteo(k_cases[i].json, strlen(k_cases[i].json), &k_south, 14, &acc,
+                                                 s_err, 24)); /* the sync's detail */
+        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
+    }
+}
+
+int main(void)
+{
+    UNITY_BEGIN();
+    RUN_TEST(test_the_model_follows_the_formula);
+    RUN_TEST(test_the_model_gains_in_the_cold);
+    RUN_TEST(test_no_light_no_power);
+    RUN_TEST(test_a_quarter_hour_period_fills_its_own);
+    RUN_TEST(test_a_half_hour_fills_two_quarter_hours);
+    RUN_TEST(test_a_line_is_integrated_by_quarter_hour);
+    RUN_TEST(test_periods_beyond_the_three_days_are_left_out);
+    RUN_TEST(test_the_night_the_clocks_go_back_keeps_both_hours);
+    RUN_TEST(test_the_night_the_clocks_go_forward_skips_an_hour);
+    RUN_TEST(test_the_inverter_caps_each_quarter_hour);
+    RUN_TEST(test_times_far_off_count_only_within_the_three_days);
+    RUN_TEST(test_power_out_of_reason_stays_within_bounds);
+    RUN_TEST(test_quarter_hours_a_reply_leaves_out_keep_the_last_forecast);
+    RUN_TEST(test_yesterdays_forecast_gives_today_its_tomorrow);
+    RUN_TEST(test_today_and_tomorrow_follow_the_local_day);
+    RUN_TEST(test_what_is_left_counts_the_rest_of_the_day);
+    RUN_TEST(test_the_peak_is_the_highest_quarter_hour);
+    RUN_TEST(test_open_meteo_asks_for_one_plane);
+    RUN_TEST(test_open_meteo_gives_three_days);
+    RUN_TEST(test_two_planes_add_up);
+    RUN_TEST(test_open_meteo_leaves_out_what_it_has_no_value_for);
+    RUN_TEST(test_open_meteo_refusals);
+    return UNITY_END();
+}
```


`test/host/test_util_json.c`:

```diff
--- a/test/host/test_util_json.c
+++ b/test/host/test_util_json.c
@@ -18,10 +18,27 @@ static void test_brackets_inside_strings_do_not_count(void)
     TEST_ASSERT_EQUAL_INT(1, util_json_depth("{\"a\": \"quote \\\" [[[\"}"));
 }
 
+/* A provider's words cut to fit end between characters, never inside one (UTF-8). */
+static void test_text_is_cut_between_characters(void)
+{
+    char out[8];
+    TEST_ASSERT_EQUAL_UINT(7, util_json_text(out, sizeof(out), "abcdefghij"));
+    TEST_ASSERT_EQUAL_STRING("abcdefg", out);
+    TEST_ASSERT_EQUAL_UINT(7, util_json_text(out, sizeof(out), "to 90\xc2\xb0."));
+    TEST_ASSERT_EQUAL_STRING("to 90\xc2\xb0", out);
+    TEST_ASSERT_EQUAL_UINT(6, util_json_text(out, sizeof(out), "to -90\xc2\xb0")); /* ° would need 8 */
+    TEST_ASSERT_EQUAL_STRING("to -90", out);
+    TEST_ASSERT_EQUAL_UINT(5, util_json_text(out, sizeof(out), "abcde\xe2\x82\xac"));
+    TEST_ASSERT_EQUAL_UINT(4, util_json_text(out, sizeof(out), "abcd\xf0\x9f\x8c\x9e"));
+    TEST_ASSERT_EQUAL_UINT(0, util_json_text(out, sizeof(out), NULL));
+    TEST_ASSERT_EQUAL_STRING("", out);
+}
+
 int main(void)
 {
     UNITY_BEGIN();
     RUN_TEST(test_depth_counts_objects_and_lists);
     RUN_TEST(test_brackets_inside_strings_do_not_count);
+    RUN_TEST(test_text_is_cut_between_characters);
     return UNITY_END();
 }
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -181,6 +181,13 @@ target_include_directories(weather_logic PUBLIC ${REPO_ROOT}/components/weather/
 target_compile_options(weather_logic PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(weather_logic PUBLIC datastore PRIVATE cjson util m)
 
+# solar: the PV forecast's requests, replies, model and local quarter hours (spec §11.5); pure C.
+add_library(solar_logic STATIC ${REPO_ROOT}/components/solar/solar.c ${REPO_ROOT}/components/solar/solar_url.c
+            ${REPO_ROOT}/components/solar/solar_parse.c)
+target_include_directories(solar_logic PUBLIC ${REPO_ROOT}/components/solar/include)
+target_compile_options(solar_logic PRIVATE ${REFLBO_WARNINGS})
+target_link_libraries(solar_logic PUBLIC util PRIVATE cjson m)
+
 # ui: pure C on top of gfx, locale, the datastore and the scheduler's local-time rules.
 file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
 add_library(ui STATIC ${UI_SOURCES})
@@ -247,6 +254,8 @@ reflbo_host_test(test_datastore_weather datastore)
 reflbo_host_test(test_weather weather_logic)
 reflbo_host_test(test_sync_ntp sync_logic)
 target_compile_definitions(test_weather PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/open-meteo")
+reflbo_host_test(test_solar solar_logic)
+target_compile_definitions(test_solar PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/solar")
 reflbo_host_test(test_ui_fields ui)
 reflbo_host_test(test_ui_preset ui)
 reflbo_host_test(test_ui_split ui)
```


The recorded reply:

```bash
git checkout plan/m6d -- \
  test/host/fixtures/solar/open-meteo-brno.json
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake -S test/host -B build-host -G Ninja 2>&1 | grep -m2 -A1 'CMake Error'`
Expected: the sources don't exist yet:

```
CMake Error at CMakeLists.txt:185 (add_library):
  Cannot find source file:
--
CMake Error at CMakeLists.txt:185 (add_library):
  No SOURCES given to target: solar_logic
```

- [ ] **Step 3: The component.** A provider's words cut between characters (`util_json`), the header, then the model and the quarter hours (`solar.c`), the request (`solar_url.c`) and the reply (`solar_parse.c`); the component lists its sources:

`components/util/include/util_json.h`:

```diff
--- a/components/util/include/util_json.h
+++ b/components/util/include/util_json.h
@@ -1,5 +1,10 @@
 #pragma once
 
+#include <stddef.h>
+
 /* How deeply a JSON text nests objects and arrays, ignoring brackets inside strings. Parsers
  * check it before cJSON, whose recursion would otherwise run as deep as the text asks. Pure C. */
 int util_json_depth(const char *text);
+/* `text` (NULL: none) into `out`, cut where it doesn't fit before a character, never inside one (UTF-8): a
+ * provider's words in a short detail. Returns the length written. */
+size_t util_json_text(char *out, size_t size, const char *text);
```


`components/util/util_json.c`:

```diff
--- a/components/util/util_json.c
+++ b/components/util/util_json.c
@@ -2,6 +2,7 @@
 
 #include <stdbool.h>
 #include <stddef.h>
+#include <string.h>
 
 int util_json_depth(const char *text)
 {
@@ -29,3 +30,22 @@ int util_json_depth(const char *text)
     }
     return max;
 }
+
+size_t util_json_text(char *out, size_t size, const char *text)
+{
+    if (size == 0) {
+        return 0;
+    }
+    size_t n = text != NULL ? strlen(text) : 0;
+    if (n >= size) {
+        n = size - 1;
+        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) {
+            n--; /* text[n] continues a character: cut before the byte that starts it */
+        }
+    }
+    if (n > 0) {
+        memcpy(out, text, n);
+    }
+    out[n] = '\0';
+    return n;
+}
```


`components/solar/include/solar.h`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/solar/include/solar.h
@@ -0,0 +1,78 @@
+#pragma once
+
+#include <stdbool.h>
+#include <stddef.h>
+#include <stdint.h>
+#include <time.h>
+
+/*
+ * The PV forecast (spec §11.5, D35): the providers' requests and replies, our PV model, and the
+ * forecast as the device keeps it, by local quarter hour. Times map to local quarter hours by the
+ * TZ variable. Pure C on cJSON, host-buildable; the sync task fetches.
+ */
+
+#define SOLAR_STEPS 96           /* local quarter hours a day */
+#define SOLAR_DAYS 3             /* days with a total: the forecast's first, the next, the one after */
+#define SOLAR_UNIT_W 10          /* a quarter hour's power is kept in tens of W: 655 kW at most */
+#define SOLAR_WH_NONE UINT32_MAX /* a day the source sent nothing for */
+#define SOLAR_PLANES_MAX 2
+#define SOLAR_URL_MAX 320
+
+/* A roof plane (spec §11.5): azimuth 0 south, -90 east, 90 west. */
+typedef struct {
+    float kwp;   /* 0.1..100 */
+    int tilt;    /* 0..90° */
+    int azimuth; /* -180..180° */
+} solar_plane_t;
+
+/* The forecast as kept (the snapshot, /fs/state/solar.bin): local day `day` and the next by quarter
+ * hour, and three days' totals. */
+typedef struct {
+    int32_t day;                /* the local day of q[0], days since 1970-01-01; 0 = no forecast */
+    uint32_t fetched;           /* when it came, UTC */
+    uint16_t q[2][SOLAR_STEPS]; /* each quarter hour's mean power, in SOLAR_UNIT_W: `day`, `day` + 1 */
+    uint32_t wh[SOLAR_DAYS];    /* each day's energy, Wh, or SOLAR_WH_NONE; the first two are q's */
+} solar_forecast_t;
+
+/* A reply on its way into a forecast: each local quarter hour's energy over three days from the
+ * local day of the fetch, and which of them the reply covered. */
+typedef struct {
+    int32_t day;
+    float wh[SOLAR_DAYS][SOLAR_STEPS];
+    bool got[SOLAR_DAYS][SOLAR_STEPS];
+} solar_acc_t;
+
+/* Our model (spec §11.5), a plane's output in W: kWp × GTI ÷ 1000 × (1 − 0.004 × (T_cell − 25 °C))
+ * × (1 − losses), with T_cell = T_air + 0.031 × GTI; never below 0. */
+float solar_model_w(float kwp, float gti_w_m2, float t_air_c, int losses_pct);
+
+/* Starts with nothing from the local day of `now`. */
+void solar_acc_init(solar_acc_t *acc, time_t now);
+/* A period's mean power, from `seconds` before `end` to `end` (UTC), over the quarter hours it covers. */
+void solar_acc_period(solar_acc_t *acc, time_t end, int seconds, float watts);
+/* Power at two instants (Forecast.Solar's points): the line between them, integrated by quarter hour. */
+void solar_acc_line(solar_acc_t *acc, time_t t0, float w0, time_t t1, float w1);
+/* The inverter's limit on each quarter hour's mean power (our model's `inverter_kw`). */
+void solar_acc_cap(solar_acc_t *acc, float watts);
+/* The forecast from `acc`, fetched at `fetched` (UTC). Quarter hours the reply didn't cover keep
+ * `old`'s of the same local day (Solcast leaves out the past); `old` may be NULL, or `out` itself. */
+void solar_acc_finish(const solar_acc_t *acc, const solar_forecast_t *old, uint32_t fetched,
+                      solar_forecast_t *out);
+
+/* Local day `day`'s quarter hours: q[0], or q[1] once the forecast's first day is over; NULL for a
+ * day the forecast doesn't cover. */
+const uint16_t *solar_day(const solar_forecast_t *f, int32_t day);
+/* Local day `day`'s energy in Wh, for three days from the forecast's first; else SOLAR_WH_NONE. */
+uint32_t solar_day_wh(const solar_forecast_t *f, int32_t day);
+/* What is still to come on day `day` from `seconds` into quarter hour `quarter`, Wh; SOLAR_WH_NONE
+ * without its quarter hours. */
+uint32_t solar_left_wh(const solar_forecast_t *f, int32_t day, int quarter, int seconds);
+/* Day `day`'s highest quarter hour: its mean power in W and its index; false without output. */
+bool solar_peak(const solar_forecast_t *f, int32_t day, uint32_t *w, int *quarter);
+
+/* Open-Meteo's tilted irradiance for one plane (spec §11.5). Returns the length, or 0 (and "") when
+ * the URL doesn't fit. */
+size_t solar_open_meteo_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4, const solar_plane_t *plane);
+/* Open-Meteo's reply for `plane` through our model, added to `acc`; false with the reason in `err`. */
+bool solar_parse_open_meteo(const char *json, size_t len, const solar_plane_t *plane, int losses_pct,
+                            solar_acc_t *acc, char *err, size_t err_size);
```


`components/solar/solar.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/solar/solar.c
@@ -0,0 +1,194 @@
+#define _POSIX_C_SOURCE 200809L /* localtime_r */
+
+#include "solar.h"
+
+#include <math.h>
+#include <string.h>
+
+#include "util_time.h"
+
+/* Our PV model and the local quarter hours (spec §11.5). */
+
+#define QUARTER_S 900
+#define MAX_W (SOLAR_UNIT_W * (float)UINT16_MAX) /* what a quarter hour keeps: 655.35 kW */
+
+float solar_model_w(float kwp, float gti_w_m2, float t_air_c, int losses_pct)
+{
+    if (!(gti_w_m2 > 0.0f)) {
+        return 0.0f;
+    }
+    float t_cell = t_air_c + 0.031f * gti_w_m2;
+    float w = kwp * gti_w_m2 * (1.0f - 0.004f * (t_cell - 25.0f)) * (1.0f - (float)losses_pct / 100.0f);
+    return w > 0.0f ? w : 0.0f;
+}
+
+static int32_t local_day(time_t t, int *quarter)
+{
+    struct tm lt;
+    localtime_r(&t, &lt);
+    *quarter = lt.tm_hour * 4 + lt.tm_min / 15;
+    return (int32_t)util_days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
+}
+
+void solar_acc_init(solar_acc_t *acc, time_t now)
+{
+    memset(acc, 0, sizeof(*acc));
+    int quarter;
+    acc->day = local_day(now, &quarter);
+}
+
+/* `wh` into the quarter hour that holds `t`, if it is one of the three days. */
+static void add(solar_acc_t *acc, time_t t, float wh)
+{
+    int quarter;
+    int k = local_day(t, &quarter) - acc->day;
+    if (k >= 0 && k < SOLAR_DAYS) {
+        acc->wh[k][quarter] += wh;
+        acc->got[k][quarter] = true;
+    }
+}
+
+/* The next quarter-hour mark after `t`: local quarter hours start on UTC ones, as every time zone's
+ * offset is a whole number of quarter hours. */
+static time_t next_mark(time_t t, time_t end)
+{
+    time_t next = (t / QUARTER_S + 1) * QUARTER_S;
+    return next < end ? next : end;
+}
+
+/* Power a provider sent, within 0 and what a quarter hour keeps; 0 for what isn't a number. */
+static float sane_w(float w)
+{
+    return w > 0.0f ? (w < MAX_W ? w : MAX_W) : 0.0f;
+}
+
+/* [from, to) cut to the UTC the three local days lie in, with a day to spare for any time zone, so a provider's
+ * time years away costs nothing. */
+static void clip(const solar_acc_t *acc, time_t *from, time_t *to)
+{
+    time_t lo = (time_t)(acc->day - 1) * 86400, hi = (time_t)(acc->day + SOLAR_DAYS + 1) * 86400;
+    *from = *from > lo ? *from : lo;
+    *to = *to < hi ? *to : hi;
+}
+
+void solar_acc_period(solar_acc_t *acc, time_t end, int seconds, float watts)
+{
+    time_t t = end - seconds;
+    clip(acc, &t, &end);
+    watts = sane_w(watts);
+    while (t < end) {
+        time_t next = next_mark(t, end);
+        add(acc, t, watts * (float)(next - t) / 3600.0f);
+        t = next;
+    }
+}
+
+void solar_acc_line(solar_acc_t *acc, time_t t0, float w0, time_t t1, float w1)
+{
+    if (t1 <= t0) {
+        return;
+    }
+    w0 = sane_w(w0);
+    w1 = sane_w(w1);
+    float slope = (w1 - w0) / (float)(t1 - t0);
+    time_t from = t0;
+    clip(acc, &from, &t1);
+    for (time_t t = from; t < t1;) {
+        time_t next = next_mark(t, t1);
+        float wa = w0 + slope * (float)(t - t0), wb = w0 + slope * (float)(next - t0);
+        add(acc, t, (wa + wb) / 2.0f * (float)(next - t) / 3600.0f);
+        t = next;
+    }
+}
+
+void solar_acc_cap(solar_acc_t *acc, float watts)
+{
+    float cap_wh = watts / 4.0f;
+    for (int k = 0; k < SOLAR_DAYS; k++) {
+        for (int i = 0; i < SOLAR_STEPS; i++) {
+            acc->wh[k][i] = acc->wh[k][i] > cap_wh ? cap_wh : acc->wh[k][i];
+        }
+    }
+}
+
+void solar_acc_finish(const solar_acc_t *acc, const solar_forecast_t *old, uint32_t fetched,
+                      solar_forecast_t *out)
+{
+    solar_forecast_t before;
+    bool keep = old != NULL && old->day != 0;
+    if (keep) {
+        before = *old; /* `out` may be `old` */
+    }
+    memset(out, 0, sizeof(*out));
+    out->day = acc->day;
+    out->fetched = fetched;
+    for (int k = 0; k < SOLAR_DAYS; k++) {
+        const uint16_t *kept = keep && k < 2 ? solar_day(&before, acc->day + k) : NULL;
+        bool any = false;
+        double sum = 0.0;
+        for (int i = 0; i < SOLAR_STEPS; i++) {
+            float wh;
+            if (acc->got[k][i]) {
+                wh = acc->wh[k][i] < MAX_W / 4.0f ? acc->wh[k][i] : MAX_W / 4.0f; /* two planes' sum */
+            } else if (kept != NULL) {
+                wh = kept[i] * (SOLAR_UNIT_W / 4.0f);
+            } else {
+                continue;
+            }
+            any = true;
+            if (k < 2) { /* today's and tomorrow's totals are their quarter hours', so the fields agree */
+                out->q[k][i] = (uint16_t)lroundf(wh * 4.0f / SOLAR_UNIT_W);
+                sum += out->q[k][i] * (SOLAR_UNIT_W / 4.0);
+            } else {
+                sum += wh;
+            }
+        }
+        out->wh[k] = any ? (uint32_t)lround(sum) : SOLAR_WH_NONE;
+    }
+}
+
+const uint16_t *solar_day(const solar_forecast_t *f, int32_t day)
+{
+    if (f == NULL || f->day == 0) {
+        return NULL;
+    }
+    int32_t k = day - f->day;
+    return k >= 0 && k < 2 && f->wh[k] != SOLAR_WH_NONE ? f->q[k] : NULL;
+}
+
+uint32_t solar_day_wh(const solar_forecast_t *f, int32_t day)
+{
+    if (f == NULL || f->day == 0) {
+        return SOLAR_WH_NONE;
+    }
+    int32_t k = day - f->day;
+    return k >= 0 && k < SOLAR_DAYS ? f->wh[k] : SOLAR_WH_NONE;
+}
+
+uint32_t solar_left_wh(const solar_forecast_t *f, int32_t day, int quarter, int seconds)
+{
+    const uint16_t *q = solar_day(f, day);
+    if (q == NULL || quarter < 0 || quarter >= SOLAR_STEPS) {
+        return SOLAR_WH_NONE;
+    }
+    double wh = q[quarter] * (SOLAR_UNIT_W / 4.0) * (QUARTER_S - seconds) / QUARTER_S;
+    for (int i = quarter + 1; i < SOLAR_STEPS; i++) {
+        wh += q[i] * (SOLAR_UNIT_W / 4.0);
+    }
+    return (uint32_t)lround(wh);
+}
+
+bool solar_peak(const solar_forecast_t *f, int32_t day, uint32_t *w, int *quarter)
+{
+    const uint16_t *q = solar_day(f, day);
+    if (q == NULL) {
+        return false;
+    }
+    int best = 0;
+    for (int i = 1; i < SOLAR_STEPS; i++) {
+        best = q[i] > q[best] ? i : best;
+    }
+    *w = (uint32_t)q[best] * SOLAR_UNIT_W;
+    *quarter = best;
+    return q[best] > 0;
+}
```


`components/solar/solar_url.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/solar/solar_url.c
@@ -0,0 +1,37 @@
+#include <stdio.h>
+#include <string.h>
+
+#include "solar.h"
+
+/* The providers' requests (spec §11.5). Ours are in 1e-4 degrees; they take decimal degrees. */
+
+static void degrees(char *out, size_t size, int32_t e4)
+{
+    uint32_t a = e4 < 0 ? (uint32_t)(-(int64_t)e4) : (uint32_t)e4;
+    snprintf(out, size, "%s%lu.%04lu", e4 < 0 ? "-" : "", (unsigned long)(a / 10000), (unsigned long)(a % 10000));
+}
+
+/* snprintf's result, or 0 and "" when it didn't fit: callers never see half a URL. */
+static size_t fitted(int n, char *out, size_t size)
+{
+    if (n > 0 && (size_t)n < size) {
+        return (size_t)n;
+    }
+    if (size > 0) {
+        out[0] = '\0';
+    }
+    return 0;
+}
+
+size_t solar_open_meteo_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4, const solar_plane_t *plane)
+{
+    char lat[16], lon[16];
+    degrees(lat, sizeof(lat), lat_e4);
+    degrees(lon, sizeof(lon), lon_e4);
+    return fitted(snprintf(out, size,
+                           "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
+                           "&minutely_15=global_tilted_irradiance,temperature_2m&tilt=%d&azimuth=%d"
+                           "&timezone=auto&timeformat=unixtime&forecast_days=3",
+                           lat, lon, plane->tilt, plane->azimuth),
+                  out, size);
+}
```


`components/solar/solar_parse.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/solar/solar_parse.c
@@ -0,0 +1,80 @@
+#include <math.h>
+#include <stdio.h>
+#include <string.h>
+
+#include "cJSON.h"
+#include "solar.h"
+#include "util_json.h"
+
+/* The providers' replies (spec §11.5), into a solar_acc_t. */
+
+#define OPEN_METEO_DEPTH 4 /* root, block, series */
+
+static bool fail(char *err, size_t err_size, const char *why)
+{
+    snprintf(err, err_size, "%s", why);
+    return false;
+}
+
+static const cJSON *member(const cJSON *o, const char *key)
+{
+    return cJSON_GetObjectItemCaseSensitive(o, key);
+}
+
+static bool number(const cJSON *item, double *out)
+{
+    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
+        return false; /* also null */
+    }
+    *out = item->valuedouble;
+    return true;
+}
+
+/* The root object, after the depth check; NULL with the reason in `err`. */
+static cJSON *open_reply(const char *json, size_t len, int depth, char *err, size_t err_size)
+{
+    if (util_json_depth(json) > depth) {
+        fail(err, err_size, "nested too deeply");
+        return NULL;
+    }
+    cJSON *root = cJSON_ParseWithLength(json, len);
+    if (!cJSON_IsObject(root)) {
+        cJSON_Delete(root);
+        fail(err, err_size, "not a JSON object");
+        return NULL;
+    }
+    return root;
+}
+
+bool solar_parse_open_meteo(const char *json, size_t len, const solar_plane_t *plane, int losses_pct,
+                            solar_acc_t *acc, char *err, size_t err_size)
+{
+    cJSON *root = open_reply(json, len, OPEN_METEO_DEPTH, err, err_size);
+    if (root == NULL) {
+        return false;
+    }
+    const cJSON *reason = member(root, "reason");
+    if (cJSON_IsTrue(member(root, "error"))) {
+        util_json_text(err, err_size, cJSON_IsString(reason) ? reason->valuestring : "refused"); /* the step names it */
+        cJSON_Delete(root);
+        return false;
+    }
+    const cJSON *m = member(root, "minutely_15");
+    const cJSON *times = member(m, "time"), *gti = member(m, "global_tilted_irradiance");
+    const cJSON *temp = member(m, "temperature_2m");
+    int n = cJSON_IsArray(times) ? cJSON_GetArraySize(times) : 0;
+    int used = 0;
+    if (cJSON_IsArray(gti) && cJSON_IsArray(temp) && cJSON_GetArraySize(gti) == n && cJSON_GetArraySize(temp) == n) {
+        const cJSON *t = times->child, *g = gti->child, *a = temp->child;
+        for (; t != NULL && g != NULL && a != NULL; t = t->next, g = g->next, a = a->next) {
+            double at, w_m2, c;
+            if (number(t, &at) && at >= 0.0 && at < 1e10 && number(g, &w_m2) && number(a, &c)) { /* 1970-2286 */
+                /* each value: the 15 min before */
+                solar_acc_period(acc, (time_t)at, 900, solar_model_w(plane->kwp, (float)w_m2, (float)c, losses_pct));
+                used++;
+            }
+        }
+    }
+    cJSON_Delete(root);
+    return used > 0 || fail(err, err_size, "no data");
+}
```


`components/solar/CMakeLists.txt`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/solar/CMakeLists.txt
@@ -0,0 +1,5 @@
+# The PV forecast (spec §11.5, D35): the providers' requests and replies, our PV model and the local
+# quarter hours. Pure C on cJSON, also built on the host; the sync task fetches.
+idf_component_register(SRCS "solar.c" "solar_url.c" "solar_parse.c"
+                       INCLUDE_DIRS "include"
+                       PRIV_REQUIRES json util)
```


- [ ] **Step 4: Run the tests.**

Run: `cmake -S test/host -B build-host -G Ninja >/dev/null && cmake --build build-host && ./build-host/test_solar | tail -2 && ./build-host/test_util_json | tail -2 && ctest --test-dir build-host | tail -3`
Expected:

```
22 Tests 0 Failures 0 Ignored
OK
3 Tests 0 Failures 0 Ignored
OK
100% tests passed, 0 tests failed out of 62
```

- [ ] **Step 5: The firmware builds** with the new component: `tools/idf.sh reconfigure && tools/idf.sh build`, clean, without a warning (nothing calls `solar` yet, so the app's size doesn't change).

- [ ] **Step 6: Commit.**

```bash
git add components/solar components/util test/host/CMakeLists.txt test/host/test_solar.c test/host/test_util_json.c \
  test/host/fixtures/solar
git commit -m "feat(solar): our PV model, the local quarter hours and Open-Meteo's forecast"
```

### Task 2: Forecast.Solar's and Solcast's forecasts, and Solcast's budget (`solar`, `timekeeping`)

**Files:**
- Modify: `components/solar/include/solar.h`, `components/solar/solar.c`, `components/solar/solar_url.c`, `components/solar/solar_parse.c`, `components/solar/CMakeLists.txt`, `components/timekeeping/include/timekeeping_iso.h`, `components/timekeeping/timekeeping_iso.c`, `test/host/CMakeLists.txt`
- Test: `test/host/test_solar.c`, `test/host/test_timekeeping_iso.c`; fixtures `forecast-solar-brno.json`, `forecast-solar-key.json`, `solcast-brno.json` (copied from the branch)

**Interfaces:**
- Consumes: Task 1's `solar_acc_*`, `solar_plane_t`; `timekeeping_parse_iso8601()` (timekeeping_iso.h), which now takes a fraction of a second ("2026-10-05T05:30:00.0000000Z", Solcast's).
- Produces (Tasks 9, 10):
  - `SOLAR_KEY_MAX` (64, a key or a site id with its NUL);
  - `size_t solar_forecast_solar_url(out, size, lat_e4, lon_e4, planes, count, key)`: `/estimate/watts/`, every plane with a key, the first alone without one; a key of other characters than letters and digits makes no URL;
  - `bool solar_parse_forecast_solar(json, len, acc, err, err_size)`: `result` (or `result.watts`), the power at each point, the line between them; a refusal's text without the provider's name, cut between characters (Task 1's `util_json_text()`);
  - `size_t solar_solcast_url(out, size, site)`, `bool solar_parse_solcast(json, len, acc, err, err_size)`: `pv_estimate` in kW for each period ending at `period_end` (PT30M by default);
  - `uint32_t solar_solcast_wait_s(int sites)` (3 h, or 6 h with two sites), `bool solar_solcast_due(uint32_t last_asked, int sites, uint32_t now)` (never asked, the clock went back, or the wait is over; failed calls count).

The `solar` component now needs `timekeeping` for the time stamps (spec §3.1 named `util` alone; the as-built revision records it).

- [ ] **Step 1: Write the failing tests.** Forecast.Solar's requests with and without a key and its refusals (in the sync's 24-byte detail); its free reply against its own day totals (15 025 and 17 343 Wh, from `watt_hours_day` of the same request), a quarter hour on the line (07:00–07:15 local: 140 W from 110 W at 07:00 towards 352 W at 08:00); its keyed reply's third day; Solcast's request, a reply that starts at 11:00, two sites adding up, its refusals (in 24 bytes too) and its budget; a fraction of a second in a time stamp:

`test/host/test_solar.c`:

```diff
--- a/test/host/test_solar.c
+++ b/test/host/test_solar.c
@@ -11,8 +11,10 @@
 #include "util_time.h"
 
 /* The PV forecast (spec §11.5, D35): our model, the local quarter hours and the replies. The
- * Open-Meteo fixture was fetched on 2026-10-05 for Brno (49.1951, 16.6068), a plane tilted 35°
- * facing south. */
+ * Open-Meteo and Forecast.Solar fixtures were fetched on 2026-10-05 for Brno (49.1951, 16.6068), a
+ * plane of 5 kWp tilted 35° facing south (Forecast.Solar's free tier; its rate limit's address is
+ * replaced). Forecast.Solar with a key and Solcast are built in their documented formats, as there
+ * is no account to record them with. */
 
 #define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"
 
@@ -415,6 +417,158 @@ static void test_open_meteo_refusals(void)
     }
 }
 
+/* --- Forecast.Solar --- */
+
+static void test_forecast_solar_asks_without_a_key(void)
+{
+    char url[SOLAR_URL_MAX];
+    TEST_ASSERT_TRUE(solar_forecast_solar_url(url, sizeof(url), 491951, 166068, &k_south, 1, "") > 0);
+    TEST_ASSERT_EQUAL_STRING("https://api.forecast.solar/estimate/watts/49.1951/16.6068/35/0/5?time=utc", url);
+}
+
+static void test_forecast_solar_with_a_key_names_both_planes(void)
+{
+    const solar_plane_t planes[2] = { { .kwp = 5.2f, .tilt = 35, .azimuth = 0 },
+                                      { .kwp = 2.45f, .tilt = 20, .azimuth = -90 } };
+    char url[SOLAR_URL_MAX];
+    TEST_ASSERT_TRUE(solar_forecast_solar_url(url, sizeof(url), 491951, 166068, planes, 2, "AbC123xyz") > 0);
+    TEST_ASSERT_EQUAL_STRING("https://api.forecast.solar/AbC123xyz/estimate/watts/49.1951/16.6068/35/0/5.2"
+                             "/20/-90/2.45?time=utc",
+                             url);
+    TEST_ASSERT_TRUE(solar_forecast_solar_url(url, sizeof(url), 491951, 166068, planes, 2, "") > 0);
+    TEST_ASSERT_EQUAL_STRING("https://api.forecast.solar/estimate/watts/49.1951/16.6068/35/0/5.2?time=utc",
+                             url); /* without a key, the first plane alone: a free account takes one */
+    TEST_ASSERT_EQUAL(0, solar_forecast_solar_url(url, sizeof(url), 491951, 166068, planes, 1, "ab/c"));
+    TEST_ASSERT_EQUAL_STRING("", url); /* a key goes into the path as it is */
+}
+
+static void test_forecast_solar_follows_the_line_between_its_points(void)
+{
+    const char *json = fixture("forecast-solar-brno.json");
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 28));
+    TEST_ASSERT_TRUE_MESSAGE(solar_parse_forecast_solar(json, s_len, &acc, s_err, sizeof(s_err)), s_err);
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_UINT32_WITHIN(30, 15025, f.wh[0]); /* its own watt_hours_day for the same request */
+    TEST_ASSERT_UINT32_WITHIN(30, 17343, f.wh[1]);
+    TEST_ASSERT_EQUAL_UINT32(SOLAR_WH_NONE, f.wh[2]); /* the free tier has two days */
+    TEST_ASSERT_EQUAL_UINT16(14, f.q[0][28]); /* 07:00-07:15: from 110 W at 07:00 towards 352 W at 08:00 */
+    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][20]);  /* before sunrise */
+}
+
+static void test_forecast_solar_with_a_key_has_a_third_day(void)
+{
+    const char *json = fixture("forecast-solar-key.json"); /* 15-minute points for three days */
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 28));
+    TEST_ASSERT_TRUE_MESSAGE(solar_parse_forecast_solar(json, s_len, &acc, s_err, sizeof(s_err)), s_err);
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_UINT32_WITHIN(30, 24703, f.wh[0]);
+    TEST_ASSERT_UINT32_WITHIN(30, 26622, f.wh[1]);
+    TEST_ASSERT_UINT32_WITHIN(30, 14260, f.wh[2]);
+}
+
+static void test_forecast_solar_refusals(void)
+{
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 28));
+    static const struct {
+        const char *json, *why;
+    } k_cases[] = {
+        { "not json", "not a JSON object" },
+        { "{\"result\":null,\"message\":{\"code\":429,\"type\":\"error\",\"text\":\"Rate limit for API calls "
+          "reached.\"}}",
+          "Rate limit for API call" },
+        { "{\"result\":{},\"message\":{\"code\":0,\"type\":\"success\"}}", "no data" },
+        { "{\"result\":{\"2026-10-05T05:00:00+00:00\":110}}", "no data" }, /* a point is no line */
+        { "{\"result\":{\"yesterday\":1,\"today\":2}}", "no data" },
+        { "{\"result\":{\"a\":[[[[1]]]]}}", "nested too deeply" },
+    };
+    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
+        TEST_ASSERT_FALSE(solar_parse_forecast_solar(k_cases[i].json, strlen(k_cases[i].json), &acc, s_err, 24));
+        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
+    }
+}
+
+/* --- Solcast --- */
+
+static void test_solcast_asks_for_a_site(void)
+{
+    char url[SOLAR_URL_MAX];
+    TEST_ASSERT_TRUE(solar_solcast_url(url, sizeof(url), "ab12-cd34-ef56-7890") > 0);
+    TEST_ASSERT_EQUAL_STRING("https://api.solcast.com.au/rooftop_sites/ab12-cd34-ef56-7890/forecasts"
+                             "?format=json&hours=72",
+                             url);
+    TEST_ASSERT_EQUAL(0, solar_solcast_url(url, sizeof(url), "ab12/../x"));
+    TEST_ASSERT_EQUAL(0, solar_solcast_url(url, sizeof(url), ""));
+}
+
+static void test_solcast_fills_half_hours_from_now(void)
+{
+    const char *json = fixture("solcast-brno.json"); /* 72 h from 11:00 */
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
+    TEST_ASSERT_TRUE_MESSAGE(solar_parse_solcast(json, s_len, &acc, s_err, sizeof(s_err)), s_err);
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_EQUAL_UINT16(359, f.q[0][44]); /* 3.5904 kW from 11:00 to 11:30 */
+    TEST_ASSERT_EQUAL_UINT16(359, f.q[0][45]);
+    TEST_ASSERT_EQUAL_UINT16(0, f.q[0][43]); /* the morning is past: not in the reply */
+    TEST_ASSERT_UINT32_WITHIN(30, 20040, f.wh[0]);
+    TEST_ASSERT_UINT32_WITHIN(30, 29295, f.wh[1]);
+    TEST_ASSERT_UINT32_WITHIN(30, 15689, f.wh[2]);
+}
+
+static void test_two_solcast_sites_add_up(void)
+{
+    const char *json = fixture("solcast-brno.json");
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
+    TEST_ASSERT_TRUE(solar_parse_solcast(json, s_len, &acc, s_err, sizeof(s_err)));
+    TEST_ASSERT_TRUE(solar_parse_solcast(json, s_len, &acc, s_err, sizeof(s_err)));
+    solar_forecast_t f;
+    solar_acc_finish(&acc, NULL, 0, &f);
+    TEST_ASSERT_EQUAL_UINT16(718, f.q[0][44]);
+}
+
+static void test_solcast_refusals(void)
+{
+    solar_acc_t acc;
+    solar_acc_init(&acc, local(2026, 10, 5, 11, 16));
+    static const struct {
+        const char *json, *why;
+    } k_cases[] = {
+        { "not json", "not a JSON object" },
+        { "{\"response_status\":{\"error_code\":\"TooManyRequests\",\"message\":\"You have exceeded your free "
+          "daily limit.\",\"errors\":[]}}",
+          "You have exceeded your " },
+        { "{\"forecasts\":[]}", "no data" },
+        { "{\"forecasts\":[{\"pv_estimate\":1.0,\"period_end\":\"soon\",\"period\":\"PT30M\"},"
+          "{\"pv_estimate\":1.0,\"period_end\":\"2026-10-05T10:00:00.0000000Z\",\"period\":\"P1D\"}]}",
+          "no data" },
+        { "{\"forecasts\":[[[[1]]]]}", "nested too deeply" },
+    };
+    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
+        TEST_ASSERT_FALSE(solar_parse_solcast(k_cases[i].json, strlen(k_cases[i].json), &acc, s_err, 24));
+        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
+    }
+}
+
+static void test_solcast_keeps_within_its_ten_calls_a_day(void)
+{
+    const uint32_t at = 1791195300;
+    TEST_ASSERT_TRUE(solar_solcast_due(0, 1, at)); /* never asked */
+    TEST_ASSERT_FALSE(solar_solcast_due(at, 1, at + 2 * 3600 + 3599));
+    TEST_ASSERT_TRUE(solar_solcast_due(at, 1, at + 3 * 3600)); /* 8 a day with one site */
+    TEST_ASSERT_FALSE(solar_solcast_due(at, 2, at + 5 * 3600));
+    TEST_ASSERT_TRUE(solar_solcast_due(at, 2, at + 6 * 3600)); /* 4 a day, two calls each */
+    TEST_ASSERT_TRUE(solar_solcast_due(at, 1, at - 60));       /* the clock went back: don't wait for it */
+    TEST_ASSERT_EQUAL_UINT32(3 * 3600, solar_solcast_wait_s(1));
+    TEST_ASSERT_EQUAL_UINT32(6 * 3600, solar_solcast_wait_s(2));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -440,5 +594,15 @@ int main(void)
     RUN_TEST(test_two_planes_add_up);
     RUN_TEST(test_open_meteo_leaves_out_what_it_has_no_value_for);
     RUN_TEST(test_open_meteo_refusals);
+    RUN_TEST(test_forecast_solar_asks_without_a_key);
+    RUN_TEST(test_forecast_solar_with_a_key_names_both_planes);
+    RUN_TEST(test_forecast_solar_follows_the_line_between_its_points);
+    RUN_TEST(test_forecast_solar_with_a_key_has_a_third_day);
+    RUN_TEST(test_forecast_solar_refusals);
+    RUN_TEST(test_solcast_asks_for_a_site);
+    RUN_TEST(test_solcast_fills_half_hours_from_now);
+    RUN_TEST(test_two_solcast_sites_add_up);
+    RUN_TEST(test_solcast_refusals);
+    RUN_TEST(test_solcast_keeps_within_its_ten_calls_a_day);
     return UNITY_END();
 }
```


`test/host/test_timekeeping_iso.c`:

```diff
--- a/test/host/test_timekeeping_iso.c
+++ b/test/host/test_timekeeping_iso.c
@@ -48,6 +48,17 @@ static void test_seconds_are_optional(void)
     TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805 - 5, t);
 }
 
+static void test_a_fraction_of_a_second_is_dropped(void)
+{
+    time_t t = 0;
+    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25T20:48:05.0000000Z", &t)); /* Solcast's form */
+    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
+    TEST_ASSERT_TRUE(timekeeping_parse_iso8601("2026-09-25T22:48:05.75+02:00", &t));
+    TEST_ASSERT_EQUAL_INT64(FRI_2026_09_25_204805, t);
+    TEST_ASSERT_FALSE(timekeeping_parse_iso8601("2026-09-25T20:48:05.Z", &t)); /* a point needs digits */
+    TEST_ASSERT_FALSE(timekeeping_parse_iso8601("2026-09-25T20:48.5Z", &t));  /* and seconds before it */
+}
+
 static void test_rejects_malformed_or_impossible_input(void)
 {
     const char *bad[] = { "", "garbage", "2026-13-01T00:00Z", "2026-02-30T00:00Z", "2026-09-25T25:00Z",
@@ -67,6 +78,7 @@ int main(void)
     RUN_TEST(test_parses_an_offset);
     RUN_TEST(test_parses_local_time_with_the_tz_rules);
     RUN_TEST(test_seconds_are_optional);
+    RUN_TEST(test_a_fraction_of_a_second_is_dropped);
     RUN_TEST(test_rejects_malformed_or_impossible_input);
     return UNITY_END();
 }
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -186,7 +186,7 @@ add_library(solar_logic STATIC ${REPO_ROOT}/components/solar/solar.c ${REPO_ROOT
             ${REPO_ROOT}/components/solar/solar_parse.c)
 target_include_directories(solar_logic PUBLIC ${REPO_ROOT}/components/solar/include)
 target_compile_options(solar_logic PRIVATE ${REFLBO_WARNINGS})
-target_link_libraries(solar_logic PUBLIC util PRIVATE cjson m)
+target_link_libraries(solar_logic PUBLIC util timekeeping_logic PRIVATE cjson m)
 
 # ui: pure C on top of gfx, locale, the datastore and the scheduler's local-time rules.
 file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
```


The replies: Forecast.Solar's free one recorded on 2026-10-05, the others built in their documented formats.

```bash
git checkout plan/m6d -- \
  test/host/fixtures/solar/forecast-solar-brno.json \
  test/host/fixtures/solar/forecast-solar-key.json \
  test/host/fixtures/solar/solcast-brno.json
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host --target test_solar 2>&1 | grep -E 'error:' | sed -E 's/.*error: //' | sort | uniq -c | sort -rn | head -5; cmake --build build-host --target test_timekeeping_iso >/dev/null && ./build-host/test_timekeeping_iso | grep -E 'FAIL|Tests'`
Expected: the new functions aren't declared, and the fraction is refused:

```
   6 call to undeclared function 'solar_solcast_due'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
   4 call to undeclared function 'solar_parse_solcast'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
   4 call to undeclared function 'solar_forecast_solar_url'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
   3 call to undeclared function 'solar_parse_forecast_solar'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
   2 call to undeclared function 'solar_solcast_url'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
…/test/host/test_timekeeping_iso.c:54:test_a_fraction_of_a_second_is_dropped:FAIL: Expected TRUE Was FALSE
6 Tests 1 Failures 0 Ignored
FAIL
```

- [ ] **Step 3: The fraction of a second.** `timekeeping_parse_iso8601()` drops it, where the seconds came first:

`components/timekeeping/include/timekeeping_iso.h`:

```diff
--- a/components/timekeeping/include/timekeeping_iso.h
+++ b/components/timekeeping/include/timekeeping_iso.h
@@ -4,8 +4,9 @@
 #include <time.h>
 
 /*
- * Parses an ISO 8601 date and time for `rtc set`: "YYYY-MM-DDTHH:MM[:SS]" followed by "Z", an
- * offset "+HH:MM" / "-HH:MM", or nothing for local time (the TZ variable). A space may replace the
- * "T". Pure C, host-buildable.
+ * Parses an ISO 8601 date and time for `rtc set` and the providers' replies:
+ * "YYYY-MM-DDTHH:MM[:SS[.fff]]" followed by "Z", an offset "+HH:MM" / "-HH:MM", or nothing for local
+ * time (the TZ variable). A space may replace the "T"; a fraction of the second is dropped. Pure C,
+ * host-buildable.
  */
 bool timekeeping_parse_iso8601(const char *text, time_t *utc);
```


`components/timekeeping/timekeeping_iso.c`:

```diff
--- a/components/timekeeping/timekeeping_iso.c
+++ b/components/timekeeping/timekeeping_iso.c
@@ -50,9 +50,17 @@ bool timekeeping_parse_iso8601(const char *text, time_t *utc)
     if (!number(&p, 2, &h) || !expect(&p, ':') || !number(&p, 2, &mi)) {
         return false;
     }
-    if (*p == ':' && (p++, !number(&p, 2, &s))) {
+    bool with_seconds = *p == ':';
+    if (with_seconds && (p++, !number(&p, 2, &s))) {
         return false;
     }
+    if (*p == '.' && with_seconds) { /* a fraction of the second (Solcast's), dropped */
+        if (!isdigit((unsigned char)p[1])) {
+            return false;
+        }
+        for (p++; isdigit((unsigned char)*p); p++) {
+        }
+    }
     if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) || h > 23 || mi > 59 || s > 59) {
         return false;
     }
```


- [ ] **Step 4: Forecast.Solar and Solcast.** Their requests, their replies into `solar_acc_t`, and Solcast's budget:

`components/solar/include/solar.h`:

```diff
--- a/components/solar/include/solar.h
+++ b/components/solar/include/solar.h
@@ -17,6 +17,7 @@
 #define SOLAR_WH_NONE UINT32_MAX /* a day the source sent nothing for */
 #define SOLAR_PLANES_MAX 2
 #define SOLAR_URL_MAX 320
+#define SOLAR_KEY_MAX 64 /* a key or a site id, NUL included */
 
 /* A roof plane (spec §11.5): azimuth 0 south, -90 east, 90 west. */
 typedef struct {
@@ -76,3 +77,20 @@ size_t solar_open_meteo_url(char *out, size_t size, int32_t lat_e4, int32_t lon_
 /* Open-Meteo's reply for `plane` through our model, added to `acc`; false with the reason in `err`. */
 bool solar_parse_open_meteo(const char *json, size_t len, const solar_plane_t *plane, int losses_pct,
                             solar_acc_t *acc, char *err, size_t err_size);
+
+/* Forecast.Solar's power at points (spec §11.5): every plane with a key, the first alone without one
+ * (`key` ""). A key of other characters than letters and digits makes no URL (0 and ""). */
+size_t solar_forecast_solar_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4, const solar_plane_t *planes,
+                                int count, const char *key);
+/* Its reply, `watts` at each point, in W; the line between the points, added to `acc`. */
+bool solar_parse_forecast_solar(const char *json, size_t len, solar_acc_t *acc, char *err, size_t err_size);
+
+/* Solcast's forecast for one rooftop site (spec §11.5), asked with "Authorization: Bearer <key>";
+ * a site id of other characters than letters, digits and '-' makes no URL. */
+size_t solar_solcast_url(char *out, size_t size, const char *site);
+/* Its reply, `pv_estimate` in kW for each period ending at `period_end`, added to `acc`. */
+bool solar_parse_solcast(const char *json, size_t len, solar_acc_t *acc, char *err, size_t err_size);
+/* Solcast's 10 calls a UTC day: at most one a site every 3 h with one site, every 6 h with two. */
+uint32_t solar_solcast_wait_s(int sites);
+/* Whether a sync may ask now, given when it last asked (0 for never; failed calls count). */
+bool solar_solcast_due(uint32_t last_asked, int sites, uint32_t now);
```


`components/solar/solar_url.c`:

```diff
--- a/components/solar/solar_url.c
+++ b/components/solar/solar_url.c
@@ -1,3 +1,4 @@
+#include <ctype.h>
 #include <stdio.h>
 #include <string.h>
 
@@ -35,3 +36,57 @@ size_t solar_open_meteo_url(char *out, size_t size, int32_t lat_e4, int32_t lon_
                            lat, lon, plane->tilt, plane->azimuth),
                   out, size);
 }
+
+/* Letters and digits, and `extra` where given: what goes into a path as it is. */
+static bool plain(const char *s, char extra)
+{
+    for (; *s != '\0'; s++) {
+        if (!isalnum((unsigned char)*s) && *s != extra) {
+            return false;
+        }
+    }
+    return true;
+}
+
+/* kWp as Forecast.Solar takes it: "5", "5.2", "2.45". */
+static void kwp_text(char *out, size_t size, float kwp)
+{
+    snprintf(out, size, "%.2f", (double)kwp);
+    char *end = out + strlen(out);
+    while (end > out && end[-1] == '0') {
+        *--end = '\0';
+    }
+    if (end > out && end[-1] == '.') {
+        end[-1] = '\0';
+    }
+}
+
+size_t solar_forecast_solar_url(char *out, size_t size, int32_t lat_e4, int32_t lon_e4, const solar_plane_t *planes,
+                                int count, const char *key)
+{
+    if (!plain(key, '\0')) {
+        return fitted(-1, out, size);
+    }
+    char lat[16], lon[16], path[96] = "";
+    degrees(lat, sizeof(lat), lat_e4);
+    degrees(lon, sizeof(lon), lon_e4);
+    for (int i = 0; i < (key[0] != '\0' ? count : 1) && i < SOLAR_PLANES_MAX; i++) {
+        char kwp[16];
+        kwp_text(kwp, sizeof(kwp), planes[i].kwp);
+        size_t n = strlen(path);
+        snprintf(path + n, sizeof(path) - n, "/%d/%d/%s", planes[i].tilt, planes[i].azimuth, kwp);
+    }
+    return fitted(snprintf(out, size, "https://api.forecast.solar/%s%sestimate/watts/%s/%s%s?time=utc", key,
+                           key[0] != '\0' ? "/" : "", lat, lon, path),
+                  out, size);
+}
+
+size_t solar_solcast_url(char *out, size_t size, const char *site)
+{
+    if (site[0] == '\0' || !plain(site, '-')) {
+        return fitted(-1, out, size);
+    }
+    return fitted(snprintf(out, size, "https://api.solcast.com.au/rooftop_sites/%s/forecasts?format=json&hours=72",
+                           site),
+                  out, size);
+}
```


`components/solar/solar_parse.c`:

```diff
--- a/components/solar/solar_parse.c
+++ b/components/solar/solar_parse.c
@@ -4,11 +4,14 @@
 
 #include "cJSON.h"
 #include "solar.h"
+#include "timekeeping_iso.h"
 #include "util_json.h"
 
 /* The providers' replies (spec §11.5), into a solar_acc_t. */
 
-#define OPEN_METEO_DEPTH 4 /* root, block, series */
+#define OPEN_METEO_DEPTH 4     /* root, block, series */
+#define FORECAST_SOLAR_DEPTH 4 /* root, result (or message), its watts (or info) */
+#define SOLCAST_DEPTH 4        /* root, forecasts, a period (or the error's list) */
 
 static bool fail(char *err, size_t err_size, const char *why)
 {
@@ -78,3 +81,92 @@ bool solar_parse_open_meteo(const char *json, size_t len, const solar_plane_t *p
     cJSON_Delete(root);
     return used > 0 || fail(err, err_size, "no data");
 }
+
+/* A provider's own words for a refusal, cut between characters; the step and its source name the provider. */
+static bool refused(char *err, size_t err_size, const cJSON *text)
+{
+    util_json_text(err, err_size, cJSON_IsString(text) ? text->valuestring : "refused");
+    return false;
+}
+
+bool solar_parse_forecast_solar(const char *json, size_t len, solar_acc_t *acc, char *err, size_t err_size)
+{
+    cJSON *root = open_reply(json, len, FORECAST_SOLAR_DEPTH, err, err_size);
+    if (root == NULL) {
+        return false;
+    }
+    const cJSON *message = member(root, "message");
+    const cJSON *type = member(message, "type");
+    if (cJSON_IsString(type) && strcmp(type->valuestring, "error") == 0) {
+        refused(err, err_size, member(message, "text"));
+        cJSON_Delete(root);
+        return false;
+    }
+    const cJSON *points = member(root, "result");
+    if (cJSON_IsObject(member(points, "watts"))) {
+        points = member(points, "watts"); /* the whole estimate rather than its watts alone */
+    }
+    int lines = 0;
+    bool have = false;
+    time_t t0 = 0;
+    double w0 = 0;
+    for (const cJSON *p = cJSON_IsObject(points) ? points->child : NULL; p != NULL; p = p->next) {
+        time_t t;
+        double w;
+        if (!number(p, &w) || !timekeeping_parse_iso8601(p->string, &t)) {
+            continue;
+        }
+        if (have && t > t0) {
+            solar_acc_line(acc, t0, (float)w0, t, (float)w);
+            lines++;
+        }
+        have = true;
+        t0 = t;
+        w0 = w;
+    }
+    cJSON_Delete(root);
+    return lines > 0 || fail(err, err_size, "no data");
+}
+
+/* "PT30M", "PT15M", "PT1H": seconds, or 0. */
+static int period_s(const cJSON *item)
+{
+    if (!cJSON_IsString(item)) {
+        return 1800; /* Solcast's default */
+    }
+    int n;
+    char unit, rest;
+    if (sscanf(item->valuestring, "PT%d%c%c", &n, &unit, &rest) != 2 || n <= 0 || n > 24 * 60) {
+        return 0;
+    }
+    return unit == 'M' ? n * 60 : unit == 'H' && n <= 24 ? n * 3600 : 0;
+}
+
+bool solar_parse_solcast(const char *json, size_t len, solar_acc_t *acc, char *err, size_t err_size)
+{
+    cJSON *root = open_reply(json, len, SOLCAST_DEPTH, err, err_size);
+    if (root == NULL) {
+        return false;
+    }
+    const cJSON *status = member(root, "response_status");
+    if (cJSON_IsString(member(status, "error_code"))) {
+        refused(err, err_size, member(status, "message"));
+        cJSON_Delete(root);
+        return false;
+    }
+    const cJSON *periods = member(root, "forecasts");
+    int used = 0;
+    for (const cJSON *p = cJSON_IsArray(periods) ? periods->child : NULL; p != NULL; p = p->next) {
+        const cJSON *end = member(p, "period_end");
+        double kw;
+        time_t t;
+        int seconds = period_s(member(p, "period"));
+        if (number(member(p, "pv_estimate"), &kw) && cJSON_IsString(end) &&
+            timekeeping_parse_iso8601(end->valuestring, &t) && seconds > 0) {
+            solar_acc_period(acc, t, seconds, (float)(kw * 1000.0));
+            used++;
+        }
+    }
+    cJSON_Delete(root);
+    return used > 0 || fail(err, err_size, "no data");
+}
```


`components/solar/solar.c`:

```diff
--- a/components/solar/solar.c
+++ b/components/solar/solar.c
@@ -192,3 +192,13 @@ bool solar_peak(const solar_forecast_t *f, int32_t day, uint32_t *w, int *quarte
     *quarter = best;
     return q[best] > 0;
 }
+
+uint32_t solar_solcast_wait_s(int sites)
+{
+    return sites > 1 ? 6 * 3600 : 3 * 3600;
+}
+
+bool solar_solcast_due(uint32_t last_asked, int sites, uint32_t now)
+{
+    return last_asked == 0 || now < last_asked || now - last_asked >= solar_solcast_wait_s(sites);
+}
```


`components/solar/CMakeLists.txt`:

```diff
--- a/components/solar/CMakeLists.txt
+++ b/components/solar/CMakeLists.txt
@@ -2,4 +2,4 @@
 # quarter hours. Pure C on cJSON, also built on the host; the sync task fetches.
 idf_component_register(SRCS "solar.c" "solar_url.c" "solar_parse.c"
                        INCLUDE_DIRS "include"
-                       PRIV_REQUIRES json util)
+                       PRIV_REQUIRES json util timekeeping)
```


- [ ] **Step 5: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_solar | tail -2 && ./build-host/test_timekeeping_iso | tail -2 && ctest --test-dir build-host | tail -3`
Expected:

```
32 Tests 0 Failures 0 Ignored
OK
6 Tests 0 Failures 0 Ignored
OK
100% tests passed, 0 tests failed out of 62
```

- [ ] **Step 6: The firmware builds:** `tools/idf.sh build`, clean, without a warning.

- [ ] **Step 7: Commit.**

```bash
git add components/solar components/timekeeping test/host/CMakeLists.txt test/host/test_solar.c \
  test/host/test_timekeeping_iso.c test/host/fixtures/solar
git commit -m "feat(solar): Forecast.Solar's and Solcast's forecasts, and Solcast's budget"
```

### Task 3: SolaX Cloud's readings, the battery's rule and the day's totals (`energy`)

**Files:**
- Create: `components/energy/CMakeLists.txt`, `components/energy/include/energy.h`, `components/energy/energy.c`
- Modify: `test/host/CMakeLists.txt`
- Test: `test/host/test_energy.c`; fixtures `test/host/fixtures/energy/solax-*.json` (copied from the branch)

**Interfaces:**
- Consumes: `timekeeping_parse_iso8601()` (SolaX's `uploadTime` is local time), `util_days_from_civil()`, `util_json_depth()`, cJSON.
- Produces (Tasks 5–10):
  - `energy_reading_t { at, pv_w, grid_w, load_w, bat_w, soc, inverter, yield_wh, to_grid_wh, from_grid_wh }`: our signs, grid + from the grid and − to it, battery + charging; `soc` −1 when the reply has none;
  - `energy_day_t`: the local day, the reading taken as its midnight's (`base_*`), the latest in the hour before the next midnight (`next_*`), the last upload counted (`last_at`), each quarter hour's mean output (`q`, in `ENERGY_UNIT_W`, `ENERGY_NONE` without a reading) and its count (`n`);
  - `ENERGY_URL_MAX`, `ENERGY_KEY_MAX`, `ENERGY_STEPS`, `ENERGY_UNIT_W`, `ENERGY_NONE`, `ENERGY_WH_NONE`, `ENERGY_FRESH_S` (900), `ENERGY_MIDNIGHT_S` (3600); `energy_battery_t` (`AUTO`, `ON`, `OFF`, as `settings_energy_battery_t`);
  - `size_t energy_solax_url(out, size, token, sn)`, `bool energy_parse_solax(json, len, out, err, err_size)` (SolaX's `exception` when it says `success: false`);
  - `bool energy_battery_shown(setting, r)`, `bool energy_fresh(r, now)`, `int32_t energy_reading_day(r)`;
  - `void energy_day_init(d)`, `void energy_day_add(d, r)`, `const uint16_t *energy_day_q(d, day)`, `uint32_t energy_to_grid_wh(d, r, day)`, `uint32_t energy_from_grid_wh(d, r, day)`, `int energy_self_pct(d, r, day)`.

Numbers out of reason make no nonsense of a reading (the review's finding): the output is never below 0 (a hybrid without its strings in the reply charges from the grid with a negative `acpower`), every number is clamped before its cast, and `inverterType` is range-checked as a double first.

The reading taken as midnight's is the one nearest to it within an hour either side (spec §11.6): one before midnight waits in `next_*` until the day turns. A day without one has no totals to or from the grid; what was produced is the inverter's own count (`yieldtoday`). A later day starts afresh; an older reading is left out; the same upload counts once.

- [ ] **Step 1: Write the failing tests.** The request; the documented reply (local time, a night of zeros, an X1-Boost); a string inverter by day (its strings, before the inverter, as the panels' output; SolaX's feed-in positive while exporting, ours importing); without strings, the inverter's output; a hybrid and the battery's rule for each type; the EPS loads; values out of reason (also under UBSan); a refusal and its reason; the day's totals from the reading nearest midnight, before or after it; a day without one; the quarter hours' means; a new day; own use; freshness:

`test/host/test_energy.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/test/host/test_energy.c
@@ -0,0 +1,351 @@
+#define _POSIX_C_SOURCE 200809L /* setenv, tzset */
+
+#include <stdio.h>
+#include <stdlib.h>
+#include <string.h>
+#include <time.h>
+
+#include "energy.h"
+#include "unity.h"
+#include "util_time.h"
+
+/* The house's energy (spec §11.6, D36): SolaX Cloud's real-time reply, its signs, the battery's
+ * rule, today's totals and the quarter-hour readings. solax-documented.json is the example in SolaX's
+ * user monitoring API V4.0 (its serial numbers blanked there too); the others are built in its
+ * format, as there is no inverter to record them from: a string inverter by day, a hybrid charging
+ * its battery, and a refusal. */
+
+#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"
+
+static char s_json[4096];
+static size_t s_len;
+static char s_err[96];
+
+void setUp(void)
+{
+    setenv("TZ", TZ_PRAGUE, 1);
+    tzset();
+}
+
+void tearDown(void) {}
+
+static const char *fixture(const char *name)
+{
+    char path[256];
+    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
+    FILE *f = fopen(path, "rb");
+    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
+    s_len = fread(s_json, 1, sizeof(s_json) - 1, f);
+    fclose(f);
+    s_json[s_len] = '\0';
+    return s_json;
+}
+
+static time_t local(int y, int mo, int d, int h, int mi)
+{
+    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h, .tm_min = mi,
+                    .tm_isdst = -1 };
+    return mktime(&t);
+}
+
+static int32_t day_of(int y, int mo, int d)
+{
+    return (int32_t)util_days_from_civil(y, mo, d);
+}
+
+static energy_reading_t parsed(const char *name)
+{
+    const char *json = fixture(name);
+    energy_reading_t r;
+    TEST_ASSERT_TRUE_MESSAGE(energy_parse_solax(json, s_len, &r, s_err, sizeof(s_err)), s_err);
+    return r;
+}
+
+/* --- the request --- */
+
+static void test_the_request_names_the_token_and_the_dongle(void)
+{
+    char url[ENERGY_URL_MAX];
+    TEST_ASSERT_TRUE(energy_solax_url(url, sizeof(url), "20200722185111234567890", "ABCDEFGHIJ") > 0);
+    TEST_ASSERT_EQUAL_STRING("https://www.solaxcloud.com/proxyApp/proxy/api/getRealtimeInfo.do"
+                             "?tokenId=20200722185111234567890&sn=ABCDEFGHIJ",
+                             url);
+    TEST_ASSERT_EQUAL(0, energy_solax_url(url, sizeof(url), "2020&sn=x", "ABCDEFGHIJ")); /* nothing escapes */
+    TEST_ASSERT_EQUAL_STRING("", url);
+    TEST_ASSERT_EQUAL(0, energy_solax_url(url, sizeof(url), "", "ABCDEFGHIJ"));
+    TEST_ASSERT_EQUAL(0, energy_solax_url(url, sizeof(url), "2020", ""));
+    TEST_ASSERT_EQUAL(0, energy_solax_url(url, 40, "20200722185111234567890", "ABCDEFGHIJ"));
+}
+
+/* --- the reply --- */
+
+static void test_the_documented_reply(void)
+{
+    energy_reading_t r = parsed("solax-documented.json");
+    TEST_ASSERT_EQUAL_UINT32((uint32_t)local(2021, 3, 13, 19, 9) + 49, r.at); /* local time */
+    TEST_ASSERT_EQUAL_INT32(0, r.pv_w);
+    TEST_ASSERT_EQUAL_INT32(0, r.grid_w);
+    TEST_ASSERT_EQUAL_INT32(0, r.load_w);
+    TEST_ASSERT_EQUAL_UINT32(12600, r.yield_wh);
+    TEST_ASSERT_EQUAL_INT16(0, r.soc);
+    TEST_ASSERT_EQUAL_UINT8(4, r.inverter); /* X1-Boost/Air/Mini, a string inverter */
+}
+
+static void test_a_string_inverter_by_day(void)
+{
+    energy_reading_t r = parsed("solax-string.json");
+    TEST_ASSERT_EQUAL_INT32(3560, r.pv_w);    /* its two strings, before the inverter */
+    TEST_ASSERT_EQUAL_INT32(-2560, r.grid_w); /* SolaX's feed-in is positive while exporting; ours imports */
+    TEST_ASSERT_EQUAL_INT32(860, r.load_w);   /* what the inverter gives less what goes out */
+    TEST_ASSERT_EQUAL_INT32(0, r.bat_w);
+    TEST_ASSERT_EQUAL_UINT32(9400, r.yield_wh);
+    TEST_ASSERT_EQUAL_UINT32(1523450, r.to_grid_wh);
+    TEST_ASSERT_EQUAL_UINT32(2210170, r.from_grid_wh);
+    TEST_ASSERT_FALSE(energy_battery_shown(ENERGY_BATTERY_AUTO, &r));
+}
+
+static void test_without_its_strings_the_inverter_output_counts(void)
+{
+    const char *json = "{\"success\":true,\"result\":{\"acpower\":1500.0,\"feedinpower\":-300.0,"
+                       "\"uploadTime\":\"2026-10-05 13:17:02\"}}";
+    energy_reading_t r;
+    TEST_ASSERT_TRUE_MESSAGE(energy_parse_solax(json, strlen(json), &r, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_INT32(1500, r.pv_w);
+    TEST_ASSERT_EQUAL_INT32(300, r.grid_w); /* importing */
+    TEST_ASSERT_EQUAL_INT32(1800, r.load_w);
+    TEST_ASSERT_EQUAL_INT16(-1, r.soc); /* no charge in the reply */
+    TEST_ASSERT_EQUAL_UINT8(0, r.inverter);
+}
+
+static void test_a_hybrid_shows_its_battery(void)
+{
+    energy_reading_t r = parsed("solax-hybrid.json");
+    TEST_ASSERT_EQUAL_INT32(3420, r.pv_w);
+    TEST_ASSERT_EQUAL_INT32(-1360, r.grid_w);
+    TEST_ASSERT_EQUAL_INT32(860, r.load_w);
+    TEST_ASSERT_EQUAL_INT32(1200, r.bat_w); /* charging */
+    TEST_ASSERT_EQUAL_INT16(64, r.soc);
+    TEST_ASSERT_EQUAL_UINT8(5, r.inverter); /* X3-Hybrid */
+    TEST_ASSERT_TRUE(energy_battery_shown(ENERGY_BATTERY_AUTO, &r));
+    TEST_ASSERT_FALSE(energy_battery_shown(ENERGY_BATTERY_OFF, &r));
+}
+
+static void test_the_battery_rule(void)
+{
+    energy_reading_t r = parsed("solax-string.json");
+    TEST_ASSERT_TRUE(energy_battery_shown(ENERGY_BATTERY_ON, &r));
+    static const uint8_t k_hybrids[] = { 2, 3, 5, 10, 13 }; /* X-Hybrid, X1-, X3-, A1-Hybrid, J1-ESS */
+    for (size_t i = 0; i < sizeof(k_hybrids); i++) {
+        r.inverter = k_hybrids[i];
+        TEST_ASSERT_TRUE(energy_battery_shown(ENERGY_BATTERY_AUTO, &r));
+    }
+    r.inverter = 7;
+    TEST_ASSERT_FALSE(energy_battery_shown(ENERGY_BATTERY_AUTO, &r));
+    r.soc = 12; /* a charge above 0 % says there is one */
+    TEST_ASSERT_TRUE(energy_battery_shown(ENERGY_BATTERY_AUTO, &r));
+    TEST_ASSERT_FALSE(energy_battery_shown(ENERGY_BATTERY_AUTO, NULL));
+}
+
+static void test_eps_loads_count_in_the_house(void)
+{
+    const char *json = "{\"success\":true,\"result\":{\"acpower\":1000.0,\"feedinpower\":0.0,\"peps1\":120.0,"
+                       "\"peps2\":null,\"peps3\":30.0,\"uploadTime\":\"2026-10-05 13:17:02\"}}";
+    energy_reading_t r;
+    TEST_ASSERT_TRUE(energy_parse_solax(json, strlen(json), &r, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_INT32(1150, r.load_w);
+}
+
+/* Values out of reason make no nonsense of a reading (Review Focus): a negative AC power without DC strings (a hybrid
+ * charging from the grid) is no solar; numbers past any range clamp instead of overflowing a cast. */
+static void test_values_out_of_reason_are_clamped(void)
+{
+    const char *json = "{\"success\":true,\"result\":{\"acpower\":-2000.0,\"feedinpower\":-2500.0,"
+                       "\"inverterType\":1e300,\"soc\":1e300,\"yieldtoday\":-3,"
+                       "\"uploadTime\":\"2026-10-05 13:17:02\"}}";
+    energy_reading_t r;
+    TEST_ASSERT_TRUE_MESSAGE(energy_parse_solax(json, strlen(json), &r, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_INT32(0, r.pv_w);
+    TEST_ASSERT_EQUAL_INT32(2500, r.grid_w);
+    TEST_ASSERT_EQUAL_INT32(500, r.load_w);
+    TEST_ASSERT_EQUAL_UINT8(0, r.inverter);
+    TEST_ASSERT_EQUAL_INT16(100, r.soc);
+    TEST_ASSERT_EQUAL_UINT32(0, r.yield_wh);
+    json = "{\"success\":true,\"result\":{\"powerdc1\":1e308,\"powerdc2\":1e308,"
+           "\"uploadTime\":\"2026-10-05 13:17:02\"}}";
+    TEST_ASSERT_TRUE_MESSAGE(energy_parse_solax(json, strlen(json), &r, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_INT32(10000000, r.pv_w); /* their sum is past any number: 10 MW */
+}
+
+static void test_a_refusal_gives_its_reason(void)
+{
+    const char *json = fixture("solax-refused.json");
+    energy_reading_t r;
+    TEST_ASSERT_FALSE(energy_parse_solax(json, s_len, &r, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("tokenId is invalid", s_err);
+}
+
+static void test_refusals(void)
+{
+    static const struct {
+        const char *json, *why;
+    } k_cases[] = {
+        { "not json", "not a JSON object" },
+        { "{\"success\":false}", "refused" },
+        { "{\"success\":true,\"result\":null}", "no data" },
+        { "{\"success\":true,\"result\":{\"acpower\":1.0,\"uploadTime\":\"yesterday\"}}", "no data" },
+        { "{\"success\":true,\"result\":{\"uploadTime\":\"2026-10-05 13:17:02\"}}", "no data" },
+        { "{\"success\":true,\"result\":{\"a\":[[[1]]]}}", "nested too deeply" },
+    };
+    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
+        energy_reading_t r;
+        TEST_ASSERT_FALSE(energy_parse_solax(k_cases[i].json, strlen(k_cases[i].json), &r, s_err, sizeof(s_err)));
+        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
+    }
+}
+
+/* --- the day --- */
+
+static energy_reading_t reading(time_t at, int32_t pv_w, double to_kwh, double from_kwh, double yield_kwh)
+{
+    energy_reading_t r;
+    memset(&r, 0, sizeof(r));
+    r.at = (uint32_t)at;
+    r.pv_w = pv_w;
+    r.soc = -1;
+    r.to_grid_wh = (uint32_t)(to_kwh * 1000.0 + 0.5);
+    r.from_grid_wh = (uint32_t)(from_kwh * 1000.0 + 0.5);
+    r.yield_wh = (uint32_t)(yield_kwh * 1000.0 + 0.5);
+    return r;
+}
+
+static void test_the_reading_nearest_midnight_starts_the_days_totals(void)
+{
+    energy_day_t d;
+    energy_day_init(&d);
+    energy_reading_t r = reading(local(2026, 10, 4, 23, 50), 0, 1000.0, 2000.0, 9.0);
+    energy_day_add(&d, &r);
+    r = reading(local(2026, 10, 5, 0, 5), 0, 1000.1, 2000.2, 0.0); /* 5 min after: nearer than 23:50 */
+    energy_day_add(&d, &r);
+    r = reading(local(2026, 10, 5, 12, 0), 2500, 1005.0, 2001.0, 6.0);
+    energy_day_add(&d, &r);
+    int32_t today = day_of(2026, 10, 5);
+    TEST_ASSERT_EQUAL_INT32(today, d.day);
+    TEST_ASSERT_EQUAL_UINT32(4900, energy_to_grid_wh(&d, &r, today));
+    TEST_ASSERT_EQUAL_UINT32(800, energy_from_grid_wh(&d, &r, today));
+    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, energy_to_grid_wh(&d, &r, today + 1)); /* not today's reading */
+}
+
+static void test_a_reading_before_midnight_can_be_its_base(void)
+{
+    energy_day_t d;
+    energy_day_init(&d);
+    energy_reading_t r = reading(local(2026, 10, 4, 23, 58), 0, 1000.0, 2000.0, 9.0);
+    energy_day_add(&d, &r);
+    r = reading(local(2026, 10, 5, 0, 40), 0, 1000.5, 2000.5, 0.0);
+    energy_day_add(&d, &r);
+    r = reading(local(2026, 10, 5, 12, 0), 2500, 1005.0, 2001.0, 6.0);
+    energy_day_add(&d, &r);
+    TEST_ASSERT_EQUAL_UINT32(5000, energy_to_grid_wh(&d, &r, day_of(2026, 10, 5)));
+}
+
+static void test_a_day_without_a_reading_near_midnight_has_no_totals(void)
+{
+    energy_day_t d;
+    energy_day_init(&d);
+    energy_reading_t r = reading(local(2026, 10, 4, 22, 30), 0, 1000.0, 2000.0, 9.0); /* 90 min before */
+    energy_day_add(&d, &r);
+    r = reading(local(2026, 10, 5, 5, 30), 0, 1000.0, 2001.0, 0.0); /* the morning's sync */
+    energy_day_add(&d, &r);
+    int32_t today = day_of(2026, 10, 5);
+    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, energy_to_grid_wh(&d, &r, today));
+    TEST_ASSERT_EQUAL_UINT32(ENERGY_WH_NONE, energy_from_grid_wh(&d, &r, today));
+    TEST_ASSERT_EQUAL_INT(-1, energy_self_pct(&d, &r, today));
+}
+
+static void test_quarter_hours_average_their_readings(void)
+{
+    energy_day_t d;
+    energy_day_init(&d);
+    static const struct {
+        int mi;
+        int32_t w;
+    } k_readings[] = { { 1, 2000 }, { 6, 3000 }, { 11, 4004 }, { 16, 1000 } };
+    energy_reading_t r;
+    for (size_t i = 0; i < sizeof(k_readings) / sizeof(k_readings[0]); i++) {
+        r = reading(local(2026, 10, 5, 12, k_readings[i].mi), k_readings[i].w, 1000.0, 2000.0, 5.0);
+        energy_day_add(&d, &r);
+    }
+    energy_day_add(&d, &r); /* the same upload again counts once */
+    const uint16_t *q = energy_day_q(&d, day_of(2026, 10, 5));
+    TEST_ASSERT_NOT_NULL(q);
+    TEST_ASSERT_EQUAL_UINT16(300, q[48]); /* 12:00-12:15: 3.0 kW on average, in tens of W */
+    TEST_ASSERT_EQUAL_UINT16(100, q[49]);
+    TEST_ASSERT_EQUAL_UINT16(ENERGY_NONE, q[47]);
+    TEST_ASSERT_EQUAL_UINT16(ENERGY_NONE, q[50]);
+    TEST_ASSERT_NULL(energy_day_q(&d, day_of(2026, 10, 6)));
+}
+
+static void test_a_new_day_starts_afresh(void)
+{
+    energy_day_t d;
+    energy_day_init(&d);
+    energy_reading_t r = reading(local(2026, 10, 4, 12, 1), 2000, 1000.0, 2000.0, 5.0);
+    energy_day_add(&d, &r);
+    r = reading(local(2026, 10, 5, 9, 1), 1000, 1000.0, 2000.0, 1.0);
+    energy_day_add(&d, &r);
+    const uint16_t *q = energy_day_q(&d, day_of(2026, 10, 5));
+    TEST_ASSERT_EQUAL_UINT16(ENERGY_NONE, q[48]);
+    TEST_ASSERT_EQUAL_UINT16(100, q[36]);
+    r = reading(local(2026, 10, 4, 18, 0), 3000, 1000.0, 2000.0, 7.0); /* an older reading is left out */
+    energy_day_add(&d, &r);
+    TEST_ASSERT_EQUAL_UINT16(ENERGY_NONE, q[72]);
+}
+
+static void test_own_use_is_the_share_the_house_kept(void)
+{
+    energy_day_t d;
+    energy_day_init(&d);
+    energy_reading_t r = reading(local(2026, 10, 5, 0, 5), 0, 1000.0, 2000.0, 0.0);
+    energy_day_add(&d, &r);
+    r = reading(local(2026, 10, 5, 13, 17), 3000, 1004.9, 2000.6, 9.4);
+    energy_day_add(&d, &r);
+    int32_t today = day_of(2026, 10, 5);
+    TEST_ASSERT_EQUAL_INT(48, energy_self_pct(&d, &r, today)); /* (9.4 - 4.9) / 9.4 */
+    r.yield_wh = 0; /* nothing produced yet */
+    TEST_ASSERT_EQUAL_INT(-1, energy_self_pct(&d, &r, today));
+    r.yield_wh = 4000; /* more went out than the inverter counted: none kept */
+    TEST_ASSERT_EQUAL_INT(0, energy_self_pct(&d, &r, today));
+}
+
+static void test_a_reading_is_fresh_for_15_minutes(void)
+{
+    energy_reading_t r = reading(local(2026, 10, 5, 13, 0), 0, 0, 0, 0);
+    TEST_ASSERT_TRUE(energy_fresh(&r, r.at + ENERGY_FRESH_S));
+    TEST_ASSERT_FALSE(energy_fresh(&r, r.at + ENERGY_FRESH_S + 1));
+    TEST_ASSERT_TRUE(energy_fresh(&r, r.at - 120)); /* the inverter's clock a little ahead */
+    r.at = 0;
+    TEST_ASSERT_FALSE(energy_fresh(&r, 1791195300));
+}
+
+int main(void)
+{
+    UNITY_BEGIN();
+    RUN_TEST(test_the_request_names_the_token_and_the_dongle);
+    RUN_TEST(test_the_documented_reply);
+    RUN_TEST(test_a_string_inverter_by_day);
+    RUN_TEST(test_without_its_strings_the_inverter_output_counts);
+    RUN_TEST(test_a_hybrid_shows_its_battery);
+    RUN_TEST(test_the_battery_rule);
+    RUN_TEST(test_eps_loads_count_in_the_house);
+    RUN_TEST(test_values_out_of_reason_are_clamped);
+    RUN_TEST(test_a_refusal_gives_its_reason);
+    RUN_TEST(test_refusals);
+    RUN_TEST(test_the_reading_nearest_midnight_starts_the_days_totals);
+    RUN_TEST(test_a_reading_before_midnight_can_be_its_base);
+    RUN_TEST(test_a_day_without_a_reading_near_midnight_has_no_totals);
+    RUN_TEST(test_quarter_hours_average_their_readings);
+    RUN_TEST(test_a_new_day_starts_afresh);
+    RUN_TEST(test_own_use_is_the_share_the_house_kept);
+    RUN_TEST(test_a_reading_is_fresh_for_15_minutes);
+    return UNITY_END();
+}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -188,6 +188,12 @@ target_include_directories(solar_logic PUBLIC ${REPO_ROOT}/components/solar/incl
 target_compile_options(solar_logic PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(solar_logic PUBLIC util timekeeping_logic PRIVATE cjson m)
 
+# energy: SolaX Cloud's reply, the signs, the battery's rule and the day's totals (spec §11.6); pure C.
+add_library(energy_logic STATIC ${REPO_ROOT}/components/energy/energy.c)
+target_include_directories(energy_logic PUBLIC ${REPO_ROOT}/components/energy/include)
+target_compile_options(energy_logic PRIVATE ${REFLBO_WARNINGS})
+target_link_libraries(energy_logic PUBLIC util timekeeping_logic PRIVATE cjson m)
+
 # ui: pure C on top of gfx, locale, the datastore and the scheduler's local-time rules.
 file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
 add_library(ui STATIC ${UI_SOURCES})
@@ -256,6 +262,8 @@ reflbo_host_test(test_sync_ntp sync_logic)
 target_compile_definitions(test_weather PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/open-meteo")
 reflbo_host_test(test_solar solar_logic)
 target_compile_definitions(test_solar PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/solar")
+reflbo_host_test(test_energy energy_logic)
+target_compile_definitions(test_energy PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/energy")
 reflbo_host_test(test_ui_fields ui)
 reflbo_host_test(test_ui_preset ui)
 reflbo_host_test(test_ui_split ui)
```


```bash
git checkout plan/m6d -- \
  test/host/fixtures/energy/solax-documented.json \
  test/host/fixtures/energy/solax-hybrid.json \
  test/host/fixtures/energy/solax-refused.json \
  test/host/fixtures/energy/solax-string.json
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake -S test/host -B build-host -G Ninja 2>&1 | grep -m1 -A1 'CMake Error'`
Expected:

```
CMake Error at CMakeLists.txt:192 (add_library):
  Cannot find source file:
```

- [ ] **Step 3: The component.**

`components/energy/include/energy.h`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/energy/include/energy.h
@@ -0,0 +1,73 @@
+#pragma once
+
+#include <stdbool.h>
+#include <stddef.h>
+#include <stdint.h>
+
+/*
+ * The house's energy (spec §11.6, D36): SolaX Cloud's real-time request and reply, our signs, the
+ * battery's rule, and today's totals and quarter hours from the readings. Local days and quarter
+ * hours follow the TZ variable. Pure C on cJSON, host-buildable; the sync task fetches.
+ */
+
+#define ENERGY_URL_MAX 256
+#define ENERGY_KEY_MAX 64          /* the token or the registration number, NUL included */
+#define ENERGY_STEPS 96            /* local quarter hours a day */
+#define ENERGY_UNIT_W 10           /* a quarter hour's mean output is kept in tens of W */
+#define ENERGY_NONE UINT16_MAX     /* a quarter hour without a reading */
+#define ENERGY_WH_NONE UINT32_MAX  /* a total there is no reading for */
+#define ENERGY_FRESH_S (15 * 60)   /* a reading is fresh for 15 min (spec §5.1) */
+#define ENERGY_MIDNIGHT_S 3600     /* the reading taken as midnight's lies within an hour of it */
+
+/* energy.battery; the values match settings_energy_battery_t. */
+typedef enum { ENERGY_BATTERY_AUTO, ENERGY_BATTERY_ON, ENERGY_BATTERY_OFF } energy_battery_t;
+
+/* One reading, in our signs (W unless named). */
+typedef struct {
+    uint32_t at;          /* the inverter's upload, UTC; 0 = none */
+    int32_t pv_w;         /* the panels: its strings added up, else the inverter's output */
+    int32_t grid_w;       /* + from the grid, - to it */
+    int32_t load_w;       /* the house */
+    int32_t bat_w;        /* + charging, - discharging */
+    int16_t soc;          /* the battery's charge, %; -1 when the reply has none */
+    uint8_t inverter;     /* SolaX's inverter type, 0 when unknown */
+    uint32_t yield_wh;    /* produced today, as the inverter counts it */
+    uint32_t to_grid_wh;  /* the totals since installation */
+    uint32_t from_grid_wh;
+} energy_reading_t;
+
+/* A local day from the readings: its midnight totals and each quarter hour's mean output. */
+typedef struct {
+    int32_t day;                       /* days since 1970-01-01; 0 = none yet */
+    uint32_t base_at;                  /* the reading taken as its midnight's, UTC; 0 = none */
+    uint32_t base_to_wh, base_from_wh; /* its totals */
+    uint32_t next_at;                  /* the latest reading in the hour before the next midnight */
+    uint32_t next_to_wh, next_from_wh;
+    uint32_t last_at;                  /* the last upload counted: one is counted once */
+    uint16_t q[ENERGY_STEPS];          /* mean output in ENERGY_UNIT_W, or ENERGY_NONE */
+    uint8_t n[ENERGY_STEPS];           /* the readings in each mean */
+} energy_day_t;
+
+/* SolaX Cloud's real-time request. A token or registration number of other characters than letters
+ * and digits makes no URL (0 and ""), as both go into the query as they are. */
+size_t energy_solax_url(char *out, size_t size, const char *token, const char *sn);
+/* Its reply; false with the reason in `err`: SolaX's `exception` when it says `success: false`. */
+bool energy_parse_solax(const char *json, size_t len, energy_reading_t *out, char *err, size_t err_size);
+/* The battery shows: `on`; with `auto`, a hybrid inverter or a charge above 0 % (spec §11.6). */
+bool energy_battery_shown(energy_battery_t setting, const energy_reading_t *r);
+/* At most ENERGY_FRESH_S old at `now` (UTC). */
+bool energy_fresh(const energy_reading_t *r, uint32_t now);
+
+void energy_day_init(energy_day_t *d);
+/* A reading into its local day: a later day starts afresh, an earlier one is left out. */
+void energy_day_add(energy_day_t *d, const energy_reading_t *r);
+/* Local day `day`'s quarter hours, or NULL when the readings are of another day. */
+const uint16_t *energy_day_q(const energy_day_t *d, int32_t day);
+/* Today's totals to and from the grid at reading `r`, since day `day`'s midnight; ENERGY_WH_NONE
+ * without a reading near that midnight, or when `r` is of another day. */
+uint32_t energy_to_grid_wh(const energy_day_t *d, const energy_reading_t *r, int32_t day);
+uint32_t energy_from_grid_wh(const energy_day_t *d, const energy_reading_t *r, int32_t day);
+/* Own use: the share of today's production the house kept, %; -1 without a production or an export. */
+int energy_self_pct(const energy_day_t *d, const energy_reading_t *r, int32_t day);
+/* The local day of reading `r`, days since 1970-01-01; 0 for none. */
+int32_t energy_reading_day(const energy_reading_t *r);
```


`components/energy/energy.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/energy/energy.c
@@ -0,0 +1,275 @@
+#define _POSIX_C_SOURCE 200809L /* localtime_r */
+
+#include "energy.h"
+
+#include <ctype.h>
+#include <math.h>
+#include <stdio.h>
+#include <stdlib.h>
+#include <string.h>
+#include <time.h>
+
+#include "cJSON.h"
+#include "timekeeping_iso.h"
+#include "util_json.h"
+#include "util_time.h"
+
+/* SolaX Cloud's real-time reply (spec §11.6) and the day it builds. */
+
+#define DEPTH_MAX 3 /* root, result, a value */
+
+static bool fail(char *err, size_t err_size, const char *why)
+{
+    snprintf(err, err_size, "%s", why);
+    return false;
+}
+
+static bool plain(const char *s)
+{
+    for (; *s != '\0'; s++) {
+        if (!isalnum((unsigned char)*s)) {
+            return false;
+        }
+    }
+    return true;
+}
+
+size_t energy_solax_url(char *out, size_t size, const char *token, const char *sn)
+{
+    int n = token[0] != '\0' && sn[0] != '\0' && plain(token) && plain(sn)
+                ? snprintf(out, size,
+                           "https://www.solaxcloud.com/proxyApp/proxy/api/getRealtimeInfo.do?tokenId=%s&sn=%s", token,
+                           sn)
+                : -1;
+    if (n > 0 && (size_t)n < size) {
+        return (size_t)n;
+    }
+    if (size > 0) {
+        out[0] = '\0';
+    }
+    return 0;
+}
+
+static bool number(const cJSON *o, const char *key, double *out)
+{
+    const cJSON *item = cJSON_GetObjectItemCaseSensitive(o, key);
+    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
+        return false; /* also null */
+    }
+    *out = item->valuedouble;
+    return true;
+}
+
+static int32_t watts(double v)
+{
+    return (int32_t)lround(v < -1e7 ? -1e7 : v > 1e7 ? 1e7 : v);
+}
+
+/* kWh to Wh, never below 0 */
+static uint32_t wh(const cJSON *o, const char *key)
+{
+    double v = 0;
+    number(o, key, &v);
+    return v <= 0 ? 0 : v >= 4e6 ? 4000000000u : (uint32_t)lround(v * 1000.0);
+}
+
+/* "4" or 4 */
+static uint8_t inverter_type(const cJSON *item)
+{
+    bool digits = cJSON_IsString(item) && isdigit((unsigned char)item->valuestring[0]);
+    double v = cJSON_IsNumber(item) ? item->valuedouble : digits ? strtod(item->valuestring, NULL) : 0;
+    return v >= 1 && v < 256 ? (uint8_t)v : 0; /* checked before the cast */
+}
+
+bool energy_parse_solax(const char *json, size_t len, energy_reading_t *out, char *err, size_t err_size)
+{
+    if (util_json_depth(json) > DEPTH_MAX) {
+        return fail(err, err_size, "nested too deeply");
+    }
+    cJSON *root = cJSON_ParseWithLength(json, len);
+    if (!cJSON_IsObject(root)) {
+        cJSON_Delete(root);
+        return fail(err, err_size, "not a JSON object");
+    }
+    const cJSON *exception = cJSON_GetObjectItemCaseSensitive(root, "exception");
+    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "success"))) {
+        bool said = cJSON_IsString(exception) && exception->valuestring[0] != '\0';
+        snprintf(err, err_size, "%s", said ? exception->valuestring : "refused");
+        cJSON_Delete(root);
+        return false;
+    }
+    const cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "result");
+    const cJSON *upload = cJSON_GetObjectItemCaseSensitive(r, "uploadTime");
+    time_t at;
+    double ac = 0, feed_in = 0, v;
+    bool have_ac = number(r, "acpower", &ac);
+    bool have_dc = false;
+    double dc = 0;
+    static const char *const k_dc[] = { "powerdc1", "powerdc2", "powerdc3", "powerdc4" };
+    for (size_t i = 0; i < sizeof(k_dc) / sizeof(k_dc[0]); i++) {
+        if (number(r, k_dc[i], &v)) {
+            dc += v;
+            have_dc = true;
+        }
+    }
+    if (!cJSON_IsObject(r) || !cJSON_IsString(upload) || !timekeeping_parse_iso8601(upload->valuestring, &at) ||
+        at <= 0 || (!have_ac && !have_dc)) {
+        cJSON_Delete(root);
+        return fail(err, err_size, "no data");
+    }
+    memset(out, 0, sizeof(*out));
+    out->at = (uint32_t)at;
+    number(r, "feedinpower", &feed_in); /* SolaX: positive while exporting */
+    double pv = have_dc ? dc : ac;
+    out->pv_w = watts(pv < 0 ? 0 : pv); /* a hybrid without its strings in the reply, charging from the grid */
+    out->grid_w = watts(-feed_in);
+    double load = ac - feed_in;
+    static const char *const k_eps[] = { "peps1", "peps2", "peps3" };
+    for (size_t i = 0; i < sizeof(k_eps) / sizeof(k_eps[0]); i++) {
+        load += number(r, k_eps[i], &v) ? v : 0;
+    }
+    out->load_w = watts(load < 0 ? 0 : load);
+    out->bat_w = number(r, "batPower", &v) ? watts(v) : 0;
+    out->soc = number(r, "soc", &v) ? (int16_t)lround(v < 0 ? 0 : v > 100 ? 100 : v) : -1;
+    out->inverter = inverter_type(cJSON_GetObjectItemCaseSensitive(r, "inverterType"));
+    out->yield_wh = wh(r, "yieldtoday");
+    out->to_grid_wh = wh(r, "feedinenergy");
+    out->from_grid_wh = wh(r, "consumeenergy");
+    cJSON_Delete(root);
+    return true;
+}
+
+bool energy_battery_shown(energy_battery_t setting, const energy_reading_t *r)
+{
+    if (setting != ENERGY_BATTERY_AUTO) {
+        return setting == ENERGY_BATTERY_ON;
+    }
+    if (r == NULL || r->at == 0) {
+        return false;
+    }
+    static const uint8_t k_hybrids[] = { 2, 3, 5, 10, 13 }; /* X-Hybrid, X1-, X3-, A1-Hybrid, J1-ESS */
+    for (size_t i = 0; i < sizeof(k_hybrids); i++) {
+        if (r->inverter == k_hybrids[i]) {
+            return true;
+        }
+    }
+    return r->soc > 0;
+}
+
+bool energy_fresh(const energy_reading_t *r, uint32_t now)
+{
+    return r != NULL && r->at != 0 && (now <= r->at || now - r->at <= ENERGY_FRESH_S);
+}
+
+/* The local day of `t` and the UTC instants of its midnight and the next. */
+static int32_t day_bounds(time_t t, struct tm *lt, time_t *midnight, time_t *next)
+{
+    localtime_r(&t, lt);
+    struct tm m = { .tm_year = lt->tm_year, .tm_mon = lt->tm_mon, .tm_mday = lt->tm_mday, .tm_isdst = -1 };
+    *midnight = mktime(&m);
+    struct tm n = { .tm_year = lt->tm_year, .tm_mon = lt->tm_mon, .tm_mday = lt->tm_mday + 1, .tm_isdst = -1 };
+    *next = mktime(&n);
+    return (int32_t)util_days_from_civil(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
+}
+
+int32_t energy_reading_day(const energy_reading_t *r)
+{
+    if (r == NULL || r->at == 0) {
+        return 0;
+    }
+    struct tm lt;
+    time_t midnight, next;
+    return day_bounds((time_t)r->at, &lt, &midnight, &next);
+}
+
+void energy_day_init(energy_day_t *d)
+{
+    memset(d, 0, sizeof(*d));
+    for (int i = 0; i < ENERGY_STEPS; i++) {
+        d->q[i] = ENERGY_NONE;
+    }
+}
+
+void energy_day_add(energy_day_t *d, const energy_reading_t *r)
+{
+    if (r->at == 0 || r->at == d->last_at) {
+        return;
+    }
+    struct tm lt;
+    time_t t = (time_t)r->at, midnight, next;
+    int32_t day = day_bounds(t, &lt, &midnight, &next);
+    if (day < d->day) {
+        return;
+    }
+    if (day > d->day) { /* a new day: the reading in the hour before its midnight, if one came, is its base */
+        energy_day_t before = *d;
+        energy_day_init(d);
+        d->day = day;
+        if (before.next_at != 0 && before.next_at < midnight && midnight - before.next_at <= ENERGY_MIDNIGHT_S) {
+            d->base_at = before.next_at;
+            d->base_to_wh = before.next_to_wh;
+            d->base_from_wh = before.next_from_wh;
+        }
+    }
+    d->last_at = r->at;
+    uint32_t base_away = d->base_at == 0                     ? UINT32_MAX
+                         : d->base_at < (uint32_t)midnight ? (uint32_t)midnight - d->base_at
+                                                             : d->base_at - (uint32_t)midnight;
+    if (t - midnight <= ENERGY_MIDNIGHT_S && (uint32_t)(t - midnight) < base_away) {
+        d->base_at = r->at;
+        d->base_to_wh = r->to_grid_wh;
+        d->base_from_wh = r->from_grid_wh;
+    }
+    if (next - t <= ENERGY_MIDNIGHT_S) {
+        d->next_at = r->at;
+        d->next_to_wh = r->to_grid_wh;
+        d->next_from_wh = r->from_grid_wh;
+    }
+    int i = lt.tm_hour * 4 + lt.tm_min / 15;
+    long w = lround((double)(r->pv_w < 0 ? 0 : r->pv_w) / ENERGY_UNIT_W);
+    w = w >= ENERGY_NONE ? ENERGY_NONE - 1 : w;
+    if (d->n[i] == 0) {
+        d->q[i] = (uint16_t)w;
+        d->n[i] = 1;
+    } else if (d->n[i] < UINT8_MAX) {
+        d->n[i]++;
+        d->q[i] = (uint16_t)(((long)d->q[i] * (d->n[i] - 1) + w + d->n[i] / 2) / d->n[i]);
+    }
+}
+
+const uint16_t *energy_day_q(const energy_day_t *d, int32_t day)
+{
+    return d != NULL && d->day != 0 && d->day == day ? d->q : NULL;
+}
+
+/* `total` less its value at day `day`'s midnight. */
+static uint32_t since_midnight(const energy_day_t *d, const energy_reading_t *r, int32_t day, uint32_t total,
+                               uint32_t base)
+{
+    if (d == NULL || r == NULL || d->day != day || d->base_at == 0 || energy_reading_day(r) != day) {
+        return ENERGY_WH_NONE;
+    }
+    return total >= base ? total - base : 0;
+}
+
+uint32_t energy_to_grid_wh(const energy_day_t *d, const energy_reading_t *r, int32_t day)
+{
+    return since_midnight(d, r, day, r != NULL ? r->to_grid_wh : 0, d != NULL ? d->base_to_wh : 0);
+}
+
+uint32_t energy_from_grid_wh(const energy_day_t *d, const energy_reading_t *r, int32_t day)
+{
+    return since_midnight(d, r, day, r != NULL ? r->from_grid_wh : 0, d != NULL ? d->base_from_wh : 0);
+}
+
+int energy_self_pct(const energy_day_t *d, const energy_reading_t *r, int32_t day)
+{
+    uint32_t out = energy_to_grid_wh(d, r, day);
+    if (out == ENERGY_WH_NONE || r->yield_wh == 0) {
+        return -1;
+    }
+    if (out >= r->yield_wh) {
+        return 0;
+    }
+    return (int)lround(100.0 * (double)(r->yield_wh - out) / (double)r->yield_wh);
+}
```


`components/energy/CMakeLists.txt`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/energy/CMakeLists.txt
@@ -0,0 +1,5 @@
+# The house's energy (spec §11.6, D36): SolaX Cloud's request and reply, our signs, the battery's rule,
+# and today's totals and quarter hours. Pure C on cJSON, also built on the host; the sync task fetches.
+idf_component_register(SRCS "energy.c"
+                       INCLUDE_DIRS "include"
+                       PRIV_REQUIRES json util timekeeping)
```


- [ ] **Step 4: Run the tests.**

Run: `cmake -S test/host -B build-host -G Ninja >/dev/null && cmake --build build-host && ./build-host/test_energy | tail -2 && ctest --test-dir build-host | tail -3`
Expected:

```
17 Tests 0 Failures 0 Ignored
OK
100% tests passed, 0 tests failed out of 63
```

- [ ] **Step 5: The firmware builds:** `tools/idf.sh reconfigure && tools/idf.sh build`, clean, without a warning.

- [ ] **Step 6: Commit.**

```bash
git add components/energy test/host/CMakeLists.txt test/host/test_energy.c test/host/fixtures/energy
git commit -m "feat(energy): SolaX Cloud's readings, the battery's rule and the day's totals"
```

### Task 4: The steps' switches, the solar and energy settings, and their keys (`storage`)

**Files:**
- Modify: `components/storage/include/settings.h`, `components/storage/settings.c`, `main/app_ui.c`, `main/app_web.c`
- Test: `test/host/test_settings.c`

**Interfaces:**
- Consumes: cJSON, `util_json_depth()`.
- Produces (Tasks 8–11):
  - in `settings_t`: `sync_steps` (`settings_step_t` bits `SETTINGS_STEP_WEATHER`, `_AIR`, `_RADAR`, `_SOLAR`, `_ENERGY`; `SETTINGS_STEPS_ALL`), `solar_source` (`settings_solar_source_t`: `OFF`, `OPEN_METEO`, `FORECAST_SOLAR`, `SOLCAST`, as `solar_source_t`), `solar_plane_count` and `solar_planes[2]` (`settings_plane_t { kwp_e2; tilt; azimuth; }`), `solar_losses_pct`, `solar_inverter_kw_e2`, `energy_source` (`OFF`, `SOLAX`), `energy_battery` (`AUTO`, `ON`, `OFF`);
  - `void settings_solar_defaults(settings_t *out)`; `const char *settings_step_name(settings_step_t)`; `bool settings_check_solar(s, fs_key_set, err, size)`;
  - `SETTINGS_FILE_MAX` (4096);
  - the keys: `settings_secret_t` (`FS_KEY`, `SOLCAST_KEY`, `SOLCAST_SITE1`, `SOLCAST_SITE2`, `SOLAX_TOKEN`, `SOLAX_SN`), `settings_secrets_t { bool given[]; char value[][SETTINGS_SECRET_LEN]; }`, `const char *settings_secret_key(which)` (the NVS key), `size_t settings_take_secrets(patch, out, size, secrets, err, err_size)`, `bool settings_secrets_solcast(secrets)` (a Solcast key or site is set or cleared: its budget starts over, Task 9).

`sync.steps` is a list of names; a missing list, or one that isn't a list, is every step, and an unknown name is left out. The keys a PATCH may carry are taken out before the patch reaches the file: `solar.fs_key`, `solar.solcast_key`, `solar.solcast_sites` (a list of up to two), `energy.solax_token`, `energy.solax_sn`; `null` or `""` clears one, and each must be what its request can carry as it is (the URL builders refuse anything else): letters and digits, `-` and `_` too in Solcast's key, `-` in its site ids, 63 characters at most. Every copy of a key is taken out, in every `solar` and `energy` object, the last counting as in the merge, and the `keys` flags a GET adds are dropped, so a page that sends them back stores nothing of them.

- [ ] **Step 1: Write the failing tests.** The defaults and a file from before M6d; the steps; the solar and energy settings, clamped and round-tripped; the keys never reaching the file, not even named twice or in a second object, cleared with `null` or nothing, refused when a request couldn't carry them; which keys start Solcast's budget over; a second Forecast.Solar plane without a key; the largest `settings.json` in half of the app's 4 KB, leaving room for M7's `mqtt.*`:

`test/host/test_settings.c`:

```diff
--- a/test/host/test_settings.c
+++ b/test/host/test_settings.c
@@ -6,7 +6,7 @@
 
 static settings_t s_defaults, s_out;
 static char s_err[96];
-static char s_json[2048];
+static char s_json[SETTINGS_FILE_MAX];
 
 void setUp(void)
 {
@@ -18,6 +18,7 @@ void setUp(void)
                                .sync_interval_min = 60, .quiet = false, .quiet_from = 1380, .quiet_to = 360,
                                .ntp = { "cz.pool.ntp.org", "pool.ntp.org" } };
     settings_radar_defaults(&s_defaults);
+    settings_solar_defaults(&s_defaults);
     memset(&s_out, 0xAA, sizeof(s_out));
     s_err[0] = '\0';
 }
@@ -489,6 +490,246 @@ static void test_settings_that_replace_others_remember_the_mode_left(void)
     TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, next.sync_mode_before_always);
 }
 
+static void test_the_solar_defaults_are_the_specs(void)
+{
+    settings_t defaults;
+    memset(&defaults, 0, sizeof(defaults));
+    settings_solar_defaults(&defaults); /* spec §14.3 */
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_STEPS_ALL, defaults.sync_steps);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SOLAR_OFF, defaults.solar_source);
+    TEST_ASSERT_EQUAL_UINT8(1, defaults.solar_plane_count);
+    TEST_ASSERT_EQUAL_UINT16(500, defaults.solar_planes[0].kwp_e2); /* 5 kWp, 35°, south */
+    TEST_ASSERT_EQUAL_UINT8(35, defaults.solar_planes[0].tilt);
+    TEST_ASSERT_EQUAL_INT16(0, defaults.solar_planes[0].azimuth);
+    TEST_ASSERT_EQUAL_UINT8(14, defaults.solar_losses_pct);
+    TEST_ASSERT_EQUAL_UINT16(0, defaults.solar_inverter_kw_e2);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_ENERGY_OFF, defaults.energy_source);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BATTERY_AUTO, defaults.energy_battery);
+    /* A file from before M6d runs every step, with no solar source and no inverter */
+    const char *json = "{\"schema\":1,\"sync\":{\"mode\":\"interval\"}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_STEPS_ALL, s_out.sync_steps);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SOLAR_OFF, s_out.solar_source);
+}
+
+static void test_the_sync_steps_parse_and_round_trip(void)
+{
+    const char *json = "{\"schema\":1,\"sync\":{\"steps\":[\"weather\",\"radar\",\"energy\",\"time\",\"tides\"]}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_STEP_WEATHER | SETTINGS_STEP_RADAR | SETTINGS_STEP_ENERGY, s_out.sync_steps);
+    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"steps\":\t[\"weather\", \"radar\", \"energy\"]"));
+    settings_t again;
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(s_out.sync_steps, again.sync_steps);
+    json = "{\"schema\":1,\"sync\":{\"steps\":[]}}"; /* every data step off: the time still runs */
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(0, s_out.sync_steps);
+    json = "{\"schema\":1,\"sync\":{\"steps\":\"all\"}}"; /* not a list: all of them */
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_STEPS_ALL, s_out.sync_steps);
+    TEST_ASSERT_EQUAL_STRING("solar", settings_step_name(SETTINGS_STEP_SOLAR));
+}
+
+static void test_the_solar_settings_parse_clamp_and_round_trip(void)
+{
+    const char *json = "{\"schema\":1,\"solar\":{\"source\":\"forecast-solar\",\"planes\":["
+                       "{\"kwp\":5.2,\"tilt\":35,\"azimuth\":0},{\"kwp\":2.45,\"tilt\":20,\"azimuth\":-90}],"
+                       "\"losses_pct\":10,\"inverter_kw\":4.6},"
+                       "\"energy\":{\"source\":\"solax\",\"battery\":\"on\"}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SOLAR_FORECAST_SOLAR, s_out.solar_source);
+    TEST_ASSERT_EQUAL_UINT8(2, s_out.solar_plane_count);
+    TEST_ASSERT_EQUAL_UINT16(520, s_out.solar_planes[0].kwp_e2);
+    TEST_ASSERT_EQUAL_UINT16(245, s_out.solar_planes[1].kwp_e2);
+    TEST_ASSERT_EQUAL_UINT8(20, s_out.solar_planes[1].tilt);
+    TEST_ASSERT_EQUAL_INT16(-90, s_out.solar_planes[1].azimuth);
+    TEST_ASSERT_EQUAL_UINT8(10, s_out.solar_losses_pct);
+    TEST_ASSERT_EQUAL_UINT16(460, s_out.solar_inverter_kw_e2);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_ENERGY_SOLAX, s_out.energy_source);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BATTERY_ON, s_out.energy_battery);
+
+    settings_t again;
+    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(&s_out, &again, sizeof(again));
+
+    json = "{\"schema\":1,\"solar\":{\"source\":\"sun\",\"planes\":[{\"kwp\":0.01,\"tilt\":95,\"azimuth\":200},"
+           "{\"kwp\":150,\"tilt\":-5,\"azimuth\":-200},{\"kwp\":3}],\"losses_pct\":80,\"inverter_kw\":150},"
+           "\"energy\":{\"source\":\"fronius\",\"battery\":\"maybe\"}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SOLAR_OFF, s_out.solar_source); /* unknown: the default */
+    TEST_ASSERT_EQUAL_UINT8(2, s_out.solar_plane_count);             /* two at most */
+    TEST_ASSERT_EQUAL_UINT16(10, s_out.solar_planes[0].kwp_e2);
+    TEST_ASSERT_EQUAL_UINT8(90, s_out.solar_planes[0].tilt);
+    TEST_ASSERT_EQUAL_INT16(180, s_out.solar_planes[0].azimuth);
+    TEST_ASSERT_EQUAL_UINT16(10000, s_out.solar_planes[1].kwp_e2);
+    TEST_ASSERT_EQUAL_UINT8(0, s_out.solar_planes[1].tilt);
+    TEST_ASSERT_EQUAL_INT16(-180, s_out.solar_planes[1].azimuth);
+    TEST_ASSERT_EQUAL_UINT8(50, s_out.solar_losses_pct);
+    TEST_ASSERT_EQUAL_UINT16(10000, s_out.solar_inverter_kw_e2);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_ENERGY_OFF, s_out.energy_source);
+    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BATTERY_AUTO, s_out.energy_battery);
+    json = "{\"schema\":1,\"solar\":{\"planes\":[{\"tilt\":10}]}}"; /* a plane's missing values: the default's */
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(1, s_out.solar_plane_count);
+    TEST_ASSERT_EQUAL_UINT16(500, s_out.solar_planes[0].kwp_e2);
+    TEST_ASSERT_EQUAL_UINT8(10, s_out.solar_planes[0].tilt);
+    json = "{\"schema\":1,\"solar\":{\"planes\":[]}}"; /* no plane is the default one */
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(1, s_out.solar_plane_count);
+}
+
+static void test_secrets_never_reach_the_file(void)
+{
+    const char *patch = "{\"solar\":{\"source\":\"solcast\",\"solcast_key\":\"Kk_1-2\","
+                        "\"solcast_sites\":[\"ab12-cd34\",\"ef56\"],\"fs_key\":\"AbC123\"},"
+                        "\"energy\":{\"solax_token\":\"20200722\",\"solax_sn\":\"SXA1B2C3D4\",\"battery\":\"off\"}}";
+    settings_secrets_t secrets;
+    char clean[512];
+    TEST_ASSERT_TRUE_MESSAGE(settings_take_secrets(patch, clean, sizeof(clean), &secrets, s_err, sizeof(s_err)) > 0,
+                             s_err);
+    TEST_ASSERT_NULL(strstr(clean, "solcast_key"));
+    TEST_ASSERT_NULL(strstr(clean, "solax"));
+    TEST_ASSERT_NULL(strstr(clean, "fs_key"));
+    TEST_ASSERT_NULL(strstr(clean, "Kk_1"));
+    TEST_ASSERT_NOT_NULL(strstr(clean, "\"source\":\"solcast\""));
+    TEST_ASSERT_NOT_NULL(strstr(clean, "\"battery\":\"off\""));
+    for (int i = 0; i < SETTINGS_SECRET_COUNT; i++) {
+        TEST_ASSERT_TRUE(secrets.given[i]);
+    }
+    TEST_ASSERT_EQUAL_STRING("Kk_1-2", secrets.value[SETTINGS_SECRET_SOLCAST_KEY]);
+    TEST_ASSERT_EQUAL_STRING("ab12-cd34", secrets.value[SETTINGS_SECRET_SOLCAST_SITE1]);
+    TEST_ASSERT_EQUAL_STRING("ef56", secrets.value[SETTINGS_SECRET_SOLCAST_SITE2]);
+    TEST_ASSERT_EQUAL_STRING("AbC123", secrets.value[SETTINGS_SECRET_FS_KEY]);
+    TEST_ASSERT_EQUAL_STRING("20200722", secrets.value[SETTINGS_SECRET_SOLAX_TOKEN]);
+    TEST_ASSERT_EQUAL_STRING("SXA1B2C3D4", secrets.value[SETTINGS_SECRET_SOLAX_SN]);
+    TEST_ASSERT_EQUAL_STRING("solcast_site2", settings_secret_key(SETTINGS_SECRET_SOLCAST_SITE2));
+    TEST_ASSERT_TRUE(settings_patch("{\"schema\":1}", clean, s_json, sizeof(s_json), s_err, sizeof(s_err)) > 0);
+    TEST_ASSERT_NULL(strstr(s_json, "Kk_1"));
+}
+
+static void test_a_secret_is_cleared_with_null_or_nothing(void)
+{
+    settings_secrets_t secrets;
+    char clean[256];
+    const char *patch = "{\"solar\":{\"fs_key\":null,\"solcast_sites\":[]},\"energy\":{\"solax_token\":\"\"}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_take_secrets(patch, clean, sizeof(clean), &secrets, s_err, sizeof(s_err)) > 0,
+                             s_err);
+    TEST_ASSERT_TRUE(secrets.given[SETTINGS_SECRET_FS_KEY]);
+    TEST_ASSERT_EQUAL_STRING("", secrets.value[SETTINGS_SECRET_FS_KEY]);
+    TEST_ASSERT_TRUE(secrets.given[SETTINGS_SECRET_SOLCAST_SITE1]);
+    TEST_ASSERT_TRUE(secrets.given[SETTINGS_SECRET_SOLCAST_SITE2]);
+    TEST_ASSERT_TRUE(secrets.given[SETTINGS_SECRET_SOLAX_TOKEN]);
+    TEST_ASSERT_FALSE(secrets.given[SETTINGS_SECRET_SOLAX_SN]); /* not named: as it is */
+    TEST_ASSERT_FALSE(secrets.given[SETTINGS_SECRET_SOLCAST_KEY]);
+    TEST_ASSERT_EQUAL_STRING("{\"solar\":{},\"energy\":{}}", clean);
+}
+
+/* A key named twice, or in a second "solar" object, is taken out too, the last counting as in a merge; the "keys"
+ * flags a GET adds are dropped, so a page that sends them back doesn't store them. */
+static void test_no_key_stays_behind(void)
+{
+    settings_secrets_t secrets;
+    char clean[256];
+    const char *patch = "{\"solar\":{\"fs_key\":\"Aa1\",\"fs_key\":\"Bb2\",\"keys\":{\"fs_key\":true}},"
+                        "\"solar\":{\"solcast_key\":\"Cc3\"},"
+                        "\"energy\":{\"keys\":{\"solax_sn\":false}}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_take_secrets(patch, clean, sizeof(clean), &secrets, s_err, sizeof(s_err)) > 0,
+                             s_err);
+    TEST_ASSERT_EQUAL_STRING("{\"solar\":{},\"solar\":{},\"energy\":{}}", clean);
+    TEST_ASSERT_EQUAL_STRING("Bb2", secrets.value[SETTINGS_SECRET_FS_KEY]);
+    TEST_ASSERT_EQUAL_STRING("Cc3", secrets.value[SETTINGS_SECRET_SOLCAST_KEY]);
+}
+
+/* A new Solcast key or site starts its budget over, so the page's Check now asks at once (spec §11.5); other keys
+ * don't. */
+static void test_a_solcast_key_or_site_starts_its_budget_over(void)
+{
+    settings_secrets_t secrets = { 0 };
+    secrets.given[SETTINGS_SECRET_FS_KEY] = secrets.given[SETTINGS_SECRET_SOLAX_TOKEN] = true;
+    TEST_ASSERT_FALSE(settings_secrets_solcast(&secrets));
+    for (int i = SETTINGS_SECRET_SOLCAST_KEY; i <= SETTINGS_SECRET_SOLCAST_SITE2; i++) {
+        settings_secrets_t one = { 0 };
+        one.given[i] = true;
+        TEST_ASSERT_TRUE(settings_secrets_solcast(&one));
+    }
+}
+
+static void test_secrets_the_requests_cannot_carry_are_refused(void)
+{
+    static const struct {
+        const char *patch, *why;
+    } k_cases[] = {
+        { "{\"solar\":{\"fs_key\":\"ab/c\"}}", "solar.fs_key: letters and digits only" },
+        { "{\"solar\":{\"solcast_key\":\"a b\"}}", "solar.solcast_key: letters, digits, - and _ only" },
+        { "{\"solar\":{\"solcast_sites\":[\"ab12?x\"]}}", "solar.solcast_sites: letters, digits and - only" },
+        { "{\"solar\":{\"solcast_sites\":[\"a\",\"b\",\"c\"]}}", "solar.solcast_sites: two at most" },
+        { "{\"solar\":{\"solcast_sites\":\"a\"}}", "solar.solcast_sites: a list" },
+        { "{\"energy\":{\"solax_token\":\"20\\r\\n20\"}}", "energy.solax_token: letters and digits only" },
+        { "{\"energy\":{\"solax_sn\":42}}", "energy.solax_sn: a string" },
+        { "{\"energy\":{\"solax_sn\":\"0123456789012345678901234567890123456789012345678901234567890123\"}}",
+          "energy.solax_sn: 63 characters at most" },
+    };
+    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
+        settings_secrets_t secrets;
+        char clean[256];
+        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, settings_take_secrets(k_cases[i].patch, clean, sizeof(clean), &secrets,
+                                                                s_err, sizeof(s_err)),
+                                       k_cases[i].patch);
+        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
+    }
+}
+
+static void test_a_second_plane_needs_a_forecast_solar_key(void)
+{
+    settings_t s = s_defaults;
+    s.solar_source = SETTINGS_SOLAR_FORECAST_SOLAR;
+    s.solar_plane_count = 2;
+    TEST_ASSERT_FALSE(settings_check_solar(&s, false, s_err, sizeof(s_err))); /* spec §11.5 */
+    TEST_ASSERT_EQUAL_STRING("a second plane needs a Forecast.Solar key", s_err);
+    TEST_ASSERT_TRUE(settings_check_solar(&s, true, s_err, sizeof(s_err)));
+    s.solar_source = SETTINGS_SOLAR_OPEN_METEO; /* our model takes both */
+    TEST_ASSERT_TRUE(settings_check_solar(&s, false, s_err, sizeof(s_err)));
+}
+
+static void test_the_largest_settings_fit_the_file_buffer(void)
+{
+    settings_t s = s_defaults;
+    memset(s.place, 'W', sizeof(s.place) - 1);
+    s.place[sizeof(s.place) - 1] = '\0';
+    memset(s.tz_iana, 'Z', sizeof(s.tz_iana) - 1);
+    s.tz_iana[sizeof(s.tz_iana) - 1] = '\0';
+    memset(s.tz_posix, 'P', sizeof(s.tz_posix) - 1);
+    s.tz_posix[sizeof(s.tz_posix) - 1] = '\0';
+    for (int i = 0; i < SETTINGS_NTP_MAX; i++) {
+        memset(s.ntp[i], 'n', SETTINGS_HOST_LEN - 1);
+        s.ntp[i][SETTINGS_HOST_LEN - 1] = '\0';
+    }
+    s.sync_time_count = SETTINGS_SYNC_TIMES_MAX;
+    for (int i = 0; i < SETTINGS_SYNC_TIMES_MAX; i++) {
+        s.sync_times[i] = (uint16_t)(1300 + i * 17);
+    }
+    for (int i = 0; i < SETTINGS_BAT_CURVE_POINTS; i++) {
+        s.bat_learned_mv[i] = (uint16_t)(3333 + i);
+    }
+    s.bat_learned_at = UINT32_MAX;
+    s.temp_offset_c100 = -999;
+    s.hum_offset_pct100 = -1999;
+    s.wx_centre_set = s.fl_centre_set = true;
+    s.wx_lat_e4 = s.fl_lat_e4 = -849999;
+    s.wx_lon_e4 = s.fl_lon_e4 = -1799999;
+    s.solar_plane_count = 2;
+    for (int i = 0; i < 2; i++) {
+        s.solar_planes[i] = (settings_plane_t){ .kwp_e2 = 9999, .tilt = 89, .azimuth = -179 };
+    }
+    s.solar_source = SETTINGS_SOLAR_FORECAST_SOLAR;
+    s.solar_inverter_kw_e2 = 9999;
+    size_t n = settings_to_json(&s, NULL, s_json, sizeof(s_json));
+    TEST_ASSERT_TRUE(n > 0);
+    TEST_ASSERT_TRUE(n < SETTINGS_FILE_MAX / 2); /* room for keys a later firmware adds (M7's mqtt.*) */
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -519,5 +760,15 @@ int main(void)
     RUN_TEST(test_the_radars_default_to_the_location);
     RUN_TEST(test_the_radar_settings_parse_clamp_and_round_trip);
     RUN_TEST(test_a_centre_that_follows_the_location_is_not_saved);
+    RUN_TEST(test_the_solar_defaults_are_the_specs);
+    RUN_TEST(test_the_sync_steps_parse_and_round_trip);
+    RUN_TEST(test_the_solar_settings_parse_clamp_and_round_trip);
+    RUN_TEST(test_secrets_never_reach_the_file);
+    RUN_TEST(test_a_secret_is_cleared_with_null_or_nothing);
+    RUN_TEST(test_no_key_stays_behind);
+    RUN_TEST(test_a_solcast_key_or_site_starts_its_budget_over);
+    RUN_TEST(test_secrets_the_requests_cannot_carry_are_refused);
+    RUN_TEST(test_a_second_plane_needs_a_forecast_solar_key);
+    RUN_TEST(test_the_largest_settings_fit_the_file_buffer);
     return UNITY_END();
 }
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host --target test_settings 2>&1 | grep -E 'error:' | sed -E 's/.*error: //' | sort | uniq -c | sort -rn | head -6`
Expected:

```
   3 no member named 'solar_planes' in 'settings_t'
   2 use of undeclared identifier 'SETTINGS_STEPS_ALL'
   2 no member named 'sync_steps' in 'settings_t'
   2 call to undeclared function 'settings_solar_defaults'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
   1 use of undeclared identifier 'SETTINGS_SOLAR_OFF'
   1 use of undeclared identifier 'SETTINGS_FILE_MAX'
```

- [ ] **Step 3: The settings.**

`components/storage/include/settings.h`:

```diff
--- a/components/storage/include/settings.h
+++ b/components/storage/include/settings.h
@@ -38,6 +38,38 @@ typedef enum {
 #define SETTINGS_SYNC_TIMES_MAX 8
 #define SETTINGS_NTP_MAX 2
 #define SETTINGS_HOST_LEN 64
+#define SETTINGS_FILE_MAX 4096 /* settings.json as the app reads and writes it */
+
+/* sync.steps (spec §9.3, D35): the data steps that run, as bits. The time always runs. */
+typedef enum {
+    SETTINGS_STEP_WEATHER = 1 << 0,
+    SETTINGS_STEP_AIR = 1 << 1,
+    SETTINGS_STEP_RADAR = 1 << 2,
+    SETTINGS_STEP_SOLAR = 1 << 3,
+    SETTINGS_STEP_ENERGY = 1 << 4,
+} settings_step_t;
+#define SETTINGS_STEPS_ALL 0x1F
+
+/* solar.source (spec §11.5); the values match solar_source_t in the solar component. */
+typedef enum {
+    SETTINGS_SOLAR_OFF,
+    SETTINGS_SOLAR_OPEN_METEO,
+    SETTINGS_SOLAR_FORECAST_SOLAR,
+    SETTINGS_SOLAR_SOLCAST,
+} settings_solar_source_t;
+
+/* energy.source and energy.battery (spec §11.6); the battery's values match energy_battery_t. */
+typedef enum { SETTINGS_ENERGY_OFF, SETTINGS_ENERGY_SOLAX } settings_energy_source_t;
+typedef enum { SETTINGS_BATTERY_AUTO, SETTINGS_BATTERY_ON, SETTINGS_BATTERY_OFF } settings_energy_battery_t;
+
+#define SETTINGS_PLANES_MAX 2
+
+/* A roof plane (spec §11.5). */
+typedef struct {
+    uint16_t kwp_e2; /* 10..10000: 0.1 to 100 kWp in hundredths */
+    uint8_t tilt;    /* 0..90° */
+    int16_t azimuth; /* -180..180°: 0 south, -90 east, 90 west */
+} settings_plane_t;
 
 typedef struct {
     char language[4];                    /* "en" */
@@ -73,6 +105,14 @@ typedef struct {
     uint16_t fl_min_alt_ft; /* 0..60000 */
     bool fl_ground;         /* aircraft on the ground too */
     uint8_t fl_max;         /* aircraft shown at most, 1..100 */
+    uint8_t sync_steps;     /* settings_step_t bits (D35) */
+    uint8_t solar_source;   /* settings_solar_source_t */
+    uint8_t solar_plane_count;                         /* 1..SETTINGS_PLANES_MAX */
+    settings_plane_t solar_planes[SETTINGS_PLANES_MAX];
+    uint8_t solar_losses_pct;      /* 0..50, our model's */
+    uint16_t solar_inverter_kw_e2; /* 0..10000: the inverter's limit in hundredths of kW, 0 for none */
+    uint8_t energy_source;         /* settings_energy_source_t */
+    uint8_t energy_battery;        /* settings_energy_battery_t */
 } settings_t;
 
 /* The sync's and the NTP servers' defaults (spec §14.3): times mode at 05:30, a 60 min interval,
@@ -94,6 +134,42 @@ void settings_toggle_always(settings_t *s);
  * 100 aircraft; both centres on out->lat_e4 and lon_e4, so set the location first. */
 void settings_radar_defaults(settings_t *out);
 
+/* The steps', the PV forecast's and the house's defaults (spec §14.3): every step on; no source; one
+ * plane of 5 kWp tilted 35° to the south, 14 % losses, no inverter limit; the battery on auto. */
+void settings_solar_defaults(settings_t *out);
+/* A step's name in sync.steps: "weather", "air", "radar", "solar", "energy". */
+const char *settings_step_name(settings_step_t step);
+/* Forecast.Solar takes a second plane only with a key (spec §11.5): false with the reason. */
+bool settings_check_solar(const settings_t *s, bool fs_key_set, char *err, size_t err_size);
+
+/* The secrets a PATCH may carry (spec §10.3, §14.2): write-only, kept in NVS `secrets`, never in the
+ * file. */
+typedef enum {
+    SETTINGS_SECRET_FS_KEY,        /* solar.fs_key */
+    SETTINGS_SECRET_SOLCAST_KEY,   /* solar.solcast_key */
+    SETTINGS_SECRET_SOLCAST_SITE1, /* solar.solcast_sites[0] */
+    SETTINGS_SECRET_SOLCAST_SITE2, /* solar.solcast_sites[1] */
+    SETTINGS_SECRET_SOLAX_TOKEN,   /* energy.solax_token */
+    SETTINGS_SECRET_SOLAX_SN,      /* energy.solax_sn */
+    SETTINGS_SECRET_COUNT,
+} settings_secret_t;
+#define SETTINGS_SECRET_LEN 64
+
+typedef struct {
+    bool given[SETTINGS_SECRET_COUNT];                    /* the patch names it; "" or null clears it */
+    char value[SETTINGS_SECRET_COUNT][SETTINGS_SECRET_LEN];
+} settings_secrets_t;
+
+/* Its key in NVS `secrets`: "fs_key", "solcast_key", "solcast_site1", ... */
+const char *settings_secret_key(settings_secret_t secret);
+/* `patch` without its secrets into `out`, the secrets into `secrets`. Each must be one the requests
+ * can carry: letters and digits (the Solcast key also - and _, its sites -), 63 at most. Returns the
+ * length written, or 0 with the reason in `err`. */
+size_t settings_take_secrets(const char *patch, char *out, size_t size, settings_secrets_t *secrets, char *err,
+                             size_t err_size);
+/* Whether `secrets` sets or clears Solcast's key or a site: its budget then starts over (spec §11.5). */
+bool settings_secrets_solcast(const settings_secrets_t *secrets);
+
 /* Fails only if the text is not a JSON object with "schema": 1. */
 bool settings_from_json(const char *json, const settings_t *defaults, settings_t *out, char *err, size_t err_size);
 /* `base_json` (the file as read, or NULL) with the known keys replaced by `s`. Returns the
```


`components/storage/settings.c`:

```diff
--- a/components/storage/settings.c
+++ b/components/storage/settings.c
@@ -225,6 +225,97 @@ static void read_radar(const cJSON *radar, settings_t *out)
     out->fl_max = (uint8_t)read_scaled(fl, "max", out->fl_max, 1, 1, 100);
 }
 
+static const char *const k_steps[] = { "weather", "air", "radar", "solar", "energy" };
+static const char *const k_solar_sources[] = { [SETTINGS_SOLAR_OFF] = "off", [SETTINGS_SOLAR_OPEN_METEO] = "open-meteo",
+                                               [SETTINGS_SOLAR_FORECAST_SOLAR] = "forecast-solar",
+                                               [SETTINGS_SOLAR_SOLCAST] = "solcast" };
+static const char *const k_energy_sources[] = { [SETTINGS_ENERGY_OFF] = "off", [SETTINGS_ENERGY_SOLAX] = "solax" };
+static const char *const k_batteries[] = { [SETTINGS_BATTERY_AUTO] = "auto", [SETTINGS_BATTERY_ON] = "on",
+                                           [SETTINGS_BATTERY_OFF] = "off" };
+
+/* The index of `item`'s string in `names`, or `fallback`. */
+static uint8_t choice(const cJSON *item, const char *const *names, size_t count, uint8_t fallback)
+{
+    for (size_t i = 0; cJSON_IsString(item) && i < count; i++) {
+        if (strcmp(item->valuestring, names[i]) == 0) {
+            return (uint8_t)i;
+        }
+    }
+    return fallback;
+}
+
+/* sync.steps: a missing list or one that isn't is all of them; an unknown name is left out. */
+static void read_steps(const cJSON *steps, settings_t *out)
+{
+    if (!cJSON_IsArray(steps)) {
+        return;
+    }
+    out->sync_steps = 0;
+    const cJSON *item;
+    cJSON_ArrayForEach(item, steps)
+    {
+        uint8_t i = choice(item, k_steps, sizeof(k_steps) / sizeof(k_steps[0]), UINT8_MAX);
+        out->sync_steps |= i != UINT8_MAX ? (uint8_t)(1u << i) : 0;
+    }
+}
+
+static void read_solar(const cJSON *solar, const cJSON *energy, settings_t *out)
+{
+    out->solar_source = choice(child(solar, "source"), k_solar_sources,
+                               sizeof(k_solar_sources) / sizeof(k_solar_sources[0]), out->solar_source);
+    const cJSON *planes = child(solar, "planes");
+    int n = cJSON_IsArray(planes) ? cJSON_GetArraySize(planes) : 0;
+    if (n > 0) {
+        settings_plane_t fallback = out->solar_planes[0];
+        out->solar_plane_count = (uint8_t)(n < SETTINGS_PLANES_MAX ? n : SETTINGS_PLANES_MAX);
+        for (int i = 0; i < out->solar_plane_count; i++) {
+            const cJSON *p = cJSON_GetArrayItem(planes, i);
+            settings_plane_t *plane = &out->solar_planes[i];
+            *plane = i < 1 || plane->kwp_e2 == 0 ? fallback : *plane;
+            plane->kwp_e2 = (uint16_t)read_scaled(p, "kwp", plane->kwp_e2, 100, 10, 10000);
+            plane->tilt = (uint8_t)read_scaled(p, "tilt", plane->tilt, 1, 0, 90);
+            plane->azimuth = (int16_t)read_scaled(p, "azimuth", plane->azimuth, 1, -180, 180);
+        }
+    }
+    out->solar_losses_pct = (uint8_t)read_scaled(solar, "losses_pct", out->solar_losses_pct, 1, 0, 50);
+    out->solar_inverter_kw_e2 = (uint16_t)read_scaled(solar, "inverter_kw", out->solar_inverter_kw_e2, 100, 0, 10000);
+    out->energy_source = choice(child(energy, "source"), k_energy_sources,
+                                sizeof(k_energy_sources) / sizeof(k_energy_sources[0]), out->energy_source);
+    out->energy_battery = choice(child(energy, "battery"), k_batteries, sizeof(k_batteries) / sizeof(k_batteries[0]),
+                                 out->energy_battery);
+}
+
+void settings_solar_defaults(settings_t *out)
+{
+    out->sync_steps = SETTINGS_STEPS_ALL;
+    out->solar_source = SETTINGS_SOLAR_OFF;
+    out->solar_plane_count = 1;
+    memset(out->solar_planes, 0, sizeof(out->solar_planes));
+    out->solar_planes[0] = (settings_plane_t){ .kwp_e2 = 500, .tilt = 35, .azimuth = 0 };
+    out->solar_losses_pct = 14;
+    out->solar_inverter_kw_e2 = 0;
+    out->energy_source = SETTINGS_ENERGY_OFF;
+    out->energy_battery = SETTINGS_BATTERY_AUTO;
+}
+
+const char *settings_step_name(settings_step_t step)
+{
+    for (size_t i = 0; i < sizeof(k_steps) / sizeof(k_steps[0]); i++) {
+        if (step == (settings_step_t)(1u << i)) {
+            return k_steps[i];
+        }
+    }
+    return "";
+}
+
+bool settings_check_solar(const settings_t *s, bool fs_key_set, char *err, size_t err_size)
+{
+    if (s->solar_source == SETTINGS_SOLAR_FORECAST_SOLAR && s->solar_plane_count > 1 && !fs_key_set) {
+        return fail(err, err_size, "a second plane needs a Forecast.Solar key");
+    }
+    return true;
+}
+
 void settings_radar_defaults(settings_t *out)
 {
     out->wx_centre_set = false;
@@ -333,6 +424,8 @@ bool settings_from_json(const char *json, const settings_t *defaults, settings_t
     read_ntp(child(time, "ntp"), out);
     read_sync(child(root, "sync"), out);
     read_radar(child(root, "radar"), out); /* after the location, which its centres may follow */
+    read_steps(child(child(root, "sync"), "steps"), out);
+    read_solar(child(root, "solar"), child(root, "energy"), out);
     cJSON_Delete(root);
     return true;
 }
@@ -448,6 +541,32 @@ size_t settings_to_json(const settings_t *s, const char *base_json, char *out, s
     put(fl, "min_alt_ft", cJSON_CreateNumber(s->fl_min_alt_ft));
     put(fl, "ground", cJSON_CreateBool(s->fl_ground));
     put(fl, "max", cJSON_CreateNumber(s->fl_max));
+    cJSON *steps = cJSON_CreateArray();
+    for (size_t i = 0; i < sizeof(k_steps) / sizeof(k_steps[0]); i++) {
+        if (s->sync_steps & (1u << i)) {
+            cJSON_AddItemToArray(steps, cJSON_CreateString(k_steps[i]));
+        }
+    }
+    put(sync, "steps", steps);
+    cJSON *solar = object_at(root, "solar");
+    put(solar, "source", cJSON_CreateString(k_solar_sources[s->solar_source <= SETTINGS_SOLAR_SOLCAST ? s->solar_source
+                                                                                                    : 0]));
+    cJSON *planes = cJSON_CreateArray();
+    for (int i = 0; i < s->solar_plane_count && i < SETTINGS_PLANES_MAX; i++) {
+        cJSON *plane = cJSON_CreateObject();
+        cJSON_AddNumberToObject(plane, "kwp", s->solar_planes[i].kwp_e2 / 100.0);
+        cJSON_AddNumberToObject(plane, "tilt", s->solar_planes[i].tilt);
+        cJSON_AddNumberToObject(plane, "azimuth", s->solar_planes[i].azimuth);
+        cJSON_AddItemToArray(planes, plane);
+    }
+    put(solar, "planes", planes);
+    put(solar, "losses_pct", cJSON_CreateNumber(s->solar_losses_pct));
+    put(solar, "inverter_kw", cJSON_CreateNumber(s->solar_inverter_kw_e2 / 100.0));
+    cJSON *energy = object_at(root, "energy");
+    uint8_t energy_source = s->energy_source <= SETTINGS_ENERGY_SOLAX ? s->energy_source : 0;
+    uint8_t energy_battery = s->energy_battery <= SETTINGS_BATTERY_OFF ? s->energy_battery : 0;
+    put(energy, "source", cJSON_CreateString(k_energy_sources[energy_source]));
+    put(energy, "battery", cJSON_CreateString(k_batteries[energy_battery]));
     bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, true);
     cJSON_Delete(root);
     return ok ? strlen(out) : 0;
@@ -499,3 +618,129 @@ size_t settings_patch(const char *base_json, const char *patch, char *out, size_
     cJSON_Delete(root);
     return n;
 }
+
+static const char *const k_secret_keys[SETTINGS_SECRET_COUNT] = {
+    "fs_key", "solcast_key", "solcast_site1", "solcast_site2", "solax_token", "solax_sn",
+};
+
+const char *settings_secret_key(settings_secret_t secret)
+{
+    return (unsigned)secret < SETTINGS_SECRET_COUNT ? k_secret_keys[secret] : "";
+}
+
+/* Letters and digits, and those of `extra`. */
+static bool plain_text(const char *s, const char *extra)
+{
+    for (; *s != '\0'; s++) {
+        bool letter = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9');
+        if (!letter && strchr(extra, *s) == NULL) {
+            return false;
+        }
+    }
+    return true;
+}
+
+/* One secret's value (a string, "" or null) into `secrets`; false with the reason. */
+static bool take_one(const cJSON *item, const char *path, const char *extra, const char *rule,
+                     settings_secrets_t *secrets, settings_secret_t which, char *err, size_t err_size)
+{
+    if (item != NULL && !cJSON_IsNull(item) && !cJSON_IsString(item)) { /* NULL: past a list's end */
+        return fail(err, err_size, "%s: a string", path);
+    }
+    const char *v = cJSON_IsString(item) ? item->valuestring : "";
+    if (strlen(v) >= SETTINGS_SECRET_LEN) {
+        return fail(err, err_size, "%s: %d characters at most", path, SETTINGS_SECRET_LEN - 1);
+    }
+    if (!plain_text(v, extra)) {
+        return fail(err, err_size, "%s: %s", path, rule);
+    }
+    secrets->given[which] = true;
+    snprintf(secrets->value[which], SETTINGS_SECRET_LEN, "%s", v);
+    return true;
+}
+
+/* Every obj[key] taken out and parsed into `which`, the last counting, as in a merge. */
+static bool take(cJSON *obj, const char *key, const char *path, const char *extra, const char *rule,
+                 settings_secrets_t *secrets, settings_secret_t which, char *err, size_t err_size)
+{
+    bool ok = true;
+    for (cJSON *item; ok && (item = cJSON_DetachItemFromObjectCaseSensitive(obj, key)) != NULL;) {
+        ok = take_one(item, path, extra, rule, secrets, which, err, err_size);
+        cJSON_Delete(item);
+    }
+    return ok;
+}
+
+static bool take_sites(cJSON *solar, settings_secrets_t *secrets, char *err, size_t err_size)
+{
+    bool ok = true;
+    for (cJSON *sites; ok && (sites = cJSON_DetachItemFromObjectCaseSensitive(solar, "solcast_sites")) != NULL;) {
+        if (!cJSON_IsNull(sites) && !cJSON_IsArray(sites)) {
+            ok = fail(err, err_size, "solar.solcast_sites: a list");
+        } else if (cJSON_GetArraySize(sites) > 2) {
+            ok = fail(err, err_size, "solar.solcast_sites: two at most");
+        } else {
+            for (int i = 0; ok && i < 2; i++) {
+                ok = take_one(cJSON_GetArrayItem(sites, i), "solar.solcast_sites", "-", "letters, digits and - only",
+                              secrets, (settings_secret_t)(SETTINGS_SECRET_SOLCAST_SITE1 + i), err, err_size);
+            }
+        }
+        cJSON_Delete(sites);
+    }
+    return ok;
+}
+
+/* The keys of every "solar" and "energy" object, and the flags a GET adds ("keys"), out of `root`. */
+static bool take_all(cJSON *root, settings_secrets_t *secrets, char *err, size_t err_size)
+{
+    static const char k_alnum[] = "letters and digits only";
+    bool ok = true;
+    for (cJSON *c = root->child; ok && c != NULL; c = c->next) {
+        bool solar = c->string != NULL && strcmp(c->string, "solar") == 0;
+        bool energy = c->string != NULL && strcmp(c->string, "energy") == 0;
+        if (!cJSON_IsObject(c) || !(solar || energy)) {
+            continue;
+        }
+        for (cJSON *flags; (flags = cJSON_DetachItemFromObjectCaseSensitive(c, "keys")) != NULL;) {
+            cJSON_Delete(flags);
+        }
+        ok = solar ? take(c, "fs_key", "solar.fs_key", "", k_alnum, secrets, SETTINGS_SECRET_FS_KEY, err, err_size) &&
+                         take(c, "solcast_key", "solar.solcast_key", "-_", "letters, digits, - and _ only", secrets,
+                              SETTINGS_SECRET_SOLCAST_KEY, err, err_size) &&
+                         take_sites(c, secrets, err, err_size)
+                   : take(c, "solax_token", "energy.solax_token", "", k_alnum, secrets, SETTINGS_SECRET_SOLAX_TOKEN,
+                          err, err_size) &&
+                         take(c, "solax_sn", "energy.solax_sn", "", k_alnum, secrets, SETTINGS_SECRET_SOLAX_SN, err,
+                              err_size);
+    }
+    return ok;
+}
+
+bool settings_secrets_solcast(const settings_secrets_t *secrets)
+{
+    return secrets->given[SETTINGS_SECRET_SOLCAST_KEY] || secrets->given[SETTINGS_SECRET_SOLCAST_SITE1] ||
+           secrets->given[SETTINGS_SECRET_SOLCAST_SITE2];
+}
+
+size_t settings_take_secrets(const char *patch, char *out, size_t size, settings_secrets_t *secrets, char *err,
+                             size_t err_size)
+{
+    memset(secrets, 0, sizeof(*secrets));
+    if (util_json_depth(patch) > SETTINGS_JSON_MAX_DEPTH) {
+        fail(err, err_size, "nested more than %d levels", SETTINGS_JSON_MAX_DEPTH);
+        return 0;
+    }
+    cJSON *root = patch != NULL ? cJSON_Parse(patch) : NULL;
+    if (!cJSON_IsObject(root)) {
+        cJSON_Delete(root);
+        fail(err, err_size, "not a JSON object");
+        return 0;
+    }
+    bool ok = take_all(root, secrets, err, err_size);
+    bool printed = ok && size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
+    cJSON_Delete(root);
+    if (ok && !printed) {
+        fail(err, err_size, "too large");
+    }
+    return printed ? strlen(out) : 0;
+}
```


- [ ] **Step 4: The app's defaults and its 4 KB.** The defaults come from `components/storage` (`AGENTS.md` §8), and the backup's assert follows the larger file:

`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -27,7 +27,7 @@ static const char *TAG = "app_ui";
 static app_ui_state_t s;
 /* The config files' text: scratch buffers in PSRAM (AGENTS.md §8). */
 EXT_RAM_BSS_ATTR static char s_file[UI_PRESETS_JSON_MAX];
-EXT_RAM_BSS_ATTR static char s_settings_base[2048]; /* settings.json as read: unknown keys stay */
+EXT_RAM_BSS_ATTR static char s_settings_base[SETTINGS_FILE_MAX]; /* settings.json as read: unknown keys stay */
 static char s_err[96];
 static char s_toast[64];
 static int64_t s_toast_until_ms;
@@ -57,6 +57,7 @@ static void default_settings(settings_t *out)
     };
     settings_sync_defaults(out);
     settings_radar_defaults(out);
+    settings_solar_defaults(out);
     snprintf(out->place, sizeof(out->place), "%s", CONFIG_REFLBO_LOCATION_NAME);
     snprintf(out->tz_posix, sizeof(out->tz_posix), "%s", CONFIG_REFLBO_TZ);
     snprintf(out->tz_iana, sizeof(out->tz_iana), "%s", CONFIG_REFLBO_TZ_NAME);
```


`main/app_web.c`:

```diff
--- a/main/app_web.c
+++ b/main/app_web.c
@@ -359,16 +359,17 @@ static void learn(const char *body, uint8_t *out, size_t size, webui_reply_t *re
     reply_cjson(reply, out, size, o);
 }
 
-/* A backup of the largest files restores: settings.json up to the 2 KB app_ui.c keeps, presets.json up to
+/* A backup of the largest files restores: settings.json up to the 4 KB app_ui.c keeps, presets.json up to
  * its own limit, and the bundle around them; and of the deepest, as each limit leaves room for the next. */
-_Static_assert(2048 + UI_PRESETS_JSON_MAX + 512 <= WEBUI_BODY_MAX, "a backup of the largest files fits a request");
+_Static_assert(SETTINGS_FILE_MAX + UI_PRESETS_JSON_MAX + 512 <= WEBUI_BODY_MAX,
+               "a backup of the largest files fits a request");
 _Static_assert(UI_JSON_MAX_DEPTH + 2 <= BACKUP_MAX_DEPTH, "a backup holds the deepest presets.json");
 _Static_assert(BACKUP_MAX_DEPTH <= WEBUI_JSON_MAX_DEPTH, "the web server passes the deepest backup");
 
 /* GET /api/backup (spec §14.4): the /cfg files as the firmware would save them now. */
 static void backup(uint8_t *out, size_t size, webui_reply_t *reply)
 {
-    EXT_RAM_BSS_ATTR static char settings[2048];
+    EXT_RAM_BSS_ATTR static char settings[SETTINGS_FILE_MAX];
     EXT_RAM_BSS_ATTR static char presets[UI_PRESETS_JSON_MAX];
     netmgr_status_t net;
     netmgr_status(&net);
```


- [ ] **Step 5: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_settings | tail -2 && ctest --test-dir build-host | tail -3`
Expected:

```
37 Tests 0 Failures 0 Ignored
OK
100% tests passed, 0 tests failed out of 63
```

- [ ] **Step 6: The firmware builds:** `tools/idf.sh build`, clean, without a warning.

- [ ] **Step 7: Commit.**

```bash
git add components/storage main/app_ui.c main/app_web.c test/host/test_settings.c
git commit -m "feat(storage): the steps' switches, the solar and energy settings, and their keys"
```

### Task 5: The `pv.*` and `energy.*` fields at every size (`ui`, `locale`, `gfx` icons)

**Files:**
- Create: `components/ui/include/ui_solar.h`, `components/ui/ui_solar.c`, `test/host/solar_fixtures.h`
- Modify: `assets/icons/icons.txt`, `components/gfx/icons/gfx_icons.c` and `components/gfx/include/gfx_icons.h` (generated), `components/locale/include/lang.h`, `components/locale/lang_en.c`, `components/locale/lang_cs.c`, `components/ui/CMakeLists.txt`, `components/ui/include/ui_fields.h`, `components/ui/ui_fields.c`, `components/ui/ui_internal.h`, `components/ui/ui_widget.c`, `test/host/CMakeLists.txt`
- Test: `test/host/test_ui_fields.c`, `test/host/test_ui_widget_fit.c`

**Interfaces:**
- Consumes: Tasks 1–3's `solar_*` and `energy_*`; `ui_widget_draw()`'s number and battery widgets, which already step a number down to its `short_text`, its unit and its symbol before a cut (M6c).
- Produces (Tasks 6, 7, 9):
  - fields `UI_FIELD_PV_NOW`, `_PV_TODAY`, `_PV_LEFT`, `_PV_TOMORROW`, `_PV_PEAK` and `UI_FIELD_EN_PV`, `_EN_GRID`, `_EN_LOAD`, `_EN_BATTERY`, `_EN_YIELD`, `_EN_EXPORT`, `_EN_IMPORT`, `_EN_SELF` (numbers, the battery a battery), named `pv.now` … `energy.self`;
  - `struct ui_solar { forecast; forecast_ttl_s; reading; day; battery; }` (`ui_solar.h`), `ctx->solar` and `value->solar`; `UI_SOLAR_IDLE_W` (20: less flows nowhere);
  - `bool ui_resolve_solar(ctx, field, out)` (ui_internal.h);
  - `void ui_solar_demo(day, midnight, now, live, battery, forecast, reading, energy_day)`: the sample day;
  - icons `gfx_icon_forecast_*`, `_solar_*`, `_house_*`, `_grid_*`, `_self_use_*` at 16, 24 and 48 px;
  - strings `LS_PV_NOW` … `LS_EN_SELF`, `LS_EN_EXPORTING`, `LS_EN_IMPORTING`;
  - `test/host/solar_fixtures.h`: `fixture_solar(now, live, battery)`, `fixture_solar_none()`, `fixture_solar_largest(now)`, `FIX_MIDNIGHT`, `FIX_SOLAR_NOW` (13:20 CEST).

The numbers (spec §5.1): kW with two decimals ("3.42", "12.3" from 10 kW), its short form one decimal from 1 kW ("3.4") and whole from 10 kW; kWh with one decimal, whole from 10 kWh ("27"); smaller values keep their decimals. A sun marks the forecast and solar panels the measured output. `energy.grid`'s label says "Export" or "Import" in M and up, its trend arrow ↑ to the grid or ↓ from it in S and XS, and nothing under 20 W; the battery shows its charge, a bolt while charging, ↓ while discharging and its power in M and up. The forecast is fresh for its view's `forecast_ttl_s` (Task 9: the sync interval or Solcast's wait, and 2 h; 0, never stale, in sync mode `manual`); a reading for 15 min; today's totals come only from a reading of today. The fields, the chart and the flow are missing before the first forecast or reading, and the forecast's fields on a day it doesn't cover.

- [ ] **Step 1: Write the failing tests.** Each field's text, unit, short form, label, state and age, from a forecast and a reading the test writes out; after midnight tomorrow is today; the forecast's wait; the house now, its grid's way, its battery; today's totals, with and without a reading near midnight; 15 minutes of freshness:

`test/host/test_ui_fields.c`:

```diff
--- a/test/host/test_ui_fields.c
+++ b/test/host/test_ui_fields.c
@@ -6,6 +6,7 @@
 #include "context_fixtures.h"
 #include "ui_fields.h"
 #include "ui_radar.h"
+#include "ui_solar.h"
 #include "unity.h"
 
 static ui_context_t s_ctx;
@@ -337,6 +338,224 @@ static void test_none_resolves_to_missing(void)
     TEST_ASSERT_EQUAL(UI_FIELD_NONE, v.field);
 }
 
+/* --- the PV forecast and the house (M6d) --- */
+
+#define FIX_1320 (FIX_NOW - 7 * 3600 - 28 * 60) /* 13:20 CEST on the fixture's day */
+
+static solar_forecast_t s_forecast;
+static energy_reading_t s_reading;
+static energy_day_t s_day;
+static ui_solar_t s_solar;
+
+/* A forecast fetched at 05:48 for the fixture's day and the next, and a reading at 13:17 with today's midnight
+ * reading at 00:02; the context at 13:20. */
+static void with_solar(void)
+{
+    memset(&s_forecast, 0, sizeof(s_forecast));
+    s_forecast.day = FIX_DAY;
+    s_forecast.fetched = (uint32_t)(FIX_NOW - 15 * 3600);
+    s_forecast.q[0][51] = 360; /* 12:45: the day's peak, 3.6 kW */
+    s_forecast.q[0][53] = 350; /* 13:15-13:30 */
+    s_forecast.q[0][54] = 86;  /* 0.86 kW */
+    s_forecast.q[1][48] = 100;
+    s_forecast.wh[0] = 18400;
+    s_forecast.wh[1] = 1400;
+    s_forecast.wh[2] = 27400;
+    energy_day_init(&s_day);
+    s_day.day = FIX_DAY;
+    s_day.base_at = (uint32_t)(FIX_1320 - 13 * 3600 - 18 * 60); /* 00:02 */
+    s_day.base_to_wh = 1000000;
+    s_day.base_from_wh = 2000000;
+    s_reading = (energy_reading_t){ .at = (uint32_t)(FIX_1320 - 180), .pv_w = 3420, .grid_w = -2560, .load_w = 860,
+                                    .bat_w = 0, .soc = -1, .inverter = 4, .yield_wh = 9400,
+                                    .to_grid_wh = 1004900, .from_grid_wh = 2000600 };
+    s_solar = (ui_solar_t){ .forecast = &s_forecast, .forecast_ttl_s = 26 * 3600, .reading = &s_reading,
+                            .day = &s_day, .battery = false };
+    s_ctx.solar = &s_solar;
+    s_ctx.now = FIX_1320;
+    s_ctx.local = fixture_local(13, 20, 0);
+}
+
+static void test_the_forecast_fields_read_the_quarter_hour_now(void)
+{
+    with_solar();
+    ui_value_t v = resolve(UI_FIELD_PV_NOW);
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
+    TEST_ASSERT_EQUAL_STRING("3.50", v.text); /* 13:15-13:30 */
+    TEST_ASSERT_EQUAL_STRING("3.5", v.short_text);
+    TEST_ASSERT_EQUAL_STRING("kW", v.unit);
+    TEST_ASSERT_EQUAL_STRING("Forecast now", v.label);
+    v = resolve(UI_FIELD_PV_TODAY);
+    TEST_ASSERT_EQUAL_STRING("18.4", v.text);
+    TEST_ASSERT_EQUAL_STRING("18", v.short_text);
+    TEST_ASSERT_EQUAL_STRING("kWh", v.unit);
+    v = resolve(UI_FIELD_PV_LEFT); /* two thirds of 13:15's and all of 13:30's: 583 + 215 Wh */
+    TEST_ASSERT_EQUAL_STRING("0.8", v.text);
+    TEST_ASSERT_EQUAL_STRING("", v.short_text); /* small values keep their decimals */
+    v = resolve(UI_FIELD_PV_TOMORROW);
+    TEST_ASSERT_EQUAL_STRING("1.4", v.text);
+    v = resolve(UI_FIELD_PV_PEAK);
+    TEST_ASSERT_EQUAL_STRING("3.60", v.text);
+    TEST_ASSERT_EQUAL_STRING("12:45", v.extra);
+    s_ctx.clock_24h = false;
+    v = resolve(UI_FIELD_PV_PEAK);
+    TEST_ASSERT_EQUAL_STRING("12:45 PM", v.extra);
+}
+
+static void test_kilowatts_shorten_only_from_one_and_kilowatt_hours_from_ten(void)
+{
+    with_solar();
+    s_forecast.q[0][53] = 86;
+    ui_value_t v = resolve(UI_FIELD_PV_NOW);
+    TEST_ASSERT_EQUAL_STRING("0.86", v.text);
+    TEST_ASSERT_EQUAL_STRING("", v.short_text);
+    s_forecast.q[0][53] = 1234; /* 12.34 kW: one decimal from 10 kW */
+    v = resolve(UI_FIELD_PV_NOW);
+    TEST_ASSERT_EQUAL_STRING("12.3", v.text);
+    TEST_ASSERT_EQUAL_STRING("12", v.short_text);
+    s_forecast.q[0][53] = 19999; /* 199.99 kW */
+    v = resolve(UI_FIELD_PV_NOW);
+    TEST_ASSERT_EQUAL_STRING("200.0", v.text);
+    TEST_ASSERT_EQUAL_STRING("200", v.short_text);
+    s_forecast.wh[0] = 9949;
+    v = resolve(UI_FIELD_PV_TODAY);
+    TEST_ASSERT_EQUAL_STRING("9.9", v.text);
+    TEST_ASSERT_EQUAL_STRING("", v.short_text);
+    s_ctx.lang = lang_get("cs");
+    v = resolve(UI_FIELD_PV_TODAY);
+    TEST_ASSERT_EQUAL_STRING("9,9", v.text);
+    TEST_ASSERT_EQUAL_STRING("Předpověď dnes", v.label);
+}
+
+static void test_after_midnight_tomorrow_is_today(void)
+{
+    with_solar();
+    s_ctx.now += 86400;
+    s_ctx.local_day = FIX_DAY + 1;
+    TEST_ASSERT_EQUAL_STRING("1.4", resolve(UI_FIELD_PV_TODAY).text);
+    TEST_ASSERT_EQUAL_STRING("27.4", resolve(UI_FIELD_PV_TOMORROW).text); /* the third day's total */
+    TEST_ASSERT_EQUAL_STRING("0.00", resolve(UI_FIELD_PV_NOW).text);
+    s_ctx.now += 86400;
+    s_ctx.local_day = FIX_DAY + 2;
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_PV_NOW).state); /* no quarter hours for that day */
+    TEST_ASSERT_EQUAL_STRING("27.4", resolve(UI_FIELD_PV_TODAY).text);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_PV_TOMORROW).state);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_PV_LEFT).state);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_PV_PEAK).state);
+}
+
+static void test_the_forecast_goes_stale_after_its_wait(void)
+{
+    with_solar();
+    s_ctx.now = (time_t)s_forecast.fetched + 26 * 3600 + 60;
+    ui_value_t v = resolve(UI_FIELD_PV_TODAY);
+    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
+    TEST_ASSERT_EQUAL_UINT32(26 * 3600 + 60, v.age_s);
+    s_solar.forecast_ttl_s = 0; /* sync mode manual: never stale, only old */
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, resolve(UI_FIELD_PV_TODAY).state);
+}
+
+static void test_without_a_forecast_or_a_reading_the_fields_are_missing(void)
+{
+    with_solar();
+    s_forecast.day = 0;
+    s_reading.at = 0;
+    for (int f = UI_FIELD_PV_NOW; f <= UI_FIELD_EN_SELF; f++) {
+        TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve((ui_field_id_t)f).state);
+    }
+    s_ctx.solar = NULL;
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_PV_NOW).state);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_PV).state);
+    TEST_ASSERT_EQUAL_STRING("Solar", resolve(UI_FIELD_EN_PV).label);
+}
+
+static void test_the_house_now(void)
+{
+    with_solar();
+    ui_value_t v = resolve(UI_FIELD_EN_PV);
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
+    TEST_ASSERT_EQUAL_STRING("3.42", v.text);
+    TEST_ASSERT_EQUAL_STRING("kW", v.unit);
+    TEST_ASSERT_EQUAL_STRING("0.86", resolve(UI_FIELD_EN_LOAD).text);
+    v = resolve(UI_FIELD_EN_GRID); /* exporting: its label in M and up, an arrow up in S and XS */
+    TEST_ASSERT_EQUAL_STRING("2.56", v.text);
+    TEST_ASSERT_EQUAL_STRING("Export", v.label);
+    TEST_ASSERT_EQUAL_INT(1, v.trend);
+    s_reading.grid_w = 430;
+    v = resolve(UI_FIELD_EN_GRID);
+    TEST_ASSERT_EQUAL_STRING("0.43", v.text);
+    TEST_ASSERT_EQUAL_STRING("Import", v.label);
+    TEST_ASSERT_EQUAL_INT(-1, v.trend);
+    s_reading.grid_w = -12; /* under 20 W flows nowhere */
+    v = resolve(UI_FIELD_EN_GRID);
+    TEST_ASSERT_EQUAL_STRING("Grid", v.label);
+    TEST_ASSERT_EQUAL_INT(0, v.trend);
+}
+
+static void test_the_home_battery(void)
+{
+    with_solar();
+    s_reading.soc = 64;
+    s_reading.bat_w = 1200;
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_BATTERY).state); /* it doesn't show */
+    s_solar.battery = true;
+    ui_value_t v = resolve(UI_FIELD_EN_BATTERY);
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
+    TEST_ASSERT_EQUAL_STRING("64", v.text);
+    TEST_ASSERT_EQUAL_STRING("%", v.unit);
+    TEST_ASSERT_EQUAL_INT(64, v.percent);
+    TEST_ASSERT_EQUAL(DS_BAT_CHARGING, v.battery); /* the bolt */
+    TEST_ASSERT_EQUAL_INT(0, v.trend);
+    TEST_ASSERT_EQUAL_STRING("1.20 kW", v.extra); /* its power in M and up */
+    TEST_ASSERT_EQUAL_STRING("Home battery", v.label);
+    s_reading.bat_w = -800;
+    v = resolve(UI_FIELD_EN_BATTERY);
+    TEST_ASSERT_EQUAL(DS_BAT_DISCHARGING, v.battery);
+    TEST_ASSERT_EQUAL_INT(-1, v.trend); /* the arrow down */
+    TEST_ASSERT_EQUAL_STRING("0.80 kW", v.extra);
+    s_reading.bat_w = 5;
+    TEST_ASSERT_EQUAL_STRING("", resolve(UI_FIELD_EN_BATTERY).extra);
+    s_reading.soc = -1; /* shown, but the reply has no charge */
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_BATTERY).state);
+}
+
+static void test_todays_totals(void)
+{
+    with_solar();
+    ui_value_t v = resolve(UI_FIELD_EN_YIELD);
+    TEST_ASSERT_EQUAL_STRING("9.4", v.text);
+    TEST_ASSERT_EQUAL_STRING("kWh", v.unit);
+    v = resolve(UI_FIELD_EN_EXPORT);
+    TEST_ASSERT_EQUAL_STRING("4.9", v.text);
+    TEST_ASSERT_EQUAL_INT(1, v.trend);
+    v = resolve(UI_FIELD_EN_IMPORT);
+    TEST_ASSERT_EQUAL_STRING("0.6", v.text);
+    TEST_ASSERT_EQUAL_INT(-1, v.trend);
+    v = resolve(UI_FIELD_EN_SELF);
+    TEST_ASSERT_EQUAL_STRING("48", v.text); /* (9.4 - 4.9) / 9.4 */
+    TEST_ASSERT_EQUAL_STRING("%", v.unit);
+    s_day.base_at = 0; /* no reading near midnight */
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_EXPORT).state);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_IMPORT).state);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_SELF).state);
+    TEST_ASSERT_EQUAL_STRING("9.4", resolve(UI_FIELD_EN_YIELD).text);
+    s_ctx.now += 86400; /* yesterday's reading says nothing about today's totals */
+    s_ctx.local_day = FIX_DAY + 1;
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_EN_YIELD).state);
+    TEST_ASSERT_EQUAL(UI_VALUE_STALE, resolve(UI_FIELD_EN_PV).state); /* the power is stale, not gone */
+}
+
+static void test_a_reading_goes_stale_after_15_minutes(void)
+{
+    with_solar();
+    s_ctx.now = (time_t)s_reading.at + 15 * 60;
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, resolve(UI_FIELD_EN_PV).state);
+    s_ctx.now += 60;
+    ui_value_t v = resolve(UI_FIELD_EN_PV);
+    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
+    TEST_ASSERT_EQUAL_UINT32(16 * 60, v.age_s);
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -360,5 +579,14 @@ int main(void)
     RUN_TEST(test_old_readings_are_stale_with_their_age);
     RUN_TEST(test_none_resolves_to_missing);
     RUN_TEST(test_numbers_with_decimals_carry_a_whole_number_form);
+    RUN_TEST(test_the_forecast_fields_read_the_quarter_hour_now);
+    RUN_TEST(test_kilowatts_shorten_only_from_one_and_kilowatt_hours_from_ten);
+    RUN_TEST(test_after_midnight_tomorrow_is_today);
+    RUN_TEST(test_the_forecast_goes_stale_after_its_wait);
+    RUN_TEST(test_without_a_forecast_or_a_reading_the_fields_are_missing);
+    RUN_TEST(test_the_house_now);
+    RUN_TEST(test_the_home_battery);
+    RUN_TEST(test_todays_totals);
+    RUN_TEST(test_a_reading_goes_stale_after_15_minutes);
     return UNITY_END();
 }
```


Every field in every cell, now with the solar view in each of the fifteen sets of data: the sample day, a battery in every other set, read three hours ago and forecast two days ago in set 2, nothing in set 3, the largest values in set 14. Each field shows its symbol, and the grid its way:

`test/host/test_ui_widget_fit.c`:

```diff
--- a/test/host/test_ui_widget_fit.c
+++ b/test/host/test_ui_widget_fit.c
@@ -5,6 +5,7 @@
 #include <string.h>
 
 #include "dashboard_fixtures.h"
+#include "solar_fixtures.h"
 #include "gfx.h"
 #include "gfx_fonts.h"
 #include "gfx_icons.h"
@@ -203,7 +204,8 @@ static void test_a_number_keeps_its_size_as_its_digits_change(void)
  * by day and rain today, 9 snow showers at night in Czech and a battery charging at 100 %, 10 Monday
  * 28 September in Czech, a public holiday, 11 a polar night (89.9° N), 12 a polar day (89.9° S) in Czech,
  * 13 Wednesday 30 September on a 12-hour clock, 14 the largest values (-23.5 °C at 100 %, 123 days of battery, an
- * air quality index of 250, a UV index of 13, PM at 255 µg/m³, pollen at 6 500 grains/m³). */
+ * air quality index of 250, a UV index of 13, PM at 255 µg/m³, pollen at 6 500 grains/m³; from M6d 200 kW from the
+ * roof and 1599 kWh today). Each set also has the solar view (solar_fixtures.h). */
 #define VARIANTS 15
 
 static ui_context_t split_context(int variant)
@@ -270,6 +272,14 @@ static ui_context_t split_context(int variant)
         ctx.lang = lang_get(variant == 12 ? "cs" : "en");
         ctx.lat_e4 = variant == 11 ? 899000 : -899000;
     }
+    /* M6d: the sample day, a battery in every other set; read three hours ago and forecast two days ago in 2, nothing
+     * in 3, the largest values in 14 */
+    ctx.solar = variant == 3    ? fixture_solar_none()
+                : variant == 14 ? fixture_solar_largest(FIX_NOW)
+                                : fixture_solar(variant == 2 ? FIX_NOW - 3 * 3600 : FIX_NOW, true, variant % 2 == 1);
+    if (variant == 2) {
+        s_fix_forecast.fetched = (uint32_t)(FIX_NOW - 50 * 3600);
+    }
     return ctx;
 }
 
@@ -859,6 +869,60 @@ static void test_a_field_without_room_draws_nothing(void)
     TEST_ASSERT_FALSE(inked(0, 399, 0, 299));
 }
 
+/* The house's fields and the forecast's show their own symbols (spec §5.1, D35): the sun for the forecast, the panels
+ * for what they make, the house, the grid's meter and the leaf for own use. */
+static void test_the_solar_fields_show_their_symbols(void)
+{
+    static const struct {
+        ui_field_id_t field;
+        const gfx_bitmap_t *s24, *s16;
+    } k_fields[] = {
+        { UI_FIELD_PV_NOW, &gfx_icon_forecast_24, &gfx_icon_forecast_16 },
+        { UI_FIELD_PV_TODAY, &gfx_icon_forecast_24, &gfx_icon_forecast_16 },
+        { UI_FIELD_EN_PV, &gfx_icon_solar_24, &gfx_icon_solar_16 },
+        { UI_FIELD_EN_YIELD, &gfx_icon_solar_24, &gfx_icon_solar_16 },
+        { UI_FIELD_EN_LOAD, &gfx_icon_house_24, &gfx_icon_house_16 },
+        { UI_FIELD_EN_GRID, &gfx_icon_grid_24, &gfx_icon_grid_16 },
+        { UI_FIELD_EN_EXPORT, &gfx_icon_grid_24, &gfx_icon_grid_16 },
+        { UI_FIELD_EN_SELF, &gfx_icon_self_use_24, &gfx_icon_self_use_16 },
+    };
+    ui_context_t ctx = split_context(0);
+    for (size_t i = 0; i < sizeof(k_fields) / sizeof(k_fields[0]); i++) {
+        gfx_rect_t s = { 267, 231, 133, 69 }, xs = { 200, 278, 200, 22 };
+        gfx_fb_init(&s_fb, s_buf, 400, 300);
+        gfx_clear(&s_fb, GFX_WHITE);
+        ui_draw_cell(&s_fb, s, &ctx, k_fields[i].field, UI_STALE_STALE);
+        ui_draw_cell(&s_fb, xs, &ctx, k_fields[i].field, UI_STALE_STALE);
+        const char *id = ui_field_info(k_fields[i].field)->id;
+        TEST_ASSERT_TRUE_MESSAGE(icon_in(s, s, k_fields[i].s24), id);
+        TEST_ASSERT_TRUE_MESSAGE(icon_in(xs, xs, k_fields[i].s16), id);
+    }
+}
+
+/* The grid's way shows as an arrow in S and XS, up to the grid, down from it; in M its label says it, "Export" or
+ * "Import", without the arrow (spec §5.1). */
+static void test_the_grid_shows_which_way_its_power_goes(void)
+{
+    static uint8_t out[133 * 69], in[133 * 69];
+    ui_context_t ctx = split_context(0);
+    static const int16_t k_cells[][2] = { { 133, 69 }, { 66, 69 }, { 120, 22 } };
+    for (size_t i = 0; i < sizeof(k_cells) / sizeof(k_cells[0]); i++) {
+        int w = k_cells[i][0], h = k_cells[i][1];
+        s_fix_reading.grid_w = -2560;
+        cell_bits(&ctx, UI_FIELD_EN_GRID, w, h, out);
+        s_fix_reading.grid_w = 2560;
+        cell_bits(&ctx, UI_FIELD_EN_GRID, w, h, in);
+        TEST_ASSERT_FALSE(memcmp(out, in, (size_t)(w * h)) == 0);
+    }
+    gfx_rect_t m = { 200, 160, 200, 140 };
+    s_fix_reading.grid_w = -2560;
+    gfx_fb_init(&s_fb, s_buf, 400, 300);
+    gfx_clear(&s_fb, GFX_WHITE);
+    ui_draw_cell(&s_fb, m, &ctx, UI_FIELD_EN_GRID, UI_STALE_STALE);
+    int pen = m.x + 6 + gfx_text_width(&gfx_font_sans_12, "Export");
+    TEST_ASSERT_FALSE(inked(pen + 1, m.x + m.w - 1, m.y, m.y + 6 + gfx_font_sans_12.line_height)); /* no arrow */
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -880,5 +944,7 @@ int main(void)
     RUN_TEST(test_xs_keeps_the_marks_s_shows);
     RUN_TEST(test_every_small_field_fits_every_short_s_cell);
     RUN_TEST(test_short_s_cells_show_a_short_form_before_cutting);
+    RUN_TEST(test_the_solar_fields_show_their_symbols);
+    RUN_TEST(test_the_grid_shows_which_way_its_power_goes);
     return UNITY_END();
 }
```


`test/host/solar_fixtures.h`:

```c
#pragma once

#include <string.h>

#include "context_fixtures.h"
#include "ui_solar.h"

/* The solar view for the ui tests (M6d): the sample day of ui_solar_demo() on the fixture's day, a 5.2 kWp roof
 * facing south in Brno, as the goldens show it. */

#define FIX_MIDNIGHT ((time_t)FIX_DAY * 86400 - 2 * 3600) /* 00:00 CEST */
#define FIX_SOLAR_NOW (FIX_NOW - 7 * 3600 - 28 * 60)      /* 13:20 CEST */

static solar_forecast_t s_fix_forecast;
static energy_reading_t s_fix_reading;
static energy_day_t s_fix_energy_day;
static ui_solar_t s_fix_solar;

/* The sample day until `now`; without `live`, the forecast alone. Fresh for the next sync in `times` mode. */
static inline const ui_solar_t *fixture_solar(time_t now, bool live, bool battery)
{
    ui_solar_demo(FIX_DAY, FIX_MIDNIGHT, now, live, battery, &s_fix_forecast, &s_fix_reading, &s_fix_energy_day);
    s_fix_solar = (ui_solar_t){ .forecast = &s_fix_forecast, .forecast_ttl_s = 26 * 3600, .reading = &s_fix_reading,
                                .day = &s_fix_energy_day, .battery = battery };
    return &s_fix_solar;
}

/* Nothing yet: no forecast, no reading. */
static inline const ui_solar_t *fixture_solar_none(void)
{
    memset(&s_fix_forecast, 0, sizeof(s_fix_forecast));
    memset(&s_fix_reading, 0, sizeof(s_fix_reading));
    energy_day_init(&s_fix_energy_day);
    s_fix_solar = (ui_solar_t){ .forecast = &s_fix_forecast, .reading = &s_fix_reading, .day = &s_fix_energy_day };
    return &s_fix_solar;
}

/* The largest values: two planes of 100 kWp at full power all day, a house drawing 99.99 kW, a battery at 100 %
 * giving 99.99 kW, 199.99 kW to the grid, and a day's totals in MWh. */
static inline const ui_solar_t *fixture_solar_largest(time_t now)
{
    fixture_solar(now, true, true);
    for (int d = 0; d < 2; d++) {
        for (int i = 24; i < 80; i++) {
            s_fix_forecast.q[d][i] = 19999;
        }
        s_fix_forecast.wh[d] = 56 * 19999 * 10 / 4;
    }
    s_fix_forecast.wh[2] = 2799860;
    for (int i = 0; i < ENERGY_STEPS; i++) {
        s_fix_energy_day.q[i] = s_fix_energy_day.q[i] == ENERGY_NONE ? ENERGY_NONE : 19999;
    }
    s_fix_reading.pv_w = 199990;
    s_fix_reading.load_w = 99990;
    s_fix_reading.bat_w = -99990;
    s_fix_reading.grid_w = -199990;
    s_fix_reading.soc = 100;
    s_fix_reading.yield_wh = 1599000;
    s_fix_reading.to_grid_wh = s_fix_energy_day.base_to_wh + 1199000;
    s_fix_reading.from_grid_wh = s_fix_energy_day.base_from_wh + 999000;
    return &s_fix_solar;
}
```


`ui` links `solar_logic` and `energy_logic`; the screen goldens then reach `timekeeping_logic` through them, so they no longer name it (Apple's linker warns about a library named twice):

`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -199,7 +199,7 @@ file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
 add_library(ui STATIC ${UI_SOURCES})
 target_include_directories(ui PUBLIC ${REPO_ROOT}/components/ui/include)
 target_compile_options(ui PRIVATE ${REFLBO_WARNINGS})
-target_link_libraries(ui PUBLIC gfx locale datastore util map radar_logic adsb_logic
+target_link_libraries(ui PUBLIC gfx locale datastore util map radar_logic adsb_logic solar_logic energy_logic
                       PRIVATE cjson scheduler weather_logic astro m)
 
 # reflbo_host_test(<name> <libs...>): builds <name>.c against Unity and registers it with ctest.
@@ -293,12 +293,12 @@ add_executable(render_dashboard render_dashboard.c)
 target_compile_options(render_dashboard PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(render_dashboard PRIVATE ui)
 
-reflbo_host_test(test_ui_screens_golden ui timekeeping_logic)
+reflbo_host_test(test_ui_screens_golden ui) # the time zones through ui's solar and energy
 target_compile_definitions(test_ui_screens_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
 
 add_executable(render_screen render_screen.c)
 target_compile_options(render_screen PRIVATE ${REFLBO_WARNINGS})
-target_link_libraries(render_screen PRIVATE ui timekeeping_logic)
+target_link_libraries(render_screen PRIVATE ui)
 
 # Python tool tests (stdlib unittest; pyserial is not needed).
 find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host --target test_ui_fields test_ui_widget_fit 2>&1 | grep -E 'error:' | sed -E 's/.*error: //' | sort | uniq -c | sort -rn | head -6`
Expected: the view and the fields don't exist yet:

```
   2 'ui_solar.h' file not found
```

- [ ] **Step 3: The icons.** Five more, each at 16, 24 and 48 px, then generate:

`assets/icons/icons.txt`:

```diff
--- a/assets/icons/icons.txt
+++ b/assets/icons/icons.txt
@@ -19,6 +19,13 @@ air            air                   16 24 48
 particles      blur_on               16 24 48
 uv             light_mode            16 24 48
 pollen         local_florist         16 24 48
+# The PV forecast and the house's energy (M6d, D35, D36): a sun for the forecast, panels for what they
+# make, the house, the grid's meter and a leaf for own use.
+forecast       wb_sunny              16 24 48
+solar          solar_power           16 24 48
+house          house                 16 24 48
+grid           electric_meter        16 24 48
+self_use       energy_savings_leaf   16 24 48
 # Weather codes (spec §11), day and night where they differ, and the sun's times (M5); 16 px for XS cells (M6c).
 wx_clear_day        wi:day-sunny                16 24 48
 wx_clear_night      wi:night-clear              16 24 48
```


Run: `tools/gen_icons.sh && git diff --stat plan/m6d -- components/gfx`
Expected: `components/gfx/icons/gfx_icons.c: 120 icons`, and no difference from the branch.

- [ ] **Step 4: The strings.**

`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -199,6 +199,21 @@ typedef enum {
     LS_DIR_SW,
     LS_DIR_W,
     LS_DIR_NW,
+    LS_PV_NOW, /* the PV forecast's fields (spec §11.5, M6d) */
+    LS_PV_TODAY,
+    LS_PV_LEFT,
+    LS_PV_TOMORROW,
+    LS_PV_PEAK,
+    LS_EN_PV, /* the house's energy fields (spec §11.6, M6d) */
+    LS_EN_GRID,
+    LS_EN_LOAD,
+    LS_EN_BATTERY,
+    LS_EN_YIELD,
+    LS_EN_EXPORT,
+    LS_EN_IMPORT,
+    LS_EN_SELF,
+    LS_EN_EXPORTING, /* energy.grid's label in M and up while power goes out, and in */
+    LS_EN_IMPORTING,
     LS_COUNT,
 } lang_str_t;
 
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -210,6 +210,21 @@ const lang_t lang_en = {
         [LS_DIR_SW] = "SW",
         [LS_DIR_W] = "W",
         [LS_DIR_NW] = "NW",
+        [LS_PV_NOW] = "Forecast now",
+        [LS_PV_TODAY] = "Forecast today",
+        [LS_PV_LEFT] = "Still to come",
+        [LS_PV_TOMORROW] = "Forecast tomorrow",
+        [LS_PV_PEAK] = "Peak",
+        [LS_EN_PV] = "Solar",
+        [LS_EN_GRID] = "Grid",
+        [LS_EN_LOAD] = "Home",
+        [LS_EN_BATTERY] = "Home battery",
+        [LS_EN_YIELD] = "Produced today",
+        [LS_EN_EXPORT] = "To grid today",
+        [LS_EN_IMPORT] = "From grid today",
+        [LS_EN_SELF] = "Own use",
+        [LS_EN_EXPORTING] = "Export",
+        [LS_EN_IMPORTING] = "Import",
     },
     .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
     .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -250,6 +250,21 @@ const lang_t lang_cs = {
         [LS_DIR_SW] = "JZ",
         [LS_DIR_W] = "Z",
         [LS_DIR_NW] = "SZ",
+        [LS_PV_NOW] = "Předpověď teď",
+        [LS_PV_TODAY] = "Předpověď dnes",
+        [LS_PV_LEFT] = "Ještě dnes",
+        [LS_PV_TOMORROW] = "Předpověď zítra",
+        [LS_PV_PEAK] = "Špička",
+        [LS_EN_PV] = "FVE",
+        [LS_EN_GRID] = "Síť",
+        [LS_EN_LOAD] = "Dům",
+        [LS_EN_BATTERY] = "Baterie FVE",
+        [LS_EN_YIELD] = "Vyrobeno dnes",
+        [LS_EN_EXPORT] = "Do sítě dnes",
+        [LS_EN_IMPORT] = "Ze sítě dnes",
+        [LS_EN_SELF] = "Vlastní spotřeba",
+        [LS_EN_EXPORTING] = "Přetok",
+        [LS_EN_IMPORTING] = "Odběr",
     },
     .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
     .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
```


- [ ] **Step 5: The view, the fields and the sample day.** `ui` now needs `solar` and `energy`, as it needs `radar` and `adsb` for the radars' views:

`components/ui/include/ui_fields.h`:

```diff
--- a/components/ui/include/ui_fields.h
+++ b/components/ui/include/ui_fields.h
@@ -67,6 +67,19 @@ typedef enum {
     UI_FIELD_POLLEN_RAGWEED,
     UI_FIELD_WX_RAIN2H, /* M6 (D27) */
     UI_FIELD_RAIN_MAP,  /* M6 (D23) */
+    UI_FIELD_PV_NOW,    /* M6d (D35): the PV forecast */
+    UI_FIELD_PV_TODAY,
+    UI_FIELD_PV_LEFT,
+    UI_FIELD_PV_TOMORROW,
+    UI_FIELD_PV_PEAK,
+    UI_FIELD_EN_PV, /* M6d (D36): the house's energy */
+    UI_FIELD_EN_GRID,
+    UI_FIELD_EN_LOAD,
+    UI_FIELD_EN_BATTERY,
+    UI_FIELD_EN_YIELD,
+    UI_FIELD_EN_EXPORT,
+    UI_FIELD_EN_IMPORT,
+    UI_FIELD_EN_SELF,
     UI_FIELD_COUNT,
 } ui_field_id_t;
 
@@ -107,6 +120,7 @@ typedef struct {
 } ui_series_point_t;
 
 typedef struct ui_radar ui_radar_t; /* ui_radar.h */
+typedef struct ui_solar ui_solar_t; /* ui_solar.h */
 
 /* Everything the dashboard reads, gathered by the app for one render. */
 typedef struct {
@@ -124,6 +138,7 @@ typedef struct {
     ui_sync_mark_t sync;    /* the status bar's sync state (spec §5.2) */
     ui_wifi_mark_t wifi;
     const ui_radar_t *radar; /* M6: the radars' map, frames and settings; NULL for none */
+    const ui_solar_t *solar; /* M6d: the PV forecast and the house's energy; NULL for none */
 } ui_context_t;
 
 typedef enum {
@@ -157,6 +172,7 @@ typedef struct {
     uint8_t rain_mm10[UI_RAIN_STEPS]; /* wx.rain2h: each quarter hour from now, as ds_rain_t keeps them */
     uint8_t rain_prob[UI_RAIN_STEPS];
     const ui_radar_t *radar; /* rain.map: what its map draws; `text` is its frame's time */
+    const ui_solar_t *solar; /* pv.* and energy.*: the view they come from */
 } ui_value_t;
 
 void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
```


`components/ui/include/ui_solar.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "energy.h"
#include "gfx.h"
#include "solar.h"
#include "ui_fields.h"

/*
 * The solar view (spec §11.5, §11.6, M6d): what the app gathers for the pv.* and energy.* fields, and the
 * sample day the goldens and `solar demo` show. Pure C, host-buildable.
 */

#define UI_SOLAR_IDLE_W 20 /* less than this flows nowhere: no arrow, a dotted line (spec §11.6) */

struct ui_solar {
    const solar_forecast_t *forecast; /* the PV forecast; NULL, or day 0, before the first */
    uint32_t forecast_ttl_s;          /* fresh this long after it came; 0: never stale (sync mode `manual`) */
    const energy_reading_t *reading;  /* the house's last reading; NULL, or at 0, before the first */
    const energy_day_t *day;          /* today's totals and quarter hours, from the readings */
    bool battery;                     /* the battery shows (energy.battery, spec §11.6) */
};

/* The sample day (spec §15): a 5.2 kWp roof facing south on local day `day`, whose midnight is `midnight` (UTC),
 * its forecast fetched at 05:48: a clear morning with cloud passing in the afternoon, tomorrow overcast, the day
 * after sunny. With `live`, the inverter's readings until `now`: a misty start, then a little above the forecast,
 * and the house drawing 0.86 kW, exporting the rest; with `battery`, a battery at 64 % charging at 1.2 kW. */
void ui_solar_demo(int32_t day, time_t midnight, time_t now, bool live, bool battery, solar_forecast_t *forecast,
                   energy_reading_t *reading, energy_day_t *energy_day);
```


`components/ui/ui_fields.c`:

```diff
--- a/components/ui/ui_fields.c
+++ b/components/ui/ui_fields.c
@@ -40,6 +40,19 @@ static const ui_field_info_t k_fields[UI_FIELD_COUNT] = {
     [UI_FIELD_POLLEN_RAGWEED] = { "pollen.ragweed", UI_FK_POLLEN, LS_POLLEN_RAGWEED, -1 },
     [UI_FIELD_WX_RAIN2H] = { "wx.rain2h", UI_FK_SERIES, LS_RAIN_2H, -1 },
     [UI_FIELD_RAIN_MAP] = { "rain.map", UI_FK_RAIN_MAP, LS_RAIN_MAP, -1 },
+    [UI_FIELD_PV_NOW] = { "pv.now", UI_FK_NUMBER, LS_PV_NOW, -1 },
+    [UI_FIELD_PV_TODAY] = { "pv.today", UI_FK_NUMBER, LS_PV_TODAY, -1 },
+    [UI_FIELD_PV_LEFT] = { "pv.left", UI_FK_NUMBER, LS_PV_LEFT, -1 },
+    [UI_FIELD_PV_TOMORROW] = { "pv.tomorrow", UI_FK_NUMBER, LS_PV_TOMORROW, -1 },
+    [UI_FIELD_PV_PEAK] = { "pv.peak", UI_FK_NUMBER, LS_PV_PEAK, -1 },
+    [UI_FIELD_EN_PV] = { "energy.pv", UI_FK_NUMBER, LS_EN_PV, -1 },
+    [UI_FIELD_EN_GRID] = { "energy.grid", UI_FK_NUMBER, LS_EN_GRID, -1 },
+    [UI_FIELD_EN_LOAD] = { "energy.load", UI_FK_NUMBER, LS_EN_LOAD, -1 },
+    [UI_FIELD_EN_BATTERY] = { "energy.battery", UI_FK_BATTERY, LS_EN_BATTERY, -1 },
+    [UI_FIELD_EN_YIELD] = { "energy.yield", UI_FK_NUMBER, LS_EN_YIELD, -1 },
+    [UI_FIELD_EN_EXPORT] = { "energy.export", UI_FK_NUMBER, LS_EN_EXPORT, -1 },
+    [UI_FIELD_EN_IMPORT] = { "energy.import", UI_FK_NUMBER, LS_EN_IMPORT, -1 },
+    [UI_FIELD_EN_SELF] = { "energy.self", UI_FK_NUMBER, LS_EN_SELF, -1 },
 };
 
 const ui_field_info_t *ui_field_info(ui_field_id_t field)
@@ -215,7 +228,8 @@ void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
     out->label = lang_str(ctx->lang, info->label);
     if (info->ds_field >= 0) {
         resolve_store(ctx, field, out);
-    } else if (!ui_resolve_forecast(ctx, field, out) && !ui_resolve_radar(ctx, field, out)) {
+    } else if (!ui_resolve_forecast(ctx, field, out) && !ui_resolve_radar(ctx, field, out) &&
+               !ui_resolve_solar(ctx, field, out)) {
         resolve_clock(ctx, field, out);
     }
 }
```


`components/ui/ui_internal.h`:

```diff
--- a/components/ui/ui_internal.h
+++ b/components/ui/ui_internal.h
@@ -19,6 +19,8 @@ void ui_fill(const char *pattern, const char *value, char *out, size_t size);
 bool ui_resolve_radar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
 /* Its widget in an M or L slot; false for any other kind. */
 bool ui_radar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v);
+/* pv.* and energy.* (ui_solar.c, M6d); false for any other field. */
+bool ui_resolve_solar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
 
 /* The weather, air quality, pollen and sun fields (ui_forecast.c, D25); false for any other field. */
 bool ui_resolve_forecast(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
```


`components/ui/ui_solar.c`:

```c
#include "ui_solar.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

/* The pv.* and energy.* fields (spec §5.1, §11.5, §11.6, M6d) and the sample day. */

/* "2.31" kW from W, "12.3" from 10 kW up. */
static void kw_text(const lang_t *lang, int32_t w, char *out, size_t size)
{
    long a = w < 0 ? -(long)w : w;
    if (a >= 10000) {
        lang_format_decimal(lang, (a + 50) / 100, 1, out, size);
    } else {
        lang_format_decimal(lang, (a + 5) / 10, 2, out, size);
    }
}

/* "18.4" kWh from Wh. */
static void kwh_text(const lang_t *lang, uint32_t wh, char *out, size_t size)
{
    lang_format_decimal(lang, (long)((wh + 50) / 100), 1, out, size);
}

/* A power and its shorter form where a number doesn't fit (spec §5.1): from 1 kW, one decimal, from 10 kW whole;
 * below 1 kW it would lose most of the value. */
static void set_kw(const lang_t *lang, int32_t w, ui_value_t *out)
{
    kw_text(lang, w, out->text, sizeof(out->text));
    long a = w < 0 ? -(long)w : w;
    if (a >= 10000) {
        snprintf(out->short_text, sizeof(out->short_text), "%ld", (a + 500) / 1000);
    } else if (a >= 1000) {
        lang_format_decimal(lang, (a + 50) / 100, 1, out->short_text, sizeof(out->short_text));
    }
    snprintf(out->unit, sizeof(out->unit), "kW");
}

/* An energy and its shorter form: whole kWh from 10 kWh. */
static void set_kwh(const lang_t *lang, uint32_t wh, ui_value_t *out)
{
    kwh_text(lang, wh, out->text, sizeof(out->text));
    if (wh >= 9950) {
        snprintf(out->short_text, sizeof(out->short_text), "%lu", (unsigned long)((wh + 500) / 1000));
    }
    snprintf(out->unit, sizeof(out->unit), "kWh");
}

/* A quarter hour's start as the clock setting shows it: "12:45", "12:45 PM". */
static void quarter_time(const ui_context_t *ctx, int quarter, char *out, size_t size)
{
    const char *suffix;
    char hm[12];
    lang_format_time(quarter / 4, quarter % 4 * 15, 0, ctx->clock_24h, false, hm, sizeof(hm), &suffix);
    snprintf(out, size, "%s%s%s", hm, suffix[0] ? " " : "", suffix);
}

static int flow_dir(int32_t w)
{
    return w >= UI_SOLAR_IDLE_W ? 1 : w <= -UI_SOLAR_IDLE_W ? -1 : 0;
}

/* pv.*: the forecast's day today, fresh until its wait is over (spec §5.1). */
static void resolve_forecast(const ui_context_t *ctx, const ui_solar_t *s, ui_field_id_t field, ui_value_t *out)
{
    const solar_forecast_t *f = s->forecast;
    if (f == NULL || f->day == 0) {
        return;
    }
    const lang_t *lang = ctx->lang;
    int32_t today = ctx->local_day;
    int quarter = ctx->local.tm_hour * 4 + ctx->local.tm_min / 15;
    int seconds = ctx->local.tm_min % 15 * 60 + ctx->local.tm_sec;
    const uint16_t *q = solar_day(f, today);
    uint32_t wh = SOLAR_WH_NONE;
    switch (field) {
    case UI_FIELD_PV_NOW:
        if (q != NULL) {
            set_kw(lang, q[quarter] * SOLAR_UNIT_W, out);
            out->state = UI_VALUE_FRESH;
        }
        break;
    case UI_FIELD_PV_TODAY:
    case UI_FIELD_PV_TOMORROW:
        wh = solar_day_wh(f, field == UI_FIELD_PV_TODAY ? today : today + 1);
        break;
    case UI_FIELD_PV_LEFT:
        wh = solar_left_wh(f, today, quarter, seconds);
        break;
    case UI_FIELD_PV_PEAK: {
        uint32_t w = 0;
        int at = 0;
        if (q != NULL) {
            if (solar_peak(f, today, &w, &at)) {
                quarter_time(ctx, at, out->extra, sizeof(out->extra));
            }
            set_kw(lang, (int32_t)w, out);
            out->state = UI_VALUE_FRESH;
        }
        break;
    }
    default:
        break;
    }
    if (wh != SOLAR_WH_NONE) {
        set_kwh(lang, wh, out);
        out->state = UI_VALUE_FRESH;
    }
    if (out->state == UI_VALUE_FRESH) {
        out->age_s = (uint32_t)ctx->now > f->fetched ? (uint32_t)ctx->now - f->fetched : 0;
        out->state = s->forecast_ttl_s == 0 || out->age_s <= s->forecast_ttl_s ? UI_VALUE_FRESH : UI_VALUE_STALE;
    }
}

/* energy.*: the last reading, fresh for 15 min; today's totals from a reading of today. */
static void resolve_house(const ui_context_t *ctx, const ui_solar_t *s, ui_field_id_t field, ui_value_t *out)
{
    const energy_reading_t *r = s->reading;
    if (r == NULL || r->at == 0) {
        return;
    }
    const lang_t *lang = ctx->lang;
    int32_t today = ctx->local_day;
    bool of_today = energy_reading_day(r) == today;
    uint32_t wh = ENERGY_WH_NONE;
    out->state = UI_VALUE_FRESH;
    switch (field) {
    case UI_FIELD_EN_PV:
        set_kw(lang, r->pv_w, out);
        break;
    case UI_FIELD_EN_LOAD:
        set_kw(lang, r->load_w, out);
        break;
    case UI_FIELD_EN_GRID: /* the way it goes: the label in M and up, an arrow in S and XS (up: to the grid) */
        set_kw(lang, r->grid_w, out);
        if (flow_dir(r->grid_w) != 0) {
            out->label = lang_str(lang, r->grid_w < 0 ? LS_EN_EXPORTING : LS_EN_IMPORTING);
            out->trend = r->grid_w < 0 ? 1 : -1;
        }
        break;
    case UI_FIELD_EN_BATTERY: /* like the device's: its charge, a bolt while charging, an arrow down while not */
        if (!s->battery || r->soc < 0) {
            out->state = UI_VALUE_MISSING;
            break;
        }
        out->percent = r->soc;
        out->battery = flow_dir(r->bat_w) > 0 ? DS_BAT_CHARGING : DS_BAT_DISCHARGING;
        out->trend = flow_dir(r->bat_w) < 0 ? -1 : 0;
        snprintf(out->text, sizeof(out->text), "%d", r->soc);
        snprintf(out->unit, sizeof(out->unit), "%%");
        if (flow_dir(r->bat_w) != 0) {
            char t[16];
            kw_text(lang, r->bat_w, t, sizeof(t));
            snprintf(out->extra, sizeof(out->extra), "%s kW", t);
        }
        break;
    case UI_FIELD_EN_YIELD:
        wh = of_today ? r->yield_wh : ENERGY_WH_NONE;
        break;
    case UI_FIELD_EN_EXPORT:
        wh = energy_to_grid_wh(s->day, r, today);
        out->trend = 1;
        break;
    case UI_FIELD_EN_IMPORT:
        wh = energy_from_grid_wh(s->day, r, today);
        out->trend = -1;
        break;
    case UI_FIELD_EN_SELF: {
        int pct = energy_self_pct(s->day, r, today);
        if (pct < 0) {
            out->state = UI_VALUE_MISSING;
        } else {
            snprintf(out->text, sizeof(out->text), "%d", pct);
            snprintf(out->unit, sizeof(out->unit), "%%");
        }
        break;
    }
    default:
        break;
    }
    if (field == UI_FIELD_EN_YIELD || field == UI_FIELD_EN_EXPORT || field == UI_FIELD_EN_IMPORT) {
        if (wh == ENERGY_WH_NONE) {
            out->state = UI_VALUE_MISSING;
            out->trend = 0;
        } else {
            set_kwh(lang, wh, out);
        }
    }
    if (out->state == UI_VALUE_FRESH) {
        out->age_s = (uint32_t)ctx->now > r->at ? (uint32_t)ctx->now - r->at : 0;
        out->state = energy_fresh(r, (uint32_t)ctx->now) ? UI_VALUE_FRESH : UI_VALUE_STALE;
    }
}

bool ui_resolve_solar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    if (field < UI_FIELD_PV_NOW || field > UI_FIELD_EN_SELF) {
        return false;
    }
    const ui_solar_t *s = ctx->solar;
    out->solar = s;
    if (s == NULL) {
        return true;
    }
    if (field <= UI_FIELD_PV_PEAK) {
        resolve_forecast(ctx, s, field, out);
    } else {
        resolve_house(ctx, s, field, out);
    }
    return true;
}

/* ---- the sample day ---- */

#define PI 3.14159265358979323846
#define DEMO_PEAK_W 4120.0
#define DEMO_RISE_H 6.75
#define DEMO_SET_H 18.85

/* The forecast's mean power for quarter hour `i` of day `d` (0 today, 1 tomorrow), in W. */
static double demo_w(int d, int i)
{
    double t = (i + 0.5) / 4.0;
    if (t <= DEMO_RISE_H || t >= DEMO_SET_H) {
        return 0;
    }
    double w = DEMO_PEAK_W * pow(sin(PI * (t - DEMO_RISE_H) / (DEMO_SET_H - DEMO_RISE_H)), 1.25);
    if (d == 0 && t > 15.0 && t < 17.0) {
        w *= 0.55 + 0.25 * sin((t - 15.0) * 6.0); /* cloud */
    }
    if (d == 1) {
        w *= 0.38 + 0.12 * sin(t * 2.1); /* overcast */
    }
    return w;
}

void ui_solar_demo(int32_t day, time_t midnight, time_t now, bool live, bool battery, solar_forecast_t *forecast,
                   energy_reading_t *reading, energy_day_t *energy_day)
{
    memset(forecast, 0, sizeof(*forecast));
    forecast->day = day;
    forecast->fetched = (uint32_t)(midnight + 5 * 3600 + 48 * 60);
    for (int d = 0; d < 2; d++) {
        uint32_t q_sum = 0;
        for (int i = 0; i < SOLAR_STEPS; i++) {
            forecast->q[d][i] = (uint16_t)lround(demo_w(d, i) / SOLAR_UNIT_W);
            q_sum += forecast->q[d][i];
        }
        forecast->wh[d] = (uint32_t)lround(q_sum * (SOLAR_UNIT_W / 4.0));
    }
    forecast->wh[2] = 21400;
    memset(reading, 0, sizeof(*reading));
    energy_day_init(energy_day);
    if (!live) {
        return;
    }
    double yield = 0;
    for (int i = 0; i < SOLAR_STEPS && midnight + (i + 1) * 900 <= now; i++) {
        double f = demo_w(0, i), a = i < 38 ? f * 0.7 : f * (1.04 + 0.03 * sin(i * 1.7)); /* mist, then sun */
        energy_day->q[i] = (uint16_t)lround(a / ENERGY_UNIT_W);
        energy_day->n[i] = 3;
        yield += a / 4.0;
    }
    uint32_t yield_wh = (uint32_t)lround(yield);
    uint32_t exported = battery ? yield_wh * 38 / 100 : yield_wh * 62 / 100;
    *reading = (energy_reading_t){
        .at = (uint32_t)(now - 180),
        .pv_w = 3420,
        .load_w = 860,
        .bat_w = battery ? 1200 : 0,
        .soc = (int16_t)(battery ? 64 : -1),
        .inverter = battery ? 5 : 4, /* X3-Hybrid, X1-Boost */
        .yield_wh = yield_wh,
        .to_grid_wh = 1000000 + exported,
        .from_grid_wh = 2000000 + (battery ? 600 : 1400),
    };
    reading->grid_w = -(reading->pv_w - reading->load_w - reading->bat_w);
    energy_day->day = day;
    energy_day->base_at = (uint32_t)(midnight + 120);
    energy_day->base_to_wh = 1000000;
    energy_day->base_from_wh = 2000000;
    energy_day->last_at = reading->at;
}
```


`components/ui/CMakeLists.txt`:

```diff
--- a/components/ui/CMakeLists.txt
+++ b/components/ui/CMakeLists.txt
@@ -2,7 +2,7 @@
 idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                             "ui_status.c" "ui_dashboard.c" "ui_schedule.c" "ui_menu.c" "ui_menu_draw.c"
                             "ui_screens.c" "ui_config.c" "ui_catalog.c" "ui_forecast.c" "ui_radar.c"
-                            "ui_flights.c" "ui_split.c"
+                            "ui_flights.c" "ui_split.c" "ui_solar.c"
                        INCLUDE_DIRS "include"
-                       REQUIRES gfx locale datastore util map radar adsb
+                       REQUIRES gfx locale datastore util map radar adsb solar energy
                        PRIV_REQUIRES json scheduler weather astro)
```


- [ ] **Step 6: Their symbols, and the grid's label without its arrow.**

`components/ui/ui_widget.c`:

```diff
--- a/components/ui/ui_widget.c
+++ b/components/ui/ui_widget.c
@@ -98,6 +98,28 @@ static const gfx_bitmap_t *field_icon(ui_field_id_t field, int size)
     case UI_FIELD_POLLEN_RAGWEED:
         s16 = &gfx_icon_pollen_16, s24 = &gfx_icon_pollen_24, s48 = &gfx_icon_pollen_48;
         break;
+    case UI_FIELD_PV_NOW: /* the forecast: a sun; what the panels make: the panels (spec §5.1, D35) */
+    case UI_FIELD_PV_TODAY:
+    case UI_FIELD_PV_LEFT:
+    case UI_FIELD_PV_TOMORROW:
+    case UI_FIELD_PV_PEAK:
+        s16 = &gfx_icon_forecast_16, s24 = &gfx_icon_forecast_24, s48 = &gfx_icon_forecast_48;
+        break;
+    case UI_FIELD_EN_PV:
+    case UI_FIELD_EN_YIELD:
+        s16 = &gfx_icon_solar_16, s24 = &gfx_icon_solar_24, s48 = &gfx_icon_solar_48;
+        break;
+    case UI_FIELD_EN_LOAD:
+        s16 = &gfx_icon_house_16, s24 = &gfx_icon_house_24, s48 = &gfx_icon_house_48;
+        break;
+    case UI_FIELD_EN_GRID:
+    case UI_FIELD_EN_EXPORT:
+    case UI_FIELD_EN_IMPORT:
+        s16 = &gfx_icon_grid_16, s24 = &gfx_icon_grid_24, s48 = &gfx_icon_grid_48;
+        break;
+    case UI_FIELD_EN_SELF:
+        s16 = &gfx_icon_self_use_16, s24 = &gfx_icon_self_use_24, s48 = &gfx_icon_self_use_48;
+        break;
     default:
         break;
     }
@@ -817,11 +839,13 @@ static void draw_labelled(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_v
     int top = r.y + 6;
     if (f->label != NULL && v->kind != UI_FK_TIME && !(size == UI_SIZE_M && v->kind == UI_FK_DATE)) {
         char label[40];
-        int arrow_w = v->trend ? gfx_text_width(f->label, ARROW_UP) + 4 : 0;
+        bool says = v->field == UI_FIELD_EN_GRID || v->field == UI_FIELD_EN_EXPORT || v->field == UI_FIELD_EN_IMPORT;
+        int trend = says ? 0 : v->trend; /* "Export" or "To grid today" says which way */
+        int arrow_w = trend ? gfx_text_width(f->label, ARROW_UP) + 4 : 0;
         gfx_text_ellipsize(f->label, v->label, r.w - 12 - arrow_w, label, sizeof(label));
         int pen = gfx_text(fb, f->label, r.x + 6, top + f->label->ascent, label, GFX_BLACK);
-        if (v->trend) { /* beside the label, where it doesn't widen the value */
-            gfx_text(fb, f->label, pen + 4, top + f->label->ascent, v->trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
+        if (trend) { /* beside the label, where it doesn't widen the value */
+            gfx_text(fb, f->label, pen + 4, top + f->label->ascent, trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
         }
         top += f->label->line_height;
     }
```


- [ ] **Step 7: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_ui_fields | tail -2 && ./build-host/test_ui_widget_fit | tail -2 && ctest --test-dir build-host | tail -3`
Expected (the fit test takes about 40 s now):

```
29 Tests 0 Failures 0 Ignored
OK
20 Tests 0 Failures 0 Ignored
OK
100% tests passed, 0 tests failed out of 63
```

- [ ] **Step 8: The firmware builds:** `tools/idf.sh build`, clean, without a warning.

- [ ] **Step 9: Commit.**

```bash
git add assets/icons components/gfx components/locale components/ui test/host/CMakeLists.txt \
  test/host/solar_fixtures.h test/host/test_ui_fields.c test/host/test_ui_widget_fit.c
git commit -m "feat(ui): the pv.* and energy.* fields at every size"
```

### Task 6: The chart and the flow (`ui`)

**Files:**
- Create: `test/host/test_ui_solar.c`
- Modify: `components/locale/include/lang.h`, `components/locale/lang_en.c`, `components/locale/lang_cs.c`, `components/ui/include/ui_fields.h`, `components/ui/include/ui_layout.h`, `components/ui/ui_catalog.c`, `components/ui/ui_fields.c`, `components/ui/ui_internal.h`, `components/ui/ui_solar.c`, `components/ui/ui_widget.c`, `test/host/CMakeLists.txt`, `test/host/dashboard_fixtures.h`
- Test: `test/host/test_ui_solar.c`, `test/host/test_ui_catalog.c`, `test/host/test_ui_dashboard_golden.c` with four goldens (copied from the branch)

**Interfaces:**
- Consumes: Task 5's view, `ui_resolve_solar()`, `set_kw()`'s short forms, `fixture_solar()`.
- Produces (Task 7):
  - kinds `UI_FK_CHART` and `UI_FK_FLOW` ("chart", "flow"), in `UI_KINDS_M` and `UI_KINDS_L` only; fields `UI_FIELD_PV_CHART` (`pv.chart`) and `UI_FIELD_EN_FLOW` (`energy.flow`);
  - in `ui_value_t`: `quarter`, `minute` (the chart's now), `chart_q` (today's forecast quarter hours) and `chart_read` (the readings' means, NULL without readings);
  - `bool ui_solar_widget(fb, r, size, v, lang)` (ui_internal.h); in `ui_solar.c` the drawing Task 7's layouts reuse: `draw_chart()`, `flow_line()`, `arrow()`, `heading()`, `centred()`, `kw_fit()`, `soc_fit()`;
  - `fixture_solar_ctx(ctx, live, battery)` (dashboard_fixtures.h): 13:20 CEST with the sun out.

The chart (spec §5.3) shows today's forecast as bars from the first hour with output to the last, a quarter hour a bar or an hour where a quarter's bar would be under 3 px; the past solid, the rest outlined; with readings the past bars are what was produced and a line over them the forecast; a dashed line and a mark at now; in L kW on the left and every third hour below. Its heading is the sun and today's total, its label right where it fits. The flow has its heading with "kW" at the right: from 76 px under the heading a diagram (the panels above a junction, the grid left, the house right, the battery below from 100 px), else a row (a battery at its end from 180 px of width, with a bolt while it charges); arrows where at least 20 W flow, dotted lines where less does.

- [ ] **Step 1: Write the failing tests.** Where they draw; the flow's diagram and row; the battery where it has room; no arrow under 20 W; missing without data:

`test/host/test_ui_solar.c`:

```c
#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <stdio.h>
#include <string.h>

#include "dashboard_fixtures.h"
#include "gfx.h"
#include "gfx_icons.h"
#include "ui_split.h"
#include "unity.h"

/* The chart and the flow (spec §5.3, M6d): where they draw, the flow's diagram and row, its battery and arrows. The
 * goldens (dash_grid_solar, dash_weather_solar, dash_focus_solar) show them whole. */

static uint8_t s_buf[400 * 300 / 8];
static gfx_fb_t s_fb;
static ui_context_t s_ctx;

void setUp(void)
{
    s_ctx = fixture_context();
    ui_preset_t preset;
    fixture_dashboard("weather_solar", &s_ctx, &preset); /* 13:20, the sample day with a battery */
}

void tearDown(void) {}

static void draw(gfx_rect_t r, ui_field_id_t field)
{
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    gfx_clear(&s_fb, GFX_WHITE);
    ui_draw_cell(&s_fb, r, &s_ctx, field, UI_STALE_STALE);
}

/* Where `icon` is drawn whole in `area`, a blank pixel or the edge all round it; false if nowhere. */
static bool find_icon(gfx_rect_t area, const gfx_bitmap_t *icon, int *x, int *y)
{
    int rb = (icon->width + 7) / 8;
    for (int y0 = area.y; y0 + icon->height <= area.y + area.h; y0++) {
        for (int x0 = area.x; x0 + icon->width <= area.x + area.w; x0++) {
            bool same = true;
            for (int iy = 0; same && iy < icon->height; iy++) {
                for (int ix = 0; same && ix < icon->width; ix++) {
                    bool ink = (icon->bits[iy * rb + ix / 8] >> (7 - ix % 8)) & 1;
                    same = gfx_get_pixel(&s_fb, x0 + ix, y0 + iy) == ink;
                }
            }
            if (same) {
                *x = x0;
                *y = y0;
                return true;
            }
        }
    }
    return false;
}

static void snapshot(gfx_rect_t r, uint8_t *out)
{
    for (int y = 0; y < r.h; y++) {
        for (int x = 0; x < r.w; x++) {
            out[y * r.w + x] = (uint8_t)gfx_get_pixel(&s_fb, r.x + x, r.y + y);
        }
    }
}

static void test_the_chart_and_the_flow_need_m_or_more(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_CHART, 129, 279)); /* S at most */
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_FLOW, 400, 79));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_CHART, 130, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_FLOW, 130, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_field_size(UI_FK_CHART, 400, 279));
}

/* In a tall cell the panels sit above the junction, the grid left of it and the house right; in a short one all
 * of them in a row. */
static void test_the_flow_is_a_diagram_in_a_tall_cell_and_a_row_in_a_short_one(void)
{
    gfx_rect_t tall = { 200, 21, 199, 139 }, short_cell = { 200, 21, 199, 80 };
    int px, py, hx, hy, gx, gy;
    draw(tall, UI_FIELD_EN_FLOW);
    TEST_ASSERT_TRUE(find_icon(tall, &gfx_icon_solar_24, &px, &py));
    TEST_ASSERT_TRUE(find_icon(tall, &gfx_icon_house_24, &hx, &hy));
    TEST_ASSERT_TRUE(find_icon(tall, &gfx_icon_grid_24, &gx, &gy));
    TEST_ASSERT_TRUE(py + 24 < hy);       /* the panels above */
    TEST_ASSERT_EQUAL_INT(hy, gy);        /* the grid and the house level */
    TEST_ASSERT_TRUE(gx < px && px < hx); /* the grid left, the house right */
    draw(short_cell, UI_FIELD_EN_FLOW);
    TEST_ASSERT_TRUE(find_icon(short_cell, &gfx_icon_solar_24, &px, &py));
    TEST_ASSERT_TRUE(find_icon(short_cell, &gfx_icon_house_24, &hx, &hy));
    TEST_ASSERT_TRUE(find_icon(short_cell, &gfx_icon_grid_24, &gx, &gy));
    TEST_ASSERT_EQUAL_INT(py, hy); /* one row: the panels, the house, the grid */
    TEST_ASSERT_EQUAL_INT(py, gy);
    TEST_ASSERT_TRUE(px < hx && hx < gx);
}

/* The battery joins the diagram from 100 px under the heading (22 px in M), and the row from 180 px of width. */
static void test_the_battery_joins_the_flow_where_it_has_room(void)
{
    static uint8_t with[200 * 150], without[200 * 150];
    static const struct {
        int16_t w, h;
        bool joins;
    } k_cells[] = { { 199, 139, true }, { 200, 121, false }, { 200, 122, true },
                    { 199, 80, true },  { 179, 80, false },  { 133, 139, true } };
    for (size_t i = 0; i < sizeof(k_cells) / sizeof(k_cells[0]); i++) {
        gfx_rect_t r = { 0, 21, k_cells[i].w, k_cells[i].h };
        s_fix_solar.battery = true;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, with);
        s_fix_solar.battery = false;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, without);
        char msg[32];
        snprintf(msg, sizeof(msg), "%d×%d", r.w, r.h);
        bool differ = memcmp(with, without, (size_t)(r.w * r.h)) != 0;
        TEST_ASSERT_EQUAL_MESSAGE(k_cells[i].joins, differ, msg);
    }
}

/* Less than 20 W flows nowhere: no arrow, so its way makes no difference; from 20 W the arrow shows it. */
static void test_a_flow_under_20_watts_has_no_arrow(void)
{
    static uint8_t a[200 * 150], b[200 * 150];
    gfx_rect_t cells[] = { { 0, 21, 199, 139 }, { 0, 21, 199, 80 } };
    for (size_t i = 0; i < sizeof(cells) / sizeof(cells[0]); i++) {
        gfx_rect_t r = cells[i];
        s_fix_reading.grid_w = 15;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, a);
        s_fix_reading.grid_w = -15;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, b);
        TEST_ASSERT_EQUAL_MEMORY(a, b, (size_t)(r.w * r.h));
        s_fix_reading.grid_w = 25;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, a);
        s_fix_reading.grid_w = -25;
        draw(r, UI_FIELD_EN_FLOW);
        snapshot(r, b);
        TEST_ASSERT_FALSE(memcmp(a, b, (size_t)(r.w * r.h)) == 0);
    }
}

/* Without a reading the flow is missing, and without today's quarter hours the chart is. */
static void test_without_data_the_chart_and_the_flow_are_missing(void)
{
    ui_value_t v;
    ui_resolve(&s_ctx, UI_FIELD_PV_CHART, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    TEST_ASSERT_EQUAL_STRING("27.4", v.text); /* today's total, its heading */
    ui_resolve(&s_ctx, UI_FIELD_EN_FLOW, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
    s_fix_reading.at = 0;
    ui_resolve(&s_ctx, UI_FIELD_EN_FLOW, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
    s_ctx.local_day += 2; /* the forecast has no quarter hours for that day */
    ui_resolve(&s_ctx, UI_FIELD_PV_CHART, &v);
    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_chart_and_the_flow_need_m_or_more);
    RUN_TEST(test_the_flow_is_a_diagram_in_a_tall_cell_and_a_row_in_a_short_one);
    RUN_TEST(test_the_battery_joins_the_flow_where_it_has_room);
    RUN_TEST(test_a_flow_under_20_watts_has_no_arrow);
    RUN_TEST(test_without_data_the_chart_and_the_flow_are_missing);
    return UNITY_END();
}
```


The published rules: the chart and the flow in M and L, not in S or XS:

`test/host/test_ui_catalog.c`:

```diff
--- a/test/host/test_ui_catalog.c
+++ b/test/host/test_ui_catalog.c
@@ -80,8 +80,10 @@ static void test_layouts_list_their_slots_with_rectangles_sizes_and_kinds(void)
     const cJSON *hourly = by_id(cJSON_GetObjectItemCaseSensitive(by_id(layouts, "weather"), "slots"), "hourly");
     TEST_ASSERT_EQUAL_STRING("M", str(hourly, "size"));
     const cJSON *hourly_kinds = cJSON_GetObjectItemCaseSensitive(hourly, "kinds");
-    const cJSON *last = cJSON_GetArrayItem(hourly_kinds, cJSON_GetArraySize(hourly_kinds) - 1);
-    TEST_ASSERT_EQUAL_STRING("rain_map", last->valuestring); /* a medium slot takes the rain map */
+    int n = cJSON_GetArraySize(hourly_kinds); /* a medium slot takes the rain map, the chart and the flow (M6d) */
+    TEST_ASSERT_EQUAL_STRING("rain_map", cJSON_GetArrayItem(hourly_kinds, n - 3)->valuestring);
+    TEST_ASSERT_EQUAL_STRING("chart", cJSON_GetArrayItem(hourly_kinds, n - 2)->valuestring);
+    TEST_ASSERT_EQUAL_STRING("flow", cJSON_GetArrayItem(hourly_kinds, n - 1)->valuestring);
 }
 
 /* The size a field draws at by the published rules, as the editor reads them (web/app.js). */
@@ -145,10 +147,15 @@ static void test_layouts_publish_the_split_rules(void)
     TEST_ASSERT_EQUAL_INT(49, cJSON_GetArrayItem(sun, 0)->valueint);
     TEST_ASSERT_EQUAL_INT(49, cJSON_GetArrayItem(sun, 1)->valueint);
     TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(s, "kinds"), "series"));
+    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(s, "kinds"), "chart"));
+    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(xs, "kinds"), "flow"));
+    const cJSON *m = cJSON_GetArrayItem(sizes, 2); /* the chart and the flow need M or larger (spec §5.1) */
+    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(m, "kinds"), "chart"));
+    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(m, "kinds"), "flow"));
     /* the rules give what the renderer does, everywhere */
     static const char *const k_kinds[UI_FK_COUNT] = { "time", "date", "number", "battery", "moon", "text",
                                                       "weather_now", "weather_day", "series", "sun", "level",
-                                                      "pollen", "rain_map" };
+                                                      "pollen", "rain_map", "chart", "flow" };
     static const char *const k_names[] = { "XS", "S", "M", "L", "XL" };
     for (int k = 0; k < UI_FK_COUNT; k++) {
         for (int w = UI_SPLIT_MIN_W; w <= 400; w++) { /* every size, so no boundary falls between two */
```


The goldens of the spike's renders with the chart and the flow in Grid, Weather and Focus, and the house's numbers in Classic's small slots:

`test/host/dashboard_fixtures.h`:

```diff
--- a/test/host/dashboard_fixtures.h
+++ b/test/host/dashboard_fixtures.h
@@ -4,6 +4,7 @@
 
 #include "context_fixtures.h"
 #include "radar_fixtures.h"
+#include "solar_fixtures.h"
 #include "ui_dashboard.h"
 
 /* The dashboard fixtures for the golden renders (test_ui_dashboard_golden.c, render_dashboard.c):
@@ -79,6 +80,23 @@ static const uint8_t k_fixture_small24[24] = {
     UI_FIELD_POLLEN_BIRCH,  UI_FIELD_POLLEN_ALDER, UI_FIELD_POLLEN_MUGWORT, UI_FIELD_POLLEN_RAGWEED,
 };
 
+/* M6d: 13:20 CEST on the fixture's day, the sun out, the sample day of ui_solar_demo(). */
+static inline void fixture_solar_ctx(ui_context_t *ctx, bool live, bool battery)
+{
+    ctx->now = FIX_SOLAR_NOW;
+    ctx->local = fixture_local(13, 20, 0);
+    fixture_fill(&s_fix_ds, ctx->now);
+    fixture_forecast(&s_fix_ds, ctx->now - 3600);
+    static ds_weather_t w;
+    w = *ds_weather(&s_fix_ds);
+    w.now_is_day = 1;
+    w.now_code = 1;
+    w.now_time = (uint32_t)(ctx->now - 20 * 60);
+    w.now_temp_c10 = 196;
+    ds_set_weather(&s_fix_ds, &w);
+    ctx->solar = fixture_solar(ctx->now, live, battery);
+}
+
 /* Each fixture: a context and a preset. */
 static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_preset_t *preset)
 {
@@ -349,6 +367,30 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
         preset->slots[0] = UI_FIELD_TIME_CLOCK;
         ctx->clock_24h = false;
         ctx->local = fixture_local(12, 58, 0);
+    } else if (strcmp(name, "home_energy") == 0) { /* M6d: Classic's small slots, the house now */
+        *preset = fixture_preset("home");
+        preset->slots[2] = UI_FIELD_EN_PV;
+        preset->slots[3] = UI_FIELD_EN_GRID;
+        preset->slots[4] = UI_FIELD_EN_LOAD;
+        preset->slots[5] = UI_FIELD_PV_TODAY;
+        fixture_solar_ctx(ctx, true, false);
+    } else if (strcmp(name, "grid_solar") == 0) { /* Grid's medium cells: the chart, the flow, the numbers */
+        *preset = fixture_preset("indoor");
+        static const uint8_t k_slots[6] = { UI_FIELD_PV_CHART, UI_FIELD_EN_FLOW, UI_FIELD_PV_LEFT,
+                                            UI_FIELD_EN_GRID,  UI_FIELD_EN_SELF, UI_FIELD_PV_TOMORROW };
+        memcpy(preset->slots, k_slots, sizeof(k_slots));
+        fixture_solar_ctx(ctx, true, false);
+    } else if (strcmp(name, "weather_solar") == 0) { /* the chart in Weather's large slot, the flow beside */
+        *preset = fixture_preset("weather");
+        static const uint8_t k_slots[5] = { UI_FIELD_PV_CHART, UI_FIELD_WX_TODAY, UI_FIELD_EN_FLOW, UI_FIELD_EN_PV,
+                                            UI_FIELD_EN_BATTERY };
+        memcpy(preset->slots, k_slots, sizeof(k_slots));
+        fixture_solar_ctx(ctx, true, true);
+    } else if (strcmp(name, "focus_solar") == 0) { /* the clock, with the chart and the flow under it */
+        *preset = fixture_preset("focus");
+        preset->slots[1] = UI_FIELD_PV_CHART;
+        preset->slots[2] = UI_FIELD_EN_FLOW;
+        fixture_solar_ctx(ctx, true, false);
     } else {
         return false;
     }
@@ -373,4 +415,5 @@ static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather",
                                                     "split_eight", "home_temp_main", "home_temp_main_cs",
                                                     "weather_frost_cs", "weather_hot_f", "split_compact",
                                                     "split_compact_cs", "split_xs_rows", "split_xs_grid",
-                                                    "split_xs_narrow" };
+                                                    "split_xs_narrow", "home_energy", "grid_solar",
+                                                    "weather_solar", "focus_solar" };
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -274,6 +274,7 @@ reflbo_host_test(test_ui_menu ui)
 reflbo_host_test(test_ui_config ui)
 reflbo_host_test(test_ui_catalog ui cjson)
 reflbo_host_test(test_ui_flights ui)
+reflbo_host_test(test_ui_solar ui)
 reflbo_host_test(test_settings storage_logic)
 reflbo_host_test(test_storage_file storage_logic)
 target_compile_definitions(test_storage_file PRIVATE TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/storage_tmp")
```


```bash
git checkout plan/m6d -- \
  test/host/golden/dash_focus_solar.pbm \
  test/host/golden/dash_grid_solar.pbm \
  test/host/golden/dash_home_energy.pbm \
  test/host/golden/dash_weather_solar.pbm
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host --target test_ui_solar test_ui_dashboard_golden 2>&1 | grep -E 'error:' | sed -E 's/.*error: //' | sort | uniq -c | sort -rn | head -6`
Expected:

```
  14 use of undeclared identifier 'UI_FIELD_EN_FLOW'; did you mean 'UI_FIELD_EN_LOAD'?
   6 use of undeclared identifier 'UI_FIELD_PV_CHART'
   3 use of undeclared identifier 'UI_FK_CHART'; did you mean 'UI_FK_COUNT'?
   2 use of undeclared identifier 'UI_FK_FLOW'; did you mean 'UI_FK_MOON'?
   1 too many errors emitted, stopping now [-ferror-limit=]
```

- [ ] **Step 3: The kinds, the fields and the strings.**

`components/ui/include/ui_fields.h`:

```diff
--- a/components/ui/include/ui_fields.h
+++ b/components/ui/include/ui_fields.h
@@ -29,6 +29,8 @@ typedef enum {
     UI_FK_LEVEL,  /* a number and its band: the air quality index (D25) */
     UI_FK_POLLEN, /* a pollen level, and its type or count (D25) */
     UI_FK_RAIN_MAP, /* the weather radar's map (M6) */
+    UI_FK_CHART,    /* the day's PV forecast as bars (M6d) */
+    UI_FK_FLOW,     /* the house's energy flow (M6d) */
     UI_FK_COUNT,
 } ui_field_kind_t;
 
@@ -80,6 +82,8 @@ typedef enum {
     UI_FIELD_EN_EXPORT,
     UI_FIELD_EN_IMPORT,
     UI_FIELD_EN_SELF,
+    UI_FIELD_PV_CHART, /* M6d: the day's forecast and the readings, as bars */
+    UI_FIELD_EN_FLOW,  /* M6d: the house's energy flow now */
     UI_FIELD_COUNT,
 } ui_field_id_t;
 
@@ -173,6 +177,9 @@ typedef struct {
     uint8_t rain_prob[UI_RAIN_STEPS];
     const ui_radar_t *radar; /* rain.map: what its map draws; `text` is its frame's time */
     const ui_solar_t *solar; /* pv.* and energy.*: the view they come from */
+    int quarter, minute;     /* pv.chart: the local quarter hour now and the minute */
+    const uint16_t *chart_q;    /* pv.chart: today's forecast by quarter hour, in tens of W */
+    const uint16_t *chart_read; /* and the readings' means (0xFFFF where none came); NULL without readings */
 } ui_value_t;
 
 void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
```


`components/ui/include/ui_layout.h`:

```diff
--- a/components/ui/include/ui_layout.h
+++ b/components/ui/include/ui_layout.h
@@ -35,8 +35,9 @@ typedef enum {
      UI_KIND(UI_FK_MOON) | UI_KIND(UI_FK_TEXT) | UI_KIND(UI_FK_WEATHER_NOW) | UI_KIND(UI_FK_WEATHER_DAY) |           \
      UI_KIND(UI_FK_SUN) | UI_KIND(UI_FK_LEVEL) | UI_KIND(UI_FK_POLLEN))
 #define UI_KINDS_XS UI_KINDS_S
-#define UI_KINDS_M (UI_KINDS_S | UI_KIND(UI_FK_SERIES) | UI_KIND(UI_FK_RAIN_MAP))
-#define UI_KINDS_L (UI_KINDS_S | UI_KIND(UI_FK_RAIN_MAP))
+#define UI_KINDS_M                                                                                                   \
+    (UI_KINDS_S | UI_KIND(UI_FK_SERIES) | UI_KIND(UI_FK_RAIN_MAP) | UI_KIND(UI_FK_CHART) | UI_KIND(UI_FK_FLOW))
+#define UI_KINDS_L (UI_KINDS_S | UI_KIND(UI_FK_RAIN_MAP) | UI_KIND(UI_FK_CHART) | UI_KIND(UI_FK_FLOW))
 #define UI_KINDS_XL (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_NUMBER))
 
 typedef struct {
```


`components/ui/ui_catalog.c`:

```diff
--- a/components/ui/ui_catalog.c
+++ b/components/ui/ui_catalog.c
@@ -24,8 +24,10 @@ static const char *const k_kinds[UI_FK_COUNT] = {
     [UI_FK_LEVEL] = "level",
     [UI_FK_POLLEN] = "pollen",
     [UI_FK_RAIN_MAP] = "rain_map",
+    [UI_FK_CHART] = "chart",
+    [UI_FK_FLOW] = "flow",
 };
-_Static_assert(UI_FK_COUNT == 13, "every kind has a name in the catalogue");
+_Static_assert(UI_FK_COUNT == 15, "every kind has a name in the catalogue");
 static const char *const k_sizes[] = { [UI_SIZE_XS] = "XS", [UI_SIZE_S] = "S", [UI_SIZE_M] = "M",
                                        [UI_SIZE_L] = "L", [UI_SIZE_XL] = "XL" };
 
```


`components/ui/ui_fields.c`:

```diff
--- a/components/ui/ui_fields.c
+++ b/components/ui/ui_fields.c
@@ -53,6 +53,8 @@ static const ui_field_info_t k_fields[UI_FIELD_COUNT] = {
     [UI_FIELD_EN_EXPORT] = { "energy.export", UI_FK_NUMBER, LS_EN_EXPORT, -1 },
     [UI_FIELD_EN_IMPORT] = { "energy.import", UI_FK_NUMBER, LS_EN_IMPORT, -1 },
     [UI_FIELD_EN_SELF] = { "energy.self", UI_FK_NUMBER, LS_EN_SELF, -1 },
+    [UI_FIELD_PV_CHART] = { "pv.chart", UI_FK_CHART, LS_PV_CHART, -1 },
+    [UI_FIELD_EN_FLOW] = { "energy.flow", UI_FK_FLOW, LS_EN_FLOW, -1 },
 };
 
 const ui_field_info_t *ui_field_info(ui_field_id_t field)
```


`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -214,6 +214,8 @@ typedef enum {
     LS_EN_SELF,
     LS_EN_EXPORTING, /* energy.grid's label in M and up while power goes out, and in */
     LS_EN_IMPORTING,
+    LS_PV_CHART, /* pv.chart and energy.flow (spec §5.3) */
+    LS_EN_FLOW,
     LS_COUNT,
 } lang_str_t;
 
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -225,6 +225,8 @@ const lang_t lang_en = {
         [LS_EN_SELF] = "Own use",
         [LS_EN_EXPORTING] = "Export",
         [LS_EN_IMPORTING] = "Import",
+        [LS_PV_CHART] = "Solar forecast",
+        [LS_EN_FLOW] = "Energy flow",
     },
     .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
     .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -265,6 +265,8 @@ const lang_t lang_cs = {
         [LS_EN_SELF] = "Vlastní spotřeba",
         [LS_EN_EXPORTING] = "Přetok",
         [LS_EN_IMPORTING] = "Odběr",
+        [LS_PV_CHART] = "Předpověď FVE",
+        [LS_EN_FLOW] = "Toky energie",
     },
     .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
     .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
```


- [ ] **Step 4: The widgets.** Ported from the spike onto the real data: the forecast's quarter hours in tens of W and the readings' means. The fit test's largest values (200 kW) need three things the spike didn't have: the axis labels' margin follows the widest label, the "now" mark stays inside the plot, and the heading's total steps to its short form ("2800 kWh"), then drops its unit, before a cut; the flow's values take their short forms where a column is too narrow ("200"), and "64 %" becomes "64%".

`components/ui/ui_solar.c`:

```diff
--- a/components/ui/ui_solar.c
+++ b/components/ui/ui_solar.c
@@ -4,9 +4,12 @@
 #include <stdio.h>
 #include <string.h>
 
+#include "gfx_fonts.h"
+#include "gfx_icons.h"
 #include "ui_internal.h"
 
-/* The pv.* and energy.* fields (spec §5.1, §11.5, §11.6, M6d) and the sample day. */
+/* The pv.* and energy.* fields (spec §5.1, §11.5, §11.6, M6d), the chart and the flow (spec §5.3), and the
+ * sample day. */
 
 /* "2.31" kW from W, "12.3" from 10 kW up. */
 static void kw_text(const lang_t *lang, int32_t w, char *out, size_t size)
@@ -87,6 +90,15 @@ static void resolve_forecast(const ui_context_t *ctx, const ui_solar_t *s, ui_fi
     case UI_FIELD_PV_TOMORROW:
         wh = solar_day_wh(f, field == UI_FIELD_PV_TODAY ? today : today + 1);
         break;
+    case UI_FIELD_PV_CHART: /* the day's bars under its total */
+        if (q != NULL) {
+            wh = solar_day_wh(f, today);
+            out->quarter = quarter;
+            out->minute = ctx->local.tm_min;
+            out->chart_q = q;
+            out->chart_read = s->reading != NULL && s->reading->at != 0 ? energy_day_q(s->day, today) : NULL;
+        }
+        break;
     case UI_FIELD_PV_LEFT:
         wh = solar_left_wh(f, today, quarter, seconds);
         break;
@@ -168,6 +180,8 @@ static void resolve_house(const ui_context_t *ctx, const ui_solar_t *s, ui_field
         wh = energy_from_grid_wh(s->day, r, today);
         out->trend = -1;
         break;
+    case UI_FIELD_EN_FLOW:
+        break;
     case UI_FIELD_EN_SELF: {
         int pct = energy_self_pct(s->day, r, today);
         if (pct < 0) {
@@ -197,7 +211,7 @@ static void resolve_house(const ui_context_t *ctx, const ui_solar_t *s, ui_field
 
 bool ui_resolve_solar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
 {
-    if (field < UI_FIELD_PV_NOW || field > UI_FIELD_EN_SELF) {
+    if (field < UI_FIELD_PV_NOW || field > UI_FIELD_EN_FLOW) {
         return false;
     }
     const ui_solar_t *s = ctx->solar;
@@ -205,7 +219,7 @@ bool ui_resolve_solar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *
     if (s == NULL) {
         return true;
     }
-    if (field <= UI_FIELD_PV_PEAK) {
+    if (field <= UI_FIELD_PV_PEAK || field == UI_FIELD_PV_CHART) {
         resolve_forecast(ctx, s, field, out);
     } else {
         resolve_house(ctx, s, field, out);
@@ -213,6 +227,358 @@ bool ui_resolve_solar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *
     return true;
 }
 
+/* ---- the chart ---- */
+
+static void dotted_hline(gfx_fb_t *fb, int x, int y, int w)
+{
+    for (int i = 0; i < w; i += 3) {
+        gfx_pixel(fb, x + i, y, GFX_BLACK);
+    }
+}
+
+static void dashed_vline(gfx_fb_t *fb, int x, int y, int h)
+{
+    for (int i = 0; i < h; i += 4) {
+        gfx_vline(fb, x, y + i, h - i < 2 ? h - i : 2, GFX_BLACK);
+    }
+}
+
+/* A gridline every 0.5, 1, 2, 10 or 20 kW, as many as the chart's highest bar needs. */
+static uint32_t grid_step(uint32_t top_w)
+{
+    return top_w > 60000 ? 20000 : top_w > 25000 ? 10000 : top_w > 6000 ? 2000 : top_w > 2500 ? 1000 : 500;
+}
+
+/* A gridline's label: "4", "0.5", "180". */
+static void grid_label(const lang_t *lang, uint32_t g, char *out, size_t size)
+{
+    if (g % 1000 == 0) {
+        lang_format_decimal(lang, (long)(g / 1000), 0, out, size);
+    } else {
+        lang_format_decimal(lang, (long)(g / 100), 1, out, size);
+    }
+}
+
+/* The left edge of the plot with labels: room for the widest label the highest quarter hour could need. */
+static int labels_left(gfx_rect_t r, const uint16_t *fq, const uint16_t *aq, const lang_t *lang)
+{
+    uint32_t top_w = 0;
+    for (int i = 0; i < SOLAR_STEPS; i++) {
+        uint32_t f = fq[i] * (uint32_t)SOLAR_UNIT_W, a = aq != NULL && aq[i] != ENERGY_NONE ? aq[i] * 10u : 0;
+        top_w = f > top_w ? f : top_w;
+        top_w = a > top_w ? a : top_w;
+    }
+    uint32_t step = grid_step(top_w), scale = (top_w / step + 1) * step;
+    int widest = 0;
+    for (uint32_t g = step; g < scale; g += step) {
+        char t[8];
+        grid_label(lang, g, t, sizeof(t));
+        int w = gfx_text_width(&gfx_font_sans_12, t);
+        widest = w > widest ? w : widest;
+    }
+    return widest + 8 > 24 ? r.x + widest + 8 : r.x + 24;
+}
+
+/* The day's forecast as bars from the first hour with any to the last: a bar a quarter hour, or an hour where a
+ * quarter's would be under 3 px; the past solid, the rest outlined. With readings, the past's bars are what was
+ * produced and a line over them is what was forecast. With `labels`, kW on the left and every third hour below. */
+static void draw_chart(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v, const lang_t *lang, bool labels)
+{
+    const uint16_t *fq = v->chart_q, *aq = v->chart_read;
+    int step = v->quarter;
+    bool actual = aq != NULL;
+    int first = SOLAR_STEPS, last = -1;
+    for (int i = 0; i < SOLAR_STEPS; i++) {
+        bool read = actual && aq[i] != ENERGY_NONE && aq[i] > 0;
+        if (fq[i] > 0 || read) {
+            first = i < first ? i : first;
+            last = i;
+        }
+    }
+    if (last < 0) {
+        first = 24, last = 71; /* a day without sun: 6 to 18 */
+    }
+    first = first / 4 * 4;
+    last = last / 4 * 4 + 3;
+    int left = labels ? labels_left(r, fq, aq, lang) : r.x + 2;
+    int bottom = labels ? r.y + r.h - 15 : r.y + r.h - 2, top = r.y + 6;
+    int plot_w = r.x + r.w - 2 - left, plot_h = bottom - top;
+    int per = plot_w / (last - first + 1) >= 3 ? 1 : 4; /* quarter hours a bar */
+    int n = (last - first + 1) / per;
+    uint32_t fw[SOLAR_STEPS], aw[SOLAR_STEPS], top_w = 0;
+    for (int i = 0; i < n; i++) {
+        uint32_t f = 0, a = 0;
+        for (int k = 0; k < per; k++) {
+            int st = first + i * per + k;
+            f += fq[st] * (uint32_t)SOLAR_UNIT_W;
+            a += actual && aq[st] != ENERGY_NONE ? aq[st] * (uint32_t)ENERGY_UNIT_W : 0;
+        }
+        fw[i] = f / (uint32_t)per, aw[i] = a / (uint32_t)per;
+        top_w = fw[i] > top_w ? fw[i] : top_w;
+        top_w = aw[i] > top_w ? aw[i] : top_w;
+    }
+    uint32_t grid_w = grid_step(top_w);
+    uint32_t scale = (top_w / grid_w + 1) * grid_w;
+    int pitch = plot_w / n > 1 ? plot_w / n : 1;
+    int bar = pitch >= 4 ? pitch - 1 - (per == 4 && pitch >= 8) : pitch;
+    int x0 = left + (plot_w - pitch * n) / 2;
+    for (uint32_t g = grid_w; g < scale; g += grid_w) { /* a gridline each kW or two, labelled */
+        int y = bottom - (int)((uint64_t)g * (uint32_t)plot_h / scale);
+        dotted_hline(fb, x0, y, pitch * n);
+        if (labels) {
+            char t[8];
+            grid_label(lang, g, t, sizeof(t));
+            gfx_text(fb, &gfx_font_sans_12, left - 4 - gfx_text_width(&gfx_font_sans_12, t), y + 4, t, GFX_BLACK);
+        }
+    }
+    if (labels) {
+        gfx_text(fb, &gfx_font_sans_12, r.x + 2, top + 3, "kW", GFX_BLACK);
+    }
+    int prev_x = -1, prev_y = -1;
+    for (int i = 0; i < n; i++) {
+        int start = first + i * per;
+        int x = x0 + i * pitch;
+        int fh = (int)((uint64_t)fw[i] * (uint32_t)plot_h / scale);
+        bool past = start + per <= step;
+        if (actual && past) {
+            int ah = (int)((uint64_t)aw[i] * (uint32_t)plot_h / scale);
+            if (ah > 0) {
+                gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)x, (int16_t)(bottom - ah), (int16_t)bar, (int16_t)ah },
+                              GFX_BLACK);
+            }
+        } else if (fh > 0) {
+            gfx_rect_t b = { (int16_t)x, (int16_t)(bottom - fh), (int16_t)bar, (int16_t)fh };
+            if (past) {
+                gfx_fill_rect(fb, b, GFX_BLACK);
+            } else {
+                gfx_rect(fb, b, GFX_BLACK);
+            }
+        }
+        if (actual && start < step) { /* the forecast's line over what came */
+            int cx = x + bar / 2, cy = bottom - fh;
+            if (prev_x >= 0) {
+                gfx_line(fb, prev_x, prev_y, cx, cy, GFX_BLACK);
+                gfx_line(fb, prev_x, prev_y - 1, cx, cy - 1, GFX_BLACK);
+            }
+            prev_x = cx, prev_y = cy;
+        }
+    }
+    gfx_hline(fb, x0, bottom, pitch * n, GFX_BLACK);
+    if (step >= first && step <= last) { /* now: a dashed line and a mark above it, the mark inside the plot */
+        int x = x0 + ((step - first) * 15 + v->minute % 15) * pitch / (per * 15);
+        int mark = x < left + 4 ? left + 4 : x > left + plot_w - 4 ? left + plot_w - 4 : x;
+        dashed_vline(fb, x, top, plot_h);
+        gfx_fill_triangle(fb, mark - 4, top - 5, mark + 4, top - 5, mark, top + 1, GFX_BLACK);
+    }
+    if (labels) {
+        for (int h = first / 4; h * 4 <= last + 1; h++) {
+            if (h % 3 != 0) {
+                continue;
+            }
+            int x = x0 + (h * 4 - first) * pitch / per;
+            gfx_vline(fb, x, bottom, 3, GFX_BLACK);
+            char t[12];
+            snprintf(t, sizeof(t), "%d", h);
+            gfx_text(fb, &gfx_font_sans_12, x - gfx_text_width(&gfx_font_sans_12, t) / 2, bottom + 13, t, GFX_BLACK);
+        }
+    }
+}
+
+/* pv.chart in an M or L slot: the sun and today's total over the chart, the label right where it fits. */
+static void draw_chart_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, const lang_t *lang)
+{
+    int top = r.y + 4;
+    gfx_bitmap(fb, r.x + 6, top, &gfx_icon_forecast_16, GFX_BLACK);
+    const gfx_font_t *tf = &gfx_font_sans_bold_16;
+    const char *number = v->short_text[0] ? v->short_text : v->text;
+    char forms[3][sizeof(v->text) + sizeof(v->unit) + 2], t[sizeof(forms[0])];
+    snprintf(forms[0], sizeof(forms[0]), "%s %s", v->text, v->unit); /* then "2800 kWh", then "2800" */
+    snprintf(forms[1], sizeof(forms[1]), "%s %s", number, v->unit);
+    snprintf(forms[2], sizeof(forms[2]), "%s", number);
+    int room = r.w - 32;
+    int k = 0;
+    while (k < 2 && gfx_text_width(tf, forms[k]) > room) {
+        k++;
+    }
+    gfx_text_ellipsize(tf, forms[k], room, t, sizeof(t));
+    int pen = gfx_text(fb, tf, r.x + 26, top + 13, t, GFX_BLACK);
+    const gfx_font_t *lf = size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16;
+    if (pen + 8 + gfx_text_width(lf, v->label) <= r.x + r.w - 6) {
+        gfx_text(fb, lf, r.x + r.w - 6 - gfx_text_width(lf, v->label), top + 13, v->label, GFX_BLACK);
+    }
+    top += 20;
+    draw_chart(fb, (gfx_rect_t){ (int16_t)(r.x + 2), (int16_t)top, (int16_t)(r.w - 4), (int16_t)(r.y + r.h - top - 2) },
+               v, lang, size != UI_SIZE_M);
+}
+
+/* ---- the flow ---- */
+
+static void arrow(gfx_fb_t *fb, int x, int y, int dx, int dy, int k)
+{
+    if (dx != 0) {
+        gfx_fill_triangle(fb, x + dx * k, y, x - dx * k, y - k, x - dx * k, y + k, GFX_BLACK);
+    } else {
+        gfx_fill_triangle(fb, x, y + dy * k, x - k, y - dy * k, x + k, y - dy * k, GFX_BLACK);
+    }
+}
+
+/* A line from (x0, y0) to (x1, y1), horizontal or vertical, `t` px thick: solid while power flows, dotted when
+ * not; its arrow (`k` px) midway points the way the power goes (`dir` 1: towards the second point, -1 back). */
+static void flow_line(gfx_fb_t *fb, int x0, int y0, int x1, int y1, int dir, int t, int k)
+{
+    bool h = y0 == y1;
+    int len = h ? x1 - x0 : y1 - y0;
+    for (int i = 0; i <= len; i++) {
+        if (dir == 0 && i % 4 >= 2) {
+            continue;
+        }
+        if (h) {
+            gfx_vline(fb, x0 + i, y0 - t / 2, t, GFX_BLACK);
+        } else {
+            gfx_hline(fb, x0 - t / 2, y0 + i, t, GFX_BLACK);
+        }
+    }
+    if (dir != 0) {
+        arrow(fb, h ? x0 + len / 2 : x0, h ? y0 : y0 + len / 2, h ? dir : 0, h ? 0 : dir, k);
+    }
+}
+
+/* The widgets' heading: the label left (cut to fit) and a note right, in the label's face. */
+static int heading(gfx_fb_t *fb, gfx_rect_t r, const gfx_font_t *f, const char *label, const char *note)
+{
+    int note_w = note != NULL ? gfx_text_width(f, note) + 8 : 0;
+    char cut[40];
+    gfx_text_ellipsize(f, label, r.w - 12 - note_w, cut, sizeof(cut));
+    int base = r.y + 4 + f->ascent;
+    gfx_text(fb, f, r.x + 6, base, cut, GFX_BLACK);
+    if (note != NULL) {
+        gfx_text(fb, f, r.x + r.w - 6 - gfx_text_width(f, note), base, note, GFX_BLACK);
+    }
+    return r.y + 4 + f->line_height + 2;
+}
+
+static void centred(gfx_fb_t *fb, const gfx_font_t *f, int cx, int baseline, const char *text)
+{
+    gfx_text(fb, f, cx - gfx_text_width(f, text) / 2, baseline, text, GFX_BLACK);
+}
+
+/* A power as the flow shows it within `max_w` px: "2.31", or its shorter form, "2.3" or "200" (spec §5.1). */
+static void kw_fit(const lang_t *lang, int32_t w, const gfx_font_t *f, int max_w, char *out, size_t size)
+{
+    kw_text(lang, w, out, size);
+    if (gfx_text_width(f, out) > max_w) {
+        ui_value_t v = { 0 };
+        set_kw(lang, w, &v);
+        if (v.short_text[0]) {
+            snprintf(out, size, "%s", v.short_text);
+        }
+    }
+}
+
+/* A battery's charge within `max_w` px: "64 %", else "64%". */
+static void soc_fit(int soc, const gfx_font_t *f, int max_w, char *out, size_t size)
+{
+    snprintf(out, size, "%d %%", soc);
+    if (gfx_text_width(f, out) > max_w) {
+        snprintf(out, size, "%d%%", soc);
+    }
+}
+
+/* energy.flow, tall: the panels above a junction, the grid left, the house right, the battery under it. */
+static void flow_diagram(gfx_fb_t *fb, gfx_rect_t r, int top, const ui_solar_t *s, const lang_t *lang)
+{
+    const energy_reading_t *e = s->reading;
+    char t[16];
+    int body = r.y + r.h - top;
+    bool bat = s->battery && e->soc >= 0 && body >= 100;
+    int cx = r.x + r.w / 2;
+    int py = top + (body - (bat ? 100 : 78)) / 2;
+    int jy = py + 24 + 20;
+    int gx = r.x + 22, hx = r.x + r.w - 22;
+    const gfx_font_t *vf = &gfx_font_sans_bold_16;
+    gfx_bitmap(fb, cx - 12, py, &gfx_icon_solar_24, GFX_BLACK);
+    kw_fit(lang, e->pv_w, vf, r.x + r.w - 2 - (cx + 16), t, sizeof(t));
+    gfx_text(fb, vf, cx + 16, py + 18, t, GFX_BLACK);
+    flow_line(fb, cx, py + 26, cx, jy - 4, flow_dir(e->pv_w), 2, 4);
+    gfx_bitmap(fb, gx - 12, jy - 12, &gfx_icon_grid_24, GFX_BLACK);
+    gfx_bitmap(fb, hx - 12, jy - 12, &gfx_icon_house_24, GFX_BLACK);
+    flow_line(fb, gx + 15, jy, cx - 4, jy, flow_dir(e->grid_w), 2, 4); /* + import: towards the house */
+    flow_line(fb, cx + 4, jy, hx - 15, jy, flow_dir(e->load_w), 2, 4);
+    gfx_fill_circle(fb, cx, jy, 3, GFX_BLACK);
+    int side_w = 2 * (gx - r.x) - 4; /* the grid's and the house's values, centred under them, clear of the edges */
+    kw_fit(lang, e->grid_w, vf, side_w, t, sizeof(t));
+    centred(fb, vf, gx, jy + 12 + 17, t);
+    kw_fit(lang, e->load_w, vf, side_w, t, sizeof(t));
+    centred(fb, vf, hx, jy + 12 + 17, t);
+    if (bat) {
+        int by = jy + 24;
+        flow_line(fb, cx, jy + 4, cx, by - 3, flow_dir(e->bat_w), 2, 4);
+        ui_draw_battery(fb, cx - 15, by, 30, 14, e->soc);
+        soc_fit(e->soc, vf, r.w - 4, t, sizeof(t));
+        centred(fb, vf, cx, by + 14 + 16, t);
+    }
+}
+
+/* energy.flow, short: the panels, the house, the grid and the battery in a row, arrows between. */
+static void flow_row(gfx_fb_t *fb, gfx_rect_t r, int top, const ui_solar_t *s, const lang_t *lang)
+{
+    const energy_reading_t *e = s->reading;
+    bool bat = s->battery && e->soc >= 0 && r.w >= 180;
+    int n = bat ? 4 : 3, col = (r.w - 8) / n;
+    int iy = top + (r.y + r.h - top - 24 - 20) / 2;
+    const gfx_bitmap_t *icons[3] = { &gfx_icon_solar_24, &gfx_icon_house_24, &gfx_icon_grid_24 };
+    int32_t watts[3] = { e->pv_w, e->load_w, e->grid_w };
+    const gfx_font_t *vf = &gfx_font_sans_bold_16;
+    for (int i = 0; i < n; i++) {
+        int cx = r.x + 4 + col * i + col / 2;
+        char t[16];
+        if (i == 3) {
+            ui_draw_battery(fb, cx - 15, iy + 5, 30, 14, e->soc);
+            soc_fit(e->soc, vf, col - 2, t, sizeof(t));
+        } else {
+            gfx_bitmap(fb, cx - 12, iy, icons[i], GFX_BLACK);
+            kw_fit(lang, watts[i], vf, col - 2, t, sizeof(t));
+        }
+        centred(fb, vf, cx, iy + 24 + 17, t);
+    }
+    int ay = iy + 12;
+    if (flow_dir(e->pv_w) > 0) { /* the panels feed the house */
+        arrow(fb, r.x + 4 + col, ay, 1, 0, 5);
+    }
+    int gd = flow_dir(e->grid_w);
+    if (gd != 0) { /* to the grid, or from it */
+        arrow(fb, r.x + 4 + 2 * col, ay, -gd, 0, 5);
+    }
+    if (bat && flow_dir(e->bat_w) > 0) { /* a branch, not in line: a bolt while it charges */
+        gfx_bitmap(fb, r.x + 4 + 3 * col - 8, iy + 4, &gfx_icon_bolt_16, GFX_BLACK);
+    }
+}
+
+/* energy.flow in an M or L slot: a diagram from 76 px under the heading, a row below that. */
+static void draw_flow_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, const lang_t *lang)
+{
+    const gfx_font_t *lf = size == UI_SIZE_M ? &gfx_font_sans_12 : &gfx_font_sans_16;
+    int top = heading(fb, r, lf, v->label, "kW");
+    if (r.y + r.h - top >= 76) {
+        flow_diagram(fb, r, top, v->solar, lang);
+    } else {
+        flow_row(fb, r, top, v->solar, lang);
+    }
+}
+
+bool ui_solar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, const lang_t *lang)
+{
+    if (v->state == UI_VALUE_MISSING || v->solar == NULL || (v->kind != UI_FK_CHART && v->kind != UI_FK_FLOW)) {
+        return false;
+    }
+    if (v->kind == UI_FK_FLOW) {
+        draw_flow_widget(fb, r, size, v, lang);
+    } else {
+        draw_chart_widget(fb, r, size, v, lang);
+    }
+    return true;
+}
+
 /* ---- the sample day ---- */
 
 #define PI 3.14159265358979323846
```


`components/ui/ui_internal.h`:

```diff
--- a/components/ui/ui_internal.h
+++ b/components/ui/ui_internal.h
@@ -21,6 +21,8 @@ bool ui_resolve_radar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *
 bool ui_radar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v);
 /* pv.* and energy.* (ui_solar.c, M6d); false for any other field. */
 bool ui_resolve_solar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
+/* pv.chart and energy.flow in an M or L slot (ui_solar.c); false for any other kind. */
+bool ui_solar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v, const lang_t *lang);
 
 /* The weather, air quality, pollen and sun fields (ui_forecast.c, D25); false for any other field. */
 bool ui_resolve_forecast(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
```


`components/ui/ui_widget.c`:

```diff
--- a/components/ui/ui_widget.c
+++ b/components/ui/ui_widget.c
@@ -103,6 +103,7 @@ static const gfx_bitmap_t *field_icon(ui_field_id_t field, int size)
     case UI_FIELD_PV_LEFT:
     case UI_FIELD_PV_TOMORROW:
     case UI_FIELD_PV_PEAK:
+    case UI_FIELD_PV_CHART:
         s16 = &gfx_icon_forecast_16, s24 = &gfx_icon_forecast_24, s48 = &gfx_icon_forecast_48;
         break;
     case UI_FIELD_EN_PV:
@@ -110,6 +111,7 @@ static const gfx_bitmap_t *field_icon(ui_field_id_t field, int size)
         s16 = &gfx_icon_solar_16, s24 = &gfx_icon_solar_24, s48 = &gfx_icon_solar_48;
         break;
     case UI_FIELD_EN_LOAD:
+    case UI_FIELD_EN_FLOW:
         s16 = &gfx_icon_house_16, s24 = &gfx_icon_house_24, s48 = &gfx_icon_house_48;
         break;
     case UI_FIELD_EN_GRID:
@@ -935,6 +937,8 @@ void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t
         /* the weather, air quality, pollen and sun widgets (ui_forecast.c) */
     } else if (ui_radar_widget(fb, r, size, &shown)) {
         /* rain.map (ui_radar.c) */
+    } else if (ui_solar_widget(fb, r, size, &shown, lang)) {
+        /* pv.chart and energy.flow (ui_solar.c) */
     } else if (size == UI_SIZE_XS) {
         draw_tiny(fb, r, &shown);
     } else if (size == UI_SIZE_S) {
```


- [ ] **Step 5: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_ui_solar | tail -2 && ./build-host/test_ui_catalog | tail -2 && ./build-host/test_ui_dashboard_golden | tail -2 && ctest --test-dir build-host | tail -3`
Expected:

```
5 Tests 0 Failures 0 Ignored
OK
4 Tests 0 Failures 0 Ignored
OK
1 Tests 0 Failures 0 Ignored
OK
100% tests passed, 0 tests failed out of 64
```

The four goldens: `home_energy` byte for byte the spike's; `grid_solar`, `weather_solar` and `focus_solar` differ from it in 3, 19 and 5 pixels on the forecast's line and bar tops, from the tens of W (`python3 tools/render.py`, then compare with `captures/spike/`).

- [ ] **Step 6: The firmware builds:** `tools/idf.sh build`, clean, without a warning.

- [ ] **Step 7: Commit.**

```bash
git add components/locale components/ui test/host/CMakeLists.txt test/host/dashboard_fixtures.h \
  test/host/test_ui_solar.c test/host/test_ui_catalog.c test/host/golden
git commit -m "feat(ui): the chart and the flow"
```

### Task 7: The Solar and Energy layouts and their presets (`ui`)

**Files:**
- Modify: `components/locale/include/lang.h`, `components/locale/lang_en.c`, `components/locale/lang_cs.c`, `components/ui/include/ui_layout.h`, `components/ui/include/ui_preset.h`, `components/ui/include/ui_solar.h`, `components/ui/ui_dashboard.c`, `components/ui/ui_layout.c`, `components/ui/ui_preset.c`, `components/ui/ui_preset_json.c`, `components/ui/ui_solar.c`, `test/host/dashboard_fixtures.h`
- Test: `test/host/test_ui_preset.c`, `test/host/test_ui_solar.c`, `test/host/test_ui_catalog.c`, `test/host/test_ui_dashboard_golden.c` with eight goldens (copied from the branch)

**Interfaces:**
- Consumes: Task 6's `draw_chart()`, `flow_line()`, `arrow()`, `heading()`; `ui_resolve()` for the totals.
- Produces (Tasks 9, 11):
  - layouts `UI_LAYOUT_SOLAR` ("solar") and `UI_LAYOUT_ENERGY` ("energy"), without slots;
  - `bool ui_draw_solar_layout(fb, a, ctx)`, `bool ui_draw_energy_layout(fb, a, ctx)` (`ui_solar.h`): each returns whether what it shows is stale, for the status bar's warning;
  - built-in presets "solar" (Solar) and "energy" (Energy), outside the cycle, with the status clock; `UI_OFFERED_SOLAR`, `UI_OFFERED_ENERGY` and the marker's names "solar", "energy";
  - strings `LS_NOW`, `LS_EN_CHARGING`, `LS_EN_DISCHARGING`, `LS_EN_PRODUCED`, `LS_EN_EXPORTED`, `LS_EN_IMPORTED`, `LS_NO_SOLAR`, `LS_NO_ENERGY`.

The layouts are the spike's (spec §11.5, §11.6): Solar has the forecast icon and "Forecast today" with the day's total, now, the peak with its time and what is still to come; the day's chart with kW and every third hour; the next two days' totals with their weather, from the weather forecast. Energy has "SolaX" and the reading's time at the top left; the panels above, the grid left ("Export" or "Import"), the house right, the battery below when it shows, with its charge, state and power; arrows where at least 20 W flow; today's totals two by two, or in a row with a battery. "No solar forecast yet" without today's quarter hours, "No data from the inverter yet" before the first reading. A day without a total and a total without a reading near midnight show a dash. Old data raise the status bar's stale warning, as a stale slot does (spec §5.2).

The largest days keep the numbers apart (the review's finding): from 100 kWh a day today's total takes whole kWh where it would reach the column beside it, the column's numbers shorten beside their labels ("Still to come 1333 kWh"), and a total in the row of four takes whole kWh, then starts under its own icon, ending 6 px before the next total's number; at the sample day's values nothing moves.

A `presets.json` saved before M6d gains Solar and Energy once, at the end, if there is room and their ids are free; the marker then names them, so one deleted later stays deleted (spec §5.4).

- [ ] **Step 1: Write the failing tests.** Eight built-ins; an M5 file gains all four offered ones, an M6c file Solar and Energy, once; room and free ids; the layouts take no slots:

`test/host/test_ui_preset.c`:

```diff
--- a/test/host/test_ui_preset.c
+++ b/test/host/test_ui_preset.c
@@ -19,9 +19,9 @@ void setUp(void)
 
 void tearDown(void) {}
 
-static void test_defaults_are_the_six_built_ins(void)
+static void test_defaults_are_the_eight_built_ins(void)
 {
-    TEST_ASSERT_EQUAL_INT(6, s_p.count);
+    TEST_ASSERT_EQUAL_INT(8, s_p.count);
     TEST_ASSERT_EQUAL_STRING("home", s_p.presets[s_p.active].id);
     TEST_ASSERT_EQUAL(UI_LAYOUT_CLASSIC, s_p.presets[0].layout);
     TEST_ASSERT_EQUAL(UI_FIELD_TIME_CLOCK, s_p.presets[0].slots[0]);
@@ -35,6 +35,14 @@ static void test_defaults_are_the_six_built_ins(void)
     TEST_ASSERT_EQUAL(UI_LAYOUT_FLIGHTS, s_p.presets[5].layout);
     TEST_ASSERT_TRUE(s_p.presets[5].in_cycle);
     TEST_ASSERT_TRUE(s_p.presets[4].status_clock && s_p.presets[5].status_clock); /* a map fills the screen */
+    TEST_ASSERT_EQUAL_INT(6, ui_presets_find(&s_p, "solar")); /* M6d (D35, D36) */
+    TEST_ASSERT_EQUAL(UI_LAYOUT_SOLAR, s_p.presets[6].layout);
+    TEST_ASSERT_EQUAL_STRING("Solar", s_p.presets[6].name);
+    TEST_ASSERT_EQUAL_INT(7, ui_presets_find(&s_p, "energy"));
+    TEST_ASSERT_EQUAL(UI_LAYOUT_ENERGY, s_p.presets[7].layout);
+    TEST_ASSERT_EQUAL_STRING("Energy", s_p.presets[7].name);
+    TEST_ASSERT_FALSE(s_p.presets[6].in_cycle || s_p.presets[7].in_cycle); /* outside the cycle */
+    TEST_ASSERT_TRUE(s_p.presets[6].status_clock && s_p.presets[7].status_clock);
     TEST_ASSERT_EQUAL_UINT8(UI_OFFERED_ALL, s_p.offered); /* nothing left to add */
     TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "nope"));
 }
@@ -78,21 +86,48 @@ static void test_a_file_from_before_m6_gains_the_radars_once(void)
     TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_m5_file, &s_p, s_err, sizeof(s_err)), s_err);
     TEST_ASSERT_EQUAL_UINT8(0, s_p.offered);
     TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p)); /* changed: save it */
-    TEST_ASSERT_EQUAL_INT(6, s_p.count);
+    TEST_ASSERT_EQUAL_INT(8, s_p.count);
     TEST_ASSERT_EQUAL_INT(4, ui_presets_find(&s_p, "rain"));
     TEST_ASSERT_EQUAL_INT(5, ui_presets_find(&s_p, "flights"));
+    TEST_ASSERT_EQUAL_INT(6, ui_presets_find(&s_p, "solar")); /* and M6d's */
     TEST_ASSERT_EQUAL_STRING("weather", s_p.presets[s_p.active].id); /* the active one stays */
     TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p));
 
     TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
-    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"offered\":[\"rain\",\"flights\"]"));
-    s_p.count = 5; /* the owner deletes Flights */
+    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"offered\":[\"rain\",\"flights\",\"solar\",\"energy\"]"));
+    memmove(&s_p.presets[5], &s_p.presets[6], 2 * sizeof(s_p.presets[0])); /* the owner deletes Flights */
+    s_p.count = 7;
     TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
     TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)), s_err);
     TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p)); /* and it stays deleted */
     TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "flights"));
 }
 
+/* A presets.json saved by M6c: the radars offered, its own presets, the Weather preset active. */
+static const char k_m6c_file[] =
+    "{\"schema\":1,\"active\":\"weather\",\"offered\":[\"rain\",\"flights\"],\"presets\":["
+    "{\"id\":\"home\",\"layout\":\"classic\"},{\"id\":\"weather\",\"layout\":\"weather\"},"
+    "{\"id\":\"rain\",\"layout\":\"radar\"}]}";
+
+static void test_a_file_from_m6c_gains_solar_and_energy_once(void)
+{
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_m6c_file, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(UI_OFFERED_RAIN | UI_OFFERED_FLIGHTS, s_p.offered);
+    TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p));
+    TEST_ASSERT_EQUAL_INT(5, s_p.count); /* Flights was deleted under M6c: it stays deleted */
+    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "flights"));
+    TEST_ASSERT_EQUAL_INT(3, ui_presets_find(&s_p, "solar"));
+    TEST_ASSERT_EQUAL_INT(4, ui_presets_find(&s_p, "energy"));
+    TEST_ASSERT_FALSE(s_p.presets[3].in_cycle || s_p.presets[4].in_cycle);
+    TEST_ASSERT_EQUAL_STRING("weather", s_p.presets[s_p.active].id);
+    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p));
+    s_p.count = 4; /* the owner deletes Energy */
+    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p)); /* and it stays deleted */
+    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "energy"));
+}
+
 static void test_the_radars_need_room_and_a_free_id(void)
 {
     memset(&s_p, 0, sizeof(s_p));
@@ -109,9 +144,24 @@ static void test_the_radars_need_room_and_a_free_id(void)
     s_p.presets[0].layout = UI_LAYOUT_GRID;
     s_p.count = 1;
     TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p));
-    TEST_ASSERT_EQUAL_INT(2, s_p.count); /* Flights only */
+    TEST_ASSERT_EQUAL_INT(4, s_p.count); /* Flights, Solar and Energy */
     TEST_ASSERT_EQUAL(UI_LAYOUT_GRID, s_p.presets[0].layout);
     TEST_ASSERT_EQUAL_STRING("flights", s_p.presets[1].id);
+    TEST_ASSERT_EQUAL_STRING("energy", s_p.presets[3].id);
+}
+
+static void test_the_solar_layouts_have_no_slots(void)
+{
+    static const char k_ok[] = "{\"schema\":1,\"presets\":[{\"id\":\"s\",\"layout\":\"solar\"},"
+                               "{\"id\":\"e\",\"layout\":\"energy\",\"slots\":{}}]}";
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_ok, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL(UI_LAYOUT_SOLAR, s_p.presets[0].layout);
+    TEST_ASSERT_EQUAL(UI_LAYOUT_ENERGY, s_p.presets[1].layout);
+    TEST_ASSERT_EQUAL_INT(0, ui_preset_slots(&s_p.presets[0]));
+    static const char k_slot[] =
+        "{\"schema\":1,\"presets\":[{\"id\":\"e\",\"layout\":\"energy\",\"slots\":{\"flow\":\"energy.flow\"}}]}";
+    TEST_ASSERT_FALSE(ui_presets_from_json(k_slot, &s_p, s_err, sizeof(s_err)));
+    TEST_ASSERT_NOT_NULL(strstr(s_err, "has no slot"));
 }
 
 static void test_the_radar_layouts_have_no_slots_and_the_rain_map_needs_room(void)
@@ -575,11 +625,13 @@ static void test_a_preset_counts_the_slots_its_layout_uses(void)
 int main(void)
 {
     UNITY_BEGIN();
-    RUN_TEST(test_defaults_are_the_six_built_ins);
+    RUN_TEST(test_defaults_are_the_eight_built_ins);
     RUN_TEST(test_next_follows_cycle_order_and_skips_presets_out_of_it);
     RUN_TEST(test_the_cycle_visits_flights_only_in_sync_mode_always);
     RUN_TEST(test_a_file_from_before_m6_gains_the_radars_once);
+    RUN_TEST(test_a_file_from_m6c_gains_solar_and_energy_once);
     RUN_TEST(test_the_radars_need_room_and_a_free_id);
+    RUN_TEST(test_the_solar_layouts_have_no_slots);
     RUN_TEST(test_the_radar_layouts_have_no_slots_and_the_rain_map_needs_room);
     RUN_TEST(test_defaults_survive_a_json_round_trip);
     RUN_TEST(test_the_spec_example_parses);
```


A dash for a day without a total and for the totals without a reading near midnight; the stale warning; the largest values kept apart:

`test/host/test_ui_solar.c`:

```diff
--- a/test/host/test_ui_solar.c
+++ b/test/host/test_ui_solar.c
@@ -5,6 +5,7 @@
 
 #include "dashboard_fixtures.h"
 #include "gfx.h"
+#include "gfx_fonts.h"
 #include "gfx_icons.h"
 #include "ui_split.h"
 #include "unity.h"
@@ -160,6 +161,112 @@ static void test_without_data_the_chart_and_the_flow_are_missing(void)
     TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
 }
 
+/* Whether glyph `cp` of `f` is drawn somewhere in `area`, a blank pixel all round it. */
+static bool has_glyph(gfx_rect_t area, const gfx_font_t *f, uint32_t cp)
+{
+    const gfx_glyph_t *g = gfx_font_glyph(f, cp);
+    int rb = (g->width + 7) / 8;
+    for (int y0 = area.y + 1; y0 + g->height < area.y + area.h; y0++) {
+        for (int x0 = area.x + 1; x0 + g->width < area.x + area.w; x0++) {
+            bool same = true;
+            for (int y = -1; same && y <= g->height; y++) {
+                for (int x = -1; same && x <= g->width; x++) {
+                    bool want = x >= 0 && y >= 0 && x < g->width && y < g->height &&
+                                ((f->bitmap[g->offset + y * rb + x / 8] >> (7 - x % 8)) & 1);
+                    same = gfx_get_pixel(&s_fb, x0 + x, y0 + y) == want;
+                }
+            }
+            if (same) {
+                return true;
+            }
+        }
+    }
+    return false;
+}
+
+static void draw_preset(const char *fixture)
+{
+    ui_preset_t preset;
+    ui_context_t ctx;
+    TEST_ASSERT_TRUE(fixture_dashboard(fixture, &ctx, &preset));
+    s_ctx = ctx;
+    gfx_fb_init(&s_fb, s_buf, 400, 300);
+    ui_draw_dashboard(&s_fb, &s_ctx, &preset);
+}
+
+static void redraw(const char *preset_id)
+{
+    ui_preset_t preset = fixture_preset(preset_id);
+    gfx_fb_init(&s_fb, s_buf, 400, 300);
+    ui_draw_dashboard(&s_fb, &s_ctx, &preset);
+}
+
+#define EM_DASH 0x2014
+
+/* A day the forecast has no total for shows a dash in the Solar layout's footer (Forecast.Solar's free tier
+ * has two days). */
+static void test_the_solar_layout_marks_a_day_without_a_total_with_a_dash(void)
+{
+    gfx_rect_t after = { 200, 266, 200, 34 }; /* the day after tomorrow, bottom right */
+    draw_preset("solar");
+    TEST_ASSERT_FALSE(has_glyph(after, &gfx_font_sans_bold_20, EM_DASH));
+    s_fix_forecast.wh[2] = SOLAR_WH_NONE;
+    redraw("solar");
+    TEST_ASSERT_TRUE(has_glyph(after, &gfx_font_sans_bold_20, EM_DASH));
+}
+
+/* Old data raises the status bar's stale warning on both layouts, as a stale slot does (spec §5.2). */
+static void test_stale_data_raises_the_status_bars_warning(void)
+{
+    gfx_rect_t bar = { 0, 0, 400, UI_STATUS_H };
+    int x, y;
+    draw_preset("solar_actual");
+    TEST_ASSERT_FALSE(find_icon(bar, &gfx_icon_stale_16, &x, &y));
+    s_fix_forecast.fetched = (uint32_t)(s_ctx.now - 27 * 3600); /* past its wait */
+    redraw("solar");
+    TEST_ASSERT_TRUE(find_icon(bar, &gfx_icon_stale_16, &x, &y));
+    draw_preset("energy");
+    TEST_ASSERT_FALSE(find_icon(bar, &gfx_icon_stale_16, &x, &y));
+    s_fix_reading.at = (uint32_t)(s_ctx.now - 16 * 60); /* older than 15 min */
+    redraw("energy");
+    TEST_ASSERT_TRUE(find_icon(bar, &gfx_icon_stale_16, &x, &y));
+}
+
+/* Without a reading near midnight the day's totals to and from the grid and the own use are dashes; what was
+ * produced still shows, as the inverter counts it. */
+static void test_the_energy_totals_without_a_midnight_reading_are_dashes(void)
+{
+    gfx_rect_t totals = { 0, 200, 400, 100 };
+    draw_preset("energy");
+    TEST_ASSERT_FALSE(has_glyph(totals, &gfx_font_sans_bold_20, EM_DASH));
+    s_fix_energy_day.base_at = 0;
+    redraw("energy");
+    TEST_ASSERT_TRUE(has_glyph(totals, &gfx_font_sans_bold_20, EM_DASH));
+}
+
+/* The largest values keep the layouts' numbers apart (Review Focus): today's total in the Solar layout ends before
+ * the column beside it, whose labels and values keep apart too, and each of the Energy layout's totals in a row with
+ * a battery ends before the next one's number. */
+static void test_the_largest_totals_keep_to_their_places(void)
+{
+    int top = UI_STATUS_H + 1;
+    draw_preset("solar");
+    fixture_solar_largest(s_ctx.now);
+    redraw("solar");
+    TEST_ASSERT_TRUE_MESSAGE(has_glyph((gfx_rect_t){ 0, (int16_t)top, 196, 84 }, &gfx_font_sans_bold_20, 'h'),
+                             "today's kWh");
+    TEST_ASSERT_TRUE_MESSAGE(has_glyph((gfx_rect_t){ 196, (int16_t)(top + 56), 194, 20 }, &gfx_font_sans_16, 'e'),
+                             "the column's \"Still to come\" beside its value");
+    draw_preset("energy_battery");
+    fixture_solar_largest(s_ctx.now);
+    redraw("energy");
+    for (int i = 0; i < 3; i++) { /* produced, to the grid, from it (the own use is a share): before the next number */
+        TEST_ASSERT_TRUE_MESSAGE(has_glyph((gfx_rect_t){ (int16_t)(i * 100), (int16_t)(top + 222), 120, 48 },
+                                           &gfx_font_sans_16, 'h'),
+                                 "a total's kWh");
+    }
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -168,5 +275,9 @@ int main(void)
     RUN_TEST(test_the_battery_joins_the_flow_where_it_has_room);
     RUN_TEST(test_a_flow_under_20_watts_has_no_arrow);
     RUN_TEST(test_without_data_the_chart_and_the_flow_are_missing);
+    RUN_TEST(test_the_solar_layout_marks_a_day_without_a_total_with_a_dash);
+    RUN_TEST(test_stale_data_raises_the_status_bars_warning);
+    RUN_TEST(test_the_energy_totals_without_a_midnight_reading_are_dashes);
+    RUN_TEST(test_the_largest_totals_keep_to_their_places);
     return UNITY_END();
 }
```


`test/host/test_ui_catalog.c`:

```diff
--- a/test/host/test_ui_catalog.c
+++ b/test/host/test_ui_catalog.c
@@ -55,7 +55,12 @@ static void test_layouts_list_their_slots_with_rectangles_sizes_and_kinds(void)
     TEST_ASSERT_EQUAL_INT(400, num(s_root, "width"));
     TEST_ASSERT_EQUAL_INT(300, num(s_root, "height"));
     const cJSON *layouts = cJSON_GetObjectItemCaseSensitive(s_root, "layouts");
-    TEST_ASSERT_EQUAL_INT(7, cJSON_GetArraySize(layouts));
+    TEST_ASSERT_EQUAL_INT(9, cJSON_GetArraySize(layouts));
+    for (int i = 0; i < 2; i++) { /* M6d: Solar and Energy draw their own views, without slots */
+        const cJSON *view = by_id(layouts, i == 0 ? "solar" : "energy");
+        TEST_ASSERT_NOT_NULL(view);
+        TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(view, "slots")));
+    }
     const cJSON *radar = by_id(layouts, "radar"); /* M6: the radars draw their own map, without slots */
     TEST_ASSERT_NOT_NULL(radar);
     TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(radar, "slots")));
```


The goldens of the spike's eight layout renders:

`test/host/dashboard_fixtures.h`:

```diff
--- a/test/host/dashboard_fixtures.h
+++ b/test/host/dashboard_fixtures.h
@@ -367,6 +367,39 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
         preset->slots[0] = UI_FIELD_TIME_CLOCK;
         ctx->clock_24h = false;
         ctx->local = fixture_local(12, 58, 0);
+    } else if (strcmp(name, "solar") == 0) { /* M6d: the forecast alone, at 13:20 */
+        *preset = fixture_preset("solar");
+        fixture_solar_ctx(ctx, false, false);
+    } else if (strcmp(name, "solar_actual") == 0) { /* with the inverter's readings: what came, against the line */
+        *preset = fixture_preset("solar");
+        fixture_solar_ctx(ctx, true, false);
+    } else if (strcmp(name, "solar_cs") == 0) {
+        fixture_dashboard("solar_actual", ctx, preset);
+        ctx->lang = lang_get("cs");
+    } else if (strcmp(name, "solar_none") == 0) { /* before the first forecast */
+        *preset = fixture_preset("solar");
+        fixture_solar_ctx(ctx, false, false);
+        ctx->solar = fixture_solar_none();
+    } else if (strcmp(name, "energy") == 0) { /* the house now, without a battery: exporting */
+        *preset = fixture_preset("energy");
+        fixture_solar_ctx(ctx, true, false);
+    } else if (strcmp(name, "energy_battery") == 0) { /* with a battery, charging */
+        *preset = fixture_preset("energy");
+        fixture_solar_ctx(ctx, true, true);
+    } else if (strcmp(name, "energy_night_cs") == 0) { /* 22:08 in Czech: nothing from the roof, importing */
+        *preset = fixture_preset("energy");
+        ctx->lang = lang_get("cs");
+        fixture_solar_ctx(ctx, true, false);
+        ctx->now = FIX_NOW + 80 * 60;
+        ctx->local = fixture_local(22, 8, 0);
+        fixture_fill(&s_fix_ds, ctx->now);
+        s_fix_reading.at = (uint32_t)(ctx->now - 3 * 60);
+        s_fix_reading.pv_w = 0;
+        s_fix_reading.load_w = 430;
+        s_fix_reading.grid_w = 430;
+    } else if (strcmp(name, "energy_none") == 0) { /* before the first reading */
+        *preset = fixture_preset("energy");
+        fixture_solar_ctx(ctx, false, false);
     } else if (strcmp(name, "home_energy") == 0) { /* M6d: Classic's small slots, the house now */
         *preset = fixture_preset("home");
         preset->slots[2] = UI_FIELD_EN_PV;
@@ -416,4 +449,6 @@ static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather",
                                                     "weather_frost_cs", "weather_hot_f", "split_compact",
                                                     "split_compact_cs", "split_xs_rows", "split_xs_grid",
                                                     "split_xs_narrow", "home_energy", "grid_solar",
-                                                    "weather_solar", "focus_solar" };
+                                                    "weather_solar", "focus_solar", "solar", "solar_actual",
+                                                    "solar_cs", "solar_none", "energy", "energy_battery",
+                                                    "energy_night_cs", "energy_none" };
```


```bash
git checkout plan/m6d -- \
  test/host/golden/dash_energy.pbm \
  test/host/golden/dash_energy_battery.pbm \
  test/host/golden/dash_energy_night_cs.pbm \
  test/host/golden/dash_energy_none.pbm \
  test/host/golden/dash_solar.pbm \
  test/host/golden/dash_solar_actual.pbm \
  test/host/golden/dash_solar_cs.pbm \
  test/host/golden/dash_solar_none.pbm
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host --target test_ui_preset test_ui_solar test_ui_dashboard_golden 2>&1 | grep -E 'error:' | sed -E 's/.*error: //' | sort | uniq -c | sort -rn | head -6`
Expected:

```
   2 use of undeclared identifier 'UI_LAYOUT_SOLAR'
   2 use of undeclared identifier 'UI_LAYOUT_ENERGY'; did you mean 'UI_LAYOUT_GRID'?
```

- [ ] **Step 3: The layouts, the built-ins and the strings.**

`components/ui/include/ui_layout.h`:

```diff
--- a/components/ui/include/ui_layout.h
+++ b/components/ui/include/ui_layout.h
@@ -18,6 +18,8 @@ typedef enum {
     UI_LAYOUT_RADAR,   /* M6: the weather radar's map, without slots (spec §5.2) */
     UI_LAYOUT_FLIGHTS, /* M6: the flight radar's map and panel, without slots */
     UI_LAYOUT_SPLIT,   /* M6b: cells from the preset's own tree (ui_split.h, D31), without fixed slots */
+    UI_LAYOUT_SOLAR,   /* M6d: today's PV forecast, its chart and the next two days (spec §11.5), without slots */
+    UI_LAYOUT_ENERGY,  /* M6d: the house's energy now and today's totals (spec §11.6), without slots */
     UI_LAYOUT_COUNT,
 } ui_layout_id_t;
 
```


`components/ui/ui_layout.c`:

```diff
--- a/components/ui/ui_layout.c
+++ b/components/ui/ui_layout.c
@@ -44,6 +44,8 @@ static const ui_layout_t k_layouts[UI_LAYOUT_COUNT] = {
     [UI_LAYOUT_RADAR] = { "radar", NULL, 0 },
     [UI_LAYOUT_FLIGHTS] = { "flights", NULL, 0 },
     [UI_LAYOUT_SPLIT] = { "split", NULL, 0 },
+    [UI_LAYOUT_SOLAR] = { "solar", NULL, 0 },
+    [UI_LAYOUT_ENERGY] = { "energy", NULL, 0 },
 };
 
 const ui_layout_t *ui_layout(ui_layout_id_t id)
```


`components/ui/include/ui_preset.h`:

```diff
--- a/components/ui/include/ui_preset.h
+++ b/components/ui/include/ui_preset.h
@@ -97,8 +97,10 @@ time_t ui_schedule_after_night(time_t until);
 enum {
     UI_OFFERED_RAIN = 1u << 0,    /* "rain": Rain radar (M6) */
     UI_OFFERED_FLIGHTS = 1u << 1, /* "flights": Flights (M6) */
+    UI_OFFERED_SOLAR = 1u << 2,   /* "solar": Solar (M6d) */
+    UI_OFFERED_ENERGY = 1u << 3,  /* "energy": Energy (M6d) */
 };
-#define UI_OFFERED_ALL (UI_OFFERED_RAIN | UI_OFFERED_FLIGHTS)
+#define UI_OFFERED_ALL (UI_OFFERED_RAIN | UI_OFFERED_FLIGHTS | UI_OFFERED_SOLAR | UI_OFFERED_ENERGY)
 
 typedef struct {
     uint8_t count;
```


`components/ui/ui_preset.c`:

```diff
--- a/components/ui/ui_preset.c
+++ b/components/ui/ui_preset.c
@@ -40,17 +40,21 @@ void ui_presets_defaults(ui_presets_t *p)
     p->active = 0;
     p->cycle_enabled = false;
     p->cycle_interval_s = 60;
-    ui_presets_offer_builtins(p); /* the radars (M6) */
+    ui_presets_offer_builtins(p); /* the radars (M6), Solar and Energy (M6d) */
 }
 
-/* The built-ins added after M5: the two radars (D28), whose layouts have no slots. */
+/* The built-ins added after M5, whose layouts have no slots: the two radars (D28) in the cycle, Solar and Energy
+ * (D35, D36) outside it. */
 static const struct {
     uint8_t bit;
     const char *id, *name;
     ui_layout_id_t layout;
+    bool in_cycle;
 } k_offered[] = {
-    { UI_OFFERED_RAIN, "rain", "Rain radar", UI_LAYOUT_RADAR },
-    { UI_OFFERED_FLIGHTS, "flights", "Flights", UI_LAYOUT_FLIGHTS },
+    { UI_OFFERED_RAIN, "rain", "Rain radar", UI_LAYOUT_RADAR, true },
+    { UI_OFFERED_FLIGHTS, "flights", "Flights", UI_LAYOUT_FLIGHTS, true },
+    { UI_OFFERED_SOLAR, "solar", "Solar", UI_LAYOUT_SOLAR, false },
+    { UI_OFFERED_ENERGY, "energy", "Energy", UI_LAYOUT_ENERGY, false },
 };
 
 bool ui_presets_offer_builtins(ui_presets_t *p)
@@ -62,9 +66,9 @@ bool ui_presets_offer_builtins(ui_presets_t *p)
         }
         if (ui_presets_find(p, k_offered[i].id) < 0 && p->count < UI_PRESET_MAX) {
             ui_preset_t *added = &p->presets[p->count++];
-            *added = make(k_offered[i].id, k_offered[i].name, k_offered[i].layout, true,
+            *added = make(k_offered[i].id, k_offered[i].name, k_offered[i].layout, k_offered[i].in_cycle,
                           (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_NONE });
-            added->status_clock = true; /* a map fills the screen: the time goes to the status bar */
+            added->status_clock = true; /* the view fills the screen: the time goes to the status bar */
         }
         p->offered |= k_offered[i].bit;
         changed = true;
```


`components/ui/ui_preset_json.c`:

```diff
--- a/components/ui/ui_preset_json.c
+++ b/components/ui/ui_preset_json.c
@@ -13,7 +13,7 @@
 static const char *const k_policies[] = { [UI_STALE_STALE] = "stale", [UI_STALE_PLACEHOLDER] = "placeholder",
                                           [UI_STALE_HIDE] = "hide" };
 static const char *const k_battery_parts[] = { "percent", "voltage", "days" }; /* UI_STATUS_BAT_* bit order */
-static const char *const k_offered_ids[] = { "rain", "flights" };              /* UI_OFFERED_* bit order */
+static const char *const k_offered_ids[] = { "rain", "flights", "solar", "energy" }; /* UI_OFFERED_* bit order */
 
 static bool fail(char *err, size_t size, const char *fmt, ...)
 {
```


`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -216,6 +216,14 @@ typedef enum {
     LS_EN_IMPORTING,
     LS_PV_CHART, /* pv.chart and energy.flow (spec §5.3) */
     LS_EN_FLOW,
+    LS_NOW, /* the Solar and Energy layouts (spec §11.5, §11.6) */
+    LS_EN_CHARGING,
+    LS_EN_DISCHARGING,
+    LS_EN_PRODUCED,
+    LS_EN_EXPORTED,
+    LS_EN_IMPORTED,
+    LS_NO_SOLAR,
+    LS_NO_ENERGY,
     LS_COUNT,
 } lang_str_t;
 
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -227,6 +227,14 @@ const lang_t lang_en = {
         [LS_EN_IMPORTING] = "Import",
         [LS_PV_CHART] = "Solar forecast",
         [LS_EN_FLOW] = "Energy flow",
+        [LS_NOW] = "Now",
+        [LS_EN_CHARGING] = "Charging",
+        [LS_EN_DISCHARGING] = "Discharging",
+        [LS_EN_PRODUCED] = "Produced",
+        [LS_EN_EXPORTED] = "To grid",
+        [LS_EN_IMPORTED] = "From grid",
+        [LS_NO_SOLAR] = "No solar forecast yet",
+        [LS_NO_ENERGY] = "No data from the inverter yet",
     },
     .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
     .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -267,6 +267,14 @@ const lang_t lang_cs = {
         [LS_EN_IMPORTING] = "Odběr",
         [LS_PV_CHART] = "Předpověď FVE",
         [LS_EN_FLOW] = "Toky energie",
+        [LS_NOW] = "Teď",
+        [LS_EN_CHARGING] = "Nabíjí",
+        [LS_EN_DISCHARGING] = "Vybíjí",
+        [LS_EN_PRODUCED] = "Vyrobeno",
+        [LS_EN_EXPORTED] = "Do sítě",
+        [LS_EN_IMPORTED] = "Ze sítě",
+        [LS_NO_SOLAR] = "Předpověď FVE zatím není",
+        [LS_NO_ENERGY] = "Ze střídače zatím nic",
     },
     .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
     .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
```


- [ ] **Step 4: The drawing.** Ported from the spike onto the real data; the next days' weather comes from the datastore's forecast:

`components/ui/include/ui_solar.h`:

```diff
--- a/components/ui/include/ui_solar.h
+++ b/components/ui/include/ui_solar.h
@@ -24,6 +24,15 @@ struct ui_solar {
     bool battery;                     /* the battery shows (energy.battery, spec §11.6) */
 };
 
+/* The Solar layout under the status bar in `a` (spec §11.5): today's total, now, the peak and what is still to
+ * come; the day's chart; the next two days' totals with their weather. "No solar forecast yet" without today's
+ * quarter hours. Returns whether what it shows is stale, for the status bar's warning. */
+bool ui_draw_solar_layout(gfx_fb_t *fb, gfx_rect_t a, const ui_context_t *ctx);
+/* The Energy layout (spec §11.6): the reading's time, the panels above a junction, the grid left, the house right,
+ * the battery below when it shows; today's totals. "No data from the inverter yet" before the first reading.
+ * Returns whether the reading is stale. */
+bool ui_draw_energy_layout(gfx_fb_t *fb, gfx_rect_t a, const ui_context_t *ctx);
+
 /* The sample day (spec §15): a 5.2 kWp roof facing south on local day `day`, whose midnight is `midnight` (UTC),
  * its forecast fetched at 05:48: a clear morning with cloud passing in the afternoon, tomorrow overcast, the day
  * after sunny. With `live`, the inverter's readings until `now`: a misty start, then a little above the forecast,
```


`components/ui/ui_solar.c`:

```diff
--- a/components/ui/ui_solar.c
+++ b/components/ui/ui_solar.c
@@ -7,6 +7,7 @@
 #include "gfx_fonts.h"
 #include "gfx_icons.h"
 #include "ui_internal.h"
+#include "weather.h"
 
 /* The pv.* and energy.* fields (spec §5.1, §11.5, §11.6, M6d), the chart and the flow (spec §5.3), and the
  * sample day. */
@@ -579,6 +580,257 @@ bool ui_solar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_
     return true;
 }
 
+/* ---- the Solar and Energy layouts ---- */
+
+#define PLACEHOLDER "\xE2\x80\x94" /* em dash: a value there is none of */
+
+static int ink(const gfx_font_t *f)
+{
+    const gfx_glyph_t *g = gfx_font_glyph(f, '0');
+    return g != NULL ? g->height : f->ascent;
+}
+
+static void stat_row(gfx_fb_t *fb, int x, int right, int baseline, const char *label, const char *value)
+{
+    gfx_text(fb, &gfx_font_sans_16, x, baseline, label, GFX_BLACK);
+    gfx_text(fb, &gfx_font_sans_bold_16, right - gfx_text_width(&gfx_font_sans_bold_16, value), baseline, value,
+             GFX_BLACK);
+}
+
+/* What a stat row's number may take beside `label` between `x` and `right`, 8 px apart, before its `unit`. */
+static int stat_room(int x, int right, const char *label, const char *unit)
+{
+    return right - x - 8 - gfx_text_width(&gfx_font_sans_16, label) - gfx_text_width(&gfx_font_sans_bold_16, unit);
+}
+
+/* A stat row's power: "4.12 kW", its number shorter where it wouldn't fit ("200 kW"). */
+static void stat_kw(const lang_t *lang, int32_t w, int x, int right, const char *label, char *out, size_t size)
+{
+    char n[16];
+    kw_fit(lang, w, &gfx_font_sans_bold_16, stat_room(x, right, label, " kW"), n, sizeof(n));
+    snprintf(out, size, "%s kW", n);
+}
+
+/* A stat row's energy: "18.4 kWh", whole kWh where it wouldn't fit ("1333 kWh"); a dash without one. */
+static void stat_kwh(const lang_t *lang, uint32_t wh, int x, int right, const char *label, char *out, size_t size)
+{
+    char n[16];
+    if (wh == SOLAR_WH_NONE) {
+        snprintf(out, size, "%s", PLACEHOLDER);
+        return;
+    }
+    kwh_text(lang, wh, n, sizeof(n));
+    if (gfx_text_width(&gfx_font_sans_bold_16, n) > stat_room(x, right, label, " kWh") && wh >= 9950) {
+        snprintf(n, sizeof(n), "%lu", (unsigned long)((wh + 500) / 1000));
+    }
+    snprintf(out, size, "%s kWh", n);
+}
+
+static void placeholder(gfx_fb_t *fb, gfx_rect_t a, const gfx_bitmap_t *icon, const char *text)
+{
+    gfx_bitmap(fb, a.x + (a.w - icon->width) / 2, a.y + a.h / 2 - 48, icon, GFX_BLACK);
+    gfx_text_in_rect(fb, &gfx_font_sans_bold_20, (gfx_rect_t){ a.x, (int16_t)(a.y + a.h / 2 + 10), a.w, 28 },
+                     GFX_ALIGN_CENTER, text, GFX_BLACK);
+}
+
+/* The weather forecast's sky for local day `day`, or -1 where it has none. */
+static int sky_on(const ui_context_t *ctx, int32_t day)
+{
+    const ds_weather_t *w = ctx->ds != NULL ? ds_weather(ctx->ds) : NULL;
+    int k = w != NULL ? day - w->day0_local : -1;
+    return k >= 0 && k < DS_WX_DAYS ? (int)weather_sky(w->days[k].code) : -1;
+}
+
+/* A day's total in a footer half, centred: its weekday, its weather and its energy, or a dash without one. */
+static void day_total(gfx_fb_t *fb, gfx_rect_t c, const ui_context_t *ctx, const char *name, int sky, uint32_t wh)
+{
+    char t[16];
+    bool known = wh != SOLAR_WH_NONE;
+    if (known) {
+        kwh_text(ctx->lang, wh, t, sizeof(t));
+    } else {
+        snprintf(t, sizeof(t), "%s", PLACEHOLDER);
+    }
+    const gfx_bitmap_t *icon = sky >= 0 ? ui_sky_icon(sky, false, 24) : NULL;
+    int w = gfx_text_width(&gfx_font_sans_16, name) + 8 + (icon ? 30 : 0) + gfx_text_width(&gfx_font_sans_bold_20, t) +
+            (known ? 3 + gfx_text_width(&gfx_font_sans_16, "kWh") : 0);
+    int base = c.y + (c.h + ink(&gfx_font_sans_bold_20)) / 2;
+    int x = c.x + (c.w - w) / 2;
+    x = gfx_text(fb, &gfx_font_sans_16, x, base, name, GFX_BLACK) + 8;
+    if (icon != NULL) {
+        gfx_bitmap(fb, x, c.y + (c.h - 24) / 2, icon, GFX_BLACK);
+        x += 30;
+    }
+    x = gfx_text(fb, &gfx_font_sans_bold_20, x, base, t, GFX_BLACK) + 3;
+    if (known) {
+        gfx_text(fb, &gfx_font_sans_16, x, base, "kWh", GFX_BLACK);
+    }
+}
+
+bool ui_draw_solar_layout(gfx_fb_t *fb, gfx_rect_t a, const ui_context_t *ctx)
+{
+    const lang_t *lang = ctx->lang;
+    ui_value_t chart;
+    ui_resolve(ctx, UI_FIELD_PV_CHART, &chart); /* today's quarter hours, the quarter hour now, the readings' */
+    if (chart.state == UI_VALUE_MISSING) {
+        placeholder(fb, a, &gfx_icon_forecast_48, lang_str(lang, LS_NO_SOLAR));
+        return false;
+    }
+    const solar_forecast_t *f = ctx->solar->forecast;
+    int32_t today = ctx->local_day;
+    char t[32], u[sizeof(t) + 8];
+    /* today, large, and three numbers beside it */
+    gfx_bitmap(fb, a.x + 8, a.y + 6, &gfx_icon_forecast_24, GFX_BLACK);
+    gfx_text(fb, &gfx_font_sans_16, a.x + 38, a.y + 23, lang_str(lang, LS_PV_TODAY), GFX_BLACK);
+    int base = a.y + 36 + ink(&gfx_font_num_cb_48);
+    int col = a.x + 196, right = a.x + a.w - 10;
+    int room = col - 6 - (a.x + 8) - 5 - gfx_text_width(&gfx_font_sans_bold_20, "kWh");
+    const char *big = gfx_text_width(&gfx_font_num_cb_48, chart.text) > room && chart.short_text[0] != '\0'
+                          ? chart.short_text /* from 100 kWh: whole kWh */
+                          : chart.text;
+    int pen = gfx_text(fb, &gfx_font_num_cb_48, a.x + 8, base, big, GFX_BLACK);
+    gfx_text(fb, &gfx_font_sans_bold_20, pen + 5, base, "kWh", GFX_BLACK);
+    stat_kw(lang, chart.chart_q[chart.quarter] * SOLAR_UNIT_W, col, right, lang_str(lang, LS_NOW), u, sizeof(u));
+    stat_row(fb, col, right, a.y + 22, lang_str(lang, LS_NOW), u);
+    uint32_t peak_w = 0;
+    int peak_at = 0;
+    char label[40];
+    if (solar_peak(f, today, &peak_w, &peak_at)) { /* "Peak 12:45 ... 4.12 kW" */
+        char at[16];
+        quarter_time(ctx, peak_at, at, sizeof(at));
+        snprintf(label, sizeof(label), "%s %s", lang_str(lang, LS_PV_PEAK), at);
+    } else {
+        snprintf(label, sizeof(label), "%s", lang_str(lang, LS_PV_PEAK));
+    }
+    stat_kw(lang, (int32_t)peak_w, col, right, label, u, sizeof(u));
+    stat_row(fb, col, right, a.y + 46, label, u);
+    int seconds = ctx->local.tm_min % 15 * 60 + ctx->local.tm_sec;
+    stat_kwh(lang, solar_left_wh(f, today, chart.quarter, seconds), col, right, lang_str(lang, LS_PV_LEFT), u,
+             sizeof(u));
+    stat_row(fb, col, right, a.y + 70, lang_str(lang, LS_PV_LEFT), u);
+    gfx_hline(fb, a.x + 8, a.y + 82, a.w - 16, GFX_BLACK);
+    /* the day's chart */
+    draw_chart(fb, (gfx_rect_t){ (int16_t)(a.x + 2), (int16_t)(a.y + 88), (int16_t)(a.w - 6), 152 }, &chart, lang,
+               true);
+    /* tomorrow and the day after */
+    gfx_hline(fb, a.x + 8, a.y + 245, a.w - 16, GFX_BLACK);
+    gfx_vline(fb, a.x + a.w / 2, a.y + 251, 22, GFX_BLACK);
+    for (int d = 1; d <= 2; d++) {
+        gfx_rect_t c = { (int16_t)(a.x + (d - 1) * a.w / 2), (int16_t)(a.y + 247), (int16_t)(a.w / 2), 32 };
+        day_total(fb, c, ctx, lang->weekdays_short[(ctx->local.tm_wday + d) % 7], sky_on(ctx, today + d),
+                  solar_day_wh(f, today + d));
+    }
+    return chart.state == UI_VALUE_STALE;
+}
+
+/* A node's words under its icon: the label, then the power, centred on cx. */
+static void node_text(gfx_fb_t *fb, int cx, int y, const char *label, const char *value)
+{
+    gfx_text(fb, &gfx_font_sans_16, cx - gfx_text_width(&gfx_font_sans_16, label) / 2, y + 14, label, GFX_BLACK);
+    gfx_text(fb, &gfx_font_sans_bold_20, cx - gfx_text_width(&gfx_font_sans_bold_20, value) / 2, y + 36, value,
+             GFX_BLACK);
+}
+
+/* One of today's totals: its icon and label, then its value and unit, or a dash without one. The value may run on
+ * under the next cell's icon, ending before `limit`: in a row of four, from 100 kWh whole kWh, then under its own
+ * icon. */
+static void total(gfx_fb_t *fb, gfx_rect_t c, int limit, const gfx_bitmap_t *icon, const char *label,
+                  const ui_value_t *v)
+{
+    gfx_bitmap(fb, c.x + 6, c.y + 4, icon, GFX_BLACK);
+    gfx_text(fb, &gfx_font_sans_12, c.x + 26, c.y + 15, label, GFX_BLACK);
+    bool known = v->state != UI_VALUE_MISSING;
+    const char *text = known ? v->text : PLACEHOLDER;
+    int x = c.x + 26, end = limit - (known ? 3 + gfx_text_width(&gfx_font_sans_16, v->unit) : 0);
+    if (known && x + gfx_text_width(&gfx_font_sans_bold_20, text) > end && v->short_text[0] != '\0') {
+        text = v->short_text;
+        x = x + gfx_text_width(&gfx_font_sans_bold_20, text) > end ? c.x + 6 : x;
+    }
+    int pen = gfx_text(fb, &gfx_font_sans_bold_20, x, c.y + 38, text, GFX_BLACK);
+    if (known) {
+        gfx_text(fb, &gfx_font_sans_16, pen + 3, c.y + 38, v->unit, GFX_BLACK);
+    }
+}
+
+bool ui_draw_energy_layout(gfx_fb_t *fb, gfx_rect_t a, const ui_context_t *ctx)
+{
+    const ui_solar_t *s = ctx->solar;
+    const lang_t *lang = ctx->lang;
+    const energy_reading_t *e = s != NULL ? s->reading : NULL;
+    if (e == NULL || e->at == 0) {
+        placeholder(fb, a, &gfx_icon_house_48, lang_str(lang, LS_NO_ENERGY));
+        return false;
+    }
+    bool bat = s->battery && e->soc >= 0;
+    char t[24], v[32];
+    int cx = a.x + a.w / 2;
+    int jy = a.y + (bat ? 92 : 104); /* where the lines meet */
+    int gx = a.x + 56, hx = a.x + a.w - 56;
+    /* the panels, top centre, the power beside them */
+    gfx_bitmap(fb, cx - 24, a.y + 4, &gfx_icon_solar_48, GFX_BLACK);
+    kw_text(lang, e->pv_w, t, sizeof(t));
+    int pen = gfx_text(fb, &gfx_font_sans_bold_28, cx + 34, a.y + 44, t, GFX_BLACK);
+    gfx_text(fb, &gfx_font_sans_16, pen + 3, a.y + 44, "kW", GFX_BLACK);
+    gfx_text(fb, &gfx_font_sans_16, cx + 34, a.y + 18, lang_str(lang, LS_EN_PV), GFX_BLACK);
+    /* when the inverter's reading came */
+    char when[16];
+    ui_clock_text(ctx, (time_t)e->at, when, sizeof(when));
+    snprintf(v, sizeof(v), "SolaX %s", when);
+    gfx_text(fb, &gfx_font_sans_12, a.x + 6, a.y + 14, v, GFX_BLACK);
+    /* the lines, then the junction */
+    flow_line(fb, cx, a.y + 56, cx, jy - 6, flow_dir(e->pv_w), 2, 6);
+    flow_line(fb, gx + 30, jy, cx - 6, jy, flow_dir(e->grid_w), 2, 6); /* + import: towards the house */
+    flow_line(fb, cx + 6, jy, hx - 30, jy, flow_dir(e->load_w), 2, 6);
+    gfx_fill_circle(fb, cx, jy, 5, GFX_BLACK);
+    /* the grid, left; the house, right */
+    gfx_bitmap(fb, gx - 24, jy - 24, &gfx_icon_grid_48, GFX_BLACK);
+    gfx_bitmap(fb, hx - 24, jy - 24, &gfx_icon_house_48, GFX_BLACK);
+    kw_text(lang, e->grid_w, t, sizeof(t));
+    snprintf(v, sizeof(v), "%s kW", t);
+    int gd = flow_dir(e->grid_w);
+    node_text(fb, gx, jy + 26, lang_str(lang, gd < 0 ? LS_EN_EXPORTING : gd > 0 ? LS_EN_IMPORTING : LS_EN_GRID), v);
+    kw_text(lang, e->load_w, t, sizeof(t));
+    snprintf(v, sizeof(v), "%s kW", t);
+    node_text(fb, hx, jy + 26, lang_str(lang, LS_EN_LOAD), v);
+    int totals_y = a.y + (bat ? 222 : 176);
+    if (bat) { /* the battery, below the junction: its state and power left of it, its charge right */
+        int by = jy + 70;
+        flow_line(fb, cx, jy + 6, cx, by - 6, flow_dir(e->bat_w), 2, 6);
+        ui_draw_battery(fb, cx - 30, by, 60, 28, e->soc);
+        snprintf(t, sizeof(t), "%d %%", e->soc);
+        gfx_text(fb, &gfx_font_sans_bold_28, cx + 40, by + 24, t, GFX_BLACK);
+        int bd = flow_dir(e->bat_w);
+        const char *state = lang_str(lang, bd > 0 ? LS_EN_CHARGING : bd < 0 ? LS_EN_DISCHARGING : LS_EN_BATTERY);
+        kw_text(lang, e->bat_w, t, sizeof(t));
+        snprintf(v, sizeof(v), "%s kW", t);
+        int rx = cx - 40;
+        gfx_text(fb, &gfx_font_sans_16, rx - gfx_text_width(&gfx_font_sans_16, state), by + 10, state, GFX_BLACK);
+        gfx_text(fb, &gfx_font_sans_bold_20, rx - gfx_text_width(&gfx_font_sans_bold_20, v), by + 32, v, GFX_BLACK);
+    }
+    /* today's totals, two by two, or in a row with a battery */
+    gfx_hline(fb, a.x + 8, totals_y - 6, a.w - 16, GFX_BLACK);
+    static const ui_field_id_t k_totals[4] = { UI_FIELD_EN_YIELD, UI_FIELD_EN_EXPORT, UI_FIELD_EN_IMPORT,
+                                               UI_FIELD_EN_SELF };
+    static const lang_str_t k_labels[4] = { LS_EN_PRODUCED, LS_EN_EXPORTED, LS_EN_IMPORTED, LS_EN_SELF };
+    const gfx_bitmap_t *icons[4] = { &gfx_icon_solar_16, &gfx_icon_grid_16, &gfx_icon_grid_16,
+                                     &gfx_icon_self_use_16 };
+    int w = a.w / (bat ? 4 : 2), h = bat ? 48 : 50;
+    bool stale = false;
+    for (int i = 0; i < 4; i++) {
+        gfx_rect_t c = bat ? (gfx_rect_t){ (int16_t)(a.x + i * w), (int16_t)totals_y, (int16_t)w, (int16_t)h }
+                           : (gfx_rect_t){ (int16_t)(a.x + (i % 2) * w), (int16_t)(totals_y + (i / 2) * h),
+                                           (int16_t)w, (int16_t)h };
+        ui_value_t value;
+        ui_resolve(ctx, k_totals[i], &value);
+        bool last = bat ? i == 3 : i % 2 == 1; /* the others end 6 px before the next one's number */
+        total(fb, c, last ? a.x + a.w - 4 : c.x + c.w + 20, icons[i], lang_str(lang, k_labels[i]), &value);
+    }
+    ui_value_t power;
+    ui_resolve(ctx, UI_FIELD_EN_PV, &power);
+    stale = power.state == UI_VALUE_STALE;
+    return stale;
+}
+
 /* ---- the sample day ---- */
 
 #define PI 3.14159265358979323846
```


`components/ui/ui_dashboard.c`:

```diff
--- a/components/ui/ui_dashboard.c
+++ b/components/ui/ui_dashboard.c
@@ -2,6 +2,7 @@
 
 #include "ui_internal.h"
 #include "ui_radar.h"
+#include "ui_solar.h"
 #include "ui_split.h"
 
 static void draw_separators(gfx_fb_t *fb, ui_layout_id_t layout)
@@ -93,6 +94,10 @@ void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t
         ui_draw_flights_view(fb, below, &c);
     } else if (preset->layout == UI_LAYOUT_SPLIT) {
         any_stale = draw_split(fb, &c, preset);
+    } else if (preset->layout == UI_LAYOUT_SOLAR) { /* M6d */
+        any_stale = ui_draw_solar_layout(fb, below, &c);
+    } else if (preset->layout == UI_LAYOUT_ENERGY) {
+        any_stale = ui_draw_energy_layout(fb, below, &c);
     } else if (layout != NULL) {
         draw_separators(fb, (ui_layout_id_t)preset->layout);
         for (int i = 0; i < layout->slot_count; i++) {
```


- [ ] **Step 5: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_ui_preset | tail -2 && ./build-host/test_ui_solar | tail -2 && ./build-host/test_ui_dashboard_golden | tail -2 && ctest --test-dir build-host | tail -3`
Expected:

```
31 Tests 0 Failures 0 Ignored
OK
9 Tests 0 Failures 0 Ignored
OK
1 Tests 0 Failures 0 Ignored
OK
100% tests passed, 0 tests failed out of 64
```

The eight goldens: the five Energy renders byte for byte the spike's; `solar`, `solar_actual` and `solar_cs` differ from it in 10, 22 and 22 pixels of bar tops, from the tens of W. Every number on them matches the approved renders (27.4 kWh, 4.06 kW now, 4.12 kW at 12:45, 10.4 kWh to come, 11.2 and 21.4 kWh).

- [ ] **Step 6: The firmware builds:** `tools/idf.sh build`, clean, without a warning.

- [ ] **Step 7: Commit.**

```bash
git add components/locale components/ui test/host/dashboard_fixtures.h test/host/test_ui_preset.c \
  test/host/test_ui_solar.c test/host/test_ui_catalog.c test/host/golden
git commit -m "feat(ui): the Solar and Energy layouts and their presets"
```

### Task 8: Sync ▸ Steps in the menu (`ui`, `main`)

**Files:**
- Modify: `components/locale/include/lang.h`, `components/locale/lang_en.c`, `components/locale/lang_cs.c`, `components/ui/include/ui_menu.h`, `components/ui/ui_menu.c`, `main/app_menu.c`
- Test: `test/host/test_ui_menu.c`

**Interfaces:**
- Consumes: Task 4's `settings_t.sync_steps` and `settings_step_t`.
- Produces (Task 10): `UI_MI_SYNC_STEPS` (a section in Sync) and its toggles `UI_MI_STEP_WEATHER` … `UI_MI_STEP_ENERGY`, in `settings_step_t` order; strings `LS_SYNC_STEP_SOLAR` ("Solar forecast"), `LS_SYNC_STEP_ENERGY` ("House energy"), `LS_M_SYNC_STEPS` ("Steps"). The step names in `sync_step_t` order now end with Solar and Energy, as Info ▸ Last sync names them (Task 10).

Spec §5.7: Sync ▸ Steps switches each data step on and off: Weather, Air quality, Radar, Solar forecast, House energy; the time has no switch, as it always runs. A switch flips at once and is saved; the next sync, and in `always` the next refresh, follows it.

- [ ] **Step 1: Write the failing test.**

`test/host/test_ui_menu.c`:

```diff
--- a/test/host/test_ui_menu.c
+++ b/test/host/test_ui_menu.c
@@ -299,9 +299,9 @@ static void test_the_sync_section_offers_its_mode_and_quiet_hours(void)
     s_model.hidden[UI_MI_SYNC_INTERVAL] = true; /* the app hides it outside interval mode */
     open_item(UI_MI_SYNC);
     ui_menu_item_t items[UI_MI_COUNT];
-    const ui_menu_item_t expected[] = { UI_MI_SYNC_NOW, UI_MI_SYNC_MODE, UI_MI_QUIET_HOURS };
-    TEST_ASSERT_EQUAL_INT(3, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
-    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 3);
+    const ui_menu_item_t expected[] = { UI_MI_SYNC_NOW, UI_MI_SYNC_MODE, UI_MI_QUIET_HOURS, UI_MI_SYNC_STEPS };
+    TEST_ASSERT_EQUAL_INT(4, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
+    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 4);
     ui_menu_intent_t in = open_item(UI_MI_SYNC_NOW);
     TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind); /* no question: a sync loses nothing */
     TEST_ASSERT_EQUAL_INT(UI_MI_SYNC_NOW, in.item);
@@ -311,10 +311,40 @@ static void test_the_sync_section_offers_its_mode_and_quiet_hours(void)
     TEST_ASSERT_EQUAL_INT(1, in.value);
 }
 
+/* Sync ▸ Steps (M6d, D35): a switch for each data step; the time has none, as it always runs (spec §9.3). */
+static void test_the_sync_steps_switch_on_and_off(void)
+{
+    open_item(UI_MI_SYNC);
+    open_item(UI_MI_SYNC_STEPS);
+    ui_menu_item_t items[UI_MI_COUNT];
+    const ui_menu_item_t expected[] = { UI_MI_STEP_WEATHER, UI_MI_STEP_AIR, UI_MI_STEP_RADAR, UI_MI_STEP_SOLAR,
+                                        UI_MI_STEP_ENERGY };
+    TEST_ASSERT_EQUAL_INT(5, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
+    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 5);
+    const lang_t *en = lang_get("en"), *cs = lang_get("cs");
+    TEST_ASSERT_EQUAL_STRING("Steps", ui_menu_label(UI_MI_SYNC_STEPS, en));
+    TEST_ASSERT_EQUAL_STRING("Air quality", ui_menu_label(UI_MI_STEP_AIR, en));
+    TEST_ASSERT_EQUAL_STRING("Solar forecast", ui_menu_label(UI_MI_STEP_SOLAR, en));
+    TEST_ASSERT_EQUAL_STRING("House energy", ui_menu_label(UI_MI_STEP_ENERGY, en));
+    TEST_ASSERT_EQUAL_STRING("Kroky", ui_menu_label(UI_MI_SYNC_STEPS, cs));
+    TEST_ASSERT_EQUAL_STRING("Energie domu", ui_menu_label(UI_MI_STEP_ENERGY, cs));
+    s_model.value[UI_MI_STEP_SOLAR] = 1;
+    ui_menu_intent_t in = open_item(UI_MI_STEP_SOLAR);
+    TEST_ASSERT_EQUAL(UI_MENU_SET, in.kind); /* a switch flips at once */
+    TEST_ASSERT_EQUAL_INT(UI_MI_STEP_SOLAR, in.item);
+    TEST_ASSERT_EQUAL_INT(0, in.value);
+    char text[16];
+    ui_menu_value_text(UI_MI_STEP_SOLAR, 0, &s_model, en, text, sizeof(text));
+    TEST_ASSERT_EQUAL_STRING("Off", text);
+    press(UI_MENU_KEY_BACK);
+    TEST_ASSERT_EQUAL_INT(UI_MI_SYNC_STEPS, ui_menu_current(&s_m, &s_model)); /* back where it was */
+}
+
 int main(void)
 {
     UNITY_BEGIN();
     RUN_TEST(test_the_sync_section_offers_its_mode_and_quiet_hours);
+    RUN_TEST(test_the_sync_steps_switch_on_and_off);
     RUN_TEST(test_the_root_lists_the_sections_in_order);
     RUN_TEST(test_next_wraps_select_enters_and_back_returns_to_the_section);
     RUN_TEST(test_back_at_the_root_and_exit_anywhere_close_the_menu);
```


- [ ] **Step 2: Run it to see it fail.**

Run: `cmake --build build-host --target test_ui_menu 2>&1 | grep -E 'error:' | sed -E 's/.*error: //' | sort | uniq -c`
Expected:

```
   2 use of undeclared identifier 'UI_MI_STEP_AIR'
   3 use of undeclared identifier 'UI_MI_STEP_ENERGY'
   1 use of undeclared identifier 'UI_MI_STEP_RADAR'
   6 use of undeclared identifier 'UI_MI_STEP_SOLAR'
   1 use of undeclared identifier 'UI_MI_STEP_WEATHER'; did you mean 'LS_SYNC_STEP_WEATHER'?
   5 use of undeclared identifier 'UI_MI_SYNC_STEPS'
```

- [ ] **Step 3: The items and their labels.** A section may hold a section: Back from Steps returns to Sync with the cursor on Steps.

`components/ui/include/ui_menu.h`:

```diff
--- a/components/ui/include/ui_menu.h
+++ b/components/ui/include/ui_menu.h
@@ -30,6 +30,12 @@ typedef enum {
     UI_MI_SYNC_MODE,     /* choice: times, interval, always, manual (spec §9.3) */
     UI_MI_SYNC_INTERVAL, /* choice: interval labels; shown in interval mode */
     UI_MI_QUIET_HOURS,   /* toggle (D25) */
+    UI_MI_SYNC_STEPS,    /* section (M6d, D35): a switch for each data step (spec §9.3) */
+    UI_MI_STEP_WEATHER,  /* toggles, in settings_step_t order */
+    UI_MI_STEP_AIR,
+    UI_MI_STEP_RADAR,
+    UI_MI_STEP_SOLAR,
+    UI_MI_STEP_ENERGY,
     UI_MI_TIME,
     UI_MI_SET_DATETIME, /* the date-time editor */
     UI_MI_CLOCK_24H,    /* toggle */
```


`components/ui/ui_menu.c`:

```diff
--- a/components/ui/ui_menu.c
+++ b/components/ui/ui_menu.c
@@ -44,6 +44,12 @@ static const node_t k_nodes[UI_MI_COUNT] = {
     [UI_MI_SYNC_MODE] = { .label = LS_M_SYNC_MODE, .kind = K_CHOICE, .parent = UI_MI_SYNC },
     [UI_MI_SYNC_INTERVAL] = { .label = LS_M_SYNC_INTERVAL, .kind = K_CHOICE, .parent = UI_MI_SYNC },
     [UI_MI_QUIET_HOURS] = { .label = LS_M_QUIET_HOURS, .kind = K_TOGGLE, .parent = UI_MI_SYNC },
+    [UI_MI_SYNC_STEPS] = { .label = LS_M_SYNC_STEPS, .kind = K_SECTION, .parent = UI_MI_SYNC },
+    [UI_MI_STEP_WEATHER] = { .label = LS_SYNC_STEP_WEATHER, .kind = K_TOGGLE, .parent = UI_MI_SYNC_STEPS },
+    [UI_MI_STEP_AIR] = { .label = LS_SYNC_STEP_AIR, .kind = K_TOGGLE, .parent = UI_MI_SYNC_STEPS },
+    [UI_MI_STEP_RADAR] = { .label = LS_SYNC_STEP_RADAR, .kind = K_TOGGLE, .parent = UI_MI_SYNC_STEPS },
+    [UI_MI_STEP_SOLAR] = { .label = LS_SYNC_STEP_SOLAR, .kind = K_TOGGLE, .parent = UI_MI_SYNC_STEPS },
+    [UI_MI_STEP_ENERGY] = { .label = LS_SYNC_STEP_ENERGY, .kind = K_TOGGLE, .parent = UI_MI_SYNC_STEPS },
     [UI_MI_TIME] = { .label = LS_M_TIME, .kind = K_SECTION, .parent = UI_MI_ROOT },
     [UI_MI_SET_DATETIME] = { .label = LS_M_SET_DATETIME, .kind = K_DATETIME, .parent = UI_MI_TIME },
     [UI_MI_CLOCK_24H] = { .label = LS_M_CLOCK_24H, .kind = K_TOGGLE, .parent = UI_MI_TIME },
```


`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -162,6 +162,8 @@ typedef enum {
     LS_SYNC_STEP_WEATHER,
     LS_SYNC_STEP_AIR,
     LS_SYNC_STEP_RADAR,
+    LS_SYNC_STEP_SOLAR,  /* M6d (D35, D36) */
+    LS_SYNC_STEP_ENERGY,
     LS_SYNC_NEVER,
     LS_SYNC_RUNNING,
     LS_T_SYNC_STARTED,
@@ -224,6 +226,7 @@ typedef enum {
     LS_EN_IMPORTED,
     LS_NO_SOLAR,
     LS_NO_ENERGY,
+    LS_M_SYNC_STEPS, /* Sync ▸ Steps (M6d, D35) */
     LS_COUNT,
 } lang_str_t;
 
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -173,6 +173,8 @@ const lang_t lang_en = {
         [LS_SYNC_STEP_WEATHER] = "Weather",
         [LS_SYNC_STEP_AIR] = "Air quality",
         [LS_SYNC_STEP_RADAR] = "Radar",
+        [LS_SYNC_STEP_SOLAR] = "Solar forecast",
+        [LS_SYNC_STEP_ENERGY] = "House energy",
         [LS_SYNC_NEVER] = "Never",
         [LS_SYNC_RUNNING] = "Running",
         [LS_T_SYNC_STARTED] = "Syncing…",
@@ -235,6 +237,7 @@ const lang_t lang_en = {
         [LS_EN_IMPORTED] = "From grid",
         [LS_NO_SOLAR] = "No solar forecast yet",
         [LS_NO_ENERGY] = "No data from the inverter yet",
+        [LS_M_SYNC_STEPS] = "Steps",
     },
     .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
     .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -213,6 +213,8 @@ const lang_t lang_cs = {
         [LS_SYNC_STEP_WEATHER] = "Počasí",
         [LS_SYNC_STEP_AIR] = "Ovzduší",
         [LS_SYNC_STEP_RADAR] = "Radar",
+        [LS_SYNC_STEP_SOLAR] = "Předpověď FVE",
+        [LS_SYNC_STEP_ENERGY] = "Energie domu",
         [LS_SYNC_NEVER] = "Nikdy",
         [LS_SYNC_RUNNING] = "Probíhá",
         [LS_T_SYNC_STARTED] = "Synchronizuji…",
@@ -275,6 +277,7 @@ const lang_t lang_cs = {
         [LS_EN_IMPORTED] = "Ze sítě",
         [LS_NO_SOLAR] = "Předpověď FVE zatím není",
         [LS_NO_ENERGY] = "Ze střídače zatím nic",
+        [LS_M_SYNC_STEPS] = "Kroky",
     },
     .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
     .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
```


- [ ] **Step 4: The app.** The switches read and write the bits:

`main/app_menu.c`:

```diff
--- a/main/app_menu.c
+++ b/main/app_menu.c
@@ -26,6 +26,9 @@
 
 static const char *TAG = "app_menu";
 
+_Static_assert(UI_MI_STEP_ENERGY - UI_MI_STEP_WEATHER == 4 && SETTINGS_STEP_ENERGY == 1 << 4,
+               "Sync ▸ Steps follows settings_step_t: a switch a bit");
+
 static int64_t s_closed_ms; /* app_uptime_ms() when the menu last closed */
 
 /* Gesture timings (spec §5.6): the dashboard binds both double presses (BOOT's since M6b, D31) and
@@ -164,6 +167,9 @@ static void build_model(void)
     m->value[UI_MI_SYNC_INTERVAL] = nearest_index(k_sync_min, SYNC_STEPS, set->sync_interval_min);
     m->hidden[UI_MI_SYNC_INTERVAL] = set->sync_mode != SETTINGS_SYNC_INTERVAL;
     m->value[UI_MI_QUIET_HOURS] = set->quiet;
+    for (int i = 0; i < 5; i++) { /* Sync ▸ Steps (D35), in settings_step_t order */
+        m->value[UI_MI_STEP_WEATHER + i] = (set->sync_steps >> i) & 1;
+    }
 
     m->value[UI_MI_UPDATE_INTERVAL] = set->display_every_min;
     int rate = 0;
@@ -298,6 +304,16 @@ static void apply(const ui_menu_intent_t *in)
             set->quiet = in->value != 0;
             save_settings = retime = true;
             break;
+        case UI_MI_STEP_WEATHER:
+        case UI_MI_STEP_AIR:
+        case UI_MI_STEP_RADAR:
+        case UI_MI_STEP_SOLAR:
+        case UI_MI_STEP_ENERGY: { /* a step that is off makes no requests, its refreshes in `always` included */
+            uint8_t bit = (uint8_t)(1u << (in->item - UI_MI_STEP_WEATHER));
+            set->sync_steps = (uint8_t)(in->value ? set->sync_steps | bit : set->sync_steps & ~bit);
+            save_settings = true;
+            break;
+        }
         case UI_MI_REFRESH_RATE:
             set->lpm_quarter_hz = k_quarter_hz[in->value < RATE_COUNT ? in->value : 2];
             apply_settings = save_settings = true;
```


- [ ] **Step 5: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_ui_menu | tail -2 && ctest --test-dir build-host | tail -3`
Expected:

```
15 Tests 0 Failures 0 Ignored
OK
100% tests passed, 0 tests failed out of 64
```

- [ ] **Step 6: The firmware builds:** `tools/idf.sh build`, clean, without a warning.

- [ ] **Step 7: Commit.**

```bash
git add components/locale components/ui main/app_menu.c test/host/test_ui_menu.c
git commit -m "feat(ui): Sync ▸ Steps in the menu"
```

### Task 9: The solar state, `solar.bin`, the keys in NVS and the `solar` commands (`main`, `solar`)

**Files:**
- Create: `main/app_solar.c`, `main/app_secrets.c`
- Modify: `components/solar/include/solar.h`, `components/solar/solar.c`, `main/CMakeLists.txt`, `main/app.c`, `main/app_cmds.c`, `main/app_internal.h`, `main/app_ui.c`
- Test: `test/host/test_solar.c`

**Interfaces:**
- Consumes: Tasks 1–5: `solar_acc_finish()`, `energy_day_add()`, `energy_battery_shown()`, `ui_solar_demo()`, `ui_solar_t`; `app_sync_expected_s()`; `storage_write_atomic()`, `util_snapshot_seal()`.
- Produces (Tasks 10, 11):
  - `solar_source_t` (`SOLAR_OFF`, `SOLAR_OPEN_METEO`, `SOLAR_FORECAST_SOLAR`, `SOLAR_SOLCAST`) and `uint32_t solar_fresh_s(source, sites, expected_s)` (solar.h): the expected sync interval, or Solcast's wait where that is longer, and 2 h; 0 without an interval;
  - `app_solar_state_t` (app_internal.h), in `app_ui_state_t` and so in the RTC snapshot: the forecast, Solcast's last call and sites, the last Solar step's time, its last call's error and why it kept the forecast, the reading, the day, the last Energy step's time and error, when `solar.bin` took the readings, and `demo`;
  - `const ui_solar_t *app_solar_ui(void)`, `const app_solar_state_t *app_solar_state(void)`, `void app_solar_forecast_done(acc, error, kept, asked, sites)` (a kept step says why in `kept` and leaves the last call's error be), `void app_solar_reading_done(r, error)`, `void app_solar_restore(void)`, `void app_solar_demo(bool on)`, `void app_solar_budget_reset(void)`;
  - `bool app_secret_get(which, out, size)`, `bool app_secret_set(which)`, `esp_err_t app_secrets_apply(const settings_secrets_t *)` (NVS `secrets`; never a value in a log; a new Solcast key or site starts its budget over, Task 4's `settings_secrets_solcast()`);
  - console `solar status`, `solar demo on`, `solar demo off`.

`/fs/state/solar.bin` (magic "rfsl", version 1, a CRC-32) holds the forecast, Solcast's last call and sites, the reading and the day: written after each Solar step that changed something (spec §11.5: a step that kept everything writes nothing), and after a reading at most every 30 min (spec §11.6); read at a cold boot, after the datastore's. Its buffers and the demo's sit in PSRAM. The snapshot's version goes to 10: the solar state now, and Task 10's two steps and their details in the syncs' state. `solar demo on` draws the sample day for today until now, with a battery when `energy.battery` is `on`, from its own buffers: the real state is saved as ever while it shows, the demo never; a restart ends it.

- [ ] **Step 1: Write the failing test.** The forecast's freshness:

`test/host/test_solar.c`:

```diff
--- a/test/host/test_solar.c
+++ b/test/host/test_solar.c
@@ -569,6 +569,17 @@ static void test_solcast_keeps_within_its_ten_calls_a_day(void)
     TEST_ASSERT_EQUAL_UINT32(6 * 3600, solar_solcast_wait_s(2));
 }
 
+/* pv.* are fresh as the weather is (spec §5.1), Solcast's wait in place of a shorter sync interval. */
+static void test_the_forecast_is_fresh_for_the_sync_interval_and_two_hours(void)
+{
+    TEST_ASSERT_EQUAL_UINT32(26 * 3600, solar_fresh_s(SOLAR_OPEN_METEO, 1, 24 * 3600)); /* one sync a day */
+    TEST_ASSERT_EQUAL_UINT32(3 * 3600, solar_fresh_s(SOLAR_FORECAST_SOLAR, 1, 3600));
+    TEST_ASSERT_EQUAL_UINT32(5 * 3600, solar_fresh_s(SOLAR_SOLCAST, 1, 3600)); /* it waits 3 h between calls */
+    TEST_ASSERT_EQUAL_UINT32(8 * 3600, solar_fresh_s(SOLAR_SOLCAST, 2, 3600)); /* and 6 h with two sites */
+    TEST_ASSERT_EQUAL_UINT32(26 * 3600, solar_fresh_s(SOLAR_SOLCAST, 2, 24 * 3600));
+    TEST_ASSERT_EQUAL_UINT32(0, solar_fresh_s(SOLAR_SOLCAST, 1, 0)); /* sync mode manual: never stale */
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -604,5 +615,6 @@ int main(void)
     RUN_TEST(test_two_solcast_sites_add_up);
     RUN_TEST(test_solcast_refusals);
     RUN_TEST(test_solcast_keeps_within_its_ten_calls_a_day);
+    RUN_TEST(test_the_forecast_is_fresh_for_the_sync_interval_and_two_hours);
     return UNITY_END();
 }
```


- [ ] **Step 2: Run it to see it fail.**

Run: `cmake --build build-host --target test_solar 2>&1 | grep -E 'error:' | sed -E 's/.*error: //' | sort | uniq -c`
Expected:

```
   1 call to undeclared function 'solar_fresh_s'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
   1 use of undeclared identifier 'SOLAR_FORECAST_SOLAR'
   1 use of undeclared identifier 'SOLAR_OPEN_METEO'
   4 use of undeclared identifier 'SOLAR_SOLCAST'
```

- [ ] **Step 3: The source and the freshness.**

`components/solar/include/solar.h`:

```diff
--- a/components/solar/include/solar.h
+++ b/components/solar/include/solar.h
@@ -19,6 +19,9 @@
 #define SOLAR_URL_MAX 320
 #define SOLAR_KEY_MAX 64 /* a key or a site id, NUL included */
 
+/* solar.source; the values match settings_solar_source_t. */
+typedef enum { SOLAR_OFF, SOLAR_OPEN_METEO, SOLAR_FORECAST_SOLAR, SOLAR_SOLCAST } solar_source_t;
+
 /* A roof plane (spec §11.5): azimuth 0 south, -90 east, 90 west. */
 typedef struct {
     float kwp;   /* 0.1..100 */
@@ -94,3 +97,6 @@ bool solar_parse_solcast(const char *json, size_t len, solar_acc_t *acc, char *e
 uint32_t solar_solcast_wait_s(int sites);
 /* Whether a sync may ask now, given when it last asked (0 for never; failed calls count). */
 bool solar_solcast_due(uint32_t last_asked, int sites, uint32_t now);
+/* How long a forecast stays fresh (spec §5.1): the expected sync interval, or Solcast's wait where that is
+ * longer, and 2 h; 0, never stale, without an expected interval (sync mode `manual`). */
+uint32_t solar_fresh_s(solar_source_t source, int sites, uint32_t expected_s);
```


`components/solar/solar.c`:

```diff
--- a/components/solar/solar.c
+++ b/components/solar/solar.c
@@ -202,3 +202,12 @@ bool solar_solcast_due(uint32_t last_asked, int sites, uint32_t now)
 {
     return last_asked == 0 || now < last_asked || now - last_asked >= solar_solcast_wait_s(sites);
 }
+
+uint32_t solar_fresh_s(solar_source_t source, int sites, uint32_t expected_s)
+{
+    if (expected_s == 0) {
+        return 0;
+    }
+    uint32_t wait = source == SOLAR_SOLCAST ? solar_solcast_wait_s(sites) : 0;
+    return (expected_s > wait ? expected_s : wait) + 2 * 3600;
+}
```


Run: `cmake --build build-host && ./build-host/test_solar | tail -2`
Expected: `33 Tests 0 Failures 0 Ignored`, `OK`.

- [ ] **Step 4: The keys in NVS.**

`main/app_secrets.c`:

```c
#include <string.h>

#include "app_internal.h"
#include "esp_log.h"
#include "nvs.h"

/* The PV forecast's and the house's keys in NVS `secrets` (spec §14.2, M6d): written from the web UI,
 * never returned or logged; a factory reset erases them with the namespace. NVS must be up: a routine
 * deep-sleep wake brings it up through app_net_init() before a sync (gotcha 31). */

static const char *TAG = "app_secrets";

#define NAMESPACE "secrets"

bool app_secret_get(settings_secret_t which, char *out, size_t size)
{
    out[0] = '\0';
    nvs_handle_t nvs;
    if (nvs_open(NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t n = size;
    esp_err_t err = nvs_get_str(nvs, settings_secret_key(which), out, &n);
    nvs_close(nvs);
    if (err != ESP_OK) {
        out[0] = '\0';
    }
    return out[0] != '\0';
}

bool app_secret_set(settings_secret_t which)
{
    char value[SETTINGS_SECRET_LEN];
    bool set = app_secret_get(which, value, sizeof(value));
    memset(value, 0, sizeof(value));
    return set;
}

esp_err_t app_secrets_apply(const settings_secrets_t *secrets)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open: %s", esp_err_to_name(err));
        return err;
    }
    for (int i = 0; i < SETTINGS_SECRET_COUNT && err == ESP_OK; i++) {
        if (!secrets->given[i]) {
            continue;
        }
        const char *key = settings_secret_key((settings_secret_t)i);
        if (secrets->value[i][0] != '\0') {
            err = nvs_set_str(nvs, key, secrets->value[i]);
        } else {
            err = nvs_erase_key(nvs, key);
            err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err; /* clearing one that isn't there */
        }
        ESP_LOGI(TAG, "%s %s", key, secrets->value[i][0] != '\0' ? "set" : "cleared"); /* never the value */
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write: %s", esp_err_to_name(err));
    } else if (settings_secrets_solcast(secrets)) {
        app_solar_budget_reset(); /* a new key or site: the next step or check asks at once (spec §11.5) */
    }
    return err;
}
```


- [ ] **Step 5: The solar state, its file and its view.**

`main/app_internal.h`:

```diff
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -7,16 +7,19 @@
 #include "adsb_task.h"
 #include "board_buttons.h"
 #include "datastore.h"
+#include "energy.h"
 #include "esp_err.h"
 #include "radar_fetch.h"
 #include "scheduler.h"
 #include "settings.h"
+#include "solar.h"
 #include "sync.h"
 #include "sync_plan.h"
 #include "ui_fields.h"
 #include "ui_menu.h"
 #include "ui_preset.h"
 #include "ui_radar.h"
+#include "ui_solar.h"
 #include "webui.h"
 
 /* The dashboard's state and behaviour (main/app_ui.c, main/app_menu.c). All of it belongs to the
@@ -32,6 +35,23 @@ typedef struct {
     char last_detail[SYNC_DETAIL_LEN];
 } app_sync_state_t;
 
+/* The PV forecast and the house's energy (main/app_solar.c, spec §11.5, §11.6): kept through deep sleep
+ * with the rest, and in /fs/state/solar.bin. */
+typedef struct {
+    solar_forecast_t forecast;            /* day 0: none yet */
+    uint32_t solcast_asked;               /* when Solcast was last asked (UTC), its budget's clock */
+    uint8_t solcast_sites;                /* its sites at the last step: its wait (spec §11.5) */
+    uint32_t forecast_tried;              /* when the last Solar step ran (UTC); 0: none since the cold boot */
+    char forecast_error[SYNC_DETAIL_LEN]; /* why its last call failed; "" after a good one; a kept step leaves it */
+    char forecast_kept[SYNC_DETAIL_LEN];  /* why the last step kept it ("kept", "HTTP 429"); "" if it didn't */
+    energy_reading_t reading;             /* at 0: none yet */
+    energy_day_t day;                     /* today's totals and quarter hours */
+    uint32_t energy_tried;                /* when the last Energy step ran (UTC) */
+    char energy_error[SYNC_DETAIL_LEN];
+    uint32_t saved_at;                    /* when solar.bin last took the readings (UTC) */
+    bool demo;                            /* `solar demo on`: the sample day (spec §15) */
+} app_solar_state_t;
+
 typedef struct {
     ds_t ds;
     settings_t settings;
@@ -44,6 +64,7 @@ typedef struct {
     bool critical;        /* the critical-battery screen is up (spec §8) */
     bool first_run;       /* settings.json didn't exist at boot: the first-run screen (spec §5.5) */
     app_sync_state_t sync;
+    app_solar_state_t solar; /* M6d */
 } app_ui_state_t;
 
 /* Kconfig settings, the built-in presets and an empty datastore. */
@@ -146,6 +167,27 @@ uint32_t app_sync_expected_s(void);
 /* Info ▸ Last sync: "12:05 OK", "05:30 Weather: HTTP 503", "Running", "Never". */
 void app_sync_summary(char *out, size_t size);
 
+/* The PV forecast and the house's energy (main/app_solar.c, M6d). */
+const ui_solar_t *app_solar_ui(void); /* what a render draws now: the state, or the sample day */
+const app_solar_state_t *app_solar_state(void);
+/* A Solar step that ran: the reply in `acc`, or NULL with why it failed (`error`) or kept the forecast (`kept`,
+ * which leaves the last call's error be); `asked` when it asked Solcast (0: it didn't), `sites` Solcast's (0: as
+ * they were). Saved to solar.bin, unless the step kept everything. */
+void app_solar_forecast_done(const solar_acc_t *acc, const char *error, const char *kept, uint32_t asked,
+                             uint8_t sites);
+/* A new Solcast key or site (spec §11.5): its budget starts over, so the next step or check asks at once. */
+void app_solar_budget_reset(void);
+/* An Energy step or refresh that ran: the reading, or NULL and why. Into today's totals, and solar.bin at most
+ * every 30 min. */
+void app_solar_reading_done(const energy_reading_t *r, const char *error);
+void app_solar_restore(void); /* a cold boot: solar.bin */
+void app_solar_demo(bool on); /* `solar demo on|off` */
+
+/* The keys in NVS `secrets` (main/app_secrets.c, spec §14.2): write-only from the web UI, never logged. */
+bool app_secret_get(settings_secret_t which, char *out, size_t size); /* false and "" when unset */
+bool app_secret_set(settings_secret_t which);
+esp_err_t app_secrets_apply(const settings_secrets_t *secrets); /* the given ones written, "" erased */
+
 /* The weather radar (main/app_radar.c, spec §11.2): its frames, kept in PSRAM, the newest also in
  * /fs/state/radar.bin; the sync task fetches them. */
 typedef struct {
```


`main/app_solar.c`:

```c
#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_internal.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "storage.h"
#include "ui_solar.h"
#include "util_snapshot.h"
#include "util_time.h"

/* The PV forecast and the house's energy (spec §11.5, §11.6, M6d): what the Solar and Energy steps
 * bring, kept with the app's state through deep sleep and in /fs/state/solar.bin, and the view the
 * dashboard draws. It belongs to the app task. */

#define SOLAR_PATH "/fs/state/solar.bin"
#define SOLAR_MAGIC 0x7266736cu /* "rfsl" */
#define SOLAR_FILE_VERSION 1
#define READINGS_SAVE_S (30 * 60) /* spec §11.6: the readings go to the file at most every 30 min */

static const char *TAG = "app_solar";

typedef struct {
    util_snapshot_hdr_t hdr;
    solar_forecast_t forecast;
    uint32_t solcast_asked;
    uint8_t solcast_sites; /* its wait, so a two-site forecast stays fresh 6 h after a cold boot */
    energy_reading_t reading;
    energy_day_t day;
} solar_file_t;

static app_solar_state_t *st(void)
{
    return &app_state()->solar;
}

/* The real state, the demo's never being in it (it draws from its own sample day). */
static void save(void)
{
    EXT_RAM_BSS_ATTR static solar_file_t f;
    app_solar_state_t *s = st();
    if (storage_init() != ESP_OK) {
        return;
    }
    memset(&f, 0, sizeof(f));
    f.forecast = s->forecast;
    f.solcast_asked = s->solcast_asked;
    f.solcast_sites = s->solcast_sites;
    f.reading = s->reading;
    f.day = s->day;
    util_snapshot_seal(&f, sizeof(f), SOLAR_MAGIC, SOLAR_FILE_VERSION);
    esp_err_t err = storage_write_atomic(SOLAR_PATH, (const char *)&f, sizeof(f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s not saved: %s", SOLAR_PATH, esp_err_to_name(err));
        return;
    }
    s->saved_at = (uint32_t)time(NULL);
}

void app_solar_restore(void)
{
    EXT_RAM_BSS_ATTR static solar_file_t f;
    if (!storage_ready()) {
        return;
    }
    FILE *file = fopen(SOLAR_PATH, "rb");
    if (file == NULL) {
        return;
    }
    size_t n = fread(&f, 1, sizeof(f), file);
    fclose(file);
    if (n != sizeof(f) || !util_snapshot_valid(&f, sizeof(f), SOLAR_MAGIC, SOLAR_FILE_VERSION)) {
        ESP_LOGW(TAG, "%s: not of this firmware; left out", SOLAR_PATH);
        return;
    }
    app_solar_state_t *s = st();
    s->forecast = f.forecast;
    s->solcast_asked = f.solcast_asked;
    s->solcast_sites = f.solcast_sites;
    s->reading = f.reading;
    s->day = f.day;
    s->saved_at = (uint32_t)time(NULL);
    ESP_LOGI(TAG, "forecast from %lu and reading from %lu restored", (unsigned long)f.forecast.fetched,
             (unsigned long)f.reading.at);
}

void app_solar_forecast_done(const solar_acc_t *acc, const char *error, const char *kept, uint32_t asked,
                             uint8_t sites)
{
    app_solar_state_t *s = st();
    uint32_t now = (uint32_t)time(NULL);
    s->forecast_tried = now;
    snprintf(s->forecast_kept, sizeof(s->forecast_kept), "%s", kept != NULL ? kept : "");
    if (kept != NULL && asked == 0) {
        return; /* nothing new to keep: the last call's error and the file stay as they are */
    }
    if (sites != 0) {
        s->solcast_sites = sites;
    }
    if (asked != 0) {
        s->solcast_asked = asked; /* failed calls count against the budget too (spec §11.5) */
    }
    if (acc != NULL) {
        solar_acc_finish(acc, &s->forecast, now, &s->forecast); /* gaps keep the last forecast's quarter hours */
    }
    if (kept == NULL) {
        snprintf(s->forecast_error, sizeof(s->forecast_error), "%s", error != NULL ? error : "");
    }
    save(); /* spec §11.5: after each Solar step */
}

void app_solar_budget_reset(void)
{
    st()->solcast_asked = 0;
    save();
}

void app_solar_reading_done(const energy_reading_t *r, const char *error)
{
    app_solar_state_t *s = st();
    uint32_t now = (uint32_t)time(NULL);
    s->energy_tried = now;
    snprintf(s->energy_error, sizeof(s->energy_error), "%s", error != NULL ? error : "");
    if (r == NULL) {
        return;
    }
    s->reading = *r;
    energy_day_add(&s->day, r);
    if (s->saved_at == 0 || now < s->saved_at || now - s->saved_at >= READINGS_SAVE_S) {
        save();
    }
}

void app_solar_demo(bool on)
{
    st()->demo = on;
    ESP_LOGI(TAG, "the sample day %s", on ? "shows" : "is gone");
}

/* The local midnight of `now` and its day. */
static int32_t today(time_t now, time_t *midnight)
{
    struct tm lt;
    localtime_r(&now, &lt);
    struct tm m = { .tm_year = lt.tm_year, .tm_mon = lt.tm_mon, .tm_mday = lt.tm_mday, .tm_isdst = -1 };
    *midnight = mktime(&m);
    return (int32_t)util_days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

const ui_solar_t *app_solar_ui(void)
{
    static ui_solar_t view;
    EXT_RAM_BSS_ATTR static solar_forecast_t demo_forecast;
    EXT_RAM_BSS_ATTR static energy_reading_t demo_reading;
    EXT_RAM_BSS_ATTR static energy_day_t demo_day;
    const settings_t *set = app_settings();
    app_solar_state_t *s = st();
    if (s->demo) { /* spec §15: the goldens' sample day, today, its readings until now */
        time_t now = time(NULL), midnight;
        int32_t day = today(now, &midnight);
        bool battery = set->energy_battery == SETTINGS_BATTERY_ON;
        ui_solar_demo(day, midnight, now, true, battery, &demo_forecast, &demo_reading, &demo_day);
        view = (ui_solar_t){ .forecast = &demo_forecast, .reading = &demo_reading, .day = &demo_day,
                             .battery = battery };
        return &view;
    }
    view = (ui_solar_t){
        .forecast = &s->forecast,
        .forecast_ttl_s = solar_fresh_s((solar_source_t)set->solar_source, s->solcast_sites, app_sync_expected_s()),
        .reading = &s->reading,
        .day = &s->day,
        .battery = energy_battery_shown((energy_battery_t)set->energy_battery, &s->reading),
    };
    return &view;
}

const app_solar_state_t *app_solar_state(void)
{
    return st();
}
```


`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -225,6 +225,7 @@ void app_ui_context(ui_context_t *ctx)
     localtime_r(&now, &ctx->local);
     ctx->local_day = local_day(&ctx->local);
     ctx->radar = app_radar_ui(); /* M6 */
+    ctx->solar = app_solar_ui(); /* M6d */
 }
 
 void app_ui_render(void)
```


`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -49,8 +49,8 @@
 #define TETHER_RECHECK_MS 1000
 #define RETRY_S           300  /* after a failed boot with no PC attached */
 #define SNAP_MAGIC        0x72666c62u /* "rflb" */
-#define SNAP_VERSION      9 /* 6: the weather, the air quality and the syncs' state; 7: the rain; 8: split presets;
-                                  9: 24 cells (M6c) */
+#define SNAP_VERSION      10 /* 6: the weather, the air quality and the syncs' state; 7: the rain; 8: split presets;
+                                   9: 24 cells (M6c); 10: the solar state and the sync's two steps (M6d) */
 #define PEEK_MS           60000 /* a button during the night shows the dashboard this long (spec §9.1) */
 #define NIGHT_RECHECK_S   60    /* a night sleep with a button held looks again this often (D16) */
 #define CRITICAL_RECHECK_S 600  /* the critical sleep checks again this often if KEY is held */
@@ -588,6 +588,7 @@ static esp_err_t boot(void)
         app_ui_defaults();
         app_ui_load();
         app_ui_restore_forecast(); /* spec §6: shown as stale by its age */
+        app_solar_restore();       /* spec §11.5, §11.6: the forecast and today's readings */
     }
 
     ESP_RETURN_ON_ERROR(board_init(wake == POWER_WAKE_COLD), TAG, "board");
```


`main/CMakeLists.txt`:

```diff
--- a/main/CMakeLists.txt
+++ b/main/CMakeLists.txt
@@ -1,7 +1,7 @@
 idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_menu.c" "app_cmds.c" "app_config.c" "app_web.c" "app_sync.c"
-                            "app_radar.c" "app_flights.c"
+                            "app_radar.c" "app_flights.c" "app_solar.c" "app_secrets.c"
                        INCLUDE_DIRS "."
-                       PRIV_REQUIRES adsb app_update board console datastore diag display esp_app_format
+                       PRIV_REQUIRES adsb app_update board console datastore diag display energy esp_app_format
                                      esp_driver_gpio esp_timer espcoredump gfx heap json locale map netmgr nvs_flash
-                                     power radar rtc scheduler sensors st7305 storage sync timekeeping ui util weather
-                                     webui)
+                                     power radar rtc scheduler sensors solar st7305 storage sync timekeeping ui util
+                                     weather webui)
```


- [ ] **Step 6: The commands.** `solar status` prints the source, the forecast's fetch, its days and its last step (why it kept the forecast, the last call's error), Solcast's last call and sites, the reading, its day's totals, midnight's reading and the last Energy step; never a key.

`main/app_cmds.c`:

```diff
--- a/main/app_cmds.c
+++ b/main/app_cmds.c
@@ -11,6 +11,7 @@
 #include "netmgr.h"
 #include "ui_fields.h"
 #include "ui_layout.h"
+#include "util_time.h"
 
 /* `field` and `preset` (spec §15): inspect the dashboard and inject test data on the device. */
 
@@ -398,6 +399,84 @@ static int cmd_radar(int argc, char **argv)
     return diag_on_owner(radar_body, argc, argv);
 }
 
+/* "18.4 kWh", or "-" for a day without one. */
+static void print_wh(const char *label, uint32_t wh)
+{
+    if (wh == SOLAR_WH_NONE) {
+        printf(" %s -", label);
+    } else {
+        printf(" %s %lu.%lu kWh", label, (unsigned long)(wh / 1000), (unsigned long)(wh % 1000 / 100));
+    }
+}
+
+/* `solar status | demo on | demo off` (spec §15, M6d): never a key. */
+static int solar_body(int argc, char **argv)
+{
+    static const char *const k_usage = "solar status | solar demo on | solar demo off";
+    if (argc == 3 && strcmp(argv[1], "demo") == 0 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
+        app_solar_demo(strcmp(argv[2], "on") == 0);
+        app_ui_render();
+        printf("solar: the sample day %s\n", strcmp(argv[2], "on") == 0 ? "shows" : "is gone");
+        return 0;
+    }
+    if (argc != 2 || strcmp(argv[1], "status") != 0) {
+        return usage(k_usage);
+    }
+    static const char *const k_sources[] = { "off", "open-meteo", "forecast-solar", "solcast" };
+    const settings_t *set = app_settings();
+    const app_solar_state_t *s = app_solar_state();
+    printf("forecast: %s%s\n", k_sources[set->solar_source <= SETTINGS_SOLAR_SOLCAST ? set->solar_source : 0],
+           s->demo ? " (the sample day shows)" : "");
+    print_time("  fetched", (time_t)s->forecast.fetched);
+    if (s->forecast.day != 0) {
+        int y, m, d;
+        util_civil_from_days(s->forecast.day, &y, &m, &d);
+        printf("  from %04d-%02d-%02d:", y, m, d);
+        print_wh("first", s->forecast.wh[0]);
+        print_wh("next", s->forecast.wh[1]);
+        print_wh("after", s->forecast.wh[2]);
+        printf("\n");
+    }
+    print_time("  last step", (time_t)s->forecast_tried);
+    if (s->forecast_kept[0] != '\0') {
+        printf("  kept: %s\n", s->forecast_kept);
+    }
+    if (s->forecast_error[0] != '\0') {
+        printf("  last call's error: %s\n", s->forecast_error);
+    }
+    if (set->solar_source == SETTINGS_SOLAR_SOLCAST) {
+        print_time("  Solcast asked", (time_t)s->solcast_asked);
+        printf("  its sites: %u\n", s->solcast_sites);
+    }
+    printf("energy: %s, battery %s\n", set->energy_source == SETTINGS_ENERGY_SOLAX ? "solax" : "off",
+           set->energy_battery == SETTINGS_BATTERY_ON ? "on" : set->energy_battery == SETTINGS_BATTERY_OFF ? "off"
+                                                                                                          : "auto");
+    const energy_reading_t *r = &s->reading;
+    print_time("  reading", (time_t)r->at);
+    if (r->at != 0) {
+        printf("  solar %ld W, grid %ld W, home %ld W, battery %ld W at %d %%, inverter type %u\n", (long)r->pv_w,
+               (long)r->grid_w, (long)r->load_w, (long)r->bat_w, r->soc, r->inverter);
+        int32_t day = energy_reading_day(r);
+        uint32_t out = energy_to_grid_wh(&s->day, r, day), in = energy_from_grid_wh(&s->day, r, day);
+        printf("  its day:");
+        print_wh("produced", r->yield_wh);
+        print_wh("to the grid", out == ENERGY_WH_NONE ? SOLAR_WH_NONE : out);
+        print_wh("from it", in == ENERGY_WH_NONE ? SOLAR_WH_NONE : in);
+        printf("\n");
+        print_time("  midnight's reading", (time_t)s->day.base_at);
+    }
+    print_time("  last step", (time_t)s->energy_tried);
+    if (s->energy_error[0] != '\0') {
+        printf("  error: %s\n", s->energy_error);
+    }
+    return 0;
+}
+
+static int cmd_solar(int argc, char **argv)
+{
+    return diag_on_owner(solar_body, argc, argv);
+}
+
 void app_register_commands(void)
 {
     const esp_console_cmd_t cmds[] = {
@@ -409,6 +488,7 @@ void app_register_commands(void)
         { .command = "wifi", .help = "wifi status | scan", .func = &cmd_wifi },
         { .command = "sync", .help = "sync now | status (spec §9.3)", .func = &cmd_sync },
         { .command = "radar", .help = "radar status | loop (spec §11.2, §11.3)", .func = &cmd_radar },
+        { .command = "solar", .help = "solar status | demo on | demo off (spec §11.5, §11.6)", .func = &cmd_solar },
     };
     for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
         esp_err_t err = esp_console_cmd_register(&cmds[i]);
```


- [ ] **Step 7: The firmware builds** (`tools/idf.sh build`), clean, without a warning, and the snapshot fits its 6 KB:

Run: `tools/idf.sh exec bash -c 'xtensa-esp32s3-elf-nm -S build/reflbo.elf | grep " s_snap$"; xtensa-esp32s3-elf-size -A build/reflbo.elf | grep -E "rtc.data|force_slow"'`
Expected: `s_snap` of `0x15a0` bytes (5 536; 4 664 before; Task 10's details bring it to 5 680); `.rtc.data` 5656 and `.rtc.force_slow` 32 of the 8 192 bytes.

- [ ] **Step 8: Commit.**

```bash
git add components/solar main test/host/test_solar.c
git commit -m "feat: the solar state, solar.bin, the keys in NVS and the solar commands"
```

### Task 10: The Solar and Energy steps, the steps' switches and the energy refresh (`sync`, `fetch`, `util`, `main`)

**Files:**
- Create: `components/util/include/util_url.h`, `components/util/util_url.c`
- Modify: `components/util/CMakeLists.txt`, `components/fetch/include/fetch.h`, `components/fetch/fetch.c`, `components/fetch/CMakeLists.txt`, `components/sync/include/sync_plan.h`, `components/sync/sync_plan.c`, `components/sync/include/sync.h`, `components/sync/sync.c`, `components/sync/CMakeLists.txt`, `main/app_internal.h`, `main/app_solar.c`, `main/app_sync.c`, `main/app_cmds.c`, `main/app_web.c`, `test/host/CMakeLists.txt`
- Test: `test/host/test_sync_plan.c`, `test/host/test_util_url.c`

**Interfaces:**
- Consumes: Tasks 1–4 and 9.
- Produces (Task 11):
  - `sync_step_t` and `sync_step_result_t` move to `sync_plan.h`, with `SYNC_STEP_SOLAR`, `SYNC_STEP_ENERGY` and `SYNC_STEP_KEPT`; `bool sync_report_failed(result)` (a failed step fails the sync, except the Energy step; a kept or skipped one doesn't), `int sync_first_failed(result)`; `sync_need_t sync_need(clock_valid, always_wifi_off, have_forecast, weather_on)` (what the device lacks: the clock, `always` mode's Wi-Fi, then the first forecast, only with the Weather step on);
  - `size_t util_url_host(url, out, size)`;
  - `fetch_session_t.bearer`; a failed GET logs its host alone;
  - `sync_kind_t` (`SYNC_KIND_SYNC`, `_REFRESH`, `_CHECK`), `sync_solar_req_t`, `sync_energy_req_t`; in `sync_request_t` `kind`, `steps`, `now`, `refresh_radar`, `refresh_energy`, `solar`, `energy` (`radar_only` goes); in `sync_report_t` `kind`, `solar`, `solcast_asked`, `solcast_sites`, `energy`;
  - `void app_solar_request(sync_solar_req_t *, sync_energy_req_t *)`, `esp_err_t app_sync_check(void)` (`ESP_ERR_INVALID_ARG` when both sources are off: nothing to check);
  - `app_sync_state_t.last_detail[SYNC_STEP_COUNT][SYNC_DETAIL_LEN]`: every step's detail of the last sync (why it failed, kept or was skipped), for the Sync page.

The steps (spec §9.3): a data step that is off makes no requests and is "skipped: off"; Solar and Energy without a source are "skipped: no source". Solar asks once a plane at Open-Meteo (our model, the inverter's cap), once at Forecast.Solar, once a site at Solcast unless its budget says keep ("kept"); a 429 keeps the forecast too ("kept: HTTP 429"), and neither fails the sync. Energy's failure shows but fails nothing. In `always`, the house's reading comes every 5 min, in the radar's refresh when one runs then (RainViewer's comes every 10 min) and alone otherwise, outside the history and the retries. The Solar page's check (Task 11) runs both steps, joining a network first if need be, whatever the switches say, outside the history and the retries. Solcast's 18 KB reply needs a 32 KB body; the sync's buffers move to PSRAM. A check that can't join says why in both steps, so the Solar page shows it. A detail is cut between characters (Task 1's `util_json_text()`).

The review's findings here: with the Weather step off and no forecast stored, a successful sync brought none and the next was due at once, without end, so the forecast's need now follows the Weather step (`sync_need()`, host-tested); and a refresh for the house alone, or a check, stopped a playing radar loop, so the radar is applied only when its step ran or frames came (`apply_radar()`).

- [ ] **Step 1: Write the failing tests.** Which steps fail a sync; what the device lacks, the Weather step's switch included; a URL's host:

`test/host/test_sync_plan.c`:

```diff
--- a/test/host/test_sync_plan.c
+++ b/test/host/test_sync_plan.c
@@ -405,6 +405,23 @@ static void test_no_forecast_yet_syncs_at_once_unless_a_sync_failed(void)
     TEST_ASSERT_EQUAL_INT64(oct2(5, 30), due.at);
 }
 
+/* The clock first, then `always` mode's Wi-Fi, then the first forecast, which only a sync with its Weather step on
+ * brings (D35): with that step off the schedule decides, or each successful sync would start the next at once. */
+static void test_only_the_weather_step_brings_the_first_forecast(void)
+{
+    TEST_ASSERT_EQUAL_INT(SYNC_NEED_TIME, sync_need(false, true, false, true));
+    TEST_ASSERT_EQUAL_INT(SYNC_NEED_TIME, sync_need(false, false, false, false)); /* the time step has no switch */
+    TEST_ASSERT_EQUAL_INT(SYNC_NEED_WIFI, sync_need(true, true, false, false));
+    TEST_ASSERT_EQUAL_INT(SYNC_NEED_FORECAST, sync_need(true, false, false, true));
+    TEST_ASSERT_EQUAL_INT(SYNC_NEED_NOTHING, sync_need(true, false, true, true));
+    TEST_ASSERT_EQUAL_INT(SYNC_NEED_NOTHING, sync_need(true, false, false, false));
+    sync_schedule_t s = at_times(1, k_half_past_five);
+    sync_history_t h = { 0 };
+    sync_history_record(&h, (sync_due_t){ .at = oct1(18, 40) }, true, oct1(18, 41)); /* it brought no forecast */
+    sync_due_t due = sync_next_due_needing(&s, &h, oct1(18, 41), false, sync_need(true, false, false, false));
+    TEST_ASSERT_EQUAL_INT64(oct2(5, 30), due.at);
+}
+
 static void test_always_brings_wifi_back_at_once_unless_a_sync_failed(void)
 {
     sync_schedule_t s = { .mode = SYNC_MODE_ALWAYS };
@@ -439,6 +456,29 @@ static void test_interval_slots_on_the_fall_back_day(void)
     TEST_ASSERT_EQUAL_UINT32(2 * 3600, sync_expected_interval_s(&s, utc(2026, 10, 25, 10, 0, 0)));
 }
 
+/* A sync fails when one of its steps fails, but not for the house's energy (D36): it shows, starts no retry. A step
+ * that kept what it had (Solcast's budget, a provider's 429) or didn't run (switched off) is no failure (D35). */
+static void test_a_sync_fails_on_any_step_but_the_energy(void)
+{
+    uint8_t r[SYNC_STEP_COUNT];
+    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
+        r[i] = SYNC_STEP_OK;
+    }
+    TEST_ASSERT_FALSE(sync_report_failed(r));
+    TEST_ASSERT_EQUAL_INT(SYNC_STEP_COUNT, sync_first_failed(r));
+    r[SYNC_STEP_ENERGY] = SYNC_STEP_FAILED;
+    TEST_ASSERT_FALSE(sync_report_failed(r));
+    TEST_ASSERT_EQUAL_INT(SYNC_STEP_ENERGY, sync_first_failed(r)); /* Info still names it */
+    r[SYNC_STEP_SOLAR] = SYNC_STEP_KEPT;
+    r[SYNC_STEP_RADAR] = SYNC_STEP_NOT_RUN;
+    TEST_ASSERT_FALSE(sync_report_failed(r));
+    r[SYNC_STEP_SOLAR] = SYNC_STEP_FAILED;
+    TEST_ASSERT_TRUE(sync_report_failed(r));
+    TEST_ASSERT_EQUAL_INT(SYNC_STEP_SOLAR, sync_first_failed(r));
+    r[SYNC_STEP_WIFI] = SYNC_STEP_FAILED;
+    TEST_ASSERT_EQUAL_INT(SYNC_STEP_WIFI, sync_first_failed(r));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -462,6 +502,7 @@ int main(void)
     RUN_TEST(test_an_overdue_retry_is_due_now);
     RUN_TEST(test_a_lost_clock_syncs_at_once_then_waits_between_tries);
     RUN_TEST(test_no_forecast_yet_syncs_at_once_unless_a_sync_failed);
+    RUN_TEST(test_only_the_weather_step_brings_the_first_forecast);
     RUN_TEST(test_always_brings_wifi_back_at_once_unless_a_sync_failed);
     RUN_TEST(test_a_step_gets_its_limit_or_what_is_left_of_the_sync);
     RUN_TEST(test_a_failure_after_now_counts_from_now);
@@ -473,6 +514,7 @@ int main(void)
     RUN_TEST(test_the_expected_interval_of_always_and_manual);
     RUN_TEST(test_wifi_is_wanted_in_always_mode_outside_quiet_hours);
     RUN_TEST(test_radar_refreshes_follow_the_frames);
+    RUN_TEST(test_a_sync_fails_on_any_step_but_the_energy);
     RUN_TEST(test_hhmm_text);
     RUN_TEST(test_a_time_in_the_spring_forward_gap_runs_after_it);
     RUN_TEST(test_a_time_in_the_repeated_hour_runs_once);
```


`test/host/test_util_url.c`:

```c
#include <string.h>

#include "unity.h"
#include "util_url.h"

/* A URL's host, which is all `fetch` logs (spec §10.4): a key in the path (Forecast.Solar's) or the query (SolaX's
 * token) never reaches the log. */

void setUp(void) {}
void tearDown(void) {}

static const char *host(const char *url)
{
    static char out[64];
    util_url_host(url, out, sizeof(out));
    return out;
}

static void test_the_host_alone(void)
{
    TEST_ASSERT_EQUAL_STRING("api.forecast.solar",
                             host("https://api.forecast.solar/AbC123/estimate/watts/49/16"));
    TEST_ASSERT_EQUAL_STRING("www.solaxcloud.com",
                             host("https://www.solaxcloud.com/proxyApp/proxy/api/getRealtimeInfo.do"
                                  "?tokenId=2020&sn=X"));
    TEST_ASSERT_EQUAL_STRING("example.com:8080", host("http://example.com:8080/x"));
    TEST_ASSERT_EQUAL_STRING("example.com", host("https://example.com?q=1"));
    TEST_ASSERT_EQUAL_STRING("example.com", host("https://example.com#frag"));
    TEST_ASSERT_EQUAL_STRING("example.com", host("example.com/path"));
}

static void test_no_user_or_password(void)
{
    TEST_ASSERT_EQUAL_STRING("host", host("https://user:secret@host/x"));
    TEST_ASSERT_EQUAL_STRING("host", host("https://a@b:c@host"));
}

static void test_short_buffers_and_nothing(void)
{
    char out[5];
    TEST_ASSERT_EQUAL_UINT(4, util_url_host("https://example.com/x", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("exam", out);
    TEST_ASSERT_EQUAL_UINT(0, util_url_host(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_STRING("", host(""));
    TEST_ASSERT_EQUAL_STRING("", host("https:///path"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_host_alone);
    RUN_TEST(test_no_user_or_password);
    RUN_TEST(test_short_buffers_and_nothing);
    return UNITY_END();
}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -223,6 +223,7 @@ reflbo_host_test(test_util_time util)
 reflbo_host_test(test_util_calendar util)
 reflbo_host_test(test_util_json util)
 reflbo_host_test(test_util_sha256 util)
+reflbo_host_test(test_util_url util)
 reflbo_host_test(test_scheduler scheduler)
 reflbo_host_test(test_astro astro)
 reflbo_host_test(test_sync_plan sync_logic)
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake -S test/host -B build-host -G Ninja >/dev/null; for t in test_util_url test_sync_plan; do cmake --build build-host --target $t 2>&1 | grep -E 'error:' | sed -E 's/.*error: //' | sort | uniq -c | sort -rn | head -6; done`
Expected:

```
   1 'util_url.h' file not found
   4 call to undeclared function 'sync_report_failed'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
   3 use of undeclared identifier 'SYNC_STEP_COUNT'
   2 use of undeclared identifier 'SYNC_STEP_SOLAR'
   2 use of undeclared identifier 'SYNC_STEP_FAILED'
   2 use of undeclared identifier 'SYNC_STEP_ENERGY'
   1 use of undeclared identifier 'SYNC_STEP_RADAR'
```

- [ ] **Step 3: A URL's host, and the rule.**

`components/util/include/util_url.h`:

```c
#pragma once

#include <stddef.h>

/* A URL's host (and port), without its scheme, user, path or query: what may be logged of it, as keys and
 * tokens go into paths and queries (spec §10.4). Returns its length, cut to fit `size`. Pure C. */
size_t util_url_host(const char *url, char *out, size_t size);
```


`components/util/util_url.c`:

```c
#include "util_url.h"

#include <string.h>

size_t util_url_host(const char *url, char *out, size_t size)
{
    if (size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (url == NULL) {
        return 0;
    }
    const char *start = strstr(url, "://");
    start = start != NULL ? start + 3 : url;
    size_t len = strcspn(start, "/?#");
    for (const char *at = memchr(start, '@', len); at != NULL; at = memchr(start, '@', len)) {
        len -= (size_t)(at + 1 - start); /* a user and password are not the host */
        start = at + 1;
    }
    len = len < size - 1 ? len : size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return len;
}
```


`components/util/CMakeLists.txt`:

```diff
--- a/components/util/CMakeLists.txt
+++ b/components/util/CMakeLists.txt
@@ -1,4 +1,4 @@
 # Small pure-C helpers shared by the firmware and the host tests (no ESP-IDF headers).
 idf_component_register(SRCS "util_crc32.c" "util_base64.c" "util_snapshot.c" "util_ticks.c" "util_time.c"
-                            "util_calendar.c" "util_json.c" "util_sha256.c"
+                            "util_calendar.c" "util_json.c" "util_sha256.c" "util_url.c"
                        INCLUDE_DIRS "include")
```


`components/sync/include/sync_plan.h`:

```diff
--- a/components/sync/include/sync_plan.h
+++ b/components/sync/include/sync_plan.h
@@ -13,6 +13,31 @@
 
 typedef enum { SYNC_MODE_TIMES, SYNC_MODE_INTERVAL, SYNC_MODE_ALWAYS, SYNC_MODE_MANUAL } sync_mode_t;
 
+/* A sync's steps (spec §9.3), in their order. */
+typedef enum {
+    SYNC_STEP_WIFI,
+    SYNC_STEP_TIME,
+    SYNC_STEP_WEATHER,
+    SYNC_STEP_AIR,
+    SYNC_STEP_RADAR,  /* M6 (spec §11.2) */
+    SYNC_STEP_SOLAR,  /* M6d (spec §11.5) */
+    SYNC_STEP_ENERGY, /* M6d (spec §11.6) */
+    SYNC_STEP_COUNT,
+} sync_step_t;
+
+typedef enum {
+    SYNC_STEP_NOT_RUN, /* skipped: switched off, without a source, or the sync never got there */
+    SYNC_STEP_OK,
+    SYNC_STEP_FAILED,
+    SYNC_STEP_KEPT, /* it ran and kept what it had: Solcast's budget ("kept"), a provider's 429 (M6d) */
+} sync_step_result_t;
+
+/* A sync fails when a step fails, except the Energy step (D36; from M7 the MQTT step's, D32): their failures show
+ * but start no retry. A step kept or not run is no failure. */
+bool sync_report_failed(const uint8_t result[SYNC_STEP_COUNT]);
+/* The first step that failed, whichever it is, for Info ▸ Last sync; SYNC_STEP_COUNT for none. */
+int sync_first_failed(const uint8_t result[SYNC_STEP_COUNT]);
+
 #define SYNC_TIMES_MAX 8
 #define SYNC_ALWAYS_REFRESH_MIN 60 /* weather and air quality in `always` mode */
 #define SYNC_RETRY_COUNT 3         /* retries 15, 30 and 60 min after each failure */
@@ -52,6 +77,10 @@ typedef enum {
     SYNC_NEED_WIFI,     /* sync mode `always` wants Wi-Fi, and it is off: a night or quiet hours ended */
     SYNC_NEED_TIME,     /* the clock is lost (D9) */
 } sync_need_t;
+/* What a device lacks that a sync brings, the most urgent first: the clock; the Wi-Fi that sync mode `always` wants
+ * while it is off (`always_wifi_off`); the first forecast, which only a sync with its Weather step on brings (D35),
+ * so with that step off the schedule decides. */
+sync_need_t sync_need(bool clock_valid, bool always_wifi_off, bool have_forecast, bool weather_on);
 
 /* sync_next_due() for a device that lacks what a sync brings. SYNC_NEED_TIME: at `now`, or after a
  * failure at the next retry, 15, 30 then 60 min on and every 60 min after the third, whatever the
```


`components/sync/sync_plan.c`:

```diff
--- a/components/sync/sync_plan.c
+++ b/components/sync/sync_plan.c
@@ -125,6 +125,14 @@ sync_due_t sync_next_due(const sync_schedule_t *s, const sync_history_t *h, time
     return due;
 }
 
+sync_need_t sync_need(bool clock_valid, bool always_wifi_off, bool have_forecast, bool weather_on)
+{
+    return !clock_valid                   ? SYNC_NEED_TIME
+           : always_wifi_off              ? SYNC_NEED_WIFI
+           : !have_forecast && weather_on ? SYNC_NEED_FORECAST
+                                          : SYNC_NEED_NOTHING;
+}
+
 sync_due_t sync_next_due_needing(const sync_schedule_t *s, const sync_history_t *h, time_t now, bool low_battery,
                                  sync_need_t need)
 {
@@ -210,3 +218,23 @@ time_t sync_radar_next(time_t after, uint32_t step_s)
     time_t t = after - SYNC_RADAR_DELAY_S;
     return t - t % (time_t)step_s + (time_t)step_s + SYNC_RADAR_DELAY_S;
 }
+
+bool sync_report_failed(const uint8_t result[SYNC_STEP_COUNT])
+{
+    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
+        if (result[i] == SYNC_STEP_FAILED && i != SYNC_STEP_ENERGY) {
+            return true;
+        }
+    }
+    return false;
+}
+
+int sync_first_failed(const uint8_t result[SYNC_STEP_COUNT])
+{
+    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
+        if (result[i] == SYNC_STEP_FAILED) {
+            return i;
+        }
+    }
+    return SYNC_STEP_COUNT;
+}
```


Run: `cmake --build build-host && ./build-host/test_util_url | tail -2 && ./build-host/test_sync_plan | tail -2`
Expected:

```
3 Tests 0 Failures 0 Ignored
OK
37 Tests 0 Failures 0 Ignored
OK
```

- [ ] **Step 4: fetch.** Solcast's bearer header; a failed GET logs its host, never a path or a query:

`components/fetch/include/fetch.h`:

```diff
--- a/components/fetch/include/fetch.h
+++ b/components/fetch/include/fetch.h
@@ -7,11 +7,13 @@
 /*
  * HTTPS GETs with the certificate bundle and a "reflbo/<version>" User-Agent (spec §11): the sync's
  * requests, the place search, the radar's tiles and the flight radar's polls. A session keeps its
- * connection for the next GET to the same host (spec §11.2, §11.3). Device only.
+ * connection for the next GET to the same host (spec §11.2, §11.3). Only a URL's host is logged, as
+ * keys go into paths and queries (spec §10.4). Device only.
  */
 
 typedef struct {
-    void *client; /* esp_http_client_handle_t; NULL until the first GET */
+    void *client;       /* esp_http_client_handle_t; NULL until the first GET */
+    const char *bearer; /* sent as "Authorization: Bearer <bearer>" (Solcast's key, M6d); NULL for none */
 } fetch_session_t;
 
 /* `url` into `buf`: the body, NUL-terminated after `*len` bytes; a body of `size - 1` bytes or more
```


`components/fetch/fetch.c`:

```diff
--- a/components/fetch/fetch.c
+++ b/components/fetch/fetch.c
@@ -8,6 +8,7 @@
 #include "esp_crt_bundle.h"
 #include "esp_http_client.h"
 #include "esp_log.h"
+#include "util_url.h"
 
 static const char *TAG = "fetch";
 
@@ -44,6 +45,13 @@ static esp_err_t get_once(fetch_session_t *s, const char *url, char *buf, size_t
         err = esp_http_client_set_url(client, url); /* another host closes the old connection */
         esp_http_client_set_timeout_ms(client, timeout_ms);
     }
+    if (err == ESP_OK && s->bearer != NULL) {
+        char auth[96];
+        snprintf(auth, sizeof(auth), "Bearer %s", s->bearer);
+        err = esp_http_client_set_header(client, "Authorization", auth);
+    } else if (err == ESP_OK) {
+        esp_http_client_delete_header(client, "Authorization");
+    }
     if (err == ESP_OK) {
         err = esp_http_client_open(client, 0);
     }
@@ -94,7 +102,9 @@ esp_err_t fetch_get(fetch_session_t *s, const char *url, void *buf, size_t size,
         err = get_once(s, url, buf, size, len, timeout_ms, status);
     }
     if (err != ESP_OK) {
-        ESP_LOGW(TAG, "GET %.48s...: %s, HTTP %d, %u bytes", url, esp_err_to_name(err), *status, (unsigned)*len);
+        char host[64];
+        util_url_host(url, host, sizeof(host)); /* never a key or a token */
+        ESP_LOGW(TAG, "GET %s: %s, HTTP %d, %u bytes", host, esp_err_to_name(err), *status, (unsigned)*len);
     }
     return err;
 }
```


`components/fetch/CMakeLists.txt`:

```diff
--- a/components/fetch/CMakeLists.txt
+++ b/components/fetch/CMakeLists.txt
@@ -2,4 +2,4 @@
 idf_component_register(SRCS "fetch.c"
                        INCLUDE_DIRS "include"
                        REQUIRES esp_common
-                       PRIV_REQUIRES esp_http_client mbedtls esp_app_format log)
+                       PRIV_REQUIRES esp_http_client mbedtls esp_app_format log util)
```


- [ ] **Step 5: The sync.** The request and the report, the Solar and Energy steps, the switches, the three kinds:

`components/sync/include/sync.h`:

```diff
--- a/components/sync/include/sync.h
+++ b/components/sync/include/sync.h
@@ -4,52 +4,77 @@
 #include <stdint.h>
 
 #include "datastore.h"
+#include "energy.h"
 #include "esp_err.h"
 #include "radar_fetch.h"
 #include "settings.h"
+#include "solar.h"
+#include "sync_plan.h"
 
 /*
- * The sync (spec §9.3): Wi-Fi, the time, the weather, the air quality and the weather radar, on a
- * task of its own; in sync mode `always` also a radar-only refresh. It only fetches: the app task
- * applies the report, as it owns the clock, the RTC, the datastore and the radar's frames (spec
- * §3.2). Wi-Fi stays on afterwards; the app turns it off unless config mode or sync mode `always`
- * keeps it.
+ * The sync (spec §9.3): Wi-Fi, the time, the weather, the air quality, the weather radar, the PV
+ * forecast and the house's energy, on a task of its own; in sync mode `always` also a refresh of the
+ * radar and the house's reading, and from the Solar page a check of the last two. It only fetches:
+ * the app task applies the report, as it owns the clock, the RTC, the datastore, the radar's frames
+ * and the solar state (spec §3.2). Wi-Fi stays on afterwards; the app turns it off unless config mode
+ * or sync mode `always` keeps it.
  */
 
-typedef enum {
-    SYNC_STEP_WIFI,
-    SYNC_STEP_TIME,
-    SYNC_STEP_WEATHER,
-    SYNC_STEP_AIR,
-    SYNC_STEP_RADAR, /* M6 (spec §11.2) */
-    SYNC_STEP_COUNT,
-} sync_step_t;
+#define SYNC_DETAIL_LEN 24
 
 typedef enum {
-    SYNC_STEP_NOT_RUN, /* skipped: an earlier step failed, or the sync never got there */
-    SYNC_STEP_OK,
-    SYNC_STEP_FAILED,
-} sync_step_result_t;
+    SYNC_KIND_SYNC,    /* every step, on a saved network it joins first */
+    SYNC_KIND_REFRESH, /* sync mode `always`: the radar's frames, the house's reading or both, on the network
+                          Wi-Fi is on already (D23: never joining) */
+    SYNC_KIND_CHECK,   /* the Solar page's Check now (M6d): the Solar and Energy steps, joining first if need be */
+} sync_kind_t;
 
-#define SYNC_DETAIL_LEN 24
+/* The Solar step's request (spec §11.5); keys stay in it, never in a log. */
+typedef struct {
+    uint8_t source; /* solar_source_t; SOLAR_OFF: the step is skipped */
+    uint8_t plane_count;
+    solar_plane_t planes[SOLAR_PLANES_MAX];
+    uint8_t losses_pct;
+    float inverter_w;              /* our model's limit; 0 for none */
+    char key[SOLAR_KEY_MAX];       /* Forecast.Solar's, optional; Solcast's */
+    char sites[2][SOLAR_KEY_MAX];  /* Solcast's site ids; "" for none */
+    uint32_t solcast_asked;        /* when Solcast was last asked (UTC): its budget */
+} sync_solar_req_t;
+
+/* The Energy step's request (spec §11.6). */
+typedef struct {
+    bool on; /* energy.source is SolaX Cloud */
+    char token[ENERGY_KEY_MAX];
+    char sn[ENERGY_KEY_MAX];
+} sync_energy_req_t;
 
 typedef struct {
+    uint8_t kind;  /* sync_kind_t */
+    uint8_t steps; /* settings_step_t bits: the data steps that run (D35); the time always does */
+    uint32_t now;  /* the clock at the start (UTC) while it is valid, else 0 */
     int32_t lat_e4, lon_e4;
     char ntp[SETTINGS_NTP_MAX][SETTINGS_HOST_LEN];
     radar_fetch_req_t radar; /* its deadline is the sync's to set */
-    bool radar_only;         /* sync mode `always`'s radar refresh: Wi-Fi up already, the radar alone */
+    bool refresh_radar;      /* SYNC_KIND_REFRESH: the radar's frames */
+    bool refresh_energy;     /* SYNC_KIND_REFRESH: the house's reading (D36) */
+    sync_solar_req_t solar;
+    sync_energy_req_t energy;
 } sync_request_t;
 
 typedef struct {
+    uint8_t kind;                                   /* the request's */
     uint8_t result[SYNC_STEP_COUNT];                /* sync_step_result_t */
-    char detail[SYNC_STEP_COUNT][SYNC_DETAIL_LEN];  /* why a step failed: "not found", "HTTP 503" */
+    char detail[SYNC_STEP_COUNT][SYNC_DETAIL_LEN];  /* why a step failed or kept: "not found", "HTTP 503", "kept" */
     int64_t ntp_utc_us;  /* SYNC_STEP_TIME: the true time (UTC µs) at the monotonic instant below */
     int64_t ntp_mono_us; /* esp_timer_get_time() */
     int64_t ntp_delay_us;
     ds_weather_t weather; /* SYNC_STEP_WEATHER; `fetched` is the app's to set */
     ds_air_t air;         /* SYNC_STEP_AIR */
     radar_fetch_result_t radar; /* SYNC_STEP_RADAR: its frames are the app's to take or free */
-    bool radar_only;
+    const solar_acc_t *solar;   /* SYNC_STEP_SOLAR ok: the reply, for the app to finish into its forecast */
+    uint32_t solcast_asked;     /* when the step asked Solcast (UTC); 0: it didn't */
+    uint8_t solcast_sites;      /* Solcast's sites, for the forecast's freshness */
+    energy_reading_t energy;    /* SYNC_STEP_ENERGY ok: the reading */
 } sync_report_t;
 
 /* Starts a sync; `done` runs on the sync task when it ends, and must hand the report to the app task
@@ -59,4 +84,4 @@ esp_err_t sync_start(const sync_request_t *req, void (*done)(sync_report_t *repo
 bool sync_running(void);
 /* The step running now, for the progress the web UI shows; SYNC_STEP_COUNT when none runs. */
 sync_step_t sync_step(void);
-const char *sync_step_name(sync_step_t step); /* "wifi", "time", "weather", "air", "radar" */
+const char *sync_step_name(sync_step_t step); /* "wifi", "time", "weather", "air", "radar", "solar", "energy" */
```


`components/sync/sync.c`:

```diff
--- a/components/sync/sync.c
+++ b/components/sync/sync.c
@@ -8,6 +8,7 @@
 #include "esp_check.h"
 #include "esp_log.h"
 #include "esp_timer.h"
+#include "fetch.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "lwip/netdb.h"
@@ -15,8 +16,8 @@
 #include "netmgr.h"
 #include "sync_ntp.h"
 #include "sync_plan.h"
+#include "util_json.h"
 #include "weather.h"
-#include "weather_http.h"
 
 static const char *TAG = "sync";
 
@@ -25,25 +26,50 @@ static const char *TAG = "sync";
 #define NTP_TIMEOUT_MS 5000 /* spec §9.3 */
 #define HTTP_TIMEOUT_MS 10000
 #define JOIN_MAX_MS 25000 /* three 8 s attempts; the rest of the 45 s is the steps' (spec §9.3) */
-#define BODY_MAX (12 * 1024) /* the largest reply, the forecast, is about 5 KB */
+#define BODY_MAX (32 * 1024) /* the largest reply, Solcast's 72 h, is about 18 KB (M6d) */
 #define RADAR_STEP_MS 10000 /* spec §9.3 */
 #define REFRESH_MAX_MS 30000 /* a radar-only refresh, the last hour's 12 frames at most (D28) */
 
 static volatile bool s_running;
 static volatile uint8_t s_step = SYNC_STEP_COUNT;
-static sync_request_t s_req;
+EXT_RAM_BSS_ATTR static sync_request_t s_req; /* in PSRAM, as the keys and the reports grew (M6d) */
 static void (*s_done)(sync_report_t *report);
-static sync_report_t s_report;
+EXT_RAM_BSS_ATTR static sync_report_t s_report;
+EXT_RAM_BSS_ATTR static solar_acc_t s_solar; /* the Solar step's reply, the app's until the next sync */
 static int64_t s_deadline_us; /* esp_timer: SYNC_RADIO_MAX_MS after the start */
 EXT_RAM_BSS_ATTR static char s_body[BODY_MAX];
 
 static void failed(sync_step_t step, const char *detail)
 {
     s_report.result[step] = SYNC_STEP_FAILED;
-    snprintf(s_report.detail[step], SYNC_DETAIL_LEN, "%s", detail);
+    util_json_text(s_report.detail[step], SYNC_DETAIL_LEN, detail); /* between characters */
     ESP_LOGW(TAG, "%s: %s", sync_step_name(step), detail);
 }
 
+/* It ran, and kept what it had (spec §11.5): Solcast's budget, a provider's 429. */
+static void kept(sync_step_t step, const char *detail)
+{
+    s_report.result[step] = SYNC_STEP_KEPT;
+    util_json_text(s_report.detail[step], SYNC_DETAIL_LEN, detail); /* between characters */
+    ESP_LOGI(TAG, "%s: kept (%s)", sync_step_name(step), detail);
+}
+
+/* Not run: switched off ("off", D35) or without a source ("no source"). */
+static void skipped(sync_step_t step, const char *detail)
+{
+    s_report.result[step] = SYNC_STEP_NOT_RUN;
+    util_json_text(s_report.detail[step], SYNC_DETAIL_LEN, detail); /* between characters */
+}
+
+static bool wanted(sync_step_t step, settings_step_t bit)
+{
+    if (s_req.steps & bit) {
+        return true;
+    }
+    skipped(step, "off"); /* no requests, and its data age out as usual (spec §9.3) */
+    return false;
+}
+
 static int64_t clock_us(void)
 {
     struct timeval tv;
@@ -103,24 +129,39 @@ static void step_time(void)
     failed(SYNC_STEP_TIME, "no answer");
 }
 
-static bool fetch(sync_step_t step, const char *url)
+/* One GET into s_body within its share of the radio's 45 s, with `bearer` for Solcast; false with the reason in
+ * `detail` ("HTTP 401", "timeout") and the status in `*status` (0 when no reply came). */
+static bool get(const char *url, const char *bearer, int *status, char detail[SYNC_DETAIL_LEN])
 {
     size_t len;
-    int status;
+    *status = 0;
     int budget = sync_budget_ms(esp_timer_get_time(), s_deadline_us, HTTP_TIMEOUT_MS);
     if (budget == 0) {
-        failed(step, "timeout"); /* the radio's 45 s are up (spec §9.3) */
+        snprintf(detail, SYNC_DETAIL_LEN, "timeout"); /* the radio's 45 s are up (spec §9.3) */
         return false;
     }
-    esp_err_t err = weather_http_get(url, s_body, sizeof(s_body), &len, budget, &status);
+    fetch_session_t session = { .bearer = bearer };
+    esp_err_t err = fetch_get(&session, url, s_body, sizeof(s_body), &len, budget, status);
+    fetch_close(&session);
     if (err == ESP_OK) {
         return true;
     }
-    char detail[SYNC_DETAIL_LEN];
-    if (status != 0 && status != 200) {
-        snprintf(detail, sizeof(detail), "HTTP %d", status);
+    if (*status != 0 && *status != 200) {
+        snprintf(detail, SYNC_DETAIL_LEN, "HTTP %d", *status);
     } else {
-        snprintf(detail, sizeof(detail), "%s", err == ESP_ERR_TIMEOUT ? "timeout" : esp_err_to_name(err));
+        snprintf(detail, SYNC_DETAIL_LEN, "%s", err == ESP_ERR_TIMEOUT        ? "timeout"
+                                                : err == ESP_ERR_INVALID_SIZE ? "too big"
+                                                                              : esp_err_to_name(err));
+    }
+    return false;
+}
+
+static bool fetch(sync_step_t step, const char *url)
+{
+    int status;
+    char detail[SYNC_DETAIL_LEN];
+    if (get(url, NULL, &status, detail)) {
+        return true;
     }
     failed(step, detail);
     return false;
@@ -128,6 +169,9 @@ static bool fetch(sync_step_t step, const char *url)
 
 static void step_weather(void)
 {
+    if (!wanted(SYNC_STEP_WEATHER, SETTINGS_STEP_WEATHER)) {
+        return;
+    }
     char url[WEATHER_URL_MAX], err[SYNC_DETAIL_LEN];
     weather_forecast_url(url, sizeof(url), s_req.lat_e4, s_req.lon_e4);
     if (!fetch(SYNC_STEP_WEATHER, url)) {
@@ -142,6 +186,9 @@ static void step_weather(void)
 
 static void step_air(void)
 {
+    if (!wanted(SYNC_STEP_AIR, SETTINGS_STEP_AIR)) {
+        return;
+    }
     char url[WEATHER_URL_MAX], err[SYNC_DETAIL_LEN];
     weather_air_url(url, sizeof(url), s_req.lat_e4, s_req.lon_e4);
     if (!fetch(SYNC_STEP_AIR, url)) {
@@ -154,8 +201,21 @@ static void step_air(void)
     }
 }
 
+/* The sync's own time: its NTP answer where the time step worked, as the app sets the clock only after the
+ * sync; else the clock if it is valid; else 0. */
+static uint32_t sync_now(void)
+{
+    if (s_report.result[SYNC_STEP_TIME] == SYNC_STEP_OK) {
+        return (uint32_t)((s_report.ntp_utc_us + esp_timer_get_time() - s_report.ntp_mono_us) / 1000000);
+    }
+    return s_req.now;
+}
+
 static void step_radar(int max_ms)
 {
+    if (!wanted(SYNC_STEP_RADAR, SETTINGS_STEP_RADAR)) {
+        return;
+    }
     int budget = sync_budget_ms(esp_timer_get_time(), s_deadline_us, max_ms);
     if (budget == 0) {
         failed(SYNC_STEP_RADAR, "timeout");
@@ -164,7 +224,7 @@ static void step_radar(int max_ms)
     radar_fetch_req_t req = s_req.radar;
     req.deadline_us = esp_timer_get_time() + (int64_t)budget * 1000;
     if (s_report.result[SYNC_STEP_TIME] == SYNC_STEP_OK) { /* the app sets the clock only after the sync */
-        req.now = (uint32_t)((s_report.ntp_utc_us + esp_timer_get_time() - s_report.ntp_mono_us) / 1000000);
+        req.now = sync_now();
     }
     if (radar_fetch(&req, &s_report.radar) == ESP_OK) {
         s_report.result[SYNC_STEP_RADAR] = SYNC_STEP_OK;
@@ -173,7 +233,136 @@ static void step_radar(int max_ms)
     }
 }
 
-/* Sync mode `always`: the radar alone, on the network Wi-Fi is on already (D23: never joining). */
+/* A reply that failed: a 429 keeps the forecast and doesn't fail the sync (spec §9.3, D35). */
+static void solar_failed(int status, const char *detail)
+{
+    if (status == 429) {
+        kept(SYNC_STEP_SOLAR, detail);
+    } else {
+        failed(SYNC_STEP_SOLAR, detail);
+    }
+}
+
+/* One provider's reply into s_solar, or the step's failure. */
+static bool solar_reply(const char *url, const char *bearer, const solar_plane_t *plane)
+{
+    const sync_solar_req_t *q = &s_req.solar;
+    int status;
+    char detail[SYNC_DETAIL_LEN];
+    if (!get(url, bearer, &status, detail)) {
+        solar_failed(status, detail);
+        return false;
+    }
+    size_t n = strlen(s_body);
+    bool ok = q->source == SOLAR_OPEN_METEO     ? solar_parse_open_meteo(s_body, n, plane, q->losses_pct, &s_solar,
+                                                                          detail, sizeof(detail))
+              : q->source == SOLAR_FORECAST_SOLAR ? solar_parse_forecast_solar(s_body, n, &s_solar, detail,
+                                                                               sizeof(detail))
+                                                  : solar_parse_solcast(s_body, n, &s_solar, detail, sizeof(detail));
+    if (!ok) {
+        failed(SYNC_STEP_SOLAR, detail);
+    }
+    return ok;
+}
+
+/* The PV forecast (spec §11.5): a request a plane at Open-Meteo, one at Forecast.Solar, one a site at Solcast
+ * unless its budget says keep. */
+static void step_solar(void)
+{
+    const sync_solar_req_t *q = &s_req.solar;
+    if (!wanted(SYNC_STEP_SOLAR, SETTINGS_STEP_SOLAR)) {
+        return;
+    }
+    if (q->source == SOLAR_OFF) {
+        skipped(SYNC_STEP_SOLAR, "no source");
+        return;
+    }
+    uint32_t now = sync_now();
+    if (now == 0) {
+        failed(SYNC_STEP_SOLAR, "no time"); /* the local quarter hours need the date */
+        return;
+    }
+    solar_acc_init(&s_solar, (time_t)now);
+    char url[SOLAR_URL_MAX];
+    if (q->source == SOLAR_OPEN_METEO) {
+        for (int i = 0; i < q->plane_count && i < SOLAR_PLANES_MAX; i++) {
+            solar_open_meteo_url(url, sizeof(url), s_req.lat_e4, s_req.lon_e4, &q->planes[i]);
+            if (!solar_reply(url, NULL, &q->planes[i])) {
+                return;
+            }
+        }
+        if (q->inverter_w > 0) {
+            solar_acc_cap(&s_solar, q->inverter_w);
+        }
+    } else if (q->source == SOLAR_FORECAST_SOLAR) {
+        if (solar_forecast_solar_url(url, sizeof(url), s_req.lat_e4, s_req.lon_e4, q->planes, q->plane_count,
+                                     q->key) == 0) {
+            failed(SYNC_STEP_SOLAR, "bad key");
+            return;
+        }
+        if (!solar_reply(url, NULL, NULL)) {
+            return;
+        }
+    } else {
+        int sites = (q->sites[0][0] != '\0') + (q->sites[1][0] != '\0');
+        s_report.solcast_sites = (uint8_t)sites;
+        if (q->key[0] == '\0' || sites == 0) {
+            failed(SYNC_STEP_SOLAR, q->key[0] == '\0' ? "no key" : "no site");
+            return;
+        }
+        if (!solar_solcast_due(q->solcast_asked, sites, now)) {
+            kept(SYNC_STEP_SOLAR, "kept"); /* within its 10 calls a day: the step counts as done */
+            return;
+        }
+        s_report.solcast_asked = now; /* a failed call counts too */
+        for (int i = 0; i < 2; i++) {
+            if (q->sites[i][0] == '\0') {
+                continue;
+            }
+            if (solar_solcast_url(url, sizeof(url), q->sites[i]) == 0) {
+                failed(SYNC_STEP_SOLAR, "bad site");
+                return;
+            }
+            if (!solar_reply(url, q->key, NULL)) {
+                return;
+            }
+        }
+    }
+    s_report.solar = &s_solar;
+    s_report.result[SYNC_STEP_SOLAR] = SYNC_STEP_OK;
+}
+
+/* The house's energy (spec §11.6): one SolaX Cloud reading; its failure shows but doesn't fail the sync. */
+static void step_energy(void)
+{
+    const sync_energy_req_t *q = &s_req.energy;
+    if (!wanted(SYNC_STEP_ENERGY, SETTINGS_STEP_ENERGY)) {
+        return;
+    }
+    if (!q->on) {
+        skipped(SYNC_STEP_ENERGY, "no source");
+        return;
+    }
+    if (q->token[0] == '\0' || q->sn[0] == '\0') {
+        failed(SYNC_STEP_ENERGY, q->token[0] == '\0' ? "no token" : "no registration number");
+        return;
+    }
+    char url[ENERGY_URL_MAX], detail[SYNC_DETAIL_LEN];
+    int status;
+    if (energy_solax_url(url, sizeof(url), q->token, q->sn) == 0) {
+        failed(SYNC_STEP_ENERGY, "bad token");
+        return;
+    }
+    if (!get(url, NULL, &status, detail) ||
+        !energy_parse_solax(s_body, strlen(s_body), &s_report.energy, detail, sizeof(detail))) {
+        failed(SYNC_STEP_ENERGY, detail);
+        return;
+    }
+    s_report.result[SYNC_STEP_ENERGY] = SYNC_STEP_OK;
+}
+
+/* Sync mode `always`: the radar, the house's reading or both, on the network Wi-Fi is on already (D23: never
+ * joining). */
 static void refresh_task(int64_t start)
 {
     s_deadline_us = start + (int64_t)REFRESH_MAX_MS * 1000;
@@ -184,8 +373,14 @@ static void refresh_task(int64_t start)
         return;
     }
     s_report.result[SYNC_STEP_WIFI] = SYNC_STEP_OK;
-    s_step = SYNC_STEP_RADAR;
-    step_radar(REFRESH_MAX_MS);
+    if (s_req.refresh_radar) {
+        s_step = SYNC_STEP_RADAR;
+        step_radar(REFRESH_MAX_MS);
+    }
+    if (s_req.refresh_energy) {
+        s_step = SYNC_STEP_ENERGY;
+        step_energy();
+    }
 }
 
 static void sync_task(void *arg)
@@ -194,9 +389,16 @@ static void sync_task(void *arg)
     int64_t start = esp_timer_get_time();
     s_deadline_us = start + (int64_t)SYNC_RADIO_MAX_MS * 1000;
     s_step = SYNC_STEP_WIFI;
-    esp_err_t err = s_req.radar_only ? ESP_OK : netmgr_join(JOIN_MAX_MS);
-    if (s_req.radar_only) {
+    bool refresh = s_req.kind == SYNC_KIND_REFRESH;
+    esp_err_t err = refresh ? ESP_OK : netmgr_join(JOIN_MAX_MS);
+    if (refresh) {
         refresh_task(start);
+    } else if (err == ESP_OK && s_req.kind == SYNC_KIND_CHECK) { /* the Solar page's Check now */
+        s_report.result[SYNC_STEP_WIFI] = SYNC_STEP_OK;
+        s_step = SYNC_STEP_SOLAR;
+        step_solar();
+        s_step = SYNC_STEP_ENERGY;
+        step_energy();
     } else if (err == ESP_OK) {
         s_report.result[SYNC_STEP_WIFI] = SYNC_STEP_OK;
         s_step = SYNC_STEP_TIME; /* the steps are independent (spec §9.3): one failing skips nothing */
@@ -207,10 +409,22 @@ static void sync_task(void *arg)
         step_air();
         s_step = SYNC_STEP_RADAR;
         step_radar(RADAR_STEP_MS);
+        s_step = SYNC_STEP_SOLAR;
+        step_solar();
+        s_step = SYNC_STEP_ENERGY;
+        step_energy();
     } else {
-        failed(SYNC_STEP_WIFI, err == ESP_ERR_NOT_FOUND       ? "no network saved"
-                               : err == ESP_ERR_INVALID_STATE ? "Wi-Fi busy"
-                                                              : "not joined");
+        const char *why = err == ESP_ERR_NOT_FOUND ? "no network saved" : err == ESP_ERR_INVALID_STATE ? "Wi-Fi busy"
+                                                                                                    : "not joined";
+        failed(SYNC_STEP_WIFI, why);
+        if (s_req.kind == SYNC_KIND_CHECK) { /* the Solar page shows why its check brought nothing */
+            if (s_req.solar.source != SOLAR_OFF) {
+                failed(SYNC_STEP_SOLAR, why);
+            }
+            if (s_req.energy.on) {
+                failed(SYNC_STEP_ENERGY, why);
+            }
+        }
     }
     ESP_LOGI(TAG, "done in %lld ms", (long long)((esp_timer_get_time() - start) / 1000));
     s_step = SYNC_STEP_COUNT;
@@ -228,7 +442,7 @@ esp_err_t sync_start(const sync_request_t *req, void (*done)(sync_report_t *repo
     s_req = *req;
     s_done = done;
     memset(&s_report, 0, sizeof(s_report)); /* the last report's frames were the app's */
-    s_report.radar_only = req->radar_only;
+    s_report.kind = req->kind;
     BaseType_t ok = xTaskCreatePinnedToCore(sync_task, "sync", TASK_STACK, NULL, TASK_PRIORITY, NULL, 0);
     if (ok != pdPASS) {
         s_running = false;
@@ -249,6 +463,7 @@ sync_step_t sync_step(void)
 
 const char *sync_step_name(sync_step_t step)
 {
-    static const char *const k_names[SYNC_STEP_COUNT] = { "wifi", "time", "weather", "air", "radar" };
+    static const char *const k_names[SYNC_STEP_COUNT] = { "wifi", "time", "weather", "air", "radar", "solar",
+                                                          "energy" };
     return (unsigned)step < SYNC_STEP_COUNT ? k_names[step] : "";
 }
```


`components/sync/CMakeLists.txt`:

```diff
--- a/components/sync/CMakeLists.txt
+++ b/components/sync/CMakeLists.txt
@@ -2,5 +2,5 @@
 # pure C and also build on the host; sync.c is the task.
 idf_component_register(SRCS "sync_plan.c" "sync_ntp.c" "sync.c"
                        INCLUDE_DIRS "include"
-                       REQUIRES scheduler datastore storage esp_common radar
-                       PRIV_REQUIRES netmgr weather lwip esp_timer)
+                       REQUIRES scheduler datastore storage esp_common radar solar energy
+                       PRIV_REQUIRES netmgr weather fetch lwip esp_timer util)
```


- [ ] **Step 6: The app.** The requests from the settings and the keys; the reports into the solar state, a kept step with why; Info's Last sync names the first step that failed, the house's energy too, while the status bar's mark and the retries follow `sync_report_failed()`; the forecast's need from `sync_need()`; the radar applied only when it ran; the energy refresh; Check now, refused with nothing to check; "kept" and every step's detail in `sync status` and `/api/status` (`sync.last.details`):

`main/app_internal.h`:

```diff
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -32,7 +32,7 @@ typedef struct {
     uint32_t last_at;           /* when the last sync started (UTC); 0 = none since the cold boot */
     uint8_t last_result[SYNC_STEP_COUNT]; /* sync_step_result_t */
     uint8_t last_failed_step;   /* the first step that failed; SYNC_STEP_COUNT if none */
-    char last_detail[SYNC_DETAIL_LEN];
+    char last_detail[SYNC_STEP_COUNT][SYNC_DETAIL_LEN]; /* why each step failed, kept or was skipped */
 } app_sync_state_t;
 
 /* The PV forecast and the house's energy (main/app_solar.c, spec §11.5, §11.6): kept through deep sleep
@@ -150,13 +150,17 @@ void app_sync_schedule(void);    /* the next automatic sync, after a sync, a set
 time_t app_sync_due(void);       /* for the wake scheduler; 0 = none */
 void app_sync_tick(void);        /* starts a sync that is due; quiet hours in sync mode `always` */
 esp_err_t app_sync_now(void);    /* on demand: ESP_ERR_NOT_FOUND with no network saved */
+/* The Solar page's Check now (M6d): the Solar and Energy steps alone, outside the syncs' history and retries;
+ * ESP_ERR_NOT_FOUND with no network saved, ESP_ERR_INVALID_STATE while a sync runs or with only the device's own
+ * network. */
+esp_err_t app_sync_check(void);
 void app_sync_now_toast(void);   /* the same, with the menu's toast: Sync now, and BOOT on the Radar layout (D30) */
 /* BOOT double on the dashboard (spec §5.6, D31): sync mode `always` on, or back to the mode before,
  * saved and scheduled, with a toast; refused with no network saved ("No Wi-Fi network saved") and on a
  * critical battery. */
 void app_sync_toggle_always(void);
 bool app_sync_active(void);      /* a sync or a radar-only refresh runs */
-bool app_sync_refreshing(void);  /* what runs is a radar-only refresh (spec §9.3): not shown as a sync */
+bool app_sync_refreshing(void);  /* what runs is a refresh or a check (spec §9.3): not shown as a sync */
 bool app_sync_running(void);     /* a sync runs, or waits for a refresh to end: what the screens show */
 bool app_sync_failed(void);      /* the last sync failed a step */
 bool app_sync_holds_wifi(void);  /* sync mode `always` keeps Wi-Fi now */
@@ -181,6 +185,8 @@ void app_solar_budget_reset(void);
  * every 30 min. */
 void app_solar_reading_done(const energy_reading_t *r, const char *error);
 void app_solar_restore(void); /* a cold boot: solar.bin */
+/* The Solar and Energy steps' requests from the settings and the keys in NVS, which must be up. */
+void app_solar_request(sync_solar_req_t *solar, sync_energy_req_t *energy);
 void app_solar_demo(bool on); /* `solar demo on|off` */
 
 /* The keys in NVS `secrets` (main/app_secrets.c, spec §14.2): write-only from the web UI, never logged. */
```


`main/app_solar.c`:

```diff
--- a/main/app_solar.c
+++ b/main/app_solar.c
@@ -177,6 +177,34 @@ const ui_solar_t *app_solar_ui(void)
     return &view;
 }
 
+void app_solar_request(sync_solar_req_t *solar, sync_energy_req_t *energy)
+{
+    const settings_t *set = app_settings();
+    memset(solar, 0, sizeof(*solar));
+    solar->source = set->solar_source;
+    solar->plane_count = set->solar_plane_count;
+    for (int i = 0; i < set->solar_plane_count && i < SOLAR_PLANES_MAX; i++) {
+        const settings_plane_t *p = &set->solar_planes[i];
+        solar->planes[i] = (solar_plane_t){ .kwp = p->kwp_e2 / 100.0f, .tilt = p->tilt, .azimuth = p->azimuth };
+    }
+    solar->losses_pct = set->solar_losses_pct;
+    solar->inverter_w = set->solar_inverter_kw_e2 * 10.0f; /* hundredths of kW */
+    if (set->solar_source == SETTINGS_SOLAR_FORECAST_SOLAR) {
+        app_secret_get(SETTINGS_SECRET_FS_KEY, solar->key, sizeof(solar->key));
+    } else if (set->solar_source == SETTINGS_SOLAR_SOLCAST) {
+        app_secret_get(SETTINGS_SECRET_SOLCAST_KEY, solar->key, sizeof(solar->key));
+        app_secret_get(SETTINGS_SECRET_SOLCAST_SITE1, solar->sites[0], sizeof(solar->sites[0]));
+        app_secret_get(SETTINGS_SECRET_SOLCAST_SITE2, solar->sites[1], sizeof(solar->sites[1]));
+    }
+    solar->solcast_asked = st()->solcast_asked;
+    memset(energy, 0, sizeof(*energy));
+    energy->on = set->energy_source == SETTINGS_ENERGY_SOLAX;
+    if (energy->on) {
+        app_secret_get(SETTINGS_SECRET_SOLAX_TOKEN, energy->token, sizeof(energy->token));
+        app_secret_get(SETTINGS_SECRET_SOLAX_SN, energy->sn, sizeof(energy->sn));
+    }
+}
+
 const app_solar_state_t *app_solar_state(void)
 {
     return st();
```


`main/app_sync.c`:

```diff
--- a/main/app_sync.c
+++ b/main/app_sync.c
@@ -3,6 +3,7 @@
 
 #include "app.h"
 #include "app_internal.h"
+#include "esp_attr.h"
 #include "esp_log.h"
 #include "esp_timer.h"
 #include "freertos/FreeRTOS.h"
@@ -19,14 +20,16 @@
 
 #define LOW_BATTERY_PCT 15 /* spec §8: no retries */
 #define WEB_REPLY_MS 3000  /* a web request just served gets its reply out before Wi-Fi goes */
+#define ENERGY_REFRESH_S 300 /* spec §9.3: in sync mode `always`, a reading every 5 min (D36) */
 
 static const char *TAG = "app_sync";
 
 static bool s_active;           /* a sync runs, or its report waits for apply(): no other may start */
 static bool s_manual;           /* the running sync was asked for: it ends with a toast */
 static sync_due_t s_started_by; /* what started the running sync ({0, false} on demand) */
-static time_t s_radar_next;     /* sync mode `always`: the next radar-only refresh; 0 = at once */
-static bool s_refresh;          /* what runs is such a refresh, not a sync */
+static time_t s_radar_next;     /* sync mode `always`: the next radar refresh; 0 = at once */
+static time_t s_energy_next;    /* and the next reading of the house's energy (M6d) */
+static bool s_refresh;          /* what runs is such a refresh, or a check (M6d), not a sync */
 static bool s_manual_waiting;   /* a sync asked for during such a refresh: it starts when that ends */
 
 static bool always_wanted(time_t now);
@@ -81,10 +84,8 @@ void app_sync_schedule(void)
     } else {
         /* spec §3.3, §7: a lost time at once, then with the retries' pauses; `always` mode's Wi-Fi and
          * the first forecast at once, unless a sync failed since */
-        sync_need_t need = !timekeeping_valid()                           ? SYNC_NEED_TIME
-                           : always_wanted(now) && !s_active && wifi_off() ? SYNC_NEED_WIFI
-                           : ds_weather(app_ds()) == NULL                  ? SYNC_NEED_FORECAST
-                                                                           : SYNC_NEED_NOTHING;
+        sync_need_t need = sync_need(timekeeping_valid(), always_wanted(now) && !s_active && wifi_off(),
+                                     ds_weather(app_ds()) != NULL, app_settings()->sync_steps & SETTINGS_STEP_WEATHER);
         st()->due = sync_next_due_needing(&s, &st()->history, now, low_battery(), need);
         if (st()->due.at > now && st()->due.at % 60 != 0) {
             st()->due.at += 60 - st()->due.at % 60; /* minute wakes use the RTC alarm (spec §9.2) */
@@ -120,15 +121,7 @@ bool app_sync_running(void)
 bool app_sync_failed(void)
 {
     const app_sync_state_t *s = st();
-    if (s->last_at == 0) {
-        return false;
-    }
-    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
-        if (s->last_result[i] != SYNC_STEP_OK) {
-            return true;
-        }
-    }
-    return false;
+    return s->last_at != 0 && sync_report_failed(s->last_result); /* not for the house's energy (D36) */
 }
 
 /* Wi-Fi is off: netmgr isn't up yet (a routine wake), or it says so. */
@@ -187,25 +180,47 @@ static void save_summary(const sync_report_t *r, time_t started)
 {
     app_sync_state_t *s = st();
     s->last_at = (uint32_t)started;
-    s->last_detail[0] = '\0';
-    s->last_failed_step = SYNC_STEP_COUNT;
-    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
-        s->last_result[i] = r->result[i];
-        if (r->result[i] != SYNC_STEP_OK && s->last_failed_step == SYNC_STEP_COUNT) {
-            s->last_failed_step = (uint8_t)i;
-            snprintf(s->last_detail, sizeof(s->last_detail), "%s", r->detail[i]);
-        }
-    }
+    memcpy(s->last_result, r->result, sizeof(s->last_result));
+    s->last_failed_step = (uint8_t)sync_first_failed(r->result); /* the house's energy too: Info shows it */
+    memcpy(s->last_detail, r->detail, sizeof(s->last_detail));
 }
 
 static int64_t s_started_mono; /* esp_timer µs at the start, to date it once the clock is right */
 
 static esp_err_t start(bool manual, sync_due_t due);
 
-/* A radar-only refresh's report (spec §9.3): the frames, outside the syncs' history and retries. */
+/* The Solar and Energy steps that ran, into the solar state (spec §11.5, §11.6). A step that kept the forecast
+ * (Solcast's budget, a 429) shows its detail on the Sync page, not as an error. */
+static void apply_solar(const sync_report_t *r)
+{
+    uint8_t solar = r->result[SYNC_STEP_SOLAR], energy = r->result[SYNC_STEP_ENERGY];
+    if (solar != SYNC_STEP_NOT_RUN) {
+        app_solar_forecast_done(solar == SYNC_STEP_OK ? r->solar : NULL,
+                                solar == SYNC_STEP_FAILED ? r->detail[SYNC_STEP_SOLAR] : NULL,
+                                solar == SYNC_STEP_KEPT ? r->detail[SYNC_STEP_SOLAR] : NULL, r->solcast_asked,
+                                r->solcast_sites);
+    }
+    if (energy != SYNC_STEP_NOT_RUN) {
+        app_solar_reading_done(energy == SYNC_STEP_OK ? &r->energy : NULL,
+                               energy == SYNC_STEP_FAILED ? r->detail[SYNC_STEP_ENERGY] : NULL);
+    }
+}
+
+/* The radar's frames and status, when its step ran or brought frames: a refresh for the house's reading alone, a
+ * check, or a sync with the radar switched off leaves a playing loop be (spec §11.2). */
+static void apply_radar(sync_report_t *r)
+{
+    if (r->result[SYNC_STEP_RADAR] != SYNC_STEP_NOT_RUN || r->radar.count > 0) {
+        app_radar_apply(&r->radar, r->result[SYNC_STEP_RADAR], r->detail[SYNC_STEP_RADAR]);
+    }
+}
+
+/* A refresh's or a check's report (spec §9.3): the frames, the reading, the forecast, outside the syncs' history
+ * and retries. */
 static void apply_refresh(sync_report_t *r)
 {
-    app_radar_apply(&r->radar, r->result[SYNC_STEP_RADAR], r->detail[SYNC_STEP_RADAR]);
+    apply_radar(r);
+    apply_solar(r);
     s_active = false;
     s_refresh = false;
     if (s_manual_waiting) {
@@ -222,7 +237,10 @@ static void apply_refresh(sync_report_t *r)
 static void apply(void *arg)
 {
     sync_report_t *r = arg;
-    if (r->radar_only) {
+    if (r->kind != SYNC_KIND_SYNC) {
+        if (r->kind == SYNC_KIND_CHECK) {
+            release_wifi(); /* as a sync does: config mode or `always` keep it */
+        }
         apply_refresh(r);
         return;
     }
@@ -249,13 +267,14 @@ static void apply(void *arg)
     if (r->result[SYNC_STEP_WEATHER] == SYNC_STEP_OK || r->result[SYNC_STEP_AIR] == SYNC_STEP_OK) {
         app_ui_save_forecast(); /* spec §6: it survives a power-off, shown as stale */
     }
-    app_radar_apply(&r->radar, r->result[SYNC_STEP_RADAR], r->detail[SYNC_STEP_RADAR]);
+    apply_radar(r);
+    apply_solar(r);
     time_t started = now - (time_t)((esp_timer_get_time() - s_started_mono) / 1000000);
     save_summary(r, started);
     bool ok = !app_sync_failed();
     sync_history_record(&st()->history, s_started_by, ok, now);
     ESP_LOGI(TAG, "sync %s%s%s", ok ? "done" : "failed at ", ok ? "" : sync_step_name(st()->last_failed_step),
-             ok ? "" : st()->last_detail);
+             ok ? "" : st()->last_detail[st()->last_failed_step]);
     s_active = false; /* the report is applied: the next sync may overwrite it */
     release_wifi();
     app_sync_schedule();
@@ -285,10 +304,12 @@ static esp_err_t start(bool manual, sync_due_t due)
         return ESP_FAIL;
     }
     const settings_t *set = app_settings();
-    static sync_request_t req; /* the radar's part is large for the app task's stack */
-    req = (sync_request_t){ .lat_e4 = set->lat_e4, .lon_e4 = set->lon_e4 };
+    EXT_RAM_BSS_ATTR static sync_request_t req; /* the radar's and the solar parts are large for the stack */
+    req = (sync_request_t){ .kind = SYNC_KIND_SYNC, .steps = set->sync_steps, .lat_e4 = set->lat_e4,
+                            .lon_e4 = set->lon_e4, .now = timekeeping_valid() ? (uint32_t)time(NULL) : 0 };
     memcpy(req.ntp, set->ntp, sizeof(req.ntp));
     app_radar_request(&req.radar);
+    app_solar_request(&req.solar, &req.energy);
     esp_err_t err = sync_start(&req, done);
     if (err == ESP_OK) {
         s_active = true;
@@ -365,6 +386,42 @@ esp_err_t app_sync_now(void)
     return start(true, (sync_due_t){ 0 });
 }
 
+esp_err_t app_sync_check(void)
+{
+    if (app_state()->critical) {
+        return ESP_ERR_INVALID_STATE;
+    }
+    if (!networks_saved()) {
+        return ESP_ERR_NOT_FOUND;
+    }
+    if (s_active) {
+        return ESP_ERR_INVALID_STATE; /* a sync or a refresh runs: they bring the same */
+    }
+    if (app_config_active()) {
+        netmgr_status_t ns;
+        netmgr_status(&ns);
+        if (ns.state != NETMGR_STATION) {
+            return ESP_ERR_INVALID_STATE; /* only the device's own network: nothing to reach */
+        }
+    }
+    const settings_t *set = app_settings();
+    if (set->solar_source == SETTINGS_SOLAR_OFF && set->energy_source == SETTINGS_ENERGY_OFF) {
+        return ESP_ERR_INVALID_ARG; /* nothing to check */
+    }
+    EXT_RAM_BSS_ATTR static sync_request_t req;
+    req = (sync_request_t){ .kind = SYNC_KIND_CHECK, .steps = SETTINGS_STEPS_ALL, .lat_e4 = set->lat_e4,
+                            .lon_e4 = set->lon_e4, .now = timekeeping_valid() ? (uint32_t)time(NULL) : 0 };
+    app_solar_request(&req.solar, &req.energy); /* asked for: the steps' switches don't hold it back */
+    esp_err_t err = sync_start(&req, done);
+    if (err == ESP_OK) {
+        s_active = true;
+        s_refresh = true; /* not a sync: no mark in the status bar, no history */
+        s_manual = false;
+        ESP_LOGI(TAG, "solar check starts");
+    }
+    return err;
+}
+
 bool app_sync_wifi_pending(void)
 {
     if (!app_net_ready() || app_config_active() || s_active) {
@@ -392,26 +449,43 @@ void app_sync_wifi_check(void)
     app_net_refresh();
 }
 
-/* Sync mode `always` on the network: the radar alone every 5 min (RainViewer: 10), at once when the
- * mode begins, so the loop's hour comes in one go (spec §9.3, D28). */
-static void radar_refresh_tick(time_t now)
+/* Sync mode `always` on the network: the radar every 5 min (RainViewer: 10), at once when the mode begins, so
+ * the loop's hour comes in one go (spec §9.3, D28); the house's reading every 5 min, in the radar's refresh when
+ * one runs then (D36). A step that is off makes no requests (D35). */
+static void refresh_tick(time_t now)
 {
     if (!app_sync_lan_ui()) {
         s_radar_next = 0; /* the next time `always` holds Wi-Fi, the hour comes at once */
+        s_energy_next = 0;
         return;
     }
-    if (s_active || now < s_radar_next) {
+    const settings_t *set = app_settings();
+    bool radar = (set->sync_steps & SETTINGS_STEP_RADAR) && now >= s_radar_next;
+    bool energy = (set->sync_steps & SETTINGS_STEP_ENERGY) && set->energy_source == SETTINGS_ENERGY_SOLAX &&
+                  now >= s_energy_next;
+    if (s_active || (!radar && !energy)) {
         return;
     }
-    static sync_request_t req;
-    req = (sync_request_t){ .radar_only = true };
-    app_radar_request(&req.radar);
+    EXT_RAM_BSS_ATTR static sync_request_t req;
+    req = (sync_request_t){ .kind = SYNC_KIND_REFRESH, .steps = set->sync_steps, .refresh_radar = radar,
+                            .refresh_energy = energy, .now = timekeeping_valid() ? (uint32_t)now : 0 };
+    if (radar) {
+        app_radar_request(&req.radar);
+    }
+    if (energy) {
+        app_solar_request(&req.solar, &req.energy);
+    }
     if (sync_start(&req, done) == ESP_OK) {
         s_active = true;
         s_refresh = true;
         s_manual = false;
-        s_radar_next = sync_radar_next(now, app_radar_step_s());
-        ESP_LOGI(TAG, "radar refresh: %u frames kept", req.radar.have_count);
+        if (radar) {
+            s_radar_next = sync_radar_next(now, app_radar_step_s());
+        }
+        if (energy) {
+            s_energy_next = sync_radar_next(now, ENERGY_REFRESH_S);
+        }
+        ESP_LOGI(TAG, "refresh:%s%s", radar ? " radar" : "", energy ? " energy" : "");
     }
 }
 
@@ -423,7 +497,7 @@ void app_sync_tick(void)
     if (critical || app_ui_night()) {
         return; /* nothing may drain the battery, and the night runs nothing (spec §9.1) */
     }
-    radar_refresh_tick(now);
+    refresh_tick(now);
     sync_due_t due = st()->due;
     if (!s_active && due.at > now && st()->history.failed_at == 0 && always_wanted(now) && wifi_off()) {
         app_sync_schedule(); /* a night ended in sync mode `always`: Wi-Fi back at once, not at the hour */
@@ -464,12 +538,13 @@ void app_sync_summary(char *out, size_t size)
     char when[12];
     const char *suffix;
     lang_format_time(local.tm_hour, local.tm_min, 0, app_settings()->clock_24h, false, when, sizeof(when), &suffix);
-    static const lang_str_t k_steps[SYNC_STEP_COUNT] = { LS_SYNC_STEP_WIFI, LS_SYNC_STEP_TIME, LS_SYNC_STEP_WEATHER,
-                                                         LS_SYNC_STEP_AIR, LS_SYNC_STEP_RADAR };
+    static const lang_str_t k_steps[SYNC_STEP_COUNT] = { LS_SYNC_STEP_WIFI,  LS_SYNC_STEP_TIME,  LS_SYNC_STEP_WEATHER,
+                                                         LS_SYNC_STEP_AIR,   LS_SYNC_STEP_RADAR, LS_SYNC_STEP_SOLAR,
+                                                         LS_SYNC_STEP_ENERGY };
     if (s->last_failed_step >= SYNC_STEP_COUNT) {
         snprintf(out, size, "%s%s%s OK", when, suffix[0] ? " " : "", suffix);
     } else {
         snprintf(out, size, "%s%s%s %s: %s", when, suffix[0] ? " " : "", suffix,
-                 lang_str(lang, k_steps[s->last_failed_step]), s->last_detail);
+                 lang_str(lang, k_steps[s->last_failed_step]), s->last_detail[s->last_failed_step]);
     }
 }
```


`main/app_cmds.c`:

```diff
--- a/main/app_cmds.c
+++ b/main/app_cmds.c
@@ -339,11 +339,11 @@ static int sync_body(int argc, char **argv)
         }
         print_time("last", st->last_at);
         if (st->last_at != 0) {
-            static const char *const k_results[] = { "skipped", "ok", "failed" };
+            static const char *const k_results[] = { "skipped", "ok", "failed", "kept" };
             for (int i = 0; i < SYNC_STEP_COUNT; i++) {
                 printf("  %-8s %s%s%s\n", sync_step_name((sync_step_t)i),
-                       k_results[st->last_result[i] <= SYNC_STEP_FAILED ? st->last_result[i] : 0],
-                       i == st->last_failed_step ? ": " : "", i == st->last_failed_step ? st->last_detail : "");
+                       k_results[st->last_result[i] <= SYNC_STEP_KEPT ? st->last_result[i] : 0],
+                       st->last_detail[i][0] != '\0' ? ": " : "", st->last_detail[i]);
             }
         }
         print_time(st->due.retry ? "next (a retry)" : "next", st->due.at);
```


`main/app_web.c`:

```diff
--- a/main/app_web.c
+++ b/main/app_web.c
@@ -169,14 +169,20 @@ static void get_status(uint8_t *out, size_t size, webui_reply_t *reply)
         cJSON *last = cJSON_AddObjectToObject(sync, "last");
         cJSON_AddNumberToObject(last, "at", st->sync.last_at);
         cJSON *steps = cJSON_AddObjectToObject(last, "steps");
-        static const char *const k_results[] = { "skipped", "ok", "failed" };
+        static const char *const k_results[] = { "skipped", "ok", "failed", "kept" };
         for (int i = 0; i < SYNC_STEP_COUNT; i++) {
             uint8_t r = st->sync.last_result[i];
-            cJSON_AddStringToObject(steps, sync_step_name((sync_step_t)i), k_results[r <= SYNC_STEP_FAILED ? r : 0]);
+            cJSON_AddStringToObject(steps, sync_step_name((sync_step_t)i), k_results[r <= SYNC_STEP_KEPT ? r : 0]);
+        }
+        cJSON *details = cJSON_AddObjectToObject(last, "details"); /* why a step failed, kept or was skipped */
+        for (int i = 0; i < SYNC_STEP_COUNT; i++) {
+            if (st->sync.last_detail[i][0] != '\0') {
+                cJSON_AddStringToObject(details, sync_step_name((sync_step_t)i), st->sync.last_detail[i]);
+            }
         }
         if (st->sync.last_failed_step < SYNC_STEP_COUNT) {
             cJSON_AddStringToObject(last, "failed", sync_step_name((sync_step_t)st->sync.last_failed_step));
-            cJSON_AddStringToObject(last, "detail", st->sync.last_detail);
+            cJSON_AddStringToObject(last, "detail", st->sync.last_detail[st->sync.last_failed_step]);
         }
     }
     if (st->sync.due.at != 0) {
```


- [ ] **Step 7: Run everything.**

Run: `ctest --test-dir build-host | tail -3 && tools/idf.sh build 2>&1 | grep -cE 'warning:'; tools/idf.sh exec bash -c 'xtensa-esp32s3-elf-nm -S build/reflbo.elf | grep -E " (s_req|s_report|s_solar|s_body)$"'`
Expected: `100% tests passed, 0 tests failed out of 65`; no warning; `s_req`, `s_report` and `s_solar` of the sync at PSRAM addresses (`3c…`), with its 32 KB `s_body`. `s_snap` is now `0x1630` bytes (5 680: every step's detail), `.rtc.data` 5800 and `.rtc.force_slow` 32 of the 8 192 bytes (`xtensa-esp32s3-elf-nm -S build/reflbo.elf | grep " s_snap$"`, `xtensa-esp32s3-elf-size -A build/reflbo.elf`).

- [ ] **Step 8: Commit.**

```bash
git add components/util components/fetch components/sync main test/host/CMakeLists.txt \
  test/host/test_sync_plan.c test/host/test_util_url.c
git commit -m "feat(sync): the Solar and Energy steps, the steps' switches and the energy refresh"
```

### Task 11: The Solar page, the steps' switches and write-only keys (`web/`, `main`)

**Files:**
- Modify: `web/app.js`, `web/index.html`, `main/app_web.c`, `main/app_ui.c`, `main/app_internal.h`
- Test: `test/web/test_app.mjs`

**Interfaces:**
- Consumes: Task 4's `settings_take_secrets()`, `settings_check_solar()`; Task 9's `app_secret_set()`, `app_secrets_apply()`, `app_solar_state()`; Task 10's `app_sync_check()`.
- Produces: the API (spec §10.3):
  - `GET /api/status`: `solar` (`source`, `fetched_at`, `day`, `tried_at`, `error` of the last call, `kept`: why the last step kept the forecast, `next_at`: when Solcast may be asked again, `demo`) and `energy` (`source`, `reading_at`, `tried_at`, `error`);
  - `GET /api/settings`: the file's settings with `solar.keys` (`fs_key`, `solcast_key`, `solcast_sites`: how many) and `energy.keys` (`solax_token`, `solax_sn`), never a key;
  - `PATCH /api/settings`: the keys to NVS, the rest merged into the file; 400 for a key a request can't carry and for a second Forecast.Solar plane without a key;
  - `POST /api/solar/check`: 202 once it starts; 409 while a sync runs, with no saved network, on a critical battery, with only the device's own network, or with both sources off;
  - `POST /api/restore` drops any key a bundle carries, and refuses a bundle whose keys can't be taken out (a number where a token would be) or with a second Forecast.Solar plane and no key in NVS: a key never reaches the file through a restore (the review's finding). `app_ui_check_settings()` goes: the restore parses the cleaned file itself.

The page (spec §10.3): Solar shows only the fields its source needs (up to 2 planes of kWp, tilt and azimuth; the losses and the inverter's limit for our model; Forecast.Solar's optional key; Solcast's key and 1–2 site ids, its planes being set on solcast.com); the house's energy (off or SolaX Cloud, its token and registration number, the home battery); a key field is empty and says whether one is set, sends what you type and Clear sends `null`; Check now; the last forecast and reading with their times and errors, a kept forecast with why and, for Solcast, when it is asked again; the credits. The Sync page gets a card of switches for the data steps, and shows Solar and Energy among the steps, each with why it failed, kept or was skipped. The editor names the Solar and Energy layouts, says they have no slots, groups the Solar and Energy fields and marks a field whose step is off.

- [ ] **Step 1: Write the failing tests.**

`test/web/test_app.mjs`:

```diff
--- a/test/web/test_app.mjs
+++ b/test/web/test_app.mjs
@@ -672,3 +672,250 @@ test('a cell says when its field draws at a smaller size than the cell\'s', asyn
   await ctx.presetsPage();
   assert.deepEqual(cellLabels(main), ['1 · 400×209 · XL (Next hours at M)', '2 · 400×69 · S']);
 });
+
+/* ---- M6d: the steps' switches, the Solar page, the editor's Solar and Energy fields ---- */
+
+const STEP_SETTINGS = { ...SYNC_SETTINGS, sync: { ...SYNC_SETTINGS.sync,
+                                                  steps: ['weather', 'air', 'radar', 'solar', 'energy'] } };
+
+test('the Sync page switches the data steps on and off, the time always on', async () => {
+  const patches = [];
+  const { ctx, main } = await load({
+    'GET /api/settings': () => reply(200, STEP_SETTINGS),
+    'GET /api/status': () => reply(200, syncStatus({ mode: 'times', running: false })),
+    'PATCH /api/settings': (init) => { patches.push(JSON.parse(init.body)); return reply(200, STEP_SETTINGS); },
+  });
+  await ctx.syncPage();
+  const box = (name) => below(main).find((e) => e.tag === 'label' && text(e) === name).children[0];
+  assert.equal(box('Radar').checked, true);
+  box('Radar').checked = false;
+  box('House energy').checked = false;
+  await buttonNamed(main, 'Save steps').click();
+  assert.deepEqual(patches.at(-1), { sync: { steps: ['weather', 'air', 'solar'] } });
+  assert.match(text(main), /The time always runs/);
+});
+
+test('the Sync page shows the Solar and Energy steps, a kept one as kept', async () => {
+  const last = { at: 1790880000, failed: 'energy', detail: 'tokenId is invalid',
+                 steps: { wifi: 'ok', time: 'ok', weather: 'ok', air: 'ok', radar: 'ok', solar: 'kept', energy: 'failed' } };
+  const { ctx, main } = await load({
+    'GET /api/settings': () => reply(200, STEP_SETTINGS),
+    'GET /api/status': () => reply(200, syncStatus({ mode: 'times', running: false, last })),
+  });
+  await ctx.syncPage();
+  assert.match(text(main), /Solar forecastkept/);
+  assert.match(text(main), /House energyfailed: tokenId is invalid/);
+});
+
+test('the Sync page says why a step was kept or skipped', async () => {
+  const last = { at: 1790880000, steps: { wifi: 'ok', time: 'ok', weather: 'skipped', air: 'ok', radar: 'ok',
+                                          solar: 'kept', energy: 'skipped' },
+                 details: { weather: 'off', solar: 'HTTP 429', energy: 'no source' } };
+  const { ctx, main } = await load({
+    'GET /api/settings': () => reply(200, STEP_SETTINGS),
+    'GET /api/status': () => reply(200, syncStatus({ mode: 'times', running: false, last })),
+  });
+  await ctx.syncPage();
+  assert.match(text(main), /Weatherskipped: off/);
+  assert.match(text(main), /Solar forecastkept: HTTP 429/);
+  assert.match(text(main), /House energyskipped: no source/);
+});
+
+const SOLAR_SETTINGS = { schema: 1, location: { name: 'Brno', lat: 49.1951, lon: 16.6068 },
+                         sync: { steps: ['weather', 'air', 'radar', 'solar', 'energy'] },
+                         solar: { source: 'open-meteo', planes: [{ kwp: 5, tilt: 35, azimuth: 0 }], losses_pct: 14,
+                                  inverter_kw: 0, keys: { fs_key: false, solcast_key: false, solcast_sites: 0 } },
+                         energy: { source: 'off', battery: 'auto', keys: { solax_token: false, solax_sn: false } } };
+const solarStatus = (solar = {}, energy = {}) => ({ device: {}, time: { valid: true }, battery: {}, sensors: {},
+  preset: {}, wifi: { state: 'station', ap_on: false }, sync: { mode: 'times', running: false },
+  solar: { source: 'open-meteo', ...solar }, energy: { source: 'off', ...energy } });
+
+function solarDevice(patches, settings = SOLAR_SETTINGS, extra = {}) {
+  return {
+    'GET /api/settings': () => reply(200, JSON.parse(JSON.stringify(settings))),
+    'GET /api/status': () => reply(200, solarStatus()),
+    'PATCH /api/settings': (init) => { patches.push(JSON.parse(init.body)); return reply(200, settings); },
+    ...extra,
+  };
+}
+
+/* An input by its label's text, in the order the page shows them. */
+function inputNamed(root, name, n = 0) {
+  const labels = below(root).filter((e) => e.tag === 'label' && text(e) === name);
+  assert.ok(labels.length > n, `no field "${name}"`);
+  const all = below(root);
+  return all.slice(all.indexOf(labels[n]) + 1).find((e) => e.tag === 'input' || e.tag === 'select');
+}
+
+async function type(el, value) {
+  el.value = value;
+  await Promise.all([...(el.listeners.input || []), ...(el.listeners.change || [])].map((fn) => fn({ target: el })));
+}
+
+test('the Solar page saves our model\'s planes, losses and inverter limit', async () => {
+  const patches = [];
+  const { ctx, main } = await load(solarDevice(patches));
+  await ctx.solarPage();
+  await type(inputNamed(main, 'kWp'), '5.2');
+  await buttonNamed(main, 'Add a second plane').click();
+  await type(inputNamed(main, 'kWp', 1), '2.4');
+  await type(inputNamed(main, 'Tilt (°)', 1), '20');
+  await type(inputNamed(main, 'Azimuth (°)', 1), '-90');
+  await type(inputNamed(main, 'Losses (%)'), '10');
+  await type(inputNamed(main, 'Inverter limit (kW)'), '4.6');
+  await buttonNamed(main, 'Save').click();
+  assert.deepEqual(patches.at(-1).solar, { source: 'open-meteo', planes: [{ kwp: 5.2, tilt: 35, azimuth: 0 },
+                                                                        { kwp: 2.4, tilt: 20, azimuth: -90 }],
+                                           losses_pct: 10, inverter_kw: 4.6 });
+});
+
+test('keys are write-only: the page says which are set and sends only what you type', async () => {
+  const patches = [];
+  const settings = { ...SOLAR_SETTINGS, solar: { ...SOLAR_SETTINGS.solar, source: 'forecast-solar',
+                                                 keys: { fs_key: true, solcast_key: false, solcast_sites: 0 } } };
+  const { ctx, main } = await load(solarDevice(patches, settings));
+  await ctx.solarPage();
+  const key = inputNamed(main, 'Forecast.Solar key (optional)');
+  assert.equal(key.value, '');
+  assert.equal(key.attrs.type, 'password');
+  assert.match(text(main), /A key is set/);
+  await buttonNamed(main, 'Save').click();
+  assert.equal(patches.at(-1).solar.fs_key, undefined); /* nothing typed: the key stays as it is */
+  await type(key, 'AbC123');
+  await buttonNamed(main, 'Save').click();
+  assert.equal(patches.at(-1).solar.fs_key, 'AbC123');
+  await buttonNamed(main, 'Clear the key').click();
+  await buttonNamed(main, 'Save').click();
+  assert.equal(patches.at(-1).solar.fs_key, null);
+});
+
+test('a refused save says why: a second plane needs a Forecast.Solar key', async () => {
+  const patches = [];
+  const settings = { ...SOLAR_SETTINGS, solar: { ...SOLAR_SETTINGS.solar, source: 'forecast-solar' } };
+  const { ctx, main } = await load(solarDevice(patches, settings, {
+    'PATCH /api/settings': () => reply(400, { error: 'a second plane needs a Forecast.Solar key' }),
+  }));
+  await ctx.solarPage();
+  await buttonNamed(main, 'Add a second plane').click();
+  await buttonNamed(main, 'Save').click();
+  assert.match(text(main), /A second plane needs a Forecast.Solar key/);
+});
+
+test('Solcast takes its key and two sites, and its planes are set on solcast.com', async () => {
+  const patches = [];
+  const settings = { ...SOLAR_SETTINGS, solar: { ...SOLAR_SETTINGS.solar, source: 'solcast' } };
+  const { ctx, main } = await load(solarDevice(patches, settings));
+  await ctx.solarPage();
+  assert.equal(below(main).some((e) => e.tag === 'label' && text(e) === 'kWp' && !hiddenAbove(main, e)), false);
+  await type(inputNamed(main, 'Solcast API key'), 'Kk_1-2');
+  await type(inputNamed(main, 'First site id'), 'ab12-cd34');
+  await type(inputNamed(main, 'Second site id (optional)'), 'ef56');
+  await buttonNamed(main, 'Save').click();
+  assert.equal(patches.at(-1).solar.solcast_key, 'Kk_1-2');
+  assert.deepEqual(patches.at(-1).solar.solcast_sites, ['ab12-cd34', 'ef56']);
+  assert.match(text(main), /10 calls a day/);
+});
+
+/* Whether `el` sits in a part of the page that is hidden. */
+function hiddenAbove(root, el) {
+  const path = (node, target, trail = []) => {
+    if (node === target) return trail;
+    for (const k of node.children.filter((c) => c instanceof FakeElement)) {
+      const found = path(k, target, [...trail, node]);
+      if (found) return found;
+    }
+    return null;
+  };
+  return (path(root, el) || []).some((n) => n.hidden);
+}
+
+test('the house\'s energy takes SolaX Cloud\'s token and registration number, and the battery', async () => {
+  const patches = [];
+  const { ctx, main } = await load(solarDevice(patches));
+  await ctx.solarPage();
+  await type(inputNamed(main, 'Source', 1), 'solax');
+  await type(inputNamed(main, 'Token'), '20200722');
+  await type(inputNamed(main, 'Registration number'), 'SXA1B2C3D4');
+  await type(inputNamed(main, 'Home battery'), 'on');
+  await buttonNamed(main, 'Save').click();
+  assert.deepEqual(patches.at(-1).energy, { source: 'solax', battery: 'on', solax_token: '20200722',
+                                            solax_sn: 'SXA1B2C3D4' });
+});
+
+test('Check now runs the two steps and shows what they brought', async () => {
+  let polls = 0;
+  const { ctx, calls, main } = await load(solarDevice([], SOLAR_SETTINGS, {
+    'GET /api/status': () => reply(200, ++polls < 3 ? solarStatus({ tried_at: 100 }, { source: 'solax', tried_at: 100 })
+      : solarStatus({ tried_at: 200, fetched_at: 200, day: 20731 },
+                    { source: 'solax', tried_at: 200, error: 'tokenId is invalid' })),
+    'POST /api/solar/check': () => reply(202, { started: true }),
+  }));
+  ctx.setTimeout = (fn) => { fn(); return 0; };
+  await ctx.solarPage();
+  await buttonNamed(main, 'Check now').click();
+  await settle();
+  assert.ok(calls.some((c) => c.path === '/api/solar/check' && c.init.method === 'POST'));
+  assert.match(text(main), /tokenId is invalid/);
+  assert.match(text(main), /Checked/);
+});
+
+test('the Solar page says when Solcast is asked again, and why its last call failed', async () => {
+  const solcast = solarStatus({ source: 'solcast', fetched_at: 100, tried_at: 300, kept: 'kept', next_at: 1790890800,
+                                error: 'HTTP 401' });
+  const { ctx, main } = await load(solarDevice([], SOLAR_SETTINGS, { 'GET /api/status': () => reply(200, solcast) }));
+  await ctx.solarPage();
+  assert.match(text(main), /Its last stepkept the forecast: Solcast is asked again from /);
+  assert.match(text(main), /Its last callfailed: HTTP 401/);
+  const limited = solarStatus({ source: 'forecast-solar', fetched_at: 100, tried_at: 300, kept: 'HTTP 429' });
+  const page = await load(solarDevice([], SOLAR_SETTINGS, { 'GET /api/status': () => reply(200, limited) }));
+  await page.ctx.solarPage();
+  assert.match(text(page.main), /Its last stepkept the forecast \(HTTP 429\)/);
+  assert.doesNotMatch(text(page.main), /Its last call/);
+});
+
+test('the Solar page credits its sources', async () => {
+  const { ctx, main } = await load(solarDevice([]));
+  await ctx.solarPage();
+  const all = text(main);
+  for (const credit of ['Open-Meteo', 'Forecast.Solar', 'CC BY-SA 4.0', 'Solcast', 'personal use', 'SolaX Cloud']) {
+    assert.ok(all.includes(credit), credit);
+  }
+});
+
+test('a preset on the Solar or Energy layout has no slots to fill', async () => {
+  const catalogue = { ...CATALOGUE, layouts: [...CATALOGUE.layouts, { id: 'solar', slots: [] }, { id: 'energy', slots: [] }] };
+  const doc = { schema: 1, active: 'solar', presets: [{ id: 'solar', name: 'Solar', layout: 'solar', in_cycle: false,
+                                                          slots: {}, options: {} }],
+                cycle: { enabled: false, interval_s: 60 }, schedule: { enabled: false, entries: [] } };
+  const { ctx, main } = await load({
+    'GET /api/layouts': () => reply(200, catalogue), 'GET /api/presets': () => reply(200, doc),
+    'GET /api/fields': () => reply(200, FIELDS), 'POST /api/preview.bmp': () => reply(200, 'BM', 'image/bmp'),
+    'GET /api/settings': () => reply(200, STEP_SETTINGS),
+  });
+  await ctx.presetsPage();
+  assert.match(text(main), /today's PV forecast: its source is on the Solar page/);
+  assert.match(text(main), /Solar/);
+});
+
+test('the field lists group the Solar and Energy fields and mark those whose step is off', async () => {
+  const catalogue = { ...CATALOGUE, layouts: [{ id: 'classic', slots: [
+    { id: 's1', x: 0, y: 172, w: 200, h: 128, size: 'S', kinds: ['number'] }] }] };
+  const fields = { fields: [{ id: 'env.temp', kind: 'number', label: 'Temperature', value: '23.7 °C' },
+                            { id: 'pv.now', kind: 'number', label: 'Forecast now', value: '3.50 kW' },
+                            { id: 'energy.pv', kind: 'number', label: 'Solar', value: '3.42 kW' }] };
+  const doc = { schema: 1, active: 'home', presets: [{ id: 'home', name: 'Home', layout: 'classic', in_cycle: true,
+                                                         slots: {}, options: {} }],
+                cycle: { enabled: false, interval_s: 60 }, schedule: { enabled: false, entries: [] } };
+  const settings = { ...STEP_SETTINGS, sync: { ...STEP_SETTINGS.sync, steps: ['weather', 'air', 'radar', 'solar'] } };
+  const { ctx, main } = await load({
+    'GET /api/layouts': () => reply(200, catalogue), 'GET /api/presets': () => reply(200, doc),
+    'GET /api/fields': () => reply(200, fields), 'POST /api/preview.bmp': () => reply(200, 'BM', 'image/bmp'),
+    'GET /api/settings': () => reply(200, settings),
+  });
+  await ctx.presetsPage();
+  const groups = below(main).filter((e) => e.tag === 'optgroup').map((g) => g.attrs.label);
+  assert.ok(groups.includes('Solar forecast') && groups.includes('House energy'), groups.join());
+  const options = below(main).filter((e) => e.tag === 'option').map(text);
+  assert.ok(options.some((o) => /^Solar — 3.42 kW \(its sync step is off\)$/.test(o)), options.join(' | '));
+  assert.ok(options.some((o) => /^Forecast now — 3.50 kW$/.test(o)), options.join(' | '));
+});
```


- [ ] **Step 2: Run them to see them fail.**

Run: `node --test test/web/test_app.mjs 2>&1 | grep -E '^not ok|^# (pass|fail)'`
Expected:

```
not ok 37 - the Sync page switches the data steps on and off, the time always on
not ok 38 - the Sync page shows the Solar and Energy steps, a kept one as kept
not ok 39 - the Sync page says why a step was kept or skipped
not ok 40 - the Solar page saves our model's planes, losses and inverter limit
not ok 41 - keys are write-only: the page says which are set and sends only what you type
not ok 42 - a refused save says why: a second plane needs a Forecast.Solar key
not ok 43 - Solcast takes its key and two sites, and its planes are set on solcast.com
not ok 44 - the house's energy takes SolaX Cloud's token and registration number, and the battery
not ok 45 - Check now runs the two steps and shows what they brought
not ok 46 - the Solar page says when Solcast is asked again, and why its last call failed
not ok 47 - the Solar page credits its sources
not ok 48 - a preset on the Solar or Energy layout has no slots to fill
not ok 49 - the field lists group the Solar and Energy fields and mark those whose step is off
# pass 36
# fail 13
```

- [ ] **Step 3: The page.** Its value is set on a select as well as on its option, and `replaceChildren` never gets `null` (a browser would show it as text):

`web/app.js`:

```diff
--- a/web/app.js
+++ b/web/app.js
@@ -224,7 +224,7 @@ document.getElementById('done').onclick = async () => {
 /* ---- pages ---- */
 
 const pages = { status: statusPage, wifi: wifiPage, place: placePage, sync: syncPage, radar: radarPage,
-                device: devicePage, presets: presetsPage, firmware: firmwarePage, backup: backupPage };
+                solar: solarPage, device: devicePage, presets: presetsPage, firmware: firmwarePage, backup: backupPage };
 
 function route() {
   const name = location.hash.slice(1) || 'status';
@@ -307,7 +307,9 @@ async function statusPage() {
 /* ---- Sync (spec §9.3, D25) ---- */
 
 const SYNC_STEPS = [['wifi', 'Wi-Fi'], ['time', 'Time'], ['weather', 'Weather'], ['air', 'Air quality'],
-                    ['radar', 'Radar']];
+                    ['radar', 'Radar'], ['solar', 'Solar forecast'], ['energy', 'House energy']];
+/* The data steps a sync can leave out (D35): the time always runs, as the clock and its trim need it. */
+const STEP_SWITCHES = SYNC_STEPS.slice(2);
 const SYNC_INTERVALS = [15, 30, 60, 120, 180, 360, 720, 1440];
 const intervalLabel = (m) => (m < 60 ? `${m} min` : `${m / 60} h`);
 
@@ -339,9 +341,11 @@ async function syncPage() {
   const steps = h('dl', { class: 'facts' });
   const showSteps = (status) => {
     const last = status.sync.last;
+    const detail = (k) => (last.details || {})[k] || (last.failed === k ? last.detail : '');
     steps.replaceChildren(...SYNC_STEPS.flatMap(([k, name]) => [h('dt', { text: name }),
       h('dd', { class: !last ? '' : last.steps[k] === 'ok' ? 'good' : last.steps[k] === 'failed' ? 'bad' : 'muted',
-                text: !last ? '—' : last.steps[k] === 'failed' && last.failed === k ? `failed: ${last.detail}` : last.steps[k] })]));
+                text: !last ? '—' : last.steps[k] && last.steps[k] !== 'ok' && detail(k) ? `${last.steps[k]}: ${detail(k)}`
+                  : last.steps[k] || '—' })]));
   };
   const summary = h('div');
   const rtc = h('p', { class: 'muted small' });
@@ -439,7 +443,22 @@ async function syncPage() {
       note.textContent = moved.length ? `Saved. ${moved.join(', ')} falls in the quiet hours: it runs at ${to.value}.` : 'Saved.';
       toast('Saved');
     }), 'primary')));
-  main.replaceChildren(h('h1', { text: 'Sync' }), nowCard, schedCard);
+  const on = new Set(Array.isArray(sync.steps) ? sync.steps : STEP_SWITCHES.map(([k]) => k));
+  const switches = STEP_SWITCHES.map(([k, name]) => h('label', { class: 'check' },
+    h('input', { type: 'checkbox', checked: on.has(k) }), name));
+  const stepsNote = h('p');
+  const stepsCard = card('Steps',
+    h('p', { class: 'muted small', text: 'A step that is off makes no requests, and its data age out as usual. The time ' +
+      'always runs: the clock and its trim need it.' }),
+    ...switches, stepsNote,
+    actions(button('Save steps', () => busy(stepsCard, stepsNote, async () => {
+      const list = STEP_SWITCHES.filter((_, i) => switches[i].children[0].checked).map(([k]) => k);
+      await api('PATCH', '/api/settings', { sync: { steps: list } });
+      stepsNote.className = 'good';
+      stepsNote.textContent = 'Saved.';
+      toast('Saved');
+    }), 'primary')));
+  main.replaceChildren(h('h1', { text: 'Sync' }), nowCard, schedCard, stepsCard);
 }
 
 /* ---- Wi-Fi (spec §10.1, §10.2) ---- */
@@ -764,6 +783,151 @@ async function radarPage() {
 
 /* ---- Device: the settings the menu also has (spec §5.7, D19) ---- */
 
+/* ---- Solar (spec §11.5, §11.6, D35, D36) ---- */
+
+const SOLAR_SOURCES = [['off', 'Off'], ['open-meteo', 'Open-Meteo, through the device\'s own model'],
+                       ['forecast-solar', 'Forecast.Solar'], ['solcast', 'Solcast']];
+const ENERGY_SOURCES = [['off', 'Off'], ['solax', 'SolaX Cloud']];
+const BATTERY_MODES = [['auto', 'Automatic: a hybrid inverter, or a charge above 0 %'], ['on', 'Shown'],
+                       ['off', 'Hidden']];
+
+function choose(options, value) {
+  const el = h('select', {}, options.map(([v, t]) => h('option', { value: v, selected: v === value }, t)));
+  el.value = value;
+  return el;
+}
+
+/* A number input bound to obj[key]. */
+function numberInput(obj, key, min, max, step) {
+  return h('input', { type: 'number', min: String(min), max: String(max), step: String(step), value: String(obj[key]),
+                      oninput: (ev) => { obj[key] = Number(ev.target.value); } });
+}
+
+/* A write-only key (spec §10.3): empty, it says whether one is set; what you type is sent; Clear sends null. */
+function secretInput(label, isSet, hint) {
+  const input = h('input', { type: 'password', autocomplete: 'off', placeholder: isSet ? 'set: type to replace it' : '' });
+  const note = h('p', { class: 'muted small', text: isSet ? 'A key is set.' : 'None is set.' });
+  const s = { input, isSet, cleared: false };
+  s.value = () => (input.value ? input.value : s.cleared ? null : undefined);
+  s.el = [field(label, input, hint), note,
+          isSet ? actions(button('Clear the key', () => { s.cleared = true; input.value = ''; note.textContent = 'It goes when you save.'; }))
+                : null];
+  return s;
+}
+
+async function solarPage() {
+  const [s, st] = await Promise.all([api('GET', '/api/settings'), api('GET', '/api/status')]);
+  const solar = s.solar || {}, energy = s.energy || {}, keys = solar.keys || {}, ekeys = energy.keys || {};
+
+  const source = choose(SOLAR_SOURCES, solar.source || 'off');
+  const planes = (Array.isArray(solar.planes) && solar.planes.length ? solar.planes : [{ kwp: 5, tilt: 35, azimuth: 0 }])
+    .slice(0, 2).map((q) => ({ kwp: q.kwp, tilt: q.tilt, azimuth: q.azimuth }));
+  const planeBox = h('div');
+  const showPlanes = () => planeBox.replaceChildren(...[...planes.map((q, i) => h('div', {},
+    h('h3', { text: planes.length > 1 ? `Plane ${i + 1}` : 'The roof' }),
+    h('div', { class: 'row' }, h('div', {}, field('kWp', numberInput(q, 'kwp', 0.1, 100, 0.01))),
+      h('div', {}, field('Tilt (°)', numberInput(q, 'tilt', 0, 90, 1))),
+      h('div', {}, field('Azimuth (°)', numberInput(q, 'azimuth', -180, 180, 1)))),
+    planes.length > 1 ? actions(button('Remove this plane', () => { planes.splice(i, 1); showPlanes(); })) : null)),
+  h('p', { class: 'muted small', text: 'Azimuth 0 is south, -90 east, 90 west.' }),
+  planes.length < 2 ? actions(button('Add a second plane', () => { planes.push({ kwp: 2, tilt: 35, azimuth: -90 }); showPlanes(); }))
+    : null].filter(Boolean));
+  showPlanes();
+  const model = { losses: solar.losses_pct ?? 14, inverter: solar.inverter_kw ?? 0 };
+  const modelBox = h('div', { class: 'row' },
+    h('div', {}, field('Losses (%)', numberInput(model, 'losses', 0, 50, 1))),
+    h('div', {}, field('Inverter limit (kW)', numberInput(model, 'inverter', 0, 100, 0.01), '0 for none.')));
+  const fsKey = secretInput('Forecast.Solar key (optional)', !!keys.fs_key,
+    'Without one: one plane, hourly values, today and tomorrow. A second plane needs one.');
+  const scKey = secretInput('Solcast API key', !!keys.solcast_key);
+  const sites = [secretInput('First site id', (keys.solcast_sites || 0) >= 1),
+                 secretInput('Second site id (optional)', (keys.solcast_sites || 0) >= 2)];
+  const fsBox = h('div', {}, fsKey.el);
+  const scBox = h('div', {}, h('p', { class: 'muted small', text: 'Solcast knows the roof: its tilt, azimuth and kWp ' +
+    'are set on solcast.com. A hobbyist account has 10 calls a day: the device asks at most every 3 h with one site, ' +
+    'every 6 h with two, and keeps the forecast it has in between.' }), scKey.el, sites[0].el, sites[1].el);
+  const show = () => {
+    const v = source.value;
+    planeBox.hidden = v !== 'open-meteo' && v !== 'forecast-solar';
+    modelBox.hidden = v !== 'open-meteo';
+    fsBox.hidden = v !== 'forecast-solar';
+    scBox.hidden = v !== 'solcast';
+  };
+  source.addEventListener('change', show);
+  show();
+
+  const esource = choose(ENERGY_SOURCES, energy.source || 'off');
+  const token = secretInput('Token', !!ekeys.solax_token, 'From the API page of solaxcloud.com.');
+  const sn = secretInput('Registration number', !!ekeys.solax_sn, 'The dongle\'s, on its label.');
+  const battery = choose(BATTERY_MODES, energy.battery || 'auto');
+  const solaxBox = h('div', {}, token.el, sn.el);
+  const eshow = () => { solaxBox.hidden = esource.value !== 'solax'; };
+  esource.addEventListener('change', eshow);
+  eshow();
+
+  const note = h('p');
+  const save = card(null, note, actions(button('Save', () => busy(save, note, async () => {
+    const touched = sites.some((x) => x.value() !== undefined);
+    if (touched && sites.some((x) => x.isSet && x.value() === undefined)) {
+      throw new ApiError('Type both site ids, or clear the one you don\'t want.');
+    }
+    const out = { solar: { source: source.value, planes: planes.map((q) => ({ kwp: Number(q.kwp), tilt: Number(q.tilt),
+                                                                             azimuth: Number(q.azimuth) })),
+                           losses_pct: Number(model.losses), inverter_kw: Number(model.inverter) },
+                  energy: { source: esource.value, battery: battery.value } };
+    const put = (obj, key, v) => { if (v !== undefined) obj[key] = v; };
+    put(out.solar, 'fs_key', fsKey.value());
+    put(out.solar, 'solcast_key', scKey.value());
+    if (touched) out.solar.solcast_sites = sites.map((x) => x.value() || '').filter(Boolean);
+    put(out.energy, 'solax_token', token.value());
+    put(out.energy, 'solax_sn', sn.value());
+    await api('PATCH', '/api/settings', out);
+    note.className = 'good';
+    note.textContent = 'Saved.';
+    toast('Saved');
+  }), 'primary')));
+
+  const facts2 = h('div');
+  const showNow = (status) => {
+    const f = status.solar || {}, e = status.energy || {};
+    facts2.replaceChildren(facts([
+      ['Forecast', f.fetched_at ? `from ${when(f.fetched_at)}` : 'none yet'],
+      f.kept ? ['Its last step', f.kept !== 'kept' ? `kept the forecast (${f.kept})` : 'kept the forecast: Solcast ' +
+        `is asked again from ${f.next_at ? when(f.next_at) : 'its next sync'}, within its 10 calls a day`] : null,
+      f.error ? ['Its last call', `failed: ${f.error}`] : null,
+      ['House reading', e.reading_at ? `from ${when(e.reading_at)}` : 'none yet'],
+      e.error ? ['Its last step', `failed: ${e.error}`] : null,
+    ]));
+  };
+  showNow(st);
+  const checkNote = h('p');
+  const nowCard = card('Now', facts2, checkNote, actions(button('Check now', () => busy(nowCard, checkNote, async () => {
+    const before = await api('GET', '/api/status');
+    const tried = (x) => `${(x.solar || {}).tried_at}/${(x.energy || {}).tried_at}`;
+    await api('POST', '/api/solar/check');
+    checkNote.className = 'muted';
+    checkNote.textContent = 'Checking…';
+    for (let i = 0; i < 40; i++) { /* the two steps take some seconds, 45 s at most (spec §9.3) */
+      await sleep(1500);
+      const now = await api('GET', '/api/status');
+      if (tried(now) !== tried(before)) {
+        showNow(now);
+        checkNote.className = 'good';
+        checkNote.textContent = 'Checked.';
+        return;
+      }
+    }
+    checkNote.textContent = 'No result yet; see Sync.';
+  }))));
+
+  main.replaceChildren(h('h1', { text: 'Solar' }),
+    card('PV forecast', field('Source', source), planeBox, modelBox, fsBox, scBox),
+    card('The house\'s energy', field('Source', esource), solaxBox, field('Home battery', battery)),
+    save, nowCard,
+    card('Credits', h('p', { class: 'muted small', text: 'Forecasts: Open-Meteo (CC BY 4.0), Forecast.Solar (CC BY-SA ' +
+      '4.0), Solcast (for personal use only, as its terms say). The house\'s readings: SolaX Cloud.' })));
+}
+
 const LANGUAGES = [['en', 'English'], ['cs', 'Čeština']];
 const SENSOR_MIN = [1, 2, 5, 10, 15, 30];
 const RATES = [0.25, 0.5, 1, 2, 4, 8];
@@ -842,9 +1006,28 @@ async function devicePage() {
 /* ---- Presets (spec §5.4) ---- */
 
 const LAYOUT_NAMES = { classic: 'Classic', weather: 'Weather', grid: 'Grid', focus: 'Focus', radar: 'Radar',
-                       flights: 'Flights', split: 'Split' };
+                       flights: 'Flights', split: 'Split', solar: 'Solar', energy: 'Energy' };
 const NO_SLOTS = { radar: 'This layout draws the weather radar: its centre and zoom are on the Radar page.',
-                   flights: 'This layout draws the flight radar: its centre, range and filters are on the Radar page.' };
+                   flights: 'This layout draws the flight radar: its centre, range and filters are on the Radar page.',
+                   solar: 'This layout draws today\'s PV forecast: its source is on the Solar page.',
+                   energy: 'This layout draws the house\'s energy now: its source is on the Solar page.' };
+/* The sync step a field's data come from (spec §9.3, D35): a field whose step is off ages out. */
+const fieldStep = (id) => (/^(wx\.)/.test(id) ? 'weather' : /^(aq|pollen)\./.test(id) ? 'air' : id === 'rain.map' ? 'radar'
+  : id.startsWith('pv.') ? 'solar' : id.startsWith('energy.') ? 'energy' : null);
+const FIELD_GROUPS = [['solar', 'Solar forecast'], ['energy', 'House energy']];
+
+/* A slot's or a cell's choices: the fields `fits` allows, the Solar and Energy ones in their groups, each marked
+ * when its sync step is off. */
+function fieldOptions(ed, fits, selected) {
+  const option = (f) => h('option', { value: f.id, selected: selected === f.id },
+    `${f.label} — ${f.value || 'no data yet'}${ed.stepsOff.has(fieldStep(f.id)) ? ' (its sync step is off)' : ''}`);
+  const grouped = new Set(FIELD_GROUPS.map(([k]) => k));
+  return [h('option', { value: '' }, '(empty)'), fits.filter((f) => !grouped.has(fieldStep(f.id))).map(option),
+          FIELD_GROUPS.map(([k, label]) => {
+            const list = fits.filter((f) => fieldStep(f.id) === k);
+            return list.length ? h('optgroup', { label }, list.map(option)) : null;
+          })];
+}
 const CYCLE_S = [10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600];
 const cycleLabel = (s) => (s < 60 ? `${s} s` : s < 3600 ? `${s / 60} min` : `${s / 3600} h`);
 const DAYS = ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']; /* bit 0 is Monday */
@@ -860,9 +1043,12 @@ function uniqueId(doc, base) {
 
 async function presetsPage() {
   if (!catalogue) catalogue = await api('GET', '/api/layouts');
-  const [doc, fieldList] = await Promise.all([api('GET', '/api/presets'), api('GET', '/api/fields')]);
+  const [doc, fieldList, settings] = await Promise.all([api('GET', '/api/presets'), api('GET', '/api/fields'),
+    api('GET', '/api/settings').catch(() => ({}))]);
+  const steps = (settings.sync || {}).steps;
   const ed = {
     doc, fields: fieldList.fields, dirty: false,
+    stepsOff: new Set(Array.isArray(steps) ? STEP_SWITCHES.map(([k]) => k).filter((k) => !steps.includes(k)) : []),
     sel: Math.max(0, doc.presets.findIndex((p) => p.id === doc.active)),
     img: h('img', { class: 'screen', alt: 'Preview' }), previewNote: h('p', { class: 'small' }),
     timer: null, url: null, saveNote: h('p'),
@@ -951,8 +1137,7 @@ function splitEditor(ed, p) {
         if (ev.target.value) node.field = ev.target.value;
         else delete node.field;
         changed(ed, false);
-      } }, h('option', { value: '' }, '(empty)'), fits.map((f) => h('option',
-        { value: f.id, selected: node.field === f.id }, `${f.label} — ${f.value || 'no data yet'}`)));
+      } }, fieldOptions(ed, fits, node.field));
       const splitButton = (dir, text) => {
         const half = { split: dir, ratio: '1/2', line: true, a: {}, b: {} }; /* the field goes to the first part */
         const b = button(text, () => {
@@ -1076,9 +1261,7 @@ function renderPresets(ed) {
       if (ev.target.value) p.slots[slot.id] = ev.target.value;
       else delete p.slots[slot.id];
       changed(ed, false);
-    } }, h('option', { value: '' }, '(empty)'),
-    ed.fields.filter((f) => slot.kinds.includes(f.kind)).map((f) => h('option',
-      { value: f.id, selected: p.slots[slot.id] === f.id }, `${f.label} — ${f.value || 'no data yet'}`))))));
+    } }, fieldOptions(ed, ed.fields.filter((f) => slot.kinds.includes(f.kind)), p.slots[slot.id])))));
 
   const o = p.options;
   const check = (key, text) => h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: !!o[key],
```


`web/index.html`:

```diff
--- a/web/index.html
+++ b/web/index.html
@@ -18,6 +18,7 @@
   <a href="#place">Location &amp; time</a>
   <a href="#sync">Sync</a>
   <a href="#radar">Radar</a>
+  <a href="#solar">Solar</a>
   <a href="#device">Device</a>
   <a href="#presets">Presets</a>
   <a href="#firmware">Firmware</a>
```


- [ ] **Step 4: Run the page's tests.**

Run: `node --test test/web/test_app.mjs 2>&1 | grep -E '^not ok|^# (pass|fail)'`
Expected:

```
# pass 49
# fail 0
```

- [ ] **Step 5: The API.**

`main/app_web.c`:

```diff
--- a/main/app_web.c
+++ b/main/app_web.c
@@ -18,6 +18,7 @@
 #include "timekeeping.h"
 #include "ui_catalog.h"
 #include "ui_dashboard.h"
+#include "util_time.h"
 #include "webui.h"
 
 /* The web configurator's API (spec §10.3), apart from what webui answers itself (the password,
@@ -236,6 +237,49 @@ static void get_status(uint8_t *out, size_t size, webui_reply_t *reply)
         cJSON_AddNumberToObject(flights, "routes_paused_until", (double)fs.routes_paused_until);
     }
 
+    /* spec §10.3, M6d: the PV forecast and the house's energy, never a key */
+    const app_solar_state_t *ss = app_solar_state();
+    static const char *const k_solar[] = { "off", "open-meteo", "forecast-solar", "solcast" };
+    cJSON *solar = cJSON_AddObjectToObject(o, "solar");
+    uint8_t src = st->settings.solar_source;
+    cJSON_AddStringToObject(solar, "source", k_solar[src <= SETTINGS_SOLAR_SOLCAST ? src : 0]);
+    if (ss->forecast.fetched != 0) {
+        cJSON_AddNumberToObject(solar, "fetched_at", ss->forecast.fetched);
+    }
+    if (ss->forecast.day != 0) {
+        int y, m, d;
+        util_civil_from_days(ss->forecast.day, &y, &m, &d);
+        char day[16];
+        snprintf(day, sizeof(day), "%04d-%02d-%02d", y, m, d);
+        cJSON_AddStringToObject(solar, "day", day); /* the local day of its first quarter hours */
+    }
+    if (ss->forecast_tried != 0) {
+        cJSON_AddNumberToObject(solar, "tried_at", ss->forecast_tried);
+    }
+    if (ss->forecast_error[0] != '\0') {
+        cJSON_AddStringToObject(solar, "error", ss->forecast_error); /* the last call's */
+    }
+    if (ss->forecast_kept[0] != '\0') {
+        cJSON_AddStringToObject(solar, "kept", ss->forecast_kept);
+    }
+    if (src == SETTINGS_SOLAR_SOLCAST && ss->solcast_asked != 0) { /* when its budget lets the next step ask */
+        cJSON_AddNumberToObject(solar, "next_at", (double)ss->solcast_asked + solar_solcast_wait_s(ss->solcast_sites));
+    }
+    if (ss->demo) {
+        cJSON_AddBoolToObject(solar, "demo", true);
+    }
+    cJSON *energy = cJSON_AddObjectToObject(o, "energy");
+    cJSON_AddStringToObject(energy, "source", st->settings.energy_source == SETTINGS_ENERGY_SOLAX ? "solax" : "off");
+    if (ss->reading.at != 0) {
+        cJSON_AddNumberToObject(energy, "reading_at", ss->reading.at);
+    }
+    if (ss->energy_tried != 0) {
+        cJSON_AddNumberToObject(energy, "tried_at", ss->energy_tried);
+    }
+    if (ss->energy_error[0] != '\0') {
+        cJSON_AddStringToObject(energy, "error", ss->energy_error);
+    }
+
     const ui_preset_t *active = &st->presets.presets[st->presets.active];
     cJSON *preset = cJSON_AddObjectToObject(o, "preset");
     cJSON_AddStringToObject(preset, "active", active->id);
@@ -387,6 +431,54 @@ static void backup(uint8_t *out, size_t size, webui_reply_t *reply)
                backup_build(files, 2, net.host, esp_app_get_description()->version, (char *)out, size));
 }
 
+/* GET /api/settings (spec §10.3): the file's settings, and which keys are set, never the keys. */
+static void get_settings(uint8_t *out, size_t size, webui_reply_t *reply)
+{
+    EXT_RAM_BSS_ATTR static char text[SETTINGS_FILE_MAX];
+    cJSON *o = app_ui_settings_json(text, sizeof(text)) > 0 ? cJSON_Parse(text) : NULL;
+    if (!cJSON_IsObject(o)) {
+        cJSON_Delete(o);
+        reply_error(reply, out, size, 500, "the settings don't print");
+        return;
+    }
+    cJSON *keys = cJSON_AddObjectToObject(cJSON_GetObjectItemCaseSensitive(o, "solar"), "keys");
+    cJSON_AddBoolToObject(keys, "fs_key", app_secret_set(SETTINGS_SECRET_FS_KEY));
+    cJSON_AddBoolToObject(keys, "solcast_key", app_secret_set(SETTINGS_SECRET_SOLCAST_KEY));
+    cJSON_AddNumberToObject(keys, "solcast_sites", app_secret_set(SETTINGS_SECRET_SOLCAST_SITE1) +
+                                                       app_secret_set(SETTINGS_SECRET_SOLCAST_SITE2));
+    keys = cJSON_AddObjectToObject(cJSON_GetObjectItemCaseSensitive(o, "energy"), "keys");
+    cJSON_AddBoolToObject(keys, "solax_token", app_secret_set(SETTINGS_SECRET_SOLAX_TOKEN));
+    cJSON_AddBoolToObject(keys, "solax_sn", app_secret_set(SETTINGS_SECRET_SOLAX_SN));
+    reply_cjson(reply, out, size, o);
+}
+
+/* PATCH /api/settings (spec §10.3, §14.3): the keys to NVS `secrets`, the rest merged into the file; a second
+ * Forecast.Solar plane without a key is refused (spec §11.5). */
+static void patch_settings(const char *body, uint8_t *out, size_t size, webui_reply_t *reply)
+{
+    EXT_RAM_BSS_ATTR static char clean[SETTINGS_FILE_MAX], base[SETTINGS_FILE_MAX], merged[SETTINGS_FILE_MAX];
+    EXT_RAM_BSS_ATTR static settings_secrets_t secrets;
+    EXT_RAM_BSS_ATTR static settings_t next;
+    char err[112] = "the settings don't fit";
+    bool ok = settings_take_secrets(body, clean, sizeof(clean), &secrets, err, sizeof(err)) > 0 &&
+              app_ui_settings_json(base, sizeof(base)) > 0 &&
+              settings_patch(base, clean, merged, sizeof(merged), err, sizeof(err)) > 0 &&
+              settings_from_json(merged, app_settings(), &next, err, sizeof(err));
+    bool fs_key = secrets.given[SETTINGS_SECRET_FS_KEY] ? secrets.value[SETTINGS_SECRET_FS_KEY][0] != '\0'
+                                                         : app_secret_set(SETTINGS_SECRET_FS_KEY);
+    ok = ok && settings_check_solar(&next, fs_key, err, sizeof(err)) &&
+         app_ui_patch_settings(clean, err, sizeof(err)) == ESP_OK;
+    esp_err_t saved = ok ? app_secrets_apply(&secrets) : ESP_OK;
+    memset(&secrets, 0, sizeof(secrets)); /* the keys stay in NVS alone */
+    if (!ok) {
+        reply_error(reply, out, size, 400, err);
+    } else if (saved != ESP_OK) {
+        reply_error(reply, out, size, 500, "the keys weren't saved");
+    } else {
+        get_settings(out, size, reply);
+    }
+}
+
 /* POST /api/restore: every file is checked before any is replaced. */
 static void restore(const char *body, uint8_t *out, size_t size, webui_reply_t *reply)
 {
@@ -411,7 +503,18 @@ static void restore(const char *body, uint8_t *out, size_t size, webui_reply_t *
         }
     }
     char why[96];
-    if (settings != NULL && !app_ui_check_settings(settings, why, sizeof(why))) {
+    EXT_RAM_BSS_ATTR static char clean[SETTINGS_FILE_MAX];
+    EXT_RAM_BSS_ATTR static settings_secrets_t dropped;
+    EXT_RAM_BSS_ATTR static settings_t next;
+    bool settings_ok = true;
+    if (settings != NULL) { /* a key a bundle carries never reaches the file (spec §14.4); a second plane needs one */
+        settings_ok = settings_take_secrets(settings, clean, sizeof(clean), &dropped, why, sizeof(why)) > 0 &&
+                      settings_from_json(clean, app_settings(), &next, why, sizeof(why)) &&
+                      settings_check_solar(&next, app_secret_set(SETTINGS_SECRET_FS_KEY), why, sizeof(why));
+        memset(&dropped, 0, sizeof(dropped));
+        settings = clean;
+    }
+    if (!settings_ok) {
         snprintf(err, sizeof(err), "settings.json: %s", why);
     } else if (presets_text != NULL && !ui_presets_from_json(presets_text, &presets, why, sizeof(why))) {
         snprintf(err, sizeof(err), "presets.json: %s", why);
@@ -448,13 +551,9 @@ void app_web_api(const char *method, const char *path, const char *query, const
     if (strcmp(path, "/api/status") == 0 && get) {
         get_status(out, size, reply);
     } else if (strcmp(path, "/api/settings") == 0 && get) {
-        reply_text(reply, out, size, app_ui_settings_json((char *)out, size));
+        get_settings(out, size, reply);
     } else if (strcmp(path, "/api/settings") == 0 && strcmp(method, "PATCH") == 0) {
-        if (app_ui_patch_settings(body, err, sizeof(err)) != ESP_OK) {
-            reply_error(reply, out, size, 400, err);
-        } else {
-            reply_text(reply, out, size, app_ui_settings_json((char *)out, size));
-        }
+        patch_settings(body, out, size, reply);
     } else if (strcmp(path, "/api/layouts") == 0 && get) {
         reply_text(reply, out, size, ui_catalog_layouts_json((char *)out, size));
     } else if (strcmp(path, "/api/fields") == 0 && get) {
@@ -491,6 +590,19 @@ void app_web_api(const char *method, const char *path, const char *query, const
                                                        "is on its own network only"
                                                      : "the sync didn't start");
         }
+    } else if (strcmp(path, "/api/solar/check") == 0 && strcmp(method, "POST") == 0) { /* spec §10.3, M6d */
+        esp_err_t e = app_sync_check();
+        if (e == ESP_OK) {
+            reply_text(reply, out, size, (size_t)snprintf((char *)out, size, "{\"started\":true}"));
+            reply->status = 202; /* the page follows it in GET /api/status */
+        } else {
+            reply_error(reply, out, size, 409,
+                        e == ESP_ERR_NOT_FOUND       ? "no Wi-Fi network is saved"
+                        : e == ESP_ERR_INVALID_ARG   ? "nothing to check: the forecast and the house's energy are off"
+                        : e == ESP_ERR_INVALID_STATE ? "not now: a sync runs, the battery is critical, or the device "
+                                                       "is on its own network only"
+                                                     : "the check didn't start");
+        }
     } else if (strcmp(path, "/api/backup") == 0 && get) {
         backup(out, size, reply);
     } else if (strcmp(path, "/api/restore") == 0 && strcmp(method, "POST") == 0) {
```


The restore parses the cleaned file itself, so the app's check of a whole file goes:

`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -661,12 +661,6 @@ size_t app_ui_settings_json(char *out, size_t size)
     return settings_to_json(&s.settings, s_settings_base[0] ? s_settings_base : NULL, out, size);
 }
 
-bool app_ui_check_settings(const char *json, char *err, size_t err_size)
-{
-    static settings_t parsed;
-    return settings_from_json(json, &s.settings, &parsed, err, err_size);
-}
-
 /* The new settings take effect everywhere: time zone, offsets, panel rate, slots and the text. */
 static void settings_changed(void)
 {
```


`main/app_internal.h`:

```diff
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -119,7 +119,6 @@ bool app_ui_first_run(void);
 void app_ui_end_first_run(void);
 /* settings.json as it would be saved now (GET /api/settings, the backup). */
 size_t app_ui_settings_json(char *out, size_t size);
-bool app_ui_check_settings(const char *json, char *err, size_t err_size);
 /* A whole new settings.json (a restore) or a merge patch (PATCH /api/settings): validated, saved
  * and applied at once. ESP_ERR_INVALID_ARG with the reason in `err` if it doesn't parse. */
 esp_err_t app_ui_replace_settings(const char *json, char *err, size_t err_size);
```


- [ ] **Step 6: Run everything.** `ctest --test-dir build-host` (65 targets, the page's tests among them) and `tools/idf.sh build`: clean, without a warning. The app grows by 6.7 KB with the page, to 0x268b10 bytes (40 % of its partition free): M6d adds 36 KB in all (0x25fc30 before Task 1).

- [ ] **Step 7: Commit.**

```bash
git add web main/app_web.c main/app_ui.c main/app_internal.h test/web/test_app.mjs
git commit -m "feat(web): the Solar page, the steps' switches and write-only keys"
```

### Task 12: On the board, and the docs as built

**Files:**
- Modify (only if the checks find something): whatever they point at, each fix with its own test where one can fail first.
- Modify: `docs/specs/2026-09-25-firmware-design.md` (r38, as built), `AGENTS.md`, `README.md`, `docs/guide.md`, `THIRD_PARTY.md`, `tools/docs_images.py`, `docs/images/panel/` and `docs/images/web/` (generated)

**Needs the owner first:** the board plugged into this Mac, and a choice of when. These checks flash the board, use config mode over the device's own network (where the board also joins the home network, gotcha 43) and reach Open-Meteo and Forecast.Solar's free tier, which need no account. They don't use Solcast or SolaX Cloud: those need the owner's keys, which only the owner types in (the Owner acceptance below). They start syncs on demand, so M5's RTC-trim run, if one is still going, restarts its count (memory `m5-deferred-owner-checks`). The board has the home network saved and no web password: Step 0 sets a temporary one over the device's own network, kept in a file outside the repository (`$SCRATCH`, such as the session's scratchpad) for Steps 3 and 9, and Step 11 clears it again. Ask, and wait.

Before anything else, confirm the port is this board (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'` shows `14:C1:9F:54:BB:94`), and note what the checks may change, to compare in Step 11: `tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "sync status" -o captures/m6d-before.log`.

- [ ] **Step 0: The way back, before flashing.** Under the firmware the board runs now, save its configuration for the way back: `stable-m6b` refuses M6d's Solar and Energy presets, and once a preset switch on M6d saves, `presets.json.bak` holds them too, so only a backup taken now restores under it. Config mode (`btn boot long`; the password is on the screen), join the device's network (`networksetup -setairportnetwork en0 reflbo-bb94 <password>`), then:

```bash
PW=$(openssl rand -hex 8); echo "$PW" > "$SCRATCH/m6d-web-pw" # the temporary web password; never in the repo
curl -s -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/setup
curl -s -c jar -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/login
mkdir -p captures/stable && curl -s -b jar http://192.168.4.1/api/backup > captures/stable/before-m6d-backup.json
python3 -c 'import json; b=json.load(open("captures/stable/before-m6d-backup.json")); print(b["firmware"], [p["id"] for p in b["files"]["presets.json"]["presets"]])'
```

Expected: the running firmware's version and the owner's presets, without `solar` or `energy`. Keep the file (gitignored; Step 11 leaves it) until `stable-m6d` is tagged: spec §12.10 and `AGENTS.md` §6 name it (Step 12). Leave config mode (`btn boot long`).

- [ ] **Step 1: Flash and boot.**

```bash
tools/idf.sh -p /dev/cu.usbmodemXXXX flash 2>&1 | grep -E 'MAC:|Hash of data verified'
tools/idf.sh exec python tools/devlog.py --cmd reboot --until "reflbo ready" -t 30 -o captures/m6d-boot.log
grep -E 'presets.json|settings.json|solar|E \(' captures/m6d-boot.log
tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "solar status" --cmd "sync status"
```

Expected: `MAC: 14:c1:9f:54:bb:94`; no `E (` line and nothing about the files being invalid (the owner's M6c files parse as they are); the boot is cold (a version-9 snapshot is never read, gotcha 29). `preset list` ends with `solar` and `energy`, out of the cycle (the owner's `presets.json` gained them once, and the file now names them in `offered`); `solar status`: `forecast: off`, nothing fetched, `energy: off, battery auto`; `sync status` lists seven steps. (If the board sits in download mode after flashing, leave it as gotcha 22 says.)

- [ ] **Step 2: The layouts on the panel, with the sample day.**

```bash
tools/idf.sh exec python tools/devlog.py --cmd "preset set solar" --cmd "solar demo on" --cmd screenshot -o captures/m6d-demo-solar.log
tools/idf.sh exec python tools/devlog.py --cmd "preset set energy" --cmd screenshot -o captures/m6d-demo-energy.log
tools/idf.sh exec python tools/devlog.py --cmd "solar demo off" --cmd screenshot -o captures/m6d-none-energy.log
```

Decode each with `screenshot.extract_pbm()` (`AGENTS.md` §7). Expected: the Solar layout as `dash_solar_actual` draws it, with today's date and the time now (the readings' bars up to now); the Energy layout as `dash_energy`; after `demo off`, "No data from the inverter yet". The owner looks at the panel for each (Owner acceptance 1).

- [ ] **Step 3: Config mode and a backup.** Join the device's network (`btn boot long`; the password is on the screen; `networksetup -setairportnetwork en0 reflbo-bb94 <password>`), log in with Step 0's password and save the board's configuration under M6d:

```bash
PW=$(cat "$SCRATCH/m6d-web-pw")
curl -s -c jar -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/login
curl -s -b jar http://192.168.4.1/api/backup > captures/m6d-backup.json
curl -s -b jar http://192.168.4.1/api/settings | python3 -c 'import json,sys; s=json.load(sys.stdin); print(s["sync"]["steps"], s["solar"], s["energy"])'
```

Expected: every step on; `solar` off with one plane of 5 kWp, 35°, south, `keys` all false; `energy` off, battery `auto`, `keys` false. `tools/idf.sh exec python tools/devlog.py --cmd "wifi status"` shows the board on the home network beside its AP (gotcha 43).

- [ ] **Step 4: Open-Meteo through our model.** First a check with both sources still off, then one with Open-Meteo:

```bash
curl -s -w ' %{http_code}\n' -b jar -X POST -H 'Content-Type: application/json' http://192.168.4.1/api/solar/check
curl -s -b jar -X PATCH -H 'Content-Type: application/json' -d '{"solar":{"source":"open-meteo"}}' http://192.168.4.1/api/settings >/dev/null
curl -s -w ' %{http_code}\n' -b jar -X POST -H 'Content-Type: application/json' http://192.168.4.1/api/solar/check
sleep 15; curl -s -b jar http://192.168.4.1/api/status | python3 -c 'import json,sys; s=json.load(sys.stdin); print(s["solar"], s["energy"])'
tools/idf.sh exec python tools/devlog.py --cmd "solar status" --cmd "preset set solar" --cmd screenshot -o captures/m6d-om.log
```

Expected: `{"error":"nothing to check: the forecast and the house's energy are off"} 409`, then `{"started":true} 202`; `solar` with `fetched_at`, `day` today, `tried_at` and no `error`; `energy` untouched (no source: the step didn't run); `solar status` prints three days' totals; the screenshot the Solar layout with today's forecast for the default roof, the next two days with their weather. (`/api/status` and `solar status` may be compared with Forecast.Solar's in Step 5: our model is simpler, spec §20.)

- [ ] **Step 5: Forecast.Solar, and a second plane without a key.**

```bash
curl -s -b jar -X PATCH -H 'Content-Type: application/json' -d '{"solar":{"source":"forecast-solar"}}' http://192.168.4.1/api/settings >/dev/null
curl -s -b jar -X POST -H 'Content-Type: application/json' http://192.168.4.1/api/solar/check; sleep 15
tools/idf.sh exec python tools/devlog.py --cmd "solar status" -o captures/m6d-fs.log
curl -s -w ' %{http_code}\n' -b jar -X PATCH -H 'Content-Type: application/json' \
  -d '{"solar":{"planes":[{"kwp":5,"tilt":35,"azimuth":0},{"kwp":2,"tilt":30,"azimuth":-90}]}}' http://192.168.4.1/api/settings
```

Expected: `solar status` with Forecast.Solar's fetch, today's and tomorrow's totals and the third day `-` (its free tier has two days); the PATCH `{"error":"a second plane needs a Forecast.Solar key"} 400`.

- [ ] **Step 6: Keys are write-only.** A made-up key, then cleared:

```bash
curl -s -b jar -X PATCH -H 'Content-Type: application/json' -d '{"solar":{"fs_key":"Zz9Test0Key"}}' http://192.168.4.1/api/settings | grep -c Zz9Test0Key
curl -s -b jar http://192.168.4.1/api/settings | python3 -c 'import json,sys; print(json.load(sys.stdin)["solar"]["keys"])'
curl -s -b jar http://192.168.4.1/api/backup | grep -c Zz9Test0Key
curl -s -w ' %{http_code}\n' -b jar -X PATCH -H 'Content-Type: application/json' -d '{"solar":{"fs_key":"a/b"}}' http://192.168.4.1/api/settings
tools/idf.sh exec python tools/devlog.py --cmd "solar status" -t 5 -o captures/m6d-keys.log; grep -c Zz9Test0Key captures/m6d-keys.log
curl -s -b jar -X PATCH -H 'Content-Type: application/json' \
  -d '{"solar":{"fs_key":"Zz9Test1Key","fs_key":"Zz9Test2Key","keys":{"fs_key":true}}}' http://192.168.4.1/api/settings >/dev/null
curl -s -b jar http://192.168.4.1/api/backup | grep -cE 'Zz9Test|"keys"'
curl -s -b jar -X PATCH -H 'Content-Type: application/json' -d '{"solar":{"fs_key":null}}' http://192.168.4.1/api/settings >/dev/null
curl -s -b jar http://192.168.4.1/api/settings | python3 -c 'import json,sys; print(json.load(sys.stdin)["solar"]["keys"]["fs_key"])'
python3 -c 'import json; b=json.load(open("captures/m6d-backup.json")); b["files"]["settings.json"].setdefault("energy", {})["solax_token"]=20220101; json.dump(b, open("captures/m6d-backup-bad.json", "w"))'
curl -s -w ' %{http_code}\n' -b jar -H 'Content-Type: application/json' --data-binary @captures/m6d-backup-bad.json \
  http://192.168.4.1/api/restore
```

Expected: `0`; `{'fs_key': True, 'solcast_key': False, 'solcast_sites': 0}`; `0`; `{"error":"solar.fs_key: letters and digits only"} 400`; `0`; `0` (a key named twice and the echoed flags reach neither the file nor a backup); `False`; `{"error":"settings.json: energy.solax_token: a string"} 400`, nothing restored. (A made-up key would fail at Forecast.Solar; no check runs with it. The bundle's `files` hold each file as JSON, so the one-liner plants a number where a token would be.)

- [ ] **Step 7: A switched-off step makes no request.** Radar and Air quality off, then a sync:

```bash
RADAR_BEFORE=$(curl -s -b jar http://192.168.4.1/api/status | python3 -c 'import json,sys; print(json.load(sys.stdin)["radar"]["weather"].get("fetched_at"))')
curl -s -b jar -X PATCH -H 'Content-Type: application/json' -d '{"sync":{"steps":["weather","solar","energy"]}}' http://192.168.4.1/api/settings >/dev/null
tools/idf.sh exec python tools/devlog.py --cmd "sync now" -t 40 --until "app_sync: sync (done|failed)" -o captures/m6d-steps.log
tools/idf.sh exec python tools/devlog.py --cmd "sync status" --cmd "radar status" -o captures/m6d-steps-status.log
curl -s -b jar -X PATCH -H 'Content-Type: application/json' -d '{"sync":{"steps":["weather","air","radar","solar","energy"]}}' http://192.168.4.1/api/settings >/dev/null
```

Expected: `sync done` (the steps that are off fail nothing); `sync status` lists `air skipped`, `radar skipped`, `solar ok`, `energy skipped`; `radar status`'s fetch time is still `$RADAR_BEFORE`'s. The Sync page (Step 10) shows the same.

- [ ] **Step 8: Through deep sleep, and a cold boot.** Leave config mode (`btn boot long`), with the Solar preset active:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "preset set solar" --cmd "sleep test deep 2" -t 200 -o captures/m6d-deep.log
tools/idf.sh exec python tools/devlog.py --cmd "sleep stats" --cmd "solar status" --cmd screenshot -o captures/m6d-after-deep.log
tools/idf.sh exec python tools/devlog.py --cmd reboot --until "reflbo ready" -t 30 -o captures/m6d-cold.log
grep -E 'app_solar' captures/m6d-cold.log; tools/idf.sh exec python tools/devlog.py --cmd "solar status"
```

Expected: two warm wakes in `sleep stats`; the forecast and its screenshot as before the sleep (the snapshot, 5 680 bytes at version 10); after the reboot, `solar status` with the same forecast's fetch time and totals (`/fs/state/solar.bin`; the boot's `app_solar: forecast from … restored` line shows where info logs are on). The awake time of a wake that draws the Solar layout, against the dashboards' (about 60 ms), goes in the spec as built.

- [ ] **Step 9: The heap, the stack and a sync's length with every step.** In config mode again (`btn boot long`, log in as in Step 3), with Open-Meteo and every step on: `curl … /api/solar/check`, then `sync now` (`--until "app_sync: sync (done|failed)"`), then `heap`, and `tasks` once with the Solar preset showing and once with a Grid preset that holds `pv.chart` (the deepest render path: about 3.3 KB of stack before `gfx`, against 1.2 KB for the Solar layout). Expected: the lowest internal heap since boot stays above 60 KB (M5's 80 KB with config mode and a sync; Open-Meteo's GTI reply is 6 KB); the app task keeps more than 1 KB of its stack free (M6c: 3 120 B); the sync's `done in … ms` (its log line) with every step on, on the owner's link, goes in the spec as built. Solcast's 18 KB reply is the larger one; it is measured with the owner's keys (Owner acceptance 3), and `heap` read after it.

- [ ] **Step 10: The pages.** Headless Chrome on `http://192.168.4.1/#solar` and `#sync` (`AGENTS.md` §6: the session cookie from the jar through `chromium-cookies`): the Solar page shows Forecast.Solar with its optional key field (empty, "None is set"), the house's energy off, Check now and the credits; with the source set to Solcast its planes go and its key and site fields come; the Sync page's Steps card with five switches and "The time always runs". Screenshots to `captures/m6d-page-*.png` for the owner, and phone-sized ones of the Solar page into `docs/images/web/` for the guide.

- [ ] **Step 11: Back to the start.** Restore the owner's configuration and compare:

```bash
curl -s -w ' %{http_code}\n' -b jar -H 'Content-Type: application/json' --data-binary @captures/m6d-backup.json \
  http://192.168.4.1/api/restore
curl -s -b jar http://192.168.4.1/api/backup > captures/m6d-backup-after.json
python3 -c 'import json; a=json.load(open("captures/m6d-backup.json"))["files"]; b=json.load(open("captures/m6d-backup-after.json"))["files"]; print(a == b)'
```

Expected: `200`; `True`. The forecast fetched in the checks stays in `solar.bin` until the next Solar step; with the source off again, it ages out. Then clear the temporary web password: Menu ▸ Wi-Fi ▸ Reset web password (`btn key long` opens the menu; `btn key short` steps to the next item and `btn key long` enters it; the confirmation asks for KEY held; a screenshot after each press shows where the menu is). Last, `networksetup -removepreferredwirelessnetwork en0 reflbo-bb94` and `rm jar captures/m6d-backup*.json "$SCRATCH/m6d-web-pw"`; `captures/stable/before-m6d-backup.json` stays. `preset list` and `sync status` as in `captures/m6d-before.log`, apart from the syncs these checks ran.

- [ ] **Step 12: Write down what was built.**
  - Spec r38: §3.1 (`solar` and `energy` need `timekeeping`; `ui` needs both, as it needs `radar`), §5.1 (the fields as built: the short forms, the grid's label and arrow), §5.2 and §5.3 (the layouts and the chart and flow as built, the fit test's three fixes, the stale warning and the dashes), §5.4 (the built-ins and the marker), §5.7 (Sync ▸ Steps as a section), §6 (the snapshot: 5 680 bytes, version 10; RTC slow memory 5 832 of 8 192 bytes), §9.3 (the steps' results: skipped "off" or "no source", kept; which failures fail a sync; Info's Last sync names an Energy failure; the forecast's need follows the Weather step; the energy refresh's timing; the check, and a check that can't join; a new Solcast key or site starts its budget over; a kept step keeps the last call's error), §10.3 and §10.4 (the API as built: `tried_at`, `demo`, `keys`, `kept`, `next_at`, every step's detail in `sync.last.details`, `sync.last.ok`; a restored bundle's keys dropped, one whose keys can't be taken out refused, one with a second Forecast.Solar plane and no key restored with a note (the forecast uses the first plane until one is set); a reading more than a day ahead of the clock refused; Check now's 409 with nothing to check; `fetch` logs a host), §11.5 (Forecast.Solar's watts are the power at each point, integrated as a line; its `/estimate/watts/`; the measured Brno day), §11.6, §12.10 (`stable-m6b` refuses a `presets.json` with the Solar or Energy layout, as it refuses 24 cells: after flashing it back, restore `captures/stable/before-m6d-backup.json`, Step 0's; `stable-m6d` replaces it after the acceptance), §14.3 (`solar.bin`'s layout), §15 (`solar status` as built), §17 (the tests as built), §18 (M6d built), §20 (anything Steps 1–11 found; the review's four deferred minors: the G4 hybrid types, "No solar forecast yet" for an expired one, Solcast's first morning, no daily count of its calls), §21.
  - `tools/docs_images.py`: the Solar and Energy layouts (`dash_solar_actual`, `dash_energy_battery`) and a dashboard with the fields (`dash_weather_solar`); run it.
  - `docs/guide.md`: a "Solar and the house's energy" section (the sources and what each needs, the planes, the keys being write-only, Check now, the fields and the two layouts, Sync ▸ Steps), with the images; that today's totals to and from the grid need a reading within an hour of midnight, which sync mode `always` brings, or a sync time such as 00:05 (SolaX sends lifetime counters); Solcast's personal-use terms and Forecast.Solar's CC BY-SA credit; "Coming next" loses M6d.
  - `THIRD_PARTY.md`: Forecast.Solar (CC BY-SA 4.0) and Solcast's terms beside Open-Meteo; the five new Material Icons.
  - `README.md`: the roadmap's M6d row "Built".
  - `AGENTS.md`: the status (M6d built, its owner checks), the roadmap row, the components (`solar`, `energy` without "(planned)"), §6 (`solar status`, `solar demo`; `stable-m6b` refuses the Solar and Energy presets: restore `captures/stable/before-m6d-backup.json`), §7's console list, and any gotcha the checks found.

- [ ] **Step 13: Commit and push.**

```bash
git add docs AGENTS.md README.md THIRD_PARTY.md tools/docs_images.py
git commit -m "docs: record M6d as built (spec r38)"
git push origin main
```

## Owner acceptance

M6d is done (spec §18) once the owner has checked, with the expected results:

1. **The goldens** (`python3 tools/render.py`, `captures/render/dash_solar*.png`, `dash_energy*.png`, `dash_home_energy.png`, `dash_grid_solar.png`, `dash_weather_solar.png`, `dash_focus_solar.png`), and the panel in Step 2: as approved in the spike's renders, or with the changes asked for.
2. **A forecast from Open-Meteo and from Forecast.Solar renders after a sync** (Steps 4 and 5): the owner's own roof (kWp, tilt, azimuth on the Solar page), compared with what the owner expects of it.
3. **With the owner's keys, typed in on the Solar page by the owner** (never into this session): Solcast's forecast renders, and `heap` after its 18 KB reply keeps the lowest internal heap above 60 KB; a SolaX reading renders on the Energy layout, and the owner compares it with the SolaX app (solar output, grid, home, and the battery if one shows).
4. **A switched-off step makes no request** (Step 7), and the owner's own switches as wanted.
5. **A sync's energy with the Solar and Energy steps** measured on battery with the USB power meter (spec §9.4), written into `docs/power.md`.

Then, as D36 says, the M6d build becomes the stable firmware: tag `stable-m6d`, its build kept in `captures/stable/stable-m6d/` with a `flash.sh` like `stable-m6b`'s, `AGENTS.md` §6 and spec §12.10 pointing at it.

Still open from earlier milestones, offered when the owner is at the board: M6c's acceptance (memory `m6c-plan`), M6b's (memory `m6b-design`), M6's rainy-day ČHMÚ frame and its power measurements (memory `m6-acceptance-open`), M5's trim result, sync on battery and router-off checks (memory `m5-deferred-owner-checks`), M3's night-sleep current and night peek, M4's update from the page and first run. M7's plan is refreshed after M6d's acceptance (D33).
