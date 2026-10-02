# M6: The Radar Views, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** M6 (spec §18): the radar views. A web-Mercator map with built-in borders, coasts, towns and airports; the weather radar (ČHMÚ, RainViewer outside its coverage) as the Radar layout and the `rain.map` widget, with a frame each sync, every 5 min in sync mode `always`, and the last hour's loop; the flight radar (adsb.fi, sync mode `always` only) as the Flights preset, with the nearest aircraft's route from adsb.lol; rain in the next 2 hours; the Radar page. It carries the owner's M6 decisions (D22–D24, D27, D28).

**Architecture:**

- **Pure logic, host-tested:**
  - `png`: a reader for 8-bit palette and RGBA PNGs, row by row; inflating through the ROM's `tinfl` on the device and zlib on the host.
  - `map`: web-Mercator views (centre, zoom, projection, distance and bearing); the built-in map, packed by `tools/gen_map.py` into `assets/map/map.bin` and embedded in the app image; its drawing (lines with a halo, towns and airports with labels that keep clear of each other, home, range rings).
  - `radar`: ČHMÚ's composite and RainViewer's tiles decoded into frames of three rain levels; the frames' store, file and drawing into any view.
  - `adsb`: adsb.fi's reply filtered to the Flights map, nearest first; the poll interval; adsb.lol's route and its cache.
  - `datastore`, `weather`: Open-Meteo's 15-minute rain, stored with the forecast.
  - `storage`: `radar.*` in `settings.json`.
  - `gfx`, `locale`, `ui`: a filled triangle; the strings; the Radar and Flights layouts, `rain.map`, `wx.rain2h`, the built-in Rain radar and Flights presets offered once to an older `presets.json`, the cycle that visits Flights only in sync mode `always`.
  - `sync_plan`: when a radar-only refresh runs.
- **Device side:**
  - `fetch` (new): HTTPS GETs over kept-alive sessions, closing the connections a server won't keep; M5's `weather_http_get()` becomes a one-shot use of it.
  - `radar_fetch.c`: a sync's frames, ČHMÚ's by file name with the 404 fallback, RainViewer's index and tiles over one connection.
  - `sync`: the radar step after air quality; the radar-only refresh, which never joins Wi-Fi itself.
  - `adsb_task.c`: the flight radar's own polling task.
  - `main`: `app_radar.c` (the frames in PSRAM, the newest in `/fs/state/radar.bin`, the map and the file opened only for presets that draw them, the loop), `app_flights.c` (the polling while the Flights view shows), the console's `radar status|loop`, the status API's `radar` block, previews of the radar layouts.
  - `web/`: the Radar page; the Sync page's radar step; the preset editor's radar layouts.

**Tech stack:** ESP-IDF v5.5.5 with `esp_http_client` and mbedTLS's certificate bundle (now allocating in PSRAM), the ROM's `tinfl`, the bundled cJSON (allocating in PSRAM since M5), LittleFS; Unity on the host with the system zlib; Node's test runner for the page; plain Python 3 for `tools/gen_map.py`. New data, all public domain: Natural Earth 5.1.2 (borders at 1:10 m, coasts at 1:50 m, populated places) and OurAirports. New services: ČHMÚ open data (CC BY 4.0), RainViewer (free for personal use, with a link), adsb.fi (non-commercial, credited with a link), adsb.lol.

**Spec:** `docs/specs/2026-09-25-firmware-design.md` r28. Task 15 brings it to r29, as built.
- Relevant: §5.1, §5.2, §5.4, §5.6, §6, §9.3, §9.4, §10.3, §11.1–§11.4, §14.3, §15, §17, §18 (M6), §19.1, §19.2, §20; D11, D22–D24, D27, D28.
- Also `AGENTS.md` §3.4 (gotchas 11, 18, 22, 25, 28, 29, 30), §5.3, §6–§8.

**Research behind this plan (2026-10-01 and 2026-10-02):**
- The plan's code was built and tested task by task on the local branch `plan/m6`, one commit per task (`plan-m6 task N: …`); the plan carries that code. On the host the whole suite passed after every task: 60 ctest targets at the end (53 before), the page's 24 tests (17 before) and the 64 tool tests (11 new for `gen_map.py`) among them; the ASan/UBSan build passed at the end. The firmware built clean after every task, without a warning: 2.36 MB (1.56 MB before; 748 KB of it is the map).
- **Binary files** the plan can't carry inline — the PNG fixtures, `assets/map/map.bin` and the new goldens — come from `plan/m6`, which is pushed to `origin` with this plan: each task names them and copies them with `git checkout plan/m6 -- <paths>` (in a clone without the branch, `git fetch origin plan/m6:plan/m6` first). The goldens are rendered again by the task, and must match the branch's byte for byte.
- **Checked against the live services:**
  - ČHMÚ's composite: its georeference from the HDF5 (ODIM) file of the same frame (a 598 × 378 grid of 1555.7 m in web-Mercator metres, its top at 6 702 777.85 m), and its palette paired pixel by pixel with the HDF5's dBZ; the PNG holds the grid at rows 82–459.
  - RainViewer: its index (13 past frames, 10 min apart) and tiles (zoom 7 at most); every colour scheme returns Universal Blue since 2026, and only tiles without smoothing (`0_0`) keep exact colours.
  - adsb.fi: the fixture is its reply for 105 NM around Brno on 2026-10-01 at 22:52 UTC, cut to 13 of its 32 aircraft (about 650–770 bytes an aircraft).
  - adsb.lol: TVS7UZ's route (BRQ → AYT), an `unknown` reply, a `500` for BAW15, and a `403` without a User-Agent.
  - Open-Meteo's `minutely_15`: 96 values from the current quarter hour; the fixture is Bergen drizzling at 00:57 local on 2026-10-02.
- **The map's sources:** Natural Earth's files are pinned by tag; OurAirports' `airports.csv` changes daily, so `map.bin` is the one of 2026-10-01, with every source's SHA-256 in `assets/map/sources.txt`. Natural Earth names only 12 towns in Czechia; GeoNames would give more but needs credit (CC BY 4.0): raised with the owner at the handoff.
- **Not checked on the board:** every device behaviour is Task 15's: the fetches, the refresh, the loop, the polling, the heap.
- **A review of the whole branch** (2026-10-02), before this plan was written, found four important problems and eleven smaller ones. Their fixes are in the tasks that own the code, so every task carries its fixed version:
  - `fetch` never retried a connection the server had closed, and ČHMÚ closes every one: a connection the server won't keep is now closed after its reply, and a request without a reply on a kept connection is tried again on a new one (Task 11).
  - ČHMÚ's file names came from a clock the sync hadn't set yet, so the boot sync after a lost time asked for a file from 2000: the sync's own NTP time names them (Task 11).
  - The first render after every wake ran a CRC over the 748 KB map, and radar presets read the frame file whether they drew it or not: the CRC is the host's (`map_data_verify()`, Task 3), and `app_radar_prepare()` opens the map and reads the file only for presets that draw them (Task 11).
  - A Radar layout render took about half a second in soft doubles: per-column and per-row tables into the frame's grid (Task 5), and a single-precision projection for the map (Task 4).
  - Smaller ones: the rain strip read Open-Meteo's quarter hours 15 minutes late (Task 7); labels ran out with many aircraft (Task 4: 192, not 64); the frame file rewritten every 5 minutes in sync mode `always`, RainViewer keeping two hours, a sync on demand during a refresh that never started, a step that never ran blanking the radar's status, and a status read before it was set (Task 11); polling under the menu, and a `memcmp()` over a struct's padding (Task 12); AGENTS.md behind the components (each task now updates it).
  - Left as it is: a second `sync_start()` could wipe a report the app hasn't applied yet and leak its frames; `app_sync.c` never starts one before applying the last, which guards it. Raised at the handoff as a deferred minor.

**Rulings this plan makes (each costs little if wrong):**
- **Host inflate through the system zlib** (Task 1), not miniz's `tinfl.c` as spec §11.2 says: zlib ships with macOS and every Linux, so nothing is vendored; the device inflates through the ROM's `tinfl` either way.
- **Coasts from Natural Earth's 1:50 m set** (Task 3), beside the borders spec §11.1 names: without them a view near any sea ends in blank paper. They take about 270 KB of the 748 KB map; the app partition is 4 MB.
- **ČHMÚ's frames keep only the data area** (Task 5): 598 × 378 pixels, 56 KB, not the 680 × 460 image (78 KB, spec §11.2); the rest is the title and the scale.
- **Labels on white boxes** (Task 4), not a 1 px halo around each glyph (spec §11.1): over dithered rain a glyph halo left them hard to read in the renders.
- **RainViewer's tiles without smoothing or snow** (`2/0_0.png`, Task 5), not `2/1_1.png` (spec §11.2): smoothing blends colours that no level of the table names.
- **The newest frame is kept as `/fs/state/radar.bin`** (Task 11): its levels with a header and CRC-32, not the PNG of spec §11.2 and §14.3. RainViewer's frame is several tiles, and a deep-sleep wake reads it back without decoding; it takes 56 KB for ČHMÚ and up to 147 KB for RainViewer, where a PNG takes 3–40 KB.
- **A radar-only refresh isn't shown as a sync** (Task 11): neither the status bar's sync mark nor the Sync page's "running"; spec §9.3 keeps it outside the syncs' history and retries.
- **One HTTPS helper, `fetch`, with kept-alive sessions** (Task 11): the tiles and the flight radar's polls need them (spec §11.2, §11.3); M5's getter becomes a one-shot use of it.
- **mbedTLS allocates in PSRAM** (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`, Task 12): sync mode `always` now holds adsb.fi's TLS session open while refreshes and syncs open theirs, and M5's lowest free internal heap was 80 KB. Handshakes get slower; Task 15 times a sync and measures the heap.
- **adsb.lol's connection closes after each lookup** (Task 12); only adsb.fi's is kept: lookups are rare, and a TLS session costs 20–40 KB.
- **The Rain radar and Flights presets show the status bar's clock** (Task 9), as Indoor and Weather do: a map fills the screen.
- **The Radar page previews the saved settings** (Task 14), refreshed after each save; the preset editor's preview is live, but a live radar preview would need its settings in the request.
- **Before the first poll** after the Flights view comes up, its panel says "No aircraft data" (Task 10): spec §11.3 names no wording for that moment, which lasts about a second.
- **A RainViewer refusal shows "No radar frame yet"** (Tasks 9, 11), or the last frame kept, with the reason in `radar status` and on the Radar page; spec §20 says "No radar here". One message covers every reason a frame is missing — cost: the panel doesn't say why.
- **The map's CRC is the host's** (Task 3): `map_data_open()` checks the header and the sections' bounds, and `map_data_verify()`, the CRC-32, runs in the tests. On the device the app image's own SHA-256 covers the blob, and a CRC over 748 KB would add an estimated 50–75 ms to the first render after each wake.
- **The map projects in single precision** (Task 4): the S3's FPU has no doubles. Where float and double round differently a line moves by one pixel; two goldens show it.
- **A frame stays while it covers the view** (Task 11): ČHMÚ's for any view centred inside its data, RainViewer's while it holds the tiles the view needs at its zoom, so a small move of the centre keeps them, and a frame read from the file is dropped if it doesn't cover the view then.
- **The frame file is written at most every 30 min in sync mode `always`** (Task 11): a frame comes every 5 or 10 minutes there, and each write is 56–147 KB plus its `.bak`. Cost: a power-off can lose up to 30 minutes of frames; the next refresh brings the hour back at once.
- **A sync asked for during a radar refresh waits for it** (Task 11), 30 s at most, and shows as running from the moment it is asked.
- **ČHMÚ's frames need the time** (Task 11): this sync's NTP time, else a valid clock; with neither, the radar step fails with "no time", and the frame comes with the next sync that knows the time.
- **Only presets that draw them open the map and read the frame file** (`app_radar_prepare()`, Task 11). Cost: on the deep idle strategy (not the default, D3) the Rain radar preset reads its file at each routine wake, as a deep sleep loses PSRAM.
- **Review minors** (D25's rule, where M6 touches their code): only the over-long line in `dashboard_fixtures.h`'s fixture list (Task 10). The rest stay in their memories.

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, Python 3 host tools, plain HTML/CSS/JS in `web/` with no build step and no external resources (links to the data sources are plain `<a>` links).
- `[host]` components (`util`, `gfx`, `locale`, `datastore`, `ui`, `scheduler`, `astro`, `png`, `map`) and the pure files named per task (`radar_frame.c`, `radar_chmu.c`, `radar_rainviewer.c`, `radar_store.c`, `adsb_parse.c`, `adsb_route.c`, `weather_*.c` but `weather_http.c`, `sync_plan.c`, `settings.c`) include no ESP-IDF headers. cJSON counts as plain C.
- ESP-IDF style: 4-space indent, `snake_case`, a component or module prefix on public APIs, public headers in `include/`. C lines stay within 120 characters.
- The app task owns the display, `st7305`, storage, the datastore, the radar's frames, the flight radar's reports, sleep, the menu and the syncs' state (spec §3.2, `AGENTS.md` §5.3). The sync and adsb tasks only fetch; they reach the app through `app_post()`, which doesn't wait. A report's PSRAM is handed over whole: the app takes it, or frees it.
- **The radars never turn Wi-Fi on by themselves** (D23): a frame comes with a sync, or a refresh while sync mode `always` holds Wi-Fi; the flight radar polls only then, while the Flights view is on screen, and not in quiet hours or a night (D22).
- **Services' terms** (spec §11.2, §11.3): adsb.fi at most 1 request a second (the polls are 5–15 s apart); adsb.lol only for the nearest aircraft when it changes, backing off an hour after a 403 or 429; RainViewer at most 100 requests a minute; every request with a `reflbo/<version>` User-Agent. Credits: "Data: ČHMÚ, opendata.chmi.cz, CC BY 4.0", RainViewer with a link to rainviewer.com, adsb.fi with a link, adsb.lol beside it.
- **Secrets.** Never commit or log a Wi-Fi password, the AP password or the web password. The board has no web password now (reset with the owner's agreement at the end of the M5 checks): a check that sets one over the device's own network clears it again with Menu ▸ Wi-Fi ▸ Reset web password, so the owner's first visit chooses theirs.
- **This Mac's Wi-Fi** may join the board's AP for checks (owner, 2026-09-30): `networksetup -setairportnetwork en0 reflbo-XXXX <password from the screen>`, removed afterwards with `networksetup -removepreferredwirelessnetwork en0 reflbo-XXXX`.
- Never erase flash or NVS without asking. A factory reset erases `storage` and the NVS namespaces `wifi`, `secrets` and `ctr` (spec §14.4): only with the owner's agreement.
  - Board commands go through `tools/idf.sh` with an explicit `-p`.
  - The port must be confirmed as this board: Espressif `303A:1001` with the USB serial number `14:C1:9F:54:BB:94` (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'`), and `flash` prints `MAC: 14:c1:9f:54:bb:94`.
- List every component source in `SRCS`. Run `tools/idf.sh reconfigure` after adding a component directory. Change configuration through `sdkconfig.defaults`, then delete `sdkconfig` and build. Dependencies come with ESP-IDF; nothing new from the registry.
- Large buffers go in PSRAM (`MALLOC_CAP_SPIRAM`, or `EXT_RAM_BSS_ATTR` for static ones); Wi-Fi needs the internal RAM, and so does every task stack.
- Small, focused Conventional Commits that each build. Push to `origin` freely; never force-push. **No attribution of any kind: no `Co-Authored-By` or other trailer in any commit** (the owner's rule, memory `no-commit-trailers`).
- Build only what the spec covers (r28: D22–D24, D27, D28). Not in M6: MQTT (M7), radar or flight history over days, a detailed map pack and an aircraft registration database (M9 candidates, D28), a static IP, web UI translations (spec §5.8).
- Czech text follows Czech typography (decimal comma, en dash), and every string's glyphs must exist in the fonts (`test_lang_glyphs`).
- Every network call has a timeout (spec §16); a sync's radio stays on for at most 45 s (spec §9.3), its radar step 10 s of it; a radar-only refresh takes at most 30 s.

## Review Focus

1. **Services that misbehave.** ČHMÚ without the newest file yet (404), a PNG cut short or with a bad CRC, RainViewer's index without frames or a tile that fails, adsb.fi's 429, a reply over 128 KB or not JSON, adsb.lol's 403, 429 or 500. Expected: each fails alone with a reason that `radar status` and the Radar page show; the views keep the last frame or say "No aircraft data (HH:MM)"; nothing crashes or leaks. Pinned by:
   - `test_png`'s refusals (Task 1), `test_radar`'s damaged file (Task 11), `test_adsb`'s refusals and route replies (Task 6);
   - `radar_fetch.c`'s per-frame handling and `adsb_task.c`'s back-off (Tasks 11, 12);
   - the board's failing fetches (Task 15).
2. **Views that move.** The weather radar's centre moved into or out of ČHMÚ's coverage, a RainViewer view changed while its tiles came, a new zoom, sync mode `always` ending with twelve frames kept. Expected: frames that no longer cover the view go; the loop never mixes sources; "No radar frame yet" until the next frame; one frame kept outside `always`. Pinned by:
   - `app_radar.c`'s `tidy()` and `app_radar_apply()` (Task 11), `test_radar`'s store (`radar_store_keep`) and `radar_frame_covers()` (Task 11);
   - the board's centre change (Task 15).
3. **Hours in sync mode `always`.** Frames every 5 min, polls every 5–15 s, routes, TLS sessions: PSRAM handed between tasks, and the internal heap Wi-Fi needs. Expected: no growth over hours; the free internal heap stays above 40 KB with a sync, a refresh and the polling together. Pinned by:
   - the ownership rules in `app_radar.c`, `app_flights.c` and `adsb_task.c` (Tasks 11, 12); the ASan/UBSan suite;
   - the board's hour in `always` with `heap` (Task 15).
4. **The map at the world's edges.** A centre near a pole or across the antimeridian, a view of open sea, a dense city. Expected: latitudes clamp at ±85°; lines clip to the view; a label without room is left out, never drawn over another. Pinned by:
   - `test_map_view` (Task 2) and `test_map_draw` (Task 4);
   - the goldens at 100 km and around Berlin (Tasks 9, 10).
5. **Text that must fit.** A long airport or place name, an aircraft without a callsign, type or speed, Czech words, the rain's line in a narrow cell, a 12-hour clock. Expected: cut at a character, ellipsized, never outside its slot or panel. Pinned by:
   - `test_adsb`'s cut name (Task 6), `test_ui_fields`' rain line (Task 7), `test_ui_flights` (Task 10);
   - the Czech goldens (Tasks 7, 9, 10).

---



### Task 1: A PNG reader (`png`)

**Files:**
- Create: `components/png/CMakeLists.txt`, `components/png/include/png.h`, `components/png/include/png_inflate.h`, `components/png/png.c`, `components/png/png_inflate_rom.c`
- Create: `test/host/png_inflate_zlib.c`, `test/host/test_png.c`, and the fixtures `test/host/fixtures/png/chmi_rain.png`, `chmi_dry.png`, `rainviewer_tile.png`
- Modify: `test/host/CMakeLists.txt`
- Modify: `AGENTS.md` (the `png/` line in §5.2)

**Interfaces:**
- Consumes: `util_crc32()` (`util_crc32.h`).
- Produces (Tasks 5 and 11 rely on these names):
  - `png_decode(const uint8_t *data, size_t len, const png_mem_t *mem, png_info_t *info, png_row_fn row, void *ctx)` → `png_err_t` (`PNG_OK`, `PNG_ERR_FORMAT`, `PNG_ERR_UNSUPPORTED`, `PNG_ERR_TOO_BIG`, `PNG_ERR_NO_MEMORY`, `PNG_ERR_INFLATE`); `png_err_name()`.
  - `png_info_t` (`width`, `height`, `type`, `palette_size`, `palette[256][4]`), `png_type_t` (`PNG_PALETTE`, `PNG_RGBA`), `png_row_fn(void *ctx, int y, const uint8_t *row, const png_info_t *info)`, `png_mem_t` (`alloc`, `release`; NULL for malloc).
  - `PNG_MAX_SIDE` 1024, `PNG_MAX_BYTES` 256 KB.
  - `png_inflate(in, in_len, out, out_len)`: the ROM's `tinfl` on the device (`png_inflate_rom.c`), zlib on the host (`test/host/png_inflate_zlib.c`).

The fixtures are real files: ČHMÚ's composite of 2026-09-24 11:20 UTC in rain (41 KB) and a dry one (3 KB), and RainViewer's tile (4, 2) of zoom 3, fetched on 2026-10-01. Copy them from the branch:

```bash
git checkout plan/m6 -- \
  test/host/fixtures/png/chmi_dry.png \
  test/host/fixtures/png/chmi_rain.png \
  test/host/fixtures/png/rainviewer_tile.png
```


- [ ] **Step 1: Write the failing test.** The real frames decode to their size, palette and pixels per palette index (counted with Pillow over the same files); RainViewer's tile to its opaque pixels and alpha sum; a crafted 4 × 5 image, one filter type a row, to its pixels; broken and unsupported files are refused one by one; the inflate buffer comes from the hooks and goes back.

`test/host/test_png.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "png.h"
#include "unity.h"
#include "util_crc32.h"

/* The ČHMÚ frames and the RainViewer tile were fetched on 2026-10-01; their pixel counts come from
 * Pillow, so they check this reader against another one. */

void setUp(void) {}
void tearDown(void) {}

static uint8_t *load(const char *name, size_t *len)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*len);
    TEST_ASSERT_EQUAL_size_t(*len, fread(data, 1, *len, f));
    fclose(f);
    return data;
}

typedef struct {
    int rows;
    long count[256]; /* palette images: pixels per index */
    long opaque, alpha_sum;
    int first_x, first_y;
    uint8_t first[4];
    uint8_t probe[4]; /* the pixel at (probe_x, probe_y) */
    int probe_x, probe_y;
} stats_t;

static void on_row(void *ctx, int y, const uint8_t *row, const png_info_t *info)
{
    stats_t *s = ctx;
    TEST_ASSERT_EQUAL_INT(s->rows, y);
    s->rows++;
    for (int x = 0; x < info->width; x++) {
        if (info->type == PNG_PALETTE) {
            s->count[row[x]]++;
            if (x == s->probe_x && y == s->probe_y) {
                s->probe[0] = row[x];
            }
            continue;
        }
        const uint8_t *px = row + 4 * x;
        if (px[3] != 0) {
            if (s->opaque == 0) {
                s->first_x = x;
                s->first_y = y;
                memcpy(s->first, px, 4);
            }
            s->opaque++;
            s->alpha_sum += px[3];
        }
        if (x == s->probe_x && y == s->probe_y) {
            memcpy(s->probe, px, 4);
        }
    }
}

static void test_a_chmu_frame_decodes_with_its_palette(void)
{
    size_t len;
    uint8_t *data = load("chmi_rain.png", &len);
    png_info_t info;
    static stats_t s;
    memset(&s, 0, sizeof(s));
    s.probe_x = 300;
    s.probe_y = 300;
    TEST_ASSERT_EQUAL_INT(PNG_OK, png_decode(data, len, NULL, &info, on_row, &s));
    TEST_ASSERT_EQUAL_UINT16(680, info.width);
    TEST_ASSERT_EQUAL_UINT16(460, info.height);
    TEST_ASSERT_EQUAL_UINT8(PNG_PALETTE, info.type);
    TEST_ASSERT_EQUAL_UINT16(256, info.palette_size);
    static const uint8_t k_green[4] = { 0, 160, 0, 255 }; /* 20 dBZ on ČHMÚ's scale */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_green, info.palette[191], 4);
    TEST_ASSERT_EQUAL_UINT8(0, info.palette[0][3]); /* tRNS: no echo is transparent */
    TEST_ASSERT_EQUAL_INT(460, s.rows);
    TEST_ASSERT_EQUAL_INT32(195187, s.count[0]);
    TEST_ASSERT_EQUAL_INT32(15079, s.count[191]);
    TEST_ASSERT_EQUAL_INT32(2, s.count[182]);
    TEST_ASSERT_EQUAL_INT32(4911, s.count[145]);
    TEST_ASSERT_EQUAL_UINT8(194, s.probe[0]);
    free(data);

    data = load("chmi_dry.png", &len); /* 3 KB: a dry night */
    memset(&s, 0, sizeof(s));
    TEST_ASSERT_EQUAL_INT(PNG_OK, png_decode(data, len, NULL, &info, on_row, &s));
    TEST_ASSERT_EQUAL_INT32(304736, s.count[0]);
    TEST_ASSERT_EQUAL_INT32(5833, s.count[145]);
    free(data);
}

static void test_a_rainviewer_tile_decodes_as_rgba(void)
{
    size_t len;
    uint8_t *data = load("rainviewer_tile.png", &len);
    png_info_t info;
    static stats_t s;
    memset(&s, 0, sizeof(s));
    s.probe_x = 128;
    s.probe_y = 128;
    TEST_ASSERT_EQUAL_INT(PNG_OK, png_decode(data, len, NULL, &info, on_row, &s));
    TEST_ASSERT_EQUAL_UINT8(PNG_RGBA, info.type);
    TEST_ASSERT_EQUAL_UINT16(256, info.width);
    TEST_ASSERT_EQUAL_INT(256, s.rows);
    TEST_ASSERT_EQUAL_INT32(9297, s.opaque);
    TEST_ASSERT_EQUAL_INT32(1686742, s.alpha_sum);
    TEST_ASSERT_EQUAL_INT(64, s.first_x);
    TEST_ASSERT_EQUAL_INT(0, s.first_y);
    static const uint8_t k_first[4] = { 130, 123, 105, 73 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_first, s.first, 4);
    static const uint8_t k_clear[4] = { 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_clear, s.probe, 4);
    free(data);
}

/* ---- crafted files ---- */

static size_t put_chunk(uint8_t *out, const char *type, const uint8_t *body, uint32_t n)
{
    out[0] = (uint8_t)(n >> 24);
    out[1] = (uint8_t)(n >> 16);
    out[2] = (uint8_t)(n >> 8);
    out[3] = (uint8_t)n;
    memcpy(out + 4, type, 4);
    if (n > 0) {
        memcpy(out + 8, body, n);
    }
    uint32_t crc = util_crc32(util_crc32(0, type, 4), body, n);
    out[8 + n] = (uint8_t)(crc >> 24);
    out[9 + n] = (uint8_t)(crc >> 16);
    out[10 + n] = (uint8_t)(crc >> 8);
    out[11 + n] = (uint8_t)crc;
    return 12 + n;
}

/* A whole file around raw scanlines (each with its filter byte), compressed with zlib. */
static size_t make_png(uint8_t *out, uint32_t w, uint32_t h, uint8_t depth, uint8_t type, uint8_t interlace,
                       const uint8_t *raw, size_t raw_len)
{
    static const uint8_t k_sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    memcpy(out, k_sig, 8);
    size_t n = 8;
    uint8_t ihdr[13] = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                         (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
                         depth, type, 0, 0, interlace };
    n += put_chunk(out + n, "IHDR", ihdr, 13);
    if (type == PNG_PALETTE) {
        uint8_t plte[12] = { 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255 };
        n += put_chunk(out + n, "PLTE", plte, 12);
    }
    static uint8_t z[4096];
    uLongf zn = sizeof(z);
    TEST_ASSERT_EQUAL_INT(Z_OK, compress(z, &zn, raw, (uLong)raw_len));
    n += put_chunk(out + n, "IDAT", z, (uint32_t)zn);
    n += put_chunk(out + n, "IEND", NULL, 0);
    return n;
}

typedef struct {
    uint8_t px[5][4]; /* palette indices, 4 x 5 */
    int rows;
} small_t;

static void on_small_row(void *ctx, int y, const uint8_t *row, const png_info_t *info)
{
    small_t *s = ctx;
    TEST_ASSERT_EQUAL_UINT16(4, info->width);
    memcpy(s->px[y], row, 4);
    s->rows++;
}

static void test_every_filter_type_is_undone(void)
{
    /* Rows filtered with None, Sub, Up, Average and Paeth; each decodes to the row in `want`. */
    static const uint8_t k_want[5][4] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 9, 9, 9 },
                                          { 2, 4, 6, 8 }, { 3, 1, 4, 1 } };
    uint8_t raw[5 * 5];
    for (int y = 0; y < 5; y++) {
        const uint8_t *cur = k_want[y];
        const uint8_t *up = y > 0 ? k_want[y - 1] : (const uint8_t[4]){ 0 };
        uint8_t *r = raw + y * 5;
        r[0] = (uint8_t)y; /* filter type = row number */
        for (int x = 0; x < 4; x++) {
            int a = x > 0 ? cur[x - 1] : 0, b = up[x], c = x > 0 ? up[x - 1] : 0;
            int pred = 0;
            switch (y) {
            case 1: pred = a; break;
            case 2: pred = b; break;
            case 3: pred = (a + b) / 2; break;
            case 4: {
                int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                pred = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                break;
            }
            default: break;
            }
            r[1 + x] = (uint8_t)(cur[x] - pred);
        }
    }
    static uint8_t file[8192];
    size_t n = make_png(file, 4, 5, 8, PNG_PALETTE, 0, raw, sizeof(raw));
    png_info_t info;
    small_t s;
    memset(&s, 0, sizeof(s));
    TEST_ASSERT_EQUAL_INT(PNG_OK, png_decode(file, n, NULL, &info, on_small_row, &s));
    TEST_ASSERT_EQUAL_INT(5, s.rows);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_want, s.px, sizeof(k_want));
    TEST_ASSERT_EQUAL_UINT8(255, info.palette[2][3]); /* no tRNS: every entry opaque */
}

static void ignore_row(void *ctx, int y, const uint8_t *row, const png_info_t *info)
{
    (void)y;
    (void)row;
    (void)info;
    (*(int *)ctx)++;
}

static void test_broken_and_unsupported_files_are_refused(void)
{
    static uint8_t raw[5 * 5]; /* five None rows of zeros */
    static uint8_t file[8192];
    png_info_t info;
    int rows = 0;
    size_t n = make_png(file, 4, 5, 8, PNG_PALETTE, 0, raw, sizeof(raw));
    TEST_ASSERT_EQUAL_INT(PNG_OK, png_decode(file, n, NULL, &info, ignore_row, &rows));

    TEST_ASSERT_EQUAL_INT(PNG_ERR_FORMAT, png_decode(file, n - 13, NULL, &info, ignore_row, &rows)); /* cut short */
    file[20] ^= 1; /* a bit of IHDR's height: its CRC no longer matches */
    TEST_ASSERT_EQUAL_INT(PNG_ERR_FORMAT, png_decode(file, n, NULL, &info, ignore_row, &rows));
    file[20] ^= 1;
    file[1] = 'Q';
    TEST_ASSERT_EQUAL_INT(PNG_ERR_FORMAT, png_decode(file, n, NULL, &info, ignore_row, &rows)); /* not a PNG */
    file[1] = 'P';

    n = make_png(file, 4, 5, 8, PNG_PALETTE, 1, raw, sizeof(raw)); /* Adam7 */
    TEST_ASSERT_EQUAL_INT(PNG_ERR_UNSUPPORTED, png_decode(file, n, NULL, &info, ignore_row, &rows));
    n = make_png(file, 4, 5, 16, PNG_RGBA, 0, raw, sizeof(raw));
    TEST_ASSERT_EQUAL_INT(PNG_ERR_UNSUPPORTED, png_decode(file, n, NULL, &info, ignore_row, &rows));
    n = make_png(file, 4, 5, 8, 2, 0, raw, sizeof(raw)); /* RGB without alpha */
    TEST_ASSERT_EQUAL_INT(PNG_ERR_UNSUPPORTED, png_decode(file, n, NULL, &info, ignore_row, &rows));
    n = make_png(file, PNG_MAX_SIDE + 1, 5, 8, PNG_PALETTE, 0, raw, sizeof(raw));
    TEST_ASSERT_EQUAL_INT(PNG_ERR_TOO_BIG, png_decode(file, n, NULL, &info, ignore_row, &rows));
    TEST_ASSERT_EQUAL_INT(PNG_ERR_TOO_BIG, png_decode(file, PNG_MAX_BYTES + 1, NULL, &info, ignore_row, &rows));

    n = make_png(file, 4, 6, 8, PNG_PALETTE, 0, raw, sizeof(raw)); /* six rows declared, five sent */
    TEST_ASSERT_EQUAL_INT(PNG_ERR_INFLATE, png_decode(file, n, NULL, &info, ignore_row, &rows));
    raw[5] = 7; /* the second row's filter byte: no such filter */
    n = make_png(file, 4, 5, 8, PNG_PALETTE, 0, raw, sizeof(raw));
    TEST_ASSERT_EQUAL_INT(PNG_ERR_FORMAT, png_decode(file, n, NULL, &info, ignore_row, &rows));
    TEST_ASSERT_EQUAL_STRING("unsupported", png_err_name(PNG_ERR_UNSUPPORTED));
}

static int s_allocs, s_frees, s_fail_after;

static void *counting_alloc(size_t size)
{
    if (s_fail_after >= 0 && s_allocs >= s_fail_after) {
        return NULL;
    }
    s_allocs++;
    return malloc(size);
}

static void counting_free(void *p)
{
    if (p != NULL) {
        s_frees++;
    }
    free(p);
}

static void test_memory_comes_from_the_hooks_and_goes_back(void)
{
    size_t len;
    uint8_t *data = load("chmi_dry.png", &len);
    const png_mem_t mem = { counting_alloc, counting_free };
    png_info_t info;
    int rows = 0;
    s_allocs = s_frees = 0;
    s_fail_after = -1;
    TEST_ASSERT_EQUAL_INT(PNG_OK, png_decode(data, len, &mem, &info, ignore_row, &rows));
    TEST_ASSERT_TRUE(s_allocs > 0);
    TEST_ASSERT_EQUAL_INT(s_allocs, s_frees);
    for (int fail = 0; fail < 2; fail++) {
        s_allocs = s_frees = 0;
        s_fail_after = fail;
        TEST_ASSERT_EQUAL_INT(PNG_ERR_NO_MEMORY, png_decode(data, len, &mem, &info, ignore_row, &rows));
        TEST_ASSERT_EQUAL_INT(s_allocs, s_frees);
    }
    free(data);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_chmu_frame_decodes_with_its_palette);
    RUN_TEST(test_a_rainviewer_tile_decodes_as_rgba);
    RUN_TEST(test_every_filter_type_is_undone);
    RUN_TEST(test_broken_and_unsupported_files_are_refused);
    RUN_TEST(test_memory_comes_from_the_hooks_and_goes_back);
    return UNITY_END();
}
```


`test/host/png_inflate_zlib.c`:

```c
#define ZLIB_CONST
#include <zlib.h>

#include "png_inflate.h"

/* The host tests inflate through zlib; the device has the ROM's tinfl (components/png/png_inflate_rom.c). */
bool png_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    z_stream s = { 0 };
    if (inflateInit(&s) != Z_OK) {
        return false;
    }
    s.next_in = in;
    s.avail_in = (uInt)in_len;
    s.next_out = out;
    s.avail_out = (uInt)out_len;
    int r = inflate(&s, Z_FINISH);
    bool ok = r == Z_STREAM_END && s.total_out == out_len;
    inflateEnd(&s);
    return ok;
}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -56,6 +56,13 @@ target_include_directories(astro PUBLIC ${REPO_ROOT}/components/astro/include)
 target_compile_options(astro PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(astro PUBLIC m)
 
+# png: the reader is pure C; on the host it inflates through zlib, on the device through the ROM's tinfl.
+find_package(ZLIB REQUIRED)
+add_library(png STATIC ${REPO_ROOT}/components/png/png.c png_inflate_zlib.c)
+target_include_directories(png PUBLIC ${REPO_ROOT}/components/png/include)
+target_compile_options(png PRIVATE ${REFLBO_WARNINGS})
+target_link_libraries(png PUBLIC util PRIVATE ZLIB::ZLIB)
+
 # sync: the planner (modes, quiet hours, retries) and the SNTP packets build on the host; the task does not.
 add_library(sync_logic STATIC ${REPO_ROOT}/components/sync/sync_plan.c ${REPO_ROOT}/components/sync/sync_ntp.c)
 target_include_directories(sync_logic PUBLIC ${REPO_ROOT}/components/sync/include)
@@ -179,6 +186,8 @@ reflbo_host_test(test_util_sha256 util)
 reflbo_host_test(test_scheduler scheduler)
 reflbo_host_test(test_astro astro)
 reflbo_host_test(test_sync_plan sync_logic)
+reflbo_host_test(test_png png ZLIB::ZLIB)
+target_compile_definitions(test_png PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/png")
 reflbo_host_test(test_gesture board_logic)
 reflbo_host_test(test_shtc3_codec sensors_logic)
 reflbo_host_test(test_battery_model sensors_logic)
```


- [ ] **Step 2: Run it to see it fail.**

Run: `cmake -S test/host -B build-host -G Ninja`
Expected: `CMake Error at CMakeLists.txt:… (add_library): Cannot find source file:` naming `components/png/png.c`.

- [ ] **Step 3: The reader.** Two passes: the chunks are checked (signature, CRCs, `IHDR` first, `PLTE` and `tRNS` before `IDAT`, `IEND` last) and the `IDAT`s' size summed; then they are concatenated, inflated into one buffer from `mem`, and unfiltered row by row.

`components/png/include/png.h`:

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * A small PNG reader for the radar images (spec §11.2): 8-bit palette and 8-bit RGBA, not
 * interlaced, handed over row by row. Pure C, host-buildable: it inflates through png_inflate()
 * (png_inflate.h), which the device takes from the ROM's tinfl and the host tests from zlib.
 */

#define PNG_MAX_SIDE 1024          /* pixels, either way */
#define PNG_MAX_BYTES (256 * 1024) /* the whole file */

typedef enum {
    PNG_OK,
    PNG_ERR_FORMAT,      /* not a PNG, a chunk cut short or out of place, a bad CRC or filter */
    PNG_ERR_UNSUPPORTED, /* interlaced, or not 8-bit palette or 8-bit RGBA */
    PNG_ERR_TOO_BIG,     /* over PNG_MAX_SIDE a side, or PNG_MAX_BYTES in all */
    PNG_ERR_NO_MEMORY,
    PNG_ERR_INFLATE,     /* the image data don't inflate to the image's size */
} png_err_t;

typedef enum {
    PNG_PALETTE = 3, /* the PNG colour types */
    PNG_RGBA = 6,
} png_type_t;

typedef struct {
    uint16_t width, height;
    uint8_t type;            /* png_type_t */
    uint16_t palette_size;   /* PNG_PALETTE: entries */
    uint8_t palette[256][4]; /* PNG_PALETTE: RGBA, the alpha from tRNS, 255 without */
} png_info_t;

/* One decoded row, top first: `width` palette indices, or 4 x `width` bytes of RGBA. */
typedef void (*png_row_fn)(void *ctx, int y, const uint8_t *row, const png_info_t *info);

/* Where the image data are inflated (height x (1 + width x 4) bytes at most): NULL for malloc. */
typedef struct {
    void *(*alloc)(size_t size);
    void (*release)(void *p);
} png_mem_t;

png_err_t png_decode(const uint8_t *data, size_t len, const png_mem_t *mem, png_info_t *info, png_row_fn row,
                     void *ctx);
const char *png_err_name(png_err_t err);
```


`components/png/include/png_inflate.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Inflates the zlib stream in[0, in_len) into exactly out_len bytes; false if it fails, comes out
 * short, or has more. png_inflate_rom.c (the ROM's tinfl) on the device, zlib on the host. */
bool png_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len);
```


`components/png/png.c`:

```c
#include "png.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "png_inflate.h"
#include "util_crc32.h"

static const uint8_t k_signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

typedef struct {
    const uint8_t *type; /* 4 bytes */
    const uint8_t *data;
    uint32_t len;
} chunk_t;

/* The chunk at *pos with its CRC checked, and *pos past it; false if it is cut short or damaged. */
static bool next_chunk(const uint8_t *data, size_t len, size_t *pos, chunk_t *c)
{
    if (len - *pos < 12) {
        return false;
    }
    uint32_t n = be32(data + *pos);
    if (n > len - *pos - 12) {
        return false;
    }
    c->type = data + *pos + 4;
    c->data = data + *pos + 8;
    c->len = n;
    if (util_crc32(util_crc32(0, c->type, 4), c->data, n) != be32(c->data + n)) {
        return false;
    }
    *pos += 12 + (size_t)n;
    return true;
}

static bool is(const chunk_t *c, const char *type)
{
    return memcmp(c->type, type, 4) == 0;
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* Undoes a row's filter in place; `prev` is the row above, already undone, or NULL for the first. */
static bool unfilter(uint8_t *row, const uint8_t *prev, size_t n, size_t bpp, uint8_t filter)
{
    for (size_t i = 0; i < n; i++) {
        int a = i >= bpp ? row[i - bpp] : 0;
        int b = prev != NULL ? prev[i] : 0;
        int c = prev != NULL && i >= bpp ? prev[i - bpp] : 0;
        switch (filter) {
        case 0: /* None */
            return true;
        case 1: /* Sub */
            row[i] = (uint8_t)(row[i] + a);
            break;
        case 2: /* Up */
            row[i] = (uint8_t)(row[i] + b);
            break;
        case 3: /* Average */
            row[i] = (uint8_t)(row[i] + (a + b) / 2);
            break;
        case 4: /* Paeth */
            row[i] = (uint8_t)(row[i] + paeth(a, b, c));
            break;
        default:
            return false;
        }
    }
    return filter <= 4;
}

/* The first pass: every chunk in order and undamaged, the header supported, the image data counted. */
static png_err_t check(const uint8_t *data, size_t len, png_info_t *info, size_t *idat_len)
{
    size_t pos = 8;
    bool header = false;
    *idat_len = 0;
    for (;;) {
        chunk_t c;
        if (!next_chunk(data, len, &pos, &c)) {
            return PNG_ERR_FORMAT;
        }
        if (!header) {
            if (!is(&c, "IHDR") || c.len != 13) {
                return PNG_ERR_FORMAT;
            }
            uint32_t w = be32(c.data), h = be32(c.data + 4);
            if (w == 0 || h == 0 || c.data[10] != 0 || c.data[11] != 0) { /* compression and filter method 0 */
                return PNG_ERR_FORMAT;
            }
            if (c.data[8] != 8 || (c.data[9] != PNG_PALETTE && c.data[9] != PNG_RGBA) || c.data[12] != 0) {
                return PNG_ERR_UNSUPPORTED;
            }
            if (w > PNG_MAX_SIDE || h > PNG_MAX_SIDE) {
                return PNG_ERR_TOO_BIG;
            }
            info->width = (uint16_t)w;
            info->height = (uint16_t)h;
            info->type = c.data[9];
            header = true;
        } else if (is(&c, "PLTE")) {
            if (c.len % 3 != 0 || c.len > 3 * 256 || *idat_len > 0) {
                return PNG_ERR_FORMAT;
            }
            info->palette_size = (uint16_t)(c.len / 3);
            for (int i = 0; i < info->palette_size; i++) {
                memcpy(info->palette[i], c.data + 3 * i, 3);
                info->palette[i][3] = 255;
            }
        } else if (is(&c, "tRNS")) {
            if (info->type == PNG_PALETTE) {
                if (c.len > info->palette_size) {
                    return PNG_ERR_FORMAT;
                }
                for (uint32_t i = 0; i < c.len; i++) {
                    info->palette[i][3] = c.data[i];
                }
            }
        } else if (is(&c, "IDAT")) {
            *idat_len += c.len;
        } else if (is(&c, "IEND")) {
            break;
        } else if (!(c.type[0] & 0x20)) {
            return PNG_ERR_UNSUPPORTED; /* a critical chunk this reader doesn't know */
        }
    }
    if (*idat_len == 0 || (info->type == PNG_PALETTE && info->palette_size == 0)) {
        return PNG_ERR_FORMAT;
    }
    return PNG_OK;
}

png_err_t png_decode(const uint8_t *data, size_t len, const png_mem_t *mem, png_info_t *info, png_row_fn row,
                     void *ctx)
{
    if (len > PNG_MAX_BYTES) {
        return PNG_ERR_TOO_BIG;
    }
    if (len < sizeof(k_signature) || memcmp(data, k_signature, sizeof(k_signature)) != 0) {
        return PNG_ERR_FORMAT;
    }
    memset(info, 0, sizeof(*info));
    size_t idat_len;
    png_err_t err = check(data, len, info, &idat_len);
    if (err != PNG_OK) {
        return err;
    }
    void *(*alloc)(size_t) = mem != NULL && mem->alloc != NULL ? mem->alloc : malloc;
    void (*release)(void *) = mem != NULL && mem->release != NULL ? mem->release : free;
    size_t bpp = info->type == PNG_RGBA ? 4 : 1;
    size_t stride = 1 + info->width * bpp, raw_len = stride * info->height;
    uint8_t *z = alloc(idat_len);
    if (z == NULL) {
        return PNG_ERR_NO_MEMORY;
    }
    uint8_t *raw = alloc(raw_len);
    if (raw == NULL) {
        release(z);
        return PNG_ERR_NO_MEMORY;
    }
    size_t pos = 8, at = 0;
    chunk_t c;
    do { /* the second pass: the image data in one piece; check() made sure every chunk reads */
        next_chunk(data, len, &pos, &c);
        if (is(&c, "IDAT")) {
            memcpy(z + at, c.data, c.len);
            at += c.len;
        }
    } while (!is(&c, "IEND"));
    bool inflated = png_inflate(z, idat_len, raw, raw_len);
    release(z);
    err = inflated ? PNG_OK : PNG_ERR_INFLATE;
    for (int y = 0; err == PNG_OK && y < info->height; y++) {
        uint8_t *line = raw + (size_t)y * stride;
        const uint8_t *prev = y > 0 ? line - stride + 1 : NULL;
        if (!unfilter(line + 1, prev, stride - 1, bpp, line[0])) {
            err = PNG_ERR_FORMAT;
        } else {
            row(ctx, y, line + 1, info);
        }
    }
    release(raw);
    return err;
}

const char *png_err_name(png_err_t err)
{
    switch (err) {
    case PNG_OK: return "ok";
    case PNG_ERR_FORMAT: return "not a PNG, or damaged";
    case PNG_ERR_UNSUPPORTED: return "unsupported";
    case PNG_ERR_TOO_BIG: return "too big";
    case PNG_ERR_NO_MEMORY: return "no memory";
    case PNG_ERR_INFLATE: return "image data damaged";
    }
    return "?";
}
```


- [ ] **Step 4: The device's inflate and the component.** The ROM's `tinfl_decompress` (ESP32-S3 ROM, `esp_rom/include/miniz.h`) with its 11 KB state in PSRAM, not on a task's stack.

`components/png/png_inflate_rom.c`:

```c
#include "png_inflate.h"

#include "esp_heap_caps.h"
#include "miniz.h"

/* The ESP32-S3 ROM's tinfl (esp_rom/include/miniz.h), into one buffer that holds the whole image, so
 * it needs no dictionary of its own. Its state is some 11 KB: in PSRAM, not on the caller's stack. */
bool png_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    tinfl_decompressor *d = heap_caps_malloc(sizeof(*d), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (d == NULL) {
        return false;
    }
    tinfl_init(d);
    size_t in_size = in_len, out_size = out_len;
    tinfl_status st = tinfl_decompress(d, in, &in_size, out, out, &out_size,
                                       TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    heap_caps_free(d);
    return st == TINFL_STATUS_DONE && out_size == out_len;
}
```


`components/png/CMakeLists.txt`:

```cmake
# A small PNG reader for the radar images (spec §11.2). png.c is pure C and also builds on the
# host, which inflates through zlib; png_inflate_rom.c uses the ROM's tinfl.
idf_component_register(SRCS "png.c" "png_inflate_rom.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES util esp_rom heap)
```


- [ ] **Step 5: Run the tests.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ./build-host/test_png`
Expected: `5 Tests 0 Failures 0 Ignored`. Then `ctest --test-dir build-host`: `100% tests passed, 0 tests failed out of 54`.

- [ ] **Step 6: The firmware builds.** A new component directory needs a reconfigure first.

Run: `tools/idf.sh reconfigure && tools/idf.sh build 2>&1 | tail -3`
Expected: `Project build complete`, no warning from `components/png`.

- [ ] **Step 7: AGENTS.md, and commit.** The PNG reader joins the component list in §5.2 (AGENTS.md changes in the same commit, §8):

`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -220,6 +220,7 @@ components/
   netmgr/        Wi-Fi STA/AP, captive DNS, mDNS
   webui/         HTTP server, REST API, embedded web assets
   weather/       Open-Meteo URLs, parsers, bands and levels; the HTTPS fetch
+  png/           PNG reader for the radar images: palette and RGBA, row by row   [host]
   ha_mqtt/       MQTT session, discovery, state, commands, field mappings   (planned)
   sync/          when syncs run and their retries, SNTP packets, the sync task
   audio/         codec control, tone/WAV/stream players, alarm ringing      (planned)
```


```bash
git add components/png test/host/png_inflate_zlib.c test/host/test_png.c test/host/fixtures/png test/host/CMakeLists.txt AGENTS.md
git commit -m "feat(png): a PNG reader for the radar images"
```


### Task 2: Web-Mercator views (`map`)

**Files:**
- Create: `components/map/CMakeLists.txt`, `components/map/include/map_view.h`, `components/map/map_view.c`
- Create: `test/host/test_map_view.c`
- Modify: `test/host/CMakeLists.txt` (Unity's doubles; the `map` library; the test)
- Modify: `AGENTS.md` (the `map/` line in §5.2)

**Interfaces:**
- Consumes: nothing.
- Produces (Tasks 3–6 and 9–12 rely on these names):
  - `map_view_t` (`cx`, `cy` in world pixels, `zoom`, `w`, `h`), `map_bounds_t` (`lat_min`, `lat_max`, `lon_min`, `lon_max`), `MAP_EARTH_R` 6 378 137, `MAP_TILE_PX` 256.
  - `map_view_init(v, lat_e4, lon_e4, zoom, w, h)`, `map_zoom_for_range(lat_e4, range_m, px)`, `map_world_px()`, `map_mercator()`, `map_project()`, `map_unproject()`, `map_pixel_to_mercator()`, `map_metres_per_px()`, `map_view_bounds()`, `map_bounds_contain()`, `map_distance_m()` (the mean Earth radius), `map_bearing_deg()` (0 = north, clockwise).

- [ ] **Step 1: Write the failing test.** Reference values from Python's `math` over the same formulas, and ČHMÚ's grid in web-Mercator metres. Unity compares doubles only with `UNITY_INCLUDE_DOUBLE`, which this step turns on for every host test.

`test/host/test_map_view.c`:

```c
#include <math.h>

#include "map_view.h"
#include "unity.h"

/* Reference values from Python's math module on 2026-10-01 (spec §11.1), and ČHMÚ's own grid: its
 * HDF5 composite says its south-west corner, 48.047275 N 11.266869 E, lies at x = 1254222.15 m,
 * and its north edge, 51.458369 N, at y = 6702777.85 m (ODIM `where`). */

void setUp(void) {}
void tearDown(void) {}

#define BRNO_LAT_E4 491951
#define BRNO_LON_E4 166068

static void test_the_centre_lands_mid_view_and_round_trips(void)
{
    map_view_t v;
    map_view_init(&v, BRNO_LAT_E4, BRNO_LON_E4, 6.5, 400, 280);
    double x, y;
    map_project(&v, 49.1951, 16.6068, &x, &y);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 200.0, x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 140.0, y);
    double lat, lon;
    map_project(&v, 50.0755, 14.4378, &x, &y); /* Praha: north-west of Brno, up and left */
    TEST_ASSERT_TRUE(x < 200 && y < 140);
    map_unproject(&v, x, y, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 50.0755, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 14.4378, lon);
}

static void test_world_pixels_match_the_tiles(void)
{
    /* At zoom 7 Brno falls in tile (69, 43), as RainViewer numbers its tiles */
    double x, y;
    map_world_px(49.1951, 16.6068, 7.0, &x, &y);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 17895.588, x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 11226.134, y);
    TEST_ASSERT_EQUAL_INT(69, (int)(x / MAP_TILE_PX));
    TEST_ASSERT_EQUAL_INT(43, (int)(y / MAP_TILE_PX));
}

static void test_mercator_metres_match_chmus_grid(void)
{
    double mx, my;
    map_mercator(48.047275, 11.266869, &mx, &my);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 1254222.12, mx);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 6114723.32, my);
    map_mercator(51.458369, 19.623974, &mx, &my);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 2184530.79, mx);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 6702777.93, my);

    /* A view's pixel in metres, as the radar's reprojection reads it */
    map_view_t v;
    map_view_init(&v, BRNO_LAT_E4, BRNO_LON_E4, 6.5, 400, 280);
    map_pixel_to_mercator(&v, 200.0, 140.0, &mx, &my);
    double bx, by;
    map_mercator(49.1951, 16.6068, &bx, &by);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, bx, mx);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, by, my);
}

static void test_the_scale_and_the_range_zoom(void)
{
    map_view_t v;
    map_view_init(&v, BRNO_LAT_E4, BRNO_LON_E4, 6.5, 400, 280);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1130.25, map_metres_per_px(&v)); /* spec §11.1: about 1.1 km */

    double z = map_zoom_for_range(BRNO_LAT_E4, 50000.0, 119); /* the flight radar: 50 km, centre to the top */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 7.927604, z);
    map_view_init(&v, BRNO_LAT_E4, BRNO_LON_E4, z, 400, 238);
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 50000.0, 119 * map_metres_per_px(&v));
}

static void test_distance_and_bearing(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(50.0, 184332.5, map_distance_m(49.1951, 16.6068, 50.0755, 14.4378)); /* Praha */
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 302.90, map_bearing_deg(49.1951, 16.6068, 50.0755, 14.4378));
    TEST_ASSERT_DOUBLE_WITHIN(50.0, 48785.2, map_distance_m(49.1951, 16.6068, 49.631836, 16.671102));
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 5.45, map_bearing_deg(49.1951, 16.6068, 49.631836, 16.671102));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, map_distance_m(49.1951, 16.6068, 49.1951, 16.6068));
}

static void test_the_bounds_hold_the_corners(void)
{
    map_view_t v;
    map_view_init(&v, BRNO_LAT_E4, BRNO_LON_E4, 6.5, 400, 280);
    map_bounds_t b;
    map_view_bounds(&v, &b);
    double lat, lon;
    map_unproject(&v, 0, 0, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, lat, b.lat_max);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, lon, b.lon_min);
    map_unproject(&v, 400, 280, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, lat, b.lat_min);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, lon, b.lon_max);
    TEST_ASSERT_TRUE(map_bounds_contain(&b, 49.1951, 16.6068));
    TEST_ASSERT_FALSE(map_bounds_contain(&b, 52.52, 13.405)); /* Berlin is off the edge */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_centre_lands_mid_view_and_round_trips);
    RUN_TEST(test_world_pixels_match_the_tiles);
    RUN_TEST(test_mercator_metres_match_chmus_grid);
    RUN_TEST(test_the_scale_and_the_range_zoom);
    RUN_TEST(test_distance_and_bearing);
    RUN_TEST(test_the_bounds_hold_the_corners);
    return UNITY_END();
}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -23,6 +23,7 @@ enable_testing()
 
 add_library(unity STATIC third_party/unity/unity.c)
 target_include_directories(unity PUBLIC third_party/unity)
+target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE) # the map's projection compares doubles
 
 # Components under test: the same sources the firmware compiles.
 file(GLOB UTIL_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/util/*.c)
@@ -63,6 +64,13 @@ target_include_directories(png PUBLIC ${REPO_ROOT}/components/png/include)
 target_compile_options(png PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(png PUBLIC util PRIVATE ZLIB::ZLIB)
 
+# map: the web-Mercator views, the built-in map data and its drawing (spec §11.1); pure C.
+file(GLOB MAP_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/map/*.c)
+add_library(map STATIC ${MAP_SOURCES})
+target_include_directories(map PUBLIC ${REPO_ROOT}/components/map/include)
+target_compile_options(map PRIVATE ${REFLBO_WARNINGS})
+target_link_libraries(map PUBLIC gfx util m)
+
 # sync: the planner (modes, quiet hours, retries) and the SNTP packets build on the host; the task does not.
 add_library(sync_logic STATIC ${REPO_ROOT}/components/sync/sync_plan.c ${REPO_ROOT}/components/sync/sync_ntp.c)
 target_include_directories(sync_logic PUBLIC ${REPO_ROOT}/components/sync/include)
@@ -188,6 +196,7 @@ reflbo_host_test(test_astro astro)
 reflbo_host_test(test_sync_plan sync_logic)
 reflbo_host_test(test_png png ZLIB::ZLIB)
 target_compile_definitions(test_png PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/png")
+reflbo_host_test(test_map_view map)
 reflbo_host_test(test_gesture board_logic)
 reflbo_host_test(test_shtc3_codec sensors_logic)
 reflbo_host_test(test_battery_model sensors_logic)
```


- [ ] **Step 2: Run it to see it fail.**

Run: `cmake -S test/host -B build-host -G Ninja`
Expected: `CMake Error … No SOURCES given to target: map` (the library globs `components/map/*.c`, and there are none yet).

- [ ] **Step 3: The views.**

`components/map/include/map_view.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Web Mercator views (spec §11.1), the projection of both radar sources: a centre and a zoom over
 * a w x h pixel rectangle. At zoom z the world is 256 x 2^z pixels across, as for map tiles.
 * Pure C, host-buildable.
 */

#define MAP_EARTH_R 6378137.0 /* web Mercator's sphere (EPSG:3857), metres */
#define MAP_TILE_PX 256

typedef struct {
    double cx, cy; /* the centre in world pixels at `zoom` */
    double zoom;
    int16_t w, h; /* the view in screen pixels */
} map_view_t;

typedef struct {
    double lat_min, lat_max, lon_min, lon_max; /* degrees */
} map_bounds_t;

/* A w x h view centred on (lat_e4, lon_e4) at `zoom`; latitudes beyond ±85° are clamped. */
void map_view_init(map_view_t *v, int32_t lat_e4, int32_t lon_e4, double zoom, int16_t w, int16_t h);
/* The zoom at which `range_m` on the ground spans `px` pixels at latitude lat_e4: the flight
 * radar's range from its centre to the top edge. */
double map_zoom_for_range(int32_t lat_e4, double range_m, int px);

/* World pixels of a point at `zoom`. */
void map_world_px(double lat, double lon, double zoom, double *x, double *y);
/* Web Mercator metres of a point (EPSG:3857), as ČHMÚ's grid and the projection strings use them. */
void map_mercator(double lat, double lon, double *mx, double *my);

/* Screen pixels of a point, from the view's top-left; it may fall outside the view. */
void map_project(const map_view_t *v, double lat, double lon, double *x, double *y);
/* The point at a screen pixel. */
void map_unproject(const map_view_t *v, double x, double y, double *lat, double *lon);
/* Web Mercator metres at a screen pixel: what the radar's reprojection reads. */
void map_pixel_to_mercator(const map_view_t *v, double x, double y, double *mx, double *my);
/* Ground metres per screen pixel at the view's centre. */
double map_metres_per_px(const map_view_t *v);

void map_view_bounds(const map_view_t *v, map_bounds_t *b);
bool map_bounds_contain(const map_bounds_t *b, double lat, double lon);

/* Great-circle distance (metres, the mean Earth radius) and initial bearing (degrees, 0 = north,
 * clockwise) from point 1 to point 2. */
double map_distance_m(double lat1, double lon1, double lat2, double lon2);
double map_bearing_deg(double lat1, double lon1, double lat2, double lon2);
```


`components/map/map_view.c`:

```c
#include "map_view.h"

#include <math.h>

#define PI 3.14159265358979323846
#define MEAN_EARTH_R 6371008.8 /* metres, for distances on the ground */
#define MAX_LAT 85.0           /* web Mercator's own limit is 85.0511 */

static double rad(double deg)
{
    return deg * PI / 180.0;
}

static double deg(double r)
{
    return r * 180.0 / PI;
}

static double world_size(double zoom)
{
    return MAP_TILE_PX * pow(2.0, zoom);
}

static double clamp_lat(double lat)
{
    return lat > MAX_LAT ? MAX_LAT : lat < -MAX_LAT ? -MAX_LAT : lat;
}

void map_world_px(double lat, double lon, double zoom, double *x, double *y)
{
    double w = world_size(zoom);
    *x = (lon + 180.0) / 360.0 * w;
    *y = (0.5 - log(tan(PI / 4 + rad(clamp_lat(lat)) / 2)) / (2 * PI)) * w;
}

void map_mercator(double lat, double lon, double *mx, double *my)
{
    *mx = MAP_EARTH_R * rad(lon);
    *my = MAP_EARTH_R * log(tan(PI / 4 + rad(clamp_lat(lat)) / 2));
}

void map_view_init(map_view_t *v, int32_t lat_e4, int32_t lon_e4, double zoom, int16_t w, int16_t h)
{
    v->zoom = zoom;
    v->w = w;
    v->h = h;
    map_world_px(lat_e4 / 1e4, lon_e4 / 1e4, zoom, &v->cx, &v->cy);
}

double map_zoom_for_range(int32_t lat_e4, double range_m, int px)
{
    double m_per_px = range_m / (px > 0 ? px : 1);
    return log2(2 * PI * MAP_EARTH_R * cos(rad(clamp_lat(lat_e4 / 1e4))) / m_per_px / MAP_TILE_PX);
}

void map_project(const map_view_t *v, double lat, double lon, double *x, double *y)
{
    double wx, wy;
    map_world_px(lat, lon, v->zoom, &wx, &wy);
    *x = wx - v->cx + v->w / 2.0;
    *y = wy - v->cy + v->h / 2.0;
}

void map_unproject(const map_view_t *v, double x, double y, double *lat, double *lon)
{
    double w = world_size(v->zoom);
    double wx = x + v->cx - v->w / 2.0, wy = y + v->cy - v->h / 2.0;
    *lon = wx / w * 360.0 - 180.0;
    *lat = deg(2 * atan(exp((0.5 - wy / w) * 2 * PI)) - PI / 2);
}

void map_pixel_to_mercator(const map_view_t *v, double x, double y, double *mx, double *my)
{
    double w = world_size(v->zoom);
    double wx = x + v->cx - v->w / 2.0, wy = y + v->cy - v->h / 2.0;
    *mx = (wx / w - 0.5) * 2 * PI * MAP_EARTH_R;
    *my = (0.5 - wy / w) * 2 * PI * MAP_EARTH_R;
}

double map_metres_per_px(const map_view_t *v)
{
    double lat, lon;
    map_unproject(v, v->w / 2.0, v->h / 2.0, &lat, &lon);
    return 2 * PI * MAP_EARTH_R * cos(rad(lat)) / world_size(v->zoom);
}

void map_view_bounds(const map_view_t *v, map_bounds_t *b)
{
    map_unproject(v, 0, 0, &b->lat_max, &b->lon_min);
    map_unproject(v, v->w, v->h, &b->lat_min, &b->lon_max);
}

bool map_bounds_contain(const map_bounds_t *b, double lat, double lon)
{
    return lat >= b->lat_min && lat <= b->lat_max && lon >= b->lon_min && lon <= b->lon_max;
}

double map_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = rad(lat1), p2 = rad(lat2), dp = p2 - p1, dl = rad(lon2 - lon1);
    double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 2 * MEAN_EARTH_R * asin(sqrt(h > 1 ? 1 : h));
}

double map_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = rad(lat1), p2 = rad(lat2), dl = rad(lon2 - lon1);
    double b = deg(atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)));
    return b < 0 ? b + 360.0 : b;
}
```


`components/map/CMakeLists.txt`:

```cmake
# The map under both radars (spec §11.1): web-Mercator views, the built-in borders, towns and
# airports, and their drawing. Pure C: also built on the host.
idf_component_register(SRCS "map_view.c"
                       INCLUDE_DIRS "include")
```


- [ ] **Step 4: Run the tests.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ./build-host/test_map_view`
Expected: `6 Tests 0 Failures 0 Ignored`; `ctest --test-dir build-host`: `out of 55`, all passing.

- [ ] **Step 5: The firmware builds.** `tools/idf.sh reconfigure && tools/idf.sh build`: clean.

- [ ] **Step 6: AGENTS.md, and commit.** The `map` component joins the list in §5.2 (AGENTS.md changes in the same commit, §8):

`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -221,6 +221,7 @@ components/
   webui/         HTTP server, REST API, embedded web assets
   weather/       Open-Meteo URLs, parsers, bands and levels; the HTTPS fetch
   png/           PNG reader for the radar images: palette and RGBA, row by row   [host]
+  map/           web-Mercator views: projection, distance and bearing   [host]
   ha_mqtt/       MQTT session, discovery, state, commands, field mappings   (planned)
   sync/          when syncs run and their retries, SNTP packets, the sync task
   audio/         codec control, tone/WAV/stream players, alarm ringing      (planned)
```


```bash
git add components/map test/host/test_map_view.c test/host/CMakeLists.txt AGENTS.md
git commit -m "feat(map): web-Mercator views"
```


### Task 3: The built-in map (`tools/gen_map.py`, `map_data`)

**Files:**
- Create: `tools/gen_map.py`, `tools/gen_map.sh`, `tools/tests/test_gen_map.py`
- Create: `assets/map/map.bin`, `assets/map/sources.txt`
- Create: `components/map/include/map_data.h`, `components/map/map_data.c`, `test/host/test_map_data.c`
- Modify: `components/map/CMakeLists.txt` (the source, and `map.bin` embedded), `test/host/CMakeLists.txt`, `THIRD_PARTY.md`
- Modify: `AGENTS.md` (§5.2's `map/` line, §6's command)

**Interfaces:**
- Consumes: `map_view.h` (Task 2), `util_crc32()`.
- Produces (Tasks 4, 9–12): `map_data_t`, `map_data_open(d, blob, len)` (the header and the sections' bounds), `map_data_verify(d)` (the CRC-32 over the blob: the host's check), `map_data_segments(d, bounds, fn, ctx)` with `map_segment_fn(ctx, kind, lat0, lon0, lat1, lon1)` and `map_line_kind_t` (`MAP_LINE_BORDER`, `MAP_LINE_COAST`), `map_data_towns()` and `map_data_airports()` with callbacks that return false to stop, `map_town_t` (`lat`, `lon`, `population`, `name`), `map_airport_t` (`lat`, `lon`, `iata`, `icao`, `large`), `MAP_DATA_VERSION`. The firmware sees the blob as `_binary_map_bin_start` … `_binary_map_bin_end`.

`map.bin` (748 KB: 2324 line pieces, 7342 towns, 5281 airports) is built from the sources of 2026-10-01; OurAirports changes daily, so a fresh run would differ. Copy it from the branch rather than generating it:

```bash
git checkout plan/m6 -- \
  assets/map/map.bin
```


- [ ] **Step 1: Write the failing tests.** The generator against small fixtures it builds itself; the reader against the real blob (`MAP_BIN`).

`tools/tests/test_gen_map.py`:

```python
import binascii
import struct
import unittest

import gen_map


class ChainTest(unittest.TestCase):
    def test_lines_that_meet_end_to_end_join(self):
        a, b, c, d, e = (0, 0), (1, 0), (2, 1), (5, 5), (6, 6)
        joined = gen_map.chain([[a, b], [c, b], [d, e]])  # the second one runs backwards
        self.assertEqual(len(joined), 2)
        self.assertIn(max(joined, key=len), ([a, b, c], [c, b, a]))

    def test_three_lines_meeting_at_a_point_stay_apart(self):
        hub = (0, 0)
        joined = gen_map.chain([[hub, (1, 0)], [hub, (0, 1)], [hub, (-1, 0)]])
        self.assertEqual(len(joined), 3)


class SimplifyTest(unittest.TestCase):
    def test_a_point_close_to_the_line_goes_and_a_far_one_stays(self):
        near = [(0, 0), (1, 0.001), (2, 0)]
        self.assertEqual(gen_map.simplify(near, 0.002), [(0, 0), (2, 0)])
        far = [(0, 0), (1, 0.01), (2, 0)]
        self.assertEqual(gen_map.simplify(far, 0.002), far)

    def test_longitude_is_scaled_by_the_latitude(self):
        # At 60 degrees a degree of longitude is half a degree of latitude long
        line = [(0, 60), (1, 60.0015), (2, 60)]
        self.assertEqual(len(gen_map.simplify(line, 0.002)), 2)


class SplitTest(unittest.TestCase):
    def test_pieces_overlap_by_a_point_and_rebuild_the_line(self):
        line = [(i, 0) for i in range(300)]
        pieces = gen_map.split(line, 128)
        self.assertTrue(all(2 <= len(p) <= 128 for p in pieces))
        rebuilt = pieces[0] + [pt for p in pieces[1:] for pt in p[1:]]
        self.assertEqual(rebuilt, line)
        self.assertEqual(gen_map.split(line[:5], 128), [line[:5]])


class EncodePointsTest(unittest.TestCase):
    def test_small_steps_are_int16_deltas(self):
        data = gen_map.encode_points([(16.6068, 49.1951), (16.6168, 49.1851)])
        self.assertEqual(data, struct.pack("<iihh", 491951, 166068, -100, 100))

    def test_a_jump_beyond_int16_is_escaped(self):
        data = gen_map.encode_points([(0, 0), (5, 0)])  # 5 degrees = 50000 units
        self.assertEqual(data, struct.pack("<iihhii", 0, 0, -32768, -32768, 0, 50000))


class NamesTest(unittest.TestCase):
    def test_the_local_name_the_ascii_one_and_the_fixes(self):
        self.assertEqual(gen_map.town_name({"name": "Prague", "namepar": "Praha"}), "Praha")
        self.assertEqual(gen_map.town_name({"name": "Brno"}), "Brno")
        self.assertEqual(gen_map.town_name({"name": "Пермь", "nameascii": "Perm"}), "Perm")
        self.assertEqual(gen_map.town_name({"name": "Pizen", "nameascii": "Pizen"}), "Plzeň")

    def test_towns_come_largest_first(self):
        geojson = {"features": [
            {"properties": {"name": "Brno", "latitude": 49.2, "longitude": 16.61, "pop_max": 388277}},
            {"properties": {"name": "Prague", "namepar": "Praha", "latitude": 50.08, "longitude": 14.44,
                            "pop_max": 1162000}},
        ]}
        self.assertEqual(gen_map.towns(geojson), [(500800, 144400, 1162000, "Praha"), (492000, 166100, 388277, "Brno")])


class AirportsTest(unittest.TestCase):
    CSV = ('"id","ident","type","name","latitude_deg","longitude_deg","icao_code","iata_code"\n'
           '1,"LKTB","medium_airport","Brno-Tuřany",49.151901,16.6944,"LKTB","BRQ"\n'
           '2,"LKPR","large_airport","Václav Havel",50.1008,14.26,"LKPR","PRG"\n'
           '3,"CZ-0001","small_airport","Somewhere",49.0,16.0,"",""\n'
           '4,"LKCV","heliport","Pad",49.9,15.4,"",""\n')

    def test_only_large_and_medium_airports_and_large_first(self):
        self.assertEqual(gen_map.airports(self.CSV), [(501008, 142600, "PRG", "LKPR", 0), (491519, 166944, "BRQ", "LKTB", 1)])


class PackTest(unittest.TestCase):
    def test_the_header_its_crc_and_the_sections(self):
        lines = [(gen_map.KIND_BORDER, [(16.0, 49.0), (16.5, 49.5)]), (gen_map.KIND_COAST, [(0, 0), (5, 0), (5, 1)])]
        towns = [(492000, 166100, 388277, "Brno")]
        airports = [(491519, 166944, "BRQ", "LKTB", 1)]
        blob = gen_map.pack(lines, towns, airports)
        magic, version, _, crc, *words = gen_map.HEADER.unpack_from(blob)
        self.assertEqual((magic, version), (b"RMAP", 1))
        self.assertEqual(crc, binascii.crc32(blob[12:]) & 0xFFFFFFFF)
        n_lines, line_off, point_off, point_bytes, n_towns, town_off, n_airports, airport_off, name_off, name_bytes = words
        self.assertEqual((n_lines, n_towns, n_airports), (2, 1, 1))
        self.assertTrue(all(off % 4 == 0 for off in (line_off, point_off, town_off, airport_off, name_off)))
        self.assertEqual(gen_map.LINE.unpack_from(blob, line_off), (4900, 4950, 1600, 1650, 0, 2, gen_map.KIND_BORDER))
        self.assertEqual(gen_map.LINE.unpack_from(blob, line_off + 16), (0, 100, 0, 500, 12, 3, gen_map.KIND_COAST))
        self.assertEqual(point_bytes, 12 + 8 + 12 + 4)  # the escaped jump costs 12 bytes, the step after it 4
        self.assertEqual(gen_map.TOWN.unpack_from(blob, town_off), (492000, 166100, 388277, 0))
        self.assertEqual(blob[name_off:name_off + name_bytes], b"Brno\0")
        self.assertEqual(gen_map.AIRPORT.unpack_from(blob, airport_off), (491519, 166944, b"BRQ", b"LKTB", 1))


if __name__ == "__main__":
    unittest.main()
```


`test/host/test_map_data.c`:

```c
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map_data.h"
#include "unity.h"

/* The real assets/map/map.bin, as tools/gen_map.py packs it (spec §11.1). */

static uint8_t *s_blob;
static size_t s_len;
static map_data_t s_map;

void setUp(void) {}
void tearDown(void) {}

static void load(void)
{
    FILE *f = fopen(MAP_BIN, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, MAP_BIN);
    fseek(f, 0, SEEK_END);
    s_len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    s_blob = malloc(s_len);
    TEST_ASSERT_EQUAL_size_t(s_len, fread(s_blob, 1, s_len, f));
    fclose(f);
}

static void test_the_blob_opens_and_holds_the_world(void)
{
    TEST_ASSERT_TRUE(map_data_open(&s_map, s_blob, s_len));
    TEST_ASSERT_TRUE(map_data_verify(&s_map)); /* the file as read from disk */
    TEST_ASSERT_TRUE(s_map.lines > 2000);
    TEST_ASSERT_TRUE(s_map.towns > 7000);
    TEST_ASSERT_TRUE(s_map.airports > 5000);
}

typedef struct {
    char names[16][32];
    int count;
    uint32_t last_population;
    bool rising; /* a town larger than the one before it */
} towns_t;

static bool on_town(void *ctx, const map_town_t *t)
{
    towns_t *c = ctx;
    if (c->count < 16) {
        snprintf(c->names[c->count], sizeof(c->names[0]), "%s", t->name);
    }
    c->rising |= c->count > 0 && t->population > c->last_population;
    c->last_population = t->population;
    c->count++;
    return true;
}

static int index_of(const towns_t *c, const char *name)
{
    for (int i = 0; i < c->count && i < 16; i++) {
        if (strcmp(c->names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static void test_towns_around_brno_come_largest_first(void)
{
    map_view_t v;
    map_view_init(&v, 491951, 166068, 6.5, 400, 280); /* the weather radar's default view */
    map_bounds_t b;
    map_view_bounds(&v, &b);
    towns_t c = { 0 };
    map_data_towns(&s_map, &b, on_town, &c);
    TEST_ASSERT_FALSE(c.rising);
    int wien = index_of(&c, "Wien"), praha = index_of(&c, "Praha"); /* Natural Earth's local names */
    TEST_ASSERT_TRUE(wien >= 0 && praha > wien);
    TEST_ASSERT_TRUE(index_of(&c, "Brno") > praha);
    TEST_ASSERT_TRUE(index_of(&c, "Olomouc") > index_of(&c, "Brno"));
    TEST_ASSERT_EQUAL_INT(-1, index_of(&c, "Plzeň")); /* west of the view */

    b = (map_bounds_t){ 49.0, 50.5, 12.5, 14.0 };
    towns_t west = { 0 };
    map_data_towns(&s_map, &b, on_town, &west);
    TEST_ASSERT_TRUE(index_of(&west, "Plzeň") >= 0); /* corrected from "Pizen" */
}

static bool stop_after_one(void *ctx, const map_town_t *t)
{
    (void)t;
    (*(int *)ctx)++;
    return false;
}

static void test_a_callback_can_stop_the_walk(void)
{
    map_bounds_t world = { -90, 90, -180, 180 };
    int n = 0;
    map_data_towns(&s_map, &world, stop_after_one, &n);
    TEST_ASSERT_EQUAL_INT(1, n);
}

typedef struct {
    char iata[8][4];
    bool large[8];
    int count;
} airports_t;

static bool on_airport(void *ctx, const map_airport_t *a)
{
    airports_t *c = ctx;
    if (c->count < 8) {
        memcpy(c->iata[c->count], a->iata, 4);
        c->large[c->count] = a->large;
    }
    c->count++;
    return true;
}

static void test_airports_near_brno_come_large_first(void)
{
    map_bounds_t b = { 48.0, 49.5, 16.0, 17.5 };
    airports_t c = { 0 };
    map_data_airports(&s_map, &b, on_airport, &c);
    TEST_ASSERT_TRUE(c.count >= 2);
    TEST_ASSERT_EQUAL_STRING("VIE", c.iata[0]);
    TEST_ASSERT_TRUE(c.large[0]);
    bool brq = false;
    for (int i = 1; i < c.count && i < 8; i++) {
        brq |= strcmp(c.iata[i], "BRQ") == 0 && !c.large[i];
    }
    TEST_ASSERT_TRUE(brq);
}

typedef struct {
    map_bounds_t box;
    int borders, coasts, outside;
} segments_t;

static void on_segment(void *ctx, map_line_kind_t kind, double lat0, double lon0, double lat1, double lon1)
{
    segments_t *s = ctx;
    if (kind == MAP_LINE_BORDER) {
        s->borders++;
    } else {
        s->coasts++;
    }
    if (fmax(lat0, lat1) < s->box.lat_min || fmin(lat0, lat1) > s->box.lat_max || fmax(lon0, lon1) < s->box.lon_min ||
        fmin(lon0, lon1) > s->box.lon_max) {
        s->outside++;
    }
}

static void test_segments_near_mikulov_are_the_border_only(void)
{
    segments_t s = { .box = { 48.7, 48.9, 16.5, 16.8 } }; /* the Czech-Austrian border, far from any sea */
    map_data_segments(&s_map, &s.box, on_segment, &s);
    TEST_ASSERT_TRUE(s.borders > 0);
    TEST_ASSERT_EQUAL_INT(0, s.coasts);
    TEST_ASSERT_EQUAL_INT(0, s.outside);

    segments_t sea = { .box = { 53.5, 54.5, 9.5, 11.0 } }; /* Kiel: the Baltic coast */
    map_data_segments(&s_map, &sea.box, on_segment, &sea);
    TEST_ASSERT_TRUE(sea.coasts > 0);
}

static void test_a_damaged_blob_is_refused(void)
{
    uint8_t *copy = malloc(s_len);
    memcpy(copy, s_blob, s_len);
    map_data_t d;
    copy[s_len / 2] ^= 0x10; /* the CRC no longer matches: it opens, as the device opens it, but fails the check */
    TEST_ASSERT_TRUE(map_data_open(&d, copy, s_len));
    TEST_ASSERT_FALSE(map_data_verify(&d));
    copy[s_len / 2] ^= 0x10;
    TEST_ASSERT_TRUE(map_data_open(&d, copy, s_len));
    TEST_ASSERT_TRUE(map_data_verify(&d));
    TEST_ASSERT_FALSE(map_data_open(&d, copy, s_len - 1) && map_data_verify(&d)); /* cut short: one refuses it */
    TEST_ASSERT_FALSE(map_data_open(&d, copy, s_len / 2));                       /* cut into its sections */
    TEST_ASSERT_FALSE(map_data_open(&d, copy, 20));
    copy[4] = 2; /* a version this firmware doesn't read */
    TEST_ASSERT_FALSE(map_data_open(&d, copy, s_len));
    copy[4] = 1;
    copy[0] = 'X';
    TEST_ASSERT_FALSE(map_data_open(&d, copy, s_len));
    free(copy);
}

int main(void)
{
    load();
    UNITY_BEGIN();
    RUN_TEST(test_the_blob_opens_and_holds_the_world);
    RUN_TEST(test_towns_around_brno_come_largest_first);
    RUN_TEST(test_a_callback_can_stop_the_walk);
    RUN_TEST(test_airports_near_brno_come_large_first);
    RUN_TEST(test_segments_near_mikulov_are_the_border_only);
    RUN_TEST(test_a_damaged_blob_is_refused);
    int failures = UNITY_END();
    free(s_blob);
    return failures;
}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -197,6 +197,8 @@ reflbo_host_test(test_sync_plan sync_logic)
 reflbo_host_test(test_png png ZLIB::ZLIB)
 target_compile_definitions(test_png PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/png")
 reflbo_host_test(test_map_view map)
+reflbo_host_test(test_map_data map)
+target_compile_definitions(test_map_data PRIVATE MAP_BIN="${REPO_ROOT}/assets/map/map.bin")
 reflbo_host_test(test_gesture board_logic)
 reflbo_host_test(test_shtc3_codec sensors_logic)
 reflbo_host_test(test_battery_model sensors_logic)
```


- [ ] **Step 2: Run them to see them fail.**

Run: `python3 -m unittest discover -s tools/tests -t tools 2>&1 | tail -3` and `cmake --build build-host 2>&1 | grep -m1 error`
Expected: `ModuleNotFoundError: No module named 'gen_map'`; `fatal error: 'map_data.h' file not found`.

- [ ] **Step 3: The generator.** Plain Python 3: the sources download once into `ref/mapdata/` (git-ignored) and are checked against `assets/map/sources.txt`; borders that meet end to end are chained (7980 pieces into 398 lines), simplified with Douglas–Peucker at 0.002° (about 200 m), and cut into pieces of at most 128 points; towns take their local name where Natural Earth has one, with two fixes.

`tools/gen_map.py`:

```python
#!/usr/bin/env python3
"""Pack the built-in map under the radars (spec §11.1) into assets/map/map.bin.

Run it through tools/gen_map.sh; it needs Python 3 and nothing else. The sources are public domain
and are downloaded once into ref/mapdata/ (git-ignored); their SHA-256 go in assets/map/sources.txt:
  Natural Earth 5.1.2: the 1:10 m land borders, the 1:50 m coastline, the 1:10 m populated places
  OurAirports: airports.csv, of which the large and medium airports
The layout of map.bin is documented in components/map/include/map_data.h; this file writes it.
"""
import argparse
import binascii
import csv
import hashlib
import io
import json
import math
import pathlib
import struct
import sys
import urllib.request

NE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/"
SOURCES = {
    "borders": NE + "ne_10m_admin_0_boundary_lines_land.geojson",
    "coast": NE + "ne_50m_coastline.geojson",
    "places": NE + "ne_10m_populated_places_simple.geojson",
    "airports": "https://davidmegginson.github.io/ourairports-data/airports.csv",
}
VERSION = 1
MAGIC = b"RMAP"
HEADER = struct.Struct("<4sHHI10I")  # magic, version, reserved, crc32, then the ten section words
LINE = struct.Struct("<4hIHBx")      # bbox in 0.01 deg (lat_min, lat_max, lon_min, lon_max), point pos, count, kind
TOWN = struct.Struct("<iiII")        # lat_e4, lon_e4, population, name pos
AIRPORT = struct.Struct("<ii3s4sB")  # lat_e4, lon_e4, IATA, ICAO, kind (0 large, 1 medium)
KIND_BORDER, KIND_COAST = 0, 1
TOLERANCE_DEG = 0.002   # Douglas-Peucker, about 200 m (spec §11.1)
PIECE_POINTS = 128      # a line's points per record, so a render decodes only what lies near
ESCAPE = -32768         # a delta pair (ESCAPE, ESCAPE) is followed by an absolute point

# Natural Earth's own spelling, corrected where a Czech user would see it
NAME_FIXES = {"Pizen": "Plzeň", "Usti Nad Labem": "Ústí nad Labem"}


def line_parts(geojson):
    """GeoJSON LineStrings and MultiLineStrings -> [[(lon, lat), ...], ...]."""
    out = []
    for feature in geojson["features"]:
        geometry = feature.get("geometry")
        if not geometry:
            continue
        parts = [geometry["coordinates"]] if geometry["type"] == "LineString" else geometry["coordinates"]
        out.extend([[(float(x), float(y)) for x, y, *_ in part] for part in parts if len(part) >= 2])
    return out


def _key(point):
    return round(point[0], 6), round(point[1], 6)


def chain(lines):
    """Joins lines that meet end to end where only two meet, so 7980 border pieces become ~400 lines."""
    ends = {}
    for i, line in enumerate(lines):
        for point in (line[0], line[-1]):
            ends.setdefault(_key(point), []).append(i)
    used = [False] * len(lines)
    out = []
    for i, line in enumerate(lines):
        if used[i]:
            continue
        used[i] = True
        current = list(line)
        for _ in range(2):  # grow at the tail, then reversed, at the head
            while True:
                tail = _key(current[-1])
                if len(ends.get(tail, [])) != 2:
                    break
                nxt = [j for j in ends[tail] if not used[j]]
                if not nxt:
                    break
                used[nxt[0]] = True
                other = lines[nxt[0]]
                current.extend(other[1:] if _key(other[0]) == tail else list(reversed(other))[1:])
            current.reverse()
        out.append(current)
    return out


def simplify(points, tolerance):
    """Douglas-Peucker on (lon, lat), with longitude scaled by the cosine of the latitude."""
    if len(points) < 3:
        return list(points)
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        a, b = stack.pop()
        (x1, y1), (x2, y2) = points[a], points[b]
        k = math.cos(math.radians((y1 + y2) / 2))
        dx, dy = (x2 - x1) * k, y2 - y1
        length = math.hypot(dx, dy)
        best, index = -1.0, -1
        for i in range(a + 1, b):
            px, py = (points[i][0] - x1) * k, points[i][1] - y1
            d = abs(dx * py - dy * px) / length if length else math.hypot(px, py)
            if d > best:
                best, index = d, i
        if best > tolerance:
            keep[index] = True
            stack.extend([(a, index), (index, b)])
    return [p for p, kept in zip(points, keep) if kept]


def split(points, size=PIECE_POINTS):
    """Pieces of at most `size` points, each starting where the last one ended."""
    if len(points) <= size:
        return [points]
    return [points[i:i + size] for i in range(0, len(points) - 1, size - 1) if len(points[i:i + size]) >= 2]


def encode_points(points):
    """The first point absolute (int32 lat, lon in 1e-4 degrees), then int16 deltas; a delta that
    doesn't fit is an (ESCAPE, ESCAPE) pair followed by the point absolute."""
    out = bytearray()
    last = None
    for lon, lat in points:
        q = (round(lat * 1e4), round(lon * 1e4))
        if last is None:
            out += struct.pack("<ii", *q)
        else:
            d = (q[0] - last[0], q[1] - last[1])
            if -32767 <= d[0] <= 32767 and -32767 <= d[1] <= 32767:
                out += struct.pack("<hh", *d)
            else:
                out += struct.pack("<hhii", ESCAPE, ESCAPE, *q)
        last = q
    return bytes(out)


def bbox(points):
    """(lat_min, lat_max, lon_min, lon_max) in 0.01 degrees, rounded outwards."""
    lats = [p[1] for p in points]
    lons = [p[0] for p in points]
    return (math.floor(min(lats) * 100), math.ceil(max(lats) * 100),
            math.floor(min(lons) * 100), math.ceil(max(lons) * 100))


def latin(text):
    """Every character within Latin-1 and Latin Extended-A, which the fonts cover (spec §4.4)."""
    return all(ord(c) <= 0x17F for c in text)


def town_name(props):
    """The local name where Natural Earth has one (`namepar`: "Praha"), its own otherwise, in ASCII
    when the fonts lack its letters; corrected where a Czech user would see it."""
    name = props.get("namepar") or props.get("name") or ""
    if not latin(name):
        name = props.get("nameascii") or ""
    return NAME_FIXES.get(name, name)


def towns(geojson):
    """[(lat_e4, lon_e4, population, name)], the largest first."""
    out = []
    for feature in geojson["features"]:
        props = feature["properties"]
        name = town_name(props)
        if not name:
            continue
        out.append((round(props["latitude"] * 1e4), round(props["longitude"] * 1e4),
                    max(0, int(props.get("pop_max") or 0)), name))
    out.sort(key=lambda t: (-t[2], t[3]))
    return out


def airports(csv_text):
    """[(lat_e4, lon_e4, iata, icao, kind)]: the large and medium airports, large first."""
    out = []
    for row in csv.DictReader(io.StringIO(csv_text)):
        kind = {"large_airport": 0, "medium_airport": 1}.get(row["type"])
        if kind is None:
            continue
        icao = (row.get("icao_code") or row.get("ident") or "")[:4]
        out.append((round(float(row["latitude_deg"]) * 1e4), round(float(row["longitude_deg"]) * 1e4),
                    row.get("iata_code", "")[:3], icao, kind))
    out.sort(key=lambda a: (a[4], a[3]))
    return out


def lines_of(border_lines, coast_lines, tolerance=TOLERANCE_DEG):
    """[(kind, points)] for map.bin: the borders chained, both simplified and split into pieces."""
    out = []
    for kind, lines in ((KIND_BORDER, chain(border_lines)), (KIND_COAST, chain(coast_lines))):
        for line in lines:
            for piece in split(simplify(line, tolerance)):
                out.append((kind, piece))
    return out


def pack(lines, town_list, airport_list):
    """map.bin's bytes (layout in components/map/include/map_data.h)."""
    points = bytearray()
    line_bytes = bytearray()
    for kind, piece in lines:
        line_bytes += LINE.pack(*bbox(piece), len(points), len(piece), kind)
        points += encode_points(piece)
    names = bytearray()
    town_bytes = bytearray()
    for lat, lon, pop, name in town_list:
        town_bytes += TOWN.pack(lat, lon, pop, len(names))
        names += name.encode("utf-8") + b"\0"
    airport_bytes = bytearray()
    for lat, lon, iata, icao, kind in airport_list:
        airport_bytes += AIRPORT.pack(lat, lon, iata.encode("ascii", "replace").ljust(3, b"\0"),
                                      icao.encode("ascii", "replace").ljust(4, b"\0"), kind)
    body = bytearray()
    offsets = []
    for section in (line_bytes, points, town_bytes, airport_bytes, names):
        offsets.append(HEADER.size + len(body))
        body += section
        body += b"\0" * (-len(body) % 4)  # every section starts on a word
    words = (len(lines), offsets[0], offsets[1], len(points), len(town_list), offsets[2],
             len(airport_list), offsets[3], offsets[4], len(names))
    crc = binascii.crc32(struct.pack("<10I", *words) + bytes(body)) & 0xFFFFFFFF
    return HEADER.pack(MAGIC, VERSION, 0, crc, *words) + bytes(body)


def fetch(name, cache):
    """A source from the cache, downloaded first if it isn't there."""
    path = cache / pathlib.Path(SOURCES[name]).name
    if not path.exists():
        cache.mkdir(parents=True, exist_ok=True)
        print(f"downloading {SOURCES[name]}", file=sys.stderr)
        with urllib.request.urlopen(SOURCES[name], timeout=120) as response:
            path.write_bytes(response.read())
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cache", default="ref/mapdata", help="where the sources are kept")
    parser.add_argument("--out", default="assets/map/map.bin")
    parser.add_argument("--sources", default="assets/map/sources.txt")
    args = parser.parse_args(argv)
    cache = pathlib.Path(args.cache)
    paths = {name: fetch(name, cache) for name in SOURCES}
    lines = lines_of(line_parts(json.loads(paths["borders"].read_text())),
                     line_parts(json.loads(paths["coast"].read_text())))
    town_list = towns(json.loads(paths["places"].read_text()))
    airport_list = airports(paths["airports"].read_text(encoding="utf-8"))
    blob = pack(lines, town_list, airport_list)
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    with open(args.sources, "w") as f:
        f.write("# The sources of map.bin (tools/gen_map.py), all public domain\n")
        for name, path in paths.items():
            f.write(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {SOURCES[name]}\n")
    print(f"{out}: {len(blob)} bytes, {len(lines)} lines, {len(town_list)} towns, {len(airport_list)} airports")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```


`tools/gen_map.sh`:

```sh
#!/usr/bin/env bash
# Regenerate assets/map/map.bin from Natural Earth and OurAirports (spec §11.1). Plain Python 3;
# the sources are downloaded once into ref/mapdata/.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
python3 tools/gen_map.py "$@"
```


`assets/map/sources.txt`:

```text
# The sources of map.bin (tools/gen_map.py), all public domain
74d9c16229c095fde65943a9919e337682f044bcebccb120764f38edf3b70f4a  https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/ne_10m_admin_0_boundary_lines_land.geojson
271f1c4c1908312bac6b29d158ea1356544beafc129f260005300913aa5ea283  https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/ne_50m_coastline.geojson
fd3fa867a320cbd5c5b6bb5bc550afeec2939fb2cef688e508007282a55ac42f  https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/ne_10m_populated_places_simple.geojson
7a3fe6ee4a451469cb3197d43ad71838b587dc4bca8bef98728d779d2a475722  https://davidmegginson.github.io/ourairports-data/airports.csv
```


- [ ] **Step 4: The reader, and the blob in the image.** Opening checks the header and that every section lies inside the blob; the CRC-32 is a separate `map_data_verify()`, which the tests run (ruling: on the device the app image's own SHA-256 covers the blob, and a CRC over 748 KB would cost about 60 ms at each first render after a wake).

`components/map/include/map_data.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "map_view.h"

/*
 * The built-in map (spec §11.1): borders, coasts, towns and airports, packed by tools/gen_map.py
 * into assets/map/map.bin and read in place. Pure C, host-buildable. The layout, little-endian:
 *   header, 52 bytes: "RMAP", u16 version 1, u16 0, u32 CRC-32 of everything after this word,
 *     then u32 line count, line offset, point offset, point bytes, town count, town offset,
 *     airport count, airport offset, name offset, name bytes; every section on a word
 *   line, 16 bytes: i16 lat_min, lat_max, lon_min, lon_max (0.01°, rounded outwards), u32 point
 *     pos, u16 point count, u8 kind (map_line_kind_t), u8 0. Its points: the first as i32 lat,
 *     lon (1e-4°), the rest as i16 deltas; a delta pair (-32768, -32768) is followed by an
 *     absolute point
 *   town, 16 bytes: i32 lat, lon (1e-4°), u32 population, u32 name pos; the largest first
 *   airport, 16 bytes: i32 lat, lon (1e-4°), 3 bytes IATA, 4 bytes ICAO (NUL-padded), u8 kind
 *     (0 large, 1 medium); large first
 *   names: NUL-terminated UTF-8
 */

#define MAP_DATA_VERSION 1

typedef enum {
    MAP_LINE_BORDER,
    MAP_LINE_COAST,
} map_line_kind_t;

typedef struct {
    const uint8_t *blob;
    size_t len;
    uint32_t lines, line_off, point_off, point_bytes;
    uint32_t towns, town_off, airports, airport_off, name_off, name_bytes;
} map_data_t;

typedef struct {
    double lat, lon;
    uint32_t population;
    const char *name; /* in the blob */
} map_town_t;

typedef struct {
    double lat, lon;
    char iata[4]; /* "" where it has none */
    char icao[5];
    bool large;
} map_airport_t;

/* False if `blob` isn't a map: the wrong magic or version, or a section outside it. It doesn't read
 * the whole blob: the device's copy is part of its app image, which the bootloader and OTA check. */
bool map_data_open(map_data_t *d, const uint8_t *blob, size_t len);
/* The blob's CRC-32: for a copy that came from elsewhere, such as the file the host tests read. */
bool map_data_verify(const map_data_t *d);

typedef void (*map_segment_fn)(void *ctx, map_line_kind_t kind, double lat0, double lon0, double lat1, double lon1);
/* Every segment whose box meets `b`. */
void map_data_segments(const map_data_t *d, const map_bounds_t *b, map_segment_fn fn, void *ctx);

/* The towns inside `b`, the largest first, until `fn` returns false. */
typedef bool (*map_town_fn)(void *ctx, const map_town_t *t);
void map_data_towns(const map_data_t *d, const map_bounds_t *b, map_town_fn fn, void *ctx);

/* The airports inside `b`, large first, until `fn` returns false. */
typedef bool (*map_airport_fn)(void *ctx, const map_airport_t *a);
void map_data_airports(const map_data_t *d, const map_bounds_t *b, map_airport_fn fn, void *ctx);
```


`components/map/map_data.c`:

```c
#include "map_data.h"

#include <string.h>

#include "util_crc32.h"

#define HEADER_LEN 52
#define RECORD_LEN 16
#define ESCAPE (-32768)

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t le32s(const uint8_t *p)
{
    return (int32_t)le32(p);
}

static int16_t le16s(const uint8_t *p)
{
    return (int16_t)(p[0] | p[1] << 8);
}

static bool fits(uint32_t offset, uint64_t size, size_t len)
{
    return offset >= HEADER_LEN && offset <= len && size <= len - offset;
}

bool map_data_open(map_data_t *d, const uint8_t *blob, size_t len)
{
    memset(d, 0, sizeof(*d));
    if (len < HEADER_LEN || memcmp(blob, "RMAP", 4) != 0 || (blob[4] | blob[5] << 8) != MAP_DATA_VERSION) {
        return false;
    }
    map_data_t m = { .blob = blob, .len = len };
    uint32_t *words[] = { &m.lines, &m.line_off, &m.point_off, &m.point_bytes, &m.towns,
                          &m.town_off, &m.airports, &m.airport_off, &m.name_off, &m.name_bytes };
    for (int i = 0; i < 10; i++) {
        *words[i] = le32(blob + 12 + 4 * i);
    }
    if (!fits(m.line_off, (uint64_t)m.lines * RECORD_LEN, len) || !fits(m.point_off, m.point_bytes, len) ||
        !fits(m.town_off, (uint64_t)m.towns * RECORD_LEN, len) ||
        !fits(m.airport_off, (uint64_t)m.airports * RECORD_LEN, len) || !fits(m.name_off, m.name_bytes, len) ||
        (m.name_bytes > 0 && blob[m.name_off + m.name_bytes - 1] != '\0')) {
        return false;
    }
    *d = m;
    return true;
}

bool map_data_verify(const map_data_t *d)
{
    return d->blob != NULL && util_crc32(0, d->blob + 12, d->len - 12) == le32(d->blob + 8);
}

static bool meets(const map_bounds_t *b, double lat0, double lon0, double lat1, double lon1)
{
    double lat_lo = lat0 < lat1 ? lat0 : lat1, lat_hi = lat0 < lat1 ? lat1 : lat0;
    double lon_lo = lon0 < lon1 ? lon0 : lon1, lon_hi = lon0 < lon1 ? lon1 : lon0;
    return lat_hi >= b->lat_min && lat_lo <= b->lat_max && lon_hi >= b->lon_min && lon_lo <= b->lon_max;
}

void map_data_segments(const map_data_t *d, const map_bounds_t *b, map_segment_fn fn, void *ctx)
{
    const uint8_t *points = d->blob + d->point_off;
    for (uint32_t i = 0; i < d->lines; i++) {
        const uint8_t *l = d->blob + d->line_off + (size_t)i * RECORD_LEN;
        if (!meets(b, le16s(l) / 100.0, le16s(l + 4) / 100.0, le16s(l + 2) / 100.0, le16s(l + 6) / 100.0)) {
            continue;
        }
        uint32_t at = le32(l + 8);
        int count = l[12] | l[13] << 8;
        map_line_kind_t kind = (map_line_kind_t)l[14];
        if (at > d->point_bytes || d->point_bytes - at < 8) {
            continue;
        }
        int32_t lat = le32s(points + at), lon = le32s(points + at + 4);
        at += 8;
        for (int k = 1; k < count; k++) {
            if (d->point_bytes - at < 4) {
                break;
            }
            int16_t dlat = le16s(points + at), dlon = le16s(points + at + 2);
            at += 4;
            int32_t next_lat = lat + dlat, next_lon = lon + dlon;
            if (dlat == ESCAPE && dlon == ESCAPE) {
                if (d->point_bytes - at < 8) {
                    break;
                }
                next_lat = le32s(points + at);
                next_lon = le32s(points + at + 4);
                at += 8;
            }
            double a0 = lat / 1e4, o0 = lon / 1e4, a1 = next_lat / 1e4, o1 = next_lon / 1e4;
            if (meets(b, a0, o0, a1, o1)) {
                fn(ctx, kind, a0, o0, a1, o1);
            }
            lat = next_lat;
            lon = next_lon;
        }
    }
}

void map_data_towns(const map_data_t *d, const map_bounds_t *b, map_town_fn fn, void *ctx)
{
    for (uint32_t i = 0; i < d->towns; i++) {
        const uint8_t *t = d->blob + d->town_off + (size_t)i * RECORD_LEN;
        uint32_t name = le32(t + 12);
        map_town_t town = { .lat = le32s(t) / 1e4, .lon = le32s(t + 4) / 1e4, .population = le32(t + 8) };
        if (name >= d->name_bytes || !map_bounds_contain(b, town.lat, town.lon)) {
            continue;
        }
        town.name = (const char *)d->blob + d->name_off + name;
        if (!fn(ctx, &town)) {
            return;
        }
    }
}

void map_data_airports(const map_data_t *d, const map_bounds_t *b, map_airport_fn fn, void *ctx)
{
    for (uint32_t i = 0; i < d->airports; i++) {
        const uint8_t *a = d->blob + d->airport_off + (size_t)i * RECORD_LEN;
        map_airport_t airport = { .lat = le32s(a) / 1e4, .lon = le32s(a + 4) / 1e4, .large = a[15] == 0 };
        if (!map_bounds_contain(b, airport.lat, airport.lon)) {
            continue;
        }
        memcpy(airport.iata, a + 8, 3);
        memcpy(airport.icao, a + 11, 4);
        if (!fn(ctx, &airport)) {
            return;
        }
    }
}
```


`components/map/CMakeLists.txt`:

```diff
--- a/components/map/CMakeLists.txt
+++ b/components/map/CMakeLists.txt
@@ -1,4 +1,8 @@
-# The map under both radars (spec §11.1): web-Mercator views, the built-in borders, towns and
-# airports, and their drawing. Pure C: also built on the host.
-idf_component_register(SRCS "map_view.c"
-                       INCLUDE_DIRS "include")
+# The map under both radars (spec §11.1): web-Mercator views, the built-in borders, coasts, towns
+# and airports, and their drawing. Pure C: also built on the host. tools/gen_map.sh packs
+# assets/map/map.bin, embedded here as _binary_map_bin_start.._end.
+idf_component_register(SRCS "map_view.c" "map_data.c"
+                       INCLUDE_DIRS "include"
+                       PRIV_REQUIRES util)
+
+target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../../assets/map/map.bin" BINARY)
```


- [ ] **Step 5: The credits.** Both sources are public domain and need none, but say where the map comes from:

`THIRD_PARTY.md`:

```diff
--- a/THIRD_PARTY.md
+++ b/THIRD_PARTY.md
@@ -10,5 +10,7 @@ licence headers. Add a row whenever you copy or adapt anything.
 | DejaVu Sans 2.37 (Regular, Bold, Condensed Bold) | https://github.com/dejavu-fonts/dejavu-fonts | Bitstream Vera + Arev font licence, DejaVu changes public domain (`assets/fonts/LICENSE-DejaVu.txt`) | `assets/fonts/`; bitmaps rendered into `components/gfx/fonts/` | Rasterised by `tools/gen_fonts.sh`; C symbols use neutral `sans_*` names as the licence reserves the font names |
 | Material Icons (Regular), commit `f7bd4f25f3764883717c09a1fd867f560c9a9581` | https://github.com/google/material-design-icons | Apache-2.0 (`assets/icons/LICENSE-MaterialIcons.txt`) | `assets/icons/`; bitmaps rendered into `components/gfx/icons/` | Rasterised by `tools/gen_icons.sh` from the names in `assets/icons/icons.txt`; font and codepoint list unmodified |
 | Weather Icons 2.0 (font version 1.100), commit `bb80982bf1f43f2d57f9dd753e7413bf88beb9ed` | https://github.com/erikflowers/weather-icons | SIL OFL 1.1 (`assets/icons/LICENSE-WeatherIcons.txt`); its CSS, the codepoints' source, MIT | `assets/icons/`; bitmaps rendered into `components/gfx/icons/` | The weather codes and the sun's times (D26); rasterised by `tools/gen_icons.sh`, fitted to the square; font unmodified |
+| Natural Earth 5.1.2 (1:10 m land borders and populated places, 1:50 m coastline) | https://www.naturalearthdata.com, GeoJSON from https://github.com/nvkelso/natural-earth-vector | Public domain | `assets/map/map.bin` (packed) | The map's borders, coasts and towns (spec §11.1); simplified to about 200 m and packed by `tools/gen_map.py`, with the local names and two Czech spellings corrected; sources and their SHA-256 in `assets/map/sources.txt` |
+| OurAirports `airports.csv` | https://ourairports.com/data/ | Public domain | `assets/map/map.bin` (packed) | The large and medium airports with their IATA and ICAO codes (spec §11.1), packed by `tools/gen_map.py` |
 | QR Code generator library (C), commit `3c6d0b3cefb4e049dc337e82237c9644399716a8` | https://github.com/nayuki/QR-Code-generator | MIT (the header of each file) | `components/gfx/qrcodegen/` | The config screen's QR codes; unmodified |
 | tz database 2026c (`zone.tab` and the TZif footers) | https://www.iana.org/time-zones | Public domain | `web/zones.js` | Generated by `tools/gen_zones.py` from this Mac's `/usr/share/zoneinfo` |
```


- [ ] **Step 6: Run the tests.**

Run: `python3 -m unittest discover -s tools/tests -t tools 2>&1 | tail -1`, then `cmake --build build-host && ./build-host/test_map_data`
Expected: `OK`; `6 Tests 0 Failures 0 Ignored`; `ctest --test-dir build-host`: `out of 56`, all passing.

- [ ] **Step 7: The firmware builds.** `tools/idf.sh build`: clean. (The image doesn't grow yet: the linker leaves the blob out until Task 11 reads it.)

- [ ] **Step 8: AGENTS.md, and commit.** The generator's command joins §6, and the `map/` line in §5.2 names the built-in map (AGENTS.md changes in the same commit, §8):

`AGENTS.md`:

````diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -221,7 +221,7 @@ components/
   webui/         HTTP server, REST API, embedded web assets
   weather/       Open-Meteo URLs, parsers, bands and levels; the HTTPS fetch
   png/           PNG reader for the radar images: palette and RGBA, row by row   [host]
-  map/           web-Mercator views: projection, distance and bearing   [host]
+  map/           web-Mercator views; the built-in map (assets/map/map.bin)   [host]
   ha_mqtt/       MQTT session, discovery, state, commands, field mappings   (planned)
   sync/          when syncs run and their retries, SNTP packets, the sync task
   audio/         codec control, tone/WAV/stream players, alarm ringing      (planned)
@@ -296,6 +296,7 @@ cmake -S test/host -B build-host-asan -G Ninja -DREFLBO_SANITIZE=ON && cmake --b
   && ctest --test-dir build-host-asan --output-on-failure   # the same tests with ASan and UBSan
 tools/gen_fonts.sh                          # regenerate components/gfx/fonts (needs uv; versions in tools/requirements.txt)
 tools/gen_icons.sh                          # regenerate components/gfx/icons from assets/icons (needs uv)
+tools/gen_map.sh                            # regenerate assets/map/map.bin (plain Python 3; sources cached in ref/mapdata/)
 python3 tools/render.py                     # host renderings to captures/render/*.png (after the host build)
 node --test test/web/test_app.mjs           # the page script against a fake device; ctest runs it when node is found
 ```
````


```bash
git add tools/gen_map.py tools/gen_map.sh tools/tests/test_gen_map.py assets/map components/map test/host/test_map_data.c test/host/CMakeLists.txt THIRD_PARTY.md AGENTS.md
git commit -m "feat(map): the built-in borders, coasts, towns and airports"
```


### Task 4: Drawing the map (`map_draw`)

**Files:**
- Create: `components/map/include/map_draw.h`, `components/map/map_draw.c`, `test/host/test_map_draw.c`
- Modify: `components/map/CMakeLists.txt`, `test/host/CMakeLists.txt`
- Modify: `AGENTS.md` (§5.2's `map/` line)

**Interfaces:**
- Consumes: `map_view.h`, `map_data.h` (Tasks 2, 3), `gfx.h`, `gfx_fonts.h`.
- Produces (Tasks 9, 10): `map_labels_t` (`MAP_LABELS_MAX` 192: the flight radar reserves a box for each of up to 100 aircraft before the places), `map_style_t` (`airports`, `halo`, `max_towns`), `map_labels_init()`, `map_labels_reserve(l, rect)`, `map_label(fb, area, l, font, x, y, gap, text)` (right, left, above or below, the first place inside `area` and clear; false if none), `map_draw_lines(fb, area, v, d, style)`, `map_draw_places(fb, area, v, d, style, labels)`, `map_draw_home(fb, area, v, lat_e4, lon_e4, labels)`, `map_draw_rings(fb, area, v, range_m, labels)`. `area` is a screen rectangle whose top-left is the view's (0, 0); reserved rectangles are in screen pixels.

- [ ] **Step 1: Write the failing test.** On the real `map.bin`: labels keep clear and give up when boxed in; a reserved rectangle stays free; the halo clears ink around a label; lines stay inside their area; a known vertex of the Czech–Austrian border near Mikulov (48.7783 N, 16.6431 E) lands where it projects; home, rings and places.

`test/host/test_map_draw.c`:

```c
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "gfx_fonts.h"
#include "map_draw.h"
#include "unity.h"

/* Drawing the map (spec §11.1) on the real assets/map/map.bin. */

static uint8_t s_buf[400 * 300 / 8];
static gfx_fb_t s_fb;
static uint8_t *s_blob;
static map_data_t s_map;

void setUp(void)
{
    gfx_fb_init(&s_fb, s_buf, 400, 300);
    gfx_clear(&s_fb, GFX_WHITE);
}

void tearDown(void) {}

static void load(void)
{
    FILE *f = fopen(MAP_BIN, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, MAP_BIN);
    fseek(f, 0, SEEK_END);
    size_t len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    s_blob = malloc(len);
    TEST_ASSERT_EQUAL_size_t(len, fread(s_blob, 1, len, f));
    fclose(f);
    TEST_ASSERT_TRUE(map_data_open(&s_map, s_blob, len));
}

static int ink_in(gfx_rect_t r)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            n += gfx_get_pixel(&s_fb, x, y);
        }
    }
    return n;
}

static bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void test_labels_keep_clear_and_give_up_when_boxed_in(void)
{
    gfx_rect_t area = { 0, 0, 200, 100 };
    map_labels_t l;
    map_labels_init(&l);
    for (int i = 0; i < 4; i++) { /* right, left, above, below */
        TEST_ASSERT_TRUE_MESSAGE(map_label(&s_fb, area, &l, &gfx_font_sans_12, 100, 50, 3, "Brno"), "a free side");
    }
    TEST_ASSERT_EQUAL_INT(4, l.count);
    for (int i = 0; i < l.count; i++) {
        TEST_ASSERT_TRUE(ink_in(l.r[i]) > 0);
        for (int j = i + 1; j < l.count; j++) {
            TEST_ASSERT_FALSE(overlap(l.r[i], l.r[j]));
        }
    }
    TEST_ASSERT_FALSE(map_label(&s_fb, area, &l, &gfx_font_sans_12, 100, 50, 3, "Brno")); /* boxed in */
    TEST_ASSERT_EQUAL_INT(4, l.count);
    /* a label that would leave the area tries the other sides */
    TEST_ASSERT_TRUE(map_label(&s_fb, area, &l, &gfx_font_sans_12, 195, 20, 3, "Olomouc"));
    TEST_ASSERT_TRUE(l.r[4].x + l.r[4].w <= 200);
}

static void test_a_reserved_rectangle_stays_free(void)
{
    gfx_rect_t area = { 0, 0, 200, 100 };
    map_labels_t l;
    map_labels_init(&l);
    map_labels_reserve(&l, (gfx_rect_t){ 103, 40, 60, 20 }); /* where the label would go first */
    TEST_ASSERT_TRUE(map_label(&s_fb, area, &l, &gfx_font_sans_12, 100, 50, 3, "Brno"));
    TEST_ASSERT_FALSE(overlap(l.r[1], (gfx_rect_t){ 103, 40, 60, 20 }));
}

static void test_the_halo_clears_ink_around_a_label(void)
{
    gfx_fill_rect(&s_fb, (gfx_rect_t){ 0, 0, 200, 100 }, GFX_BLACK); /* heavy rain everywhere */
    map_labels_t l;
    map_labels_init(&l);
    TEST_ASSERT_TRUE(map_label(&s_fb, (gfx_rect_t){ 0, 0, 200, 100 }, &l, &gfx_font_sans_12, 100, 50, 3, "Brno"));
    gfx_rect_t r = l.r[0];
    int white = r.w * r.h - ink_in(r);
    TEST_ASSERT_TRUE_MESSAGE(white > r.w * r.h / 3, "the halo opens the rain around the text");
    TEST_ASSERT_TRUE(ink_in(r) > 0); /* and the text is still there */
}

static void test_lines_stay_inside_their_area(void)
{
    map_view_t v;
    map_view_init(&v, 491951, 166068, 6.5, 200, 140);
    gfx_rect_t area = { 100, 80, 200, 140 };
    map_style_t s = { .halo = false };
    map_draw_lines(&s_fb, area, &v, &s_map, &s);
    TEST_ASSERT_TRUE(ink_in(area) > 50);
    int outside = ink_in((gfx_rect_t){ 0, 0, 400, 80 }) + ink_in((gfx_rect_t){ 0, 220, 400, 80 }) +
                  ink_in((gfx_rect_t){ 0, 80, 100, 140 }) + ink_in((gfx_rect_t){ 300, 80, 100, 140 });
    TEST_ASSERT_EQUAL_INT(0, outside);
}

static void test_the_border_lands_where_it_is_projected(void)
{
    map_view_t v;
    map_view_init(&v, 487967, 166368, 9.0, 400, 280); /* Mikulov, on the Czech-Austrian border */
    gfx_rect_t area = { 0, 20, 400, 280 };
    map_style_t s = { .halo = true };
    map_draw_lines(&s_fb, area, &v, &s_map, &s);
    double x, y;
    map_project(&v, 48.7783, 16.6431, &x, &y); /* a vertex of the border south of Mikulov, from map.bin */
    gfx_rect_t near = { (int16_t)(x - 3), (int16_t)(20 + y - 3), 7, 7 };
    TEST_ASSERT_TRUE_MESSAGE(ink_in(near) > 0, "the border within 3 px of where it is");
}

static void test_home_is_a_ring_with_a_dot_and_is_reserved(void)
{
    map_view_t v;
    map_view_init(&v, 491951, 166068, 6.5, 400, 280);
    gfx_rect_t area = { 0, 20, 400, 280 };
    map_labels_t l;
    map_labels_init(&l);
    map_draw_home(&s_fb, area, &v, 491951, 166068, &l);
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 200, 160));  /* the dot at the centre */
    TEST_ASSERT_FALSE(gfx_get_pixel(&s_fb, 202, 160)); /* white inside the ring */
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 205, 160));  /* the ring, 5 px out */
    TEST_ASSERT_EQUAL_INT(1, l.count);
}

static void test_rings_mark_the_range_and_its_half(void)
{
    map_view_t v;
    double z = map_zoom_for_range(491951, 50000.0, 119);
    map_view_init(&v, 491951, 166068, z, 400, 238);
    gfx_rect_t area = { 0, 21, 400, 238 };
    map_labels_t l;
    map_labels_init(&l);
    map_draw_rings(&s_fb, area, &v, 50000.0, &l);
    int cx = 200, cy = 21 + 119;
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, cx, cy - 119 + 1) || gfx_get_pixel(&s_fb, cx, cy - 119)); /* 50 km */
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, cx, cy - 60) || gfx_get_pixel(&s_fb, cx, cy - 59));   /* 25 km */
    TEST_ASSERT_EQUAL_INT(2, l.count); /* "50 km" and "25 km" */
}

static void test_places_are_labelled_without_overlaps(void)
{
    map_view_t v;
    double z = map_zoom_for_range(491951, 100000.0, 119);
    map_view_init(&v, 491951, 166068, z, 400, 238);
    gfx_rect_t area = { 0, 21, 400, 238 };
    map_labels_t l;
    map_labels_init(&l);
    map_style_t s = { .airports = true, .max_towns = 12 };
    map_draw_places(&s_fb, area, &v, &s_map, &s, &l);
    TEST_ASSERT_TRUE(l.count >= 2); /* Brno and its airport, BRQ, at least */
    for (int i = 0; i < l.count; i++) {
        TEST_ASSERT_TRUE(l.r[i].x >= area.x && l.r[i].x + l.r[i].w <= area.x + area.w);
        for (int j = i + 1; j < l.count; j++) {
            TEST_ASSERT_FALSE(overlap(l.r[i], l.r[j]));
        }
    }
}

int main(void)
{
    load();
    UNITY_BEGIN();
    RUN_TEST(test_labels_keep_clear_and_give_up_when_boxed_in);
    RUN_TEST(test_a_reserved_rectangle_stays_free);
    RUN_TEST(test_the_halo_clears_ink_around_a_label);
    RUN_TEST(test_lines_stay_inside_their_area);
    RUN_TEST(test_the_border_lands_where_it_is_projected);
    RUN_TEST(test_home_is_a_ring_with_a_dot_and_is_reserved);
    RUN_TEST(test_rings_mark_the_range_and_its_half);
    RUN_TEST(test_places_are_labelled_without_overlaps);
    int failures = UNITY_END();
    free(s_blob);
    return failures;
}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -199,6 +199,8 @@ target_compile_definitions(test_png PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_
 reflbo_host_test(test_map_view map)
 reflbo_host_test(test_map_data map)
 target_compile_definitions(test_map_data PRIVATE MAP_BIN="${REPO_ROOT}/assets/map/map.bin")
+reflbo_host_test(test_map_draw map)
+target_compile_definitions(test_map_draw PRIVATE MAP_BIN="${REPO_ROOT}/assets/map/map.bin")
 reflbo_host_test(test_gesture board_logic)
 reflbo_host_test(test_shtc3_codec sensors_logic)
 reflbo_host_test(test_battery_model sensors_logic)
```


- [ ] **Step 2: Run it to see it fail.**

Run: `cmake --build build-host 2>&1 | grep -m1 error`
Expected: `fatal error: 'map_draw.h' file not found`.

- [ ] **Step 3: The drawing.** Lines get a white 4-neighbour halo when `halo` is set (over rain), then black; labels sit on white boxes a pixel larger than their text (ruling: a glyph halo read poorly over dithered rain); home is a ring of 5 px with a 3 × 3 dot; airports a short runway mark with their IATA code. Lines and places project in single precision through `fast_view_t`, clipped by Liang–Barsky in floats (ruling: the S3's FPU has no doubles, and a wide view crosses thousands of segments; `map_project()`, in doubles, stays for everything else, and where the two round differently a line moves by one pixel).

`components/map/include/map_draw.h`:

```c
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "map_data.h"
#include "map_view.h"

/*
 * Drawing the map (spec §11.1) into a screen rectangle `area`, whose top-left is the view's (0, 0).
 * Lines and dots get a 1 px white halo and labels a white box, so they stay legible over the radar's
 * rain. Pure C.
 */

#define MAP_LABELS_MAX 192 /* the flight radar reserves a box for each of up to 100 aircraft too */

/* What one map has placed so far, so later labels keep clear of it. */
typedef struct {
    gfx_rect_t r[MAP_LABELS_MAX];
    int count;
} map_labels_t;

typedef struct {
    bool airports;  /* the flight radar's map: airports with their IATA codes */
    bool halo;      /* lines get a white halo: there is rain under them */
    int max_towns;  /* labelled towns at most */
} map_style_t;

void map_labels_init(map_labels_t *l);
/* Keeps labels off a rectangle: a symbol, a caption, a panel. */
void map_labels_reserve(map_labels_t *l, gfx_rect_t r);
/* Draws `text` on a white box beside (x, y): right of it at `gap` px, else left, above or below, the
 * first place that is inside `area` and clear; false, and nothing drawn, if none is. */
bool map_label(gfx_fb_t *fb, gfx_rect_t area, map_labels_t *l, const gfx_font_t *font, int x, int y, int gap,
               const char *text);

/* Borders and coasts, clipped to `area`. */
void map_draw_lines(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const map_data_t *d, const map_style_t *s);
/* Towns as dots with labels, the largest first, and with s->airports the airports too (first, as
 * the flight radar cares more for them); a place whose label has no room is left out. */
void map_draw_places(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const map_data_t *d,
                     const map_style_t *s, map_labels_t *l);
/* Home as ⊙ at (lat_e4, lon_e4); its square is reserved. */
void map_draw_home(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, int32_t lat_e4, int32_t lon_e4,
                   map_labels_t *l);
/* Rings around the view's centre at `range_m` and half of it, labelled in km at their top right. */
void map_draw_rings(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, double range_m, map_labels_t *l);
```


`components/map/map_draw.c`:

```c
#include "map_draw.h"

#include <math.h>
#include <stdio.h>

#include "gfx_fonts.h"

#define TOWNS_TRIED 300 /* places looked at per map, so a dense view stays quick */
#define HOME_R 5
#define PI_F 3.14159265f

/* The view's projection in single precision, with its constants worked out once: the S3's FPU
 * does floats, not doubles, and the thousands of segments a wide view holds would take most of
 * a second in software doubles. A float stays within a hundredth of a pixel here. */
typedef struct {
    float kx, ox, ky, oy;
} fast_view_t;

static fast_view_t fast_view(const map_view_t *v)
{
    double w = MAP_TILE_PX * pow(2.0, v->zoom); /* the world's width in pixels */
    return (fast_view_t){ .kx = (float)(w / 360.0), .ox = (float)(w / 2 - v->cx + v->w / 2.0),
                          .ky = (float)(w / (2 * PI_F)), .oy = (float)(w / 2 - v->cy + v->h / 2.0) };
}

/* map_project() of a point, faster and as good as a screen needs. */
static void fast_project(const fast_view_t *f, double lat, double lon, float *x, float *y)
{
    float la = (float)(lat > 85.0 ? 85.0 : lat < -85.0 ? -85.0 : lat);
    *x = (float)lon * f->kx + f->ox;
    *y = f->oy - logf(tanf(PI_F / 4 + la * (PI_F / 360))) * f->ky;
}

void map_labels_init(map_labels_t *l)
{
    l->count = 0;
}

void map_labels_reserve(map_labels_t *l, gfx_rect_t r)
{
    if (l->count < MAP_LABELS_MAX) {
        l->r[l->count++] = r;
    }
}

static bool overlaps(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static bool inside(gfx_rect_t r, gfx_rect_t area)
{
    return r.x >= area.x && r.y >= area.y && r.x + r.w <= area.x + area.w && r.y + r.h <= area.y + area.h;
}

static bool is_free(const map_labels_t *l, gfx_rect_t r)
{
    for (int i = 0; i < l->count; i++) {
        if (overlaps(l->r[i], r)) {
            return false;
        }
    }
    return true;
}

/* Text on a white box a pixel larger: on a 1-bit panel it reads better over dithered rain than
 * an outline around each glyph. */
static void boxed_text(gfx_fb_t *fb, const gfx_font_t *font, gfx_rect_t box, const char *text)
{
    gfx_fill_rect(fb, box, GFX_WHITE);
    gfx_text(fb, font, box.x + 1, box.y + 1 + font->ascent, text, GFX_BLACK);
}

bool map_label(gfx_fb_t *fb, gfx_rect_t area, map_labels_t *l, const gfx_font_t *font, int x, int y, int gap,
               const char *text)
{
    if (l->count >= MAP_LABELS_MAX) {
        return false;
    }
    int w = gfx_text_width(font, text) + 2, h = font->line_height + 2; /* the halo's pixel on each side */
    const gfx_rect_t k_spots[4] = {
        /* right and left, centred on y; above and below, clear of those two */
        { (int16_t)(x + gap), (int16_t)(y - h / 2), (int16_t)w, (int16_t)h },
        { (int16_t)(x - gap - w), (int16_t)(y - h / 2), (int16_t)w, (int16_t)h },
        { (int16_t)(x - w / 2), (int16_t)(y - h / 2 - h), (int16_t)w, (int16_t)h },
        { (int16_t)(x - w / 2), (int16_t)(y - h / 2 + h), (int16_t)w, (int16_t)h },
    };
    for (int i = 0; i < 4; i++) {
        gfx_rect_t r = k_spots[i];
        if (inside(r, area) && is_free(l, r)) {
            boxed_text(fb, font, r, text);
            l->r[l->count++] = r;
            return true;
        }
    }
    return false;
}

/* Liang-Barsky: clips the segment to [x0, x1] x [y0, y1]; false if nothing is left. */
static bool clip(float *ax, float *ay, float *bx, float *by, float x0, float y0, float x1, float y1)
{
    float dx = *bx - *ax, dy = *by - *ay, t0 = 0.0f, t1 = 1.0f;
    const float p[4] = { -dx, dx, -dy, dy };
    const float q[4] = { *ax - x0, x1 - *ax, *ay - y0, y1 - *ay };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) {
                return false;
            }
            continue;
        }
        float t = q[i] / p[i];
        if (p[i] < 0.0f) {
            if (t > t1) {
                return false;
            }
            t0 = t > t0 ? t : t0;
        } else {
            if (t < t0) {
                return false;
            }
            t1 = t < t1 ? t : t1;
        }
    }
    float sx = *ax, sy = *ay;
    *ax = sx + t0 * dx;
    *ay = sy + t0 * dy;
    *bx = sx + t1 * dx;
    *by = sy + t1 * dy;
    return true;
}

typedef struct {
    gfx_fb_t *fb;
    gfx_rect_t area;
    fast_view_t fast;
    bool halo;
    bool white; /* this pass draws the halo */
} lines_ctx_t;

static void on_segment(void *arg, map_line_kind_t kind, double lat0, double lon0, double lat1, double lon1)
{
    (void)kind; /* borders and coasts look alike: 1 px lines */
    lines_ctx_t *c = arg;
    float ax, ay, bx, by;
    fast_project(&c->fast, lat0, lon0, &ax, &ay);
    fast_project(&c->fast, lat1, lon1, &bx, &by);
    if (!clip(&ax, &ay, &bx, &by, 0, 0, c->area.w - 1, c->area.h - 1)) {
        return;
    }
    int x0 = c->area.x + (int)lroundf(ax), y0 = c->area.y + (int)lroundf(ay);
    int x1 = c->area.x + (int)lroundf(bx), y1 = c->area.y + (int)lroundf(by);
    if (c->white) {
        static const int8_t k_cross[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        for (int i = 0; i < 4; i++) {
            gfx_line(c->fb, x0 + k_cross[i][0], y0 + k_cross[i][1], x1 + k_cross[i][0], y1 + k_cross[i][1], GFX_WHITE);
        }
    } else {
        gfx_line(c->fb, x0, y0, x1, y1, GFX_BLACK);
    }
}

void map_draw_lines(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const map_data_t *d, const map_style_t *s)
{
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    map_bounds_t b;
    map_view_bounds(v, &b);
    lines_ctx_t c = { .fb = fb, .area = area, .fast = fast_view(v), .halo = s->halo };
    if (s->halo) { /* every halo first, then every line, so no halo cuts a line already drawn */
        c.white = true;
        map_data_segments(d, &b, on_segment, &c);
    }
    c.white = false;
    map_data_segments(d, &b, on_segment, &c);
    fb->clip = saved;
}

typedef struct {
    gfx_fb_t *fb;
    gfx_rect_t area;
    fast_view_t fast;
    map_labels_t *l;
    int placed, tried, max;
} places_ctx_t;

/* A dot at (x, y) with its label; both or neither. */
static bool place(places_ctx_t *c, double lat, double lon, const char *text, bool airport)
{
    float fx, fy;
    fast_project(&c->fast, lat, lon, &fx, &fy);
    int x = c->area.x + (int)lroundf(fx), y = c->area.y + (int)lroundf(fy);
    gfx_rect_t mark = airport ? (gfx_rect_t){ (int16_t)(x - 4), (int16_t)(y - 2), 9, 4 }
                              : (gfx_rect_t){ (int16_t)(x - 2), (int16_t)(y - 2), 5, 5 };
    if (!inside(mark, c->area) || !is_free(c->l, mark)) {
        return false;
    }
    if (!map_label(c->fb, c->area, c->l, &gfx_font_sans_12, x, y, mark.w / 2 + 2, text)) {
        return false;
    }
    gfx_fill_rect(c->fb, mark, GFX_WHITE); /* the halo */
    if (airport) {
        gfx_fill_rect(c->fb, (gfx_rect_t){ (int16_t)(x - 3), (int16_t)(y - 1), 7, 2 }, GFX_BLACK); /* a runway */
    } else {
        gfx_fill_rect(c->fb, (gfx_rect_t){ (int16_t)(x - 1), (int16_t)(y - 1), 3, 3 }, GFX_BLACK);
    }
    map_labels_reserve(c->l, mark);
    return true;
}

static bool on_town(void *arg, const map_town_t *t)
{
    places_ctx_t *c = arg;
    if (place(c, t->lat, t->lon, t->name, false)) {
        c->placed++;
    }
    return c->placed < c->max && ++c->tried < TOWNS_TRIED;
}

static bool on_airport(void *arg, const map_airport_t *a)
{
    places_ctx_t *c = arg;
    if (a->iata[0] != '\0') {
        place(c, a->lat, a->lon, a->iata, true);
    }
    return ++c->tried < TOWNS_TRIED;
}

void map_draw_places(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const map_data_t *d,
                     const map_style_t *s, map_labels_t *l)
{
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    map_bounds_t b;
    map_view_bounds(v, &b);
    places_ctx_t c = { .fb = fb, .area = area, .fast = fast_view(v), .l = l,
                       .max = s->max_towns > 0 ? s->max_towns : 8 };
    if (s->airports) {
        map_data_airports(d, &b, on_airport, &c);
        c.tried = 0;
    }
    map_data_towns(d, &b, on_town, &c);
    fb->clip = saved;
}

void map_draw_home(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, int32_t lat_e4, int32_t lon_e4,
                   map_labels_t *l)
{
    double fx, fy;
    map_project(v, lat_e4 / 1e4, lon_e4 / 1e4, &fx, &fy);
    int x = area.x + (int)lround(fx), y = area.y + (int)lround(fy);
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    gfx_fill_circle(fb, x, y, HOME_R + 1, GFX_WHITE);
    gfx_circle(fb, x, y, HOME_R, GFX_BLACK);
    gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x - 1), (int16_t)(y - 1), 3, 3 }, GFX_BLACK);
    fb->clip = saved;
    map_labels_reserve(l, (gfx_rect_t){ (int16_t)(x - HOME_R - 1), (int16_t)(y - HOME_R - 1), 2 * HOME_R + 3,
                                        2 * HOME_R + 3 });
}

void map_draw_rings(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, double range_m, map_labels_t *l)
{
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    int cx = area.x + v->w / 2, cy = area.y + v->h / 2;
    double px_per_m = 1.0 / map_metres_per_px(v);
    for (int i = 1; i <= 2; i++) {
        double metres = range_m * i / 2;
        int r = (int)lround(metres * px_per_m);
        gfx_circle(fb, cx, cy, r, GFX_BLACK);
        char text[12];
        snprintf(text, sizeof(text), "%d km", (int)lround(metres / 1000));
        int lx = cx + (int)lround(r * 0.7071), ly = cy - (int)lround(r * 0.7071);
        map_label(fb, area, l, &gfx_font_sans_12, lx, ly, 2, text);
    }
    fb->clip = saved;
}
```


`components/map/CMakeLists.txt`:

```diff
--- a/components/map/CMakeLists.txt
+++ b/components/map/CMakeLists.txt
@@ -1,8 +1,9 @@
 # The map under both radars (spec §11.1): web-Mercator views, the built-in borders, coasts, towns
 # and airports, and their drawing. Pure C: also built on the host. tools/gen_map.sh packs
 # assets/map/map.bin, embedded here as _binary_map_bin_start.._end.
-idf_component_register(SRCS "map_view.c" "map_data.c"
+idf_component_register(SRCS "map_view.c" "map_data.c" "map_draw.c"
                        INCLUDE_DIRS "include"
+                       REQUIRES gfx
                        PRIV_REQUIRES util)
 
 target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../../assets/map/map.bin" BINARY)
```


- [ ] **Step 4: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_map_draw`
Expected: `8 Tests 0 Failures 0 Ignored`; `ctest --test-dir build-host`: `out of 57`, all passing.

- [ ] **Step 5: The firmware builds.** `tools/idf.sh build`: clean.

- [ ] **Step 6: AGENTS.md, and commit.** The `map/` line in §5.2 names the drawing too (AGENTS.md changes in the same commit, §8):

`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -221,7 +221,7 @@ components/
   webui/         HTTP server, REST API, embedded web assets
   weather/       Open-Meteo URLs, parsers, bands and levels; the HTTPS fetch
   png/           PNG reader for the radar images: palette and RGBA, row by row   [host]
-  map/           web-Mercator views; the built-in map (assets/map/map.bin)   [host]
+  map/           web-Mercator views; the built-in map (assets/map/map.bin) and its drawing   [host]
   ha_mqtt/       MQTT session, discovery, state, commands, field mappings   (planned)
   sync/          when syncs run and their retries, SNTP packets, the sync task
   audio/         codec control, tone/WAV/stream players, alarm ringing      (planned)
```


```bash
git add components/map test/host/test_map_draw.c test/host/CMakeLists.txt AGENTS.md
git commit -m "feat(map): draw the map with its labels, home and rings"
```


### Task 5: The weather radar's frames (`radar`)

**Files:**
- Create: `components/radar/CMakeLists.txt`, `components/radar/include/radar.h`, `components/radar/radar_frame.c`, `components/radar/radar_chmu.c`, `components/radar/radar_rainviewer.c`, `components/radar/radar_store.c`
- Create: `test/host/test_radar.c`, `test/host/fixtures/radar/rainviewer_maps.json`
- Modify: `test/host/CMakeLists.txt`
- Modify: `AGENTS.md` (the `radar/` line in §5.2)

**Interfaces:**
- Consumes: `png_decode()`, `png_mem_t`, `png_info_t`, `PNG_PALETTE`, `PNG_RGBA` (Task 1); `map_view_t`, `map_mercator()`, `map_pixel_to_mercator()`, `MAP_EARTH_R`, `MAP_TILE_PX` (Task 2); `util_json_depth()`; cJSON.
- Produces (Tasks 9, 11, 13):
  - `radar_frame_t` (`time`, `source`, `w`, `h`, `mx0`, `my0`, `scale`, `levels`: 2 bits a pixel), `radar_level_t` (`RADAR_NONE`, `RADAR_LIGHT`, `RADAR_MODERATE`, `RADAR_HEAVY`), `radar_source_t` (`RADAR_SOURCE_CHMU`, `RADAR_SOURCE_RAINVIEWER`); `radar_frame_alloc()`, `radar_frame_free()`, `radar_frame_level()`, `radar_frame_set()`.
  - ČHMÚ: `RADAR_CHMU_URL`, `RADAR_CHMU_STEP_S` 300, the grid's `RADAR_CHMU_W`/`H`/`SCALE`/`MX0`/`MY0`/`PNG_TOP`; `radar_chmu_name(t, out, size)`, `radar_chmu_newest(now)`, `radar_chmu_covers(lat, lon)`, `radar_chmu_decode(png, len, mem, time, out)`.
  - RainViewer: `RADAR_RV_INDEX_URL`, `RADAR_RV_MAX_ZOOM` 7, `RADAR_RV_TILES_MAX` 3; `radar_rv_index_t`, `radar_rv_tiles_t`; `radar_rv_parse_index()`, `radar_rv_tiles(v, t)`, `radar_rv_tile_url()`, `radar_rv_frame_alloc(f, t, time, mem)`, `radar_rv_decode_tile(png, len, mem, t, x, y, f)`.
  - `radar_render(fb, area, v, f)`, `radar_any_rain(v, f)`.
  - `radar_store_t` (`RADAR_LOOP_FRAMES` 12, newest last), `radar_store_init()`, `radar_store_put(s, f, keep)` (takes the frame), `radar_store_newest()`, `radar_store_has()`, `radar_store_missing()`, `radar_store_clear()`.

- [ ] **Step 1: Write the failing test.** The expected level counts come from Pillow and numpy over the same files, through ČHMÚ's colour scale (`scl/scl-dbz-mmh.png`) and RainViewer's Universal Blue table; the fixture `rainviewer_maps.json` is RainViewer's index of 2026-10-01.

`test/host/test_radar.c`:

```c
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "radar.h"
#include "unity.h"

/* The weather radar (spec §11.2). The level counts come from Pillow and numpy over the same files,
 * with ČHMÚ's colour scale (scl/scl-dbz-mmh.png) and RainViewer's Universal Blue table. */

void setUp(void) {}
void tearDown(void) {}

static uint8_t *load(const char *name, size_t *len)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*len + 1);
    TEST_ASSERT_EQUAL_size_t(*len, fread(data, 1, *len, f));
    data[*len] = '\0';
    fclose(f);
    return data;
}

static void count_levels(const radar_frame_t *f, long out[4])
{
    memset(out, 0, 4 * sizeof(long));
    for (int y = 0; y < f->h; y++) {
        for (int x = 0; x < f->w; x++) {
            out[radar_frame_level(f, x, y)]++;
        }
    }
}

#define T_20260924_1120 1790248800 /* 2026-09-24 11:20 UTC */

static void test_chmu_names_and_the_newest_step(void)
{
    char name[64];
    radar_chmu_name(T_20260924_1120, name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("pacz2gmaps3.z_max3d.20260924.1120.0.png", name);
    TEST_ASSERT_EQUAL_INT64(T_20260924_1120, radar_chmu_newest(T_20260924_1120 + 7 * 60 + 30)); /* 11:27:30 */
    TEST_ASSERT_EQUAL_INT64(T_20260924_1120, radar_chmu_newest(T_20260924_1120 + 5 * 60));      /* 11:25:00 */
    TEST_ASSERT_EQUAL_INT64(T_20260924_1120 - 300, radar_chmu_newest(T_20260924_1120 + 4 * 60 + 59));
}

static void test_chmu_covers_czechia_and_its_rim(void)
{
    TEST_ASSERT_TRUE(radar_chmu_covers(49.1951, 16.6068));  /* Brno */
    TEST_ASSERT_TRUE(radar_chmu_covers(48.3069, 14.2858));  /* Linz */
    TEST_ASSERT_TRUE(radar_chmu_covers(48.1372, 11.5756));  /* München */
    TEST_ASSERT_FALSE(radar_chmu_covers(52.5200, 13.4050)); /* Berlin, north of it */
    TEST_ASSERT_FALSE(radar_chmu_covers(47.4979, 19.0402)); /* Budapest, south of it */
    TEST_ASSERT_FALSE(radar_chmu_covers(49.8397, 24.0297)); /* Lviv, east of it */
}

static void test_a_rainy_chmu_frame_decodes_to_its_levels(void)
{
    size_t len;
    uint8_t *png = load("png/chmi_rain.png", &len);
    radar_frame_t f;
    TEST_ASSERT_EQUAL_INT(PNG_OK, radar_chmu_decode(png, len, NULL, T_20260924_1120, &f));
    TEST_ASSERT_EQUAL_UINT16(RADAR_CHMU_W, f.w);
    TEST_ASSERT_EQUAL_UINT16(RADAR_CHMU_H, f.h);
    TEST_ASSERT_EQUAL_UINT8(RADAR_SOURCE_CHMU, f.source);
    TEST_ASSERT_EQUAL_UINT32(T_20260924_1120, f.time);
    long n[4];
    count_levels(&f, n);
    TEST_ASSERT_EQUAL_INT32(192493, n[RADAR_NONE]);
    TEST_ASSERT_EQUAL_INT32(30113, n[RADAR_LIGHT]);
    TEST_ASSERT_EQUAL_INT32(3152, n[RADAR_MODERATE]);
    TEST_ASSERT_EQUAL_INT32(286, n[RADAR_HEAVY]);
    TEST_ASSERT_EQUAL_INT(RADAR_HEAVY, radar_frame_level(&f, 152, 67)); /* the first heavy pixel, 50.87 N 13.40 E */
    radar_frame_free(&f, NULL);
    free(png);

    png = load("png/chmi_dry.png", &len);
    TEST_ASSERT_EQUAL_INT(PNG_OK, radar_chmu_decode(png, len, NULL, T_20260924_1120, &f));
    count_levels(&f, n);
    TEST_ASSERT_EQUAL_INT32(RADAR_CHMU_W * RADAR_CHMU_H, n[RADAR_NONE]); /* the black overlay lines are no rain */
    radar_frame_free(&f, NULL);
    free(png);
}

/* A frame on ČHMÚ's grid with one square of the given level around a place. */
static void square_at(radar_frame_t *f, double lat, double lon, int half, radar_level_t level)
{
    TEST_ASSERT_TRUE(radar_frame_alloc(f, RADAR_CHMU_W, RADAR_CHMU_H, NULL));
    f->mx0 = RADAR_CHMU_MX0;
    f->my0 = RADAR_CHMU_MY0;
    f->scale = RADAR_CHMU_SCALE;
    double mx, my;
    map_mercator(lat, lon, &mx, &my);
    int gx = (int)((mx - f->mx0) / f->scale), gy = (int)((f->my0 - my) / f->scale);
    for (int y = gy - half; y <= gy + half; y++) {
        for (int x = gx - half; x <= gx + half; x++) {
            radar_frame_set(f, x, y, level);
        }
    }
}

static int ink(const gfx_fb_t *fb, gfx_rect_t r)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            n += gfx_get_pixel(fb, x, y);
        }
    }
    return n;
}

static void test_rain_lands_where_it_falls_in_any_view(void)
{
    static uint8_t buf[400 * 300 / 8];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    for (int z = 0; z < 2; z++) {
        double zoom = z == 0 ? 6.5 : 8.0;
        static const radar_level_t k_levels[3] = { RADAR_HEAVY, RADAR_MODERATE, RADAR_LIGHT };
        static const int k_ink[3] = { 64, 32, 16 }; /* of an 8 x 8 square: solid, half, a quarter */
        for (int i = 0; i < 3; i++) {
            radar_frame_t f;
            square_at(&f, 49.1951, 16.6068, 8, k_levels[i]); /* 17 grid pixels around Brno */
            map_view_t v;
            map_view_init(&v, 491951, 166068, zoom, 400, 280);
            gfx_clear(&fb, GFX_WHITE);
            radar_render(&fb, (gfx_rect_t){ 0, 20, 400, 280 }, &v, &f);
            TEST_ASSERT_EQUAL_INT_MESSAGE(k_ink[i], ink(&fb, (gfx_rect_t){ 196, 156, 8, 8 }), "at the centre");
            TEST_ASSERT_EQUAL_INT(0, ink(&fb, (gfx_rect_t){ 0, 20, 60, 60 })); /* dry far away */
            TEST_ASSERT_TRUE(radar_any_rain(&v, &f));
            radar_frame_free(&f, NULL);
        }
    }
}

static void test_outside_the_data_the_edge_is_dotted(void)
{
    static uint8_t buf[400 * 300 / 8];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    gfx_clear(&fb, GFX_WHITE);
    radar_frame_t f;
    square_at(&f, 49.1951, 16.6068, 2, RADAR_HEAVY);
    map_view_t v;
    map_view_init(&v, 479000, 166068, 6.5, 400, 280); /* centred south of ČHMÚ's data: its south edge crosses */
    radar_render(&fb, (gfx_rect_t){ 0, 20, 400, 280 }, &v, &f);
    double x, y;
    map_project(&v, 48.047275, 16.6068, &x, &y); /* the south edge of the data */
    int row = 20 + (int)lround(y);
    int dots = ink(&fb, (gfx_rect_t){ 0, (int16_t)(row - 1), 400, 3 });
    TEST_ASSERT_TRUE_MESSAGE(dots > 100 && dots < 300, "a dotted line, every other pixel");
    TEST_ASSERT_FALSE(radar_any_rain(&(map_view_t){ .cx = 0, .cy = 0, .zoom = 6.5, .w = 400, .h = 280 }, &f));
    radar_frame_free(&f, NULL);
}

static void test_the_rainviewer_index_names_its_frames(void)
{
    size_t len;
    char *json = (char *)load("radar/rainviewer_maps.json", &len);
    radar_rv_index_t idx;
    TEST_ASSERT_TRUE(radar_rv_parse_index(json, len, &idx));
    TEST_ASSERT_EQUAL_STRING("https://tilecache.rainviewer.com", idx.host);
    TEST_ASSERT_EQUAL_INT(13, idx.count);
    TEST_ASSERT_EQUAL_UINT32(1790881800, idx.frames[0].time);
    TEST_ASSERT_EQUAL_STRING("/v2/radar/c15f405b89f4", idx.frames[12].path);
    TEST_ASSERT_EQUAL_UINT32(1790889000, idx.frames[12].time);
    TEST_ASSERT_FALSE(radar_rv_parse_index("{\"radar\":{\"past\":[]}}", 21, &idx));
    TEST_ASSERT_FALSE(radar_rv_parse_index("[]", 2, &idx));
    free(json);
}

static void test_rainviewer_tiles_cover_the_view(void)
{
    map_view_t v;
    map_view_init(&v, 491951, 166068, 6.5, 400, 280);
    radar_rv_tiles_t t;
    radar_rv_tiles(&v, &t);
    TEST_ASSERT_EQUAL_INT(6, t.z);
    TEST_ASSERT_EQUAL_INT(34, t.x0);
    TEST_ASSERT_EQUAL_INT(21, t.y0);
    TEST_ASSERT_EQUAL_INT(2, t.nx);
    TEST_ASSERT_EQUAL_INT(2, t.ny);
    map_view_init(&v, 491951, 166068, 9.0, 400, 280);
    radar_rv_tiles(&v, &t);
    TEST_ASSERT_EQUAL_INT(RADAR_RV_MAX_ZOOM, t.z); /* nothing above zoom 7: enlarged */
    TEST_ASSERT_TRUE(t.nx <= 2 && t.ny <= 2);
    char url[160];
    radar_rv_tile_url(url, sizeof(url), "https://tilecache.rainviewer.com", "/v2/radar/c15f405b89f4", 7, 69, 43);
    TEST_ASSERT_EQUAL_STRING("https://tilecache.rainviewer.com/v2/radar/c15f405b89f4/256/7/69/43/2/0_0.png", url);
}

static void test_a_rainviewer_tile_decodes_by_its_colour_table(void)
{
    size_t len;
    uint8_t *png = load("png/rainviewer_tile.png", &len); /* zoom 3, tile (4, 2) */
    radar_rv_tiles_t t = { .z = 3, .x0 = 4, .y0 = 2, .nx = 1, .ny = 1 };
    radar_frame_t f;
    TEST_ASSERT_TRUE(radar_rv_frame_alloc(&f, &t, 1790889000, NULL));
    TEST_ASSERT_EQUAL_UINT16(256, f.w);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2 * 3.14159265358979323846 * MAP_EARTH_R / (256 * 8), f.scale);
    TEST_ASSERT_EQUAL_INT(PNG_OK, radar_rv_decode_tile(png, len, NULL, &t, 4, 2, &f));
    long n[4];
    count_levels(&f, n);
    TEST_ASSERT_EQUAL_INT32(62286, n[RADAR_NONE]);
    TEST_ASSERT_EQUAL_INT32(2575, n[RADAR_LIGHT]);
    TEST_ASSERT_EQUAL_INT32(629, n[RADAR_MODERATE]);
    TEST_ASSERT_EQUAL_INT32(46, n[RADAR_HEAVY]);
    radar_frame_free(&f, NULL);
    free(png);
}

static int s_live;

static void *counted_alloc(size_t n)
{
    s_live++;
    return calloc(1, n);
}

static void counted_free(void *p)
{
    if (p != NULL) {
        s_live--;
    }
    free(p);
}

static void test_the_store_keeps_frames_in_order_and_frees_the_rest(void)
{
    const png_mem_t mem = { counted_alloc, counted_free };
    radar_store_t s;
    radar_store_init(&s, &mem);
    s_live = 0;
    const uint32_t t0 = 1790880000;
    static const uint32_t k_times[] = { 600, 300, 900, 600 }; /* out of order, and one twice */
    for (size_t i = 0; i < sizeof(k_times) / sizeof(k_times[0]); i++) {
        radar_frame_t f;
        TEST_ASSERT_TRUE(radar_frame_alloc(&f, 8, 8, &mem));
        f.time = t0 + k_times[i];
        radar_store_put(&s, &f, RADAR_LOOP_FRAMES);
    }
    TEST_ASSERT_EQUAL_INT(3, s.count);
    TEST_ASSERT_EQUAL_INT(3, s_live); /* the repeated time replaced its frame */
    TEST_ASSERT_EQUAL_UINT32(t0 + 300, s.frames[0].time);
    TEST_ASSERT_EQUAL_UINT32(t0 + 900, radar_store_newest(&s)->time);
    TEST_ASSERT_TRUE(radar_store_has(&s, t0 + 600));

    uint32_t missing[12];
    int n = radar_store_missing(&s, t0 + 900, 300, missing, 12);
    TEST_ASSERT_EQUAL_INT(9, n); /* the hour has 12 steps; 3 are kept */
    TEST_ASSERT_EQUAL_UINT32(t0, missing[0]); /* newest first */
    TEST_ASSERT_EQUAL_UINT32(t0 - 8 * 300, missing[8]);

    radar_frame_t f;
    TEST_ASSERT_TRUE(radar_frame_alloc(&f, 8, 8, &mem));
    f.time = t0 + 1200;
    radar_store_put(&s, &f, 1); /* outside sync mode always: only the newest stays */
    TEST_ASSERT_EQUAL_INT(1, s.count);
    TEST_ASSERT_EQUAL_INT(1, s_live);
    TEST_ASSERT_EQUAL_UINT32(t0 + 1200, radar_store_newest(&s)->time);
    radar_store_clear(&s);
    TEST_ASSERT_EQUAL_INT(0, s_live);
    TEST_ASSERT_NULL(radar_store_newest(&s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_chmu_names_and_the_newest_step);
    RUN_TEST(test_chmu_covers_czechia_and_its_rim);
    RUN_TEST(test_a_rainy_chmu_frame_decodes_to_its_levels);
    RUN_TEST(test_rain_lands_where_it_falls_in_any_view);
    RUN_TEST(test_outside_the_data_the_edge_is_dotted);
    RUN_TEST(test_the_rainviewer_index_names_its_frames);
    RUN_TEST(test_rainviewer_tiles_cover_the_view);
    RUN_TEST(test_a_rainviewer_tile_decodes_by_its_colour_table);
    RUN_TEST(test_the_store_keeps_frames_in_order_and_frees_the_rest);
    return UNITY_END();
}
```


`test/host/fixtures/radar/rainviewer_maps.json`:

```json
{"version":"2.0","generated":1790889024,"host":"https://tilecache.rainviewer.com","radar":{"past":[{"time":1790881800,"path":"/v2/radar/10b349f7f2fd"},{"time":1790882400,"path":"/v2/radar/09aa952e970a"},{"time":1790883000,"path":"/v2/radar/84ba2d7f32ad"},{"time":1790883600,"path":"/v2/radar/0d283b6b58ab"},{"time":1790884200,"path":"/v2/radar/6b3d6337bfe4"},{"time":1790884800,"path":"/v2/radar/06db21a05dc8"},{"time":1790885400,"path":"/v2/radar/aa342d9571cb"},{"time":1790886000,"path":"/v2/radar/036bb7923432"},{"time":1790886600,"path":"/v2/radar/975bb9c8918f"},{"time":1790887200,"path":"/v2/radar/f83ce53cc8e5"},{"time":1790887800,"path":"/v2/radar/d858458368f1"},{"time":1790888400,"path":"/v2/radar/04cbb951abe6"},{"time":1790889000,"path":"/v2/radar/c15f405b89f4"}],"nowcast":[]},"satellite":{"infrared":[]}}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -71,6 +71,14 @@ target_include_directories(map PUBLIC ${REPO_ROOT}/components/map/include)
 target_compile_options(map PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(map PUBLIC gfx util m)
 
+# radar: the decoders, the frame store and the drawing build on the host; the fetching does not.
+add_library(radar_logic STATIC ${REPO_ROOT}/components/radar/radar_frame.c
+            ${REPO_ROOT}/components/radar/radar_chmu.c ${REPO_ROOT}/components/radar/radar_rainviewer.c
+            ${REPO_ROOT}/components/radar/radar_store.c)
+target_include_directories(radar_logic PUBLIC ${REPO_ROOT}/components/radar/include)
+target_compile_options(radar_logic PRIVATE ${REFLBO_WARNINGS})
+target_link_libraries(radar_logic PUBLIC png map gfx PRIVATE cjson util m)
+
 # sync: the planner (modes, quiet hours, retries) and the SNTP packets build on the host; the task does not.
 add_library(sync_logic STATIC ${REPO_ROOT}/components/sync/sync_plan.c ${REPO_ROOT}/components/sync/sync_ntp.c)
 target_include_directories(sync_logic PUBLIC ${REPO_ROOT}/components/sync/include)
@@ -201,6 +209,8 @@ reflbo_host_test(test_map_data map)
 target_compile_definitions(test_map_data PRIVATE MAP_BIN="${REPO_ROOT}/assets/map/map.bin")
 reflbo_host_test(test_map_draw map)
 target_compile_definitions(test_map_draw PRIVATE MAP_BIN="${REPO_ROOT}/assets/map/map.bin")
+reflbo_host_test(test_radar radar_logic)
+target_compile_definitions(test_radar PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
 reflbo_host_test(test_gesture board_logic)
 reflbo_host_test(test_shtc3_codec sensors_logic)
 reflbo_host_test(test_battery_model sensors_logic)
```


- [ ] **Step 2: Run it to see it fail.**

Run: `cmake -S test/host -B build-host -G Ninja`
Expected: `CMake Error at CMakeLists.txt:… (add_library): Cannot find source file:` naming `components/radar/radar_frame.c`.

- [ ] **Step 3: Frames, their drawing and the header.** Light rain inks one pixel in four (x and y even), moderate a checkerboard, heavy everything; where the frame has no data, its edge is dotted. A render maps each screen column and row to the frame's grid once, into two tables of `int16_t` (static, 4 KB, off the app task's stack), so a pixel costs two lookups and no floating point: the loop draws three frames a second.

`components/radar/include/radar.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "gfx.h"
#include "map_view.h"
#include "png.h"

/*
 * The weather radar (spec §11.2): ČHMÚ's composite where it covers the view, RainViewer's tiles
 * elsewhere, both decoded once into a frame of three rain levels and drawn into any view. Pure C,
 * host-buildable; the fetching is in the sync task.
 */

typedef enum {
    RADAR_NONE,
    RADAR_LIGHT,    /* from about 20 dBZ */
    RADAR_MODERATE, /* from about 35 dBZ (ČHMÚ: 36) */
    RADAR_HEAVY,    /* from about 45 dBZ (ČHMÚ: 44) */
} radar_level_t;

typedef enum {
    RADAR_SOURCE_CHMU,
    RADAR_SOURCE_RAINVIEWER,
} radar_source_t;

/* A frame: rain levels on a grid of web-Mercator pixels. */
typedef struct {
    uint32_t time;   /* UTC seconds: the end of ČHMÚ's 5 minutes, RainViewer's frame time */
    uint8_t source;  /* radar_source_t */
    uint16_t w, h;   /* grid pixels */
    double mx0, my0; /* web Mercator metres of the grid's top-left corner */
    double scale;    /* metres per grid pixel */
    uint8_t *levels; /* w x h, 2 bits a pixel, 4 to a byte, the first in the top bits */
} radar_frame_t;

/* The grid zeroed (no rain), from `mem` (NULL: malloc); false without memory. */
bool radar_frame_alloc(radar_frame_t *f, uint16_t w, uint16_t h, const png_mem_t *mem);
void radar_frame_free(radar_frame_t *f, const png_mem_t *mem);
radar_level_t radar_frame_level(const radar_frame_t *f, int x, int y); /* RADAR_NONE outside */
void radar_frame_set(radar_frame_t *f, int x, int y, radar_level_t level);

/* ---- ČHMÚ's composite of maximum reflectivity (CC BY 4.0) ---- */

#define RADAR_CHMU_URL "https://opendata.chmi.cz/meteorology/weather/radar/composite/maxz/png/"
#define RADAR_CHMU_STEP_S 300
/* Its data area, from the HDF5 composite's corners (ODIM `where`): 598 x 378 pixels of 1555.7 m. */
#define RADAR_CHMU_W 598
#define RADAR_CHMU_H 378
#define RADAR_CHMU_SCALE 1555.7
#define RADAR_CHMU_MX0 1254222.15 /* 11.266869 E */
#define RADAR_CHMU_MY0 6702777.85 /* 51.458369 N */
#define RADAR_CHMU_PNG_TOP 82     /* the PNG's rows above the data: title and side projection */

/* The file name of the frame that ends at `t` (UTC), "pacz2gmaps3.z_max3d.20260924.1120.0.png". */
void radar_chmu_name(time_t t, char *out, size_t size);
/* The newest frame to ask for at `now`: the latest 5-minute step at least 5 minutes old. */
time_t radar_chmu_newest(time_t now);
/* The point lies inside ČHMÚ's data area. */
bool radar_chmu_covers(double lat, double lon);
/* ČHMÚ's PNG into `out`: its data area, each palette colour to its level by ČHMÚ's colour scale. */
png_err_t radar_chmu_decode(const uint8_t *png, size_t len, const png_mem_t *mem, uint32_t time,
                            radar_frame_t *out);

/* ---- RainViewer (personal and educational use) ---- */

#define RADAR_RV_INDEX_URL "https://api.rainviewer.com/public/weather-maps.json"
#define RADAR_RV_MAX_ZOOM 7
#define RADAR_RV_FRAMES 16
#define RADAR_RV_TILES_MAX 3 /* a side: at most 3 x 3 tiles a view */

typedef struct {
    uint32_t time;
    char path[48]; /* "/v2/radar/c15f405b89f4" */
} radar_rv_frame_t;

typedef struct {
    char host[64]; /* "https://tilecache.rainviewer.com" */
    int count;     /* the past frames, oldest first */
    radar_rv_frame_t frames[RADAR_RV_FRAMES];
} radar_rv_index_t;

typedef struct {
    int z;      /* RainViewer's zoom: the view's, rounded down, at most 7 */
    int x0, y0; /* the top-left tile */
    int nx, ny; /* tiles across and down */
} radar_rv_tiles_t;

/* weather-maps.json; false if it isn't one, or names no frames. */
bool radar_rv_parse_index(const char *json, size_t len, radar_rv_index_t *out);
/* The tiles that cover the view. */
void radar_rv_tiles(const map_view_t *v, radar_rv_tiles_t *t);
/* A tile's URL, in RainViewer's one colour scheme and without smoothing, so its colours are exact. */
int radar_rv_tile_url(char *out, size_t size, const char *host, const char *path, int z, int x, int y);
/* A frame for those tiles: its grid allocated, its place set. */
bool radar_rv_frame_alloc(radar_frame_t *f, const radar_rv_tiles_t *t, uint32_t time, const png_mem_t *mem);
/* One tile's PNG into the frame at tile (x, y) of the world. */
png_err_t radar_rv_decode_tile(const uint8_t *png, size_t len, const png_mem_t *mem, const radar_rv_tiles_t *t,
                               int x, int y, radar_frame_t *f);

/* ---- drawing ---- */

/* The frame's rain in `area`, whose top-left is the view's (0, 0): light as one pixel in four,
 * moderate as a checkerboard, heavy solid; where the frame has no data, the edge of its data
 * dotted. Draws over what is there, so the map goes on top. */
void radar_render(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const radar_frame_t *f);
/* Any rain inside the view. */
bool radar_any_rain(const map_view_t *v, const radar_frame_t *f);

/* ---- the frames kept ---- */

#define RADAR_LOOP_FRAMES 12 /* the last hour at ČHMÚ's 5 minutes (D28) */

typedef struct {
    radar_frame_t frames[RADAR_LOOP_FRAMES]; /* oldest first */
    int count;
    png_mem_t mem; /* where their grids came from */
} radar_store_t;

void radar_store_init(radar_store_t *s, const png_mem_t *mem);
/* Takes `f`, its grid with it: in time order, replacing a frame of the same time; then keeps the
 * `keep` newest (1 outside sync mode `always`, up to RADAR_LOOP_FRAMES) and frees the rest. */
void radar_store_put(radar_store_t *s, radar_frame_t *f, int keep);
const radar_frame_t *radar_store_newest(const radar_store_t *s); /* NULL when empty */
bool radar_store_has(const radar_store_t *s, uint32_t time);
/* The frame times of the hour before `newest` (in steps of `step_s`) not kept yet, newest first. */
int radar_store_missing(const radar_store_t *s, uint32_t newest, uint32_t step_s, uint32_t *out, int max);
void radar_store_clear(radar_store_t *s);
```


`components/radar/radar_frame.c`:

```c
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "radar.h"

#define PI 3.14159265358979323846
#define VIEW_W_MAX 1024 /* screen columns a render maps at most */

static size_t grid_bytes(uint16_t w, uint16_t h)
{
    return ((size_t)w * h + 3) / 4;
}

bool radar_frame_alloc(radar_frame_t *f, uint16_t w, uint16_t h, const png_mem_t *mem)
{
    memset(f, 0, sizeof(*f));
    size_t n = grid_bytes(w, h);
    f->levels = mem != NULL && mem->alloc != NULL ? mem->alloc(n) : malloc(n);
    if (f->levels == NULL) {
        return false;
    }
    memset(f->levels, 0, n);
    f->w = w;
    f->h = h;
    return true;
}

void radar_frame_free(radar_frame_t *f, const png_mem_t *mem)
{
    if (mem != NULL && mem->release != NULL) {
        mem->release(f->levels);
    } else {
        free(f->levels);
    }
    f->levels = NULL;
}

radar_level_t radar_frame_level(const radar_frame_t *f, int x, int y)
{
    if (f->levels == NULL || x < 0 || y < 0 || x >= f->w || y >= f->h) {
        return RADAR_NONE;
    }
    size_t i = (size_t)y * f->w + x;
    return (radar_level_t)(f->levels[i / 4] >> (6 - 2 * (i % 4)) & 3);
}

void radar_frame_set(radar_frame_t *f, int x, int y, radar_level_t level)
{
    if (f->levels == NULL || x < 0 || y < 0 || x >= f->w || y >= f->h) {
        return;
    }
    size_t i = (size_t)y * f->w + x;
    int shift = 6 - 2 * (int)(i % 4);
    f->levels[i / 4] = (uint8_t)((f->levels[i / 4] & ~(3 << shift)) | (level & 3) << shift);
}

static bool inks(radar_level_t level, int x, int y)
{
    switch (level) {
    case RADAR_LIGHT:
        return (x % 2 == 0) && (y % 2 == 0); /* one pixel in four */
    case RADAR_MODERATE:
        return (x + y) % 2 == 0; /* a checkerboard */
    case RADAR_HEAVY:
        return true;
    default:
        return false;
    }
}

/* Web Mercator metres per screen pixel, and the metres of the view's pixel (0, 0)'s centre. */
static void view_metres(const map_view_t *v, double *step, double *mx, double *my)
{
    *step = 2 * PI * MAP_EARTH_R / (MAP_TILE_PX * pow(2.0, v->zoom));
    map_pixel_to_mercator(v, 0.5, 0.5, mx, my);
}

/* Where a view's pixels fall in a frame's grid, worked out once a render: each screen column's grid
 * column and each row's grid row, -1 off the frame. The S3 does doubles in software, and a pixel's
 * own would cost most of a second a frame. */
typedef struct {
    int w, h;
    int16_t cols[VIEW_W_MAX], rows[VIEW_W_MAX];
} grid_map_t;

static void grid_map(const map_view_t *v, const radar_frame_t *f, int w, int h, grid_map_t *g)
{
    double step, mx0, my0;
    view_metres(v, &step, &mx0, &my0);
    g->w = w < VIEW_W_MAX ? w : VIEW_W_MAX;
    g->h = h < VIEW_W_MAX ? h : VIEW_W_MAX;
    for (int sx = 0; sx < g->w; sx++) {
        int gx = (int)floor((mx0 + sx * step - f->mx0) / f->scale);
        g->cols[sx] = (int16_t)(gx >= 0 && gx < f->w ? gx : -1);
    }
    for (int sy = 0; sy < g->h; sy++) {
        int gy = (int)floor((f->my0 - (my0 - sy * step)) / f->scale);
        g->rows[sy] = (int16_t)(gy >= 0 && gy < f->h ? gy : -1);
    }
}

void radar_render(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const radar_frame_t *f)
{
    if (f == NULL || f->levels == NULL) {
        return;
    }
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    static grid_map_t g; /* 4 KB: off the app task's stack */
    grid_map(v, f, area.w, area.h, &g);
    for (int sy = 0; sy < g.h; sy++) {
        for (int sx = 0; g.rows[sy] >= 0 && sx < g.w; sx++) {
            int x = area.x + sx, y = area.y + sy;
            if (g.cols[sx] >= 0 && inks(radar_frame_level(f, g.cols[sx], g.rows[sy]), x, y)) {
                gfx_pixel(fb, x, y, GFX_BLACK);
            }
        }
    }
    double step, mx0, my0;
    view_metres(v, &step, &mx0, &my0);
    /* the edge of the frame's data, dotted, where it crosses the view */
    double left = (f->mx0 - mx0) / step, right = (f->mx0 + f->w * f->scale - mx0) / step;
    double top = (my0 - f->my0) / step, bottom = (my0 - (f->my0 - f->h * f->scale)) / step;
    int l = (int)lround(left), r = (int)lround(right), t = (int)lround(top), b = (int)lround(bottom);
    for (int sx = l < 0 ? 0 : l; sx <= r && sx < area.w; sx += 2) {
        if (t >= 0 && t < area.h) {
            gfx_pixel(fb, area.x + sx, area.y + t, GFX_BLACK);
        }
        if (b >= 0 && b < area.h) {
            gfx_pixel(fb, area.x + sx, area.y + b, GFX_BLACK);
        }
    }
    for (int sy = t < 0 ? 0 : t; sy <= b && sy < area.h; sy += 2) {
        if (l >= 0 && l < area.w) {
            gfx_pixel(fb, area.x + l, area.y + sy, GFX_BLACK);
        }
        if (r >= 0 && r < area.w) {
            gfx_pixel(fb, area.x + r, area.y + sy, GFX_BLACK);
        }
    }
    fb->clip = saved;
}

bool radar_any_rain(const map_view_t *v, const radar_frame_t *f)
{
    if (f == NULL || f->levels == NULL) {
        return false;
    }
    static grid_map_t g;
    grid_map(v, f, v->w, v->h, &g);
    for (int sy = 0; sy < g.h; sy++) {
        for (int sx = 0; g.rows[sy] >= 0 && sx < g.w; sx++) {
            if (g.cols[sx] >= 0 && radar_frame_level(f, g.cols[sx], g.rows[sy]) != RADAR_NONE) {
                return true;
            }
        }
    }
    return false;
}
```


- [ ] **Step 4: ČHMÚ's composite.** The file name from the clock (the newest 5-min step at least 5 min old); the palette's colours matched to the scale's classes (light 20–35 dBZ, moderate 36–43, heavy from 44); only the data area kept (ruling).

`components/radar/radar_chmu.c`:

```c
#define _POSIX_C_SOURCE 200809L /* gmtime_r */

#include <stdio.h>
#include <string.h>

#include "radar.h"

/* ČHMÚ's colour scale (radar/scl/scl-dbz-mmh.png): each class's colour and its lowest dBZ, 4 dBZ
 * apart; checked on 2026-10-01 against the HDF5 composite of the same frame. */
static const struct {
    uint8_t rgb[3];
    int8_t dbz;
} k_scale[] = {
    { { 252, 252, 252 }, 60 }, { { 160, 0, 0 }, 56 },   { { 252, 0, 0 }, 52 },   { { 252, 88, 0 }, 48 },
    { { 252, 132, 0 }, 44 },   { { 252, 176, 0 }, 40 }, { { 224, 220, 0 }, 36 }, { { 156, 220, 0 }, 32 },
    { { 52, 216, 0 }, 28 },    { { 0, 188, 0 }, 24 },   { { 0, 160, 0 }, 20 },   { { 0, 108, 192 }, 16 },
    { { 0, 0, 252 }, 12 },     { { 48, 0, 168 }, 8 },   { { 56, 0, 112 }, 4 },
};

static radar_level_t level_of_dbz(int dbz)
{
    return dbz >= 44 ? RADAR_HEAVY : dbz >= 36 ? RADAR_MODERATE : dbz >= 20 ? RADAR_LIGHT : RADAR_NONE;
}

void radar_chmu_name(time_t t, char *out, size_t size)
{
    struct tm u;
    gmtime_r(&t, &u);
    snprintf(out, size, "pacz2gmaps3.z_max3d.%04d%02d%02d.%02d%02d.0.png", u.tm_year + 1900, u.tm_mon + 1,
             u.tm_mday, u.tm_hour, u.tm_min);
}

time_t radar_chmu_newest(time_t now)
{
    time_t t = now - RADAR_CHMU_STEP_S;
    return t - t % RADAR_CHMU_STEP_S;
}

bool radar_chmu_covers(double lat, double lon)
{
    double mx, my;
    map_mercator(lat, lon, &mx, &my);
    return mx >= RADAR_CHMU_MX0 && mx < RADAR_CHMU_MX0 + RADAR_CHMU_W * RADAR_CHMU_SCALE &&
           my <= RADAR_CHMU_MY0 && my > RADAR_CHMU_MY0 - RADAR_CHMU_H * RADAR_CHMU_SCALE;
}

typedef struct {
    radar_frame_t *f;
    bool lut_ready;
    uint8_t lut[256];
} chmu_ctx_t;

static void build_lut(chmu_ctx_t *c, const png_info_t *info)
{
    memset(c->lut, RADAR_NONE, sizeof(c->lut));
    for (int i = 0; i < info->palette_size; i++) {
        if (info->palette[i][3] == 0) {
            continue; /* no echo */
        }
        for (size_t k = 0; k < sizeof(k_scale) / sizeof(k_scale[0]); k++) {
            if (memcmp(info->palette[i], k_scale[k].rgb, 3) == 0) {
                c->lut[i] = (uint8_t)level_of_dbz(k_scale[k].dbz);
                break;
            }
        }
    }
    c->lut_ready = true;
}

static void on_row(void *ctx, int y, const uint8_t *row, const png_info_t *info)
{
    chmu_ctx_t *c = ctx;
    if (!c->lut_ready) {
        build_lut(c, info);
    }
    int gy = y - RADAR_CHMU_PNG_TOP;
    if (gy < 0 || gy >= c->f->h) {
        return;
    }
    for (int x = 0; x < c->f->w && x < info->width; x++) {
        if (c->lut[row[x]] != RADAR_NONE) {
            radar_frame_set(c->f, x, gy, (radar_level_t)c->lut[row[x]]);
        }
    }
}

png_err_t radar_chmu_decode(const uint8_t *png, size_t len, const png_mem_t *mem, uint32_t time,
                            radar_frame_t *out)
{
    if (!radar_frame_alloc(out, RADAR_CHMU_W, RADAR_CHMU_H, mem)) {
        return PNG_ERR_NO_MEMORY;
    }
    out->time = time;
    out->source = RADAR_SOURCE_CHMU;
    out->mx0 = RADAR_CHMU_MX0;
    out->my0 = RADAR_CHMU_MY0;
    out->scale = RADAR_CHMU_SCALE;
    static chmu_ctx_t c; /* 260 bytes; off the sync task's stack */
    c.f = out;
    c.lut_ready = false;
    static png_info_t info;
    png_err_t err = png_decode(png, len, mem, &info, on_row, &c);
    if (err == PNG_OK && (info.type != PNG_PALETTE || info.width < RADAR_CHMU_W ||
                          info.height < RADAR_CHMU_PNG_TOP + RADAR_CHMU_H)) {
        err = PNG_ERR_UNSUPPORTED; /* not the composite's layout */
    }
    if (err != PNG_OK) {
        radar_frame_free(out, mem);
    }
    return err;
}
```


- [ ] **Step 5: RainViewer.** The index's newest 16 frames; the tiles that cover a view at zoom 7 at most, 3 × 3 at most; each tile's colours through the table (light below 35 dBZ, moderate below 45, heavy from there), without smoothing (ruling: `2/0_0.png`).

`components/radar/radar_rainviewer.c`:

```c
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "radar.h"
#include "util_json.h"

#define PI 3.14159265358979323846
#define JSON_MAX_DEPTH 8 /* weather-maps.json is 4 deep */

/* RainViewer's Universal Blue colours from 20 dBZ up (rainviewer.com/files/rainviewer_api_colors_table.csv,
 * checked 2026-10-01; every scheme number returns it since 2026), sorted by RGB, with their level:
 * light below 35 dBZ, moderate below 45, heavy from there. */
static const struct {
    uint32_t rgb;
    uint8_t level;
} k_colours[] = {
    { 0x004768, 1 }, { 0x004a70, 1 }, { 0x004e78, 1 }, { 0x005180, 1 }, { 0x005588, 1 }, { 0x005b8e, 1 },
    { 0x006295, 1 }, { 0x00699c, 1 }, { 0x0070a3, 1 }, { 0x0077aa, 1 }, { 0x007fb4, 1 }, { 0x0088bf, 1 },
    { 0x0091ca, 1 }, { 0x009ad5, 1 }, { 0x00a3e0, 1 }, { 0x00ff00, 3 }, { 0x5d0000, 3 }, { 0x760000, 3 },
    { 0x8f0000, 3 }, { 0xa80000, 3 }, { 0xc10000, 3 }, { 0xcd0d00, 3 }, { 0xd91b00, 3 }, { 0xe62800, 3 },
    { 0xf23600, 3 }, { 0xff4400, 3 }, { 0xff4eff, 3 }, { 0xff58ff, 3 }, { 0xff62ff, 3 }, { 0xff6cff, 3 },
    { 0xff77ff, 3 }, { 0xff8100, 2 }, { 0xff81ff, 3 }, { 0xff8b00, 2 }, { 0xff8bff, 3 }, { 0xff9500, 2 },
    { 0xff95ff, 3 }, { 0xff9f00, 2 }, { 0xff9fff, 3 }, { 0xffaa00, 2 }, { 0xffaaff, 3 }, { 0xffb700, 2 },
    { 0xffc500, 2 }, { 0xffd200, 2 }, { 0xffe000, 2 }, { 0xffee00, 2 }, { 0xffffff, 3 },
};

static radar_level_t level_of_rgb(uint32_t rgb)
{
    int lo = 0, hi = (int)(sizeof(k_colours) / sizeof(k_colours[0])) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (k_colours[mid].rgb == rgb) {
            return (radar_level_t)k_colours[mid].level;
        }
        if (k_colours[mid].rgb < rgb) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return RADAR_NONE; /* under 20 dBZ, or no colour of the table */
}

bool radar_rv_parse_index(const char *json, size_t len, radar_rv_index_t *out)
{
    memset(out, 0, sizeof(*out));
    if (util_json_depth(json) > JSON_MAX_DEPTH) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    const cJSON *host = cJSON_GetObjectItemCaseSensitive(root, "host");
    const cJSON *past = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "radar"), "past");
    int n = cJSON_GetArraySize(past);
    if (cJSON_IsString(host) && strlen(host->valuestring) < sizeof(out->host)) {
        snprintf(out->host, sizeof(out->host), "%s", host->valuestring);
        for (int i = n > RADAR_RV_FRAMES ? n - RADAR_RV_FRAMES : 0; i < n; i++) { /* the newest 16 */
            const cJSON *item = cJSON_GetArrayItem(past, i);
            const cJSON *time = cJSON_GetObjectItemCaseSensitive(item, "time");
            const cJSON *path = cJSON_GetObjectItemCaseSensitive(item, "path");
            if (cJSON_IsNumber(time) && time->valuedouble > 0 && cJSON_IsString(path) &&
                strlen(path->valuestring) < sizeof(out->frames[0].path)) {
                radar_rv_frame_t *f = &out->frames[out->count++];
                f->time = (uint32_t)time->valuedouble;
                snprintf(f->path, sizeof(f->path), "%s", path->valuestring);
            }
        }
    }
    cJSON_Delete(root);
    return out->count > 0;
}

void radar_rv_tiles(const map_view_t *v, radar_rv_tiles_t *t)
{
    int z = (int)floor(v->zoom);
    t->z = z < 0 ? 0 : z > RADAR_RV_MAX_ZOOM ? RADAR_RV_MAX_ZOOM : z;
    double s = pow(2.0, t->z - v->zoom); /* the view's pixels at the tiles' zoom */
    double left = (v->cx - v->w / 2.0) * s, right = (v->cx + v->w / 2.0) * s;
    double top = (v->cy - v->h / 2.0) * s, bottom = (v->cy + v->h / 2.0) * s;
    int last = (1 << t->z) - 1;
    int x0 = (int)floor(left / MAP_TILE_PX), x1 = (int)floor((right - 1e-6) / MAP_TILE_PX);
    int y0 = (int)floor(top / MAP_TILE_PX), y1 = (int)floor((bottom - 1e-6) / MAP_TILE_PX);
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > last ? last : x1;
    y1 = y1 > last ? last : y1;
    t->x0 = x0;
    t->y0 = y0;
    t->nx = x1 - x0 + 1 > RADAR_RV_TILES_MAX ? RADAR_RV_TILES_MAX : x1 - x0 + 1;
    t->ny = y1 - y0 + 1 > RADAR_RV_TILES_MAX ? RADAR_RV_TILES_MAX : y1 - y0 + 1;
}

int radar_rv_tile_url(char *out, size_t size, const char *host, const char *path, int z, int x, int y)
{
    return snprintf(out, size, "%s%s/256/%d/%d/%d/2/0_0.png", host, path, z, x, y);
}

bool radar_rv_frame_alloc(radar_frame_t *f, const radar_rv_tiles_t *t, uint32_t time, const png_mem_t *mem)
{
    if (!radar_frame_alloc(f, (uint16_t)(t->nx * MAP_TILE_PX), (uint16_t)(t->ny * MAP_TILE_PX), mem)) {
        return false;
    }
    double tiles = pow(2.0, t->z);
    f->time = time;
    f->source = RADAR_SOURCE_RAINVIEWER;
    f->scale = 2 * PI * MAP_EARTH_R / (MAP_TILE_PX * tiles);
    f->mx0 = (t->x0 / tiles - 0.5) * 2 * PI * MAP_EARTH_R;
    f->my0 = (0.5 - t->y0 / tiles) * 2 * PI * MAP_EARTH_R;
    return true;
}

typedef struct {
    radar_frame_t *f;
    int ox, oy; /* the tile's top-left in the frame's grid */
} tile_ctx_t;

static void on_tile_row(void *ctx, int y, const uint8_t *row, const png_info_t *info)
{
    tile_ctx_t *c = ctx;
    if (info->type != PNG_RGBA) {
        return;
    }
    for (int x = 0; x < info->width; x++) {
        const uint8_t *px = row + 4 * x;
        if (px[3] != 0) {
            radar_level_t level = level_of_rgb((uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | px[2]);
            if (level != RADAR_NONE) {
                radar_frame_set(c->f, c->ox + x, c->oy + y, level);
            }
        }
    }
}

png_err_t radar_rv_decode_tile(const uint8_t *png, size_t len, const png_mem_t *mem, const radar_rv_tiles_t *t,
                               int x, int y, radar_frame_t *f)
{
    tile_ctx_t c = { .f = f, .ox = (x - t->x0) * MAP_TILE_PX, .oy = (y - t->y0) * MAP_TILE_PX };
    static png_info_t info; /* 1 KB; off the sync task's stack */
    png_err_t err = png_decode(png, len, mem, &info, on_tile_row, &c);
    return err == PNG_OK && info.type != PNG_RGBA ? PNG_ERR_UNSUPPORTED : err;
}
```


- [ ] **Step 6: The store.**

`components/radar/radar_store.c`:

```c
#include <string.h>

#include "radar.h"

void radar_store_init(radar_store_t *s, const png_mem_t *mem)
{
    memset(s, 0, sizeof(*s));
    if (mem != NULL) {
        s->mem = *mem;
    }
}

static void drop(radar_store_t *s, int i)
{
    radar_frame_free(&s->frames[i], &s->mem);
    memmove(&s->frames[i], &s->frames[i + 1], (size_t)(s->count - i - 1) * sizeof(s->frames[0]));
    s->count--;
}

void radar_store_put(radar_store_t *s, radar_frame_t *f, int keep)
{
    for (int i = 0; i < s->count; i++) {
        if (s->frames[i].time == f->time) {
            drop(s, i); /* the newer copy of the same frame wins */
            break;
        }
    }
    if (s->count == RADAR_LOOP_FRAMES) {
        if (f->time < s->frames[0].time) { /* older than all the full store holds */
            radar_frame_free(f, &s->mem);
            return;
        }
        drop(s, 0);
    }
    int at = s->count;
    while (at > 0 && s->frames[at - 1].time > f->time) {
        at--;
    }
    memmove(&s->frames[at + 1], &s->frames[at], (size_t)(s->count - at) * sizeof(s->frames[0]));
    s->frames[at] = *f;
    s->count++;
    f->levels = NULL; /* the store owns it now */
    keep = keep < 1 ? 1 : keep > RADAR_LOOP_FRAMES ? RADAR_LOOP_FRAMES : keep;
    while (s->count > keep) {
        drop(s, 0);
    }
}

const radar_frame_t *radar_store_newest(const radar_store_t *s)
{
    return s->count > 0 ? &s->frames[s->count - 1] : NULL;
}

bool radar_store_has(const radar_store_t *s, uint32_t time)
{
    for (int i = 0; i < s->count; i++) {
        if (s->frames[i].time == time) {
            return true;
        }
    }
    return false;
}

int radar_store_missing(const radar_store_t *s, uint32_t newest, uint32_t step_s, uint32_t *out, int max)
{
    int n = 0;
    for (int k = 0; k < RADAR_LOOP_FRAMES && n < max && (uint64_t)k * step_s <= newest; k++) {
        uint32_t t = newest - (uint32_t)k * step_s;
        if (!radar_store_has(s, t)) {
            out[n++] = t;
        }
    }
    return n;
}

void radar_store_clear(radar_store_t *s)
{
    while (s->count > 0) {
        drop(s, s->count - 1);
    }
}
```


`components/radar/CMakeLists.txt`:

```cmake
# The weather radar (spec §11.2): ČHMÚ's composite and RainViewer's tiles decoded into rain levels,
# their store, and their drawing into any map view. These sources are pure C and also build on the
# host; the sync task fetches the images.
idf_component_register(SRCS "radar_frame.c" "radar_chmu.c" "radar_rainviewer.c" "radar_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx map png
                       PRIV_REQUIRES json util)
```


- [ ] **Step 7: Run the tests.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ./build-host/test_radar`
Expected: `9 Tests 0 Failures 0 Ignored`; `ctest --test-dir build-host`: `out of 58`, all passing.

- [ ] **Step 8: The firmware builds.** `tools/idf.sh reconfigure && tools/idf.sh build`: clean.

- [ ] **Step 9: AGENTS.md, and commit.** The `radar` component joins the list in §5.2 (AGENTS.md changes in the same commit, §8):

`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -222,6 +222,7 @@ components/
   weather/       Open-Meteo URLs, parsers, bands and levels; the HTTPS fetch
   png/           PNG reader for the radar images: palette and RGBA, row by row   [host]
   map/           web-Mercator views; the built-in map (assets/map/map.bin) and its drawing   [host]
+  radar/         ČHMÚ's and RainViewer's frames: their decoding, store and drawing   [host]
   ha_mqtt/       MQTT session, discovery, state, commands, field mappings   (planned)
   sync/          when syncs run and their retries, SNTP packets, the sync task
   audio/         codec control, tone/WAV/stream players, alarm ringing      (planned)
```


```bash
git add components/radar test/host/test_radar.c test/host/fixtures/radar test/host/CMakeLists.txt AGENTS.md
git commit -m "feat(radar): ČHMÚ's and RainViewer's frames, their store and drawing"
```


### Task 6: The flight radar's data (`adsb`)

**Files:**
- Create: `components/adsb/CMakeLists.txt`, `components/adsb/include/adsb.h`, `components/adsb/adsb_parse.c`, `components/adsb/adsb_route.c`
- Create: `test/host/test_adsb.c`, `test/host/fixtures/adsb/adsb_fi.json`, `route_tvs7uz.json`, `route_unknown.json`
- Modify: `test/host/CMakeLists.txt`
- Modify: `AGENTS.md` (the `adsb/` line in §5.2)

**Interfaces:**
- Consumes: `map_view_t`, `map_view_init()`, `map_zoom_for_range()`, `map_project()`, `map_unproject()`, `map_distance_m()`, `map_bearing_deg()` (Task 2); `util_json_depth()`; cJSON.
- Produces (Tasks 10, 12, 13):
  - `adsb_aircraft_t` (`hex`, `callsign`, `type`, `lat`, `lon`, `alt_ft`, `speed_kt`, `track`, `dist_m`, `bearing`), `ADSB_ALT_GROUND`, `ADSB_ALT_UNKNOWN`, `adsb_filter_t` (`view`, `lat`, `lon`, `min_alt_ft`, `ground`, `max`), `adsb_list_t` (`now_ms`, `count`, `ac[ADSB_MAX]`), `ADSB_MAX` 100, `ADSB_REPLY_MAX` 128 KB.
  - `adsb_parse(json, len, filter, out)` → `adsb_err_t` (`ADSB_OK`, `ADSB_ERR_TOO_BIG`, `ADSB_ERR_DEPTH`, `ADSB_ERR_FORMAT`), `adsb_err_name()`, `adsb_url(out, size, filter)`, `adsb_poll_s(range_km, failures)`.
  - Routes: `adsb_airport_t` (`iata`, `icao`, `place`), `adsb_route_t` (`callsign`, `looked_up`, `known`, `from`, `to`), `adsb_routes_t` (`ADSB_ROUTES_MAX` 64), `ADSB_ROUTE_KEEP_S` 24 h, `ADSB_ROUTE_RETRY_S` 1 h; `adsb_route_url()`, `adsb_route_parse()`, `adsb_routes_init()`, `adsb_routes_find(c, callsign, now)`, `adsb_routes_put()`.

Aircraft are kept when they fall on the Flights map (400 × 238), not within a circle: the query's radius reaches the map's farthest corner (106.4 NM at a 100 km range, the southern corners being the farther on the ground), and the list is the nearest first, at most `max`. A string altitude "ground" counts as on the ground; an unknown altitude passes only a `min_alt_ft` of 0.

- [ ] **Step 1: Write the failing test.** The fixtures are adsb.fi's reply for 105 NM around Brno on 2026-10-01 at 22:52 UTC, its own lines kept, cut to 13 of its 32 aircraft; and adsb.lol's replies of the same minute. The expected distances come from Python over the same coordinates.

`test/host/test_adsb.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adsb.h"
#include "unity.h"

/* The flight radar's data (spec §11.3). adsb_fi.json is adsb.fi's reply for 105 NM around Brno on
 * 2026-10-01 at 22:52 UTC, cut to 13 of its 32 aircraft; the routes are adsb.lol's replies of the
 * same minute. The expected distances come from Python over the same coordinates. */

#define BRNO_LAT 49.1951
#define BRNO_LON 16.6068

static adsb_list_t s_list;

void setUp(void)
{
    memset(&s_list, 0, sizeof(s_list));
}

void tearDown(void) {}

static char *load(const char *name, size_t *len)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(*len + 1);
    TEST_ASSERT_EQUAL_size_t(*len, fread(data, 1, *len, f));
    data[*len] = '\0';
    fclose(f);
    return data;
}

/* The Flights map (400 x 238) around Brno with its range to the top edge. */
static adsb_filter_t filter(double range_km)
{
    adsb_filter_t f = { .lat = BRNO_LAT, .lon = BRNO_LON, .min_alt_ft = 0, .ground = false, .max = ADSB_MAX };
    map_view_init(&f.view, 491951, 166068, map_zoom_for_range(491951, range_km * 1000.0, 119), 400, 238);
    return f;
}

static void test_a_real_reply_keeps_the_aircraft_on_the_map_nearest_first(void)
{
    size_t len;
    char *json = load("adsb_fi.json", &len);
    adsb_filter_t f = filter(100);
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(json, len, &f, &s_list));
    TEST_ASSERT_EQUAL_INT64(1790895156001LL, s_list.now_ms);
    TEST_ASSERT_EQUAL_INT(8, s_list.count); /* 13 in the reply: 2 on the ground and 3 more off the map */
    static const char *const k_order[] = { "TVS7UZ", "BAW15", "SIA321", "BLX204", "SXS6WN", "TVS56K", "RYR3698",
                                           "CAI1HA" };
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_STRING(k_order[i], s_list.ac[i].callsign);
    }
    const adsb_aircraft_t *a = &s_list.ac[0];
    TEST_ASSERT_EQUAL_STRING("49d67d", a->hex);
    TEST_ASSERT_EQUAL_STRING("B38M", a->type);
    TEST_ASSERT_EQUAL_INT32(3675, a->alt_ft);
    TEST_ASSERT_EQUAL_INT16(248, a->speed_kt); /* 247.8 */
    TEST_ASSERT_EQUAL_INT16(122, a->track);    /* 121.64 */
    TEST_ASSERT_UINT32_WITHIN(2, 21726, a->dist_m);
    TEST_ASSERT_EQUAL_UINT16(119, a->bearing); /* 118.7: east-south-east, to the airport's east */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 49.100973, a->lat);
    TEST_ASSERT_UINT32_WITHIN(2, 160803, s_list.ac[7].dist_m);
    free(json);
}

static void test_the_filters_and_the_cap_apply(void)
{
    size_t len;
    char *json = load("adsb_fi.json", &len);
    adsb_filter_t f = filter(100);
    f.max = 3;
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(json, len, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(3, s_list.count); /* the nearest three */
    TEST_ASSERT_EQUAL_STRING("SIA321", s_list.ac[2].callsign);

    f = filter(100);
    f.min_alt_ft = 20000;
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(json, len, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(6, s_list.count); /* TVS7UZ at 3675 ft and TVS56K at 19000 ft are left out */
    TEST_ASSERT_EQUAL_STRING("BAW15", s_list.ac[0].callsign);

    f = filter(50);
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(json, len, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(4, s_list.count); /* a closer map holds fewer */
    TEST_ASSERT_EQUAL_STRING("BLX204", s_list.ac[3].callsign);

    f = filter(100);
    f.max = 0; /* clamped to 1 */
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(json, len, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(1, s_list.count);
    free(json);
}

static const char k_odd[] =
    "{\"now\":1790895156001,\"total\":6,\"ac\":["
    "{\"hex\":\"aaaaa1\",\"flight\":\"GND1    \",\"t\":\"A320\",\"alt_baro\":\"ground\",\"gs\":12.0,"
    "\"lat\":49.20,\"lon\":16.61},"
    "{\"hex\":\"aaaaa2\",\"flight\":\"LOW1\",\"t\":\"C172\",\"alt_baro\":1500,\"gs\":95,\"track\":45,"
    "\"lat\":49.21,\"lon\":16.62},"
    "{\"hex\":\"aaaaa3\",\"flight\":\"NOPOS\",\"alt_baro\":30000},"
    "{\"hex\":\"~bbbbb4\",\"alt_baro\":12000,\"gs\":300,\"track\":359.9,\"lat\":49.25,\"lon\":16.70},"
    "{\"hex\":\"aaaaa5\",\"flight\":\"NOALT\",\"lat\":49.30,\"lon\":16.60},"
    "{\"hex\":\"aaaaa6\",\"flight\":\"FAR\",\"alt_baro\":30000,\"lat\":10.0,\"lon\":10.0},"
    "\"junk\",17,"
    "{\"hex\":\"aaaaa7\",\"flight\":\"BADLAT\",\"alt_baro\":30000,\"lat\":\"49.2\",\"lon\":16.6}"
    "]}";

static void test_odd_aircraft_are_kept_or_left_out_as_they_should(void)
{
    adsb_filter_t f = filter(25);
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(k_odd, strlen(k_odd), &f, &s_list));
    TEST_ASSERT_EQUAL_INT(3, s_list.count); /* not on the ground, not without a position, not off the map */
    TEST_ASSERT_EQUAL_STRING("LOW1", s_list.ac[0].callsign);
    TEST_ASSERT_UINT32_WITHIN(2, 1914, s_list.ac[0].dist_m);
    TEST_ASSERT_EQUAL_STRING("", s_list.ac[1].callsign); /* no callsign: the view shows the hex */
    TEST_ASSERT_EQUAL_STRING("~bbbbb4", s_list.ac[1].hex);
    TEST_ASSERT_EQUAL_INT16(0, s_list.ac[1].track); /* 359.9 rounds to north */
    TEST_ASSERT_EQUAL_STRING("", s_list.ac[1].type);
    TEST_ASSERT_EQUAL_STRING("NOALT", s_list.ac[2].callsign);
    TEST_ASSERT_EQUAL_INT32(ADSB_ALT_UNKNOWN, s_list.ac[2].alt_ft);
    TEST_ASSERT_EQUAL_INT16(-1, s_list.ac[2].speed_kt);
    TEST_ASSERT_EQUAL_INT16(-1, s_list.ac[2].track);

    f.ground = true;
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(k_odd, strlen(k_odd), &f, &s_list));
    TEST_ASSERT_EQUAL_INT(4, s_list.count);
    TEST_ASSERT_EQUAL_STRING("GND1", s_list.ac[0].callsign);
    TEST_ASSERT_EQUAL_INT32(ADSB_ALT_GROUND, s_list.ac[0].alt_ft);

    f.min_alt_ft = 5000; /* no unknown or lower altitudes; the ground stays as asked */
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(k_odd, strlen(k_odd), &f, &s_list));
    TEST_ASSERT_EQUAL_INT(2, s_list.count);
    TEST_ASSERT_EQUAL_STRING("GND1", s_list.ac[0].callsign);
    TEST_ASSERT_EQUAL_STRING("~bbbbb4", s_list.ac[1].hex);
}

static void test_bad_replies_are_refused(void)
{
    adsb_filter_t f = filter(50);
    TEST_ASSERT_EQUAL_INT(ADSB_ERR_TOO_BIG, adsb_parse("{\"ac\":[]}", ADSB_REPLY_MAX + 1, &f, &s_list));
    char deep[64] = "{\"ac\":[";
    for (int i = 0; i < 20; i++) {
        strcat(deep, "[");
    }
    strcat(deep, "1");
    for (int i = 0; i < 20; i++) {
        strcat(deep, "]");
    }
    strcat(deep, "]}");
    TEST_ASSERT_EQUAL_INT(ADSB_ERR_DEPTH, adsb_parse(deep, strlen(deep), &f, &s_list));
    TEST_ASSERT_EQUAL_INT(ADSB_ERR_FORMAT, adsb_parse("{}", 2, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(ADSB_ERR_FORMAT, adsb_parse("[]", 2, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(ADSB_ERR_FORMAT, adsb_parse("{\"ac\":[", 7, &f, &s_list)); /* cut short */
    TEST_ASSERT_EQUAL_INT(0, s_list.count);
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse("{\"ac\":[],\"now\":5}", 18, &f, &s_list)); /* an empty sky */
    TEST_ASSERT_EQUAL_INT(0, s_list.count);
    TEST_ASSERT_EQUAL_STRING("bad reply", adsb_err_name(ADSB_ERR_FORMAT));
}

static void test_the_query_reaches_the_corners_of_the_map(void)
{
    char url[128];
    adsb_filter_t f = filter(50);
    adsb_url(url, sizeof(url), &f);
    TEST_ASSERT_EQUAL_STRING("https://opendata.adsb.fi/api/v3/lat/49.1951/lon/16.6068/dist/53", url);
    f = filter(100);
    adsb_url(url, sizeof(url), &f); /* 104.5 NM to the north corners, 106.4 to the south ones */
    TEST_ASSERT_EQUAL_STRING("https://opendata.adsb.fi/api/v3/lat/49.1951/lon/16.6068/dist/107", url);
    f = filter(25);
    adsb_url(url, sizeof(url), &f);
    TEST_ASSERT_EQUAL_STRING("https://opendata.adsb.fi/api/v3/lat/49.1951/lon/16.6068/dist/27", url);
}

static void test_polls_slow_down_with_range_and_failures(void)
{
    TEST_ASSERT_EQUAL_INT(5, adsb_poll_s(10, 0));
    TEST_ASSERT_EQUAL_INT(5, adsb_poll_s(25, 0));
    TEST_ASSERT_EQUAL_INT(10, adsb_poll_s(26, 0));
    TEST_ASSERT_EQUAL_INT(10, adsb_poll_s(50, 0));
    TEST_ASSERT_EQUAL_INT(15, adsb_poll_s(51, 0));
    TEST_ASSERT_EQUAL_INT(15, adsb_poll_s(100, 0));
    TEST_ASSERT_EQUAL_INT(10, adsb_poll_s(25, 1)); /* doubled after each failure */
    TEST_ASSERT_EQUAL_INT(40, adsb_poll_s(25, 3));
    TEST_ASSERT_EQUAL_INT(60, adsb_poll_s(25, 4)); /* up to a minute */
    TEST_ASSERT_EQUAL_INT(60, adsb_poll_s(100, 2));
    TEST_ASSERT_EQUAL_INT(60, adsb_poll_s(100, 1000));
}

static void test_a_route_names_its_airports(void)
{
    size_t len;
    char *json = load("route_tvs7uz.json", &len);
    adsb_route_t r;
    TEST_ASSERT_TRUE(adsb_route_parse(json, len, &r));
    TEST_ASSERT_TRUE(r.known);
    TEST_ASSERT_EQUAL_STRING("TVS7UZ", r.callsign);
    TEST_ASSERT_EQUAL_STRING("BRQ", r.from.iata);
    TEST_ASSERT_EQUAL_STRING("LKTB", r.from.icao);
    TEST_ASSERT_EQUAL_STRING("Brno", r.from.place);
    TEST_ASSERT_EQUAL_STRING("AYT", r.to.iata);
    TEST_ASSERT_EQUAL_STRING("Antalya", r.to.place);
    free(json);

    json = load("route_unknown.json", &len);
    TEST_ASSERT_FALSE(adsb_route_parse(json, len, &r)); /* adsb.lol doesn't know it */
    TEST_ASSERT_FALSE(r.known);
    free(json);
    TEST_ASSERT_FALSE(adsb_route_parse("Internal Server Error", 21, &r)); /* its 500 */

    static const char k_legs[] =
        "{\"callsign\":\"DLH1\",\"plausible\":true,\"_airports\":["
        "{\"iata\":\"FRA\",\"icao\":\"EDDF\",\"location\":\"Frankfurt am Main\"},"
        "{\"iata\":\"PRG\",\"icao\":\"LKPR\",\"location\":\"Praha\"},"
        "{\"iata\":\"\",\"icao\":\"LKKV\",\"location\":\"Karlovy Vary – Ruzyně Mezinárodní Letiště\"}]}";
    TEST_ASSERT_TRUE(adsb_route_parse(k_legs, strlen(k_legs), &r));
    TEST_ASSERT_EQUAL_STRING("FRA", r.from.iata); /* a route of legs: its first and last airports */
    TEST_ASSERT_EQUAL_STRING("", r.to.iata);
    TEST_ASSERT_EQUAL_STRING("LKKV", r.to.icao);
    TEST_ASSERT_EQUAL_STRING("Karlovy Vary – Ruzyn", r.to.place); /* cut at a character, not inside "ě" */

    static const char k_implausible[] =
        "{\"callsign\":\"TVS7UZ\",\"plausible\":false,\"_airports\":["
        "{\"iata\":\"BRQ\",\"icao\":\"LKTB\",\"location\":\"Brno\"},"
        "{\"iata\":\"AYT\",\"icao\":\"LTAI\",\"location\":\"Antalya\"}]}";
    TEST_ASSERT_FALSE(adsb_route_parse(k_implausible, strlen(k_implausible), &r));
}

static void test_route_urls_take_only_callsigns(void)
{
    char url[128];
    TEST_ASSERT_TRUE(adsb_route_url(url, sizeof(url), "TVS7UZ", 49.100973, 16.868567));
    TEST_ASSERT_EQUAL_STRING("https://api.adsb.lol/api/0/route/TVS7UZ/49.1010/16.8686", url);
    TEST_ASSERT_FALSE(adsb_route_url(url, sizeof(url), "", 49.1, 16.8));
    TEST_ASSERT_FALSE(adsb_route_url(url, sizeof(url), "AB/../C", 49.1, 16.8));
    TEST_ASSERT_FALSE(adsb_route_url(url, sizeof(url), "AB C", 49.1, 16.8));
}

static adsb_route_t route(const char *callsign, uint32_t at, bool known)
{
    adsb_route_t r;
    memset(&r, 0, sizeof(r));
    snprintf(r.callsign, sizeof(r.callsign), "%s", callsign);
    r.looked_up = at;
    r.known = known;
    return r;
}

static void test_the_route_cache_keeps_a_day_and_retries_after_an_hour(void)
{
    static adsb_routes_t c;
    adsb_routes_init(&c);
    const uint32_t t0 = 1790895156;
    adsb_route_t r = route("TVS7UZ", t0, true);
    adsb_routes_put(&c, &r);
    r = route("BAW15", t0, false); /* its lookup failed */
    adsb_routes_put(&c, &r);
    TEST_ASSERT_NOT_NULL(adsb_routes_find(&c, "TVS7UZ", t0 + 23 * 3600));
    TEST_ASSERT_NULL(adsb_routes_find(&c, "TVS7UZ", t0 + 24 * 3600)); /* look it up again */
    TEST_ASSERT_NOT_NULL(adsb_routes_find(&c, "BAW15", t0 + 3599));
    TEST_ASSERT_FALSE(adsb_routes_find(&c, "BAW15", t0 + 3599)->known);
    TEST_ASSERT_NULL(adsb_routes_find(&c, "BAW15", t0 + 3600));
    TEST_ASSERT_NULL(adsb_routes_find(&c, "SIA321", t0));

    r = route("BAW15", t0 + 3600, true); /* the retry found it: the entry is replaced */
    adsb_routes_put(&c, &r);
    TEST_ASSERT_TRUE(adsb_routes_find(&c, "BAW15", t0 + 3600)->known);
    TEST_ASSERT_EQUAL_INT(2, c.count);

    for (int i = 0; i < ADSB_ROUTES_MAX; i++) { /* a full cache drops the oldest */
        char name[9];
        snprintf(name, sizeof(name), "X%d", i);
        r = route(name, t0 + 7200 + (uint32_t)i, true);
        adsb_routes_put(&c, &r);
    }
    TEST_ASSERT_EQUAL_INT(ADSB_ROUTES_MAX, c.count);
    TEST_ASSERT_NULL(adsb_routes_find(&c, "TVS7UZ", t0 + 7300));
    TEST_ASSERT_NULL(adsb_routes_find(&c, "BAW15", t0 + 7300));
    TEST_ASSERT_NOT_NULL(adsb_routes_find(&c, "X0", t0 + 7300));
    TEST_ASSERT_NOT_NULL(adsb_routes_find(&c, "X63", t0 + 7300));
    TEST_ASSERT_NULL(adsb_routes_find(&c, "X63", t0 + 7200)); /* asked before it was looked up: the clock went back */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_real_reply_keeps_the_aircraft_on_the_map_nearest_first);
    RUN_TEST(test_the_filters_and_the_cap_apply);
    RUN_TEST(test_odd_aircraft_are_kept_or_left_out_as_they_should);
    RUN_TEST(test_bad_replies_are_refused);
    RUN_TEST(test_the_query_reaches_the_corners_of_the_map);
    RUN_TEST(test_polls_slow_down_with_range_and_failures);
    RUN_TEST(test_a_route_names_its_airports);
    RUN_TEST(test_route_urls_take_only_callsigns);
    RUN_TEST(test_the_route_cache_keeps_a_day_and_retries_after_an_hour);
    return UNITY_END();
}
```


`test/host/fixtures/adsb/adsb_fi.json`:

```json
{"ac":[
{"hex":"49d67d","type":"adsb_icao","flight":"TVS7UZ  ","r":"OK-SWO","t":"B38M","desc":"BOEING 737 MAX 8","alt_baro":3675,"alt_geom":4275,"gs":247.8,"ias":249,"tas":264,"mach":0.400,"wd":172,"ws":24,"oat":14,"tat":23,"track":121.64,"track_rate":0.03,"roll":0.35,"mag_heading":120.06,"true_heading":125.64,"baro_rate":2048,"geom_rate":2016,"squawk":"1411","emergency":"none","category":"A3","nav_qnh":1028.8,"nav_altitude_mcp":8992,"nav_altitude_fms":9008,"nav_heading":120.23,"lat":49.100973,"lon":16.868567,"nic":8,"rc":186,"seen_pos":0.844,"version":2,"nic_baro":1,"nac_p":11,"nac_v":2,"sil":3,"sil_type":"perhour","gva":2,"sda":2,"alert":0,"spi":0,"mlat":[],"tisb":[],"messages":1153,"seen":0.5,"rssi":-21.2,"dst":11.714,"dir":118.7},
{"hex":"406f73","type":"adsb_icao","flight":"BAW15   ","r":"G-ZBKG","t":"B789","desc":"BOEING 787-9 Dreamliner","alt_baro":35000,"alt_geom":36325,"gs":504.0,"ias":285,"tas":484,"mach":0.836,"wd":6,"ws":42,"oat":-52,"tat":-22,"track":126.53,"track_rate":0.03,"roll":-0.18,"mag_heading":116.54,"true_heading":122.16,"baro_rate":96,"geom_rate":-64,"squawk":"2212","emergency":"none","category":"A5","nav_qnh":1012.8,"nav_altitude_mcp":35008,"nav_heading":116.72,"nav_modes":["autopilot","vnav","lnav","tcas"],"lat":49.057617,"lon":17.164800,"nic":8,"rc":186,"seen_pos":0.205,"version":2,"nic_baro":1,"nac_p":9,"nac_v":1,"sil":3,"sil_type":"perhour","gva":2,"sda":2,"alert":0,"spi":0,"mlat":[],"tisb":[],"messages":245674,"seen":0.0,"rssi":-20.7,"dst":23.384,"dir":110.4},
{"hex":"76ceeb","type":"adsb_icao","flight":"SIA321  ","r":"9V-SWK","t":"B77W","desc":"BOEING 777-300ER","alt_baro":31000,"alt_geom":32375,"gs":509.1,"ias":310,"tas":492,"mach":0.832,"wd":319,"ws":18,"oat":-43,"tat":-11,"track":120.19,"roll":-0.18,"mag_heading":114.08,"true_heading":119.50,"baro_rate":0,"geom_rate":0,"squawk":"2257","emergency":"none","category":"A5","nav_qnh":1012.8,"nav_altitude_mcp":31008,"nav_heading":113.91,"lat":49.375538,"lon":16.013730,"nic":8,"rc":186,"seen_pos":0.449,"version":2,"nic_baro":1,"nac_p":9,"nac_v":1,"sil":3,"sil_type":"perhour","gva":2,"sda":2,"alert":0,"spi":0,"mlat":[],"tisb":[],"messages":217890,"seen":0.1,"rssi":-9.3,"dst":25.587,"dir":295.2},
{"hex":"4ac9c6","type":"adsb_icao","flight":"BLX204  ","r":"SE-RNF","t":"B38M","desc":"BOEING 737 MAX 8","alt_baro":36000,"alt_geom":37400,"gs":423.7,"ias":260,"tas":452,"mach":0.784,"wd":22,"ws":29,"oat":-54,"tat":-27,"track":6.50,"track_rate":0.00,"roll":-0.18,"mag_heading":2.11,"true_heading":7.50,"baro_rate":0,"geom_rate":0,"squawk":"1000","category":"A3","nav_qnh":1013.6,"nav_altitude_mcp":36000,"nav_altitude_fms":36000,"nav_heading":355.08,"lat":49.418335,"lon":15.863131,"nic":8,"rc":186,"seen_pos":0.350,"version":2,"nic_baro":1,"nac_p":10,"nac_v":2,"sil":3,"sil_type":"perhour","gva":2,"sda":2,"alert":0,"spi":0,"mlat":[],"tisb":[],"messages":153774,"seen":0.0,"rssi":-14.4,"dst":31.997,"dir":295.0},
{"hex":"4bcda9","type":"adsb_icao","flight":"SXS6WN  ","r":"TC-SMI","t":"B38M","desc":"BOEING 737 MAX 8","alt_baro":37000,"alt_geom":38400,"gs":483.7,"ias":255,"tas":452,"mach":0.788,"wd":350,"ws":34,"oat":-56,"tat":-30,"track":147.07,"track_rate":0.00,"roll":-0.18,"mag_heading":139.92,"true_heading":145.32,"baro_rate":0,"geom_rate":0,"squawk":"7626","emergency":"none","category":"A3","nav_qnh":1013.6,"nav_altitude_mcp":36992,"nav_altitude_fms":37008,"nav_heading":142.03,"lat":48.488571,"lon":16.481887,"nic":8,"rc":186,"seen_pos":0.449,"version":2,"nic_baro":1,"nac_p":11,"nac_v":2,"sil":3,"sil_type":"perhour","gva":2,"sda":2,"alert":0,"spi":0,"mlat":[],"tisb":[],"messages":121748,"seen":0.0,"rssi":-16.8,"dst":42.752,"dir":186.7},
{"hex":"49d26c","type":"adsb_icao","flight":"TVS56K  ","r":"OK-TVR","t":"B738","desc":"BOEING 737-800","alt_baro":19000,"alt_geom":20125,"gs":346.2,"ias":255,"tas":342,"mach":0.548,"wd":44,"ws":20,"oat":-17,"tat":-1,"track":302.50,"track_rate":0.03,"roll":-0.18,"mag_heading":299.88,"true_heading":305.53,"baro_rate":-1984,"geom_rate":-1984,"squawk":"2364","emergency":"none","category":"A3","nav_qnh":1013.6,"nav_altitude_mcp":12000,"nav_heading":302.34,"lat":48.627490,"lon":17.518278,"nic":8,"rc":186,"seen_pos":0.369,"version":2,"nic_baro":1,"nac_p":8,"nac_v":1,"sil":3,"sil_type":"perhour","gva":1,"sda":2,"alert":0,"spi":0,"mlat":[],"tisb":[],"messages":128163,"seen":0.0,"rssi":-16.7,"dst":49.516,"dir":133.1},
{"hex":"02a262","type":"adsb_icao","flight":"TAR642  ","r":"TS-ITC","t":"A320","desc":"AIRBUS A-320","alt_baro":4400,"alt_geom":5125,"gs":292.0,"ias":250,"tas":268,"mach":0.408,"wd":150,"ws":24,"oat":11,"tat":20,"track":343.49,"track_rate":0.03,"roll":0.53,"mag_heading":339.26,"true_heading":344.69,"baro_rate":-1920,"geom_rate":-1920,"squawk":"1000","category":"A3","nav_qnh":1028.0,"nav_altitude_mcp":3008,"lat":48.275834,"lon":16.641830,"nic":8,"rc":186,"seen_pos":0.116,"version":2,"nic_baro":1,"nac_p":11,"nac_v":4,"sil":3,"sil_type":"perhour","gva":2,"sda":3,"alert":0,"spi":0,"mlat":[],"tisb":[],"messages":96846,"seen":0.0,"rssi":-8.9,"dst":55.272,"dir":178.5},
{"hex":"3cc187","type":"adsb_icao","flight":"AWH91R  ","r":"D-CAPB","t":"C560","desc":"CESSNA 560 Citation Ultra","alt_baro":"ground","gs":25.5,"true_heading":295.31,"category":"A3","lat":48.116867,"lon":16.558096,"nic":8,"rc":186,"seen_pos":21.155,"version":2,"sil_type":"perhour","alert":0,"spi":0,"mlat":[],"tisb":[],"messages":328230,"seen":21.2,"rssi":-25.2,"dst":64.839,"dir":181.7},
{"hex":"505f14","type":"adsb_icao_nt","r":"TWR","t":"TWR","alt_baro":"ground","lat":48.170151,"lon":17.217161,"nic":11,"rc":8,"seen_pos":0.809,"nac_p":11,"sil":2,"sil_type":"unknown","mlat":[],"tisb":[],"messages":686242,"seen":0.8,"rssi":-22.1,"dst":66.168,"dir":158.3},
{"hex":"48c2b3","type":"adsb_icao","flight":"RYR3698 ","r":"SP-RZU","t":"B38M","desc":"BOEING 737 MAX 8","alt_baro":37000,"alt_geom":38400,"gs":463.5,"ias":257,"tas":454,"mach":0.792,"wd":324,"ws":15,"oat":-57,"tat":-30,"track":92.72,"track_rate":0.00,"roll":-0.35,"mag_heading":85.78,"true_heading":91.19,"baro_rate":0,"geom_rate":0,"squawk":"1000","category":"A3","nav_qnh":1013.6,"nav_altitude_mcp":36992,"nav_altitude_fms":37008,"nav_heading":87.89,"lat":50.045937,"lon":15.605188,"nic":8,"rc":186,"seen_pos":0.096,"version":2,"nic_baro":1,"nac_p":11,"nac_v":2,"sil":3,"sil_type":"perhour","gva":2,"sda":2,"alert":0,"spi":0,"mlat":[],"tisb":[],"messages":193175,"seen":0.0,"rssi":-10.2,"dst":64.238,"dir":323.0},
{"hex":"501c5b","type":"adsb_icao","flight":"9ACAW   ","alt_baro":33000,"alt_geom":34400,"gs":473.0,"ias":276,"tas":456,"mach":0.780,"wd":307,"ws":21,"oat":-48,"tat":-21,"track":165.05,"track_rate":0.00,"roll":-0.18,"mag_heading":161.72,"true_heading":166.74,"baro_rate":-32,"geom_rate":0,"squawk":"7675","emergency":"none","category":"A3","nav_qnh":1012.8,"nav_altitude_mcp":32992,"nav_altitude_fms":5504,"nav_heading":161.72,"lat":47.914169,"lon":14.893890,"nic":8,"rc":186,"seen_pos":0.276,"version":2,"nic_baro":1,"nac_p":10,"nac_v":1,"sil":3,"sil_type":"perhour","gva":2,"sda":2,"alert":0,"spi":0,"mlat":[],"tisb":[],"messages":155876,"seen":0.0,"rssi":-21.8,"dst":102.671,"dir":222.2},
{"hex":"447ac7","type":"mlat","flight":"FFMSNE  ","r":"TWR","t":"TWR","alt_baro":6800,"gs":7.0,"track":296.00,"baro_rate":0,"squawk":"7777","lat":47.767386,"lon":15.803425,"nic":0,"rc":0,"seen_pos":4.589,"alert":0,"spi":0,"mlat":["callsign","gs","track","baro_rate","lat","lon","nic","rc"],"tisb":[],"messages":10035854,"seen":1.4,"rssi":-11.8,"dst":91.554,"dir":200.8},
{"hex":"4b8de8","type":"adsb_icao","flight":"CAI1HA  ","r":"TC-COH","t":"B738","desc":"BOEING 737-800","alt_baro":36000,"alt_geom":37450,"gs":443.4,"ias":259,"tas":450,"mach":0.780,"wd":21,"ws":38,"oat":-54,"tat":-27,"track":298.13,"track_rate":0.00,"roll":0.18,"mag_heading":297.95,"true_heading":302.99,"baro_rate":0,"geom_rate":0,"squawk":"5323","emergency":"none","category":"A3","nav_qnh":1013.6,"nav_altitude_mcp":36000,"nav_altitude_fms":36000,"nav_heading":298.12,"lat":48.386163,"lon":14.787164,"nic":8,"rc":186,"seen_pos":0.000,"version":2,"nic_baro":1,"nac_p":9,"nac_v":1,"sil":3,"sil_type":"perhour","gva":2,"sda":2,"alert":0,"spi":0,"mlat":[],"tisb":[],"messages":146801,"seen":0.0,"rssi":-15.5,"dst":86.701,"dir":236.6}
]
,"msg": "No error"
,"now": 1790895156001
,"total": 13
,"ctime": 1790895156001
,"ptime": 0
}
```


`test/host/fixtures/adsb/route_tvs7uz.json`:

```json
{
  "_airport_codes_iata": "BRQ-AYT",
  "_airports": [
    {
      "alt_feet": 778.0,
      "alt_meters": 237.13,
      "countryiso2": "CZ",
      "iata": "BRQ",
      "icao": "LKTB",
      "lat": 49.151299,
      "location": "Brno",
      "lon": 16.694401,
      "name": "Brno-Tuřany Airport"
    },
    {
      "alt_feet": 177.0,
      "alt_meters": 53.95,
      "countryiso2": "TR",
      "iata": "AYT",
      "icao": "LTAI",
      "lat": 36.898701,
      "location": "Antalya",
      "lon": 30.800501,
      "name": "Antalya International Airport"
    }
  ],
  "airline_code": "TVS",
  "airport_codes": "LKTB-LTAI",
  "callsign": "TVS7UZ",
  "number": "7UZ",
  "plausible": true
}
```


`test/host/fixtures/adsb/route_unknown.json`:

```json
{
  "_airport_codes_iata": "unknown",
  "_airports": [],
  "airline_code": "unknown",
  "airport_codes": "unknown",
  "callsign": "FFMSNE",
  "number": "unknown"
}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -79,6 +79,12 @@ target_include_directories(radar_logic PUBLIC ${REPO_ROOT}/components/radar/incl
 target_compile_options(radar_logic PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(radar_logic PUBLIC png map gfx PRIVATE cjson util m)
 
+# adsb: the flight radar's parser, filters and route cache build on the host; the polling does not.
+add_library(adsb_logic STATIC ${REPO_ROOT}/components/adsb/adsb_parse.c ${REPO_ROOT}/components/adsb/adsb_route.c)
+target_include_directories(adsb_logic PUBLIC ${REPO_ROOT}/components/adsb/include)
+target_compile_options(adsb_logic PRIVATE ${REFLBO_WARNINGS})
+target_link_libraries(adsb_logic PUBLIC map PRIVATE cjson util m)
+
 # sync: the planner (modes, quiet hours, retries) and the SNTP packets build on the host; the task does not.
 add_library(sync_logic STATIC ${REPO_ROOT}/components/sync/sync_plan.c ${REPO_ROOT}/components/sync/sync_ntp.c)
 target_include_directories(sync_logic PUBLIC ${REPO_ROOT}/components/sync/include)
@@ -211,6 +217,8 @@ reflbo_host_test(test_map_draw map)
 target_compile_definitions(test_map_draw PRIVATE MAP_BIN="${REPO_ROOT}/assets/map/map.bin")
 reflbo_host_test(test_radar radar_logic)
 target_compile_definitions(test_radar PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
+reflbo_host_test(test_adsb adsb_logic)
+target_compile_definitions(test_adsb PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/adsb")
 reflbo_host_test(test_gesture board_logic)
 reflbo_host_test(test_shtc3_codec sensors_logic)
 reflbo_host_test(test_battery_model sensors_logic)
```


- [ ] **Step 2: Run it to see it fail.**

Run: `cmake -S test/host -B build-host -G Ninja`
Expected: `CMake Error … Cannot find source file:` naming `components/adsb/adsb_parse.c`.

- [ ] **Step 3: The parser, the filters and the query.**

`components/adsb/include/adsb.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "map_view.h"

/*
 * The flight radar's data (spec §11.3): the aircraft adsb.fi reports around the radar's centre,
 * filtered to its map, and the nearest one's route from adsb.lol. Pure C, host-buildable; the
 * polling is in adsb_task.c.
 */

#define ADSB_MAX 100                /* aircraft kept at most: radar.flights.max's ceiling */
#define ADSB_REPLY_MAX (128 * 1024) /* a longer reply is refused */
#define ADSB_ALT_GROUND INT32_MIN   /* alt_baro "ground" */
#define ADSB_ALT_UNKNOWN (INT32_MIN + 1)

typedef struct {
    char hex[8];      /* the ICAO address, as sent: "49d67d", or "~…" for one that isn't */
    char callsign[9]; /* trimmed; "" when the aircraft sends none */
    char type[5];     /* the ICAO type designator: "B38M"; "" when unknown */
    double lat, lon;
    int32_t alt_ft;   /* barometric, or ADSB_ALT_GROUND or ADSB_ALT_UNKNOWN */
    int16_t speed_kt; /* ground speed; -1 when unknown */
    int16_t track;    /* degrees 0–359, 0 = north; -1 when unknown */
    uint32_t dist_m;  /* from the radar's centre */
    uint16_t bearing; /* from the radar's centre, degrees 0–359 */
} adsb_aircraft_t;

typedef struct {
    map_view_t view;    /* the Flights map: aircraft off it are left out */
    double lat, lon;    /* its centre, which distances and bearings are from */
    int32_t min_alt_ft; /* radar.flights.min_alt_ft: lower and unknown altitudes are left out */
    bool ground;        /* radar.flights.ground: keep the aircraft on the ground */
    int max;            /* radar.flights.max, clamped to 1..ADSB_MAX */
} adsb_filter_t;

typedef struct {
    int64_t now_ms; /* the reply's own time */
    int count;
    adsb_aircraft_t ac[ADSB_MAX]; /* the nearest first */
} adsb_list_t;

typedef enum {
    ADSB_OK,
    ADSB_ERR_TOO_BIG, /* over ADSB_REPLY_MAX */
    ADSB_ERR_DEPTH,   /* nested deeper than any reply (AGENTS.md gotcha 30) */
    ADSB_ERR_FORMAT,  /* not JSON, or no `ac` array */
} adsb_err_t;

/* adsb.fi's readsb reply, `len` bytes with a NUL after them, into `out`. */
adsb_err_t adsb_parse(const char *json, size_t len, const adsb_filter_t *f, adsb_list_t *out);
const char *adsb_err_name(adsb_err_t err);
/* adsb.fi's query: a circle of whole nautical miles around the centre that reaches the map's
 * corners. Returns what snprintf returns. */
int adsb_url(char *out, size_t size, const adsb_filter_t *f);
/* Seconds to the next poll: 5 up to a 25 km range, 10 up to 50 km, 15 beyond, doubled after each
 * failure in a row up to 60. */
int adsb_poll_s(int range_km, int failures);

/* Routes from adsb.lol, looked up when the nearest aircraft changes (D27). */

#define ADSB_ROUTES_MAX 64
#define ADSB_ROUTE_KEEP_S (24 * 3600) /* a known route */
#define ADSB_ROUTE_RETRY_S 3600       /* an unknown route, or a lookup that failed */

typedef struct {
    char iata[4]; /* "" when it has none */
    char icao[5];
    char place[24]; /* adsb.lol's "location", the town, cut at a character */
} adsb_airport_t;

typedef struct {
    char callsign[9];
    uint32_t looked_up; /* UTC seconds */
    bool known;         /* false: unknown or failed */
    adsb_airport_t from, to;
} adsb_route_t;

typedef struct {
    adsb_route_t r[ADSB_ROUTES_MAX];
    int count;
} adsb_routes_t;

/* adsb.lol's route query; false for a callsign that isn't 1–8 letters and digits. */
bool adsb_route_url(char *out, size_t size, const char *callsign, double lat, double lon);
/* Its reply (NUL after `len` bytes): true with the first and last airports of a plausible route;
 * false, with out->known false, for "unknown", an implausible route or a reply that isn't one. */
bool adsb_route_parse(const char *json, size_t len, adsb_route_t *out);
void adsb_routes_init(adsb_routes_t *c);
/* The entry for `callsign` while it is good at `now`: a known route for ADSB_ROUTE_KEEP_S, an
 * unknown one for ADSB_ROUTE_RETRY_S. NULL: look it up. */
const adsb_route_t *adsb_routes_find(const adsb_routes_t *c, const char *callsign, uint32_t now);
/* Keeps a lookup's result: in place of the same callsign's, else in a free entry, else in place of
 * the oldest. */
void adsb_routes_put(adsb_routes_t *c, const adsb_route_t *r);
```


`components/adsb/adsb_parse.c`:

```c
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "adsb.h"
#include "cJSON.h"
#include "util_json.h"

#define JSON_MAX_DEPTH 8 /* a readsb reply nests 4 deep: its arrays of strings in each aircraft */
#define QUERY_MAX_NM 250 /* adsb.fi's limit */
#define M_PER_NM 1852.0

static const cJSON *get(const cJSON *o, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(o, key);
}

/* A string without its trailing blanks (readsb pads callsigns to 8), cut to fit. */
static void copy_trimmed(char *out, size_t size, const cJSON *s)
{
    out[0] = '\0';
    if (!cJSON_IsString(s)) {
        return;
    }
    size_t n = strlen(s->valuestring);
    while (n > 0 && s->valuestring[n - 1] == ' ') {
        n--;
    }
    n = n < size ? n : size - 1;
    memcpy(out, s->valuestring, n);
    out[n] = '\0';
}

/* A number within [lo, hi], rounded; -1 for anything else. */
static int number(const cJSON *v, double lo, double hi)
{
    return cJSON_IsNumber(v) && v->valuedouble >= lo && v->valuedouble <= hi ? (int)lround(v->valuedouble) : -1;
}

static int32_t altitude(const cJSON *alt)
{
    if (cJSON_IsNumber(alt) && alt->valuedouble > -2000 && alt->valuedouble < 200000) {
        return (int32_t)lround(alt->valuedouble);
    }
    if (cJSON_IsString(alt) && strcmp(alt->valuestring, "ground") == 0) {
        return ADSB_ALT_GROUND;
    }
    return ADSB_ALT_UNKNOWN;
}

static bool altitude_kept(int32_t alt, const adsb_filter_t *f)
{
    if (alt == ADSB_ALT_GROUND) {
        return f->ground;
    }
    if (alt == ADSB_ALT_UNKNOWN) {
        return f->min_alt_ft <= 0;
    }
    return alt >= f->min_alt_ft;
}

/* One entry of `ac` that passes the filter. */
static bool aircraft(const cJSON *item, const adsb_filter_t *f, adsb_aircraft_t *a)
{
    const cJSON *lat = get(item, "lat"), *lon = get(item, "lon");
    if (!cJSON_IsObject(item) || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) || fabs(lat->valuedouble) > 90 ||
        fabs(lon->valuedouble) > 180) {
        return false; /* no position: nothing to draw */
    }
    memset(a, 0, sizeof(*a));
    a->lat = lat->valuedouble;
    a->lon = lon->valuedouble;
    a->alt_ft = altitude(get(item, "alt_baro"));
    if (!altitude_kept(a->alt_ft, f)) {
        return false;
    }
    double x, y;
    map_project(&f->view, a->lat, a->lon, &x, &y);
    if (x < 0 || y < 0 || x >= f->view.w || y >= f->view.h) {
        return false;
    }
    copy_trimmed(a->hex, sizeof(a->hex), get(item, "hex"));
    copy_trimmed(a->callsign, sizeof(a->callsign), get(item, "flight"));
    copy_trimmed(a->type, sizeof(a->type), get(item, "t"));
    a->speed_kt = (int16_t)number(get(item, "gs"), 0, 2000);
    int track = number(get(item, "track"), 0, 360);
    a->track = (int16_t)(track < 0 ? -1 : track % 360);
    a->dist_m = (uint32_t)lround(map_distance_m(f->lat, f->lon, a->lat, a->lon));
    a->bearing = (uint16_t)(lround(map_bearing_deg(f->lat, f->lon, a->lat, a->lon)) % 360);
    return true;
}

/* Into the list, nearest first, keeping at most `max`. */
static void insert(adsb_list_t *out, int max, const adsb_aircraft_t *a)
{
    int at = out->count;
    while (at > 0 && out->ac[at - 1].dist_m > a->dist_m) {
        at--;
    }
    if (at >= max) {
        return; /* farther than every one kept, and the list is full */
    }
    int end = out->count < max ? out->count : max - 1; /* the entries that move down */
    memmove(&out->ac[at + 1], &out->ac[at], (size_t)(end - at) * sizeof(out->ac[0]));
    out->ac[at] = *a;
    if (out->count < max) {
        out->count++;
    }
}

adsb_err_t adsb_parse(const char *json, size_t len, const adsb_filter_t *f, adsb_list_t *out)
{
    out->count = 0;
    out->now_ms = 0;
    if (len > ADSB_REPLY_MAX) {
        return ADSB_ERR_TOO_BIG;
    }
    if (util_json_depth(json) > JSON_MAX_DEPTH) {
        return ADSB_ERR_DEPTH;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    const cJSON *ac = get(root, "ac");
    if (!cJSON_IsArray(ac)) {
        cJSON_Delete(root);
        return ADSB_ERR_FORMAT;
    }
    const cJSON *now = get(root, "now");
    out->now_ms = cJSON_IsNumber(now) ? (int64_t)now->valuedouble : 0;
    int max = f->max < 1 ? 1 : f->max > ADSB_MAX ? ADSB_MAX : f->max;
    const cJSON *item;
    cJSON_ArrayForEach(item, ac)
    {
        adsb_aircraft_t a;
        if (aircraft(item, f, &a)) {
            insert(out, max, &a);
        }
    }
    cJSON_Delete(root);
    return ADSB_OK;
}

const char *adsb_err_name(adsb_err_t err)
{
    switch (err) {
    case ADSB_OK:
        return "ok";
    case ADSB_ERR_TOO_BIG:
        return "too big";
    case ADSB_ERR_DEPTH:
        return "too deep";
    default:
        return "bad reply";
    }
}

int adsb_url(char *out, size_t size, const adsb_filter_t *f)
{
    double far = 0;
    for (int i = 0; i < 4; i++) { /* the corners; the southern ones are the farther on the ground */
        double lat, lon;
        map_unproject(&f->view, i & 1 ? f->view.w : 0, i & 2 ? f->view.h : 0, &lat, &lon);
        double d = map_distance_m(f->lat, f->lon, lat, lon);
        far = d > far ? d : far;
    }
    int nm = (int)ceil(far / M_PER_NM);
    nm = nm < 1 ? 1 : nm > QUERY_MAX_NM ? QUERY_MAX_NM : nm;
    return snprintf(out, size, "https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%d", f->lat, f->lon, nm);
}

int adsb_poll_s(int range_km, int failures)
{
    int s = range_km <= 25 ? 5 : range_km <= 50 ? 10 : 15;
    for (int i = 0; i < failures && s < 60; i++) {
        s *= 2;
    }
    return s > 60 ? 60 : s;
}
```


- [ ] **Step 4: Routes and their cache.** A callsign goes into the URL's path, so only 1–8 letters and digits pass; a route needs two airports and a `plausible` that isn't false; a place name is cut at a character boundary.

`components/adsb/adsb_route.c`:

```c
#include <stdio.h>
#include <string.h>

#include "adsb.h"
#include "cJSON.h"
#include "util_json.h"

#define JSON_MAX_DEPTH 8 /* adsb.lol's route nests 3 deep */

static const cJSON *get(const cJSON *o, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(o, key);
}

static bool callsign_ok(const char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return n >= 1 && n <= 8;
}

bool adsb_route_url(char *out, size_t size, const char *callsign, double lat, double lon)
{
    if (!callsign_ok(callsign)) {
        return false; /* it goes into the path */
    }
    int n = snprintf(out, size, "https://api.adsb.lol/api/0/route/%s/%.4f/%.4f", callsign, lat, lon);
    return n > 0 && (size_t)n < size;
}

/* A copy cut to fit at a character boundary (a limit inside "ě" would split it). */
static void copy_utf8(char *out, size_t size, const char *s)
{
    size_t n = strlen(s);
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) {
            n--; /* s[n] continues a character: cut before its first byte */
        }
    }
    memcpy(out, s, n);
    out[n] = '\0';
}

static void copy_code(char *out, size_t size, const cJSON *s)
{
    out[0] = '\0';
    if (cJSON_IsString(s) && strlen(s->valuestring) < size) {
        memcpy(out, s->valuestring, strlen(s->valuestring) + 1);
    }
}

static bool airport(const cJSON *a, adsb_airport_t *out)
{
    memset(out, 0, sizeof(*out));
    copy_code(out->iata, sizeof(out->iata), get(a, "iata"));
    copy_code(out->icao, sizeof(out->icao), get(a, "icao"));
    const cJSON *place = get(a, "location");
    if (cJSON_IsString(place)) {
        copy_utf8(out->place, sizeof(out->place), place->valuestring);
    }
    return out->iata[0] != '\0' || out->icao[0] != '\0';
}

bool adsb_route_parse(const char *json, size_t len, adsb_route_t *out)
{
    memset(out, 0, sizeof(*out));
    if (util_json_depth(json) > JSON_MAX_DEPTH) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    copy_code(out->callsign, sizeof(out->callsign), get(root, "callsign"));
    const cJSON *airports = get(root, "_airports");
    int n = cJSON_IsArray(airports) ? cJSON_GetArraySize(airports) : 0;
    if (n >= 2 && !cJSON_IsFalse(get(root, "plausible"))) { /* the first and last airports of its legs */
        bool from = airport(cJSON_GetArrayItem(airports, 0), &out->from);
        out->known = airport(cJSON_GetArrayItem(airports, n - 1), &out->to) && from;
    }
    cJSON_Delete(root);
    return out->known;
}

void adsb_routes_init(adsb_routes_t *c)
{
    memset(c, 0, sizeof(*c));
}

const adsb_route_t *adsb_routes_find(const adsb_routes_t *c, const char *callsign, uint32_t now)
{
    for (int i = 0; i < c->count; i++) {
        const adsb_route_t *r = &c->r[i];
        if (strcmp(r->callsign, callsign) == 0) {
            uint32_t keep = r->known ? ADSB_ROUTE_KEEP_S : ADSB_ROUTE_RETRY_S;
            return now >= r->looked_up && now - r->looked_up < keep ? r : NULL; /* a clock set back: ask again */
        }
    }
    return NULL;
}

void adsb_routes_put(adsb_routes_t *c, const adsb_route_t *r)
{
    int at = -1;
    for (int i = 0; i < c->count && at < 0; i++) {
        at = strcmp(c->r[i].callsign, r->callsign) == 0 ? i : -1;
    }
    if (at < 0 && c->count < ADSB_ROUTES_MAX) {
        at = c->count++;
    }
    if (at < 0) {
        at = 0;
        for (int i = 1; i < c->count; i++) {
            at = c->r[i].looked_up < c->r[at].looked_up ? i : at;
        }
    }
    c->r[at] = *r;
}
```


`components/adsb/CMakeLists.txt`:

```cmake
# The flight radar's data (spec §11.3): adsb.fi's aircraft around the radar's centre and adsb.lol's
# routes. adsb_parse.c and adsb_route.c are pure C and also build on the host.
idf_component_register(SRCS "adsb_parse.c" "adsb_route.c"
                       INCLUDE_DIRS "include"
                       REQUIRES map
                       PRIV_REQUIRES json util)
```


- [ ] **Step 5: Run the tests.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ./build-host/test_adsb`
Expected: `9 Tests 0 Failures 0 Ignored`; `ctest --test-dir build-host`: `out of 59`, all passing; the ASan build passes `test_adsb` too.

- [ ] **Step 6: The firmware builds.** `tools/idf.sh reconfigure && tools/idf.sh build`: clean.

- [ ] **Step 7: AGENTS.md, and commit.** The `adsb` component joins the list in §5.2 (AGENTS.md changes in the same commit, §8):

`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -223,6 +223,7 @@ components/
   png/           PNG reader for the radar images: palette and RGBA, row by row   [host]
   map/           web-Mercator views; the built-in map (assets/map/map.bin) and its drawing   [host]
   radar/         ČHMÚ's and RainViewer's frames: their decoding, store and drawing   [host]
+  adsb/          adsb.fi's aircraft on the Flights map, adsb.lol's routes and their cache   [host]
   ha_mqtt/       MQTT session, discovery, state, commands, field mappings   (planned)
   sync/          when syncs run and their retries, SNTP packets, the sync task
   audio/         codec control, tone/WAV/stream players, alarm ringing      (planned)
```


```bash
git add components/adsb test/host/test_adsb.c test/host/fixtures/adsb test/host/CMakeLists.txt AGENTS.md
git commit -m "feat(adsb): the flight radar's parser, filters and routes"
```


### Task 7: Rain in the next 2 hours (`weather`, `datastore`, `wx.rain2h`)

**Files:**
- Modify: `components/datastore/include/datastore.h`, `components/datastore/datastore.c` (`ds_rain_t` in the forecast, `ds_rain_index()`)
- Modify: `components/weather/weather_url.c`, `components/weather/weather_parse.c` (`minutely_15`)
- Modify: `components/locale/include/lang.h`, `lang_en.c`, `lang_cs.c`
- Modify: `components/ui/include/ui_fields.h`, `components/ui/ui_fields.c`, `components/ui/ui_forecast.c`
- Modify: `main/app.c`, `main/app_ui.c` (the snapshot's and the forecast file's versions)
- Modify: `test/host/test_datastore_weather.c`, `test/host/test_weather.c`, `test/host/test_ui_fields.c`, `test/host/context_fixtures.h`, `test/host/dashboard_fixtures.h`
- Create: `test/host/fixtures/open-meteo/forecast_bergen_2026-10-02.json`, three goldens

**Interfaces:**
- Consumes: M5's `ds_weather_t`, `ds_set_weather()`, `ds_weather_freshness()`, `weather_parse_forecast()`, `lang_format_decimal()`.
- Produces (Task 9 builds on the field list):
  - `ds_rain_t` (`t0`, `mm10[DS_RAIN_STEPS]`, `prob[DS_RAIN_STEPS]`), `DS_RAIN_STEPS` 96, `DS_RAIN_STEP_S` 900, `DS_RAIN_NONE`; `ds_weather_t.rain`; `int ds_rain_index(uint32_t t0, time_t t)`: the entry whose quarter hour holds `t`, the one that ends at or after it.
  - `UI_FIELD_WX_RAIN2H` ("wx.rain2h", kind `UI_FK_SERIES`), `UI_RAIN_STEPS` 8, `ui_value_t.rain_mm10[]`, `rain_prob[]`; the strings `LS_RAIN_2H`, `LS_RAIN_NOW`, `LS_RAIN_FROM`, `LS_DRY_2H`, `LS_MM_PER_H`.
  - The RTC-RAM snapshot is version 7 and `/fs/state/datastore.bin` version 2: an older one is left out, as its layout differs.

Each `minutely_15` amount is the sum of the 15 minutes *before* its time (Open-Meteo's docs), so the quarter hour now is the entry that ends at the next quarter-hour mark. The line says when the rain is: "Rain now · 1.2 mm/h" when the quarter hour now has 0.1 mm or more (its amount × 4 as a rate), "Rain from 21:45" with the start of the first quarter hour that does, "Dry for 2 h" when none does. The field is missing unless all 8 quarter hours lie within the stored day, or when none of the 8 has an amount (Open-Meteo's nulls: no data, not a dry spell); it is stale like the forecast. The bars are a third, two thirds or the whole height for light, moderate or heavy rain (below 2.5 mm/h, below 7.6 mm/h, above: the usual intensity bounds), solid when the probability is 50 % or more and an outline below it.

- [ ] **Step 1: Write the failing tests.** The Bergen fixture is Open-Meteo's reply with `minutely_15` for 60.39 N, 5.32 E at 00:57 local on 2026-10-02, drizzling (0.1 mm in the first quarter hour). The context fixtures gain a shower from 21:45 CEST on the fixture day (its first wet entry is the one labelled 22:00, as each is labelled by its end), and two helpers that make it rain now or stay dry.

`test/host/test_datastore_weather.c`:

```diff
--- a/test/host/test_datastore_weather.c
+++ b/test/host/test_datastore_weather.c
@@ -108,6 +108,20 @@ static void test_hour_index_follows_the_first_entry(void)
     TEST_ASSERT_EQUAL_INT(-1, ds_hour_index(0, HOUR0)); /* never fetched */
 }
 
+static void test_the_rain_steps_follow_their_quarter_hours(void)
+{
+    /* Open-Meteo sums the 15 minutes before each time: the entry at 06:15 holds 06:00-06:15 */
+    const uint32_t t0 = HOUR0 + 6 * 3600 + 900; /* 06:15 */
+    TEST_ASSERT_EQUAL_INT(-1, ds_rain_index(t0, t0 - 900));
+    TEST_ASSERT_EQUAL_INT(0, ds_rain_index(t0, t0 - 899));
+    TEST_ASSERT_EQUAL_INT(0, ds_rain_index(t0, t0));
+    TEST_ASSERT_EQUAL_INT(1, ds_rain_index(t0, t0 + 1));
+    TEST_ASSERT_EQUAL_INT(1, ds_rain_index(t0, t0 + 900));
+    TEST_ASSERT_EQUAL_INT(DS_RAIN_STEPS - 1, ds_rain_index(t0, t0 + (DS_RAIN_STEPS - 1) * 900));
+    TEST_ASSERT_EQUAL_INT(-1, ds_rain_index(t0, t0 + (DS_RAIN_STEPS - 1) * 900 + 1)); /* past the 24 h (spec §6) */
+    TEST_ASSERT_EQUAL_INT(-1, ds_rain_index(0, HOUR0));                               /* none came */
+}
+
 static void test_air_quality_now_is_this_hours_entry(void)
 {
     ds_air_t a;
@@ -155,6 +169,7 @@ int main(void)
     RUN_TEST(test_a_forecast_is_missing_past_its_last_hour);
     RUN_TEST(test_now_is_the_current_block_for_an_hour_then_the_hourly_entry);
     RUN_TEST(test_hour_index_follows_the_first_entry);
+    RUN_TEST(test_the_rain_steps_follow_their_quarter_hours);
     RUN_TEST(test_air_quality_now_is_this_hours_entry);
     RUN_TEST(test_a_new_fetch_replaces_the_old_one_whole);
     return UNITY_END();
```


`test/host/test_weather.c`:

```diff
--- a/test/host/test_weather.c
+++ b/test/host/test_weather.c
@@ -37,6 +37,7 @@ static void test_the_requests_ask_for_what_the_spec_lists(void)
                              "&hourly=temperature_2m,weather_code,precipitation_probability"
                              "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
                              "precipitation_probability_max,sunrise,sunset"
+                             "&minutely_15=precipitation,precipitation_probability&forecast_minutely_15=96"
                              "&timezone=auto&timeformat=unixtime&forecast_days=3",
                              url);
     TEST_ASSERT_TRUE(weather_air_url(url, sizeof(url), -338688, -1512093) > 0); /* Sydney: both signs */
@@ -84,6 +85,48 @@ static void test_a_forecast_parses_into_the_datastore_form(void)
     TEST_ASSERT_EQUAL_INT16(225, w.days[2].max_c10);
 }
 
+/* Bergen at 00:57 local on 2026-10-02, drizzling: 0.1 mm in the first quarter hour (spec §11.4). */
+static void test_the_rain_in_quarter_hours_comes_with_the_forecast(void)
+{
+    ds_weather_t w;
+    const char *json = fixture("forecast_bergen_2026-10-02.json");
+    TEST_ASSERT_TRUE_MESSAGE(weather_parse_forecast(json, s_len, &w, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT32(1790895600u, w.rain.t0); /* 22:45 UTC: the quarter hour of the fetch */
+    TEST_ASSERT_EQUAL_UINT8(1, w.rain.mm10[0]);
+    TEST_ASSERT_EQUAL_UINT8(0, w.rain.mm10[1]);
+    TEST_ASSERT_EQUAL_UINT8(100, w.rain.prob[0]);
+    TEST_ASSERT_EQUAL_UINT8(97, w.rain.prob[5]);
+    TEST_ASSERT_EQUAL_UINT8(27, w.rain.prob[DS_RAIN_STEPS - 1]);
+    TEST_ASSERT_EQUAL_UINT32(1790892000u, w.hour0); /* the rest as before: Oslo's midnight */
+
+    json = fixture("forecast_brno_2026-10-01.json"); /* an M5 reply, without them */
+    TEST_ASSERT_TRUE_MESSAGE(weather_parse_forecast(json, s_len, &w, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT32(0, w.rain.t0);
+}
+
+static void test_rain_is_capped_and_its_gaps_stay_missing(void)
+{
+    static const char k_json[] =
+        "{\"hourly\":{\"time\":[1790892000]},"
+        "\"minutely_15\":{\"time\":[1790895600,1790896500,1790897400],"
+        "\"precipitation\":[30.0,null,0.04],\"precipitation_probability\":[null,120,55]}}";
+    ds_weather_t w;
+    TEST_ASSERT_TRUE_MESSAGE(weather_parse_forecast(k_json, strlen(k_json), &w, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT32(1790895600u, w.rain.t0);
+    TEST_ASSERT_EQUAL_UINT8(254, w.rain.mm10[0]); /* 25.4 mm at most */
+    TEST_ASSERT_EQUAL_UINT8(DS_RAIN_NONE, w.rain.mm10[1]);
+    TEST_ASSERT_EQUAL_UINT8(0, w.rain.mm10[2]); /* 0.04 mm rounds to none */
+    TEST_ASSERT_EQUAL_UINT8(DS_RAIN_NONE, w.rain.prob[0]);
+    TEST_ASSERT_EQUAL_UINT8(100, w.rain.prob[1]);
+    TEST_ASSERT_EQUAL_UINT8(DS_RAIN_NONE, w.rain.mm10[3]); /* beyond the series */
+
+    static const char k_gap[] = /* not 15 minutes apart: no rain series, the forecast still stands */
+        "{\"hourly\":{\"time\":[1790892000]},"
+        "\"minutely_15\":{\"time\":[1790895600,1790897400],\"precipitation\":[1.0,1.0]}}";
+    TEST_ASSERT_TRUE_MESSAGE(weather_parse_forecast(k_gap, strlen(k_gap), &w, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT32(0, w.rain.t0);
+}
+
 static void test_nulls_and_short_series_stay_missing(void)
 {
     static const char k_json[] =
@@ -279,6 +322,8 @@ int main(void)
     RUN_TEST(test_the_requests_ask_for_what_the_spec_lists);
     RUN_TEST(test_the_place_search_encodes_the_query);
     RUN_TEST(test_a_forecast_parses_into_the_datastore_form);
+    RUN_TEST(test_the_rain_in_quarter_hours_comes_with_the_forecast);
+    RUN_TEST(test_rain_is_capped_and_its_gaps_stay_missing);
     RUN_TEST(test_nulls_and_short_series_stay_missing);
     RUN_TEST(test_a_reply_that_is_no_forecast_is_refused);
     RUN_TEST(test_air_quality_parses_with_daily_pollen_peaks);
```


`test/host/test_ui_fields.c`:

```diff
--- a/test/host/test_ui_fields.c
+++ b/test/host/test_ui_fields.c
@@ -143,6 +143,69 @@ static void test_the_hourly_strip_starts_at_the_next_hour_every_two_hours(void)
     TEST_ASSERT_EQUAL_STRING("Sun", v.series[2].label);
 }
 
+static void test_rain_in_the_next_two_hours_says_when(void)
+{
+    ui_context_t ctx = fixture_context();
+    fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
+    ui_value_t v;
+    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
+    TEST_ASSERT_EQUAL(UI_FK_SERIES, v.kind);
+    TEST_ASSERT_EQUAL_INT(UI_RAIN_STEPS, v.series_count);
+    TEST_ASSERT_EQUAL_STRING("Rain from 21:45", v.text); /* 20:48 now: the fifth quarter hour */
+    TEST_ASSERT_EQUAL_STRING("", v.extra);
+    TEST_ASSERT_EQUAL_UINT8(0, v.rain_mm10[0]); /* 20:45-21:00, Open-Meteo's entry at 21:00 */
+    TEST_ASSERT_EQUAL_UINT8(2, v.rain_mm10[4]); /* 21:45-22:00 */
+    TEST_ASSERT_EQUAL_UINT8(40, v.rain_prob[4]);
+    ctx.clock_24h = false;
+    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
+    TEST_ASSERT_EQUAL_STRING("Rain from 9:45 PM", v.text);
+
+    fixture_rain_now(&s_fix_ds);
+    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
+    TEST_ASSERT_EQUAL_STRING("Rain now", v.text);
+    TEST_ASSERT_EQUAL_STRING("1.2 mm/h", v.extra); /* 0.3 mm in the quarter hour */
+    ctx.lang = lang_get("cs");
+    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
+    TEST_ASSERT_EQUAL_STRING("Prší", v.text);
+    TEST_ASSERT_EQUAL_STRING("1,2 mm/h", v.extra);
+
+    fixture_rain_dry(&s_fix_ds);
+    ctx.lang = lang_get("en");
+    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
+    TEST_ASSERT_EQUAL_STRING("Dry for 2 h", v.text);
+}
+
+static void test_rain_needs_its_two_hours_stored_and_ages_like_the_forecast(void)
+{
+    ui_context_t ctx = fixture_context();
+    fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
+    ui_value_t v;
+    ctx.now = (time_t)ds_weather(&s_fix_ds)->rain.t0 + 23 * 3600; /* four quarter hours left */
+    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
+    ctx.now = (time_t)ds_weather(&s_fix_ds)->rain.t0 + 22 * 3600; /* just the two hours */
+    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
+
+    ctx = fixture_context();
+    fixture_forecast(&s_fix_ds, FIX_NOW - 3 * 3600);
+    ds_set_forecast_ttl(&s_fix_ds, 2 * 3600); /* an interval sync of an hour that missed two */
+    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
+    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
+    TEST_ASSERT_EQUAL_UINT32(3 * 3600, v.age_s);
+
+    ds_weather_t w = *ds_weather(&s_fix_ds);
+    memset(w.rain.mm10, DS_RAIN_NONE, sizeof(w.rain.mm10)); /* Open-Meteo sent nulls: no data, not a dry spell */
+    ds_set_weather(&s_fix_ds, &w);
+    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
+    w.rain.t0 = 0; /* a forecast from before M6 */
+    ds_set_weather(&s_fix_ds, &w);
+    ui_resolve(&ctx, UI_FIELD_WX_RAIN2H, &v);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
+}
+
 static void test_a_forecast_older_than_its_ttl_is_stale(void)
 {
     fixture_forecast(&s_fix_ds, FIX_NOW - 30 * 3600);
@@ -236,6 +299,8 @@ int main(void)
     RUN_TEST(test_english_has_no_name_days_or_holidays_and_weather_waits_for_a_sync);
     RUN_TEST(test_the_weather_now_and_today_come_from_the_forecast);
     RUN_TEST(test_the_hourly_strip_starts_at_the_next_hour_every_two_hours);
+    RUN_TEST(test_rain_in_the_next_two_hours_says_when);
+    RUN_TEST(test_rain_needs_its_two_hours_stored_and_ages_like_the_forecast);
     RUN_TEST(test_a_forecast_older_than_its_ttl_is_stale);
     RUN_TEST(test_the_sun_rises_and_sets_over_brno);
     RUN_TEST(test_air_quality_and_pollen_name_their_band_and_level);
```


`test/host/context_fixtures.h`:

```diff
--- a/test/host/context_fixtures.h
+++ b/test/host/context_fixtures.h
@@ -65,6 +65,17 @@ static inline void fixture_forecast(ds_t *ds, time_t fetched)
     w.days[0] = (ds_wx_day_t){ .min_c10 = 88, .max_c10 = 183, .code = 61, .precip = 60 };
     w.days[1] = (ds_wx_day_t){ .min_c10 = 61, .max_c10 = 157, .code = 3, .precip = 20 };
     w.days[2] = (ds_wx_day_t){ .min_c10 = 32, .max_c10 = 171, .code = 0, .precip = 0 };
+    /* a shower of five quarter hours from 21:45 CEST on the fixture's day: the entries from 22:00, as
+     * each holds the 15 minutes before its time */
+    static const uint8_t k_mm10[5] = { 2, 5, 9, 6, 3 }, k_prob[5] = { 40, 60, 80, 70, 55 };
+    const time_t shower = (time_t)FIX_DAY * 86400 + 20 * 3600;
+    w.rain.t0 = (uint32_t)(fetched - fetched % 900); /* the quarter hour of the fetch, as Open-Meteo sends */
+    for (int i = 0; i < DS_RAIN_STEPS; i++) {
+        time_t t = (time_t)w.rain.t0 + 900 * i;
+        int k = t >= shower ? (int)((t - shower) / 900) : -1;
+        w.rain.mm10[i] = k >= 0 && k < 5 ? k_mm10[k] : 0;
+        w.rain.prob[i] = k >= 0 && k < 5 ? k_prob[k] : 10;
+    }
     ds_set_weather(ds, &w);
     memset(&a, 0, sizeof(a));
     a.fetched = (uint32_t)fetched;
@@ -86,6 +97,31 @@ static inline void fixture_forecast(ds_t *ds, time_t fetched)
     ds_take_changes(ds);
 }
 
+/* The stored forecast's rain changed: raining at 20:45 CEST (1.2 mm/h), easing off, one unlikely
+ * quarter hour; or dry all day. */
+static inline void fixture_rain_now(ds_t *ds)
+{
+    static ds_weather_t w;
+    w = *ds_weather(ds);
+    static const uint8_t k_mm10[4] = { 3, 4, 2, 1 }, k_prob[4] = { 90, 85, 45, 30 };
+    int i0 = ds_rain_index(w.rain.t0, FIX_NOW);
+    for (int i = 0; i < DS_RAIN_STEPS; i++) {
+        bool rain = i >= i0 && i < i0 + 4;
+        w.rain.mm10[i] = rain ? k_mm10[i - i0] : 0;
+        w.rain.prob[i] = rain ? k_prob[i - i0] : 5;
+    }
+    ds_set_weather(ds, &w);
+}
+
+static inline void fixture_rain_dry(ds_t *ds)
+{
+    static ds_weather_t w;
+    w = *ds_weather(ds);
+    memset(w.rain.mm10, 0, sizeof(w.rain.mm10));
+    memset(w.rain.prob, 0, sizeof(w.rain.prob));
+    ds_set_weather(ds, &w);
+}
+
 /* The fixtures' zone, Europe/Prague: the forecast's hours and the sun are in local time. The file
  * that includes this one defines _POSIX_C_SOURCE first, for setenv(). */
 static inline void fixture_zone(void)
```


`test/host/dashboard_fixtures.h`:

```diff
--- a/test/host/dashboard_fixtures.h
+++ b/test/host/dashboard_fixtures.h
@@ -153,6 +153,23 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
         ctx->wifi = UI_WIFI_REJOINING;
         ctx->sync = UI_SYNC_FAILED;
         ctx->web_session = true;
+    } else if (strcmp(name, "weather_rain") == 0) { /* M6 (D27): rain in the next 2 h for the hours */
+        *preset = fixture_preset("weather");
+        preset->slots[2] = UI_FIELD_WX_RAIN2H;
+        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
+    } else if (strcmp(name, "focus_rain_now") == 0) { /* raining now; one unlikely quarter hour */
+        *preset = fixture_preset("focus");
+        preset->slots[1] = UI_FIELD_WX_RAIN2H;
+        preset->slots[2] = UI_FIELD_WX_HOURLY;
+        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
+        fixture_rain_now(&s_fix_ds);
+    } else if (strcmp(name, "grid_rain_cs") == 0) { /* narrow cells, in Czech: raining now, and dry */
+        *preset = fixture_preset("indoor");
+        preset->slots[0] = UI_FIELD_WX_RAIN2H;
+        preset->slots[1] = UI_FIELD_WX_NOW;
+        ctx->lang = lang_get("cs");
+        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
+        fixture_rain_now(&s_fix_ds);
     } else if (strcmp(name, "grid_clock_12h") == 0) { /* a clock in a grid cell, 12-hour */
         *preset = fixture_preset("indoor");
         preset->slots[0] = UI_FIELD_TIME_CLOCK;
@@ -172,4 +189,5 @@ static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather",
                                                     "home_web", "home_stale_web", "weather_now",
                                                     "weather_noon_cs", "weather_stale", "air_grid", "air_grid_cs", "grid_sun_uv", "home_forecast",
                                                     "focus_forecast", "home_syncing", "home_sync_failed",
-                                                    "home_always", "home_always_rejoining" };
+                                                    "home_always", "home_always_rejoining", "weather_rain",
+                                                    "focus_rain_now", "grid_rain_cs" };
```


Copy the fixture (one 4.7 KB line, as Open-Meteo sends it) and the three goldens from the branch:

```bash
git checkout plan/m6 -- test/host/fixtures/open-meteo/forecast_bergen_2026-10-02.json
```

```bash
git checkout plan/m6 -- \
  test/host/golden/dash_focus_rain_now.pbm \
  test/host/golden/dash_grid_rain_cs.pbm \
  test/host/golden/dash_weather_rain.pbm
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host -- -k 0 2>&1 | grep 'error:' | sed 's/.*error: //' | sort -u`
Expected, among others: `no member named 'rain' in 'ds_weather_t'`, `call to undeclared function 'ds_rain_index'`, `use of undeclared identifier 'DS_RAIN_STEPS'` and `use of undeclared identifier 'UI_FIELD_WX_RAIN2H'`.

- [ ] **Step 3: The datastore.**

`components/datastore/include/datastore.h`:

```diff
--- a/components/datastore/include/datastore.h
+++ b/components/datastore/include/datastore.h
@@ -73,6 +73,18 @@ typedef struct {
     uint8_t precip; /* the day's highest precipitation probability, % */
 } ds_wx_day_t;
 
+/* Rain in quarter hours (spec §6, §11.4, D27): Open-Meteo's `minutely_15`, 24 h from the fetch. */
+#define DS_RAIN_STEPS 96
+#define DS_RAIN_STEP_S 900
+#define DS_RAIN_NONE 0xFF /* a missing amount or probability; amounts are capped at 254 (25.4 mm) */
+
+typedef struct {
+    uint32_t t0;                 /* UTC of mm10[0], on a quarter hour; 0 = none came */
+    uint8_t mm10[DS_RAIN_STEPS]; /* precipitation in the quarter hour up to t0 + 900 i, 0.1 mm (Open-Meteo sums
+                                  * the 15 minutes before each time) */
+    uint8_t prob[DS_RAIN_STEPS]; /* its probability, % */
+} ds_rain_t;
+
 typedef struct {
     uint32_t fetched; /* UTC seconds; 0 = never */
     uint32_t now_time; /* the `current` block: when it was valid (UTC) */
@@ -83,6 +95,7 @@ typedef struct {
     ds_wx_hour_t hours[DS_WX_HOURS];
     int32_t day0_local; /* days[0] as days since 1970-01-01, the location's local date */
     ds_wx_day_t days[DS_WX_DAYS];
+    ds_rain_t rain; /* M6 */
 } ds_weather_t;
 
 typedef enum {
@@ -167,5 +180,8 @@ ds_freshness_t ds_weather_freshness(const ds_t *ds, time_t now);
 ds_freshness_t ds_air_freshness(const ds_t *ds, time_t now);
 /* The hourly entry that holds `t`, from `hour0`: 0..DS_WX_HOURS-1, or -1 outside them (or hour0 0). */
 int ds_hour_index(uint32_t hour0, time_t t);
+/* The entry whose quarter hour holds `t`, the one that ends at or after it: 0..DS_RAIN_STEPS-1, or -1
+ * outside them (or t0 0). */
+int ds_rain_index(uint32_t t0, time_t t);
 /* False before the first fetch, or when neither the current block nor this hour's entry exists. */
 bool ds_weather_now(const ds_t *ds, time_t now, ds_wx_now_t *out);
```


`components/datastore/datastore.c`:

```diff
--- a/components/datastore/datastore.c
+++ b/components/datastore/datastore.c
@@ -179,6 +179,16 @@ int ds_hour_index(uint32_t hour0, time_t t)
     return i < DS_WX_HOURS ? (int)i : -1;
 }
 
+int ds_rain_index(uint32_t t0, time_t t)
+{
+    if (t0 == 0 || t <= (time_t)t0 - DS_RAIN_STEP_S) {
+        return -1;
+    }
+    /* entry i holds the quarter hour (t0 + 900 (i - 1), t0 + 900 i] */
+    time_t i = (t - (time_t)t0 + DS_RAIN_STEP_S - 1) / DS_RAIN_STEP_S;
+    return i < DS_RAIN_STEPS ? (int)i : -1;
+}
+
 static ds_freshness_t forecast_freshness(const ds_t *ds, uint32_t fetched, uint32_t hour0, time_t now)
 {
     if (fetched == 0 || ds_hour_index(hour0, now) < 0) {
```


- [ ] **Step 4: The request and the parser.** A `minutely_15` whose times aren't 15 minutes apart is left out, `t0` 0, and the forecast still stands.

`components/weather/weather_url.c`:

```diff
--- a/components/weather/weather_url.c
+++ b/components/weather/weather_url.c
@@ -29,6 +29,7 @@ size_t weather_forecast_url(char *out, size_t size, int32_t lat_e4, int32_t lon_
                                "&hourly=temperature_2m,weather_code,precipitation_probability"
                                "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
                                "precipitation_probability_max,sunrise,sunset"
+                               "&minutely_15=precipitation,precipitation_probability&forecast_minutely_15=96"
                                "&timezone=auto&timeformat=unixtime&forecast_days=3",
                                lat, lon),
                       size);
```


`components/weather/weather_parse.c`:

```diff
--- a/components/weather/weather_parse.c
+++ b/components/weather/weather_parse.c
@@ -105,6 +105,42 @@ static cJSON *open_reply(const char *json, size_t len, double *hour0, double *of
     return root;
 }
 
+/* An amount in 0.1 mm, capped at 254; DS_RAIN_NONE when missing. */
+static uint8_t rain_tenths(const cJSON *item)
+{
+    double v;
+    if (!number(item, &v)) {
+        return DS_RAIN_NONE;
+    }
+    long t = rounded(v * 10.0);
+    return (uint8_t)(t < 0 ? 0 : t > 254 ? 254 : t);
+}
+
+/* `minutely_15` (spec §11.4): left out, t0 0, unless its times are 15 minutes apart. A forecast
+ * without it still stands. */
+static void parse_rain(const cJSON *root, ds_rain_t *out)
+{
+    memset(out->mm10, DS_RAIN_NONE, sizeof(out->mm10));
+    memset(out->prob, DS_RAIN_NONE, sizeof(out->prob));
+    const cJSON *m = member(root, "minutely_15");
+    const cJSON *times = member(m, "time");
+    int n = cJSON_IsArray(times) ? cJSON_GetArraySize(times) : 0;
+    double t0 = 0, t;
+    bool ok = n > 0 && number_at(times, 0, &t0) && t0 > 0;
+    for (int i = 1; ok && i < n && i < DS_RAIN_STEPS; i++) {
+        ok = number_at(times, i, &t) && t == t0 + (double)DS_RAIN_STEP_S * i;
+    }
+    if (!ok) {
+        return;
+    }
+    out->t0 = (uint32_t)t0;
+    const cJSON *mm = member(m, "precipitation"), *prob = member(m, "precipitation_probability");
+    for (int i = 0; i < n && i < DS_RAIN_STEPS; i++) {
+        out->mm10[i] = rain_tenths(cJSON_IsArray(mm) ? cJSON_GetArrayItem(mm, i) : NULL);
+        out->prob[i] = (uint8_t)whole(cJSON_IsArray(prob) ? cJSON_GetArrayItem(prob, i) : NULL, 100, DS_RAIN_NONE);
+    }
+}
+
 bool weather_parse_forecast(const char *json, size_t len, ds_weather_t *out, char *err, size_t err_size)
 {
     double hour0, offset;
@@ -156,6 +192,7 @@ bool weather_parse_forecast(const char *json, size_t len, ds_weather_t *out, cha
                                      DS_WX_NO_PCT),
         };
     }
+    parse_rain(root, &out->rain);
     cJSON_Delete(root);
     return true;
 }
```


- [ ] **Step 5: The strings.**

`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -173,6 +173,11 @@ typedef enum {
     LS_UV_HIGH,
     LS_UV_VERY_HIGH,
     LS_UV_EXTREME,
+    LS_RAIN_2H,   /* wx.rain2h (spec §11.4, D27) */
+    LS_RAIN_NOW,  /* followed by " · 1.2 mm/h" */
+    LS_RAIN_FROM, /* followed by " 21:45" */
+    LS_DRY_2H,
+    LS_MM_PER_H,
     LS_COUNT,
 } lang_str_t;
 
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -181,6 +181,11 @@ const lang_t lang_en = {
         [LS_UV_HIGH] = "High",
         [LS_UV_VERY_HIGH] = "Very high",
         [LS_UV_EXTREME] = "Extreme",
+        [LS_RAIN_2H] = "Rain, 2 h",
+        [LS_RAIN_NOW] = "Rain now",
+        [LS_RAIN_FROM] = "Rain from",
+        [LS_DRY_2H] = "Dry for 2 h",
+        [LS_MM_PER_H] = "mm/h",
     },
     .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
     .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -221,6 +221,11 @@ const lang_t lang_cs = {
         [LS_UV_HIGH] = "Vysoký",
         [LS_UV_VERY_HIGH] = "Velmi vysoký",
         [LS_UV_EXTREME] = "Extrémní",
+        [LS_RAIN_2H] = "Déšť do 2 h",
+        [LS_RAIN_NOW] = "Prší",
+        [LS_RAIN_FROM] = "Déšť od",
+        [LS_DRY_2H] = "2 h bez deště",
+        [LS_MM_PER_H] = "mm/h",
     },
     .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
     .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
```


- [ ] **Step 6: The field and its strip.**

`components/ui/include/ui_fields.h`:

```diff
--- a/components/ui/include/ui_fields.h
+++ b/components/ui/include/ui_fields.h
@@ -64,6 +64,7 @@ typedef enum {
     UI_FIELD_POLLEN_MUGWORT,
     UI_FIELD_POLLEN_OLIVE,
     UI_FIELD_POLLEN_RAGWEED,
+    UI_FIELD_WX_RAIN2H, /* M6 (D27) */
     UI_FIELD_COUNT,
 } ui_field_id_t;
 
@@ -92,6 +93,7 @@ typedef enum {
 } ui_wifi_mark_t;
 
 #define UI_SERIES_MAX 6
+#define UI_RAIN_STEPS 8 /* wx.rain2h: two hours of quarter hours */
 
 /* One column of a forecast strip. */
 typedef struct {
@@ -146,6 +148,8 @@ typedef struct {
     int bands;         /* UI_FK_LEVEL: how many bands its scale has; `percent` is the one it is in */
     int series_count;
     ui_series_point_t series[UI_SERIES_MAX];
+    uint8_t rain_mm10[UI_RAIN_STEPS]; /* wx.rain2h: each quarter hour from now, as ds_rain_t keeps them */
+    uint8_t rain_prob[UI_RAIN_STEPS];
 } ui_value_t;
 
 void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
```


`components/ui/ui_fields.c`:

```diff
--- a/components/ui/ui_fields.c
+++ b/components/ui/ui_fields.c
@@ -38,6 +38,7 @@ static const ui_field_info_t k_fields[UI_FIELD_COUNT] = {
     [UI_FIELD_POLLEN_MUGWORT] = { "pollen.mugwort", UI_FK_POLLEN, LS_POLLEN_MUGWORT, -1 },
     [UI_FIELD_POLLEN_OLIVE] = { "pollen.olive", UI_FK_POLLEN, LS_POLLEN_OLIVE, -1 },
     [UI_FIELD_POLLEN_RAGWEED] = { "pollen.ragweed", UI_FK_POLLEN, LS_POLLEN_RAGWEED, -1 },
+    [UI_FIELD_WX_RAIN2H] = { "wx.rain2h", UI_FK_SERIES, LS_RAIN_2H, -1 },
 };
 
 const ui_field_info_t *ui_field_info(ui_field_id_t field)
```


`components/ui/ui_forecast.c`:

```diff
--- a/components/ui/ui_forecast.c
+++ b/components/ui/ui_forecast.c
@@ -247,6 +247,45 @@ static void resolve_daily(const ui_context_t *ctx, ui_value_t *out)
     }
 }
 
+/* wx.rain2h (spec §11.4, D27): the next 8 quarter hours, and when the rain is. Missing unless all 8
+ * are stored. */
+static void resolve_rain(const ui_context_t *ctx, ui_value_t *out)
+{
+    const ds_weather_t *w = ds_weather(ctx->ds);
+    int i0 = w != NULL ? ds_rain_index(w->rain.t0, ctx->now) : -1;
+    if (i0 < 0 || i0 + UI_RAIN_STEPS > DS_RAIN_STEPS ||
+        !forecast_state(ds_weather_freshness(ctx->ds, ctx->now), w->fetched, ctx, out)) {
+        out->state = UI_VALUE_MISSING;
+        return;
+    }
+    int first = -1, known = 0; /* the first quarter hour with 0.1 mm or more; those with an amount */
+    for (int i = 0; i < UI_RAIN_STEPS; i++) {
+        out->rain_mm10[i] = w->rain.mm10[i0 + i];
+        out->rain_prob[i] = w->rain.prob[i0 + i];
+        known += out->rain_mm10[i] != DS_RAIN_NONE;
+        if (first < 0 && out->rain_mm10[i] != DS_RAIN_NONE && out->rain_mm10[i] > 0) {
+            first = i;
+        }
+    }
+    if (known == 0) {
+        out->state = UI_VALUE_MISSING; /* nulls: no data, not a dry spell */
+        return;
+    }
+    out->series_count = UI_RAIN_STEPS;
+    if (first == 0) {
+        snprintf(out->text, sizeof(out->text), "%s", lang_str(ctx->lang, LS_RAIN_NOW));
+        char rate[12];
+        lang_format_decimal(ctx->lang, (long)out->rain_mm10[0] * 4, 1, rate, sizeof(rate)); /* per hour */
+        snprintf(out->extra, sizeof(out->extra), "%s %s", rate, lang_str(ctx->lang, LS_MM_PER_H));
+    } else if (first > 0) {
+        char at[12]; /* where its quarter hour starts */
+        clock_text(ctx, (time_t)w->rain.t0 + (time_t)(i0 + first - 1) * DS_RAIN_STEP_S, at, sizeof(at));
+        snprintf(out->text, sizeof(out->text), "%s %s", lang_str(ctx->lang, LS_RAIN_FROM), at);
+    } else {
+        snprintf(out->text, sizeof(out->text), "%s", lang_str(ctx->lang, LS_DRY_2H));
+    }
+}
+
 /* ---- air quality and pollen ---- */
 
 static const lang_str_t k_bands[] = { LS_AQ_GOOD, LS_AQ_FAIR, LS_AQ_MODERATE,
@@ -352,6 +391,9 @@ bool ui_resolve_forecast(const ui_context_t *ctx, ui_field_id_t field, ui_value_
     case UI_FIELD_WX_DAILY:
         resolve_daily(ctx, out);
         return true;
+    case UI_FIELD_WX_RAIN2H:
+        resolve_rain(ctx, out);
+        return true;
     case UI_FIELD_SUN_TIMES:
         resolve_sun(ctx, out);
         return true;
@@ -605,6 +647,62 @@ static void draw_series(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
     }
 }
 
+/* wx.rain2h: its words over 8 bars, one a quarter hour (D27). A bar is a third, two thirds or the
+ * whole height for light, moderate or heavy rain (below 2.5 mm/h, below 7.6 mm/h, above); a likely
+ * one is solid, one under 50 % an outline; a dry one leaves the baseline. */
+static void draw_rain(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
+{
+    const gfx_font_t *f = &gfx_font_sans_bold_16;
+    int max_w = r.w - 12;
+    char line1[sizeof(v->text) + sizeof(v->extra) + 4], line2[sizeof(line1)] = "";
+    snprintf(line1, sizeof(line1), "%s%s%s", v->text, v->extra[0] ? " \xC2\xB7 " : "", v->extra);
+    if (gfx_text_width(f, line1) > max_w) { /* two lines: the words over the rate, or split at a space */
+        if (v->extra[0]) {
+            snprintf(line1, sizeof(line1), "%s", v->text);
+            snprintf(line2, sizeof(line2), "%s", v->extra);
+        } else {
+            ui_split_two_lines(f, v->text, max_w, line1, line2, sizeof(line1));
+        }
+    }
+    int text_h = (line2[0] ? 2 : 1) * f->line_height;
+    int room = r.h - 12 - (v->state == UI_VALUE_STALE ? 18 : 0) - text_h - 8 - 5; /* the age mark goes below */
+    int bar_h = room < 36 ? room : 36;
+    if (bar_h < 9) {
+        bar_h = 0; /* no room for bars: the words alone */
+    }
+    int block = text_h + (bar_h ? 8 + bar_h + 5 : 0);
+    int top = v->state == UI_VALUE_STALE ? r.y + 6 : r.y + (r.h - block) / 2;
+    centred(fb, f, r, top + f->ascent, line1);
+    if (line2[0]) {
+        centred(fb, f, r, top + f->line_height + f->ascent, line2);
+    }
+    if (bar_h == 0) {
+        return;
+    }
+    int base = top + text_h + 8 + bar_h;
+    int w = r.w - 16;
+    int pitch = w / UI_RAIN_STEPS;
+    int x0 = r.x + 8 + (w - pitch * UI_RAIN_STEPS) / 2;
+    for (int i = 0; i < UI_RAIN_STEPS; i++) {
+        int mm10 = v->rain_mm10[i];
+        int level = mm10 == DS_RAIN_NONE ? 0 : mm10 >= 19 ? 3 : mm10 >= 6 ? 2 : mm10 >= 1 ? 1 : 0;
+        if (level == 0) {
+            continue;
+        }
+        int h = level * bar_h / 3;
+        gfx_rect_t bar = { (int16_t)(x0 + i * pitch + 1), (int16_t)(base - h), (int16_t)(pitch - 3), (int16_t)h };
+        if (v->rain_prob[i] != DS_RAIN_NONE && v->rain_prob[i] < 50) {
+            gfx_rect(fb, bar, GFX_BLACK);
+        } else {
+            gfx_fill_rect(fb, bar, GFX_BLACK);
+        }
+    }
+    gfx_hline(fb, x0, base, pitch * UI_RAIN_STEPS, GFX_BLACK);
+    for (int i = 0; i <= UI_RAIN_STEPS; i += UI_RAIN_STEPS / 2) { /* now, in 1 h, in 2 h */
+        gfx_vline(fb, x0 + i * pitch - (i == UI_RAIN_STEPS ? 1 : 0), base, 5, GFX_BLACK);
+    }
+}
+
 static void draw_sun(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
 {
     int top = r.y;
@@ -714,7 +812,11 @@ bool ui_forecast_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value
         draw_weather_day(fb, r, size, v);
         return true;
     case UI_FK_SERIES:
-        draw_series(fb, r, v);
+        if (v->field == UI_FIELD_WX_RAIN2H) {
+            draw_rain(fb, r, v);
+        } else {
+            draw_series(fb, r, v);
+        }
         return true;
     case UI_FK_SUN:
         draw_sun(fb, r, size, v);
```


- [ ] **Step 7: The versions.** The forecast grows by 196 bytes, so the snapshot (3.6 of its 4 KB) and the forecast file change layout:

`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -49,7 +49,7 @@
 #define TETHER_RECHECK_MS 1000
 #define RETRY_S           300  /* after a failed boot with no PC attached */
 #define SNAP_MAGIC        0x72666c62u /* "rflb" */
-#define SNAP_VERSION      6 /* 6: the weather, the air quality and the syncs' state */
+#define SNAP_VERSION      7 /* 6: the weather, the air quality and the syncs' state; 7: the rain */
 #define PEEK_MS           60000 /* a button during the night shows the dashboard this long (spec §9.1) */
 #define NIGHT_RECHECK_S   60    /* a night sleep with a button held looks again this often (D16) */
 #define CRITICAL_RECHECK_S 600  /* the critical sleep checks again this often if KEY is held */
```


`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -35,7 +35,7 @@ static int64_t s_toast_until_ms;
 #define LEARN_PATH "/fs/state/battery_learn.txt" /* the battery learning session in base64 (D21) */
 #define FORECAST_PATH "/fs/state/datastore.bin"    /* the last weather and air quality (spec §6) */
 #define FORECAST_MAGIC 0x72666366u /* "rfcf" */
-#define FORECAST_VERSION 1
+#define FORECAST_VERSION 2 /* 2: the rain in quarter hours (M6) */
 #define LEARN_TEXT_MAX ((BATTERY_LEARN_PACKED_MAX + 2) / 3 * 4 + 1)
 _Static_assert(SETTINGS_BAT_CURVE_POINTS == BATTERY_CURVE_POINTS, "settings keep a learned curve whole");
 
```


- [ ] **Step 8: Run the tests and look at the goldens.**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `out of 59`, all passing; `test_datastore_weather` 9 tests, `test_weather` 17, `test_ui_fields` 18.

Render the three new goldens and compare them with the branch's: `for n in weather_rain focus_rain_now grid_rain_cs; do build-host/render_dashboard $n /tmp/$n.pbm && cmp /tmp/$n.pbm test/host/golden/dash_$n.pbm; done` prints nothing. `python3 tools/render.py` and look at `captures/render/dash_weather_rain.png` ("Rain from 21:45" over the bars, an outlined one first), `dash_focus_rain_now.png` ("Rain now · 1.2 mm/h") and `dash_grid_rain_cs.png` ("Prší" over "1,2 mm/h" in a narrow cell).

- [ ] **Step 9: The firmware builds.** `tools/idf.sh build`: clean, with no `-Wformat-truncation` from `ui_forecast.c` (GCC sees what clang doesn't: the strip's line buffer is sized for its text and its rate).

- [ ] **Step 10: Commit.**

```bash
git add components/datastore components/weather components/locale components/ui main/app.c main/app_ui.c test/host
git commit -m "feat: rain in the next 2 hours (D27)"
```


### Task 8: The radars' settings (`storage`)

**Files:**
- Modify: `components/storage/include/settings.h`, `components/storage/settings.c`, `main/app_ui.c`, `test/host/test_settings.c`

**Interfaces:**
- Consumes: M5's settings codec.
- Produces (Tasks 11–14): in `settings_t`, `wx_centre_set`, `wx_lat_e4`, `wx_lon_e4`, `wx_zoom_q` (quarters, `SETTINGS_ZOOM_Q_MIN` 16 to `SETTINGS_ZOOM_Q_MAX` 36), `fl_centre_set`, `fl_lat_e4`, `fl_lon_e4`, `fl_range_km` (10–100), `fl_min_alt_ft` (0–60 000), `fl_ground`, `fl_max` (1–100); `void settings_radar_defaults(settings_t *out)`.

A radar's centre counts as its own only when both `lat` and `lon` are given; otherwise it is the location's and follows it, and saving writes neither, so a new location still moves it. The web UI's "the location" sends `"lat": null, "lon": null`, which the merge patch removes (RFC 7396).

- [ ] **Step 1: Write the failing test.**

`test/host/test_settings.c`:

```diff
--- a/test/host/test_settings.c
+++ b/test/host/test_settings.c
@@ -17,6 +17,7 @@ void setUp(void)
                                .sync_mode = SETTINGS_SYNC_TIMES, .sync_time_count = 1, .sync_times = { 330 },
                                .sync_interval_min = 60, .quiet = false, .quiet_from = 1380, .quiet_to = 360,
                                .ntp = { "cz.pool.ntp.org", "pool.ntp.org" } };
+    settings_radar_defaults(&s_defaults);
     memset(&s_out, 0xAA, sizeof(s_out));
     s_err[0] = '\0';
 }
@@ -335,6 +336,91 @@ static void test_the_sync_defaults_are_the_specs(void)
     TEST_ASSERT_EQUAL_STRING("cz.pool.ntp.org", s_out.ntp[0]);
 }
 
+static void test_the_radars_default_to_the_location(void)
+{
+    settings_t defaults;
+    memset(&defaults, 0, sizeof(defaults));
+    settings_radar_defaults(&defaults); /* spec §14.3 */
+    TEST_ASSERT_FALSE(defaults.wx_centre_set);
+    TEST_ASSERT_EQUAL_UINT8(26, defaults.wx_zoom_q); /* 6.5 */
+    TEST_ASSERT_FALSE(defaults.fl_centre_set);
+    TEST_ASSERT_EQUAL_UINT8(50, defaults.fl_range_km);
+    TEST_ASSERT_EQUAL_UINT16(0, defaults.fl_min_alt_ft);
+    TEST_ASSERT_FALSE(defaults.fl_ground);
+    TEST_ASSERT_EQUAL_UINT8(100, defaults.fl_max);
+    /* A file from before M6: both centres sit on its location, and move with it */
+    const char *json = "{\"schema\":1,\"location\":{\"name\":\"Ostrava\",\"lat\":49.8209,\"lon\":18.2625}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_FALSE(s_out.wx_centre_set);
+    TEST_ASSERT_EQUAL_INT32(498209, s_out.wx_lat_e4);
+    TEST_ASSERT_EQUAL_INT32(182625, s_out.wx_lon_e4);
+    TEST_ASSERT_EQUAL_INT32(498209, s_out.fl_lat_e4);
+    TEST_ASSERT_EQUAL_INT32(182625, s_out.fl_lon_e4);
+}
+
+static void test_the_radar_settings_parse_clamp_and_round_trip(void)
+{
+    const char *json = "{\"schema\":1,\"radar\":{"
+                       "\"weather\":{\"lat\":50.0755,\"lon\":14.4378,\"zoom\":7.3},"
+                       "\"flights\":{\"lat\":48.1103,\"lon\":16.5697,\"range_km\":25,\"min_alt_ft\":3000,"
+                       "\"ground\":true,\"max\":40}}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_TRUE(s_out.wx_centre_set);
+    TEST_ASSERT_EQUAL_INT32(500755, s_out.wx_lat_e4);
+    TEST_ASSERT_EQUAL_INT32(144378, s_out.wx_lon_e4);
+    TEST_ASSERT_EQUAL_UINT8(29, s_out.wx_zoom_q); /* 7.3 to the nearest quarter: 7.25 */
+    TEST_ASSERT_TRUE(s_out.fl_centre_set);
+    TEST_ASSERT_EQUAL_INT32(481103, s_out.fl_lat_e4);
+    TEST_ASSERT_EQUAL_UINT8(25, s_out.fl_range_km);
+    TEST_ASSERT_EQUAL_UINT16(3000, s_out.fl_min_alt_ft);
+    TEST_ASSERT_TRUE(s_out.fl_ground);
+    TEST_ASSERT_EQUAL_UINT8(40, s_out.fl_max);
+
+    settings_t again;
+    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"zoom\":\t7.25"));
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(&s_out, &again, sizeof(again));
+
+    json = "{\"schema\":1,\"radar\":{\"weather\":{\"zoom\":12},"
+           "\"flights\":{\"range_km\":5,\"min_alt_ft\":70000,\"max\":0}}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(36, s_out.wx_zoom_q); /* 9 at most */
+    TEST_ASSERT_EQUAL_UINT8(10, s_out.fl_range_km);
+    TEST_ASSERT_EQUAL_UINT16(60000, s_out.fl_min_alt_ft);
+    TEST_ASSERT_EQUAL_UINT8(1, s_out.fl_max);
+    json = "{\"schema\":1,\"radar\":{\"weather\":{\"zoom\":1,\"lat\":50.1},"
+           "\"flights\":{\"range_km\":250,\"min_alt_ft\":-5,\"max\":500,\"ground\":\"yes\"}}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(16, s_out.wx_zoom_q); /* 4 at least */
+    TEST_ASSERT_FALSE(s_out.wx_centre_set);      /* a latitude alone is no centre */
+    TEST_ASSERT_EQUAL_INT32(491951, s_out.wx_lat_e4);
+    TEST_ASSERT_EQUAL_UINT8(100, s_out.fl_range_km);
+    TEST_ASSERT_EQUAL_UINT16(0, s_out.fl_min_alt_ft);
+    TEST_ASSERT_EQUAL_UINT8(100, s_out.fl_max);
+    TEST_ASSERT_FALSE(s_out.fl_ground);
+}
+
+static void test_a_centre_that_follows_the_location_is_not_saved(void)
+{
+    TEST_ASSERT_TRUE(settings_to_json(&s_defaults, NULL, s_json, sizeof(s_json)) > 0);
+    const char *radar = strstr(s_json, "\"radar\"");
+    TEST_ASSERT_NOT_NULL(radar);
+    TEST_ASSERT_NULL(strstr(radar, "\"lat\"")); /* so a new location moves it */
+    TEST_ASSERT_NOT_NULL(strstr(radar, "\"zoom\":\t6.5"));
+    TEST_ASSERT_NOT_NULL(strstr(radar, "\"range_km\":\t50"));
+
+    /* The web UI's "use the location": the merge patch removes a centre */
+    const char *base = "{\"schema\":1,\"radar\":{\"weather\":{\"lat\":50.0755,\"lon\":14.4378,\"zoom\":7}}}";
+    char patched[512];
+    TEST_ASSERT_TRUE(settings_patch(base, "{\"radar\":{\"weather\":{\"lat\":null,\"lon\":null}}}", patched,
+                                    sizeof(patched), s_err, sizeof(s_err)) > 0);
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(patched, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_FALSE(s_out.wx_centre_set);
+    TEST_ASSERT_EQUAL_INT32(491951, s_out.wx_lat_e4);
+    TEST_ASSERT_EQUAL_UINT8(28, s_out.wx_zoom_q);
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -358,5 +444,8 @@ int main(void)
     RUN_TEST(test_sync_times_are_sorted_without_repeats_or_bad_ones);
     RUN_TEST(test_sync_settings_fall_back_one_by_one);
     RUN_TEST(test_the_sync_defaults_are_the_specs);
+    RUN_TEST(test_the_radars_default_to_the_location);
+    RUN_TEST(test_the_radar_settings_parse_clamp_and_round_trip);
+    RUN_TEST(test_a_centre_that_follows_the_location_is_not_saved);
     return UNITY_END();
 }
```


- [ ] **Step 2: Run it to see it fail.**

Run: `cmake --build build-host --target test_settings 2>&1 | grep -m2 error`
Expected: `call to undeclared function 'settings_radar_defaults'` and `no member named 'wx_centre_set' in 'settings_t'`.

- [ ] **Step 3: The settings.** `settings_radar_defaults()` puts both centres on the location already in `out`, so set the location first; the app's defaults call it after their Kconfig location.

`components/storage/include/settings.h`:

```diff
--- a/components/storage/include/settings.h
+++ b/components/storage/include/settings.h
@@ -32,6 +32,9 @@ typedef enum {
     SETTINGS_SYNC_MANUAL,
 } settings_sync_mode_t;
 
+#define SETTINGS_ZOOM_Q_MIN 16 /* radar.weather.zoom in quarters: 4 to 9 (spec §14.3) */
+#define SETTINGS_ZOOM_Q_MAX 36
+
 #define SETTINGS_SYNC_TIMES_MAX 8
 #define SETTINGS_NTP_MAX 2
 #define SETTINGS_HOST_LEN 64
@@ -60,6 +63,15 @@ typedef struct {
     bool quiet;                                         /* quiet hours (D25) */
     uint16_t quiet_from, quiet_to;                      /* minutes after midnight */
     char ntp[SETTINGS_NTP_MAX][SETTINGS_HOST_LEN];      /* time.ntp; "" for an unused entry */
+    bool wx_centre_set;           /* radar.weather.lat and .lon are given; else they follow the location */
+    int32_t wx_lat_e4, wx_lon_e4; /* the weather radar's centre */
+    uint8_t wx_zoom_q;            /* radar.weather.zoom in quarters: 16..36 */
+    bool fl_centre_set;           /* the same for radar.flights */
+    int32_t fl_lat_e4, fl_lon_e4;
+    uint8_t fl_range_km;    /* 10..100: from the centre to the map's top edge */
+    uint16_t fl_min_alt_ft; /* 0..60000 */
+    bool fl_ground;         /* aircraft on the ground too */
+    uint8_t fl_max;         /* aircraft shown at most, 1..100 */
 } settings_t;
 
 /* The sync's and the NTP servers' defaults (spec §14.3): times mode at 05:30, a 60 min interval,
@@ -67,6 +79,10 @@ typedef struct {
  * them, so a settings.json from before M5 syncs. */
 void settings_sync_defaults(settings_t *out);
 
+/* The radars' defaults (spec §14.3): zoom 6.5; a range of 50 km, every altitude, none on the ground,
+ * 100 aircraft; both centres on out->lat_e4 and lon_e4, so set the location first. */
+void settings_radar_defaults(settings_t *out);
+
 /* Fails only if the text is not a JSON object with "schema": 1. */
 bool settings_from_json(const char *json, const settings_t *defaults, settings_t *out, char *err, size_t err_size);
 /* `base_json` (the file as read, or NULL) with the known keys replaced by `s`. Returns the
```


`components/storage/settings.c`:

```diff
--- a/components/storage/settings.c
+++ b/components/storage/settings.c
@@ -198,6 +198,42 @@ static void read_sync(const cJSON *sync, settings_t *out)
     out->quiet_to = to >= 0 ? (uint16_t)to : out->quiet_to;
 }
 
+/* A radar's centre: both coordinates as numbers, or else the location's, which it then follows. */
+static void read_centre(const cJSON *obj, const settings_t *s, bool *set, int32_t *lat, int32_t *lon)
+{
+    const cJSON *la = child(obj, "lat"), *lo = child(obj, "lon");
+    *set = cJSON_IsNumber(la) && cJSON_IsNumber(lo) && isfinite(la->valuedouble) && isfinite(lo->valuedouble);
+    *lat = *set ? (int32_t)read_scaled(obj, "lat", s->lat_e4, 1e4, -850000, 850000) : s->lat_e4; /* web Mercator */
+    *lon = *set ? (int32_t)read_scaled(obj, "lon", s->lon_e4, 1e4, -1800000, 1800000) : s->lon_e4;
+}
+
+static void read_radar(const cJSON *radar, settings_t *out)
+{
+    const cJSON *wx = child(radar, "weather"), *fl = child(radar, "flights");
+    read_centre(wx, out, &out->wx_centre_set, &out->wx_lat_e4, &out->wx_lon_e4);
+    out->wx_zoom_q = (uint8_t)read_scaled(wx, "zoom", out->wx_zoom_q, 4, SETTINGS_ZOOM_Q_MIN, SETTINGS_ZOOM_Q_MAX);
+    read_centre(fl, out, &out->fl_centre_set, &out->fl_lat_e4, &out->fl_lon_e4);
+    out->fl_range_km = (uint8_t)read_scaled(fl, "range_km", out->fl_range_km, 1, 10, 100);
+    out->fl_min_alt_ft = (uint16_t)read_scaled(fl, "min_alt_ft", out->fl_min_alt_ft, 1, 0, 60000);
+    read_bool(fl, "ground", &out->fl_ground);
+    out->fl_max = (uint8_t)read_scaled(fl, "max", out->fl_max, 1, 1, 100);
+}
+
+void settings_radar_defaults(settings_t *out)
+{
+    out->wx_centre_set = false;
+    out->wx_lat_e4 = out->lat_e4;
+    out->wx_lon_e4 = out->lon_e4;
+    out->wx_zoom_q = 26;
+    out->fl_centre_set = false;
+    out->fl_lat_e4 = out->lat_e4;
+    out->fl_lon_e4 = out->lon_e4;
+    out->fl_range_km = 50;
+    out->fl_min_alt_ft = 0;
+    out->fl_ground = false;
+    out->fl_max = 100;
+}
+
 void settings_sync_defaults(settings_t *out)
 {
     out->sync_mode = SETTINGS_SYNC_TIMES;
@@ -267,6 +303,7 @@ bool settings_from_json(const char *json, const settings_t *defaults, settings_t
     }
     read_ntp(child(time, "ntp"), out);
     read_sync(child(root, "sync"), out);
+    read_radar(child(root, "radar"), out); /* after the location, which its centres may follow */
     cJSON_Delete(root);
     return true;
 }
@@ -291,6 +328,18 @@ static void put(cJSON *obj, const char *key, cJSON *value)
     }
 }
 
+/* A centre that follows the location isn't saved, so a new location still moves it. */
+static void put_centre(cJSON *obj, bool set, int32_t lat_e4, int32_t lon_e4)
+{
+    if (set) {
+        put(obj, "lat", cJSON_CreateNumber(lat_e4 / 1e4));
+        put(obj, "lon", cJSON_CreateNumber(lon_e4 / 1e4));
+    } else {
+        cJSON_DeleteItemFromObjectCaseSensitive(obj, "lat");
+        cJSON_DeleteItemFromObjectCaseSensitive(obj, "lon");
+    }
+}
+
 /* The file a save or patch merges into: NULL for none, or one nested past the cap, which the loader
  * rejects too and whose parse could overrun the app task's stack. */
 static cJSON *parse_base(const char *base_json)
@@ -357,6 +406,16 @@ size_t settings_to_json(const settings_t *s, const char *base_json, char *out, s
     put(quiet, "enabled", cJSON_CreateBool(s->quiet));
     put(quiet, "from", hhmm_json(s->quiet_from));
     put(quiet, "to", hhmm_json(s->quiet_to));
+    cJSON *radar = object_at(root, "radar");
+    cJSON *wx = object_at(radar, "weather");
+    put_centre(wx, s->wx_centre_set, s->wx_lat_e4, s->wx_lon_e4);
+    put(wx, "zoom", cJSON_CreateNumber(s->wx_zoom_q / 4.0));
+    cJSON *fl = object_at(radar, "flights");
+    put_centre(fl, s->fl_centre_set, s->fl_lat_e4, s->fl_lon_e4);
+    put(fl, "range_km", cJSON_CreateNumber(s->fl_range_km));
+    put(fl, "min_alt_ft", cJSON_CreateNumber(s->fl_min_alt_ft));
+    put(fl, "ground", cJSON_CreateBool(s->fl_ground));
+    put(fl, "max", cJSON_CreateNumber(s->fl_max));
     bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, true);
     cJSON_Delete(root);
     return ok ? strlen(out) : 0;
```


`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -56,6 +56,7 @@ static void default_settings(settings_t *out)
         .bat_full_mv = BATTERY_FULL_MV,
     };
     settings_sync_defaults(out);
+    settings_radar_defaults(out);
     snprintf(out->place, sizeof(out->place), "%s", CONFIG_REFLBO_LOCATION_NAME);
     snprintf(out->tz_posix, sizeof(out->tz_posix), "%s", CONFIG_REFLBO_TZ);
     snprintf(out->tz_iana, sizeof(out->tz_iana), "%s", CONFIG_REFLBO_TZ_NAME);
```


- [ ] **Step 4: Run the tests.**

Run: `cmake --build build-host && ./build-host/test_settings && ctest --test-dir build-host`
Expected: `23 Tests 0 Failures 0 Ignored`; `out of 59`, all passing. (The round-trip tests compare the parsed defaults with the defaults byte for byte: they pass only because the default centres sit on the location.)

- [ ] **Step 5: The firmware builds.** `tools/idf.sh build`: clean.

- [ ] **Step 6: Commit.**

```bash
git add components/storage main/app_ui.c test/host/test_settings.c
git commit -m "feat(storage): radar.* settings"
```


### Task 9: The Radar layout, `rain.map` and the radar presets (`ui`)

**Files:**
- Create: `components/ui/include/ui_radar.h`, `components/ui/ui_radar.c`, `test/host/radar_fixtures.h`, eight goldens
- Modify: `components/ui/include/ui_fields.h`, `ui_layout.h`, `ui_preset.h`, `components/ui/ui_fields.c`, `ui_layout.c`, `ui_preset.c`, `ui_preset_json.c`, `ui_catalog.c`, `ui_dashboard.c`, `ui_widget.c`, `ui_forecast.c`, `ui_internal.h`, `components/ui/CMakeLists.txt`
- Modify: `components/radar/include/radar.h`, `components/radar/radar_frame.c` (the dither, for the legend)
- Modify: `components/locale/include/lang.h`, `lang_en.c`, `lang_cs.c`
- Modify: `main/app.c`, `main/app_ui.c` (the cycle's new argument; the built-ins offered at load; the marker kept across a replace)
- Modify: `test/host/CMakeLists.txt`, `test/host/dashboard_fixtures.h`, `test/host/test_ui_preset.c`, `test/host/test_ui_fields.c`, `test/host/test_ui_catalog.c`

**Interfaces:**
- Consumes: `map_*` (Tasks 2–4), `radar_*` (Task 5), `ui_*` of M5.
- Produces (Tasks 10–14):
  - `ui_radar_t` (`struct ui_radar` in `ui_radar.h`: `map`, `home_lat_e4`, `home_lon_e4`, `frame`, `wx_lat_e4`, `wx_lon_e4`, `wx_zoom_q`, `loop_at`, `loop_count`), `UI_RADAR_OLD_S` 30 min, `ui_context_t.radar`; `void ui_draw_radar_view(gfx_fb_t *, gfx_rect_t, const ui_context_t *)`.
  - `UI_FK_RAIN_MAP`, `UI_FIELD_RAIN_MAP` ("rain.map", in M and L slots), `ui_value_t.radar`.
  - `UI_LAYOUT_RADAR` ("radar") and `UI_LAYOUT_FLIGHTS` ("flights"), both without slots; the dashboard draws them below the status bar.
  - `UI_OFFERED_RAIN`, `UI_OFFERED_FLIGHTS`, `UI_OFFERED_ALL`, `ui_presets_t.offered` ("offered" in `presets.json`), `bool ui_presets_offer_builtins(ui_presets_t *)`; `int ui_presets_next(const ui_presets_t *, bool always)` (signature changed).
  - `bool radar_inks(radar_level_t, int x, int y)` (was static).
  - Shared in `ui_internal.h`: `ui_clock_text()` (was `clock_text` in `ui_forecast.c`), `ui_format_age()` (was `format_age` in `ui_widget.c`), `ui_resolve_radar()`, `ui_radar_widget()`.

The Radar layout shows its frame's time and source at the bottom left ("20:40 · ČHMÚ"), inverted with its age once older than 30 min ("17:40 · 3 h ago"), and a legend of the three levels at the bottom right; during the loop, the frame's time alone and a dot a frame. In an M or L slot, `rain.map` is the same map cropped to the slot around the weather radar's centre at its zoom, with the frame's time. `rain.map` never counts as stale: its age shows on the map (spec §5.1). The built-in presets grow to six: Rain radar ("rain", the Radar layout) and Flights ("flights"), both in the cycle and with the status bar's clock (ruling). A `presets.json` without "offered" gains both once at load, if their ids are free and there is room, and is saved with the marker; a preset deleted later stays deleted.

- [ ] **Step 1: Write the failing tests.** The radar fixtures load the map, ČHMÚ's frame and RainViewer's tile once (paths from CMake, for every host target that includes them).

`test/host/radar_fixtures.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "map_data.h"
#include "radar.h"
#include "ui_radar.h"

/* The radars' inputs for the golden renders (dashboard_fixtures.h): the built-in map, ČHMÚ's rainy
 * frame of 2026-09-24 11:20 UTC and RainViewer's tile of zoom 3 (test_radar.c's fixtures), each
 * loaded once. REFLBO_MAP_BIN and REFLBO_PNG_FIXTURES come from CMake. */

static inline uint8_t *fixture_bytes(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*len);
    if (data != NULL && fread(data, 1, *len, f) != *len) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

static inline const map_data_t *fixture_map(void)
{
    static map_data_t map;
    static int state; /* 0 not tried, 1 loaded, -1 failed */
    if (state == 0) {
        size_t len = 0;
        uint8_t *blob = fixture_bytes(REFLBO_MAP_BIN, &len);
        state = blob != NULL && map_data_open(&map, blob, len) ? 1 : -1;
    }
    return state > 0 ? &map : NULL;
}

/* ČHMÚ's frame, stamped `time` (UTC). */
static inline const radar_frame_t *fixture_chmu(time_t time)
{
    static radar_frame_t f;
    static int state;
    if (state == 0) {
        size_t len = 0;
        uint8_t *png = fixture_bytes(REFLBO_PNG_FIXTURES "/chmi_rain.png", &len);
        state = png != NULL && radar_chmu_decode(png, len, NULL, 0, &f) == PNG_OK ? 1 : -1;
        free(png);
    }
    f.time = (uint32_t)time;
    return state > 0 ? &f : NULL;
}

/* RainViewer's tile (4, 2) of zoom 3 as a frame, stamped `time`. */
static inline const radar_frame_t *fixture_rainviewer(time_t time)
{
    static radar_frame_t f;
    static int state;
    if (state == 0) {
        size_t len = 0;
        uint8_t *png = fixture_bytes(REFLBO_PNG_FIXTURES "/rainviewer_tile.png", &len);
        radar_rv_tiles_t t = { .z = 3, .x0 = 4, .y0 = 2, .nx = 1, .ny = 1 };
        state = png != NULL && radar_rv_frame_alloc(&f, &t, 0, NULL) &&
                        radar_rv_decode_tile(png, len, NULL, &t, 4, 2, &f) == PNG_OK
                    ? 1
                    : -1;
        free(png);
    }
    f.time = (uint32_t)time;
    return state > 0 ? &f : NULL;
}

/* The weather radar over Brno at zoom 6.5 with `frame` (NULL: none yet). */
static inline ui_radar_t *fixture_radar(const radar_frame_t *frame)
{
    static ui_radar_t r;
    r = (ui_radar_t){ .map = fixture_map(), .home_lat_e4 = 491951, .home_lon_e4 = 166068, .frame = frame,
                      .wx_lat_e4 = 491951, .wx_lon_e4 = 166068, .wx_zoom_q = 26 };
    return &r;
}
```


`test/host/test_ui_preset.c`:

```diff
--- a/test/host/test_ui_preset.c
+++ b/test/host/test_ui_preset.c
@@ -17,30 +17,118 @@ void setUp(void)
 
 void tearDown(void) {}
 
-static void test_defaults_are_home_indoor_weather_and_focus(void)
+static void test_defaults_are_the_six_built_ins(void)
 {
-    TEST_ASSERT_EQUAL_INT(4, s_p.count);
+    TEST_ASSERT_EQUAL_INT(6, s_p.count);
     TEST_ASSERT_EQUAL_STRING("home", s_p.presets[s_p.active].id);
     TEST_ASSERT_EQUAL(UI_LAYOUT_CLASSIC, s_p.presets[0].layout);
     TEST_ASSERT_EQUAL(UI_FIELD_TIME_CLOCK, s_p.presets[0].slots[0]);
     TEST_ASSERT_EQUAL_INT(2, ui_presets_find(&s_p, "weather"));
     TEST_ASSERT_TRUE(s_p.presets[2].in_cycle); /* M5 brings its data (spec §5.4) */
+    TEST_ASSERT_EQUAL_INT(4, ui_presets_find(&s_p, "rain")); /* M6 (D28) */
+    TEST_ASSERT_EQUAL(UI_LAYOUT_RADAR, s_p.presets[4].layout);
+    TEST_ASSERT_EQUAL_STRING("Rain radar", s_p.presets[4].name);
+    TEST_ASSERT_TRUE(s_p.presets[4].in_cycle);
+    TEST_ASSERT_EQUAL_INT(5, ui_presets_find(&s_p, "flights"));
+    TEST_ASSERT_EQUAL(UI_LAYOUT_FLIGHTS, s_p.presets[5].layout);
+    TEST_ASSERT_TRUE(s_p.presets[5].in_cycle);
+    TEST_ASSERT_TRUE(s_p.presets[4].status_clock && s_p.presets[5].status_clock); /* a map fills the screen */
+    TEST_ASSERT_EQUAL_UINT8(UI_OFFERED_ALL, s_p.offered); /* nothing left to add */
     TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "nope"));
 }
 
 static void test_next_follows_cycle_order_and_skips_presets_out_of_it(void)
 {
-    TEST_ASSERT_EQUAL_INT(1, ui_presets_next(&s_p)); /* home -> indoor */
+    TEST_ASSERT_EQUAL_INT(1, ui_presets_next(&s_p, true)); /* home -> indoor */
     s_p.active = 1;
-    TEST_ASSERT_EQUAL_INT(2, ui_presets_next(&s_p)); /* indoor -> weather */
+    TEST_ASSERT_EQUAL_INT(2, ui_presets_next(&s_p, true)); /* indoor -> weather */
     s_p.presets[2].in_cycle = false;
-    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p)); /* indoor -> focus, skipping weather out of the cycle */
-    s_p.active = 3;
-    TEST_ASSERT_EQUAL_INT(0, ui_presets_next(&s_p)); /* wraps */
+    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p, true)); /* indoor -> focus, skipping weather out of the cycle */
+    s_p.active = 5;
+    TEST_ASSERT_EQUAL_INT(0, ui_presets_next(&s_p, true)); /* wraps */
     for (int i = 0; i < s_p.count; i++) {
         s_p.presets[i].in_cycle = i == 3;
     }
-    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p)); /* the only one: stays */
+    TEST_ASSERT_EQUAL_INT(3, ui_presets_next(&s_p, true)); /* the only one: stays */
+}
+
+static void test_the_cycle_visits_flights_only_in_sync_mode_always(void)
+{
+    s_p.active = 4; /* rain */
+    TEST_ASSERT_EQUAL_INT(5, ui_presets_next(&s_p, true));
+    TEST_ASSERT_EQUAL_INT(0, ui_presets_next(&s_p, false)); /* past flights to home */
+    for (int i = 0; i < s_p.count; i++) {
+        s_p.presets[i].in_cycle = i == 5;
+    }
+    s_p.active = 0;
+    TEST_ASSERT_EQUAL_INT(0, ui_presets_next(&s_p, false)); /* only flights in the cycle: stays */
+    TEST_ASSERT_EQUAL_INT(5, ui_presets_next(&s_p, true));
+}
+
+/* A presets.json saved by M5: the four presets of its day, no marker. */
+static const char k_m5_file[] =
+    "{\"schema\":1,\"active\":\"weather\",\"presets\":["
+    "{\"id\":\"home\",\"layout\":\"classic\"},{\"id\":\"indoor\",\"layout\":\"grid\"},"
+    "{\"id\":\"weather\",\"layout\":\"weather\"},{\"id\":\"focus\",\"layout\":\"focus\"}]}";
+
+static void test_a_file_from_before_m6_gains_the_radars_once(void)
+{
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_m5_file, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(0, s_p.offered);
+    TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p)); /* changed: save it */
+    TEST_ASSERT_EQUAL_INT(6, s_p.count);
+    TEST_ASSERT_EQUAL_INT(4, ui_presets_find(&s_p, "rain"));
+    TEST_ASSERT_EQUAL_INT(5, ui_presets_find(&s_p, "flights"));
+    TEST_ASSERT_EQUAL_STRING("weather", s_p.presets[s_p.active].id); /* the active one stays */
+    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p));
+
+    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"offered\":[\"rain\",\"flights\"]"));
+    s_p.count = 5; /* the owner deletes Flights */
+    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_FALSE(ui_presets_offer_builtins(&s_p)); /* and it stays deleted */
+    TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "flights"));
+}
+
+static void test_the_radars_need_room_and_a_free_id(void)
+{
+    memset(&s_p, 0, sizeof(s_p));
+    for (int i = 0; i < UI_PRESET_MAX; i++) {
+        snprintf(s_p.presets[i].id, sizeof(s_p.presets[i].id), "p%d", i);
+    }
+    s_p.count = UI_PRESET_MAX;
+    TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p)); /* the marker changed, nothing was added */
+    TEST_ASSERT_EQUAL_INT(UI_PRESET_MAX, s_p.count);
+    TEST_ASSERT_EQUAL_UINT8(UI_OFFERED_ALL, s_p.offered);
+
+    memset(&s_p, 0, sizeof(s_p));
+    snprintf(s_p.presets[0].id, sizeof(s_p.presets[0].id), "rain"); /* the owner's own, on another layout */
+    s_p.presets[0].layout = UI_LAYOUT_GRID;
+    s_p.count = 1;
+    TEST_ASSERT_TRUE(ui_presets_offer_builtins(&s_p));
+    TEST_ASSERT_EQUAL_INT(2, s_p.count); /* Flights only */
+    TEST_ASSERT_EQUAL(UI_LAYOUT_GRID, s_p.presets[0].layout);
+    TEST_ASSERT_EQUAL_STRING("flights", s_p.presets[1].id);
+}
+
+static void test_the_radar_layouts_have_no_slots_and_the_rain_map_needs_room(void)
+{
+    static const char k_ok[] =
+        "{\"schema\":1,\"presets\":[{\"id\":\"r\",\"layout\":\"radar\"},"
+        "{\"id\":\"f\",\"layout\":\"flights\",\"slots\":{}},"
+        "{\"id\":\"g\",\"layout\":\"grid\",\"slots\":{\"g1\":\"rain.map\"}},"
+        "{\"id\":\"w\",\"layout\":\"weather\",\"slots\":{\"now\":\"rain.map\"}}]}";
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(k_ok, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL(UI_FIELD_RAIN_MAP, s_p.presets[2].slots[0]);
+    static const char k_slot[] =
+        "{\"schema\":1,\"presets\":[{\"id\":\"r\",\"layout\":\"radar\",\"slots\":{\"map\":\"rain.map\"}}]}";
+    TEST_ASSERT_FALSE(ui_presets_from_json(k_slot, &s_p, s_err, sizeof(s_err)));
+    TEST_ASSERT_NOT_NULL(strstr(s_err, "has no slot"));
+    static const char k_small[] =
+        "{\"schema\":1,\"presets\":[{\"id\":\"h\",\"layout\":\"classic\",\"slots\":{\"s1\":\"rain.map\"}}]}";
+    TEST_ASSERT_FALSE(ui_presets_from_json(k_small, &s_p, s_err, sizeof(s_err)));
+    TEST_ASSERT_NOT_NULL(strstr(s_err, "can't show"));
 }
 
 static void test_defaults_survive_a_json_round_trip(void)
@@ -266,6 +354,7 @@ static void test_a_full_set_fits_the_save_buffer(void)
     }
     s_p.count = UI_PRESET_MAX;
     s_p.cycle_interval_s = 60;
+    s_p.offered = UI_OFFERED_ALL;
     s_p.schedule.count = UI_SCHEDULE_MAX;
     for (int i = 0; i < UI_SCHEDULE_MAX; i++) {
         s_p.schedule.entries[i] = (ui_schedule_entry_t){ .at_min = 600, .days = 0x7F, .action = UI_SCHED_NIGHT,
@@ -282,8 +371,12 @@ static void test_a_full_set_fits_the_save_buffer(void)
 int main(void)
 {
     UNITY_BEGIN();
-    RUN_TEST(test_defaults_are_home_indoor_weather_and_focus);
+    RUN_TEST(test_defaults_are_the_six_built_ins);
     RUN_TEST(test_next_follows_cycle_order_and_skips_presets_out_of_it);
+    RUN_TEST(test_the_cycle_visits_flights_only_in_sync_mode_always);
+    RUN_TEST(test_a_file_from_before_m6_gains_the_radars_once);
+    RUN_TEST(test_the_radars_need_room_and_a_free_id);
+    RUN_TEST(test_the_radar_layouts_have_no_slots_and_the_rain_map_needs_room);
     RUN_TEST(test_defaults_survive_a_json_round_trip);
     RUN_TEST(test_the_spec_example_parses);
     RUN_TEST(test_invalid_files_are_rejected_with_a_reason);
```


`test/host/test_ui_fields.c`:

```diff
--- a/test/host/test_ui_fields.c
+++ b/test/host/test_ui_fields.c
@@ -4,6 +4,7 @@
 
 #include "context_fixtures.h"
 #include "ui_fields.h"
+#include "ui_radar.h"
 #include "unity.h"
 
 static ui_context_t s_ctx;
@@ -206,6 +207,29 @@ static void test_rain_needs_its_two_hours_stored_and_ages_like_the_forecast(void
     TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
 }
 
+static void test_the_rain_map_shows_a_frame_with_its_time(void)
+{
+    ui_value_t v = resolve(UI_FIELD_RAIN_MAP);
+    TEST_ASSERT_EQUAL(UI_FK_RAIN_MAP, v.kind);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state); /* no radar at all */
+    static radar_frame_t f;
+    static ui_radar_t r;
+    r = (ui_radar_t){ .wx_zoom_q = 26 };
+    s_ctx.radar = &r;
+    v = resolve(UI_FIELD_RAIN_MAP);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state); /* before the first frame */
+    f.time = (uint32_t)(FIX_NOW - 8 * 60);
+    r.frame = &f;
+    v = resolve(UI_FIELD_RAIN_MAP);
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
+    TEST_ASSERT_EQUAL_STRING("20:40", v.text);
+    TEST_ASSERT_EQUAL_PTR(&r, v.radar);
+    f.time = (uint32_t)(FIX_NOW - 3 * 3600);
+    v = resolve(UI_FIELD_RAIN_MAP);
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state); /* its age shows on the map, never as stale (spec §5.1) */
+    TEST_ASSERT_EQUAL_UINT32(3 * 3600, v.age_s);
+}
+
 static void test_a_forecast_older_than_its_ttl_is_stale(void)
 {
     fixture_forecast(&s_fix_ds, FIX_NOW - 30 * 3600);
@@ -301,6 +325,7 @@ int main(void)
     RUN_TEST(test_the_hourly_strip_starts_at_the_next_hour_every_two_hours);
     RUN_TEST(test_rain_in_the_next_two_hours_says_when);
     RUN_TEST(test_rain_needs_its_two_hours_stored_and_ages_like_the_forecast);
+    RUN_TEST(test_the_rain_map_shows_a_frame_with_its_time);
     RUN_TEST(test_a_forecast_older_than_its_ttl_is_stale);
     RUN_TEST(test_the_sun_rises_and_sets_over_brno);
     RUN_TEST(test_air_quality_and_pollen_name_their_band_and_level);
```


`test/host/test_ui_catalog.c`:

```diff
--- a/test/host/test_ui_catalog.c
+++ b/test/host/test_ui_catalog.c
@@ -54,7 +54,11 @@ static void test_layouts_list_their_slots_with_rectangles_sizes_and_kinds(void)
     TEST_ASSERT_EQUAL_INT(400, num(s_root, "width"));
     TEST_ASSERT_EQUAL_INT(300, num(s_root, "height"));
     const cJSON *layouts = cJSON_GetObjectItemCaseSensitive(s_root, "layouts");
-    TEST_ASSERT_EQUAL_INT(4, cJSON_GetArraySize(layouts));
+    TEST_ASSERT_EQUAL_INT(6, cJSON_GetArraySize(layouts));
+    const cJSON *radar = by_id(layouts, "radar"); /* M6: the radars draw their own map, without slots */
+    TEST_ASSERT_NOT_NULL(radar);
+    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(radar, "slots")));
+    TEST_ASSERT_NOT_NULL(by_id(layouts, "flights"));
     const cJSON *classic = by_id(layouts, "classic");
     TEST_ASSERT_NOT_NULL(classic);
     const cJSON *slots = cJSON_GetObjectItemCaseSensitive(classic, "slots");
@@ -71,6 +75,9 @@ static void test_layouts_list_their_slots_with_rectangles_sizes_and_kinds(void)
     TEST_ASSERT_EQUAL_STRING("number", cJSON_GetArrayItem(kinds, 1)->valuestring);
     const cJSON *hourly = by_id(cJSON_GetObjectItemCaseSensitive(by_id(layouts, "weather"), "slots"), "hourly");
     TEST_ASSERT_EQUAL_STRING("M", str(hourly, "size"));
+    const cJSON *hourly_kinds = cJSON_GetObjectItemCaseSensitive(hourly, "kinds");
+    const cJSON *last = cJSON_GetArrayItem(hourly_kinds, cJSON_GetArraySize(hourly_kinds) - 1);
+    TEST_ASSERT_EQUAL_STRING("rain_map", last->valuestring); /* a medium slot takes the rain map */
 }
 
 /* GET /api/fields: the catalogue with values right now, labels in English (spec §5.8). */
```


`test/host/dashboard_fixtures.h`:

```diff
--- a/test/host/dashboard_fixtures.h
+++ b/test/host/dashboard_fixtures.h
@@ -3,6 +3,7 @@
 #include <string.h>
 
 #include "context_fixtures.h"
+#include "radar_fixtures.h"
 #include "ui_dashboard.h"
 
 /* The dashboard fixtures for the golden renders (test_ui_dashboard_golden.c, render_dashboard.c):
@@ -170,6 +171,44 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
         ctx->lang = lang_get("cs");
         fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
         fixture_rain_now(&s_fix_ds);
+    } else if (strcmp(name, "radar") == 0) { /* M6: the Rain radar preset, ČHMÚ's frame 8 min old */
+        *preset = fixture_preset("rain");
+        ctx->radar = fixture_radar(fixture_chmu(FIX_NOW - 8 * 60));
+    } else if (strcmp(name, "radar_stale") == 0) { /* 3 h old: the time inverted, with its age */
+        *preset = fixture_preset("rain");
+        ctx->radar = fixture_radar(fixture_chmu(FIX_NOW - 3 * 3600 - 8 * 60));
+    } else if (strcmp(name, "radar_stale_cs") == 0) {
+        fixture_dashboard("radar_stale", ctx, preset);
+        ctx->lang = lang_get("cs");
+    } else if (strcmp(name, "radar_loop") == 0) { /* the loop's seventh frame of twelve (D28) */
+        *preset = fixture_preset("rain");
+        ui_radar_t *radar = fixture_radar(fixture_chmu(FIX_NOW - 33 * 60));
+        radar->loop_at = 6;
+        radar->loop_count = 12;
+        ctx->radar = radar;
+    } else if (strcmp(name, "radar_none") == 0) { /* before the first frame */
+        *preset = fixture_preset("rain");
+        ctx->radar = fixture_radar(NULL);
+    } else if (strcmp(name, "radar_rainviewer") == 0) { /* outside ČHMÚ: Berlin from RainViewer at zoom 5 */
+        *preset = fixture_preset("rain");
+        ui_radar_t *radar = fixture_radar(fixture_rainviewer(FIX_NOW - 14 * 60));
+        radar->wx_lat_e4 = 525200;
+        radar->wx_lon_e4 = 134050;
+        radar->wx_zoom_q = 20;
+        ctx->radar = radar;
+    } else if (strcmp(name, "grid_rain_map") == 0) { /* the rain map in grid cells */
+        *preset = fixture_preset("indoor");
+        preset->slots[0] = UI_FIELD_RAIN_MAP;
+        preset->slots[4] = UI_FIELD_RAIN_MAP;
+        ui_radar_t *radar = fixture_radar(fixture_chmu(FIX_NOW - 3 * 3600 - 8 * 60));
+        radar->wx_lat_e4 = 506000; /* the rain north of Praha */
+        radar->wx_lon_e4 = 139000;
+        ctx->radar = radar;
+    } else if (strcmp(name, "weather_rain_map") == 0) { /* in the Weather layout's large slot */
+        *preset = fixture_preset("weather");
+        preset->slots[0] = UI_FIELD_RAIN_MAP;
+        fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
+        ctx->radar = fixture_radar(fixture_chmu(FIX_NOW - 8 * 60));
     } else if (strcmp(name, "grid_clock_12h") == 0) { /* a clock in a grid cell, 12-hour */
         *preset = fixture_preset("indoor");
         preset->slots[0] = UI_FIELD_TIME_CLOCK;
@@ -190,4 +229,6 @@ static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather",
                                                     "weather_noon_cs", "weather_stale", "air_grid", "air_grid_cs", "grid_sun_uv", "home_forecast",
                                                     "focus_forecast", "home_syncing", "home_sync_failed",
                                                     "home_always", "home_always_rejoining", "weather_rain",
-                                                    "focus_rain_now", "grid_rain_cs" };
+                                                    "focus_rain_now", "grid_rain_cs", "radar", "radar_stale",
+                                                    "radar_stale_cs", "radar_loop", "radar_none",
+                                                    "radar_rainviewer", "grid_rain_map", "weather_rain_map" };
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -10,6 +10,9 @@ set(CMAKE_C_STANDARD_REQUIRED ON)
 set(CMAKE_C_EXTENSIONS OFF)
 
 set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
+# The radar views' golden renders read the built-in map and the radar fixtures (radar_fixtures.h).
+add_compile_definitions(REFLBO_MAP_BIN="${REPO_ROOT}/assets/map/map.bin"
+                        REFLBO_PNG_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/png")
 set(REFLBO_WARNINGS -Wall -Wextra -Werror)
 
 # cmake -DREFLBO_SANITIZE=ON: everything with AddressSanitizer and UndefinedBehaviorSanitizer.
@@ -182,7 +185,7 @@ file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
 add_library(ui STATIC ${UI_SOURCES})
 target_include_directories(ui PUBLIC ${REPO_ROOT}/components/ui/include)
 target_compile_options(ui PRIVATE ${REFLBO_WARNINGS})
-target_link_libraries(ui PUBLIC gfx locale datastore util PRIVATE cjson scheduler weather_logic astro m)
+target_link_libraries(ui PUBLIC gfx locale datastore util map radar_logic PRIVATE cjson scheduler weather_logic astro m)
 
 # reflbo_host_test(<name> <libs...>): builds <name>.c against Unity and registers it with ctest.
 function(reflbo_host_test name)
```


Copy the goldens from the branch:

```bash
git checkout plan/m6 -- \
  test/host/golden/dash_grid_rain_map.pbm \
  test/host/golden/dash_radar.pbm \
  test/host/golden/dash_radar_loop.pbm \
  test/host/golden/dash_radar_none.pbm \
  test/host/golden/dash_radar_rainviewer.pbm \
  test/host/golden/dash_radar_stale.pbm \
  test/host/golden/dash_radar_stale_cs.pbm \
  test/host/golden/dash_weather_rain_map.pbm
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host 2>&1 | grep -m3 error`
Expected: `'ui_radar.h' file not found`, `use of undeclared identifier 'UI_LAYOUT_RADAR'`, `call to undeclared function 'ui_presets_offer_builtins'`.

- [ ] **Step 3: The field, the layouts and the context.**

`components/ui/include/ui_fields.h`:

```diff
--- a/components/ui/include/ui_fields.h
+++ b/components/ui/include/ui_fields.h
@@ -28,6 +28,7 @@ typedef enum {
     UI_FK_SUN,
     UI_FK_LEVEL,  /* a number and its band: the air quality index (D25) */
     UI_FK_POLLEN, /* a pollen level, and its type or count (D25) */
+    UI_FK_RAIN_MAP, /* the weather radar's map (M6) */
     UI_FK_COUNT,
 } ui_field_kind_t;
 
@@ -65,6 +66,7 @@ typedef enum {
     UI_FIELD_POLLEN_OLIVE,
     UI_FIELD_POLLEN_RAGWEED,
     UI_FIELD_WX_RAIN2H, /* M6 (D27) */
+    UI_FIELD_RAIN_MAP,  /* M6 (D23) */
     UI_FIELD_COUNT,
 } ui_field_id_t;
 
@@ -104,6 +106,8 @@ typedef struct {
     bool night;
 } ui_series_point_t;
 
+typedef struct ui_radar ui_radar_t; /* ui_radar.h */
+
 /* Everything the dashboard reads, gathered by the app for one render. */
 typedef struct {
     time_t now;          /* UTC */
@@ -119,6 +123,7 @@ typedef struct {
     int32_t lat_e4, lon_e4; /* the location, for the sun's times */
     ui_sync_mark_t sync;    /* the status bar's sync state (spec §5.2) */
     ui_wifi_mark_t wifi;
+    const ui_radar_t *radar; /* M6: the radars' map, frames and settings; NULL for none */
 } ui_context_t;
 
 typedef enum {
@@ -150,6 +155,7 @@ typedef struct {
     ui_series_point_t series[UI_SERIES_MAX];
     uint8_t rain_mm10[UI_RAIN_STEPS]; /* wx.rain2h: each quarter hour from now, as ds_rain_t keeps them */
     uint8_t rain_prob[UI_RAIN_STEPS];
+    const ui_radar_t *radar; /* rain.map: what its map draws; `text` is its frame's time */
 } ui_value_t;
 
 void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
```


`components/ui/ui_fields.c`:

```diff
--- a/components/ui/ui_fields.c
+++ b/components/ui/ui_fields.c
@@ -39,6 +39,7 @@ static const ui_field_info_t k_fields[UI_FIELD_COUNT] = {
     [UI_FIELD_POLLEN_OLIVE] = { "pollen.olive", UI_FK_POLLEN, LS_POLLEN_OLIVE, -1 },
     [UI_FIELD_POLLEN_RAGWEED] = { "pollen.ragweed", UI_FK_POLLEN, LS_POLLEN_RAGWEED, -1 },
     [UI_FIELD_WX_RAIN2H] = { "wx.rain2h", UI_FK_SERIES, LS_RAIN_2H, -1 },
+    [UI_FIELD_RAIN_MAP] = { "rain.map", UI_FK_RAIN_MAP, LS_RAIN_MAP, -1 },
 };
 
 const ui_field_info_t *ui_field_info(ui_field_id_t field)
@@ -213,7 +214,7 @@ void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
     out->label = lang_str(ctx->lang, info->label);
     if (info->ds_field >= 0) {
         resolve_store(ctx, field, out);
-    } else if (!ui_resolve_forecast(ctx, field, out)) {
+    } else if (!ui_resolve_forecast(ctx, field, out) && !ui_resolve_radar(ctx, field, out)) {
         resolve_clock(ctx, field, out);
     }
 }
```


`components/ui/include/ui_layout.h`:

```diff
--- a/components/ui/include/ui_layout.h
+++ b/components/ui/include/ui_layout.h
@@ -14,6 +14,8 @@ typedef enum {
     UI_LAYOUT_WEATHER,
     UI_LAYOUT_GRID,
     UI_LAYOUT_FOCUS,
+    UI_LAYOUT_RADAR,   /* M6: the weather radar's map, without slots (spec §5.2) */
+    UI_LAYOUT_FLIGHTS, /* M6: the flight radar's map and panel, without slots */
     UI_LAYOUT_COUNT,
 } ui_layout_id_t;
 
```


`components/ui/ui_layout.c`:

```diff
--- a/components/ui/ui_layout.c
+++ b/components/ui/ui_layout.c
@@ -10,8 +10,8 @@
     (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_DATE) | UI_KIND(UI_FK_NUMBER) | UI_KIND(UI_FK_BATTERY) |                    \
      UI_KIND(UI_FK_MOON) | UI_KIND(UI_FK_TEXT) | UI_KIND(UI_FK_WEATHER_NOW) | UI_KIND(UI_FK_WEATHER_DAY) |           \
      UI_KIND(UI_FK_SUN) | UI_KIND(UI_FK_LEVEL) | UI_KIND(UI_FK_POLLEN))
-#define K_ANY_MEDIUM (K_ANY_SMALL | UI_KIND(UI_FK_SERIES))
-#define K_LARGE (K_ANY_SMALL)
+#define K_ANY_MEDIUM (K_ANY_SMALL | UI_KIND(UI_FK_SERIES) | UI_KIND(UI_FK_RAIN_MAP))
+#define K_LARGE (K_ANY_SMALL | UI_KIND(UI_FK_RAIN_MAP))
 #define K_XL (UI_KIND(UI_FK_TIME) | UI_KIND(UI_FK_NUMBER))
 
 static const ui_slot_t k_classic[] = {
@@ -51,6 +51,8 @@ static const ui_layout_t k_layouts[UI_LAYOUT_COUNT] = {
     [UI_LAYOUT_WEATHER] = { "weather", k_weather, sizeof(k_weather) / sizeof(k_weather[0]) },
     [UI_LAYOUT_GRID] = { "grid", k_grid, sizeof(k_grid) / sizeof(k_grid[0]) },
     [UI_LAYOUT_FOCUS] = { "focus", k_focus, sizeof(k_focus) / sizeof(k_focus[0]) },
+    [UI_LAYOUT_RADAR] = { "radar", NULL, 0 },
+    [UI_LAYOUT_FLIGHTS] = { "flights", NULL, 0 },
 };
 
 const ui_layout_t *ui_layout(ui_layout_id_t id)
```


`components/ui/ui_catalog.c`:

```diff
--- a/components/ui/ui_catalog.c
+++ b/components/ui/ui_catalog.c
@@ -22,8 +22,9 @@ static const char *const k_kinds[UI_FK_COUNT] = {
     [UI_FK_SUN] = "sun",
     [UI_FK_LEVEL] = "level",
     [UI_FK_POLLEN] = "pollen",
+    [UI_FK_RAIN_MAP] = "rain_map",
 };
-_Static_assert(UI_FK_COUNT == 12, "every kind has a name in the catalogue");
+_Static_assert(UI_FK_COUNT == 13, "every kind has a name in the catalogue");
 static const char *const k_sizes[] = { "S", "M", "L", "XL" };
 
 static size_t print(cJSON *root, char *out, size_t size)
```


- [ ] **Step 4: The presets.**

`components/ui/include/ui_preset.h`:

```diff
--- a/components/ui/include/ui_preset.h
+++ b/components/ui/include/ui_preset.h
@@ -89,6 +89,14 @@ int ui_schedule_step(const ui_schedule_t *schedule, time_t *checked, time_t now,
  * the entries inside it don't run, and one at the end minute does. */
 time_t ui_schedule_after_night(time_t until);
 
+/* Built-in presets a file from an earlier firmware is offered once (spec §5.4): presets.json's
+ * "offered" names them, so one deleted later stays deleted. */
+enum {
+    UI_OFFERED_RAIN = 1u << 0,    /* "rain": Rain radar (M6) */
+    UI_OFFERED_FLIGHTS = 1u << 1, /* "flights": Flights (M6) */
+};
+#define UI_OFFERED_ALL (UI_OFFERED_RAIN | UI_OFFERED_FLIGHTS)
+
 typedef struct {
     uint8_t count;
     uint8_t active; /* index into presets */
@@ -96,14 +104,19 @@ typedef struct {
     uint16_t cycle_interval_s;
     ui_preset_t presets[UI_PRESET_MAX];
     ui_schedule_t schedule;
+    uint8_t offered; /* UI_OFFERED_* */
 } ui_presets_t;
 
 /* The built-in presets (spec §5.4): used when presets.json is missing or invalid. */
 void ui_presets_defaults(ui_presets_t *p);
 int ui_presets_find(const ui_presets_t *p, const char *id); /* index, or -1 */
 /* The next preset in cycle order after the active one, wrapping; the active one if no other
- * preset is in the cycle (spec §5.4, KEY short). */
-int ui_presets_next(const ui_presets_t *p);
+ * preset is in the cycle (spec §5.4, KEY short). Presets on the Flights layout are in it only in
+ * sync mode `always` (D28). */
+int ui_presets_next(const ui_presets_t *p, bool always);
+/* Offers the built-ins `offered` doesn't name yet: each joins at the end if its id is free and
+ * there is room, and is marked offered either way. True if anything changed: save the file. */
+bool ui_presets_offer_builtins(ui_presets_t *p);
 /* Parses and validates presets.json. On failure returns false with a reason in `err` and leaves
  * *out unspecified. */
 bool ui_presets_from_json(const char *json, ui_presets_t *out, char *err, size_t err_size);
```


`components/ui/ui_preset.c`:

```diff
--- a/components/ui/ui_preset.c
+++ b/components/ui/ui_preset.c
@@ -40,6 +40,36 @@ void ui_presets_defaults(ui_presets_t *p)
     p->active = 0;
     p->cycle_enabled = false;
     p->cycle_interval_s = 60;
+    ui_presets_offer_builtins(p); /* the radars (M6) */
+}
+
+/* The built-ins added after M5: the two radars (D28), whose layouts have no slots. */
+static const struct {
+    uint8_t bit;
+    const char *id, *name;
+    ui_layout_id_t layout;
+} k_offered[] = {
+    { UI_OFFERED_RAIN, "rain", "Rain radar", UI_LAYOUT_RADAR },
+    { UI_OFFERED_FLIGHTS, "flights", "Flights", UI_LAYOUT_FLIGHTS },
+};
+
+bool ui_presets_offer_builtins(ui_presets_t *p)
+{
+    bool changed = false;
+    for (size_t i = 0; i < sizeof(k_offered) / sizeof(k_offered[0]); i++) {
+        if (p->offered & k_offered[i].bit) {
+            continue;
+        }
+        if (ui_presets_find(p, k_offered[i].id) < 0 && p->count < UI_PRESET_MAX) {
+            ui_preset_t *added = &p->presets[p->count++];
+            *added = make(k_offered[i].id, k_offered[i].name, k_offered[i].layout, true,
+                          (ui_field_id_t[UI_SLOT_MAX]){ UI_FIELD_NONE });
+            added->status_clock = true; /* a map fills the screen: the time goes to the status bar */
+        }
+        p->offered |= k_offered[i].bit;
+        changed = true;
+    }
+    return changed;
 }
 
 int ui_presets_find(const ui_presets_t *p, const char *id)
@@ -52,11 +82,11 @@ int ui_presets_find(const ui_presets_t *p, const char *id)
     return -1;
 }
 
-int ui_presets_next(const ui_presets_t *p)
+int ui_presets_next(const ui_presets_t *p, bool always)
 {
     for (int step = 1; step < p->count; step++) {
         int i = (p->active + step) % p->count;
-        if (p->presets[i].in_cycle) {
+        if (p->presets[i].in_cycle && (always || p->presets[i].layout != UI_LAYOUT_FLIGHTS)) {
             return i;
         }
     }
```


`components/ui/ui_preset_json.c`:

```diff
--- a/components/ui/ui_preset_json.c
+++ b/components/ui/ui_preset_json.c
@@ -12,6 +12,7 @@
 static const char *const k_policies[] = { [UI_STALE_STALE] = "stale", [UI_STALE_PLACEHOLDER] = "placeholder",
                                           [UI_STALE_HIDE] = "hide" };
 static const char *const k_battery_parts[] = { "percent", "voltage", "days" }; /* UI_STATUS_BAT_* bit order */
+static const char *const k_offered_ids[] = { "rain", "flights" };              /* UI_OFFERED_* bit order */
 
 static bool fail(char *err, size_t size, const char *fmt, ...)
 {
@@ -264,6 +265,17 @@ static bool parse(const cJSON *root, ui_presets_t *out, char *err, size_t size)
     out->cycle_interval_s = (uint16_t)(seconds < UI_CYCLE_MIN_S ? UI_CYCLE_MIN_S
                                        : seconds > UI_CYCLE_MAX_S ? UI_CYCLE_MAX_S
                                                                   : seconds);
+    const cJSON *offered = cJSON_GetObjectItemCaseSensitive(root, "offered");
+    const cJSON *names = cJSON_IsArray(offered) ? offered : NULL;
+    const cJSON *id;
+    cJSON_ArrayForEach(id, names)
+    {
+        for (int i = 0; cJSON_IsString(id) && i < (int)(sizeof(k_offered_ids) / sizeof(k_offered_ids[0])); i++) {
+            if (strcmp(id->valuestring, k_offered_ids[i]) == 0) {
+                out->offered |= (uint8_t)(1u << i); /* unknown names are a later firmware's */
+            }
+        }
+    }
     const cJSON *schedule = cJSON_GetObjectItemCaseSensitive(root, "schedule");
     return schedule == NULL || cJSON_IsNull(schedule) || parse_schedule(schedule, out, err, size);
 }
@@ -348,6 +360,14 @@ size_t ui_presets_to_json(const ui_presets_t *p, char *out, size_t size)
             cJSON_AddItemToArray(entries, obj);
         }
     }
+    if (p->offered) {
+        cJSON *offered = cJSON_AddArrayToObject(root, "offered");
+        for (int i = 0; i < (int)(sizeof(k_offered_ids) / sizeof(k_offered_ids[0])); i++) {
+            if (p->offered & (1u << i)) {
+                cJSON_AddItemToArray(offered, cJSON_CreateString(k_offered_ids[i]));
+            }
+        }
+    }
     /* unformatted: the device reads it, and 16 presets must fit UI_PRESETS_JSON_MAX */
     bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
     cJSON_Delete(root);
```


- [ ] **Step 5: The map views.** The labels placed so far (1.5 KB) are a static, off the app task's 8 KB stack: only the app task draws, the web UI's previews included (spec §3.2).

`components/ui/include/ui_radar.h`:

```c
#pragma once

#include <stdint.h>

#include "gfx.h"
#include "map_data.h"
#include "radar.h"
#include "ui_fields.h"

/*
 * The radars' views (spec §11.1–§11.3): what the app gathers for them, and the weather radar's map
 * as the Radar layout and the rain.map widget draw it. Pure C, host-buildable.
 */

#define UI_RADAR_OLD_S (30 * 60) /* an older frame shows its time inverted, with its age (spec §11.2) */

struct ui_radar {
    const map_data_t *map;            /* the built-in map; NULL: none drawn */
    int32_t home_lat_e4, home_lon_e4; /* location.*: home's ⊙ */
    const radar_frame_t *frame;       /* the weather radar's frame to show; NULL before the first */
    int32_t wx_lat_e4, wx_lon_e4;     /* radar.weather's centre */
    uint8_t wx_zoom_q;                /* and its zoom, in quarters */
    uint8_t loop_at, loop_count;      /* the loop (D28): frame loop_at of loop_count; count 0 outside it */
};

/* The Radar layout's map in `r`: the rain, the frame's time and source at the bottom left, the legend
 * or the loop's progress at the bottom right; "No radar frame yet" before the first. */
void ui_draw_radar_view(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx);
```


`components/ui/ui_radar.c`:

```c
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "map_draw.h"
#include "ui_internal.h"
#include "ui_radar.h"

/* The weather radar's map (spec §11.2): the rain under the map, then the frame's time and the
 * legend, or the loop's progress, on boxes along the bottom. */

#define PAD 3

static bool frame_old(time_t now, const radar_frame_t *f)
{
    return now > (time_t)f->time && now - (time_t)f->time > UI_RADAR_OLD_S;
}

/* The pattern's "%s" replaced by `value`, as the packs' LS_AGO has it ("%s ago", "před %s"). */
static void fill(const char *pattern, const char *value, char *out, size_t size)
{
    const char *at = strstr(pattern, "%s");
    if (at == NULL) {
        snprintf(out, size, "%s", pattern);
        return;
    }
    snprintf(out, size, "%.*s%s%s", (int)(at - pattern), pattern, value, at + 2);
}

/* "20:40 · ČHMÚ", or once the frame is old "17:40 · 3 h ago"; `brief`: the time alone. */
static void caption_text(const ui_context_t *ctx, const radar_frame_t *f, bool brief, char *out, size_t size)
{
    char at[16];
    ui_clock_text(ctx, (time_t)f->time, at, sizeof(at));
    if (brief) {
        snprintf(out, size, "%s", at);
    } else if (frame_old(ctx->now, f)) {
        char age[16], ago[32];
        ui_format_age(ctx->lang, (uint32_t)(ctx->now - (time_t)f->time), age, sizeof(age));
        fill(lang_str(ctx->lang, LS_AGO), age, ago, sizeof(ago));
        snprintf(out, size, "%s \xC2\xB7 %s", at, ago);
    } else {
        snprintf(out, size, "%s \xC2\xB7 %s", at, f->source == RADAR_SOURCE_CHMU ? "\xC4\x8CHM\xC3\x9A" : "RainViewer");
    }
}

static gfx_rect_t bottom_box(gfx_rect_t r, int w, int h, bool right)
{
    return (gfx_rect_t){ (int16_t)(right ? r.x + r.w - w : r.x), (int16_t)(r.y + r.h - h), (int16_t)w, (int16_t)h };
}

static void boxed(gfx_fb_t *fb, gfx_rect_t box, const gfx_font_t *f, const char *text, bool inverted)
{
    gfx_fill_rect(fb, box, inverted ? GFX_BLACK : GFX_WHITE);
    gfx_text(fb, f, box.x + PAD, box.y + PAD + f->ascent, text, inverted ? GFX_WHITE : GFX_BLACK);
}

/* The three levels as swatches with their words. */
static const lang_str_t k_level_words[3] = { LS_RAIN_LIGHT, LS_RAIN_MODERATE, LS_RAIN_HEAVY };
#define SWATCH_W 12
#define SWATCH_H 9

static int legend_w(const ui_context_t *ctx, const gfx_font_t *f)
{
    int w = PAD;
    for (int i = 0; i < 3; i++) {
        w += SWATCH_W + 3 + gfx_text_width(f, lang_str(ctx->lang, k_level_words[i])) + (i < 2 ? 8 : PAD);
    }
    return w;
}

static void legend(gfx_fb_t *fb, gfx_rect_t box, const ui_context_t *ctx, const gfx_font_t *f)
{
    gfx_fill_rect(fb, box, GFX_WHITE);
    int x = box.x + PAD, base = box.y + PAD + f->ascent;
    for (int i = 0; i < 3; i++) {
        gfx_rect_t sw = { (int16_t)x, (int16_t)(base - SWATCH_H + 1), SWATCH_W, SWATCH_H };
        gfx_rect(fb, sw, GFX_BLACK);
        for (int y = sw.y + 1; y < sw.y + sw.h - 1; y++) {
            for (int px = sw.x + 1; px < sw.x + sw.w - 1; px++) {
                if (radar_inks((radar_level_t)(RADAR_LIGHT + i), px, y)) {
                    gfx_pixel(fb, px, y, GFX_BLACK);
                }
            }
        }
        x = gfx_text(fb, f, x + SWATCH_W + 3, base, lang_str(ctx->lang, k_level_words[i]), GFX_BLACK) + 8;
    }
}

/* The loop's progress: a dot a frame, the one shown filled. */
static void loop_dots(gfx_fb_t *fb, gfx_rect_t box, const ui_radar_t *rad)
{
    gfx_fill_rect(fb, box, GFX_WHITE);
    int cy = box.y + box.h / 2;
    for (int i = 0; i < rad->loop_count; i++) {
        int cx = box.x + PAD + 4 + i * 9;
        if (i == rad->loop_at) {
            gfx_fill_circle(fb, cx, cy, 3, GFX_BLACK);
        } else {
            gfx_circle(fb, cx, cy, 2, GFX_BLACK);
        }
    }
}

/* The map with the rain and `caption` at the bottom left; with `ctx`, the Radar layout's: the legend or
 * the loop at the bottom right, and the message before the first frame. */
static void draw_map(gfx_fb_t *fb, gfx_rect_t r, const ui_radar_t *rad, ui_size_t size, const char *caption,
                     bool old, const ui_context_t *ctx)
{
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, r));
    map_view_t v;
    map_view_init(&v, rad->wx_lat_e4, rad->wx_lon_e4, rad->wx_zoom_q / 4.0, r.w, r.h);
    if (rad->frame != NULL) {
        radar_render(fb, r, &v, rad->frame);
    }
    map_style_t style = { .airports = false, .halo = true,
                          .max_towns = size == UI_SIZE_XL ? 12 : size == UI_SIZE_L ? 5 : 3 };
    if (rad->map != NULL) {
        map_draw_lines(fb, r, &v, rad->map, &style);
    }
    static map_labels_t labels; /* 1.5 KB, off the stack: only the app task draws (spec §3.2) */
    map_labels_init(&labels);
    const gfx_font_t *cf = size == UI_SIZE_XL ? &gfx_font_sans_bold_16 : &gfx_font_sans_12, *lf = &gfx_font_sans_12;
    gfx_rect_t cap = { 0 }, right = { 0 };
    if (rad->frame != NULL) { /* the bottom row first, so the places keep clear of it */
        cap = bottom_box(r, gfx_text_width(cf, caption) + 2 * PAD, cf->line_height + 2 * PAD - 2, false);
        map_labels_reserve(&labels, cap);
        if (ctx != NULL) {
            int w = rad->loop_count > 0 ? rad->loop_count * 9 + 2 * PAD : legend_w(ctx, lf);
            right = bottom_box(r, w, lf->line_height + 2 * PAD - 2, true);
            map_labels_reserve(&labels, right);
        }
    }
    map_draw_home(fb, r, &v, rad->home_lat_e4, rad->home_lon_e4, &labels);
    if (rad->map != NULL) {
        map_draw_places(fb, r, &v, rad->map, &style, &labels);
    }
    if (rad->frame != NULL) {
        boxed(fb, cap, cf, caption, old);
        if (ctx != NULL && rad->loop_count > 0) {
            loop_dots(fb, right, rad);
        } else if (ctx != NULL) {
            legend(fb, right, ctx, lf);
        }
    } else if (ctx != NULL) {
        const char *none = lang_str(ctx->lang, LS_NO_RADAR_FRAME);
        const gfx_font_t *nf = &gfx_font_sans_bold_20;
        int w = gfx_text_width(nf, none) + 4 * PAD, h = nf->line_height + 4 * PAD;
        gfx_rect_t box = { (int16_t)(r.x + (r.w - w) / 2), (int16_t)(r.y + (r.h - h) / 2), (int16_t)w, (int16_t)h };
        gfx_fill_rect(fb, box, GFX_WHITE);
        gfx_rect(fb, box, GFX_BLACK);
        gfx_text(fb, nf, box.x + 2 * PAD, box.y + 2 * PAD + nf->ascent, none, GFX_BLACK);
    }
    fb->clip = saved;
}

void ui_draw_radar_view(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx)
{
    ui_radar_t at_home = { .home_lat_e4 = ctx->lat_e4, .home_lon_e4 = ctx->lon_e4, .wx_lat_e4 = ctx->lat_e4,
                           .wx_lon_e4 = ctx->lon_e4, .wx_zoom_q = 26 };
    const ui_radar_t *rad = ctx->radar != NULL ? ctx->radar : &at_home; /* no radar at all: an empty map */
    char caption[64] = "";
    bool loop = rad->loop_count > 0; /* the loop shows each frame's time alone (D28) */
    if (rad->frame != NULL) {
        caption_text(ctx, rad->frame, loop, caption, sizeof(caption));
    }
    draw_map(fb, r, rad, UI_SIZE_XL, caption, !loop && rad->frame != NULL && frame_old(ctx->now, rad->frame), ctx);
}

bool ui_resolve_radar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
{
    if (field != UI_FIELD_RAIN_MAP) {
        return false;
    }
    const ui_radar_t *rad = ctx->radar;
    if (rad == NULL || rad->frame == NULL) {
        out->state = UI_VALUE_MISSING;
        return true;
    }
    out->state = UI_VALUE_FRESH; /* its age shows on the map, not as stale (spec §5.1) */
    out->radar = rad;
    out->age_s = ctx->now > (time_t)rad->frame->time ? (uint32_t)(ctx->now - (time_t)rad->frame->time) : 0;
    caption_text(ctx, rad->frame, true, out->text, sizeof(out->text));
    return true;
}

bool ui_radar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
{
    if (v->kind != UI_FK_RAIN_MAP || v->state == UI_VALUE_MISSING || v->radar == NULL) {
        return false;
    }
    draw_map(fb, r, v->radar, size, v->text, v->age_s > UI_RADAR_OLD_S, NULL); /* the time, inverted once old */
    return true;
}
```


`components/radar/include/radar.h`:

```diff
--- a/components/radar/include/radar.h
+++ b/components/radar/include/radar.h
@@ -109,6 +109,8 @@ png_err_t radar_rv_decode_tile(const uint8_t *png, size_t len, const png_mem_t *
 void radar_render(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const radar_frame_t *f);
 /* Any rain inside the view. */
 bool radar_any_rain(const map_view_t *v, const radar_frame_t *f);
+/* The dither: whether screen pixel (x, y) is inked at `level`, as radar_render() draws it. */
+bool radar_inks(radar_level_t level, int x, int y);
 
 /* ---- the frames kept ---- */
 
```


`components/radar/radar_frame.c`:

```diff
--- a/components/radar/radar_frame.c
+++ b/components/radar/radar_frame.c
@@ -55,7 +55,7 @@ void radar_frame_set(radar_frame_t *f, int x, int y, radar_level_t level)
     f->levels[i / 4] = (uint8_t)((f->levels[i / 4] & ~(3 << shift)) | (level & 3) << shift);
 }
 
-static bool inks(radar_level_t level, int x, int y)
+bool radar_inks(radar_level_t level, int x, int y)
 {
     switch (level) {
     case RADAR_LIGHT:
@@ -112,7 +112,7 @@ void radar_render(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const rada
     for (int sy = 0; sy < g.h; sy++) {
         for (int sx = 0; g.rows[sy] >= 0 && sx < g.w; sx++) {
             int x = area.x + sx, y = area.y + sy;
-            if (g.cols[sx] >= 0 && inks(radar_frame_level(f, g.cols[sx], g.rows[sy]), x, y)) {
+            if (g.cols[sx] >= 0 && radar_inks(radar_frame_level(f, g.cols[sx], g.rows[sy]), x, y)) {
                 gfx_pixel(fb, x, y, GFX_BLACK);
             }
         }
```


`components/ui/ui_internal.h`:

```diff
--- a/components/ui/ui_internal.h
+++ b/components/ui/ui_internal.h
@@ -9,6 +9,15 @@
 
 #define UI_BATTERY_LOW_PCT 15 /* spec §8: the status bar marks a low battery */
 
+/* "20:48" or "8:48 PM": a UTC time in local time, as the clock setting shows it (ui_forecast.c). */
+void ui_clock_text(const ui_context_t *ctx, time_t t, char *out, size_t size);
+/* An age as the stale mark shows it: "45 min", "3 h", "2 d" (ui_widget.c). */
+void ui_format_age(const lang_t *lang, uint32_t age_s, char *out, size_t size);
+/* rain.map (ui_radar.c, M6); false for any other field. */
+bool ui_resolve_radar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
+/* Its widget in an M or L slot; false for any other kind. */
+bool ui_radar_widget(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v);
+
 /* The weather, air quality, pollen and sun fields (ui_forecast.c, D25); false for any other field. */
 bool ui_resolve_forecast(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
 /* Their widgets; false for a kind they don't draw. */
```


`components/ui/ui_widget.c`:

```diff
--- a/components/ui/ui_widget.c
+++ b/components/ui/ui_widget.c
@@ -170,7 +170,7 @@ void ui_split_two_lines(const gfx_font_t *font, const char *text, int max_w, cha
     gfx_text_ellipsize(font, first, max_w, line1, size); /* no space that helps: one cut line */
 }
 
-static void format_age(const lang_t *lang, uint32_t age_s, char *out, size_t size)
+void ui_format_age(const lang_t *lang, uint32_t age_s, char *out, size_t size)
 {
     if (age_s < 3600) {
         snprintf(out, size, "%lu %s", (unsigned long)(age_s / 60), lang_str(lang, LS_MINUTES_UNIT));
@@ -185,7 +185,7 @@ static void format_age(const lang_t *lang, uint32_t age_s, char *out, size_t siz
 static void draw_age(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v, const lang_t *lang)
 {
     char age[16];
-    format_age(lang, v->age_s, age, sizeof(age));
+    ui_format_age(lang, v->age_s, age, sizeof(age));
     const gfx_font_t *f = &gfx_font_sans_12;
     int w = gfx_text_width(f, age);
     int x = r.x + r.w - 6 - w;
@@ -414,6 +414,8 @@ void ui_widget_draw(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t
     gfx_set_clip(fb, gfx_rect_intersect(saved, r));
     if (ui_forecast_draw(fb, r, size, &shown)) {
         /* the weather, air quality, pollen and sun widgets (ui_forecast.c) */
+    } else if (ui_radar_widget(fb, r, size, &shown)) {
+        /* rain.map (ui_radar.c) */
     } else if (size == UI_SIZE_S) {
         draw_small(fb, r, &shown);
     } else {
```


`components/ui/ui_forecast.c`:

```diff
--- a/components/ui/ui_forecast.c
+++ b/components/ui/ui_forecast.c
@@ -72,7 +72,7 @@ static bool night_at(const ui_context_t *ctx, time_t t)
     return t < sun.sunrise || t >= sun.sunset;
 }
 
-static void clock_text(const ui_context_t *ctx, time_t t, char *out, size_t size)
+void ui_clock_text(const ui_context_t *ctx, time_t t, char *out, size_t size)
 {
     struct tm local;
     localtime_r(&t, &local);
@@ -94,8 +94,8 @@ static void resolve_sun(const ui_context_t *ctx, ui_value_t *out)
         snprintf(out->text, sizeof(out->text), "%s", lang_str(ctx->lang, out->polar == 1 ? LS_POLAR_DAY : LS_POLAR_NIGHT));
         return;
     }
-    clock_text(ctx, (time_t)sun.sunrise, out->text, sizeof(out->text));
-    clock_text(ctx, (time_t)sun.sunset, out->extra, sizeof(out->extra));
+    ui_clock_text(ctx, (time_t)sun.sunrise, out->text, sizeof(out->text));
+    ui_clock_text(ctx, (time_t)sun.sunset, out->extra, sizeof(out->extra));
     int minutes = (sun.day_length_s + 30) / 60;
     const char *h = lang_str(ctx->lang, LS_HOURS_UNIT), *min = lang_str(ctx->lang, LS_MINUTES_UNIT);
     astro_sun_t before = sun_on(ctx, ctx->local_day - 1); /* D26: the change since yesterday */
@@ -279,7 +279,7 @@ static void resolve_rain(const ui_context_t *ctx, ui_value_t *out)
         snprintf(out->extra, sizeof(out->extra), "%s %s", rate, lang_str(ctx->lang, LS_MM_PER_H));
     } else if (first > 0) {
         char at[12]; /* where its quarter hour starts */
-        clock_text(ctx, (time_t)w->rain.t0 + (time_t)(i0 + first - 1) * DS_RAIN_STEP_S, at, sizeof(at));
+        ui_clock_text(ctx, (time_t)w->rain.t0 + (time_t)(i0 + first - 1) * DS_RAIN_STEP_S, at, sizeof(at));
         snprintf(out->text, sizeof(out->text), "%s %s", lang_str(ctx->lang, LS_RAIN_FROM), at);
     } else {
         snprintf(out->text, sizeof(out->text), "%s", lang_str(ctx->lang, LS_DRY_2H));
```


`components/ui/ui_dashboard.c`:

```diff
--- a/components/ui/ui_dashboard.c
+++ b/components/ui/ui_dashboard.c
@@ -1,6 +1,7 @@
 #include "ui_dashboard.h"
 
 #include "ui_internal.h"
+#include "ui_radar.h"
 
 static void draw_separators(gfx_fb_t *fb, ui_layout_id_t layout)
 {
@@ -43,7 +44,10 @@ void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t
     gfx_clear(fb, GFX_WHITE);
     const ui_layout_t *layout = ui_layout((ui_layout_id_t)preset->layout);
     bool any_stale = false;
-    if (layout != NULL) {
+    if (preset->layout == UI_LAYOUT_RADAR) { /* spec §5.2: the map under the status bar */
+        ui_draw_radar_view(fb, (gfx_rect_t){ 0, UI_STATUS_H + 1, fb->width, (int16_t)(fb->height - UI_STATUS_H - 1) },
+                           &c);
+    } else if (layout != NULL) {
         draw_separators(fb, (ui_layout_id_t)preset->layout);
         for (int i = 0; i < layout->slot_count; i++) {
             ui_value_t v;
```


`components/ui/CMakeLists.txt`:

```diff
--- a/components/ui/CMakeLists.txt
+++ b/components/ui/CMakeLists.txt
@@ -1,7 +1,7 @@
 # Dashboard UI (spec §5): fields, widgets, layouts, presets. Pure C: also built on the host.
 idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                             "ui_status.c" "ui_dashboard.c" "ui_schedule.c" "ui_menu.c" "ui_menu_draw.c"
-                            "ui_screens.c" "ui_config.c" "ui_catalog.c" "ui_forecast.c"
+                            "ui_screens.c" "ui_config.c" "ui_catalog.c" "ui_forecast.c" "ui_radar.c"
                        INCLUDE_DIRS "include"
-                       REQUIRES gfx locale datastore util
+                       REQUIRES gfx locale datastore util map radar
                        PRIV_REQUIRES json scheduler weather astro)
```


- [ ] **Step 6: The strings.**

`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -178,6 +178,12 @@ typedef enum {
     LS_RAIN_FROM, /* followed by " 21:45" */
     LS_DRY_2H,
     LS_MM_PER_H,
+    LS_RAIN_MAP, /* rain.map (spec §11.2) */
+    LS_NO_RADAR_FRAME,
+    LS_RAIN_LIGHT, /* the radar's legend */
+    LS_RAIN_MODERATE,
+    LS_RAIN_HEAVY,
+    LS_AGO, /* "%s ago": the one "%s" is an age, "3 h" */
     LS_COUNT,
 } lang_str_t;
 
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -186,6 +186,12 @@ const lang_t lang_en = {
         [LS_RAIN_FROM] = "Rain from",
         [LS_DRY_2H] = "Dry for 2 h",
         [LS_MM_PER_H] = "mm/h",
+        [LS_RAIN_MAP] = "Rain map",
+        [LS_NO_RADAR_FRAME] = "No radar frame yet",
+        [LS_RAIN_LIGHT] = "light",
+        [LS_RAIN_MODERATE] = "moderate",
+        [LS_RAIN_HEAVY] = "heavy",
+        [LS_AGO] = "%s ago",
     },
     .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
     .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -226,6 +226,12 @@ const lang_t lang_cs = {
         [LS_RAIN_FROM] = "Déšť od",
         [LS_DRY_2H] = "2 h bez deště",
         [LS_MM_PER_H] = "mm/h",
+        [LS_RAIN_MAP] = "Mapa srážek",
+        [LS_NO_RADAR_FRAME] = "Zatím žádný snímek radaru",
+        [LS_RAIN_LIGHT] = "slabý",
+        [LS_RAIN_MODERATE] = "mírný",
+        [LS_RAIN_HEAVY] = "silný",
+        [LS_AGO] = "před %s",
     },
     .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
     .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
```


- [ ] **Step 7: The app.** KEY short and the auto-cycle pass sync mode `always` to the cycle; a loaded `presets.json` is offered the built-ins and saved if that changed it; a replaced one (the web UI's PUT, a restore) keeps what was offered.

`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -299,7 +299,7 @@ static void handle_button(board_button_t button, gesture_t gesture)
             }
         }
     } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_SHORT) {
-        app_ui_select(ui_presets_next(app_presets()), true);
+        app_ui_select(ui_presets_next(app_presets(), app_state()->settings.sync_mode == SETTINGS_SYNC_ALWAYS), true);
         snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_PRESET), preset_name());
         app_ui_toast(text);
     } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_DOUBLE) {
```


`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -129,6 +129,10 @@ void app_ui_load(void)
     s.first_run = settings == ESP_ERR_NOT_FOUND; /* a new board, or a factory reset (spec §5.5) */
     esp_err_t presets = load_one(STORAGE_PRESETS_PATH, s_file, sizeof(s_file), parse_presets, &s.presets,
                                  sizeof(s.presets), &presets_defaults);
+    if (presets == ESP_OK && ui_presets_offer_builtins(&s.presets)) { /* spec §5.4: the radars, once */
+        ESP_LOGI(TAG, "presets.json: offered the built-in presets it didn't have");
+        app_ui_save_presets();
+    }
     if (settings == ESP_ERR_INVALID_RESPONSE || presets == ESP_ERR_INVALID_RESPONSE) { /* spec §14.3: say so */
         app_ui_toast(lang_str(lang_get(s.settings.language), LS_T_DEFAULTS));
     }
@@ -605,7 +609,8 @@ void app_ui_tick(bool force)
         s.cycle_at = now + s.presets.cycle_interval_s; /* the first tick, or the clock moved back */
     }
     if (s.presets.cycle_enabled && now >= s.cycle_at) {
-        app_ui_select(ui_presets_next(&s.presets), false); /* renders; not saved, the cycle will move on */
+        app_ui_select(ui_presets_next(&s.presets, s.settings.sync_mode == SETTINGS_SYNC_ALWAYS),
+                      false); /* renders; not saved, the cycle will move on */
         return;
     }
     if (render) {
@@ -693,7 +698,9 @@ esp_err_t app_ui_patch_settings(const char *patch, char *err, size_t err_size)
 
 esp_err_t app_ui_replace_presets(const ui_presets_t *presets)
 {
+    uint8_t offered = s.presets.offered;
     s.presets = *presets;
+    s.presets.offered |= offered; /* a page or backup without the marker: what was offered stays offered */
     s.cycle_at = 0;              /* the next tick starts the cycle interval */
     s.sched_checked = time(NULL); /* entries don't run late for a new schedule */
     esp_err_t err = app_ui_save_presets();
```


- [ ] **Step 8: Run the tests and look at the goldens.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `out of 59`, all passing; `test_ui_preset` 22 tests, `test_ui_fields` 19, `test_ui_catalog` 3.

Render the eight new goldens to `/tmp` and `cmp` them with the branch's, as in Task 7 (`radar radar_stale radar_stale_cs radar_loop radar_none radar_rainviewer grid_rain_map weather_rain_map`): no output. In `python3 tools/render.py`'s PNGs: the rain under the borders and towns, the dotted east edge of ČHMÚ's data near 19.6 E, "20:40 · ČHMÚ" and the legend; "17:40 · 3 h ago" inverted (Czech: "před 3 h"); "20:15" and twelve dots, the seventh filled; "No radar frame yet"; Berlin from RainViewer at zoom 5, "20:34 · RainViewer"; the map in grid cells and in the Weather layout's large slot, with the time inverted when old.

- [ ] **Step 9: The firmware builds.** `tools/idf.sh build`: clean.

- [ ] **Step 10: Commit.**

```bash
git add components/ui components/radar components/locale main/app.c main/app_ui.c test/host
git commit -m "feat(ui): the Radar layout, rain.map and the radar presets (D23, D28)"
```


### Task 10: The Flights view (`ui`, `gfx`)

**Files:**
- Create: `components/ui/ui_flights.c`, `test/host/test_ui_flights.c`, six goldens
- Modify: `components/gfx/include/gfx.h`, `components/gfx/gfx.c` (`gfx_fill_triangle`), `test/host/test_gfx.c`
- Modify: `components/ui/include/ui_radar.h`, `components/ui/ui_radar.c` (`ui_fill()` shared), `components/ui/ui_dashboard.c`, `components/ui/ui_internal.h`, `components/ui/CMakeLists.txt`
- Modify: `components/locale/include/lang.h`, `lang_en.c`, `lang_cs.c`
- Modify: `test/host/CMakeLists.txt`, `test/host/radar_fixtures.h`, `test/host/dashboard_fixtures.h`

**Interfaces:**
- Consumes: `adsb_*` (Task 6), `map_*` (Tasks 2–4), `ui_radar_t` (Task 9).
- Produces (Tasks 12, 13):
  - In `ui_radar_t`: `fl_lat_e4`, `fl_lon_e4`, `fl_range_km`, `fl_always`, `fl_updated`, `fl_failed`, `aircraft` (`const adsb_list_t *`), `route` (`const adsb_route_t *`); `UI_FLIGHTS_MAP_H` 238, `UI_FLIGHTS_OLD_S` 120.
  - `void ui_draw_flights_view(gfx_fb_t *, gfx_rect_t, const ui_context_t *)`, `void ui_flights_panel_text(const ui_context_t *, char *line1, char *line2, char *credit, size_t size)`, `void ui_flight_altitude(int32_t alt_ft, char *out, size_t size)`.
  - `void gfx_fill_triangle(gfx_fb_t *, int x0, int y0, int x1, int y1, int x2, int y2, gfx_color_t)`: the pixels whose centres lie inside or on it, in any vertex order, with integer edges (gfx has no libm).

The map is 400 × 238 under the status bar, the range from its centre to its top edge, with rings at the range and half of it, airports with their codes, and home. Aircraft are 12 px arrows turned to the nearest of 16 headings (a dot without a track), each on a white halo, the nearest on top and ringed; labels "TVS7UZ 3675 ft" or "BAW15 FL350" go nearest first, and one without room is left out. The panel below a line at y 259: the nearest's callsign (or address), type, altitude and speed in km/h, then its distance and direction and its route once known, with the credit at the right; or a message: "Flights need sync mode Always on", "No aircraft data" (no poll yet), "No aircraft data (20:40)" (the last good poll more than 2 min old, or failed), "No aircraft within 50 km".

- [ ] **Step 1: Write the failing tests.**

`test/host/test_gfx.c`:

```diff
--- a/test/host/test_gfx.c
+++ b/test/host/test_gfx.c
@@ -237,6 +237,38 @@ static void test_bitmap_draws_ink_only_and_clips(void)
     gfx_bitmap(&s_fb, 0, 0, NULL, GFX_WHITE);
 }
 
+static int count_in(const gfx_fb_t *fb)
+{
+    int n = 0;
+    for (int y = 0; y < fb->height; y++) {
+        for (int x = 0; x < fb->width; x++) {
+            n += gfx_get_pixel(fb, x, y);
+        }
+    }
+    return n;
+}
+
+static void test_a_filled_triangle_covers_its_inside_in_any_order_and_clips(void)
+{
+    static uint8_t buf[32 * 32 / 8];
+    gfx_fb_t fb;
+    gfx_fb_init(&fb, buf, 32, 32);
+    gfx_clear(&fb, GFX_WHITE);
+    gfx_fill_triangle(&fb, 2, 2, 20, 2, 2, 20, GFX_BLACK); /* legs of 19 px: 19 + 18 + ... + 1 */
+    TEST_ASSERT_EQUAL_INT(190, count_in(&fb));
+    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 2, 2) && gfx_get_pixel(&fb, 20, 2) && gfx_get_pixel(&fb, 2, 20));
+    TEST_ASSERT_FALSE(gfx_get_pixel(&fb, 12, 12)); /* beyond the hypotenuse x + y = 22 */
+    gfx_clear(&fb, GFX_WHITE);
+    gfx_fill_triangle(&fb, 2, 20, 20, 2, 2, 2, GFX_BLACK);
+    TEST_ASSERT_EQUAL_INT(190, count_in(&fb));
+    gfx_clear(&fb, GFX_WHITE);
+    gfx_fill_triangle(&fb, 3, 5, 9, 5, 6, 5, GFX_BLACK); /* flat: a line */
+    TEST_ASSERT_EQUAL_INT(7, count_in(&fb));
+    gfx_fill_triangle(&s_fb, -100, -100, 300, -100, -100, 300, GFX_BLACK); /* far past the 16 x 4 buffer */
+    TEST_ASSERT_EQUAL_INT(64, count_black());
+    assert_guards_intact();
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -254,5 +286,6 @@ int main(void)
     RUN_TEST(test_circle_is_symmetric_and_draws_each_pixel_once);
     RUN_TEST(test_filled_circle_covers_its_outline);
     RUN_TEST(test_bitmap_draws_ink_only_and_clips);
+    RUN_TEST(test_a_filled_triangle_covers_its_inside_in_any_order_and_clips);
     return UNITY_END();
 }
```


`test/host/test_ui_flights.c`:

```c
#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <string.h>

#include "context_fixtures.h"
#include "radar_fixtures.h"
#include "ui_radar.h"
#include "unity.h"

/* The Flights panel's words (spec §11.3) in each state; the goldens show where they go. */

static ui_context_t s_ctx;
static char s_line1[96], s_line2[96], s_credit[32];

void setUp(void)
{
    s_ctx = fixture_context();
}

void tearDown(void) {}

static void panel(void)
{
    ui_flights_panel_text(&s_ctx, s_line1, s_line2, s_credit, sizeof(s_line1));
}

static void test_the_nearest_aircraft_with_its_route(void)
{
    s_ctx.radar = fixture_flights(50, fixture_aircraft(50), fixture_route(), s_ctx.now);
    panel();
    TEST_ASSERT_EQUAL_STRING("TVS7UZ \xC2\xB7 B38M \xC2\xB7 3675 ft \xC2\xB7 459 km/h", s_line1); /* 247.8 kt */
    TEST_ASSERT_EQUAL_STRING("22 km SE \xC2\xB7 BRQ Brno \xE2\x86\x92 AYT Antalya", s_line2);
    TEST_ASSERT_EQUAL_STRING("adsb.fi \xC2\xB7 adsb.lol", s_credit);
    s_ctx.lang = lang_get("cs");
    panel();
    TEST_ASSERT_EQUAL_STRING("22 km JV \xC2\xB7 BRQ Brno \xE2\x86\x92 AYT Antalya", s_line2);
}

static void test_flight_levels_from_ten_thousand_feet(void)
{
    char text[16];
    ui_flight_altitude(33800, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("FL338", text);
    ui_flight_altitude(36975, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("FL370", text); /* to the nearest hundred feet */
    ui_flight_altitude(10000, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("FL100", text);
    ui_flight_altitude(9975, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("9975 ft", text);
    ui_flight_altitude(ADSB_ALT_GROUND, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("GND", text);
    ui_flight_altitude(ADSB_ALT_UNKNOWN, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("", text);
}

static void test_an_aircraft_without_a_callsign_type_or_speed(void)
{
    static adsb_list_t list;
    list = (adsb_list_t){ .count = 1 };
    list.ac[0] = (adsb_aircraft_t){ .hex = "~bbbbb4", .alt_ft = ADSB_ALT_UNKNOWN, .speed_kt = -1, .track = -1,
                                    .dist_m = 1400, .bearing = 359 };
    s_ctx.radar = fixture_flights(50, &list, NULL, s_ctx.now);
    panel();
    TEST_ASSERT_EQUAL_STRING("~BBBBB4", s_line1); /* its address, as nothing else is known */
    TEST_ASSERT_EQUAL_STRING("1 km N", s_line2);
    TEST_ASSERT_EQUAL_STRING("adsb.fi", s_credit);
}

static void test_the_messages_when_there_is_nothing_to_show(void)
{
    static const adsb_list_t k_empty;
    ui_radar_t *r = fixture_flights(50, &k_empty, NULL, s_ctx.now);
    s_ctx.radar = r;
    panel();
    TEST_ASSERT_EQUAL_STRING("No aircraft within 50 km", s_line1);
    TEST_ASSERT_EQUAL_STRING("", s_line2);

    r->aircraft = fixture_aircraft(50);
    r->fl_failed = true; /* a failure after a poll 4 s ago: its aircraft still show */
    panel();
    TEST_ASSERT_EQUAL_STRING("TVS7UZ", strtok(s_line1, " "));
    r->fl_updated = s_ctx.now - 8 * 60; /* the last good poll at 20:40 */
    panel();
    TEST_ASSERT_EQUAL_STRING("No aircraft data (20:40)", s_line1);
    r->fl_failed = false; /* no failure, but nothing new for 2 min: the same */
    r->fl_updated = s_ctx.now - UI_FLIGHTS_OLD_S - 1;
    panel();
    TEST_ASSERT_EQUAL_STRING("No aircraft data (20:45)", s_line1);
    r->fl_updated = 0; /* not polled yet */
    panel();
    TEST_ASSERT_EQUAL_STRING("No aircraft data", s_line1);
    r->fl_always = false;
    panel();
    TEST_ASSERT_EQUAL_STRING("Flights need sync mode Always on", s_line1);
    s_ctx.radar = NULL;
    panel();
    TEST_ASSERT_EQUAL_STRING("Flights need sync mode Always on", s_line1);
    s_ctx.lang = lang_get("cs");
    panel();
    TEST_ASSERT_EQUAL_STRING("Lety jen v synchronizaci Stále", s_line1);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_nearest_aircraft_with_its_route);
    RUN_TEST(test_flight_levels_from_ten_thousand_feet);
    RUN_TEST(test_an_aircraft_without_a_callsign_type_or_speed);
    RUN_TEST(test_the_messages_when_there_is_nothing_to_show);
    return UNITY_END();
}
```


`test/host/radar_fixtures.h`:

```diff
--- a/test/host/radar_fixtures.h
+++ b/test/host/radar_fixtures.h
@@ -3,14 +3,17 @@
 #include <stdbool.h>
 #include <stdio.h>
 #include <stdlib.h>
+#include <string.h>
 
+#include "adsb.h"
 #include "map_data.h"
 #include "radar.h"
 #include "ui_radar.h"
 
 /* The radars' inputs for the golden renders (dashboard_fixtures.h): the built-in map, ČHMÚ's rainy
  * frame of 2026-09-24 11:20 UTC and RainViewer's tile of zoom 3 (test_radar.c's fixtures), each
- * loaded once. REFLBO_MAP_BIN and REFLBO_PNG_FIXTURES come from CMake. */
+ * loaded once; and adsb.fi's reply and adsb.lol's route of test_adsb.c. REFLBO_MAP_BIN,
+ * REFLBO_PNG_FIXTURES and REFLBO_ADSB_FIXTURES come from CMake. */
 
 static inline uint8_t *fixture_bytes(const char *path, size_t *len)
 {
@@ -84,3 +87,51 @@ static inline ui_radar_t *fixture_radar(const radar_frame_t *frame)
                       .wx_lat_e4 = 491951, .wx_lon_e4 = 166068, .wx_zoom_q = 26 };
     return &r;
 }
+
+/* adsb.fi's reply around Brno, kept for the Flights map of `range_km`, and the nearest one's route. */
+static inline const adsb_list_t *fixture_aircraft(int range_km)
+{
+    static adsb_list_t list;
+    size_t len = 0;
+    uint8_t *json = fixture_bytes(REFLBO_ADSB_FIXTURES "/adsb_fi.json", &len);
+    char *text = json != NULL ? realloc(json, len + 1) : NULL;
+    adsb_filter_t f = { .lat = 49.1951, .lon = 16.6068, .max = ADSB_MAX };
+    map_view_init(&f.view, 491951, 166068, map_zoom_for_range(491951, range_km * 1000.0, UI_FLIGHTS_MAP_H / 2), 400,
+                  UI_FLIGHTS_MAP_H);
+    memset(&list, 0, sizeof(list));
+    if (text != NULL) {
+        text[len] = '\0';
+        adsb_parse(text, len, &f, &list);
+    }
+    free(text);
+    return &list;
+}
+
+static inline const adsb_route_t *fixture_route(void)
+{
+    static adsb_route_t r;
+    size_t len = 0;
+    uint8_t *json = fixture_bytes(REFLBO_ADSB_FIXTURES "/route_tvs7uz.json", &len);
+    char *text = json != NULL ? realloc(json, len + 1) : NULL;
+    if (text != NULL) {
+        text[len] = '\0';
+        adsb_route_parse(text, len, &r);
+    }
+    free(text);
+    return &r;
+}
+
+/* The flight radar over Brno: sync mode `always` with a poll a few seconds ago, unless changed. */
+static inline ui_radar_t *fixture_flights(int range_km, const adsb_list_t *aircraft, const adsb_route_t *route,
+                                          time_t now)
+{
+    ui_radar_t *r = fixture_radar(NULL);
+    r->fl_lat_e4 = 491951;
+    r->fl_lon_e4 = 166068;
+    r->fl_range_km = (uint8_t)range_km;
+    r->fl_always = true;
+    r->fl_updated = now - 4;
+    r->aircraft = aircraft;
+    r->route = route;
+    return r;
+}
```


`test/host/dashboard_fixtures.h`:

```diff
--- a/test/host/dashboard_fixtures.h
+++ b/test/host/dashboard_fixtures.h
@@ -209,6 +209,31 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
         preset->slots[0] = UI_FIELD_RAIN_MAP;
         fixture_forecast(&s_fix_ds, FIX_NOW - 3600);
         ctx->radar = fixture_radar(fixture_chmu(FIX_NOW - 8 * 60));
+    } else if (strcmp(name, "flights") == 0) { /* M6: the Flights preset at 50 km, the nearest's route known */
+        *preset = fixture_preset("flights");
+        ctx->radar = fixture_flights(50, fixture_aircraft(50), fixture_route(), ctx->now);
+    } else if (strcmp(name, "flights_100") == 0) { /* 100 km: more aircraft, labels that must give way */
+        *preset = fixture_preset("flights");
+        ctx->radar = fixture_flights(100, fixture_aircraft(100), NULL, ctx->now);
+    } else if (strcmp(name, "flights_cs") == 0) {
+        fixture_dashboard("flights", ctx, preset);
+        ctx->lang = lang_get("cs");
+    } else if (strcmp(name, "flights_none") == 0) { /* a poll with nobody in the sky */
+        *preset = fixture_preset("flights");
+        static const adsb_list_t k_empty;
+        ctx->radar = fixture_flights(50, &k_empty, NULL, ctx->now);
+    } else if (strcmp(name, "flights_failed") == 0) { /* the last good poll at 20:40 */
+        *preset = fixture_preset("flights");
+        ui_radar_t *radar = fixture_flights(50, fixture_aircraft(50), NULL, ctx->now);
+        radar->fl_updated = ctx->now - 8 * 60;
+        radar->fl_failed = true;
+        ctx->radar = radar;
+    } else if (strcmp(name, "flights_off") == 0) { /* not sync mode `always` (spec §5.4) */
+        *preset = fixture_preset("flights");
+        ui_radar_t *radar = fixture_flights(50, NULL, NULL, ctx->now);
+        radar->fl_always = false;
+        radar->fl_updated = 0;
+        ctx->radar = radar;
     } else if (strcmp(name, "grid_clock_12h") == 0) { /* a clock in a grid cell, 12-hour */
         *preset = fixture_preset("indoor");
         preset->slots[0] = UI_FIELD_TIME_CLOCK;
@@ -226,9 +251,12 @@ static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather",
                                                     "indoor_hot_f", "indoor_frost", "home_frost", "grid_clock_12h",
                                                     "home_cs", "indoor_cs", "home_holiday_cs", "home_low_battery",
                                                     "home_web", "home_stale_web", "weather_now",
-                                                    "weather_noon_cs", "weather_stale", "air_grid", "air_grid_cs", "grid_sun_uv", "home_forecast",
+                                                    "weather_noon_cs", "weather_stale", "air_grid", "air_grid_cs",
+                                                    "grid_sun_uv", "home_forecast",
                                                     "focus_forecast", "home_syncing", "home_sync_failed",
                                                     "home_always", "home_always_rejoining", "weather_rain",
                                                     "focus_rain_now", "grid_rain_cs", "radar", "radar_stale",
                                                     "radar_stale_cs", "radar_loop", "radar_none",
-                                                    "radar_rainviewer", "grid_rain_map", "weather_rain_map" };
+                                                    "radar_rainviewer", "grid_rain_map", "weather_rain_map",
+                                                    "flights", "flights_100", "flights_cs", "flights_none",
+                                                    "flights_failed", "flights_off" };
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -12,7 +12,8 @@ set(CMAKE_C_EXTENSIONS OFF)
 set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
 # The radar views' golden renders read the built-in map and the radar fixtures (radar_fixtures.h).
 add_compile_definitions(REFLBO_MAP_BIN="${REPO_ROOT}/assets/map/map.bin"
-                        REFLBO_PNG_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/png")
+                        REFLBO_PNG_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/png"
+                        REFLBO_ADSB_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/adsb")
 set(REFLBO_WARNINGS -Wall -Wextra -Werror)
 
 # cmake -DREFLBO_SANITIZE=ON: everything with AddressSanitizer and UndefinedBehaviorSanitizer.
@@ -185,7 +186,8 @@ file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
 add_library(ui STATIC ${UI_SOURCES})
 target_include_directories(ui PUBLIC ${REPO_ROOT}/components/ui/include)
 target_compile_options(ui PRIVATE ${REFLBO_WARNINGS})
-target_link_libraries(ui PUBLIC gfx locale datastore util map radar_logic PRIVATE cjson scheduler weather_logic astro m)
+target_link_libraries(ui PUBLIC gfx locale datastore util map radar_logic adsb_logic
+                      PRIVATE cjson scheduler weather_logic astro m)
 
 # reflbo_host_test(<name> <libs...>): builds <name>.c against Unity and registers it with ctest.
 function(reflbo_host_test name)
@@ -252,6 +254,7 @@ reflbo_host_test(test_ui_widget_fit ui)
 reflbo_host_test(test_ui_menu ui)
 reflbo_host_test(test_ui_config ui)
 reflbo_host_test(test_ui_catalog ui cjson)
+reflbo_host_test(test_ui_flights ui)
 reflbo_host_test(test_settings storage_logic)
 reflbo_host_test(test_storage_file storage_logic)
 target_compile_definitions(test_storage_file PRIVATE TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/storage_tmp")
```


Copy the goldens from the branch:

```bash
git checkout plan/m6 -- \
  test/host/golden/dash_flights.pbm \
  test/host/golden/dash_flights_100.pbm \
  test/host/golden/dash_flights_cs.pbm \
  test/host/golden/dash_flights_failed.pbm \
  test/host/golden/dash_flights_none.pbm \
  test/host/golden/dash_flights_off.pbm
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host 2>&1 | grep -m2 error`
Expected: `call to undeclared function 'gfx_fill_triangle'` in `test_gfx.c`; once Step 3 builds, `call to undeclared function 'ui_flights_panel_text'` in `test_ui_flights.c`.

- [ ] **Step 3: The triangle.**

`components/gfx/include/gfx.h`:

```diff
--- a/components/gfx/include/gfx.h
+++ b/components/gfx/include/gfx.h
@@ -49,6 +49,8 @@ void gfx_rect(gfx_fb_t *fb, gfx_rect_t r, gfx_color_t color); /* outline, each p
 void gfx_fill_rect(gfx_fb_t *fb, gfx_rect_t r, gfx_color_t color);
 void gfx_circle(gfx_fb_t *fb, int cx, int cy, int r, gfx_color_t color); /* outline, each pixel drawn once */
 void gfx_fill_circle(gfx_fb_t *fb, int cx, int cy, int r, gfx_color_t color);
+/* The pixels whose centres lie inside or on the triangle, in any vertex order. */
+void gfx_fill_triangle(gfx_fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2, gfx_color_t color);
 
 /* 1-bpp image in the glyph format: rows MSB first, each row padded to whole bytes, 1 = ink. */
 typedef struct {
```


`components/gfx/gfx.c`:

```diff
--- a/components/gfx/gfx.c
+++ b/components/gfx/gfx.c
@@ -1,6 +1,7 @@
 #include "gfx.h"
 
 #include <stdbool.h>
+#include <stdlib.h>
 #include <string.h>
 
 static int min_int(int a, int b)
@@ -270,6 +271,46 @@ void gfx_fill_circle(gfx_fb_t *fb, int cx, int cy, int r, gfx_color_t color)
     }
 }
 
+static long floor_div(long a, long b) /* b > 0 */
+{
+    long q = a / b;
+    return a % b != 0 && a < 0 ? q - 1 : q;
+}
+
+/* Where the edge from (ax, ay) down to (bx, by) crosses row y, to the nearest pixel. */
+static int edge_x(int ax, int ay, int bx, int by, int y)
+{
+    if (by == ay) {
+        return ax;
+    }
+    long dy = by - ay;
+    return ax + (int)floor_div(2L * (y - ay) * (bx - ax) + dy, 2 * dy);
+}
+
+void gfx_fill_triangle(gfx_fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2, gfx_color_t color)
+{
+    int t;
+#define SWAP_IF(a, b)                                                                                                  \
+    if (y##a > y##b) {                                                                                                 \
+        t = x##a, x##a = x##b, x##b = t;                                                                               \
+        t = y##a, y##a = y##b, y##b = t;                                                                               \
+    }
+    SWAP_IF(0, 1)
+    SWAP_IF(1, 2)
+    SWAP_IF(0, 1)
+#undef SWAP_IF
+    int top = max_int(y0, fb->clip.y), bottom = min_int(y2, fb->clip.y + fb->clip.h - 1);
+    for (int y = top; y <= bottom; y++) {
+        int a = edge_x(x0, y0, x2, y2, y);                                          /* the long edge */
+        int b = y < y1 ? edge_x(x0, y0, x1, y1, y) : edge_x(x1, y1, x2, y2, y);   /* the two short ones */
+        if (y0 == y2) { /* flat: from the leftmost vertex to the rightmost */
+            a = min_int(x0, min_int(x1, x2));
+            b = max_int(x0, max_int(x1, x2));
+        }
+        gfx_hline(fb, min_int(a, b), y, abs(b - a) + 1, color);
+    }
+}
+
 void gfx_bitmap(gfx_fb_t *fb, int x, int y, const gfx_bitmap_t *bm, gfx_color_t color)
 {
     if (bm == NULL || bm->bits == NULL) {
```


- [ ] **Step 4: The view.** As in Task 9, the labels and the aircraft's screen positions (another 800 bytes) are statics, off the app task's stack.

`components/ui/include/ui_radar.h`:

```diff
--- a/components/ui/include/ui_radar.h
+++ b/components/ui/include/ui_radar.h
@@ -2,6 +2,7 @@
 
 #include <stdint.h>
 
+#include "adsb.h"
 #include "gfx.h"
 #include "map_data.h"
 #include "radar.h"
@@ -13,6 +14,8 @@
  */
 
 #define UI_RADAR_OLD_S (30 * 60) /* an older frame shows its time inverted, with its age (spec §11.2) */
+#define UI_FLIGHTS_MAP_H 238      /* the Flights map, 400 px wide, over its separator and 40 px panel */
+#define UI_FLIGHTS_OLD_S 120      /* aircraft from an older poll are no longer shown (spec §11.3) */
 
 struct ui_radar {
     const map_data_t *map;            /* the built-in map; NULL: none drawn */
@@ -21,8 +24,24 @@ struct ui_radar {
     int32_t wx_lat_e4, wx_lon_e4;     /* radar.weather's centre */
     uint8_t wx_zoom_q;                /* and its zoom, in quarters */
     uint8_t loop_at, loop_count;      /* the loop (D28): frame loop_at of loop_count; count 0 outside it */
+    int32_t fl_lat_e4, fl_lon_e4;     /* radar.flights' centre */
+    uint8_t fl_range_km;              /* and its range, from the centre to the map's top edge */
+    bool fl_always;                   /* sync mode `always`: the flight radar runs (D22) */
+    time_t fl_updated;                /* the last good poll, UTC; 0 = none yet */
+    bool fl_failed;                   /* the last poll failed */
+    const adsb_list_t *aircraft;      /* from the last good poll, the nearest first */
+    const adsb_route_t *route;        /* the nearest one's route, once known (D27) */
 };
 
 /* The Radar layout's map in `r`: the rain, the frame's time and source at the bottom left, the legend
  * or the loop's progress at the bottom right; "No radar frame yet" before the first. */
 void ui_draw_radar_view(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx);
+
+/* The Flights layout under the status bar in `r`: the map with its rings, the aircraft, and the
+ * panel for the nearest one (spec §11.3). */
+void ui_draw_flights_view(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx);
+/* The panel's words: the nearest aircraft ("TVS7UZ · B38M · 3675 ft · 459 km/h") over its distance,
+ * direction and route, or a message over nothing; `credit` names the sources shown. */
+void ui_flights_panel_text(const ui_context_t *ctx, char *line1, char *line2, char *credit, size_t size);
+/* "FL338" from 10 000 ft, "9975 ft" below, "GND" on the ground, "" when unknown. */
+void ui_flight_altitude(int32_t alt_ft, char *out, size_t size);
```


`components/ui/ui_flights.c`:

```c
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "map_draw.h"
#include "ui_internal.h"
#include "ui_radar.h"

/* The flight radar's view (spec §11.3): the map at its centre and range with two rings, the
 * aircraft as arrows turned to their track, and a panel for the nearest one. */

#define PI 3.14159265358979323846
#define PANEL_H 40
#define KMH_PER_KT 1.852
#define ARROW_BOX 14 /* a 12 px arrow and its halo */
#define NEAREST_R 10 /* the ring that marks the panel's aircraft */

static const lang_str_t k_dirs[8] = { LS_DIR_N, LS_DIR_NE, LS_DIR_E, LS_DIR_SE,
                                      LS_DIR_S, LS_DIR_SW, LS_DIR_W, LS_DIR_NW };

/* The arrow pointing north around (0, 0): its tip, left rear, notch and right rear. */
static const int8_t k_arrow[4][2] = { { 0, -6 }, { -5, 5 }, { 0, 2 }, { 5, 5 } };

void ui_flight_altitude(int32_t alt_ft, char *out, size_t size)
{
    if (alt_ft == ADSB_ALT_GROUND) {
        snprintf(out, size, "GND");
    } else if (alt_ft == ADSB_ALT_UNKNOWN) {
        snprintf(out, size, "%s", "");
    } else if (alt_ft >= 10000) {
        snprintf(out, size, "FL%03ld", ((long)alt_ft + 50) / 100);
    } else {
        snprintf(out, size, "%ld ft", (long)alt_ft);
    }
}

/* Aircraft from a poll at most UI_FLIGHTS_OLD_S old; a clock set back counts as recent. */
static bool fresh(const ui_context_t *ctx, const ui_radar_t *r)
{
    return r->fl_always && r->aircraft != NULL && r->fl_updated != 0 &&
           ctx->now - r->fl_updated <= UI_FLIGHTS_OLD_S;
}

/* Its callsign, or its address when it sends none. */
static void name_of(const adsb_aircraft_t *a, char *out, size_t size)
{
    if (a->callsign[0] != '\0') {
        snprintf(out, size, "%s", a->callsign);
        return;
    }
    size_t n = 0;
    for (; a->hex[n] != '\0' && n + 1 < size; n++) {
        char c = a->hex[n];
        out[n] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    out[n] = '\0';
}

static void cat(char *out, size_t size, const char *part)
{
    size_t n = strlen(out);
    if (n + 1 < size) {
        snprintf(out + n, size - n, "%s", part);
    }
}

static void airport_text(const adsb_airport_t *ap, char *out, size_t size)
{
    const char *code = ap->iata[0] != '\0' ? ap->iata : ap->icao;
    snprintf(out, size, "%s%s%s", code, code[0] != '\0' && ap->place[0] != '\0' ? " " : "", ap->place);
}

void ui_flights_panel_text(const ui_context_t *ctx, char *line1, char *line2, char *credit, size_t size)
{
    const ui_radar_t *r = ctx->radar;
    line1[0] = line2[0] = '\0';
    snprintf(credit, size, "adsb.fi");
    if (r == NULL || !r->fl_always) {
        snprintf(line1, size, "%s", lang_str(ctx->lang, LS_FLIGHTS_NEED_ALWAYS));
        return;
    }
    if (!fresh(ctx, r)) {
        snprintf(line1, size, "%s", lang_str(ctx->lang, LS_NO_AIRCRAFT_DATA));
        if (r->fl_updated != 0) { /* "(20:40)": when the last good poll was */
            char at[16], when[24];
            ui_clock_text(ctx, r->fl_updated, at, sizeof(at));
            snprintf(when, sizeof(when), " (%s)", at);
            cat(line1, size, when);
        }
        return;
    }
    if (r->aircraft->count == 0) {
        char km[16];
        snprintf(km, sizeof(km), "%d km", r->fl_range_km);
        ui_fill(lang_str(ctx->lang, LS_NO_AIRCRAFT), km, line1, size);
        return;
    }
    const adsb_aircraft_t *a = &r->aircraft->ac[0];
    char part[48];
    name_of(a, line1, size);
    if (a->type[0] != '\0') {
        snprintf(part, sizeof(part), " \xC2\xB7 %s", a->type);
        cat(line1, size, part);
    }
    char alt[16];
    ui_flight_altitude(a->alt_ft, alt, sizeof(alt));
    if (alt[0] != '\0') {
        snprintf(part, sizeof(part), " \xC2\xB7 %s", alt);
        cat(line1, size, part);
    }
    if (a->speed_kt >= 0) {
        snprintf(part, sizeof(part), " \xC2\xB7 %ld km/h", lround(a->speed_kt * KMH_PER_KT));
        cat(line1, size, part);
    }
    snprintf(line2, size, "%lu km %s", (unsigned long)((a->dist_m + 500) / 1000),
             lang_str(ctx->lang, k_dirs[(a->bearing + 22) / 45 % 8]));
    const adsb_route_t *route = r->route;
    if (route != NULL && route->known && a->callsign[0] != '\0' && strcmp(route->callsign, a->callsign) == 0) {
        char from[40], to[40], leg[96];
        airport_text(&route->from, from, sizeof(from));
        airport_text(&route->to, to, sizeof(to));
        snprintf(leg, sizeof(leg), " \xC2\xB7 %s \xE2\x86\x92 %s", from, to);
        cat(line2, size, leg);
        snprintf(credit, size, "adsb.fi \xC2\xB7 adsb.lol"); /* D27: the route's source too */
    }
}

/* An arrow turned to the nearest of 16 headings; a dot when the track is unknown. */
static void arrow_at(gfx_fb_t *fb, int cx, int cy, int track, gfx_color_t color)
{
    if (track < 0) {
        gfx_fill_circle(fb, cx, cy, 3, color);
        return;
    }
    int step = (track * 2 + 22) / 45 % 16; /* track / 22.5, rounded */
    double a = step * 22.5 * PI / 180, c = cos(a), s = sin(a);
    int x[4], y[4];
    for (int i = 0; i < 4; i++) { /* clockwise on screen, where y grows downwards */
        x[i] = cx + (int)lround(k_arrow[i][0] * c - k_arrow[i][1] * s);
        y[i] = cy + (int)lround(k_arrow[i][0] * s + k_arrow[i][1] * c);
    }
    gfx_fill_triangle(fb, x[0], y[0], x[1], y[1], x[2], y[2], color);
    gfx_fill_triangle(fb, x[0], y[0], x[2], y[2], x[3], y[3], color);
}

static void arrow(gfx_fb_t *fb, int cx, int cy, int track)
{
    for (int dy = -1; dy <= 1; dy++) { /* a white halo, so it reads over a border or a ring */
        for (int dx = -1; dx <= 1; dx++) {
            if (dx != 0 || dy != 0) {
                arrow_at(fb, cx + dx, cy + dy, track, GFX_WHITE);
            }
        }
    }
    arrow_at(fb, cx, cy, track, GFX_BLACK);
}

static void draw_aircraft(gfx_fb_t *fb, gfx_rect_t area, const map_view_t *v, const adsb_list_t *list,
                          map_labels_t *labels)
{
    static int x[ADSB_MAX], y[ADSB_MAX]; /* off the stack, as the labels: only the app task draws */
    for (int i = 0; i < list->count; i++) {
        double px, py;
        map_project(v, list->ac[i].lat, list->ac[i].lon, &px, &py);
        x[i] = area.x + (int)lround(px);
        y[i] = area.y + (int)lround(py);
        map_labels_reserve(labels, (gfx_rect_t){ (int16_t)(x[i] - ARROW_BOX / 2), (int16_t)(y[i] - ARROW_BOX / 2),
                                                 ARROW_BOX, ARROW_BOX });
    }
    for (int i = 0; i < list->count; i++) { /* the nearest first; a label without room is left out */
        char name[12], alt[16], label[32];
        name_of(&list->ac[i], name, sizeof(name));
        ui_flight_altitude(list->ac[i].alt_ft, alt, sizeof(alt));
        snprintf(label, sizeof(label), "%s%s%s", name, alt[0] != '\0' ? " " : "", alt);
        map_label(fb, area, labels, &gfx_font_sans_12, x[i], y[i], ARROW_BOX / 2 + 2, label);
    }
    for (int i = list->count - 1; i >= 0; i--) { /* the nearest on top */
        arrow(fb, x[i], y[i], list->ac[i].track);
    }
    if (list->count > 0) {
        gfx_circle(fb, x[0], y[0], NEAREST_R + 1, GFX_WHITE);
        gfx_circle(fb, x[0], y[0], NEAREST_R, GFX_BLACK);
    }
}

void ui_draw_flights_view(gfx_fb_t *fb, gfx_rect_t r, const ui_context_t *ctx)
{
    ui_radar_t at_home = { .home_lat_e4 = ctx->lat_e4, .home_lon_e4 = ctx->lon_e4, .fl_lat_e4 = ctx->lat_e4,
                           .fl_lon_e4 = ctx->lon_e4, .fl_range_km = 50 };
    const ui_radar_t *rad = ctx->radar != NULL ? ctx->radar : &at_home; /* no radar at all: an empty map */
    gfx_rect_t area = { r.x, r.y, r.w, UI_FLIGHTS_MAP_H };
    map_view_t v;
    double range_m = rad->fl_range_km * 1000.0;
    map_view_init(&v, rad->fl_lat_e4, rad->fl_lon_e4, map_zoom_for_range(rad->fl_lat_e4, range_m, area.h / 2), area.w,
                  area.h);
    gfx_rect_t saved = fb->clip;
    gfx_set_clip(fb, gfx_rect_intersect(saved, area));
    map_style_t style = { .airports = true, .halo = false, .max_towns = 8 };
    if (rad->map != NULL) {
        map_draw_lines(fb, area, &v, rad->map, &style);
    }
    static map_labels_t labels; /* 1.5 KB, off the stack: only the app task draws (spec §3.2) */
    map_labels_init(&labels);
    map_draw_rings(fb, area, &v, range_m, &labels);
    map_draw_home(fb, area, &v, rad->home_lat_e4, rad->home_lon_e4, &labels);
    if (fresh(ctx, rad)) {
        draw_aircraft(fb, area, &v, rad->aircraft, &labels);
    }
    if (rad->map != NULL) {
        map_draw_places(fb, area, &v, rad->map, &style, &labels);
    }
    fb->clip = saved;

    gfx_rect_t panel = { r.x, (int16_t)(r.y + r.h - PANEL_H), r.w, PANEL_H };
    gfx_hline(fb, r.x, panel.y - 1, r.w, GFX_BLACK);
    char line1[96], line2[96], credit[32], fit[96];
    ui_flights_panel_text(ctx, line1, line2, credit, sizeof(line1));
    const gfx_font_t *f1 = &gfx_font_sans_bold_16, *f2 = &gfx_font_sans_12;
    gfx_text_ellipsize(f1, line1, panel.w - 12, fit, sizeof(fit));
    gfx_text(fb, f1, panel.x + 6, panel.y + 2 + f1->ascent, fit, GFX_BLACK);
    int credit_w = gfx_text_width(f2, credit);
    int base2 = panel.y + panel.h - 4 - (f2->line_height - f2->ascent);
    gfx_text(fb, f2, panel.x + panel.w - 6 - credit_w, base2, credit, GFX_BLACK);
    gfx_text_ellipsize(f2, line2, panel.w - 12 - credit_w - 10, fit, sizeof(fit));
    gfx_text(fb, f2, panel.x + 6, base2, fit, GFX_BLACK);
}
```


`components/ui/ui_radar.c`:

```diff
--- a/components/ui/ui_radar.c
+++ b/components/ui/ui_radar.c
@@ -16,8 +16,7 @@ static bool frame_old(time_t now, const radar_frame_t *f)
     return now > (time_t)f->time && now - (time_t)f->time > UI_RADAR_OLD_S;
 }
 
-/* The pattern's "%s" replaced by `value`, as the packs' LS_AGO has it ("%s ago", "před %s"). */
-static void fill(const char *pattern, const char *value, char *out, size_t size)
+void ui_fill(const char *pattern, const char *value, char *out, size_t size)
 {
     const char *at = strstr(pattern, "%s");
     if (at == NULL) {
@@ -37,7 +36,7 @@ static void caption_text(const ui_context_t *ctx, const radar_frame_t *f, bool b
     } else if (frame_old(ctx->now, f)) {
         char age[16], ago[32];
         ui_format_age(ctx->lang, (uint32_t)(ctx->now - (time_t)f->time), age, sizeof(age));
-        fill(lang_str(ctx->lang, LS_AGO), age, ago, sizeof(ago));
+        ui_fill(lang_str(ctx->lang, LS_AGO), age, ago, sizeof(ago));
         snprintf(out, size, "%s \xC2\xB7 %s", at, ago);
     } else {
         snprintf(out, size, "%s \xC2\xB7 %s", at, f->source == RADAR_SOURCE_CHMU ? "\xC4\x8CHM\xC3\x9A" : "RainViewer");
```


`components/ui/ui_internal.h`:

```diff
--- a/components/ui/ui_internal.h
+++ b/components/ui/ui_internal.h
@@ -13,6 +13,8 @@
 void ui_clock_text(const ui_context_t *ctx, time_t t, char *out, size_t size);
 /* An age as the stale mark shows it: "45 min", "3 h", "2 d" (ui_widget.c). */
 void ui_format_age(const lang_t *lang, uint32_t age_s, char *out, size_t size);
+/* The pattern's one "%s" replaced by `value`, as the packs' LS_AGO has it ("%s ago", "před %s"). */
+void ui_fill(const char *pattern, const char *value, char *out, size_t size);
 /* rain.map (ui_radar.c, M6); false for any other field. */
 bool ui_resolve_radar(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
 /* Its widget in an M or L slot; false for any other kind. */
```


`components/ui/ui_dashboard.c`:

```diff
--- a/components/ui/ui_dashboard.c
+++ b/components/ui/ui_dashboard.c
@@ -44,9 +44,11 @@ void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t
     gfx_clear(fb, GFX_WHITE);
     const ui_layout_t *layout = ui_layout((ui_layout_id_t)preset->layout);
     bool any_stale = false;
+    gfx_rect_t below = { 0, UI_STATUS_H + 1, fb->width, (int16_t)(fb->height - UI_STATUS_H - 1) };
     if (preset->layout == UI_LAYOUT_RADAR) { /* spec §5.2: the map under the status bar */
-        ui_draw_radar_view(fb, (gfx_rect_t){ 0, UI_STATUS_H + 1, fb->width, (int16_t)(fb->height - UI_STATUS_H - 1) },
-                           &c);
+        ui_draw_radar_view(fb, below, &c);
+    } else if (preset->layout == UI_LAYOUT_FLIGHTS) {
+        ui_draw_flights_view(fb, below, &c);
     } else if (layout != NULL) {
         draw_separators(fb, (ui_layout_id_t)preset->layout);
         for (int i = 0; i < layout->slot_count; i++) {
```


`components/ui/CMakeLists.txt`:

```diff
--- a/components/ui/CMakeLists.txt
+++ b/components/ui/CMakeLists.txt
@@ -2,6 +2,7 @@
 idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset_json.c" "ui_widget.c"
                             "ui_status.c" "ui_dashboard.c" "ui_schedule.c" "ui_menu.c" "ui_menu_draw.c"
                             "ui_screens.c" "ui_config.c" "ui_catalog.c" "ui_forecast.c" "ui_radar.c"
+                            "ui_flights.c"
                        INCLUDE_DIRS "include"
-                       REQUIRES gfx locale datastore util map radar
+                       REQUIRES gfx locale datastore util map radar adsb
                        PRIV_REQUIRES json scheduler weather astro)
```


- [ ] **Step 5: The strings.**

`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -184,6 +184,17 @@ typedef enum {
     LS_RAIN_MODERATE,
     LS_RAIN_HEAVY,
     LS_AGO, /* "%s ago": the one "%s" is an age, "3 h" */
+    LS_FLIGHTS_NEED_ALWAYS, /* the Flights view outside sync mode `always` (spec §5.4) */
+    LS_NO_AIRCRAFT,         /* "No aircraft within %s": the one "%s" is the range, "50 km" */
+    LS_NO_AIRCRAFT_DATA,    /* followed by " (20:40)", the last good poll, when there was one */
+    LS_DIR_N,               /* the 8 directions, clockwise from north */
+    LS_DIR_NE,
+    LS_DIR_E,
+    LS_DIR_SE,
+    LS_DIR_S,
+    LS_DIR_SW,
+    LS_DIR_W,
+    LS_DIR_NW,
     LS_COUNT,
 } lang_str_t;
 
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -192,6 +192,17 @@ const lang_t lang_en = {
         [LS_RAIN_MODERATE] = "moderate",
         [LS_RAIN_HEAVY] = "heavy",
         [LS_AGO] = "%s ago",
+        [LS_FLIGHTS_NEED_ALWAYS] = "Flights need sync mode Always on",
+        [LS_NO_AIRCRAFT] = "No aircraft within %s",
+        [LS_NO_AIRCRAFT_DATA] = "No aircraft data",
+        [LS_DIR_N] = "N",
+        [LS_DIR_NE] = "NE",
+        [LS_DIR_E] = "E",
+        [LS_DIR_SE] = "SE",
+        [LS_DIR_S] = "S",
+        [LS_DIR_SW] = "SW",
+        [LS_DIR_W] = "W",
+        [LS_DIR_NW] = "NW",
     },
     .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
     .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -232,6 +232,17 @@ const lang_t lang_cs = {
         [LS_RAIN_MODERATE] = "mírný",
         [LS_RAIN_HEAVY] = "silný",
         [LS_AGO] = "před %s",
+        [LS_FLIGHTS_NEED_ALWAYS] = "Lety jen v synchronizaci Stále",
+        [LS_NO_AIRCRAFT] = "Do %s žádná letadla",
+        [LS_NO_AIRCRAFT_DATA] = "Žádná data o letadlech",
+        [LS_DIR_N] = "S",
+        [LS_DIR_NE] = "SV",
+        [LS_DIR_E] = "V",
+        [LS_DIR_SE] = "JV",
+        [LS_DIR_S] = "J",
+        [LS_DIR_SW] = "JZ",
+        [LS_DIR_W] = "Z",
+        [LS_DIR_NW] = "SZ",
     },
     .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
     .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
```


- [ ] **Step 6: Run the tests and look at the goldens.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `out of 60`, all passing; `test_gfx` 15 tests, `test_ui_flights` 4.

Render the six goldens (`flights flights_100 flights_cs flights_none flights_failed flights_off`) to `/tmp` and `cmp` them with the branch's: no output. In the PNGs: four aircraft at 50 km with TVS7UZ ringed and its route "BRQ Brno → AYT Antalya"; eight at 100 km, where the nearest's label gives way to its neighbours; the Czech direction "JV"; "No aircraft within 50 km"; "No aircraft data (20:40)"; "Flights need sync mode Always on" over the map.

- [ ] **Step 7: The firmware builds.** `tools/idf.sh build`: clean.

- [ ] **Step 8: Commit.**

```bash
git add components/gfx components/ui components/locale test/host
git commit -m "feat(ui): the Flights view (D22, D27)"
```


### Task 11: The weather radar on the device (`fetch`, `radar_fetch`, `sync`, `app_radar`)

**Files:**
- Create: `components/fetch/CMakeLists.txt`, `components/fetch/include/fetch.h`, `components/fetch/fetch.c`
- Create: `components/radar/include/radar_fetch.h`, `components/radar/radar_fetch.c`, `main/app_radar.c`
- Modify: `components/radar/include/radar.h`, `radar_frame.c`, `radar_store.c`, `radar_rainviewer.c`, `components/radar/CMakeLists.txt` (`radar_wanted()`, `radar_store_keep()`, the frame file, `radar_frame_covers()`, `RADAR_RV_STEP_S`)
- Modify: `components/weather/weather_http.c`, `components/weather/include/weather_http.h`, `components/weather/CMakeLists.txt` (on `fetch`)
- Modify: `components/sync/include/sync.h`, `sync.c`, `include/sync_plan.h`, `sync_plan.c`, `components/sync/CMakeLists.txt`
- Modify: `components/locale/include/lang.h`, `lang_en.c`, `lang_cs.c` (the step's name)
- Modify: `main/app_internal.h`, `main/app_sync.c`, `main/app_ui.c`, `main/app_web.c`, `main/app_cmds.c`, `main/CMakeLists.txt`
- Modify: `test/host/test_radar.c`, `test/host/test_sync_plan.c`
- Modify: `AGENTS.md` (§5.2: `fetch/`, the `radar/` line)

**Interfaces:**
- Consumes: Tasks 2–5 and 8–9; M5's sync task and `weather_http_get()`.
- Produces (Tasks 12, 13):
  - `fetch_session_t`, `fetch_get(s, url, buf, size, len, timeout_ms, status)`, `fetch_close(s)`.
  - `radar_wanted(newest, step_s, want, have, have_count, out)`, `radar_store_keep(s, keep)`, `RADAR_FILE_MAGIC`, `RADAR_FILE_VERSION`, `RADAR_FILE_HEADER` 48, `radar_frame_file_size()`, `radar_frame_to_file()`, `radar_frame_from_file()`, `RADAR_RV_STEP_S` 600.
  - `bool radar_frame_covers(const radar_frame_t *f, const map_view_t *v)`; `RADAR_VIEW_W` 400 and `RADAR_VIEW_H` 279, the Radar layout's map, which holds any slot's.
  - `radar_fetch_req_t` (`lat_e4`, `lon_e4`, `zoom_q`, `want`, `have_count`, `have[]`, `now`, `deadline_us`), `radar_fetch_result_t` (`frames[]`, `count`, `detail`), `radar_fetch_mem` (PSRAM), `radar_fetch(req, out)`.
  - `SYNC_STEP_RADAR` ("radar"), `sync_request_t.radar` and `.radar_only`, `sync_report_t.radar` and `.radar_only`; `sync_start()`'s `done` now gets a writable report (the app takes its frames); `sync_radar_next(after, step_s)`, `SYNC_RADAR_DELAY_S` 60.
  - In `main/app_internal.h`: `app_radar_status_t`, `app_radar_request(out)`, `app_radar_apply(res, result, detail)` (the step's `sync_step_result_t` and detail), `app_radar_prepare(preset)`, `app_radar_ui()`, `app_radar_step_s()`, `app_radar_status()`; `app_sync_refreshing()`, `app_sync_running()`.

How it runs:
- **A sync's radar step** (spec §9.3, 10 s of its 45): ČHMÚ when the weather radar's centre lies in its data, by file name from the time, stepping back up to twice on a 404; otherwise RainViewer's index and the view's tiles over one connection. The time is this sync's NTP time when its time step worked, as the app sets the clock only after the sync; else the clock, if it is valid; with neither, the step fails with "no time" (the boot sync of a board that lost its time). Outside sync mode `always` it wants the newest frame; in `always`, the last hour too (12 of ČHMÚ's, 6 of RainViewer's), less those kept.
- **A radar-only refresh** in sync mode `always`, while it holds Wi-Fi: at once when `always` takes Wi-Fi (the hour in one go), then a minute after each 5-minute step (RainViewer: 10). It doesn't join Wi-Fi, shows no sync mark, and stays out of the syncs' history and retries (ruling); 30 s at most. A sync asked for meanwhile (the menu, the web UI) starts when the refresh ends, and shows as running from the moment it is asked (ruling).
- **The frames** move whole from the sync task's report to the app task's store, which keeps the loop's hour in `always` (12 of ČHMÚ's, 6 of RainViewer's) and 1 otherwise. A frame stays while it covers the view, the Radar layout's 400 × 279 map (`radar_frame_covers()`, ruling): ČHMÚ's any view centred inside its data, RainViewer's while it holds the tiles the view needs at its zoom. Frames that no longer cover it go when the centre, zoom or source changes, and so do frames that arrive for a view changed while they came. The newest goes to `/fs/state/radar.bin` (ruling): at once outside `always`, at most every 30 min in it (ruling), as a frame comes every 5 or 10 min there and each write is 56–147 KB. It comes back from the file after a cold boot or a deep sleep, the first time a preset shows the weather radar.
- **`app_radar_prepare(preset)`**, before each render and preview: it opens the map blob for a preset with a map (the Radar and Flights layouts, a `rain.map` slot), checking only its header (Task 3's ruling), and reads the frame's file for the weather radar; any other preset touches neither, so a routine wake doesn't either (ruling). The image grows by the map's 748 KB now that something reads it.
- **The status** (`radar status`, the web UI): a fetch's time, whether it worked and why not ("timeout" when the sync's 45 s ran out before it); a radar step that never ran, as Wi-Fi didn't come up, leaves it as it was.

- [ ] **Step 1: Write the failing tests** for the pure parts: which frames a fetch wants, the frame file, the store's keep, which views a frame covers, the refresh's times.

`test/host/test_radar.c`:

```diff
--- a/test/host/test_radar.c
+++ b/test/host/test_radar.c
@@ -217,6 +217,26 @@ static void test_a_rainviewer_tile_decodes_by_its_colour_table(void)
     free(png);
 }
 
+static void test_a_frame_covers_the_views_it_has_the_rain_for(void)
+{
+    map_view_t v, slot, deeper, paris;
+    map_view_init(&v, 525200, 134050, 6.5, RADAR_VIEW_W, RADAR_VIEW_H); /* Berlin: RainViewer */
+    radar_rv_tiles_t t;
+    radar_rv_tiles(&v, &t);
+    radar_frame_t f;
+    TEST_ASSERT_TRUE(radar_rv_frame_alloc(&f, &t, 1790889000, NULL));
+    TEST_ASSERT_TRUE(radar_frame_covers(&f, &v));
+    map_view_init(&slot, 525200, 134050, 6.5, 196, 120); /* a slot's map: some of the same tiles */
+    TEST_ASSERT_TRUE(radar_frame_covers(&f, &slot));
+    map_view_init(&deeper, 525200, 134050, 7.0, RADAR_VIEW_W, RADAR_VIEW_H); /* zoom 7's tiles */
+    TEST_ASSERT_FALSE(radar_frame_covers(&f, &deeper));
+    map_view_init(&paris, 488566, 23522, 6.5, RADAR_VIEW_W, RADAR_VIEW_H);
+    TEST_ASSERT_FALSE(radar_frame_covers(&f, &paris));
+    radar_frame_free(&f, NULL);
+    radar_frame_t chmu = { .source = RADAR_SOURCE_CHMU };
+    TEST_ASSERT_TRUE(radar_frame_covers(&chmu, &deeper)); /* one grid for every view: its edge shows */
+}
+
 static int s_live;
 
 static void *counted_alloc(size_t n)
@@ -259,6 +279,10 @@ static void test_the_store_keeps_frames_in_order_and_frees_the_rest(void)
     TEST_ASSERT_EQUAL_UINT32(t0, missing[0]); /* newest first */
     TEST_ASSERT_EQUAL_UINT32(t0 - 8 * 300, missing[8]);
 
+    radar_store_keep(&s, 2); /* sync mode always ends: the hour goes */
+    TEST_ASSERT_EQUAL_INT(2, s.count);
+    TEST_ASSERT_EQUAL_INT(2, s_live);
+    TEST_ASSERT_EQUAL_UINT32(t0 + 600, s.frames[0].time);
     radar_frame_t f;
     TEST_ASSERT_TRUE(radar_frame_alloc(&f, 8, 8, &mem));
     f.time = t0 + 1200;
@@ -271,6 +295,59 @@ static void test_the_store_keeps_frames_in_order_and_frees_the_rest(void)
     TEST_ASSERT_NULL(radar_store_newest(&s));
 }
 
+static void test_a_fetch_wants_the_hour_it_lacks(void)
+{
+    const uint32_t t0 = 1790880000;
+    uint32_t out[RADAR_LOOP_FRAMES];
+    TEST_ASSERT_EQUAL_INT(1, radar_wanted(t0, 300, 1, NULL, 0, out)); /* outside sync mode always: the newest */
+    TEST_ASSERT_EQUAL_UINT32(t0, out[0]);
+    const uint32_t have[] = { t0 - 300, t0 - 900, t0 - 6000 }; /* the last one is older than the hour */
+    TEST_ASSERT_EQUAL_INT(10, radar_wanted(t0, 300, RADAR_LOOP_FRAMES, have, 3, out));
+    TEST_ASSERT_EQUAL_UINT32(t0, out[0]); /* newest first */
+    TEST_ASSERT_EQUAL_UINT32(t0 - 600, out[1]);
+    TEST_ASSERT_EQUAL_UINT32(t0 - 3300, out[9]);
+    TEST_ASSERT_EQUAL_INT(0, radar_wanted(t0 - 300, 300, 1, have, 3, out)); /* nothing new */
+    TEST_ASSERT_EQUAL_INT(6, radar_wanted(t0, 600, 6, NULL, 0, out)); /* RainViewer's hour */
+    TEST_ASSERT_EQUAL_UINT32(t0 - 3000, out[5]);
+    TEST_ASSERT_EQUAL_INT(RADAR_LOOP_FRAMES, radar_wanted(t0, 300, 40, NULL, 0, out)); /* never more than kept */
+}
+
+static void test_a_frame_survives_its_file_and_a_damaged_file_is_refused(void)
+{
+    radar_frame_t f, back;
+    TEST_ASSERT_TRUE(radar_frame_alloc(&f, 30, 10, NULL));
+    f.time = 1790880000;
+    f.source = RADAR_SOURCE_RAINVIEWER;
+    f.mx0 = 1254222.15;
+    f.my0 = 6702777.85;
+    f.scale = 1555.7;
+    radar_frame_set(&f, 29, 9, RADAR_HEAVY);
+    radar_frame_set(&f, 3, 4, RADAR_LIGHT);
+    static uint8_t file[512];
+    size_t n = radar_frame_to_file(&f, file, sizeof(file));
+    TEST_ASSERT_EQUAL_size_t(radar_frame_file_size(&f), n);
+    TEST_ASSERT_EQUAL_size_t(RADAR_FILE_HEADER + 75, n); /* 300 pixels at 2 bits */
+    TEST_ASSERT_TRUE(radar_frame_from_file(file, n, NULL, &back));
+    TEST_ASSERT_EQUAL_UINT32(f.time, back.time);
+    TEST_ASSERT_EQUAL_UINT8(RADAR_SOURCE_RAINVIEWER, back.source);
+    TEST_ASSERT_EQUAL_UINT16(30, back.w);
+    TEST_ASSERT_EQUAL_DOUBLE(f.my0, back.my0);
+    TEST_ASSERT_EQUAL_DOUBLE(f.scale, back.scale);
+    TEST_ASSERT_EQUAL_MEMORY(f.levels, back.levels, 75);
+    radar_frame_free(&back, NULL);
+
+    TEST_ASSERT_EQUAL_size_t(0, radar_frame_to_file(&f, file, n - 1)); /* too small: nothing written */
+    n = radar_frame_to_file(&f, file, sizeof(file));
+    TEST_ASSERT_FALSE(radar_frame_from_file(file, n - 1, NULL, &back)); /* cut short */
+    file[RADAR_FILE_HEADER + 10] ^= 0x10;
+    TEST_ASSERT_FALSE(radar_frame_from_file(file, n, NULL, &back)); /* its CRC */
+    file[RADAR_FILE_HEADER + 10] ^= 0x10;
+    file[0] ^= 1;
+    TEST_ASSERT_FALSE(radar_frame_from_file(file, n, NULL, &back)); /* not a frame */
+    TEST_ASSERT_NULL(back.levels);
+    radar_frame_free(&f, NULL);
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -282,6 +359,9 @@ int main(void)
     RUN_TEST(test_the_rainviewer_index_names_its_frames);
     RUN_TEST(test_rainviewer_tiles_cover_the_view);
     RUN_TEST(test_a_rainviewer_tile_decodes_by_its_colour_table);
+    RUN_TEST(test_a_frame_covers_the_views_it_has_the_rain_for);
     RUN_TEST(test_the_store_keeps_frames_in_order_and_frees_the_rest);
+    RUN_TEST(test_a_fetch_wants_the_hour_it_lacks);
+    RUN_TEST(test_a_frame_survives_its_file_and_a_damaged_file_is_refused);
     return UNITY_END();
 }
```


`test/host/test_sync_plan.c`:

```diff
--- a/test/host/test_sync_plan.c
+++ b/test/host/test_sync_plan.c
@@ -325,6 +325,16 @@ static void test_wifi_is_wanted_in_always_mode_outside_quiet_hours(void)
     TEST_ASSERT_FALSE(sync_wifi_wanted(&daily, oct1(12, 0)));
 }
 
+static void test_radar_refreshes_follow_the_frames(void)
+{
+    /* spec §9.3: ČHMÚ every 5 min, RainViewer every 10, a minute after each step for the frame to appear */
+    TEST_ASSERT_EQUAL_INT64(oct1(12, 1), sync_radar_next(oct1(12, 0), 300));
+    TEST_ASSERT_EQUAL_INT64(oct1(12, 6), sync_radar_next(oct1(12, 1), 300)); /* strictly after */
+    TEST_ASSERT_EQUAL_INT64(oct1(12, 6), sync_radar_next(oct1(12, 4) + 59, 300));
+    TEST_ASSERT_EQUAL_INT64(oct1(12, 11), sync_radar_next(oct1(12, 1), 600));
+    TEST_ASSERT_EQUAL_INT64(oct1(12, 1), sync_radar_next(oct1(11, 51) + 1, 600));
+}
+
 static void test_hhmm_text(void)
 {
     TEST_ASSERT_EQUAL_INT(330, sync_parse_hhmm("05:30"));
@@ -462,6 +472,7 @@ int main(void)
     RUN_TEST(test_quiet_hours_stretch_the_expected_interval);
     RUN_TEST(test_the_expected_interval_of_always_and_manual);
     RUN_TEST(test_wifi_is_wanted_in_always_mode_outside_quiet_hours);
+    RUN_TEST(test_radar_refreshes_follow_the_frames);
     RUN_TEST(test_hhmm_text);
     RUN_TEST(test_a_time_in_the_spring_forward_gap_runs_after_it);
     RUN_TEST(test_a_time_in_the_repeated_hour_runs_once);
```


- [ ] **Step 2: Run them to see them fail.**

Run: `cmake --build build-host -- -k 0 2>&1 | grep -o "undeclared [a-z]* '[a-zA-Z_]*'" | sort -u`
Expected: the functions `radar_frame_covers`, `radar_frame_file_size`, `radar_frame_from_file`, `radar_frame_to_file`, `radar_store_keep`, `radar_wanted` and `sync_radar_next`, and the identifiers `RADAR_FILE_HEADER`, `RADAR_VIEW_W` and `RADAR_VIEW_H`.

- [ ] **Step 3: The pure parts.**

`components/radar/include/radar.h`:

```diff
--- a/components/radar/include/radar.h
+++ b/components/radar/include/radar.h
@@ -69,8 +69,11 @@ png_err_t radar_chmu_decode(const uint8_t *png, size_t len, const png_mem_t *mem
 
 #define RADAR_RV_INDEX_URL "https://api.rainviewer.com/public/weather-maps.json"
 #define RADAR_RV_MAX_ZOOM 7
+#define RADAR_RV_STEP_S 600 /* its frames come every 10 min */
 #define RADAR_RV_FRAMES 16
 #define RADAR_RV_TILES_MAX 3 /* a side: at most 3 x 3 tiles a view */
+#define RADAR_VIEW_W 400 /* the Radar layout's map, which holds any slot's: the view a fetch covers */
+#define RADAR_VIEW_H 279
 
 typedef struct {
     uint32_t time;
@@ -100,6 +103,21 @@ bool radar_rv_frame_alloc(radar_frame_t *f, const radar_rv_tiles_t *t, uint32_t
 /* One tile's PNG into the frame at tile (x, y) of the world. */
 png_err_t radar_rv_decode_tile(const uint8_t *png, size_t len, const png_mem_t *mem, const radar_rv_tiles_t *t,
                                int x, int y, radar_frame_t *f);
+/* The frame has the rain for all of the view: ČHMÚ's for any (beyond its grid the edge shows),
+ * RainViewer's when it holds the tiles the view needs, at the view's tile zoom. */
+bool radar_frame_covers(const radar_frame_t *f, const map_view_t *v);
+
+/* A frame as /fs/state/radar.bin keeps it (spec §11.2, D28), so a power-off or a deep sleep keeps
+ * the latest: 48 bytes of header (magic "rfrm", version, source, size, time, georeference, the
+ * levels' length and CRC-32), then the levels. */
+#define RADAR_FILE_MAGIC 0x6d726672u
+#define RADAR_FILE_VERSION 1
+#define RADAR_FILE_HEADER 48
+size_t radar_frame_file_size(const radar_frame_t *f);
+/* The file's bytes; 0 if `size` is too small. */
+size_t radar_frame_to_file(const radar_frame_t *f, uint8_t *out, size_t size);
+/* A frame from them, its levels from `mem`; false, and out->levels NULL, for anything else. */
+bool radar_frame_from_file(const uint8_t *data, size_t len, const png_mem_t *mem, radar_frame_t *out);
 
 /* ---- drawing ---- */
 
@@ -131,3 +149,8 @@ bool radar_store_has(const radar_store_t *s, uint32_t time);
 /* The frame times of the hour before `newest` (in steps of `step_s`) not kept yet, newest first. */
 int radar_store_missing(const radar_store_t *s, uint32_t newest, uint32_t step_s, uint32_t *out, int max);
 void radar_store_clear(radar_store_t *s);
+/* Frees the oldest frames beyond `keep` (clamped to 1..RADAR_LOOP_FRAMES). */
+void radar_store_keep(radar_store_t *s, int keep);
+/* The frame times a fetch wants (spec §11.2): `newest` and the `want - 1` steps before it, newest
+ * first, less those in `have`; `want` is clamped to 1..RADAR_LOOP_FRAMES. Returns how many. */
+int radar_wanted(uint32_t newest, uint32_t step_s, int want, const uint32_t *have, int have_count, uint32_t *out);
```


`components/radar/radar_store.c`:

```diff
--- a/components/radar/radar_store.c
+++ b/components/radar/radar_store.c
@@ -40,6 +40,11 @@ void radar_store_put(radar_store_t *s, radar_frame_t *f, int keep)
     s->frames[at] = *f;
     s->count++;
     f->levels = NULL; /* the store owns it now */
+    radar_store_keep(s, keep);
+}
+
+void radar_store_keep(radar_store_t *s, int keep)
+{
     keep = keep < 1 ? 1 : keep > RADAR_LOOP_FRAMES ? RADAR_LOOP_FRAMES : keep;
     while (s->count > keep) {
         drop(s, 0);
@@ -73,6 +78,23 @@ int radar_store_missing(const radar_store_t *s, uint32_t newest, uint32_t step_s
     return n;
 }
 
+int radar_wanted(uint32_t newest, uint32_t step_s, int want, const uint32_t *have, int have_count, uint32_t *out)
+{
+    want = want < 1 ? 1 : want > RADAR_LOOP_FRAMES ? RADAR_LOOP_FRAMES : want;
+    int n = 0;
+    for (int k = 0; k < want && (uint64_t)k * step_s <= newest; k++) {
+        uint32_t t = newest - (uint32_t)k * step_s;
+        bool kept = false;
+        for (int i = 0; i < have_count && !kept; i++) {
+            kept = have[i] == t;
+        }
+        if (!kept) {
+            out[n++] = t;
+        }
+    }
+    return n;
+}
+
 void radar_store_clear(radar_store_t *s)
 {
     while (s->count > 0) {
```


`components/radar/radar_frame.c`:

```diff
--- a/components/radar/radar_frame.c
+++ b/components/radar/radar_frame.c
@@ -3,6 +3,7 @@
 #include <string.h>
 
 #include "radar.h"
+#include "util_crc32.h"
 
 #define PI 3.14159265358979323846
 #define VIEW_W_MAX 1024 /* screen columns a render maps at most */
@@ -12,6 +13,70 @@ static size_t grid_bytes(uint16_t w, uint16_t h)
     return ((size_t)w * h + 3) / 4;
 }
 
+/* Little-endian fields at fixed offsets: the file reads the same on the host and the chip. */
+static void put32(uint8_t *p, uint32_t v)
+{
+    for (int i = 0; i < 4; i++) {
+        p[i] = (uint8_t)(v >> (8 * i));
+    }
+}
+
+static uint32_t get32(const uint8_t *p)
+{
+    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
+}
+
+size_t radar_frame_file_size(const radar_frame_t *f)
+{
+    return RADAR_FILE_HEADER + grid_bytes(f->w, f->h);
+}
+
+size_t radar_frame_to_file(const radar_frame_t *f, uint8_t *out, size_t size)
+{
+    size_t n = radar_frame_file_size(f), levels = n - RADAR_FILE_HEADER;
+    if (f->levels == NULL || size < n) {
+        return 0;
+    }
+    memset(out, 0, RADAR_FILE_HEADER);
+    put32(out, RADAR_FILE_MAGIC);
+    out[4] = RADAR_FILE_VERSION;
+    out[6] = f->source;
+    out[8] = (uint8_t)f->w;
+    out[9] = (uint8_t)(f->w >> 8);
+    out[10] = (uint8_t)f->h;
+    out[11] = (uint8_t)(f->h >> 8);
+    put32(out + 12, f->time);
+    memcpy(out + 16, &f->mx0, 8); /* IEEE 754 doubles, little-endian on both */
+    memcpy(out + 24, &f->my0, 8);
+    memcpy(out + 32, &f->scale, 8);
+    put32(out + 40, (uint32_t)levels);
+    put32(out + 44, util_crc32(0, f->levels, levels));
+    memcpy(out + RADAR_FILE_HEADER, f->levels, levels);
+    return n;
+}
+
+bool radar_frame_from_file(const uint8_t *data, size_t len, const png_mem_t *mem, radar_frame_t *out)
+{
+    memset(out, 0, sizeof(*out));
+    if (len < RADAR_FILE_HEADER || get32(data) != RADAR_FILE_MAGIC || data[4] != RADAR_FILE_VERSION) {
+        return false;
+    }
+    uint16_t w = (uint16_t)(data[8] | data[9] << 8), h = (uint16_t)(data[10] | data[11] << 8);
+    size_t levels = get32(data + 40);
+    if (levels != grid_bytes(w, h) || len != RADAR_FILE_HEADER + levels ||
+        get32(data + 44) != util_crc32(0, data + RADAR_FILE_HEADER, levels) ||
+        !radar_frame_alloc(out, w, h, mem)) {
+        return false;
+    }
+    out->source = data[6];
+    out->time = get32(data + 12);
+    memcpy(&out->mx0, data + 16, 8);
+    memcpy(&out->my0, data + 24, 8);
+    memcpy(&out->scale, data + 32, 8);
+    memcpy(out->levels, data + RADAR_FILE_HEADER, levels);
+    return true;
+}
+
 bool radar_frame_alloc(radar_frame_t *f, uint16_t w, uint16_t h, const png_mem_t *mem)
 {
     memset(f, 0, sizeof(*f));
```


`components/radar/radar_rainviewer.c`:

```diff
--- a/components/radar/radar_rainviewer.c
+++ b/components/radar/radar_rainviewer.c
@@ -111,6 +111,21 @@ bool radar_rv_frame_alloc(radar_frame_t *f, const radar_rv_tiles_t *t, uint32_t
     return true;
 }
 
+bool radar_frame_covers(const radar_frame_t *f, const map_view_t *v)
+{
+    if (f->source == RADAR_SOURCE_CHMU) {
+        return true;
+    }
+    radar_rv_tiles_t t;
+    radar_rv_tiles(v, &t);
+    double tiles = pow(2.0, t.z), world = 2 * PI * MAP_EARTH_R;
+    if (fabs(f->scale * MAP_TILE_PX * tiles - world) > 1.0) {
+        return false; /* another zoom's tiles */
+    }
+    int x0 = (int)lround((f->mx0 / world + 0.5) * tiles), y0 = (int)lround((0.5 - f->my0 / world) * tiles);
+    return t.x0 >= x0 && t.y0 >= y0 && t.x0 + t.nx <= x0 + f->w / MAP_TILE_PX && t.y0 + t.ny <= y0 + f->h / MAP_TILE_PX;
+}
+
 typedef struct {
     radar_frame_t *f;
     int ox, oy; /* the tile's top-left in the frame's grid */
```


`components/sync/include/sync_plan.h`:

```diff
--- a/components/sync/include/sync_plan.h
+++ b/components/sync/include/sync_plan.h
@@ -76,5 +76,9 @@ void sync_history_record(sync_history_t *h, sync_due_t due, bool ok, time_t ende
 uint32_t sync_expected_interval_s(const sync_schedule_t *s, time_t now);
 /* `always` mode wants Wi-Fi at `t`: on unless quiet hours; false in the other modes. */
 bool sync_wifi_wanted(const sync_schedule_t *s, time_t t);
+#define SYNC_RADAR_DELAY_S 60 /* a radar-only refresh waits this long after a frame's step */
+/* The next radar-only refresh strictly after `after` (spec §9.3), in sync mode `always`: a minute
+ * past each `step_s` of the clock, 300 s for ČHMÚ and 600 for RainViewer. */
+time_t sync_radar_next(time_t after, uint32_t step_s);
 /* Settings text: "HH:MM" <-> minutes after midnight; -1 for anything else. */
 int sync_parse_hhmm(const char *text);
```


`components/sync/sync_plan.c`:

```diff
--- a/components/sync/sync_plan.c
+++ b/components/sync/sync_plan.c
@@ -204,3 +204,9 @@ int sync_parse_hhmm(const char *text)
     int h = (text[0] - '0') * 10 + (text[1] - '0'), m = (text[3] - '0') * 10 + (text[4] - '0');
     return text[5] == '\0' && h < 24 && m < 60 ? h * 60 + m : -1;
 }
+
+time_t sync_radar_next(time_t after, uint32_t step_s)
+{
+    time_t t = after - SYNC_RADAR_DELAY_S;
+    return t - t % (time_t)step_s + (time_t)step_s + SYNC_RADAR_DELAY_S;
+}
```


Run: `cmake --build build-host && ./build-host/test_radar && ./build-host/test_sync_plan`
Expected: `12 Tests 0 Failures 0 Ignored`; `test_sync_plan` all passing (one test more).

- [ ] **Step 4: `fetch`, and the weather on it.** A session keeps its client; another host's URL closes the old connection inside `esp_http_client_set_url()`. After each reply, a connection the server won't keep is closed (`esp_http_client_is_persistent_connection()`: ČHMÚ answers every request with `Connection: close`), so the next GET connects at once. A request that fails closes the session, and one without a reply on a kept connection is tried once more on a new one: `esp_http_client_fetch_headers()` then returns -1 and leaves the status at -1, which `fetch_get()` reports as 0, no reply.

`components/fetch/include/fetch.h`:

```c
#pragma once

#include <stddef.h>

#include "esp_err.h"

/*
 * HTTPS GETs with the certificate bundle and a "reflbo/<version>" User-Agent (spec §11): the sync's
 * requests, the place search, the radar's tiles and the flight radar's polls. A session keeps its
 * connection for the next GET to the same host (spec §11.2, §11.3). Device only.
 */

typedef struct {
    void *client; /* esp_http_client_handle_t; NULL until the first GET */
} fetch_session_t;

/* `url` into `buf`: the body, NUL-terminated after `*len` bytes; a body of `size - 1` bytes or more
 * is ESP_ERR_INVALID_SIZE. `*status` is the HTTP status, 0 when no reply came; ESP_FAIL for a status
 * other than 200. A connection the server closed meanwhile is opened again, once. */
esp_err_t fetch_get(fetch_session_t *s, const char *url, void *buf, size_t size, size_t *len, int timeout_ms,
                    int *status);
void fetch_close(fetch_session_t *s);
```


`components/fetch/fetch.c`:

```c
#include "fetch.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "fetch";

void fetch_close(fetch_session_t *s)
{
    if (s->client != NULL) {
        esp_http_client_cleanup(s->client);
        s->client = NULL;
    }
}

/* One request on the session's client, opened here if it has none. */
static esp_err_t get_once(fetch_session_t *s, const char *url, char *buf, size_t size, size_t *len, int timeout_ms,
                          int *status)
{
    *len = 0;
    *status = 0;
    esp_http_client_handle_t client = s->client;
    esp_err_t err = ESP_OK;
    if (client == NULL) {
        char agent[48];
        snprintf(agent, sizeof(agent), "reflbo/%s", esp_app_get_description()->version);
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = timeout_ms,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .user_agent = agent,
            .buffer_size = 2048,
        };
        client = esp_http_client_init(&cfg);
        ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_NO_MEM, TAG, "client");
        s->client = client;
    } else {
        err = esp_http_client_set_url(client, url); /* another host closes the old connection */
        esp_http_client_set_timeout_ms(client, timeout_ms);
    }
    if (err == ESP_OK) {
        err = esp_http_client_open(client, 0);
    }
    if (err == ESP_OK) {
        int64_t length = esp_http_client_fetch_headers(client);
        if (length < 0) {
            err = ESP_FAIL; /* no reply: a connection the server closed meanwhile reads as status -1 */
        } else {
            *status = esp_http_client_get_status_code(client);
            if (length >= (int64_t)size) {
                err = ESP_ERR_INVALID_SIZE;
            }
        }
        while (err == ESP_OK) {
            if (*len + 1 >= size) {
                err = ESP_ERR_INVALID_SIZE; /* chunked and too long */
                break;
            }
            int n = esp_http_client_read(client, buf + *len, (int)(size - 1 - *len));
            if (n < 0) {
                err = ESP_FAIL;
            } else if (n == 0) {
                break; /* the end, or the timeout: is_complete tells them apart */
            } else {
                *len += (size_t)n;
            }
        }
        if (err == ESP_OK && !esp_http_client_is_complete_data_received(client)) {
            err = ESP_ERR_TIMEOUT;
        }
    }
    buf[*len] = '\0';
    if (err != ESP_OK || !esp_http_client_is_persistent_connection(client)) {
        fetch_close(s); /* a connection in an unknown state, or one the server closes (ČHMÚ's), isn't reused */
    }
    if (err == ESP_OK && *status != 200) {
        err = ESP_FAIL;
    }
    return err;
}

esp_err_t fetch_get(fetch_session_t *s, const char *url, void *buf, size_t size, size_t *len, int timeout_ms,
                    int *status)
{
    bool reused = s->client != NULL;
    esp_err_t err = get_once(s, url, buf, size, len, timeout_ms, status);
    if (err != ESP_OK && reused && *status <= 0) { /* the server closed the kept connection: once more */
        err = get_once(s, url, buf, size, len, timeout_ms, status);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %.48s...: %s, HTTP %d, %u bytes", url, esp_err_to_name(err), *status, (unsigned)*len);
    }
    return err;
}
```


`components/fetch/CMakeLists.txt`:

```cmake
# HTTPS GETs (spec §11): one at a time, or several over one kept-alive connection. Device only.
idf_component_register(SRCS "fetch.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_http_client mbedtls esp_app_format log)
```


`components/weather/weather_http.c`:

```c
#include "weather_http.h"

#include "fetch.h"

esp_err_t weather_http_get(const char *url, char *buf, size_t size, size_t *len, int timeout_ms, int *status)
{
    fetch_session_t s = { 0 };
    esp_err_t err = fetch_get(&s, url, buf, size, len, timeout_ms, status);
    fetch_close(&s);
    return err;
}
```


`components/weather/include/weather_http.h`:

```diff
--- a/components/weather/include/weather_http.h
+++ b/components/weather/include/weather_http.h
@@ -4,7 +4,7 @@
 
 #include "esp_err.h"
 
-/* An HTTPS GET with the certificate bundle (spec §11), for the sync task and the place search.
+/* One HTTPS GET through `fetch` (spec §11), for the sync task and the place search.
  * `buf` gets the body, NUL-terminated; a body larger than `size - 1` is ESP_ERR_INVALID_SIZE.
  * `*status` is the HTTP status (0 when no reply came). ESP_FAIL for a status other than 200. */
 esp_err_t weather_http_get(const char *url, char *buf, size_t size, size_t *len, int timeout_ms, int *status);
```


`components/weather/CMakeLists.txt`:

```diff
--- a/components/weather/CMakeLists.txt
+++ b/components/weather/CMakeLists.txt
@@ -2,5 +2,5 @@
 # also build on the host; weather_http.c fetches on the device.
 idf_component_register(SRCS "weather_url.c" "weather_parse.c" "weather_levels.c" "weather_http.c"
                        INCLUDE_DIRS "include"
-                       REQUIRES datastore esp_http_client
-                       PRIV_REQUIRES json util mbedtls esp_app_format)
+                       REQUIRES datastore esp_common
+                       PRIV_REQUIRES json util fetch)
```


- [ ] **Step 5: The radar's fetch.**

`components/radar/include/radar_fetch.h`:

```c
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "radar.h"

/*
 * The weather radar's frames over HTTPS (spec §11.2), fetched on the sync task: ČHMÚ's composite
 * where it covers the view's centre, else RainViewer's index and the view's tiles. Device only.
 */

#define RADAR_FETCH_DETAIL_LEN 24

typedef struct {
    int32_t lat_e4, lon_e4; /* radar.weather's centre */
    uint8_t zoom_q;         /* and its zoom, in quarters */
    uint8_t want;           /* the frames kept: 1, or in sync mode `always` the hour's (ČHMÚ 12, RainViewer 6) */
    uint8_t have_count;
    uint32_t have[RADAR_LOOP_FRAMES]; /* the frame times the app keeps, not fetched again */
    uint32_t now;                     /* UTC seconds, ČHMÚ's file names; 0 while the clock is unknown */
    int64_t deadline_us;              /* esp_timer_get_time() by which it stops */
} radar_fetch_req_t;

typedef struct {
    radar_frame_t frames[RADAR_LOOP_FRAMES]; /* in PSRAM, from radar_fetch_mem: the taker frees them */
    uint8_t count;
    char detail[RADAR_FETCH_DETAIL_LEN]; /* why it failed: "HTTP 503", "timeout", "png: bad crc" */
} radar_fetch_result_t;

extern const png_mem_t radar_fetch_mem; /* PSRAM */

/* The newest frame and the older ones `want` asks for that `have` lacks. ESP_OK with what came,
 * maybe nothing new; an error, with `detail`, when the newest didn't come. */
esp_err_t radar_fetch(const radar_fetch_req_t *req, radar_fetch_result_t *out);
```


`components/radar/radar_fetch.c`:

```c
#include "radar_fetch.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fetch.h"

static const char *TAG = "radar";

#define GET_TIMEOUT_MS 10000
#define INDEX_MAX 4096           /* weather-maps.json is about 2 KB */
#define CHMU_TRIES 3             /* the newest step, then up to two back on a 404 (spec §11.2) */
#define RV_HOUR (3600 / RADAR_RV_STEP_S) /* the loop's hour of RainViewer's frames */

EXT_RAM_BSS_ATTR static uint8_t s_png[PNG_MAX_BYTES + 1];
EXT_RAM_BSS_ATTR static char s_index[INDEX_MAX];

static void *psram_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
}

const png_mem_t radar_fetch_mem = { psram_alloc, heap_caps_free };

/* What is left before the deadline, at most GET_TIMEOUT_MS; 0 when under a second. */
static int budget_ms(const radar_fetch_req_t *req)
{
    int64_t left = (req->deadline_us - esp_timer_get_time()) / 1000;
    return left < 1000 ? 0 : left > GET_TIMEOUT_MS ? GET_TIMEOUT_MS : (int)left;
}

static esp_err_t get(fetch_session_t *s, const radar_fetch_req_t *req, const char *url, void *buf, size_t size,
                     size_t *len, int *status, char *detail)
{
    *len = 0;
    *status = 0;
    int budget = budget_ms(req);
    if (budget == 0) {
        snprintf(detail, RADAR_FETCH_DETAIL_LEN, "timeout");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = fetch_get(s, url, buf, size, len, budget, status);
    if (err != ESP_OK) {
        if (*status != 0 && *status != 200) {
            snprintf(detail, RADAR_FETCH_DETAIL_LEN, "HTTP %d", *status);
        } else {
            snprintf(detail, RADAR_FETCH_DETAIL_LEN, "%s", err == ESP_ERR_TIMEOUT ? "timeout" : esp_err_to_name(err));
        }
    }
    return err;
}

static void keep(radar_fetch_result_t *out, radar_frame_t *f)
{
    out->frames[out->count++] = *f;
    f->levels = NULL;
}

/* One ČHMÚ frame by its time; a 404 is ESP_ERR_NOT_FOUND. */
static esp_err_t chmu_frame(fetch_session_t *s, const radar_fetch_req_t *req, uint32_t t, radar_fetch_result_t *out)
{
    char name[64], url[160];
    radar_chmu_name((time_t)t, name, sizeof(name));
    snprintf(url, sizeof(url), "%s%s", RADAR_CHMU_URL, name);
    size_t len;
    int status;
    esp_err_t err = get(s, req, url, s_png, sizeof(s_png), &len, &status, out->detail);
    if (status == 404) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }
    radar_frame_t f;
    png_err_t perr = radar_chmu_decode(s_png, len, &radar_fetch_mem, t, &f);
    if (perr != PNG_OK) {
        snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "png: %s", png_err_name(perr));
        return ESP_ERR_INVALID_RESPONSE;
    }
    keep(out, &f);
    return ESP_OK;
}

static esp_err_t fetch_chmu(fetch_session_t *s, const radar_fetch_req_t *req, radar_fetch_result_t *out)
{
    if (req->now == 0) {
        snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "no time"); /* its files are named by the time */
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t newest = (uint32_t)radar_chmu_newest((time_t)req->now);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < CHMU_TRIES; i++) {
        bool kept = false;
        for (int k = 0; k < req->have_count && !kept; k++) {
            kept = req->have[k] == newest;
        }
        err = kept ? ESP_OK : chmu_frame(s, req, newest, out); /* the newest is here already: done */
        if (err != ESP_ERR_NOT_FOUND) {
            break;
        }
        newest -= RADAR_CHMU_STEP_S; /* not there yet: a step back */
    }
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) {
            snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "HTTP 404");
        }
        return err;
    }
    uint32_t times[RADAR_LOOP_FRAMES];
    int n = radar_wanted(newest, RADAR_CHMU_STEP_S, req->want, req->have, req->have_count, times);
    for (int i = 0; i < n && budget_ms(req) > 0; i++) { /* the hour before it, newest first */
        bool fetched = false;
        for (int k = 0; k < out->count && !fetched; k++) {
            fetched = out->frames[k].time == times[i];
        }
        if (!fetched && chmu_frame(s, req, times[i], out) != ESP_OK) {
            ESP_LOGW(TAG, "frame %lu: %s", (unsigned long)times[i], out->detail);
        }
    }
    out->detail[0] = '\0';
    return ESP_OK;
}

static esp_err_t rv_frame(fetch_session_t *s, const radar_fetch_req_t *req, const radar_rv_index_t *idx,
                          const radar_rv_frame_t *rf, const radar_rv_tiles_t *t, radar_fetch_result_t *out)
{
    radar_frame_t f;
    if (!radar_rv_frame_alloc(&f, t, rf->time, &radar_fetch_mem)) {
        snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "no memory");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = ESP_OK;
    for (int y = t->y0; y < t->y0 + t->ny && err == ESP_OK; y++) {
        for (int x = t->x0; x < t->x0 + t->nx && err == ESP_OK; x++) { /* one connection for them all */
            char url[192];
            radar_rv_tile_url(url, sizeof(url), idx->host, rf->path, t->z, x, y);
            size_t len;
            int status;
            err = get(s, req, url, s_png, sizeof(s_png), &len, &status, out->detail);
            png_err_t perr = err == ESP_OK ? radar_rv_decode_tile(s_png, len, &radar_fetch_mem, t, x, y, &f) : PNG_OK;
            if (perr != PNG_OK) {
                snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "png: %s", png_err_name(perr));
                err = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    if (err != ESP_OK) {
        radar_frame_free(&f, &radar_fetch_mem);
        return err;
    }
    keep(out, &f);
    return ESP_OK;
}

static esp_err_t fetch_rainviewer(fetch_session_t *s, const radar_fetch_req_t *req, radar_fetch_result_t *out)
{
    size_t len;
    int status;
    esp_err_t err = get(s, req, RADAR_RV_INDEX_URL, s_index, sizeof(s_index), &len, &status, out->detail);
    if (err != ESP_OK) {
        return err;
    }
    static radar_rv_index_t idx;
    if (!radar_rv_parse_index(s_index, len, &idx)) {
        snprintf(out->detail, RADAR_FETCH_DETAIL_LEN, "bad index");
        return ESP_ERR_INVALID_RESPONSE;
    }
    map_view_t v;
    map_view_init(&v, req->lat_e4, req->lon_e4, req->zoom_q / 4.0, RADAR_VIEW_W, RADAR_VIEW_H);
    radar_rv_tiles_t t;
    radar_rv_tiles(&v, &t);
    const radar_rv_frame_t *newest = &idx.frames[idx.count - 1];
    uint32_t times[RADAR_LOOP_FRAMES];
    int want = req->want > 1 ? RV_HOUR : 1; /* the hour holds 6 of its frames */
    int n = radar_wanted(newest->time, RADAR_RV_STEP_S, want, req->have, req->have_count, times);
    for (int i = 0; i < n; i++) { /* newest first; the index's times sit on the 10-minute steps */
        for (int k = idx.count - 1; k >= 0; k--) {
            if (idx.frames[k].time != times[i]) {
                continue;
            }
            err = rv_frame(s, req, &idx, &idx.frames[k], &t, out);
            if (err != ESP_OK && i == 0) {
                return err; /* not even the newest */
            }
            break;
        }
        if (budget_ms(req) == 0) {
            break;
        }
    }
    out->detail[0] = '\0';
    return ESP_OK;
}

esp_err_t radar_fetch(const radar_fetch_req_t *req, radar_fetch_result_t *out)
{
    memset(out, 0, sizeof(*out));
    fetch_session_t s = { 0 };
    bool chmu = radar_chmu_covers(req->lat_e4 / 1e4, req->lon_e4 / 1e4);
    esp_err_t err = chmu ? fetch_chmu(&s, req, out) : fetch_rainviewer(&s, req, out);
    fetch_close(&s);
    ESP_LOGI(TAG, "%s: %u new frame%s%s%s", chmu ? "ChMU" : "RainViewer", out->count, out->count == 1 ? "" : "s",
             err == ESP_OK ? "" : ", ", out->detail);
    return err;
}
```


`components/radar/CMakeLists.txt`:

```diff
--- a/components/radar/CMakeLists.txt
+++ b/components/radar/CMakeLists.txt
@@ -1,7 +1,7 @@
 # The weather radar (spec §11.2): ČHMÚ's composite and RainViewer's tiles decoded into rain levels,
 # their store, and their drawing into any map view. These sources are pure C and also build on the
-# host; the sync task fetches the images.
-idf_component_register(SRCS "radar_frame.c" "radar_chmu.c" "radar_rainviewer.c" "radar_store.c"
+# host; radar_fetch.c fetches the images on the sync task.
+idf_component_register(SRCS "radar_frame.c" "radar_chmu.c" "radar_rainviewer.c" "radar_store.c" "radar_fetch.c"
                        INCLUDE_DIRS "include"
-                       REQUIRES gfx map png
-                       PRIV_REQUIRES json util)
+                       REQUIRES gfx map png esp_common
+                       PRIV_REQUIRES json util fetch esp_timer heap log)
```


- [ ] **Step 6: The sync's step and the refresh.**

`components/sync/include/sync.h`:

```diff
--- a/components/sync/include/sync.h
+++ b/components/sync/include/sync.h
@@ -5,13 +5,15 @@
 
 #include "datastore.h"
 #include "esp_err.h"
+#include "radar_fetch.h"
 #include "settings.h"
 
 /*
- * The sync (spec §9.3): Wi-Fi, the time, the weather and the air quality, on a task of its own. It
- * only fetches: the app task applies the report, as it owns the clock, the RTC and the datastore
- * (spec §3.2). Wi-Fi stays on afterwards; the app turns it off unless config mode or sync mode
- * `always` keeps it.
+ * The sync (spec §9.3): Wi-Fi, the time, the weather, the air quality and the weather radar, on a
+ * task of its own; in sync mode `always` also a radar-only refresh. It only fetches: the app task
+ * applies the report, as it owns the clock, the RTC, the datastore and the radar's frames (spec
+ * §3.2). Wi-Fi stays on afterwards; the app turns it off unless config mode or sync mode `always`
+ * keeps it.
  */
 
 typedef enum {
@@ -19,6 +21,7 @@ typedef enum {
     SYNC_STEP_TIME,
     SYNC_STEP_WEATHER,
     SYNC_STEP_AIR,
+    SYNC_STEP_RADAR, /* M6 (spec §11.2) */
     SYNC_STEP_COUNT,
 } sync_step_t;
 
@@ -33,6 +36,8 @@ typedef enum {
 typedef struct {
     int32_t lat_e4, lon_e4;
     char ntp[SETTINGS_NTP_MAX][SETTINGS_HOST_LEN];
+    radar_fetch_req_t radar; /* its deadline is the sync's to set */
+    bool radar_only;         /* sync mode `always`'s radar refresh: Wi-Fi up already, the radar alone */
 } sync_request_t;
 
 typedef struct {
@@ -43,12 +48,15 @@ typedef struct {
     int64_t ntp_delay_us;
     ds_weather_t weather; /* SYNC_STEP_WEATHER; `fetched` is the app's to set */
     ds_air_t air;         /* SYNC_STEP_AIR */
+    radar_fetch_result_t radar; /* SYNC_STEP_RADAR: its frames are the app's to take or free */
+    bool radar_only;
 } sync_report_t;
 
 /* Starts a sync; `done` runs on the sync task when it ends, and must hand the report to the app task
- * without blocking (it stays valid until the next sync starts). ESP_ERR_INVALID_STATE while one runs. */
-esp_err_t sync_start(const sync_request_t *req, void (*done)(const sync_report_t *report));
+ * without blocking (it stays valid until the next sync starts, and the app takes its radar frames).
+ * ESP_ERR_INVALID_STATE while one runs. */
+esp_err_t sync_start(const sync_request_t *req, void (*done)(sync_report_t *report));
 bool sync_running(void);
 /* The step running now, for the progress the web UI shows; SYNC_STEP_COUNT when none runs. */
 sync_step_t sync_step(void);
-const char *sync_step_name(sync_step_t step); /* "wifi", "time", "weather", "air" */
+const char *sync_step_name(sync_step_t step); /* "wifi", "time", "weather", "air", "radar" */
```


`components/sync/sync.c`:

```diff
--- a/components/sync/sync.c
+++ b/components/sync/sync.c
@@ -25,12 +25,14 @@ static const char *TAG = "sync";
 #define NTP_TIMEOUT_MS 5000 /* spec §9.3 */
 #define HTTP_TIMEOUT_MS 10000
 #define JOIN_MAX_MS 25000 /* three 8 s attempts; the rest of the 45 s is the steps' (spec §9.3) */
-#define BODY_MAX (12 * 1024) /* the largest reply, air quality, is about 4 KB */
+#define BODY_MAX (12 * 1024) /* the largest reply, the forecast, is about 5 KB */
+#define RADAR_STEP_MS 10000 /* spec §9.3 */
+#define REFRESH_MAX_MS 30000 /* a radar-only refresh, the last hour's 12 frames at most (D28) */
 
 static volatile bool s_running;
 static volatile uint8_t s_step = SYNC_STEP_COUNT;
 static sync_request_t s_req;
-static void (*s_done)(const sync_report_t *report);
+static void (*s_done)(sync_report_t *report);
 static sync_report_t s_report;
 static int64_t s_deadline_us; /* esp_timer: SYNC_RADIO_MAX_MS after the start */
 EXT_RAM_BSS_ATTR static char s_body[BODY_MAX];
@@ -152,14 +154,50 @@ static void step_air(void)
     }
 }
 
+static void step_radar(int max_ms)
+{
+    int budget = sync_budget_ms(esp_timer_get_time(), s_deadline_us, max_ms);
+    if (budget == 0) {
+        failed(SYNC_STEP_RADAR, "timeout");
+        return;
+    }
+    radar_fetch_req_t req = s_req.radar;
+    req.deadline_us = esp_timer_get_time() + (int64_t)budget * 1000;
+    if (s_report.result[SYNC_STEP_TIME] == SYNC_STEP_OK) { /* the app sets the clock only after the sync */
+        req.now = (uint32_t)((s_report.ntp_utc_us + esp_timer_get_time() - s_report.ntp_mono_us) / 1000000);
+    }
+    if (radar_fetch(&req, &s_report.radar) == ESP_OK) {
+        s_report.result[SYNC_STEP_RADAR] = SYNC_STEP_OK;
+    } else {
+        failed(SYNC_STEP_RADAR, s_report.radar.detail);
+    }
+}
+
+/* Sync mode `always`: the radar alone, on the network Wi-Fi is on already (D23: never joining). */
+static void refresh_task(int64_t start)
+{
+    s_deadline_us = start + (int64_t)REFRESH_MAX_MS * 1000;
+    netmgr_status_t ns;
+    netmgr_status(&ns);
+    if (ns.state != NETMGR_STATION || ns.ip[0] == '\0') {
+        failed(SYNC_STEP_WIFI, "not joined");
+        return;
+    }
+    s_report.result[SYNC_STEP_WIFI] = SYNC_STEP_OK;
+    s_step = SYNC_STEP_RADAR;
+    step_radar(REFRESH_MAX_MS);
+}
+
 static void sync_task(void *arg)
 {
     (void)arg;
     int64_t start = esp_timer_get_time();
     s_deadline_us = start + (int64_t)SYNC_RADIO_MAX_MS * 1000;
     s_step = SYNC_STEP_WIFI;
-    esp_err_t err = netmgr_join(JOIN_MAX_MS);
-    if (err == ESP_OK) {
+    esp_err_t err = s_req.radar_only ? ESP_OK : netmgr_join(JOIN_MAX_MS);
+    if (s_req.radar_only) {
+        refresh_task(start);
+    } else if (err == ESP_OK) {
         s_report.result[SYNC_STEP_WIFI] = SYNC_STEP_OK;
         s_step = SYNC_STEP_TIME; /* the steps are independent (spec §9.3): one failing skips nothing */
         step_time();
@@ -167,6 +205,8 @@ static void sync_task(void *arg)
         step_weather();
         s_step = SYNC_STEP_AIR;
         step_air();
+        s_step = SYNC_STEP_RADAR;
+        step_radar(RADAR_STEP_MS);
     } else {
         failed(SYNC_STEP_WIFI, err == ESP_ERR_NOT_FOUND       ? "no network saved"
                                : err == ESP_ERR_INVALID_STATE ? "Wi-Fi busy"
@@ -179,7 +219,7 @@ static void sync_task(void *arg)
     vTaskDelete(NULL);
 }
 
-esp_err_t sync_start(const sync_request_t *req, void (*done)(const sync_report_t *report))
+esp_err_t sync_start(const sync_request_t *req, void (*done)(sync_report_t *report))
 {
     if (s_running) {
         return ESP_ERR_INVALID_STATE;
@@ -187,7 +227,8 @@ esp_err_t sync_start(const sync_request_t *req, void (*done)(const sync_report_t
     s_running = true;
     s_req = *req;
     s_done = done;
-    memset(&s_report, 0, sizeof(s_report));
+    memset(&s_report, 0, sizeof(s_report)); /* the last report's frames were the app's */
+    s_report.radar_only = req->radar_only;
     BaseType_t ok = xTaskCreatePinnedToCore(sync_task, "sync", TASK_STACK, NULL, TASK_PRIORITY, NULL, 0);
     if (ok != pdPASS) {
         s_running = false;
@@ -208,6 +249,6 @@ sync_step_t sync_step(void)
 
 const char *sync_step_name(sync_step_t step)
 {
-    static const char *const k_names[SYNC_STEP_COUNT] = { "wifi", "time", "weather", "air" };
+    static const char *const k_names[SYNC_STEP_COUNT] = { "wifi", "time", "weather", "air", "radar" };
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
-                       REQUIRES scheduler datastore storage esp_common
+                       REQUIRES scheduler datastore storage esp_common radar
                        PRIV_REQUIRES netmgr weather lwip esp_timer)
```


`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -161,6 +161,7 @@ typedef enum {
     LS_SYNC_STEP_TIME,
     LS_SYNC_STEP_WEATHER,
     LS_SYNC_STEP_AIR,
+    LS_SYNC_STEP_RADAR,
     LS_SYNC_NEVER,
     LS_SYNC_RUNNING,
     LS_T_SYNC_STARTED,
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -169,6 +169,7 @@ const lang_t lang_en = {
         [LS_SYNC_STEP_TIME] = "Time",
         [LS_SYNC_STEP_WEATHER] = "Weather",
         [LS_SYNC_STEP_AIR] = "Air quality",
+        [LS_SYNC_STEP_RADAR] = "Radar",
         [LS_SYNC_NEVER] = "Never",
         [LS_SYNC_RUNNING] = "Running",
         [LS_T_SYNC_STARTED] = "Syncing…",
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -209,6 +209,7 @@ const lang_t lang_cs = {
         [LS_SYNC_STEP_TIME] = "Čas",
         [LS_SYNC_STEP_WEATHER] = "Počasí",
         [LS_SYNC_STEP_AIR] = "Ovzduší",
+        [LS_SYNC_STEP_RADAR] = "Radar",
         [LS_SYNC_NEVER] = "Nikdy",
         [LS_SYNC_RUNNING] = "Probíhá",
         [LS_T_SYNC_STARTED] = "Synchronizuji…",
```


- [ ] **Step 7: The app's side.**

`main/app_radar.c`:

```c
#include <stdio.h>
#include <string.h>

#include "app_internal.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "map_data.h"
#include "storage.h"
#include "timekeeping.h"

/* The weather radar on the device (spec §11.2): the frames kept in PSRAM, the newest also in
 * /fs/state/radar.bin, and what the views draw. It belongs to the app task; the sync task fetches. */

static const char *TAG = "app_radar";

#define FRAME_PATH "/fs/state/radar.bin"
#define FRAME_FILE_MAX (RADAR_FILE_HEADER + 3 * 256 * 3 * 256 / 4) /* RainViewer's 3 x 3 tiles */
#define SAVE_EVERY_S 1800 /* sync mode `always`: the file follows its new frames at most this often */

extern const uint8_t k_map_start[] asm("_binary_map_bin_start");
extern const uint8_t k_map_end[] asm("_binary_map_bin_end");

static radar_store_t s_store;
static bool s_ready;
static bool s_loaded;       /* the file was read since the boot or the wake */
static uint32_t s_saved_at; /* the time of the frame in the file; 0 = none known */
static map_data_t s_map;
static int s_map_state; /* 0 not opened yet, 1 open, -1 broken */
static app_radar_status_t s_status;
static ui_radar_t s_ui;

static void ready(void)
{
    if (!s_ready) {
        radar_store_init(&s_store, &radar_fetch_mem);
        s_ready = true;
    }
}

static uint8_t source_now(void)
{
    const settings_t *set = app_settings();
    return radar_chmu_covers(set->wx_lat_e4 / 1e4, set->wx_lon_e4 / 1e4) ? RADAR_SOURCE_CHMU : RADAR_SOURCE_RAINVIEWER;
}

/* One frame, or in sync mode `always` the loop's hour (D28): 12 of ČHMÚ's, 6 of RainViewer's. */
static int keep_count(void)
{
    if (app_settings()->sync_mode != SETTINGS_SYNC_ALWAYS) {
        return 1;
    }
    return source_now() == RADAR_SOURCE_CHMU ? RADAR_LOOP_FRAMES : 3600 / RADAR_RV_STEP_S;
}

uint32_t app_radar_step_s(void)
{
    return source_now() == RADAR_SOURCE_CHMU ? RADAR_CHMU_STEP_S : RADAR_RV_STEP_S;
}

/* A frame with the rain for the view as it is now: the Radar layout's map, which holds any slot's. */
static bool fits(const radar_frame_t *f)
{
    const settings_t *set = app_settings();
    if (f->source != source_now()) {
        return false;
    }
    map_view_t v;
    map_view_init(&v, set->wx_lat_e4, set->wx_lon_e4, set->wx_zoom_q / 4.0, RADAR_VIEW_W, RADAR_VIEW_H);
    return radar_frame_covers(f, &v);
}

/* A new centre or zoom, a new mode: frames that no longer fit go, and so do those beyond the keep. */
static void tidy(void)
{
    ready();
    const radar_frame_t *newest = radar_store_newest(&s_store);
    if (newest != NULL && !fits(newest)) {
        ESP_LOGI(TAG, "the view moved: its frames are dropped");
        radar_store_clear(&s_store);
    }
    radar_store_keep(&s_store, keep_count()); /* sync mode `always` ended: the newest stays */
}

/* The newest frame from its file, once a boot or a wake: a deep sleep loses PSRAM. */
static void load(void)
{
    ready();
    if (s_loaded || s_store.count > 0) {
        return;
    }
    s_loaded = true;
    if (storage_init() != ESP_OK) {
        return;
    }
    FILE *file = fopen(FRAME_PATH, "rb");
    if (file == NULL) {
        return;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *buf = size > 0 && size <= FRAME_FILE_MAX ? heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM) : NULL;
    bool read = buf != NULL && fread(buf, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    radar_frame_t f;
    if (read && radar_frame_from_file(buf, (size_t)size, &radar_fetch_mem, &f)) {
        s_saved_at = f.time;
        ESP_LOGI(TAG, "frame of %lu from %s", (unsigned long)f.time, FRAME_PATH);
        radar_store_put(&s_store, &f, keep_count());
        tidy(); /* a frame for another view goes */
    } else if (buf != NULL) {
        ESP_LOGW(TAG, "%s: not a frame of this firmware; left out", FRAME_PATH);
    }
    heap_caps_free(buf);
}

/* The newest frame into its file, so a power-off or a deep sleep keeps it; in sync mode `always`,
 * where one comes every 5 or 10 minutes, at most every SAVE_EVERY_S, to spare the flash. */
static void save_newest(void)
{
    const radar_frame_t *f = radar_store_newest(&s_store);
    if (f == NULL || f->time == s_saved_at) {
        return;
    }
    if (app_settings()->sync_mode == SETTINGS_SYNC_ALWAYS && s_saved_at != 0 && f->time < s_saved_at + SAVE_EVERY_S) {
        return;
    }
    if (storage_init() != ESP_OK) {
        return;
    }
    size_t n = radar_frame_file_size(f);
    uint8_t *buf = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        return;
    }
    radar_frame_to_file(f, buf, n);
    esp_err_t err = storage_write_atomic(FRAME_PATH, (const char *)buf, n);
    heap_caps_free(buf);
    if (err == ESP_OK) {
        s_saved_at = f->time;
    } else {
        ESP_LOGW(TAG, "%s not saved: %s", FRAME_PATH, esp_err_to_name(err));
    }
}

void app_radar_request(radar_fetch_req_t *out)
{
    load(); /* the frame in the file is had already */
    tidy();
    const settings_t *set = app_settings();
    memset(out, 0, sizeof(*out));
    out->lat_e4 = set->wx_lat_e4;
    out->lon_e4 = set->wx_lon_e4;
    out->zoom_q = set->wx_zoom_q;
    out->want = (uint8_t)keep_count();
    for (int i = 0; i < s_store.count; i++) {
        out->have[out->have_count++] = s_store.frames[i].time;
    }
    out->now = timekeeping_valid() ? (uint32_t)time(NULL) : 0; /* a sync's own NTP time replaces it */
}

void app_radar_apply(radar_fetch_result_t *res, uint8_t result, const char *detail)
{
    tidy();
    int added = 0;
    for (int i = 0; i < res->count; i++) {
        radar_frame_t *f = &res->frames[i];
        if (fits(f)) {
            radar_store_put(&s_store, f, keep_count()); /* the store takes it */
            added++;
        } else {
            radar_frame_free(f, &radar_fetch_mem); /* the view moved while it came */
        }
    }
    res->count = 0;
    if (result != SYNC_STEP_NOT_RUN) { /* a sync that never got to it leaves the last fetch's status */
        s_status.fetched_at = time(NULL);
        s_status.ok = result == SYNC_STEP_OK;
        snprintf(s_status.detail, sizeof(s_status.detail), "%s", s_status.ok ? "" : detail);
    }
    if (added > 0) {
        save_newest();
    }
}

void app_radar_status(app_radar_status_t *out)
{
    tidy();
    *out = s_status;
    out->frames = (uint8_t)s_store.count;
    const radar_frame_t *f = radar_store_newest(&s_store);
    out->frame_at = f != NULL ? f->time : 0;
    out->source = source_now();
}

/* The preset draws the weather radar: the Radar layout, or a slot's map. */
static bool shows_weather(const ui_preset_t *p)
{
    if (p->layout == UI_LAYOUT_RADAR) {
        return true;
    }
    const ui_layout_t *layout = ui_layout((ui_layout_id_t)p->layout);
    for (int i = 0; layout != NULL && i < layout->slot_count; i++) {
        if (p->slots[i] == UI_FIELD_RAIN_MAP) {
            return true;
        }
    }
    return false;
}

void app_radar_prepare(const ui_preset_t *p)
{
    bool weather = shows_weather(p);
    if (s_map_state == 0 && (weather || p->layout == UI_LAYOUT_FLIGHTS)) {
        /* the image's own checksum covers it: only its header is checked (map_data_verify is the host's) */
        s_map_state = map_data_open(&s_map, k_map_start, (size_t)(k_map_end - k_map_start)) ? 1 : -1;
        if (s_map_state < 0) {
            ESP_LOGE(TAG, "the built-in map doesn't open");
        }
    }
    if (weather) {
        load();
    }
}

const ui_radar_t *app_radar_ui(void)
{
    tidy();
    const settings_t *set = app_settings();
    s_ui = (ui_radar_t){ .map = s_map_state > 0 ? &s_map : NULL, .home_lat_e4 = set->lat_e4,
                         .home_lon_e4 = set->lon_e4, .frame = radar_store_newest(&s_store),
                         .wx_lat_e4 = set->wx_lat_e4, .wx_lon_e4 = set->wx_lon_e4, .wx_zoom_q = set->wx_zoom_q,
                         .fl_lat_e4 = set->fl_lat_e4, .fl_lon_e4 = set->fl_lon_e4,
                         .fl_range_km = set->fl_range_km,
                         .fl_always = set->sync_mode == SETTINGS_SYNC_ALWAYS };
    return &s_ui;
}
```


`main/app_internal.h`:

```diff
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -7,6 +7,7 @@
 #include "board_buttons.h"
 #include "datastore.h"
 #include "esp_err.h"
+#include "radar_fetch.h"
 #include "scheduler.h"
 #include "settings.h"
 #include "sync.h"
@@ -14,6 +15,7 @@
 #include "ui_fields.h"
 #include "ui_menu.h"
 #include "ui_preset.h"
+#include "ui_radar.h"
 #include "webui.h"
 
 /* The dashboard's state and behaviour (main/app_ui.c, main/app_menu.c). All of it belongs to the
@@ -125,7 +127,9 @@ void app_sync_schedule(void);    /* the next automatic sync, after a sync, a set
 time_t app_sync_due(void);       /* for the wake scheduler; 0 = none */
 void app_sync_tick(void);        /* starts a sync that is due; quiet hours in sync mode `always` */
 esp_err_t app_sync_now(void);    /* on demand: ESP_ERR_NOT_FOUND with no network saved */
-bool app_sync_active(void);      /* a sync runs */
+bool app_sync_active(void);      /* a sync or a radar-only refresh runs */
+bool app_sync_refreshing(void);  /* what runs is a radar-only refresh (spec §9.3): not shown as a sync */
+bool app_sync_running(void);     /* a sync runs, or waits for a refresh to end: what the screens show */
 bool app_sync_failed(void);      /* the last sync failed a step */
 bool app_sync_holds_wifi(void);  /* sync mode `always` keeps Wi-Fi now */
 bool app_sync_wifi_pending(void); /* Wi-Fi is on, but nothing needs it: awake until it is off */
@@ -135,6 +139,27 @@ uint32_t app_sync_expected_s(void);
 /* Info ▸ Last sync: "12:05 OK", "05:30 Weather: HTTP 503", "Running", "Never". */
 void app_sync_summary(char *out, size_t size);
 
+/* The weather radar (main/app_radar.c, spec §11.2): its frames, kept in PSRAM, the newest also in
+ * /fs/state/radar.bin; the sync task fetches them. */
+typedef struct {
+    time_t fetched_at; /* the last fetch (UTC); 0 = none since the boot */
+    bool ok;           /* it brought the newest frame, or found it had it */
+    char detail[RADAR_FETCH_DETAIL_LEN];
+    uint8_t frames;    /* kept */
+    uint32_t frame_at; /* the newest one's time (UTC); 0 = none */
+    uint8_t source;    /* radar_source_t for the view now */
+} app_radar_status_t;
+void app_radar_request(radar_fetch_req_t *out); /* what a sync or a refresh fetches */
+/* The radar step's result (sync_step_result_t) and its detail: the frames that still fit the view are
+ * kept, the rest freed; a step that never ran leaves the status as it was. */
+void app_radar_apply(radar_fetch_result_t *res, uint8_t result, const char *detail);
+/* Before drawing `p`: the built-in map opened (spec §11.1) if `p` has one, and for the weather radar
+ * the newest frame read from its file, each once a boot or a wake, so other presets touch neither. */
+void app_radar_prepare(const ui_preset_t *p);
+const ui_radar_t *app_radar_ui(void); /* the radars as a render draws them now */
+uint32_t app_radar_step_s(void);      /* the source's frame step: 300 s for ČHMÚ, 600 for RainViewer */
+void app_radar_status(app_radar_status_t *out);
+
 /* Config mode (main/app_config.c, spec §10.2). */
 esp_err_t app_net_init(void); /* the Wi-Fi manager, started on first use */
 bool app_net_ready(void);     /* it started: netmgr_status() may be called */
```


`main/app_sync.c`:

```diff
--- a/main/app_sync.c
+++ b/main/app_sync.c
@@ -25,6 +25,9 @@ static const char *TAG = "app_sync";
 static bool s_active;           /* a sync runs, or its report waits for apply(): no other may start */
 static bool s_manual;           /* the running sync was asked for: it ends with a toast */
 static sync_due_t s_started_by; /* what started the running sync ({0, false} on demand) */
+static time_t s_radar_next;     /* sync mode `always`: the next radar-only refresh; 0 = at once */
+static bool s_refresh;          /* what runs is such a refresh, not a sync */
+static bool s_manual_waiting;   /* a sync asked for during such a refresh: it starts when that ends */
 
 static bool always_wanted(time_t now);
 static bool wifi_off(void);
@@ -104,6 +107,16 @@ bool app_sync_active(void)
     return s_active;
 }
 
+bool app_sync_refreshing(void)
+{
+    return s_active && s_refresh;
+}
+
+bool app_sync_running(void)
+{
+    return (s_active && !s_refresh) || s_manual_waiting;
+}
+
 bool app_sync_failed(void)
 {
     const app_sync_state_t *s = st();
@@ -187,10 +200,32 @@ static void save_summary(const sync_report_t *r, time_t started)
 
 static int64_t s_started_mono; /* esp_timer µs at the start, to date it once the clock is right */
 
+static esp_err_t start(bool manual, sync_due_t due);
+
+/* A radar-only refresh's report (spec §9.3): the frames, outside the syncs' history and retries. */
+static void apply_refresh(sync_report_t *r)
+{
+    app_radar_apply(&r->radar, r->result[SYNC_STEP_RADAR], r->detail[SYNC_STEP_RADAR]);
+    s_active = false;
+    s_refresh = false;
+    if (s_manual_waiting) {
+        s_manual_waiting = false;
+        if (start(true, (sync_due_t){ 0 }) != ESP_OK) {
+            app_ui_toast(lang_str(lang_get(app_settings()->language), LS_T_SYNC_FAILED));
+        }
+        return;
+    }
+    app_ui_render();
+}
+
 /* The report, on the app task. The EV_CALL around it sees the clock move and calls app_clock_moved(). */
 static void apply(void *arg)
 {
-    const sync_report_t *r = arg;
+    sync_report_t *r = arg;
+    if (r->radar_only) {
+        apply_refresh(r);
+        return;
+    }
     if (r->result[SYNC_STEP_TIME] == SYNC_STEP_OK) {
         int64_t moved_ms = 0;
         esp_err_t err = timekeeping_apply_true_time(r->ntp_utc_us, r->ntp_mono_us, &moved_ms);
@@ -214,6 +249,7 @@ static void apply(void *arg)
     if (r->result[SYNC_STEP_WEATHER] == SYNC_STEP_OK || r->result[SYNC_STEP_AIR] == SYNC_STEP_OK) {
         app_ui_save_forecast(); /* spec §6: it survives a power-off, shown as stale */
     }
+    app_radar_apply(&r->radar, r->result[SYNC_STEP_RADAR], r->detail[SYNC_STEP_RADAR]);
     time_t started = now - (time_t)((esp_timer_get_time() - s_started_mono) / 1000000);
     save_summary(r, started);
     bool ok = !app_sync_failed();
@@ -231,7 +267,7 @@ static void apply(void *arg)
     }
 }
 
-static void done(const sync_report_t *report) /* on the sync task */
+static void done(sync_report_t *report) /* on the sync task */
 {
     /* The report must arrive: until apply() runs, s_active holds every other sync and sleep off. */
     while (app_post(apply, (void *)report) != ESP_OK) {
@@ -249,8 +285,10 @@ static esp_err_t start(bool manual, sync_due_t due)
         return ESP_FAIL;
     }
     const settings_t *set = app_settings();
-    sync_request_t req = { .lat_e4 = set->lat_e4, .lon_e4 = set->lon_e4 };
+    static sync_request_t req; /* the radar's part is large for the app task's stack */
+    req = (sync_request_t){ .lat_e4 = set->lat_e4, .lon_e4 = set->lon_e4 };
     memcpy(req.ntp, set->ntp, sizeof(req.ntp));
+    app_radar_request(&req.radar);
     esp_err_t err = sync_start(&req, done);
     if (err == ESP_OK) {
         s_active = true;
@@ -278,6 +316,11 @@ esp_err_t app_sync_now(void)
             return ESP_ERR_INVALID_STATE; /* only the device's own network: nothing to reach */
         }
     }
+    if (s_active && s_refresh) { /* the radar's refresh ends within 30 s: the sync follows it */
+        s_manual_waiting = true;
+        app_ui_render(); /* the status bar shows it running */
+        return ESP_OK;
+    }
     return start(true, (sync_due_t){ 0 });
 }
 
@@ -308,6 +351,29 @@ void app_sync_wifi_check(void)
     app_net_refresh();
 }
 
+/* Sync mode `always` on the network: the radar alone every 5 min (RainViewer: 10), at once when the
+ * mode begins, so the loop's hour comes in one go (spec §9.3, D28). */
+static void radar_refresh_tick(time_t now)
+{
+    if (!app_sync_lan_ui()) {
+        s_radar_next = 0; /* the next time `always` holds Wi-Fi, the hour comes at once */
+        return;
+    }
+    if (s_active || now < s_radar_next) {
+        return;
+    }
+    static sync_request_t req;
+    req = (sync_request_t){ .radar_only = true };
+    app_radar_request(&req.radar);
+    if (sync_start(&req, done) == ESP_OK) {
+        s_active = true;
+        s_refresh = true;
+        s_manual = false;
+        s_radar_next = sync_radar_next(now, app_radar_step_s());
+        ESP_LOGI(TAG, "radar refresh: %u frames kept", req.radar.have_count);
+    }
+}
+
 void app_sync_tick(void)
 {
     time_t now = time(NULL);
@@ -316,6 +382,7 @@ void app_sync_tick(void)
     if (critical || app_ui_night()) {
         return; /* nothing may drain the battery, and the night runs nothing (spec §9.1) */
     }
+    radar_refresh_tick(now);
     sync_due_t due = st()->due;
     if (!s_active && due.at > now && st()->history.failed_at == 0 && always_wanted(now) && wifi_off()) {
         app_sync_schedule(); /* a night ended in sync mode `always`: Wi-Fi back at once, not at the hour */
@@ -342,7 +409,7 @@ void app_sync_summary(char *out, size_t size)
 {
     const lang_t *lang = lang_get(app_settings()->language);
     const app_sync_state_t *s = st();
-    if (s_active) {
+    if (app_sync_running()) {
         snprintf(out, size, "%s", lang_str(lang, LS_SYNC_RUNNING));
         return;
     }
@@ -357,7 +424,7 @@ void app_sync_summary(char *out, size_t size)
     const char *suffix;
     lang_format_time(local.tm_hour, local.tm_min, 0, app_settings()->clock_24h, false, when, sizeof(when), &suffix);
     static const lang_str_t k_steps[SYNC_STEP_COUNT] = { LS_SYNC_STEP_WIFI, LS_SYNC_STEP_TIME, LS_SYNC_STEP_WEATHER,
-                                                         LS_SYNC_STEP_AIR };
+                                                         LS_SYNC_STEP_AIR, LS_SYNC_STEP_RADAR };
     if (s->last_failed_step >= SYNC_STEP_COUNT) {
         snprintf(out, size, "%s%s%s OK", when, suffix[0] ? " " : "", suffix);
     } else {
```


`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -213,7 +213,7 @@ void app_ui_context(ui_context_t *ctx)
                            .fahrenheit = s.settings.fahrenheit,
                            .web_session = (app_config_active() || app_sync_lan_ui()) && webui_session_active(),
                            .lat_e4 = s.settings.lat_e4, .lon_e4 = s.settings.lon_e4,
-                           .sync = app_sync_active()   ? UI_SYNC_RUNNING
+                           .sync = app_sync_running()  ? UI_SYNC_RUNNING
                                    : app_sync_failed() ? UI_SYNC_FAILED
                                                        : UI_SYNC_IDLE };
     if (app_sync_holds_wifi() && !app_config_active()) { /* spec §5.2: sync mode `always` */
@@ -223,6 +223,7 @@ void app_ui_context(ui_context_t *ctx)
     }
     localtime_r(&now, &ctx->local);
     ctx->local_day = local_day(&ctx->local);
+    ctx->radar = app_radar_ui(); /* M6 */
 }
 
 void app_ui_render(void)
@@ -235,6 +236,7 @@ void app_ui_render(void)
         app_menu_render();
         return;
     }
+    app_radar_prepare(&s.presets.presets[s.presets.active]);
     ui_context_t ctx;
     app_ui_context(&ctx);
     if (s.critical) {
```


`main/app_web.c`:

```diff
--- a/main/app_web.c
+++ b/main/app_web.c
@@ -160,8 +160,9 @@ static void get_status(uint8_t *out, size_t size, webui_reply_t *reply)
     static const char *const k_modes[] = { "times", "interval", "always", "manual" };
     cJSON_AddStringToObject(sync, "mode", k_modes[st->settings.sync_mode <= SETTINGS_SYNC_MANUAL
                                                        ? st->settings.sync_mode : 0]);
-    cJSON_AddBoolToObject(sync, "running", app_sync_active());
-    if (app_sync_active()) {
+    bool running = app_sync_running(); /* a radar refresh isn't a sync (spec §9.3) */
+    cJSON_AddBoolToObject(sync, "running", running);
+    if (running) {
         cJSON_AddStringToObject(sync, "step", sync_step_name(sync_step()));
     }
     if (st->sync.last_at != 0) {
```


`main/app_cmds.c`:

```diff
--- a/main/app_cmds.c
+++ b/main/app_cmds.c
@@ -333,7 +333,8 @@ static int sync_body(int argc, char **argv)
                s->quiet_to / 60, s->quiet_to % 60, s->quiet ? "on" : "off", (unsigned long)app_sync_expected_s());
         const app_sync_state_t *st = &app_state()->sync;
         if (app_sync_active()) {
-            printf("running: %s\n", sync_running() ? sync_step_name(sync_step()) : "applying its report");
+            printf("running: %s%s\n", app_sync_refreshing() ? "radar refresh, " : "",
+                   sync_running() ? sync_step_name(sync_step()) : "applying its report");
         }
         print_time("last", st->last_at);
         if (st->last_at != 0) {
```


`main/CMakeLists.txt`:

```diff
--- a/main/CMakeLists.txt
+++ b/main/CMakeLists.txt
@@ -1,5 +1,7 @@
 idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_menu.c" "app_cmds.c" "app_config.c" "app_web.c" "app_sync.c"
+                            "app_radar.c"
                        INCLUDE_DIRS "."
                        PRIV_REQUIRES app_update board console datastore diag display esp_app_format
-                                     esp_driver_gpio esp_timer espcoredump gfx json locale netmgr nvs_flash power
-                                     rtc scheduler sensors st7305 storage sync timekeeping ui util weather webui)
+                                     esp_driver_gpio esp_timer espcoredump gfx heap json locale map netmgr nvs_flash
+                                     power radar rtc scheduler sensors st7305 storage sync timekeeping ui util weather
+                                     webui)
```


- [ ] **Step 8: Build and test.**

Run: `tools/idf.sh reconfigure && tools/idf.sh build 2>&1 | grep -E 'warning:|error:|binary size'` and `cmake --build build-host && ctest --test-dir build-host`
Expected: no warning or error; `reflbo.bin binary size 0x23…` (about 2.35 MB, 44 % of the slot free); `out of 60`, all passing.

- [ ] **Step 9: AGENTS.md, and commit.** The `fetch` component joins the list in §5.2, and the `radar/` line names its fetch (AGENTS.md changes in the same commit, §8):

`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -219,10 +219,11 @@ components/
   power/         power states, idle strategy, sleep entry
   netmgr/        Wi-Fi STA/AP, captive DNS, mDNS
   webui/         HTTP server, REST API, embedded web assets
+  fetch/         HTTPS GETs with the certificate bundle, one connection kept per host
   weather/       Open-Meteo URLs, parsers, bands and levels; the HTTPS fetch
   png/           PNG reader for the radar images: palette and RGBA, row by row   [host]
   map/           web-Mercator views; the built-in map (assets/map/map.bin) and its drawing   [host]
-  radar/         ČHMÚ's and RainViewer's frames: their decoding, store and drawing   [host]
+  radar/         ČHMÚ's and RainViewer's frames: their decoding, store and drawing [host]; their fetch
   adsb/          adsb.fi's aircraft on the Flights map, adsb.lol's routes and their cache   [host]
   ha_mqtt/       MQTT session, discovery, state, commands, field mappings   (planned)
   sync/          when syncs run and their retries, SNTP packets, the sync task
```


```bash
git add components/fetch components/radar components/weather components/sync components/locale main test/host AGENTS.md
git commit -m "feat: the weather radar on the device (spec §11.2)"
```


### Task 12: The flight radar's polling (`adsb_task`, `app_flights`)

**Files:**
- Create: `components/adsb/include/adsb_task.h`, `components/adsb/adsb_task.c`, `main/app_flights.c`
- Modify: `components/adsb/CMakeLists.txt`, `main/app_internal.h`, `main/app_radar.c`, `main/app_ui.c`, `main/CMakeLists.txt`, `sdkconfig.defaults`
- Modify: `AGENTS.md` (§5.1's key settings, §5.2's `adsb/` line)

**Interfaces:**
- Consumes: `adsb_*` (Task 6), `fetch_*` (Task 11), `ui_radar_t`'s flight fields (Task 10), `app_post()`, `app_sync_lan_ui()`.
- Produces (Task 13): `adsb_task_req_t` (`filter`, `range_km`), `adsb_report_t` (`ok`, `detail`, `list`, `has_route`, `route`), `adsb_task_start(req, done)`, `adsb_task_stop()`, `adsb_task_running()`, `adsb_report_free()`; in `main/app_internal.h` `app_flights_status_t`, `app_flights_tick(bool shown)`, `app_flights_fill(ui_radar_t *)`, `app_flights_status()`.

How it runs (spec §11.3, D22, D27):
- The task polls while the Flights view is on the dashboard and sync mode `always` holds Wi-Fi on the network (`app_sync_lan_ui()`: not in quiet hours, a night or the critical screen); every render decides, before the menu or a night returns early, so a preset switch, the menu or a lost network starts or stops it at once. A render that asks for the same poll leaves the task alone: the request is compared field by field, not with `memcmp()`, as a struct's padding needn't match.
- Every 5 s up to a 25 km range, 10 s up to 50 km, 15 s beyond, doubling after each failure up to 60 s; a new request (a settings change) polls at once. adsb.fi's connection stays open between polls.
- When the nearest aircraft's callsign has no route in the cache, one lookup at adsb.lol on a connection closed straight after (ruling); a failed or unknown answer is kept an hour, a 403 or 429 pauses lookups for an hour.
- Each poll's report is PSRAM handed to the app task, which keeps the last good one and frees the rest.
- TLS now allocates in PSRAM (ruling): delete `sdkconfig` after editing `sdkconfig.defaults`, then build.

- [ ] **Step 1: The task.** It only fetches; the device is its test, in Task 15.

`components/adsb/include/adsb_task.h`:

```c
#pragma once

#include <stdbool.h>

#include "adsb.h"
#include "esp_err.h"

/*
 * The flight radar's polling (spec §11.3): adsb.fi every 5-15 s over one kept-alive connection, and
 * the nearest aircraft's route from adsb.lol, on a task of its own. It only fetches: each report goes
 * to the app task, which shows it. Device only.
 */

#define ADSB_DETAIL_LEN 24

typedef struct {
    adsb_filter_t filter; /* the Flights map and the settings' filters */
    int range_km;         /* radar.flights.range_km: the poll interval follows it */
} adsb_task_req_t;

typedef struct {
    bool ok;
    char detail[ADSB_DETAIL_LEN]; /* why the poll failed: "HTTP 429", "too big" */
    adsb_list_t list;             /* when ok: the nearest first */
    bool has_route;
    adsb_route_t route; /* the nearest one's, when the cache has it */
} adsb_report_t;

/* Starts polling, or changes what the running task polls; `done` runs on the adsb task after each
 * poll and hands the report to the app task, which frees it with adsb_report_free(). */
esp_err_t adsb_task_start(const adsb_task_req_t *req, void (*done)(adsb_report_t *report));
/* Stops after the poll in flight. */
void adsb_task_stop(void);
bool adsb_task_running(void);
void adsb_report_free(adsb_report_t *report);
```


`components/adsb/adsb_task.c`:

```c
#include "adsb_task.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fetch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "adsb";

#define TASK_STACK 10240 /* TLS on this task */
#define TASK_PRIORITY 2  /* below the sync (3), netmgr (4) and the app (5) */
#define POLL_TIMEOUT_MS 10000
#define ROUTE_TIMEOUT_MS 5000
#define ROUTE_BODY_MAX 2048          /* adsb.lol's route is under 1 KB */
#define ROUTE_PAUSE_S 3600           /* after a 403 or 429 from adsb.lol */

EXT_RAM_BSS_ATTR static char s_body[ADSB_REPLY_MAX + 1];
EXT_RAM_BSS_ATTR static char s_route_body[ROUTE_BODY_MAX];
EXT_RAM_BSS_ATTR static adsb_routes_t s_routes;

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static bool s_running, s_stop;
static adsb_task_req_t s_req;
static void (*s_done)(adsb_report_t *report);
static time_t s_routes_paused_until;

void adsb_report_free(adsb_report_t *report)
{
    heap_caps_free(report);
}

/* The nearest aircraft's route: from the cache, else one lookup at adsb.lol (D27). */
static void route_for(const adsb_aircraft_t *a, adsb_report_t *r)
{
    time_t now = time(NULL);
    if (a->callsign[0] == '\0') {
        return;
    }
    const adsb_route_t *cached = adsb_routes_find(&s_routes, a->callsign, (uint32_t)now);
    char url[128];
    if (cached == NULL && now >= s_routes_paused_until &&
        adsb_route_url(url, sizeof(url), a->callsign, a->lat, a->lon)) {
        fetch_session_t lol = { 0 }; /* closed again at once: its TLS session isn't kept for the rare lookup */
        size_t len;
        int status;
        esp_err_t err = fetch_get(&lol, url, s_route_body, sizeof(s_route_body), &len, ROUTE_TIMEOUT_MS, &status);
        fetch_close(&lol);
        if (status == 403 || status == 429) {
            s_routes_paused_until = now + ROUTE_PAUSE_S; /* adsb.lol asks us to back off */
            ESP_LOGW(TAG, "routes paused: HTTP %d", status);
            return;
        }
        adsb_route_t found;
        if (err != ESP_OK || !adsb_route_parse(s_route_body, len, &found)) {
            memset(&found, 0, sizeof(found)); /* unknown, asked again in an hour */
        }
        snprintf(found.callsign, sizeof(found.callsign), "%s", a->callsign);
        found.looked_up = (uint32_t)now;
        adsb_routes_put(&s_routes, &found);
        ESP_LOGI(TAG, "route of %s: %s", a->callsign, found.known ? "known" : "unknown");
        cached = adsb_routes_find(&s_routes, a->callsign, (uint32_t)now);
    }
    if (cached != NULL && cached->known) {
        r->route = *cached;
        r->has_route = true;
    }
}

static void poll(fetch_session_t *fi, const adsb_task_req_t *req, adsb_report_t *r)
{
    char url[128];
    adsb_url(url, sizeof(url), &req->filter);
    size_t len;
    int status;
    esp_err_t err = fetch_get(fi, url, s_body, sizeof(s_body), &len, POLL_TIMEOUT_MS, &status);
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_SIZE) {
            snprintf(r->detail, sizeof(r->detail), "%s", adsb_err_name(ADSB_ERR_TOO_BIG));
        } else if (status != 0 && status != 200) {
            snprintf(r->detail, sizeof(r->detail), "HTTP %d", status);
        } else {
            snprintf(r->detail, sizeof(r->detail), "%s", err == ESP_ERR_TIMEOUT ? "timeout" : esp_err_to_name(err));
        }
        return;
    }
    adsb_err_t perr = adsb_parse(s_body, len, &req->filter, &r->list);
    r->ok = perr == ADSB_OK;
    if (!r->ok) {
        snprintf(r->detail, sizeof(r->detail), "%s", adsb_err_name(perr));
    } else if (r->list.count > 0) {
        route_for(&r->list.ac[0], r);
    }
}

static void adsb_task(void *arg)
{
    (void)arg;
    fetch_session_t fi = { 0 }; /* kept alive between polls (spec §11.3) */
    int failures = 0;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_stop) {
            s_running = false;
            s_task = NULL;
            xSemaphoreGive(s_lock);
            break;
        }
        adsb_task_req_t req = s_req;
        void (*done)(adsb_report_t *) = s_done;
        xSemaphoreGive(s_lock);
        adsb_report_t *r = heap_caps_calloc(1, sizeof(*r), MALLOC_CAP_SPIRAM);
        if (r != NULL) {
            poll(&fi, &req, r);
            failures = r->ok ? 0 : failures + 1;
            if (!r->ok) {
                ESP_LOGW(TAG, "poll: %s", r->detail);
            }
            done(r); /* the app task's now */
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(adsb_poll_s(req.range_km, failures) * 1000)); /* or a new request */
    }
    fetch_close(&fi);
    ESP_LOGI(TAG, "stopped");
    vTaskDelete(NULL);
}

esp_err_t adsb_task_start(const adsb_task_req_t *req, void (*done)(adsb_report_t *report))
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock");
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_req = *req;
    s_done = done;
    s_stop = false;
    esp_err_t err = ESP_OK;
    if (s_running) {
        xTaskNotifyGive(s_task); /* a poll for the new request now */
    } else if (xTaskCreatePinnedToCore(adsb_task, "adsb", TASK_STACK, NULL, TASK_PRIORITY, &s_task, 0) == pdPASS) {
        s_running = true;
        ESP_LOGI(TAG, "polling");
    } else {
        err = ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_lock);
    return err;
}

void adsb_task_stop(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_running && !s_stop) {
        s_stop = true;
        xTaskNotifyGive(s_task);
    }
    xSemaphoreGive(s_lock);
}

bool adsb_task_running(void)
{
    return s_running;
}
```


`components/adsb/CMakeLists.txt`:

```diff
--- a/components/adsb/CMakeLists.txt
+++ b/components/adsb/CMakeLists.txt
@@ -1,6 +1,6 @@
 # The flight radar's data (spec §11.3): adsb.fi's aircraft around the radar's centre and adsb.lol's
-# routes. adsb_parse.c and adsb_route.c are pure C and also build on the host.
-idf_component_register(SRCS "adsb_parse.c" "adsb_route.c"
+# routes. adsb_parse.c and adsb_route.c are pure C and also build on the host; adsb_task.c polls.
+idf_component_register(SRCS "adsb_parse.c" "adsb_route.c" "adsb_task.c"
                        INCLUDE_DIRS "include"
-                       REQUIRES map
-                       PRIV_REQUIRES json util)
+                       REQUIRES map esp_common
+                       PRIV_REQUIRES json util fetch heap log freertos)
```


- [ ] **Step 2: The app's side.**

`main/app_flights.c`:

```c
#include <string.h>

#include "adsb_task.h"
#include "app_internal.h"
#include "esp_log.h"

/* The flight radar on the device (spec §11.3): its task polls while sync mode `always` is on the
 * network and the Flights view is on screen, outside quiet hours and nights (D22); the app task keeps
 * the last good poll's report for the view. */

static const char *TAG = "app_flights";

static adsb_report_t *s_last; /* the last good poll's report; the app task's */
static time_t s_updated;      /* when it came (UTC); 0 = none since the view came up */
static bool s_failed;         /* the last poll failed */
static adsb_task_req_t s_asked; /* what the task polls now */
static bool s_on;

static void apply(void *arg) /* on the app task */
{
    adsb_report_t *r = arg;
    if (!s_on) { /* the view went away meanwhile */
        adsb_report_free(r);
        return;
    }
    s_failed = !r->ok;
    if (r->ok) {
        adsb_report_free(s_last);
        s_last = r;
        s_updated = time(NULL);
    } else {
        adsb_report_free(r);
    }
    app_ui_render();
}

static void done(adsb_report_t *report) /* on the adsb task */
{
    if (app_post(apply, report) != ESP_OK) {
        adsb_report_free(report); /* the app is busy: the next poll comes soon */
    }
}

/* The Flights map as the view draws it, with the settings' filters. */
static void request(adsb_task_req_t *out)
{
    const settings_t *set = app_settings();
    memset(out, 0, sizeof(*out));
    out->range_km = set->fl_range_km;
    out->filter = (adsb_filter_t){ .lat = set->fl_lat_e4 / 1e4, .lon = set->fl_lon_e4 / 1e4,
                                   .min_alt_ft = set->fl_min_alt_ft, .ground = set->fl_ground, .max = set->fl_max };
    map_view_init(&out->filter.view, set->fl_lat_e4, set->fl_lon_e4,
                  map_zoom_for_range(set->fl_lat_e4, set->fl_range_km * 1000.0, UI_FLIGHTS_MAP_H / 2), 400,
                  UI_FLIGHTS_MAP_H);
}

/* The same poll as the task's: compared field by field, as a struct's padding may differ. */
static bool same(const adsb_task_req_t *a, const adsb_task_req_t *b)
{
    return a->range_km == b->range_km && a->filter.lat == b->filter.lat && a->filter.lon == b->filter.lon &&
           a->filter.min_alt_ft == b->filter.min_alt_ft && a->filter.ground == b->filter.ground &&
           a->filter.max == b->filter.max; /* the view follows from the centre and the range */
}

void app_flights_tick(bool shown)
{
    bool wanted = shown && app_sync_lan_ui(); /* `always` on the network: not in quiet hours or a night */
    if (!wanted) {
        if (s_on) {
            adsb_task_stop();
            s_on = false;
            ESP_LOGI(TAG, "off");
        }
        return;
    }
    adsb_task_req_t req;
    request(&req);
    if (s_on && same(&req, &s_asked)) {
        return;
    }
    if (!s_on) { /* aircraft from an earlier visit are old: none until the first poll */
        adsb_report_free(s_last);
        s_last = NULL;
        s_updated = 0;
        s_failed = false;
    }
    if (adsb_task_start(&req, done) == ESP_OK) {
        s_asked = req;
        s_on = true;
    }
}

void app_flights_fill(ui_radar_t *ui)
{
    ui->fl_updated = s_updated;
    ui->fl_failed = s_failed;
    ui->aircraft = s_last != NULL ? &s_last->list : NULL;
    ui->route = s_last != NULL && s_last->has_route ? &s_last->route : NULL;
}

void app_flights_status(app_flights_status_t *out)
{
    *out = (app_flights_status_t){ .on = s_on, .updated = s_updated, .failed = s_failed,
                                   .aircraft = s_last != NULL ? (uint8_t)s_last->list.count : 0 };
}
```


`main/app_internal.h`:

```diff
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -160,6 +160,18 @@ const ui_radar_t *app_radar_ui(void); /* the radars as a render draws them now *
 uint32_t app_radar_step_s(void);      /* the source's frame step: 300 s for ČHMÚ, 600 for RainViewer */
 void app_radar_status(app_radar_status_t *out);
 
+/* The flight radar (main/app_flights.c, spec §11.3): its task, and the reports it brings. */
+typedef struct {
+    bool on;          /* its task polls */
+    time_t updated;   /* the last good poll (UTC); 0 = none since the view came up */
+    bool failed;      /* the last poll failed */
+    uint8_t aircraft; /* on the map at the last good poll */
+} app_flights_status_t;
+/* Starts or stops the polling: `shown` says the Flights view is on screen; call before each render. */
+void app_flights_tick(bool shown);
+void app_flights_fill(ui_radar_t *ui); /* the last good poll into the view's context */
+void app_flights_status(app_flights_status_t *out);
+
 /* Config mode (main/app_config.c, spec §10.2). */
 esp_err_t app_net_init(void); /* the Wi-Fi manager, started on first use */
 bool app_net_ready(void);     /* it started: netmgr_status() may be called */
```


`main/app_radar.c`:

```diff
--- a/main/app_radar.c
+++ b/main/app_radar.c
@@ -233,5 +233,6 @@ const ui_radar_t *app_radar_ui(void)
                          .fl_lat_e4 = set->fl_lat_e4, .fl_lon_e4 = set->fl_lon_e4,
                          .fl_range_km = set->fl_range_km,
                          .fl_always = set->sync_mode == SETTINGS_SYNC_ALWAYS };
+    app_flights_fill(&s_ui);
     return &s_ui;
 }
```


`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -229,6 +229,10 @@ void app_ui_context(ui_context_t *ctx)
 void app_ui_render(void)
 {
     gfx_fb_t *fb = display_fb();
+    const ui_preset_t *active = &s.presets.presets[s.presets.active];
+    bool flights = fb != NULL && !display_asleep() && !app_menu_is_open() && !s.critical &&
+                   !app_config_shows_setup() && !s.first_run && active->layout == UI_LAYOUT_FLIGHTS;
+    app_flights_tick(flights); /* spec §11.3: it polls only while the Flights view shows */
     if (fb == NULL || display_asleep()) {
         return; /* night sleep: nothing is drawn (spec §9.1) */
     }
@@ -236,7 +240,7 @@ void app_ui_render(void)
         app_menu_render();
         return;
     }
-    app_radar_prepare(&s.presets.presets[s.presets.active]);
+    app_radar_prepare(active);
     ui_context_t ctx;
     app_ui_context(&ctx);
     if (s.critical) {
```


`main/CMakeLists.txt`:

```diff
--- a/main/CMakeLists.txt
+++ b/main/CMakeLists.txt
@@ -1,7 +1,7 @@
 idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_menu.c" "app_cmds.c" "app_config.c" "app_web.c" "app_sync.c"
-                            "app_radar.c"
+                            "app_radar.c" "app_flights.c"
                        INCLUDE_DIRS "."
-                       PRIV_REQUIRES app_update board console datastore diag display esp_app_format
+                       PRIV_REQUIRES adsb app_update board console datastore diag display esp_app_format
                                      esp_driver_gpio esp_timer espcoredump gfx heap json locale map netmgr nvs_flash
                                      power radar rtc scheduler sensors st7305 storage sync timekeeping ui util weather
                                      webui)
```


- [ ] **Step 3: TLS in PSRAM.**

`sdkconfig.defaults`:

```diff
--- a/sdkconfig.defaults
+++ b/sdkconfig.defaults
@@ -51,3 +51,8 @@ CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
 # the captive DNS one more: the default 10 sockets run out, and accept() fails with ENFILE.
 CONFIG_HTTPD_MAX_REQ_HDR_LEN=2048
 CONFIG_LWIP_MAX_SOCKETS=16
+
+# TLS (M6): mbedTLS takes its buffers from PSRAM. In sync mode `always` the flight radar keeps a
+# connection to adsb.fi open while radar refreshes and syncs open their own; on internal RAM their
+# 20-40 KB each would leave Wi-Fi and the web server short (M5's lowest was 80 KB free).
+CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
```


- [ ] **Step 4: Build from a fresh `sdkconfig`.**

Run: `rm sdkconfig && tools/idf.sh reconfigure && tools/idf.sh build 2>&1 | grep -E 'warning:|error:|binary size'; grep MBEDTLS_EXTERNAL_MEM_ALLOC sdkconfig`
Expected: no warning or error; `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`. The host suite is unchanged: `out of 60`, all passing.

- [ ] **Step 5: AGENTS.md, and commit.** The key settings in §5.1 gain TLS in PSRAM, and the `adsb/` line in §5.2 its polling task (AGENTS.md changes in the same commit, §8):

`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -196,7 +196,7 @@ The spec has the full design. This section keeps the essentials at hand.
 | Audio | `esp_codec_dev` (ES8311/ES7210) and the Espressif audio decoder |
 | OTA | Two app slots with rollback |
 
-Partition table: spec §14.1. Key `sdkconfig.defaults`: 16 MB QIO flash; octal PSRAM at 80 MHz with `CONFIG_SPIRAM_MEMTEST=n`; console on USB-Serial-JTAG; custom partition table; `CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`; bootloader and app logs at warning level (the app raises its logs to info once the board stays awake); core dumps without the boot-time check and logs; `tasks` statistics; app rollback; 16 lwIP sockets and 2 KB request headers for the web server (gotcha 25; other gadgets' cookies for 192.168.4.1 come along); large static buffers in PSRAM (`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` with `EXT_RAM_BSS_ATTR`). Automatic light sleep (`CONFIG_PM_ENABLE`) is not used (D14).
+Partition table: spec §14.1. Key `sdkconfig.defaults`: 16 MB QIO flash; octal PSRAM at 80 MHz with `CONFIG_SPIRAM_MEMTEST=n`; console on USB-Serial-JTAG; custom partition table; `CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`; bootloader and app logs at warning level (the app raises its logs to info once the board stays awake); core dumps without the boot-time check and logs; `tasks` statistics; app rollback; 16 lwIP sockets and 2 KB request headers for the web server (gotcha 25; other gadgets' cookies for 192.168.4.1 come along); large static buffers in PSRAM (`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` with `EXT_RAM_BSS_ATTR`); TLS allocations in PSRAM too (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`), so the flight radar's polls and a sync each hold a connection without exhausting internal RAM. Automatic light sleep (`CONFIG_PM_ENABLE`) is not used (D14).
 
 ### 5.2 Components
 
@@ -224,7 +224,7 @@ components/
   png/           PNG reader for the radar images: palette and RGBA, row by row   [host]
   map/           web-Mercator views; the built-in map (assets/map/map.bin) and its drawing   [host]
   radar/         ČHMÚ's and RainViewer's frames: their decoding, store and drawing [host]; their fetch
-  adsb/          adsb.fi's aircraft on the Flights map, adsb.lol's routes and their cache   [host]
+  adsb/          adsb.fi's aircraft on the Flights map, adsb.lol's routes and their cache [host]; the polling task
   ha_mqtt/       MQTT session, discovery, state, commands, field mappings   (planned)
   sync/          when syncs run and their retries, SNTP packets, the sync task
   audio/         codec control, tone/WAV/stream players, alarm ringing      (planned)
```


```bash
git add components/adsb main sdkconfig.defaults AGENTS.md
git commit -m "feat(adsb): the flight radar's polling task (D22)"
```


### Task 13: The loop, the console, the status and the preview (`main`)

**Files:**
- Modify: `main/app.c`, `main/app_radar.c`, `main/app_internal.h`, `main/app_cmds.c`, `main/app_web.c`
- Modify: `AGENTS.md` (§6's commands, §7's console list)

**Interfaces:**
- Consumes: Tasks 11 and 12.
- Produces (Tasks 14, 15): `app_radar_loop_start()`, `app_radar_loop_tick()`, `app_radar_loop_stop()`, `app_radar_loop_deadline_ms()`; the console's `radar status` and `radar loop`; `GET /api/status`'s `radar` block (`weather`: `source` "chmu" or "rainviewer", `frames`, `frame_at`, `fetched_at`, `error`; `flights`: `on`, `aircraft`, `updated`, `failed`).

- **The loop** (D28, spec §11.2): BOOT short on the Radar layout in sync mode `always`, with two frames or more, switches the panel to HPM and shows the kept frames oldest first, a frame every 333 ms, then the newest again and LPM. Any other gesture ends it first, so KEY short still switches the preset; new frames end it too. Outside `always`, or with one frame, BOOT short refreshes the sensors as before. The app loop waits for the next frame's time and stays awake while it plays.
- **A preview** goes through `app_radar_prepare()` like a render: a preset with a map opens it, and one with the weather radar reads the newest frame from its file if a wake left none in PSRAM.

- [ ] **Step 1: The loop.**

`main/app_radar.c`:

```diff
--- a/main/app_radar.c
+++ b/main/app_radar.c
@@ -5,6 +5,7 @@
 #include "esp_heap_caps.h"
 #include "esp_log.h"
 #include "map_data.h"
+#include "st7305.h"
 #include "storage.h"
 #include "timekeeping.h"
 
@@ -16,6 +17,7 @@ static const char *TAG = "app_radar";
 #define FRAME_PATH "/fs/state/radar.bin"
 #define FRAME_FILE_MAX (RADAR_FILE_HEADER + 3 * 256 * 3 * 256 / 4) /* RainViewer's 3 x 3 tiles */
 #define SAVE_EVERY_S 1800 /* sync mode `always`: the file follows its new frames at most this often */
+#define LOOP_STEP_MS 333 /* the loop: about 3 frames a second (spec §11.2) */
 
 extern const uint8_t k_map_start[] asm("_binary_map_bin_start");
 extern const uint8_t k_map_end[] asm("_binary_map_bin_end");
@@ -28,6 +30,8 @@ static map_data_t s_map;
 static int s_map_state; /* 0 not opened yet, 1 open, -1 broken */
 static app_radar_status_t s_status;
 static ui_radar_t s_ui;
+static uint8_t s_loop_at, s_loop_count; /* the loop (D28): the frame shown of how many; 0 when it doesn't run */
+static int64_t s_loop_next_ms;          /* app_uptime_ms() of its next frame */
 
 static void ready(void)
 {
@@ -161,6 +165,7 @@ void app_radar_request(radar_fetch_req_t *out)
 
 void app_radar_apply(radar_fetch_result_t *res, uint8_t result, const char *detail)
 {
+    app_radar_loop_stop(); /* new frames move the old ones along */
     tidy();
     int added = 0;
     for (int i = 0; i < res->count; i++) {
@@ -227,8 +232,11 @@ const ui_radar_t *app_radar_ui(void)
 {
     tidy();
     const settings_t *set = app_settings();
+    bool loop = s_loop_count > 0 && s_loop_at < s_store.count;
     s_ui = (ui_radar_t){ .map = s_map_state > 0 ? &s_map : NULL, .home_lat_e4 = set->lat_e4,
-                         .home_lon_e4 = set->lon_e4, .frame = radar_store_newest(&s_store),
+                         .home_lon_e4 = set->lon_e4,
+                         .frame = loop ? &s_store.frames[s_loop_at] : radar_store_newest(&s_store),
+                         .loop_at = loop ? s_loop_at : 0, .loop_count = loop ? s_loop_count : 0,
                          .wx_lat_e4 = set->wx_lat_e4, .wx_lon_e4 = set->wx_lon_e4, .wx_zoom_q = set->wx_zoom_q,
                          .fl_lat_e4 = set->fl_lat_e4, .fl_lon_e4 = set->fl_lon_e4,
                          .fl_range_km = set->fl_range_km,
@@ -236,3 +244,52 @@ const ui_radar_t *app_radar_ui(void)
     app_flights_fill(&s_ui);
     return &s_ui;
 }
+
+bool app_radar_loop_start(void)
+{
+    tidy();
+    if (app_settings()->sync_mode != SETTINGS_SYNC_ALWAYS || s_store.count < 2) {
+        return false; /* outside sync mode `always` there is one frame (spec §11.2) */
+    }
+    s_loop_count = (uint8_t)s_store.count;
+    s_loop_at = 0;
+    esp_err_t err = st7305_set_mode(ST7305_MODE_HPM); /* each frame shows at once */
+    if (err != ESP_OK) {
+        ESP_LOGW(TAG, "loop: HPM: %s", esp_err_to_name(err));
+    }
+    s_loop_next_ms = app_uptime_ms() + LOOP_STEP_MS;
+    ESP_LOGI(TAG, "loop: %u frames", s_loop_count);
+    app_ui_render();
+    return true;
+}
+
+void app_radar_loop_tick(void)
+{
+    if (s_loop_count == 0 || app_uptime_ms() < s_loop_next_ms) {
+        return;
+    }
+    if (++s_loop_at >= s_loop_count || s_loop_at >= s_store.count) {
+        app_radar_loop_stop(); /* it ends on the newest */
+        return;
+    }
+    s_loop_next_ms += LOOP_STEP_MS;
+    app_ui_render();
+}
+
+void app_radar_loop_stop(void)
+{
+    if (s_loop_count == 0) {
+        return;
+    }
+    s_loop_count = 0;
+    esp_err_t err = st7305_set_mode(ST7305_MODE_LPM);
+    if (err != ESP_OK) {
+        ESP_LOGW(TAG, "loop: LPM: %s", esp_err_to_name(err));
+    }
+    app_ui_render();
+}
+
+int64_t app_radar_loop_deadline_ms(void)
+{
+    return s_loop_count > 0 ? s_loop_next_ms : 0;
+}
```


`main/app_internal.h`:

```diff
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -159,6 +159,12 @@ void app_radar_prepare(const ui_preset_t *p);
 const ui_radar_t *app_radar_ui(void); /* the radars as a render draws them now */
 uint32_t app_radar_step_s(void);      /* the source's frame step: 300 s for ČHMÚ, 600 for RainViewer */
 void app_radar_status(app_radar_status_t *out);
+/* The loop (D28): BOOT short on the Radar layout in sync mode `always` plays the kept frames in HPM,
+ * oldest first, then stops on the newest in LPM. False, and nothing played, with fewer than two. */
+bool app_radar_loop_start(void);
+void app_radar_loop_tick(void);  /* its next frame when it is due; call from the app loop */
+void app_radar_loop_stop(void);  /* KEY, the menu, new frames */
+int64_t app_radar_loop_deadline_ms(void); /* app_uptime_ms() of its next frame; 0 when it doesn't run */
 
 /* The flight radar (main/app_flights.c, spec §11.3): its task, and the reports it brings. */
 typedef struct {
```


`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -276,6 +276,9 @@ static void handle_button(board_button_t button, gesture_t gesture)
     }
     const lang_t *lang = lang_get(app_settings()->language);
     char text[64];
+    if (button != BOARD_BUTTON_BOOT || gesture != GESTURE_SHORT) {
+        app_radar_loop_stop(); /* spec §11.2: KEY short still switches the preset */
+    }
     if (app_menu_is_open()) {
         bool held = gesture == GESTURE_LONG;
         app_menu_key(button == BOARD_BUTTON_KEY ? (held ? UI_MENU_KEY_SELECT : UI_MENU_KEY_NEXT)
@@ -307,6 +310,9 @@ static void handle_button(board_button_t button, gesture_t gesture)
         app_ui_toast(lang_str(lang, app_presets()->cycle_enabled ? LS_T_CYCLE_ON : LS_T_CYCLE_OFF));
     } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_LONG) {
         app_menu_open();
+    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT &&
+               app_presets()->presets[app_presets()->active].layout == UI_LAYOUT_RADAR && app_radar_loop_start()) {
+        ESP_LOGI(TAG, "BOOT short: the radar's loop"); /* D28 */
     } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT) {
         app_ui_sample(time(NULL));
         app_ui_render();
@@ -672,6 +678,7 @@ static void app_task(void *arg)
         /* Config mode and a new image waiting to prove itself keep the chip awake: sleep would
          * drop Wi-Fi, and a deep-sleep wake would roll the image back (spec §10.5). */
         bool pending = uxQueueMessagesWaiting(s_queue) > 0 || board_buttons_busy() || app_config_active() ||
+                       app_radar_loop_deadline_ms() != 0 ||
                        s_ota_pending || app_sync_active() || app_sync_holds_wifi() || /* neither sleep keeps Wi-Fi */
                        app_sync_wifi_pending();
         if (err == ESP_OK) {
@@ -684,6 +691,7 @@ static void app_task(void *arg)
             app_config_tick();
             app_sync_wifi_check();
             app_ui_toast_expire();
+            app_radar_loop_tick();
             bool busy = pending || app_menu_is_open() || app_ui_toast_active();
             if (!busy && app_ui_night() && mono >= s_peek_until_ms) {
                 enter_night_sleep(); /* returns only if it had to sleep light instead */
@@ -723,7 +731,7 @@ static void app_task(void *arg)
             int64_t mono = app_uptime_ms();
             const int64_t deadlines[] = { app_menu_deadline_ms(), app_ui_toast_until_ms(),
                                           app_ui_night() ? s_peek_until_ms : 0, app_config_redraw_ms(),
-                                          s_ota_pending ? OTA_VERIFY_MS : 0 };
+                                          s_ota_pending ? OTA_VERIFY_MS : 0, app_radar_loop_deadline_ms() };
             for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++) {
                 if (deadlines[i] != 0 && deadlines[i] - mono < wait_ms) {
                     wait_ms = deadlines[i] - mono;
```


- [ ] **Step 2: The console and the API.**

`main/app_cmds.c`:

```diff
--- a/main/app_cmds.c
+++ b/main/app_cmds.c
@@ -356,6 +356,45 @@ static int cmd_sync(int argc, char **argv)
     return diag_on_owner(sync_body, argc, argv);
 }
 
+/* `radar status | loop` (spec §15, M6). */
+static int radar_body(int argc, char **argv)
+{
+    static const char *const k_usage = "radar status | radar loop";
+    if (argc == 2 && strcmp(argv[1], "status") == 0) {
+        app_radar_status_t r;
+        app_radar_status(&r);
+        printf("weather: %s, %u frame%s kept\n", r.source == RADAR_SOURCE_CHMU ? "ChMU" : "RainViewer", r.frames,
+               r.frames == 1 ? "" : "s");
+        print_time("  newest", (time_t)r.frame_at);
+        print_time("  fetched", r.fetched_at);
+        if (r.fetched_at != 0 && !r.ok) {
+            printf("  failed: %s\n", r.detail);
+        }
+        app_flights_status_t f;
+        app_flights_status(&f);
+        printf("flights: %s, %u aircraft%s\n", f.on ? "polling" : "off", f.aircraft,
+               f.failed ? ", the last poll failed" : "");
+        print_time("  updated", f.updated);
+        return 0;
+    }
+    if (argc == 2 && strcmp(argv[1], "loop") == 0) {
+        const ui_presets_t *p = app_presets();
+        if (p->presets[p->active].layout != UI_LAYOUT_RADAR) {
+            printf("radar: the loop plays on the Radar layout\n");
+            return 1;
+        }
+        bool ok = app_radar_loop_start();
+        printf("radar: %s\n", ok ? "the loop plays" : "fewer than two frames: sync mode always keeps the hour");
+        return ok ? 0 : 1;
+    }
+    return usage(k_usage);
+}
+
+static int cmd_radar(int argc, char **argv)
+{
+    return diag_on_owner(radar_body, argc, argv);
+}
+
 void app_register_commands(void)
 {
     const esp_console_cmd_t cmds[] = {
@@ -366,6 +405,7 @@ void app_register_commands(void)
                                          "add <HH:MM> night <HH:MM> [days]", .func = &cmd_schedule },
         { .command = "wifi", .help = "wifi status | scan", .func = &cmd_wifi },
         { .command = "sync", .help = "sync now | status (spec §9.3)", .func = &cmd_sync },
+        { .command = "radar", .help = "radar status | loop (spec §11.2, §11.3)", .func = &cmd_radar },
     };
     for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
         esp_err_t err = esp_console_cmd_register(&cmds[i]);
```


`main/app_web.c`:

```diff
--- a/main/app_web.c
+++ b/main/app_web.c
@@ -198,6 +198,32 @@ static void get_status(uint8_t *out, size_t size, webui_reply_t *reply)
         cJSON_AddNumberToObject(rtc, "drift_s_per_day", timekeeping_trim_drift_s10_per_day(trim) / 10.0);
     }
 
+    /* spec §10.3, M6: the weather radar's frames and the flight radar's polls */
+    app_radar_status_t rs;
+    app_radar_status(&rs);
+    cJSON *radar = cJSON_AddObjectToObject(o, "radar");
+    cJSON *weather = cJSON_AddObjectToObject(radar, "weather");
+    cJSON_AddStringToObject(weather, "source", rs.source == RADAR_SOURCE_CHMU ? "chmu" : "rainviewer");
+    cJSON_AddNumberToObject(weather, "frames", rs.frames);
+    if (rs.frame_at != 0) {
+        cJSON_AddNumberToObject(weather, "frame_at", rs.frame_at);
+    }
+    if (rs.fetched_at != 0) {
+        cJSON_AddNumberToObject(weather, "fetched_at", (double)rs.fetched_at);
+        if (!rs.ok) {
+            cJSON_AddStringToObject(weather, "error", rs.detail);
+        }
+    }
+    app_flights_status_t fs;
+    app_flights_status(&fs);
+    cJSON *flights = cJSON_AddObjectToObject(radar, "flights");
+    cJSON_AddBoolToObject(flights, "on", fs.on);
+    cJSON_AddNumberToObject(flights, "aircraft", fs.aircraft);
+    if (fs.updated != 0) {
+        cJSON_AddNumberToObject(flights, "updated", (double)fs.updated);
+    }
+    cJSON_AddBoolToObject(flights, "failed", fs.failed);
+
     const ui_preset_t *active = &st->presets.presets[st->presets.active];
     cJSON *preset = cJSON_AddObjectToObject(o, "preset");
     cJSON_AddStringToObject(preset, "active", active->id);
@@ -259,6 +285,7 @@ static void preview(const char *method, const char *query, const char *body, uin
     }
     gfx_fb_t *fb = preview_fb();
     if (fb != NULL) {
+        app_radar_prepare(&doc.presets[index]); /* its map, and the frame from its file if PSRAM has none */
         ui_context_t ctx;
         app_ui_context(&ctx);
         ui_draw_dashboard(fb, &ctx, &doc.presets[index]);
```


- [ ] **Step 3: Build.** `tools/idf.sh build 2>&1 | grep -E 'warning:|error:'`: nothing. The host suite is unchanged: `out of 60`, all passing.

- [ ] **Step 4: AGENTS.md, and commit.** The command `radar status` joins §6, and `radar status|loop` the console list in §7 (AGENTS.md changes in the same commit, §8):

`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -291,6 +291,7 @@ tools/idf.sh exec python tools/devlog.py --cmd "btn boot long"   # config mode:
 tools/idf.sh exec python tools/devlog.py --cmd "wifi status" --cmd "wifi scan"
 tools/idf.sh exec python tools/devlog.py --cmd "battery learn start"   # learn the curve from the next full discharge (D21)
 tools/idf.sh exec python tools/devlog.py --cmd "sync now" -t 40 --until "app_sync: sync (done|failed)"   # then: sync status, rtc get
+tools/idf.sh exec python tools/devlog.py --cmd "radar status"   # the weather radar's frames and the flight radar's polls
 python3 tools/gen_zones.py                 # regenerate web/zones.js from this Mac's tz database
 tools/idf.sh exec python tools/screenshot.py -o captures/screen.png --compare test/host/golden/test_pattern.pbm
 cmake -S test/host -B build-host -G Ninja && cmake --build build-host \
@@ -337,7 +338,7 @@ Use the cheapest level that proves the change. Any UI change needs at least leve
 
 **Screenshots**: the `screenshot` console command prints the canonical framebuffer as base64 PBM between `-----BEGIN RLCD PBM-----` and `-----END RLCD PBM-----`. `tools/screenshot.py` turns that into a PNG using only pyserial and the standard library. In config mode the web UI serves `/api/screenshot.bmp`, and `/api/preview.bmp` renders any preset with live data. A screenshot shows what the firmware drew, not what the panel shows, because the ST7305 is write-only. After any display-driver change, have the owner confirm the test pattern.
 
-**Diagnostics console** (`diag`; full list in spec §15). Available now: `help`, `version`, `heap`, `reboot`, `screenshot`, `panel status|test|clear|mode <hpm|lpm>|rate <0.25|0.5|1|2|4|8>|fps [s]|sleep|wake|init <factory|xiaozhi>`, `btn <key|boot> <short|double|long>` (simulated presses), `sensors`, `battery [learn start|stop]`, `rtc get|set <ISO 8601>`, `tasks`, `power idle [deep|light]`, `sleep stats [reset]|test <deep|light> <n>`, `field list|get <id>|set <id> <value>|clear <id>`, `preset list|set <id>`, `night <minutes>`, `schedule list|on|off|clear|add <HH:MM> preset <id> [days]|add <HH:MM> night <HH:MM> [days]`, `wifi status|scan`, `sync now|status`; `rtc get` also prints the trim and the last drift. Planned: `audio tone`. Drive the UI, the menu included, with `btn` and `screenshot` instead of asking the owner to press buttons. A toast lasts 3 s, less than two port sessions take, so send `--cmd "btn key short" --cmd screenshot` in one devlog call and decode the log with `screenshot.extract_pbm()`. A `btn` gesture reaches the app through the buttons task, so a command in the same devlog call can run before it has taken effect: check its result in a separate call. Inject test data with `field set`. Run commands with `tools/idf.sh exec python tools/devlog.py --cmd <command>`. The console runs in plain line mode on purpose: no history, arrow keys or tab completion, even in a terminal. It never sends escape-code queries that a script can't answer (spec §15, `components/diag/diag.c`).
+**Diagnostics console** (`diag`; full list in spec §15). Available now: `help`, `version`, `heap`, `reboot`, `screenshot`, `panel status|test|clear|mode <hpm|lpm>|rate <0.25|0.5|1|2|4|8>|fps [s]|sleep|wake|init <factory|xiaozhi>`, `btn <key|boot> <short|double|long>` (simulated presses), `sensors`, `battery [learn start|stop]`, `rtc get|set <ISO 8601>`, `tasks`, `power idle [deep|light]`, `sleep stats [reset]|test <deep|light> <n>`, `field list|get <id>|set <id> <value>|clear <id>`, `preset list|set <id>`, `night <minutes>`, `schedule list|on|off|clear|add <HH:MM> preset <id> [days]|add <HH:MM> night <HH:MM> [days]`, `wifi status|scan`, `sync now|status`, `radar status|loop`; `rtc get` also prints the trim and the last drift. Planned: `audio tone`. Drive the UI, the menu included, with `btn` and `screenshot` instead of asking the owner to press buttons. A toast lasts 3 s, less than two port sessions take, so send `--cmd "btn key short" --cmd screenshot` in one devlog call and decode the log with `screenshot.extract_pbm()`. A `btn` gesture reaches the app through the buttons task, so a command in the same devlog call can run before it has taken effect: check its result in a separate call. Inject test data with `field set`. Run commands with `tools/idf.sh exec python tools/devlog.py --cmd <command>`. The console runs in plain line mode on purpose: no history, arrow keys or tab completion, even in a terminal. It never sends escape-code queries that a script can't answer (spec §15, `components/diag/diag.c`).
 
 **Done** means: the acceptance criteria pass at the right level, new logic has tests, power-affecting changes have measurements in `docs/power.md`, and this file and `docs/` are updated.
 
```


```bash
git add main AGENTS.md
git commit -m "feat(app): the radar's loop, console, status and preview (D28)"
```


### Task 14: The Radar page (`web/`)

**Files:**
- Modify: `web/app.js`, `web/index.html`, `test/web/test_app.mjs`

**Interfaces:**
- Consumes: `GET/PATCH /api/settings` (`radar.*`, Task 8), `GET /api/status`'s `radar` block (Task 13), `POST /api/preview.bmp` (M4), `GET /api/geocode` (M5).
- Produces: the `#radar` page; `placeSearch(note, onPick)` shared by the Location page and both radars' centres (ruling); the Sync page's radar step; the preset editor's names for the radar layouts and a word where a layout has no slots.

The page (spec §10.3): a card for each radar, each with its preview (the device draws the layout as saved, refreshed after each save: ruling), its state, its centre (the location, or a place of its own from the place search or coordinates), and its credits. The weather radar's zoom is offered from 4 to 9 in quarters, each with the ground it spans across the map; the flight radar has its range, lowest altitude, aircraft on the ground and the most aircraft shown, and says it runs only in sync mode Always on, with a link to the Sync page, unless the mode is on.

- [ ] **Step 1: Write the failing tests.**

`test/web/test_app.mjs`:

```diff
--- a/test/web/test_app.mjs
+++ b/test/web/test_app.mjs
@@ -375,3 +375,120 @@ test('Done keeps the page when sync mode Always on keeps the network', async ()
   await byId.done.onclick();
   assert.doesNotMatch(text(byId.main), /Wi-Fi is off/);
 });
+
+/* ---- the Radar page (spec §10.3, M6) ---- */
+
+const RADAR_SETTINGS = { schema: 1, location: { name: 'Brno', lat: 49.1951, lon: 16.6068 }, sync: { mode: 'times' },
+                         radar: { weather: { zoom: 6.5 }, flights: { range_km: 50, min_alt_ft: 0, ground: false, max: 100 } } };
+const radarStatus = (radar) => ({ device: {}, time: { valid: true }, battery: {}, sensors: {}, preset: {}, sync: {}, radar });
+
+function radarDevice(settings, patches, previews) {
+  return {
+    'GET /api/settings': () => reply(200, settings),
+    'GET /api/status': () => reply(200, radarStatus({ weather: { source: 'chmu', frames: 1, frame_at: 1790880000 },
+                                                      flights: { on: false, aircraft: 0, failed: false } })),
+    'PATCH /api/settings': (init) => { patches.push(JSON.parse(init.body)); return reply(200, settings); },
+    'POST /api/preview.bmp': (init) => { previews.push(JSON.parse(init.body)); return reply(200, 'BM', 'image/bmp'); },
+  };
+}
+
+/* The radio buttons of a radar's card: a browser unchecks the other of a group by itself. */
+function pick(main, group, label) {
+  const radios = below(main).filter((e) => e.tag === 'input' && e.attrs.type === 'radio' && e.attrs.name === group);
+  const labels = below(main).filter((e) => e.tag === 'label' && e.children.some((k) => radios.includes(k)));
+  for (const l of labels) {
+    const r = l.children.find((k) => radios.includes(k));
+    r.checked = text(l).startsWith(label);
+    if (r.checked) (r.listeners.change || []).forEach((fn) => fn({ target: r }));
+  }
+}
+
+test('the Radar page saves the weather radar\'s own centre and zoom', async () => {
+  const patches = [], previews = [];
+  const { ctx, main } = await load(radarDevice(RADAR_SETTINGS, patches, previews));
+  await ctx.radarPage();
+  pick(main, 'wx-centre', 'A place of its own');
+  const numbers = below(main).filter((e) => e.tag === 'input' && e.attrs.type === 'number');
+  numbers[0].value = '50.0755'; /* the weather card's latitude and longitude come first */
+  numbers[1].value = '14.4378';
+  const zoom = below(main).find((e) => e.tag === 'select' && e.attrs.name === 'zoom');
+  zoom.value = '7.25';
+  await buttonNamed(main, 'Save weather radar').click();
+  assert.deepEqual(patches.at(-1), { radar: { weather: { lat: 50.0755, lon: 14.4378, zoom: 7.25 } } });
+});
+
+test('the location again clears a radar\'s own centre', async () => {
+  const patches = [], previews = [];
+  const settings = { ...RADAR_SETTINGS, radar: { weather: { lat: 50.0755, lon: 14.4378, zoom: 7 }, flights: {} } };
+  const { ctx, main } = await load(radarDevice(settings, patches, previews));
+  await ctx.radarPage();
+  pick(main, 'wx-centre', 'The location');
+  await buttonNamed(main, 'Save weather radar').click();
+  assert.deepEqual(patches.at(-1), { radar: { weather: { lat: null, lon: null, zoom: 7 } } }); /* RFC 7396 removes them */
+});
+
+test('the flight radar\'s filters are saved within their bounds', async () => {
+  const patches = [], previews = [];
+  const { ctx, main } = await load(radarDevice(RADAR_SETTINGS, patches, previews));
+  await ctx.radarPage();
+  const range = below(main).find((e) => e.tag === 'select' && e.attrs.name === 'range');
+  range.value = '25';
+  const byName = (n) => below(main).find((e) => e.tag === 'input' && e.attrs.name === n);
+  byName('min_alt_ft').value = '3000';
+  byName('ground').checked = true;
+  byName('max').value = '0';
+  await buttonNamed(main, 'Save flight radar').click();
+  assert.equal(patches.length, 0);
+  assert.match(text(main), /Aircraft at most: 1 to 100/);
+  byName('max').value = '40';
+  await buttonNamed(main, 'Save flight radar').click();
+  assert.deepEqual(patches.at(-1), { radar: { flights: { lat: null, lon: null, range_km: 25, min_alt_ft: 3000,
+                                                         ground: true, max: 40 } } });
+});
+
+test('the flight radar says it runs only in sync mode Always on', async () => {
+  const { ctx, main } = await load(radarDevice(RADAR_SETTINGS, [], []));
+  await ctx.radarPage();
+  assert.match(text(main), /runs only in sync mode Always on/);
+  const always = { ...RADAR_SETTINGS, sync: { mode: 'always' } };
+  const again = await load(radarDevice(always, [], []));
+  await again.ctx.radarPage();
+  assert.doesNotMatch(text(again.main), /runs only in sync mode Always on/);
+});
+
+test('the Radar page previews both radars and credits their sources', async () => {
+  const previews = [];
+  const { ctx, main } = await load(radarDevice(RADAR_SETTINGS, [], previews));
+  await ctx.radarPage();
+  await settle();
+  assert.deepEqual(previews.map((d) => d.presets[0].layout).sort(), ['flights', 'radar']);
+  const all = text(main);
+  for (const credit of ['ČHMÚ', 'CC BY 4.0', 'RainViewer', 'adsb.fi', 'adsb.lol']) assert.ok(all.includes(credit), credit);
+  assert.match(all, /Newest frame/);
+});
+
+test('the Sync page shows the radar\'s step', async () => {
+  const last = { at: 1790880000, steps: { wifi: 'ok', time: 'ok', weather: 'ok', air: 'ok', radar: 'failed' },
+                 failed: 'radar', detail: 'HTTP 503' };
+  const { ctx, main } = await load({
+    'GET /api/settings': () => reply(200, SYNC_SETTINGS),
+    'GET /api/status': () => reply(200, syncStatus({ mode: 'times', running: false, last })),
+  });
+  await ctx.syncPage();
+  assert.match(text(main), /Radar failed \(HTTP 503\)/);
+  assert.match(text(main), /Radarfailed: HTTP 503/); /* the steps' list: its name, then its result */
+});
+
+test('a preset on a radar layout has no slots to fill', async () => {
+  const catalogue = { ...CATALOGUE, layouts: [...CATALOGUE.layouts, { id: 'radar', slots: [] }, { id: 'flights', slots: [] }] };
+  const doc = { schema: 1, active: 'rain', presets: [{ id: 'rain', name: 'Rain radar', layout: 'radar', in_cycle: true,
+                                                         slots: {}, options: {} }],
+                cycle: { enabled: false, interval_s: 60 }, schedule: { enabled: false, entries: [] } };
+  const { ctx, main } = await load({
+    'GET /api/layouts': () => reply(200, catalogue), 'GET /api/presets': () => reply(200, doc),
+    'GET /api/fields': () => reply(200, FIELDS), 'POST /api/preview.bmp': () => reply(200, 'BM', 'image/bmp'),
+  });
+  await ctx.presetsPage();
+  assert.match(text(main), /Rain radar Radar/); /* the list names its layout */
+  assert.match(text(main), /This layout draws the weather radar/);
+});
```


- [ ] **Step 2: Run them to see them fail.**

Run: `node --test test/web/test_app.mjs 2>&1 | grep -E '^# (pass|fail)'`
Expected: `# pass 17`, `# fail 7`.

- [ ] **Step 3: The page.**

`web/app.js`:

```diff
--- a/web/app.js
+++ b/web/app.js
@@ -223,8 +223,8 @@ document.getElementById('done').onclick = async () => {
 
 /* ---- pages ---- */
 
-const pages = { status: statusPage, wifi: wifiPage, place: placePage, sync: syncPage, device: devicePage,
-                presets: presetsPage, firmware: firmwarePage, backup: backupPage };
+const pages = { status: statusPage, wifi: wifiPage, place: placePage, sync: syncPage, radar: radarPage,
+                device: devicePage, presets: presetsPage, firmware: firmwarePage, backup: backupPage };
 
 function route() {
   const name = location.hash.slice(1) || 'status';
@@ -306,7 +306,8 @@ async function statusPage() {
 
 /* ---- Sync (spec §9.3, D25) ---- */
 
-const SYNC_STEPS = [['wifi', 'Wi-Fi'], ['time', 'Time'], ['weather', 'Weather'], ['air', 'Air quality']];
+const SYNC_STEPS = [['wifi', 'Wi-Fi'], ['time', 'Time'], ['weather', 'Weather'], ['air', 'Air quality'],
+                    ['radar', 'Radar']];
 const SYNC_INTERVALS = [15, 30, 60, 120, 180, 360, 720, 1440];
 const intervalLabel = (m) => (m < 60 ? `${m} min` : `${m / 60} h`);
 
@@ -565,29 +566,38 @@ async function wifiPage() {
 
 /* ---- Location and time ---- */
 
-async function placePage() {
-  const s = await api('GET', '/api/settings');
-  const loc = s.location || {}, time = s.time || {};
-  const name = h('input', { type: 'text', maxlength: 31, value: loc.name || '' });
-  const lat = h('input', { type: 'number', step: 'any', min: -90, max: 90, value: loc.lat ?? '' });
-  const lon = h('input', { type: 'number', step: 'any', min: -180, max: 180, value: loc.lon ?? '' });
-  const locNote = h('p');
+/* A place by its name, through the device, which is online (spec §10.3): `onPick` gets the one tapped. */
+function placeSearch(note, onPick) {
   const query = h('input', { type: 'search', placeholder: 'A town or city' });
   const found = h('div');
-  const search = () => busy(locCard, locNote, async () => { /* spec §10.3: through the device, which is online */
+  const search = () => busy(box, note, async () => {
     if (!query.value.trim()) throw new ApiError('Type a place to look for.');
     const r = await api('GET', '/api/geocode?q=' + encodeURIComponent(query.value.trim()));
     found.replaceChildren(...(r.places.length ? r.places.map((pl) => button(
       `${pl.name}${pl.region ? ', ' + pl.region : ''}${pl.country ? ', ' + pl.country : ''}`, () => {
-        name.value = pl.name.slice(0, 31);
-        lat.value = String(pl.lat);
-        lon.value = String(pl.lon);
+        onPick(pl);
         found.replaceChildren(h('p', { class: 'muted small', text: `${pl.name}: ${pl.lat}, ${pl.lon}. Save it below.` }));
       })) : [h('p', { class: 'muted small', text: 'Nothing found by that name.' })]));
   });
+  const box = h('div', {}, h('div', { class: 'row' }, h('div', {}, field('Find a place', query)),
+    actions(button('Search', search))), found);
+  return box;
+}
+
+async function placePage() {
+  const s = await api('GET', '/api/settings');
+  const loc = s.location || {}, time = s.time || {};
+  const name = h('input', { type: 'text', maxlength: 31, value: loc.name || '' });
+  const lat = h('input', { type: 'number', step: 'any', min: -90, max: 90, value: loc.lat ?? '' });
+  const lon = h('input', { type: 'number', step: 'any', min: -180, max: 180, value: loc.lon ?? '' });
+  const locNote = h('p');
   const locCard = card('Location', h('p', { class: 'muted small', text: 'For sunrise, sunset and the weather. ' +
     'Decimal degrees: north and east are positive.' }),
-  h('div', { class: 'row' }, h('div', {}, field('Find a place', query)), actions(button('Search', search))), found,
+  placeSearch(locNote, (pl) => {
+    name.value = pl.name.slice(0, 31);
+    lat.value = String(pl.lat);
+    lon.value = String(pl.lon);
+  }),
   field('Name', name), h('div', { class: 'row' }, h('div', {}, field('Latitude', lat)), h('div', {}, field('Longitude', lon))),
   locNote, actions(button('Save location', () => busy(locCard, locNote, async () => {
     const la = Number(lat.value), lo = Number(lon.value);
@@ -636,6 +646,116 @@ async function placePage() {
   main.replaceChildren(h('h1', { text: 'Location and time' }), locCard, tzCard, clockCard);
 }
 
+/* ---- Radar (spec §10.3, §11.1-§11.3, D22, D23, D28) ---- */
+
+const ZOOMS = Array.from({ length: 21 }, (_, i) => 4 + i / 4); /* 4 to 9 in quarters (spec §14.3) */
+const RANGES = [10, 15, 20, 25, 30, 40, 50, 60, 75, 100];
+/* The ground the Radar layout's 400 px span at a zoom and latitude, in web Mercator (spec §11.1). */
+const acrossKm = (zoom, lat) => Math.round(400 * 156543.03 * Math.cos((lat || 0) * Math.PI / 180) / 2 ** zoom / 1000);
+
+/* A radar's centre (spec §11.1): the location, which it then follows, or a place of its own. */
+function centreEditor(group, settings, own, note) {
+  const loc = settings.location || {};
+  const ownSet = typeof (own || {}).lat === 'number' && typeof own.lon === 'number';
+  const lat = h('input', { type: 'number', step: 'any', min: -85, max: 85, value: ownSet ? own.lat : loc.lat ?? '' });
+  const lon = h('input', { type: 'number', step: 'any', min: -180, max: 180, value: ownSet ? own.lon : loc.lon ?? '' });
+  const where = h('div', {}, placeSearch(note, (pl) => { lat.value = String(pl.lat); lon.value = String(pl.lon); }),
+    h('div', { class: 'row' }, h('div', {}, field('Latitude', lat)), h('div', {}, field('Longitude', lon))));
+  where.hidden = !ownSet;
+  const atHome = h('input', { type: 'radio', name: group, checked: !ownSet, onchange: () => { where.hidden = true; } });
+  const elsewhere = h('input', { type: 'radio', name: group, checked: ownSet, onchange: () => { where.hidden = false; } });
+  return {
+    els: [h('label', {}, 'Centre'), h('label', { class: 'check' }, atHome, `The location${loc.name ? ` (${loc.name})` : ''}`),
+          h('label', { class: 'check' }, elsewhere, 'A place of its own'), where],
+    /* the patch's coordinates: null follows the location again, as a merge patch removes them */
+    value() {
+      if (atHome.checked) return { lat: null, lon: null };
+      const la = Number(lat.value), lo = Number(lon.value);
+      if (lat.value === '' || !(la >= -85 && la <= 85)) throw new ApiError('Latitude: -85 to 85.');
+      if (lon.value === '' || !(lo >= -180 && lo <= 180)) throw new ApiError('Longitude: -180 to 180.');
+      return { lat: la, lon: lo };
+    },
+  };
+}
+
+/* The device draws a radar's layout as saved (POST /api/preview.bmp): one preset on it. */
+async function radarPreview(img, layout) {
+  const doc = { schema: 1, active: 'preview', presets: [{ id: 'preview', name: 'Preview', layout,
+                                                          options: { status_clock: true } }] };
+  const bmp = await api('POST', '/api/preview.bmp?preset=preview', doc);
+  if (img.src && img.src.startsWith('blob:')) URL.revokeObjectURL(img.src);
+  img.src = URL.createObjectURL(bmp);
+}
+
+const link = (href, text) => h('a', { href, target: '_blank', rel: 'noopener' }, text);
+
+async function radarPage() {
+  const [s, st] = await Promise.all([api('GET', '/api/settings'), api('GET', '/api/status')]);
+  const radar = s.radar || {}, wx = radar.weather || {}, fl = radar.flights || {}, now = st.radar || {};
+  const always = (s.sync || {}).mode === 'always';
+  const lat = typeof wx.lat === 'number' ? wx.lat : (s.location || {}).lat;
+
+  const wxNote = h('p');
+  const wxCentre = centreEditor('wx-centre', s, wx, wxNote);
+  const zoom = h('select', { name: 'zoom' }, ZOOMS.map((z) => h('option', { value: String(z), selected: z === (wx.zoom ?? 6.5) },
+    `${z}: about ${acrossKm(z, lat)} km across`)));
+  zoom.value = String(ZOOMS.includes(wx.zoom) ? wx.zoom : 6.5);
+  const wxImg = h('img', { class: 'screen', alt: 'The Radar layout' });
+  const w = now.weather || {};
+  const wxCard = card('Weather radar', wxImg, facts([
+    ['Source', w.source === 'rainviewer' ? 'RainViewer, as the centre is outside ČHMÚ\'s area' : 'ČHMÚ'],
+    ['Newest frame', w.frame_at ? when(w.frame_at) : 'none yet'],
+    ['Frames kept', `${w.frames ?? 0}${always ? ', the last hour for the loop' : ''}`],
+    w.error ? ['Last fetch', `failed: ${w.error}`] : null,
+  ]),
+  h('p', { class: 'muted small', text: 'A frame comes with each sync, and every 5 minutes in sync mode Always on ' +
+    '(RainViewer: 10). Rain for a new centre or zoom comes with the next one. In sync mode Always on, BOOT on the ' +
+    'Radar layout plays the last hour.' }),
+  wxCentre.els, field('Zoom', zoom), wxNote,
+  actions(button('Save weather radar', () => busy(wxCard, wxNote, async () => {
+    await api('PATCH', '/api/settings', { radar: { weather: { ...wxCentre.value(), zoom: Number(zoom.value) } } });
+    toast('Weather radar saved');
+    await radarPreview(wxImg, 'radar');
+  }), 'primary')),
+  h('p', { class: 'muted small' }, 'Rain: ', link('https://opendata.chmi.cz', 'Data: ČHMÚ, opendata.chmi.cz'),
+    ', CC BY 4.0; outside its area ', link('https://www.rainviewer.com', 'RainViewer'), '.'));
+
+  const flNote = h('p');
+  const flCentre = centreEditor('fl-centre', s, fl, flNote);
+  const ranges = RANGES.includes(fl.range_km) || fl.range_km === undefined ? RANGES : [...RANGES, fl.range_km].sort((a, b) => a - b);
+  const range = h('select', { name: 'range' }, ranges.map((km) => h('option', { value: String(km), selected: km === (fl.range_km ?? 50) },
+    `${km} km`)));
+  range.value = String(fl.range_km ?? 50);
+  const minAlt = h('input', { type: 'number', name: 'min_alt_ft', min: 0, max: 60000, step: 100, value: fl.min_alt_ft ?? 0 });
+  const ground = h('input', { type: 'checkbox', name: 'ground', checked: !!fl.ground });
+  const max = h('input', { type: 'number', name: 'max', min: 1, max: 100, step: 1, value: fl.max ?? 100 });
+  const f = now.flights || {};
+  const flImg = h('img', { class: 'screen', alt: 'The Flights layout' });
+  const flCard = card('Flight radar',
+    always ? null : h('p', { class: 'bad small' }, 'It runs only in sync mode Always on, set on the ',
+      h('a', { href: '#sync' }, 'Sync'), ' page.'),
+    flImg, facts([['Now', f.on ? `${f.aircraft} aircraft${f.updated ? ' at ' + when(f.updated) : ''}`
+      : 'resting: it asks while the Flights view is on the screen']]),
+    h('p', { class: 'muted small', text: 'While the Flights view shows, it asks adsb.fi every 5 to 15 s, more often for ' +
+      'a smaller range, and the nearest aircraft\'s route comes from adsb.lol.' }),
+    flCentre.els, field('Range', range, 'From the centre to the map\'s top edge.'),
+    field('Lowest altitude (ft)', minAlt), h('label', { class: 'check' }, ground, 'Aircraft on the ground too'),
+    field('Aircraft at most', max), flNote,
+    actions(button('Save flight radar', () => busy(flCard, flNote, async () => {
+      const lo = Number(minAlt.value), mx = Number(max.value);
+      if (minAlt.value === '' || !(lo >= 0 && lo <= 60000)) throw new ApiError('Lowest altitude: 0 to 60000 ft.');
+      if (max.value === '' || !Number.isInteger(mx) || mx < 1 || mx > 100) throw new ApiError('Aircraft at most: 1 to 100.');
+      await api('PATCH', '/api/settings', { radar: { flights: { ...flCentre.value(), range_km: Number(range.value),
+        min_alt_ft: Math.round(lo), ground: ground.checked, max: mx } } });
+      toast('Flight radar saved');
+      await radarPreview(flImg, 'flights');
+    }), 'primary')),
+    h('p', { class: 'muted small' }, 'Aircraft: ', link('https://adsb.fi', 'adsb.fi'), '; routes: ',
+      link('https://adsb.lol', 'adsb.lol'), '.'));
+  main.replaceChildren(h('h1', { text: 'Radar' }), wxCard, flCard);
+  await Promise.all([radarPreview(wxImg, 'radar'), radarPreview(flImg, 'flights')]).catch(() => {});
+}
+
 /* ---- Device: the settings the menu also has (spec §5.7, D19) ---- */
 
 const LANGUAGES = [['en', 'English'], ['cs', 'Čeština']];
@@ -715,7 +835,10 @@ async function devicePage() {
 
 /* ---- Presets (spec §5.4) ---- */
 
-const LAYOUT_NAMES = { classic: 'Classic', weather: 'Weather', grid: 'Grid', focus: 'Focus' };
+const LAYOUT_NAMES = { classic: 'Classic', weather: 'Weather', grid: 'Grid', focus: 'Focus', radar: 'Radar',
+                       flights: 'Flights' };
+const NO_SLOTS = { radar: 'This layout draws the weather radar: its centre and zoom are on the Radar page.',
+                   flights: 'This layout draws the flight radar: its centre, range and filters are on the Radar page.' };
 const CYCLE_S = [10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600];
 const cycleLabel = (s) => (s < 60 ? `${s} s` : s < 3600 ? `${s / 60} min` : `${s / 3600} h`);
 const DAYS = ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']; /* bit 0 is Monday */
@@ -851,6 +974,7 @@ function renderPresets(ed) {
     } }), text);
   const editCard = card(`Edit ${p.name}`, previewBox(ed, layoutOf(p.layout)), ed.previewNote,
     field('Name', name), field('Layout', layout), slots,
+    NO_SLOTS[p.layout] ? h('p', { class: 'muted small', text: NO_SLOTS[p.layout] }) : null,
     field('Time format', clock), check('seconds', 'Show seconds'),
     o.seconds ? h('p', { class: 'bad small', text: 'Seconds wake the device every second: the battery lasts far less.' }) : null,
     check('invert', 'White on black'), field('Old or missing data', stale),
```


`web/index.html`:

```diff
--- a/web/index.html
+++ b/web/index.html
@@ -17,6 +17,7 @@
   <a href="#wifi">Wi-Fi</a>
   <a href="#place">Location &amp; time</a>
   <a href="#sync">Sync</a>
+  <a href="#radar">Radar</a>
   <a href="#device">Device</a>
   <a href="#presets">Presets</a>
   <a href="#firmware">Firmware</a>
```


- [ ] **Step 4: Run the tests.**

Run: `node --test test/web/test_app.mjs 2>&1 | grep -E '^# (pass|fail)'`, then `ctest --test-dir build-host`
Expected: `# pass 24`, `# fail 0`; `out of 60`, all passing. The firmware embeds the page: `tools/idf.sh build` clean.

- [ ] **Step 5: Commit.**

```bash
git add web test/web/test_app.mjs
git commit -m "feat(web): the Radar page"
```


### Task 15: On the board, and the docs as built

**Files:**
- Modify (only if the checks find something): whatever they point at, each fix with its own test where one can fail first.
- Modify: `docs/specs/2026-09-25-firmware-design.md` (r29, as built), `AGENTS.md`, `docs/power.md`, `README.md` (the data sources' credits)

**Needs the owner first:** the board plugged into this Mac. It has the home network saved (M5's checks) and no web password (reset at their end, with the owner's agreement). The API checks need a session: this task sets a temporary password over the device's own network and clears it again in Step 10, so the owner's first visit still chooses theirs. Ask, and wait.

Before anything else, confirm the port is this board (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'` shows `14:C1:9F:54:BB:94`), and note the settings the checks change, to restore them in Step 10: `tools/idf.sh exec python tools/devlog.py --cmd "sync status" --cmd "preset list" -o captures/m6-before.log`.

- [ ] **Step 1: Flash and boot.**

```bash
tools/idf.sh -p /dev/cu.usbmodemXXXX flash 2>&1 | grep -E 'MAC:|Hash of data verified'
tools/idf.sh exec python tools/devlog.py --cmd reboot --until "reflbo ready" -t 30 -o captures/m6-boot.log
grep -E 'offered the built-in|presets.json|E \(' captures/m6-boot.log
tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd heap
```

Expected: `MAC: 14:c1:9f:54:bb:94`; in the log `presets.json: offered the built-in presets it didn't have` (M5's file), no `E (` line; `preset list` ends with `rain` and `flights`; `internal free` above 100 000. (If the board sits in download mode after flashing, leave it as gotcha 22 says.)

- [ ] **Step 2: A sync with the radar.**

```bash
tools/idf.sh exec python tools/devlog.py --cmd "sync now" -t 30 -o captures/m6-sync.log
tools/idf.sh exec python tools/devlog.py --cmd "sync status" --cmd "radar status" --cmd "field get wx.rain2h"
tools/idf.sh exec python tools/devlog.py --cmd "preset set rain"
tools/idf.sh exec python tools/screenshot.py -o captures/m6-rain.png
```

Expected: in the log `ChMU: 1 new frame` and `done in … ms` under 15 000; `sync status` shows `radar ok`; `radar status` `weather: ChMU, 1 frame kept` with a newest frame 5–15 min old; `wx.rain2h` "Dry for 2 h", "Rain from …" or "Rain now · …". The screenshot: the Rain radar over Czechia with "HH:MM · ČHMÚ"; its rain where ČHMÚ's own viewer (`https://www.chmi.cz`, Radar) has it for that time.

Then the frame's name from the sync's own time: put the clock 2 hours back (`rtc set` with `date -u -v-2H +%Y-%m-%dT%H:%M:%SZ`) and `sync now` again. Expected: `ChMU: 1 new frame` again, not a 404 for a file 2 hours old; `the clock moved` about 7 200 000 ms; `rtc get` right afterwards within a second of this Mac's clock.

- [ ] **Step 3: The frame keeps.** `reboot`, then a screenshot once `reflbo ready`: the same frame, and the log's `frame of … from /fs/state/radar.bin`. Then `power idle deep` and `sleep test deep 2`, and a screenshot after: the frame again, read from its file after each wake; `power idle light` afterwards.

- [ ] **Step 4: The web UI, and RainViewer.** Join the device's network (`btn boot long`; the password is on the screen; `networksetup -setairportnetwork en0 reflbo-bb94 <password>`), set a temporary web password and log in:

```bash
PW=$(openssl rand -hex 8) # a throwaway, never written down; Step 10 clears it
curl -s -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/setup
curl -s -c jar -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/login
curl -s -b jar http://192.168.4.1/api/status | python3 -m json.tool | grep -A12 '"radar"'
```

Expected: the `radar` block with `"source": "chmu"`, `"frames": 1`, a `frame_at`. Headless Chrome on `http://192.168.4.1/#radar` (AGENTS §6): both cards with their previews, the zoom's list, the flight radar's note that it needs sync mode Always on, the credits. Then a centre outside ČHMÚ's data:

```bash
curl -s -b jar -X PATCH -H 'Content-Type: application/json' -d '{"radar":{"weather":{"lat":52.52,"lon":13.405}}}' http://192.168.4.1/api/settings
curl -s -b jar -X POST -H 'Content-Type: application/json' http://192.168.4.1/api/sync
```

Expected: `radar status` `weather: RainViewer, 1 frame kept` once the sync is done; a screenshot of Berlin with "HH:MM · RainViewer". Put the centre back on the location: `-d '{"radar":{"weather":{"lat":null,"lon":null}}}'`; `radar status` then has no frame until the next sync (the view moved).

- [ ] **Step 5: Sync mode `always`, the hour and the loop.** `PATCH {"sync":{"mode":"always"}}`; `btn boot long` ends config mode, and Wi-Fi stays on the home network. Within a minute the log has `radar refresh: … frames kept`, and `radar status` then shows 12 frames. Then:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "preset set rain" --cmd "btn boot short" --cmd screenshot --cmd "panel status" -o captures/m6-loop.log
tools/idf.sh exec python tools/devlog.py --cmd "panel status"
```

Expected: the screenshot (decoded with `screenshot.extract_pbm()`) shows a loop frame, its time alone and a row of 12 dots; the first `panel status` `mode hpm`, the second, a few seconds later, `mode lpm`. Over the next 20 minutes the log has a refresh a minute after each 5-minute step, each adding one frame, and no `fetch: GET` warning: ČHMÚ closes each connection, and each GET opens its own.

A sync asked for during a refresh: with a capture running, `sync now` as soon as `radar refresh:` appears. Expected: `sync now` answers that the sync started; the status bar's sync mark at once; `sync starts (asked for)` once the refresh's report is in, then `sync done`; `curl -s -b jar http://reflbo-bb94.local/api/status` meanwhile has `"running": true`.

- [ ] **Step 6: The flight radar.**

```bash
tools/idf.sh exec python tools/devlog.py --cmd "preset set flights" -t 25 -o captures/m6-flights.log
tools/idf.sh exec python tools/devlog.py --cmd "radar status" --cmd heap
tools/idf.sh exec python tools/screenshot.py -o captures/m6-flights.png
tools/idf.sh exec python tools/devlog.py --cmd "preset set home" -t 3
```

Expected: `adsb: polling`, then a poll every 10 s at the default 50 km; `radar status` `flights: polling, N aircraft`; the screenshot: aircraft over the map, the nearest ringed, the panel with its callsign, type, altitude and speed, and its route once `route of … known` appears in the log; `btn key long` opens the menu and the log has `adsb: stopped`, and once the menu closes by itself (60 s) `adsb: polling` again; after `preset set home`, `adsb: stopped`. The heap while it polls: `internal … min` above 40 000. With the log still running, `sync now`: `done in … ms` within a second or two of Step 2's (TLS in PSRAM).

A centre with no aircraft: `PATCH {"radar":{"flights":{"lat":0,"lon":-140}}}`, `preset set flights`: "No aircraft within 50 km". Put it back with `"lat":null,"lon":null`.

- [ ] **Step 7: Quiet hours.** In sync mode `always` with the Flights view on, quiet hours over the next minutes (`PATCH {"sync":{"quiet":{"enabled":true,"from":"<now+2 min>","to":"<now+5 min>"}}}`). Expected: at the start `adsb: stopped`, `Wi-Fi off: nothing needs it`, the panel's "No aircraft data (HH:MM)"; at the end Wi-Fi back, a sync, `adsb: polling`, and a radar refresh within a minute.

- [ ] **Step 8: The panel itself** (the owner, at the board): the rain's three patterns, the labels over rain, the arrows and the panel's text are legible on the reflective panel in daylight and under a lamp.

- [ ] **Step 9: The Sync page's radar step.** Headless Chrome on `http://reflbo-bb94.local/#sync` (logged in on the LAN): the steps list ends with Radar.

- [ ] **Step 10: Back to the start.** Sync mode, quiet hours and the radars' settings as `captures/m6-before.log` had them (the offered presets stay: they are the owner's now); then clear the temporary web password: Menu ▸ Wi-Fi ▸ Reset web password. `btn key long` opens the menu; `btn key short` steps to the next item and `btn key long` enters it; the confirmation asks for KEY held. A screenshot after each press shows where the menu is. Last, `networksetup -removepreferredwirelessnetwork en0 reflbo-bb94` and `rm jar`.

- [ ] **Step 11: Write down what was built.**
  - Spec r29: §3.1 (`png`, `map`, `radar`, `adsb`, `fetch` and their host parts), §5.1 and §5.2 as built (the strip's three heights, the Flights panel's words), §5.4 (the "offered" marker), §6 (the snapshot's size with the rain), §9.3 (the refresh as built, a minute after each step), §11.1–§11.4 as built with the plan's rulings (zlib on the host, the 1:50 m coasts, the data area only, labels on boxes, `2/0_0.png`, `/fs/state/radar.bin` and its 30 minutes in `always`, frames kept while they cover the view, ČHMÚ's names from the sync's NTP time, a sync on demand after a refresh, the map's CRC on the host only), §14.3 (`radar.bin`), §15 (`radar status|loop`), §17 (the new tests), §20 (anything Steps 1–9 found), §21.
  - `AGENTS.md`: the status (M6 built), §5.2's components (`png`, `map`, `radar`, `adsb`, `fetch`), the console list (`radar status|loop`), §6's commands (`tools/gen_map.sh`, `radar status`), new gotchas from the work (at least: the linker leaves an embedded blob out until code reads it, so the image grows only then; `util_snapshot` blocks are at most 64 KB; `cJSON_ArrayForEach` takes a plain pointer, not an expression; TLS allocates in PSRAM; ČHMÚ closes every connection, and a GET on a socket the server closed fails in `esp_http_client_fetch_headers()` with the status at -1; each Open-Meteo `minutely_15` amount is the 15 minutes before its time), D27 and D28 already there.
  - `README.md`: the data sources and their credits (ČHMÚ, RainViewer, adsb.fi, adsb.lol, Natural Earth, OurAirports).
  - `docs/power.md`: an M6 section with the radar step's time from Step 2's log, and the rows the owner's measurements (below) fill in.

- [ ] **Step 12: Commit and push.**

```bash
git add docs AGENTS.md README.md
git commit -m "docs: record M6 as built (spec r29)"
git push origin main
```

## Owner acceptance

M6 is done (spec §18) once the owner has checked, with the expected results:

1. **The goldens** of both radars and the new widgets (`python3 tools/render.py`, `captures/render/dash_radar*.png`, `dash_flights*.png`, `dash_*rain*.png`): as wanted, or with the changes asked for.
2. **A ČHMÚ frame after a sync** (Step 2) on the panel: rain where ČHMÚ shows it.
3. **The loop in sync mode `always`:** BOOT on the Rain radar plays the last hour, then stops on the newest.
4. **Aircraft from adsb.fi in sync mode `always`:** the Flights preset shows them, with the nearest one's route.
5. **Both on the panel** (Step 8): legible.
6. **Power** (spec §9.4, the USB meter, the battery full or out):
   - a radar frame per sync: the mAh over 10 syncs on demand, two minutes apart, against M5's syncs without it;
   - the flight radar in sync mode `always`: an hour with the Flights view, against an hour of `always` with the clock.
   The results go in `docs/power.md` with the date, the commit and the settings.

Still open from earlier milestones, offered when the owner is at the board: M5's trim over days, sync on battery and router-off checks (memory `m5-deferred-owner-checks`), M3's night-sleep current and night peek, M4's update from the page and first run.
