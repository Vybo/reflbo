# M4: Wi-Fi Setup, Web Configurator and OTA, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** M4 (spec §18): the device's own Wi-Fi network with a captive portal, config mode with its QR screen, the web configurator with its API and a live preset preview, setting the time from a phone, and firmware updates with rollback; plus the owner's M4 decisions (D18, D19).

**Architecture:**

- **Pure logic, host-tested:**
  - `util`: SHA-256, HMAC-SHA256, PBKDF2-SHA256 and a constant-time compare, for the web password.
  - `gfx`: 1-bit BMP images for browsers, and QR codes through Nayuki's qrcodegen (MIT, vendored unmodified).
  - `locale` and `ui`: the menu's Wi-Fi section and Info rows; the config-mode and first-run screens with their QR codes; the preset editor's catalogue of layouts and fields as JSON.
  - `storage`: the location settings, the settings merge patch (RFC 7396) and the backup bundle.
  - `netmgr`: the captive DNS reply, the saved-network list, and what a scan decides (the AP's channel, the network to join).
  - `webui`: the password record, sessions and login throttle, and small HTTP helpers.
  - `tools/gen_zones.py`: every tz database zone with its POSIX rule, for the web UI.
- **Device side:**
  - `netmgr`: a task that joins saved networks or runs the AP with captive DNS and mDNS, scans, and tests a network in the background.
  - `webui`: the HTTP server with the embedded pages, the password routes, the Wi-Fi and OTA routes; every other API route runs on the app task through `main`.
  - `web/`: the pages, plain HTML, CSS and JavaScript, gzipped into the app image.
  - `main`: config mode (`app_config.c`), the API routes (`app_web.c`), the first run, the menu's Wi-Fi actions, OTA validity and rollback, restarts that return in config mode, and the `wifi` console command.

**Tech stack:** ESP-IDF v5.5.5 with `esp_wifi`, `esp_netif`, `esp_http_server`, `app_update` and its bundled cJSON; `espressif/mdns` 1.13.1 (new, from the component registry); `joltwallet/littlefs` 1.22.3 as before. Unity on the host. The web UI has no build step and loads nothing from the internet: the device's own network has none.

**Spec:** `docs/specs/2026-09-25-firmware-design.md` r14. Task 11 brings it to r15.
- Relevant: §5.2, §5.5–§5.7, §8, §10, §14.2–§14.4, §15, §17, §18 (M4), D9, D18 and D19.
- Also `AGENTS.md` §3.4 (gotchas 10, 11, 20, 22), §5.3, §6–§8.

**Research behind this plan (2026-09-30):**
- A spike in a throwaway worktree built every task below, and the owner reviewed its screens and pages on 2026-09-30 (D19). On the board, with this Mac joined to the device's network (owner's permission, 2026-09-30), it confirmed:
  - **Config mode**: BOOT held 3 s and Menu ▸ Wi-Fi ▸ Config mode turn Wi-Fi on; KEY switches the QR code; BOOT held ends it with a toast. With no page request it ended between 9 min 45 s and 11 min, with the Mac still joined.
  - **The portal**: the DNS answers every name with 192.168.4.1; Apple's and Android's probe URLs redirect to the page; `reflbo-bb94.local` resolves.
  - **The password**: set on the first visit from the device's network, refused from elsewhere; a login takes about 2 s; a wrong one says so; sessions end with config mode; the menu's reset clears it.
  - **Every API route**, with curl; the pages in headless Chrome. The live preview redrew a preset moved to another layout at once.
  - **Testing a network**: three tests of a network out of reach answered "not found" in about 3 s each, and the Mac stayed on the device's network.
  - **OTA**: a 1.3 MB image uploaded in 11.5 s, booted as pending and was valid after 60 s. A build that crashed after 25 s rolled back to the previous image, which came back in config mode and reported it.
  - **Time zones**: newlib reads the angle-bracket rules (`<+04>-4`, `<-03>3`) correctly.
- Found on the board and fixed in the spike (the tasks below carry the fixes):
  - Chrome ran lwIP out of its 10 sockets: `accept()` failed with ENFILE (gotcha 25).
  - Re-applying the AP's settings during a test dropped the phone; so did a test's connect scan (gotcha 26).
  - A joined phone's connectivity probes kept config mode on forever (gotcha 27).
  - 46 KB of new static buffers sat in internal RAM; they are in PSRAM now, and 113 KB of internal RAM stays free with Wi-Fi on.
  - `tools/gen_zones.py` failed on a zone missing from the zoneinfo tree, and misread an empty footer; its tests pin both.
- **Not checked on the board:** a real phone (the iOS sign-in sheet, Android's notification), joining the owner's network with its password, and the first-run screen, which needs a factory reset. They are in the owner acceptance at the end.

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, Python 3 host tools, plain HTML/CSS/JS in `web/` with no build step and no external resources.
- `[host]` components (`util`, `gfx`, `locale`, `datastore`, `ui`, `scheduler`) and the pure files named per task include no ESP-IDF headers. cJSON counts as plain C.
- ESP-IDF style: 4-space indent, `snake_case`, a component or module prefix on public APIs, public headers in `include/`. Lines stay within 120 characters.
- The app task owns the display, `st7305`, the I²C devices, storage, the datastore, sleep, the menu, toasts and config mode (spec §3.2, AGENTS §5.3). netmgr and webui run in their own tasks and reach the app only through `app_execute()` (waits) and `app_post()` (doesn't).
- **Secrets.** Never commit or log a Wi-Fi password, the AP password or the web password. A board check that sets a web password clears it again with Menu ▸ Wi-Fi ▸ Reset web password, so the owner's first visit chooses theirs.
- **This Mac's Wi-Fi** may join the board's AP for checks (owner, 2026-09-30): `networksetup -setairportnetwork en0 reflbo-XXXX <password from the screen>`. Afterwards remove it with `networksetup -removepreferredwirelessnetwork en0 reflbo-XXXX`.
- Never erase flash or NVS without asking. A factory reset (menu or web UI) erases `storage` and the NVS namespaces `wifi`, `secrets` and `ctr` (spec §14.4): only with the owner's agreement.
  - Board commands go through `tools/idf.sh` with an explicit `-p`.
  - The port must be confirmed as this board: Espressif `303A:1001` with the USB serial number `14:C1:9F:54:BB:94` (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'`), and `flash` prints `MAC: 14:c1:9f:54:bb:94`.
- List every component source in `SRCS`. Run `tools/idf.sh reconfigure` after adding a component directory. Dependencies are pinned in `idf_component.yml`; commit `dependencies.lock`; never edit `managed_components/`. After a change to `sdkconfig.defaults`, delete `sdkconfig` and build.
- Large buffers go in PSRAM (`MALLOC_CAP_SPIRAM`, or `EXT_RAM_BSS_ATTR` for static ones); Wi-Fi needs the internal RAM.
- Small, focused Conventional Commits that each build. Push to `origin` freely; never force-push. No AI or assistant attribution anywhere.
- Build only what the spec covers (r14, D18 and D19). Not in M4: the status bar's Wi-Fi state (M5, M6), `/api/geocode` and `/api/sync` (M5), web UI translations (spec §5.8).
- Czech text follows Czech typography (decimal comma, en dash), and every string's glyphs must exist in the fonts (`test_lang_glyphs`).

## Review Focus

1. **A phone on the device's network while the device tries another**: a network out of reach, a wrong password, the right network on another channel. Expected: a network out of reach never drops the phone; the page reports every result, waiting while the phone rejoins; after a failure the device is back where it was. Pinned by:
   - `test_the_ap_starts_on_the_channel_of_the_likely_network` (Task 6);
   - the scan-first `do_test()` and the AP left alone (Task 8);
   - `tryNetwork()` polling through a lost connection (Task 9);
   - the board's three tests of a missing network (Task 10);
   - owner check 1.
2. **Browsers behaving like browsers**: parallel connections, long headers, connectivity probes from iOS, Android and macOS, a page left open. Expected: no empty replies, no 431, probes redirected without keeping config mode on, and config mode ending 10 min after the last page request. Pinned by:
   - `CONFIG_LWIP_MAX_SOCKETS` and `CONFIG_HTTPD_MAX_REQ_HDR_LEN` (Task 10);
   - `asset_handler()` and `fallback_handler()` touching only for the device's own pages (Task 9);
   - the board's Chrome and idle-timeout checks (Task 10);
   - owner check 1.
3. **An update that goes wrong**: an upload that breaks off, a file that isn't reflbo firmware, an image that crashes before it has proved itself. Expected: the running image stays put; a crashing image rolls back, and the old one returns in config mode and says so; nothing deep-sleeps while an image is pending. Pinned by:
   - the header and hash checks in `ota_upload()` (Task 9);
   - `check_ota()`, the pending state in `power_plan()` and the resume flag (Task 10);
   - the board's crash-build check (Task 10).
4. **Careless or hostile requests**: bad JSON, deep nesting, oversized bodies, a form post from another site, choosing the password from the LAN, guessing it, a restore file from another firmware. Expected: a 4xx with a reason, nothing half-applied, no crash. Pinned by:
   - `test_webui_auth` (the throttle) and `test_webui_http` (the content type) (Task 7);
   - `test_a_patch_must_be_an_object_that_keeps_the_schema` and `test_restore_refuses_anything_but_a_backup` (Task 5);
   - the 413 and 415 answers in `api_handler()` (Task 9);
   - the board's curl checks (Task 10).
5. **Config mode meeting the rest of the device**: the deep-sleep idle strategy, a night that comes due, a critical battery, the first run, the menu. Expected: awake all through config mode; a night waits until it ends; a critical battery ends it; config mode ends the first run; no battery samples meanwhile. Pinned by:
   - the config-mode and pending-image terms of `pending` in `app_task()`, and `app_config_tick()` (Task 10);
   - the board's forced-sleep check (Task 10).

---

### Task 1: SHA-256, HMAC and PBKDF2 (`util`)

**Files:**
- Create: `components/util/include/util_sha256.h`, `components/util/util_sha256.c`, `test/host/test_util_sha256.c`
- Modify: `components/util/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing new.
- Produces (Task 7 builds the password record on them):
  - `void util_sha256(const void *data, size_t len, uint8_t out[UTIL_SHA256_LEN])`, and the streaming `util_sha256_init()`, `util_sha256_update()`, `util_sha256_final()` on a `util_sha256_t`.
  - `void util_hmac_sha256(const void *key, size_t key_len, const void *data, size_t len, uint8_t out[UTIL_SHA256_LEN])`.
  - `void util_pbkdf2_sha256(const void *pass, size_t pass_len, const void *salt, size_t salt_len, uint32_t iterations, uint8_t *out, size_t out_len)`.
  - `bool util_ct_equal(const void *a, const void *b, size_t len)`: compares in time independent of the contents.

- [ ] **Step 1: Write the failing test.** The vectors are FIPS 180-2 (SHA-256), RFC 4231 test case 2 (HMAC) and RFC 7914 §11 (PBKDF2-HMAC-SHA256).

```c
#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "util_sha256.h"

void setUp(void) {}
void tearDown(void) {}

static void check_hex(const char *expected, const uint8_t *bytes, size_t len)
{
    char hex[2 * 64 + 1];
    for (size_t i = 0; i < len; i++) {
        snprintf(hex + 2 * i, 3, "%02x", bytes[i]);
    }
    TEST_ASSERT_EQUAL_STRING(expected, hex);
}

/* FIPS 180-4 examples, checked against Python's hashlib. */
static void test_sha256_of_the_standard_messages(void)
{
    uint8_t out[32];
    util_sha256("abc", 3, out);
    check_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", out, 32);
    util_sha256("", 0, out);
    check_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", out, 32);
    const char *two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    util_sha256(two_blocks, strlen(two_blocks), out);
    check_hex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", out, 32);
    util_sha256_t s; /* a million 'a' in uneven pieces */
    util_sha256_init(&s);
    static uint8_t chunk[997];
    memset(chunk, 'a', sizeof(chunk));
    size_t left = 1000000;
    while (left > 0) {
        size_t n = left < sizeof(chunk) ? left : sizeof(chunk);
        util_sha256_update(&s, chunk, n);
        left -= n;
    }
    util_sha256_final(&s, out);
    check_hex("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", out, 32);
}

/* RFC 4231 test case 2. */
static void test_hmac_sha256_of_rfc_4231(void)
{
    uint8_t out[32];
    const char *data = "what do ya want for nothing?";
    util_hmac_sha256("Jefe", 4, data, strlen(data), out);
    check_hex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", out, 32);
}

/* RFC 7914 §11. */
static void test_pbkdf2_sha256_of_rfc_7914(void)
{
    uint8_t out[64];
    util_pbkdf2_sha256("passwd", 6, "salt", 4, 1, out, sizeof(out));
    check_hex("55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
              "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783",
              out, sizeof(out));
    util_pbkdf2_sha256("Password", 8, "NaCl", 4, 80000, out, sizeof(out));
    check_hex("4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
              "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d",
              out, sizeof(out));
}

static void test_constant_time_equal(void)
{
    TEST_ASSERT_TRUE(util_ct_equal("abcd", "abcd", 4));
    TEST_ASSERT_FALSE(util_ct_equal("abcd", "abce", 4));
    TEST_ASSERT_TRUE(util_ct_equal("", "", 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_of_the_standard_messages);
    RUN_TEST(test_hmac_sha256_of_rfc_4231);
    RUN_TEST(test_pbkdf2_sha256_of_rfc_7914);
    RUN_TEST(test_constant_time_equal);
    return UNITY_END();
}
```

- [ ] **Step 2: Register it and watch it fail.** In `test/host/CMakeLists.txt`, after `reflbo_host_test(test_util_json util)`:

```cmake
reflbo_host_test(test_util_sha256 util)
```

Run: `cmake --build build-host`
Expected: FAIL to compile: `util_sha256.h` not found.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 2104) and PBKDF2-HMAC-SHA256 (RFC 8018), for the web UI
 * password's salted hash (spec §10.4). Pure C, host-buildable. */

#define UTIL_SHA256_LEN 32

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} util_sha256_t;

void util_sha256_init(util_sha256_t *s);
void util_sha256_update(util_sha256_t *s, const void *data, size_t len);
void util_sha256_final(util_sha256_t *s, uint8_t out[UTIL_SHA256_LEN]);
void util_sha256(const void *data, size_t len, uint8_t out[UTIL_SHA256_LEN]);
void util_hmac_sha256(const void *key, size_t key_len, const void *data, size_t len, uint8_t out[UTIL_SHA256_LEN]);
void util_pbkdf2_sha256(const void *pass, size_t pass_len, const void *salt, size_t salt_len, uint32_t iterations,
                        uint8_t *out, size_t out_len);
/* Compares in time that doesn't depend on where the bytes differ. */
bool util_ct_equal(const void *a, const void *b, size_t len);
```

```c
#include "util_sha256.h"

#include <string.h>

static const uint32_t k_round[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotr(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void compress(uint32_t h[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 | (uint32_t)block[4 * i + 2] << 8 |
               (uint32_t)block[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k_round[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += k;
}

void util_sha256_init(util_sha256_t *s)
{
    static const uint32_t k_init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, k_init, sizeof(k_init));
    s->bytes = 0;
    s->used = 0;
}

void util_sha256_update(util_sha256_t *s, const void *data, size_t len)
{
    const uint8_t *p = data;
    s->bytes += len;
    while (len > 0) {
        size_t take = 64 - s->used < len ? 64 - s->used : len;
        memcpy(s->block + s->used, p, take);
        s->used += take;
        p += take;
        len -= take;
        if (s->used == 64) {
            compress(s->h, s->block);
            s->used = 0;
        }
    }
}

void util_sha256_final(util_sha256_t *s, uint8_t out[UTIL_SHA256_LEN])
{
    uint64_t bits = s->bytes * 8;
    uint8_t pad = 0x80;
    util_sha256_update(s, &pad, 1);
    pad = 0;
    while (s->used != 56) {
        util_sha256_update(s, &pad, 1);
    }
    uint8_t len[8];
    for (int i = 0; i < 8; i++) {
        len[i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    util_sha256_update(s, len, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(s->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)s->h[i];
    }
}

void util_sha256(const void *data, size_t len, uint8_t out[UTIL_SHA256_LEN])
{
    util_sha256_t s;
    util_sha256_init(&s);
    util_sha256_update(&s, data, len);
    util_sha256_final(&s, out);
}

/* The inner and outer states after the padded key, reused across PBKDF2's iterations. */
typedef struct {
    util_sha256_t inner, outer;
} hmac_t;

static void hmac_init(hmac_t *m, const void *key, size_t key_len)
{
    uint8_t k[64] = { 0 };
    if (key_len > 64) {
        util_sha256(key, key_len, k);
    } else {
        memcpy(k, key, key_len);
    }
    uint8_t pad[64];
    for (int i = 0; i < 64; i++) {
        pad[i] = k[i] ^ 0x36;
    }
    util_sha256_init(&m->inner);
    util_sha256_update(&m->inner, pad, 64);
    for (int i = 0; i < 64; i++) {
        pad[i] = k[i] ^ 0x5c;
    }
    util_sha256_init(&m->outer);
    util_sha256_update(&m->outer, pad, 64);
}

static void hmac_run(const hmac_t *m, const void *data, size_t len, const void *data2, size_t len2,
                     uint8_t out[UTIL_SHA256_LEN])
{
    util_sha256_t s = m->inner;
    util_sha256_update(&s, data, len);
    util_sha256_update(&s, data2, len2);
    uint8_t inner[UTIL_SHA256_LEN];
    util_sha256_final(&s, inner);
    s = m->outer;
    util_sha256_update(&s, inner, sizeof(inner));
    util_sha256_final(&s, out);
}

void util_hmac_sha256(const void *key, size_t key_len, const void *data, size_t len, uint8_t out[UTIL_SHA256_LEN])
{
    hmac_t m;
    hmac_init(&m, key, key_len);
    hmac_run(&m, data, len, NULL, 0, out);
}

void util_pbkdf2_sha256(const void *pass, size_t pass_len, const void *salt, size_t salt_len, uint32_t iterations,
                        uint8_t *out, size_t out_len)
{
    hmac_t m;
    hmac_init(&m, pass, pass_len);
    for (uint32_t block = 1; out_len > 0; block++) {
        uint8_t index[4] = { (uint8_t)(block >> 24), (uint8_t)(block >> 16), (uint8_t)(block >> 8), (uint8_t)block };
        uint8_t u[UTIL_SHA256_LEN], t[UTIL_SHA256_LEN];
        hmac_run(&m, salt, salt_len, index, sizeof(index), u);
        memcpy(t, u, sizeof(t));
        for (uint32_t i = 1; i < iterations; i++) {
            hmac_run(&m, u, sizeof(u), NULL, 0, u);
            for (int j = 0; j < UTIL_SHA256_LEN; j++) {
                t[j] ^= u[j];
            }
        }
        size_t take = out_len < sizeof(t) ? out_len : sizeof(t);
        memcpy(out, t, take);
        out += take;
        out_len -= take;
    }
}

bool util_ct_equal(const void *a, const void *b, size_t len)
{
    const volatile uint8_t *x = a, *y = b;
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= x[i] ^ y[i];
    }
    return diff == 0;
}
```

```diff
diff --git a/components/util/CMakeLists.txt b/components/util/CMakeLists.txt
index 3a4b30e..21596da 100644
--- a/components/util/CMakeLists.txt
+++ b/components/util/CMakeLists.txt
@@ -1,4 +1,4 @@
 # Small pure-C helpers shared by the firmware and the host tests (no ESP-IDF headers).
 idf_component_register(SRCS "util_crc32.c" "util_base64.c" "util_snapshot.c" "util_ticks.c" "util_time.c"
-                            "util_calendar.c" "util_json.c"
+                            "util_calendar.c" "util_json.c" "util_sha256.c"
                        INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests.**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure -R test_util_sha256`
Expected: PASS, 4 tests.

- [ ] **Step 5: Commit.**

```bash
git add components/util test/host/test_util_sha256.c test/host/CMakeLists.txt
git commit -m "feat(util): add SHA-256, HMAC-SHA256 and PBKDF2-SHA256"
```

---

### Task 2: BMP images and QR codes (`gfx`)

**Files:**
- Create: `components/gfx/qrcodegen/qrcodegen.c`, `components/gfx/qrcodegen/qrcodegen.h` (vendored), `components/gfx/gfx_bmp.c`, `components/gfx/gfx_qr.c`, `test/host/test_gfx_bmp_qr.c`
- Modify: `components/gfx/include/gfx.h`, `components/gfx/CMakeLists.txt`, `test/host/CMakeLists.txt`, `THIRD_PARTY.md` (in Task 11)

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `size_t gfx_bmp_size(const gfx_fb_t *fb)` and `size_t gfx_bmp_encode(const gfx_fb_t *fb, uint8_t *out, size_t out_size)`: a 1-bit BMP, bottom-up rows padded to 4 bytes, palette 0 white and 1 black; 15 662 bytes for 400×300. The web UI's preview and screenshot (Task 10).
  - `int gfx_qr(gfx_fb_t *fb, int x, int y, int scale, const char *text)` and `int gfx_qr_side(const char *text, int scale)`: ECC medium, versions up to 10, with the 4-module quiet zone; the side in pixels, or 0 if the text doesn't fit. Not reentrant (one encoding buffer). The config screen (Task 3).

- [ ] **Step 1: Vendor qrcodegen.** Nayuki's QR Code generator library, MIT, unmodified:

```bash
mkdir -p components/gfx/qrcodegen
for f in qrcodegen.c qrcodegen.h; do
  curl -fsSL -o components/gfx/qrcodegen/$f \
    https://raw.githubusercontent.com/nayuki/QR-Code-generator/3c6d0b3cefb4e049dc337e82237c9644399716a8/c/$f
done
shasum -a 256 components/gfx/qrcodegen/*
```

Expected:
- `6a2b9cc65176f2345dde260c74b6d352627e8a0a6385d086ae0e9c5d0913c70c  components/gfx/qrcodegen/qrcodegen.c`
- `e82df4bff37d18b5863b9e7486fe6bda1b6cda8c3b9ecebfec473907265cb589  components/gfx/qrcodegen/qrcodegen.h`

Task 11 adds its row to `THIRD_PARTY.md`.

- [ ] **Step 2: Write the failing test.**

```c
#include <string.h>

#include "gfx.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void test_a_bmp_has_its_headers_and_rows_bottom_up(void)
{
    static uint8_t buf[4];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 16, 2);
    gfx_clear(&fb, GFX_WHITE);
    gfx_pixel(&fb, 0, 0, GFX_BLACK);  /* top-left */
    gfx_pixel(&fb, 15, 1, GFX_BLACK); /* bottom-right */
    uint8_t out[80];
    TEST_ASSERT_EQUAL_UINT(70, gfx_bmp_size(&fb)); /* 62 bytes of headers, two 4-byte rows */
    TEST_ASSERT_EQUAL_UINT(70, gfx_bmp_encode(&fb, out, sizeof(out)));
    TEST_ASSERT_EQUAL_MEMORY("BM", out, 2);
    TEST_ASSERT_EQUAL_UINT32(70, le32(out + 2));
    TEST_ASSERT_EQUAL_UINT32(62, le32(out + 10));
    TEST_ASSERT_EQUAL_UINT32(16, le32(out + 18));
    TEST_ASSERT_EQUAL_UINT32(2, le32(out + 22));
    TEST_ASSERT_EQUAL_UINT8(1, out[28]); /* 1 bit per pixel */
    TEST_ASSERT_EQUAL_MEMORY("\xFF\xFF\xFF\x00\x00\x00\x00\x00", out + 54, 8);
    const uint8_t bottom[] = { 0x00, 0x01, 0x00, 0x00 }, top[] = { 0x80, 0x00, 0x00, 0x00 };
    TEST_ASSERT_EQUAL_MEMORY(bottom, out + 62, 4); /* the last row comes first */
    TEST_ASSERT_EQUAL_MEMORY(top, out + 66, 4);
    TEST_ASSERT_EQUAL_UINT(0, gfx_bmp_encode(&fb, out, 69));
}

static void test_the_screen_as_bmp_is_15662_bytes(void)
{
    static uint8_t buf[400 * 300 / 8];
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    TEST_ASSERT_EQUAL_UINT(62 + 52 * 300, gfx_bmp_size(&fb));
}

/* A URL fits version 2 (25 modules); with the 4-module quiet zone at 2 px a module it is 66 px. */
static void test_a_qr_code_is_drawn_with_its_quiet_zone_and_finders(void)
{
    static uint8_t buf[13 * 100]; /* stride 13: (100 + 7) / 8 */
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 100, 100);
    gfx_clear(&fb, GFX_BLACK);
    TEST_ASSERT_EQUAL_INT(66, gfx_qr_side("http://192.168.4.1", 2));
    TEST_ASSERT_EQUAL_INT(66, gfx_qr(&fb, 10, 10, 2, "http://192.168.4.1"));
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 9, 9));        /* outside: untouched */
    TEST_ASSERT_FALSE(gfx_get_pixel(&fb, 10, 10));     /* the quiet zone is white */
    TEST_ASSERT_FALSE(gfx_get_pixel(&fb, 17, 17));
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 18, 18));      /* the top-left finder's corner */
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 18 + 6 * 2, 18));  /* its top-right corner */
    TEST_ASSERT_FALSE(gfx_get_pixel(&fb, 18 + 2, 18 + 2)); /* its white ring */
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 18 + 18 * 2, 18)); /* the top-right finder */
    TEST_ASSERT_TRUE(gfx_get_pixel(&fb, 18, 18 + 18 * 2)); /* the bottom-left finder */
}

static void test_text_too_long_for_a_qr_code_draws_nothing(void)
{
    static uint8_t buf[13 * 100]; /* stride 13: (100 + 7) / 8 */
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 100, 100);
    char text[301];
    memset(text, 'x', 300);
    text[300] = '\0';
    TEST_ASSERT_EQUAL_INT(0, gfx_qr_side(text, 1));
    TEST_ASSERT_EQUAL_INT(0, gfx_qr(&fb, 0, 0, 1, text));
    TEST_ASSERT_EQUAL_INT(0, gfx_qr_side(NULL, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_bmp_has_its_headers_and_rows_bottom_up);
    RUN_TEST(test_the_screen_as_bmp_is_15662_bytes);
    RUN_TEST(test_a_qr_code_is_drawn_with_its_quiet_zone_and_finders);
    RUN_TEST(test_text_too_long_for_a_qr_code_draws_nothing);
    return UNITY_END();
}
```

- [ ] **Step 3: Register it and watch it fail.** In `test/host/CMakeLists.txt`, the gfx library also globs the vendored sources, with their header private:

```cmake
file(GLOB GFX_SOURCES CONFIGURE_DEPENDS
     ${REPO_ROOT}/components/gfx/*.c ${REPO_ROOT}/components/gfx/fonts/*.c ${REPO_ROOT}/components/gfx/icons/*.c
     ${REPO_ROOT}/components/gfx/qrcodegen/*.c)
add_library(gfx STATIC ${GFX_SOURCES})
target_include_directories(gfx PUBLIC ${REPO_ROOT}/components/gfx/include
                           PRIVATE ${REPO_ROOT}/components/gfx/qrcodegen)
```

and after `reflbo_host_test(test_gfx_fonts gfx)`:

```cmake
reflbo_host_test(test_gfx_bmp_qr gfx)
```

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL to link or compile: `gfx_bmp_encode`, `gfx_qr` and `gfx_qr_side` undeclared.

- [ ] **Step 4: Implement.**

```diff
diff --git a/components/gfx/include/gfx.h b/components/gfx/include/gfx.h
index c8b3208..268ee6c 100644
--- a/components/gfx/include/gfx.h
+++ b/components/gfx/include/gfx.h
@@ -65,6 +65,16 @@ void gfx_bitmap(gfx_fb_t *fb, int x, int y, const gfx_bitmap_t *bm, gfx_color_t
 size_t gfx_pbm_size(const gfx_fb_t *fb);
 size_t gfx_pbm_encode(const gfx_fb_t *fb, uint8_t *out, size_t out_size); /* 0 if out_size is too small */
 
+/* 1-bit BMP of the framebuffer, for browsers (the web UI's preview and screenshot). */
+size_t gfx_bmp_size(const gfx_fb_t *fb);
+size_t gfx_bmp_encode(const gfx_fb_t *fb, uint8_t *out, size_t out_size); /* 0 if out_size is too small */
+
+/* `text` as a QR code (ECC medium, versions up to 10), with its 4-module quiet zone, `scale`
+ * pixels per module, its top-left corner at (x, y). Returns its side in pixels, or 0 if the text
+ * doesn't fit. Not reentrant: one encoding buffer is shared. */
+int gfx_qr(gfx_fb_t *fb, int x, int y, int scale, const char *text);
+int gfx_qr_side(const char *text, int scale); /* what gfx_qr() would draw; 0 if it doesn't fit */
+
 
 typedef enum {
     GFX_ALIGN_LEFT,
```

```c
#include <string.h>

#include "gfx.h"

/* A 1-bit BMP (spec §4.3), for the web UI's preview and screenshot: BITMAPFILEHEADER,
 * BITMAPINFOHEADER, a two-colour palette with index 1 black, then the rows bottom-up, each padded
 * to 4 bytes. Index 1 black lets the canonical rows (1 = black, MSB first) go in unchanged. */

#define HEADERS_SIZE (14 + 40 + 8)

static size_t row_size(const gfx_fb_t *fb)
{
    return ((size_t)fb->stride + 3u) & ~(size_t)3u;
}

size_t gfx_bmp_size(const gfx_fb_t *fb)
{
    return HEADERS_SIZE + row_size(fb) * (size_t)fb->height;
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xFFFFu);
    put16(p + 2, v >> 16);
}

size_t gfx_bmp_encode(const gfx_fb_t *fb, uint8_t *out, size_t out_size)
{
    size_t total = gfx_bmp_size(fb), row = row_size(fb);
    if (out_size < total) {
        return 0;
    }
    memset(out, 0, HEADERS_SIZE);
    out[0] = 'B';
    out[1] = 'M';
    put32(out + 2, (uint32_t)total);
    put32(out + 10, HEADERS_SIZE); /* where the pixels start */
    put32(out + 14, 40);           /* BITMAPINFOHEADER */
    put32(out + 18, (uint32_t)fb->width);
    put32(out + 22, (uint32_t)fb->height); /* positive: bottom-up rows */
    put16(out + 26, 1);                    /* planes */
    put16(out + 28, 1);                    /* bits per pixel */
    put32(out + 34, (uint32_t)(row * (size_t)fb->height));
    put32(out + 38, 2835); /* 72 dpi */
    put32(out + 42, 2835);
    put32(out + 46, 2); /* colours in the palette */
    memcpy(out + 54, "\xFF\xFF\xFF\x00\x00\x00\x00\x00", 8); /* 0 white, 1 black */
    for (int y = 0; y < fb->height; y++) {
        uint8_t *dst = out + HEADERS_SIZE + row * (size_t)(fb->height - 1 - y);
        memcpy(dst, fb->buf + (size_t)y * (size_t)fb->stride, (size_t)fb->stride);
        memset(dst + fb->stride, 0, row - (size_t)fb->stride);
    }
    return total;
}
```

```c
#include "gfx.h"
#include "qrcodegen.h"

/* QR codes (spec §4.3) through Nayuki's qrcodegen, vendored in qrcodegen/ (MIT). */

#define QR_MAX_VERSION 10 /* 57 modules: a Wi-Fi join string or a URL needs version 2-4 */
#define QUIET 4           /* modules of white border, as ISO/IEC 18004 asks */

static uint8_t s_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
static uint8_t s_temp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];

static int encode(const char *text)
{
    if (text == NULL || !qrcodegen_encodeText(text, s_temp, s_qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                              QR_MAX_VERSION, qrcodegen_Mask_AUTO, true)) {
        return 0;
    }
    return qrcodegen_getSize(s_qr);
}

int gfx_qr_side(const char *text, int scale)
{
    int modules = encode(text);
    return modules ? (modules + 2 * QUIET) * scale : 0;
}

int gfx_qr(gfx_fb_t *fb, int x, int y, int scale, const char *text)
{
    int modules = encode(text);
    if (modules == 0 || scale < 1) {
        return 0;
    }
    int side = (modules + 2 * QUIET) * scale;
    gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)side, (int16_t)side }, GFX_WHITE);
    for (int my = 0; my < modules; my++) {
        for (int mx = 0; mx < modules; mx++) {
            if (qrcodegen_getModule(s_qr, mx, my)) {
                gfx_fill_rect(fb, (gfx_rect_t){ (int16_t)(x + (QUIET + mx) * scale),
                                                (int16_t)(y + (QUIET + my) * scale), (int16_t)scale,
                                                (int16_t)scale },
                              GFX_BLACK);
            }
        }
    }
    return side;
}
```

```diff
diff --git a/components/gfx/CMakeLists.txt b/components/gfx/CMakeLists.txt
index 1d361e8..61985f2 100644
--- a/components/gfx/CMakeLists.txt
+++ b/components/gfx/CMakeLists.txt
@@ -1,10 +1,12 @@
 # 1-bpp drawing, fonts and icons (spec §4.3–4.5). Pure C: also built on the host by test/host.
 # Sources are listed, not globbed: ESP-IDF globs SRC_DIRS only at configure time.
-idf_component_register(SRCS "gfx.c" "gfx_pbm.c" "gfx_test_pattern.c" "gfx_text.c"
+idf_component_register(SRCS "gfx.c" "gfx_bmp.c" "gfx_pbm.c" "gfx_qr.c" "gfx_test_pattern.c" "gfx_text.c"
+                            "qrcodegen/qrcodegen.c"
                             "fonts/gfx_font_sans_12.c" "fonts/gfx_font_sans_16.c"
                             "fonts/gfx_font_sans_20.c" "fonts/gfx_font_sans_bold_20.c"
                             "fonts/gfx_font_sans_bold_16.c" "fonts/gfx_font_sans_bold_28.c"
                             "fonts/gfx_font_num_cb_48.c" "fonts/gfx_font_num_cb_72.c" "fonts/gfx_font_num_cb_110.c"
                             "fonts/gfx_font_num_cb_130.c"
                             "icons/gfx_icons.c"
-                       INCLUDE_DIRS "include")
+                       INCLUDE_DIRS "include"
+                       PRIV_INCLUDE_DIRS "qrcodegen")
```

- [ ] **Step 5: Run the tests.**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure -R 'test_gfx'`
Expected: PASS: `test_gfx_bmp_qr` with 4 tests, and the other gfx tests as before.

- [ ] **Step 6: Commit.**

```bash
git add components/gfx test/host/test_gfx_bmp_qr.c test/host/CMakeLists.txt
git commit -m "feat(gfx): add BMP encoding and QR codes (vendored qrcodegen, MIT)"
```

---

### Task 3: The Wi-Fi menu, the config screen and the first run (`locale`, `ui`)

**Files:**
- Create: `components/ui/ui_config.c`, `test/host/test_ui_config.c`, and the goldens `test/host/golden/screen_menu_wifi_en.pbm`, `screen_menu_confirm_password_cs.pbm`, `screen_config_ap_en.pbm`, `screen_config_ap_url_cs.pbm`, `screen_config_starting_en.pbm`, `screen_config_joining_en.pbm`, `screen_config_station_en.pbm`, `screen_config_station_ap_cs.pbm`, `screen_first_run_en.pbm`, `screen_first_run_invalid_cs.pbm`
- Modify: `components/locale/include/lang.h`, `components/locale/lang_en.c`, `components/locale/lang_cs.c`, `components/ui/include/ui_menu.h`, `components/ui/ui_menu.c`, `components/ui/ui_menu_draw.c`, `components/ui/include/ui_screens.h`, `components/ui/ui_screens.c`, `components/ui/CMakeLists.txt`, `test/host/test_ui_menu.c`, `test/host/screen_fixtures.h`, `test/host/CMakeLists.txt`, and the goldens `screen_menu_root_en.pbm`, `screen_menu_root_cs.pbm`, `screen_menu_info_en.pbm`

**Interfaces:**
- Consumes: `gfx_qr()` and `gfx_qr_side()` (Task 2).
- Produces:
  - Strings: `LS_M_WIFI`, `LS_M_CONFIG_MODE`, `LS_M_FORGET_NETWORKS`, `LS_M_RESET_PASSWORD`, `LS_M_IP`, `LS_M_MAC`, `LS_CONFIRM_FORGET_NETWORKS`, `LS_CONFIRM_RESET_PASSWORD`, `LS_T_NETWORKS_FORGOTTEN`, `LS_T_PASSWORD_CLEARED`, `LS_T_WIFI_OFF`, `LS_T_UPDATED`, the config screen's `LS_C_*`, `LS_HINT_CONFIG`, `LS_HINT_CONFIG_SWITCH`, and the first run's `LS_F_*`.
  - Menu items (spec §5.7): the section `UI_MI_WIFI` with the actions `UI_MI_CONFIG_MODE`, `UI_MI_FORGET_NETWORKS` and `UI_MI_RESET_PASSWORD` (the last two confirmed first); the Info rows `UI_MI_INFO_IP` and `UI_MI_INFO_MAC`. `bool ui_menu_is_section(ui_menu_item_t)` and `const char *ui_menu_question(ui_menu_item_t, const lang_t *)` (NULL for items that don't ask).
  - The config screen: `ui_net_state_t` (`UI_NET_STARTING`, `UI_NET_JOINING`, `UI_NET_STATION`, `UI_NET_AP`), `ui_config_view_t`, `ui_qr_kind_t` (`UI_QR_NONE`, `UI_QR_JOIN`, `UI_QR_OPEN`), `ui_qr_kind_t ui_config_qr(const ui_config_view_t *, char *out, size_t size)`, `bool ui_config_can_switch(const ui_config_view_t *)` and `void ui_draw_config(gfx_fb_t *, const ui_config_view_t *, const lang_t *)`.
  - `void ui_draw_first_run(gfx_fb_t *, const ui_context_t *)`.
  - Task 10 fills the menu model's new rows, carries out the Wi-Fi actions, and draws both screens.

- [ ] **Step 1: Write the failing tests.** The menu gains the Wi-Fi section and the Info rows; the config screen's QR choice gets its own test.

```diff
diff --git a/test/host/test_ui_menu.c b/test/host/test_ui_menu.c
index 6ebd42a..26afa7a 100644
--- a/test/host/test_ui_menu.c
+++ b/test/host/test_ui_menu.c
@@ -37,18 +37,18 @@ static ui_menu_intent_t open_item(ui_menu_item_t item)
 
 static void test_the_root_lists_the_sections_in_order(void)
 {
-    const ui_menu_item_t expected[] = { UI_MI_PRESETS, UI_MI_TIME, UI_MI_DISPLAY, UI_MI_SENSORS, UI_MI_INFO,
-                                        UI_MI_SYSTEM };
+    const ui_menu_item_t expected[] = { UI_MI_PRESETS, UI_MI_WIFI, UI_MI_TIME, UI_MI_DISPLAY, UI_MI_SENSORS,
+                                        UI_MI_INFO, UI_MI_SYSTEM };
     ui_menu_item_t items[UI_MI_COUNT];
     int n = ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT);
-    TEST_ASSERT_EQUAL_INT(6, n);
-    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 6);
+    TEST_ASSERT_EQUAL_INT(7, n);
+    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 7);
     TEST_ASSERT_EQUAL_INT(UI_MI_PRESETS, ui_menu_current(&s_m, &s_model));
 }
 
 static void test_next_wraps_select_enters_and_back_returns_to_the_section(void)
 {
-    for (int i = 0; i < 6; i++) {
+    for (int i = 0; i < 7; i++) {
         press(UI_MENU_KEY_NEXT);
     }
     TEST_ASSERT_EQUAL_INT(UI_MI_PRESETS, ui_menu_current(&s_m, &s_model)); /* wrapped */
@@ -183,7 +183,8 @@ static void test_hidden_items_are_skipped_and_info_does_nothing(void)
 {
     s_model.hidden[UI_MI_DISPLAY] = true;
     ui_menu_item_t items[UI_MI_COUNT];
-    TEST_ASSERT_EQUAL_INT(5, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
+    TEST_ASSERT_EQUAL_INT(6, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
+    press(UI_MENU_KEY_NEXT);
     press(UI_MENU_KEY_NEXT);
     press(UI_MENU_KEY_NEXT);
     TEST_ASSERT_EQUAL_INT(UI_MI_SENSORS, ui_menu_current(&s_m, &s_model));
@@ -192,6 +193,48 @@ static void test_hidden_items_are_skipped_and_info_does_nothing(void)
     TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
 }
 
+/* Spec §5.7, D18: config mode starts at once; forgetting the networks or the web password asks. */
+static void test_the_wifi_section_asks_before_it_forgets_anything(void)
+{
+    open_item(UI_MI_WIFI);
+    ui_menu_item_t items[UI_MI_COUNT];
+    const ui_menu_item_t expected[] = { UI_MI_CONFIG_MODE, UI_MI_FORGET_NETWORKS, UI_MI_RESET_PASSWORD };
+    TEST_ASSERT_EQUAL_INT(3, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
+    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 3);
+    ui_menu_intent_t in = open_item(UI_MI_CONFIG_MODE);
+    TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind);
+    TEST_ASSERT_EQUAL_INT(UI_MI_CONFIG_MODE, in.item);
+    const lang_t *en = lang_get("en");
+    TEST_ASSERT_EQUAL(UI_MENU_NONE, open_item(UI_MI_FORGET_NETWORKS).kind);
+    TEST_ASSERT_EQUAL(UI_MENU_CONFIRM, s_m.mode);
+    TEST_ASSERT_EQUAL_STRING(lang_str(en, LS_CONFIRM_FORGET_NETWORKS), ui_menu_question(UI_MI_FORGET_NETWORKS, en));
+    in = press(UI_MENU_KEY_SELECT);
+    TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind);
+    TEST_ASSERT_EQUAL_INT(UI_MI_FORGET_NETWORKS, in.item);
+    TEST_ASSERT_EQUAL(UI_MENU_NONE, open_item(UI_MI_RESET_PASSWORD).kind);
+    TEST_ASSERT_EQUAL(UI_MENU_CONFIRM, s_m.mode);
+    TEST_ASSERT_EQUAL_STRING(lang_str(en, LS_CONFIRM_RESET_PASSWORD), ui_menu_question(UI_MI_RESET_PASSWORD, en));
+    TEST_ASSERT_EQUAL(UI_MENU_NONE, press(UI_MENU_KEY_NEXT).kind); /* only a long press confirms */
+    in = press(UI_MENU_KEY_SELECT);
+    TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind);
+    TEST_ASSERT_EQUAL_INT(UI_MI_RESET_PASSWORD, in.item);
+    TEST_ASSERT_EQUAL_STRING(lang_str(en, LS_CONFIRM_FACTORY_RESET), ui_menu_question(UI_MI_FACTORY_RESET, en));
+    TEST_ASSERT_NULL(ui_menu_question(UI_MI_REBOOT, en));
+}
+
+/* Spec §5.7: Info gains the IP address and the MAC with M4. */
+static void test_info_shows_the_network_addresses(void)
+{
+    open_item(UI_MI_INFO);
+    ui_menu_item_t items[UI_MI_COUNT];
+    const ui_menu_item_t expected[] = { UI_MI_INFO_BATTERY, UI_MI_INFO_FIRMWARE, UI_MI_INFO_DEVICE, UI_MI_INFO_IP,
+                                        UI_MI_INFO_MAC, UI_MI_INFO_UPTIME, UI_MI_INFO_MEMORY };
+    TEST_ASSERT_EQUAL_INT(7, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
+    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 7);
+    TEST_ASSERT_TRUE(ui_menu_is_section(UI_MI_WIFI));
+    TEST_ASSERT_FALSE(ui_menu_is_section(UI_MI_INFO_IP));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -205,5 +248,7 @@ int main(void)
     RUN_TEST(test_the_date_time_editor_starts_in_2026_after_the_clock_was_lost);
     RUN_TEST(test_factory_reset_asks_first);
     RUN_TEST(test_hidden_items_are_skipped_and_info_does_nothing);
+    RUN_TEST(test_the_wifi_section_asks_before_it_forgets_anything);
+    RUN_TEST(test_info_shows_the_network_addresses);
     return UNITY_END();
 }
```

```c
#include <string.h>

#include "ui_screens.h"
#include "unity.h"

static ui_config_view_t s_v;
static char s_text[128];

void setUp(void)
{
    s_v = (ui_config_view_t){ .state = UI_NET_AP, .ap_on = true, .ssid = "", .ip = "", .host = "reflbo-bb94",
                              .ap_ssid = "reflbo-bb94", .ap_pass = "k7m2xq9pde", .minutes_left = 10 };
}

void tearDown(void) {}

static void test_the_ap_code_joins_the_devices_network(void)
{
    TEST_ASSERT_EQUAL(UI_QR_JOIN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:reflbo-bb94;P:k7m2xq9pde;;", s_text);
    s_v.qr_url = true; /* KEY: the address of the web UI on the AP */
    TEST_ASSERT_EQUAL(UI_QR_OPEN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("http://192.168.4.1/", s_text);
    TEST_ASSERT_TRUE(ui_config_can_switch(&s_v));
}

/* The Wi-Fi QR format escapes \ ; , : and " with a backslash. */
static void test_special_characters_in_the_join_code_are_escaped(void)
{
    s_v.ap_ssid = "a;b,c";
    s_v.ap_pass = "p:\"q\\";
    TEST_ASSERT_EQUAL(UI_QR_JOIN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:a\\;b\\,c;P:p\\:\\\"q\\\\;;", s_text);
}

static void test_on_a_network_the_code_opens_the_devices_address(void)
{
    s_v = (ui_config_view_t){ .state = UI_NET_STATION, .ssid = "Home", .ip = "192.168.1.57", .host = "reflbo-bb94",
                              .ap_ssid = "reflbo-bb94", .ap_pass = "k7m2xq9pde" };
    TEST_ASSERT_EQUAL(UI_QR_OPEN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.57/", s_text); /* Android may not resolve .local names */
    TEST_ASSERT_FALSE(ui_config_can_switch(&s_v)); /* no AP to join */
}

/* D18: while no web password is set the AP runs beside the station, and its code comes first. */
static void test_with_the_ap_beside_a_network_key_switches_the_codes(void)
{
    s_v = (ui_config_view_t){ .state = UI_NET_STATION, .ap_on = true, .ssid = "Home", .ip = "192.168.1.57",
                              .host = "reflbo-bb94", .ap_ssid = "reflbo-bb94", .ap_pass = "k7m2xq9pde" };
    TEST_ASSERT_EQUAL(UI_QR_JOIN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_TRUE(ui_config_can_switch(&s_v));
    s_v.qr_url = true;
    TEST_ASSERT_EQUAL(UI_QR_OPEN, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.57/", s_text);
}

static void test_no_code_until_there_is_something_to_join_or_open(void)
{
    s_v.state = UI_NET_STARTING;
    s_v.ap_on = false;
    TEST_ASSERT_EQUAL(UI_QR_NONE, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    TEST_ASSERT_EQUAL_STRING("", s_text);
    s_v.state = UI_NET_JOINING;
    s_v.ssid = "Home";
    TEST_ASSERT_EQUAL(UI_QR_NONE, ui_config_qr(&s_v, s_text, sizeof(s_text)));
    s_v.ap_on = true; /* joining with the AP up (D18) */
    s_v.qr_url = true;
    TEST_ASSERT_EQUAL(UI_QR_JOIN, ui_config_qr(&s_v, s_text, sizeof(s_text))); /* no address yet */
    TEST_ASSERT_FALSE(ui_config_can_switch(&s_v));
    s_v = (ui_config_view_t){ .state = UI_NET_STATION, .ssid = "Home", .ip = "", .host = "reflbo-bb94" };
    TEST_ASSERT_EQUAL(UI_QR_NONE, ui_config_qr(&s_v, s_text, sizeof(s_text))); /* lost the network */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_ap_code_joins_the_devices_network);
    RUN_TEST(test_special_characters_in_the_join_code_are_escaped);
    RUN_TEST(test_on_a_network_the_code_opens_the_devices_address);
    RUN_TEST(test_with_the_ap_beside_a_network_key_switches_the_codes);
    RUN_TEST(test_no_code_until_there_is_something_to_join_or_open);
    return UNITY_END();
}
```

The screen fixtures gain the Info rows' values, the new menu screens, every config-mode state and the first run:

```diff
diff --git a/test/host/screen_fixtures.h b/test/host/screen_fixtures.h
index f416fb9..a735f3b 100644
--- a/test/host/screen_fixtures.h
+++ b/test/host/screen_fixtures.h
@@ -66,6 +66,8 @@ static inline void fixture_menu_model(ui_menu_model_t *m, const lang_t *lang)
     m->info[UI_MI_INFO_BATTERY] = "82 % · 4.04 V";
     m->info[UI_MI_INFO_FIRMWARE] = "0.3.0 (0b21fcd)";
     m->info[UI_MI_INFO_DEVICE] = "reflbo-bb94";
+    m->info[UI_MI_INFO_IP] = "192.168.1.57";
+    m->info[UI_MI_INFO_MAC] = "14:c1:9f:54:bb:94";
     m->info[UI_MI_INFO_UPTIME] = "2 d 3 h";
     m->info[UI_MI_INFO_MEMORY] = "7.9 MB";
     m->local = fixture_local(20, 48, 0);
@@ -90,6 +92,37 @@ static inline void fixture_menu_open(ui_menu_t *m, const ui_menu_model_t *model,
     fixture_menu_to(m, model, item);
 }
 
+/* Config mode (spec §10.2) in each Wi-Fi state; `name` picks the state and the QR code. */
+static inline bool fixture_config(const char *name, ui_config_view_t *v)
+{
+    *v = (ui_config_view_t){ .state = UI_NET_AP, .ap_on = true, .ssid = "", .ip = "", .host = "reflbo-bb94",
+                             .ap_ssid = "reflbo-bb94", .ap_pass = "k7m2xq9pde", .minutes_left = 10 };
+    if (strncmp(name, "config_ap_url", 13) == 0) {
+        v->qr_url = true;
+    } else if (strncmp(name, "config_starting", 15) == 0) {
+        *v = (ui_config_view_t){ .state = UI_NET_STARTING, .ssid = "", .ip = "", .host = "reflbo-bb94",
+                                 .ap_ssid = "reflbo-bb94", .ap_pass = "k7m2xq9pde", .minutes_left = 10 };
+    } else if (strncmp(name, "config_joining", 14) == 0) {
+        v->state = UI_NET_JOINING;
+        v->ap_on = false;
+        v->ssid = "Vybiral Home 5G";
+    } else if (strncmp(name, "config_station_ap", 17) == 0) { /* no web password yet (D18) */
+        v->state = UI_NET_STATION;
+        v->ssid = "Vybiral Home 5G";
+        v->ip = "192.168.1.57";
+        v->minutes_left = 7;
+    } else if (strncmp(name, "config_station", 14) == 0) {
+        v->state = UI_NET_STATION;
+        v->ap_on = false;
+        v->ssid = "Vybiral Home 5G";
+        v->ip = "192.168.1.57";
+        v->minutes_left = 3;
+    } else if (strncmp(name, "config_ap", 9) != 0) {
+        return false;
+    }
+    return true;
+}
+
 /* Draws screen fixture `name` into fb; false for an unknown name. */
 static inline bool fixture_screen(const char *name, gfx_fb_t *fb)
 {
@@ -119,6 +152,24 @@ static inline bool fixture_screen(const char *name, gfx_fb_t *fb)
     } else if (strcmp(name, "menu_confirm_cs") == 0) {
         fixture_menu_open(&menu, &model, UI_MI_SYSTEM, UI_MI_FACTORY_RESET);
         ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
+    } else if (strcmp(name, "menu_wifi_en") == 0) {
+        fixture_menu_open(&menu, &model, UI_MI_WIFI, UI_MI_CONFIG_MODE);
+    } else if (strcmp(name, "menu_confirm_password_cs") == 0) {
+        fixture_menu_open(&menu, &model, UI_MI_WIFI, UI_MI_RESET_PASSWORD);
+        ui_menu_input(&menu, &model, UI_MENU_KEY_SELECT);
+    } else if (strncmp(name, "config_", 7) == 0) {
+        ui_config_view_t v;
+        if (!fixture_config(name, &v)) {
+            return false;
+        }
+        ui_draw_config(fb, &v, lang);
+        return true;
+    } else if (strncmp(name, "first_run", 9) == 0) {
+        ui_context_t ctx = fixture_context();
+        ctx.lang = lang;
+        ctx.time_valid = strstr(name, "invalid") == NULL;
+        ui_draw_first_run(fb, &ctx);
+        return true;
     } else if (strcmp(name, "toast_preset_cs") == 0) {
         ui_context_t ctx;
         ui_preset_t preset;
@@ -143,4 +194,7 @@ static inline bool fixture_screen(const char *name, gfx_fb_t *fb)
 static const char *const k_screen_fixtures[] = { "menu_root_en", "menu_root_cs", "menu_presets_cs",
                                                  "menu_edit_zone_en", "menu_edit_offset_cs", "menu_info_en",
                                                  "menu_system_cs", "menu_datetime_cs", "menu_confirm_cs",
-                                                 "toast_preset_cs", "critical_en", "critical_cs" };
+                                                 "toast_preset_cs", "critical_en", "critical_cs", "menu_wifi_en",
+                                                 "menu_confirm_password_cs", "config_ap_en", "config_ap_url_cs",
+                                                 "config_starting_en", "config_joining_en", "config_station_en",
+                                                 "config_station_ap_cs", "first_run_en", "first_run_invalid_cs" };
```

- [ ] **Step 2: Register the new test and watch the tests fail.** In `test/host/CMakeLists.txt`, after `reflbo_host_test(test_ui_menu ui)`:

```cmake
reflbo_host_test(test_ui_config ui)
```

Run: `cmake --build build-host`
Expected: FAIL to compile: `UI_MI_WIFI` and `ui_config_view_t` undeclared.

- [ ] **Step 3: Implement the strings.** English and Czech; the glyph test checks every one against the fonts.

```diff
diff --git a/components/locale/include/lang.h b/components/locale/include/lang.h
index 833e69e..4ff1fd6 100644
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -39,6 +39,10 @@ typedef enum {
     LS_M_AUTO_CYCLE,
     LS_M_CYCLE_INTERVAL,
     LS_M_SCHEDULE,
+    LS_M_WIFI,
+    LS_M_CONFIG_MODE,
+    LS_M_FORGET_NETWORKS,
+    LS_M_RESET_PASSWORD,
     LS_M_TIME,
     LS_M_SET_DATETIME,
     LS_M_CLOCK_24H,
@@ -53,6 +57,8 @@ typedef enum {
     LS_M_INFO,
     LS_M_FIRMWARE,
     LS_M_DEVICE,
+    LS_M_IP,
+    LS_M_MAC,
     LS_M_UPTIME,
     LS_M_FREE_MEMORY,
     LS_M_SYSTEM,
@@ -66,6 +72,8 @@ typedef enum {
     LS_HINT_DATETIME,
     LS_HINT_CONFIRM,
     LS_CONFIRM_FACTORY_RESET,
+    LS_CONFIRM_FORGET_NETWORKS,
+    LS_CONFIRM_RESET_PASSWORD,
     /* Toasts and special screens (spec §5.5) */
     LS_T_PRESET, /* followed by ": <preset name>" */
     LS_T_CYCLE_ON,
@@ -74,8 +82,30 @@ typedef enum {
     LS_T_NIGHT_UNTIL, /* followed by " 06:00" */
     LS_T_REBOOTING,
     LS_T_RESETTING,
+    LS_T_NETWORKS_FORGOTTEN,
+    LS_T_PASSWORD_CLEARED,
+    LS_T_WIFI_OFF,  /* config mode ended */
+    LS_T_UPDATED,   /* a firmware upload finished; the board restarts */
     LS_BATTERY_EMPTY,
     LS_CHARGE_ME,
+    /* Config mode and the first run (spec §5.5, §10.2) */
+    LS_C_TITLE,
+    LS_C_CLOSES_IN, /* followed by " 9 min" */
+    LS_C_STARTING,
+    LS_C_CONNECTING, /* a label; the network's name follows below it */
+    LS_C_CONNECTED,
+    LS_C_NETWORK,
+    LS_C_PASSWORD,
+    LS_C_THEN_OPEN,
+    LS_C_ADDRESS,
+    LS_C_SCAN_JOIN, /* what the QR code does */
+    LS_C_SCAN_OPEN,
+    LS_HINT_CONFIG,
+    LS_HINT_CONFIG_SWITCH, /* with a second QR code to switch to */
+    LS_F_TITLE,
+    LS_F_WIFI,
+    LS_F_MENU,
+    LS_F_CONTINUE,
     LS_COUNT,
 } lang_str_t;
 
diff --git a/components/locale/lang_cs.c b/components/locale/lang_cs.c
index fd6b756..06a855d 100644
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -89,6 +89,10 @@ const lang_t lang_cs = {
         [LS_M_AUTO_CYCLE] = "Střídání",
         [LS_M_CYCLE_INTERVAL] = "Interval střídání",
         [LS_M_SCHEDULE] = "Rozvrh",
+        [LS_M_WIFI] = "Wi-Fi",
+        [LS_M_CONFIG_MODE] = "Režim nastavení",
+        [LS_M_FORGET_NETWORKS] = "Zapomenout sítě",
+        [LS_M_RESET_PASSWORD] = "Resetovat heslo webu",
         [LS_M_TIME] = "Čas",
         [LS_M_SET_DATETIME] = "Nastavit datum a čas",
         [LS_M_CLOCK_24H] = "24hodinový čas",
@@ -103,6 +107,8 @@ const lang_t lang_cs = {
         [LS_M_INFO] = "Informace",
         [LS_M_FIRMWARE] = "Firmware",
         [LS_M_DEVICE] = "Zařízení",
+        [LS_M_IP] = "IP adresa",
+        [LS_M_MAC] = "MAC adresa",
         [LS_M_UPTIME] = "Doba běhu",
         [LS_M_FREE_MEMORY] = "Volná paměť",
         [LS_M_SYSTEM] = "Systém",
@@ -116,6 +122,8 @@ const lang_t lang_cs = {
         [LS_HINT_DATETIME] = "KEY + · podržet: další     BOOT – · podržet: zrušit",
         [LS_HINT_CONFIRM] = "Podržte KEY pro potvrzení · BOOT zruší",
         [LS_CONFIRM_FACTORY_RESET] = "Smazat všechna nastavení a předvolby?",
+        [LS_CONFIRM_FORGET_NETWORKS] = "Zapomenout všechny uložené sítě Wi-Fi?",
+        [LS_CONFIRM_RESET_PASSWORD] = "Smazat heslo webu? Při další návštěvě zvolíte nové.",
         [LS_T_PRESET] = "Předvolba",
         [LS_T_CYCLE_ON] = "Střídání zapnuto",
         [LS_T_CYCLE_OFF] = "Střídání vypnuto",
@@ -123,8 +131,29 @@ const lang_t lang_cs = {
         [LS_T_NIGHT_UNTIL] = "Noc do",
         [LS_T_REBOOTING] = "Restartuji…",
         [LS_T_RESETTING] = "Obnovuji tovární nastavení…",
+        [LS_T_NETWORKS_FORGOTTEN] = "Sítě zapomenuty",
+        [LS_T_PASSWORD_CLEARED] = "Heslo webu smazáno",
+        [LS_T_WIFI_OFF] = "Wi-Fi vypnuto",
+        [LS_T_UPDATED] = "Firmware aktualizován",
         [LS_BATTERY_EMPTY] = "Baterie je vybitá",
         [LS_CHARGE_ME] = "Nabijte mě, prosím",
+        [LS_C_TITLE] = "Nastavení Wi-Fi",
+        [LS_C_CLOSES_IN] = "konec za",
+        [LS_C_STARTING] = "Zapínám Wi-Fi…",
+        [LS_C_CONNECTING] = "Připojuji k síti",
+        [LS_C_CONNECTED] = "Připojeno k síti",
+        [LS_C_NETWORK] = "Síť Wi-Fi",
+        [LS_C_PASSWORD] = "Heslo",
+        [LS_C_THEN_OPEN] = "Pak otevřete",
+        [LS_C_ADDRESS] = "Otevřete v prohlížeči",
+        [LS_C_SCAN_JOIN] = "Připojit se",
+        [LS_C_SCAN_OPEN] = "Otevřít",
+        [LS_HINT_CONFIG] = "Podržte BOOT pro vypnutí Wi-Fi",
+        [LS_HINT_CONFIG_SWITCH] = "KEY jiný kód     Podržte BOOT pro vypnutí Wi-Fi",
+        [LS_F_TITLE] = "Vítejte",
+        [LS_F_WIFI] = "Podržte BOOT 3 s pro nastavení Wi-Fi",
+        [LS_F_MENU] = "Podržte KEY pro menu",
+        [LS_F_CONTINUE] = "Stiskněte KEY pro pokračování",
     },
     .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
     .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
diff --git a/components/locale/lang_en.c b/components/locale/lang_en.c
index af0a1dc..1af3463 100644
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -49,6 +49,10 @@ const lang_t lang_en = {
         [LS_M_AUTO_CYCLE] = "Auto-cycle",
         [LS_M_CYCLE_INTERVAL] = "Interval",
         [LS_M_SCHEDULE] = "Schedule",
+        [LS_M_WIFI] = "Wi-Fi",
+        [LS_M_CONFIG_MODE] = "Config mode",
+        [LS_M_FORGET_NETWORKS] = "Forget networks",
+        [LS_M_RESET_PASSWORD] = "Reset web password",
         [LS_M_TIME] = "Time",
         [LS_M_SET_DATETIME] = "Set date and time",
         [LS_M_CLOCK_24H] = "24-hour clock",
@@ -63,6 +67,8 @@ const lang_t lang_en = {
         [LS_M_INFO] = "Info",
         [LS_M_FIRMWARE] = "Firmware",
         [LS_M_DEVICE] = "Device",
+        [LS_M_IP] = "IP address",
+        [LS_M_MAC] = "MAC address",
         [LS_M_UPTIME] = "Uptime",
         [LS_M_FREE_MEMORY] = "Free memory",
         [LS_M_SYSTEM] = "System",
@@ -76,6 +82,8 @@ const lang_t lang_en = {
         [LS_HINT_DATETIME] = "KEY + · hold: next     BOOT – · hold: cancel",
         [LS_HINT_CONFIRM] = "Hold KEY to confirm · BOOT cancels",
         [LS_CONFIRM_FACTORY_RESET] = "Erase all settings and presets?",
+        [LS_CONFIRM_FORGET_NETWORKS] = "Forget all saved Wi-Fi networks?",
+        [LS_CONFIRM_RESET_PASSWORD] = "Clear the web password? The next visit sets a new one.",
         [LS_T_PRESET] = "Preset",
         [LS_T_CYCLE_ON] = "Auto-cycle on",
         [LS_T_CYCLE_OFF] = "Auto-cycle off",
@@ -83,8 +91,29 @@ const lang_t lang_en = {
         [LS_T_NIGHT_UNTIL] = "Night until",
         [LS_T_REBOOTING] = "Rebooting…",
         [LS_T_RESETTING] = "Factory reset…",
+        [LS_T_NETWORKS_FORGOTTEN] = "Networks forgotten",
+        [LS_T_PASSWORD_CLEARED] = "Web password cleared",
+        [LS_T_WIFI_OFF] = "Wi-Fi off",
+        [LS_T_UPDATED] = "Firmware updated",
         [LS_BATTERY_EMPTY] = "Battery empty",
         [LS_CHARGE_ME] = "Please charge me",
+        [LS_C_TITLE] = "Wi-Fi setup",
+        [LS_C_CLOSES_IN] = "closes in",
+        [LS_C_STARTING] = "Starting Wi-Fi…",
+        [LS_C_CONNECTING] = "Connecting to",
+        [LS_C_CONNECTED] = "Connected to",
+        [LS_C_NETWORK] = "Wi-Fi network",
+        [LS_C_PASSWORD] = "Password",
+        [LS_C_THEN_OPEN] = "Then open",
+        [LS_C_ADDRESS] = "Open in a browser",
+        [LS_C_SCAN_JOIN] = "Scan to join",
+        [LS_C_SCAN_OPEN] = "Scan to open",
+        [LS_HINT_CONFIG] = "Hold BOOT to turn Wi-Fi off",
+        [LS_HINT_CONFIG_SWITCH] = "KEY other code     Hold BOOT to turn Wi-Fi off",
+        [LS_F_TITLE] = "Welcome",
+        [LS_F_WIFI] = "Hold BOOT 3 s to set up Wi-Fi",
+        [LS_F_MENU] = "Hold KEY for the menu",
+        [LS_F_CONTINUE] = "Press KEY to continue",
     },
     .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
     .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
```

- [ ] **Step 4: Implement the menu items.** A confirmed action now names its own question, and the list draws a section's arrow from the node table.

```diff
diff --git a/components/ui/include/ui_menu.h b/components/ui/include/ui_menu.h
index 0ba2e92..c933816 100644
--- a/components/ui/include/ui_menu.h
+++ b/components/ui/include/ui_menu.h
@@ -21,6 +21,10 @@ typedef enum {
     UI_MI_AUTO_CYCLE,     /* toggle */
     UI_MI_CYCLE_INTERVAL, /* choice: interval labels */
     UI_MI_SCHEDULE,       /* toggle */
+    UI_MI_WIFI,
+    UI_MI_CONFIG_MODE,     /* action */
+    UI_MI_FORGET_NETWORKS, /* action, confirmed first */
+    UI_MI_RESET_PASSWORD,  /* action, confirmed first (D18) */
     UI_MI_TIME,
     UI_MI_SET_DATETIME, /* the date-time editor */
     UI_MI_CLOCK_24H,    /* toggle */
@@ -36,6 +40,8 @@ typedef enum {
     UI_MI_INFO_BATTERY, /* info texts */
     UI_MI_INFO_FIRMWARE,
     UI_MI_INFO_DEVICE,
+    UI_MI_INFO_IP,
+    UI_MI_INFO_MAC,
     UI_MI_INFO_UPTIME,
     UI_MI_INFO_MEMORY,
     UI_MI_SYSTEM,
@@ -66,7 +72,7 @@ typedef enum {
     UI_MENU_NONE,     /* only the menu changed: redraw it */
     UI_MENU_SET,      /* item = value */
     UI_MENU_SET_TIME, /* the local date and time in `local` */
-    UI_MENU_ACTION,   /* run item: reboot, factory reset */
+    UI_MENU_ACTION,   /* run item: config mode, forget networks, reset the password, reboot, factory reset */
     UI_MENU_CLOSE,
 } ui_menu_intent_kind_t;
 
@@ -100,6 +106,9 @@ int ui_menu_visible(const ui_menu_t *m, const ui_menu_model_t *model, ui_menu_it
 ui_menu_item_t ui_menu_current(const ui_menu_t *m, const ui_menu_model_t *model);
 /* The label of an item in the pack's language. */
 const char *ui_menu_label(ui_menu_item_t item, const lang_t *lang);
+bool ui_menu_is_section(ui_menu_item_t item);
+/* What an action that asks first asks; NULL for any other item. */
+const char *ui_menu_question(ui_menu_item_t item, const lang_t *lang);
 /* An item's value as the list shows it: "On", "Europe/Prague", "+0,5 °C". */
 void ui_menu_value_text(ui_menu_item_t item, int32_t value, const ui_menu_model_t *model, const lang_t *lang,
                         char *out, size_t size);
diff --git a/components/ui/ui_menu.c b/components/ui/ui_menu.c
index fcaf1da..5a707b9 100644
--- a/components/ui/ui_menu.c
+++ b/components/ui/ui_menu.c
@@ -23,6 +23,7 @@ typedef struct {
     uint8_t decimals;
     const char *unit;
     bool confirm; /* actions */
+    lang_str_t question;
 } node_t;
 
 static const node_t k_nodes[UI_MI_COUNT] = {
@@ -32,6 +33,12 @@ static const node_t k_nodes[UI_MI_COUNT] = {
     [UI_MI_AUTO_CYCLE] = { .label = LS_M_AUTO_CYCLE, .kind = K_TOGGLE, .parent = UI_MI_PRESETS },
     [UI_MI_CYCLE_INTERVAL] = { .label = LS_M_CYCLE_INTERVAL, .kind = K_CHOICE, .parent = UI_MI_PRESETS },
     [UI_MI_SCHEDULE] = { .label = LS_M_SCHEDULE, .kind = K_TOGGLE, .parent = UI_MI_PRESETS },
+    [UI_MI_WIFI] = { .label = LS_M_WIFI, .kind = K_SECTION, .parent = UI_MI_ROOT },
+    [UI_MI_CONFIG_MODE] = { .label = LS_M_CONFIG_MODE, .kind = K_ACTION, .parent = UI_MI_WIFI },
+    [UI_MI_FORGET_NETWORKS] = { .label = LS_M_FORGET_NETWORKS, .kind = K_ACTION, .parent = UI_MI_WIFI,
+                                .confirm = true, .question = LS_CONFIRM_FORGET_NETWORKS },
+    [UI_MI_RESET_PASSWORD] = { .label = LS_M_RESET_PASSWORD, .kind = K_ACTION, .parent = UI_MI_WIFI,
+                               .confirm = true, .question = LS_CONFIRM_RESET_PASSWORD },
     [UI_MI_TIME] = { .label = LS_M_TIME, .kind = K_SECTION, .parent = UI_MI_ROOT },
     [UI_MI_SET_DATETIME] = { .label = LS_M_SET_DATETIME, .kind = K_DATETIME, .parent = UI_MI_TIME },
     [UI_MI_CLOCK_24H] = { .label = LS_M_CLOCK_24H, .kind = K_TOGGLE, .parent = UI_MI_TIME },
@@ -50,13 +57,15 @@ static const node_t k_nodes[UI_MI_COUNT] = {
     [UI_MI_INFO_BATTERY] = { .label = LS_BATTERY, .kind = K_INFO, .parent = UI_MI_INFO },
     [UI_MI_INFO_FIRMWARE] = { .label = LS_M_FIRMWARE, .kind = K_INFO, .parent = UI_MI_INFO },
     [UI_MI_INFO_DEVICE] = { .label = LS_M_DEVICE, .kind = K_INFO, .parent = UI_MI_INFO },
+    [UI_MI_INFO_IP] = { .label = LS_M_IP, .kind = K_INFO, .parent = UI_MI_INFO },
+    [UI_MI_INFO_MAC] = { .label = LS_M_MAC, .kind = K_INFO, .parent = UI_MI_INFO },
     [UI_MI_INFO_UPTIME] = { .label = LS_M_UPTIME, .kind = K_INFO, .parent = UI_MI_INFO },
     [UI_MI_INFO_MEMORY] = { .label = LS_M_FREE_MEMORY, .kind = K_INFO, .parent = UI_MI_INFO },
     [UI_MI_SYSTEM] = { .label = LS_M_SYSTEM, .kind = K_SECTION, .parent = UI_MI_ROOT },
     [UI_MI_LANGUAGE] = { .label = LS_M_LANGUAGE, .kind = K_CHOICE, .parent = UI_MI_SYSTEM },
     [UI_MI_REBOOT] = { .label = LS_M_REBOOT, .kind = K_ACTION, .parent = UI_MI_SYSTEM },
     [UI_MI_FACTORY_RESET] = { .label = LS_M_FACTORY_RESET, .kind = K_ACTION, .parent = UI_MI_SYSTEM,
-                              .confirm = true },
+                              .confirm = true, .question = LS_CONFIRM_FACTORY_RESET },
 };
 
 static const ui_menu_intent_t k_none = { .kind = UI_MENU_NONE };
@@ -91,6 +100,16 @@ const char *ui_menu_label(ui_menu_item_t item, const lang_t *lang)
     return (unsigned)item < UI_MI_COUNT ? lang_str(lang, k_nodes[item].label) : "";
 }
 
+bool ui_menu_is_section(ui_menu_item_t item)
+{
+    return (unsigned)item < UI_MI_COUNT && k_nodes[item].kind == K_SECTION;
+}
+
+const char *ui_menu_question(ui_menu_item_t item, const lang_t *lang)
+{
+    return (unsigned)item < UI_MI_COUNT && k_nodes[item].confirm ? lang_str(lang, k_nodes[item].question) : NULL;
+}
+
 void ui_menu_value_text(ui_menu_item_t item, int32_t value, const ui_menu_model_t *model, const lang_t *lang,
                         char *out, size_t size)
 {
diff --git a/components/ui/ui_menu_draw.c b/components/ui/ui_menu_draw.c
index 5f82cc8..606d1b2 100644
--- a/components/ui/ui_menu_draw.c
+++ b/components/ui/ui_menu_draw.c
@@ -47,8 +47,7 @@ static void draw_list(gfx_fb_t *fb, const ui_menu_t *m, const ui_menu_model_t *m
             gfx_fill_rect(fb, r, GFX_BLACK);
         }
         char value[48];
-        if (item == UI_MI_PRESETS || item == UI_MI_TIME || item == UI_MI_DISPLAY || item == UI_MI_SENSORS ||
-            item == UI_MI_INFO || item == UI_MI_SYSTEM) {
+        if (ui_menu_is_section(item)) {
             snprintf(value, sizeof(value), "\xE2\x86\x92"); /* a section: → */
         } else {
             ui_menu_value_text(item, editing ? m->edit : model->value[item], model, lang, value, sizeof(value));
@@ -153,7 +152,7 @@ void ui_draw_menu(gfx_fb_t *fb, const ui_menu_t *m, const ui_menu_model_t *model
         break;
     case UI_MENU_CONFIRM:
         draw_header(fb, ui_menu_label(item, lang));
-        draw_question(fb, lang_str(lang, LS_CONFIRM_FACTORY_RESET), 110);
+        draw_question(fb, ui_menu_question(item, lang), 110);
         draw_hints(fb, lang_str(lang, LS_HINT_CONFIRM));
         break;
     default:
```

- [ ] **Step 5: Implement the screens.** The config screen puts the QR code on the left, at the largest scale that keeps it within 210 px, and what to do on the right. The QR code the view asks for is shown if it exists now, else the other one: joining the AP (`WIFI:T:WPA;S:…;P:…;;`, with the format's backslash escapes) or opening the page (`http://<ip>/`, not the `.local` name, which Android may not resolve).

```diff
diff --git a/components/ui/include/ui_screens.h b/components/ui/include/ui_screens.h
index 2e9c518..aed6b04 100644
--- a/components/ui/include/ui_screens.h
+++ b/components/ui/include/ui_screens.h
@@ -1,5 +1,8 @@
 #pragma once
 
+#include <stdbool.h>
+#include <stddef.h>
+
 #include "gfx.h"
 #include "ui_fields.h"
 
@@ -9,3 +12,37 @@
 void ui_draw_toast(gfx_fb_t *fb, const char *text);
 /* The last screen before the battery gives out (spec §8): nothing else updates after it. */
 void ui_draw_critical(gfx_fb_t *fb, const ui_context_t *ctx);
+/* The first run (spec §5.5): what the buttons do, and the clock if the time is valid. */
+void ui_draw_first_run(gfx_fb_t *fb, const ui_context_t *ctx);
+
+/* Config mode (spec §5.5, §10.2), as the app sees the Wi-Fi manager. */
+typedef enum {
+    UI_NET_STARTING, /* Wi-Fi is on its way up */
+    UI_NET_JOINING,  /* trying the saved networks */
+    UI_NET_STATION,  /* on a network */
+    UI_NET_AP,       /* only the device's own AP */
+} ui_net_state_t;
+
+typedef struct {
+    ui_net_state_t state;
+    bool ap_on;          /* the AP runs: the AP state, or beside a station while no web password is set */
+    bool qr_url;         /* KEY short picked the code that opens the web UI; otherwise it joins the AP */
+    const char *ssid;    /* the network joined, or being joined */
+    const char *ip;      /* on that network; "" while it has none */
+    const char *host;    /* reflbo-XXXX, the mDNS name without .local */
+    const char *ap_ssid;
+    const char *ap_pass;
+    int minutes_left; /* before config mode ends by itself */
+} ui_config_view_t;
+
+typedef enum {
+    UI_QR_NONE,
+    UI_QR_JOIN, /* joins the device's AP: "WIFI:T:WPA;S:...;P:...;;" */
+    UI_QR_OPEN, /* opens the web UI: "http://<ip>/" */
+} ui_qr_kind_t;
+
+/* The QR code the screen shows: the one `qr_url` asks for if it exists now, else the other. */
+ui_qr_kind_t ui_config_qr(const ui_config_view_t *v, char *out, size_t size);
+/* Both codes exist, so KEY short has something to switch to. */
+bool ui_config_can_switch(const ui_config_view_t *v);
+void ui_draw_config(gfx_fb_t *fb, const ui_config_view_t *v, const lang_t *lang);
diff --git a/components/ui/ui_screens.c b/components/ui/ui_screens.c
index 0814c15..460f65e 100644
--- a/components/ui/ui_screens.c
+++ b/components/ui/ui_screens.c
@@ -39,3 +39,39 @@ void ui_draw_critical(gfx_fb_t *fb, const ui_context_t *ctx)
                          GFX_BLACK);
     }
 }
+
+void ui_draw_first_run(gfx_fb_t *fb, const ui_context_t *ctx)
+{
+    gfx_reset_clip(fb);
+    gfx_clear(fb, GFX_WHITE);
+    gfx_fill_rect(fb, (gfx_rect_t){ 0, 0, fb->width, 30 }, GFX_BLACK);
+    gfx_text_in_rect(fb, &gfx_font_sans_bold_20, (gfx_rect_t){ 12, 0, (int16_t)(fb->width - 24), 30 },
+                     GFX_ALIGN_LEFT, lang_str(ctx->lang, LS_F_TITLE), GFX_WHITE);
+    int y = 76;
+    if (ctx->time_valid) { /* spec §5.5: the clock, when there is a time to show */
+        ui_value_t t;
+        ui_resolve(ctx, UI_FIELD_TIME_CLOCK, &t);
+        const gfx_font_t *f = &gfx_font_num_cb_72;
+        int w = gfx_text_width(f, t.text);
+        int suffix_w = t.unit[0] ? gfx_text_width(&gfx_font_sans_bold_20, t.unit) + 6 : 0;
+        int x = gfx_text(fb, f, (fb->width - w - suffix_w) / 2, 110, t.text, GFX_BLACK);
+        if (suffix_w) {
+            gfx_text(fb, &gfx_font_sans_bold_20, x + 6, 110, t.unit, GFX_BLACK);
+        }
+        y = 138;
+    }
+    const lang_str_t hints[] = { LS_F_WIFI, LS_F_MENU, LS_F_CONTINUE };
+    for (int i = 0; i < 3; i++) {
+        const gfx_font_t *f = i == 2 ? &gfx_font_sans_20 : &gfx_font_sans_bold_20;
+        char line1[96], line2[96];
+        ui_split_two_lines(f, lang_str(ctx->lang, hints[i]), fb->width - 24, line1, line2, sizeof(line1));
+        gfx_text_in_rect(fb, f, (gfx_rect_t){ 0, (int16_t)y, fb->width, 28 }, GFX_ALIGN_CENTER, line1, GFX_BLACK);
+        y += 26;
+        if (line2[0] != '\0') {
+            gfx_text_in_rect(fb, f, (gfx_rect_t){ 0, (int16_t)y, fb->width, 28 }, GFX_ALIGN_CENTER, line2,
+                             GFX_BLACK);
+            y += 26;
+        }
+        y += 12;
+    }
+}
```

```c
#include <stdio.h>
#include <string.h>

#include "gfx_fonts.h"
#include "ui_screens.h"

/* The config-mode screen (spec §5.5, §10.2): a QR code on the left, what to do on the right. */

#define HEADER_H 30
#define FOOTER_Y 282
#define QR_MAX   210 /* leaves the text column about 185 px */
#define AP_IP    "192.168.4.1"

/* Appends `text` to out[*n], with the backslash escapes of the Wi-Fi QR format. */
static void put_escaped(char *out, size_t size, size_t *n, const char *text)
{
    for (const char *c = text; *c != '\0' && *n + 2 < size; c++) {
        if (strchr("\\;,:\"", *c) != NULL) {
            out[(*n)++] = '\\';
        }
        out[(*n)++] = *c;
    }
    out[*n] = '\0';
}

static bool url_available(const ui_config_view_t *v)
{
    return (v->state == UI_NET_STATION && v->ip[0] != '\0') || v->state == UI_NET_AP;
}

bool ui_config_can_switch(const ui_config_view_t *v)
{
    return v->ap_on && url_available(v);
}

ui_qr_kind_t ui_config_qr(const ui_config_view_t *v, char *out, size_t size)
{
    out[0] = '\0';
    if (url_available(v) && (v->qr_url || !v->ap_on)) {
        snprintf(out, size, "http://%s/", v->state == UI_NET_STATION ? v->ip : AP_IP);
        return UI_QR_OPEN;
    }
    if (!v->ap_on) {
        return UI_QR_NONE;
    }
    size_t n = (size_t)snprintf(out, size, "WIFI:T:WPA;S:");
    put_escaped(out, size, &n, v->ap_ssid);
    n += (size_t)snprintf(out + n, size - n, ";P:");
    put_escaped(out, size, &n, v->ap_pass);
    snprintf(out + n, size - n, ";;");
    return UI_QR_JOIN;
}

/* A label over its value, cut to the column; returns the y below them. */
static int draw_pair(gfx_fb_t *fb, int x, int y, int w, const char *label, const char *value)
{
    char fit[96];
    if (label != NULL) {
        gfx_text_ellipsize(&gfx_font_sans_16, label, w, fit, sizeof(fit));
        gfx_text_in_rect(fb, &gfx_font_sans_16, (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)w, 20 },
                         GFX_ALIGN_LEFT, fit, GFX_BLACK);
        y += 20;
    }
    const gfx_font_t *f = gfx_text_width(&gfx_font_sans_bold_20, value) <= w ? &gfx_font_sans_bold_20
                                                                             : &gfx_font_sans_bold_16;
    gfx_text_ellipsize(f, value, w, fit, sizeof(fit));
    gfx_text_in_rect(fb, f, (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)w, 26 }, GFX_ALIGN_LEFT, fit, GFX_BLACK);
    return y + 34;
}

static void draw_header(gfx_fb_t *fb, const ui_config_view_t *v, const lang_t *lang)
{
    gfx_fill_rect(fb, (gfx_rect_t){ 0, 0, fb->width, HEADER_H }, GFX_BLACK);
    char left[40];
    snprintf(left, sizeof(left), "%s %d %s", lang_str(lang, LS_C_CLOSES_IN), v->minutes_left,
             lang_str(lang, LS_MINUTES_UNIT));
    int left_w = gfx_text_width(&gfx_font_sans_16, left);
    gfx_text_in_rect(fb, &gfx_font_sans_16, (gfx_rect_t){ (int16_t)(fb->width - 12 - left_w), 0, (int16_t)left_w,
                                                          HEADER_H },
                     GFX_ALIGN_LEFT, left, GFX_WHITE);
    char title[64];
    gfx_text_ellipsize(&gfx_font_sans_bold_20, lang_str(lang, LS_C_TITLE), fb->width - 36 - left_w, title,
                       sizeof(title));
    gfx_text_in_rect(fb, &gfx_font_sans_bold_20, (gfx_rect_t){ 12, 0, (int16_t)(fb->width - 36 - left_w), HEADER_H },
                     GFX_ALIGN_LEFT, title, GFX_WHITE);
}

static void draw_hints(gfx_fb_t *fb, const char *hints)
{
    gfx_hline(fb, 0, FOOTER_Y - 4, fb->width, GFX_BLACK);
    char fit[96];
    gfx_text_ellipsize(&gfx_font_sans_12, hints, fb->width - 12, fit, sizeof(fit));
    gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ 0, FOOTER_Y, fb->width, (int16_t)(fb->height - FOOTER_Y) },
                     GFX_ALIGN_CENTER, fit, GFX_BLACK);
}

void ui_draw_config(gfx_fb_t *fb, const ui_config_view_t *v, const lang_t *lang)
{
    gfx_reset_clip(fb);
    gfx_clear(fb, GFX_WHITE);
    draw_header(fb, v, lang);
    draw_hints(fb, lang_str(lang, ui_config_can_switch(v) ? LS_HINT_CONFIG_SWITCH : LS_HINT_CONFIG));

    char qr[160], line[64];
    ui_qr_kind_t kind = ui_config_qr(v, qr, sizeof(qr));
    if (kind == UI_QR_NONE) { /* starting, or joining without the AP: nothing to scan yet */
        if (v->state == UI_NET_STARTING) {
            gfx_text_in_rect(fb, &gfx_font_sans_bold_20, (gfx_rect_t){ 0, 130, fb->width, 28 }, GFX_ALIGN_CENTER,
                             lang_str(lang, LS_C_STARTING), GFX_BLACK);
            return;
        }
        gfx_text_in_rect(fb, &gfx_font_sans_20, (gfx_rect_t){ 0, 110, fb->width, 28 }, GFX_ALIGN_CENTER,
                         lang_str(lang, LS_C_CONNECTING), GFX_BLACK);
        char fit[64];
        snprintf(line, sizeof(line), "%s…", v->ssid);
        gfx_text_ellipsize(&gfx_font_sans_bold_28, line, fb->width - 24, fit, sizeof(fit));
        gfx_text_in_rect(fb, &gfx_font_sans_bold_28, (gfx_rect_t){ 0, 142, fb->width, 36 }, GFX_ALIGN_CENTER, fit,
                         GFX_BLACK);
        return;
    }

    int scale = 7;
    while (scale > 1 && gfx_qr_side(qr, scale) > QR_MAX) {
        scale--;
    }
    int top = HEADER_H + 2;
    int side = gfx_qr(fb, 0, top, scale, qr);
    gfx_text_in_rect(fb, &gfx_font_sans_bold_16, (gfx_rect_t){ 0, (int16_t)(top + side - 2), (int16_t)side, 22 },
                     GFX_ALIGN_CENTER, lang_str(lang, kind == UI_QR_JOIN ? LS_C_SCAN_JOIN : LS_C_SCAN_OPEN),
                     GFX_BLACK);

    int x = side + 4, w = fb->width - x - 6, y = HEADER_H + 10;
    if (v->state == UI_NET_JOINING) {
        snprintf(line, sizeof(line), "%s…", v->ssid);
        y = draw_pair(fb, x, y, w, lang_str(lang, LS_C_CONNECTING), line);
    } else if (v->state == UI_NET_STATION) {
        y = draw_pair(fb, x, y, w, lang_str(lang, LS_C_CONNECTED), v->ssid);
    }
    if (kind == UI_QR_JOIN) {
        y = draw_pair(fb, x, y, w, lang_str(lang, LS_C_NETWORK), v->ap_ssid);
        y = draw_pair(fb, x, y, w, lang_str(lang, LS_C_PASSWORD), v->ap_pass);
        draw_pair(fb, x, y, w, lang_str(lang, LS_C_THEN_OPEN), AP_IP);
    } else if (v->state == UI_NET_STATION) {
        snprintf(line, sizeof(line), "%s.local", v->host);
        y = draw_pair(fb, x, y, w, lang_str(lang, LS_C_ADDRESS), line);
        draw_pair(fb, x, y - 8, w, NULL, v->ip);
    } else {
        y = draw_pair(fb, x, y, w, lang_str(lang, LS_C_NETWORK), v->ap_ssid);
        draw_pair(fb, x, y, w, lang_str(lang, LS_C_ADDRESS), AP_IP);
    }
}
```

In `components/ui/CMakeLists.txt`, the third line of `SRCS` becomes:

```cmake
                            "ui_screens.c" "ui_config.c"
```

- [ ] **Step 6: Render the goldens.** The new screens get goldens; the menu's root and Info screens change, as the root lists Wi-Fi and Info gains two rows.

```bash
cmake --build build-host
for f in menu_root_en menu_root_cs menu_info_en menu_wifi_en menu_confirm_password_cs config_ap_en config_ap_url_cs \
         config_starting_en config_joining_en config_station_en config_station_ap_cs first_run_en first_run_invalid_cs; do
  build-host/render_screen $f test/host/golden/screen_$f.pbm
done
shasum -a 256 test/host/golden/screen_menu_wifi_en.pbm test/host/golden/screen_config_ap_en.pbm \
  test/host/golden/screen_first_run_invalid_cs.pbm
python3 tools/render.py
```

Expected: the renders the owner reviewed on 2026-09-30:
- `9a94568d77e9797abda926062fd4c5983bb7f77e52cdcad24c6db70244667bb7  test/host/golden/screen_menu_wifi_en.pbm`
- `eface3f917d5e58b64de38574c27782d2f61b88c76eb2db623149cdb0a3d0c2b  test/host/golden/screen_config_ap_en.pbm`
- `731d14e9ab066c92484a5fda7560a856d0472af61d5f12789ae791861533442c  test/host/golden/screen_first_run_invalid_cs.pbm`

Look at `captures/render/screen_*.png`: the QR codes sit left with the text beside them, nothing is cut off, and the long Czech first-run hint wraps to two lines.

- [ ] **Step 7: Run the tests.**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`
Expected: all pass, 36 of 36: `test_ui_menu` with 12 tests, `test_ui_config` with 5, and `test_lang`, `test_lang_glyphs` and `test_ui_screens_golden` as before.

- [ ] **Step 8: Commit.**

```bash
git add components/locale components/ui test/host
git commit -m "feat(ui): add the menu's Wi-Fi section, the config-mode screen and the first run"
```

---

### Task 4: The preset editor's catalogue (`ui`)

**Files:**
- Create: `components/ui/include/ui_catalog.h`, `components/ui/ui_catalog.c`, `test/host/test_ui_catalog.c`
- Modify: `components/ui/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: the layouts and fields of M3a (`ui_layout()`, `ui_field_info()`, `ui_resolve()`).
- Produces (Task 10's `GET /api/layouts` and `GET /api/fields`; Task 9's editor reads them):
  - `size_t ui_catalog_layouts_json(char *out, size_t size)`: `{"width":400,"height":300,"status_h":20,"layouts":[{"id":"classic","slots":[{"id":"main","x":0,"y":21,"w":400,"h":125,"size":"XL","kinds":["time","number"]}, …]}, …]}`.
  - `size_t ui_catalog_fields_json(const ui_context_t *ctx, char *out, size_t size)`: `{"fields":[{"id":"env.temp","kind":"number","label":"Temperature","value":"23.4 °C","state":"fresh"}, …]}`, the label in English (the web UI's language, spec §5.8) and the value in the device's; `age_s` for a stale value.
  - Both return 0 when `size` is too small.

- [ ] **Step 1: Write the failing test.**

```c
#include <string.h>

#include "cJSON.h"
#include "context_fixtures.h"
#include "ui_catalog.h"
#include "unity.h"

static char s_out[8192];
static cJSON *s_root;

void setUp(void)
{
    s_root = NULL;
}

void tearDown(void)
{
    cJSON_Delete(s_root);
}

static const cJSON *by_id(const cJSON *array, const char *id)
{
    const cJSON *item;
    cJSON_ArrayForEach(item, array)
    {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, "id");
        if (cJSON_IsString(v) && strcmp(v->valuestring, id) == 0) {
            return item;
        }
    }
    return NULL;
}

static const char *str(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static int num(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valueint : -1;
}

/* GET /api/layouts (spec §5.2, §10.3): the preset editor offers only fields a slot can show. */
static void test_layouts_list_their_slots_with_rectangles_sizes_and_kinds(void)
{
    TEST_ASSERT_TRUE(ui_catalog_layouts_json(s_out, sizeof(s_out)) > 0);
    s_root = cJSON_Parse(s_out);
    TEST_ASSERT_NOT_NULL(s_root);
    TEST_ASSERT_EQUAL_INT(400, num(s_root, "width"));
    TEST_ASSERT_EQUAL_INT(300, num(s_root, "height"));
    const cJSON *layouts = cJSON_GetObjectItemCaseSensitive(s_root, "layouts");
    TEST_ASSERT_EQUAL_INT(4, cJSON_GetArraySize(layouts));
    const cJSON *classic = by_id(layouts, "classic");
    TEST_ASSERT_NOT_NULL(classic);
    const cJSON *slots = cJSON_GetObjectItemCaseSensitive(classic, "slots");
    TEST_ASSERT_EQUAL_INT(6, cJSON_GetArraySize(slots));
    const cJSON *main_slot = by_id(slots, "main");
    TEST_ASSERT_NOT_NULL(main_slot);
    TEST_ASSERT_EQUAL_INT(21, num(main_slot, "y"));
    TEST_ASSERT_EQUAL_INT(400, num(main_slot, "w"));
    TEST_ASSERT_EQUAL_INT(125, num(main_slot, "h"));
    TEST_ASSERT_EQUAL_STRING("XL", str(main_slot, "size"));
    const cJSON *kinds = cJSON_GetObjectItemCaseSensitive(main_slot, "kinds");
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(kinds));
    TEST_ASSERT_EQUAL_STRING("time", cJSON_GetArrayItem(kinds, 0)->valuestring);
    TEST_ASSERT_EQUAL_STRING("number", cJSON_GetArrayItem(kinds, 1)->valuestring);
    const cJSON *hourly = by_id(cJSON_GetObjectItemCaseSensitive(by_id(layouts, "weather"), "slots"), "hourly");
    TEST_ASSERT_EQUAL_STRING("M", str(hourly, "size"));
}

/* GET /api/fields: the catalogue with values right now, labels in English (spec §5.8). */
static void test_fields_carry_their_kind_label_and_current_value(void)
{
    ui_context_t ctx = fixture_context();
    ctx.lang = lang_get("cs"); /* values in the device's language, labels in the web UI's */
    TEST_ASSERT_TRUE(ui_catalog_fields_json(&ctx, s_out, sizeof(s_out)) > 0);
    s_root = cJSON_Parse(s_out);
    const cJSON *fields = cJSON_GetObjectItemCaseSensitive(s_root, "fields");
    TEST_ASSERT_EQUAL_INT(UI_FIELD_COUNT - 1, cJSON_GetArraySize(fields));
    const cJSON *temp = by_id(fields, "env.temp");
    TEST_ASSERT_EQUAL_STRING("number", str(temp, "kind"));
    TEST_ASSERT_EQUAL_STRING("Temperature", str(temp, "label"));
    TEST_ASSERT_EQUAL_STRING("23,4 °C", str(temp, "value"));
    TEST_ASSERT_EQUAL_STRING("fresh", str(temp, "state"));
    TEST_ASSERT_EQUAL_STRING("20:48", str(by_id(fields, "time.clock"), "value"));
    const cJSON *wx = by_id(fields, "wx.now");
    TEST_ASSERT_EQUAL_STRING("weather_now", str(wx, "kind"));
    TEST_ASSERT_EQUAL_STRING("missing", str(wx, "state"));
    TEST_ASSERT_EQUAL_STRING("", str(wx, "value"));
}

static void test_a_buffer_too_small_gives_nothing(void)
{
    TEST_ASSERT_EQUAL_UINT(0, ui_catalog_layouts_json(s_out, 64));
    ui_context_t ctx = fixture_context();
    TEST_ASSERT_EQUAL_UINT(0, ui_catalog_fields_json(&ctx, s_out, 64));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_layouts_list_their_slots_with_rectangles_sizes_and_kinds);
    RUN_TEST(test_fields_carry_their_kind_label_and_current_value);
    RUN_TEST(test_a_buffer_too_small_gives_nothing);
    return UNITY_END();
}
```

- [ ] **Step 2: Register it and watch it fail.** In `test/host/CMakeLists.txt`, after `reflbo_host_test(test_ui_config ui)`:

```cmake
reflbo_host_test(test_ui_catalog ui cjson)
```

Run: `cmake --build build-host`
Expected: FAIL to compile: `ui_catalog.h` not found.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stddef.h>

#include "ui_fields.h"

/* What the web UI's preset editor needs to know (spec §10.3), as JSON. Pure C, host-buildable. */

/* GET /api/layouts: the panel size and every layout's slots, with their rectangles, size classes
 * and the field kinds each one takes. Returns the length written, or 0 if `size` is too small. */
size_t ui_catalog_layouts_json(char *out, size_t size);
/* GET /api/fields: every field with its kind, its label in English (the web UI's language, spec
 * §5.8) and its value right now in the device's language. 0 if `size` is too small. */
size_t ui_catalog_fields_json(const ui_context_t *ctx, char *out, size_t size);
```

```c
#include "ui_catalog.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "ui_layout.h"

#define PANEL_W 400
#define PANEL_H 300

static const char *const k_kinds[UI_FK_COUNT] = {
    [UI_FK_TIME] = "time",
    [UI_FK_DATE] = "date",
    [UI_FK_NUMBER] = "number",
    [UI_FK_BATTERY] = "battery",
    [UI_FK_MOON] = "moon",
    [UI_FK_TEXT] = "text",
    [UI_FK_WEATHER_NOW] = "weather_now",
    [UI_FK_WEATHER_DAY] = "weather_day",
    [UI_FK_SERIES] = "series",
    [UI_FK_SUN] = "sun",
};
static const char *const k_sizes[] = { "S", "M", "L", "XL" };

static size_t print(cJSON *root, char *out, size_t size)
{
    bool ok = root != NULL && size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}

size_t ui_catalog_layouts_json(char *out, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "width", PANEL_W);
    cJSON_AddNumberToObject(root, "height", PANEL_H);
    cJSON_AddNumberToObject(root, "status_h", UI_STATUS_H);
    cJSON *layouts = cJSON_AddArrayToObject(root, "layouts");
    for (int l = 0; l < UI_LAYOUT_COUNT; l++) {
        const ui_layout_t *layout = ui_layout((ui_layout_id_t)l);
        cJSON *lo = cJSON_CreateObject();
        cJSON_AddStringToObject(lo, "id", layout->id);
        cJSON *slots = cJSON_AddArrayToObject(lo, "slots");
        for (int s = 0; s < layout->slot_count; s++) {
            const ui_slot_t *slot = &layout->slots[s];
            cJSON *so = cJSON_CreateObject();
            cJSON_AddStringToObject(so, "id", slot->name);
            cJSON_AddNumberToObject(so, "x", slot->rect.x);
            cJSON_AddNumberToObject(so, "y", slot->rect.y);
            cJSON_AddNumberToObject(so, "w", slot->rect.w);
            cJSON_AddNumberToObject(so, "h", slot->rect.h);
            cJSON_AddStringToObject(so, "size", k_sizes[slot->size]);
            cJSON *kinds = cJSON_AddArrayToObject(so, "kinds");
            for (int k = 0; k < UI_FK_COUNT; k++) {
                if (slot->kinds & UI_KIND(k)) {
                    cJSON_AddItemToArray(kinds, cJSON_CreateString(k_kinds[k]));
                }
            }
            cJSON_AddItemToArray(slots, so);
        }
        cJSON_AddItemToArray(layouts, lo);
    }
    return print(root, out, size);
}

size_t ui_catalog_fields_json(const ui_context_t *ctx, char *out, size_t size)
{
    static const char *const k_states[] = { "missing", "fresh", "stale" };
    const lang_t *en = lang_get("en");
    cJSON *root = cJSON_CreateObject();
    cJSON *fields = cJSON_AddArrayToObject(root, "fields");
    for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
        const ui_field_info_t *info = ui_field_info((ui_field_id_t)f);
        static ui_value_t v;
        ui_resolve(ctx, (ui_field_id_t)f, &v);
        char value[sizeof(v.text) + sizeof(v.unit) + 2];
        snprintf(value, sizeof(value), "%s%s%s", v.state == UI_VALUE_MISSING ? "" : v.text,
                 v.state != UI_VALUE_MISSING && v.unit[0] ? " " : "", v.state == UI_VALUE_MISSING ? "" : v.unit);
        cJSON *fo = cJSON_CreateObject();
        cJSON_AddStringToObject(fo, "id", info->id);
        cJSON_AddStringToObject(fo, "kind", k_kinds[info->kind]);
        cJSON_AddStringToObject(fo, "label", lang_str(en, info->label));
        cJSON_AddStringToObject(fo, "value", value);
        cJSON_AddStringToObject(fo, "state", k_states[v.state]);
        if (v.state == UI_VALUE_STALE) {
            cJSON_AddNumberToObject(fo, "age_s", v.age_s);
        }
        cJSON_AddItemToArray(fields, fo);
    }
    return print(root, out, size);
}
```

In `components/ui/CMakeLists.txt`, the third line of `SRCS` becomes:

```cmake
                            "ui_screens.c" "ui_config.c" "ui_catalog.c"
```

- [ ] **Step 4: Run the tests.**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure -R test_ui_catalog`
Expected: PASS, 3 tests.

- [ ] **Step 5: Commit.**

```bash
git add components/ui test/host/test_ui_catalog.c test/host/CMakeLists.txt
git commit -m "feat(ui): describe the layouts and fields for the web UI's preset editor"
```

---

### Task 5: Location, the settings merge patch and the backup bundle (`storage`)

**Files:**
- Create: `components/storage/include/storage_backup.h`, `components/storage/storage_backup.c`, `test/host/test_storage_backup.c`
- Modify: `components/storage/include/settings.h`, `components/storage/settings.c`, `components/storage/CMakeLists.txt`, `test/host/test_settings.c`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `util_json_depth()` (M3b).
- Produces:
  - `settings_t` gains `char place[SETTINGS_PLACE_LEN]` (32), `int32_t lat_e4` and `int32_t lon_e4` (1e-4 degrees, clamped to the globe), read from and written to `location.name`, `.lat` and `.lon`. Task 10 fills their defaults from Kconfig.
  - `size_t settings_patch(const char *base_json, const char *patch, char *out, size_t size, char *err, size_t err_size)`: `patch` merged into the file's text as an RFC 7396 merge patch (objects merge, `null` removes, anything else replaces); `base_json` NULL means no file yet. The result must keep `"schema": 1`. Returns the length, or 0 with the reason. `PATCH /api/settings` (Task 10).
  - `backup_file_t` (`name`, `text`), `size_t backup_build(const backup_file_t *files, int count, const char *device, const char *firmware, char *out, size_t size)` and `int backup_split(const char *bundle, backup_file_t *files, int max, char *buf, size_t buf_size, char *err, size_t err_size)`: the bundle of spec §14.4. `split` checks the shape only (format 1, plain `*.json` names, objects); the caller validates each file. `GET /api/backup` and `POST /api/restore` (Task 10).

- [ ] **Step 1: Write the failing tests.** The settings test gains the location and the merge patch; the bundle gets its own test.

```diff
diff --git a/test/host/test_settings.c b/test/host/test_settings.c
index 1f5227e..6de03f3 100644
--- a/test/host/test_settings.c
+++ b/test/host/test_settings.c
@@ -12,7 +12,7 @@ void setUp(void)
 {
     s_defaults = (settings_t){ .language = "en", .clock_24h = true, .tz_posix = "CET-1CEST,M3.5.0,M10.5.0/3",
                                .tz_iana = "Europe/Prague", .sensors_every_min = 5, .display_every_min = 1,
-                               .lpm_quarter_hz = 4 };
+                               .lpm_quarter_hz = 4, .place = "Brno", .lat_e4 = 491951, .lon_e4 = 166068 };
     memset(&s_out, 0xAA, sizeof(s_out));
     s_err[0] = '\0';
 }
@@ -23,7 +23,7 @@ static void test_the_spec_sketch_parses(void)
 {
     const char *json =
         "{ \"schema\": 1, \"language\": \"en\","
-        "  \"location\": { \"name\": \"Brno\", \"lat\": 49.1951, \"lon\": 16.6068 },"
+        "  \"location\": { \"name\": \"Kraków\", \"lat\": 50.0647, \"lon\": -19.945 },"
         "  \"time\": { \"tz_iana\": \"Europe/London\", \"tz_posix\": \"GMT0BST,M3.5.0/1,M10.5.0\","
         "            \"clock_24h\": false, \"ntp\": [\"cz.pool.ntp.org\"] },"
         "  \"units\": { \"temp\": \"F\" },"
@@ -39,6 +39,9 @@ static void test_the_spec_sketch_parses(void)
     TEST_ASSERT_EQUAL_INT16(225, s_out.hum_offset_pct100);
     TEST_ASSERT_EQUAL_UINT8(2, s_out.display_every_min);
     TEST_ASSERT_EQUAL_UINT8(1, s_out.lpm_quarter_hz);
+    TEST_ASSERT_EQUAL_STRING("Kraków", s_out.place);
+    TEST_ASSERT_EQUAL_INT32(500647, s_out.lat_e4);
+    TEST_ASSERT_EQUAL_INT32(-199450, s_out.lon_e4);
 }
 
 static void test_missing_keys_take_the_defaults(void)
@@ -111,6 +114,69 @@ static void test_saving_without_a_base_round_trips(void)
     TEST_ASSERT_EQUAL_UINT(0, settings_to_json(&s, NULL, s_json, 16));
 }
 
+static void test_the_location_is_clamped_to_the_globe(void)
+{
+    const char *json = "{\"schema\": 1, \"location\": {\"name\": \"\", \"lat\": 95, \"lon\": -200.5}}";
+    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("Brno", s_out.place); /* an empty name keeps the default */
+    TEST_ASSERT_EQUAL_INT32(900000, s_out.lat_e4);
+    TEST_ASSERT_EQUAL_INT32(-1800000, s_out.lon_e4);
+}
+
+/* PATCH /api/settings (spec §10.3): the web UI sends only what a page changed. */
+static void test_a_patch_changes_only_what_it_names(void)
+{
+    const char *base = "{\"schema\": 1, \"language\": \"cs\", \"mqtt\": {\"host\": \"ha.local\"},"
+                       " \"time\": {\"tz_iana\": \"Europe/Prague\", \"clock_24h\": false, \"ntp\": [\"a\"]}}";
+    const char *patch = "{\"time\": {\"tz_iana\": \"Europe/London\", \"tz_posix\": \"GMT0BST,M3.5.0/1,M10.5.0\"},"
+                        " \"location\": {\"name\": \"Kraków\", \"lat\": 50.0647, \"lon\": 19.945}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_patch(base, patch, s_json, sizeof(s_json), s_err, sizeof(s_err)) > 0, s_err);
+    TEST_ASSERT_TRUE(settings_from_json(s_json, &s_defaults, &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("cs", s_out.language); /* not in the patch */
+    TEST_ASSERT_FALSE(s_out.clock_24h);              /* beside a patched key */
+    TEST_ASSERT_EQUAL_STRING("Europe/London", s_out.tz_iana);
+    TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", s_out.tz_posix);
+    TEST_ASSERT_EQUAL_STRING("Kraków", s_out.place);
+    TEST_ASSERT_EQUAL_INT32(199450, s_out.lon_e4);
+    TEST_ASSERT_NOT_NULL(strstr(s_json, "ha.local")); /* keys this firmware doesn't know */
+    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"ntp\""));
+}
+
+/* RFC 7396: objects merge, null removes a key, anything else replaces what was there. */
+static void test_a_patch_follows_json_merge_patch(void)
+{
+    const char *base = "{\"schema\": 1, \"time\": {\"ntp\": [\"a\", \"b\"], \"clock_24h\": false}, \"x\": {\"y\": 1}}";
+    const char *patch = "{\"time\": {\"ntp\": [\"c\"], \"clock_24h\": null}, \"x\": 5}";
+    TEST_ASSERT_TRUE(settings_patch(base, patch, s_json, sizeof(s_json), s_err, sizeof(s_err)) > 0);
+    TEST_ASSERT_EQUAL_STRING("{\"schema\":1,\"time\":{\"ntp\":[\"c\"]},\"x\":5}", s_json);
+    TEST_ASSERT_TRUE(settings_patch(NULL, "{\"language\": \"cs\"}", s_json, sizeof(s_json), s_err, sizeof(s_err)) > 0);
+    TEST_ASSERT_EQUAL_STRING("{\"schema\":1,\"language\":\"cs\"}", s_json); /* no file yet */
+}
+
+static void test_a_patch_must_be_an_object_that_keeps_the_schema(void)
+{
+    TEST_ASSERT_EQUAL_UINT(0, settings_patch("{\"schema\": 1}", "[1]", s_json, sizeof(s_json), s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("the patch must be a JSON object", s_err);
+    TEST_ASSERT_EQUAL_UINT(0, settings_patch("{\"schema\": 1}", "{\"schema\": 2}", s_json, sizeof(s_json), s_err,
+                                             sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("schema must be 1", s_err);
+    TEST_ASSERT_EQUAL_UINT(0, settings_patch("{\"schema\": 1}", "{\"schema\": null}", s_json, sizeof(s_json), s_err,
+                                             sizeof(s_err)));
+    static char deep[128];
+    size_t n = (size_t)snprintf(deep, sizeof(deep), "{\"x\": ");
+    for (int i = 0; i < 20; i++) {
+        deep[n++] = '[';
+    }
+    for (int i = 0; i < 20; i++) {
+        deep[n++] = ']';
+    }
+    snprintf(deep + n, sizeof(deep) - n, "}");
+    TEST_ASSERT_EQUAL_UINT(0, settings_patch("{\"schema\": 1}", deep, s_json, sizeof(s_json), s_err, sizeof(s_err)));
+    TEST_ASSERT_NOT_NULL(strstr(s_err, "nested"));
+    TEST_ASSERT_EQUAL_UINT(0, settings_patch("{\"schema\": 1}", "{\"language\": \"cs\"}", s_json, 8, s_err,
+                                             sizeof(s_err))); /* no room */
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -121,5 +187,9 @@ int main(void)
     RUN_TEST(test_deep_nesting_is_rejected_before_parsing);
     RUN_TEST(test_saving_keeps_keys_this_firmware_does_not_know);
     RUN_TEST(test_saving_without_a_base_round_trips);
+    RUN_TEST(test_the_location_is_clamped_to_the_globe);
+    RUN_TEST(test_a_patch_changes_only_what_it_names);
+    RUN_TEST(test_a_patch_follows_json_merge_patch);
+    RUN_TEST(test_a_patch_must_be_an_object_that_keeps_the_schema);
     return UNITY_END();
 }
```

```c
#include <stdio.h>
#include <string.h>

#include "storage_backup.h"
#include "unity.h"

static char s_bundle[4096], s_buf[4096], s_err[96];
static backup_file_t s_files[4];

void setUp(void)
{
    s_err[0] = '\0';
}

void tearDown(void) {}

static const backup_file_t k_files[] = {
    { "settings.json", "{\"schema\": 1, \"language\": \"cs\", \"location\": {\"name\": \"Kraków\"}}" },
    { "presets.json", "{\"schema\": 1, \"active\": \"home\", \"presets\": []}" },
};

/* GET /api/backup, then POST /api/restore (spec §14.4): the same files come back. */
static void test_a_bundle_round_trips_its_files(void)
{
    size_t n = backup_build(k_files, 2, "reflbo-bb94", "0.4.0", s_bundle, sizeof(s_bundle));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_bundle, "\"reflbo_backup\":1"));
    TEST_ASSERT_NOT_NULL(strstr(s_bundle, "\"device\":\"reflbo-bb94\""));
    TEST_ASSERT_NOT_NULL(strstr(s_bundle, "\"firmware\":\"0.4.0\""));
    int count = backup_split(s_bundle, s_files, 4, s_buf, sizeof(s_buf), s_err, sizeof(s_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, count, s_err);
    TEST_ASSERT_EQUAL_STRING("settings.json", s_files[0].name);
    TEST_ASSERT_EQUAL_STRING("{\"schema\":1,\"language\":\"cs\",\"location\":{\"name\":\"Kraków\"}}", s_files[0].text);
    TEST_ASSERT_EQUAL_STRING("presets.json", s_files[1].name);
    TEST_ASSERT_EQUAL_STRING("{\"schema\":1,\"active\":\"home\",\"presets\":[]}", s_files[1].text);
}

/* A file that doesn't parse (it would be ignored at boot anyway) stays out of the bundle. */
static void test_a_broken_file_is_left_out(void)
{
    const backup_file_t files[] = { { "settings.json", "{\"schema\": 1" }, k_files[1] };
    TEST_ASSERT_TRUE(backup_build(files, 2, "reflbo-bb94", "0.4.0", s_bundle, sizeof(s_bundle)) > 0);
    TEST_ASSERT_EQUAL_INT(1, backup_split(s_bundle, s_files, 4, s_buf, sizeof(s_buf), s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("presets.json", s_files[0].name);
    TEST_ASSERT_EQUAL_UINT(0, backup_build(k_files, 2, "reflbo-bb94", "0.4.0", s_bundle, 32)); /* no room */
}

static void test_restore_refuses_anything_but_a_backup(void)
{
    const char *bad[] = {
        "not json",
        "[]",
        "{\"files\": {}}",                                        /* not a backup */
        "{\"reflbo_backup\": 2, \"files\": {}}",                  /* a later format */
        "{\"reflbo_backup\": 1, \"files\": []}",                  /* files isn't an object */
        "{\"reflbo_backup\": 1, \"files\": {\"a.json\": \"x\"}}", /* a file isn't an object */
        "{\"reflbo_backup\": 1, \"files\": {\"../x.json\": {}}}", /* not a plain name */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, backup_split(bad[i], s_files, 4, s_buf, sizeof(s_buf), s_err, sizeof(s_err)),
                                      bad[i]);
        TEST_ASSERT_TRUE(s_err[0] != '\0');
    }
    TEST_ASSERT_TRUE(backup_build(k_files, 2, "reflbo-bb94", "0.4.0", s_bundle, sizeof(s_bundle)) > 0);
    TEST_ASSERT_EQUAL_INT(-1, backup_split(s_bundle, s_files, 1, s_buf, sizeof(s_buf), s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_INT(-1, backup_split(s_bundle, s_files, 4, s_buf, 40, s_err, sizeof(s_err)));
}

static void test_deep_nesting_is_refused_before_parsing(void)
{
    static char deep[256];
    size_t n = (size_t)snprintf(deep, sizeof(deep), "{\"reflbo_backup\": 1, \"files\": {\"a.json\": ");
    for (int i = 0; i < 40; i++) {
        deep[n++] = '[';
    }
    for (int i = 0; i < 40; i++) {
        deep[n++] = ']';
    }
    snprintf(deep + n, sizeof(deep) - n, "}}");
    TEST_ASSERT_EQUAL_INT(-1, backup_split(deep, s_files, 4, s_buf, sizeof(s_buf), s_err, sizeof(s_err)));
    TEST_ASSERT_NOT_NULL(strstr(s_err, "nested"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_bundle_round_trips_its_files);
    RUN_TEST(test_a_broken_file_is_left_out);
    RUN_TEST(test_restore_refuses_anything_but_a_backup);
    RUN_TEST(test_deep_nesting_is_refused_before_parsing);
    return UNITY_END();
}
```

- [ ] **Step 2: Register the new test and watch the tests fail.** In `test/host/CMakeLists.txt`, the storage library gains the new source:

```cmake
add_library(storage_logic STATIC
            ${REPO_ROOT}/components/storage/settings.c ${REPO_ROOT}/components/storage/storage_file.c
            ${REPO_ROOT}/components/storage/storage_backup.c)
```

and after the `target_compile_definitions(test_storage_file …)` line:

```cmake
reflbo_host_test(test_storage_backup storage_logic)
```

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host`
Expected: FAIL to compile: `settings_t` has no member `place`, and `storage_backup.h` not found.

- [ ] **Step 3: Implement.**

```diff
diff --git a/components/storage/include/settings.h b/components/storage/include/settings.h
index 25385fb..d42bfbd 100644
--- a/components/storage/include/settings.h
+++ b/components/storage/include/settings.h
@@ -13,6 +13,7 @@
 
 #define SETTINGS_TZ_POSIX_LEN 64
 #define SETTINGS_TZ_IANA_LEN 40
+#define SETTINGS_PLACE_LEN 32
 
 typedef struct {
     char language[4];                    /* "en" */
@@ -25,6 +26,8 @@ typedef struct {
     int16_t hum_offset_pct100; /* -2000..2000 */
     uint8_t display_every_min; /* 1..15 */
     uint8_t lpm_quarter_hz;    /* panel refresh in LPM, 0.25 Hz steps: 1, 2, 4, 8, 16 or 32 */
+    char place[SETTINGS_PLACE_LEN]; /* location.name: "Brno" */
+    int32_t lat_e4, lon_e4;         /* location in 1e-4 degrees, north and east positive: 491951, 166068 */
 } settings_t;
 
 /* Fails only if the text is not a JSON object with "schema": 1. */
@@ -32,3 +35,8 @@ bool settings_from_json(const char *json, const settings_t *defaults, settings_t
 /* `base_json` (the file as read, or NULL) with the known keys replaced by `s`. Returns the
  * length written, or 0 if `size` is too small. */
 size_t settings_to_json(const settings_t *s, const char *base_json, char *out, size_t size);
+/* PATCH /api/settings: `patch` merged into `base_json` (the file as read, or NULL for none) as an
+ * RFC 7396 merge patch. The result must keep "schema": 1. Returns the length written, or 0 with
+ * the reason in `err`. */
+size_t settings_patch(const char *base_json, const char *patch, char *out, size_t size, char *err,
+                      size_t err_size);
diff --git a/components/storage/settings.c b/components/storage/settings.c
index 8f9bff4..e6508a1 100644
--- a/components/storage/settings.c
+++ b/components/storage/settings.c
@@ -100,6 +100,10 @@ bool settings_from_json(const char *json, const settings_t *defaults, settings_t
     const cJSON *display = child(root, "display");
     out->display_every_min = (uint8_t)read_scaled(display, "update_min", out->display_every_min, 1, 1, 15);
     out->lpm_quarter_hz = lpm_from_hz(display, out->lpm_quarter_hz);
+    const cJSON *location = child(root, "location");
+    read_string(location, "name", out->place, sizeof(out->place));
+    out->lat_e4 = (int32_t)read_scaled(location, "lat", out->lat_e4, 1e4, -900000, 900000);
+    out->lon_e4 = (int32_t)read_scaled(location, "lon", out->lon_e4, 1e4, -1800000, 1800000);
     cJSON_Delete(root);
     return true;
 }
@@ -145,7 +149,58 @@ size_t settings_to_json(const settings_t *s, const char *base_json, char *out, s
     cJSON *display = object_at(root, "display");
     put(display, "update_min", cJSON_CreateNumber(s->display_every_min));
     put(display, "lpm_hz", cJSON_CreateNumber(s->lpm_quarter_hz / 4.0));
+    cJSON *location = object_at(root, "location");
+    put(location, "name", cJSON_CreateString(s->place));
+    put(location, "lat", cJSON_CreateNumber(s->lat_e4 / 1e4));
+    put(location, "lon", cJSON_CreateNumber(s->lon_e4 / 1e4));
     bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, true);
     cJSON_Delete(root);
     return ok ? strlen(out) : 0;
 }
+
+/* RFC 7396 on cJSON trees: `patch` is an object; its members merge into `target`. */
+static void merge(cJSON *target, const cJSON *patch)
+{
+    for (const cJSON *p = patch->child; p != NULL; p = p->next) {
+        if (cJSON_IsNull(p)) {
+            cJSON_DeleteItemFromObjectCaseSensitive(target, p->string);
+        } else if (cJSON_IsObject(p)) {
+            merge(object_at(target, p->string), p);
+        } else {
+            put(target, p->string, cJSON_Duplicate(p, true));
+        }
+    }
+}
+
+size_t settings_patch(const char *base_json, const char *patch, char *out, size_t size, char *err, size_t err_size)
+{
+    if (util_json_depth(patch) > SETTINGS_JSON_MAX_DEPTH) {
+        fail(err, err_size, "nested more than %d levels", SETTINGS_JSON_MAX_DEPTH);
+        return 0;
+    }
+    cJSON *p = patch != NULL ? cJSON_Parse(patch) : NULL;
+    if (!cJSON_IsObject(p)) {
+        cJSON_Delete(p);
+        fail(err, err_size, "the patch must be a JSON object");
+        return 0;
+    }
+    cJSON *root = base_json != NULL ? cJSON_Parse(base_json) : NULL;
+    if (!cJSON_IsObject(root)) { /* no file yet, or one the loader rejected */
+        cJSON_Delete(root);
+        root = cJSON_CreateObject();
+        cJSON_AddNumberToObject(root, "schema", SCHEMA);
+    }
+    merge(root, p);
+    cJSON_Delete(p);
+    const cJSON *schema = child(root, "schema");
+    size_t n = 0;
+    if (!cJSON_IsNumber(schema) || schema->valuedouble != SCHEMA) {
+        fail(err, err_size, "schema must be %d", SCHEMA);
+    } else if (size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false)) {
+        n = strlen(out);
+    } else {
+        fail(err, err_size, "the settings don't fit %u bytes", (unsigned)size);
+    }
+    cJSON_Delete(root);
+    return n;
+}
```

```c
#pragma once

#include <stddef.h>

/*
 * The settings bundle (spec §14.4): every /cfg file in one JSON document, for GET /api/backup and
 * POST /api/restore. The files hold no secrets; those live in NVS. Pure C on cJSON, host-buildable.
 *
 *   { "reflbo_backup": 1, "device": "reflbo-bb94", "firmware": "0.4.0",
 *     "files": { "settings.json": { ... }, "presets.json": { ... } } }
 */

#define BACKUP_FORMAT 1

typedef struct {
    const char *name; /* "settings.json": a plain file name in /cfg */
    const char *text; /* its JSON */
} backup_file_t;

/* A file whose text isn't JSON is left out. Returns the length written, or 0 if `size` is too small. */
size_t backup_build(const backup_file_t *files, int count, const char *device, const char *firmware, char *out,
                    size_t size);
/* The files in `bundle`, each printed into `buf`; `files` points into it. Returns how many, or -1
 * with the reason in `err`. It checks only the bundle's shape: the caller validates each file
 * before it replaces anything. */
int backup_split(const char *bundle, backup_file_t *files, int max, char *buf, size_t buf_size, char *err,
                 size_t err_size);
```

```c
#include "storage_backup.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "util_json.h"

#define BACKUP_MAX_DEPTH 18 /* a file's own 16 levels, inside the bundle's two */

static int fail(char *err, size_t size, const char *fmt, ...)
{
    if (size > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, size, fmt, ap);
        va_end(ap);
    }
    return -1;
}

size_t backup_build(const backup_file_t *files, int count, const char *device, const char *firmware, char *out,
                    size_t size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "reflbo_backup", BACKUP_FORMAT);
    cJSON_AddStringToObject(root, "device", device);
    cJSON_AddStringToObject(root, "firmware", firmware);
    cJSON *bundle = cJSON_AddObjectToObject(root, "files");
    for (int i = 0; i < count; i++) {
        cJSON *file = files[i].text != NULL ? cJSON_Parse(files[i].text) : NULL;
        if (file != NULL) {
            cJSON_AddItemToObject(bundle, files[i].name, file);
        }
    }
    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}

/* A plain name in /cfg: letters, digits, '_' and '-', then ".json". */
static bool plain_name(const char *name)
{
    size_t n = strlen(name);
    if (n < 6 || n > 32 || strcmp(name + n - 5, ".json") != 0) {
        return false;
    }
    for (size_t i = 0; i < n - 5; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

int backup_split(const char *bundle, backup_file_t *files, int max, char *buf, size_t buf_size, char *err,
                 size_t err_size)
{
    if (util_json_depth(bundle) > BACKUP_MAX_DEPTH) {
        return fail(err, err_size, "nested more than %d levels", BACKUP_MAX_DEPTH);
    }
    cJSON *root = bundle != NULL ? cJSON_Parse(bundle) : NULL;
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(root, "reflbo_backup");
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "files");
    int count = 0;
    size_t used = 0;
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(format)) {
        count = fail(err, err_size, "not a reflbo backup");
    } else if (format->valuedouble != BACKUP_FORMAT) {
        count = fail(err, err_size, "backup format %g; this firmware reads %d", format->valuedouble, BACKUP_FORMAT);
    } else if (!cJSON_IsObject(list)) {
        count = fail(err, err_size, "\"files\" must be an object");
    }
    for (const cJSON *f = count == 0 ? list->child : NULL; f != NULL; f = f->next) {
        if (!plain_name(f->string)) {
            count = fail(err, err_size, "\"%.40s\" is not a config file name", f->string);
            break;
        }
        if (!cJSON_IsObject(f)) {
            count = fail(err, err_size, "%s is not a JSON object", f->string);
            break;
        }
        if (count == max) {
            count = fail(err, err_size, "more than %d files", max);
            break;
        }
        size_t name_len = strlen(f->string) + 1;
        if (used + name_len >= buf_size ||
            !cJSON_PrintPreallocated((cJSON *)f, buf + used + name_len, (int)(buf_size - used - name_len), false)) {
            count = fail(err, err_size, "the files don't fit %u bytes", (unsigned)buf_size);
            break;
        }
        memcpy(buf + used, f->string, name_len); /* the tree is freed below */
        files[count].name = buf + used;
        files[count].text = buf + used + name_len;
        used += name_len + strlen(files[count].text) + 1;
        count++;
    }
    cJSON_Delete(root);
    return count;
}
```

```diff
diff --git a/components/storage/CMakeLists.txt b/components/storage/CMakeLists.txt
index 6c805d6..ea7df5a 100644
--- a/components/storage/CMakeLists.txt
+++ b/components/storage/CMakeLists.txt
@@ -1,5 +1,5 @@
 # LittleFS config files and the settings codec (spec §14). settings.c and storage_file.c are pure
 # C (cJSON and POSIX files) and are also built on the host by test/host.
-idf_component_register(SRCS "storage.c" "storage_file.c" "settings.c"
+idf_component_register(SRCS "storage.c" "storage_file.c" "settings.c" "storage_backup.c"
                        INCLUDE_DIRS "include"
                        PRIV_REQUIRES json util vfs joltwallet__littlefs)
```

- [ ] **Step 4: Run the tests.**

Run: `cmake --build build-host && ctest --test-dir build-host --output-on-failure -R 'test_settings|test_storage'`
Expected: PASS: `test_settings` with 11 tests, `test_storage_backup` with 4, `test_storage_file` as before.

- [ ] **Step 5: Commit.**

```bash
git add components/storage test/host/test_settings.c test/host/test_storage_backup.c test/host/CMakeLists.txt
git commit -m "feat(storage): add the location settings, a settings merge patch and the backup bundle"
```

---

### Task 6: Captive DNS, saved networks and scans (`netmgr`, pure parts)

**Files:**
- Create: `components/netmgr/CMakeLists.txt` (pure sources for now), `components/netmgr/include/netmgr_dns.h`, `components/netmgr/netmgr_dns.c`, `components/netmgr/include/netmgr_list.h`, `components/netmgr/netmgr_list.c`, `components/netmgr/include/netmgr_scan.h`, `components/netmgr/netmgr_scan.c`, `test/host/test_netmgr_dns.c`, `test/host/test_netmgr_list.c`, `test/host/test_netmgr_scan.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing new.
- Produces (Task 8's manager uses them all):
  - `size_t netmgr_dns_reply(const uint8_t *query, size_t len, const uint8_t ip[4], uint8_t *reply, size_t size)`: the captive DNS answer (spec §10.1). An A question gets `ip` with a TTL of 30 s; other types get an empty answer; anything malformed gets nothing (0).
  - `netmgr_net_t` (`ssid[33]`, `pass[65]`, `bssid[6]`, `channel`), `netmgr_list_t` (`count`, `nets[5]`), `bool netmgr_net_valid(const char *ssid, const char *pass)` (an SSID of 1–32 bytes; a password empty, of 8–63 printable ASCII characters, or 64 hex digits), `netmgr_list_find()`, `netmgr_list_add()` (goes first; replaces the password of a known SSID and forgets its cached BSSID; a full list drops its last), `netmgr_list_succeeded()` and `netmgr_list_forget()`.
  - `netmgr_seen_t` (a scan result with BSSID and channel), `netmgr_ap_t` (a network for the web UI's list), `int netmgr_seen_best(const netmgr_seen_t *, int n, const char *ssid)`, `int netmgr_seen_unique(const netmgr_seen_t *, int n, netmgr_ap_t *out, int max)` and `uint8_t netmgr_ap_channel(const netmgr_seen_t *, int n, const netmgr_list_t *saved)` (gotcha 26).

- [ ] **Step 1: Write the failing tests.**

```c
#include <string.h>

#include "netmgr_dns.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t k_ip[4] = { 192, 168, 4, 1 };

/* A query for connectivitycheck.gstatic.com: ID 0x1234, RD set, one question. */
static size_t make_query(uint8_t *q, uint16_t type, uint16_t additional)
{
    static const uint8_t header[] = { 0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0 };
    static const uint8_t name[] = "\x11" "connectivitycheck" "\x07" "gstatic" "\x03" "com";
    size_t n = 0;
    memcpy(q, header, sizeof(header));
    n += sizeof(header);
    memcpy(q + n, name, sizeof(name)); /* includes the terminating zero label */
    n += sizeof(name);
    q[n++] = (uint8_t)(type >> 8);
    q[n++] = (uint8_t)type;
    q[n++] = 0;
    q[n++] = 1; /* IN */
    q[11] = (uint8_t)additional;
    return n;
}

static void test_an_a_query_is_answered_with_the_ap_address(void)
{
    uint8_t q[128], r[256];
    size_t n = make_query(q, 1, 0);
    size_t m = netmgr_dns_reply(q, n, k_ip, r, sizeof(r));
    TEST_ASSERT_EQUAL_UINT(n + 16, m);
    TEST_ASSERT_EQUAL_HEX8(0x12, r[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, r[1]);
    TEST_ASSERT_EQUAL_HEX8(0x85, r[2]); /* response, authoritative, recursion desired */
    TEST_ASSERT_EQUAL_HEX8(0x00, r[3]); /* no error */
    TEST_ASSERT_EQUAL_HEX8(1, r[7]);    /* one answer */
    TEST_ASSERT_EQUAL_MEMORY(q + 12, r + 12, n - 12); /* the question as asked */
    const uint8_t answer[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, 192, 168, 4, 1 };
    TEST_ASSERT_EQUAL_MEMORY(answer, r + n, sizeof(answer));
}

static void test_other_types_get_no_records_and_edns_is_not_echoed(void)
{
    uint8_t q[128], r[256];
    size_t n = make_query(q, 28, 0); /* AAAA */
    TEST_ASSERT_EQUAL_UINT(n, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
    TEST_ASSERT_EQUAL_HEX8(0, r[7]);
    n = make_query(q, 1, 1); /* an OPT record announced after the question */
    static const uint8_t opt[] = { 0, 0, 41, 0x10, 0, 0, 0, 0, 0, 0, 0 };
    memcpy(q + n, opt, sizeof(opt));
    size_t m = netmgr_dns_reply(q, n + sizeof(opt), k_ip, r, sizeof(r));
    TEST_ASSERT_EQUAL_UINT(n + 16, m);
    TEST_ASSERT_EQUAL_HEX8(0, r[11]);
}

static void test_malformed_or_foreign_packets_are_dropped(void)
{
    uint8_t q[128], r[256];
    size_t n = make_query(q, 1, 0);
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, 11, k_ip, r, sizeof(r)));    /* shorter than a header */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n - 3, k_ip, r, sizeof(r))); /* the type cut off */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, 20, k_ip, r, sizeof(r)));    /* inside a label */
    q[2] = 0x81; /* a response */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
    q[2] = 0x28; /* opcode 5, an update */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
    q[2] = 0x01;
    q[5] = 2; /* two questions */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
    q[5] = 1;
    q[12] = 0xC0; /* a compression pointer in the question */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
    n = make_query(q, 1, 0);
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, n + 15)); /* no room for the answer */
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(NULL, 0, k_ip, r, sizeof(r)));
}

/* Labels whose lengths add up past 255 bytes are refused even if the packet is long enough. */
static void test_an_overlong_name_is_dropped(void)
{
    uint8_t q[600], r[700];
    memset(q, 0, sizeof(q));
    q[5] = 1;
    size_t n = 12;
    for (int i = 0; i < 5; i++) {
        q[n++] = 63;
        memset(q + n, 'a', 63);
        n += 63;
    }
    q[n++] = 0;
    q[n++] = 0;
    q[n++] = 1;
    q[n++] = 0;
    q[n++] = 1;
    TEST_ASSERT_EQUAL_UINT(0, netmgr_dns_reply(q, n, k_ip, r, sizeof(r)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_a_query_is_answered_with_the_ap_address);
    RUN_TEST(test_other_types_get_no_records_and_edns_is_not_echoed);
    RUN_TEST(test_malformed_or_foreign_packets_are_dropped);
    RUN_TEST(test_an_overlong_name_is_dropped);
    return UNITY_END();
}
```

```c
#include <string.h>

#include "netmgr_list.h"
#include "unity.h"

static netmgr_list_t s_l;

void setUp(void)
{
    memset(&s_l, 0, sizeof(s_l));
}

void tearDown(void) {}

static void test_passwords_follow_wpa2_rules(void)
{
    TEST_ASSERT_TRUE(netmgr_net_valid("home", "12345678"));
    TEST_ASSERT_TRUE(netmgr_net_valid("cafe", "")); /* open */
    TEST_ASSERT_FALSE(netmgr_net_valid("home", "1234567"));
    TEST_ASSERT_FALSE(netmgr_net_valid("", "12345678"));
    TEST_ASSERT_FALSE(netmgr_net_valid("an-ssid-that-is-longer-than-32-by", "12345678"));
    char hex[65];
    memset(hex, 'a', 64);
    hex[64] = '\0';
    TEST_ASSERT_TRUE(netmgr_net_valid("home", hex)); /* a 64-digit key */
    hex[10] = 'g';
    TEST_ASSERT_FALSE(netmgr_net_valid("home", hex));
    TEST_ASSERT_FALSE(netmgr_net_valid("home", "tab\there!"));
    TEST_ASSERT_TRUE(netmgr_net_valid("Kavárna", "presne 8"));   /* a UTF-8 SSID is fine */
    TEST_ASSERT_FALSE(netmgr_net_valid("Kavárna", "přesně 8")); /* a passphrase is ASCII (802.11i) */
}

static void test_new_networks_go_first_and_a_full_list_drops_its_last(void)
{
    const char *names[] = { "a", "b", "c", "d", "e", "f" };
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_TRUE(netmgr_list_add(&s_l, names[i], "password"));
    }
    TEST_ASSERT_EQUAL_INT(5, s_l.count);
    TEST_ASSERT_EQUAL_STRING("f", s_l.nets[0].ssid);
    TEST_ASSERT_EQUAL_STRING("b", s_l.nets[4].ssid); /* "a" dropped */
    TEST_ASSERT_EQUAL_INT(-1, netmgr_list_find(&s_l, "a"));
}

static void test_a_success_moves_a_network_first_and_keeps_where_it_was_found(void)
{
    netmgr_list_add(&s_l, "home", "password");
    netmgr_list_add(&s_l, "office", "password");
    const uint8_t bssid[6] = { 1, 2, 3, 4, 5, 6 };
    netmgr_list_succeeded(&s_l, netmgr_list_find(&s_l, "home"), bssid, 11);
    TEST_ASSERT_EQUAL_STRING("home", s_l.nets[0].ssid);
    TEST_ASSERT_EQUAL_MEMORY(bssid, s_l.nets[0].bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(11, s_l.nets[0].channel);
    netmgr_list_add(&s_l, "home", "password"); /* same password: the cache stays */
    TEST_ASSERT_EQUAL_UINT8(11, s_l.nets[0].channel);
    netmgr_list_add(&s_l, "home", "new-password"); /* a new one drops it */
    TEST_ASSERT_EQUAL_UINT8(0, s_l.nets[0].channel);
    TEST_ASSERT_EQUAL_STRING("new-password", s_l.nets[0].pass);
    TEST_ASSERT_EQUAL_INT(2, s_l.count);
}

static void test_forgetting_a_network_closes_the_gap(void)
{
    netmgr_list_add(&s_l, "a", "password");
    netmgr_list_add(&s_l, "b", "password");
    netmgr_list_add(&s_l, "c", "password"); /* c b a */
    TEST_ASSERT_TRUE(netmgr_list_forget(&s_l, "b"));
    TEST_ASSERT_EQUAL_INT(2, s_l.count);
    TEST_ASSERT_EQUAL_STRING("c", s_l.nets[0].ssid);
    TEST_ASSERT_EQUAL_STRING("a", s_l.nets[1].ssid);
    TEST_ASSERT_FALSE(netmgr_list_forget(&s_l, "b"));
    TEST_ASSERT_FALSE(netmgr_list_add(&s_l, "x", "short"));
    TEST_ASSERT_EQUAL_INT(2, s_l.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_passwords_follow_wpa2_rules);
    RUN_TEST(test_new_networks_go_first_and_a_full_list_drops_its_last);
    RUN_TEST(test_a_success_moves_a_network_first_and_keeps_where_it_was_found);
    RUN_TEST(test_forgetting_a_network_closes_the_gap);
    return UNITY_END();
}
```

```c
#include <string.h>

#include "netmgr_scan.h"
#include "unity.h"

/* A scan as the driver reports it: one entry per access point, strongest first. */
static const netmgr_seen_t k_seen[] = {
    { .ssid = "Neighbour", .bssid = { 1 }, .channel = 11, .rssi = -48 },
    { .ssid = "Home", .bssid = { 2 }, .channel = 6, .rssi = -55 },
    { .ssid = "", .bssid = { 3 }, .channel = 3, .rssi = -60 }, /* hidden */
    { .ssid = "Home", .bssid = { 4 }, .channel = 1, .rssi = -71 }, /* a second access point */
    { .ssid = "Cafe", .bssid = { 5 }, .channel = 9, .rssi = -80, .open = true },
};
#define SEEN (int)(sizeof(k_seen) / sizeof(k_seen[0]))

void setUp(void) {}
void tearDown(void) {}

static void test_the_best_sighting_is_the_strongest_access_point(void)
{
    TEST_ASSERT_EQUAL_INT(1, netmgr_seen_best(k_seen, SEEN, "Home"));
    TEST_ASSERT_EQUAL_INT(4, netmgr_seen_best(k_seen, SEEN, "Cafe"));
    TEST_ASSERT_EQUAL_INT(-1, netmgr_seen_best(k_seen, SEEN, "Office"));
    TEST_ASSERT_EQUAL_INT(-1, netmgr_seen_best(k_seen, SEEN, "")); /* hidden networks can't be picked */
    netmgr_seen_t weaker_first[] = { k_seen[3], k_seen[1] };
    TEST_ASSERT_EQUAL_INT(1, netmgr_seen_best(weaker_first, 2, "Home")); /* whatever the order */
}

static void test_the_list_for_the_web_ui_has_one_entry_per_name(void)
{
    netmgr_ap_t out[8];
    int n = netmgr_seen_unique(k_seen, SEEN, out, 8);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_STRING("Neighbour", out[0].ssid);
    TEST_ASSERT_EQUAL_STRING("Home", out[1].ssid);
    TEST_ASSERT_EQUAL_INT(-55, out[1].rssi);
    TEST_ASSERT_EQUAL_STRING("Cafe", out[2].ssid);
    TEST_ASSERT_TRUE(out[2].open);
    TEST_ASSERT_EQUAL_INT(2, netmgr_seen_unique(k_seen, SEEN, out, 2)); /* stops at max */
}

/* The AP shares the radio with the station: on the channel of the network the owner will most
 * likely pick, a test doesn't move it and drop the phone. */
static void test_the_ap_starts_on_the_channel_of_the_likely_network(void)
{
    netmgr_list_t saved = { 0 };
    TEST_ASSERT_EQUAL_UINT8(11, netmgr_ap_channel(k_seen, SEEN, &saved)); /* none saved: the strongest */
    netmgr_list_add(&saved, "Home", "12345678");
    TEST_ASSERT_EQUAL_UINT8(6, netmgr_ap_channel(k_seen, SEEN, &saved)); /* a saved one in sight */
    TEST_ASSERT_EQUAL_UINT8(1, netmgr_ap_channel(k_seen, 0, &saved));    /* nothing in sight */
    netmgr_seen_t hidden_only[] = { k_seen[2] };
    TEST_ASSERT_EQUAL_UINT8(1, netmgr_ap_channel(hidden_only, 1, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_best_sighting_is_the_strongest_access_point);
    RUN_TEST(test_the_list_for_the_web_ui_has_one_entry_per_name);
    RUN_TEST(test_the_ap_starts_on_the_channel_of_the_likely_network);
    return UNITY_END();
}
```

- [ ] **Step 2: Register them and watch them fail.** In `test/host/CMakeLists.txt`, after the `timekeeping_logic` library:

```cmake
# netmgr: the captive DNS reply, the saved-network list and the scan choices build on the host.
add_library(netmgr_logic STATIC ${REPO_ROOT}/components/netmgr/netmgr_dns.c
            ${REPO_ROOT}/components/netmgr/netmgr_list.c ${REPO_ROOT}/components/netmgr/netmgr_scan.c)
target_include_directories(netmgr_logic PUBLIC ${REPO_ROOT}/components/netmgr/include)
target_compile_options(netmgr_logic PRIVATE ${REFLBO_WARNINGS})
```

and after `reflbo_host_test(test_power_policy power_logic)`:

```cmake
reflbo_host_test(test_netmgr_dns netmgr_logic)
reflbo_host_test(test_netmgr_list netmgr_logic)
reflbo_host_test(test_netmgr_scan netmgr_logic)
```

Run: `cmake -S test/host -B build-host -G Ninja`
Expected: FAIL: CMake reports the missing `netmgr_*.c` sources.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Captive DNS (spec §10.1): the reply to one query packet, answering an A question with the AP's
 * address and any other question with no records. Returns the reply's length, or 0 to drop the
 * packet: not a standard query with one question, malformed, or too big for `size`. Pure C.
 */
size_t netmgr_dns_reply(const uint8_t *query, size_t len, const uint8_t ip[4], uint8_t *reply, size_t size);
```

```c
#include "netmgr_dns.h"

#include <stdbool.h>
#include <string.h>

#define HEADER_LEN 12
#define ANSWER_LEN 16 /* name pointer, type, class, TTL, length, IPv4 address */
#define TTL_S      30 /* short: once config mode ends, the phone's own DNS takes over soon */

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* The question's end (after QTYPE and QCLASS), or 0 if it runs past the packet. */
static size_t question_end(const uint8_t *q, size_t len)
{
    size_t at = HEADER_LEN, name = 0;
    for (;;) {
        if (at >= len) {
            return 0;
        }
        uint8_t label = q[at++];
        if (label == 0) {
            break;
        }
        if (label > 63) { /* a compression pointer or a reserved form: not in a query */
            return 0;
        }
        name += label + 1u;
        if (name > 255 || at + label > len) {
            return 0;
        }
        at += label;
    }
    return at + 4 <= len ? at + 4 : 0;
}

size_t netmgr_dns_reply(const uint8_t *query, size_t len, const uint8_t ip[4], uint8_t *reply, size_t size)
{
    if (query == NULL || len < HEADER_LEN) {
        return 0;
    }
    uint16_t flags = get16(query + 2);
    if ((flags & 0x8000) || (flags & 0x7800) || get16(query + 4) != 1) {
        return 0; /* a response, not a standard query, or not exactly one question */
    }
    size_t end = question_end(query, len);
    if (end == 0) {
        return 0;
    }
    bool a_in = get16(query + end - 4) == 1 && get16(query + end - 2) == 1;
    size_t total = end + (a_in ? ANSWER_LEN : 0);
    if (total > size) {
        return 0;
    }
    memcpy(reply, query, end); /* the ID and the question, as asked */
    put16(reply + 2, (uint16_t)(0x8000 | 0x0400 | (flags & 0x0100))); /* response, authoritative, RD echoed */
    put16(reply + 6, a_in ? 1 : 0);
    put16(reply + 8, 0);
    put16(reply + 10, 0); /* any EDNS record in the query is not echoed */
    if (a_in) {
        uint8_t *ans = reply + end;
        put16(ans, 0xC000 | HEADER_LEN); /* the name: a pointer to the question's */
        put16(ans + 2, 1);               /* A */
        put16(ans + 4, 1);               /* IN */
        put16(ans + 6, 0);
        put16(ans + 8, TTL_S);
        put16(ans + 10, 4);
        memcpy(ans + 12, ip, 4);
    }
    return total;
}
```

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Saved Wi-Fi networks (spec §10.1): up to 5, most recently successful first, as they are tried.
 * Each keeps the BSSID and channel of its last success for a fast connect. Pure C; netmgr keeps
 * the list in NVS (namespace `wifi`).
 */

#define NETMGR_LIST_MAX 5
#define NETMGR_SSID_MAX 32 /* bytes (802.11) */
#define NETMGR_PASS_MAX 64 /* a WPA2 passphrase of 8-63 characters, or a 64-digit hex key */

typedef struct {
    char ssid[NETMGR_SSID_MAX + 1];
    char pass[NETMGR_PASS_MAX + 1]; /* empty: an open network */
    uint8_t bssid[6];               /* all zero until a connection succeeds */
    uint8_t channel;
} netmgr_net_t;

typedef struct {
    uint8_t count;
    netmgr_net_t nets[NETMGR_LIST_MAX];
} netmgr_list_t;

/* An SSID of 1-32 bytes, and a password that is empty, 8-63 printable ASCII characters, or 64 hex
 * digits. */
bool netmgr_net_valid(const char *ssid, const char *pass);
int netmgr_list_find(const netmgr_list_t *l, const char *ssid); /* index, or -1 */
/* Adds a network, or replaces the password of one with the same SSID, and puts it first; a full
 * list drops its last. False if netmgr_net_valid() refuses it. */
bool netmgr_list_add(netmgr_list_t *l, const char *ssid, const char *pass);
/* The network at `index` connected: it moves to the front, with where it was found. */
void netmgr_list_succeeded(netmgr_list_t *l, int index, const uint8_t bssid[6], uint8_t channel);
bool netmgr_list_forget(netmgr_list_t *l, const char *ssid);
```

```c
#include "netmgr_list.h"

#include <string.h>

bool netmgr_net_valid(const char *ssid, const char *pass)
{
    size_t s = ssid ? strlen(ssid) : 0, p = pass ? strlen(pass) : 0;
    if (s < 1 || s > NETMGR_SSID_MAX || p > NETMGR_PASS_MAX || (p > 0 && p < 8)) {
        return false;
    }
    for (size_t i = 0; i < p; i++) {
        char c = pass[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (p == NETMGR_PASS_MAX ? !hex : (c < 0x20 || c > 0x7E)) {
            return false;
        }
    }
    return true;
}

int netmgr_list_find(const netmgr_list_t *l, const char *ssid)
{
    for (int i = 0; ssid != NULL && i < l->count; i++) {
        if (strcmp(l->nets[i].ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

/* Moves nets[index] to the front, keeping the others' order. */
static void to_front(netmgr_list_t *l, int index)
{
    netmgr_net_t net = l->nets[index];
    memmove(&l->nets[1], &l->nets[0], (size_t)index * sizeof(net));
    l->nets[0] = net;
}

bool netmgr_list_add(netmgr_list_t *l, const char *ssid, const char *pass)
{
    if (!netmgr_net_valid(ssid, pass)) {
        return false;
    }
    int index = netmgr_list_find(l, ssid);
    if (index < 0) {
        if (l->count < NETMGR_LIST_MAX) {
            l->count++;
        }
        index = l->count - 1; /* a full list gives up its last */
        memset(&l->nets[index], 0, sizeof(l->nets[index]));
        strcpy(l->nets[index].ssid, ssid);
    }
    netmgr_net_t *net = &l->nets[index];
    if (strcmp(net->pass, pass ? pass : "") != 0) { /* a new password may mean a new router */
        memset(net->bssid, 0, sizeof(net->bssid));
        net->channel = 0;
    }
    strcpy(net->pass, pass ? pass : "");
    to_front(l, index);
    return true;
}

void netmgr_list_succeeded(netmgr_list_t *l, int index, const uint8_t bssid[6], uint8_t channel)
{
    if (index < 0 || index >= l->count) {
        return;
    }
    memcpy(l->nets[index].bssid, bssid, 6);
    l->nets[index].channel = channel;
    to_front(l, index);
}

bool netmgr_list_forget(netmgr_list_t *l, const char *ssid)
{
    int index = netmgr_list_find(l, ssid);
    if (index < 0) {
        return false;
    }
    memmove(&l->nets[index], &l->nets[index + 1], (size_t)(l->count - index - 1) * sizeof(l->nets[0]));
    l->count--;
    memset(&l->nets[l->count], 0, sizeof(l->nets[0]));
    return true;
}
```

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "netmgr_list.h"

/* What a Wi-Fi scan saw, and the choices made from it. Pure C, host-buildable. */

typedef struct {
    char ssid[NETMGR_SSID_MAX + 1]; /* "" for a hidden network */
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    bool open;
} netmgr_seen_t;

/* A network for the web UI's list. */
typedef struct {
    char ssid[NETMGR_SSID_MAX + 1];
    int8_t rssi;
    bool open;
} netmgr_ap_t;

/* The strongest access point named `ssid`, as an index into `seen`; -1 if none. */
int netmgr_seen_best(const netmgr_seen_t *seen, int n, const char *ssid);
/* One entry per name, in the order seen (the driver lists the strongest first), hidden networks
 * left out. Returns how many went into `out`. */
int netmgr_seen_unique(const netmgr_seen_t *seen, int n, netmgr_ap_t *out, int max);
/* The channel for the device's own AP. The station shares the radio, so joining a network on
 * another channel moves the AP and drops the phone on it: start on the channel of the strongest
 * saved network in sight, else of the strongest network, else 1. `saved` may be NULL. */
uint8_t netmgr_ap_channel(const netmgr_seen_t *seen, int n, const netmgr_list_t *saved);
```

```c
#include "netmgr_scan.h"

#include <stdio.h>
#include <string.h>

#define DEFAULT_CHANNEL 1

int netmgr_seen_best(const netmgr_seen_t *seen, int n, const char *ssid)
{
    int best = -1;
    for (int i = 0; ssid != NULL && ssid[0] != '\0' && i < n; i++) {
        if (strcmp(seen[i].ssid, ssid) == 0 && (best < 0 || seen[i].rssi > seen[best].rssi)) {
            best = i;
        }
    }
    return best;
}

int netmgr_seen_unique(const netmgr_seen_t *seen, int n, netmgr_ap_t *out, int max)
{
    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        bool dup = seen[i].ssid[0] == '\0';
        for (int j = 0; j < count && !dup; j++) {
            dup = strcmp(out[j].ssid, seen[i].ssid) == 0;
        }
        if (!dup) {
            snprintf(out[count].ssid, sizeof(out[count].ssid), "%s", seen[i].ssid);
            out[count].rssi = seen[i].rssi;
            out[count].open = seen[i].open;
            count++;
        }
    }
    return count;
}

uint8_t netmgr_ap_channel(const netmgr_seen_t *seen, int n, const netmgr_list_t *saved)
{
    int best = -1;
    for (int s = 0; saved != NULL && s < saved->count; s++) {
        int i = netmgr_seen_best(seen, n, saved->nets[s].ssid);
        if (i >= 0 && (best < 0 || seen[i].rssi > seen[best].rssi)) {
            best = i;
        }
    }
    for (int i = 0; best < 0 && i < n; i++) {
        if (seen[i].ssid[0] != '\0' && (best < 0 || seen[i].rssi > seen[best].rssi)) {
            best = i;
        }
    }
    return best >= 0 && seen[best].channel >= 1 && seen[best].channel <= 13 ? seen[best].channel : DEFAULT_CHANNEL;
}
```

The component builds its pure parts for now; Task 8 adds the manager:

```cmake
# Wi-Fi manager (spec §10.1, §10.2): saved networks, the AP with captive DNS, mDNS. The files here
# are pure C and also built on the host; Task 8 of the M4 plan adds the manager itself.
idf_component_register(SRCS "netmgr_dns.c" "netmgr_list.c" "netmgr_scan.c"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 4: Run the tests, and build the firmware.** ESP-IDF finds a new component only when it configures.

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure -R test_netmgr && tools/idf.sh reconfigure && tools/idf.sh build`
Expected: PASS: `test_netmgr_dns` 4 tests, `test_netmgr_list` 4, `test_netmgr_scan` 3; the firmware builds without warnings.

- [ ] **Step 5: Commit.**

```bash
git add components/netmgr test/host/test_netmgr_*.c test/host/CMakeLists.txt
git commit -m "feat(netmgr): add the captive DNS reply, the saved-network list and the scan choices"
```

---

### Task 7: The web password and HTTP helpers (`webui`, pure parts)

**Files:**
- Create: `components/webui/CMakeLists.txt` (pure sources for now), `components/webui/include/webui_auth.h`, `components/webui/webui_auth.c`, `components/webui/include/webui_http.h`, `components/webui/webui_http.c`, `test/host/test_webui_auth.c`, `test/host/test_webui_http.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `util_pbkdf2_sha256()` and `util_ct_equal()` (Task 1).
- Produces (Task 9's server):
  - `bool webui_password_valid(const char *)` (8–64 bytes, no control characters), `bool webui_auth_record(const char *password, const uint8_t salt[16], uint32_t iterations, char *out, size_t size)` (`pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>`) and `bool webui_auth_check(const char *record, const char *password)`.
  - `webui_sessions_t` with `webui_sessions_clear()`, `webui_session_new()` (16 random bytes; the least recently used session makes room), `webui_session_check()`, `webui_cookie_token()` (`session=<32 hex>` in a Cookie header), `webui_login_allowed()` and `webui_login_result()` (after 5 failures in a row, 60 s without logins).
  - `bool webui_is_json_type(const char *content_type)`, `int webui_url_decode(const char *in, char *out, size_t size)` (the length, or -1), `bool webui_host_is(const char *host, const char *name)` (a Host header with or without its port).

- [ ] **Step 1: Write the failing tests.** The auth test runs PBKDF2 with few iterations; the record's format and the throttle are what it pins.

```c
#include <string.h>

#include "unity.h"
#include "webui_auth.h"

static webui_sessions_t s_t;
static const uint8_t k_salt[WEBUI_AUTH_SALT_LEN] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

void setUp(void)
{
    webui_sessions_clear(&s_t);
}

void tearDown(void) {}

static void test_passwords_are_8_to_64_bytes_without_control_characters(void)
{
    TEST_ASSERT_TRUE(webui_password_valid("12345678"));
    TEST_ASSERT_TRUE(webui_password_valid("heslo s háčky"));
    TEST_ASSERT_FALSE(webui_password_valid("1234567"));
    TEST_ASSERT_FALSE(webui_password_valid("tab\tinside"));
    TEST_ASSERT_FALSE(webui_password_valid(NULL));
    char long_one[66];
    memset(long_one, 'x', 65);
    long_one[65] = '\0';
    TEST_ASSERT_FALSE(webui_password_valid(long_one));
    long_one[64] = '\0';
    TEST_ASSERT_TRUE(webui_password_valid(long_one));
}

/* The record holds the salt and PBKDF2-HMAC-SHA256 (checked against Python's hashlib). */
static void test_a_record_is_salted_pbkdf2_and_checks_the_password(void)
{
    char record[WEBUI_AUTH_RECORD_LEN];
    TEST_ASSERT_TRUE(webui_auth_record("correct horse", k_salt, 3, record, sizeof(record)));
    TEST_ASSERT_EQUAL_STRING("pbkdf2-sha256$3$000102030405060708090a0b0c0d0e0f$"
                             "a788b6bff5da2b8d9a4a5f41df62a5cc17d939de95f5e7a677d654c0edbeac73",
                             record);
    TEST_ASSERT_TRUE(webui_auth_check(record, "correct horse"));
    TEST_ASSERT_FALSE(webui_auth_check(record, "correct horsE"));
    TEST_ASSERT_FALSE(webui_auth_check(record, ""));
    TEST_ASSERT_FALSE(webui_auth_record("short", k_salt, 3, record, sizeof(record)));
    TEST_ASSERT_TRUE(webui_auth_record("correct horse", k_salt, WEBUI_AUTH_ITERATIONS, record, sizeof(record)));
    TEST_ASSERT_TRUE(webui_auth_check(record, "correct horse"));
}

static void test_a_malformed_record_matches_nothing(void)
{
    const char *bad[] = {
        "",
        "sha1$3$00$00",
        "pbkdf2-sha256$x$000102030405060708090a0b0c0d0e0f$a788",
        "pbkdf2-sha256$3$0001$a788b6bff5da2b8d9a4a5f41df62a5cc17d939de95f5e7a677d654c0edbeac73",
        "pbkdf2-sha256$3$000102030405060708090a0b0c0d0e0f$a788b6",
        "pbkdf2-sha256$3$000102030405060708090a0b0c0d0e0f",
        ("pbkdf2-sha256$0$000102030405060708090a0b0c0d0e0f$"
         "a788b6bff5da2b8d9a4a5f41df62a5cc17d939de95f5e7a677d654c0edbeac73"),
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(webui_auth_check(bad[i], "correct horse"), bad[i]);
    }
    TEST_ASSERT_FALSE(webui_auth_check(NULL, "correct horse"));
}

static void test_sessions_are_random_tokens_and_the_oldest_makes_room(void)
{
    uint8_t random[16];
    char first[WEBUI_TOKEN_LEN + 1];
    for (int i = 0; i < WEBUI_SESSIONS; i++) {
        memset(random, i + 1, sizeof(random));
        const char *token = webui_session_new(&s_t, random, 100 + i);
        TEST_ASSERT_EQUAL_UINT(WEBUI_TOKEN_LEN, strlen(token));
        if (i == 0) {
            strcpy(first, token);
        }
    }
    TEST_ASSERT_EQUAL_STRING("01010101010101010101010101010101", first);
    TEST_ASSERT_TRUE(webui_session_check(&s_t, first, 200)); /* used again: now the newest */
    memset(random, 9, sizeof(random));
    webui_session_new(&s_t, random, 300); /* evicts the one made at 101 */
    TEST_ASSERT_TRUE(webui_session_check(&s_t, first, 301));
    TEST_ASSERT_FALSE(webui_session_check(&s_t, "02020202020202020202020202020202", 302));
    TEST_ASSERT_FALSE(webui_session_check(&s_t, "0101", 302));
    webui_sessions_clear(&s_t); /* config mode ended */
    TEST_ASSERT_FALSE(webui_session_check(&s_t, first, 303));
}

static void test_the_cookie_header_yields_the_token(void)
{
    char token[WEBUI_TOKEN_LEN + 1];
    TEST_ASSERT_TRUE(webui_cookie_token("theme=dark; session=0123456789abcdef0123456789abcdef; x=1", token));
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", token);
    TEST_ASSERT_TRUE(webui_cookie_token("session=0123456789abcdef0123456789abcdef", token));
    TEST_ASSERT_FALSE(webui_cookie_token("session=0123", token));
    TEST_ASSERT_FALSE(webui_cookie_token("mysession=0123456789abcdef0123456789abcdef", token));
    TEST_ASSERT_FALSE(webui_cookie_token(NULL, token));
}

static void test_five_failed_logins_make_the_next_wait_a_minute(void)
{
    for (int i = 0; i < WEBUI_FAILS_BEFORE_WAIT - 1; i++) {
        webui_login_result(&s_t, false, 10);
        TEST_ASSERT_TRUE(webui_login_allowed(&s_t, 10));
    }
    webui_login_result(&s_t, false, 10);
    TEST_ASSERT_FALSE(webui_login_allowed(&s_t, 69));
    TEST_ASSERT_TRUE(webui_login_allowed(&s_t, 70));
    webui_login_result(&s_t, false, 70);
    webui_login_result(&s_t, true, 71); /* a success starts the count over */
    for (int i = 0; i < WEBUI_FAILS_BEFORE_WAIT - 1; i++) {
        webui_login_result(&s_t, false, 72);
    }
    TEST_ASSERT_TRUE(webui_login_allowed(&s_t, 72));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_passwords_are_8_to_64_bytes_without_control_characters);
    RUN_TEST(test_a_record_is_salted_pbkdf2_and_checks_the_password);
    RUN_TEST(test_a_malformed_record_matches_nothing);
    RUN_TEST(test_sessions_are_random_tokens_and_the_oldest_makes_room);
    RUN_TEST(test_the_cookie_header_yields_the_token);
    RUN_TEST(test_five_failed_logins_make_the_next_wait_a_minute);
    return UNITY_END();
}
```

```c
#include <string.h>

#include "unity.h"
#include "webui_http.h"

void setUp(void) {}
void tearDown(void) {}

static void test_only_json_counts_as_json(void)
{
    TEST_ASSERT_TRUE(webui_is_json_type("application/json"));
    TEST_ASSERT_TRUE(webui_is_json_type("Application/JSON; charset=utf-8"));
    TEST_ASSERT_FALSE(webui_is_json_type("application/x-www-form-urlencoded")); /* a cross-site form */
    TEST_ASSERT_FALSE(webui_is_json_type("text/plain"));
    TEST_ASSERT_FALSE(webui_is_json_type("application/jsonp"));
    TEST_ASSERT_FALSE(webui_is_json_type(NULL));
}

static void test_query_values_are_url_decoded(void)
{
    char out[16];
    TEST_ASSERT_EQUAL_INT(6, webui_url_decode("My%20Net", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("My Net", out);
    TEST_ASSERT_EQUAL_INT(8, webui_url_decode("Kav%C3%A1rna", out, sizeof(out))); /* UTF-8 bytes */
    TEST_ASSERT_EQUAL_STRING("Kav\xC3\xA1rna", out);
    TEST_ASSERT_EQUAL_INT(3, webui_url_decode("a+b", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a b", out);
    TEST_ASSERT_EQUAL_INT(-1, webui_url_decode("bad%2", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, webui_url_decode("nul%00", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, webui_url_decode("0123456789abcdef", out, sizeof(out))); /* no room */
    TEST_ASSERT_EQUAL_INT(0, webui_url_decode("", out, sizeof(out)));
}

static void test_the_host_header_is_matched_loosely(void)
{
    TEST_ASSERT_TRUE(webui_host_is("reflbo-bb94.local", "reflbo-bb94.local"));
    TEST_ASSERT_TRUE(webui_host_is("REFLBO-BB94.local.", "reflbo-bb94.local"));
    TEST_ASSERT_TRUE(webui_host_is("192.168.4.1:80", "192.168.4.1"));
    TEST_ASSERT_FALSE(webui_host_is("captive.apple.com", "192.168.4.1"));
    TEST_ASSERT_FALSE(webui_host_is("192.168.4.10", "192.168.4.1"));
    TEST_ASSERT_FALSE(webui_host_is(NULL, "192.168.4.1"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_only_json_counts_as_json);
    RUN_TEST(test_query_values_are_url_decoded);
    RUN_TEST(test_the_host_header_is_matched_loosely);
    return UNITY_END();
}
```

- [ ] **Step 2: Register them and watch them fail.** In `test/host/CMakeLists.txt`, after the `netmgr_logic` library:

```cmake
# webui: the password record, the sessions and the HTTP helpers build on the host.
add_library(webui_logic STATIC ${REPO_ROOT}/components/webui/webui_auth.c
            ${REPO_ROOT}/components/webui/webui_http.c)
target_include_directories(webui_logic PUBLIC ${REPO_ROOT}/components/webui/include)
target_compile_options(webui_logic PRIVATE ${REFLBO_WARNINGS})
target_link_libraries(webui_logic PUBLIC util)
```

and after `reflbo_host_test(test_netmgr_scan netmgr_logic)`:

```cmake
reflbo_host_test(test_webui_auth webui_logic)
reflbo_host_test(test_webui_http webui_logic)
```

Run: `cmake -S test/host -B build-host -G Ninja`
Expected: FAIL: CMake reports the missing `webui_*.c` sources.

- [ ] **Step 3: Implement.**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The web UI password and sessions (spec §10.4, D18). Pure C, host-buildable; webui.c keeps the
 * record in NVS `secrets` and draws the random bytes from the hardware RNG.
 */

#define WEBUI_PASSWORD_MIN     8
#define WEBUI_PASSWORD_MAX     64
#define WEBUI_AUTH_ITERATIONS  10000 /* about 2 s per check on the ESP32-S3 (measured at M4) */
#define WEBUI_AUTH_SALT_LEN    16
#define WEBUI_AUTH_RECORD_LEN  128   /* "pbkdf2-sha256$10000$<32 hex>$<64 hex>" and its terminator */
#define WEBUI_TOKEN_LEN        32    /* hex digits of 16 random bytes */
#define WEBUI_SESSIONS         4
#define WEBUI_FAILS_BEFORE_WAIT 5
#define WEBUI_FAIL_WAIT_S      60

/* 8-64 bytes, none of them control characters. */
bool webui_password_valid(const char *password);
/* The stored form of a password: "pbkdf2-sha256$<iterations>$<salt>$<hash>", in hex. */
bool webui_auth_record(const char *password, const uint8_t salt[WEBUI_AUTH_SALT_LEN], uint32_t iterations,
                       char *out, size_t size);
/* True if `password` matches the record; false for a malformed record too. */
bool webui_auth_check(const char *record, const char *password);

typedef struct {
    char token[WEBUI_TOKEN_LEN + 1]; /* empty: a free slot */
    int64_t used_s;
} webui_session_t;

typedef struct {
    webui_session_t s[WEBUI_SESSIONS];
    uint8_t fails;      /* failed logins in a row */
    int64_t wait_until; /* no login is checked before this (monotonic seconds) */
} webui_sessions_t;

void webui_sessions_clear(webui_sessions_t *t);
/* A new session from 16 random bytes; the least recently used one makes room. Returns the token. */
const char *webui_session_new(webui_sessions_t *t, const uint8_t random[16], int64_t now_s);
/* True if `token` is a live session; it counts as used now. */
bool webui_session_check(webui_sessions_t *t, const char *token, int64_t now_s);
/* The session token in a Cookie header ("...; session=<token>; ..."), or false. */
bool webui_cookie_token(const char *cookie, char out[WEBUI_TOKEN_LEN + 1]);
/* Login throttling: false while waiting after WEBUI_FAILS_BEFORE_WAIT failures in a row. */
bool webui_login_allowed(const webui_sessions_t *t, int64_t now_s);
void webui_login_result(webui_sessions_t *t, bool ok, int64_t now_s);
```

```c
#include "webui_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util_sha256.h"

#define PREFIX "pbkdf2-sha256$"

bool webui_password_valid(const char *password)
{
    size_t n = password ? strlen(password) : 0;
    if (n < WEBUI_PASSWORD_MIN || n > WEBUI_PASSWORD_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)password[i];
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
    }
    return true;
}

static void to_hex(const uint8_t *bytes, size_t len, char *out)
{
    static const char k_hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = k_hex[bytes[i] >> 4];
        out[2 * i + 1] = k_hex[bytes[i] & 15];
    }
    out[2 * len] = '\0';
}

static bool from_hex(const char *hex, size_t hex_len, uint8_t *out, size_t len)
{
    if (hex_len != 2 * len) {
        return false;
    }
    for (size_t i = 0; i < 2 * len; i++) {
        char c = hex[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) {
            return false;
        }
        out[i / 2] = (uint8_t)(i % 2 ? (out[i / 2] | v) : v << 4);
    }
    return true;
}

bool webui_auth_record(const char *password, const uint8_t salt[WEBUI_AUTH_SALT_LEN], uint32_t iterations,
                       char *out, size_t size)
{
    if (!webui_password_valid(password) || iterations == 0) {
        return false;
    }
    uint8_t hash[UTIL_SHA256_LEN];
    util_pbkdf2_sha256(password, strlen(password), salt, WEBUI_AUTH_SALT_LEN, iterations, hash, sizeof(hash));
    char salt_hex[2 * WEBUI_AUTH_SALT_LEN + 1], hash_hex[2 * UTIL_SHA256_LEN + 1];
    to_hex(salt, WEBUI_AUTH_SALT_LEN, salt_hex);
    to_hex(hash, sizeof(hash), hash_hex);
    int n = snprintf(out, size, PREFIX "%lu$%s$%s", (unsigned long)iterations, salt_hex, hash_hex);
    return n > 0 && (size_t)n < size;
}

bool webui_auth_check(const char *record, const char *password)
{
    if (record == NULL || password == NULL || strncmp(record, PREFIX, strlen(PREFIX)) != 0) {
        return false;
    }
    const char *p = record + strlen(PREFIX);
    char *end;
    unsigned long iterations = strtoul(p, &end, 10);
    if (end == p || *end != '$' || iterations == 0 || iterations > 1000000) {
        return false;
    }
    const char *salt_hex = end + 1, *dollar = strchr(salt_hex, '$');
    uint8_t salt[WEBUI_AUTH_SALT_LEN], want[UTIL_SHA256_LEN], got[UTIL_SHA256_LEN];
    if (dollar == NULL || !from_hex(salt_hex, (size_t)(dollar - salt_hex), salt, sizeof(salt)) ||
        !from_hex(dollar + 1, strlen(dollar + 1), want, sizeof(want))) {
        return false;
    }
    util_pbkdf2_sha256(password, strlen(password), salt, sizeof(salt), (uint32_t)iterations, got, sizeof(got));
    return util_ct_equal(got, want, sizeof(got));
}

void webui_sessions_clear(webui_sessions_t *t)
{
    memset(t, 0, sizeof(*t));
}

const char *webui_session_new(webui_sessions_t *t, const uint8_t random[16], int64_t now_s)
{
    int slot = 0;
    for (int i = 0; i < WEBUI_SESSIONS; i++) {
        if (t->s[i].token[0] == '\0') {
            slot = i;
            break;
        }
        if (t->s[i].used_s < t->s[slot].used_s) {
            slot = i;
        }
    }
    to_hex(random, 16, t->s[slot].token);
    t->s[slot].used_s = now_s;
    return t->s[slot].token;
}

bool webui_session_check(webui_sessions_t *t, const char *token, int64_t now_s)
{
    if (token == NULL || strlen(token) != WEBUI_TOKEN_LEN) {
        return false;
    }
    bool found = false;
    for (int i = 0; i < WEBUI_SESSIONS; i++) { /* every slot is compared, in the same time */
        if (t->s[i].token[0] != '\0' && util_ct_equal(t->s[i].token, token, WEBUI_TOKEN_LEN)) {
            t->s[i].used_s = now_s;
            found = true;
        }
    }
    return found;
}

bool webui_cookie_token(const char *cookie, char out[WEBUI_TOKEN_LEN + 1])
{
    for (const char *p = cookie; p != NULL && *p != '\0';) {
        while (*p == ' ' || *p == ';') {
            p++;
        }
        size_t len = strcspn(p, ";");
        if (len == 8 + WEBUI_TOKEN_LEN && strncmp(p, "session=", 8) == 0) {
            memcpy(out, p + 8, WEBUI_TOKEN_LEN);
            out[WEBUI_TOKEN_LEN] = '\0';
            return true;
        }
        p += len;
    }
    return false;
}

bool webui_login_allowed(const webui_sessions_t *t, int64_t now_s)
{
    return now_s >= t->wait_until;
}

void webui_login_result(webui_sessions_t *t, bool ok, int64_t now_s)
{
    if (ok) {
        t->fails = 0;
        return;
    }
    if (++t->fails >= WEBUI_FAILS_BEFORE_WAIT) {
        t->fails = 0;
        t->wait_until = now_s + WEBUI_FAIL_WAIT_S;
    }
}
```

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Small HTTP helpers for the web configurator. Pure C, host-buildable. */

/* "application/json", with any parameters ("; charset=utf-8"), in any case (spec §10.3). */
bool webui_is_json_type(const char *content_type);
/* Decodes %XX and '+' in a query value. Returns the decoded length, or -1 for a malformed escape, a
 * NUL byte, or no room. */
int webui_url_decode(const char *in, char *out, size_t size);
/* True if a Host header names `name`, ignoring case, a port and a trailing dot. */
bool webui_host_is(const char *host, const char *name);
```

```c
#include "webui_http.h"

#include <ctype.h>
#include <string.h>

bool webui_is_json_type(const char *content_type)
{
    static const char k_json[] = "application/json";
    if (content_type == NULL) {
        return false;
    }
    while (*content_type == ' ') {
        content_type++;
    }
    size_t n = strlen(k_json);
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)content_type[i]) != k_json[i]) {
            return false;
        }
    }
    char next = content_type[n];
    return next == '\0' || next == ';' || next == ' ';
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                                                                         : -1;
}

int webui_url_decode(const char *in, char *out, size_t size)
{
    size_t n = 0;
    for (const char *p = in; p != NULL && *p != '\0'; p++) {
        char c = *p;
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            int hi = hex_digit(p[1]), lo = hi < 0 ? -1 : hex_digit(p[2]);
            if (lo < 0) {
                return -1;
            }
            c = (char)(hi << 4 | lo);
            if (c == '\0') {
                return -1;
            }
            p += 2;
        }
        if (n + 1 >= size) {
            return -1;
        }
        out[n++] = c;
    }
    if (size == 0) {
        return -1;
    }
    out[n] = '\0';
    return (int)n;
}

bool webui_host_is(const char *host, const char *name)
{
    if (host == NULL || name == NULL) {
        return false;
    }
    size_t len = strcspn(host, ":"); /* an IPv6 literal never reaches here: the AP is IPv4 */
    if (len > 0 && host[len - 1] == '.') {
        len--;
    }
    if (len != strlen(name)) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)host[i]) != tolower((unsigned char)name[i])) {
            return false;
        }
    }
    return true;
}
```

The component builds its pure parts for now; Task 9 adds the server:

```cmake
# Web configurator (spec §10.3, §10.4). The files here are pure C and also built on the host; Task 9
# of the M4 plan adds the server itself.
idf_component_register(SRCS "webui_auth.c" "webui_http.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES util)
```

- [ ] **Step 4: Run the tests, and build the firmware.**

Run: `cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host --output-on-failure && tools/idf.sh reconfigure && tools/idf.sh build`
Expected: all pass, 43 of 43: `test_webui_auth` 6 tests, `test_webui_http` 3; the firmware builds without warnings.

- [ ] **Step 5: Commit.**

```bash
git add components/webui test/host/test_webui_*.c test/host/CMakeLists.txt
git commit -m "feat(webui): add the web password record, sessions and HTTP helpers"
```

---

### Task 8: The Wi-Fi manager (`netmgr`)

**Files:**
- Create: `components/netmgr/include/netmgr.h`, `components/netmgr/netmgr.c`, `components/netmgr/idf_component.yml`
- Modify: `components/netmgr/CMakeLists.txt`, `dependencies.lock`

**Interfaces:**
- Consumes: Task 6's DNS reply, list and scan choices.
- Produces (Tasks 9 and 10):
  - `netmgr_state_t` (`NETMGR_OFF`, `NETMGR_JOINING`, `NETMGR_STATION`, `NETMGR_AP`), `netmgr_test_t` (`NONE`, `RUNNING`, `OK`, `WRONG_PASSWORD`, `NOT_FOUND`, `FAILED`, `INVALID`), `netmgr_status_t` (state, `ap_on`, network, IP, RSSI, AP clients, AP SSID and password, host name, and the last test with its SSID), `NETMGR_AP_IP` and `NETMGR_SCAN_MAX`.
  - `esp_err_t netmgr_init(void (*changed)(void))`: once, after `nvs_flash_init()`; reads the list (NVS `wifi/nets`) and the AP password (NVS `sys/ap_pass`, 10 characters made at the first boot). `changed` runs on the manager's task or the event loop after every change and must not block.
  - `void netmgr_start(bool keep_ap)`: the saved networks, then the AP; `keep_ap` runs the AP beside a station (no web password yet, D18). `void netmgr_stop(void)`: Wi-Fi off and deinitialised.
  - `void netmgr_status(netmgr_status_t *)`, `int netmgr_scan(netmgr_ap_t *out, int max)` (waits for the scan), `esp_err_t netmgr_test_start(const char *ssid, const char *pass)` (returns at once; the status reports the result), `esp_err_t netmgr_add()`, `void netmgr_networks(netmgr_list_t *)` (without passwords), `esp_err_t netmgr_forget()`, `esp_err_t netmgr_forget_all(void)` and `const char *netmgr_test_name(netmgr_test_t)`.

- [ ] **Step 1: Implement.** One task owns Wi-Fi and takes commands from a queue; the status is guarded by a mutex, so any task may read it.
  - **Starting.** The saved networks are tried in order, each first at its cached BSSID and channel, then with a scan; 8 s per try. Without a network: a scan picks the AP's channel (gotcha 26), then the AP starts with its captive DNS task, DHCP's portal address (RFC 8910) and mDNS `reflbo-XXXX.local`.
  - **A test** (spec §10.1). An AP that already runs is left alone: setting its config again drops its clients. A scan finds the network: out of reach, it is "not found" at once. Otherwise the device joins by BSSID and channel. On success the network is saved first; on failure the device rejoins the network it was on.
  - A lost network is retried; passwords are never logged and are wiped from the command buffers.

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "netmgr_list.h"
#include "netmgr_scan.h"

/*
 * The Wi-Fi manager (spec §10.1, §10.2). Wi-Fi is on only in config mode: netmgr_start() joins a
 * saved network, or starts the device's own AP with a captive portal when none is saved or none
 * answers. It runs in its own task; the calls return at once, except netmgr_scan(), which waits
 * for its result. Any task may call them.
 */

typedef enum {
    NETMGR_OFF,
    NETMGR_JOINING, /* trying the saved networks */
    NETMGR_STATION, /* on a network: `ip` is the device's address there */
    NETMGR_AP,      /* only the device's own AP runs */
} netmgr_state_t;

typedef enum {
    NETMGR_TEST_NONE,    /* no test since Wi-Fi came on */
    NETMGR_TEST_RUNNING,
    NETMGR_TEST_OK,
    NETMGR_TEST_WRONG_PASSWORD,
    NETMGR_TEST_NOT_FOUND,
    NETMGR_TEST_FAILED,  /* anything else: no address, a timeout */
    NETMGR_TEST_INVALID, /* netmgr_net_valid() refused it */
} netmgr_test_t;

typedef struct {
    netmgr_state_t state;
    bool ap_on;                    /* the AP runs: the AP state, or beside the station (keep_ap, a test) */
    char ssid[NETMGR_SSID_MAX + 1]; /* the network joined */
    char ip[16];                   /* on that network */
    int8_t rssi;
    uint8_t ap_clients;
    char ap_ssid[NETMGR_SSID_MAX + 1]; /* reflbo-XXXX */
    char ap_pass[12];
    char host[16]; /* reflbo-XXXX: the mDNS name without .local */
    netmgr_test_t test; /* the web UI's last test, and of which network */
    char test_ssid[NETMGR_SSID_MAX + 1];
} netmgr_status_t;

#define NETMGR_AP_IP "192.168.4.1"
#define NETMGR_SCAN_MAX 20


/* Reads the saved networks and the AP password (generated at the first boot) from NVS. Call
 * once, after nvs_flash_init(); a second call returns ESP_ERR_INVALID_STATE. `changed` runs on
 * the netmgr task or the event loop after every state change, and must not block. */
esp_err_t netmgr_init(void (*changed)(void));
/* Config mode starts: a saved network, else the AP. `keep_ap` keeps the AP up beside a station,
 * while no web password is set (D18). */
void netmgr_start(bool keep_ap);
void netmgr_stop(void); /* Wi-Fi off */
void netmgr_status(netmgr_status_t *out);
/* Visible networks, strongest first, one entry per SSID; returns how many (up to `max`). */
int netmgr_scan(netmgr_ap_t *out, int max);
/* The web UI's "test" (spec §10.2): joins a new network beside the AP, found by a scan first so
 * a network out of reach never takes the AP off its channel. On success it is saved first in the
 * list and the device stays on it; on failure it goes back to the network it was on. Returns at
 * once: netmgr_status() reports the result. ESP_ERR_INVALID_ARG if netmgr_net_valid() refuses
 * the network, ESP_ERR_INVALID_STATE while Wi-Fi is off or a test runs. */
esp_err_t netmgr_test_start(const char *ssid, const char *pass);
/* Saves a network without trying it, as it may be out of range now; ESP_ERR_INVALID_ARG if
 * netmgr_net_valid() refuses it. */
esp_err_t netmgr_add(const char *ssid, const char *pass);
void netmgr_networks(netmgr_list_t *out); /* the saved list; the passwords are left out */
esp_err_t netmgr_forget(const char *ssid); /* ESP_ERR_NOT_FOUND if it isn't saved */
esp_err_t netmgr_forget_all(void);         /* Menu ▸ Wi-Fi ▸ Forget networks */
const char *netmgr_test_name(netmgr_test_t result);
```

```c
#include "netmgr.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "netmgr_dns.h"
#include "nvs.h"

static const char *TAG = "netmgr";

#define TASK_STACK      6144
#define TASK_PRIORITY   4 /* below the app (5) and the buttons (6) */
#define DNS_STACK       3072
#define DNS_PRIORITY    3
#define JOIN_TIMEOUT_MS 8000 /* spec §10.1: 8 s per attempt */
#define AP_CLIENTS_MAX  2
#define LIST_VERSION    1
#define SEEN_MAX        24

typedef enum {
    CMD_START,
    CMD_STOP,
    CMD_SCAN,
    CMD_TEST,
} cmd_kind_t;

typedef struct {
    uint8_t kind;
    bool keep_ap;
    char ssid[NETMGR_SSID_MAX + 1];
    char pass[NETMGR_PASS_MAX + 1];
} cmd_t;

typedef struct {
    uint8_t version;
    netmgr_list_t list;
} stored_list_t;

#define EV_GOT_IP BIT0
#define EV_FAILED BIT1

static QueueHandle_t s_cmds;
static SemaphoreHandle_t s_lock; /* s_status and s_list: read by any task, written by the netmgr task */
static SemaphoreHandle_t s_done; /* a scan finished */
static SemaphoreHandle_t s_scan_lock; /* one netmgr_scan() at a time: the console and the web UI may both ask */
static EventGroupHandle_t s_events;
static void (*s_changed)(void);
static netmgr_status_t s_status;
static netmgr_list_t s_list;
static volatile uint8_t s_reason; /* of the last disconnection */
static volatile bool s_joining;   /* join() waits for the result itself */
static esp_netif_t *s_sta, *s_ap;
static bool s_wifi_up, s_mdns_up;
static volatile bool s_dns_run;
static TaskHandle_t s_dns_task;
static netmgr_seen_t s_seen[SEEN_MAX]; /* the last scan, on the netmgr task */
static int s_seen_n;
static netmgr_ap_t *s_scan_out;
static int s_scan_max, s_scan_n;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void notify(void)
{
    if (s_changed != NULL) {
        s_changed();
    }
}

static void set_state(netmgr_state_t state, bool ap_on)
{
    lock();
    s_status.state = state;
    s_status.ap_on = ap_on;
    if (state != NETMGR_STATION) {
        s_status.ssid[0] = '\0';
        s_status.ip[0] = '\0';
        s_status.rssi = 0;
    }
    if (!ap_on) {
        s_status.ap_clients = 0;
    }
    unlock();
    ESP_LOGI(TAG, "%s%s", state == NETMGR_OFF       ? "off"
                          : state == NETMGR_JOINING ? "joining"
                          : state == NETMGR_STATION ? "on a network"
                                                    : "AP only",
             ap_on && state != NETMGR_AP ? ", AP too" : "");
    notify();
}

static void set_test(netmgr_test_t test, const char *ssid)
{
    lock();
    s_status.test = test;
    snprintf(s_status.test_ssid, sizeof(s_status.test_ssid), "%s", ssid);
    unlock();
    notify();
}

static esp_err_t save_list(void)
{
    stored_list_t stored = { .version = LIST_VERSION };
    lock();
    stored.list = s_list;
    unlock();
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, "nets", &stored, sizeof(stored));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    memset(&stored, 0, sizeof(stored)); /* the passwords don't linger on the stack */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "saving the networks: %s", esp_err_to_name(err));
    }
    return err;
}

static void load_list(void)
{
    static stored_list_t stored;
    size_t size = sizeof(stored);
    nvs_handle_t nvs;
    memset(&s_list, 0, sizeof(s_list));
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) {
        return; /* nothing saved yet */
    }
    esp_err_t err = nvs_get_blob(nvs, "nets", &stored, &size);
    nvs_close(nvs);
    if (err != ESP_OK || size != sizeof(stored) || stored.version != LIST_VERSION ||
        stored.list.count > NETMGR_LIST_MAX) {
        return;
    }
    for (int i = stored.list.count - 1; i >= 0; i--) { /* each goes first: add the last one first */
        netmgr_net_t *n = &stored.list.nets[i];
        n->ssid[NETMGR_SSID_MAX] = '\0';
        n->pass[NETMGR_PASS_MAX] = '\0';
        if (netmgr_list_add(&s_list, n->ssid, n->pass)) { /* keeps only what still reads as valid */
            memcpy(s_list.nets[0].bssid, n->bssid, 6);
            s_list.nets[0].channel = n->channel;
        }
    }
    memset(&stored, 0, sizeof(stored));
}

/* The AP password: 10 characters without look-alikes, made at the first boot and kept in NVS. */
static void load_ap_password(void)
{
    static const char k_alphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
    nvs_handle_t nvs;
    size_t size = sizeof(s_status.ap_pass);
    if (nvs_open("sys", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    if (nvs_get_str(nvs, "ap_pass", s_status.ap_pass, &size) != ESP_OK || strlen(s_status.ap_pass) != 10) {
        for (int i = 0; i < 10; i++) {
            s_status.ap_pass[i] = k_alphabet[esp_random() % (sizeof(k_alphabet) - 1)];
        }
        s_status.ap_pass[10] = '\0';
        if (nvs_set_str(nvs, "ap_pass", s_status.ap_pass) == ESP_OK) {
            nvs_commit(nvs);
        }
    }
    nvs_close(nvs);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
        xEventGroupSetBits(s_events, EV_FAILED);
        if (!s_joining && s_status.state == NETMGR_STATION) { /* the network went away: keep trying it */
            ESP_LOGW(TAG, "lost \"%s\" (reason %u); trying again", s_status.ssid, s_reason);
            lock();
            s_status.ip[0] = '\0';
            unlock();
            esp_wifi_connect();
            notify();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        lock();
        esp_ip4addr_ntoa(&e->ip_info.ip, s_status.ip, sizeof(s_status.ip));
        unlock();
        xEventGroupSetBits(s_events, EV_GOT_IP);
        if (!s_joining) {
            notify(); /* back on the network after losing it */
        }
    } else if (base == WIFI_EVENT && (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED)) {
        wifi_sta_list_t list;
        if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) {
            lock();
            s_status.ap_clients = (uint8_t)list.num;
            unlock();
        }
        notify();
    }
}

/* Captive DNS (spec §10.1): every name is the AP, while the AP runs. */
static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53) };
    inet_aton(NETMGR_AP_IP, &addr.sin_addr);
    struct timeval timeout = { .tv_sec = 1 };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ESP_LOGE(TAG, "captive DNS: no socket");
    } else {
        static const uint8_t k_ip[4] = { 192, 168, 4, 1 };
        uint8_t query[512], reply[528];
        while (s_dns_run) {
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            int n = recvfrom(sock, query, sizeof(query), 0, (struct sockaddr *)&from, &from_len);
            if (n <= 0) {
                continue; /* the timeout: look at s_dns_run again */
            }
            size_t m = netmgr_dns_reply(query, (size_t)n, k_ip, reply, sizeof(reply));
            if (m > 0) {
                sendto(sock, reply, m, 0, (struct sockaddr *)&from, from_len);
            }
        }
    }
    if (sock >= 0) {
        close(sock);
    }
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static void mdns_up(void)
{
    if (s_mdns_up || mdns_init() != ESP_OK) {
        return;
    }
    mdns_hostname_set(s_status.host);
    mdns_instance_name_set("reflbo");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    s_mdns_up = true;
}

static void ap_services(bool on)
{
    if (on && s_dns_task == NULL) {
        static const char k_uri[] = "http://" NETMGR_AP_IP "/"; /* DHCP option 114, RFC 8910 */
        esp_netif_dhcps_stop(s_ap);
        esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, (void *)k_uri, strlen(k_uri));
        esp_netif_dhcps_start(s_ap);
        s_dns_run = true;
        xTaskCreatePinnedToCore(dns_task, "dns", DNS_STACK, NULL, DNS_PRIORITY, &s_dns_task, tskNO_AFFINITY);
    } else if (!on && s_dns_task != NULL) {
        s_dns_run = false;
        for (int i = 0; i < 30 && s_dns_task != NULL; i++) { /* its receive times out within 1 s */
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* Wi-Fi on in `mode`; the AP, when it runs, on `channel`. */
static esp_err_t wifi_up(wifi_mode_t mode, uint8_t channel)
{
    if (!s_wifi_up) {
        s_sta = esp_netif_create_default_wifi_sta();
        s_ap = esp_netif_create_default_wifi_ap();
        esp_netif_set_hostname(s_sta, s_status.host);
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            return err;
        }
        esp_wifi_set_storage(WIFI_STORAGE_RAM); /* the networks live in our own NVS list */
        s_wifi_up = true;
    }
    esp_err_t err = esp_wifi_set_mode(mode);
    if (err == ESP_OK && (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)) {
        wifi_config_t ap = { .ap = { .channel = channel, .max_connection = AP_CLIENTS_MAX,
                                     .authmode = WIFI_AUTH_WPA2_PSK } };
        strcpy((char *)ap.ap.ssid, s_status.ap_ssid);
        ap.ap.ssid_len = (uint8_t)strlen(s_status.ap_ssid);
        strcpy((char *)ap.ap.password, s_status.ap_pass);
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start(); /* already started: ESP_OK */
    }
    return err;
}

static void wifi_down(void)
{
    ap_services(false);
    if (s_mdns_up) {
        mdns_free();
        s_mdns_up = false;
    }
    if (s_wifi_up) {
        esp_wifi_stop();
        esp_wifi_deinit();
        esp_netif_destroy_default_wifi(s_sta);
        esp_netif_destroy_default_wifi(s_ap);
        s_sta = s_ap = NULL;
        s_wifi_up = false;
    }
}

/* A scan into s_seen. With the AP up the driver comes back to its channel between the others, so
 * its clients stay on. */
static void scan(void)
{
    static wifi_ap_record_t records[SEEN_MAX];
    uint16_t n = SEEN_MAX;
    s_seen_n = 0;
    if (esp_wifi_scan_start(NULL, true) != ESP_OK || esp_wifi_scan_get_ap_records(&n, records) != ESP_OK) {
        return;
    }
    for (int i = 0; i < n; i++) {
        netmgr_seen_t *s = &s_seen[s_seen_n++];
        snprintf(s->ssid, sizeof(s->ssid), "%s", (const char *)records[i].ssid);
        memcpy(s->bssid, records[i].bssid, 6);
        s->channel = records[i].primary;
        s->rssi = records[i].rssi;
        s->open = records[i].authmode == WIFI_AUTH_OPEN;
    }
}

static netmgr_test_t reason_result(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return NETMGR_TEST_NOT_FOUND;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return NETMGR_TEST_WRONG_PASSWORD;
    default:
        return NETMGR_TEST_FAILED;
    }
}

/* One attempt to join; `bssid` may be NULL, which scans for the network first. */
static netmgr_test_t join(const char *ssid, const char *pass, const uint8_t *bssid, uint8_t channel)
{
    static wifi_config_t c;
    memset(&c, 0, sizeof(c));
    strcpy((char *)c.sta.ssid, ssid);
    strcpy((char *)c.sta.password, pass);
    c.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    c.sta.pmf_cfg.capable = true;
    if (bssid != NULL) {
        c.sta.bssid_set = true;
        memcpy(c.sta.bssid, bssid, 6);
        c.sta.channel = channel;
    }
    s_joining = true;
    esp_wifi_disconnect();
    xEventGroupClearBits(s_events, EV_GOT_IP | EV_FAILED);
    s_reason = 0;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &c);
    memset(&c, 0, sizeof(c)); /* the driver keeps its own copy */
    if (err != ESP_OK || esp_wifi_connect() != ESP_OK) {
        s_joining = false;
        return NETMGR_TEST_FAILED;
    }
    EventBits_t bits = xEventGroupWaitBits(s_events, EV_GOT_IP | EV_FAILED, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(JOIN_TIMEOUT_MS));
    netmgr_test_t r = bits & EV_GOT_IP   ? NETMGR_TEST_OK
                      : bits & EV_FAILED ? reason_result(s_reason)
                                         : NETMGR_TEST_FAILED;
    if (r != NETMGR_TEST_OK) {
        esp_wifi_disconnect();
    }
    s_joining = false;
    return r;
}

/* Joined network `index` of the list: remember where, and show it. */
static void joined(int index, bool ap_on)
{
    wifi_ap_record_t info;
    uint8_t bssid[6] = { 0 }, channel = 0;
    int8_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        memcpy(bssid, info.bssid, 6);
        channel = info.primary;
        rssi = info.rssi;
    }
    lock();
    strcpy(s_status.ssid, s_list.nets[index].ssid);
    s_status.rssi = rssi;
    netmgr_list_succeeded(&s_list, index, bssid, channel);
    unlock();
    save_list();
    mdns_up();
    ESP_LOGI(TAG, "joined \"%s\" as %s", s_status.ssid, s_status.ip);
    set_state(NETMGR_STATION, ap_on);
}

/* Tries the saved networks in order, each first where it was last found; true once one joined. */
static bool join_saved(bool ap_on)
{
    static netmgr_list_t list;
    lock();
    list = s_list;
    unlock();
    bool ok = false;
    for (int i = 0; i < list.count && !ok; i++) {
        const netmgr_net_t *n = &list.nets[i];
        bool cached = n->channel != 0;
        netmgr_test_t r = join(n->ssid, n->pass, cached ? n->bssid : NULL, n->channel);
        if (r != NETMGR_TEST_OK && cached) {
            r = join(n->ssid, n->pass, NULL, 0); /* the router may have moved */
        }
        if (r == NETMGR_TEST_OK) {
            lock();
            int index = netmgr_list_find(&s_list, n->ssid);
            unlock();
            joined(index, ap_on);
            ok = true;
        } else {
            ESP_LOGI(TAG, "\"%s\": %s", n->ssid, netmgr_test_name(r));
        }
    }
    memset(&list, 0, sizeof(list));
    return ok;
}

/* No network: the AP and its captive portal (spec §10.2), on the channel of the network the owner
 * will likely pick. The station stays on for scans and tests. */
static void start_ap(void)
{
    lock();
    netmgr_list_t saved = s_list;
    unlock();
    if (wifi_up(WIFI_MODE_STA, 1) == ESP_OK) {
        scan();
    }
    uint8_t channel = netmgr_ap_channel(s_seen, s_seen_n, &saved);
    memset(&saved, 0, sizeof(saved));
    if (wifi_up(WIFI_MODE_APSTA, channel) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi didn't start");
        set_state(NETMGR_OFF, false);
        return;
    }
    ap_services(true);
    mdns_up();
    ESP_LOGI(TAG, "AP on channel %u", channel);
    set_state(NETMGR_AP, true);
}

static void do_start(bool keep_ap)
{
    lock();
    int saved = s_list.count;
    unlock();
    if (saved > 0) {
        set_state(NETMGR_JOINING, keep_ap);
        if (wifi_up(keep_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA, 1) != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi didn't start");
            set_state(NETMGR_OFF, false);
            return;
        }
        ap_services(keep_ap);
        if (join_saved(keep_ap)) {
            return;
        }
    }
    start_ap();
}

static void do_scan(void)
{
    s_scan_n = 0;
    wifi_mode_t mode;
    if (s_wifi_up && esp_wifi_get_mode(&mode) == ESP_OK && (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)) {
        scan();
        s_scan_n = netmgr_seen_unique(s_seen, s_seen_n, s_scan_out, s_scan_max);
    }
}

static netmgr_test_t do_test(const char *ssid, const char *pass)
{
    netmgr_status_t before;
    netmgr_status(&before);
    wifi_mode_t mode;
    if (!s_wifi_up || esp_wifi_get_mode(&mode) != ESP_OK) {
        return NETMGR_TEST_FAILED;
    }
    if (mode == WIFI_MODE_STA) { /* spec §10.2: AP and station together while testing */
        uint8_t channel = 1;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&channel, &second);
        if (wifi_up(WIFI_MODE_APSTA, channel) != ESP_OK) {
            return NETMGR_TEST_FAILED;
        }
        ap_services(true);
    } /* a running AP is left alone: setting its config again would drop the phone on it */
    scan();
    int best = netmgr_seen_best(s_seen, s_seen_n, ssid);
    netmgr_test_t r = NETMGR_TEST_NOT_FOUND; /* out of reach: the AP never leaves its channel */
    if (best >= 0) {
        r = join(ssid, pass, s_seen[best].bssid, s_seen[best].channel);
    }
    if (r == NETMGR_TEST_OK) {
        lock();
        netmgr_list_add(&s_list, ssid, pass);
        unlock();
        joined(0, true);
        return r;
    }
    ESP_LOGI(TAG, "test \"%s\": %s", ssid, netmgr_test_name(r));
    if (before.state == NETMGR_STATION && !join_saved(true)) { /* back where it was, if it can */
        set_state(NETMGR_AP, true);
    }
    return r;
}

static void netmgr_task(void *arg)
{
    (void)arg;
    for (;;) {
        static cmd_t cmd;
        xQueueReceive(s_cmds, &cmd, portMAX_DELAY);
        switch (cmd.kind) {
        case CMD_START:
            do_start(cmd.keep_ap);
            break;
        case CMD_STOP:
            wifi_down();
            set_test(NETMGR_TEST_NONE, "");
            set_state(NETMGR_OFF, false);
            break;
        case CMD_SCAN:
            do_scan();
            xSemaphoreGive(s_done);
            break;
        case CMD_TEST:
            set_test(do_test(cmd.ssid, cmd.pass), cmd.ssid);
            break;
        }
        memset(&cmd, 0, sizeof(cmd)); /* a test's password doesn't linger */
    }
}

esp_err_t netmgr_init(void (*changed)(void))
{
    if (s_cmds != NULL) {
        return ESP_ERR_INVALID_STATE; /* once per boot */
    }
    s_changed = changed;
    s_lock = xSemaphoreCreateMutex();
    s_done = xSemaphoreCreateBinary();
    s_scan_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    QueueHandle_t cmds = xQueueCreate(4, sizeof(cmd_t));
    if (s_lock == NULL || s_done == NULL || s_scan_lock == NULL || s_events == NULL || cmds == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_cmds = cmds;
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_status.host, sizeof(s_status.host), "reflbo-%02x%02x", mac[4], mac[5]);
    snprintf(s_status.ap_ssid, sizeof(s_status.ap_ssid), "%s", s_status.host);
    load_ap_password();
    load_list();
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
        err = err == ESP_ERR_INVALID_STATE ? ESP_OK : err; /* someone made it already */
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    }
    if (err == ESP_OK && xTaskCreatePinnedToCore(netmgr_task, "netmgr", TASK_STACK, NULL, TASK_PRIORITY, NULL,
                                                 tskNO_AFFINITY) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "%d saved network(s), AP %s", s_list.count, s_status.ap_ssid);
    return err;
}

static void post(const cmd_t *cmd)
{
    xQueueSend(s_cmds, cmd, portMAX_DELAY);
}

void netmgr_start(bool keep_ap)
{
    post(&(cmd_t){ .kind = CMD_START, .keep_ap = keep_ap });
}

void netmgr_stop(void)
{
    post(&(cmd_t){ .kind = CMD_STOP });
}

void netmgr_status(netmgr_status_t *out)
{
    lock();
    *out = s_status;
    unlock();
}

int netmgr_scan(netmgr_ap_t *out, int max)
{
    xSemaphoreTake(s_scan_lock, portMAX_DELAY);
    s_scan_out = out;
    s_scan_max = max;
    post(&(cmd_t){ .kind = CMD_SCAN });
    xSemaphoreTake(s_done, portMAX_DELAY);
    int n = s_scan_n;
    xSemaphoreGive(s_scan_lock);
    return n;
}

esp_err_t netmgr_test_start(const char *ssid, const char *pass)
{
    if (!netmgr_net_valid(ssid, pass)) {
        return ESP_ERR_INVALID_ARG;
    }
    lock();
    bool busy = s_status.test == NETMGR_TEST_RUNNING || s_status.state == NETMGR_OFF;
    if (!busy) {
        s_status.test = NETMGR_TEST_RUNNING;
        snprintf(s_status.test_ssid, sizeof(s_status.test_ssid), "%s", ssid);
    }
    unlock();
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    static cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CMD_TEST;
    snprintf(cmd.ssid, sizeof(cmd.ssid), "%s", ssid);
    snprintf(cmd.pass, sizeof(cmd.pass), "%s", pass);
    post(&cmd);
    memset(&cmd, 0, sizeof(cmd));
    return ESP_OK;
}

void netmgr_networks(netmgr_list_t *out)
{
    lock();
    *out = s_list;
    unlock();
    for (int i = 0; i < out->count; i++) {
        memset(out->nets[i].pass, 0, sizeof(out->nets[i].pass));
    }
}

esp_err_t netmgr_add(const char *ssid, const char *pass)
{
    lock();
    bool ok = netmgr_list_add(&s_list, ssid, pass);
    unlock();
    return ok ? save_list() : ESP_ERR_INVALID_ARG;
}

esp_err_t netmgr_forget(const char *ssid)
{
    lock();
    bool found = netmgr_list_forget(&s_list, ssid);
    unlock();
    return found ? save_list() : ESP_ERR_NOT_FOUND;
}

esp_err_t netmgr_forget_all(void)
{
    lock();
    memset(&s_list, 0, sizeof(s_list));
    unlock();
    return save_list();
}

const char *netmgr_test_name(netmgr_test_t result)
{
    static const char *const k_names[] = { "none",      "testing", "connected", "wrong password",
                                           "not found", "failed",  "invalid" };
    return (unsigned)result < sizeof(k_names) / sizeof(k_names[0]) ? k_names[result] : "?";
}
```

```yaml
dependencies:
  espressif/mdns: "==1.13.1"
```

```cmake
# Wi-Fi manager (spec §10.1, §10.2): saved networks, the AP with captive DNS, mDNS. netmgr_dns.c and
# netmgr_list.c are pure C and also built on the host.
idf_component_register(SRCS "netmgr.c" "netmgr_dns.c" "netmgr_list.c" "netmgr_scan.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_event esp_netif esp_wifi lwip nvs_flash)
```

- [ ] **Step 2: Fetch mDNS and build.**

Run: `tools/idf.sh reconfigure && tools/idf.sh build`
Expected: the component manager adds `espressif/mdns` 1.13.1, and the firmware builds without warnings. `dependencies.lock` changes like this:

```diff
diff --git a/dependencies.lock b/dependencies.lock
index 0276b91..6d47bf7 100644
--- a/dependencies.lock
+++ b/dependencies.lock
@@ -1,4 +1,14 @@
 dependencies:
+  espressif/mdns:
+    component_hash: b679eafd0acae2066e2645bd91d073e33ca5be515e2545cd3c4e55a3e3bce3cb
+    dependencies:
+    - name: idf
+      require: private
+      version: '>=5.0'
+    source:
+      registry_url: https://components.espressif.com/
+      type: service
+    version: 1.13.1
   idf:
     source:
       type: idf
@@ -14,7 +24,8 @@ dependencies:
       type: service
     version: 1.22.3
 direct_dependencies:
+- espressif/mdns
 - joltwallet/littlefs
-manifest_hash: 61796c6672e97e9119e91d1c5cf60e32829e7c33c649146b829edfe1e6d7813b
+manifest_hash: 49d456f25750964a11ffbe0e69e619b0a7b9bf9822bdcd213483793219c3b031
 target: esp32s3
 version: 2.0.0
```

- [ ] **Step 3: Commit.** Nothing calls the manager yet: Task 10 does, and checks it on the board.

```bash
git add components/netmgr dependencies.lock
git commit -m "feat(netmgr): add the Wi-Fi manager: saved networks, the AP with captive DNS, mDNS, tests"
```

---

### Task 9: The web server and the pages (`webui`, `web/`, `tools/gen_zones.py`)

**Files:**
- Create: `components/webui/include/webui.h`, `components/webui/webui.c`, `web/index.html`, `web/style.css`, `web/app.js`, `web/zones.js` (generated), `tools/gen_zones.py`, `tools/tests/test_gen_zones.py`
- Modify: `components/webui/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 7's password, sessions and helpers; Task 8's manager.
- Produces (Task 10):
  - `webui_reply_t` (`status`, `type`, `len`), `webui_api_fn` (method, path, query, body, the reply buffer and the reply), `webui_event_t` (`DONE`, `REBOOT`, `UPDATED`, `FACTORY_RESET`) and `webui_config_t` (`run`, `api`, `event`); `WEBUI_BODY_MAX` 16 KB and `WEBUI_REPLY_MAX` 20 KB.
  - `esp_err_t webui_start(const webui_config_t *)`, `void webui_stop(void)` (stops the server from a helper task, since a request may be waiting on the app task that calls it), `bool webui_running(void)`, `int64_t webui_last_request_ms(void)` (the last request for the device's own pages or API, gotcha 27), `bool webui_password_set(void)` and `esp_err_t webui_reset_password(void)`.
  - The pages: Status, Wi-Fi, Location & time, Device, Presets, Firmware and Backup, with the first visit and the login (spec §10.3, D19).
  - `tools/gen_zones.py` writes `web/zones.js`, `const ZONES = {"<IANA zone>": "<POSIX rule>", …}`.

- [ ] **Step 1: Write the failing tools test.** `gen_zones.py` reads each zone's POSIX rule from the footer of its TZif file (RFC 8536 §3.3).

```python
import pathlib
import tempfile
import unittest

import gen_zones

# A TZif version 2 file as far as gen_zones reads it: the magic, some binary data, and the POSIX
# footer between the last two newlines (RFC 8536 §3.3).
PRAGUE = b"TZif2" + b"\x00" * 40 + b"\nCET-1CEST,M3.5.0,M10.5.0/3\n"
DUBAI = b"TZif2" + b"\x00" * 40 + b"\n<+04>-4\n"


class PosixTest(unittest.TestCase):
    def write(self, data):
        f = tempfile.NamedTemporaryFile(delete=False)
        f.write(data)
        f.close()
        self.addCleanup(pathlib.Path(f.name).unlink)
        return pathlib.Path(f.name)

    def test_the_footer_is_the_posix_rule(self):
        self.assertEqual(gen_zones.posix_of(self.write(PRAGUE)), "CET-1CEST,M3.5.0,M10.5.0/3")
        self.assertEqual(gen_zones.posix_of(self.write(DUBAI)), "<+04>-4")

    def test_a_file_without_a_footer_has_no_rule(self):
        self.assertIsNone(gen_zones.posix_of(self.write(b"TZif" + b"\x00" * 40)))  # version 1
        self.assertIsNone(gen_zones.posix_of(self.write(b"not a tzfile\n")))
        self.assertIsNone(gen_zones.posix_of(self.write(b"TZif2" + b"\x00" * 40 + b"\n\n")))  # empty footer


class ZonesTest(unittest.TestCase):
    def test_the_table_holds_utc_and_the_zones_of_zone_tab(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            (root / "Europe").mkdir()
            (root / "Asia").mkdir()
            (root / "Europe" / "Prague").write_bytes(PRAGUE)
            (root / "Asia" / "Dubai").write_bytes(DUBAI)
            (root / "UTC").write_bytes(b"TZif2" + b"\x00" * 40 + b"\nUTC0\n")
            (root / "zone.tab").write_text("# comment\nCZ\t+5005+01426\tEurope/Prague\n"
                                           "AE\t+2518+05518\tAsia/Dubai\nXX\t+0000+00000\tNowhere/Missing\n")
            (root / "+VERSION").write_text("2026c\n")
            self.assertEqual(gen_zones.zones(root), {"Asia/Dubai": "<+04>-4", "Europe/Prague":
                                                     "CET-1CEST,M3.5.0,M10.5.0/3", "UTC": "UTC0"})
            out = root / "zones.js"
            self.assertEqual(gen_zones.main(["--zoneinfo", d, "--out", str(out)]), 0)
            text = out.read_text()
            self.assertIn("from tzdata 2026c", text)
            self.assertIn('const ZONES = {"Asia/Dubai":"<+04>-4",', text)


if __name__ == "__main__":
    unittest.main()
```

Run: `cd tools && python3 -m unittest tests.test_gen_zones; cd ..`
Expected: FAIL: `No module named 'gen_zones'`.

- [ ] **Step 2: Write the generator and the zone table.**

```python
#!/usr/bin/env python3
"""Writes web/zones.js: every IANA time zone of the tz database with its POSIX TZ string, for the
web UI's zone picker and "set time from phone" (spec §7, §10.3). The POSIX string is the last line
of each zone's TZif file (RFC 8536 §3.3); the tz database is in the public domain.

  python3 tools/gen_zones.py [--zoneinfo /usr/share/zoneinfo] [--out web/zones.js]
"""
import argparse
import json
import pathlib
import sys


def posix_of(path):
    """The footer of a TZif version 2+ file: the text between its last two newlines. None for a
    missing file, another format, a version 1 file (no footer) or an empty footer."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if not data.startswith(b"TZif") or not data.endswith(b"\n"):
        return None
    start = data.rfind(b"\n", 0, len(data) - 1)
    try:
        footer = data[start + 1:-1].decode("ascii") if start >= 0 else ""
    except UnicodeDecodeError:
        return None
    return footer or None


def zones(zoneinfo):
    names = ["UTC"]
    for line in (zoneinfo / "zone.tab").read_text().splitlines():
        if line and not line.startswith("#"):
            names.append(line.split("\t")[2])
    table = {}
    for name in sorted(set(names)):
        posix = posix_of(zoneinfo / name)
        if posix:
            table[name] = posix
    return table


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--zoneinfo", default="/usr/share/zoneinfo")
    parser.add_argument("--out", default="web/zones.js")
    args = parser.parse_args(argv)
    zoneinfo = pathlib.Path(args.zoneinfo)
    version = (zoneinfo / "+VERSION").read_text().strip() if (zoneinfo / "+VERSION").exists() else "unknown"
    table = zones(zoneinfo)
    text = (f"// Generated by tools/gen_zones.py from tzdata {version}; do not edit.\n"
            f"// IANA zone -> POSIX TZ string (spec §7). The tz database is in the public domain.\n"
            f"const ZONES = {json.dumps(table, separators=(',', ':'), sort_keys=True)};\n")
    pathlib.Path(args.out).write_text(text)
    print(f"{args.out}: {len(table)} zones from tzdata {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Run: `(cd tools && python3 -m unittest tests.test_gen_zones) && python3 tools/gen_zones.py && shasum -a 256 web/zones.js`
Expected: 3 tests pass; `web/zones.js: 419 zones from tzdata 2026c`; `7be069b5484069af3b33554c2604eaf19f908cd1cbb3d401e827c6a8a17cec8c  web/zones.js`. Another tzdata release changes the count and the hash; that is fine.

- [ ] **Step 3: The server.** It serves the gzipped pages, answers the password routes (open without a session) and, with a session, the Wi-Fi and OTA routes and `done`, `reboot` and `factory-reset` (the reply goes out before the event). Every other `/api` request runs on the app task through `run`. A mutating request other than an upload must be `application/json` (spec §10.4). A request for another host goes to the portal when it comes from the AP (spec §10.1).

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * The web configurator (spec §10.3, §10.4): an HTTP server on port 80 while config mode runs.
 *
 * - webui serves the embedded pages, redirects captive-portal probes, keeps the password and
 *   the sessions, streams OTA uploads, and answers the Wi-Fi requests through netmgr.
 * - Every other /api request goes to the app's `api` function, which runs on the app task
 *   (through `run`), so the app's state keeps one owner (spec §3.2).
 */

typedef struct {
    int status;       /* HTTP status: 200, 400, 404, 409, ... */
    const char *type; /* "application/json" or "image/bmp" */
    size_t len;       /* bytes in the out buffer */
} webui_reply_t;

/* One API request, answered into `out`. `body` is NUL-terminated, "" when there is none. */
typedef void (*webui_api_fn)(const char *method, const char *path, const char *query, const char *body,
                             uint8_t *out, size_t out_size, webui_reply_t *reply);

typedef enum {
    WEBUI_EVENT_DONE,    /* "Done" in the web UI: leave config mode */
    WEBUI_EVENT_REBOOT,  /* the Status page asked */
    WEBUI_EVENT_UPDATED, /* a firmware upload finished: restart into it */
    WEBUI_EVENT_FACTORY_RESET,
} webui_event_t;

typedef struct {
    /* Runs fn(arg) on the app task and waits for it (the app's executor). */
    esp_err_t (*run)(void (*fn)(void *arg), void *arg);
    webui_api_fn api;
    /* Called on the server's task; the app must not stop the server from inside it. */
    void (*event)(webui_event_t event);
} webui_config_t;

#define WEBUI_BODY_MAX  (16 * 1024) /* the largest request: a backup to restore */
#define WEBUI_REPLY_MAX (20 * 1024) /* the largest reply: a BMP (15 662 bytes) or a backup */

esp_err_t webui_start(const webui_config_t *config);
void webui_stop(void);
bool webui_running(void);
/* When the last request came in (esp_timer ms); config mode ends 10 min after it (spec §10.2). */
int64_t webui_last_request_ms(void);
bool webui_password_set(void);
/* Menu ▸ Wi-Fi ▸ Reset web password (D18): the next visit, over the AP, chooses a new one. */
esp_err_t webui_reset_password(void);
```

```c
#include "webui.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "netmgr.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "webui_auth.h"
#include "webui_http.h"

static const char *TAG = "webui";

#define SERVER_STACK    8192
#define SERVER_PRIORITY 4 /* below the app task (5): API calls wait for it anyway */
#define STOPPER_STACK   3072
#define OTA_CHUNK       4096
#define COOKIE_ATTRS    "; Path=/; HttpOnly; SameSite=Strict"

/* The pages, gzipped when the firmware is built (CMakeLists.txt). */
#define WEB_ASSET(name)                                              \
    extern const uint8_t name##_start[] asm("_binary_" #name "_start"); \
    extern const uint8_t name##_end[] asm("_binary_" #name "_end")
WEB_ASSET(index_html_gz);
WEB_ASSET(app_js_gz);
WEB_ASSET(style_css_gz);
WEB_ASSET(zones_js_gz);

typedef struct {
    const char *uri;
    const uint8_t *start, *end;
    const char *type;
} asset_t;

static const asset_t k_assets[] = {
    { "/", index_html_gz_start, index_html_gz_end, "text/html; charset=utf-8" },
    { "/app.js", app_js_gz_start, app_js_gz_end, "text/javascript; charset=utf-8" },
    { "/style.css", style_css_gz_start, style_css_gz_end, "text/css; charset=utf-8" },
    { "/zones.js", zones_js_gz_start, zones_js_gz_end, "text/javascript; charset=utf-8" },
};

static httpd_handle_t s_server;
static volatile bool s_stopping;
static webui_config_t s_cfg;
static char *s_body;   /* PSRAM, one request at a time: the server has one task */
static uint8_t *s_out; /* PSRAM */
static SemaphoreHandle_t s_auth_lock; /* the sessions and the stored record; the menu may reset them */
static webui_sessions_t s_sessions;
static volatile int64_t s_last_ms;

static int64_t now_s(void)
{
    return esp_timer_get_time() / 1000000;
}

static void touch(void)
{
    s_last_ms = esp_timer_get_time() / 1000;
}

/* The password's record in NVS `secrets` (D18), "" when none is set. */
static bool record_get(char *out, size_t size)
{
    nvs_handle_t nvs;
    out[0] = '\0';
    if (nvs_open("secrets", NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_str(nvs, "web_pass", out, &size);
    nvs_close(nvs);
    if (err != ESP_OK) {
        out[0] = '\0';
    }
    return out[0] != '\0';
}

static esp_err_t record_put(const char *record)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("secrets", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = record[0] ? nvs_set_str(nvs, "web_pass", record) : nvs_erase_key(nvs, "web_pass");
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK; /* nothing to erase */
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

bool webui_password_set(void)
{
    char record[WEBUI_AUTH_RECORD_LEN];
    return record_get(record, sizeof(record));
}

esp_err_t webui_reset_password(void)
{
    if (s_auth_lock == NULL) {
        s_auth_lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_auth_lock, portMAX_DELAY);
    webui_sessions_clear(&s_sessions);
    esp_err_t err = record_put("");
    xSemaphoreGive(s_auth_lock);
    ESP_LOGI(TAG, "web password reset");
    return err;
}

/* The client is on the device's own AP: 192.168.4.0/24, maybe as an IPv4-mapped IPv6 address. */
static bool peer_on_ap(httpd_req_t *req)
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&addr, &len) != 0) {
        return false;
    }
    uint32_t ip = 0;
    if (addr.ss_family == AF_INET) {
        ip = ntohl(((struct sockaddr_in *)&addr)->sin_addr.s_addr);
    } else if (addr.ss_family == AF_INET6) {
        const uint8_t *b = ((struct sockaddr_in6 *)&addr)->sin6_addr.s6_addr;
        static const uint8_t k_mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
        if (memcmp(b, k_mapped, sizeof(k_mapped)) != 0) {
            return false;
        }
        ip = (uint32_t)b[12] << 24 | (uint32_t)b[13] << 16 | (uint32_t)b[14] << 8 | b[15];
    }
    return (ip & 0xFFFFFF00u) == 0xC0A80400u;
}

/* The request names this device; a captive-portal probe names someone else's host. */
static bool for_us(httpd_req_t *req)
{
    char host[64];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        return true; /* HTTP/1.0 */
    }
    netmgr_status_t st;
    netmgr_status(&st);
    char local[sizeof(st.host) + 8];
    snprintf(local, sizeof(local), "%s.local", st.host);
    return webui_host_is(host, NETMGR_AP_IP) || webui_host_is(host, st.host) || webui_host_is(host, local) ||
           (st.ip[0] != '\0' && webui_host_is(host, st.ip));
}

static esp_err_t to_portal(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" NETMGR_AP_IP "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "reflbo setup"); /* iOS wants a body to show the portal */
}

static const char *status_line(int status)
{
    switch (status) {
    case 200: return "200 OK";
    case 202: return "202 Accepted";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 413: return "413 Content Too Large";
    case 415: return "415 Unsupported Media Type";
    case 429: return "429 Too Many Requests";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
    }
}

static esp_err_t send_json(httpd_req_t *req, int status, const char *json)
{
    httpd_resp_set_status(req, status_line(status));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_error(httpd_req_t *req, int status, const char *message)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", message);
    char *text = cJSON_PrintUnformatted(o);
    esp_err_t err = send_json(req, status, text ? text : "{}");
    cJSON_free(text);
    cJSON_Delete(o);
    return err;
}

static esp_err_t send_cjson(httpd_req_t *req, int status, cJSON *o)
{
    char *text = cJSON_PrintUnformatted(o);
    esp_err_t err = send_json(req, status, text ? text : "{}");
    cJSON_free(text);
    cJSON_Delete(o);
    return err;
}

/* The whole body into s_body, NUL-terminated. */
static bool read_body(httpd_req_t *req)
{
    if (req->content_len > WEBUI_BODY_MAX) {
        return false;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, s_body + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        got += (size_t)n;
    }
    s_body[got] = '\0';
    return true;
}

static bool session_ok(httpd_req_t *req)
{
    char cookie[160], token[WEBUI_TOKEN_LEN + 1];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK ||
        !webui_cookie_token(cookie, token)) {
        return false;
    }
    xSemaphoreTake(s_auth_lock, portMAX_DELAY);
    bool ok = webui_session_check(&s_sessions, token, now_s());
    xSemaphoreGive(s_auth_lock);
    return ok;
}

static void set_session_cookie(httpd_req_t *req)
{
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    static char header[8 + WEBUI_TOKEN_LEN + sizeof(COOKIE_ATTRS)];
    xSemaphoreTake(s_auth_lock, portMAX_DELAY);
    snprintf(header, sizeof(header), "session=%s" COOKIE_ATTRS, webui_session_new(&s_sessions, random, now_s()));
    xSemaphoreGive(s_auth_lock);
    httpd_resp_set_hdr(req, "Set-Cookie", header); /* the header is sent before this returns */
}

static const char *json_string(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static esp_err_t auth_routes(httpd_req_t *req, const char *path)
{
    char record[WEBUI_AUTH_RECORD_LEN];
    bool set = record_get(record, sizeof(record));
    if (strcmp(path, "/api/auth") == 0 && req->method == HTTP_GET) {
        netmgr_status_t st;
        netmgr_status(&st);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "password_set", set);
        cJSON_AddBoolToObject(o, "logged_in", set && session_ok(req));
        cJSON_AddBoolToObject(o, "on_ap", peer_on_ap(req));
        cJSON_AddStringToObject(o, "host", st.host);
        cJSON_AddStringToObject(o, "ap_ssid", st.ap_ssid);
        cJSON_AddStringToObject(o, "version", esp_app_get_description()->version);
        return send_cjson(req, 200, o);
    }
    if (req->method != HTTP_POST) {
        return send_error(req, 405, "use POST");
    }
    if (!read_body(req)) {
        return send_error(req, 413, "request too large");
    }
    cJSON *in = cJSON_Parse(s_body);
    const char *password = json_string(in, "password");
    esp_err_t err;
    if (strcmp(path, "/api/auth/setup") == 0) {
        if (set) {
            err = send_error(req, 409, "a password is set already");
        } else if (!peer_on_ap(req)) {
            err = send_error(req, 403, "join the device's own Wi-Fi to choose the password");
        } else if (!webui_password_valid(password)) {
            err = send_error(req, 400, "the password needs 8-64 characters");
        } else {
            uint8_t salt[WEBUI_AUTH_SALT_LEN];
            esp_fill_random(salt, sizeof(salt));
            if (!webui_auth_record(password, salt, WEBUI_AUTH_ITERATIONS, record, sizeof(record)) ||
                record_put(record) != ESP_OK) {
                err = send_error(req, 500, "the password couldn't be saved");
            } else {
                ESP_LOGI(TAG, "web password set");
                set_session_cookie(req);
                err = send_json(req, 200, "{\"ok\":true}");
            }
        }
    } else if (strcmp(path, "/api/auth/login") == 0) {
        xSemaphoreTake(s_auth_lock, portMAX_DELAY);
        bool allowed = webui_login_allowed(&s_sessions, now_s());
        xSemaphoreGive(s_auth_lock);
        if (!set) {
            err = send_error(req, 409, "choose a password first");
        } else if (!allowed) {
            err = send_error(req, 429, "too many tries; wait a minute");
        } else {
            bool ok = password != NULL && webui_auth_check(record, password);
            xSemaphoreTake(s_auth_lock, portMAX_DELAY);
            webui_login_result(&s_sessions, ok, now_s());
            xSemaphoreGive(s_auth_lock);
            if (ok) {
                set_session_cookie(req);
                err = send_json(req, 200, "{\"ok\":true}");
            } else {
                ESP_LOGW(TAG, "a failed login");
                err = send_error(req, 401, "wrong password");
            }
        }
    } else if (strcmp(path, "/api/auth/logout") == 0) {
        char cookie[160], token[WEBUI_TOKEN_LEN + 1];
        if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) == ESP_OK &&
            webui_cookie_token(cookie, token)) {
            xSemaphoreTake(s_auth_lock, portMAX_DELAY);
            for (int i = 0; i < WEBUI_SESSIONS; i++) {
                if (strcmp(s_sessions.s[i].token, token) == 0) {
                    s_sessions.s[i].token[0] = '\0';
                }
            }
            xSemaphoreGive(s_auth_lock);
        }
        httpd_resp_set_hdr(req, "Set-Cookie", "session=; Max-Age=0" COOKIE_ATTRS);
        err = send_json(req, 200, "{\"ok\":true}");
    } else if (strcmp(path, "/api/auth/password") == 0) {
        const char *old = json_string(in, "old");
        if (!set || !session_ok(req)) {
            err = send_error(req, 401, "log in first");
        } else if (old == NULL || !webui_auth_check(record, old)) {
            err = send_error(req, 403, "the current password is wrong");
        } else if (!webui_password_valid(password)) {
            err = send_error(req, 400, "the password needs 8-64 characters");
        } else {
            uint8_t salt[WEBUI_AUTH_SALT_LEN];
            esp_fill_random(salt, sizeof(salt));
            err = webui_auth_record(password, salt, WEBUI_AUTH_ITERATIONS, record, sizeof(record)) &&
                          record_put(record) == ESP_OK
                      ? send_json(req, 200, "{\"ok\":true}")
                      : send_error(req, 500, "the password couldn't be saved");
        }
    } else {
        err = send_error(req, 404, "no such API");
    }
    memset(s_body, 0, req->content_len); /* passwords don't linger */
    cJSON_Delete(in);
    return err;
}

static esp_err_t wifi_routes(httpd_req_t *req, const char *path, const char *query)
{
    if (strcmp(path, "/api/wifi/scan") == 0 && req->method == HTTP_GET) {
        static netmgr_ap_t aps[NETMGR_SCAN_MAX];
        int n = netmgr_scan(aps, NETMGR_SCAN_MAX);
        cJSON *o = cJSON_CreateObject(), *list = cJSON_AddArrayToObject(o, "networks");
        for (int i = 0; i < n; i++) {
            cJSON *ap = cJSON_CreateObject();
            cJSON_AddStringToObject(ap, "ssid", aps[i].ssid);
            cJSON_AddNumberToObject(ap, "rssi", aps[i].rssi);
            cJSON_AddBoolToObject(ap, "open", aps[i].open);
            cJSON_AddItemToArray(list, ap);
        }
        return send_cjson(req, 200, o);
    }
    if (strcmp(path, "/api/wifi/networks") != 0) {
        return send_error(req, 404, "no such API");
    }
    if (req->method == HTTP_GET) {
        netmgr_list_t saved;
        netmgr_networks(&saved);
        netmgr_status_t st;
        netmgr_status(&st);
        cJSON *o = cJSON_CreateObject(), *list = cJSON_AddArrayToObject(o, "saved");
        for (int i = 0; i < saved.count; i++) {
            cJSON_AddItemToArray(list, cJSON_CreateString(saved.nets[i].ssid));
        }
        cJSON_AddStringToObject(o, "state", st.state == NETMGR_STATION ? "station"
                                            : st.state == NETMGR_JOINING ? "joining"
                                            : st.state == NETMGR_AP      ? "ap"
                                                                         : "off");
        cJSON_AddStringToObject(o, "ssid", st.ssid);
        cJSON_AddStringToObject(o, "ip", st.ip);
        cJSON_AddNumberToObject(o, "rssi", st.rssi);
        cJSON_AddBoolToObject(o, "ap_on", st.ap_on);
        cJSON *test = cJSON_AddObjectToObject(o, "test");
        cJSON_AddStringToObject(test, "ssid", st.test_ssid);
        cJSON_AddStringToObject(test, "result", netmgr_test_name(st.test));
        return send_cjson(req, 200, o);
    }
    if (req->method == HTTP_POST) {
        if (!read_body(req)) {
            return send_error(req, 413, "request too large");
        }
        cJSON *in = cJSON_Parse(s_body);
        const char *ssid = json_string(in, "ssid"), *pass = json_string(in, "password");
        const cJSON *test = cJSON_GetObjectItemCaseSensitive(in, "test");
        esp_err_t err;
        if (!netmgr_net_valid(ssid, pass ? pass : "")) {
            err = send_error(req, 400, "a name of 1-32 bytes and a password of 8-63 characters (or none)");
        } else if (cJSON_IsFalse(test)) {
            err = netmgr_add(ssid, pass ? pass : "") == ESP_OK ? send_json(req, 200, "{\"result\":\"saved\"}")
                                                                 : send_error(req, 500, "couldn't save it");
        } else if (netmgr_test_start(ssid, pass ? pass : "") != ESP_OK) {
            err = send_error(req, 409, "a test is running already");
        } else { /* spec §10.2; GET /api/wifi/networks reports the result, as the phone may drop off meanwhile */
            err = send_json(req, 202, "{\"result\":\"testing\"}");
        }
        memset(s_body, 0, req->content_len);
        cJSON_Delete(in);
        return err;
    }
    if (req->method == HTTP_DELETE) {
        char raw[3 * NETMGR_SSID_MAX + 1], ssid[NETMGR_SSID_MAX + 1];
        if (query == NULL || httpd_query_key_value(query, "ssid", raw, sizeof(raw)) != ESP_OK ||
            webui_url_decode(raw, ssid, sizeof(ssid)) < 0) {
            return send_error(req, 400, "which network? ?ssid=...");
        }
        return netmgr_forget(ssid) == ESP_OK ? send_json(req, 200, "{\"ok\":true}")
                                             : send_error(req, 404, "not a saved network");
    }
    return send_error(req, 405, "not for this method");
}

/* A firmware image (spec §10.5), checked for this project and chip before it is written through. */
static esp_err_t ota_upload(httpd_req_t *req)
{
    if (req->method != HTTP_POST) {
        return send_error(req, 405, "use POST");
    }
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next == NULL) {
        return send_error(req, 500, "no update partition");
    }
    const size_t head = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    if (req->content_len < head || req->content_len > next->size) {
        return send_error(req, 413, "not a firmware image of a fitting size");
    }
    size_t got = 0, filled = 0;
    while (filled < head) { /* the image header and the app description first */
        int n = httpd_req_recv(req, (char *)s_out + filled, OTA_CHUNK - filled);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            return send_error(req, 400, "the upload broke off");
        }
        filled += (size_t)n;
    }
    const esp_image_header_t *image = (const esp_image_header_t *)s_out;
    const esp_app_desc_t *desc = (const esp_app_desc_t *)(s_out + sizeof(esp_image_header_t) +
                                                          sizeof(esp_image_segment_header_t));
    const esp_app_desc_t *self = esp_app_get_description();
    if (image->magic != ESP_IMAGE_HEADER_MAGIC || image->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID ||
        desc->magic_word != ESP_APP_DESC_MAGIC_WORD || strncmp(desc->project_name, self->project_name,
                                                               sizeof(desc->project_name)) != 0 ||
        desc->version[0] == '\0') {
        return send_error(req, 400, "not a reflbo firmware for this chip");
    }
    char version[sizeof(desc->version) + 1];
    snprintf(version, sizeof(version), "%.*s", (int)sizeof(desc->version), desc->version);
    ESP_LOGI(TAG, "firmware upload: %s -> %s, %u bytes into %s", self->version, version,
             (unsigned)req->content_len, next->label);
    esp_ota_handle_t ota;
    if (esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        return send_error(req, 500, "the update partition won't open");
    }
    esp_err_t err = ESP_OK;
    while (err == ESP_OK) {
        err = esp_ota_write(ota, s_out, filled);
        got += filled;
        filled = 0;
        if (got >= req->content_len) {
            break;
        }
        int n = httpd_req_recv(req, (char *)s_out, OTA_CHUNK < req->content_len - got ? OTA_CHUNK
                                                                                      : req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
        filled = (size_t)n;
        touch(); /* a long upload is activity too */
    }
    if (err != ESP_OK) {
        esp_ota_abort(ota);
        return send_error(req, 400, "the upload broke off");
    }
    err = esp_ota_end(ota); /* checks the image's own checksum and hash */
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(next);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "firmware rejected: %s", esp_err_to_name(err));
        return send_error(req, 400, "the image is damaged");
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddStringToObject(o, "version", version);
    esp_err_t sent = send_cjson(req, 200, o);
    s_cfg.event(WEBUI_EVENT_UPDATED); /* after the reply: the app restarts in a moment */
    return sent;
}

static esp_err_t ota_status(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition(), *bad = esp_ota_get_last_invalid_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(running, &state);
    const esp_app_desc_t *self = esp_app_get_description();
    char elf[17];
    esp_app_get_elf_sha256(elf, sizeof(elf));
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "version", self->version);
    cJSON_AddStringToObject(o, "elf", elf);
    cJSON_AddStringToObject(o, "built", self->date);
    cJSON_AddStringToObject(o, "partition", running->label);
    cJSON_AddBoolToObject(o, "pending", state == ESP_OTA_IMG_PENDING_VERIFY);
    cJSON_AddStringToObject(o, "rolled_back_from", bad != NULL ? bad->label : "");
    return send_cjson(req, 200, o);
}

typedef struct {
    const char *method, *path, *query, *body;
    webui_reply_t reply;
} api_call_t;

static void api_on_app(void *arg)
{
    api_call_t *c = arg;
    s_cfg.api(c->method, c->path, c->query, c->body, s_out, WEBUI_REPLY_MAX, &c->reply);
}

static const char *method_name(int method)
{
    switch (method) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    default: return "?";
    }
}

static esp_err_t api_handler(httpd_req_t *req)
{
    touch();
    char path[64], query[160];
    size_t len = strcspn(req->uri, "?");
    snprintf(path, sizeof(path), "%.*s", (int)(len < sizeof(path) - 1 ? len : sizeof(path) - 1), req->uri);
    bool has_query = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK;
    bool mutating = req->method != HTTP_GET;
    if (mutating && strcmp(path, "/api/ota") != 0) { /* spec §10.4: no cross-site form posts */
        char type[64];
        if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
            !webui_is_json_type(type)) {
            return send_error(req, 415, "send application/json");
        }
    }
    if (strncmp(path, "/api/auth", 9) == 0) {
        return auth_routes(req, path);
    }
    if (!session_ok(req)) {
        return send_error(req, 401, webui_password_set() ? "log in first" : "choose a password first");
    }
    if (strcmp(path, "/api/ota") == 0) {
        return ota_upload(req);
    }
    if (strcmp(path, "/api/ota/status") == 0) {
        return ota_status(req);
    }
    if (strncmp(path, "/api/wifi/", 10) == 0) {
        return wifi_routes(req, path, has_query ? query : NULL);
    }
    static const struct {
        const char *path;
        webui_event_t event;
    } k_events[] = { { "/api/done", WEBUI_EVENT_DONE },
                     { "/api/reboot", WEBUI_EVENT_REBOOT },
                     { "/api/factory-reset", WEBUI_EVENT_FACTORY_RESET } };
    for (size_t i = 0; i < sizeof(k_events) / sizeof(k_events[0]); i++) {
        if (strcmp(path, k_events[i].path) == 0) {
            if (req->method != HTTP_POST) {
                return send_error(req, 405, "use POST");
            }
            esp_err_t sent = send_json(req, 200, "{\"ok\":true}");
            s_cfg.event(k_events[i].event); /* after the reply */
            return sent;
        }
    }
    if (mutating && !read_body(req)) {
        return send_error(req, 413, "request too large");
    }
    api_call_t call = { .method = method_name(req->method), .path = path, .query = has_query ? query : "",
                        .body = mutating ? s_body : "", .reply = { .status = 404, .type = "application/json" } };
    if (s_cfg.run(api_on_app, &call) != ESP_OK) {
        return send_error(req, 503, "the device is busy");
    }
    httpd_resp_set_status(req, status_line(call.reply.status));
    httpd_resp_set_type(req, call.reply.type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)s_out, (ssize_t)call.reply.len);
}

static esp_err_t asset_handler(httpd_req_t *req)
{
    if (!for_us(req)) {
        return peer_on_ap(req) ? to_portal(req) : httpd_resp_send_404(req);
    }
    touch(); /* our own pages count as use; a phone's connectivity probes don't */
    const asset_t *a = req->user_ctx;
    httpd_resp_set_type(req, a->type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)a->start, a->end - a->start);
}

/* Anything else: an unknown page of ours goes to the start page, and on the AP a probe for
 * another host goes to the portal (spec §10.1). */
static esp_err_t fallback_handler(httpd_req_t *req)
{
    if (for_us(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        return httpd_resp_send(req, NULL, 0);
    }
    return peer_on_ap(req) ? to_portal(req) : httpd_resp_send_404(req);
}

esp_err_t webui_start(const webui_config_t *config)
{
    if (s_server != NULL || s_stopping) {
        return s_stopping ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    s_cfg = *config;
    if (s_auth_lock == NULL) {
        s_auth_lock = xSemaphoreCreateMutex();
    }
    s_body = heap_caps_malloc(WEBUI_BODY_MAX + 1, MALLOC_CAP_SPIRAM);
    s_out = heap_caps_malloc(WEBUI_REPLY_MAX, MALLOC_CAP_SPIRAM);
    if (s_auth_lock == NULL || s_body == NULL || s_out == NULL) {
        free(s_body);
        free(s_out);
        s_body = NULL;
        s_out = NULL;
        return ESP_ERR_NO_MEM;
    }
    webui_sessions_clear(&s_sessions); /* sessions end with config mode */
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.uri_match_fn = httpd_uri_match_wildcard;
    c.max_uri_handlers = 8;
    c.max_open_sockets = 7; /* with its 3 internal ones, within CONFIG_LWIP_MAX_SOCKETS (16) */
    c.stack_size = SERVER_STACK;
    c.task_priority = SERVER_PRIORITY;
    c.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &c);
    if (err != ESP_OK) {
        free(s_body);
        free(s_out);
        s_body = NULL;
        s_out = NULL;
        return err;
    }
    for (size_t i = 0; i < sizeof(k_assets) / sizeof(k_assets[0]); i++) {
        httpd_uri_t uri = { .uri = k_assets[i].uri, .method = HTTP_GET, .handler = asset_handler,
                            .user_ctx = (void *)&k_assets[i] };
        httpd_register_uri_handler(s_server, &uri);
    }
    httpd_uri_t api = { .uri = "/api/*", .method = HTTP_ANY, .handler = api_handler };
    httpd_register_uri_handler(s_server, &api);
    httpd_uri_t fallback = { .uri = "/*", .method = HTTP_GET, .handler = fallback_handler };
    httpd_register_uri_handler(s_server, &fallback);
    touch();
    ESP_LOGI(TAG, "web configurator up");
    return ESP_OK;
}

static void stopper(void *arg)
{
    httpd_stop((httpd_handle_t)arg);
    webui_sessions_clear(&s_sessions);
    free(s_body);
    free(s_out);
    s_body = NULL;
    s_out = NULL;
    s_stopping = false;
    ESP_LOGI(TAG, "web configurator down");
    vTaskDelete(NULL);
}

void webui_stop(void)
{
    if (s_server == NULL || s_stopping) {
        return;
    }
    /* Not on the calling task: a request may be waiting for the app task, which is likely the
     * caller, and httpd_stop() waits for that request. */
    s_stopping = true;
    httpd_handle_t server = s_server;
    s_server = NULL;
    if (xTaskCreatePinnedToCore(stopper, "webui_stop", STOPPER_STACK, server, SERVER_PRIORITY, NULL,
                                tskNO_AFFINITY) != pdPASS) {
        httpd_stop(server); /* no memory for a task: stop here and hope nothing waits */
        s_stopping = false;
    }
}

bool webui_running(void)
{
    return s_server != NULL;
}

int64_t webui_last_request_ms(void)
{
    return s_last_ms;
}
```

```cmake
# The web configurator (spec §10.3, §10.4). webui_auth.c and webui_http.c are pure C and also built
# on the host. The pages in web/ are gzipped at build time and embedded in the app image, so an OTA
# update brings them along (spec §5.3).
idf_component_register(SRCS "webui.c" "webui_auth.c" "webui_http.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_update bootloader_support esp_app_format esp_http_server esp_timer json
                                     lwip netmgr nvs_flash util)

set(web_dir "${CMAKE_CURRENT_LIST_DIR}/../../web")
foreach(page index.html app.js style.css zones.js)
    set(gz "${CMAKE_CURRENT_BINARY_DIR}/${page}.gz")
    # -n leaves out the name and time, so the same page gives the same image
    add_custom_command(OUTPUT "${gz}"
                       COMMAND gzip -9 -n -c "${web_dir}/${page}" > "${gz}"
                       DEPENDS "${web_dir}/${page}"
                       COMMENT "Compressing web/${page}")
    target_add_binary_data(${COMPONENT_LIB} "${gz}" BINARY)
endforeach()
```

- [ ] **Step 4: The pages.** Plain HTML, CSS and JavaScript: one script renders every page from the API, and `busy()` shows each error beside its form. Only a session that ended sends the page back to the login.

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>reflbo</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<header class="top">
  <a class="brand" href="#status">reflbo <span id="device"></span></a>
  <button id="done" class="btn" type="button" hidden>Done</button>
</header>
<nav id="nav" hidden>
  <a href="#status">Status</a>
  <a href="#wifi">Wi-Fi</a>
  <a href="#place">Location &amp; time</a>
  <a href="#device">Device</a>
  <a href="#presets">Presets</a>
  <a href="#firmware">Firmware</a>
  <a href="#backup">Backup</a>
</nav>
<main id="main"><p class="muted">Loading…</p></main>
<div id="toast" role="status" hidden></div>
<script src="/zones.js"></script>
<script src="/app.js"></script>
</body>
</html>
```

```css
/* reflbo web configurator (spec §10.3): mobile first, no build step. */
:root {
  --bg: #f4f4f1;
  --card: #fff;
  --ink: #1b1b1b;
  --muted: #6b6b66;
  --line: #deded8;
  --accent: #1f5f8b;
  --accent-ink: #fff;
  --bad: #a3261b;
  --good: #2c6e36;
  --radius: 10px;
  color-scheme: light dark;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #161616;
    --card: #222220;
    --ink: #ecece8;
    --muted: #a3a39c;
    --line: #3a3a36;
    --accent: #7db3dc;
    --accent-ink: #10202c;
    --bad: #f08a80;
    --good: #8fd19a;
  }
}
* { box-sizing: border-box; }
[hidden] { display: none !important; }
html { -webkit-text-size-adjust: 100%; }
body {
  margin: 0;
  background: var(--bg);
  color: var(--ink);
  font: 16px/1.45 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
.top {
  position: sticky; top: 0; z-index: 2;
  display: flex; align-items: center; justify-content: space-between; gap: 12px;
  padding: 10px 16px; background: var(--ink); color: var(--bg);
}
.brand { color: inherit; text-decoration: none; font-weight: 700; letter-spacing: .02em; }
.brand span { font-weight: 400; opacity: .75; margin-left: 6px; }
.top .btn { background: var(--bg); color: var(--ink); border-color: var(--bg); }
nav {
  display: flex; gap: 4px; overflow-x: auto; padding: 8px 12px;
  background: var(--card); border-bottom: 1px solid var(--line); scrollbar-width: none;
}
nav a {
  flex: none; padding: 6px 12px; border-radius: 999px;
  color: var(--ink); text-decoration: none; white-space: nowrap;
}
nav a.on { background: var(--ink); color: var(--bg); }
main { max-width: 760px; margin: 0 auto; padding: 16px 16px 64px; }
h1 { font-size: 1.35rem; margin: 4px 0 14px; }
h2 { font-size: 1.05rem; margin: 0 0 10px; }
.card {
  background: var(--card); border: 1px solid var(--line); border-radius: var(--radius);
  padding: 14px 16px; margin: 0 0 14px;
}
.muted { color: var(--muted); }
.small { font-size: .88rem; }
.bad { color: var(--bad); }
.good { color: var(--good); }
dl.facts { display: grid; grid-template-columns: max-content 1fr; gap: 4px 16px; margin: 0; }
dl.facts dt { color: var(--muted); }
dl.facts dd { margin: 0; overflow-wrap: anywhere; }
label { display: block; margin: 10px 0 4px; font-weight: 600; }
label.check { display: flex; align-items: center; gap: 8px; font-weight: 400; margin: 8px 0; }
input[type=text], input[type=password], input[type=number], input[type=search], input[type=time], select {
  width: 100%; padding: 10px 12px; font: inherit; color: var(--ink);
  background: var(--bg); border: 1px solid var(--line); border-radius: 8px;
}
input[type=checkbox], input[type=radio] { width: 20px; height: 20px; accent-color: var(--accent); }
.row { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; }
.row > * { flex: 1 1 140px; }
.actions { display: flex; gap: 10px; flex-wrap: wrap; margin-top: 14px; }
.btn {
  appearance: none; font: inherit; font-weight: 600; cursor: pointer;
  padding: 9px 16px; border-radius: 8px; border: 1px solid var(--line);
  background: var(--card); color: var(--ink);
}
.btn.primary { background: var(--accent); border-color: var(--accent); color: var(--accent-ink); }
.btn.danger { color: var(--bad); border-color: var(--bad); }
.btn:disabled { opacity: .5; cursor: default; }
.list { list-style: none; margin: 0; padding: 0; }
.list li {
  display: flex; align-items: center; gap: 10px; padding: 9px 0;
  border-top: 1px solid var(--line);
}
.list li:first-child { border-top: 0; }
.list .grow { flex: 1; min-width: 0; overflow-wrap: anywhere; }
.list button.link { all: unset; cursor: pointer; flex: 1; min-width: 0; overflow-wrap: anywhere; }
.list li.sel { background: var(--bg); margin: 0 -8px; padding-left: 8px; padding-right: 8px; border-radius: 6px; }
a { color: var(--accent); }
.bars { font-family: ui-monospace, monospace; letter-spacing: -2px; color: var(--muted); }
.screen {
  display: block; width: 100%; max-width: 400px; aspect-ratio: 4 / 3;
  image-rendering: pixelated; border: 1px solid var(--line); border-radius: 4px; background: #fff;
}
.slots { display: grid; grid-template-columns: 1fr; gap: 8px; }
.slot label { margin: 0 0 4px; font-weight: 400; color: var(--muted); }
.days { display: flex; gap: 2px; flex-wrap: wrap; }
.days label { display: flex; flex-direction: column; align-items: center; margin: 0; font-weight: 400; font-size: .8rem; }
.entry { border-top: 1px solid var(--line); padding: 10px 0; }
.entry:first-child { border-top: 0; }
progress { width: 100%; height: 14px; }
#toast {
  position: fixed; left: 50%; bottom: 20px; transform: translateX(-50%); z-index: 3;
  max-width: calc(100% - 32px); padding: 10px 16px; border-radius: 8px;
  background: var(--ink); color: var(--bg); box-shadow: 0 4px 16px rgba(0,0,0,.25);
}
@media (min-width: 640px) {
  .slots { grid-template-columns: 1fr 1fr; }
}
```

```js
'use strict';
/* reflbo web configurator (spec §10.3, D18): plain JS, no build step, English only (spec §5.8).
 * Every page reads the device's REST API; nothing is kept in the browser. */

const main = document.getElementById('main');
let auth = null; /* GET /api/auth */

/* ---- helpers ---- */

function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k === 'value') el.value = v;
    else if (k === 'checked' || k === 'selected' || k === 'disabled') el[k] = true;
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat(Infinity)) {
    if (kid !== null && kid !== undefined && kid !== false) el.append(kid instanceof Node ? kid : String(kid));
  }
  return el;
}

const card = (title, ...kids) => h('section', { class: 'card' }, title ? h('h2', { text: title }) : null, ...kids);
const facts = (pairs) => h('dl', { class: 'facts' }, pairs.filter(Boolean).map(([k, v]) => [h('dt', { text: k }), h('dd', {}, v)]));
const actions = (...buttons) => h('div', { class: 'actions' }, ...buttons);
const button = (text, onclick, cls) => h('button', { class: 'btn' + (cls ? ' ' + cls : ''), type: 'button', onclick }, text);
const field = (label, input, hint) => [h('label', {}, label), input, hint ? h('p', { class: 'muted small' }, hint) : null];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const bytes = (s) => new TextEncoder().encode(s).length;
/* The device's messages are lowercase phrases: "wrong password" reads "Wrong password." */
const sentence = (m) => (m ? m[0].toUpperCase() + m.slice(1) + (/[.!?…]$/.test(m) ? '' : '.') : m);

class ApiError extends Error {
  constructor(message, status, relogin) {
    super(message);
    this.status = status;
    this.relogin = !!relogin; /* the session ended: start() shows the login page instead */
  }
}

async function api(method, path, body) {
  const init = { method, headers: {} };
  if (body instanceof Blob) {
    init.body = body;
    init.headers['Content-Type'] = 'application/octet-stream';
  } else if (body !== undefined) {
    init.body = JSON.stringify(body);
    init.headers['Content-Type'] = 'application/json'; /* spec §10.4: blocks cross-site form posts */
  }
  let r;
  try {
    r = await fetch(path, init);
  } catch (e) {
    throw new ApiError("The device doesn't answer. Is this phone still on its network?", 0);
  }
  const json = (r.headers.get('Content-Type') || '').includes('application/json');
  const data = json ? await r.json() : await r.blob();
  if (r.status === 401 && !path.startsWith('/api/auth')) {
    start();
    throw new ApiError(sentence(data.error), 401, true);
  }
  if (!r.ok) throw new ApiError(sentence((json && data.error) || r.statusText), r.status);
  return data;
}

let toastTimer;
function toast(text) {
  const t = document.getElementById('toast');
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (t.hidden = true), 3000);
}

/* Runs `fn` with the element's buttons disabled; an error goes into `note`. */
async function busy(el, note, fn) {
  const buttons = [...el.querySelectorAll('button')];
  buttons.forEach((b) => (b.disabled = true));
  if (note) note.textContent = '';
  try {
    await fn();
  } catch (e) {
    if (note && !e.relogin) {
      note.className = 'bad';
      note.textContent = e.message;
    }
  } finally {
    buttons.forEach((b) => (b.disabled = false));
  }
}

function duration(s) {
  const d = Math.floor(s / 86400), hr = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d ? `${d} d ${hr} h` : hr ? `${hr} h ${m} min` : `${m} min`;
}

function phoneZone() {
  const tz = Intl.DateTimeFormat().resolvedOptions().timeZone;
  return tz && ZONES[tz] ? tz : null;
}

/* POST /api/time: the phone's clock, and its zone if asked. */
async function setClock(withZone) {
  const body = { epoch: Math.round(Date.now() / 1000) };
  const tz = withZone ? phoneZone() : null;
  if (tz) Object.assign(body, { tz_iana: tz, tz_posix: ZONES[tz] });
  await api('POST', '/api/time', body);
}

function download(blob, name) {
  const a = h('a', { href: URL.createObjectURL(blob), download: name });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

/* ---- start, password and login (D18) ---- */

async function start() {
  try {
    auth = await api('GET', '/api/auth');
  } catch (e) {
    main.replaceChildren(card('No answer', h('p', { text: e.message }), actions(button('Try again', start))));
    return;
  }
  document.getElementById('device').textContent = auth.host;
  const inside = auth.password_set && auth.logged_in;
  document.getElementById('nav').hidden = !inside;
  document.getElementById('done').hidden = !inside;
  if (!inside) return auth.password_set ? loginPage() : setupPage();
  window.onhashchange = route;
  route();
}

function passwordForm(title, text, submit, withOld) {
  const old = h('input', { type: 'password', autocomplete: 'current-password', required: true });
  const p1 = h('input', { type: 'password', autocomplete: 'new-password', minlength: 8, maxlength: 64, required: true });
  const p2 = h('input', { type: 'password', autocomplete: 'new-password', required: true });
  const note = h('p');
  const form = h('form', {
    onsubmit: (ev) => {
      ev.preventDefault();
      if (p1.value !== p2.value) {
        note.className = 'bad';
        note.textContent = 'The two passwords differ.';
        return;
      }
      busy(form, note, () => submit(p1.value, old.value, note, form));
    },
  },
  text ? h('p', { text }) : null,
  withOld ? field('Current password', old) : null,
  field('New password', p1, '8 to 64 characters.'),
  field('Repeat it', p2),
  note,
  actions(h('button', { class: 'btn primary', type: 'submit' }, title)));
  return form;
}

function setupPage() {
  if (!auth.on_ap) {
    main.replaceChildren(h('h1', { text: 'Welcome' }), card('Choose a password first',
      h('p', {}, 'Only a phone on the device\'s own Wi-Fi can choose the password for this page. Join ',
        h('b', { text: auth.ap_ssid }), ' (its password is on the device\'s screen), then open ',
        h('b', { text: 'http://192.168.4.1' }), '.')));
    return;
  }
  main.replaceChildren(h('h1', { text: 'Welcome' }), card('Choose a password for this page',
    passwordForm('Set password', 'Anyone who joins the device\'s Wi-Fi could open this page; the password keeps the ' +
      'settings to you. If you forget it: Menu ▸ Wi-Fi ▸ Reset web password on the device.',
      async (password) => {
        await api('POST', '/api/auth/setup', { password });
        toast('Password set');
        start();
      })));
}

function loginPage() {
  const pass = h('input', { type: 'password', autocomplete: 'current-password', required: true });
  const note = h('p');
  const form = h('form', {
    onsubmit: (ev) => {
      ev.preventDefault();
      busy(form, note, async () => {
        await api('POST', '/api/auth/login', { password: pass.value });
        start();
      });
    },
  }, field('Password', pass), note, actions(h('button', { class: 'btn primary', type: 'submit' }, 'Log in')));
  main.replaceChildren(h('h1', { text: 'Log in' }), card(null, form,
    h('p', { class: 'muted small', text: 'Forgot it? On the device: Menu ▸ Wi-Fi ▸ Reset web password.' })));
  pass.focus();
}

document.getElementById('done').onclick = async () => {
  try {
    await api('POST', '/api/done');
  } catch (e) {
    toast(e.message);
    return;
  }
  document.getElementById('nav').hidden = true;
  document.getElementById('done').hidden = true;
  main.replaceChildren(card('Wi-Fi is off', h('p', { text: 'You can close this page. To come back, hold BOOT on the ' +
    'device for 3 seconds.' })));
};

/* ---- pages ---- */

const pages = { status: statusPage, wifi: wifiPage, place: placePage, device: devicePage, presets: presetsPage,
                firmware: firmwarePage, backup: backupPage };

function route() {
  const name = location.hash.slice(1) || 'status';
  const page = pages[name] || statusPage;
  for (const a of document.querySelectorAll('nav a')) a.classList.toggle('on', a.getAttribute('href') === '#' + name);
  main.replaceChildren(h('p', { class: 'muted', text: 'Loading…' }));
  window.scrollTo(0, 0);
  page().catch((e) => {
    if (!e.relogin) main.replaceChildren(card('Something went wrong', h('p', { class: 'bad', text: e.message }),
      actions(button('Try again', route))));
  });
}

function wifiText(w) {
  switch (w.state) {
  case 'station': return `On ${w.ssid}${w.ip ? ' as ' + w.ip : ''}${w.ap_on ? `, and its own network ${w.ap_ssid}` : ''}`;
  case 'joining': return 'Connecting to a saved network…';
  case 'ap': return `Own network ${w.ap_ssid} only`;
  default: return 'Off';
  }
}

async function statusPage() {
  let s = await api('GET', '/api/status');
  let clockNote = null;
  if (!s.time.valid) { /* no backup cell (D9): the phone's clock is better than none */
    await setClock(false);
    s = await api('GET', '/api/status');
    clockNote = h('p', { class: 'good small', text: 'The device had lost the time; it now has this phone\'s.' });
  }
  const off = Math.round(s.time.epoch - Date.now() / 1000);
  const b = s.battery, env = s.sensors;
  const shot = h('img', { class: 'screen', alt: 'What the device shows', src: '/api/screenshot.bmp?t=' + Date.now() });
  main.replaceChildren(
    h('h1', { text: 'Status' }),
    card('Screen', shot, actions(button('Refresh', () => statusPage().catch(() => {})))),
    card('Device', facts([
      ['Name', `${s.device.id}.local`],
      ['Firmware', s.device.firmware],
      ['Running for', duration(s.device.uptime_s)],
      ['Free memory', `${(s.device.free_heap / 1048576).toFixed(1)} MB`],
      ['Preset', `${s.preset.name}${s.preset.cycle ? ', cycling' : ''}`],
    ])),
    card('Time', facts([
      ['On the device', s.time.local.replace('T', ' ')],
      ['Time zone', s.time.tz_iana],
      ['Clock', Math.abs(off) <= 2 ? 'matches this phone' : `${Math.abs(off)} s ${off > 0 ? 'ahead of' : 'behind'} this phone`],
    ]), clockNote, actions(button('Set the clock from this phone', async (ev) => {
      await busy(ev.target.parentNode, null, async () => {
        await setClock(false);
        toast('Clock set');
        await statusPage();
      });
    }))),
    card('Battery and sensors', facts([
      ['Battery', b.percent !== undefined ? `${b.percent} % · ${(b.mv / 1000).toFixed(2)} V · ${b.state}` : 'no reading yet'],
      b.days_left !== null ? ['Lasts', `about ${b.days_left.toFixed(1)} days`] : null,
      ['Temperature', env.temp_c !== null ? `${env.temp_c.toFixed(1)} °C` : '—'],
      ['Humidity', env.hum_pct !== null ? `${env.hum_pct.toFixed(0)} %` : '—'],
      env.age_s !== undefined ? ['Measured', `${duration(env.age_s)} ago`] : null,
    ])),
    card('Wi-Fi', facts([['Now', wifiText(s.wifi)], s.wifi.ap_on ? ['On its network', `${s.wifi.ap_clients} device(s)`] : null])),
    card('This page', passwordForm('Change password', null, async (password, old, note, form) => {
      await api('POST', '/api/auth/password', { old, password });
      form.reset();
      note.className = 'good';
      note.textContent = 'Password changed.';
    }, true), actions(button('Log out', async () => {
      await api('POST', '/api/auth/logout');
      start();
    }), button('Restart the device', async () => {
      if (!confirm('Restart the device? Wi-Fi comes back on by itself.')) return;
      await api('POST', '/api/reboot');
      waitForRestart('Restarting…');
    }))),
  );
}

/* ---- Wi-Fi (spec §10.1, §10.2) ---- */

function bars(rssi) {
  const n = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  return '▮'.repeat(n) + '▯'.repeat(4 - n);
}

const TEST_TEXT = {
  'wrong password': 'The password is wrong.',
  'not found': "The device can't see this network. It uses 2.4 GHz only: check the name, and that the router is in reach.",
  failed: "It couldn't connect.",
  invalid: 'The name or password is not valid.',
};

/* The test runs on the device while this phone may drop off its network for a moment (spec §10.2). */
async function tryNetwork(ssid, password, note) {
  await api('POST', '/api/wifi/networks', { ssid, password });
  note.className = 'muted';
  note.textContent = `Trying ${ssid}…`;
  for (const until = Date.now() + 60000; Date.now() < until;) {
    await sleep(1000);
    let n;
    try {
      n = await api('GET', '/api/wifi/networks');
    } catch (e) {
      if (e.relogin) throw e;
      note.textContent = `Trying ${ssid}… This phone lost the device's network for a moment; waiting for it.`;
      continue;
    }
    if (n.test.ssid !== ssid || n.test.result === 'testing') continue;
    if (n.test.result === 'connected') return n;
    throw new ApiError(TEST_TEXT[n.test.result] || n.test.result, 409);
  }
  throw new ApiError('The device gave no answer in time.', 0);
}

async function wifiPage() {
  const n = await api('GET', '/api/wifi/networks');
  const ssid = h('input', { type: 'text', maxlength: 32, autocapitalize: 'off', autocomplete: 'off', required: true });
  const pass = h('input', { type: 'password', maxlength: 64, autocomplete: 'off' });
  const note = h('p');
  const found = h('ul', { class: 'list' });
  const scanNote = h('p', { class: 'muted small' });
  const scan = () => busy(found.parentNode, scanNote, async () => {
    scanNote.textContent = 'Looking…';
    const r = await api('GET', '/api/wifi/scan');
    scanNote.textContent = r.networks.length ? 'Tap one to use its name.' : 'No networks in sight.';
    found.replaceChildren(...r.networks.map((ap) => h('li', {},
      h('button', { class: 'link', type: 'button', onclick: () => { ssid.value = ap.ssid; pass.focus(); } }, ap.ssid),
      h('span', { class: 'muted small', text: ap.open ? 'open' : '' }),
      h('span', { class: 'bars', title: `${ap.rssi} dBm`, text: bars(ap.rssi) }))));
  });
  const valid = () => {
    if (bytes(ssid.value) < 1 || bytes(ssid.value) > 32) return 'The name needs 1 to 32 characters.';
    if (pass.value && (pass.value.length < 8 || pass.value.length > 63) && !/^[0-9a-fA-F]{64}$/.test(pass.value)) {
      return 'A Wi-Fi password has 8 to 63 characters.';
    }
    return null;
  };
  const form = h('form', {
    onsubmit: (ev) => {
      ev.preventDefault();
      const bad = valid();
      if (bad) {
        note.className = 'bad';
        note.textContent = bad;
        return;
      }
      busy(form, note, async () => {
        const r = await tryNetwork(ssid.value, pass.value, note);
        note.className = 'good';
        note.textContent = `Connected: the device is on ${r.ssid} as ${r.ip}. On that network, open ` +
          `http://${auth.host}.local or http://${r.ip}.`;
        pass.value = '';
        refreshLists(await api('GET', '/api/wifi/networks'));
      });
    },
  },
  field('Network name', ssid), field('Password', pass, 'Leave it empty for an open network.'), note,
  actions(h('button', { class: 'btn primary', type: 'submit' }, 'Connect'),
    button('Save without trying', () => {
      const bad = valid();
      if (bad) {
        note.className = 'bad';
        note.textContent = bad;
        return;
      }
      busy(form, note, async () => {
        await api('POST', '/api/wifi/networks', { ssid: ssid.value, password: pass.value, test: false });
        note.className = 'good';
        note.textContent = `Saved: the device tries ${ssid.value} the next time Wi-Fi comes on.`;
        pass.value = '';
        refreshLists(await api('GET', '/api/wifi/networks'));
      });
    })));
  const savedRows = (list) => list.saved.length ? list.saved.map((name) => h('li', {},
    h('span', { class: 'grow', text: name }),
    button('Forget', async (ev) => {
      if (!confirm(`Forget ${name}?`)) return;
      await busy(ev.target.parentNode, null, async () => {
        await api('DELETE', '/api/wifi/networks?ssid=' + encodeURIComponent(name));
        refreshLists(await api('GET', '/api/wifi/networks'));
      });
    }))) : [h('li', { class: 'muted', text: 'None yet.' })];
  const saved = h('ul', { class: 'list' }, ...savedRows(n));
  const now = h('div', {}, facts([['Wi-Fi', wifiText(Object.assign({ ap_ssid: auth.ap_ssid }, n))]]));
  const refreshLists = (list) => {
    saved.replaceChildren(...savedRows(list));
    now.replaceChildren(facts([['Wi-Fi', wifiText(Object.assign({ ap_ssid: auth.ap_ssid }, list))]]));
  };
  const lanNote = !auth.on_ap && n.state === 'station'
    ? h('p', { class: 'muted small', text: `This phone reaches the device over ${n.ssid}. Trying another network may ` +
      'cut this page off: "Save without trying" is safer from here.' }) : null;
  main.replaceChildren(
    h('h1', { text: 'Wi-Fi' }),
    card('Now', now),
    card('Saved networks', saved, h('p', { class: 'muted small', text: 'Tried in this order, at most 5.' })),
    card('Add a network', h('div', {}, found, scanNote, actions(button('Look for networks', scan))), form, lanNote),
  );
  scan();
}

/* ---- Location and time ---- */

async function placePage() {
  const s = await api('GET', '/api/settings');
  const loc = s.location || {}, time = s.time || {};
  const name = h('input', { type: 'text', maxlength: 31, value: loc.name || '' });
  const lat = h('input', { type: 'number', step: 'any', min: -90, max: 90, value: loc.lat ?? '' });
  const lon = h('input', { type: 'number', step: 'any', min: -180, max: 180, value: loc.lon ?? '' });
  const locNote = h('p');
  const locCard = card('Location', h('p', { class: 'muted small', text: 'For sunrise, sunset and the weather. ' +
    'Decimal degrees: north and east are positive.' }),
  field('Name', name), h('div', { class: 'row' }, h('div', {}, field('Latitude', lat)), h('div', {}, field('Longitude', lon))),
  locNote, actions(button('Save location', () => busy(locCard, locNote, async () => {
    const la = Number(lat.value), lo = Number(lon.value);
    if (!name.value.trim() || bytes(name.value) > 31) throw new ApiError('The name needs 1 to 31 bytes.');
    if (lat.value === '' || !(la >= -90 && la <= 90)) throw new ApiError('Latitude: -90 to 90.');
    if (lon.value === '' || !(lo >= -180 && lo <= 180)) throw new ApiError('Longitude: -180 to 180.');
    await api('PATCH', '/api/settings', { location: { name: name.value.trim(), lat: la, lon: lo } });
    toast('Location saved');
  }), 'primary')));

  const filter = h('input', { type: 'search', placeholder: 'Filter: Prague, New_York, UTC…' });
  const zone = h('select', { size: 8 });
  const fillZones = () => {
    const q = filter.value.trim().toLowerCase().replace(/ /g, '_');
    const names = Object.keys(ZONES).filter((z) => !q || z.toLowerCase().includes(q));
    if (time.tz_iana && !names.includes(time.tz_iana) && !q) names.unshift(time.tz_iana);
    zone.replaceChildren(...names.map((z) => h('option', { value: z, selected: z === time.tz_iana }, z.replace(/_/g, ' '))));
  };
  filter.oninput = fillZones;
  fillZones();
  const h24 = h('input', { type: 'checkbox', checked: time.clock_24h !== false });
  const tzNote = h('p');
  const mine = phoneZone();
  const tzCard = card('Time zone', field('Zone', filter), zone,
    mine && mine !== time.tz_iana ? h('p', { class: 'muted small' }, `This phone: ${mine.replace(/_/g, ' ')}. `,
      h('a', { href: '#', onclick: (ev) => { ev.preventDefault(); filter.value = ''; time.tz_iana = mine; fillZones(); } },
        'Pick it')) : null,
    h('label', { class: 'check' }, h24, '24-hour clock'), tzNote,
    actions(button('Save time zone', () => busy(tzCard, tzNote, async () => {
      const z = zone.value || time.tz_iana;
      if (!ZONES[z] && z !== time.tz_iana) throw new ApiError('Pick a zone from the list.');
      const t = { clock_24h: h24.checked };
      if (ZONES[z]) Object.assign(t, { tz_iana: z, tz_posix: ZONES[z] });
      await api('PATCH', '/api/settings', { time: t });
      time.tz_iana = z;
      toast('Time zone saved');
    }), 'primary')));

  const clockNote = h('p');
  const clockCard = card('Clock', h('p', { class: 'muted small', text: 'Sets the device from this phone\'s clock. ' +
    'Without a backup cell the device forgets the time when it is switched off.' }), clockNote,
  actions(button('Set the clock from this phone', () => busy(clockCard, clockNote, async () => {
    await setClock(false);
    toast('Clock set');
  }))));
  main.replaceChildren(h('h1', { text: 'Location and time' }), locCard, tzCard, clockCard);
}

/* ---- Device: the settings the menu also has (spec §5.7, D19) ---- */

const LANGUAGES = [['en', 'English'], ['cs', 'Čeština']];
const SENSOR_MIN = [1, 2, 5, 10, 15, 30];
const RATES = [0.25, 0.5, 1, 2, 4, 8];

async function devicePage() {
  const s = await api('GET', '/api/settings');
  const sensors = s.sensors || {}, display = s.display || {};
  const select = (pairs, current) => h('select', {}, pairs.map(([v, t]) => h('option', { value: v, selected: v === current }, t)));
  const language = select(LANGUAGES, s.language || 'en');
  const unit = select([['C', '°C'], ['F', '°F']], (s.units || {}).temp === 'F' ? 'F' : 'C');
  const temp = h('input', { type: 'number', step: 0.1, min: -10, max: 10, value: sensors.temp_offset_c ?? 0 });
  const hum = h('input', { type: 'number', step: 0.5, min: -20, max: 20, value: sensors.hum_offset_pct ?? 0 });
  const every = select(SENSOR_MIN.map((m) => [String(m), `${m} min`]), String(sensors.interval_min ?? 5));
  const update = select(Array.from({ length: 15 }, (_, i) => [String(i + 1), `${i + 1} min`]), String(display.update_min ?? 1));
  const rate = select(RATES.map((r) => [String(r), `${String(r)} Hz`]), String(display.lpm_hz ?? 1));
  const note = h('p');
  const form = h('div', {},
    card('Language and units', field('Language on the device', language), field('Temperature', unit)),
    card('Sensors', field('Temperature offset, °C', temp, 'Added to every reading. The board warms the sensor by ' +
      'about 2 °C, which the default −2.0 makes up for; trim it against a thermometer you trust.'),
    field('Humidity offset, %', hum), field('Measure every', every)),
    card('Screen', field('Update every', update, 'How often the dashboard is redrawn. Each update wakes the device.'),
      field('Refresh rate', rate, 'How often the panel repaints its image between updates. At 1 Hz it looked as ' +
        'good as the faster rates (D12).')),
    note,
    actions(button('Save', () => busy(form, note, async () => {
      const t = Number(temp.value), hu = Number(hum.value);
      if (temp.value === '' || !(t >= -10 && t <= 10)) throw new ApiError('Temperature offset: -10 to 10 °C.');
      if (hum.value === '' || !(hu >= -20 && hu <= 20)) throw new ApiError('Humidity offset: -20 to 20 %.');
      await api('PATCH', '/api/settings', {
        language: language.value,
        units: { temp: unit.value },
        sensors: { temp_offset_c: Math.round(t * 10) / 10, hum_offset_pct: Math.round(hu * 2) / 2,
                   interval_min: Number(every.value) },
        display: { update_min: Number(update.value), lpm_hz: Number(rate.value) },
      });
      toast('Saved');
    }), 'primary')));
  main.replaceChildren(h('h1', { text: 'Device' }), form);
}

/* ---- Presets (spec §5.4) ---- */

const LAYOUT_NAMES = { classic: 'Classic', weather: 'Weather', grid: 'Grid', focus: 'Focus' };
const CYCLE_S = [10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600];
const cycleLabel = (s) => (s < 60 ? `${s} s` : s < 3600 ? `${s / 60} min` : `${s / 3600} h`);
const DAYS = ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']; /* bit 0 is Monday */
let catalogue = null; /* GET /api/layouts, cached: it doesn't change while the page is open */

function uniqueId(doc, base) {
  const stem = (base.toLowerCase().normalize('NFD').replace(/[^a-z0-9_-]+/g, '-').replace(/^-+|-+$/g, '') || 'preset')
    .slice(0, 12);
  let id = stem;
  for (let i = 2; doc.presets.some((p) => p.id === id); i++) id = `${stem}-${i}`.slice(0, 15);
  return id;
}

async function presetsPage() {
  if (!catalogue) catalogue = await api('GET', '/api/layouts');
  const [doc, fieldList] = await Promise.all([api('GET', '/api/presets'), api('GET', '/api/fields')]);
  const ed = {
    doc, fields: fieldList.fields, dirty: false,
    sel: Math.max(0, doc.presets.findIndex((p) => p.id === doc.active)),
    img: h('img', { class: 'screen', alt: 'Preview' }), previewNote: h('p', { class: 'small' }),
    timer: null, url: null, saveNote: h('p'),
  };
  if (!ed.doc.schedule) ed.doc.schedule = { enabled: false, entries: [] };
  renderPresets(ed);
  preview(ed);
}

function layoutOf(id) {
  return catalogue.layouts.find((l) => l.id === id) || catalogue.layouts[0];
}

/* The device renders the preset as it stands in the editor (POST /api/preview.bmp). */
function preview(ed) {
  clearTimeout(ed.timer);
  ed.timer = setTimeout(async () => {
    const p = ed.doc.presets[ed.sel];
    try {
      const bmp = await api('POST', '/api/preview.bmp?preset=' + encodeURIComponent(p.id), ed.doc);
      if (ed.url) URL.revokeObjectURL(ed.url);
      ed.url = URL.createObjectURL(bmp);
      ed.img.src = ed.url;
      ed.previewNote.textContent = '';
    } catch (e) {
      ed.previewNote.className = 'bad small';
      ed.previewNote.textContent = e.message;
    }
  }, 250);
}

function changed(ed, rerender) {
  ed.dirty = true;
  if (rerender) renderPresets(ed);
  else ed.saveBar && (ed.saveBar.hidden = false);
  preview(ed);
}

function renderPresets(ed) {
  const doc = ed.doc, p = doc.presets[ed.sel];
  const list = h('ul', { class: 'list' }, doc.presets.map((q, i) => h('li', { class: i === ed.sel ? 'sel' : null },
    h('input', { type: 'radio', name: 'active', title: 'Show this one now', checked: q.id === doc.active,
                 onchange: () => { doc.active = q.id; changed(ed, true); } }),
    h('button', { class: 'link', type: 'button', onclick: () => { ed.sel = i; renderPresets(ed); preview(ed); } },
      h('b', { text: q.name }), ' ', h('span', { class: 'muted small', text: LAYOUT_NAMES[q.layout] || q.layout })),
    h('label', { class: 'check small', title: 'KEY and the auto-cycle step through these' },
      h('input', { type: 'checkbox', checked: q.in_cycle, onchange: (ev) => { q.in_cycle = ev.target.checked; changed(ed, false); } }),
      'cycle'))));
  const listCard = card('Presets', list, h('p', { class: 'muted small', text: 'Tap a preset to edit it. The dot marks ' +
    'the one on the screen now; "cycle" puts a preset in the order KEY and the auto-cycle step through.' }), actions(
    button('New preset', () => {
      if (doc.presets.length >= 16) return toast('16 presets at most');
      const copy = JSON.parse(JSON.stringify(p));
      copy.name = `${p.name} copy`.slice(0, 23);
      copy.id = uniqueId(doc, copy.name);
      doc.presets.splice(ed.sel + 1, 0, copy);
      ed.sel++;
      changed(ed, true);
    })));

  const name = h('input', { type: 'text', maxlength: 23, value: p.name,
                            onchange: (ev) => { p.name = ev.target.value.trim() || p.id; changed(ed, true); } });
  const layout = h('select', { onchange: (ev) => {
    const old = p.slots;
    p.layout = ev.target.value;
    p.slots = {};
    for (const slot of layoutOf(p.layout).slots) { /* keep what still fits */
      const f = old[slot.id] && ed.fields.find((x) => x.id === old[slot.id]);
      if (f && slot.kinds.includes(f.kind)) p.slots[slot.id] = f.id;
    }
    changed(ed, true);
  } }, catalogue.layouts.map((l) => h('option', { value: l.id, selected: l.id === p.layout }, LAYOUT_NAMES[l.id] || l.id)));
  const slots = h('div', { class: 'slots' }, layoutOf(p.layout).slots.map((slot) => h('div', { class: 'slot' },
    h('label', { text: `${slot.id} · ${slot.size}` }),
    h('select', { onchange: (ev) => {
      if (ev.target.value) p.slots[slot.id] = ev.target.value;
      else delete p.slots[slot.id];
      changed(ed, false);
    } }, h('option', { value: '' }, '(empty)'),
    ed.fields.filter((f) => slot.kinds.includes(f.kind)).map((f) => h('option',
      { value: f.id, selected: p.slots[slot.id] === f.id }, `${f.label} — ${f.value || 'no data yet'}`))))));

  const o = p.options;
  const check = (key, text) => h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: !!o[key],
    onchange: (ev) => { o[key] = ev.target.checked; changed(ed, key === 'seconds'); } }), text);
  const clock = h('select', { onchange: (ev) => {
    if (ev.target.value === '') delete o.clock_24h;
    else o.clock_24h = ev.target.value === '24';
    changed(ed, false);
  } }, h('option', { value: '', selected: o.clock_24h === undefined }, 'As set for the device'),
  h('option', { value: '24', selected: o.clock_24h === true }, '24-hour'),
  h('option', { value: '12', selected: o.clock_24h === false }, '12-hour'));
  const stale = h('select', { onchange: (ev) => { o.stale_policy = ev.target.value; changed(ed, false); } },
    [['stale', 'Show it with its age'], ['placeholder', 'Show a dash'], ['hide', 'Leave the slot empty']]
      .map(([v, t]) => h('option', { value: v, selected: (o.stale_policy || 'stale') === v }, t)));
  const battery = new Set(o.status_battery || ['percent']);
  const part = (key, text) => h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: battery.has(key),
    onchange: (ev) => {
      ev.target.checked ? battery.add(key) : battery.delete(key);
      o.status_battery = ['percent', 'voltage', 'days'].filter((k) => battery.has(k));
      changed(ed, false);
    } }), text);
  const editCard = card(`Edit ${p.name}`, ed.img, ed.previewNote,
    field('Name', name), field('Layout', layout), slots,
    field('Time format', clock), check('seconds', 'Show seconds'),
    o.seconds ? h('p', { class: 'bad small', text: 'Seconds wake the device every second: the battery lasts far less.' }) : null,
    check('invert', 'White on black'), field('Old or missing data', stale),
    h('label', {}, 'Status bar'), check('status_clock', 'A small clock'),
    part('percent', 'Battery level'), part('voltage', 'Battery voltage'), part('days', 'Days of battery left'),
    actions(
      button('Move up', () => { if (ed.sel > 0) { doc.presets.splice(ed.sel - 1, 0, doc.presets.splice(ed.sel, 1)[0]); ed.sel--; changed(ed, true); } }),
      button('Move down', () => { if (ed.sel < doc.presets.length - 1) { doc.presets.splice(ed.sel + 1, 0, doc.presets.splice(ed.sel, 1)[0]); ed.sel++; changed(ed, true); } }),
      button('Delete', () => {
        if (doc.presets.length < 2) return toast('The last preset stays');
        if (!confirm(`Delete ${p.name}?`)) return;
        doc.presets.splice(ed.sel, 1);
        doc.schedule.entries = doc.schedule.entries.filter((e) => e.action !== 'preset' || e.preset !== p.id);
        if (doc.active === p.id) doc.active = doc.presets[0].id;
        ed.sel = Math.min(ed.sel, doc.presets.length - 1);
        changed(ed, true);
      }, 'danger')));

  const intervals = CYCLE_S.includes(doc.cycle.interval_s) ? CYCLE_S : [...CYCLE_S, doc.cycle.interval_s].sort((a, b) => a - b);
  const cycleCard = card('Auto-cycle',
    h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: doc.cycle.enabled,
      onchange: (ev) => { doc.cycle.enabled = ev.target.checked; changed(ed, false); } }), 'Step through the presets by itself'),
    field('Every', h('select', { onchange: (ev) => { doc.cycle.interval_s = Number(ev.target.value); changed(ed, false); } },
      intervals.map((s) => h('option', { value: s, selected: s === doc.cycle.interval_s }, cycleLabel(s))))),
    h('p', { class: 'muted small', text: 'Each switch wakes the device: short intervals cost battery.' }));

  const sched = doc.schedule;
  const entry = (e, i) => {
    const at = h('input', { type: 'time', value: e.at, required: true, onchange: (ev) => { e.at = ev.target.value; changed(ed, false); } });
    const days = h('div', { class: 'days' }, DAYS.map((d, bit) => h('label', {}, h('input', { type: 'checkbox',
      checked: (e.days ?? 127) & (1 << bit), onchange: (ev) => {
        e.days = ((e.days ?? 127) & ~(1 << bit)) | (ev.target.checked ? 1 << bit : 0);
        changed(ed, false);
      } }), d)));
    const kind = h('select', { onchange: (ev) => {
      e.action = ev.target.value;
      if (e.action === 'night') { delete e.preset; e.until = e.until || '06:00'; }
      else { delete e.until; e.preset = e.preset || p.id; }
      changed(ed, true);
    } }, h('option', { value: 'preset', selected: e.action === 'preset' }, 'Switch to a preset'),
    h('option', { value: 'night', selected: e.action === 'night' }, 'Night: screen off until'));
    const what = e.action === 'night'
      ? h('input', { type: 'time', value: e.until, onchange: (ev) => { e.until = ev.target.value; changed(ed, false); } })
      : h('select', { onchange: (ev) => { e.preset = ev.target.value; changed(ed, false); } },
        doc.presets.map((q) => h('option', { value: q.id, selected: q.id === e.preset }, q.name)));
    return h('div', { class: 'entry' }, h('div', { class: 'row' }, at, kind, what), days,
      actions(button('Remove', () => { sched.entries.splice(i, 1); changed(ed, true); })));
  };
  const schedCard = card('Schedule',
    h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: sched.enabled,
      onchange: (ev) => { sched.enabled = ev.target.checked; changed(ed, false); } }), 'Run the schedule'),
    h('p', { class: 'muted small', text: 'At each time, on the ticked days, the device switches preset, or turns the ' +
      'screen off for the night. A press on KEY or BOOT shows the screen for a minute.' }),
    sched.entries.map(entry),
    actions(button('Add a time', () => {
      if (sched.entries.length >= 8) return toast('8 times at most');
      sched.entries.push({ at: '22:00', days: 127, action: 'preset', preset: p.id });
      changed(ed, true);
    })));

  ed.saveBar = card(null, ed.saveNote, actions(
    button('Save', () => busy(ed.saveBar, ed.saveNote, async () => {
      ed.doc = await api('PUT', '/api/presets', ed.doc);
      if (!ed.doc.schedule) ed.doc.schedule = { enabled: false, entries: [] };
      ed.dirty = false;
      toast('Presets saved');
      renderPresets(ed);
    }), 'primary'),
    button('Undo changes', () => presetsPage().catch(() => {}))));
  ed.saveBar.hidden = !ed.dirty;
  ed.saveBar.style.position = 'sticky';
  ed.saveBar.style.bottom = '8px';
  const y = window.scrollY; /* a re-render keeps the place on the page */
  main.replaceChildren(h('h1', { text: 'Presets' }), listCard, editCard, cycleCard, schedCard, ed.saveBar);
  window.scrollTo(0, y);
}

/* ---- Firmware (spec §10.5) ---- */

function waitForRestart(text) {
  document.getElementById('nav').hidden = true;
  document.getElementById('done').hidden = true;
  const note = h('p', { class: 'muted', text: 'Waiting for the device…' });
  main.replaceChildren(card(text, h('p', { text: 'The device restarts and turns Wi-Fi on again. If this phone ' +
    'doesn\'t rejoin its network by itself, join it again.' }), note));
  (async () => {
    await sleep(8000);
    for (const until = Date.now() + 120000; Date.now() < until; await sleep(2000)) {
      try {
        await api('GET', '/api/auth');
        location.reload();
        return;
      } catch (e) { /* not back yet */ }
    }
    note.textContent = 'No answer yet. Once the device shows its Wi-Fi screen, reload this page.';
  })();
}

function upload(file, progress) {
  return new Promise((resolve, reject) => {
    const x = new XMLHttpRequest();
    x.open('POST', '/api/ota');
    x.setRequestHeader('Content-Type', 'application/octet-stream');
    x.upload.onprogress = (e) => e.lengthComputable && (progress.value = e.loaded / e.total);
    x.onload = () => {
      let d = {};
      try { d = JSON.parse(x.responseText); } catch (e) { /* not JSON */ }
      if (x.status === 200) resolve(d);
      else reject(new ApiError(sentence(d.error || x.statusText), x.status));
    };
    x.onerror = () => reject(new ApiError('The upload broke off.', 0));
    x.send(file);
  });
}

async function firmwarePage() {
  const o = await api('GET', '/api/ota/status');
  const file = h('input', { type: 'file', accept: '.bin,application/octet-stream' });
  const progress = h('progress', { max: 1, value: 0, hidden: true });
  const note = h('p');
  const upCard = card('Update', h('p', { class: 'muted small', text: 'Pick reflbo.bin from a build. The device ' +
    'checks that it is reflbo firmware for this chip, writes it next to the running one and restarts into it. ' +
    'If the new firmware doesn\'t run properly for a minute, the device goes back to this one.' }),
  file, progress, note, actions(button('Update', () => busy(upCard, note, async () => {
    const f = file.files[0];
    if (!f) throw new ApiError('Pick a file first.');
    const head = new Uint8Array(await f.slice(0, 1).arrayBuffer());
    if (f.size < 65536 || head[0] !== 0xe9) throw new ApiError('That is not an ESP32 firmware image.');
    progress.hidden = false;
    const r = await upload(f, progress);
    waitForRestart(`Updated to ${r.version}`);
  }), 'primary')));
  main.replaceChildren(h('h1', { text: 'Firmware' }),
    card('Running', facts([
      ['Version', o.version], ['Built', o.built], ['Slot', o.partition],
      o.pending ? ['State', 'New: it proves itself during its first minute'] : null,
    ]), o.rolled_back_from ? h('p', { class: 'bad', text: `An update in ${o.rolled_back_from} didn't work, so the ` +
      'device went back to this version.' }) : null),
    upCard);
}

/* ---- Backup (spec §14.4) ---- */

async function backupPage() {
  const note = h('p');
  const file = h('input', { type: 'file', accept: '.json,application/json' });
  const saveCard = card('Back up', h('p', { class: 'muted small', text: 'Settings and presets in one file. Wi-Fi ' +
    'passwords and the web password are not in it.' }), actions(button('Download backup', async () => {
    const blob = await api('GET', '/api/backup');
    download(blob, `${auth.host}-${new Date().toISOString().slice(0, 10)}.json`);
  }, 'primary')));
  const restoreCard = card('Restore', file, note, actions(button('Restore', () => busy(restoreCard, note, async () => {
    const f = file.files[0];
    if (!f) throw new ApiError('Pick a backup file first.');
    let bundle;
    try { bundle = JSON.parse(await f.text()); } catch (e) { throw new ApiError('That file is not JSON.'); }
    const r = await api('POST', '/api/restore', bundle);
    note.className = 'good';
    note.textContent = 'Restored.' + (r.skipped.length ? ` Left out, for a later firmware: ${r.skipped.join(', ')}.` : '');
  }))));
  const resetCard = card('Factory reset', h('p', { class: 'muted small', text: 'Erases the settings, presets, saved ' +
    'Wi-Fi networks and the web password, then restarts. The device then starts like new.' }),
  actions(button('Factory reset…', async () => {
    if (!confirm('Erase everything on the device and restart it?')) return;
    await api('POST', '/api/factory-reset');
    document.getElementById('nav').hidden = true;
    document.getElementById('done').hidden = true;
    main.replaceChildren(card('Erased', h('p', { text: 'The device restarts with its first-run screen.' })));
  }, 'danger')));
  main.replaceChildren(h('h1', { text: 'Backup' }), saveCard, restoreCard, resetCard);
}

start();
```

- [ ] **Step 5: Build.**

Run: `node --check web/app.js && tools/idf.sh build && ctest --test-dir build-host --output-on-failure`
Expected: no syntax error (skip `node` if it isn't installed); the firmware builds without warnings, with the pages gzipped into it; all 43 host tests pass, `tools_unittests` with the new zone tests.

- [ ] **Step 6: Commit.** Task 10 wires the server in and checks it on the board.

```bash
git add components/webui web tools/gen_zones.py tools/tests/test_gen_zones.py
git commit -m "feat(webui): add the web configurator's server and pages"
```

---

### Task 10: Config mode, the API, the first run and OTA (`main`)

**Files:**
- Create: `main/app_config.c`, `main/app_web.c`
- Modify: `main/app.c`, `main/app_ui.c`, `main/app_internal.h`, `main/app_menu.c`, `main/app_cmds.c`, `main/CMakeLists.txt`, `main/Kconfig.projbuild`, `sdkconfig.defaults`

**Interfaces:**
- Consumes: everything above.
- Produces:
  - **Config mode** (`app_config.c`, spec §10.2): `app_net_init()`, `app_config_enter()`, `app_config_exit()`, `app_config_active()`, `app_config_toggle_qr()`, `app_config_tick()`, `app_config_deadline_ms()`, `app_config_redraw_ms()` and `app_config_draw()`. The panel is in HPM while it runs; KEY switches the QR code and BOOT held 1 s ends it; it ends 10 min after it began or after the last page request, or on a critical battery.
  - **The API** (`app_web.c`, spec §10.3): `app_web_api()`, a `webui_api_fn` for status, settings, layouts, fields, presets, the preview and screenshot BMPs, time, backup and restore. Big buffers are in PSRAM.
  - **In `app.c`**: `app_post()`; `app_restart(message, config_after)` and `app_factory_reset()`, moved from the menu; config mode and a pending image keep the board awake; BOOT held 3 s on the dashboard starts config mode; the first run's buttons (spec §5.6); OTA validity after 60 s and a rendered frame; a restart from the web UI returns in config mode, also across a rollback (the RTC_NOINIT flag).
  - **In `app_ui.c`**: the first run (`app_ui_first_run()`, `app_ui_end_first_run()`); `app_ui_settings_json()`, `app_ui_check_settings()`, `app_ui_replace_settings()`, `app_ui_patch_settings()`, `app_ui_replace_presets()` and `app_ui_set_zone()`; no battery samples during config mode (spec §8); the config screen and the first run drawn in their turn.
  - **The menu**: the Wi-Fi actions with their toasts, and the IP and MAC rows.
  - **Console**: `wifi status` and `wifi scan`.
  - **Kconfig**: the default location (Brno) and the −2.0 °C temperature offset (D19).
  - **sdkconfig**: app rollback; 16 lwIP sockets and 1 KB request headers (gotcha 25); PSRAM `.bss` for `EXT_RAM_BSS_ATTR`.

- [ ] **Step 1: Implement.** The new files:

```c
#include <stdint.h>
#include <stdio.h>

#include "app.h"
#include "app_internal.h"
#include "board_buttons.h"
#include "display.h"
#include "esp_log.h"
#include "lang.h"
#include "netmgr.h"
#include "st7305.h"
#include "ui_screens.h"
#include "webui.h"

/* Config mode (spec §10.2): Wi-Fi, the web configurator and their screen. It belongs to the app
 * task; netmgr and webui report back through app_post(). */

#define IDLE_MS (10 * 60 * 1000) /* spec §10.2: 10 min without HTTP requests */

static const char *TAG = "app_config";

/* Config mode binds KEY short (the other QR code) and BOOT long (exit), both at once (spec §5.6). */
static const gesture_config_t k_config_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = false },
    [BOARD_BUTTON_BOOT] = { .long_ms = 1000, .double_enabled = false },
};

static bool s_net_ready, s_on, s_qr_url;
static int64_t s_started_ms;
static int s_shown_minutes;

static void net_changed_on_app(void *arg)
{
    (void)arg;
    if (s_on) {
        app_ui_render();
    }
}

static void net_changed(void) /* on the netmgr task or the event loop */
{
    app_post(net_changed_on_app, NULL);
}

esp_err_t app_net_init(void)
{
    if (s_net_ready) {
        return ESP_OK;
    }
    esp_err_t err = netmgr_init(net_changed);
    s_net_ready = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi manager: %s", esp_err_to_name(err));
    }
    return err;
}

static void web_event_on_app(void *arg)
{
    webui_event_t event = (webui_event_t)(intptr_t)arg;
    switch (event) {
    case WEBUI_EVENT_DONE:
        app_config_exit();
        break;
    case WEBUI_EVENT_REBOOT:
        app_restart(LS_T_REBOOTING, true);
        break;
    case WEBUI_EVENT_UPDATED:
        app_restart(LS_T_UPDATED, true);
        break;
    case WEBUI_EVENT_FACTORY_RESET:
        app_factory_reset();
        break;
    }
}

static void web_event(webui_event_t event) /* on the server's task, after the reply went out */
{
    app_post(web_event_on_app, (void *)(intptr_t)event);
}

void app_config_enter(void)
{
    if (s_on || app_state()->critical || app_net_init() != ESP_OK) {
        return;
    }
    app_menu_close();
    app_ui_end_first_run();
    s_on = true;
    s_qr_url = false;
    s_started_ms = app_uptime_ms();
    board_buttons_set_config(k_config_buttons);
    esp_err_t err = st7305_set_mode(ST7305_MODE_HPM); /* spec §9.1: the screen answers at once */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HPM: %s", esp_err_to_name(err));
    }
    bool no_password = !webui_password_set();
    netmgr_start(no_password); /* D18: only a phone on the AP may choose the password */
    const webui_config_t web = { .run = app_execute, .api = app_web_api, .event = web_event };
    err = webui_start(&web);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "web configurator: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "config mode on%s", no_password ? ", no web password yet" : "");
    app_ui_render();
}

void app_config_exit(void)
{
    if (!s_on) {
        return;
    }
    s_on = false;
    webui_stop();
    netmgr_stop();
    board_buttons_set_config(k_app_dashboard_buttons);
    esp_err_t err = st7305_set_mode(ST7305_MODE_LPM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LPM: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "config mode off");
    app_ui_toast(lang_str(lang_get(app_settings()->language), LS_T_WIFI_OFF));
}

bool app_config_active(void)
{
    return s_on;
}

void app_config_toggle_qr(void)
{
    s_qr_url = !s_qr_url;
    app_ui_render();
}

/* Config mode ends 10 min after it began or after the last request, whichever is later. */
int64_t app_config_deadline_ms(void)
{
    if (!s_on) {
        return 0;
    }
    int64_t last = webui_last_request_ms();
    return (last > s_started_ms ? last : s_started_ms) + IDLE_MS;
}

static int minutes_left(void)
{
    int64_t left = app_config_deadline_ms() - app_uptime_ms();
    return left <= 0 ? 0 : (int)((left + 59999) / 60000);
}

int64_t app_config_redraw_ms(void)
{
    if (!s_on) {
        return 0;
    }
    int m = minutes_left(); /* the count drops at the next whole minute before the deadline */
    return m > 0 ? app_config_deadline_ms() - (int64_t)(m - 1) * 60000 : app_config_deadline_ms();
}

void app_config_tick(void)
{
    if (!s_on) {
        return;
    }
    if (app_state()->critical) { /* spec §8: nothing may drain the last of the battery */
        ESP_LOGW(TAG, "battery critical: Wi-Fi off");
        app_config_exit();
    } else if (app_uptime_ms() >= app_config_deadline_ms()) {
        ESP_LOGI(TAG, "no requests for %d min", IDLE_MS / 60000);
        app_config_exit();
    } else if (minutes_left() != s_shown_minutes) {
        app_ui_render();
    }
}

static ui_net_state_t view_state(netmgr_state_t state)
{
    switch (state) {
    case NETMGR_JOINING:
        return UI_NET_JOINING;
    case NETMGR_STATION:
        return UI_NET_STATION;
    case NETMGR_AP:
        return UI_NET_AP;
    default:
        return UI_NET_STARTING; /* the start command is still on its way */
    }
}

void app_config_draw(gfx_fb_t *fb, const lang_t *lang)
{
    static netmgr_status_t st;
    netmgr_status(&st);
    s_shown_minutes = minutes_left();
    ui_config_view_t v = { .state = view_state(st.state), .ap_on = st.ap_on, .qr_url = s_qr_url, .ssid = st.ssid,
                           .ip = st.ip, .host = st.host, .ap_ssid = st.ap_ssid, .ap_pass = st.ap_pass,
                           .minutes_left = s_shown_minutes };
    ui_draw_config(fb, &v, lang);
}
```

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_internal.h"
#include "cJSON.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "netmgr.h"
#include "storage_backup.h"
#include "timekeeping.h"
#include "ui_catalog.h"
#include "ui_dashboard.h"
#include "webui.h"

/* The web configurator's API (spec §10.3), apart from what webui answers itself (the password,
 * Wi-Fi, OTA and the system actions). Every call runs on the app task, which owns this state. */

static const char *TAG = "app_web";

#define MIN_EPOCH 1577836800 /* 2020-01-01: a phone's clock is at least this */
#define MAX_EPOCH              4102444799 /* 2099-12-31 23:59:59: the RTC's last second */

static void reply_error(webui_reply_t *reply, uint8_t *out, size_t size, int status, const char *message)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", message);
    reply->status = cJSON_PrintPreallocated(o, (char *)out, (int)size, false) ? status : 500;
    reply->len = reply->status == status ? strlen((char *)out) : 0;
    reply->type = "application/json";
    cJSON_Delete(o);
}

/* Prints `o` into the reply and frees it. */
static void reply_cjson(webui_reply_t *reply, uint8_t *out, size_t size, cJSON *o)
{
    if (cJSON_PrintPreallocated(o, (char *)out, (int)size, false)) {
        reply->status = 200;
        reply->len = strlen((char *)out);
        reply->type = "application/json";
    } else {
        reply_error(reply, out, size, 500, "the reply doesn't fit");
    }
    cJSON_Delete(o);
}

/* A JSON text written by one of the codecs; 0 bytes means it didn't fit. */
static void reply_text(webui_reply_t *reply, uint8_t *out, size_t size, size_t len)
{
    if (len == 0) {
        reply_error(reply, out, size, 500, "the reply doesn't fit");
        return;
    }
    reply->status = 200;
    reply->len = len;
    reply->type = "application/json";
}

static const char *battery_state_name(uint8_t state)
{
    static const char *const k_names[] = { "unknown", "discharging", "charging", "full" };
    return state < sizeof(k_names) / sizeof(k_names[0]) ? k_names[state] : "unknown";
}

static const char *net_state_name(netmgr_state_t state)
{
    switch (state) {
    case NETMGR_JOINING:
        return "joining";
    case NETMGR_STATION:
        return "station";
    case NETMGR_AP:
        return "ap";
    default:
        return "off";
    }
}

/* A datastore value in its unit (0.01 °C, 0.01 %, 0.1 d) as a JSON number, or null. */
static void add_value(cJSON *o, const char *key, ds_field_t field, double scale)
{
    ds_entry_t e;
    if (ds_get(app_ds(), field, &e) && e.updated != 0) {
        cJSON_AddNumberToObject(o, key, e.value / scale);
    } else {
        cJSON_AddNullToObject(o, key);
    }
}

static void get_status(uint8_t *out, size_t size, webui_reply_t *reply)
{
    const app_ui_state_t *st = app_state();
    const esp_app_desc_t *app = esp_app_get_description();
    netmgr_status_t net;
    netmgr_status(&net);
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char text[40];
    cJSON *o = cJSON_CreateObject();

    cJSON *dev = cJSON_AddObjectToObject(o, "device");
    cJSON_AddStringToObject(dev, "id", net.host);
    snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(dev, "mac", text);
    cJSON_AddStringToObject(dev, "firmware", app->version);
    cJSON_AddNumberToObject(dev, "uptime_s", (double)(app_uptime_ms() / 1000));
    cJSON_AddNumberToObject(dev, "free_heap", esp_get_free_heap_size());
    cJSON_AddStringToObject(dev, "language", st->settings.language);

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    cJSON *t = cJSON_AddObjectToObject(o, "time");
    cJSON_AddBoolToObject(t, "valid", timekeeping_valid());
    cJSON_AddNumberToObject(t, "epoch", (double)now);
    strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &local);
    cJSON_AddStringToObject(t, "local", text);
    cJSON_AddStringToObject(t, "tz_iana", st->settings.tz_iana);
    cJSON_AddStringToObject(t, "tz_posix", st->settings.tz_posix);

    cJSON *bat = cJSON_AddObjectToObject(o, "battery");
    ds_entry_t e;
    if (ds_get(app_ds(), DS_BAT_LEVEL, &e) && e.updated != 0) {
        cJSON_AddNumberToObject(bat, "percent", e.value);
        cJSON_AddNumberToObject(bat, "mv", e.mv);
        cJSON_AddStringToObject(bat, "state", battery_state_name(e.bat_state));
    }
    add_value(bat, "days_left", DS_BAT_DAYS, 10.0);
    cJSON_AddBoolToObject(bat, "critical", st->critical);

    cJSON *env = cJSON_AddObjectToObject(o, "sensors");
    add_value(env, "temp_c", DS_ENV_TEMP, 100.0);
    add_value(env, "hum_pct", DS_ENV_HUM, 100.0);
    if (ds_get(app_ds(), DS_ENV_TEMP, &e) && e.updated != 0 && now >= (time_t)e.updated) {
        cJSON_AddNumberToObject(env, "age_s", (double)(now - (time_t)e.updated));
    }

    cJSON *wifi = cJSON_AddObjectToObject(o, "wifi");
    cJSON_AddStringToObject(wifi, "state", net_state_name(net.state));
    cJSON_AddStringToObject(wifi, "ssid", net.ssid);
    cJSON_AddStringToObject(wifi, "ip", net.ip);
    cJSON_AddNumberToObject(wifi, "rssi", net.rssi);
    cJSON_AddBoolToObject(wifi, "ap_on", net.ap_on);
    cJSON_AddStringToObject(wifi, "ap_ssid", net.ap_ssid);
    cJSON_AddNumberToObject(wifi, "ap_clients", net.ap_clients);

    const ui_preset_t *active = &st->presets.presets[st->presets.active];
    cJSON *preset = cJSON_AddObjectToObject(o, "preset");
    cJSON_AddStringToObject(preset, "active", active->id);
    cJSON_AddStringToObject(preset, "name", active->name);
    cJSON_AddBoolToObject(preset, "cycle", st->presets.cycle_enabled);
    reply_cjson(reply, out, size, o);
}

/* The framebuffer the previews draw into; the display's own stays as it is. */
static gfx_fb_t *preview_fb(void)
{
    static gfx_fb_t fb;
    static uint8_t *buf;
    if (buf == NULL) {
        buf = heap_caps_malloc(gfx_fb_size(400, 300), MALLOC_CAP_SPIRAM);
        if (buf == NULL) {
            return NULL;
        }
        gfx_fb_init(&fb, buf, 400, 300);
    }
    return &fb;
}

static void reply_bmp(const gfx_fb_t *fb, uint8_t *out, size_t size, webui_reply_t *reply)
{
    size_t n = fb != NULL ? gfx_bmp_encode(fb, out, size) : 0;
    if (n == 0) {
        reply_error(reply, out, size, 500, "no image");
        return;
    }
    reply->status = 200;
    reply->len = n;
    reply->type = "image/bmp";
}

/* GET /api/preview.bmp?preset=<id>: a saved preset. POST: a presets document from the editor,
 * validated like presets.json, and ?preset=<id> in it (default: its active one). */
static void preview(const char *method, const char *query, const char *body, uint8_t *out, size_t size,
                    webui_reply_t *reply)
{
    EXT_RAM_BSS_ATTR static ui_presets_t doc; /* scratch buffers in PSRAM (AGENTS.md §8) */
    char id[UI_PRESET_ID_LEN] = "", err[96];
    const char *eq = strstr(query, "preset=");
    if (eq != NULL) {
        snprintf(id, sizeof(id), "%.*s", (int)strcspn(eq + 7, "&"), eq + 7);
    }
    if (strcmp(method, "POST") == 0) {
        if (!ui_presets_from_json(body, &doc, err, sizeof(err))) {
            reply_error(reply, out, size, 400, err);
            return;
        }
    } else {
        doc = *app_presets();
    }
    int index = id[0] ? ui_presets_find(&doc, id) : doc.active;
    if (index < 0) {
        reply_error(reply, out, size, 404, "no such preset");
        return;
    }
    gfx_fb_t *fb = preview_fb();
    if (fb != NULL) {
        ui_context_t ctx;
        app_ui_context(&ctx);
        ui_draw_dashboard(fb, &ctx, &doc.presets[index]);
    }
    reply_bmp(fb, out, size, reply);
}

static bool json_number(const cJSON *o, const char *key, double *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v)) {
        return false;
    }
    *out = v->valuedouble;
    return true;
}

/* POST /api/time (spec §10.3): {"epoch": <UTC seconds>, "tz_iana": ..., "tz_posix": ...}, the
 * zone optional. The app task notices the jump and schedules again (main/app.c). */
static void set_time(const char *body, uint8_t *out, size_t size, webui_reply_t *reply)
{
    cJSON *in = cJSON_Parse(body);
    double epoch = 0;
    const cJSON *iana = cJSON_GetObjectItemCaseSensitive(in, "tz_iana");
    const cJSON *posix = cJSON_GetObjectItemCaseSensitive(in, "tz_posix");
    bool zone = cJSON_IsString(iana) && cJSON_IsString(posix);
    if (!json_number(in, "epoch", &epoch) || epoch < MIN_EPOCH || epoch > MAX_EPOCH) {
        reply_error(reply, out, size, 400, "epoch: UTC seconds between 2020 and 2099");
    } else if (zone && (strlen(iana->valuestring) >= SETTINGS_TZ_IANA_LEN || iana->valuestring[0] == '\0' ||
                        strlen(posix->valuestring) >= SETTINGS_TZ_POSIX_LEN || posix->valuestring[0] == '\0')) {
        reply_error(reply, out, size, 400, "tz_iana and tz_posix: a zone name and its POSIX rule");
    } else if (timekeeping_set_utc((time_t)epoch) != ESP_OK) {
        reply_error(reply, out, size, 500, "the RTC didn't take the time");
    } else {
        if (zone) {
            app_ui_set_zone(iana->valuestring, posix->valuestring);
        }
        ESP_LOGI(TAG, "time set from the web UI%s", zone ? ", with the zone" : "");
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", true);
        reply_cjson(reply, out, size, o);
    }
    cJSON_Delete(in);
}

/* GET /api/backup (spec §14.4): the /cfg files as the firmware would save them now. */
static void backup(uint8_t *out, size_t size, webui_reply_t *reply)
{
    EXT_RAM_BSS_ATTR static char settings[2048];
    EXT_RAM_BSS_ATTR static char presets[UI_PRESETS_JSON_MAX];
    netmgr_status_t net;
    netmgr_status(&net);
    backup_file_t files[] = {
        { "settings.json", app_ui_settings_json(settings, sizeof(settings)) ? settings : NULL },
        { "presets.json", ui_presets_to_json(app_presets(), presets, sizeof(presets)) ? presets : NULL },
    };
    reply_text(reply, out, size,
               backup_build(files, 2, net.host, esp_app_get_description()->version, (char *)out, size));
}

/* POST /api/restore: every file is checked before any is replaced. */
static void restore(const char *body, uint8_t *out, size_t size, webui_reply_t *reply)
{
    EXT_RAM_BSS_ATTR static char buf[WEBUI_BODY_MAX];
    EXT_RAM_BSS_ATTR static ui_presets_t presets;
    backup_file_t files[8];
    char err[112];
    int n = backup_split(body, files, 8, buf, sizeof(buf), err, sizeof(err));
    if (n < 0) {
        reply_error(reply, out, size, 400, err);
        return;
    }
    const char *settings = NULL, *presets_text = NULL;
    cJSON *skipped = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        if (strcmp(files[i].name, "settings.json") == 0) {
            settings = files[i].text;
        } else if (strcmp(files[i].name, "presets.json") == 0) {
            presets_text = files[i].text;
        } else {
            cJSON_AddItemToArray(skipped, cJSON_CreateString(files[i].name)); /* from a later firmware */
        }
    }
    char why[96];
    if (settings != NULL && !app_ui_check_settings(settings, why, sizeof(why))) {
        snprintf(err, sizeof(err), "settings.json: %s", why);
    } else if (presets_text != NULL && !ui_presets_from_json(presets_text, &presets, why, sizeof(why))) {
        snprintf(err, sizeof(err), "presets.json: %s", why);
    } else {
        err[0] = '\0';
    }
    if (err[0] != '\0') {
        cJSON_Delete(skipped);
        reply_error(reply, out, size, 400, err);
        return;
    }
    esp_err_t e = settings != NULL ? app_ui_replace_settings(settings, why, sizeof(why)) : ESP_OK;
    if (e == ESP_OK && presets_text != NULL) {
        e = app_ui_replace_presets(&presets);
    }
    if (e != ESP_OK) {
        cJSON_Delete(skipped);
        reply_error(reply, out, size, 500, "saving failed; some files may be restored");
        return;
    }
    ESP_LOGI(TAG, "restored %d file(s)", n - cJSON_GetArraySize(skipped));
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddItemToObject(o, "skipped", skipped);
    reply_cjson(reply, out, size, o);
}

void app_web_api(const char *method, const char *path, const char *query, const char *body, uint8_t *out,
                 size_t size, webui_reply_t *reply)
{
    EXT_RAM_BSS_ATTR static ui_presets_t presets;
    char err[112];
    bool get = strcmp(method, "GET") == 0;
    if (strcmp(path, "/api/status") == 0 && get) {
        get_status(out, size, reply);
    } else if (strcmp(path, "/api/settings") == 0 && get) {
        reply_text(reply, out, size, app_ui_settings_json((char *)out, size));
    } else if (strcmp(path, "/api/settings") == 0 && strcmp(method, "PATCH") == 0) {
        if (app_ui_patch_settings(body, err, sizeof(err)) != ESP_OK) {
            reply_error(reply, out, size, 400, err);
        } else {
            reply_text(reply, out, size, app_ui_settings_json((char *)out, size));
        }
    } else if (strcmp(path, "/api/layouts") == 0 && get) {
        reply_text(reply, out, size, ui_catalog_layouts_json((char *)out, size));
    } else if (strcmp(path, "/api/fields") == 0 && get) {
        ui_context_t ctx;
        app_ui_context(&ctx);
        reply_text(reply, out, size, ui_catalog_fields_json(&ctx, (char *)out, size));
    } else if (strcmp(path, "/api/presets") == 0 && get) {
        reply_text(reply, out, size, ui_presets_to_json(app_presets(), (char *)out, size));
    } else if (strcmp(path, "/api/presets") == 0 && strcmp(method, "PUT") == 0) {
        if (!ui_presets_from_json(body, &presets, err, sizeof(err))) {
            reply_error(reply, out, size, 400, err);
        } else if (app_ui_replace_presets(&presets) != ESP_OK) {
            reply_error(reply, out, size, 500, "presets.json wasn't saved");
        } else {
            reply_text(reply, out, size, ui_presets_to_json(app_presets(), (char *)out, size));
        }
    } else if (strcmp(path, "/api/preview.bmp") == 0 && (get || strcmp(method, "POST") == 0)) {
        preview(method, query, body, out, size, reply);
    } else if (strcmp(path, "/api/screenshot.bmp") == 0 && get) {
        reply_bmp(display_fb(), out, size, reply);
    } else if (strcmp(path, "/api/time") == 0 && strcmp(method, "POST") == 0) {
        set_time(body, out, size, reply);
    } else if (strcmp(path, "/api/backup") == 0 && get) {
        backup(out, size, reply);
    } else if (strcmp(path, "/api/restore") == 0 && strcmp(method, "POST") == 0) {
        restore(body, out, size, reply);
    } else {
        reply_error(reply, out, size, 404, "no such API");
    }
}
```

And the patches:

```diff
diff --git a/main/CMakeLists.txt b/main/CMakeLists.txt
index d4ee902..2cb5a54 100644
--- a/main/CMakeLists.txt
+++ b/main/CMakeLists.txt
@@ -1,5 +1,5 @@
-idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_menu.c" "app_cmds.c"
+idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_menu.c" "app_cmds.c" "app_config.c" "app_web.c"
                        INCLUDE_DIRS "."
-                       PRIV_REQUIRES board console datastore diag display esp_app_format esp_driver_gpio esp_timer
-                                     espcoredump gfx locale nvs_flash power rtc scheduler sensors st7305 storage
-                                     timekeeping ui util)
+                       PRIV_REQUIRES app_update board console datastore diag display esp_app_format
+                                     esp_driver_gpio esp_timer espcoredump gfx json locale netmgr nvs_flash power
+                                     rtc scheduler sensors st7305 storage timekeeping ui util webui)
diff --git a/main/Kconfig.projbuild b/main/Kconfig.projbuild
index ef5f4a2..44f1192 100644
--- a/main/Kconfig.projbuild
+++ b/main/Kconfig.projbuild
@@ -12,13 +12,30 @@ menu "reflbo"
         help
             The name shown for REFLBO_TZ; settings.json keeps both (spec §7).
 
+    config REFLBO_LOCATION_NAME
+        string "Location name"
+        default "Brno"
+        help
+            Default location (spec §1): its name, shown in the web UI. settings.json overrides it.
+
+    config REFLBO_LOCATION_LAT_E4
+        int "Location latitude (1e-4 degrees, north positive)"
+        range -900000 900000
+        default 491951
+
+    config REFLBO_LOCATION_LON_E4
+        int "Location longitude (1e-4 degrees, east positive)"
+        range -1800000 1800000
+        default 166068
+
     config REFLBO_TEMP_OFFSET_C10
         int "SHTC3 temperature offset (0.1 °C)"
         range -100 100
-        default 0
+        default -20
         help
-            Added to every reading; the board warms the sensor (AGENTS.md gotcha 10). Calibrate
-            against a reference thermometer (spec §8).
+            Added to every reading. The board warms the sensor by about 2 °C at all times
+            (AGENTS.md gotcha 10; the owner's measurement, 2026-09-30), so the default is -2.0 °C.
+            Trim it against a reference thermometer in the menu or the web UI (spec §8).
 
     config REFLBO_HUM_OFFSET_PCT10
         int "SHTC3 humidity offset (0.1 %RH)"
diff --git a/main/app.c b/main/app.c
index b2e0220..f438284 100644
--- a/main/app.c
+++ b/main/app.c
@@ -17,11 +17,14 @@
 #include "esp_check.h"
 #include "esp_core_dump.h"
 #include "esp_log.h"
+#include "esp_ota_ops.h"
+#include "esp_system.h"
 #include "esp_timer.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/queue.h"
 #include "freertos/semphr.h"
 #include "freertos/task.h"
+#include "nvs.h"
 #include "nvs_flash.h"
 #include "power.h"
 #include "pcf85063.h"
@@ -29,6 +32,7 @@
 #include "st7305.h"
 #include "sdkconfig.h"
 #include "sensors.h"
+#include "storage.h"
 #include "timekeeping.h"
 #include "ui_screens.h"
 #include "util_snapshot.h"
@@ -43,10 +47,13 @@
 #define TETHER_RECHECK_MS 1000
 #define RETRY_S           300  /* after a failed boot with no PC attached */
 #define SNAP_MAGIC        0x72666c62u /* "rflb" */
-#define SNAP_VERSION      3
+#define SNAP_VERSION      4
 #define PEEK_MS           60000 /* a button during the night shows the dashboard this long (spec §9.1) */
 #define NIGHT_RECHECK_S   60    /* a night sleep with a button held looks again this often (D16) */
 #define CRITICAL_RECHECK_S 600  /* the critical sleep checks again this often if KEY is held */
+#define OTA_VERIFY_MS     60000 /* spec §10.5: a new image is valid after this long without a panic */
+#define RESTART_MS        1500  /* a restart's toast stays this long; it also lets a web reply go out */
+#define RESUME_MAGIC      0x72636667u /* "rcfg" */
 
 #if CONFIG_REFLBO_PANEL_INIT_XIAOZHI
 #define PANEL_VARIANT ST7305_VARIANT_XIAOZHI
@@ -88,10 +95,15 @@ typedef struct {
 _Static_assert(sizeof(app_snapshot_t) <= 4096, "the RTC-RAM snapshot is at most 4 KB (spec §6)");
 
 static RTC_DATA_ATTR app_snapshot_t s_snap;
+/* A restart the web UI asked for (an update, Reboot) comes back in config mode, so the page finds
+ * the device again. RTC_NOINIT survives a software reset; a power-on leaves garbage, hence the magic. */
+static RTC_NOINIT_ATTR uint32_t s_resume_config;
 static QueueHandle_t s_queue;
 static time_t s_next_alarm; /* the RTC alarm: the next minute slot */
 static time_t s_wake_at;    /* the earliest wake: the alarm, or a cycle switch or seconds tick before it */
 static int64_t s_peek_until_ms; /* night: the dashboard shows until then (app_uptime_ms) */
+static bool s_ota_pending;      /* this image came from an upload and is not yet marked valid */
+static bool s_rendered;         /* the first frame went out since boot */
 
 static void IRAM_ATTR on_rtc_int(void *arg)
 {
@@ -124,6 +136,47 @@ esp_err_t app_execute(void (*fn)(void *arg), void *arg)
     return ESP_OK;
 }
 
+esp_err_t app_post(void (*fn)(void *arg), void *arg)
+{
+    app_event_t ev = { .type = EV_CALL, .call = { .fn = fn, .arg = arg, .done = NULL } };
+    return xQueueSend(s_queue, &ev, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
+}
+
+void app_restart(lang_str_t message, bool config_after)
+{
+    s_resume_config = config_after ? RESUME_MAGIC : 0;
+    app_menu_close();
+    app_ui_toast(lang_str(lang_get(app_settings()->language), message));
+    vTaskDelay(pdMS_TO_TICKS(RESTART_MS));
+    esp_restart();
+}
+
+void app_factory_reset(void)
+{
+    app_menu_close();
+    app_ui_toast(lang_str(lang_get(app_settings()->language), LS_T_RESETTING));
+    esp_err_t err = storage_erase();
+    if (err != ESP_OK) {
+        ESP_LOGE(TAG, "erasing storage: %s", esp_err_to_name(err));
+    }
+    static const char *const k_namespaces[] = { "wifi", "secrets", "ctr" };
+    for (size_t i = 0; i < sizeof(k_namespaces) / sizeof(k_namespaces[0]); i++) {
+        nvs_handle_t nvs;
+        if (nvs_open(k_namespaces[i], NVS_READONLY, &nvs) != ESP_OK) {
+            continue; /* never written: nothing to erase */
+        }
+        nvs_close(nvs);
+        if (nvs_open(k_namespaces[i], NVS_READWRITE, &nvs) == ESP_OK) {
+            nvs_erase_all(nvs);
+            nvs_commit(nvs);
+            nvs_close(nvs);
+        }
+    }
+    ESP_LOGW(TAG, "factory reset done; restarting");
+    vTaskDelay(pdMS_TO_TICKS(RESTART_MS));
+    esp_restart();
+}
+
 static void schedule_next(void)
 {
     sched_wake_t wake = app_ui_next_wake(time(NULL));
@@ -200,9 +253,24 @@ static void handle_button(board_button_t button, gesture_t gesture)
         bool held = gesture == GESTURE_LONG;
         app_menu_key(button == BOARD_BUTTON_KEY ? (held ? UI_MENU_KEY_SELECT : UI_MENU_KEY_NEXT)
                                                 : (held ? UI_MENU_KEY_EXIT : UI_MENU_KEY_BACK));
+    } else if (app_config_active()) { /* spec §5.6 */
+        if (button == BOARD_BUTTON_KEY && gesture == GESTURE_SHORT) {
+            app_config_toggle_qr();
+        } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_LONG) {
+            app_config_exit();
+        }
     } else if (app_state()->critical) {
         app_ui_sample(time(NULL)); /* only a recovered battery leaves this screen (spec §8) */
         app_ui_render();
+    } else if (app_ui_first_run()) { /* spec §5.5: KEY leads on to the dashboard or the menu */
+        if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_LONG) {
+            app_config_enter();
+        } else if (button == BOARD_BUTTON_KEY && (gesture == GESTURE_SHORT || gesture == GESTURE_LONG)) {
+            app_ui_end_first_run();
+            if (gesture == GESTURE_LONG) {
+                app_menu_open();
+            }
+        }
     } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_SHORT) {
         app_ui_select(ui_presets_next(app_presets()), true);
         snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_PRESET), preset_name());
@@ -216,9 +284,8 @@ static void handle_button(board_button_t button, gesture_t gesture)
         app_ui_sample(time(NULL));
         app_ui_render();
         ESP_LOGI(TAG, "BOOT short: sensors refreshed");
-    } else {
-        ESP_LOGI(TAG, "%s %s is not bound yet (config mode comes in M4)", board_button_name(button),
-                 board_gesture_name(gesture));
+    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_LONG) {
+        app_config_enter(); /* 3 s on the dashboard (spec §5.6) */
     }
     schedule_next();
 }
@@ -280,7 +347,9 @@ static void handle_event(const app_event_t *ev)
         bool was_valid = timekeeping_valid();
         int64_t before = now_ms();
         ev->call.fn(ev->call.arg);
-        xSemaphoreGive(ev->call.done);
+        if (ev->call.done != NULL) {
+            xSemaphoreGive(ev->call.done);
+        }
         int64_t moved = now_ms() - before;
         if (timekeeping_valid() != was_valid || moved < 0 || moved > 2000) {
             app_clock_moved(moved / 1000); /* `rtc set` and friends */
@@ -439,6 +508,16 @@ static esp_err_t boot(void)
 {
     esp_err_t err = power_init();
     power_wake_t wake = power_boot_wake();
+    bool resume_config = wake == POWER_WAKE_COLD && s_resume_config == RESUME_MAGIC;
+    esp_ota_img_states_t ota = ESP_OTA_IMG_UNDEFINED;
+    s_ota_pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &ota) == ESP_OK &&
+                    ota == ESP_OTA_IMG_PENDING_VERIFY;
+    if (!s_ota_pending) {
+        /* Once: a crash in config mode must not bring it back forever. A new image keeps the flag
+         * until it is valid, so that after a rollback the old one comes back in config mode too
+         * and the web UI can tell. */
+        s_resume_config = 0;
+    }
     if (!routine_wake(wake)) {
         come_alive();
     }
@@ -500,10 +579,33 @@ static esp_err_t boot(void)
         }
         on_tick(!warm || app_state()->critical);
     }
+    s_rendered = true;
+    if (resume_config) {
+        ESP_LOGI(TAG, "restarted from the web UI: config mode again");
+        app_config_enter();
+    }
+    if (s_ota_pending) { /* spec §10.5: awake, without deep sleep, until it has proved itself */
+        ESP_LOGW(TAG, "new firmware: valid after %d s without a panic", OTA_VERIFY_MS / 1000);
+    }
     ESP_LOGI(TAG, "reflbo ready (%s wake%s)", power_wake_name(wake), warm ? ", warm" : "");
     return ESP_OK;
 }
 
+/* Spec §10.5: the panel came up and drew, and a minute passed without a panic. */
+static void check_ota(void)
+{
+    if (s_ota_pending && s_rendered && app_uptime_ms() >= OTA_VERIFY_MS) {
+        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
+        s_ota_pending = false;
+        s_resume_config = 0;
+        if (err == ESP_OK) {
+            ESP_LOGI(TAG, "new firmware marked valid");
+        } else {
+            ESP_LOGE(TAG, "marking the firmware valid: %s", esp_err_to_name(err));
+        }
+    }
+}
+
 static void app_task(void *arg)
 {
     (void)arg;
@@ -515,13 +617,18 @@ static void app_task(void *arg)
         power_hold_awake_ms(GRACE_MS);
     }
     for (;;) {
-        bool pending = uxQueueMessagesWaiting(s_queue) > 0 || board_buttons_busy();
+        /* Config mode and a new image waiting to prove itself keep the chip awake: sleep would
+         * drop Wi-Fi, and a deep-sleep wake would roll the image back (spec §10.5). */
+        bool pending = uxQueueMessagesWaiting(s_queue) > 0 || board_buttons_busy() || app_config_active() ||
+                       s_ota_pending;
         if (err == ESP_OK) {
             check_clock_jump();
+            check_ota();
             int64_t mono = app_uptime_ms();
             if (app_menu_is_open() && mono >= app_menu_deadline_ms()) {
                 app_menu_close(); /* 60 s without input (spec §5.7) */
             }
+            app_config_tick();
             app_ui_toast_expire();
             bool busy = pending || app_menu_is_open() || app_ui_toast_active();
             if (!busy && app_ui_night() && mono >= s_peek_until_ms) {
@@ -553,7 +660,8 @@ static void app_task(void *arg)
             wait_ms = due_ms < wait_ms ? due_ms : wait_ms;
             int64_t mono = app_uptime_ms();
             const int64_t deadlines[] = { app_menu_deadline_ms(), app_ui_toast_until_ms(),
-                                          app_ui_night() ? s_peek_until_ms : 0 };
+                                          app_ui_night() ? s_peek_until_ms : 0, app_config_redraw_ms(),
+                                          s_ota_pending ? OTA_VERIFY_MS : 0 };
             for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++) {
                 if (deadlines[i] != 0 && deadlines[i] - mono < wait_ms) {
                     wait_ms = deadlines[i] - mono;
diff --git a/main/app_cmds.c b/main/app_cmds.c
index 42e5a68..3cb9f86 100644
--- a/main/app_cmds.c
+++ b/main/app_cmds.c
@@ -8,6 +8,7 @@
 #include "esp_console.h"
 #include "esp_log.h"
 #include "lang.h"
+#include "netmgr.h"
 #include "ui_fields.h"
 #include "ui_layout.h"
 
@@ -248,6 +249,50 @@ static int cmd_schedule(int argc, char **argv)
     return diag_on_owner(schedule_body, argc, argv);
 }
 
+/* `wifi status | scan` (spec §15). netmgr is thread-safe: this runs on the console task, so a
+ * scan doesn't hold up the app task. */
+static int cmd_wifi(int argc, char **argv)
+{
+    static const char *const k_usage = "wifi status | wifi scan  (Wi-Fi is on in config mode: `btn boot long`)";
+    if (app_net_init() != ESP_OK) {
+        printf("wifi: the Wi-Fi manager didn't start\n");
+        return 1;
+    }
+    if (argc == 2 && strcmp(argv[1], "status") == 0) {
+        static netmgr_status_t st;
+        static netmgr_list_t saved;
+        static const char *const k_states[] = { "off", "joining", "on a network", "AP only" };
+        netmgr_status(&st);
+        netmgr_networks(&saved);
+        printf("state %s%s\n", k_states[st.state], st.ap_on && st.state != NETMGR_AP ? ", AP too" : "");
+        if (st.state == NETMGR_STATION) {
+            printf("network \"%s\", %s, %d dBm\n", st.ssid, st.ip[0] ? st.ip : "no address yet", st.rssi);
+        }
+        if (st.ap_on) {
+            printf("AP %s, %u client(s)\n", st.ap_ssid, st.ap_clients);
+        }
+        printf("host %s.local\n", st.host);
+        printf("%d saved network(s)%s", saved.count, saved.count ? ":" : "");
+        for (int i = 0; i < saved.count; i++) {
+            printf(" \"%s\"", saved.nets[i].ssid);
+        }
+        printf("\n");
+        return 0;
+    }
+    if (argc == 2 && strcmp(argv[1], "scan") == 0) {
+        static netmgr_ap_t aps[NETMGR_SCAN_MAX];
+        int n = netmgr_scan(aps, NETMGR_SCAN_MAX);
+        if (n == 0) {
+            printf("wifi: nothing found; Wi-Fi is on only in config mode\n");
+        }
+        for (int i = 0; i < n; i++) {
+            printf("%4d dBm %s %s\n", aps[i].rssi, aps[i].open ? "open" : "WPA ", aps[i].ssid);
+        }
+        return 0;
+    }
+    return usage(k_usage);
+}
+
 void app_register_commands(void)
 {
     const esp_console_cmd_t cmds[] = {
@@ -256,6 +301,7 @@ void app_register_commands(void)
         { .command = "night", .help = "night <minutes>: night sleep now (spec §9.1)", .func = &cmd_night },
         { .command = "schedule", .help = "schedule list | on | off | clear | add <HH:MM> preset <id> [days] | "
                                          "add <HH:MM> night <HH:MM> [days]", .func = &cmd_schedule },
+        { .command = "wifi", .help = "wifi status | scan", .func = &cmd_wifi },
     };
     for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
         esp_err_t err = esp_console_cmd_register(&cmds[i]);
diff --git a/main/app_internal.h b/main/app_internal.h
index 78291d0..b60e699 100644
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -12,6 +12,7 @@
 #include "ui_fields.h"
 #include "ui_menu.h"
 #include "ui_preset.h"
+#include "webui.h"
 
 /* The dashboard's state and behaviour (main/app_ui.c, main/app_menu.c). All of it belongs to the
  * app task. */
@@ -26,6 +27,7 @@ typedef struct {
     time_t sched_checked; /* schedule entries up to this time have run (spec §5.4) */
     time_t cold_boot_at;  /* for Info > Uptime; 0 while the clock is unset */
     bool critical;        /* the critical-battery screen is up (spec §8) */
+    bool first_run;       /* settings.json didn't exist at boot: the first-run screen (spec §5.5) */
 } app_ui_state_t;
 
 /* Kconfig settings, the built-in presets and an empty datastore. */
@@ -71,6 +73,21 @@ void app_ui_start_night(time_t until);
 void app_ui_end_night(void);
 bool app_ui_night(void);
 
+/* The first run (spec §5.5): shown until a button leads on; the settings file then exists. */
+bool app_ui_first_run(void);
+void app_ui_end_first_run(void);
+/* settings.json as it would be saved now (GET /api/settings, the backup). */
+size_t app_ui_settings_json(char *out, size_t size);
+bool app_ui_check_settings(const char *json, char *err, size_t err_size);
+/* A whole new settings.json (a restore) or a merge patch (PATCH /api/settings): validated, saved
+ * and applied at once. ESP_ERR_INVALID_ARG with the reason in `err` if it doesn't parse. */
+esp_err_t app_ui_replace_settings(const char *json, char *err, size_t err_size);
+esp_err_t app_ui_patch_settings(const char *patch, char *err, size_t err_size);
+/* A validated presets document (PUT /api/presets, a restore): saved and shown. */
+esp_err_t app_ui_replace_presets(const ui_presets_t *presets);
+/* The phone's zone (POST /api/time): saved and applied. */
+void app_ui_set_zone(const char *iana, const char *posix);
+
 /* The on-device menu (main/app_menu.c, spec §5.7). The dashboard's gesture timings (spec §5.6)
  * live there too, since the menu swaps them for its own. */
 extern const gesture_config_t k_app_dashboard_buttons[BOARD_BUTTON_COUNT];
@@ -81,6 +98,28 @@ void app_menu_key(ui_menu_key_t key);
 void app_menu_render(void);
 int64_t app_menu_deadline_ms(void); /* the menu closes at this time without input */
 
+/* Config mode (main/app_config.c, spec §10.2). */
+esp_err_t app_net_init(void); /* the Wi-Fi manager, started on first use */
+void app_config_enter(void);
+void app_config_exit(void);
+bool app_config_active(void);
+void app_config_toggle_qr(void); /* KEY short: the other QR code */
+void app_config_tick(void);      /* the timeout and the minutes left; call from the app loop */
+int64_t app_config_deadline_ms(void); /* app_uptime_ms() at which config mode ends; 0 when off */
+int64_t app_config_redraw_ms(void);   /* when the minutes left change next; 0 when off */
+void app_config_draw(gfx_fb_t *fb, const lang_t *lang);
+/* The API routes the app answers (main/app_web.c); a webui_api_fn. */
+void app_web_api(const char *method, const char *path, const char *query, const char *body, uint8_t *out,
+                 size_t size, webui_reply_t *reply);
+
+/* Implemented in main/app.c. app_post() runs fn(arg) on the app task later, without waiting:
+ * for netmgr and webui, whose tasks must not block on the app. */
+esp_err_t app_post(void (*fn)(void *arg), void *arg);
+/* Shows `message` for a moment, then restarts the chip; `config_after` comes back in config mode. */
+void app_restart(lang_str_t message, bool config_after);
+/* Spec §14.4: erases storage and the NVS namespaces wifi, secrets and ctr, keeps sys, restarts. */
+void app_factory_reset(void);
+
 /* Implemented in main/app.c for the menu: the clock was set, and moved by delta_s. */
 void app_clock_moved(int64_t delta_s);
 int64_t app_uptime_ms(void); /* milliseconds since boot, unmoved by clock changes: toasts, menu */
diff --git a/main/app_menu.c b/main/app_menu.c
index 9674b7a..16164ef 100644
--- a/main/app_menu.c
+++ b/main/app_menu.c
@@ -11,7 +11,7 @@
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "lang.h"
-#include "nvs.h"
+#include "netmgr.h"
 #include "power.h"
 #include "st7305.h"
 #include "storage.h"
@@ -56,7 +56,7 @@ static const char *s_zone_names[ZONES_MAX];
 static const char *s_language_names[LANGUAGE_COUNT];
 static char s_rate_text[RATE_COUNT][12];
 static const char *s_rates[RATE_COUNT];
-static char s_info[5][80];
+static char s_info[7][80];
 
 static const lang_t *lang(void)
 {
@@ -197,49 +197,21 @@ static void build_model(void)
     char mb[12];
     lang_format_decimal(l, (long)(esp_get_free_heap_size() / (1024 * 1024 / 10)), 1, mb, sizeof(mb));
     snprintf(s_info[4], sizeof(s_info[4]), "%s MB", mb);
+    netmgr_status_t net = { 0 };
+    if (app_config_active()) {
+        netmgr_status(&net); /* Wi-Fi is off otherwise */
+    }
+    snprintf(s_info[5], sizeof(s_info[5]), "%s",
+             net.ip[0] ? net.ip : net.state == NETMGR_AP ? NETMGR_AP_IP : "\xE2\x80\x94");
+    snprintf(s_info[6], sizeof(s_info[6]), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
+             mac[5]);
     const ui_menu_item_t info_items[] = { UI_MI_INFO_BATTERY, UI_MI_INFO_FIRMWARE, UI_MI_INFO_DEVICE,
-                                          UI_MI_INFO_UPTIME, UI_MI_INFO_MEMORY };
-    for (int i = 0; i < 5; i++) {
+                                          UI_MI_INFO_UPTIME, UI_MI_INFO_MEMORY, UI_MI_INFO_IP, UI_MI_INFO_MAC };
+    for (int i = 0; i < 7; i++) {
         m->info[info_items[i]] = s_info[i];
     }
 }
 
-/* Shows `message` over the dashboard for a moment, then restarts the chip. */
-static void restart_after(lang_str_t message)
-{
-    app_menu_close();
-    app_ui_toast(lang_str(lang(), message));
-    vTaskDelay(pdMS_TO_TICKS(1500));
-    esp_restart();
-}
-
-/* Spec §14.4: erases storage and the NVS namespaces wifi, secrets and ctr; keeps sys. */
-static void factory_reset(void)
-{
-    app_menu_close();
-    app_ui_toast(lang_str(lang(), LS_T_RESETTING));
-    esp_err_t err = storage_erase();
-    if (err != ESP_OK) {
-        ESP_LOGE(TAG, "erasing storage: %s", esp_err_to_name(err));
-    }
-    static const char *const k_namespaces[] = { "wifi", "secrets", "ctr" };
-    for (size_t i = 0; i < sizeof(k_namespaces) / sizeof(k_namespaces[0]); i++) {
-        nvs_handle_t nvs;
-        if (nvs_open(k_namespaces[i], NVS_READONLY, &nvs) != ESP_OK) {
-            continue; /* never written: nothing to erase */
-        }
-        nvs_close(nvs);
-        if (nvs_open(k_namespaces[i], NVS_READWRITE, &nvs) == ESP_OK) {
-            nvs_erase_all(nvs);
-            nvs_commit(nvs);
-            nvs_close(nvs);
-        }
-    }
-    ESP_LOGW(TAG, "factory reset done; restarting");
-    vTaskDelay(pdMS_TO_TICKS(1500));
-    esp_restart();
-}
-
 static void set_zone(int index)
 {
     int count = 0;
@@ -328,10 +300,30 @@ static void apply(const ui_menu_intent_t *in)
         break;
     }
     case UI_MENU_ACTION:
-        if (in->item == UI_MI_REBOOT) {
-            restart_after(LS_T_REBOOTING);
-        } else if (in->item == UI_MI_FACTORY_RESET) {
-            factory_reset();
+        switch (in->item) {
+        case UI_MI_CONFIG_MODE:
+            app_config_enter(); /* closes the menu */
+            return;
+        case UI_MI_FORGET_NETWORKS:
+            if (app_net_init() == ESP_OK && netmgr_forget_all() == ESP_OK) {
+                app_menu_close();
+                app_ui_toast(lang_str(lang(), LS_T_NETWORKS_FORGOTTEN));
+            }
+            return;
+        case UI_MI_RESET_PASSWORD:
+            if (webui_reset_password() == ESP_OK) {
+                app_menu_close();
+                app_ui_toast(lang_str(lang(), LS_T_PASSWORD_CLEARED));
+            }
+            return;
+        case UI_MI_REBOOT:
+            app_restart(LS_T_REBOOTING, false);
+            break;
+        case UI_MI_FACTORY_RESET:
+            app_factory_reset();
+            break;
+        default:
+            break;
         }
         break;
     case UI_MENU_CLOSE:
diff --git a/main/app_ui.c b/main/app_ui.c
index bab3f0c..d3f06f9 100644
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -3,6 +3,7 @@
 
 #include "app_internal.h"
 #include "display.h"
+#include "esp_attr.h"
 #include "esp_log.h"
 #include "lang.h"
 #include "power.h"
@@ -20,8 +21,9 @@
 static const char *TAG = "app_ui";
 
 static app_ui_state_t s;
-static char s_file[UI_PRESETS_JSON_MAX];
-static char s_settings_base[2048]; /* settings.json as read: keys this firmware doesn't know stay */
+/* The config files' text: scratch buffers in PSRAM (AGENTS.md §8). */
+EXT_RAM_BSS_ATTR static char s_file[UI_PRESETS_JSON_MAX];
+EXT_RAM_BSS_ATTR static char s_settings_base[2048]; /* settings.json as read: unknown keys stay */
 static char s_err[96];
 static char s_toast[64];
 static int64_t s_toast_until_ms;
@@ -36,7 +38,10 @@ static void default_settings(settings_t *out)
         .hum_offset_pct100 = CONFIG_REFLBO_HUM_OFFSET_PCT10 * 10,
         .display_every_min = CONFIG_REFLBO_DISPLAY_UPDATE_MIN,
         .lpm_quarter_hz = 4, /* 1 Hz (D12) */
+        .lat_e4 = CONFIG_REFLBO_LOCATION_LAT_E4,
+        .lon_e4 = CONFIG_REFLBO_LOCATION_LON_E4,
     };
+    snprintf(out->place, sizeof(out->place), "%s", CONFIG_REFLBO_LOCATION_NAME);
     snprintf(out->tz_posix, sizeof(out->tz_posix), "%s", CONFIG_REFLBO_TZ);
     snprintf(out->tz_iana, sizeof(out->tz_iana), "%s", CONFIG_REFLBO_TZ_NAME);
 }
@@ -69,19 +74,20 @@ static bool parse_presets(const char *text, void *ctx)
     return ok;
 }
 
-/* True if the file had to fall back to the defaults (it existed but nothing in it parsed). */
-static bool load_one(const char *path, char *buf, size_t buf_size, storage_parse_t parse, void *target, size_t size,
-                     const void *defaults)
+/* ESP_OK, ESP_ERR_NOT_FOUND (no file yet), or ESP_ERR_INVALID_RESPONSE: it existed, but nothing
+ * in it parsed, so the defaults are in use. */
+static esp_err_t load_one(const char *path, char *buf, size_t buf_size, storage_parse_t parse, void *target,
+                          size_t size, const void *defaults)
 {
     bool from_backup = false;
     esp_err_t err = storage_load(path, buf, buf_size, parse, target, &from_backup);
     if (err == ESP_OK) {
         ESP_LOGI(TAG, "%s loaded%s", path, from_backup ? " from the backup" : "");
-        return false;
+        return ESP_OK;
     }
     memcpy(target, defaults, size); /* a failed parse may have left it half-written */
     ESP_LOGI(TAG, "%s: %s; using defaults", path, err == ESP_ERR_NOT_FOUND ? "not there yet" : "invalid");
-    return err != ESP_ERR_NOT_FOUND;
+    return err == ESP_ERR_NOT_FOUND ? err : ESP_ERR_INVALID_RESPONSE;
 }
 
 void app_ui_load(void)
@@ -99,14 +105,15 @@ void app_ui_load(void)
         ESP_LOGE(TAG, "storage: %s; settings and presets use their defaults", esp_err_to_name(err));
         return;
     }
-    bool fell_back = load_one(STORAGE_SETTINGS_PATH, s_settings_base, sizeof(s_settings_base), parse_settings,
-                              &s.settings, sizeof(s.settings), &settings_defaults);
-    if (fell_back) {
+    esp_err_t settings = load_one(STORAGE_SETTINGS_PATH, s_settings_base, sizeof(s_settings_base), parse_settings,
+                                  &s.settings, sizeof(s.settings), &settings_defaults);
+    if (settings != ESP_OK) {
         s_settings_base[0] = '\0'; /* nothing worth keeping in it */
     }
-    fell_back |= load_one(STORAGE_PRESETS_PATH, s_file, sizeof(s_file), parse_presets, &s.presets, sizeof(s.presets),
-                          &presets_defaults);
-    if (fell_back) { /* spec §14.3: say so */
+    s.first_run = settings == ESP_ERR_NOT_FOUND; /* a new board, or a factory reset (spec §5.5) */
+    esp_err_t presets = load_one(STORAGE_PRESETS_PATH, s_file, sizeof(s_file), parse_presets, &s.presets,
+                                 sizeof(s.presets), &presets_defaults);
+    if (settings == ESP_ERR_INVALID_RESPONSE || presets == ESP_ERR_INVALID_RESPONSE) { /* spec §14.3: say so */
         app_ui_toast(lang_str(lang_get(s.settings.language), LS_T_DEFAULTS));
     }
 }
@@ -196,6 +203,10 @@ void app_ui_render(void)
     app_ui_context(&ctx);
     if (s.critical) {
         ui_draw_critical(fb, &ctx);
+    } else if (app_config_active()) {
+        app_config_draw(fb, ctx.lang);
+    } else if (s.first_run) {
+        ui_draw_first_run(fb, &ctx);
     } else {
         ui_draw_dashboard(fb, &ctx, &s.presets.presets[s.presets.active]);
     }
@@ -260,6 +271,10 @@ void app_ui_sample(time_t now)
     } else {
         ESP_LOGW(TAG, "SHTC3: %s; keeping the last reading", esp_err_to_name(err));
     }
+    if (app_config_active()) { /* spec §8: the battery only while idle; the radio's load pulls VBAT down */
+        ds_take_changes(&s.ds);
+        return;
+    }
     err = sensors_sample_battery(now);
     if (err == ESP_OK) {
         sensors_battery_t bat = sensors_battery(now);
@@ -321,7 +336,7 @@ esp_err_t app_ui_save_settings(void)
             s_settings_base[0] = '\0';
         }
     }
-    static char out[sizeof(s_settings_base)];
+    EXT_RAM_BSS_ATTR static char out[sizeof(s_settings_base)];
     size_t n = settings_to_json(&s.settings, s_settings_base[0] ? s_settings_base : NULL, out, sizeof(out));
     if (n == 0) {
         ESP_LOGE(TAG, "settings.json does not fit %u bytes", (unsigned)sizeof(out));
@@ -461,3 +476,84 @@ sched_wake_t app_ui_next_wake(time_t now)
     };
     return scheduler_next_wake(&in);
 }
+
+bool app_ui_first_run(void)
+{
+    return s.first_run;
+}
+
+void app_ui_end_first_run(void)
+{
+    if (!s.first_run) {
+        return;
+    }
+    s.first_run = false;
+    app_ui_save_settings(); /* the file now exists, so the first run doesn't come back */
+    app_ui_render();
+}
+
+size_t app_ui_settings_json(char *out, size_t size)
+{
+    return settings_to_json(&s.settings, s_settings_base[0] ? s_settings_base : NULL, out, size);
+}
+
+bool app_ui_check_settings(const char *json, char *err, size_t err_size)
+{
+    static settings_t parsed;
+    return settings_from_json(json, &s.settings, &parsed, err, err_size);
+}
+
+/* The new settings take effect everywhere: time zone, offsets, panel rate, slots and the text. */
+static void settings_changed(void)
+{
+    app_ui_apply_settings();
+    app_ui_sample(time(NULL));
+    app_clock_moved(0); /* new slots or a new zone: schedule again, and redraw */
+}
+
+esp_err_t app_ui_replace_settings(const char *json, char *err, size_t err_size)
+{
+    static settings_t parsed;
+    if (!settings_from_json(json, &s.settings, &parsed, err, err_size)) {
+        return ESP_ERR_INVALID_ARG;
+    }
+    size_t n = strlen(json);
+    if (n >= sizeof(s_settings_base)) {
+        snprintf(err, err_size, "settings.json is larger than %u bytes", (unsigned)sizeof(s_settings_base) - 1);
+        return ESP_ERR_INVALID_SIZE;
+    }
+    s.settings = parsed;
+    memcpy(s_settings_base, json, n + 1); /* keys this firmware doesn't know stay, as in the file */
+    esp_err_t e = app_ui_save_settings();
+    settings_changed();
+    return e;
+}
+
+esp_err_t app_ui_patch_settings(const char *patch, char *err, size_t err_size)
+{
+    EXT_RAM_BSS_ATTR static char merged[sizeof(s_settings_base)];
+    EXT_RAM_BSS_ATTR static char base[sizeof(s_settings_base)];
+    if (app_ui_settings_json(base, sizeof(base)) == 0 ||
+        settings_patch(base, patch, merged, sizeof(merged), err, err_size) == 0) {
+        return ESP_ERR_INVALID_ARG;
+    }
+    return app_ui_replace_settings(merged, err, err_size);
+}
+
+esp_err_t app_ui_replace_presets(const ui_presets_t *presets)
+{
+    s.presets = *presets;
+    s.cycle_at = 0;              /* the next tick starts the cycle interval */
+    s.sched_checked = time(NULL); /* entries don't run late for a new schedule */
+    esp_err_t err = app_ui_save_presets();
+    app_ui_render();
+    return err;
+}
+
+void app_ui_set_zone(const char *iana, const char *posix)
+{
+    snprintf(s.settings.tz_iana, sizeof(s.settings.tz_iana), "%s", iana);
+    snprintf(s.settings.tz_posix, sizeof(s.settings.tz_posix), "%s", posix);
+    app_ui_save_settings();
+    settings_changed();
+}
diff --git a/sdkconfig.defaults b/sdkconfig.defaults
index 3fc893e..cd07cd0 100644
--- a/sdkconfig.defaults
+++ b/sdkconfig.defaults
@@ -14,6 +14,8 @@ CONFIG_SPIRAM=y
 CONFIG_SPIRAM_MODE_OCT=y
 CONFIG_SPIRAM_SPEED_80M=y
 CONFIG_SPIRAM_MEMTEST=n
+# Large static scratch buffers (EXT_RAM_BSS_ATTR) go to PSRAM: Wi-Fi needs the internal RAM (M4)
+CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y
 
 # Console and logs on the USB-Serial-JTAG port
 CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
@@ -38,3 +40,13 @@ CONFIG_BOOTLOADER_LOG_LEVEL_WARN=y
 # once it knows the board stays awake, so INFO stays compiled in.
 CONFIG_LOG_DEFAULT_LEVEL_WARN=y
 CONFIG_LOG_MAXIMUM_LEVEL_INFO=y
+
+# OTA with rollback (spec §10.5): an uploaded image boots as "pending" and must mark itself valid,
+# or the bootloader goes back to the previous one at the next reset.
+CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
+
+# Web configurator (spec §10.3): phone browsers send more than the default 512 bytes of headers,
+# and open several connections at once. The server takes 7 sockets plus 3 of its own, the captive
+# DNS one more: the default 10 sockets run out, and accept() fails with ENFILE.
+CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
+CONFIG_LWIP_MAX_SOCKETS=16
```

- [ ] **Step 2: Build.** `sdkconfig.defaults` changed, so `sdkconfig` goes first.

Run: `rm -f sdkconfig && tools/idf.sh build && tools/idf.sh size && ctest --test-dir build-host --output-on-failure`
Expected: the firmware builds without warnings, about 1.3 MB of the 4 MB slot; `size` shows about 150 KB of DIRAM used; all 43 host tests pass.

- [ ] **Step 3: Commit.**

```bash
git add main sdkconfig.defaults
git commit -m "feat(app): add config mode, the web API, the first run and OTA with rollback"
```

- [ ] **Step 4: Check config mode on the board (M4 acceptance, level 3).** Confirm the port (Global Constraints), flash, and capture a boot:

```bash
tools/idf.sh -p /dev/cu.usbmodem2101 flash
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd reboot --until "reflbo ready" -t 20 -o captures/m4-boot.log
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "btn boot long"
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "wifi status" --cmd heap
tools/idf.sh exec python tools/screenshot.py -p /dev/cu.usbmodem2101 -o captures/m4-config.png
```

Expected:
- `reflbo ready (cold wake)`, with no `E` lines.
- `wifi status`: `state AP only`, `AP reflbo-bb94, 0 client(s)`, `host reflbo-bb94.local` and the saved networks (none on this board).
- `heap`: at least 100 KB of internal RAM free.
- The screenshot is the config screen with the join code, the network, its password and `192.168.4.1` (`screen_config_ap_en`, with the board's own password).

KEY switches the code, then back:

```bash
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "btn key short"
tools/idf.sh exec python tools/screenshot.py -p /dev/cu.usbmodem2101 -o captures/m4-config-url.png
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "btn key short"
```

Expected: the screenshot shows "Scan to open" and "Open in a browser: 192.168.4.1".

- [ ] **Step 5: Check the portal and the API from this Mac.** Join the board's network with the password on its screen, then (the web password below is a throwaway, cleared in Step 9):

```bash
networksetup -setairportnetwork en0 reflbo-bb94 <the password on the screen>
ifconfig en0 | grep 'inet '; route -n get default | grep interface
B=http://192.168.4.1; J='Content-Type: application/json'
curl -s $B/api/auth; echo
curl -s -o /dev/null -w "%{http_code} -> %{redirect_url}\n" -H "Host: captive.apple.com" $B/hotspot-detect.html
curl -s -o /dev/null -w "%{http_code} -> %{redirect_url}\n" -H "Host: connectivitycheck.gstatic.com" $B/generate_204
dig +short @192.168.4.1 example.com; dscacheutil -q host -a name reflbo-bb94.local
curl -s -X POST -d 'password=x' $B/api/auth/setup; echo
curl -s -H "$J" -d '{"password":"short"}' $B/api/auth/setup; echo
curl -s -c jar -H "$J" -d '{"password":"m4-check-pass"}' $B/api/auth/setup; echo
for p in status settings layouts fields presets ota/status backup; do
  curl -s -b jar -o /dev/null -w "$p %{http_code} %{size_download}\n" $B/api/$p
done
curl -s -b jar -o preview.bmp -w "preview %{http_code} %{content_type}\n" "$B/api/preview.bmp?preset=indoor"
curl -s -b jar -o shot.bmp -w "screenshot %{http_code} %{content_type}\n" $B/api/screenshot.bmp
```

Expected:
- `en0` has `192.168.4.2` or so, and the default route stays on `en8`.
- `/api/auth`: `"password_set":false`, `"on_ap":true`.
- Both probes: `302 -> http://192.168.4.1/`. `dig` answers `192.168.4.1`; the `.local` name resolves to it.
- The form post: `{"error":"send application/json"}`; the short password: `{"error":"the password needs 8-64 characters"}`; the setup `{"ok":true}` after about 2 s.
- Every route `200`; the BMPs `image/bmp`, 15 662 bytes. `sips -s format png preview.bmp --out preview.png` shows the Indoor grid with live values.

Changing things, and a test of a network out of reach:

```bash
curl -s -b jar -X PATCH -H "$J" -d '{"location":{"name":"Brno","lat":49.1951,"lon":16.6068}}' $B/api/settings | head -c 80; echo
curl -s -b jar -X PATCH -H "$J" -d '[1]' $B/api/settings; echo
curl -s -b jar -o p.json $B/api/presets && curl -s -b jar -X PUT -H "$J" --data-binary @p.json -o /dev/null -w "put %{http_code}\n" $B/api/presets
curl -s -b jar -o b.json $B/api/backup && curl -s -b jar -H "$J" --data-binary @b.json $B/api/restore; echo
curl -s -b jar -H "$J" -d '{"reflbo_backup":1,"files":{"presets.json":{"schema":1,"presets":[]}}}' $B/api/restore; echo
curl -s -b jar -H "$J" -d "{\"epoch\": $(date +%s)}" $B/api/time; echo
for t in 1 2 3; do
  curl -s -b jar -H "$J" -d "{\"ssid\":\"no-such-network-$t\",\"password\":\"12345678\"}" -w " %{http_code}\n" $B/api/wifi/networks
  sleep 5; curl -s -b jar $B/api/wifi/networks; echo
done
ifconfig en0 | grep 'inet '
rm -f p.json b.json preview.bmp preview.png shot.bmp
```

Expected:
- The patch returns the settings; `[1]` gives `{"error":"the patch must be a JSON object"}`.
- `put 200`; the restore `{"ok":true,"skipped":[]}`; the empty presets `{"error":"presets.json: presets must hold 1-16 entries"}`.
- The time `{"ok":true}`.
- Each test answers `{"result":"testing"} 202` at once, and 5 s later reports `"test":{"ssid":"no-such-network-N","result":"not found"}`. The Mac is still on `192.168.4.x` afterwards (gotcha 26).

- [ ] **Step 6: Check the pages in a browser.** Start headless Chrome as an argent Chromium target (it opens no window):

```bash
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --remote-debugging-port=9222 \
  --user-data-dir=<scratch>/chrome --window-size=500,900 http://192.168.4.1/ &
```

With argent (`list-devices` shows `chromium-cdp-9222`; `describe` before every tap): log in with the throwaway password (tap "Log in"; an Enter sent over CDP doesn't submit), then open every page: Status, Wi-Fi, Location & time, Device, Presets, Firmware and Backup. On Presets, switch Home's layout to Focus and back.

Expected:
- A wrong password shows "Wrong password."; the right one opens Status with the device's screen.
- Every page loads without an error card; the Wi-Fi page lists the networks in sight.
- The preview redraws in the Focus layout within a second, with the device's live values; "Undo changes" brings back Classic.
- The device log shows no `httpd_accept_conn` errors (gotcha 25).

- [ ] **Step 7: Check an update and a rollback.** Upload the build that runs, then a copy that crashes after 25 s:

```bash
cp build/reflbo.bin <scratch>/good.bin
curl -s -b jar -H 'Content-Type: application/octet-stream' --data-binary @<scratch>/good.bin $B/api/ota; echo
```

Wait until the device is back (about 20 s; rejoin its network if the Mac left it), log in again (sessions end with a restart), and read `/api/ota/status` twice, 60 s apart.

Expected:
- `{"ok":true,"version":"…"}` in about 12 s; the log shows `firmware upload: … into ota_1` and, after the restart, `restarted from the web UI: config mode again`.
- First `"partition":"ota_1","pending":true`, then `"pending":false`; the log has `new firmware marked valid`.

Now the crashing copy. Add these three lines at the top of `check_ota()` in `main/app.c`, build, keep the image, and restore the file. Never commit them:

```c
    if (app_uptime_ms() > 25000) { /* ROLLBACK TEST ONLY: never commit */
        abort();
    }
```

```bash
tools/idf.sh build && cp build/reflbo.bin <scratch>/crash.bin && git checkout main/app.c && tools/idf.sh build
curl -s -b jar -H 'Content-Type: application/octet-stream' --data-binary @<scratch>/crash.bin $B/api/ota; echo
```

Wait about 60 s (the crash image runs 25 s, panics, and the bootloader goes back), rejoin if needed, log in, and read `/api/ota/status`; then open Firmware in the browser.

Expected:
- While the crash image runs: `"partition":"ota_0","pending":true`.
- Afterwards: `"partition":"ota_1","pending":false,"rolled_back_from":"ota_0"`, with the device back in config mode; the Firmware page says the update in ota_0 didn't work.
- The next boot that comes alive warns of a core dump in flash: the test's abort. Leave it; it is harmless.

- [ ] **Step 8: Check the idle timeout, deep sleep and the menu.** Park the browser (`open-url about:blank`), send no requests for 11 minutes, and ask the console at 9 min 30 s and at 11 min:

```bash
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "wifi status"
```

Expected: `state AP only` at 9 min 30 s; `state off` at 11 min, although the Mac is still joined (gotcha 27).

Config mode holds off even a forced sleep (`sleep test` sleeps while tethered; config mode's hold beats it):

```bash
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "btn boot long"
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "sleep test light 1"
sleep 30
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "sleep stats" --cmd "wifi status"
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "btn boot long"
sleep 70
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "sleep stats"
```

Expected:
- After 30 s in config mode: `sleep: 0 light, 0 deep`, `test cycles left 1`, and `state AP only`.
- Once BOOT held ends config mode, the forced light sleep runs: the console stalls until the next minute, then `sleep stats` shows `1 light` and `test cycles left 0`.

The menu's Wi-Fi section:

```bash
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "btn key long"
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "btn key short"
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "btn key long"
tools/idf.sh exec python tools/screenshot.py -p /dev/cu.usbmodem2101 -o captures/m4-menu-wifi.png
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "btn key long"
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "wifi status"
tools/idf.sh exec python tools/devlog.py -p /dev/cu.usbmodem2101 --cmd "btn boot long"
```

Expected: the screenshot matches `screen_menu_wifi_en`; KEY long on Config mode starts it (`state AP only`), and BOOT held ends it.

- [ ] **Step 9: Put the board back.** Clear the throwaway web password (Menu ▸ Wi-Fi ▸ Reset web password: KEY long, KEY short, KEY long, KEY short ×2, KEY long, then KEY long to confirm), check that `/api/auth` would say `"password_set":false` next time, and restore the Mac:

```bash
networksetup -removepreferredwirelessnetwork en0 reflbo-bb94
rm -f jar; ifconfig en0 | grep 'inet '; route -n get default | grep interface
```

Expected: the log shows `web password reset` and the toast "Web password cleared"; the Mac is back on its own network with the default route on `en8`. Stop the headless Chrome.

---

### Task 11: Documentation

**Files:**
- Modify: `AGENTS.md`, `docs/specs/2026-09-25-firmware-design.md`, `THIRD_PARTY.md`

**Interfaces:** none. This task records what Tasks 1–10 built, as AGENTS.md rule 6 requires.
- **AGENTS.md**
  - §2: the status.
  - §3.4: gotchas 25 (sockets), 26 (the AP's channel), 27 (probes) and 28 (a pending image and deep sleep).
  - §5.1: the new `sdkconfig.defaults` entries.
  - §6: config mode and `wifi` on the console, `gen_zones.py`, web UI checks from this Mac, headless Chrome, and zsh.
  - §7: the web UI's BMPs and the console list.
- **Spec r15**: the components (§3.1); the menu as built (§5.7); the Wi-Fi manager (§10.1); config mode (§10.2); the API (§10.3); the password (§10.4); OTA (§10.5); NVS keys (§14.2); `location.*` and the merge patch (§14.3); the backup bundle (§14.4); `wifi` (§15); the host tests (§17); the revision row (§21).
- **THIRD_PARTY.md**: qrcodegen and the tz database.

- [ ] **Step 1: Apply the edits.** Save this script outside the repository (a scratch directory) and run it from the repo root. Each edit must match exactly once, or the script stops and writes nothing for that file.

```python
"""M4 documentation updates (Task 11). Every edit must match exactly once."""
import pathlib
import sys

EDITS = {
    "AGENTS.md": [
        # §2: status
        ("until the web UI edits schedules (M4). M4 is next; its plan gets written before it starts.",
         "or from a schedule, which the web UI now edits. M4 is built: the Wi-Fi manager with the device's own "
         "network and captive portal, config mode with its QR screen, the web configurator with its API and a live "
         "preset preview, setting the time from a phone, and firmware updates with rollback. M4 is done once the "
         "owner has set up Wi-Fi from a phone and updated the firmware from the page (Owner acceptance in the M4 "
         "plan)."),
        # §3.4: gotchas 25-28
        ("Night sleep uses both (spec §9.1); `panel sleep` and `panel wake` try them by hand.\n",
         "Night sleep uses both (spec §9.1); `panel sleep` and `panel wake` try them by hand.\n"
         "25. **lwIP has 10 sockets by default: too few for a browser.** The web server takes 7 plus 3 of its own, "
         "the captive DNS 1, and a browser opens several connections at once. At M4 `accept()` then failed with "
         "errno 23 (ENFILE) and Chrome showed an empty response. `CONFIG_LWIP_MAX_SOCKETS=16`.\n"
         "26. **The AP shares the radio with the station, and their channel.** Joining a network on another channel "
         "moves the AP there and drops the phones on it; calling `esp_wifi_set_config(WIFI_IF_AP, …)` on a running "
         "AP drops them too (seen at M4). So netmgr starts the AP on the channel of the network the owner will likely "
         "pick (spec §10.1), leaves a running AP alone, and scans before a test, so a network out of reach never "
         "moves it.\n"
         "27. **A phone on the AP keeps probing for the internet** (`captive.apple.com`, `connectivitycheck.gstatic."
         "com`), and the captive DNS sends those probes to the device. Counting them as use kept config mode on for "
         "good; only the device's own pages and API count.\n"
         "28. **A firmware image uploaded over OTA stays pending until it marks itself valid**, and any reset while "
         "it is pending rolls it back. A deep-sleep wake is a reset, so the board doesn't deep-sleep until the image "
         "is valid (spec §10.5).\n"),
        # §5.1: sdkconfig
        ("`tasks` statistics; app rollback *(planned, M4)*.",
         "`tasks` statistics; app rollback; 16 lwIP sockets and 1 KB request headers for the web server (gotcha 25); "
         "large static buffers in PSRAM (`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` with `EXT_RAM_BSS_ATTR`)."),
        # §6: commands
        ('tools/idf.sh exec python tools/devlog.py --cmd "night 60"        # night sleep now; the console drops\n',
         'tools/idf.sh exec python tools/devlog.py --cmd "night 60"        # night sleep now; the console drops\n'
         'tools/idf.sh exec python tools/devlog.py --cmd "btn boot long"   # config mode: Wi-Fi on, the AP password on screen\n'
         'tools/idf.sh exec python tools/devlog.py --cmd "wifi status" --cmd "wifi scan"\n'
         'python3 tools/gen_zones.py                 # regenerate web/zones.js from this Mac\'s tz database\n'),
        ("- Power measurements follow the USB power-meter method in spec §9.4. Record them in `docs/power.md`.",
         "- Power measurements follow the USB power-meter method in spec §9.4. Record them in `docs/power.md`.\n"
         "- **Web UI checks from this Mac** (owner, 2026-09-30).\n"
         "  - Join the board's AP: `networksetup -setairportnetwork en0 reflbo-XXXX <password from the screen>`. "
         "Wi-Fi comes after the USB Ethernet in the service order, so the Mac's internet stays up.\n"
         "  - Log in with `curl -c jar -H 'Content-Type: application/json' -d '{\"password\":\"…\"}' "
         "http://192.168.4.1/api/auth/login` and pass `-b jar` to the other calls. `--data-binary @build/reflbo.bin` "
         "with `Content-Type: application/octet-stream` on `/api/ota` updates the firmware.\n"
         "  - Afterwards run `networksetup -removepreferredwirelessnetwork en0 reflbo-XXXX`, and clear any web "
         "password a check set (Menu ▸ Wi-Fi ▸ Reset web password), so the owner's first visit chooses theirs.\n"
         "  - Headless Chrome with `--remote-debugging-port=9222` is an argent Chromium target. Its `--screenshot` "
         "renders at least 500 px wide, Chrome's smallest window. A key press of Enter sent over CDP doesn't submit "
         "a form: tap the button.\n"
         "- The Bash tool runs zsh: a variable holding several flags isn't split into words, and a word that starts "
         "with `=` is looked up as a command. Write flags out."),
        # §7: screenshots and the console
        ("The web UI will serve `/api/screenshot.bmp` *(planned, M4)*.",
         "In config mode the web UI serves `/api/screenshot.bmp`, and `/api/preview.bmp` renders any preset with "
         "live data."),
        ("`schedule list|on|off|clear|add <HH:MM> preset <id> [days]|add <HH:MM> night <HH:MM> [days]`. Planned: "
         "`wifi status|scan`, `sync now`, `audio tone`.",
         "`schedule list|on|off|clear|add <HH:MM> preset <id> [days]|add <HH:MM> night <HH:MM> [days]`, "
         "`wifi status|scan`. Planned: `sync now`, `audio tone`."),
    ],
    "THIRD_PARTY.md": [
        ("| `assets/icons/`; bitmaps rendered into `components/gfx/icons/` | Rasterised by `tools/gen_icons.sh` from "
         "the names in `assets/icons/icons.txt`; font and codepoint list unmodified |",
         "| `assets/icons/`; bitmaps rendered into `components/gfx/icons/` | Rasterised by `tools/gen_icons.sh` from "
         "the names in `assets/icons/icons.txt`; font and codepoint list unmodified |\n"
         "| QR Code generator library (C), commit `3c6d0b3cefb4e049dc337e82237c9644399716a8` | "
         "https://github.com/nayuki/QR-Code-generator | MIT (the header of each file) | `components/gfx/qrcodegen/` "
         "| The config screen's QR codes; unmodified |\n"
         "| tz database 2026c (`zone.tab` and the TZif footers) | https://www.iana.org/time-zones | Public domain | "
         "`web/zones.js` | Generated by `tools/gen_zones.py` from this Mac's `/usr/share/zoneinfo` |"),
    ],
    "docs/specs/2026-09-25-firmware-design.md": [
        # §3.1
        ("| `util` | Small pure-C helpers: CRC-32, base64, delay ticks | — | ✓ |",
         "| `util` | Small pure-C helpers: CRC-32, base64, delay ticks, SHA-256, HMAC and PBKDF2 | — | ✓ |"),
        ("| `netmgr` | Wi-Fi STA/AP state machine, captive DNS, mDNS | IDF | — |\n"
         "| `webui` | HTTP server, REST API, embedded web assets | netmgr, storage, ui | — |",
         "| `netmgr` | Wi-Fi STA/AP state machine, captive DNS, mDNS | IDF | captive DNS reply, saved-network list, "
         "scan choices |\n"
         "| `webui` | HTTP server, the password and sessions, Wi-Fi and OTA routes, embedded web assets; every other API "
         "route goes to `main` | netmgr, util | password record, sessions, HTTP helpers |"),
        # §5.7
        ("- Display ▸ Contrast is hidden for now (D17). Info ▸ IP/MAC and Last sync result join with M4 and M5.",
         "- Display ▸ Contrast is hidden for now (D17). Last sync result joins Info with M5."),
        ("  - The panel is in HPM while the menu is open.\n",
         "  - The panel is in HPM while the menu is open.\n"
         "- As built (M4):\n"
         "  - Wi-Fi ▸ Config mode starts config mode at once. Forget networks and Reset web password ask first, like "
         "Factory reset, and end with a toast.\n"
         "  - Info shows IP and MAC as two rows. The IP shows only while Wi-Fi is on, otherwise a dash.\n"),
        # §10.1
        ("- **Fast connect.** BSSID and channel are cached in NVS and RTC RAM.",
         "- **Fast connect.** BSSID and channel are cached in NVS; RTC RAM joins with the M5 sync. A try with the "
         "cached BSSID that fails is repeated with a scan, as the router may have moved."),
        ("  - Captive DNS answers every name with the AP IP, and OS connectivity-probe URLs redirect to `/`.",
         "  - Captive DNS answers every name with the AP IP, and OS connectivity-probe URLs redirect to `/`. DHCP "
         "offers `http://192.168.4.1/` as the captive portal (RFC 8910).\n"
         "  - The station shares the radio, so the AP must follow a network the station joins onto its channel, "
         "which drops the AP's clients. The AP therefore starts on the channel of the strongest saved network in "
         "sight, else of the strongest network, else 1: trying the likely network doesn't move it.\n"
         "- **Testing a network** (web UI). A scan comes first: a network out of reach is reported at once and never "
         "takes the AP off its channel. The device then joins by BSSID and channel, beside the AP. On success the "
         "network is saved first in the list and the device stays on it; on failure it returns to the network it was "
         "on. The test runs in the background, and the page polls for the result, as the phone may drop off the AP "
         "for a moment."),
        # §10.2
        ("- **Exit.** BOOT long, \"Done\" in the web UI, or 10 min without HTTP requests. Wi-Fi then switches off.",
         "- **Exit.** BOOT long, \"Done\" in the web UI, or 10 min without HTTP requests. Wi-Fi then switches off.\n"
         "  - Only requests for the device's own pages and API count: a joined phone's connectivity probes would "
         "otherwise keep config mode on for good.\n"
         "  - A critical battery (§8) ends it too.\n"
         "- **While it runs.** The board stays awake, as neither sleep keeps Wi-Fi, and the panel is in HPM. The "
         "screen shows the state, a QR code and the minutes left; KEY switches the QR code (§5.6). No battery "
         "samples are taken, as the radio's load pulls VBAT down (§8)."),
        # §10.3
        ("| `GET /api/status` | Device, battery, sensors, time, Wi-Fi, last sync steps, firmware |",
         "| `GET /api/auth` · `POST /api/auth/setup` · `POST /api/auth/login` · `POST /api/auth/logout` · "
         "`POST /api/auth/password` | The web password (§10.4): whether one is set and the session is valid; "
         "choosing it (over the AP only); logging in and out; changing it. The only routes open without a session |\n"
         "| `GET /api/status` | Device, battery, sensors, time, Wi-Fi, firmware; the last sync steps join with M5 |"),
        ("| `GET /api/wifi/scan` · `GET/POST/DELETE /api/wifi/networks` | Wi-Fi setup |",
         "| `GET /api/wifi/scan` · `GET/POST/DELETE /api/wifi/networks` | Wi-Fi setup. A POST starts a test and "
         "answers 202 at once; GET reports its result with the saved names, never their passwords. `\"test\": false` "
         "saves without trying |"),
        ("| `GET /api/preview.bmp?preset=<id>` · `POST /api/preview.bmp` | Render a saved or unsaved preset with live "
         "data, using the real renderer |",
         "| `GET /api/preview.bmp?preset=<id>` · `POST /api/preview.bmp?preset=<id>` | Render a saved preset, or one "
         "from a presets document the editor posts (validated like `presets.json`), with live data, using the real "
         "renderer; a 1-bit BMP |"),
        ("| `GET /api/geocode?q=` | Proxy for the Open-Meteo geocoding search (station mode only; manual lat/lon always "
         "works) |\n| `POST /api/sync` | Sync now |",
         "| `GET /api/geocode?q=` | Proxy for the Open-Meteo geocoding search (station mode only; manual lat/lon always "
         "works) (M5) |\n| `POST /api/sync` | Sync now (M5) |"),
        ("| `POST /api/ota` · `GET /api/ota/status` | Firmware upload |",
         "| `POST /api/ota` · `GET /api/ota/status` | Firmware upload, as `application/octet-stream`; the running "
         "version, its slot, whether it is still pending, and the slot of an update that was rolled back |"),
        ("| `POST /api/reboot` · `POST /api/factory-reset` | System |",
         "| `POST /api/done` · `POST /api/reboot` · `POST /api/factory-reset` | End config mode; system. The reply "
         "goes out before the device acts |"),
        # §10.4
        ("  - It is kept as a salted hash in NVS `secrets`, is never returned or logged, and a factory reset erases it.\n"
         "  - A login starts a session that ends with config mode.",
         "  - It is kept as a salted hash in NVS `secrets`, is never returned or logged, and a factory reset erases "
         "it. The record is `pbkdf2-sha256$10000$<salt>$<hash>` with a 16-byte random salt; a login takes about 2 s "
         "on the chip. 8–64 characters.\n"
         "  - A login starts a session that ends with config mode: at most 4, each a random 128-bit token in a cookie "
         "that is `HttpOnly` and `SameSite=Strict`.\n"
         "  - After 5 wrong passwords, logins wait 60 s."),
        # §10.5
        ("Otherwise the bootloader rolls back at the next reset.\n",
         "Otherwise the bootloader rolls back at the next reset.\n"
         "  - Until then the board doesn't deep-sleep: a deep-sleep wake is a reset, which would roll the image back.\n"
         "  - The upload streams into the other slot. Its header must name this project and chip, and the image's own "
         "checksum and hash must pass.\n"
         "  - Measured at M4: 1.3 MB in 11.5 s over the AP. A test build that crashed after 25 s rolled back to the "
         "previous image, which returned in config mode (§10.2) and reported it.\n"),
        # §14.2
        ("| `sys` | Device id, AP password, schema version, idle strategy override (`idle`) |\n"
         "| `wifi` | Saved networks (SSIDs and passwords), fast-connect cache |\n"
         "| `secrets` | MQTT password; the web UI password's salted hash (D18); future tokens |",
         "| `sys` | Device id, AP password (`ap_pass`), schema version, idle strategy override (`idle`) |\n"
         "| `wifi` | Saved networks with their passwords and fast-connect cache (`nets`, one versioned blob) |\n"
         "| `secrets` | MQTT password; the web UI password's salted hash (`web_pass`, D18); future tokens |"),
        # §14.3
        ("`display.update_min` and `display.lpm_hz`. The file must be",
         "`display.update_min` and `display.lpm_hz`; M4 adds `location.*`, with latitude and longitude clamped to the "
         "globe. `PATCH /api/settings` merges into the file as an RFC 7396 merge patch, which must keep "
         "`\"schema\": 1`. The file must be"),
        # §14.4
        ("- **Backup.** A JSON bundle of every `/cfg/*` file, without secrets. Restore validates the schemas before "
         "replacing anything.",
         "- **Backup.** A JSON bundle of every `/cfg/*` file, without secrets: `{\"reflbo_backup\": 1, \"device\": …, "
         "\"firmware\": …, \"files\": {\"settings.json\": {…}, \"presets.json\": {…}}}`. Restore validates every file it "
         "knows before replacing anything, applies them at once, and leaves out files of a later firmware."),
        # §15
        ("| `wifi status` · `wifi scan` | Wi-Fi |",
         "| `wifi status` · `wifi scan` | Wi-Fi: the state, network, address, AP clients and saved names; the "
         "networks in sight while Wi-Fi is on (config mode) |"),
        # §17
        ("  - locale formatting.\n",
         "  - locale formatting.\n"
         "  - The web configurator: SHA-256, HMAC and PBKDF2 against published vectors; the password record, "
         "sessions and login throttle; the captive DNS reply; the saved-network list and the scan choices; the "
         "settings merge patch; the backup bundle; the preset editor's catalogue; the config screen's QR codes; "
         "`tools/gen_zones.py`.\n"),
        # §21
        ("the −2.0 °C default temperature offset (§8); accepted extras (§19) |\n",
         "the −2.0 °C default temperature offset (§8); accepted extras (§19) |\n"
         "| r15 | 2026-09-30 | M4 as built: the components (§3.1); the menu's Wi-Fi section and Info rows (§5.7); the "
         "AP's channel, testing a network, and the portal address (§10.1); what ends config mode and what runs "
         "meanwhile (§10.2); the API (§10.3); the password record, sessions and login throttle (§10.4); OTA checks "
         "and measurements (§10.5); NVS keys (§14.2); `location.*` and the merge patch (§14.3); the backup bundle "
         "(§14.4); `wifi` (§15); the new host tests (§17) |\n"),
    ],
}


def main():
    for name, edits in EDITS.items():
        path = pathlib.Path(name)
        text = path.read_text()
        for old, new in edits:
            count = text.count(old)
            if count != 1:
                sys.exit(f"{name}: {count} matches for {old[:70]!r}; nothing written for this file")
            text = text.replace(old, new)
        path.write_text(text)
        print(f"{name}: {len(edits)} edits")


if __name__ == "__main__":
    main()
```

Run: `python3 <scratch>/docs_edits.py`
Expected: `AGENTS.md: 7 edits`, `THIRD_PARTY.md: 1 edits`, `docs/specs/2026-09-25-firmware-design.md: 21 edits`.

- [ ] **Step 2: Check.** Read the changed paragraphs once in `git diff`: the text matches what Tasks 1–10 built, and no *(planned)* marker is left on something M4 built.

- [ ] **Step 3: Commit.**

```bash
git add AGENTS.md docs/specs/2026-09-25-firmware-design.md THIRD_PARTY.md
git commit -m "docs: record M4 as built (spec r15)"
```

---

## Owner acceptance (M4, after the final review)

Spec §18: the owner sets up Wi-Fi from a phone in AP mode (level 4); the preview matches a device screenshot and an update and a rollback work (level 3, Task 10). Give the owner this checklist:

1. **Wi-Fi from your phone.** Hold BOOT for 3 s: the screen shows "Wi-Fi setup" with a QR code. Scan it with the phone's camera and join. Expected:
   - iOS opens a sign-in sheet with the page, Android shows a "Sign in to network" notification. If neither does, open `http://192.168.4.1`.
   - The page asks you to choose a password for it. Then Wi-Fi ▸ pick your network ▸ its password ▸ Connect: "Connected: the device is on … as 192.168.x.y". The phone may drop off the device's network for a moment and rejoin.
   - Tap Done: the device shows "Wi-Fi off".
   - Hold BOOT 3 s again: the screen now says "Connected to <your network>" with the address; open it from the phone on your network and log in.
2. **The page on your phone.** Status, Location & time, Device, Presets: edit a preset, watch the preview, save, and check the device shows it. Say whether anything reads or works badly on the phone.
3. **An update from the page.** I build an image with a new version string and you upload it from the Firmware page (or I do it from this Mac). Expected: the device restarts, comes back in config mode, and the Firmware page shows the new version; a minute later it is no longer "new".
4. **The first run, if you want to see it** (it needs a factory reset, which erases this board's settings, presets, saved networks and the web password). Expected: after the reset the first-run screen; KEY leads to the dashboard, and the screen doesn't come back at the next boot.
5. **The two M3 checks still open**: the night-sleep current (D15) and the night peek. The web UI's schedule can now start a night.
