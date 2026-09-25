# M1: Display, gfx and Screenshots, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw into a 1-bpp framebuffer, show it on the ST7305 panel, and let an agent capture exactly what was drawn over USB as a PNG. The owner confirms orientation and contrast, and picks between the two vendor init sequences.

**Architecture:**

- **`gfx`** (host-buildable): framebuffer, primitives, UTF-8 text with generated bitmap fonts, and PBM output.
- **`st7305`**:
  - `st7305_frame`: a pure-C frame conversion into the panel's 2×4-pixel byte layout, host-tested against the vendor formula.
  - `st7305.c`: the device-side SPI driver.
- **`display`**: owns the canonical framebuffer and pushes it only when its CRC changes.
- **`diag`**: gains `screenshot` (base64 PBM between markers) and `panel` commands.
- **Tools:**
  - `tools/screenshot.py` drives the console through `devlog.run()` and writes a PNG.
  - `tools/fontgen.py` turns DejaVu TTFs into C font tables.
  - `tools/render.py` renders the same test pattern on the host.
- **Golden test:** the host rendering is kept as a golden file. The device screenshot must match it byte for byte.

**Tech stack:** ESP-IDF v5.5.5 (C17, `esp_lcd` SPI panel IO), Unity v2.6.1, Python 3 stdlib, Pillow 12.3.0 and fontTools 4.66.0 pinned in `tools/requirements.txt` and run through `uv run --with-requirements` (font generation only; spec §15's "tools venv" is the one uv creates).

**Spec:** `docs/specs/2026-09-25-firmware-design.md`. Relevant sections: §4.1–§4.4, §15, §17, the M1 row of §18, and the M1 row of §19. Also `AGENTS.md` §3.4 (gotchas 4–6, 14, 15) and §6–§8.

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, Python 3 host tools.
- `[host]` components include no ESP-IDF headers. That covers `gfx`, `util` and `st7305_frame.c`.
- ESP-IDF style: 4-space indent and `snake_case`. Public APIs carry a component prefix and live in `include/`.
- Return `esp_err_t` and use `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`. No `ESP_ERROR_CHECK` on recoverable paths.
- Large buffers go in PSRAM (`MALLOC_CAP_SPIRAM`), DMA buffers in internal RAM. No allocation inside render hot loops.
- One `TAG` per module. Every task gets an explicit stack size, priority and core.
- Canonical frame: row-major 1 bpp, MSB first, 1 = black (PBM P4). The panel RAM uses 2×4-pixel blocks per byte, 1 = white (AGENTS gotcha 5).
- Fonts, icons and other assets need licences that allow redistribution in this repository. Generated files are never edited by hand.
- Board commands go through `tools/idf.sh` with an explicit `-p`. Never run `idf.py monitor` from an agent shell. Never erase flash or NVS without asking.
- Small, focused Conventional Commits that each build. No AI or assistant attribution anywhere. Pushing to `origin` is allowed; never force-push `main`.
- Features beyond the spec are proposals for the owner (AGENTS quick rule 5). Portrait orientation (spec §19, M1) is not built.

## Review Focus

1. **Text containing characters the font lacks**, such as emoji or CJK in future Home Assistant names. It must draw a hollow box with a fixed advance, never crash or skip silently. Pinned by `test_missing_glyph_draws_a_hollow_box` and `test_missing_glyph_uses_box_advance` (Task 2).
2. **Malformed or truncated UTF-8**, as from a cut-off MQTT payload. It must become U+FFFD and must never read past the terminating NUL. Pinned by `test_malformed_utf8_becomes_replacement_and_stops_at_nul` (Task 2).
3. **Geometry outside the framebuffer**: negative coordinates, huge rectangles, zero-size clips. It must be clipped, with no writes outside the buffer. Pinned by the guard-byte tests `test_drawing_outside_the_framebuffer_changes_nothing` and `test_huge_rectangles_are_clipped_to_the_framebuffer` (Task 1).
4. **A log line breaking into the screenshot's base64 stream.** The tool must fail loudly (exit 6) and never write a wrong image. Pinned by `test_noise_inside_the_image_is_rejected` (Task 8).
5. **Panel and screenshot disagreeing** because of a byte-layout bug. Pinned by `test_random_frame_matches_the_vendor_formula` (Task 5) and the owner's physical check (Task 9).

## File map

| Path | Responsibility | Task |
|---|---|---|
| `components/gfx/{CMakeLists.txt,include/gfx.h,gfx.c,gfx_pbm.c}` | Framebuffer, clip, primitives, PBM encoding | 1 |
| `components/gfx/{include/gfx_font.h,gfx_text.c}` | Font types, UTF-8, glyph lookup, text drawing | 2 |
| `tools/fontgen.py`, `tools/gen_fonts.sh`, `assets/fonts/*`, `components/gfx/fonts/*.c`, `components/gfx/include/gfx_fonts.h` | Font pipeline and the first four fonts | 3 |
| `components/util/{include/util_base64.h,util_base64.c}` | Base64 for the screenshot stream | 4 |
| `components/st7305/{CMakeLists.txt,include/st7305_frame.h,st7305_frame.c}` | Canonical → panel byte layout | 5 |
| `components/gfx/{include/gfx_test_pattern.h,gfx_test_pattern.c}`, `test/host/render_test_pattern.c`, `test/host/golden/test_pattern.pbm`, `tools/pbm_png.py`, `tools/render.py` | Test pattern, host rendering, golden image | 6 |
| `components/st7305/{include/st7305.h,st7305.c}`, `components/display/*`, `main/*` | SPI driver, display service, boot shows the pattern | 7 |
| `components/diag/diag_cmd_display.c`, `tools/screenshot.py` | `screenshot` and `panel` commands, PNG capture | 8 |
| `AGENTS.md`, spec, `THIRD_PARTY.md` | Kept current per task; decisions recorded | 3, 6–9 |
| `test/host/*` | One test file per unit | 1–6 |

---

### Task 1: `gfx` framebuffer, primitives and PBM

**Files:**
- Create: `components/gfx/CMakeLists.txt`, `components/gfx/include/gfx.h`, `components/gfx/gfx.c`, `components/gfx/gfx_pbm.c`, `test/host/test_gfx.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Produces, in `gfx.h`:
  - Types: `gfx_color_t` (`GFX_WHITE`, `GFX_BLACK`, `GFX_INVERT`), `gfx_rect_t {int16_t x, y, w, h}`, `gfx_fb_t {uint8_t *buf; int16_t width, height, stride; gfx_rect_t clip}`.
  - Setup: `gfx_fb_size`, `gfx_fb_init`, `gfx_clear`, `gfx_rect_intersect`, `gfx_set_clip`, `gfx_reset_clip`.
  - Drawing: `gfx_pixel`, `gfx_get_pixel`, `gfx_hline`, `gfx_vline`, `gfx_line`, `gfx_rect`, `gfx_fill_rect`.
  - PBM: `gfx_pbm_size`, `gfx_pbm_encode`.
- Host CMake produces the `gfx` library, built from every `components/gfx/*.c` and `components/gfx/fonts/*.c` (glob with `CONFIGURE_DEPENDS`).

- [ ] **Step 1: Write the failing tests `test/host/test_gfx.c`**

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
    return UNITY_END();
}
```

- [ ] **Step 2: Register it in `test/host/CMakeLists.txt`**

Insert after the `util` library block:

```cmake
# gfx: every source in the component, generated fonts included.
file(GLOB GFX_SOURCES CONFIGURE_DEPENDS
     ${REPO_ROOT}/components/gfx/*.c ${REPO_ROOT}/components/gfx/fonts/*.c)
add_library(gfx STATIC ${GFX_SOURCES})
target_include_directories(gfx PUBLIC ${REPO_ROOT}/components/gfx/include)
target_compile_options(gfx PRIVATE ${REFLBO_WARNINGS})
```

and after `reflbo_host_test(test_util_crc32 util)`:

```cmake
reflbo_host_test(test_gfx gfx)
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL. Either CMake reports no sources for `gfx`, or the compiler reports `'gfx.h' file not found`.

- [ ] **Step 4: Write `components/gfx/include/gfx.h`**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* PBM P4 image of the framebuffer: header "P4\n<w> <h>\n" followed by the raster. */
size_t gfx_pbm_size(const gfx_fb_t *fb);
size_t gfx_pbm_encode(const gfx_fb_t *fb, uint8_t *out, size_t out_size); /* 0 if out_size is too small */
```

- [ ] **Step 5: Write `components/gfx/gfx.c`**

```c
#include "gfx.h"

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

void gfx_line(gfx_fb_t *fb, int x0, int y0, int x1, int y1, gfx_color_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0; /* negative */
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(fb, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
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
```

- [ ] **Step 6: Write `components/gfx/gfx_pbm.c` and `components/gfx/CMakeLists.txt`**

```c
#include <stdio.h>
#include <string.h>

#include "gfx.h"

static int pbm_header(const gfx_fb_t *fb, char *out, size_t size)
{
    return snprintf(out, size, "P4\n%d %d\n", fb->width, fb->height);
}

size_t gfx_pbm_size(const gfx_fb_t *fb)
{
    char header[32];
    return (size_t)pbm_header(fb, header, sizeof(header)) + gfx_fb_size(fb->width, fb->height);
}

size_t gfx_pbm_encode(const gfx_fb_t *fb, uint8_t *out, size_t out_size)
{
    char header[32];
    int n = pbm_header(fb, header, sizeof(header));
    size_t raster = gfx_fb_size(fb->width, fb->height);
    if (n <= 0 || out_size < (size_t)n + raster) {
        return 0;
    }
    memcpy(out, header, (size_t)n);
    memcpy(out + n, fb->buf, raster);
    return (size_t)n + raster;
}
```

```cmake
# 1-bpp drawing and fonts (spec §4.3–4.4). Pure C: also built on the host by test/host.
idf_component_register(SRC_DIRS "." "fonts"
                       INCLUDE_DIRS "include")
```

Also create an empty `components/gfx/fonts/` directory with a `.gitkeep`, so `SRC_DIRS "fonts"` exists before Task 3.

- [ ] **Step 7: Run the tests and confirm they pass**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 3`, with no compiler warnings.

- [ ] **Step 8: Commit and push**

```bash
git add components/gfx test/host/test_gfx.c test/host/CMakeLists.txt
git commit -m "feat(gfx): add 1-bpp framebuffer, primitives and PBM encoding"
git push
```

---

### Task 2: `gfx` text: fonts, UTF-8, drawing

**Files:**
- Create: `components/gfx/include/gfx_font.h`, `components/gfx/gfx_text.c`, `test/host/test_gfx_text.c`
- Modify: `components/gfx/include/gfx.h` (append the text API), `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `gfx_fb_t`, `gfx_pixel`, `gfx_rect`, `gfx_rect_intersect` (Task 1).
- Produces:
  - `gfx_glyph_t {uint32_t codepoint; uint32_t offset; uint8_t width, height; int8_t x_offset, y_offset; uint8_t advance}`.
  - `gfx_font_t {const uint8_t *bitmap; const gfx_glyph_t *glyphs; uint16_t glyph_count; uint8_t ascent, line_height}`.
  - `gfx_align_t` (`GFX_ALIGN_LEFT`, `GFX_ALIGN_CENTER`, `GFX_ALIGN_RIGHT`).
  - `uint32_t gfx_utf8_next(const char **s)`.
  - `bool gfx_font_has_glyph(const gfx_font_t *font, uint32_t cp)`.
  - `int gfx_text_width(const gfx_font_t *font, const char *utf8)`.
  - `int gfx_text(gfx_fb_t *fb, const gfx_font_t *font, int x, int baseline, const char *utf8, gfx_color_t color)` returns the pen x after the text.
  - `void gfx_text_in_rect(gfx_fb_t *fb, const gfx_font_t *font, gfx_rect_t r, gfx_align_t align, const char *utf8, gfx_color_t color)`.
- Missing-glyph rule: a box `w = max(line_height / 2, 2)`, `h = max(ascent * 2 / 3, 2)`, outline drawn at `(pen + 1, baseline - h)`, advance `w + 2`.

- [ ] **Step 1: Write the failing tests `test/host/test_gfx_text.c`**

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

static void test_missing_glyph_draws_a_hollow_box(void)
{
    TEST_ASSERT_EQUAL_INT(4, gfx_text(&s_fb, &s_font, 0, 4, "B", GFX_BLACK));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 1, 2));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 2));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 1, 3));
    TEST_ASSERT_TRUE(gfx_get_pixel(&s_fb, 2, 3));
    TEST_ASSERT_EQUAL_INT(4, count_black());
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
    RUN_TEST(test_text_in_rect_centres_horizontally_and_vertically);
    RUN_TEST(test_text_in_rect_aligns_right);
    RUN_TEST(test_text_in_rect_clips_to_the_rect_and_restores_the_clip);
    return UNITY_END();
}
```

Also add `reflbo_host_test(test_gfx_text gfx)` to `test/host/CMakeLists.txt`.

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL to compile, with `unknown type name 'gfx_glyph_t'` or an implicit declaration of `gfx_utf8_next`.

- [ ] **Step 3: Write `components/gfx/include/gfx_font.h`**

```c
#pragma once

#include <stdint.h>

/* Bitmap font generated by tools/fontgen.py (spec §4.4). */
typedef struct {
    uint32_t codepoint;
    uint32_t offset;  /* into gfx_font_t.bitmap */
    uint8_t width;    /* bitmap width in px */
    uint8_t height;   /* bitmap height in px */
    int8_t x_offset;  /* left edge relative to the pen */
    int8_t y_offset;  /* top edge relative to the baseline; negative is above */
    uint8_t advance;  /* pen advance in px */
} gfx_glyph_t;

typedef struct {
    const uint8_t *bitmap;     /* glyph rows, MSB first, each row padded to whole bytes, 1 = ink */
    const gfx_glyph_t *glyphs; /* sorted by codepoint */
    uint16_t glyph_count;
    uint8_t ascent;      /* baseline distance from the top of a line */
    uint8_t line_height; /* ascent + descent */
} gfx_font_t;
```

- [ ] **Step 4: Append the text API to `components/gfx/include/gfx.h`**

```c

#include "gfx_font.h"

typedef enum {
    GFX_ALIGN_LEFT,
    GFX_ALIGN_CENTER,
    GFX_ALIGN_RIGHT,
} gfx_align_t;

/* Decodes one UTF-8 codepoint and advances *s. Returns 0 at the terminating NUL and U+FFFD for
 * malformed input; never reads past the NUL. */
uint32_t gfx_utf8_next(const char **s);

bool gfx_font_has_glyph(const gfx_font_t *font, uint32_t codepoint);
int gfx_text_width(const gfx_font_t *font, const char *utf8);
/* Draws one line with its baseline at `baseline`; returns the pen x after the text.
 * A codepoint missing from the font is drawn as a hollow box. */
int gfx_text(gfx_fb_t *fb, const gfx_font_t *font, int x, int baseline, const char *utf8, gfx_color_t color);
/* One line aligned in r, vertically centred on the font's line box, clipped to r. */
void gfx_text_in_rect(gfx_fb_t *fb, const gfx_font_t *font, gfx_rect_t r, gfx_align_t align, const char *utf8,
                      gfx_color_t color);
```

- [ ] **Step 5: Write `components/gfx/gfx_text.c`**

```c
#include "gfx.h"

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

int gfx_text_width(const gfx_font_t *font, const char *utf8)
{
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

int gfx_text(gfx_fb_t *fb, const gfx_font_t *font, int x, int baseline, const char *utf8, gfx_color_t color)
{
    uint32_t cp;
    while ((cp = gfx_utf8_next(&utf8)) != 0) {
        const gfx_glyph_t *g = find_glyph(font, cp);
        if (g != NULL) {
            draw_glyph(fb, font, g, x, baseline, color);
            x += g->advance;
        } else {
            int w, h;
            missing_box(font, &w, &h);
            gfx_rect(fb, (gfx_rect_t){ (int16_t)(x + 1), (int16_t)(baseline - h), (int16_t)w, (int16_t)h }, color);
            x += w + 2;
        }
    }
    return x;
}

void gfx_text_in_rect(gfx_fb_t *fb, const gfx_font_t *font, gfx_rect_t r, gfx_align_t align, const char *utf8,
                      gfx_color_t color)
{
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
```

- [ ] **Step 6: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 4`.

- [ ] **Step 7: Commit and push**

```bash
git add components/gfx test/host/test_gfx_text.c test/host/CMakeLists.txt
git commit -m "feat(gfx): add UTF-8 text drawing with bitmap fonts"
git push
```

---

### Task 3: Font pipeline and the first fonts

The choice was made empirically on 2026-09-25. Candidates rendered in 1-bit at 12–28 px: DejaVu Sans, Atkinson Hyperlegible, PT Sans, Fira Sans. DejaVu Sans is the crispest at 12–16 px thanks to its strong TrueType hinting, and it covers Latin Extended-A. Licence: Bitstream Vera and Arev terms, permissive, redistribution allowed. The generated C symbols use neutral names (`sans_*`), because the licence reserves the font names for unmodified fonts.

**Files:**
- Create: `tools/fontgen.py`, `tools/tests/test_fontgen.py`, `tools/gen_fonts.sh`, `tools/requirements.txt`
- Create: `assets/fonts/DejaVuSans.ttf`, `assets/fonts/DejaVuSans-Bold.ttf`, `assets/fonts/LICENSE-DejaVu.txt` (from the dejavu-fonts 2.37 release)
- Create (generated): `components/gfx/fonts/gfx_font_{sans_12,sans_16,sans_20,sans_bold_20}.c`; remove `components/gfx/fonts/.gitkeep`
- Create: `components/gfx/include/gfx_fonts.h`, `test/host/test_gfx_fonts.c`
- Modify: `test/host/CMakeLists.txt`, `THIRD_PARTY.md`, `AGENTS.md` §6

**Interfaces:**
- Consumes: `gfx_font_t` and `gfx_glyph_t` field order (Task 2).
- Produces:
  - `extern const gfx_font_t gfx_font_sans_12, gfx_font_sans_16, gfx_font_sans_20, gfx_font_sans_bold_20;` in `gfx_fonts.h`.
  - `tools/gen_fonts.sh` regenerates every font.
  - fontgen CLI: `fontgen.py --ttf PATH --size PX --charset SPEC --name NAME [--out PATH]`.

- [ ] **Step 1: Write the failing Python tests `tools/tests/test_fontgen.py`**

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

    def test_rejects_glyphs_that_do_not_fit_the_c_types(self):
        glyph = dict(cp=0x41, width=300, height=1, x=0, y=0, advance=1, rows=[[1] * 300])
        with self.assertRaises(ValueError):
            fontgen.emit_c("big", "Big.ttf", 400, [glyph], ascent=1, line_height=1)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `tools_unittests` FAILS with `ModuleNotFoundError: No module named 'fontgen'`.

- [ ] **Step 3: Write `tools/fontgen.py`**

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
    return dict(cp=cp, width=width, height=height, x=left, y=top, advance=advance, rows=rows)


def _check(glyph):
    ok = (0 <= glyph["width"] <= 255 and 0 <= glyph["height"] <= 255 and -128 <= glyph["x"] <= 127
          and -128 <= glyph["y"] <= 127 and 0 <= glyph["advance"] <= 255)
    if not ok:
        raise ValueError(f"glyph U+{glyph['cp']:04X} does not fit gfx_glyph_t: {glyph}")


def emit_c(name, source, size, glyphs, ascent, line_height):
    data = bytearray()
    offsets = []
    for glyph in glyphs:
        _check(glyph)
        offsets.append(len(data))
        data += pack_rows(glyph["rows"])
    lines = [
        f"/* Generated by tools/fontgen.py from {source} at {size} px. Do not edit; run tools/gen_fonts.sh. */",
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
    args = parser.parse_args(argv)

    from fontTools.ttLib import TTFont
    from PIL import ImageFont

    cmap = TTFont(args.ttf).getBestCmap()
    font = ImageFont.truetype(args.ttf, args.size)
    ascent, descent = font.getmetrics()
    wanted = parse_charset(args.charset)
    glyphs = [render_glyph(font, cp) for cp in wanted if cp in cmap]
    missing = [f"U+{cp:04X}" for cp in wanted if cp not in cmap]
    out = pathlib.Path(args.out or f"components/gfx/fonts/gfx_font_{args.name}.c")
    out.write_text(emit_c(args.name, pathlib.Path(args.ttf).name, args.size, glyphs, ascent, ascent + descent),
                   encoding="utf-8")
    print(f"{out}: {len(glyphs)} glyphs, ascent {ascent}, line {ascent + descent}"
          + (f", not in font: {' '.join(missing)}" if missing else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the Python tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 4`.

- [ ] **Step 5: Add the font sources**

```bash
mkdir -p assets/fonts .cache
curl -fsSL -o .cache/dejavu.tar.bz2 \
  https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2
tar -xjf .cache/dejavu.tar.bz2 -C .cache
cp .cache/dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf .cache/dejavu-fonts-ttf-2.37/ttf/DejaVuSans-Bold.ttf assets/fonts/
cp .cache/dejavu-fonts-ttf-2.37/LICENSE assets/fonts/LICENSE-DejaVu.txt
ls assets/fonts
```

Expected: `DejaVuSans-Bold.ttf  DejaVuSans.ttf  LICENSE-DejaVu.txt`. (`.cache/` is gitignored.)

- [ ] **Step 6: Write `tools/requirements.txt` and `tools/gen_fonts.sh`, then generate the fonts**

```text
# Host-tool dependencies outside the ESP-IDF environment (spec §15): the font generator.
# tools/gen_fonts.sh runs it in a throwaway uv environment built from this file.
pillow==12.3.0
fonttools==4.66.0
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

fontgen --ttf assets/fonts/DejaVuSans.ttf --size 12 --charset text --name sans_12
fontgen --ttf assets/fonts/DejaVuSans.ttf --size 16 --charset text --name sans_16
fontgen --ttf assets/fonts/DejaVuSans.ttf --size 20 --charset text --name sans_20
fontgen --ttf assets/fonts/DejaVuSans-Bold.ttf --size 20 --charset text --name sans_bold_20
```

Run: `chmod +x tools/gen_fonts.sh && git rm -q --cached components/gfx/fonts/.gitkeep; rm -f components/gfx/fonts/.gitkeep; tools/gen_fonts.sh`
Expected: four lines of the form `components/gfx/fonts/gfx_font_sans_12.c: <N> glyphs, ascent <A>, line <L>`. N is about 350, and no codepoint is listed as missing.

- [ ] **Step 7: Write `components/gfx/include/gfx_fonts.h` and the failing font test `test/host/test_gfx_fonts.c`**

```c
#pragma once

#include "gfx_font.h"

/* Bitmap fonts rendered from DejaVu Sans 2.37 by tools/gen_fonts.sh (see THIRD_PARTY.md). */
extern const gfx_font_t gfx_font_sans_12;
extern const gfx_font_t gfx_font_sans_16;
extern const gfx_font_t gfx_font_sans_20;
extern const gfx_font_t gfx_font_sans_bold_20;
```

```c
#include "gfx.h"
#include "gfx_fonts.h"
#include "unity.h"

static const gfx_font_t *const s_fonts[] = {
    &gfx_font_sans_12, &gfx_font_sans_16, &gfx_font_sans_20, &gfx_font_sans_bold_20,
};
#define FONT_COUNT (sizeof(s_fonts) / sizeof(s_fonts[0]))

void setUp(void) {}
void tearDown(void) {}

static void test_fonts_cover_czech_and_symbols(void)
{
    for (size_t f = 0; f < FONT_COUNT; f++) {
        const char *p = "ÁáČčĎďÉéĚěÍíŇňÓóŘřŠšŤťÚúŮůÝýŽž°µ²€–…→";
        uint32_t cp;
        while ((cp = gfx_utf8_next(&p)) != 0) {
            TEST_ASSERT_TRUE_MESSAGE(gfx_font_has_glyph(s_fonts[f], cp), "glyph missing");
        }
    }
}

static void test_glyphs_are_sorted_for_binary_search(void)
{
    for (size_t f = 0; f < FONT_COUNT; f++) {
        for (uint16_t i = 1; i < s_fonts[f]->glyph_count; i++) {
            TEST_ASSERT_TRUE(s_fonts[f]->glyphs[i - 1].codepoint < s_fonts[f]->glyphs[i].codepoint);
        }
    }
}

static void test_metrics_grow_with_size(void)
{
    TEST_ASSERT_TRUE(gfx_font_sans_12.line_height < gfx_font_sans_16.line_height);
    TEST_ASSERT_TRUE(gfx_font_sans_16.line_height < gfx_font_sans_20.line_height);
    for (size_t f = 0; f < FONT_COUNT; f++) {
        TEST_ASSERT_TRUE(s_fonts[f]->ascent > 0);
        TEST_ASSERT_TRUE(s_fonts[f]->ascent < s_fonts[f]->line_height);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fonts_cover_czech_and_symbols);
    RUN_TEST(test_glyphs_are_sorted_for_binary_search);
    RUN_TEST(test_metrics_grow_with_size);
    return UNITY_END();
}
```

Add `reflbo_host_test(test_gfx_fonts gfx)` to `test/host/CMakeLists.txt`.

- [ ] **Step 8: Run the tests and confirm they pass**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 5`. This is a characterization test: it passes on first run because the fonts are generated data. To see it catch a real break, regenerate one font with `--charset ascii` and watch it fail, then restore the font.

- [ ] **Step 9: Register DejaVu and document regeneration**

Append to `THIRD_PARTY.md`:

```markdown
| DejaVu Sans 2.37 (Regular, Bold) | https://github.com/dejavu-fonts/dejavu-fonts | Bitstream Vera + Arev font licence, DejaVu changes public domain (`assets/fonts/LICENSE-DejaVu.txt`) | `assets/fonts/`; bitmaps rendered into `components/gfx/fonts/` | Rasterised by `tools/gen_fonts.sh`; C symbols use neutral `sans_*` names as the licence reserves the font names |
```

In `AGENTS.md` §6, add this line to the command block, below the `ctest` line:

```sh
tools/gen_fonts.sh                          # regenerate components/gfx/fonts (needs uv; versions in tools/requirements.txt)
```

- [ ] **Step 10: Commit and push**

```bash
git add tools/fontgen.py tools/tests/test_fontgen.py tools/gen_fonts.sh tools/requirements.txt assets/fonts components/gfx test/host THIRD_PARTY.md AGENTS.md
git commit -m "feat(gfx): add font generator and DejaVu Sans bitmap fonts"
git push
```

---

### Task 4: `util_base64`

**Files:**
- Create: `components/util/include/util_base64.h`, `components/util/util_base64.c`, `test/host/test_util_base64.c`
- Modify: `components/util/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `size_t util_base64_encoded_len(size_t len)`.
  - `bool util_base64_encode(const void *data, size_t len, char *out, size_t out_size)`. It NUL-terminates the output, and returns false without writing anything if `out_size < encoded_len + 1`.

- [ ] **Step 1: Write the failing tests `test/host/test_util_base64.c`**

```c
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "util_base64.h"

void setUp(void) {}
void tearDown(void) {}

static void check(const char *in, const char *expected)
{
    char out[16];
    TEST_ASSERT_TRUE(util_base64_encode(in, strlen(in), out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(expected, out);
}

static void test_rfc4648_vectors(void)
{
    check("", "");
    check("f", "Zg==");
    check("fo", "Zm8=");
    check("foo", "Zm9v");
    check("foob", "Zm9vYg==");
    check("fooba", "Zm9vYmE=");
    check("foobar", "Zm9vYmFy");
}

static void test_binary_bytes(void)
{
    const uint8_t in[] = { 0x00, 0xFF, 0x10 };
    char out[8];
    TEST_ASSERT_TRUE(util_base64_encode(in, sizeof(in), out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("AP8Q", out);
}

static void test_encoded_length(void)
{
    TEST_ASSERT_EQUAL_INT(0, (int)util_base64_encoded_len(0));
    TEST_ASSERT_EQUAL_INT(4, (int)util_base64_encoded_len(1));
    TEST_ASSERT_EQUAL_INT(4, (int)util_base64_encoded_len(3));
    TEST_ASSERT_EQUAL_INT(8, (int)util_base64_encoded_len(4));
    TEST_ASSERT_EQUAL_INT(76, (int)util_base64_encoded_len(57));
}

static void test_too_small_buffer_writes_nothing(void)
{
    char out[5] = "abcd";
    TEST_ASSERT_FALSE(util_base64_encode("foo", 3, out, 4));
    TEST_ASSERT_EQUAL_STRING("abcd", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc4648_vectors);
    RUN_TEST(test_binary_bytes);
    RUN_TEST(test_encoded_length);
    RUN_TEST(test_too_small_buffer_writes_nothing);
    return UNITY_END();
}
```

In `test/host/CMakeLists.txt`, replace the `util` library's source list with a glob:

```cmake
file(GLOB UTIL_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/util/*.c)
add_library(util STATIC ${UTIL_SOURCES})
```

and add `reflbo_host_test(test_util_base64 util)`.

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL, with `'util_base64.h' file not found`.

- [ ] **Step 3: Write `components/util/include/util_base64.h` and `components/util/util_base64.c`**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Standard base64 (RFC 4648) with padding and no line breaks. Pure C, host-buildable. */
size_t util_base64_encoded_len(size_t len);
/* Writes the NUL-terminated encoding of data to out. Returns false, writing nothing, when out_size
 * is smaller than util_base64_encoded_len(len) + 1. */
bool util_base64_encode(const void *data, size_t len, char *out, size_t out_size);
```

```c
#include "util_base64.h"

#include <stdint.h>

static const char s_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t util_base64_encoded_len(size_t len)
{
    return (len + 2) / 3 * 4;
}

bool util_base64_encode(const void *data, size_t len, char *out, size_t out_size)
{
    if (out_size < util_base64_encoded_len(len) + 1) {
        return false;
    }
    const uint8_t *in = data;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)in[i] << 16;
        if (i + 1 < len) {
            n |= (uint32_t)in[i + 1] << 8;
        }
        if (i + 2 < len) {
            n |= in[i + 2];
        }
        out[o++] = s_alphabet[(n >> 18) & 0x3F];
        out[o++] = s_alphabet[(n >> 12) & 0x3F];
        out[o++] = i + 1 < len ? s_alphabet[(n >> 6) & 0x3F] : '=';
        out[o++] = i + 2 < len ? s_alphabet[n & 0x3F] : '=';
    }
    out[o] = '\0';
    return true;
}
```

Update `components/util/CMakeLists.txt`:

```cmake
# Small pure-C helpers shared by the firmware and the host tests (no ESP-IDF headers).
idf_component_register(SRCS "util_crc32.c" "util_base64.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 6`.

- [ ] **Step 5: Commit and push**

```bash
git add components/util test/host/test_util_base64.c test/host/CMakeLists.txt
git commit -m "feat(util): add base64 encoder"
git push
```

---

### Task 5: ST7305 frame conversion

**Files:**
- Create: `components/st7305/CMakeLists.txt`, `components/st7305/include/st7305_frame.h`, `components/st7305/st7305_frame.c`, `test/host/test_st7305_frame.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `#define ST7305_WIDTH 400`, `ST7305_HEIGHT 300`, `ST7305_FRAME_BYTES 15000`.
  - `void st7305_frame_to_panel(const uint8_t *canonical, uint8_t *panel)`.

- [ ] **Step 1: Write the failing tests `test/host/test_st7305_frame.c`**

```c
#include <stdint.h>
#include <string.h>

#include "st7305_frame.h"
#include "unity.h"

static uint8_t s_canonical[ST7305_FRAME_BYTES];
static uint8_t s_panel[ST7305_FRAME_BYTES];
static uint8_t s_expected[ST7305_FRAME_BYTES];

void setUp(void) {}
void tearDown(void) {}

/* Transcription of the vendor's landscape mapping (Waveshare display_bsp.cpp: InitLandscapeLUT and
 * RLCD_SetPixel, where ColorWhite sets the bit). */
static void vendor_set_pixel(uint8_t *panel, int x, int y, int white)
{
    int inv_y = 300 - 1 - y;
    int block_y = inv_y >> 2;
    int local_y = inv_y & 3;
    int byte_x = x >> 1;
    int local_x = x & 1;
    int index = byte_x * (300 >> 2) + block_y;
    int bit = 7 - ((local_y << 1) | local_x);
    if (white) {
        panel[index] |= (uint8_t)(1 << bit);
    } else {
        panel[index] &= (uint8_t)~(1 << bit);
    }
}

static void set_black(int x, int y)
{
    s_canonical[y * 50 + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
}

static void vendor_expected_from_canonical(void)
{
    for (int y = 0; y < 300; y++) {
        for (int x = 0; x < 400; x++) {
            int black = (s_canonical[y * 50 + (x >> 3)] >> (7 - (x & 7))) & 1;
            vendor_set_pixel(s_expected, x, y, !black);
        }
    }
}

static void test_white_frame_is_all_ones_on_the_panel(void)
{
    memset(s_canonical, 0x00, sizeof(s_canonical));
    st7305_frame_to_panel(s_canonical, s_panel);
    for (size_t i = 0; i < sizeof(s_panel); i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, s_panel[i]);
    }
}

static void test_black_frame_is_all_zeros_on_the_panel(void)
{
    memset(s_canonical, 0xFF, sizeof(s_canonical));
    st7305_frame_to_panel(s_canonical, s_panel);
    for (size_t i = 0; i < sizeof(s_panel); i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, s_panel[i]);
    }
}

static void test_corner_pixels_land_where_the_vendor_puts_them(void)
{
    const int points[][2] = { { 0, 0 }, { 399, 0 }, { 0, 299 }, { 399, 299 }, { 1, 1 }, { 200, 150 } };
    for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
        memset(s_canonical, 0x00, sizeof(s_canonical));
        set_black(points[i][0], points[i][1]);
        vendor_expected_from_canonical();
        st7305_frame_to_panel(s_canonical, s_panel);
        TEST_ASSERT_EQUAL_MEMORY(s_expected, s_panel, sizeof(s_panel));
    }
}

static void test_random_frame_matches_the_vendor_formula(void)
{
    uint32_t state = 12345u;
    for (size_t i = 0; i < sizeof(s_canonical); i++) {
        state = state * 1103515245u + 12345u;
        s_canonical[i] = (uint8_t)(state >> 16);
    }
    vendor_expected_from_canonical();
    st7305_frame_to_panel(s_canonical, s_panel);
    TEST_ASSERT_EQUAL_MEMORY(s_expected, s_panel, sizeof(s_panel));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_white_frame_is_all_ones_on_the_panel);
    RUN_TEST(test_black_frame_is_all_zeros_on_the_panel);
    RUN_TEST(test_corner_pixels_land_where_the_vendor_puts_them);
    RUN_TEST(test_random_frame_matches_the_vendor_formula);
    return UNITY_END();
}
```

Add to `test/host/CMakeLists.txt`, after the gfx library:

```cmake
# st7305: only the pure-C frame conversion builds on the host.
add_library(st7305_frame STATIC ${REPO_ROOT}/components/st7305/st7305_frame.c)
target_include_directories(st7305_frame PUBLIC ${REPO_ROOT}/components/st7305/include)
target_compile_options(st7305_frame PRIVATE ${REFLBO_WARNINGS})
```

and `reflbo_host_test(test_st7305_frame st7305_frame)`.

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL, because CMake cannot find `st7305_frame.c`.

- [ ] **Step 3: Write `components/st7305/include/st7305_frame.h`, `components/st7305/st7305_frame.c` and `components/st7305/CMakeLists.txt`**

```c
#pragma once

#include <stdint.h>

/* Panel geometry in landscape orientation (spec §4.1). */
#define ST7305_WIDTH       400
#define ST7305_HEIGHT      300
#define ST7305_FRAME_BYTES (ST7305_WIDTH * ST7305_HEIGHT / 8)

/* Converts a canonical frame (row-major 1 bpp, MSB first, 1 = black) into the panel's RAM layout:
 * 2x4-pixel blocks per byte, 1 = white (AGENTS.md gotcha 5). Pure C, host-buildable. */
void st7305_frame_to_panel(const uint8_t *canonical, uint8_t *panel);
```

```c
#include "st7305_frame.h"

#define ROW_BYTES    (ST7305_WIDTH / 8)
#define BLOCKS       (ST7305_HEIGHT / 4) /* panel bytes per column pair */

static inline int is_black(const uint8_t *canonical, int x, int y)
{
    return (canonical[y * ROW_BYTES + (x >> 3)] >> (7 - (x & 7))) & 1;
}

void st7305_frame_to_panel(const uint8_t *canonical, uint8_t *panel)
{
    for (int pair = 0; pair < ST7305_WIDTH / 2; pair++) {
        for (int block = 0; block < BLOCKS; block++) {
            uint8_t byte = 0;
            for (int ly = 0; ly < 4; ly++) {
                int y = ST7305_HEIGHT - 1 - (block * 4 + ly); /* the panel runs bottom-up */
                for (int lx = 0; lx < 2; lx++) {
                    if (!is_black(canonical, pair * 2 + lx, y)) {
                        byte |= (uint8_t)(0x80u >> (ly * 2 + lx));
                    }
                }
            }
            panel[pair * BLOCKS + block] = byte;
        }
    }
}
```

```cmake
# ST7305 reflective LCD (spec §4.2). st7305_frame.c is pure C and also built on the host.
idf_component_register(SRCS "st7305_frame.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 7`.

- [ ] **Step 5: Commit and push**

```bash
git add components/st7305 test/host/test_st7305_frame.c test/host/CMakeLists.txt
git commit -m "feat(st7305): add canonical-to-panel frame conversion"
git push
```

---

### Task 6: Test pattern, host rendering and golden image

**Files:**
- Create: `components/gfx/include/gfx_test_pattern.h`, `components/gfx/gfx_test_pattern.c`, `test/host/render_test_pattern.c`, `test/host/test_test_pattern_golden.c`, `test/host/golden/test_pattern.pbm` (generated, then reviewed)
- Create: `tools/pbm_png.py`, `tools/tests/test_pbm_png.py`, `tools/render.py`
- Modify: `test/host/CMakeLists.txt`, `AGENTS.md` §6

**Interfaces:**
- Consumes: gfx primitives and text (Tasks 1–2), `gfx_fonts.h` (Task 3).
- Produces:
  - `void gfx_draw_test_pattern(gfx_fb_t *fb)` for a 400×300 framebuffer.
  - `build-host/render_test_pattern OUT.pbm`.
  - `pbm_png.parse_pbm(bytes) -> (w, h, raster)`, `pbm_png.png_from_pbm(bytes) -> bytes`, and the CLI `python3 tools/pbm_png.py IN.pbm OUT.png`.
  - `python3 tools/render.py` writes `captures/render/test_pattern.{pbm,png}`.
  - The golden file `test/host/golden/test_pattern.pbm`.

- [ ] **Step 1: Write the failing Python tests `tools/tests/test_pbm_png.py`**

```python
import struct
import unittest
import zlib

import pbm_png


class ParsePbmTest(unittest.TestCase):
    def test_reads_header_comments_and_raster(self):
        self.assertEqual(pbm_png.parse_pbm(b"P4\n# note\n8 2\n\xF0\x0F"), (8, 2, b"\xF0\x0F"))

    def test_rejects_other_formats_and_short_rasters(self):
        with self.assertRaises(ValueError):
            pbm_png.parse_pbm(b"P1\n8 2\n\xF0\x0F")
        with self.assertRaises(ValueError):
            pbm_png.parse_pbm(b"P4\n8 2\n\xF0")


class PngTest(unittest.TestCase):
    def test_png_is_one_bit_greyscale_with_black_as_zero(self):
        png = pbm_png.png_from_pbm(b"P4\n8 2\n\xF0\x0F")
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        width, height, depth, colour = struct.unpack(">IIBB", png[16:26])
        self.assertEqual((width, height, depth, colour), (8, 2, 1, 0))
        idat_len = struct.unpack(">I", png[33:37])[0]
        self.assertEqual(png[37:41], b"IDAT")
        self.assertEqual(zlib.decompress(png[41:41 + idat_len]), b"\x00\x0F\x00\xF0")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `tools_unittests` FAILS with `No module named 'pbm_png'`.

- [ ] **Step 3: Write `tools/pbm_png.py`**

```python
#!/usr/bin/env python3
"""Convert binary PBM (P4) images to PNG using only the standard library.
  python3 tools/pbm_png.py IN.pbm OUT.png
"""
import struct
import sys
import zlib


def parse_pbm(data):
    """Returns (width, height, raster) for a P4 image; the raster is row-major, MSB first, 1 = black."""
    fields = []
    pos = 0
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] not in (b"\n", b"\r"):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        if start == pos:
            raise ValueError("truncated PBM header")
        fields.append(data[start:pos])
    if fields[0] != b"P4":
        raise ValueError("not a binary PBM (P4)")
    width, height = int(fields[1]), int(fields[2])
    pos += 1  # the single whitespace after the height
    size = (width + 7) // 8 * height
    raster = data[pos:pos + size]
    if len(raster) != size:
        raise ValueError("truncated PBM raster")
    return width, height, raster


def png_from_pbm(data):
    width, height, raster = parse_pbm(data)
    row_bytes = (width + 7) // 8
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        raw += bytes(b ^ 0xFF for b in raster[y * row_bytes:(y + 1) * row_bytes])  # PNG grey: 0 = black

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 1, 0, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(argv[0], "rb") as src, open(argv[1], "wb") as dst:
        dst.write(png_from_pbm(src.read()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Write the test pattern, the renderer and the failing golden test**

`components/gfx/include/gfx_test_pattern.h`:

```c
#pragma once

#include "gfx.h"

/* Draws the display test pattern (spec §4.2) into a 400x300 framebuffer: border, origin marker,
 * corner labels, glyph samples, a 10-px grid, two checkerboards and crossing diagonals. */
void gfx_draw_test_pattern(gfx_fb_t *fb);
```

`components/gfx/gfx_test_pattern.c`:

```c
#include "gfx_test_pattern.h"

#include "gfx_fonts.h"

void gfx_draw_test_pattern(gfx_fb_t *fb)
{
    const gfx_font_t *label = &gfx_font_sans_bold_20;
    const int16_t w = fb->width;
    const int16_t h = fb->height;

    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    gfx_rect(fb, (gfx_rect_t){ 0, 0, w, h }, GFX_BLACK);
    gfx_fill_rect(fb, (gfx_rect_t){ 2, 2, 6, 6 }, GFX_BLACK); /* origin marker */

    gfx_text_in_rect(fb, label, (gfx_rect_t){ 10, 4, 60, 24 }, GFX_ALIGN_LEFT, "TL", GFX_BLACK);
    gfx_text_in_rect(fb, label, (gfx_rect_t){ (int16_t)(w - 70), 4, 60, 24 }, GFX_ALIGN_RIGHT, "TR", GFX_BLACK);
    gfx_text_in_rect(fb, label, (gfx_rect_t){ 10, (int16_t)(h - 28), 60, 24 }, GFX_ALIGN_LEFT, "BL", GFX_BLACK);
    gfx_text_in_rect(fb, label, (gfx_rect_t){ (int16_t)(w - 70), (int16_t)(h - 28), 60, 24 }, GFX_ALIGN_RIGHT,
                     "BR", GFX_BLACK);

    gfx_text_in_rect(fb, label, (gfx_rect_t){ 0, 34, w, 26 }, GFX_ALIGN_CENTER, "reflbo test pattern", GFX_BLACK);
    gfx_text_in_rect(fb, &gfx_font_sans_16, (gfx_rect_t){ 0, 64, w, 22 }, GFX_ALIGN_CENTER,
                     "Žluťoučký kůň úpěl ďábelské ódy", GFX_BLACK);
    gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ 0, 88, w, 18 }, GFX_ALIGN_CENTER,
                     "0123456789 °C % € – … ABC abc", GFX_BLACK);

    for (int x = 20; x <= 180; x += 10) {
        gfx_vline(fb, x, 120, 141, GFX_BLACK);
    }
    for (int y = 120; y <= 260; y += 10) {
        gfx_hline(fb, 20, y, 161, GFX_BLACK);
    }

    for (int y = 0; y < 60; y++) {
        for (int x = 0; x < 60; x++) {
            if (((x + y) & 1) == 0) {
                gfx_pixel(fb, 220 + x, 120 + y, GFX_BLACK); /* 1-px checkerboard */
            }
            if ((((x >> 2) + (y >> 2)) & 1) == 0) {
                gfx_pixel(fb, 300 + x, 120 + y, GFX_BLACK); /* 4-px checkerboard */
            }
        }
    }
    gfx_line(fb, 220, 200, 360, 260, GFX_BLACK);
    gfx_line(fb, 220, 260, 360, 200, GFX_BLACK);
}
```

`test/host/render_test_pattern.c`:

```c
#include <stdio.h>

#include "gfx.h"
#include "gfx_test_pattern.h"

/* Writes the test pattern as PBM: render_test_pattern OUT.pbm */
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUT.pbm\n", argv[0]);
        return 2;
    }
    static uint8_t buf[400 * 300 / 8];
    static uint8_t pbm[16000];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    gfx_draw_test_pattern(&fb);
    size_t n = gfx_pbm_encode(&fb, pbm, sizeof(pbm));
    FILE *f = fopen(argv[1], "wb");
    if (f == NULL || fwrite(pbm, 1, n, f) != n) {
        perror(argv[1]);
        return 1;
    }
    fclose(f);
    return 0;
}
```

`test/host/test_test_pattern_golden.c`:

```c
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "gfx_test_pattern.h"
#include "unity.h"

/* The test pattern must match test/host/golden/test_pattern.pbm byte for byte. After an intentional
 * change: build-host/render_test_pattern test/host/golden/test_pattern.pbm, look at the PNG, commit. */

static uint8_t s_buf[400 * 300 / 8];
static uint8_t s_pbm[16000];
static uint8_t s_golden[16000];

void setUp(void) {}
void tearDown(void) {}

static void test_test_pattern_matches_the_golden_image(void)
{
    gfx_fb_t fb;
    gfx_fb_init(&fb, s_buf, 400, 300);
    gfx_draw_test_pattern(&fb);
    size_t n = gfx_pbm_encode(&fb, s_pbm, sizeof(s_pbm));

    FILE *f = fopen(GOLDEN_DIR "/test_pattern.pbm", "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "golden image missing: " GOLDEN_DIR "/test_pattern.pbm");
    size_t golden = fread(s_golden, 1, sizeof(s_golden), f);
    fclose(f);

    if (golden != n || memcmp(s_golden, s_pbm, n) != 0) {
        FILE *out = fopen("test_pattern.actual.pbm", "wb");
        if (out != NULL) {
            fwrite(s_pbm, 1, n, out);
            fclose(out);
        }
    }
    TEST_ASSERT_EQUAL_INT((int)n, (int)golden);
    TEST_ASSERT_EQUAL_MEMORY(s_golden, s_pbm, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_test_pattern_matches_the_golden_image);
    return UNITY_END();
}
```

Add to `test/host/CMakeLists.txt`:

```cmake
reflbo_host_test(test_test_pattern_golden gfx)
target_compile_definitions(test_test_pattern_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(render_test_pattern render_test_pattern.c)
target_compile_options(render_test_pattern PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(render_test_pattern PRIVATE gfx)
```

- [ ] **Step 5: Run it and confirm the golden test fails because the golden file doesn't exist yet**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `tools_unittests` passes. `test_test_pattern_golden` FAILS with `golden image missing`.

- [ ] **Step 6: Render the golden image, review it, and confirm the test passes**

```bash
mkdir -p test/host/golden captures/render
build-host/render_test_pattern test/host/golden/test_pattern.pbm
python3 tools/pbm_png.py test/host/golden/test_pattern.pbm captures/render/test_pattern.png
```

Look at `captures/render/test_pattern.png` (Read tool). Check each of these:
- The border is complete.
- A 6×6 black square sits just inside the top-left corner.
- TL, TR, BL and BR labels are in their corners.
- The title is centred.
- The Czech line shows every diacritic, with no hollow boxes.
- The digits and symbols line has no boxes.
- A 10-px grid is at the bottom left, two checkerboards at the right, and diagonals below them.
- Nothing overlaps.

Fix the pattern if needed, and re-render. Then run `ctest --test-dir build-host --output-on-failure`.
Expected: `100% tests passed, 0 tests failed out of 8`.

- [ ] **Step 7: Write `tools/render.py`**

```python
#!/usr/bin/env python3
"""Render host-side images to PNG (spec §15) with the renderers built in build-host.
  cmake -S test/host -B build-host -G Ninja && cmake --build build-host
  python3 tools/render.py            # captures/render/test_pattern.{pbm,png}
"""
import argparse
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pbm_png  # noqa: E402

RENDERERS = {"test_pattern": "render_test_pattern"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build-host")
    parser.add_argument("--out-dir", default="captures/render")
    args = parser.parse_args(argv)
    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, exe in RENDERERS.items():
        pbm = out_dir / f"{name}.pbm"
        subprocess.run([str(pathlib.Path(args.build_dir) / exe), str(pbm)], check=True)
        (out_dir / f"{name}.png").write_bytes(pbm_png.png_from_pbm(pbm.read_bytes()))
        print(f"{name}: {pbm} and {pbm.with_suffix('.png')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Run: `python3 tools/render.py`
Expected: `test_pattern: captures/render/test_pattern.pbm and captures/render/test_pattern.png`, and `cmp captures/render/test_pattern.pbm test/host/golden/test_pattern.pbm` prints nothing.

- [ ] **Step 8: Update `AGENTS.md` §6**

In the command block, add this line below the `tools/gen_fonts.sh` line:

```sh
python3 tools/render.py                     # host renderings to captures/render/*.png (after the host build)
```

- [ ] **Step 9: Commit and push**

```bash
git add components/gfx test/host tools/pbm_png.py tools/tests/test_pbm_png.py tools/render.py AGENTS.md
git commit -m "feat(gfx): add display test pattern with host rendering and golden image"
git push
```

---

### Task 7: ST7305 driver, display service, pattern at boot

**Files:**
- Create: `components/st7305/include/st7305.h`, `components/st7305/st7305.c`, `components/display/CMakeLists.txt`, `components/display/include/display.h`, `components/display/display.c`
- Modify: `components/st7305/CMakeLists.txt`, `main/main.c`, `main/CMakeLists.txt`, `AGENTS.md` §5.2, spec §3.1

**Interfaces:**
- Consumes: `st7305_frame_to_panel` (Task 5), gfx (Tasks 1–3, 6), `util_crc32` (M0).
- Produces:
  - `st7305_variant_t` (`ST7305_VARIANT_FACTORY`, `ST7305_VARIANT_XIAOZHI`) and `st7305_mode_t` (`ST7305_MODE_HPM`, `ST7305_MODE_LPM`).
  - `esp_err_t st7305_init(st7305_variant_t)`, `st7305_reinit(st7305_variant_t)`, `st7305_push(const uint8_t *canonical)` (blocks until the DMA finishes), `st7305_set_mode(st7305_mode_t)`.
  - `st7305_variant_t st7305_variant(void)`, `st7305_mode_t st7305_mode(void)`.
  - `esp_err_t display_init(st7305_variant_t)`, `gfx_fb_t *display_fb(void)` (NULL until `display_init` has allocated the PSRAM framebuffer), `esp_err_t display_commit(bool force)`, `esp_err_t display_set_variant(st7305_variant_t)`.
- **Warm init** (deep-sleep wake without a panel reset) waits for M2, which is when it can be exercised.

- [ ] **Step 1: Write the device check first and watch it fail**

The current firmware draws nothing, so its log has no panel line.

Run: `tools/idf.sh exec python tools/devlog.py -p PORT --reset --until "st7305: .* init sequence" -t 10; echo "exit=$?"`
Expected: `exit=4` (`pattern ... not seen`).

- [ ] **Step 2: Write `components/st7305/include/st7305.h`**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "st7305_frame.h"

/* The two vendor init sequences (AGENTS.md gotcha 6). They differ in the source high voltages
 * (contrast), the oscillator and the frame rates. */
typedef enum {
    ST7305_VARIANT_FACTORY, /* factory firmware: SPI 10 MHz, VSHP/VSHN 0x41, HPM 16 Hz, LPM 8 Hz */
    ST7305_VARIANT_XIAOZHI, /* XiaoZhi firmware: SPI 40 MHz, VSHP 0x69, VSHN 0x4B, HPM 25.5 Hz, LPM 1 Hz */
} st7305_variant_t;

typedef enum {
    ST7305_MODE_HPM, /* high power mode: fast refresh, for interaction */
    ST7305_MODE_LPM, /* low power mode: slow refresh, for idle */
} st7305_mode_t;

/* Cold start: SPI bus, hardware reset, init sequence. The panel RAM is undefined until the first push. */
esp_err_t st7305_init(st7305_variant_t variant);
/* Resets the panel and runs another init sequence (the SPI clock follows the variant). */
esp_err_t st7305_reinit(st7305_variant_t variant);
/* Converts and sends a canonical frame (spec §4.1); blocks until the DMA transfer has finished. */
esp_err_t st7305_push(const uint8_t *canonical);
esp_err_t st7305_set_mode(st7305_mode_t mode);
st7305_variant_t st7305_variant(void);
st7305_mode_t st7305_mode(void);
```

- [ ] **Step 3: Write `components/st7305/st7305.c`**

```c
#include "st7305.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define PIN_MOSI    12
#define PIN_SCLK    11
#define PIN_DC      5
#define PIN_CS      40
#define PIN_RST     41
#define SPI_HOST_ID SPI3_HOST

static const char *TAG = "st7305";

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
    return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST_ID, &cfg, &s_io);
}

static void hardware_reset(void)
{
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static esp_err_t run_init(const init_cmd_t *cmds, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, cmds[i].cmd, cmds[i].len ? cmds[i].data : NULL,
                                                      cmds[i].len),
                            TAG, "init command 0x%02X failed", cmds[i].cmd);
        if (cmds[i].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(cmds[i].delay_ms));
        }
    }
    return ESP_OK;
}

esp_err_t st7305_init(st7305_variant_t variant)
{
    ESP_RETURN_ON_FALSE(!s_bus_ready, ESP_ERR_INVALID_STATE, TAG, "already initialised");
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
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI_HOST_ID, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");
    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "reset pin");
    s_bus_ready = true;
    return st7305_reinit(variant);
}

esp_err_t st7305_reinit(st7305_variant_t variant)
{
    ESP_RETURN_ON_FALSE(s_bus_ready, ESP_ERR_INVALID_STATE, TAG, "call st7305_init first");
    const bool xiaozhi = variant == ST7305_VARIANT_XIAOZHI;
    ESP_RETURN_ON_ERROR(create_io(xiaozhi ? 40 * 1000 * 1000 : 10 * 1000 * 1000), TAG, "panel IO");
    hardware_reset();
    if (xiaozhi) {
        ESP_RETURN_ON_ERROR(run_init(s_init_xiaozhi, sizeof(s_init_xiaozhi) / sizeof(s_init_xiaozhi[0])), TAG,
                            "XiaoZhi init");
    } else {
        ESP_RETURN_ON_ERROR(run_init(s_init_factory, sizeof(s_init_factory) / sizeof(s_init_factory[0])), TAG,
                            "factory init");
    }
    s_variant = variant;
    s_mode = ST7305_MODE_HPM;
    ESP_LOGI(TAG, "%s init sequence, SPI %d MHz", xiaozhi ? "XiaoZhi" : "factory", xiaozhi ? 40 : 10);
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

esp_err_t st7305_set_mode(st7305_mode_t mode)
{
    ESP_RETURN_ON_FALSE(s_io != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, mode == ST7305_MODE_LPM ? 0x39 : 0x38, NULL, 0), TAG,
                        "power mode");
    s_mode = mode;
    return ESP_OK;
}

st7305_variant_t st7305_variant(void)
{
    return s_variant;
}

st7305_mode_t st7305_mode(void)
{
    return s_mode;
}
```

Replace `components/st7305/CMakeLists.txt`:

```cmake
# ST7305 reflective LCD (spec §4.2). st7305_frame.c is pure C and also built on the host.
idf_component_register(SRCS "st7305.c" "st7305_frame.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_lcd)
```

- [ ] **Step 4: Write the `display` component**

`components/display/include/display.h`:

```c
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "gfx.h"
#include "st7305.h"

/* Display service (spec §4.1): owns the canonical framebuffer (in PSRAM) and pushes it to the panel. */
esp_err_t display_init(st7305_variant_t variant); /* cold start: white frame, then LPM */
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

esp_err_t display_init(st7305_variant_t variant)
{
    ESP_RETURN_ON_FALSE(s_fb.buf == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    uint8_t *buf = heap_caps_calloc(1, ST7305_FRAME_BYTES, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(buf != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer");
    gfx_fb_init(&s_fb, buf, ST7305_WIDTH, ST7305_HEIGHT);
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

`components/display/CMakeLists.txt`:

```cmake
# Display service: canonical framebuffer + panel pushes (spec §4.1).
idf_component_register(SRCS "display.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx st7305
                       PRIV_REQUIRES esp_timer util)
```

- [ ] **Step 5: Show the test pattern at boot**

Replace `main/main.c`:

```c
#include "diag.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gfx_test_pattern.h"

static const char *TAG = "main";

/* Init sequence until the owner picks one (M1 Task 9). */
#define DISPLAY_VARIANT ST7305_VARIANT_XIAOZHI

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);

    esp_err_t err = display_init(DISPLAY_VARIANT);
    if (err == ESP_OK) {
        gfx_draw_test_pattern(display_fb());
        err = display_commit(false);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display failed: %s", esp_err_to_name(err));
    }

    err = diag_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "reflbo ready");
}
```

Replace `main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES diag display esp_app_format gfx)
```

- [ ] **Step 6: Build with no warnings in our code, flash, and watch the device check pass**

```bash
tools/idf.sh build 2>&1 | tee captures/build.log | tail -1
grep "warning:" captures/build.log | grep -E "/reflbo/(main|components)/" ; echo "own-warnings-exit=$?"
tools/idf.sh -p PORT flash
tools/idf.sh exec python tools/devlog.py -p PORT --reset --until "reflbo ready" -t 20 -o captures/boot.log >/dev/null; echo "exit=$?"
grep -E "st7305:|display:|(^|reflbo> )[EW] \(" captures/boot.log
```

Expected:
- `Project build complete.`, `own-warnings-exit=1`, and `exit=0`.
- The log shows `st7305: XiaoZhi init sequence, SPI 40 MHz` and `display: first frame pushed in <N> us`.
- No `E (` or `W (` lines.
- N is in the low milliseconds; record it in the ledger.

- [ ] **Step 7: Update the docs**

- In `AGENTS.md` §5.2, add this line under `st7305/` in the component tree:

  ```text
    display/       canonical framebuffer, CRC-skipped pushes to the panel
  ```

- In spec §3.1, add a row after `st7305`, and a `util` row after `gfx` (a deferred M0 doc nit):

  ```markdown
  | `display` | Canonical framebuffer; pushes it to the panel when its CRC changes | gfx, st7305, util | — |
  ```

  ```markdown
  | `util` | Small pure-C helpers: CRC-32, base64 | — | ✓ |
  ```

- [ ] **Step 8: Commit and push**

```bash
git add components/st7305 components/display main AGENTS.md docs/specs/2026-09-25-firmware-design.md
git commit -m "feat(display): add ST7305 SPI driver and display service; show test pattern at boot"
git push
```

---

### Task 8: `screenshot` and `panel` console commands, `tools/screenshot.py`

**Files:**
- Create: `components/diag/diag_cmd_display.c`, `tools/screenshot.py`, `tools/tests/test_screenshot.py`
- Modify: `components/diag/diag_internal.h`, `components/diag/diag.c`, `components/diag/CMakeLists.txt`, `AGENTS.md` §6–§7, spec §15

**Interfaces:**
- Consumes: `display_fb`, `display_commit`, `display_set_variant`, `st7305_set_mode`, `st7305_variant`, `st7305_mode` (Task 7), `gfx_pbm_*` (Task 1), `gfx_draw_test_pattern` (Task 6), `util_base64_encode` (Task 4), `devlog.run` and `devlog.PortError` (M0), `pbm_png` (Task 6).
- Produces:
  - Console `screenshot`: prints `-----BEGIN RLCD PBM-----`, then base64 PBM in 76-character lines, then `-----END RLCD PBM-----`.
  - Console `panel status | test | clear | mode <hpm|lpm> | init <factory|xiaozhi>`.
  - `esp_err_t diag_register_display_commands(void)`.
  - `screenshot.extract_pbm(text) -> bytes` (raises ValueError).
  - CLI `tools/idf.sh exec python tools/screenshot.py [-p PORT] [-o OUT.png] [-t S] [--compare REF.pbm]`. Exit codes: devlog's, 5 when the image differs from `--compare`, 6 when no valid image arrived.

- [ ] **Step 1: Write the failing Python tests `tools/tests/test_screenshot.py`**

```python
import base64
import unittest

import screenshot

PBM = b"P4\n8 2\n\xF0\x0F"


def transcript(body_lines):
    return "\n".join(["I (100) main: reflbo ready", "reflbo> screenshot", screenshot.BEGIN, *body_lines,
                      screenshot.END, "reflbo> "])


class ExtractPbmTest(unittest.TestCase):
    def test_image_between_markers_is_decoded(self):
        encoded = base64.b64encode(PBM).decode()
        self.assertEqual(screenshot.extract_pbm(transcript([encoded[:8], encoded[8:]])), PBM)

    def test_missing_end_marker_is_rejected(self):
        text = "\n".join([screenshot.BEGIN, base64.b64encode(PBM).decode()])
        with self.assertRaises(ValueError):
            screenshot.extract_pbm(text)

    def test_noise_inside_the_image_is_rejected(self):
        encoded = base64.b64encode(PBM).decode()
        with self.assertRaises(ValueError):
            screenshot.extract_pbm(transcript([encoded[:8], "I (120) wifi: scan done", encoded[8:]]))

    def test_truncated_image_is_rejected(self):
        with self.assertRaises(ValueError):
            screenshot.extract_pbm(transcript([base64.b64encode(PBM[:-1]).decode()]))

    def test_extra_data_is_rejected(self):
        with self.assertRaises(ValueError):
            screenshot.extract_pbm(transcript([base64.b64encode(PBM).decode(), "AAAA"]))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `tools_unittests` FAILS with `No module named 'screenshot'`.

- [ ] **Step 3: Write `tools/screenshot.py`**

```python
#!/usr/bin/env python3
"""Grab the board's framebuffer over USB as a PNG (spec §15).
  tools/idf.sh exec python tools/screenshot.py -p /dev/cu.usbmodemXXXX -o captures/screen.png \
      [--compare test/host/golden/test_pattern.pbm]
Writes OUT.png and the raw OUT.pbm. Exit codes: as devlog.py, plus 5 when the image differs from
--compare and 6 when no valid image arrived.
"""
import argparse
import base64
import binascii
import io
import pathlib
import sys
from types import SimpleNamespace

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import devlog  # noqa: E402
import pbm_png  # noqa: E402

BEGIN = "-----BEGIN RLCD PBM-----"
END = "-----END RLCD PBM-----"


def extract_pbm(text):
    """Returns the PBM carried between the markers in a console transcript; raises ValueError."""
    lines = [line.strip() for line in text.splitlines()]
    try:
        start = next(i for i, line in enumerate(lines) if line.endswith(BEGIN))
        end = next(i for i in range(start + 1, len(lines)) if lines[i] == END)
    except StopIteration:
        raise ValueError("no complete screenshot in the console output") from None
    try:
        data = base64.b64decode("".join(lines[start + 1:end]), validate=True)
    except binascii.Error as err:
        raise ValueError(f"corrupt screenshot data: {err}") from None
    width, height, raster = pbm_png.parse_pbm(data)
    if len(data) != len(f"P4\n{width} {height}\n") + len(raster):
        raise ValueError("screenshot data has the wrong length")  # e.g. base64-looking noise got in
    return data


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--port", help=f"serial port (default: the only {devlog.PORT_GLOB})")
    parser.add_argument("-o", "--out", default="captures/screen.png", help="PNG to write (the .pbm lands next to it)")
    parser.add_argument("-t", "--seconds", type=float, default=15.0, help="give up after this many seconds")
    parser.add_argument("--compare", help="PBM the screenshot must equal byte for byte")
    args = parser.parse_args(argv)

    transcript = io.StringIO()
    session = SimpleNamespace(port=args.port, seconds=args.seconds, until=None, cmd=["screenshot"], reset=False,
                              out=None)
    try:
        code = devlog.run(session, out=transcript)
    except devlog.PortError as err:
        print(f"screenshot: {err}", file=sys.stderr)
        return 2
    if code != 0:
        return code
    try:
        pbm = extract_pbm(transcript.getvalue())
    except ValueError as err:
        print(f"screenshot: {err}", file=sys.stderr)
        return 6

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.with_suffix(".pbm").write_bytes(pbm)
    out.write_bytes(pbm_png.png_from_pbm(pbm))
    print(f"screenshot: {out}")
    if args.compare:
        if pathlib.Path(args.compare).read_bytes() != pbm:
            print(f"screenshot: differs from {args.compare}", file=sys.stderr)
            return 5
        print(f"screenshot: identical to {args.compare}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the Python tests and confirm they pass**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 8`.

- [ ] **Step 5: Run the device check first and watch it fail**

Run: `tools/idf.sh exec python tools/screenshot.py -p PORT -o captures/screen.png; echo "exit=$?"`
Expected: `exit=6` (`no complete screenshot`). The Task 7 firmware answers `unknown command: screenshot`.

- [ ] **Step 6: Write `components/diag/diag_cmd_display.c`**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag_internal.h"
#include "display.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "gfx_test_pattern.h"
#include "util_base64.h"

#define PBM_CHUNK 57 /* bytes per line: 76 base64 characters */

static int cmd_screenshot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        printf("screenshot: display not initialised\n");
        return 1;
    }
    size_t size = gfx_pbm_size(fb);
    uint8_t *pbm = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (pbm == NULL) {
        printf("screenshot: out of memory\n");
        return 1;
    }
    size_t len = gfx_pbm_encode(fb, pbm, size);
    char line[80];
    printf("-----BEGIN RLCD PBM-----\n");
    for (size_t off = 0; off < len; off += PBM_CHUNK) {
        size_t chunk = len - off < PBM_CHUNK ? len - off : PBM_CHUNK;
        util_base64_encode(pbm + off, chunk, line, sizeof(line));
        printf("%s\n", line);
    }
    printf("-----END RLCD PBM-----\n");
    free(pbm);
    return 0;
}

static int print_status(void)
{
    printf("panel %s, %s\n", st7305_variant() == ST7305_VARIANT_XIAOZHI ? "xiaozhi" : "factory",
           st7305_mode() == ST7305_MODE_LPM ? "lpm" : "hpm");
    return 0;
}

static int cmd_panel(int argc, char **argv)
{
    esp_err_t err = ESP_ERR_INVALID_ARG;
    gfx_fb_t *fb = display_fb();
    if (fb == NULL) {
        printf("panel: display not initialised\n");
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        return print_status();
    } else if (argc == 2 && strcmp(argv[1], "test") == 0) {
        gfx_draw_test_pattern(fb);
        err = display_commit(false);
    } else if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        gfx_clear(fb, GFX_WHITE);
        err = display_commit(false);
    } else if (argc == 3 && strcmp(argv[1], "mode") == 0 && strcmp(argv[2], "hpm") == 0) {
        err = st7305_set_mode(ST7305_MODE_HPM);
    } else if (argc == 3 && strcmp(argv[1], "mode") == 0 && strcmp(argv[2], "lpm") == 0) {
        err = st7305_set_mode(ST7305_MODE_LPM);
    } else if (argc == 3 && strcmp(argv[1], "init") == 0 && strcmp(argv[2], "factory") == 0) {
        err = display_set_variant(ST7305_VARIANT_FACTORY);
    } else if (argc == 3 && strcmp(argv[1], "init") == 0 && strcmp(argv[2], "xiaozhi") == 0) {
        err = display_set_variant(ST7305_VARIANT_XIAOZHI);
    } else {
        printf("usage: panel status | test | clear | mode <hpm|lpm> | init <factory|xiaozhi>\n");
        return 1;
    }
    if (err != ESP_OK) {
        printf("panel: %s\n", esp_err_to_name(err));
        return 1;
    }
    return print_status();
}

esp_err_t diag_register_display_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "screenshot", .help = "Print the framebuffer as base64 PBM between markers", .func = &cmd_screenshot },
        { .command = "panel", .help = "panel status | test | clear | mode <hpm|lpm> | init <factory|xiaozhi>",
          .func = &cmd_panel },
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

Also make these edits:
- `components/diag/diag_internal.h`: add `esp_err_t diag_register_display_commands(void); /* screenshot, panel */` below `diag_register_system_commands`.
- `components/diag/diag.c`: after the system-commands line, add `ESP_RETURN_ON_ERROR(diag_register_display_commands(), TAG, "display commands");`.
- `components/diag/CMakeLists.txt`: add `"diag_cmd_display.c"` to `SRCS`, and `display gfx util` to `PRIV_REQUIRES`.

- [ ] **Step 7: Build, flash, and watch the device check pass**

```bash
tools/idf.sh build 2>&1 | tee captures/build.log | tail -1
grep "warning:" captures/build.log | grep -E "/reflbo/(main|components)/" ; echo "own-warnings-exit=$?"
tools/idf.sh -p PORT flash
tools/idf.sh exec python tools/screenshot.py -p PORT -o captures/screen.png --compare test/host/golden/test_pattern.pbm; echo "exit=$?"
```

Expected:
- `own-warnings-exit=1`.
- `screenshot: identical to test/host/golden/test_pattern.pbm` and `exit=0`: the device drew exactly the host rendering.
- Look at `captures/screen.png`.

- [ ] **Step 8: Exercise the panel commands**

```bash
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "panel status" --cmd "panel mode hpm" --cmd "panel init factory" --cmd "panel init xiaozhi" --cmd "panel mode lpm" -t 20 | grep -E "^panel|usage"
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "panel clear" -t 10 >/dev/null && tools/idf.sh exec python tools/screenshot.py -p PORT -o captures/clear.png --compare test/host/golden/test_pattern.pbm; echo "exit=$?"
tools/idf.sh exec python tools/devlog.py -p PORT --cmd "panel test" -t 10 >/dev/null && tools/idf.sh exec python tools/screenshot.py -p PORT --compare test/host/golden/test_pattern.pbm; echo "exit=$?"
```

Expected:
- Status lines, in order: `panel xiaozhi, lpm`, `panel xiaozhi, hpm`, `panel factory, lpm`, `panel xiaozhi, lpm`, `panel xiaozhi, lpm` (`panel init` ends in LPM).
- After `panel clear` the comparison fails with `exit=5`.
- After `panel test` it succeeds again with `exit=0`.

- [ ] **Step 9: Update the docs**

- `AGENTS.md` §6: drop the `# (planned, M1)` marker from the `tools/screenshot.py` line, and add `--compare REF.pbm` and exit codes 5 (differs) and 6 (no valid image) to the devlog exit-code bullet as a screenshot.py note.
- `AGENTS.md` §7:
  - In the **Screenshots** paragraph, replace `*(planned, M1)*` with `(M1)`.
  - In the **Diagnostics console** paragraph, change "Available now: `help`, `version`, `heap`, `reboot`" to "Available now: `help`, `version`, `heap`, `reboot`, `screenshot`, `panel status|test|clear|mode <hpm|lpm>|init <factory|xiaozhi>`", and remove `screenshot` from the planned list.
- Spec §15: add a console row `| \`panel status\` · \`panel test\` · \`panel clear\` · \`panel mode <hpm\|lpm>\` · \`panel init <factory\|xiaozhi>\` | Panel diagnostics: test pattern, power mode, init sequence |`.

- [ ] **Step 10: Commit and push**

```bash
git add components/diag tools/screenshot.py tools/tests/test_screenshot.py AGENTS.md docs/specs/2026-09-25-firmware-design.md
git commit -m "feat(diag): add screenshot and panel console commands with PNG capture tool"
git push
```

---

### Task 9: Owner check on the physical panel, and recording the choice

**Files:**
- Modify: `main/main.c` (the chosen `DISPLAY_VARIANT`), `AGENTS.md` (§2, §3.4 gotcha 6, §9), spec (§4.2, §21)

**Interfaces:**
- Consumes: the `panel` commands (Task 8).

- [ ] **Step 1: Ask the owner about orientation and variant A**

Set variant A and HPM: `tools/idf.sh exec python tools/devlog.py -p PORT --cmd "panel init xiaozhi" --cmd "panel mode lpm" -t 15`.

Then use AskUserQuestion with three questions:
1. Is "TL" at the top-left, with a small black square just inside the top-left border, and is the Czech line readable?
2. How is the contrast (crisp and dark / too faint / too dark or smeared)?
3. Is there visible flicker or pulsing? The panel is now in low-power mode, refreshing at 1 Hz.

- [ ] **Step 2: Ask the owner about variant B**

Run `tools/idf.sh exec python tools/devlog.py -p PORT --cmd "panel init factory" --cmd "panel mode lpm" -t 15`. Then ask: compared with A, is B's contrast better, the same, or worse? Is there flicker? (Its low-power refresh is 8 Hz.)

- [ ] **Step 3: Record the result**

- **If orientation is wrong:** stop, debug with systematic-debugging (frame conversion vs `MADCTL`), and repeat Steps 1–2.
- **Otherwise:**
  - Set `DISPLAY_VARIANT` in `main/main.c` to the winner. On a tie, keep XiaoZhi: its 1 Hz low-power refresh draws less power (spec §4.2).
  - Update AGENTS gotcha 6 to say which sequence was chosen and why.
  - Add decision D12 to AGENTS §9 and the spec: "Panel init sequence: <variant> (owner check 2026-09-25)".
  - Add a row to the spec §21 revision history.
- Build, flash, and run the screenshot compare once more (`exit=0`).

- [ ] **Step 4: Mark M1 done and verify**

- In `AGENTS.md` §2, set the status bullet to M1 done, with M2 next.
- Run the full host suite and the device screenshot compare.

Expected: `100% tests passed … out of 8`, and `screenshot: identical to test/host/golden/test_pattern.pbm`.

- [ ] **Step 5: Commit and push**

```bash
git add main/main.c AGENTS.md docs/specs/2026-09-25-firmware-design.md
git commit -m "feat(display): pick the <variant> init sequence after the panel check"
git push
```
