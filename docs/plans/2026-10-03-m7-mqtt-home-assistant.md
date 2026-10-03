# M7: MQTT and Home Assistant, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** M7 (spec §18, D32): the board publishes its state to Home Assistant through MQTT discovery and takes HA's commands (the preset select, Sync now and Next preset buttons) and a message, which shows as a banner until KEY dismisses it and as the `ha.message` field; values from HA and other local devices arrive as `mqtt.<key>` fields; while connected, its key presses reach HA as device triggers. A session runs in every sync, and sync mode `always` keeps one; a failed session is shown but doesn't fail the sync.

**Architecture:**

- **Pure logic, host-tested:**
  - `storage`: the `mqtt.*` settings; the write-only password handed out of a settings patch.
  - `ha_mqtt` (new): the mappings in `/cfg/mqtt_fields.json` (`ha_fields.c`); topics, commands, values, the state and the 17 discovery configs (`ha_payload.c`); the values and the message as an RTC block (`ha_store.c`); a session's rules: back-off, topics, the collect phase, when the state is due, what HA's sensors expire by (`ha_session.c`).
  - `ui`: `mqtt.<key>` fields, which presets name in a key table of their own; `ha.message`; the message banner; the MQTT mark; Info ▸ MQTT.
  - `sync`, `locale`: the sync's step 6 and the spans Wi-Fi is off; the strings.
- **Device side:**
  - `ha_mqtt.c`: one esp-mqtt client driven by a task of its own; sessions, the kept connection, test connections, discovery's hash in NVS.
  - `main/app_mqtt.c`: the hooks, the store in RTC FAST memory, commands and values on the app task, key presses, sync mode `always`, the console and the web API's helpers; `app_sync.c`, `app.c`, `app_ui.c`, `app_cmds.c`, `app_web.c`, `app_menu.c` wire them in.
  - `fetch`: the M6 review's transmit buffer and timeout minors. `webui`: 40 KB requests and replies.
  - `web/`: the MQTT page; the Sync page's MQTT step.

**Tech stack:** ESP-IDF v5.5.5 with its `mqtt` component (esp-mqtt), cJSON (allocating in PSRAM since M5), LittleFS, NVS; Unity on the host; Node's test runner for the page. Nothing new from the registry.

**Spec:** `docs/specs/2026-09-25-firmware-design.md` r34 (D32, written 2026-10-03; the owner's "before writing the plan, add a readme overview" taken as its approval). Task 11 brings it to r35, as built.
- Relevant: §12 (all of it), §5.1 (`ha.message`), §5.2 (the MQTT mark), §5.4 (`mqtt.<key>` in presets), §5.5–§5.7 (the banner, KEY, Info), §6 (the values outside the datastore), §9.3 (step 6), §10.3 (the MQTT page and API), §10.4, §14 (`sys/mqtt_disc`, `secrets/mqtt_pass`, `mqtt.*`, the backup's `mqtt_fields.json`), §15 (`mqtt status`), §17, §18 (M7); D5, D7, D11, D20, D32.
- Also `AGENTS.md` §3.4 (gotchas 11, 22, 29, 30, 31, 33, 43), §5.3, §6 (the stable firmware and board tests), §7, §8.

**Research behind this plan (2026-10-03):**
- The plan's code was built and tested task by task on the local branch `plan/m7`, one commit per task (`plan-m7 task N: …`); the plan carries that code. On the host the whole suite passed after every task: 65 ctest targets at the end (61 before: `test_ha_fields`, `test_ha_payload`, `test_ha_store` and `test_ha_session` are new), and the page's 46 tests (34 before); the ASan/UBSan build passed at the end. The firmware built clean after every task, without a warning: 2 543 552 bytes (2 477 632 before), 179 471 bytes of internal RAM (DIRAM) used at the end.
- **Binary files:** eight goldens come from `plan/m7`, which is pushed to `origin` with this plan. Tasks 5 and 6 copy them with `git checkout plan/m7 -- <paths>` (in a clone without the branch, `git fetch origin plan/m7:plan/m7` first), render them again, and they must match the branch's byte for byte.
- **esp-mqtt** (IDF 5.5.5, `components/mqtt/esp-mqtt/mqtt_client.c`): its task holds the client's API lock through each loop, and in the connecting state through the whole `esp_transport_connect()`, up to the network timeout; once connected, only briefly, as the socket is polled outside the lock. So only the client's own task calls the API, but for the app task's QoS 0 key presses while connected (Task 9). With `disable_auto_reconnect` a failed connection ends in `MQTT_EVENT_DISCONNECTED`, and `esp_mqtt_client_reconnect()` tries again; `disable_clean_session` keeps the broker's session, so QoS 1 commands wait for the sleeping device. Topics and payloads come without a terminating NUL; a message longer than the 4 KB buffer comes in pieces, which the client ignores.
- **RTC memory:** RTC SLOW (8 KB) holds the snapshot, 4 856 bytes now (3 896 at M6b plus the presets' key table of 769 bytes and the MQTT settings), under a cap raised to 5 KB. The values' block (3 968 bytes) goes in RTC FAST (8 KB), which deep sleep keeps powered because `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=y`: `sleep_modes.c` forces `ESP_PD_DOMAIN_RTC_FAST_MEM` on, the heap reserves `.rtc.force_fast` (`heap/port/esp32s3/memory_layout.c`), and on the S3 the region has no per-core limit. `idf.py size` at the end: RTC SLOW 5 008 bytes used, RTC FAST 4 148.
- **Home Assistant's discovery** (spec §12.3): 17 retained configs under one `device` block, the select's `value_template` reading the state's `preset_name`, `expire_after` omitted in manual mode; the largest config, the select's with 16 names of 23 control characters (6 bytes each in JSON), is 2 521 bytes. `test/host/fixtures/ha/discovery.txt` holds them all as a golden.
- **Sizes:** the largest `mqtt_fields.json` is 10 615 bytes (`HA_FIELDS_JSON_MAX` 12 KB); the largest `presets.json` with 32 MQTT keys of 23 bytes, 18 045 (its 20 KB limit holds). A backup of the largest files is then about 31 KB, so requests and replies grow from 24 to 40 KB, in PSRAM.
- **Not checked on the board:** everything the device does is Task 11's. There is no broker or HA yet (the owner's setup), so the checks with them wait (spec §12.10); Task 11 covers MQTT off, the MQTT page and its API, `mqtt status`, the banner through `field set`, deep sleep, and a broker that doesn't answer.
- **A review of the whole branch** (2026-10-03, opus), before this plan was written, found one critical problem, five important ones and a list of smaller ones. Their fixes are in the tasks that own the code, so every task carries its fixed version:
  - Critical: `subscribe()` held `s_lock` while it called esp-mqtt, whose task holds its API lock while it hands over values, which take `s_lock`. In sync mode `always`, a subscription (a reconnect, new mappings, the 5-min one) meeting a live value would have stopped both tasks, and the app task at its next status read. Task 7 states the rule (nothing calls esp-mqtt or a hook while holding `s_lock`) and keeps it.
  - Important: values that come in a sync's session are stamped before the sync sets the clock, so after a power-off without the backup cell they showed as 26 years stale (Task 4's `ha_store_shift_time()`, called from Task 9); discovery never went to a new broker, as its hash ignored the broker (Tasks 3 and 7: the broker in the hash, and the configs again when the broker has no session of ours); the page took hosts the device refuses and then said "Saved" (Task 10); about 14 KB of new statics in internal RAM and a 900-byte wrap on the app task's stack (Tasks 3, 4, 6, 7, 8 and 9: PSRAM, the heap for a moment, entries moved in place, two passes); Test connection in sync mode `always` read the kept client's state at once, so it failed right after a save (Task 7).
  - Smaller ones, fixed: preset names of control characters made the select's config overflow its buffer, and a config that didn't fit was sent half-written (Tasks 3, 8); `cmd/preset` matched an id before HA's names (Task 9); the signal's jitter counted as a change (Task 8); the client's start failing half-way, and a closed client's late events (Task 7); a user name of control characters pushed `settings.json` past 2 KB (Task 1); a large number's whole part could overflow 32 bits (Task 5); `mqtt status` lacked the discovery hash and the values (Task 9); a password lingered in the request buffer (Task 10); an over-long message was logged once per piece (Task 7); an unmapped slot showed as empty in the editor (Task 10); the rule that MQTT never fails a sync was written twice, now one tested function (Task 8); `ha_state_changed()` was never used (Task 3).
  - Left for later, in spec r35 §20 (Task 11): with no mapped topics a session still waits a quiet second for queued commands; a command whose post finds the app's queue full for 100 ms is lost after its PUBACK; after a restart that keeps the clock, the values wait for the next sync; a key press published on a dead link can hold the app task until the socket's timeout.

**Rulings this plan makes (each costs little if wrong):**
- **Mapped topics are subscribed at QoS 0** (Task 7), not spec §12.2's QoS 1: a QoS 1 subscription in a persistent session makes the broker queue every reading for the sleeping device, which the next session would then wade through; each subscription brings the topic's retained value anyway. `cmd/#` stays at QoS 1, so commands wait. Cost: a value published without retain while the board sleeps is missed, as spec §12.5 already says.
- **The values' block lives in RTC FAST memory** (Task 9), since the snapshot fills RTC SLOW. Cost: if a later ESP-IDF stopped keeping RTC FAST powered with the heap in it, the block would fail its check at each wake, and the values would wait for the next sync.
- **The snapshot's cap is 5 KB** and its version 9 (Tasks 1, 5): the presets' key table and the MQTT settings. A flash or an update resets the chip anyway, so the first boot after one is cold.
- **Presets keep their own key table** (Task 5): 32 field ids stand for the keys the presets name, each slot finding its mapping by key when it draws, as spec §5.4 says; a key no mapping names, or one whose kind the slot can't show, draws empty.
- **The banner is one line** (Task 6), so the slots above stay readable for the hours it may show, and **it isn't drawn in config mode at all** (Task 9), even on the dashboard config mode shows while a phone is logged in (D20), where KEY short belongs to config mode.
- **The KEY short that dismisses the banner is not reported to HA** (Task 9): it does nothing else (spec §12.7), and an automation shouldn't fire on it.
- **Key presses are published at once from the app task** (Task 9), under a small lock on the client handle: a sync's session keeps the client's task busy until it ends, and a press queued behind it would find the client gone.
- **In sync mode `always`, a command's new state goes out at once** (Task 9), not after spec §12.9's 30 s limit, so HA's select and buttons answer at once; **and the kept session subscribes again every 5 min**, so the broker sends the retained values again and a value that doesn't change doesn't go stale while connected (spec §12.9 says nothing about either). Cost: one more publish a command; up to 32 small messages every 5 min on the LAN.
- **`cmd/preset` takes a name first, then an id** (Task 9): HA's select sends names (its options), so it always gets the preset it shows; an automation or the console may send an id.
- **Discovery goes out again when the broker has no session of ours** (Task 7), besides spec §12.3's changes of the hash, which now covers the broker too (Task 3): a new broker, or one that lost its state, may have lost the retained configs. Cost: the configs again after a broker's session expires.
- **The Wi-Fi signal is no change of state** (Task 8): it jitters, so it goes out with the 5-min state rather than at most every 30 s.
- **Values move with the clock** (Tasks 4, 9): spec §12.5 dates a value by its arrival; when a sync sets the clock after its session, the arrival moves with it.
- **The sync's step enums move into `sync_plan.h`** (Task 8), so the host tests the one rule that says which step fails a sync.
- **Wrapped texts** (Task 6): `ha.message` and any too-wide text wrap at spaces in M and larger slots, up to 8 lines; the small face keeps one cut line. In narrow split cells a name that doesn't fit two lines falls back to one cut line (the M6b review's minor about 81–82 px cells).
- **A number's precision `null` keeps the payload's own decimals, up to 3** (Task 3), fewer if the value is too large for them.
- **The page checks what the device checks** (Task 10): keys, topics, paths, the port and the prefix, so a save the page allows is one the device takes; labels and units over their bytes are refused there rather than cut.
- **Review minors** where M7 touches their code (D32): `fetch`'s transmit buffer, its timeout's name and the retry's budget (M6), the crossed-out cloud after a scheduled sync only and the "failed at" log line (M5), two-line small text in narrow cells and `webui.h`'s size comment (M6b). The others stay in their memories.

## Global Constraints

- ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. C17 firmware, plain HTML/CSS/JS in `web/` with no build step and no external resources.
- `[host]` code (`ha_fields.c`, `ha_payload.c`, `ha_store.c`, `ha_session.c`, `ui`, `locale`, `sync_plan.c`, `components/storage/settings.c`) includes no ESP-IDF headers. cJSON counts as plain C.
- ESP-IDF style: 4-space indent, `snake_case`, a component or module prefix on public APIs, public headers in `include/`. C lines stay within 120 characters.
- The app task owns the display, storage, the settings, the presets, the syncs' state and the values' block (spec §3.2, `AGENTS.md` §5.3). The client's hooks run on esp-mqtt's task and hand over with `app_post()`; the state and the discovery configs are built on the app task through `app_execute()`, from the client's task only, never from the app task (it would wait for itself).
- **The RTC snapshot is at most 5 KB** (`_Static_assert` in `main/app.c`); a change to its layout bumps `SNAP_VERSION`. The values' block has its own magic, version and CRC, and is at most 4 KB.
- **Input from outside is checked before cJSON recurses** (gotcha 30): a broker's payload at most 16 levels deep, on esp-mqtt's 6 KB stack; `mqtt_fields.json` 8; a request 18 (`WEBUI_JSON_MAX_DEPTH`).
- **Secrets.** Never commit or log a Wi-Fi password, the AP password, the web password or the MQTT password. The MQTT password goes to NVS `secrets/mqtt_pass` and no API returns it.
- **Board tests** (spec §12.10, the owner's rule of 2026-10-03, memory `board-test-protocol`): back up the configuration first (`GET /api/backup`); after every MQTT/HA test flash the stable firmware back (`captures/stable/stable-m6b/flash.sh <port>`, `version` shows `elf dcb35a7e3`) and restore it (`POST /api/restore`), then compare. Never erase flash, NVS or the storage partition, or forget the saved networks, without asking; a factory reset only with the owner's agreement.
  - Board commands go through `tools/idf.sh` with an explicit `-p`.
  - The port must be confirmed as this board: Espressif `303A:1001` with the USB serial number `14:C1:9F:54:BB:94` (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'`), and `flash` prints `MAC: 14:c1:9f:54:bb:94`.
  - The web password may be reset from the menu at any time (the owner, 2026-10-02); a temporary one is set over the device's own network and cleared again at the end.
- List every component source in `SRCS`; run `tools/idf.sh reconfigure` after adding a component. Dependencies come with ESP-IDF; nothing new from the registry.
- Small, focused Conventional Commits that each build. Push to `origin` freely; never force-push. **No attribution of any kind: no `Co-Authored-By` or other trailer in any commit** (the owner's rule, memory `no-commit-trailers`).
- Build only what the spec covers (r34: D32). Not in M7: TLS to the broker, HA's REST API as a source, availability topics, web UI translations (spec §5.8, §19).
- Czech text follows Czech typography, and every string's glyphs must exist in the fonts (`test_lang_glyphs`).
- A widget never draws outside its cell, and in a cell the rules allow it, never against its edges (`test_ui_widget_fit`).

## Review Focus

1. **A broker that fails, or floods.** No broker, a refused login, a name that doesn't resolve, Wi-Fi dropping during the session, a session over its budget; in sync mode `always`, chatty topics while the client subscribes again. Expected: the sync's other steps run and it ends `done`; the MQTT step says why in a few words (Info ▸ MQTT, the Sync page, `mqtt status`), the status bar's MQTT mark shows until a session succeeds, no retry is scheduled for it, the app task never waits on the network, and nothing waits for a lock another task holds while it waits for this one. Pinned by:
   - `test_sync_plan`'s `sync_first_failed()` (Task 8), `test_ha_session`'s back-off (Task 7), and the lock rule at `s_lock` (Task 7);
   - Task 11 Steps 6 and 7, against an address nothing answers; the owner's acceptance with a broker.
2. **What a broker can send.** A payload of 4 KB or more, JSON nested past 16 levels, a NUL byte, a boolean or a string where a number is mapped, an empty message, a 200-byte message, a retained command, a command for a preset that doesn't exist. Expected: nothing crashes; a value that doesn't parse leaves the field as it was; texts are cut at a character; a retained command and an unknown preset are logged and ignored. Pinned by:
   - `test_ha_payload` (deep payloads, the message's cut, numbers from strings and booleans, texts at their limit) (Tasks 3, 9), `test_ui_preset`'s lookup (Task 9);
   - the client's checks of `current_data_offset` and `retain` (Task 7).
3. **Sleep and restarts.** A routine deep-sleep wake, a restart, an update, a power-off, a mapping edited or deleted while values are held. Expected: a routine wake keeps every value and the message, and reads no file; anything else starts the block again from the mappings, and the next sync's retained values fill it; an edited mapping keeps its value if its key and kind stay; a deleted one's slot draws empty, and the presets are never rewritten. Pinned by:
   - `test_ha_store` (the seal, a rebuild keeping values) (Task 4), `test_ui_fields` (unmapped keys) (Task 5);
   - Task 11 Step 5's deep sleep.
4. **The message and KEY in every context.** The banner over the menu, config mode, the first-run screen, the critical-battery screen, a night's peek; KEY short with and without the banner; a new message after a dismissed one; 24 h passing. Expected: the banner only on the dashboard (a night's peek included); KEY short only dismisses it; a new message shows again; after 24 h the banner goes and the field shows its age. Pinned by:
   - `test_ha_store`'s banner and the screen goldens (Tasks 4, 6), `handle_button()`'s order (Task 9);
   - Task 11 Step 4.
5. **The page against the device's rules.** A key in capitals, a duplicate key, a topic with a wildcard or 128 bytes, a path with `..`, a label over 23 bytes, a port of 70000, a prefix ending in `/`, a password of 64 bytes, a saved password left alone or forgotten. Expected: the page refuses each with a sentence before sending, and what it sends the device takes; the password is never shown. Pinned by:
   - the page's tests (Task 10), `test_ha_fields` and `test_settings` (Tasks 1, 2);
   - Task 11 Step 6's refusals over the API.

---

### Task 1: MQTT settings (`storage`, `main`)

**Files:**
- Modify: `components/storage/include/settings.h`, `components/storage/settings.c`, `test/host/test_settings.c`, `main/app.c`, `main/app_ui.c`, `main/app_web.c`

**Interfaces:**
- Consumes: the settings codec's `read_bool()`, `read_scaled()`, `host_name()`, `child()`, `object_at()`, `put()` (all static in `settings.c`), `util_json_depth()`, `SETTINGS_JSON_MAX_DEPTH`, `SETTINGS_HOST_LEN`.
- Produces:
  - `settings_t.mqtt_enabled`, `mqtt_host[SETTINGS_HOST_LEN]`, `mqtt_port`, `mqtt_user[SETTINGS_MQTT_USER_LEN]`, `mqtt_discovery`, `mqtt_prefix[SETTINGS_MQTT_PREFIX_LEN]`, written as `mqtt.enabled`, `mqtt.host`, `mqtt.port`, `mqtt.user`, `mqtt.discovery` and `mqtt.discovery_prefix` in `settings.json`;
  - `#define SETTINGS_MQTT_USER_LEN 64`, `SETTINGS_MQTT_PREFIX_LEN 32`, `SETTINGS_MQTT_PASS_LEN 64`, and `SETTINGS_JSON_MAX 2048`: `settings.json` at its largest, which `app_ui.c` and the backup keep it in;
  - `void settings_mqtt_defaults(settings_t *out)`: off, no broker or user, port 1883, discovery on under `homeassistant`;
  - `typedef enum { SETTINGS_SECRET_NONE, SETTINGS_SECRET_SET, SETTINGS_SECRET_CLEARED, SETTINGS_SECRET_BAD } settings_secret_t;` and `settings_secret_t settings_patch_secret(const char *patch, char *out, size_t size)`: what a patch says about `mqtt.password`, the password in `out` when it sets one.

The password is a secret (spec §12.1): it never enters `settings.json`. `settings_patch()` and `settings_to_json()` drop `mqtt.password` from what they write, and `settings_patch_secret()` hands it to the caller, which writes NVS (Task 10). Each value is read on its own and a bad one keeps the value before (as the other sections do); `""` clears the host and the user. The host follows the NTP servers' rule (letters, digits, dots and dashes, up to 63); the user is up to 63 bytes without control characters, which cJSON would write as 6 bytes each and push the largest `settings.json` from 1 796 bytes to 2 114, past the 2 KB `app_ui.c` keeps it in. That limit gets a name and a test. The prefix is a topic's first level: printable ASCII without wildcards, spaces or a leading or trailing `/`. The settings grow the RTC snapshot, so its version goes to 9 (gotcha 29).

- [ ] **Step 1: Write the failing tests.**

`test/host/test_settings.c`:

```diff
--- a/test/host/test_settings.c
+++ b/test/host/test_settings.c
@@ -18,6 +18,7 @@ void setUp(void)
                                .sync_interval_min = 60, .quiet = false, .quiet_from = 1380, .quiet_to = 360,
                                .ntp = { "cz.pool.ntp.org", "pool.ntp.org" } };
     settings_radar_defaults(&s_defaults);
+    settings_mqtt_defaults(&s_defaults);
     memset(&s_out, 0xAA, sizeof(s_out));
     s_err[0] = '\0';
 }
@@ -111,7 +112,7 @@ static void test_deep_nesting_is_rejected_before_parsing(void)
 
 static void test_saving_keeps_keys_this_firmware_does_not_know(void)
 {
-    const char *base = "{\"schema\": 1, \"mqtt\": {\"host\": \"ha.local\"}, \"time\": {\"ntp\": [\"a\"], \"clock_24h\": true}}";
+    const char *base = "{\"schema\": 1, \"audio\": {\"host\": \"ha.local\"}, \"time\": {\"ntp\": [\"a\"], \"clock_24h\": true}}";
     settings_t s = s_defaults;
     s.clock_24h = false;
     s.lpm_quarter_hz = 32;
@@ -489,6 +490,182 @@ static void test_settings_that_replace_others_remember_the_mode_left(void)
     TEST_ASSERT_EQUAL_UINT8(SETTINGS_SYNC_MANUAL, next.sync_mode_before_always);
 }
 
+/* mqtt.* (spec §12.1, §14.3, D32): off, no broker, port 1883, discovery on under "homeassistant". */
+static void test_the_mqtt_defaults_are_the_specs(void)
+{
+    settings_t s;
+    memset(&s, 0xAA, sizeof(s));
+    settings_mqtt_defaults(&s);
+    TEST_ASSERT_FALSE(s.mqtt_enabled);
+    TEST_ASSERT_EQUAL_STRING("", s.mqtt_host);
+    TEST_ASSERT_EQUAL_UINT16(1883, s.mqtt_port);
+    TEST_ASSERT_EQUAL_STRING("", s.mqtt_user);
+    TEST_ASSERT_TRUE(s.mqtt_discovery);
+    TEST_ASSERT_EQUAL_STRING("homeassistant", s.mqtt_prefix);
+}
+
+static void test_the_mqtt_settings_parse_clamp_and_round_trip(void)
+{
+    const char *json = "{\"schema\":1,\"mqtt\":{\"enabled\":true,\"host\":\"ha.local\",\"port\":8883,"
+                       "\"user\":\"reflbo\",\"discovery\":false,\"discovery_prefix\":\"ha/discovery\"}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_TRUE(s_out.mqtt_enabled);
+    TEST_ASSERT_EQUAL_STRING("ha.local", s_out.mqtt_host);
+    TEST_ASSERT_EQUAL_UINT16(8883, s_out.mqtt_port);
+    TEST_ASSERT_EQUAL_STRING("reflbo", s_out.mqtt_user);
+    TEST_ASSERT_FALSE(s_out.mqtt_discovery);
+    TEST_ASSERT_EQUAL_STRING("ha/discovery", s_out.mqtt_prefix);
+    TEST_ASSERT_TRUE(settings_to_json(&s_out, NULL, s_json, sizeof(s_json)) > 0);
+    settings_t again;
+    TEST_ASSERT_TRUE_MESSAGE(settings_from_json(s_json, &s_defaults, &again, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(&s_out, &again, sizeof(again));
+    json = "{\"schema\":1,\"mqtt\":{\"port\":70000}}";
+    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_UINT16(65535, s_out.mqtt_port);
+    json = "{\"schema\":1,\"mqtt\":{\"port\":0}}";
+    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_UINT16(1, s_out.mqtt_port);
+}
+
+/* A host or prefix that can't work keeps what was there; each value on its own. */
+static void test_bad_mqtt_values_fall_back_one_by_one(void)
+{
+    s_defaults.mqtt_enabled = true;
+    snprintf(s_defaults.mqtt_host, sizeof(s_defaults.mqtt_host), "%s", "ha.local");
+    static const char *const k_bad[] = {
+        "{\"schema\":1,\"mqtt\":{\"host\":\"ha local\",\"discovery_prefix\":\"ha/#\"}}",
+        "{\"schema\":1,\"mqtt\":{\"host\":5,\"discovery_prefix\":\"/ha\"}}",
+        ("{\"schema\":1,\"mqtt\":{\"host\":\"a234567890123456789012345678901234567890123456789012345678901234\","
+         "\"discovery_prefix\":\"ha/\"}}"),
+        "{\"schema\":1,\"mqtt\":{\"enabled\":\"yes\",\"discovery_prefix\":\"a+b\"}}",
+        "{\"schema\":1,\"mqtt\":{\"discovery_prefix\":\"\"}}",
+        "{\"schema\":1,\"mqtt\":{\"discovery_prefix\":\"a2345678901234567890123456789012\"}}",
+    };
+    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
+        TEST_ASSERT_TRUE_MESSAGE(settings_from_json(k_bad[i], &s_defaults, &s_out, s_err, sizeof(s_err)), k_bad[i]);
+        TEST_ASSERT_TRUE_MESSAGE(s_out.mqtt_enabled, k_bad[i]);
+        TEST_ASSERT_EQUAL_STRING_MESSAGE("ha.local", s_out.mqtt_host, k_bad[i]);
+        TEST_ASSERT_EQUAL_STRING_MESSAGE("homeassistant", s_out.mqtt_prefix, k_bad[i]);
+    }
+}
+
+/* A user name a broker can take: up to 63 bytes without control characters; anything else keeps the old one. */
+static void test_a_bad_mqtt_user_keeps_the_old_one(void)
+{
+    snprintf(s_defaults.mqtt_user, sizeof(s_defaults.mqtt_user), "%s", "reflbo");
+    static const char *const k_bad[] = {
+        "{\"schema\":1,\"mqtt\":{\"user\":\"re\\u0001flbo\"}}",
+        "{\"schema\":1,\"mqtt\":{\"user\":\"tab\\there\"}}",
+        "{\"schema\":1,\"mqtt\":{\"user\":\"a234567890123456789012345678901234567890123456789012345678901234\"}}",
+        "{\"schema\":1,\"mqtt\":{\"user\":7}}",
+    };
+    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
+        TEST_ASSERT_TRUE_MESSAGE(settings_from_json(k_bad[i], &s_defaults, &s_out, s_err, sizeof(s_err)), k_bad[i]);
+        TEST_ASSERT_EQUAL_STRING_MESSAGE("reflbo", s_out.mqtt_user, k_bad[i]);
+    }
+    const char *json = "{\"schema\":1,\"mqtt\":{\"user\":\"Čeněk z kuchyně\"}}"; /* UTF-8 is fine */
+    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("Čeněk z kuchyně", s_out.mqtt_user);
+}
+
+/* The largest settings.json fits SETTINGS_JSON_MAX, the buffer app_ui.c keeps it in: every text at its
+ * longest, the place's name of control characters (cJSON writes each in 6 bytes), every list full. */
+static void test_the_largest_settings_fit(void)
+{
+    static settings_t s;
+    memset(&s, 0, sizeof(s));
+    settings_sync_defaults(&s);
+    settings_mqtt_defaults(&s);
+    snprintf(s.language, sizeof(s.language), "%s", "cs");
+    memset(s.tz_posix, 'A', sizeof(s.tz_posix) - 1);
+    memset(s.tz_iana, 'B', sizeof(s.tz_iana) - 1);
+    memset(s.place, 0x01, sizeof(s.place) - 1);
+    s.lat_e4 = -899999;
+    s.lon_e4 = -1799999;
+    s.bat_cal = SETTINGS_BAT_LEARNED;
+    for (int i = 0; i < SETTINGS_BAT_CURVE_POINTS; i++) {
+        s.bat_learned_mv[i] = (uint16_t)(3000 + i * 50);
+    }
+    s.bat_learned_at = 4000000000u;
+    s.bat_empty_mv = 3999;
+    s.bat_full_mv = 4399;
+    for (int i = 0; i < SETTINGS_NTP_MAX; i++) {
+        memset(s.ntp[i], 'n', SETTINGS_HOST_LEN - 1);
+    }
+    s.sync_time_count = SETTINGS_SYNC_TIMES_MAX;
+    for (int i = 0; i < SETTINGS_SYNC_TIMES_MAX; i++) {
+        s.sync_times[i] = (uint16_t)(600 + i * 61);
+    }
+    s.quiet = true;
+    s.temp_offset_c100 = -999;
+    s.hum_offset_pct100 = -1999;
+    s.wx_centre_set = s.fl_centre_set = true;
+    s.wx_lat_e4 = s.fl_lat_e4 = -899999;
+    s.wx_lon_e4 = s.fl_lon_e4 = -1799999;
+    s.fl_min_alt_ft = 60000;
+    s.fl_range_km = 100;
+    s.fl_max = 100;
+    s.mqtt_enabled = true;
+    s.mqtt_port = 65535;
+    memset(s.mqtt_host, 'h', sizeof(s.mqtt_host) - 1);
+    memset(s.mqtt_user, 'u', sizeof(s.mqtt_user) - 1);
+    memset(s.mqtt_prefix, 'p', sizeof(s.mqtt_prefix) - 1);
+    static char out[SETTINGS_JSON_MAX];
+    size_t n = settings_to_json(&s, NULL, out, sizeof(out));
+    printf("the largest settings.json: %u bytes of %d\n", (unsigned)n, SETTINGS_JSON_MAX);
+    TEST_ASSERT_TRUE(n > 0);
+}
+
+/* Unlike a place's name, an empty broker or user is a value: no broker, no login. */
+static void test_an_empty_mqtt_host_or_user_clears_it(void)
+{
+    snprintf(s_defaults.mqtt_host, sizeof(s_defaults.mqtt_host), "%s", "192.168.1.10");
+    snprintf(s_defaults.mqtt_user, sizeof(s_defaults.mqtt_user), "%s", "reflbo");
+    const char *json = "{\"schema\":1,\"mqtt\":{\"host\":\"\",\"user\":\"\"}}";
+    TEST_ASSERT_TRUE(settings_from_json(json, &s_defaults, &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("", s_out.mqtt_host);
+    TEST_ASSERT_EQUAL_STRING("", s_out.mqtt_user);
+    TEST_ASSERT_TRUE(settings_from_json("{\"schema\":1,\"mqtt\":{}}", &s_defaults, &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("192.168.1.10", s_out.mqtt_host); /* not named: kept */
+    TEST_ASSERT_EQUAL_STRING("reflbo", s_out.mqtt_user);
+}
+
+/* mqtt.password is a secret (spec §12.1): PATCH /api/settings hands it to NVS, and no file holds it. */
+static void test_the_mqtt_password_never_reaches_the_file(void)
+{
+    const char *base = "{\"schema\":1,\"mqtt\":{\"host\":\"ha.local\",\"password\":\"old secret\"}}";
+    TEST_ASSERT_TRUE(settings_to_json(&s_defaults, base, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_NULL_MESSAGE(strstr(s_json, "secret"), s_json);
+    TEST_ASSERT_NULL(strstr(s_json, "password"));
+    const char *patch = "{\"mqtt\":{\"user\":\"reflbo\",\"password\":\"new secret\"}}";
+    TEST_ASSERT_TRUE_MESSAGE(settings_patch(base, patch, s_json, sizeof(s_json), s_err, sizeof(s_err)) > 0, s_err);
+    TEST_ASSERT_NULL_MESSAGE(strstr(s_json, "secret"), s_json);
+    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"user\":\"reflbo\""));
+}
+
+static void test_a_patch_hands_over_the_mqtt_password(void)
+{
+    char pass[SETTINGS_MQTT_PASS_LEN];
+    TEST_ASSERT_EQUAL(SETTINGS_SECRET_NONE, settings_patch_secret("{\"mqtt\":{\"user\":\"x\"}}", pass, sizeof(pass)));
+    TEST_ASSERT_EQUAL(SETTINGS_SECRET_NONE, settings_patch_secret("{\"language\":\"cs\"}", pass, sizeof(pass)));
+    TEST_ASSERT_EQUAL(SETTINGS_SECRET_SET, settings_patch_secret("{\"mqtt\":{\"password\":\"p ss\"}}", pass,
+                                                                 sizeof(pass)));
+    TEST_ASSERT_EQUAL_STRING("p ss", pass);
+    TEST_ASSERT_EQUAL(SETTINGS_SECRET_CLEARED, settings_patch_secret("{\"mqtt\":{\"password\":\"\"}}", pass,
+                                                                     sizeof(pass)));
+    TEST_ASSERT_EQUAL_STRING("", pass);
+    TEST_ASSERT_EQUAL(SETTINGS_SECRET_CLEARED, settings_patch_secret("{\"mqtt\":{\"password\":null}}", pass,
+                                                                     sizeof(pass)));
+    TEST_ASSERT_EQUAL(SETTINGS_SECRET_BAD, settings_patch_secret("{\"mqtt\":{\"password\":5}}", pass, sizeof(pass)));
+    char longer[80];
+    memset(longer, 'x', sizeof(longer));
+    snprintf(longer + SETTINGS_MQTT_PASS_LEN, sizeof(longer) - SETTINGS_MQTT_PASS_LEN, "%s", ""); /* 64 bytes */
+    char patch[128];
+    snprintf(patch, sizeof(patch), "{\"mqtt\":{\"password\":\"%s\"}}", longer);
+    TEST_ASSERT_EQUAL(SETTINGS_SECRET_BAD, settings_patch_secret(patch, pass, sizeof(pass)));
+    TEST_ASSERT_EQUAL(SETTINGS_SECRET_NONE, settings_patch_secret("[", pass, sizeof(pass)));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -519,5 +696,13 @@ int main(void)
     RUN_TEST(test_the_radars_default_to_the_location);
     RUN_TEST(test_the_radar_settings_parse_clamp_and_round_trip);
     RUN_TEST(test_a_centre_that_follows_the_location_is_not_saved);
+    RUN_TEST(test_the_mqtt_defaults_are_the_specs);
+    RUN_TEST(test_the_mqtt_settings_parse_clamp_and_round_trip);
+    RUN_TEST(test_bad_mqtt_values_fall_back_one_by_one);
+    RUN_TEST(test_a_bad_mqtt_user_keeps_the_old_one);
+    RUN_TEST(test_the_largest_settings_fit);
+    RUN_TEST(test_an_empty_mqtt_host_or_user_clears_it);
+    RUN_TEST(test_the_mqtt_password_never_reaches_the_file);
+    RUN_TEST(test_a_patch_hands_over_the_mqtt_password);
     return UNITY_END();
 }
```


Run: `cmake --build build-host --target test_settings 2>&1 | grep -o "error: [^;]*" | sort | uniq -c`
Expected: the build stops on errors such as `no member named 'mqtt_enabled' in 'settings_t'` (and `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_discovery`, `mqtt_prefix`) and `call to undeclared function 'settings_mqtt_defaults'`.

- [ ] **Step 2: The settings.**

`components/storage/include/settings.h`:

```diff
--- a/components/storage/include/settings.h
+++ b/components/storage/include/settings.h
@@ -37,8 +37,13 @@ typedef enum {
 
 #define SETTINGS_SYNC_TIMES_MAX 8
 #define SETTINGS_NTP_MAX 2
+#define SETTINGS_JSON_MAX 2048 /* settings.json at its largest, which test_settings.c builds */
 #define SETTINGS_HOST_LEN 64
 
+#define SETTINGS_MQTT_USER_LEN 64   /* mqtt.user, up to 63 bytes without control characters */
+#define SETTINGS_MQTT_PREFIX_LEN 32 /* mqtt.discovery_prefix, up to 31 bytes */
+#define SETTINGS_MQTT_PASS_LEN 64   /* the password, in NVS `secrets` (D32): up to 63 bytes */
+
 typedef struct {
     char language[4];                    /* "en" */
     bool clock_24h;
@@ -73,6 +78,12 @@ typedef struct {
     uint16_t fl_min_alt_ft; /* 0..60000 */
     bool fl_ground;         /* aircraft on the ground too */
     uint8_t fl_max;         /* aircraft shown at most, 1..100 */
+    bool mqtt_enabled;                          /* MQTT and Home Assistant (spec §12.1, D32) */
+    char mqtt_host[SETTINGS_HOST_LEN];          /* the broker: a host name or an address; "" for none */
+    uint16_t mqtt_port;                         /* 1..65535 */
+    char mqtt_user[SETTINGS_MQTT_USER_LEN];     /* "" logs in without a user */
+    bool mqtt_discovery;                        /* publish Home Assistant's discovery configs */
+    char mqtt_prefix[SETTINGS_MQTT_PREFIX_LEN]; /* their topics' first part: "homeassistant" */
 } settings_t;
 
 /* The sync's and the NTP servers' defaults (spec §14.3): times mode at 05:30, a 60 min interval,
@@ -90,6 +101,22 @@ void settings_replaced(settings_t *s, const settings_t *before);
  * before, or off, back to the mode it remembers. */
 void settings_toggle_always(settings_t *s);
 
+/* MQTT's defaults (spec §14.3, D32): off, no broker or user, port 1883, discovery on under
+ * "homeassistant". */
+void settings_mqtt_defaults(settings_t *out);
+
+/* PATCH /api/settings and mqtt.password, which goes to NVS `secrets`, never into the file (spec §12.1):
+ * SETTINGS_SECRET_SET with it in `out`, SETTINGS_SECRET_CLEARED for "" or null, SETTINGS_SECRET_BAD for
+ * anything but a string of up to 63 bytes, SETTINGS_SECRET_NONE when the patch doesn't name it.
+ * settings_patch() and settings_to_json() leave it out of what they write. */
+typedef enum {
+    SETTINGS_SECRET_NONE,
+    SETTINGS_SECRET_SET,
+    SETTINGS_SECRET_CLEARED,
+    SETTINGS_SECRET_BAD,
+} settings_secret_t;
+settings_secret_t settings_patch_secret(const char *patch, char *out, size_t size);
+
 /* The radars' defaults (spec §14.3): zoom 6.5; a range of 50 km, every altitude, none on the ground,
  * 100 aircraft; both centres on out->lat_e4 and lon_e4, so set the location first. */
 void settings_radar_defaults(settings_t *out);
```


`components/storage/settings.c`:

```diff
--- a/components/storage/settings.c
+++ b/components/storage/settings.c
@@ -204,6 +204,60 @@ static void read_sync(const cJSON *sync, settings_t *out)
     out->quiet_to = to >= 0 ? (uint16_t)to : out->quiet_to;
 }
 
+/* Text without control characters (UTF-8 is fine): what a broker takes as a user name. */
+static bool plain(const char *s)
+{
+    for (; *s != '\0'; s++) {
+        if ((unsigned char)*s < ' ' || *s == 0x7F) {
+            return false;
+        }
+    }
+    return true;
+}
+
+/* mqtt.discovery_prefix: 1-31 bytes of printable ASCII without wildcards, a leading or trailing "/". */
+static bool topic_prefix(const char *s)
+{
+    size_t n = strlen(s);
+    if (n == 0 || n >= SETTINGS_MQTT_PREFIX_LEN || s[0] == '/' || s[n - 1] == '/') {
+        return false;
+    }
+    for (size_t i = 0; i < n; i++) {
+        if (s[i] <= ' ' || s[i] > '~' || s[i] == '+' || s[i] == '#') {
+            return false;
+        }
+    }
+    return true;
+}
+
+/* mqtt.* (spec §12.1, §14.3): each value on its own; "" empties the host and the user. */
+static void read_mqtt(const cJSON *mqtt, settings_t *out)
+{
+    read_bool(mqtt, "enabled", &out->mqtt_enabled);
+    read_bool(mqtt, "discovery", &out->mqtt_discovery);
+    const cJSON *host = child(mqtt, "host"), *user = child(mqtt, "user"), *prefix = child(mqtt, "discovery_prefix");
+    if (cJSON_IsString(host) && (host->valuestring[0] == '\0' || host_name(host->valuestring))) {
+        snprintf(out->mqtt_host, sizeof(out->mqtt_host), "%s", host->valuestring);
+    }
+    if (cJSON_IsString(user) && strlen(user->valuestring) < sizeof(out->mqtt_user) && plain(user->valuestring)) {
+        snprintf(out->mqtt_user, sizeof(out->mqtt_user), "%s", user->valuestring);
+    }
+    if (cJSON_IsString(prefix) && topic_prefix(prefix->valuestring)) {
+        snprintf(out->mqtt_prefix, sizeof(out->mqtt_prefix), "%s", prefix->valuestring);
+    }
+    out->mqtt_port = (uint16_t)read_scaled(mqtt, "port", out->mqtt_port, 1, 1, 65535);
+}
+
+void settings_mqtt_defaults(settings_t *out)
+{
+    out->mqtt_enabled = false;
+    out->mqtt_host[0] = '\0';
+    out->mqtt_port = 1883;
+    out->mqtt_user[0] = '\0';
+    out->mqtt_discovery = true;
+    snprintf(out->mqtt_prefix, sizeof(out->mqtt_prefix), "%s", "homeassistant");
+}
+
 /* A radar's centre: both coordinates as numbers, or else the location's, which it then follows. */
 static void read_centre(const cJSON *obj, const settings_t *s, bool *set, int32_t *lat, int32_t *lon)
 {
@@ -333,6 +387,7 @@ bool settings_from_json(const char *json, const settings_t *defaults, settings_t
     read_ntp(child(time, "ntp"), out);
     read_sync(child(root, "sync"), out);
     read_radar(child(root, "radar"), out); /* after the location, which its centres may follow */
+    read_mqtt(child(root, "mqtt"), out);
     cJSON_Delete(root);
     return true;
 }
@@ -448,6 +503,14 @@ size_t settings_to_json(const settings_t *s, const char *base_json, char *out, s
     put(fl, "min_alt_ft", cJSON_CreateNumber(s->fl_min_alt_ft));
     put(fl, "ground", cJSON_CreateBool(s->fl_ground));
     put(fl, "max", cJSON_CreateNumber(s->fl_max));
+    cJSON *mqtt = object_at(root, "mqtt");
+    put(mqtt, "enabled", cJSON_CreateBool(s->mqtt_enabled));
+    put(mqtt, "host", cJSON_CreateString(s->mqtt_host));
+    put(mqtt, "port", cJSON_CreateNumber(s->mqtt_port));
+    put(mqtt, "user", cJSON_CreateString(s->mqtt_user));
+    put(mqtt, "discovery", cJSON_CreateBool(s->mqtt_discovery));
+    put(mqtt, "discovery_prefix", cJSON_CreateString(s->mqtt_prefix));
+    cJSON_DeleteItemFromObjectCaseSensitive(mqtt, "password"); /* a secret, in NVS (spec §12.1) */
     bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, true);
     cJSON_Delete(root);
     return ok ? strlen(out) : 0;
@@ -487,6 +550,7 @@ size_t settings_patch(const char *base_json, const char *patch, char *out, size_
     }
     merge(root, p);
     cJSON_Delete(p);
+    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "mqtt"), "password"); /* NVS's */
     const cJSON *schema = child(root, "schema");
     size_t n = 0;
     if (!cJSON_IsNumber(schema) || schema->valuedouble != SCHEMA) {
@@ -499,3 +563,20 @@ size_t settings_patch(const char *base_json, const char *patch, char *out, size_
     cJSON_Delete(root);
     return n;
 }
+
+settings_secret_t settings_patch_secret(const char *patch, char *out, size_t size)
+{
+    cJSON *p = patch != NULL && util_json_depth(patch) <= SETTINGS_JSON_MAX_DEPTH ? cJSON_Parse(patch) : NULL;
+    const cJSON *pass = child(child(p, "mqtt"), "password");
+    settings_secret_t r = pass == NULL                                         ? SETTINGS_SECRET_NONE
+                          : cJSON_IsNull(pass)                                 ? SETTINGS_SECRET_CLEARED
+                          : !cJSON_IsString(pass)                              ? SETTINGS_SECRET_BAD
+                          : strlen(pass->valuestring) >= SETTINGS_MQTT_PASS_LEN ? SETTINGS_SECRET_BAD
+                          : pass->valuestring[0] == '\0'                        ? SETTINGS_SECRET_CLEARED
+                                                                               : SETTINGS_SECRET_SET;
+    if (size > 0) {
+        snprintf(out, size, "%s", r == SETTINGS_SECRET_SET ? pass->valuestring : "");
+    }
+    cJSON_Delete(p);
+    return r;
+}
```


Run: `cmake --build build-host --target test_settings && ./build-host/test_settings | tail -1`
Expected: `OK` (35 tests), after `the largest settings.json: 1796 bytes of 2048`.

- [ ] **Step 3: The app's defaults, the buffers' size and the snapshot's version.**

`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -27,7 +27,7 @@ static const char *TAG = "app_ui";
 static app_ui_state_t s;
 /* The config files' text: scratch buffers in PSRAM (AGENTS.md §8). */
 EXT_RAM_BSS_ATTR static char s_file[UI_PRESETS_JSON_MAX];
-EXT_RAM_BSS_ATTR static char s_settings_base[2048]; /* settings.json as read: unknown keys stay */
+EXT_RAM_BSS_ATTR static char s_settings_base[SETTINGS_JSON_MAX]; /* settings.json as read: unknown keys stay */
 static char s_err[96];
 static char s_toast[64];
 static int64_t s_toast_until_ms;
@@ -57,6 +57,7 @@ static void default_settings(settings_t *out)
     };
     settings_sync_defaults(out);
     settings_radar_defaults(out);
+    settings_mqtt_defaults(out);
     snprintf(out->place, sizeof(out->place), "%s", CONFIG_REFLBO_LOCATION_NAME);
     snprintf(out->tz_posix, sizeof(out->tz_posix), "%s", CONFIG_REFLBO_TZ);
     snprintf(out->tz_iana, sizeof(out->tz_iana), "%s", CONFIG_REFLBO_TZ_NAME);
```


`main/app_web.c`:

```diff
--- a/main/app_web.c
+++ b/main/app_web.c
@@ -361,12 +361,13 @@ static void learn(const char *body, uint8_t *out, size_t size, webui_reply_t *re
 
 /* A backup of the largest files restores: settings.json up to the 2 KB app_ui.c keeps, presets.json up to
  * its own limit, and the bundle around them. */
-_Static_assert(2048 + UI_PRESETS_JSON_MAX + 512 <= WEBUI_BODY_MAX, "a backup of the largest files fits a request");
+_Static_assert(SETTINGS_JSON_MAX + UI_PRESETS_JSON_MAX + 512 <= WEBUI_BODY_MAX,
+               "a backup of the largest files fits a request");
 
 /* GET /api/backup (spec §14.4): the /cfg files as the firmware would save them now. */
 static void backup(uint8_t *out, size_t size, webui_reply_t *reply)
 {
-    EXT_RAM_BSS_ATTR static char settings[2048];
+    EXT_RAM_BSS_ATTR static char settings[SETTINGS_JSON_MAX];
     EXT_RAM_BSS_ATTR static char presets[UI_PRESETS_JSON_MAX];
     netmgr_status_t net;
     netmgr_status(&net);
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
+#define SNAP_VERSION      9 /* 6: the weather, the air quality and the syncs' state; 7: the rain; 8: split
+                                     presets; 9: MQTT's settings */
 #define PEEK_MS           60000 /* a button during the night shows the dashboard this long (spec §9.1) */
 #define NIGHT_RECHECK_S   60    /* a night sleep with a button held looks again this often (D16) */
 #define CRITICAL_RECHECK_S 600  /* the critical sleep checks again this often if KEY is held */
```


- [ ] **Step 4: Run the tests, and build.**

Run: `cmake --build build-host && ctest --test-dir build-host | tail -3; tools/idf.sh build 2>&1 | grep -c 'warning:'`
Expected: `100% tests passed, 0 tests failed out of 61`; `0`.

- [ ] **Step 5: Commit.**

```bash
git add components/storage main/app.c main/app_ui.c main/app_web.c test/host/test_settings.c
git commit -m "feat(storage): MQTT settings and the write-only password (D32)"
```

### Task 2: The MQTT field mappings (`ha_mqtt`)

**Files:**
- Create: `components/ha_mqtt/CMakeLists.txt`, `components/ha_mqtt/include/ha_fields.h`, `components/ha_mqtt/ha_fields.c`, `test/host/test_ha_fields.c`
- Modify: `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: cJSON; `util_json_depth()`; the host build's `reflbo_host_test()` and `REFLBO_WARNINGS`.
- Produces (`ha_fields.h`, pure C):
  - `HA_FIELDS_MAX 32`, `HA_KEY_LEN 24`, `HA_LABEL_LEN 24`, `HA_UNIT_LEN 8`, `HA_TOPIC_LEN 128`, `HA_PATH_LEN 48`, `HA_PRECISION_AUTO 0xFF`, `HA_TTL_MIN_S 60`, `HA_TTL_MAX_S 2592000`, `HA_FIELDS_JSON_MAX 12288`;
  - `typedef enum { HA_KIND_NUMBER, HA_KIND_TEXT } ha_kind_t;`
  - `typedef struct { char key[HA_KEY_LEN]; char label[HA_LABEL_LEN]; uint8_t kind; uint8_t precision; char unit[HA_UNIT_LEN]; char topic[HA_TOPIC_LEN]; char json_path[HA_PATH_LEN]; uint32_t ttl_s; } ha_field_t;`
  - `typedef struct { uint8_t count; ha_field_t field[HA_FIELDS_MAX]; } ha_fields_t;`
  - `bool ha_key_valid(const char *key)`; `bool ha_fields_from_json(const char *json, ha_fields_t *out, char *err, size_t err_size)`; `size_t ha_fields_to_json(const ha_fields_t *f, char *out, size_t size)` (0 if it doesn't fit); `int ha_fields_find(const ha_fields_t *f, const char *key)`;
  - the host library `ha_mqtt_logic`, which later tasks add their sources to.

`/cfg/mqtt_fields.json` (spec §12.5) holds up to 32 mappings. A structural error refuses the file whole and names the field: a key that isn't 1–23 characters of `a`–`z`, `0`–`9` and `_`, or repeats; a topic that isn't 1–127 printable ASCII characters, or has a wildcard (`+`, `#`), a quote or a backslash; a `json_path` that isn't keys joined by dots; a kind other than `number` or `text`; more than 32 fields; a schema other than 1; nesting past 8 levels (the file nests 3), checked before cJSON recurses (gotcha 30). Everything else is lenient, as `presets.json` is: labels (the key when missing) and units are cut at a character and at a control character, precision clamps to 0–3 (`null` or missing: as the payload has it, up to 3), `ttl_s` to 60 s–30 days (0 or missing: twice the expected sync interval). The largest legal file is 10 615 bytes; `HA_FIELDS_JSON_MAX` leaves room.

- [ ] **Step 1: Write the failing tests, and the host build's library.**

`test/host/test_ha_fields.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/test/host/test_ha_fields.c
@@ -0,0 +1,203 @@
+#include <stdio.h>
+#include <string.h>
+
+#include "ha_fields.h"
+#include "unity.h"
+
+static ha_fields_t s_out;
+static char s_err[96];
+static char s_json[HA_FIELDS_JSON_MAX];
+
+void setUp(void)
+{
+    memset(&s_out, 0xAA, sizeof(s_out));
+    s_err[0] = '\0';
+}
+
+void tearDown(void) {}
+
+/* The spec's example (§12.5). */
+static const char *k_example =
+    "{\"schema\": 1, \"fields\": ["
+    " {\"key\": \"outdoor_temp\", \"label\": \"Outside\", \"kind\": \"number\", \"unit\": \"°C\", \"precision\": 1,"
+    "  \"topic\": \"ha/statestream/sensor/outdoor_temperature/state\", \"json_path\": null, \"ttl_s\": 172800},"
+    " {\"key\": \"co2\", \"label\": \"CO2\", \"kind\": \"number\", \"unit\": \"ppm\", \"precision\": 0,"
+    "  \"topic\": \"zigbee2mqtt/living_room\", \"json_path\": \"co2\"}]}";
+
+static void test_the_spec_example_parses(void)
+{
+    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(k_example, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(2, s_out.count);
+    const ha_field_t *f = &s_out.field[0];
+    TEST_ASSERT_EQUAL_STRING("outdoor_temp", f->key);
+    TEST_ASSERT_EQUAL_STRING("Outside", f->label);
+    TEST_ASSERT_EQUAL_UINT8(HA_KIND_NUMBER, f->kind);
+    TEST_ASSERT_EQUAL_STRING("°C", f->unit);
+    TEST_ASSERT_EQUAL_UINT8(1, f->precision);
+    TEST_ASSERT_EQUAL_STRING("ha/statestream/sensor/outdoor_temperature/state", f->topic);
+    TEST_ASSERT_EQUAL_STRING("", f->json_path);
+    TEST_ASSERT_EQUAL_UINT32(172800, f->ttl_s);
+    f = &s_out.field[1];
+    TEST_ASSERT_EQUAL_STRING("co2", f->key);
+    TEST_ASSERT_EQUAL_UINT8(0, f->precision);
+    TEST_ASSERT_EQUAL_STRING("co2", f->json_path);
+    TEST_ASSERT_EQUAL_UINT32(0, f->ttl_s); /* the default: twice the expected interval */
+    TEST_ASSERT_EQUAL_INT(1, ha_fields_find(&s_out, "co2"));
+    TEST_ASSERT_EQUAL_INT(-1, ha_fields_find(&s_out, "co"));
+}
+
+/* Only the key and the topic are needed; the rest takes its defaults. */
+static void test_a_field_needs_only_a_key_and_a_topic(void)
+{
+    const char *json = "{\"schema\": 1, \"fields\": [{\"key\": \"door\", \"topic\": \"z2m/door\", \"kind\": \"text\"},"
+                       " {\"key\": \"power\", \"topic\": \"z2m/plug\"}]}";
+    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(json, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_STRING("door", s_out.field[0].label); /* the key stands in */
+    TEST_ASSERT_EQUAL_UINT8(HA_KIND_TEXT, s_out.field[0].kind);
+    TEST_ASSERT_EQUAL_UINT8(HA_KIND_NUMBER, s_out.field[1].kind);
+    TEST_ASSERT_EQUAL_UINT8(HA_PRECISION_AUTO, s_out.field[1].precision); /* the payload's own decimals */
+    TEST_ASSERT_EQUAL_STRING("", s_out.field[1].unit);
+    TEST_ASSERT_TRUE(ha_fields_from_json("{\"schema\": 1}", &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_UINT8(0, s_out.count); /* no mappings yet */
+}
+
+static void test_it_round_trips(void)
+{
+    TEST_ASSERT_TRUE(ha_fields_from_json(k_example, &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_TRUE(ha_fields_to_json(&s_out, s_json, sizeof(s_json)) > 0);
+    ha_fields_t again;
+    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(s_json, &again, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(s_out.count, again.count);
+    TEST_ASSERT_EQUAL_MEMORY(s_out.field, again.field, sizeof(again.field[0]) * again.count);
+    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_json, "\"json_path\":null"), s_json); /* the whole payload */
+    TEST_ASSERT_EQUAL_UINT(0, ha_fields_to_json(&s_out, s_json, 16)); /* no room */
+}
+
+/* Keys go into presets as mqtt.<key> (spec §12.5): 1-23 bytes of a-z, 0-9 and _. */
+static void test_keys_are_short_and_plain(void)
+{
+    TEST_ASSERT_TRUE(ha_key_valid("outdoor_temp"));
+    TEST_ASSERT_TRUE(ha_key_valid("a2345678901234567890123"));
+    TEST_ASSERT_FALSE(ha_key_valid("a23456789012345678901234"));
+    TEST_ASSERT_FALSE(ha_key_valid(""));
+    TEST_ASSERT_FALSE(ha_key_valid("Outdoor"));
+    TEST_ASSERT_FALSE(ha_key_valid("out-door"));
+    TEST_ASSERT_FALSE(ha_key_valid("out.door"));
+    TEST_ASSERT_FALSE(ha_key_valid(NULL));
+}
+
+/* A file with a structural error is refused whole, and the error names it. */
+static void test_structural_errors_refuse_the_file(void)
+{
+    static const struct {
+        const char *json, *err;
+    } k_cases[] = {
+        { "{", "not valid JSON" },
+        { "{\"schema\": 2}", "schema must be 1" },
+        { "{\"schema\": 1, \"fields\": {}}", "fields must be a list" },
+        { "{\"schema\": 1, \"fields\": [5]}", "field 1 must be an object" },
+        { "{\"schema\": 1, \"fields\": [{\"topic\": \"a\"}]}", "field 1: a key is 1-23 characters of a-z, 0-9 or _" },
+        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"x\"}, {\"key\": \"a\", \"topic\": \"y\"}]}",
+          "duplicate key \"a\"" },
+        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\"}]}", "field a: a topic of 1-127 printable characters" },
+        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"z2m/+/x\"}]}",
+          "field a: the topic has a wildcard" },
+        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"z2m/#\"}]}", "field a: the topic has a wildcard" },
+        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"z2m\\\"x\"}]}",
+          "field a: a topic of 1-127 printable characters" },
+        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"x\", \"kind\": \"bool\"}]}",
+          "field a: kind is number or text" },
+        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"x\", \"json_path\": \"a..b\"}]}",
+          "field a: json_path is keys joined by dots" },
+        { "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"x\", \"json_path\": 5}]}",
+          "field a: json_path is keys joined by dots" },
+    };
+    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
+        TEST_ASSERT_FALSE_MESSAGE(ha_fields_from_json(k_cases[i].json, &s_out, s_err, sizeof(s_err)), k_cases[i].json);
+        TEST_ASSERT_EQUAL_STRING_MESSAGE(k_cases[i].err, s_err, k_cases[i].json);
+    }
+}
+
+/* `n` fields k0, k1, ... in s_json. */
+static const char *many(int n)
+{
+    size_t at = (size_t)snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"fields\": [");
+    for (int i = 0; i < n; i++) {
+        at += (size_t)snprintf(s_json + at, sizeof(s_json) - at, "%s{\"key\": \"k%d\", \"topic\": \"t/%d\"}",
+                               i ? "," : "", i, i);
+    }
+    snprintf(s_json + at, sizeof(s_json) - at, "]}");
+    return s_json;
+}
+
+static void test_at_most_32_fields(void)
+{
+    TEST_ASSERT_FALSE(ha_fields_from_json(many(HA_FIELDS_MAX + 1), &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("at most 32 MQTT fields", s_err);
+    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(many(HA_FIELDS_MAX), &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(32, s_out.count);
+}
+
+/* Everything else is lenient: labels and units are cut at a character, numbers clamped. */
+static void test_the_rest_is_cut_or_clamped(void)
+{
+    const char *json = "{\"schema\": 1, \"fields\": [{\"key\": \"a\", \"topic\": \"x\","
+                       " \"label\": \"abcdefghijklmnopqrstuvč\", \"unit\": \"µg/m³ ok\", \"precision\": 7,"
+                       " \"ttl_s\": 5}, {\"key\": \"b\", \"topic\": \"y\", \"label\": \"one\\ntwo\", \"precision\": -1,"
+                       " \"ttl_s\": 99999999}]}";
+    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(json, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrstuv", s_out.field[0].label); /* 22 bytes: "č" would split */
+    TEST_ASSERT_EQUAL_STRING("µg/m³", s_out.field[0].unit);                 /* 7 bytes */
+    TEST_ASSERT_EQUAL_UINT8(3, s_out.field[0].precision);
+    TEST_ASSERT_EQUAL_UINT32(HA_TTL_MIN_S, s_out.field[0].ttl_s);
+    TEST_ASSERT_EQUAL_STRING("one", s_out.field[1].label); /* up to a control character */
+    TEST_ASSERT_EQUAL_UINT8(0, s_out.field[1].precision);
+    TEST_ASSERT_EQUAL_UINT32(HA_TTL_MAX_S, s_out.field[1].ttl_s);
+}
+
+static void test_deep_nesting_is_refused_before_parsing(void)
+{
+    size_t at = (size_t)snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"x\": ");
+    memset(s_json + at, '[', 40);
+    memset(s_json + at + 40, ']', 40);
+    snprintf(s_json + at + 80, sizeof(s_json) - at - 80, "}");
+    TEST_ASSERT_FALSE(ha_fields_from_json(s_json, &s_out, s_err, sizeof(s_err)));
+    TEST_ASSERT_NOT_NULL(strstr(s_err, "nested"));
+}
+
+/* The largest file: 32 fields with every text at its longest; HA_FIELDS_JSON_MAX holds it. */
+static void test_the_largest_file_fits(void)
+{
+    ha_fields_t f = { .count = HA_FIELDS_MAX };
+    for (int i = 0; i < HA_FIELDS_MAX; i++) {
+        ha_field_t *x = &f.field[i];
+        snprintf(x->key, sizeof(x->key), "k%022d", i);
+        memset(x->label, 'L', sizeof(x->label) - 1);
+        memset(x->unit, 'u', sizeof(x->unit) - 1);
+        memset(x->topic, 't', sizeof(x->topic) - 1);
+        memset(x->json_path, 'p', sizeof(x->json_path) - 1);
+        x->kind = HA_KIND_NUMBER;
+        x->precision = 3;
+        x->ttl_s = HA_TTL_MAX_S;
+    }
+    size_t n = ha_fields_to_json(&f, s_json, sizeof(s_json));
+    TEST_ASSERT_TRUE(n > 0);
+    printf("the largest mqtt_fields.json: %u bytes of %u\n", (unsigned)n, (unsigned)sizeof(s_json));
+    TEST_ASSERT_TRUE_MESSAGE(ha_fields_from_json(s_json, &s_out, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(&f, &s_out, sizeof(f));
+}
+
+int main(void)
+{
+    UNITY_BEGIN();
+    RUN_TEST(test_the_spec_example_parses);
+    RUN_TEST(test_a_field_needs_only_a_key_and_a_topic);
+    RUN_TEST(test_it_round_trips);
+    RUN_TEST(test_keys_are_short_and_plain);
+    RUN_TEST(test_structural_errors_refuse_the_file);
+    RUN_TEST(test_at_most_32_fields);
+    RUN_TEST(test_the_rest_is_cut_or_clamped);
+    RUN_TEST(test_deep_nesting_is_refused_before_parsing);
+    RUN_TEST(test_the_largest_file_fits);
+    return UNITY_END();
+}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -174,6 +174,12 @@ target_include_directories(storage_logic PUBLIC ${REPO_ROOT}/components/storage/
 target_compile_options(storage_logic PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(storage_logic PRIVATE cjson util m)
 
+# ha_mqtt: MQTT and Home Assistant's mappings, payloads and store build on the host; the client does not.
+add_library(ha_mqtt_logic STATIC ${REPO_ROOT}/components/ha_mqtt/ha_fields.c)
+target_include_directories(ha_mqtt_logic PUBLIC ${REPO_ROOT}/components/ha_mqtt/include)
+target_compile_options(ha_mqtt_logic PRIVATE ${REFLBO_WARNINGS})
+target_link_libraries(ha_mqtt_logic PRIVATE cjson util m)
+
 # weather: the Open-Meteo requests, replies, bands and levels build on the host; the fetch does not.
 add_library(weather_logic STATIC ${REPO_ROOT}/components/weather/weather_url.c
             ${REPO_ROOT}/components/weather/weather_parse.c ${REPO_ROOT}/components/weather/weather_levels.c)
@@ -260,6 +266,7 @@ reflbo_host_test(test_settings storage_logic)
 reflbo_host_test(test_storage_file storage_logic)
 target_compile_definitions(test_storage_file PRIVATE TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/storage_tmp")
 reflbo_host_test(test_storage_backup storage_logic)
+reflbo_host_test(test_ha_fields ha_mqtt_logic)
 
 reflbo_host_test(test_test_pattern_golden gfx)
 target_compile_definitions(test_test_pattern_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
```


Run: `cmake -S test/host -B build-host -G Ninja 2>&1 | grep -m1 -A1 'CMake Error'`
Expected: `CMake Error at CMakeLists.txt:178 (add_library):`, then `Cannot find source file:` and the path of `components/ha_mqtt/ha_fields.c`.

- [ ] **Step 2: The mappings.**

`components/ha_mqtt/CMakeLists.txt`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/CMakeLists.txt
@@ -0,0 +1,5 @@
+# MQTT and Home Assistant (spec §12, D32). ha_fields.c (the field mappings) is pure C and also builds on
+# the host.
+idf_component_register(SRCS "ha_fields.c"
+                       INCLUDE_DIRS "include"
+                       PRIV_REQUIRES json util)
```


`components/ha_mqtt/include/ha_fields.h`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/include/ha_fields.h
@@ -0,0 +1,55 @@
+#pragma once
+
+#include <stdbool.h>
+#include <stddef.h>
+#include <stdint.h>
+
+/*
+ * The MQTT field mappings (spec §12.5, D7): which topic, and which key in its JSON, brings each
+ * mqtt.<key> field. Kept in /cfg/mqtt_fields.json, edited on the web page's MQTT page. A structural
+ * error refuses the file whole and names it; labels and units are cut at a character, numbers
+ * clamped. Pure C on cJSON, host-buildable.
+ *
+ *   { "schema": 1, "fields": [ { "key": "outdoor_temp", "label": "Outside", "kind": "number",
+ *     "unit": "°C", "precision": 1, "topic": "ha/statestream/sensor/outdoor_temperature/state",
+ *     "json_path": null, "ttl_s": 172800 } ] }
+ */
+
+#define HA_FIELDS_MAX 32
+#define HA_KEY_LEN 24    /* 1-23 bytes of a-z, 0-9 and _ */
+#define HA_LABEL_LEN 24  /* up to 23 bytes; the key when missing */
+#define HA_UNIT_LEN 8    /* up to 7 bytes: "µg/m³" */
+#define HA_TOPIC_LEN 128 /* 1-127 printable ASCII characters, no wildcards, no quotes or backslashes */
+#define HA_PATH_LEN 48   /* dotted keys into the payload's JSON: "a.b.c"; "" for the payload itself */
+#define HA_PRECISION_AUTO 0xFF /* decimals as the payload has them, up to 3 */
+#define HA_TTL_MIN_S 60
+#define HA_TTL_MAX_S 2592000 /* 30 days */
+#define HA_FIELDS_JSON_MAX 12288 /* the largest file is 10 615 bytes (test_ha_fields.c) */
+
+typedef enum {
+    HA_KIND_NUMBER,
+    HA_KIND_TEXT,
+} ha_kind_t;
+
+typedef struct {
+    char key[HA_KEY_LEN];
+    char label[HA_LABEL_LEN];
+    uint8_t kind;      /* ha_kind_t */
+    uint8_t precision; /* 0-3 decimals, or HA_PRECISION_AUTO */
+    char unit[HA_UNIT_LEN];
+    char topic[HA_TOPIC_LEN];
+    char json_path[HA_PATH_LEN];
+    uint32_t ttl_s; /* stale after this long; 0: twice the expected sync interval (spec §5.1) */
+} ha_field_t;
+
+typedef struct {
+    uint8_t count;
+    ha_field_t field[HA_FIELDS_MAX];
+} ha_fields_t;
+
+bool ha_key_valid(const char *key);
+/* Parses and validates mqtt_fields.json; false with the reason in `err`, *out then unspecified. */
+bool ha_fields_from_json(const char *json, ha_fields_t *out, char *err, size_t err_size);
+/* Returns the length written, or 0 if `size` is too small. */
+size_t ha_fields_to_json(const ha_fields_t *f, char *out, size_t size);
+int ha_fields_find(const ha_fields_t *f, const char *key); /* index, or -1 */
```


`components/ha_mqtt/ha_fields.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/ha_fields.c
@@ -0,0 +1,220 @@
+#include "ha_fields.h"
+
+#include <math.h>
+#include <stdarg.h>
+#include <stdio.h>
+#include <string.h>
+
+#include "cJSON.h"
+#include "util_json.h"
+
+#define SCHEMA 1
+#define MAX_DEPTH 8 /* the file nests 3 levels */
+
+static bool fail(char *err, size_t size, const char *fmt, ...)
+{
+    if (size > 0) {
+        va_list ap;
+        va_start(ap, fmt);
+        vsnprintf(err, size, fmt, ap);
+        va_end(ap);
+    }
+    return false;
+}
+
+bool ha_key_valid(const char *key)
+{
+    size_t n = key != NULL ? strlen(key) : 0;
+    if (n == 0 || n >= HA_KEY_LEN) {
+        return false;
+    }
+    for (size_t i = 0; i < n; i++) {
+        char c = key[i];
+        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
+            return false;
+        }
+    }
+    return true;
+}
+
+/* 1 to size - 1 printable ASCII characters, none of them in `banned`. */
+static bool printable(const char *s, size_t size, const char *banned)
+{
+    size_t n = strlen(s);
+    if (n == 0 || n >= size) {
+        return false;
+    }
+    for (size_t i = 0; i < n; i++) {
+        if (s[i] < ' ' || s[i] > '~' || strchr(banned, s[i]) != NULL) {
+            return false;
+        }
+    }
+    return true;
+}
+
+/* A text up to its first control character, cut to fit at a character boundary. */
+static void copy_text(char *out, size_t size, const char *text)
+{
+    size_t n = 0;
+    while (text[n] != '\0' && (unsigned char)text[n] >= ' ' && text[n] != 0x7F) {
+        n++;
+    }
+    if (n >= size) {
+        n = size - 1;
+        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) { /* text[n] continues a sequence */
+            n--;
+        }
+    }
+    memcpy(out, text, n);
+    out[n] = '\0';
+}
+
+/* Dotted keys, none empty: "co2", "update.state". */
+static bool valid_path(const char *s)
+{
+    if (!printable(s, HA_PATH_LEN, "\"\\")) {
+        return false;
+    }
+    size_t n = strlen(s);
+    return s[0] != '.' && s[n - 1] != '.' && strstr(s, "..") == NULL;
+}
+
+static long clamped(const cJSON *item, long fallback, long lo, long hi)
+{
+    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
+        return fallback;
+    }
+    long v = lround(item->valuedouble);
+    return v < lo ? lo : v > hi ? hi : v;
+}
+
+static bool parse_field(const cJSON *item, int n, ha_field_t *out, char *err, size_t size)
+{
+    if (!cJSON_IsObject(item)) {
+        return fail(err, size, "field %d must be an object", n);
+    }
+    const cJSON *key = cJSON_GetObjectItemCaseSensitive(item, "key");
+    if (!cJSON_IsString(key) || !ha_key_valid(key->valuestring)) {
+        return fail(err, size, "field %d: a key is 1-23 characters of a-z, 0-9 or _", n);
+    }
+    snprintf(out->key, sizeof(out->key), "%s", key->valuestring);
+    const char *k = out->key;
+    const cJSON *topic = cJSON_GetObjectItemCaseSensitive(item, "topic");
+    if (!cJSON_IsString(topic) || !printable(topic->valuestring, HA_TOPIC_LEN, "\"\\")) {
+        return fail(err, size, "field %s: a topic of 1-127 printable characters", k);
+    }
+    if (strpbrk(topic->valuestring, "+#") != NULL) {
+        return fail(err, size, "field %s: the topic has a wildcard", k);
+    }
+    snprintf(out->topic, sizeof(out->topic), "%s", topic->valuestring);
+    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(item, "kind");
+    if (kind != NULL && !cJSON_IsNull(kind)) {
+        const char *s = cJSON_IsString(kind) ? kind->valuestring : "";
+        if (strcmp(s, "number") != 0 && strcmp(s, "text") != 0) {
+            return fail(err, size, "field %s: kind is number or text", k);
+        }
+        out->kind = strcmp(s, "text") == 0 ? HA_KIND_TEXT : HA_KIND_NUMBER;
+    }
+    const cJSON *path = cJSON_GetObjectItemCaseSensitive(item, "json_path");
+    if (path != NULL && !cJSON_IsNull(path) && !(cJSON_IsString(path) && path->valuestring[0] == '\0')) {
+        if (!cJSON_IsString(path) || !valid_path(path->valuestring)) {
+            return fail(err, size, "field %s: json_path is keys joined by dots", k);
+        }
+        snprintf(out->json_path, sizeof(out->json_path), "%s", path->valuestring);
+    }
+    const cJSON *label = cJSON_GetObjectItemCaseSensitive(item, "label");
+    copy_text(out->label, sizeof(out->label), cJSON_IsString(label) && label->valuestring[0] ? label->valuestring : k);
+    if (out->label[0] == '\0') {
+        snprintf(out->label, sizeof(out->label), "%s", k); /* it began with a control character */
+    }
+    const cJSON *unit = cJSON_GetObjectItemCaseSensitive(item, "unit");
+    if (cJSON_IsString(unit)) {
+        copy_text(out->unit, sizeof(out->unit), unit->valuestring);
+    }
+    const cJSON *precision = cJSON_GetObjectItemCaseSensitive(item, "precision");
+    out->precision = cJSON_IsNumber(precision) ? (uint8_t)clamped(precision, 0, 0, 3) : HA_PRECISION_AUTO;
+    const cJSON *ttl = cJSON_GetObjectItemCaseSensitive(item, "ttl_s");
+    long t = clamped(ttl, 0, 0, HA_TTL_MAX_S);
+    out->ttl_s = (uint32_t)(t == 0 ? 0 : t < HA_TTL_MIN_S ? HA_TTL_MIN_S : t);
+    return true;
+}
+
+bool ha_fields_from_json(const char *json, ha_fields_t *out, char *err, size_t err_size)
+{
+    if (util_json_depth(json) > MAX_DEPTH) {
+        return fail(err, err_size, "nested more than %d levels", MAX_DEPTH);
+    }
+    cJSON *root = json != NULL ? cJSON_Parse(json) : NULL;
+    if (root == NULL) {
+        return fail(err, err_size, "not valid JSON");
+    }
+    memset(out, 0, sizeof(*out));
+    bool ok = true;
+    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
+    const cJSON *fields = cJSON_GetObjectItemCaseSensitive(root, "fields");
+    if (!cJSON_IsNumber(schema) || schema->valueint != SCHEMA) {
+        ok = fail(err, err_size, "schema must be %d", SCHEMA);
+    } else if (fields != NULL && !cJSON_IsNull(fields) && !cJSON_IsArray(fields)) {
+        ok = fail(err, err_size, "fields must be a list");
+    } else if (cJSON_GetArraySize(fields) > HA_FIELDS_MAX) {
+        ok = fail(err, err_size, "at most %d MQTT fields", HA_FIELDS_MAX);
+    }
+    const cJSON *list = ok && cJSON_IsArray(fields) ? fields : NULL;
+    const cJSON *item;
+    cJSON_ArrayForEach(item, list)
+    {
+        ha_field_t *f = &out->field[out->count];
+        if (!parse_field(item, out->count + 1, f, err, err_size)) {
+            ok = false;
+            break;
+        }
+        if (ha_fields_find(out, f->key) >= 0) {
+            ok = fail(err, err_size, "duplicate key \"%s\"", f->key);
+            break;
+        }
+        out->count++;
+    }
+    cJSON_Delete(root);
+    return ok;
+}
+
+size_t ha_fields_to_json(const ha_fields_t *f, char *out, size_t size)
+{
+    cJSON *root = cJSON_CreateObject();
+    cJSON_AddNumberToObject(root, "schema", SCHEMA);
+    cJSON *fields = cJSON_AddArrayToObject(root, "fields");
+    for (int i = 0; i < f->count && i < HA_FIELDS_MAX; i++) {
+        const ha_field_t *x = &f->field[i];
+        cJSON *o = cJSON_CreateObject();
+        cJSON_AddStringToObject(o, "key", x->key);
+        cJSON_AddStringToObject(o, "label", x->label);
+        cJSON_AddStringToObject(o, "kind", x->kind == HA_KIND_TEXT ? "text" : "number");
+        cJSON_AddStringToObject(o, "unit", x->unit);
+        if (x->precision == HA_PRECISION_AUTO) {
+            cJSON_AddNullToObject(o, "precision");
+        } else {
+            cJSON_AddNumberToObject(o, "precision", x->precision);
+        }
+        cJSON_AddStringToObject(o, "topic", x->topic);
+        if (x->json_path[0] == '\0') {
+            cJSON_AddNullToObject(o, "json_path");
+        } else {
+            cJSON_AddStringToObject(o, "json_path", x->json_path);
+        }
+        cJSON_AddNumberToObject(o, "ttl_s", x->ttl_s);
+        cJSON_AddItemToArray(fields, o);
+    }
+    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
+    cJSON_Delete(root);
+    return ok ? strlen(out) : 0;
+}
+
+int ha_fields_find(const ha_fields_t *f, const char *key)
+{
+    for (int i = 0; key != NULL && i < f->count; i++) {
+        if (strcmp(f->field[i].key, key) == 0) {
+            return i;
+        }
+    }
+    return -1;
+}
```


Run: `cmake -S test/host -B build-host -G Ninja >/dev/null && cmake --build build-host --target test_ha_fields && ./build-host/test_ha_fields | tail -1`
Expected: `OK` (9 tests).

- [ ] **Step 3: Run the tests, and build.** A new component: ESP-IDF finds it only when CMake configures (AGENTS §6).

Run: `cmake --build build-host && ctest --test-dir build-host | tail -3; tools/idf.sh reconfigure >/dev/null && tools/idf.sh build 2>&1 | grep -c 'warning:'`
Expected: `100% tests passed, 0 tests failed out of 62`; `0`.

- [ ] **Step 4: Commit.**

```bash
git add components/ha_mqtt test/host/CMakeLists.txt test/host/test_ha_fields.c
git commit -m "feat(ha_mqtt): the MQTT field mappings (spec §12.5)"
```

### Task 3: MQTT topics and payloads (`ha_mqtt`)

**Files:**
- Create: `components/ha_mqtt/include/ha_payload.h`, `components/ha_mqtt/ha_payload.c`, `test/host/test_ha_payload.c`, `test/host/fixtures/ha/discovery.txt`
- Modify: `components/ha_mqtt/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `ha_fields.h` (Task 2); cJSON; `util_json_depth()`, `util_crc32()`.
- Produces (`ha_payload.h`, pure C):
  - `HA_TOPIC_MAX 128`, `HA_PAYLOAD_MAX 3072`, `HA_TEXT_LEN 48`, `HA_MESSAGE_LEN 97`;
  - `void ha_topic(char *out, size_t size, const char *id, const char *leaf)`: `reflbo/<id>/<leaf>`;
  - `typedef enum { HA_CMD_NONE, HA_CMD_PRESET, HA_CMD_NEXT, HA_CMD_SYNC, HA_CMD_MESSAGE } ha_cmd_t;`, `ha_cmd_t ha_cmd_parse(const char *id, const char *topic, size_t len)`, `bool ha_cmd_press(const char *payload, size_t len)`, `void ha_message_text(const char *payload, size_t len, char *out, size_t size)`;
  - `typedef struct { uint8_t kind; uint8_t decimals; int32_t number; char text[HA_TEXT_LEN]; } ha_value_t;` and `bool ha_value_parse(const ha_field_t *f, const char *payload, size_t len, ha_value_t *out)`;
  - `typedef struct { bool has_temp, has_hum, has_battery, has_rssi; int32_t temp_c100, hum_pct100; int bat_pct, bat_mv; const char *charging; int rssi; const char *preset_id, *preset_name; uint32_t last_sync; const char *fw; uint32_t uptime_s; } ha_state_t;`, `size_t ha_state_json(const ha_state_t *s, char *out, size_t size)`;
  - `uint32_t ha_expire_after_s(uint32_t expected_s)`;
  - `typedef struct { const char *id, *prefix, *fw; uint32_t expire_after_s; const char *const *presets; int preset_count; const char *broker; } ha_disc_t;`, `#define HA_DISC_COUNT 17`, `bool ha_disc_message(const ha_disc_t *d, int i, char *topic, size_t topic_size, char *payload, size_t payload_size)`, `uint32_t ha_disc_hash(const ha_disc_t *d)`.

What the device and Home Assistant say to each other (spec §12.2–§12.5), all of it testable without a broker. esp-mqtt hands over topics and payloads that aren't NUL-terminated, so every reader takes a length. A command is `reflbo/<id>/cmd/<name>`; button payloads are `PRESS`; the message keeps 96 bytes, control characters as spaces, cut at a character. A mapped value is the payload itself or its `json_path`'s value, a JSON number, string or boolean (a boolean is 1 or 0 for a number field, "on" or "off" for a text); a number keeps its own decimals up to 3 (fewer if the value is too large for them) or rounds half away from zero to the mapping's precision; a payload nested past 16 levels is never parsed (gotcha 30). The state carries the preset's id and, for the select's `value_template`, its name. Discovery (spec §12.3) has 17 configs under one `device` block: seven sensors (three of them diagnostics), the preset `select` with the preset names as options, the Sync now and Next preset buttons, the Message `notify` entity and six device triggers on `reflbo/<id>/action`; sensors expire after twice the expected interval plus 10 min, and in manual mode never. The golden file holds every config; the largest, the select's with 16 names of 23 control characters (JSON writes each in 6 bytes, `\u0001`), is 2 521 bytes of `HA_PAYLOAD_MAX`. The configs' hash, which NVS keeps once they went out, covers the broker (`host:port`) too, which is in no config: another broker hasn't seen them. It builds each config in memory it takes from the heap and gives back, not in a static buffer: internal RAM is scarce (AGENTS §8).

- [ ] **Step 1: Write the failing tests and the golden.**

`test/host/test_ha_payload.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/test/host/test_ha_payload.c
@@ -0,0 +1,301 @@
+#include <stdio.h>
+#include <stdlib.h>
+#include <string.h>
+
+#include "cJSON.h"
+#include "ha_payload.h"
+#include "unity.h"
+
+static char s_topic[HA_TOPIC_MAX], s_payload[HA_PAYLOAD_MAX];
+static ha_value_t s_v;
+
+void setUp(void)
+{
+    memset(&s_v, 0xAA, sizeof(s_v));
+}
+
+void tearDown(void) {}
+
+static void test_topics_are_under_the_device_id(void)
+{
+    ha_topic(s_topic, sizeof(s_topic), "reflbo-bb94", "state");
+    TEST_ASSERT_EQUAL_STRING("reflbo/reflbo-bb94/state", s_topic);
+    ha_topic(s_topic, sizeof(s_topic), "reflbo-bb94", "cmd/#");
+    TEST_ASSERT_EQUAL_STRING("reflbo/reflbo-bb94/cmd/#", s_topic);
+}
+
+/* spec §12.4: reflbo/<id>/cmd/<name>; anything else is no command. */
+static void test_commands_come_by_their_topic(void)
+{
+    static const struct {
+        const char *topic;
+        ha_cmd_t cmd;
+    } k_cases[] = {
+        { "reflbo/reflbo-bb94/cmd/preset", HA_CMD_PRESET }, { "reflbo/reflbo-bb94/cmd/next", HA_CMD_NEXT },
+        { "reflbo/reflbo-bb94/cmd/sync", HA_CMD_SYNC },     { "reflbo/reflbo-bb94/cmd/message", HA_CMD_MESSAGE },
+        { "reflbo/reflbo-bb94/cmd/reboot", HA_CMD_NONE },   { "reflbo/reflbo-aaaa/cmd/next", HA_CMD_NONE },
+        { "reflbo/reflbo-bb94/cmd/next/x", HA_CMD_NONE },   { "zigbee2mqtt/x", HA_CMD_NONE },
+        { "reflbo/reflbo-bb94/cmd/", HA_CMD_NONE },
+    };
+    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
+        TEST_ASSERT_EQUAL_MESSAGE(k_cases[i].cmd, ha_cmd_parse("reflbo-bb94", k_cases[i].topic, strlen(k_cases[i].topic)),
+                                  k_cases[i].topic);
+    }
+    /* esp-mqtt's topics aren't NUL-terminated: only `len` bytes count */
+    TEST_ASSERT_EQUAL(HA_CMD_NEXT, ha_cmd_parse("reflbo-bb94", "reflbo/reflbo-bb94/cmd/nextXYZ", 27));
+    TEST_ASSERT_TRUE(ha_cmd_press("PRESS", 5));
+    TEST_ASSERT_FALSE(ha_cmd_press("PRESSED", 7));
+    TEST_ASSERT_FALSE(ha_cmd_press("press", 5));
+}
+
+/* spec §12.7: up to 96 bytes, cut at a character; control characters as spaces. */
+static void test_a_message_is_cut_at_a_character(void)
+{
+    char text[HA_MESSAGE_LEN];
+    ha_message_text("Washing\nmachine done", 20, text, sizeof(text));
+    TEST_ASSERT_EQUAL_STRING("Washing machine done", text);
+    char long_text[128];
+    memset(long_text, 'x', 95);
+    snprintf(long_text + 95, sizeof(long_text) - 95, "%s", "čxyz"); /* "č" at bytes 95-96 */
+    ha_message_text(long_text, strlen(long_text), text, sizeof(text));
+    TEST_ASSERT_EQUAL_UINT(95, strlen(text));
+    ha_message_text("", 0, text, sizeof(text));
+    TEST_ASSERT_EQUAL_STRING("", text);
+}
+
+static ha_field_t field(ha_kind_t kind, uint8_t precision, const char *path)
+{
+    ha_field_t f = { .key = "k", .label = "K", .kind = (uint8_t)kind, .precision = precision, .topic = "t" };
+    snprintf(f.json_path, sizeof(f.json_path), "%s", path);
+    return f;
+}
+
+static bool parse(ha_field_t f, const char *payload)
+{
+    return ha_value_parse(&f, payload, strlen(payload), &s_v);
+}
+
+/* spec §12.5: the payload, or the json_path's value in it, as a number or a text. */
+static void test_numbers_keep_their_decimals_or_the_precision(void)
+{
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), "21.53"));
+    TEST_ASSERT_EQUAL_INT32(2153, s_v.number);
+    TEST_ASSERT_EQUAL_UINT8(2, s_v.decimals);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, 1, ""), " 21.56\n"));
+    TEST_ASSERT_EQUAL_INT32(216, s_v.number);
+    TEST_ASSERT_EQUAL_UINT8(1, s_v.decimals);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, 0, ""), "-3.5"));
+    TEST_ASSERT_EQUAL_INT32(-4, s_v.number); /* half away from zero */
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), "612"));
+    TEST_ASSERT_EQUAL_INT32(612, s_v.number);
+    TEST_ASSERT_EQUAL_UINT8(0, s_v.decimals);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), "3.14159"));
+    TEST_ASSERT_EQUAL_INT32(3142, s_v.number); /* 3 decimals at most */
+    TEST_ASSERT_EQUAL_UINT8(3, s_v.decimals);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, 3, ""), "1234567890"));
+    TEST_ASSERT_EQUAL_INT32(1234567890, s_v.number); /* too large for 3 decimals: fewer */
+    TEST_ASSERT_EQUAL_UINT8(0, s_v.decimals);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), "\"7.5\""));
+    TEST_ASSERT_EQUAL_INT32(75, s_v.number);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), "true"));
+    TEST_ASSERT_EQUAL_INT32(1, s_v.number);
+    static const char *const k_not_numbers[] = { "unavailable", "", "21.5 °C", "nan", "1e30", "{\"a\": 1}" };
+    for (size_t i = 0; i < sizeof(k_not_numbers) / sizeof(k_not_numbers[0]); i++) {
+        TEST_ASSERT_FALSE_MESSAGE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, ""), k_not_numbers[i]),
+                                  k_not_numbers[i]);
+    }
+}
+
+static void test_texts_are_cut_at_a_character(void)
+{
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), "Partly cloudy"));
+    TEST_ASSERT_EQUAL_STRING("Partly cloudy", s_v.text);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), "\"quoted\""));
+    TEST_ASSERT_EQUAL_STRING("quoted", s_v.text); /* a JSON string's value */
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), "21.5"));
+    TEST_ASSERT_EQUAL_STRING("21.5", s_v.text);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), "false"));
+    TEST_ASSERT_EQUAL_STRING("off", s_v.text);
+    char long_text[64];
+    memset(long_text, 'x', 46);
+    snprintf(long_text + 46, sizeof(long_text) - 46, "%s", "ěend"); /* "ě" at bytes 46-47 */
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), long_text));
+    TEST_ASSERT_EQUAL_UINT(46, strlen(s_v.text));
+    TEST_ASSERT_FALSE(parse(field(HA_KIND_TEXT, 0, ""), ""));
+}
+
+/* Zigbee2MQTT and HA's statestream attributes: JSON objects, read by dotted keys. */
+static void test_a_json_path_picks_the_value(void)
+{
+    const char *z2m = "{\"co2\": 612, \"update\": {\"state\": \"idle\", \"installed\": 2.5}, \"contact\": true}";
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, 0, "co2"), z2m));
+    TEST_ASSERT_EQUAL_INT32(612, s_v.number);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, "update.state"), z2m));
+    TEST_ASSERT_EQUAL_STRING("idle", s_v.text);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_NUMBER, HA_PRECISION_AUTO, "update.installed"), z2m));
+    TEST_ASSERT_EQUAL_INT32(25, s_v.number);
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, "contact"), z2m));
+    TEST_ASSERT_EQUAL_STRING("on", s_v.text);
+    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "voc"), z2m));
+    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "update"), z2m)); /* an object */
+    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "co2.x"), z2m));
+    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "co2"), "not json"));
+    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "co2"), "[612]"));
+}
+
+/* A payload nested past the cap is refused unparsed (AGENTS.md gotcha 30). */
+static void test_deep_payloads_are_refused(void)
+{
+    char deep[256];
+    size_t n = 0;
+    for (int i = 0; i < 40; i++) {
+        deep[n++] = '[';
+    }
+    for (int i = 0; i < 40; i++) {
+        deep[n++] = ']';
+    }
+    deep[n] = '\0';
+    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, "a"), deep));
+    TEST_ASSERT_FALSE(parse(field(HA_KIND_NUMBER, 0, ""), deep));
+    TEST_ASSERT_TRUE(parse(field(HA_KIND_TEXT, 0, ""), deep)); /* a text field shows it as it came, cut */
+    TEST_ASSERT_EQUAL_UINT(HA_TEXT_LEN - 1, strlen(s_v.text));
+}
+
+static ha_state_t sample_state(void)
+{
+    return (ha_state_t){ .has_temp = true, .temp_c100 = 2140, .has_hum = true, .hum_pct100 = 4520, .has_battery = true,
+                         .bat_pct = 78, .bat_mv = 3921, .charging = "discharging", .has_rssi = true, .rssi = -61,
+                         .preset_id = "home", .preset_name = "Home", .last_sync = 1790307012, .fw = "0.1.0",
+                         .uptime_s = 86400 };
+}
+
+/* spec §12.2's example, and the preset's name for the select. */
+static void test_the_state_json(void)
+{
+    ha_state_t st = sample_state();
+    TEST_ASSERT_TRUE(ha_state_json(&st, s_payload, sizeof(s_payload)) > 0);
+    TEST_ASSERT_EQUAL_STRING("{\"temp\":21.4,\"hum\":45.2,\"bat_pct\":78,\"bat_v\":3.92,\"charging\":\"discharging\","
+                             "\"rssi\":-61,\"preset\":\"home\",\"preset_name\":\"Home\","
+                             "\"last_sync\":\"2026-09-25T03:30:12Z\",\"fw\":\"0.1.0\",\"uptime_s\":86400}",
+                             s_payload);
+    st = (ha_state_t){ .charging = "unknown", .preset_id = "home", .preset_name = "Home", .fw = "0.1.0" };
+    TEST_ASSERT_TRUE(ha_state_json(&st, s_payload, sizeof(s_payload)) > 0);
+    TEST_ASSERT_EQUAL_STRING("{\"temp\":null,\"hum\":null,\"bat_pct\":null,\"bat_v\":null,\"charging\":\"unknown\","
+                             "\"rssi\":null,\"preset\":\"home\",\"preset_name\":\"Home\",\"last_sync\":null,"
+                             "\"fw\":\"0.1.0\",\"uptime_s\":0}",
+                             s_payload);
+    TEST_ASSERT_EQUAL_UINT(0, ha_state_json(&st, s_payload, 16));
+}
+
+/* spec §12.3: twice the expected interval plus 10 min; none without one (manual mode). */
+static void test_expire_after(void)
+{
+    TEST_ASSERT_EQUAL_UINT32(2 * 86400 + 600, ha_expire_after_s(86400));
+    TEST_ASSERT_EQUAL_UINT32(1800, ha_expire_after_s(600));
+    TEST_ASSERT_EQUAL_UINT32(0, ha_expire_after_s(0));
+}
+
+static const char *const k_presets[] = { "Home", "Indoor", "Weather", "Focus clock", "Rain radar", "Flights" };
+
+static ha_disc_t discovery(void)
+{
+    return (ha_disc_t){ .id = "reflbo-bb94", .prefix = "homeassistant", .fw = "0.1.0", .expire_after_s = 173400,
+                        .presets = k_presets, .preset_count = 6, .broker = "192.168.1.10:1883" };
+}
+
+/* Golden JSON for every entity (spec §17): test/host/fixtures/ha/discovery.txt holds each topic and payload
+ * on two lines, a blank line after each. */
+static void test_discovery_matches_its_golden(void)
+{
+    static char golden[32768], made[32768];
+    FILE *f = fopen(FIXTURE_DIR "/discovery.txt", "rb");
+    TEST_ASSERT_NOT_NULL_MESSAGE(f, FIXTURE_DIR "/discovery.txt");
+    size_t n = fread(golden, 1, sizeof(golden) - 1, f);
+    fclose(f);
+    golden[n] = '\0';
+    ha_disc_t d = discovery();
+    size_t at = 0;
+    for (int i = 0; i < HA_DISC_COUNT; i++) {
+        TEST_ASSERT_TRUE(ha_disc_message(&d, i, s_topic, sizeof(s_topic), s_payload, sizeof(s_payload)));
+        at += (size_t)snprintf(made + at, sizeof(made) - at, "%s\n%s\n\n", s_topic, s_payload);
+        cJSON *json = cJSON_Parse(s_payload);
+        TEST_ASSERT_NOT_NULL_MESSAGE(json, s_topic);
+        cJSON_Delete(json);
+    }
+    if (strcmp(golden, made) != 0) {
+        FILE *out = fopen("discovery.actual.txt", "wb");
+        if (out != NULL) {
+            fputs(made, out);
+            fclose(out);
+        }
+    }
+    TEST_ASSERT_EQUAL_STRING(golden, made);
+    TEST_ASSERT_FALSE(ha_disc_message(&d, HA_DISC_COUNT, s_topic, sizeof(s_topic), s_payload, sizeof(s_payload)));
+}
+
+/* Discovery goes out again when its hash changes (spec §12.3): the preset names, the expected interval, the
+ * prefix, the firmware, or the broker it goes to. */
+static void test_the_hash_follows_what_discovery_says(void)
+{
+    ha_disc_t d = discovery();
+    uint32_t h = ha_disc_hash(&d);
+    TEST_ASSERT_EQUAL_UINT32(h, ha_disc_hash(&d));
+    const char *renamed[] = { "Home", "Indoor", "Weather", "Focus", "Rain radar", "Flights" };
+    ha_disc_t e = d;
+    e.presets = renamed;
+    TEST_ASSERT_NOT_EQUAL(h, ha_disc_hash(&e));
+    e = d;
+    e.expire_after_s = 0;
+    TEST_ASSERT_NOT_EQUAL(h, ha_disc_hash(&e));
+    e = d;
+    e.prefix = "ha";
+    TEST_ASSERT_NOT_EQUAL(h, ha_disc_hash(&e));
+    e = d;
+    e.fw = "0.2.0";
+    TEST_ASSERT_NOT_EQUAL(h, ha_disc_hash(&e));
+    e = d;
+    e.broker = "homeassistant.local:1883"; /* another broker hasn't seen them */
+    TEST_ASSERT_NOT_EQUAL(h, ha_disc_hash(&e));
+}
+
+/* The largest discovery message: 16 presets with the longest names in the select, of control characters,
+ * which JSON writes in 6 bytes each ("\u0001"). Every config fits. */
+static void test_the_largest_discovery_message_fits(void)
+{
+    static char names[16][24];
+    const char *list[16];
+    for (int i = 0; i < 16; i++) {
+        memset(names[i], 0x01, 22);
+        names[i][22] = (char)('a' + i);
+        list[i] = names[i];
+    }
+    ha_disc_t d = discovery();
+    d.presets = list;
+    d.preset_count = 16;
+    d.prefix = "a234567890123456789012345678901"; /* 31 bytes */
+    int largest = 0;
+    for (int i = 0; i < HA_DISC_COUNT; i++) {
+        TEST_ASSERT_TRUE(ha_disc_message(&d, i, s_topic, sizeof(s_topic), s_payload, sizeof(s_payload)));
+        int len = (int)strlen(s_payload);
+        largest = len > largest ? len : largest;
+    }
+    printf("the largest discovery payload: %d bytes of %d\n", largest, HA_PAYLOAD_MAX);
+}
+
+int main(void)
+{
+    UNITY_BEGIN();
+    RUN_TEST(test_topics_are_under_the_device_id);
+    RUN_TEST(test_commands_come_by_their_topic);
+    RUN_TEST(test_a_message_is_cut_at_a_character);
+    RUN_TEST(test_numbers_keep_their_decimals_or_the_precision);
+    RUN_TEST(test_texts_are_cut_at_a_character);
+    RUN_TEST(test_a_json_path_picks_the_value);
+    RUN_TEST(test_deep_payloads_are_refused);
+    RUN_TEST(test_the_state_json);
+    RUN_TEST(test_expire_after);
+    RUN_TEST(test_discovery_matches_its_golden);
+    RUN_TEST(test_the_hash_follows_what_discovery_says);
+    RUN_TEST(test_the_largest_discovery_message_fits);
+    return UNITY_END();
+}
```


`test/host/fixtures/ha/discovery.txt`:

```diff
new file mode 100644
--- /dev/null
+++ b/test/host/fixtures/ha/discovery.txt
@@ -0,0 +1,51 @@
+homeassistant/sensor/reflbo-bb94/temperature/config
+{"name":"Temperature","unique_id":"reflbo-bb94_temperature","state_topic":"reflbo/reflbo-bb94/state","value_template":"{{ value_json.temp }}","device_class":"temperature","unit_of_measurement":"°C","state_class":"measurement","expire_after":173400,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/sensor/reflbo-bb94/humidity/config
+{"name":"Humidity","unique_id":"reflbo-bb94_humidity","state_topic":"reflbo/reflbo-bb94/state","value_template":"{{ value_json.hum }}","device_class":"humidity","unit_of_measurement":"%","state_class":"measurement","expire_after":173400,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/sensor/reflbo-bb94/battery/config
+{"name":"Battery","unique_id":"reflbo-bb94_battery","state_topic":"reflbo/reflbo-bb94/state","value_template":"{{ value_json.bat_pct }}","device_class":"battery","unit_of_measurement":"%","state_class":"measurement","expire_after":173400,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/sensor/reflbo-bb94/charging/config
+{"name":"Charging","unique_id":"reflbo-bb94_charging","state_topic":"reflbo/reflbo-bb94/state","value_template":"{{ value_json.charging }}","device_class":"enum","options":["unknown","discharging","charging","full"],"expire_after":173400,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/sensor/reflbo-bb94/battery_voltage/config
+{"name":"Battery voltage","unique_id":"reflbo-bb94_battery_voltage","state_topic":"reflbo/reflbo-bb94/state","value_template":"{{ value_json.bat_v }}","device_class":"voltage","unit_of_measurement":"V","state_class":"measurement","entity_category":"diagnostic","expire_after":173400,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/sensor/reflbo-bb94/rssi/config
+{"name":"Wi-Fi signal","unique_id":"reflbo-bb94_rssi","state_topic":"reflbo/reflbo-bb94/state","value_template":"{{ value_json.rssi }}","device_class":"signal_strength","unit_of_measurement":"dBm","state_class":"measurement","entity_category":"diagnostic","expire_after":173400,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/sensor/reflbo-bb94/last_sync/config
+{"name":"Last sync","unique_id":"reflbo-bb94_last_sync","state_topic":"reflbo/reflbo-bb94/state","value_template":"{{ value_json.last_sync }}","device_class":"timestamp","entity_category":"diagnostic","expire_after":173400,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/select/reflbo-bb94/preset/config
+{"name":"Preset","unique_id":"reflbo-bb94_preset","state_topic":"reflbo/reflbo-bb94/state","value_template":"{{ value_json.preset_name }}","command_topic":"reflbo/reflbo-bb94/cmd/preset","options":["Home","Indoor","Weather","Focus clock","Rain radar","Flights"],"qos":1,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/button/reflbo-bb94/sync_now/config
+{"name":"Sync now","unique_id":"reflbo-bb94_sync_now","command_topic":"reflbo/reflbo-bb94/cmd/sync","payload_press":"PRESS","qos":1,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/button/reflbo-bb94/next_preset/config
+{"name":"Next preset","unique_id":"reflbo-bb94_next_preset","command_topic":"reflbo/reflbo-bb94/cmd/next","payload_press":"PRESS","qos":1,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/notify/reflbo-bb94/message/config
+{"name":"Message","unique_id":"reflbo-bb94_message","command_topic":"reflbo/reflbo-bb94/cmd/message","qos":1,"device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/device_automation/reflbo-bb94/key_short/config
+{"automation_type":"trigger","topic":"reflbo/reflbo-bb94/action","type":"button_short_press","subtype":"button_1","payload":"key_short","device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/device_automation/reflbo-bb94/key_double/config
+{"automation_type":"trigger","topic":"reflbo/reflbo-bb94/action","type":"button_double_press","subtype":"button_1","payload":"key_double","device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/device_automation/reflbo-bb94/key_long/config
+{"automation_type":"trigger","topic":"reflbo/reflbo-bb94/action","type":"button_long_press","subtype":"button_1","payload":"key_long","device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/device_automation/reflbo-bb94/boot_short/config
+{"automation_type":"trigger","topic":"reflbo/reflbo-bb94/action","type":"button_short_press","subtype":"button_2","payload":"boot_short","device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/device_automation/reflbo-bb94/boot_double/config
+{"automation_type":"trigger","topic":"reflbo/reflbo-bb94/action","type":"button_double_press","subtype":"button_2","payload":"boot_double","device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
+homeassistant/device_automation/reflbo-bb94/boot_long/config
+{"automation_type":"trigger","topic":"reflbo/reflbo-bb94/action","type":"button_long_press","subtype":"button_2","payload":"boot_long","device":{"identifiers":["reflbo-bb94"],"name":"reflbo-bb94","model":"ESP32-S3-RLCD-4.2","manufacturer":"Waveshare","sw_version":"0.1.0"}}
+
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -175,7 +175,8 @@ target_compile_options(storage_logic PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(storage_logic PRIVATE cjson util m)
 
 # ha_mqtt: MQTT and Home Assistant's mappings, payloads and store build on the host; the client does not.
-add_library(ha_mqtt_logic STATIC ${REPO_ROOT}/components/ha_mqtt/ha_fields.c)
+add_library(ha_mqtt_logic STATIC ${REPO_ROOT}/components/ha_mqtt/ha_fields.c
+            ${REPO_ROOT}/components/ha_mqtt/ha_payload.c)
 target_include_directories(ha_mqtt_logic PUBLIC ${REPO_ROOT}/components/ha_mqtt/include)
 target_compile_options(ha_mqtt_logic PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(ha_mqtt_logic PRIVATE cjson util m)
@@ -267,6 +268,8 @@ reflbo_host_test(test_storage_file storage_logic)
 target_compile_definitions(test_storage_file PRIVATE TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/storage_tmp")
 reflbo_host_test(test_storage_backup storage_logic)
 reflbo_host_test(test_ha_fields ha_mqtt_logic)
+reflbo_host_test(test_ha_payload ha_mqtt_logic cjson)
+target_compile_definitions(test_ha_payload PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ha")
 
 reflbo_host_test(test_test_pattern_golden gfx)
 target_compile_definitions(test_test_pattern_golden PRIVATE GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
```


Run: `cmake -S test/host -B build-host -G Ninja 2>&1 | grep -m1 -A1 'CMake Error'`
Expected: `CMake Error at CMakeLists.txt:178 (add_library):`, then `Cannot find source file:` and the path of `components/ha_mqtt/ha_payload.c`.

- [ ] **Step 2: The payloads.**

`components/ha_mqtt/CMakeLists.txt`:

```diff
--- a/components/ha_mqtt/CMakeLists.txt
+++ b/components/ha_mqtt/CMakeLists.txt
@@ -1,5 +1,5 @@
-# MQTT and Home Assistant (spec §12, D32). ha_fields.c (the field mappings) is pure C and also builds on
-# the host.
-idf_component_register(SRCS "ha_fields.c"
+# MQTT and Home Assistant (spec §12, D32). ha_fields.c (the field mappings) and ha_payload.c (topics and
+# payloads) are pure C and also build on the host.
+idf_component_register(SRCS "ha_fields.c" "ha_payload.c"
                        INCLUDE_DIRS "include"
                        PRIV_REQUIRES json util)
```


`components/ha_mqtt/include/ha_payload.h`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/include/ha_payload.h
@@ -0,0 +1,89 @@
+#pragma once
+
+#include <stdbool.h>
+#include <stddef.h>
+#include <stdint.h>
+
+#include "ha_fields.h"
+
+/*
+ * What the device and Home Assistant say to each other over MQTT (spec §12.2-§12.5, D32): the topics,
+ * the discovery configs, the state, the commands and the mapped fields' values. Pure C on cJSON,
+ * host-buildable.
+ */
+
+#define HA_TOPIC_MAX 128   /* the longest topic the device builds: a discovery config's */
+#define HA_PAYLOAD_MAX 3072 /* the largest payload it builds: the select's config, 16 names of control characters */
+#define HA_TEXT_LEN 48     /* a text value, up to 47 bytes */
+#define HA_MESSAGE_LEN 97  /* the message, up to 96 bytes (spec §12.7) */
+
+/* "reflbo/<id>/<leaf>": the state, the action, the commands. */
+void ha_topic(char *out, size_t size, const char *id, const char *leaf);
+
+typedef enum {
+    HA_CMD_NONE,
+    HA_CMD_PRESET,  /* cmd/preset: a preset's id or name */
+    HA_CMD_NEXT,    /* cmd/next: PRESS */
+    HA_CMD_SYNC,    /* cmd/sync: PRESS */
+    HA_CMD_MESSAGE, /* cmd/message: the text; "" clears it */
+} ha_cmd_t;
+
+/* The command reflbo/<id>/cmd/<name> carries (its first `len` bytes; esp-mqtt's aren't terminated). */
+ha_cmd_t ha_cmd_parse(const char *id, const char *topic, size_t len);
+/* A button's payload, as discovery sets it: "PRESS". */
+bool ha_cmd_press(const char *payload, size_t len);
+/* The message's text: control characters as spaces, cut at a character to 96 bytes. */
+void ha_message_text(const char *payload, size_t len, char *out, size_t size);
+
+typedef struct {
+    uint8_t kind;     /* ha_kind_t */
+    uint8_t decimals; /* a number's: `number` is the value times 10^decimals */
+    int32_t number;
+    char text[HA_TEXT_LEN];
+} ha_value_t;
+
+/* A mapped topic's payload as field `f` reads it (spec §12.5): its json_path's value if it has one, else
+ * the payload itself, which may also be a JSON number, string or boolean. A number field takes a number,
+ * a boolean (1 or 0) or a string holding a number, rounded half away from zero to its precision, or to its
+ * own decimals up to 3 (fewer if it is too large for them); a text field takes anything but an object or
+ * a list (a boolean reads "on" or "off"), cut at a character. False if there is no such value. */
+bool ha_value_parse(const ha_field_t *f, const char *payload, size_t len, ha_value_t *out);
+
+/* The device's state (spec §12.2): what reflbo/<id>/state carries, retained. */
+typedef struct {
+    bool has_temp, has_hum, has_battery, has_rssi;
+    int32_t temp_c100;    /* 0.01 °C */
+    int32_t hum_pct100;   /* 0.01 % */
+    int bat_pct, bat_mv;
+    const char *charging; /* "unknown", "discharging", "charging", "full" */
+    int rssi;             /* dBm, on the network */
+    const char *preset_id, *preset_name;
+    uint32_t last_sync; /* UTC of the last sync that worked; 0 for none */
+    const char *fw;
+    uint32_t uptime_s;
+} ha_state_t;
+
+size_t ha_state_json(const ha_state_t *s, char *out, size_t size); /* 0 if `size` is too small */
+
+/* spec §12.3: twice the expected interval plus 10 min; 0 (none) without one. */
+uint32_t ha_expire_after_s(uint32_t expected_s);
+
+/* Discovery (spec §12.3): the sensors, the preset's select, two buttons, the message's notify entity and
+ * six device triggers, under one device. */
+typedef struct {
+    const char *id;     /* "reflbo-bb94": the device, its client id and its topics' node */
+    const char *prefix; /* "homeassistant" */
+    const char *fw;
+    uint32_t expire_after_s; /* the sensors'; 0 leaves it out (manual mode) */
+    const char *const *presets; /* the select's options: the presets' names */
+    int preset_count;
+    const char *broker; /* "host:port": in no config, but in the hash, as another broker hasn't seen them */
+} ha_disc_t;
+
+#define HA_DISC_COUNT 17
+/* Config `i` of HA_DISC_COUNT: its topic and its payload, retained. False past the last, or if one
+ * doesn't fit. */
+bool ha_disc_message(const ha_disc_t *d, int i, char *topic, size_t topic_size, char *payload,
+                     size_t payload_size);
+/* CRC-32 of the broker and every config's topic and payload: what NVS sys/mqtt_disc keeps once they went out. */
+uint32_t ha_disc_hash(const ha_disc_t *d);
```


`components/ha_mqtt/ha_payload.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/ha_payload.c
@@ -0,0 +1,366 @@
+#define _POSIX_C_SOURCE 200809L /* gmtime_r() on the host */
+
+#include "ha_payload.h"
+
+#include <ctype.h>
+#include <math.h>
+#include <stdio.h>
+#include <stdlib.h>
+#include <string.h>
+#include <time.h>
+
+#include "cJSON.h"
+#include "util_crc32.h"
+#include "util_json.h"
+
+#define MAX_DEPTH 16 /* a payload nested deeper is never parsed (AGENTS.md gotcha 30) */
+#define MODEL "ESP32-S3-RLCD-4.2"
+#define MANUFACTURER "Waveshare"
+
+void ha_topic(char *out, size_t size, const char *id, const char *leaf)
+{
+    snprintf(out, size, "reflbo/%s/%s", id, leaf);
+}
+
+ha_cmd_t ha_cmd_parse(const char *id, const char *topic, size_t len)
+{
+    char prefix[HA_TOPIC_MAX];
+    ha_topic(prefix, sizeof(prefix), id, "cmd/");
+    size_t p = strlen(prefix);
+    if (len <= p || memcmp(topic, prefix, p) != 0) {
+        return HA_CMD_NONE;
+    }
+    static const struct {
+        const char *name;
+        ha_cmd_t cmd;
+    } k_cmds[] = { { "preset", HA_CMD_PRESET }, { "next", HA_CMD_NEXT }, { "sync", HA_CMD_SYNC },
+                   { "message", HA_CMD_MESSAGE } };
+    for (size_t i = 0; i < sizeof(k_cmds) / sizeof(k_cmds[0]); i++) {
+        if (len - p == strlen(k_cmds[i].name) && memcmp(topic + p, k_cmds[i].name, len - p) == 0) {
+            return k_cmds[i].cmd;
+        }
+    }
+    return HA_CMD_NONE;
+}
+
+bool ha_cmd_press(const char *payload, size_t len)
+{
+    return len == 5 && memcmp(payload, "PRESS", 5) == 0;
+}
+
+/* `len` bytes of `text`, control characters as spaces, cut at a character to fit `size`. */
+static void copy_cut(const char *text, size_t len, char *out, size_t size)
+{
+    size_t n = len;
+    if (n >= size) {
+        n = size - 1;
+        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) { /* text[n] continues a sequence */
+            n--;
+        }
+    }
+    for (size_t i = 0; i < n; i++) {
+        unsigned char c = (unsigned char)text[i];
+        out[i] = c < ' ' || c == 0x7F ? ' ' : (char)c;
+    }
+    out[n] = '\0';
+}
+
+void ha_message_text(const char *payload, size_t len, char *out, size_t size)
+{
+    copy_cut(payload, len, out, size < HA_MESSAGE_LEN ? size : HA_MESSAGE_LEN);
+}
+
+/* A number as field `f` keeps it: rounded to its precision, or to its own decimals up to 3; fewer when
+ * the value with them doesn't fit an int32. */
+static bool scale_number(const ha_field_t *f, double v, ha_value_t *out)
+{
+    if (!isfinite(v)) {
+        return false;
+    }
+    int d = f->precision;
+    if (f->precision == HA_PRECISION_AUTO) {
+        for (d = 0; d < 3; d++) {
+            double x = v * pow(10, d);
+            if (fabs(x - round(x)) < 1e-6) {
+                break;
+            }
+        }
+    }
+    for (; d >= 0; d--) {
+        double x = round(v * pow(10, d)); /* half away from zero */
+        if (fabs(x) <= 2147483647.0) {
+            out->number = (int32_t)x;
+            out->decimals = (uint8_t)d;
+            return true;
+        }
+    }
+    return false;
+}
+
+/* A string holding a number, all of it: " 21.5 " yes, "21.5 °C" no. */
+static bool number_text(const char *s, double *v)
+{
+    char *end;
+    *v = strtod(s, &end);
+    if (end == s) {
+        return false;
+    }
+    while (isspace((unsigned char)*end)) {
+        end++;
+    }
+    return *end == '\0';
+}
+
+static bool from_item(const ha_field_t *f, const cJSON *item, ha_value_t *out)
+{
+    if (f->kind == HA_KIND_NUMBER) {
+        double v;
+        if (cJSON_IsNumber(item)) {
+            v = item->valuedouble;
+        } else if (cJSON_IsBool(item)) {
+            v = cJSON_IsTrue(item) ? 1 : 0;
+        } else if (!cJSON_IsString(item) || !number_text(item->valuestring, &v)) {
+            return false;
+        }
+        return scale_number(f, v, out);
+    }
+    char text[32];
+    const char *s = text;
+    if (cJSON_IsString(item)) {
+        s = item->valuestring;
+    } else if (cJSON_IsNumber(item)) {
+        snprintf(text, sizeof(text), "%.15g", item->valuedouble);
+    } else if (cJSON_IsBool(item)) {
+        s = cJSON_IsTrue(item) ? "on" : "off";
+    } else {
+        return false; /* null, an object or a list */
+    }
+    copy_cut(s, strlen(s), out->text, sizeof(out->text));
+    return out->text[0] != '\0';
+}
+
+bool ha_value_parse(const ha_field_t *f, const char *payload, size_t len, ha_value_t *out)
+{
+    memset(out, 0, sizeof(*out));
+    out->kind = f->kind;
+    char *text = malloc(len + 1);
+    if (text == NULL) {
+        return false;
+    }
+    memcpy(text, payload, len);
+    text[len] = '\0';
+    char *start = text, *end = text + len; /* the payload without the space around it */
+    while (start < end && isspace((unsigned char)*start)) {
+        start++;
+    }
+    while (end > start && isspace((unsigned char)end[-1])) {
+        *--end = '\0';
+    }
+    bool ok = false;
+    cJSON *root = util_json_depth(start) <= MAX_DEPTH ? cJSON_ParseWithOpts(start, NULL, true) : NULL;
+    if (f->json_path[0] != '\0') {
+        const cJSON *item = root;
+        char path[HA_PATH_LEN];
+        snprintf(path, sizeof(path), "%s", f->json_path);
+        for (char *key = strtok(path, "."); key != NULL && item != NULL; key = strtok(NULL, ".")) {
+            item = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, key) : NULL;
+        }
+        ok = item != NULL && from_item(f, item, out);
+    } else if (root != NULL) {
+        ok = from_item(f, root, out); /* a JSON number, string or boolean */
+    } else if (f->kind == HA_KIND_NUMBER) {
+        double v;
+        ok = number_text(start, &v) && scale_number(f, v, out);
+    } else {
+        copy_cut(start, strlen(start), out->text, sizeof(out->text)); /* plain text, as it came */
+        ok = out->text[0] != '\0';
+    }
+    cJSON_Delete(root);
+    free(text);
+    return ok;
+}
+
+static void add_tenths(cJSON *o, const char *key, bool has, int32_t hundredths)
+{
+    if (has) {
+        cJSON_AddNumberToObject(o, key, lround(hundredths / 10.0) / 10.0);
+    } else {
+        cJSON_AddNullToObject(o, key);
+    }
+}
+
+size_t ha_state_json(const ha_state_t *s, char *out, size_t size)
+{
+    cJSON *o = cJSON_CreateObject();
+    add_tenths(o, "temp", s->has_temp, s->temp_c100);
+    add_tenths(o, "hum", s->has_hum, s->hum_pct100);
+    if (s->has_battery) {
+        cJSON_AddNumberToObject(o, "bat_pct", s->bat_pct);
+        cJSON_AddNumberToObject(o, "bat_v", lround(s->bat_mv / 10.0) / 100.0);
+    } else {
+        cJSON_AddNullToObject(o, "bat_pct");
+        cJSON_AddNullToObject(o, "bat_v");
+    }
+    cJSON_AddStringToObject(o, "charging", s->charging != NULL ? s->charging : "unknown");
+    if (s->has_rssi) {
+        cJSON_AddNumberToObject(o, "rssi", s->rssi);
+    } else {
+        cJSON_AddNullToObject(o, "rssi");
+    }
+    cJSON_AddStringToObject(o, "preset", s->preset_id != NULL ? s->preset_id : "");
+    cJSON_AddStringToObject(o, "preset_name", s->preset_name != NULL ? s->preset_name : "");
+    if (s->last_sync != 0) {
+        time_t t = (time_t)s->last_sync;
+        struct tm tm;
+        char iso[24];
+        gmtime_r(&t, &tm);
+        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm);
+        cJSON_AddStringToObject(o, "last_sync", iso);
+    } else {
+        cJSON_AddNullToObject(o, "last_sync");
+    }
+    cJSON_AddStringToObject(o, "fw", s->fw != NULL ? s->fw : "");
+    cJSON_AddNumberToObject(o, "uptime_s", s->uptime_s);
+    bool ok = size > 0 && cJSON_PrintPreallocated(o, out, (int)size, false);
+    cJSON_Delete(o);
+    return ok ? strlen(out) : 0;
+}
+
+uint32_t ha_expire_after_s(uint32_t expected_s)
+{
+    return expected_s == 0 ? 0 : 2 * expected_s + 600;
+}
+
+typedef enum { E_SENSOR, E_SELECT, E_BUTTON, E_NOTIFY, E_TRIGGER } entity_kind_t;
+
+/* spec §12.3, in the order the configs go out. */
+static const struct {
+    entity_kind_t kind;
+    const char *object; /* the config's topic and unique id */
+    const char *name;   /* a sensor's, select's, button's or notify entity's name; a trigger's type */
+    const char *value;  /* a sensor's key in the state; a button's command; a trigger's payload */
+    const char *device_class; /* a sensor's; a trigger's subtype */
+    const char *unit;
+    bool diagnostic;
+} k_entities[HA_DISC_COUNT] = {
+    { E_SENSOR, "temperature", "Temperature", "temp", "temperature", "\xC2\xB0" "C", false },
+    { E_SENSOR, "humidity", "Humidity", "hum", "humidity", "%", false },
+    { E_SENSOR, "battery", "Battery", "bat_pct", "battery", "%", false },
+    { E_SENSOR, "charging", "Charging", "charging", "enum", NULL, false },
+    { E_SENSOR, "battery_voltage", "Battery voltage", "bat_v", "voltage", "V", true },
+    { E_SENSOR, "rssi", "Wi-Fi signal", "rssi", "signal_strength", "dBm", true },
+    { E_SENSOR, "last_sync", "Last sync", "last_sync", "timestamp", NULL, true },
+    { E_SELECT, "preset", "Preset", NULL, NULL, NULL, false },
+    { E_BUTTON, "sync_now", "Sync now", "sync", NULL, NULL, false },
+    { E_BUTTON, "next_preset", "Next preset", "next", NULL, NULL, false },
+    { E_NOTIFY, "message", "Message", "message", NULL, NULL, false },
+    { E_TRIGGER, "key_short", "button_short_press", "key_short", "button_1", NULL, false },
+    { E_TRIGGER, "key_double", "button_double_press", "key_double", "button_1", NULL, false },
+    { E_TRIGGER, "key_long", "button_long_press", "key_long", "button_1", NULL, false },
+    { E_TRIGGER, "boot_short", "button_short_press", "boot_short", "button_2", NULL, false },
+    { E_TRIGGER, "boot_double", "button_double_press", "boot_double", "button_2", NULL, false },
+    { E_TRIGGER, "boot_long", "button_long_press", "boot_long", "button_2", NULL, false },
+};
+
+static const char *component(entity_kind_t kind)
+{
+    static const char *const k_components[] = { "sensor", "select", "button", "notify", "device_automation" };
+    return k_components[kind];
+}
+
+bool ha_disc_message(const ha_disc_t *d, int i, char *topic, size_t topic_size, char *payload,
+                     size_t payload_size)
+{
+    if (i < 0 || i >= HA_DISC_COUNT) {
+        return false;
+    }
+    entity_kind_t kind = k_entities[i].kind;
+    int n = snprintf(topic, topic_size, "%s/%s/%s/%s/config", d->prefix, component(kind), d->id, k_entities[i].object);
+    if (n < 0 || (size_t)n >= topic_size) {
+        return false;
+    }
+    char text[HA_TOPIC_MAX];
+    cJSON *o = cJSON_CreateObject();
+    if (kind == E_TRIGGER) {
+        cJSON_AddStringToObject(o, "automation_type", "trigger");
+        ha_topic(text, sizeof(text), d->id, "action");
+        cJSON_AddStringToObject(o, "topic", text);
+        cJSON_AddStringToObject(o, "type", k_entities[i].name);
+        cJSON_AddStringToObject(o, "subtype", k_entities[i].device_class);
+        cJSON_AddStringToObject(o, "payload", k_entities[i].value);
+    } else {
+        cJSON_AddStringToObject(o, "name", k_entities[i].name);
+        snprintf(text, sizeof(text), "%s_%s", d->id, k_entities[i].object);
+        cJSON_AddStringToObject(o, "unique_id", text);
+    }
+    if (kind == E_SENSOR || kind == E_SELECT) {
+        ha_topic(text, sizeof(text), d->id, "state");
+        cJSON_AddStringToObject(o, "state_topic", text);
+        snprintf(text, sizeof(text), "{{ value_json.%s }}", kind == E_SELECT ? "preset_name" : k_entities[i].value);
+        cJSON_AddStringToObject(o, "value_template", text);
+    }
+    if (kind == E_SENSOR) {
+        cJSON_AddStringToObject(o, "device_class", k_entities[i].device_class);
+        if (k_entities[i].unit != NULL) {
+            cJSON_AddStringToObject(o, "unit_of_measurement", k_entities[i].unit);
+            cJSON_AddStringToObject(o, "state_class", "measurement");
+        }
+        if (strcmp(k_entities[i].device_class, "enum") == 0) {
+            const char *options[] = { "unknown", "discharging", "charging", "full" };
+            cJSON_AddItemToObject(o, "options", cJSON_CreateStringArray(options, 4));
+        }
+        if (k_entities[i].diagnostic) {
+            cJSON_AddStringToObject(o, "entity_category", "diagnostic");
+        }
+        if (d->expire_after_s != 0) {
+            cJSON_AddNumberToObject(o, "expire_after", d->expire_after_s);
+        }
+    }
+    if (kind == E_SELECT || kind == E_BUTTON || kind == E_NOTIFY) {
+        snprintf(text, sizeof(text), "cmd/%s", kind == E_SELECT ? "preset" : k_entities[i].value);
+        char cmd[HA_TOPIC_MAX];
+        ha_topic(cmd, sizeof(cmd), d->id, text);
+        cJSON_AddStringToObject(o, "command_topic", cmd);
+        if (kind == E_SELECT) {
+            cJSON *options = cJSON_AddArrayToObject(o, "options");
+            for (int p = 0; p < d->preset_count; p++) {
+                cJSON_AddItemToArray(options, cJSON_CreateString(d->presets[p]));
+            }
+        }
+        if (kind == E_BUTTON) {
+            cJSON_AddStringToObject(o, "payload_press", "PRESS");
+        }
+        cJSON_AddNumberToObject(o, "qos", 1); /* commands wait in the persistent session (spec §12.4) */
+    }
+    cJSON *dev = cJSON_AddObjectToObject(o, "device");
+    cJSON *ids = cJSON_AddArrayToObject(dev, "identifiers");
+    cJSON_AddItemToArray(ids, cJSON_CreateString(d->id));
+    cJSON_AddStringToObject(dev, "name", d->id);
+    cJSON_AddStringToObject(dev, "model", MODEL);
+    cJSON_AddStringToObject(dev, "manufacturer", MANUFACTURER);
+    cJSON_AddStringToObject(dev, "sw_version", d->fw);
+    bool ok = payload_size > 0 && cJSON_PrintPreallocated(o, payload, (int)payload_size, false);
+    cJSON_Delete(o);
+    return ok;
+}
+
+uint32_t ha_disc_hash(const ha_disc_t *d)
+{
+    char *topic = malloc(HA_TOPIC_MAX), *payload = malloc(HA_PAYLOAD_MAX); /* not for long: no static */
+    uint32_t crc = 0;
+    if (d->broker != NULL) {
+        crc = util_crc32(crc, d->broker, strlen(d->broker) + 1);
+    }
+    for (int i = 0; topic != NULL && payload != NULL && i < HA_DISC_COUNT; i++) {
+        if (ha_disc_message(d, i, topic, HA_TOPIC_MAX, payload, HA_PAYLOAD_MAX)) {
+            crc = util_crc32(crc, topic, strlen(topic) + 1);
+            crc = util_crc32(crc, payload, strlen(payload) + 1);
+        }
+    }
+    if (topic == NULL || payload == NULL) {
+        crc = 0; /* no memory: never the hash of what went out, so the configs go out again */
+    }
+    free(topic);
+    free(payload);
+    return crc;
+}
```


Run: `cmake -S test/host -B build-host -G Ninja >/dev/null && cmake --build build-host --target test_ha_payload && ./build-host/test_ha_payload | tail -1`
Expected: `OK` (12 tests), after `the largest discovery payload: 2521 bytes of 3072`.

- [ ] **Step 3: Run the tests, and build.**

Run: `cmake --build build-host && ctest --test-dir build-host | tail -3; tools/idf.sh build 2>&1 | grep -c 'warning:'`
Expected: `100% tests passed, 0 tests failed out of 63`; `0`.

- [ ] **Step 4: Commit.**

```bash
git add components/ha_mqtt test/host/CMakeLists.txt test/host/test_ha_payload.c test/host/fixtures/ha
git commit -m "feat(ha_mqtt): topics, commands, values, state and discovery (spec §12.2-§12.5)"
```

### Task 4: The MQTT values and the message (`ha_mqtt`)

**Files:**
- Create: `components/ha_mqtt/include/ha_store.h`, `components/ha_mqtt/ha_store.c`, `test/host/test_ha_store.c`
- Modify: `components/ha_mqtt/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `ha_fields.h` (Task 2), `ha_value_t` and `HA_MESSAGE_LEN` (Task 3); `util_snapshot_hdr_t`, `util_snapshot_seal()`, `util_snapshot_valid()`.
- Produces (`ha_store.h`, pure C):
  - `HA_STORE_MAGIC 0x72666d71u` ("rfmq"), `HA_STORE_VERSION 1`, `HA_MESSAGE_FRESH_S 86400`;
  - `typedef enum { HA_MISSING, HA_FRESH, HA_STALE } ha_freshness_t;`
  - `typedef struct { char key[HA_KEY_LEN]; char label[HA_LABEL_LEN]; char unit[HA_UNIT_LEN]; uint8_t kind; uint8_t decimals; uint32_t ttl_s; uint32_t updated; int32_t number; char text[HA_TEXT_LEN]; } ha_entry_t;`
  - `typedef struct { util_snapshot_hdr_t hdr; uint8_t count; uint32_t default_ttl_s; ha_entry_t entry[HA_FIELDS_MAX]; char message[HA_MESSAGE_LEN]; uint32_t message_at; bool message_dismissed; } ha_store_t;` (3 968 bytes)
  - `void ha_store_init(ha_store_t *s)`, `void ha_store_rebuild(ha_store_t *s, const ha_fields_t *f)`, `void ha_store_shift_time(ha_store_t *s, int64_t delta_s)`, `int ha_store_find(const ha_store_t *s, const char *key)`, `void ha_store_set_default_ttl(ha_store_t *s, uint32_t ttl_s)`, `bool ha_store_set(ha_store_t *s, int i, const ha_value_t *v, time_t now)`, `ha_freshness_t ha_store_freshness(const ha_store_t *s, int i, time_t now)`, `void ha_store_set_message(ha_store_t *s, const char *text, time_t now)`, `bool ha_store_banner(const ha_store_t *s, time_t now)`, `void ha_store_dismiss(ha_store_t *s)`, `ha_freshness_t ha_store_message_freshness(const ha_store_t *s, time_t now)`, `void ha_store_seal(ha_store_t *s)`, `bool ha_store_valid(const ha_store_t *s)`.

What the dashboard shows from MQTT (spec §12.5, §12.7), as plain data that deep sleep keeps in RTC RAM (Task 9 puts it there), so a routine wake draws `mqtt.<key>` fields without reading a file. Each entry keeps its mapping's label, unit, kind and time to live beside the last value and when it came; a rebuild (new mappings) keeps the value of an entry with the same key and kind, moving the entries in place with one spare entry rather than a 3.8 KB copy of the block (internal RAM is scarce, AGENTS §8). A sync sets the clock only after its MQTT session, so after a power-off without the backup cell (D9) the values come stamped in 2000; when the clock moves, `ha_store_shift_time()` moves every stamp with it, as the battery's history does. A value is stale after its mapping's `ttl_s`, or the store's default (twice the expected sync interval; 0, never, in manual mode). `ha_store_set()` says whether the screen changes, so a burst of retained values that changes nothing draws nothing. The message shows its banner until KEY dismisses it, a new message replaces it or 24 h pass; the `ha.message` field shows it until it is replaced or cleared, stale after 24 h. The block is sealed with its own magic, version and CRC, as the snapshot is.

- [ ] **Step 1: Write the failing tests.**

`test/host/test_ha_store.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/test/host/test_ha_store.c
@@ -0,0 +1,228 @@
+#include <stdio.h>
+#include <string.h>
+
+#include "ha_store.h"
+#include "unity.h"
+
+#define NOW ((time_t)1790307012) /* 2026-09-25 03:30:12 UTC */
+
+static ha_store_t s_store;
+static ha_fields_t s_fields;
+
+static ha_field_t mapping(const char *key, ha_kind_t kind, uint32_t ttl_s)
+{
+    ha_field_t f = { .kind = (uint8_t)kind, .precision = HA_PRECISION_AUTO, .ttl_s = ttl_s, .topic = "t" };
+    snprintf(f.key, sizeof(f.key), "%s", key);
+    snprintf(f.label, sizeof(f.label), "Label %s", key);
+    snprintf(f.unit, sizeof(f.unit), "%s", kind == HA_KIND_NUMBER ? "°C" : "");
+    return f;
+}
+
+void setUp(void)
+{
+    memset(&s_store, 0xAA, sizeof(s_store));
+    ha_store_init(&s_store);
+    s_fields = (ha_fields_t){ .count = 2 };
+    s_fields.field[0] = mapping("outdoor", HA_KIND_NUMBER, 0);
+    s_fields.field[1] = mapping("door", HA_KIND_TEXT, 600);
+    ha_store_rebuild(&s_store, &s_fields);
+}
+
+void tearDown(void) {}
+
+static ha_value_t number(int32_t n, uint8_t decimals)
+{
+    return (ha_value_t){ .kind = HA_KIND_NUMBER, .number = n, .decimals = decimals };
+}
+
+static ha_value_t text(const char *t)
+{
+    ha_value_t v = { .kind = HA_KIND_TEXT };
+    snprintf(v.text, sizeof(v.text), "%s", t);
+    return v;
+}
+
+/* The entries follow the mappings: their key, label, unit, kind and time to live, no value yet. */
+static void test_entries_follow_the_mappings(void)
+{
+    TEST_ASSERT_EQUAL_UINT8(2, s_store.count);
+    TEST_ASSERT_EQUAL_INT(0, ha_store_find(&s_store, "outdoor"));
+    TEST_ASSERT_EQUAL_INT(1, ha_store_find(&s_store, "door"));
+    TEST_ASSERT_EQUAL_INT(-1, ha_store_find(&s_store, "window"));
+    TEST_ASSERT_EQUAL_STRING("Label outdoor", s_store.entry[0].label);
+    TEST_ASSERT_EQUAL_STRING("°C", s_store.entry[0].unit);
+    TEST_ASSERT_EQUAL_UINT8(HA_KIND_TEXT, s_store.entry[1].kind);
+    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_freshness(&s_store, 0, NOW));
+    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_freshness(&s_store, 5, NOW)); /* no such entry */
+}
+
+/* spec §12.5: stale after the mapping's ttl_s, or by default twice the expected interval; never in manual. */
+static void test_values_go_stale_after_their_time_to_live(void)
+{
+    ha_store_set_default_ttl(&s_store, 7200);
+    ha_value_t v = number(215, 1);
+    TEST_ASSERT_TRUE(ha_store_set(&s_store, 0, &v, NOW));
+    v = text("open");
+    TEST_ASSERT_TRUE(ha_store_set(&s_store, 1, &v, NOW));
+    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_freshness(&s_store, 0, NOW + 7200));
+    TEST_ASSERT_EQUAL(HA_STALE, ha_store_freshness(&s_store, 0, NOW + 7201));
+    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_freshness(&s_store, 1, NOW + 600)); /* its own 10 min */
+    TEST_ASSERT_EQUAL(HA_STALE, ha_store_freshness(&s_store, 1, NOW + 601));
+    ha_store_set_default_ttl(&s_store, 0);
+    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_freshness(&s_store, 0, NOW + 30 * 86400)); /* manual: only its age */
+    TEST_ASSERT_EQUAL_INT32(215, s_store.entry[0].number);
+    TEST_ASSERT_EQUAL_UINT8(1, s_store.entry[0].decimals);
+    TEST_ASSERT_EQUAL_STRING("open", s_store.entry[1].text);
+}
+
+/* A value that shows the same needs no redraw; a stale one coming back does. */
+static void test_a_value_says_whether_the_screen_changes(void)
+{
+    ha_store_set_default_ttl(&s_store, 7200);
+    ha_value_t v = number(215, 1);
+    TEST_ASSERT_TRUE(ha_store_set(&s_store, 0, &v, NOW));
+    TEST_ASSERT_FALSE(ha_store_set(&s_store, 0, &v, NOW + 60));
+    v = number(216, 1);
+    TEST_ASSERT_TRUE(ha_store_set(&s_store, 0, &v, NOW + 120));
+    TEST_ASSERT_TRUE(ha_store_set(&s_store, 0, &v, NOW + 8000)); /* it had gone stale */
+    v = text("open");
+    TEST_ASSERT_FALSE(ha_store_set(&s_store, 0, &v, NOW + 8060)); /* the wrong kind: ignored */
+    TEST_ASSERT_EQUAL_INT32(216, s_store.entry[0].number);
+    TEST_ASSERT_FALSE(ha_store_set(&s_store, 9, &v, NOW));
+}
+
+/* Editing the mappings keeps the values of the keys that stay with their kind. */
+static void test_a_rebuild_keeps_what_still_maps(void)
+{
+    ha_value_t v = number(215, 1);
+    ha_store_set(&s_store, 0, &v, NOW);
+    v = text("open");
+    ha_store_set(&s_store, 1, &v, NOW);
+    ha_fields_t next = { .count = 3 };
+    next.field[0] = mapping("door", HA_KIND_TEXT, 0);       /* moved */
+    next.field[1] = mapping("outdoor", HA_KIND_TEXT, 0);    /* now a text: its number goes */
+    next.field[2] = mapping("window", HA_KIND_NUMBER, 0);   /* new */
+    snprintf(next.field[0].label, sizeof(next.field[0].label), "%s", "Front door");
+    ha_store_rebuild(&s_store, &next);
+    TEST_ASSERT_EQUAL_UINT8(3, s_store.count);
+    TEST_ASSERT_EQUAL_STRING("door", s_store.entry[0].key);
+    TEST_ASSERT_EQUAL_STRING("open", s_store.entry[0].text);
+    TEST_ASSERT_EQUAL_STRING("Front door", s_store.entry[0].label);
+    TEST_ASSERT_EQUAL_UINT32((uint32_t)NOW, s_store.entry[0].updated);
+    TEST_ASSERT_EQUAL_UINT32(0, s_store.entry[1].updated);
+    TEST_ASSERT_EQUAL_UINT32(0, s_store.entry[2].updated);
+    ha_store_rebuild(&s_store, &(ha_fields_t){ .count = 0 });
+    TEST_ASSERT_EQUAL_UINT8(0, s_store.count);
+}
+
+/* A rebuild moves the entries in place: values follow their keys through any reordering. */
+static void test_a_rebuild_follows_the_keys_through_any_order(void)
+{
+    static const char *const k_keys[] = { "a", "b", "c", "d" };
+    ha_fields_t f = { .count = 4 };
+    for (int i = 0; i < 4; i++) {
+        f.field[i] = mapping(k_keys[i], HA_KIND_NUMBER, 0);
+    }
+    ha_store_rebuild(&s_store, &f);
+    for (int i = 0; i < 4; i++) {
+        ha_value_t v = number(i + 1, 0);
+        ha_store_set(&s_store, i, &v, NOW);
+    }
+    ha_fields_t next = { .count = 4 };
+    next.field[0] = mapping("d", HA_KIND_NUMBER, 0);
+    next.field[1] = mapping("a", HA_KIND_NUMBER, 0);
+    next.field[2] = mapping("x", HA_KIND_NUMBER, 0); /* new */
+    next.field[3] = mapping("c", HA_KIND_NUMBER, 0); /* b goes */
+    ha_store_rebuild(&s_store, &next);
+    static const int k_want[] = { 4, 1, 0, 3 };
+    for (int i = 0; i < 4; i++) {
+        TEST_ASSERT_EQUAL_STRING(next.field[i].key, s_store.entry[i].key);
+        TEST_ASSERT_EQUAL_INT32(k_want[i], s_store.entry[i].number);
+        TEST_ASSERT_EQUAL(k_want[i] ? HA_FRESH : HA_MISSING, ha_store_freshness(&s_store, i, NOW));
+    }
+    /* 32 mappings reversed: every value travels the length of the block */
+    ha_fields_t all = { .count = HA_FIELDS_MAX }, reversed = { .count = HA_FIELDS_MAX };
+    for (int i = 0; i < HA_FIELDS_MAX; i++) {
+        char key[8];
+        snprintf(key, sizeof(key), "k%d", i);
+        all.field[i] = mapping(key, HA_KIND_NUMBER, 0);
+        reversed.field[HA_FIELDS_MAX - 1 - i] = all.field[i];
+    }
+    ha_store_rebuild(&s_store, &all);
+    for (int i = 0; i < HA_FIELDS_MAX; i++) {
+        ha_value_t v = number(100 + i, 0);
+        ha_store_set(&s_store, i, &v, NOW);
+    }
+    ha_store_rebuild(&s_store, &reversed);
+    for (int i = 0; i < HA_FIELDS_MAX; i++) {
+        TEST_ASSERT_EQUAL_INT32(100 + HA_FIELDS_MAX - 1 - i, s_store.entry[i].number);
+    }
+}
+
+/* The clock moved (a sync set it after a power-off, spec §7): every value and the message keep their age. */
+static void test_a_clock_move_keeps_the_ages(void)
+{
+    const time_t y2000 = 946684800; /* where the RTC starts without its backup cell (D9) */
+    ha_value_t v = number(215, 1);
+    ha_store_set(&s_store, 0, &v, y2000 + 60);
+    ha_store_set_message(&s_store, "Door open", y2000 + 120);
+    ha_store_shift_time(&s_store, NOW - (y2000 + 180)); /* the sync's clock: a minute after the message */
+    TEST_ASSERT_EQUAL_UINT32((uint32_t)(NOW - 120), s_store.entry[0].updated);
+    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_freshness(&s_store, 0, NOW));
+    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_freshness(&s_store, 1, NOW)); /* no value stays none */
+    TEST_ASSERT_EQUAL_UINT32((uint32_t)(NOW - 60), s_store.message_at);
+    TEST_ASSERT_TRUE(ha_store_banner(&s_store, NOW));
+    ha_store_shift_time(&s_store, -(int64_t)NOW); /* back past 1970: very old, but still there */
+    TEST_ASSERT_EQUAL_UINT32(1, s_store.entry[0].updated);
+    TEST_ASSERT_EQUAL_UINT32(1, s_store.message_at);
+}
+
+/* spec §12.7: a banner until KEY dismisses it, a new message replaces it or 24 h pass; the field shows it
+ * until it is replaced or cleared, stale after 24 h. */
+static void test_the_message(void)
+{
+    TEST_ASSERT_FALSE(ha_store_banner(&s_store, NOW));
+    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_message_freshness(&s_store, NOW));
+    ha_store_set_message(&s_store, "Washing machine done", NOW);
+    TEST_ASSERT_TRUE(ha_store_banner(&s_store, NOW + HA_MESSAGE_FRESH_S));
+    TEST_ASSERT_FALSE(ha_store_banner(&s_store, NOW + HA_MESSAGE_FRESH_S + 1));
+    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_message_freshness(&s_store, NOW + HA_MESSAGE_FRESH_S));
+    TEST_ASSERT_EQUAL(HA_STALE, ha_store_message_freshness(&s_store, NOW + HA_MESSAGE_FRESH_S + 1));
+    ha_store_dismiss(&s_store);
+    TEST_ASSERT_FALSE(ha_store_banner(&s_store, NOW + 60));
+    TEST_ASSERT_EQUAL_STRING("Washing machine done", s_store.message); /* the field keeps it */
+    ha_store_set_message(&s_store, "Door open", NOW + 120);
+    TEST_ASSERT_TRUE(ha_store_banner(&s_store, NOW + 180)); /* a new one shows again */
+    ha_store_set_message(&s_store, "", NOW + 240);
+    TEST_ASSERT_FALSE(ha_store_banner(&s_store, NOW + 240));
+    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_message_freshness(&s_store, NOW + 240));
+    ha_store_set_message(&s_store, "kept", NOW + 300);
+    ha_store_rebuild(&s_store, &s_fields);
+    TEST_ASSERT_EQUAL_STRING("kept", s_store.message); /* new mappings leave the message */
+}
+
+/* The block goes into RTC RAM through deep sleep with its own magic, version and CRC (spec §12.5). */
+static void test_the_block_is_sealed(void)
+{
+    TEST_ASSERT_FALSE(ha_store_valid(&s_store)); /* never sealed */
+    ha_store_set_message(&s_store, "hello", NOW);
+    ha_store_seal(&s_store);
+    TEST_ASSERT_TRUE(ha_store_valid(&s_store));
+    s_store.message[0] = 'j';
+    TEST_ASSERT_FALSE(ha_store_valid(&s_store));
+    printf("the store's block: %u bytes\n", (unsigned)sizeof(ha_store_t));
+}
+
+int main(void)
+{
+    UNITY_BEGIN();
+    RUN_TEST(test_entries_follow_the_mappings);
+    RUN_TEST(test_values_go_stale_after_their_time_to_live);
+    RUN_TEST(test_a_value_says_whether_the_screen_changes);
+    RUN_TEST(test_a_rebuild_keeps_what_still_maps);
+    RUN_TEST(test_a_rebuild_follows_the_keys_through_any_order);
+    RUN_TEST(test_a_clock_move_keeps_the_ages);
+    RUN_TEST(test_the_message);
+    RUN_TEST(test_the_block_is_sealed);
+    return UNITY_END();
+}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -176,10 +176,10 @@ target_link_libraries(storage_logic PRIVATE cjson util m)
 
 # ha_mqtt: MQTT and Home Assistant's mappings, payloads and store build on the host; the client does not.
 add_library(ha_mqtt_logic STATIC ${REPO_ROOT}/components/ha_mqtt/ha_fields.c
-            ${REPO_ROOT}/components/ha_mqtt/ha_payload.c)
+            ${REPO_ROOT}/components/ha_mqtt/ha_payload.c ${REPO_ROOT}/components/ha_mqtt/ha_store.c)
 target_include_directories(ha_mqtt_logic PUBLIC ${REPO_ROOT}/components/ha_mqtt/include)
 target_compile_options(ha_mqtt_logic PRIVATE ${REFLBO_WARNINGS})
-target_link_libraries(ha_mqtt_logic PRIVATE cjson util m)
+target_link_libraries(ha_mqtt_logic PUBLIC util PRIVATE cjson m)
 
 # weather: the Open-Meteo requests, replies, bands and levels build on the host; the fetch does not.
 add_library(weather_logic STATIC ${REPO_ROOT}/components/weather/weather_url.c
@@ -269,6 +269,7 @@ target_compile_definitions(test_storage_file PRIVATE TEST_TMP_DIR="${CMAKE_CURRE
 reflbo_host_test(test_storage_backup storage_logic)
 reflbo_host_test(test_ha_fields ha_mqtt_logic)
 reflbo_host_test(test_ha_payload ha_mqtt_logic cjson)
+reflbo_host_test(test_ha_store ha_mqtt_logic)
 target_compile_definitions(test_ha_payload PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ha")
 
 reflbo_host_test(test_test_pattern_golden gfx)
```


Run: `cmake -S test/host -B build-host -G Ninja 2>&1 | grep -m1 -A1 'CMake Error'`
Expected: `CMake Error at CMakeLists.txt:178 (add_library):`, then `Cannot find source file:` and the path of `components/ha_mqtt/ha_store.c`.

- [ ] **Step 2: The store.**

`components/ha_mqtt/CMakeLists.txt`:

```diff
--- a/components/ha_mqtt/CMakeLists.txt
+++ b/components/ha_mqtt/CMakeLists.txt
@@ -1,5 +1,6 @@
-# MQTT and Home Assistant (spec §12, D32). ha_fields.c (the field mappings) and ha_payload.c (topics and
-# payloads) are pure C and also build on the host.
-idf_component_register(SRCS "ha_fields.c" "ha_payload.c"
+# MQTT and Home Assistant (spec §12, D32). ha_fields.c (the field mappings), ha_payload.c (topics and
+# payloads) and ha_store.c (what the dashboard shows) are pure C and also build on the host.
+idf_component_register(SRCS "ha_fields.c" "ha_payload.c" "ha_store.c"
                        INCLUDE_DIRS "include"
-                       PRIV_REQUIRES json util)
+                       REQUIRES util
+                       PRIV_REQUIRES json)
```


`components/ha_mqtt/include/ha_store.h`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/include/ha_store.h
@@ -0,0 +1,69 @@
+#pragma once
+
+#include <stdbool.h>
+#include <stdint.h>
+#include <time.h>
+
+#include "ha_fields.h"
+#include "ha_payload.h"
+#include "util_snapshot.h"
+
+/*
+ * What the dashboard shows from MQTT (spec §12.5, §12.7): each mapping's label, unit and time to live with
+ * the last value that came, and Home Assistant's message. Plain data, sealed into RTC RAM through deep
+ * sleep, so a routine wake draws the mqtt.<key> fields without reading a file. The app task owns it.
+ * Pure C, host-buildable.
+ */
+
+#define HA_STORE_MAGIC 0x72666d71u /* "rfmq" */
+#define HA_STORE_VERSION 1
+#define HA_MESSAGE_FRESH_S 86400 /* the banner, and the field's freshness: 24 h (spec §12.7) */
+
+typedef enum {
+    HA_MISSING, /* no value, or no such entry */
+    HA_FRESH,
+    HA_STALE,
+} ha_freshness_t;
+
+typedef struct {
+    char key[HA_KEY_LEN];
+    char label[HA_LABEL_LEN];
+    char unit[HA_UNIT_LEN];
+    uint8_t kind;     /* ha_kind_t */
+    uint8_t decimals; /* the value's */
+    uint32_t ttl_s;   /* the mapping's; 0: the store's default */
+    uint32_t updated; /* UTC; 0: no value yet */
+    int32_t number;
+    char text[HA_TEXT_LEN];
+} ha_entry_t;
+
+typedef struct {
+    util_snapshot_hdr_t hdr; /* ha_store_seal() before a deep sleep */
+    uint8_t count;
+    uint32_t default_ttl_s; /* twice the expected sync interval; 0: never stale (manual mode) */
+    ha_entry_t entry[HA_FIELDS_MAX];
+    char message[HA_MESSAGE_LEN];
+    uint32_t message_at;    /* UTC; 0: none */
+    bool message_dismissed; /* KEY short took the banner away; the field keeps showing the message */
+} ha_store_t;
+
+void ha_store_init(ha_store_t *s);
+/* The mappings changed (a cold boot, the MQTT page, a restore): the entries in their order, each keeping
+ * the value of an entry with the same key and kind. The message stays. */
+void ha_store_rebuild(ha_store_t *s, const ha_fields_t *f);
+int ha_store_find(const ha_store_t *s, const char *key); /* index, or -1 */
+void ha_store_set_default_ttl(ha_store_t *s, uint32_t ttl_s);
+/* A value for entry `i` came at `now`. True if what it shows changed: a new value, or one that was stale.
+ * A value of the wrong kind, or for no entry, is ignored. */
+bool ha_store_set(ha_store_t *s, int i, const ha_value_t *v, time_t now);
+ha_freshness_t ha_store_freshness(const ha_store_t *s, int i, time_t now);
+/* The clock moved by `delta_s` (a sync set it): every value and the message keep their age. */
+void ha_store_shift_time(ha_store_t *s, int64_t delta_s);
+/* The message (spec §12.7): a new one shows its banner again; "" clears it. */
+void ha_store_set_message(ha_store_t *s, const char *text, time_t now);
+/* A message under 24 h old that KEY hasn't dismissed. */
+bool ha_store_banner(const ha_store_t *s, time_t now);
+void ha_store_dismiss(ha_store_t *s);
+ha_freshness_t ha_store_message_freshness(const ha_store_t *s, time_t now);
+void ha_store_seal(ha_store_t *s);
+bool ha_store_valid(const ha_store_t *s);
```


`components/ha_mqtt/ha_store.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/ha_store.c
@@ -0,0 +1,154 @@
+#include "ha_store.h"
+
+#include <stdio.h>
+#include <string.h>
+
+void ha_store_init(ha_store_t *s)
+{
+    memset(s, 0, sizeof(*s));
+}
+
+int ha_store_find(const ha_store_t *s, const char *key)
+{
+    for (int i = 0; key != NULL && i < s->count; i++) {
+        if (strcmp(s->entry[i].key, key) == 0) {
+            return i;
+        }
+    }
+    return -1;
+}
+
+void ha_store_rebuild(ha_store_t *s, const ha_fields_t *f)
+{
+    int n = f->count < HA_FIELDS_MAX ? f->count : HA_FIELDS_MAX;
+    int old_count = s->count < HA_FIELDS_MAX ? s->count : HA_FIELDS_MAX;
+    int8_t src[HA_FIELDS_MAX]; /* the old entry each mapping keeps, or -1 */
+    int8_t at[HA_FIELDS_MAX];  /* the old entry in each place now, or -1 */
+    int8_t pos[HA_FIELDS_MAX]; /* the place of each old entry now */
+    for (int p = 0; p < HA_FIELDS_MAX; p++) {
+        at[p] = (int8_t)(p < old_count ? p : -1);
+        pos[p] = (int8_t)p;
+        src[p] = -1;
+    }
+    for (int i = 0; i < n; i++) {
+        for (int j = 0; j < old_count && src[i] < 0; j++) {
+            if (strcmp(s->entry[j].key, f->field[i].key) == 0 && s->entry[j].kind == f->field[i].kind) {
+                src[i] = (int8_t)j;
+            }
+        }
+    }
+    /* In place, with no copy of the block: each kept entry swaps into its place, and what was there takes the
+     * kept one's old place, where a later mapping may still find it. */
+    for (int i = 0; i < n; i++) {
+        int k = src[i];
+        if (k < 0 || pos[k] == i) {
+            continue;
+        }
+        int p = pos[k], other = at[i];
+        ha_entry_t tmp = s->entry[i];
+        s->entry[i] = s->entry[p];
+        s->entry[p] = tmp;
+        at[i] = (int8_t)k;
+        pos[k] = (int8_t)i;
+        at[p] = (int8_t)other;
+        if (other >= 0) {
+            pos[other] = (int8_t)p;
+        }
+    }
+    for (int i = 0; i < HA_FIELDS_MAX; i++) {
+        if (i >= n || src[i] < 0) {
+            memset(&s->entry[i], 0, sizeof(s->entry[i])); /* a new mapping, or none: no value */
+        }
+        if (i < n) {
+            const ha_field_t *m = &f->field[i];
+            ha_entry_t *e = &s->entry[i];
+            snprintf(e->key, sizeof(e->key), "%s", m->key);
+            snprintf(e->label, sizeof(e->label), "%s", m->label);
+            snprintf(e->unit, sizeof(e->unit), "%s", m->unit);
+            e->kind = m->kind;
+            e->ttl_s = m->ttl_s;
+        }
+    }
+    s->count = (uint8_t)n;
+}
+
+void ha_store_set_default_ttl(ha_store_t *s, uint32_t ttl_s)
+{
+    s->default_ttl_s = ttl_s;
+}
+
+ha_freshness_t ha_store_freshness(const ha_store_t *s, int i, time_t now)
+{
+    if (i < 0 || i >= s->count || s->entry[i].updated == 0) {
+        return HA_MISSING;
+    }
+    const ha_entry_t *e = &s->entry[i];
+    uint32_t ttl = e->ttl_s != 0 ? e->ttl_s : s->default_ttl_s;
+    return ttl != 0 && now > (time_t)e->updated + (time_t)ttl ? HA_STALE : HA_FRESH;
+}
+
+bool ha_store_set(ha_store_t *s, int i, const ha_value_t *v, time_t now)
+{
+    if (i < 0 || i >= s->count || v->kind != s->entry[i].kind) {
+        return false;
+    }
+    ha_entry_t *e = &s->entry[i];
+    bool shown = ha_store_freshness(s, i, now) == HA_FRESH;
+    bool same = e->kind == HA_KIND_NUMBER ? e->number == v->number && e->decimals == v->decimals
+                                          : strcmp(e->text, v->text) == 0;
+    e->number = v->number;
+    e->decimals = v->decimals;
+    snprintf(e->text, sizeof(e->text), "%s", v->text);
+    e->updated = (uint32_t)now;
+    return !(shown && same);
+}
+
+/* A time moved by `delta_s`, kept at 1 or later: 0 means none. */
+static uint32_t shifted(uint32_t t, int64_t delta_s)
+{
+    int64_t moved = (int64_t)t + delta_s;
+    return t == 0 ? 0 : moved < 1 ? 1 : moved > UINT32_MAX ? UINT32_MAX : (uint32_t)moved;
+}
+
+void ha_store_shift_time(ha_store_t *s, int64_t delta_s)
+{
+    for (int i = 0; i < s->count && i < HA_FIELDS_MAX; i++) {
+        s->entry[i].updated = shifted(s->entry[i].updated, delta_s);
+    }
+    s->message_at = shifted(s->message_at, delta_s);
+}
+
+void ha_store_set_message(ha_store_t *s, const char *text, time_t now)
+{
+    snprintf(s->message, sizeof(s->message), "%s", text);
+    s->message_at = text[0] != '\0' ? (uint32_t)now : 0;
+    s->message_dismissed = false;
+}
+
+ha_freshness_t ha_store_message_freshness(const ha_store_t *s, time_t now)
+{
+    if (s->message_at == 0) {
+        return HA_MISSING;
+    }
+    return now > (time_t)s->message_at + HA_MESSAGE_FRESH_S ? HA_STALE : HA_FRESH;
+}
+
+bool ha_store_banner(const ha_store_t *s, time_t now)
+{
+    return !s->message_dismissed && ha_store_message_freshness(s, now) == HA_FRESH;
+}
+
+void ha_store_dismiss(ha_store_t *s)
+{
+    s->message_dismissed = true;
+}
+
+void ha_store_seal(ha_store_t *s)
+{
+    util_snapshot_seal(s, sizeof(*s), HA_STORE_MAGIC, HA_STORE_VERSION);
+}
+
+bool ha_store_valid(const ha_store_t *s)
+{
+    return util_snapshot_valid(s, sizeof(*s), HA_STORE_MAGIC, HA_STORE_VERSION);
+}
```


Run: `cmake -S test/host -B build-host -G Ninja >/dev/null && cmake --build build-host --target test_ha_store && ./build-host/test_ha_store | tail -1`
Expected: `OK` (8 tests).

- [ ] **Step 3: Run the tests, and build.**

Run: `cmake --build build-host && ctest --test-dir build-host | tail -3; tools/idf.sh build 2>&1 | grep -c 'warning:'`
Expected: `100% tests passed, 0 tests failed out of 64`; `0`.

- [ ] **Step 4: Commit.**

```bash
git add components/ha_mqtt test/host/CMakeLists.txt test/host/test_ha_store.c
git commit -m "feat(ha_mqtt): the MQTT values and the message, for RTC RAM (spec §12.5, §12.7)"
```

### Task 5: MQTT fields on the dashboard (`ui`, `main`)

**Files:**
- Modify: `components/ui/CMakeLists.txt`, `components/ui/include/ui_fields.h`, `components/ui/include/ui_preset.h`, `components/ui/ui_fields.c`, `components/ui/ui_preset_json.c`, `components/ui/ui_dashboard.c`, `components/ui/ui_widget.c`, `components/ui/ui_catalog.c`, `main/app.c`, `main/app_ui.c`, `main/app_web.c`, `test/host/CMakeLists.txt`, `test/host/context_fixtures.h`, `test/host/dashboard_fixtures.h`, `test/host/test_ui_fields.c`, `test/host/test_ui_preset.c`, `test/host/test_ui_catalog.c`, `test/host/test_ui_widget_fit.c`
- Create (binary, from `plan/m7`): `test/host/golden/dash_mqtt_grid.pbm`, `test/host/golden/dash_mqtt_home_cs.pbm`

**Interfaces:**
- Consumes: `ha_store_t`, `ha_store_find()`, `ha_store_freshness()` (Task 4); `HA_KEY_LEN`, `ha_key_valid()` (Task 2); `lang_format_decimal()`; `ui_split_field_size()`.
- Produces:
  - `UI_MQTT_KEYS 32`, `UI_FIELD_MQTT ((int)UI_FIELD_COUNT)`, `typedef struct { uint8_t count; char key[UI_MQTT_KEYS][HA_KEY_LEN]; } ui_mqtt_keys_t;`, `static inline bool ui_field_is_mqtt(int field)`;
  - `ui_context_t.mqtt` (`const ha_store_t *`, NULL for none) and `ui_context_t.mqtt_keys` (`const ui_mqtt_keys_t *`);
  - `ui_presets_t.mqtt` (`ui_mqtt_keys_t`): the keys the presets' slots name, in the order `presets.json` first names them;
  - `void ui_mqtt_value(const ui_context_t *ctx, int i, ui_value_t *out)`: store entry `i` as a field shows it;
  - in `GET /api/fields`, an entry per mapping (`mqtt.<key>`, its kind, label, value and state) after the built-in fields.

A slot names `mqtt.<key>` (spec §5.4, §12.5, D32). The presets keep those names: field ids `UI_FIELD_MQTT + k` stand for the presets' own key `k`, 32 at most between all presets, and a slot finds its mapping by key in the store when it draws. A key no mapping names draws as an empty slot, and so does one whose mapping's kind the slot can't show, so editing or deleting a mapping never rewrites `presets.json`. A key must be one a mapping could have (Task 2's rule), else the file is refused, naming the preset. A number's whole part, for a slot too narrow for its decimals, rounds in 64 bits: on the board `long` is 32 bits, and a value near `INT32_MAX` would overflow. An MQTT field has no icon: its label stands where the icon would. In narrow split cells a name that doesn't fit two lines falls back to one cut line (M6b review's minor: two lines cut 1–2 px of descenders in 81–82 px cells). The presets' table adds 769 bytes to the RTC snapshot, whose cap goes from 4 to 5 KB (RTC SLOW is 8 KB; the snapshot is then 4 856 bytes).

- [ ] **Step 1: Write the failing tests and the fixtures.**

`test/host/context_fixtures.h`:

```diff
--- a/test/host/context_fixtures.h
+++ b/test/host/context_fixtures.h
@@ -1,10 +1,12 @@
 #pragma once
 
+#include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <time.h>
 
 #include "datastore.h"
+#include "ha_store.h"
 #include "lang.h"
 #include "ui_fields.h"
 #include "util_time.h"
@@ -158,3 +160,52 @@ static inline ui_context_t fixture_context(void)
                          .lat_e4 = 491951, .lon_e4 = 166068 };
     return ctx;
 }
+
+/* MQTT fields (spec §12.5): the store's five mappings, and the six keys the presets name. outdoor
+ * 21.5 °C and co2 612 ppm ten minutes old, door "Closed" (a text), power 1.24 kW three hours old against
+ * its hour (stale), washer mapped but without a value yet; window is named by the presets and mapped by
+ * nothing, so its slot stays empty. FIX_MQTT(k) is the field of the presets' key k. */
+static ha_store_t s_fix_mqtt;
+static ui_mqtt_keys_t s_fix_keys;
+#define FIX_MQTT(k) ((ui_field_id_t)(UI_FIELD_MQTT + (k)))
+
+static inline void fixture_mqtt(ui_context_t *ctx)
+{
+    static const struct {
+        const char *key, *label, *unit;
+        uint8_t kind;
+        uint32_t ttl_s;
+    } k_map[] = { { "outdoor", "Outside", "\xC2\xB0" "C", HA_KIND_NUMBER, 0 },
+                  { "co2", "CO2", "ppm", HA_KIND_NUMBER, 0 },
+                  { "door", "Front door", "", HA_KIND_TEXT, 0 },
+                  { "power", "Power", "kW", HA_KIND_NUMBER, 3600 },
+                  { "washer", "Washer", "", HA_KIND_TEXT, 0 } };
+    static ha_fields_t f;
+    memset(&f, 0, sizeof(f));
+    for (size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++) {
+        ha_field_t *m = &f.field[f.count++];
+        snprintf(m->key, sizeof(m->key), "%s", k_map[i].key);
+        snprintf(m->label, sizeof(m->label), "%s", k_map[i].label);
+        snprintf(m->unit, sizeof(m->unit), "%s", k_map[i].unit);
+        m->kind = k_map[i].kind;
+        m->ttl_s = k_map[i].ttl_s;
+    }
+    ha_store_init(&s_fix_mqtt);
+    ha_store_rebuild(&s_fix_mqtt, &f);
+    ha_store_set_default_ttl(&s_fix_mqtt, 2 * 86400);
+    ha_value_t v = { .kind = HA_KIND_NUMBER, .number = 215, .decimals = 1 };
+    ha_store_set(&s_fix_mqtt, 0, &v, FIX_NOW - 600);
+    v = (ha_value_t){ .kind = HA_KIND_NUMBER, .number = 612 };
+    ha_store_set(&s_fix_mqtt, 1, &v, FIX_NOW - 600);
+    v = (ha_value_t){ .kind = HA_KIND_TEXT, .text = "Closed" };
+    ha_store_set(&s_fix_mqtt, 2, &v, FIX_NOW - 600);
+    v = (ha_value_t){ .kind = HA_KIND_NUMBER, .number = 124, .decimals = 2 };
+    ha_store_set(&s_fix_mqtt, 3, &v, FIX_NOW - 3 * 3600);
+    static const char *const k_keys[] = { "outdoor", "co2", "door", "power", "washer", "window" };
+    memset(&s_fix_keys, 0, sizeof(s_fix_keys));
+    for (size_t i = 0; i < sizeof(k_keys) / sizeof(k_keys[0]); i++) {
+        snprintf(s_fix_keys.key[s_fix_keys.count++], HA_KEY_LEN, "%s", k_keys[i]);
+    }
+    ctx->mqtt = &s_fix_mqtt;
+    ctx->mqtt_keys = &s_fix_keys;
+}
```


`test/host/dashboard_fixtures.h`:

```diff
--- a/test/host/dashboard_fixtures.h
+++ b/test/host/dashboard_fixtures.h
@@ -278,6 +278,19 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
     } else if (strcmp(name, "home_temp_main_cs") == 0) { /* in Czech the decimals give way to the comma's tail */
         fixture_dashboard("home_temp_main", ctx, preset);
         ctx->lang = lang_get("cs");
+    } else if (strcmp(name, "mqtt_grid") == 0) { /* M7: MQTT fields, fresh, a text, stale, missing, unmapped */
+        *preset = fixture_preset("indoor");
+        for (int k = 0; k < 6; k++) {
+            preset->slots[k] = (uint8_t)FIX_MQTT(k);
+        }
+        fixture_mqtt(ctx);
+    } else if (strcmp(name, "mqtt_home_cs") == 0) { /* small slots: each label where an icon would be */
+        *preset = fixture_preset("home");
+        for (int k = 0; k < 4; k++) {
+            preset->slots[2 + k] = (uint8_t)FIX_MQTT(k);
+        }
+        fixture_mqtt(ctx);
+        ctx->lang = lang_get("cs");
     } else if (strcmp(name, "grid_clock_12h") == 0) { /* a clock in a grid cell, 12-hour */
         *preset = fixture_preset("indoor");
         preset->slots[0] = UI_FIELD_TIME_CLOCK;
@@ -305,4 +318,5 @@ static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather",
                                                     "flights", "flights_100", "flights_cs", "flights_none",
                                                     "flights_failed", "flights_off", "split_weather",
                                                     "split_eight", "home_temp_main", "home_temp_main_cs",
-                                                    "weather_frost_cs", "weather_hot_f" };
+                                                    "weather_frost_cs", "weather_hot_f", "mqtt_grid",
+                                                    "mqtt_home_cs" };
```


`test/host/test_ui_fields.c`:

```diff
--- a/test/host/test_ui_fields.c
+++ b/test/host/test_ui_fields.c
@@ -304,6 +304,63 @@ static void test_numbers_with_decimals_carry_a_whole_number_form(void)
     TEST_ASSERT_EQUAL_STRING("", resolve(UI_FIELD_ENV_HUM).short_text);   /* no decimals to drop */
 }
 
+/* mqtt.<key> (spec §12.5): the mapping's label and unit, the value in the device's language. */
+static void test_mqtt_fields_come_from_the_store(void)
+{
+    fixture_mqtt(&s_ctx);
+    ui_value_t v = resolve(FIX_MQTT(0));
+    TEST_ASSERT_EQUAL(FIX_MQTT(0), v.field);
+    TEST_ASSERT_EQUAL(UI_FK_NUMBER, v.kind);
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
+    TEST_ASSERT_EQUAL_STRING("Outside", v.label);
+    TEST_ASSERT_EQUAL_STRING("21.5", v.text);
+    TEST_ASSERT_EQUAL_STRING("22", v.short_text);
+    TEST_ASSERT_EQUAL_STRING("\xC2\xB0" "C", v.unit);
+    v = resolve(FIX_MQTT(1));
+    TEST_ASSERT_EQUAL_STRING("612", v.text);
+    TEST_ASSERT_EQUAL_STRING("", v.short_text); /* no decimals to drop */
+    TEST_ASSERT_EQUAL_STRING("ppm", v.unit);
+    v = resolve(FIX_MQTT(2));
+    TEST_ASSERT_EQUAL(UI_FK_TEXT, v.kind);
+    TEST_ASSERT_EQUAL_STRING("Front door", v.label);
+    TEST_ASSERT_EQUAL_STRING("Closed", v.text);
+    TEST_ASSERT_EQUAL_STRING("", v.unit);
+    s_ctx.lang = lang_get("cs");
+    TEST_ASSERT_EQUAL_STRING("21,5", resolve(FIX_MQTT(0)).text);
+    s_ctx.lang = lang_get("en");
+    /* the largest values a payload brings: the whole number rounds without overflow, where long is 32 bits */
+    s_fix_mqtt.entry[0].number = INT32_MAX;
+    TEST_ASSERT_EQUAL_STRING("214748364.7", resolve(FIX_MQTT(0)).text);
+    TEST_ASSERT_EQUAL_STRING("214748365", resolve(FIX_MQTT(0)).short_text);
+    s_fix_mqtt.entry[0].number = -INT32_MAX;
+    TEST_ASSERT_EQUAL_STRING("-214748365", resolve(FIX_MQTT(0)).short_text);
+}
+
+static void test_mqtt_values_go_stale_with_their_age(void)
+{
+    fixture_mqtt(&s_ctx);
+    ui_value_t v = resolve(FIX_MQTT(3));
+    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
+    TEST_ASSERT_EQUAL_UINT32(3 * 3600, v.age_s);
+    TEST_ASSERT_EQUAL_STRING("1.24", v.text);
+    TEST_ASSERT_EQUAL_STRING("kW", v.unit);
+}
+
+/* A mapped key without a value yet is missing; a key no mapping names is an empty slot (spec §12.5). */
+static void test_unmapped_keys_are_empty_slots(void)
+{
+    fixture_mqtt(&s_ctx);
+    ui_value_t v = resolve(FIX_MQTT(4));
+    TEST_ASSERT_EQUAL(FIX_MQTT(4), v.field);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, v.state);
+    TEST_ASSERT_EQUAL(UI_FK_TEXT, v.kind);
+    TEST_ASSERT_EQUAL_STRING("Washer", v.label);
+    TEST_ASSERT_EQUAL(UI_FIELD_NONE, resolve(FIX_MQTT(5)).field); /* window */
+    TEST_ASSERT_EQUAL(UI_FIELD_NONE, resolve(FIX_MQTT(6)).field); /* no such key */
+    s_ctx.mqtt = NULL;
+    TEST_ASSERT_EQUAL(UI_FIELD_NONE, resolve(FIX_MQTT(0)).field);
+}
+
 static void test_none_resolves_to_missing(void)
 {
     ui_value_t v = resolve(UI_FIELD_NONE);
@@ -333,5 +390,8 @@ int main(void)
     RUN_TEST(test_old_readings_are_stale_with_their_age);
     RUN_TEST(test_none_resolves_to_missing);
     RUN_TEST(test_numbers_with_decimals_carry_a_whole_number_form);
+    RUN_TEST(test_mqtt_fields_come_from_the_store);
+    RUN_TEST(test_mqtt_values_go_stale_with_their_age);
+    RUN_TEST(test_unmapped_keys_are_empty_slots);
     return UNITY_END();
 }
```


`test/host/test_ui_preset.c`:

```diff
--- a/test/host/test_ui_preset.c
+++ b/test/host/test_ui_preset.c
@@ -522,6 +522,125 @@ static void test_a_full_set_of_split_presets_fits_the_save_buffer(void)
     TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
 }
 
+/* mqtt.<key> fields (spec §5.4, §12.5, D32): the presets name up to 32 keys between them, kept as names. */
+#define MQTT_PRESETS                                                                                               \
+    "{\"schema\": 1, \"presets\": ["                                                                                \
+    " {\"id\": \"ha\", \"layout\": \"grid\", \"slots\": {\"g1\": \"mqtt.outdoor_temp\", \"g2\": \"mqtt.co2\","        \
+    "  \"g3\": \"mqtt.outdoor_temp\", \"g4\": \"env.temp\"}},"                                                          \
+    " {\"id\": \"big\", \"layout\": \"classic\", \"slots\": {\"main\": \"mqtt.power\"}},"                           \
+    " {\"id\": \"cells\", \"layout\": \"split\", \"split\": {\"split\": \"rows\", \"ratio\": \"1/2\","               \
+    "  \"a\": {\"field\": \"mqtt.door\"}, \"b\": {\"field\": \"mqtt.co2\"}}}]}"
+
+static void test_mqtt_fields_name_their_keys(void)
+{
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(MQTT_PRESETS, &s_p, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_UINT8(4, s_p.mqtt.count); /* in the order the file first names them */
+    TEST_ASSERT_EQUAL_STRING("outdoor_temp", s_p.mqtt.key[0]);
+    TEST_ASSERT_EQUAL_STRING("co2", s_p.mqtt.key[1]);
+    TEST_ASSERT_EQUAL_STRING("power", s_p.mqtt.key[2]);
+    TEST_ASSERT_EQUAL_STRING("door", s_p.mqtt.key[3]);
+    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_MQTT + 0, s_p.presets[0].slots[0]);
+    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_MQTT + 1, s_p.presets[0].slots[1]);
+    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_MQTT + 0, s_p.presets[0].slots[2]); /* the same key, the same field */
+    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_ENV_TEMP, s_p.presets[0].slots[3]);
+    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_MQTT + 2, s_p.presets[1].slots[0]); /* Classic's XL slot takes numbers */
+    TEST_ASSERT_EQUAL_UINT8(UI_FIELD_MQTT + 3, s_p.presets[2].slots[0]);
+    TEST_ASSERT_TRUE(ui_field_is_mqtt(s_p.presets[0].slots[0]));
+    TEST_ASSERT_FALSE(ui_field_is_mqtt(UI_FIELD_ENV_TEMP));
+    TEST_ASSERT_FALSE(ui_field_is_mqtt(UI_FIELD_MQTT + UI_MQTT_KEYS));
+    ui_presets_defaults(&s_p);
+    TEST_ASSERT_EQUAL_UINT8(0, s_p.mqtt.count);
+}
+
+/* The names stay as written, mapped or not: the presets never learn which keys the mappings have. */
+static void test_mqtt_fields_survive_a_round_trip(void)
+{
+    TEST_ASSERT_TRUE(ui_presets_from_json(MQTT_PRESETS, &s_p, s_err, sizeof(s_err)));
+    TEST_ASSERT_TRUE(ui_presets_to_json(&s_p, s_json, sizeof(s_json)) > 0);
+    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"g3\":\"mqtt.outdoor_temp\""));
+    TEST_ASSERT_NOT_NULL(strstr(s_json, "{\"field\":\"mqtt.door\"}"));
+    ui_presets_t back;
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(s_json, &back, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(back));
+}
+
+static void test_bad_mqtt_fields_are_rejected(void)
+{
+    static const struct {
+        const char *field, *err;
+    } k_cases[] = {
+        { "mqtt.Outdoor", "preset \"x\": unknown field \"mqtt.Outdoor\"" },
+        { "mqtt.", "preset \"x\": unknown field \"mqtt.\"" },
+        { "mqtt.a2345678901234567890123x", "preset \"x\": unknown field \"mqtt.a2345678901234567890123x\"" },
+    };
+    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
+        snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"presets\": [{\"id\": \"x\", \"layout\": \"grid\","
+                 " \"slots\": {\"g1\": \"%s\"}}]}", k_cases[i].field);
+        TEST_ASSERT_FALSE_MESSAGE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)), k_cases[i].field);
+        TEST_ASSERT_EQUAL_STRING(k_cases[i].err, s_err);
+    }
+    /* 6 presets of 6 slots, each with a key of its own: 36 keys */
+    size_t at = (size_t)snprintf(s_json, sizeof(s_json), "{\"schema\": 1, \"presets\": [");
+    for (int i = 0; i < 6; i++) {
+        at += (size_t)snprintf(s_json + at, sizeof(s_json) - at, "%s{\"id\": \"p%d\", \"layout\": \"grid\", \"slots\": {",
+                               i ? "," : "", i);
+        for (int k = 0; k < 6; k++) {
+            at += (size_t)snprintf(s_json + at, sizeof(s_json) - at, "%s\"g%d\": \"mqtt.k%d\"", k ? "," : "", k + 1,
+                                   i * 6 + k);
+        }
+        at += (size_t)snprintf(s_json + at, sizeof(s_json) - at, "}}");
+    }
+    snprintf(s_json + at, sizeof(s_json) - at, "]}");
+    TEST_ASSERT_FALSE(ui_presets_from_json(s_json, &s_p, s_err, sizeof(s_err)));
+    TEST_ASSERT_EQUAL_STRING("preset \"p5\": at most 32 different MQTT fields", s_err);
+}
+
+/* The largest presets.json with MQTT fields: 16 split presets of 8 cells, 32 keys of 23 bytes, names of
+ * control characters, every option at its longest, 8 schedule entries. */
+static void test_a_full_set_with_mqtt_fields_fits_the_save_buffer(void)
+{
+    static const uint8_t k_tree[UI_SPLIT_NODES] = {
+        UI_RATIO_1_2 | UI_SPLIT_NO_LINE,
+        UI_RATIO_1_4 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0,
+        UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0,
+        UI_RATIO_1_4 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, UI_RATIO_1_3 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0,
+        UI_RATIO_1_2 | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE, 0, 0,
+    };
+    memset(&s_p, 0, sizeof(s_p));
+    for (int k = 0; k < UI_MQTT_KEYS; k++) {
+        snprintf(s_p.mqtt.key[k], sizeof(s_p.mqtt.key[k]), "k%022d", k);
+    }
+    s_p.mqtt.count = UI_MQTT_KEYS;
+    for (int i = 0; i < UI_PRESET_MAX; i++) {
+        ui_preset_t *p = &s_p.presets[i];
+        snprintf(p->id, sizeof(p->id), "preset-%08d", i);
+        memset(p->name, 0x01, UI_PRESET_NAME_LEN - 1);
+        p->layout = UI_LAYOUT_SPLIT;
+        memcpy(p->split, k_tree, sizeof(k_tree));
+        for (int k = 0; k < UI_SPLIT_CELLS; k++) {
+            p->slots[k] = (uint8_t)(UI_FIELD_MQTT + (i * UI_SPLIT_CELLS + k) % UI_MQTT_KEYS); /* first use in order */
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
+    printf("the largest presets.json with MQTT fields: %u bytes of %u\n", (unsigned)n, (unsigned)sizeof(buf));
+    ui_presets_t back;
+    TEST_ASSERT_TRUE_MESSAGE(ui_presets_from_json(buf, &back, s_err, sizeof(s_err)), s_err);
+    TEST_ASSERT_EQUAL_MEMORY(&s_p, &back, sizeof(s_p));
+}
+
 static void test_a_preset_counts_the_slots_its_layout_uses(void)
 {
     TEST_ASSERT_EQUAL_INT(6, ui_preset_slots(&s_p.presets[0])); /* Home: Classic */
@@ -563,5 +682,9 @@ int main(void)
     RUN_TEST(test_bad_split_trees_are_rejected_with_a_reason);
     RUN_TEST(test_a_full_set_of_split_presets_fits_the_save_buffer);
     RUN_TEST(test_a_preset_counts_the_slots_its_layout_uses);
+    RUN_TEST(test_mqtt_fields_name_their_keys);
+    RUN_TEST(test_mqtt_fields_survive_a_round_trip);
+    RUN_TEST(test_bad_mqtt_fields_are_rejected);
+    RUN_TEST(test_a_full_set_with_mqtt_fields_fits_the_save_buffer);
     return UNITY_END();
 }
```


`test/host/test_ui_catalog.c`:

```diff
--- a/test/host/test_ui_catalog.c
+++ b/test/host/test_ui_catalog.c
@@ -171,6 +171,30 @@ static void test_fields_carry_their_kind_label_and_current_value(void)
     TEST_ASSERT_EQUAL_STRING("", str(wx, "value"));
 }
 
+/* The mapped MQTT fields follow the built-in ones (spec §10.3): the editor offers them by kind. */
+static void test_fields_list_the_mapped_mqtt_fields(void)
+{
+    ui_context_t ctx = fixture_context();
+    fixture_mqtt(&ctx);
+    TEST_ASSERT_TRUE(ui_catalog_fields_json(&ctx, s_out, sizeof(s_out)) > 0);
+    s_root = cJSON_Parse(s_out);
+    const cJSON *fields = cJSON_GetObjectItemCaseSensitive(s_root, "fields");
+    TEST_ASSERT_EQUAL_INT(UI_FIELD_COUNT - 1 + 5, cJSON_GetArraySize(fields));
+    const cJSON *f = by_id(fields, "mqtt.outdoor");
+    TEST_ASSERT_EQUAL_STRING("number", str(f, "kind"));
+    TEST_ASSERT_EQUAL_STRING("Outside", str(f, "label"));
+    TEST_ASSERT_EQUAL_STRING("21.5 \xC2\xB0" "C", str(f, "value"));
+    TEST_ASSERT_EQUAL_STRING("fresh", str(f, "state"));
+    f = by_id(fields, "mqtt.door");
+    TEST_ASSERT_EQUAL_STRING("text", str(f, "kind"));
+    TEST_ASSERT_EQUAL_STRING("Closed", str(f, "value"));
+    f = by_id(fields, "mqtt.power");
+    TEST_ASSERT_EQUAL_STRING("stale", str(f, "state"));
+    TEST_ASSERT_EQUAL_INT(3 * 3600, num(f, "age_s"));
+    TEST_ASSERT_EQUAL_STRING("missing", str(by_id(fields, "mqtt.washer"), "state"));
+    TEST_ASSERT_NULL(by_id(fields, "mqtt.window")); /* the presets name it, no mapping does */
+}
+
 static void test_a_buffer_too_small_gives_nothing(void)
 {
     TEST_ASSERT_EQUAL_UINT(0, ui_catalog_layouts_json(s_out, 64));
@@ -184,6 +208,7 @@ int main(void)
     RUN_TEST(test_layouts_list_their_slots_with_rectangles_sizes_and_kinds);
     RUN_TEST(test_layouts_publish_the_split_rules);
     RUN_TEST(test_fields_carry_their_kind_label_and_current_value);
+    RUN_TEST(test_fields_list_the_mapped_mqtt_fields);
     RUN_TEST(test_a_buffer_too_small_gives_nothing);
     return UNITY_END();
 }
```


`test/host/test_ui_widget_fit.c`:

```diff
--- a/test/host/test_ui_widget_fit.c
+++ b/test/host/test_ui_widget_fit.c
@@ -274,6 +274,55 @@ static void test_every_field_fits_every_cell_a_split_can_make(void)
     }
 }
 
+/* MQTT fields at their longest (spec §12.5): a 23-byte label, a 7-byte unit, a number of 8 digits with a
+ * decimal and a 47-byte text, in English and in Czech, fresh and stale, in every cell a tree can make. */
+static void test_mqtt_fields_fit_every_cell_a_split_can_make(void)
+{
+    static ha_fields_t f;
+    static ha_store_t store;
+    static ui_mqtt_keys_t keys;
+    memset(&f, 0, sizeof(f));
+    f.count = 2;
+    f.field[0] = (ha_field_t){ .key = "n", .label = "Teplota u garáže dole", .kind = HA_KIND_NUMBER,
+                               .unit = "\xC2\xB5g/m\xC2\xB3" };
+    f.field[1] = (ha_field_t){ .key = "t", .label = "Waschmaschine im Keller", .kind = HA_KIND_TEXT };
+    ha_store_init(&store);
+    ha_store_rebuild(&store, &f);
+    ha_store_set_default_ttl(&store, 3600);
+    keys = (ui_mqtt_keys_t){ .count = 2, .key = { "n", "t" } };
+    s_cell_count = 0;
+    reach(400, 279, 0);
+    for (int variant = 0; variant < 4; variant++) {
+        ui_context_t ctx = split_context(variant % 2);
+        ctx.mqtt = &store;
+        ctx.mqtt_keys = &keys;
+        time_t at = variant < 2 ? FIX_NOW : FIX_NOW - 2 * 3600; /* stale: its age at the bottom right */
+        ha_value_t v = { .kind = HA_KIND_NUMBER, .number = -12345678, .decimals = 1 };
+        ha_store_set(&store, 0, &v, at);
+        v = (ha_value_t){ .kind = HA_KIND_TEXT, .text = "Wäsche fertig: bitte ausräumen und aufhängen" };
+        ha_store_set(&store, 1, &v, at);
+        for (int i = 0; i < s_cell_count; i++) {
+            int w = s_cells[i].w, h = s_cells[i].h;
+            for (int k = 0; k < 2; k++) {
+                if (ui_split_field_size(k == 0 ? UI_FK_NUMBER : UI_FK_TEXT, w, h) < 0) {
+                    continue; /* no room: drawn as nothing, and refused in a preset */
+                }
+                gfx_rect_t r = { (int16_t)(400 - w), (int16_t)(300 - h), (int16_t)w, (int16_t)h };
+                gfx_fb_init(&s_fb, s_buf, 400, 300);
+                gfx_clear(&s_fb, GFX_WHITE);
+                ui_draw_cell(&s_fb, r, &ctx, (ui_field_id_t)(UI_FIELD_MQTT + k), UI_STALE_STALE);
+                char msg[80];
+                snprintf(msg, sizeof(msg), "mqtt %s at %d×%d, variant %d", keys.key[k], w, h, variant);
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
 /* A field with no room in its cell isn't drawn at all, rather than cut. */
 static void test_a_field_without_room_draws_nothing(void)
 {
@@ -296,5 +345,6 @@ int main(void)
     RUN_TEST(test_a_number_keeps_its_size_as_its_digits_change);
     RUN_TEST(test_every_field_fits_every_cell_a_split_can_make);
     RUN_TEST(test_a_field_without_room_draws_nothing);
+    RUN_TEST(test_mqtt_fields_fit_every_cell_a_split_can_make);
     return UNITY_END();
 }
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -193,7 +193,7 @@ file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/components/ui/*.c)
 add_library(ui STATIC ${UI_SOURCES})
 target_include_directories(ui PUBLIC ${REPO_ROOT}/components/ui/include)
 target_compile_options(ui PRIVATE ${REFLBO_WARNINGS})
-target_link_libraries(ui PUBLIC gfx locale datastore util map radar_logic adsb_logic
+target_link_libraries(ui PUBLIC gfx locale datastore util map radar_logic adsb_logic ha_mqtt_logic
                       PRIVATE cjson scheduler weather_logic astro m)
 
 # reflbo_host_test(<name> <libs...>): builds <name>.c against Unity and registers it with ctest.
```


Run: `cmake --build build-host 2>&1 | grep -o "error: [^;]*" | sort | uniq -c`
Expected: the build stops on errors such as `use of undeclared identifier 'UI_FIELD_MQTT'`, `unknown type name 'ui_mqtt_keys_t'`, `no member named 'mqtt' in 'ui_context_t'`, `no member named 'mqtt_keys' in 'ui_context_t'`, `no member named 'mqtt' in 'ui_presets_t'` and `call to undeclared function 'ui_field_is_mqtt'`.

- [ ] **Step 2: The fields, the presets' keys and their drawing.**

`components/ui/CMakeLists.txt`:

```diff
--- a/components/ui/CMakeLists.txt
+++ b/components/ui/CMakeLists.txt
@@ -4,5 +4,5 @@ idf_component_register(SRCS "ui_fields.c" "ui_layout.c" "ui_preset.c" "ui_preset
                             "ui_screens.c" "ui_config.c" "ui_catalog.c" "ui_forecast.c" "ui_radar.c"
                             "ui_flights.c" "ui_split.c"
                        INCLUDE_DIRS "include"
-                       REQUIRES gfx locale datastore util map radar adsb
+                       REQUIRES gfx locale datastore util map radar adsb ha_mqtt
                        PRIV_REQUIRES json scheduler weather astro)
```


`components/ui/include/ui_fields.h`:

```diff
--- a/components/ui/include/ui_fields.h
+++ b/components/ui/include/ui_fields.h
@@ -6,6 +6,7 @@
 
 #include "datastore.h"
 #include "gfx.h"
+#include "ha_store.h"
 #include "lang.h"
 #include "util_calendar.h"
 
@@ -67,9 +68,24 @@ typedef enum {
     UI_FIELD_POLLEN_RAGWEED,
     UI_FIELD_WX_RAIN2H, /* M6 (D27) */
     UI_FIELD_RAIN_MAP,  /* M6 (D23) */
-    UI_FIELD_COUNT,
+    UI_FIELD_COUNT,     /* the built-in fields; the MQTT fields follow */
 } ui_field_id_t;
 
+/* mqtt.<key> fields (spec §12.5, M7): UI_FIELD_MQTT + k is the presets' key k (ui_presets_t.mqtt), which a
+ * mapping in the store may name. They have no ui_field_info(). */
+#define UI_MQTT_KEYS 32
+#define UI_FIELD_MQTT ((int)UI_FIELD_COUNT)
+
+typedef struct {
+    uint8_t count;
+    char key[UI_MQTT_KEYS][HA_KEY_LEN]; /* in the order presets.json first names them */
+} ui_mqtt_keys_t;
+
+static inline bool ui_field_is_mqtt(int field)
+{
+    return field >= UI_FIELD_MQTT && field < UI_FIELD_MQTT + UI_MQTT_KEYS;
+}
+
 typedef struct {
     const char *id; /* as in presets.json: "env.temp" */
     ui_field_kind_t kind;
@@ -124,6 +140,8 @@ typedef struct {
     ui_sync_mark_t sync;    /* the status bar's sync state (spec §5.2) */
     ui_wifi_mark_t wifi;
     const ui_radar_t *radar; /* M6: the radars' map, frames and settings; NULL for none */
+    const ha_store_t *mqtt;  /* M7: the MQTT fields' values and the message; NULL for none */
+    const ui_mqtt_keys_t *mqtt_keys; /* the keys of the presets drawn; NULL for none */
 } ui_context_t;
 
 typedef enum {
@@ -159,3 +177,5 @@ typedef struct {
 } ui_value_t;
 
 void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out);
+/* The store's entry `i` as a field shows it: its label, unit and value (the catalogue lists them all). */
+void ui_mqtt_value(const ui_context_t *ctx, int i, ui_value_t *out);
```


`components/ui/include/ui_preset.h`:

```diff
--- a/components/ui/include/ui_preset.h
+++ b/components/ui/include/ui_preset.h
@@ -108,6 +108,7 @@ typedef struct {
     ui_preset_t presets[UI_PRESET_MAX];
     ui_schedule_t schedule;
     uint8_t offered; /* UI_OFFERED_* */
+    ui_mqtt_keys_t mqtt; /* the keys the slots' mqtt.<key> fields name (M7, spec §12.5) */
 } ui_presets_t;
 
 /* How many of `p`'s slots its layout uses: a fixed layout's slots, or the cells of its split tree
```


`components/ui/ui_fields.c`:

```diff
--- a/components/ui/ui_fields.c
+++ b/components/ui/ui_fields.c
@@ -202,10 +202,51 @@ static void resolve_clock(const ui_context_t *ctx, ui_field_id_t field, ui_value
     }
 }
 
+void ui_mqtt_value(const ui_context_t *ctx, int i, ui_value_t *out)
+{
+    const ha_entry_t *e = &ctx->mqtt->entry[i];
+    out->kind = e->kind == HA_KIND_TEXT ? UI_FK_TEXT : UI_FK_NUMBER;
+    out->label = e->label;
+    ha_freshness_t fresh = ha_store_freshness(ctx->mqtt, i, ctx->now);
+    if (fresh == HA_MISSING) {
+        return;
+    }
+    out->state = fresh == HA_STALE ? UI_VALUE_STALE : UI_VALUE_FRESH;
+    out->age_s = (uint32_t)ctx->now > e->updated ? (uint32_t)ctx->now - e->updated : 0;
+    if (e->kind == HA_KIND_TEXT) {
+        snprintf(out->text, sizeof(out->text), "%s", e->text);
+        return;
+    }
+    lang_format_decimal(ctx->lang, e->number, e->decimals, out->text, sizeof(out->text));
+    if (e->decimals > 0) { /* the whole number, for a slot too narrow for the decimals (spec §5.3) */
+        int64_t scale = e->decimals == 1 ? 10 : e->decimals == 2 ? 100 : 1000; /* 64 bits: no overflow */
+        int64_t whole = ((int64_t)e->number + (e->number >= 0 ? scale / 2 : -scale / 2)) / scale;
+        lang_format_decimal(ctx->lang, (long)whole, 0, out->short_text, sizeof(out->short_text));
+    }
+    snprintf(out->unit, sizeof(out->unit), "%s", e->unit);
+}
+
+/* mqtt.<key> (spec §12.5): the mapping that names the key; a key no mapping names, or no store, is no
+ * field at all, so its slot stays empty. */
+static void resolve_mqtt(const ui_context_t *ctx, int k, ui_value_t *out)
+{
+    const ui_mqtt_keys_t *keys = ctx->mqtt_keys;
+    int i = ctx->mqtt != NULL && keys != NULL && k < keys->count ? ha_store_find(ctx->mqtt, keys->key[k]) : -1;
+    if (i < 0) {
+        out->field = UI_FIELD_NONE;
+        return;
+    }
+    ui_mqtt_value(ctx, i, out);
+}
+
 void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
 {
     memset(out, 0, sizeof(*out));
     out->field = field;
+    if (ui_field_is_mqtt(field)) {
+        resolve_mqtt(ctx, field - UI_FIELD_MQTT, out);
+        return;
+    }
     const ui_field_info_t *info = ui_field_info(field);
     if (info == NULL) {
         return;
```


`components/ui/ui_preset_json.c`:

```diff
--- a/components/ui/ui_preset_json.c
+++ b/components/ui/ui_preset_json.c
@@ -3,6 +3,7 @@
 #include <string.h>
 
 #include "cJSON.h"
+#include "ha_fields.h"
 #include "ui_fields.h"
 #include "ui_preset.h"
 #include "ui_split.h"
@@ -82,7 +83,53 @@ static bool parse_hhmm(const cJSON *item, uint16_t *out)
     return true;
 }
 
-static bool parse_slots(const cJSON *slots, const ui_layout_t *layout, ui_preset_t *out, char *err, size_t size)
+/* A field as presets.json names it: a built-in one, or mqtt.<key>, whose key joins the presets' key table
+ * (spec §12.5). UI_FIELD_NONE for neither, and *full when it would be a 33rd key. */
+static int field_by_name(ui_presets_t *doc, const char *name, bool *full)
+{
+    ui_field_id_t f = ui_field_by_name(name);
+    if (f != UI_FIELD_NONE || strncmp(name, "mqtt.", 5) != 0 || !ha_key_valid(name + 5)) {
+        return f;
+    }
+    for (int k = 0; k < doc->mqtt.count; k++) {
+        if (strcmp(doc->mqtt.key[k], name + 5) == 0) {
+            return UI_FIELD_MQTT + k;
+        }
+    }
+    if (doc->mqtt.count >= UI_MQTT_KEYS) {
+        *full = true;
+        return UI_FIELD_NONE;
+    }
+    snprintf(doc->mqtt.key[doc->mqtt.count], HA_KEY_LEN, "%s", name + 5);
+    return UI_FIELD_MQTT + doc->mqtt.count++;
+}
+
+/* The kinds a field may be: an MQTT field's mapping says a number or a text, so a slot takes one wherever
+ * it takes either (spec §12.5). */
+static uint32_t field_kinds(int field)
+{
+    return ui_field_is_mqtt(field) ? UI_KIND(UI_FK_NUMBER) | UI_KIND(UI_FK_TEXT)
+                                   : UI_KIND(ui_field_info((ui_field_id_t)field)->kind);
+}
+
+static void field_name(const ui_presets_t *doc, int field, char *out, size_t size)
+{
+    const ui_field_info_t *info = ui_field_info((ui_field_id_t)field);
+    if (ui_field_is_mqtt(field) && field - UI_FIELD_MQTT < doc->mqtt.count) {
+        snprintf(out, size, "mqtt.%s", doc->mqtt.key[field - UI_FIELD_MQTT]);
+    } else {
+        snprintf(out, size, "%s", info != NULL ? info->id : "");
+    }
+}
+
+static bool unknown_field(const char *id, const char *name, bool full, char *err, size_t size)
+{
+    return full ? fail(err, size, "preset \"%s\": at most %d different MQTT fields", id, UI_MQTT_KEYS)
+                : fail(err, size, "preset \"%s\": unknown field \"%s\"", id, name);
+}
+
+static bool parse_slots(const cJSON *slots, const ui_layout_t *layout, ui_presets_t *doc, ui_preset_t *out, char *err,
+                        size_t size)
 {
     if (!cJSON_IsObject(slots)) {
         return fail(err, size, "preset \"%s\": slots must be an object of slot names", out->id);
@@ -100,11 +147,12 @@ static bool parse_slots(const cJSON *slots, const ui_layout_t *layout, ui_preset
         if (!cJSON_IsString(slot)) {
             return fail(err, size, "preset \"%s\": slot %s needs a field id", out->id, slot->string);
         }
-        ui_field_id_t field = ui_field_by_name(slot->valuestring);
+        bool full = false;
+        int field = field_by_name(doc, slot->valuestring, &full);
         if (field == UI_FIELD_NONE) {
-            return fail(err, size, "preset \"%s\": unknown field \"%s\"", out->id, slot->valuestring);
+            return unknown_field(out->id, slot->valuestring, full, err, size);
         }
-        if (!(layout->slots[index].kinds & UI_KIND(ui_field_info(field)->kind))) {
+        if (!(layout->slots[index].kinds & field_kinds(field))) {
             return fail(err, size, "preset \"%s\": slot %s can't show %s", out->id, slot->string, slot->valuestring);
         }
         out->slots[index] = (uint8_t)field;
@@ -113,6 +161,7 @@ static bool parse_slots(const cJSON *slots, const ui_layout_t *layout, ui_preset
 }
 
 typedef struct {
+    ui_presets_t *doc;
     ui_preset_t *out;
     int nodes; /* taken so far, in preorder */
     int cells;
@@ -136,9 +185,10 @@ static bool parse_node(tree_t *t, const cJSON *node)
             if (!cJSON_IsString(field)) {
                 return fail(t->err, t->size, "preset \"%s\": a cell's field must be a field id", id);
             }
-            ui_field_id_t f = ui_field_by_name(field->valuestring);
+            bool full = false;
+            int f = field_by_name(t->doc, field->valuestring, &full);
             if (f == UI_FIELD_NONE) {
-                return fail(t->err, t->size, "preset \"%s\": unknown field \"%s\"", id, field->valuestring);
+                return unknown_field(id, field->valuestring, full, t->err, t->size);
             }
             t->out->slots[t->cells] = (uint8_t)f;
         }
@@ -164,8 +214,18 @@ static bool parse_node(tree_t *t, const cJSON *node)
     return parse_node(t, a) && parse_node(t, b);
 }
 
+/* The cell can show the field: an MQTT field as a number or as a text. */
+static bool cell_shows(int field, gfx_rect_t cell)
+{
+    if (ui_field_is_mqtt(field)) {
+        return ui_split_field_size(UI_FK_NUMBER, cell.w, cell.h) >= 0 || ui_split_field_size(UI_FK_TEXT, cell.w, cell.h) >= 0;
+    }
+    const ui_field_info_t *info = ui_field_info((ui_field_id_t)field);
+    return info == NULL || ui_split_field_size(info->kind, cell.w, cell.h) >= 0;
+}
+
 /* The split layout's tree, then its geometry and what each cell can show (spec §5.2). */
-static bool parse_split(const cJSON *split, ui_preset_t *out, char *err, size_t size)
+static bool parse_split(const cJSON *split, ui_presets_t *doc, ui_preset_t *out, char *err, size_t size)
 {
     if (split == NULL || cJSON_IsNull(split)) {
         return true; /* one empty cell */
@@ -173,7 +233,7 @@ static bool parse_split(const cJSON *split, ui_preset_t *out, char *err, size_t
     if (!cJSON_IsObject(split)) {
         return fail(err, size, "preset \"%s\": split must be a tree of splits and cells", out->id);
     }
-    tree_t t = { .out = out, .err = err, .size = size };
+    tree_t t = { .doc = doc, .out = out, .err = err, .size = size };
     if (!parse_node(&t, split)) {
         return false;
     }
@@ -183,16 +243,17 @@ static bool parse_split(const cJSON *split, ui_preset_t *out, char *err, size_t
                     UI_SPLIT_MIN_H);
     }
     for (int i = 0; i < g.cells; i++) {
-        const ui_field_info_t *info = ui_field_info((ui_field_id_t)out->slots[i]);
-        if (info != NULL && ui_split_field_size(info->kind, g.cell[i].w, g.cell[i].h) < 0) {
+        if (!cell_shows(out->slots[i], g.cell[i])) {
+            char name[HA_KEY_LEN + 8];
+            field_name(doc, out->slots[i], name, sizeof(name));
             return fail(err, size, "preset \"%s\": cell %d (%d×%d) can't show %s", out->id, i + 1, g.cell[i].w,
-                        g.cell[i].h, info->id);
+                        g.cell[i].h, name);
         }
     }
     return true;
 }
 
-static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t size)
+static bool parse_preset(const cJSON *item, ui_presets_t *doc, ui_preset_t *out, char *err, size_t size)
 {
     memset(out, 0, sizeof(*out));
     const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
@@ -215,10 +276,11 @@ static bool parse_preset(const cJSON *item, ui_preset_t *out, char *err, size_t
     out->in_cycle = optional_bool(item, "in_cycle", true);
     const cJSON *slots = cJSON_GetObjectItemCaseSensitive(item, "slots");
     if (slots != NULL && !cJSON_IsNull(slots) &&
-        !parse_slots(slots, ui_layout((ui_layout_id_t)layout), out, err, size)) {
+        !parse_slots(slots, ui_layout((ui_layout_id_t)layout), doc, out, err, size)) {
         return false;
     }
-    if (layout == UI_LAYOUT_SPLIT && !parse_split(cJSON_GetObjectItemCaseSensitive(item, "split"), out, err, size)) {
+    if (layout == UI_LAYOUT_SPLIT &&
+        !parse_split(cJSON_GetObjectItemCaseSensitive(item, "split"), doc, out, err, size)) {
         return false;
     }
     const cJSON *options = cJSON_GetObjectItemCaseSensitive(item, "options");
@@ -331,7 +393,7 @@ static bool parse(const cJSON *root, ui_presets_t *out, char *err, size_t size)
     cJSON_ArrayForEach(item, presets)
     {
         ui_preset_t *p = &out->presets[out->count];
-        if (!parse_preset(item, p, err, size)) {
+        if (!parse_preset(item, out, p, err, size)) {
             return false;
         }
         if (ui_presets_find(out, p->id) >= 0) {
@@ -379,26 +441,27 @@ bool ui_presets_from_json(const char *json, ui_presets_t *out, char *err, size_t
 }
 
 /* A split tree's node and everything under it, in preorder: `at` the node, `cell` its first cell. */
-static cJSON *node_json(const ui_preset_t *p, int *at, int *cell)
+static cJSON *node_json(const ui_presets_t *doc, const ui_preset_t *p, int *at, int *cell)
 {
     cJSON *obj = cJSON_CreateObject();
     uint8_t node = p->split[(*at)++];
     if ((node & UI_SPLIT_RATIO) == 0) {
-        const ui_field_info_t *info = ui_field_info((ui_field_id_t)p->slots[(*cell)++]);
-        if (info != NULL) {
-            cJSON_AddStringToObject(obj, "field", info->id);
+        char name[HA_KEY_LEN + 8];
+        field_name(doc, p->slots[(*cell)++], name, sizeof(name));
+        if (name[0] != '\0') {
+            cJSON_AddStringToObject(obj, "field", name);
         }
         return obj;
     }
     cJSON_AddStringToObject(obj, "split", node & UI_SPLIT_COLUMNS ? "columns" : "rows");
     cJSON_AddStringToObject(obj, "ratio", ui_split_ratio_name(node & UI_SPLIT_RATIO));
     cJSON_AddBoolToObject(obj, "line", !(node & UI_SPLIT_NO_LINE));
-    cJSON_AddItemToObject(obj, "a", node_json(p, at, cell));
-    cJSON_AddItemToObject(obj, "b", node_json(p, at, cell));
+    cJSON_AddItemToObject(obj, "a", node_json(doc, p, at, cell));
+    cJSON_AddItemToObject(obj, "b", node_json(doc, p, at, cell));
     return obj;
 }
 
-static cJSON *preset_json(const ui_preset_t *p)
+static cJSON *preset_json(const ui_presets_t *doc, const ui_preset_t *p)
 {
     const ui_layout_t *layout = ui_layout((ui_layout_id_t)p->layout);
     cJSON *obj = cJSON_CreateObject();
@@ -409,13 +472,14 @@ static cJSON *preset_json(const ui_preset_t *p)
     if (p->layout == UI_LAYOUT_SPLIT) {
         int at = 0, cell = 0;
         bool whole = ui_split_nodes(p->split) > 0; /* a tree cut short can't be walked: one empty cell */
-        cJSON_AddItemToObject(obj, "split", whole ? node_json(p, &at, &cell) : cJSON_CreateObject());
+        cJSON_AddItemToObject(obj, "split", whole ? node_json(doc, p, &at, &cell) : cJSON_CreateObject());
     } else {
         cJSON *slots = cJSON_AddObjectToObject(obj, "slots");
         for (int i = 0; i < layout->slot_count; i++) {
-            const ui_field_info_t *info = ui_field_info((ui_field_id_t)p->slots[i]);
-            if (info != NULL) {
-                cJSON_AddStringToObject(slots, layout->slots[i].name, info->id);
+            char name[HA_KEY_LEN + 8];
+            field_name(doc, p->slots[i], name, sizeof(name));
+            if (name[0] != '\0') {
+                cJSON_AddStringToObject(slots, layout->slots[i].name, name);
             }
         }
     }
@@ -446,7 +510,7 @@ size_t ui_presets_to_json(const ui_presets_t *p, char *out, size_t size)
     cJSON_AddNumberToObject(cycle, "interval_s", p->cycle_interval_s);
     cJSON *presets = cJSON_AddArrayToObject(root, "presets");
     for (int i = 0; i < p->count; i++) {
-        cJSON_AddItemToArray(presets, preset_json(&p->presets[i]));
+        cJSON_AddItemToArray(presets, preset_json(p, &p->presets[i]));
     }
     if (p->schedule.enabled || p->schedule.count) {
         cJSON *schedule = cJSON_AddObjectToObject(root, "schedule");
```


`components/ui/ui_dashboard.c`:

```diff
--- a/components/ui/ui_dashboard.c
+++ b/components/ui/ui_dashboard.c
@@ -36,13 +36,12 @@ static void draw_separators(gfx_fb_t *fb, ui_layout_id_t layout)
 bool ui_draw_cell(gfx_fb_t *fb, gfx_rect_t cell, const ui_context_t *ctx, ui_field_id_t field,
                   ui_stale_policy_t policy)
 {
-    const ui_field_info_t *info = ui_field_info(field);
-    int size = info != NULL ? ui_split_field_size(info->kind, cell.w, cell.h) : -1;
+    ui_value_t v;
+    ui_resolve(ctx, field, &v); /* an MQTT field's kind is its mapping's */
+    int size = v.field != UI_FIELD_NONE ? ui_split_field_size(v.kind, cell.w, cell.h) : -1;
     if (size < 0) {
         return false;
     }
-    ui_value_t v;
-    ui_resolve(ctx, field, &v);
     ui_widget_draw(fb, cell, (ui_size_t)size, &v, policy, ctx->lang);
     return v.state == UI_VALUE_STALE;
 }
@@ -98,6 +97,9 @@ void ui_draw_dashboard(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t
         for (int i = 0; i < layout->slot_count; i++) {
             ui_value_t v;
             ui_resolve(&c, (ui_field_id_t)preset->slots[i], &v);
+            if (v.field != UI_FIELD_NONE && !(layout->slots[i].kinds & UI_KIND(v.kind))) {
+                continue; /* an MQTT field whose mapping's kind the slot can't show: empty (spec §12.5) */
+            }
             any_stale |= v.state == UI_VALUE_STALE;
             ui_widget_draw(fb, layout->slots[i].rect, layout->slots[i].size, &v,
                            (ui_stale_policy_t)preset->stale_policy, c.lang);
```


`components/ui/ui_widget.c`:

```diff
--- a/components/ui/ui_widget.c
+++ b/components/ui/ui_widget.c
@@ -279,6 +279,12 @@ static const char *display_text(const ui_value_t *v, ui_size_t size)
     }
 }
 
+/* An MQTT field has no icon (spec §12.5): its label stands where the icon would, in the small face. */
+static bool label_symbol(const ui_value_t *v)
+{
+    return ui_field_is_mqtt(v->field) && v->label != NULL && v->label[0] != '\0';
+}
+
 static bool numeric(const ui_value_t *v)
 {
     return v->state != UI_VALUE_MISSING &&
@@ -300,7 +306,13 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
         int sym_size = v->kind == UI_FK_MOON ? 28 : f->icon;
         int sym_w = v->kind == UI_FK_BATTERY ? sym_size * 3 / 2 : sym_size;
         int sym_x = r.x + (r.w - sym_w) / 2;
-        draw_symbol(fb, v, sym_x, r.y + 12, sym_size);
+        if (label_symbol(v)) {
+            gfx_text_ellipsize(&gfx_font_sans_12, v->label, r.w - 8, fit, sizeof(fit));
+            gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ r.x, (int16_t)(r.y + 12), r.w, (int16_t)sym_size },
+                             GFX_ALIGN_CENTER, fit, GFX_BLACK);
+        } else {
+            draw_symbol(fb, v, sym_x, r.y + 12, sym_size);
+        }
         if (shown.trend) {
             gfx_text(fb, &gfx_font_sans_bold_16, sym_x + sym_w + 4, r.y + 12 + sym_size - 4,
                      shown.trend > 0 ? ARROW_UP : ARROW_DOWN, GFX_BLACK);
@@ -314,8 +326,9 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
             return;
         }
         int max_w = r.w - 8;
-        if (!numeric(v) && gfx_text_width(vf, value) > max_w) { /* a name: two lines in the regular face */
-            const gfx_font_t *tf = f->unit;
+        const gfx_font_t *tf = f->unit;
+        bool two_lines = r.h >= 12 + f->icon + 10 + 2 * tf->line_height + 2; /* else one, cut (M6b review) */
+        if (!numeric(v) && gfx_text_width(vf, value) > max_w && two_lines) { /* a name: two lines, regular face */
             char second[sizeof(fit)];
             ui_split_two_lines(tf, value, max_w, fit, second, sizeof(fit));
             int top = r.y + 12 + f->icon + 10;
@@ -336,7 +349,15 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
         draw_group(fb, f, vf, &shown, value, r.x + (r.w - w) / 2, baseline);
         return;
     }
-    int sym_w = draw_symbol(fb, v, r.x + 14, r.y + (r.h - f->icon) / 2, f->icon); /* wide: side by side */
+    int sym_w; /* wide: side by side */
+    if (label_symbol(v)) { /* up to two fifths of the cell */
+        gfx_text_ellipsize(&gfx_font_sans_12, v->label, r.w * 2 / 5, fit, sizeof(fit));
+        sym_w = gfx_text_width(&gfx_font_sans_12, fit);
+        gfx_text_in_rect(fb, &gfx_font_sans_12, (gfx_rect_t){ (int16_t)(r.x + 14), r.y, (int16_t)sym_w, r.h },
+                         GFX_ALIGN_LEFT, fit, GFX_BLACK);
+    } else {
+        sym_w = draw_symbol(fb, v, r.x + 14, r.y + (r.h - f->icon) / 2, f->icon);
+    }
     int x = r.x + 14 + sym_w + 10;
     if (!numeric(v)) {
         gfx_text_ellipsize(vf, value, r.x + r.w - 6 - x, fit, sizeof(fit));
```


`components/ui/ui_catalog.c`:

```diff
--- a/components/ui/ui_catalog.c
+++ b/components/ui/ui_catalog.c
@@ -132,5 +132,24 @@ size_t ui_catalog_fields_json(const ui_context_t *ctx, char *out, size_t size)
         }
         cJSON_AddItemToArray(fields, fo);
     }
+    for (int i = 0; ctx->mqtt != NULL && i < ctx->mqtt->count; i++) { /* the mapped ones (spec §12.5) */
+        static ui_value_t v;
+        memset(&v, 0, sizeof(v));
+        ui_mqtt_value(ctx, i, &v);
+        char id[HA_KEY_LEN + 8], value[sizeof(v.text) + sizeof(v.unit) + 2];
+        snprintf(id, sizeof(id), "mqtt.%s", ctx->mqtt->entry[i].key);
+        snprintf(value, sizeof(value), "%s%s%s", v.state == UI_VALUE_MISSING ? "" : v.text,
+                 v.state != UI_VALUE_MISSING && v.unit[0] ? " " : "", v.state == UI_VALUE_MISSING ? "" : v.unit);
+        cJSON *fo = cJSON_CreateObject();
+        cJSON_AddStringToObject(fo, "id", id);
+        cJSON_AddStringToObject(fo, "kind", k_kinds[v.kind]);
+        cJSON_AddStringToObject(fo, "label", v.label);
+        cJSON_AddStringToObject(fo, "value", value);
+        cJSON_AddStringToObject(fo, "state", k_states[v.state]);
+        if (v.state == UI_VALUE_STALE) {
+            cJSON_AddNumberToObject(fo, "age_s", v.age_s);
+        }
+        cJSON_AddItemToArray(fields, fo);
+    }
     return print(root, out, size);
 }
```


Run: `cmake --build build-host && for t in test_ui_fields test_ui_preset test_ui_catalog test_ui_widget_fit; do ./build-host/$t | tail -1; done`
Expected: `OK` four times (22, 32, 5 and 10 tests).

- [ ] **Step 3: The goldens.** Copy them from the branch and draw them again; they must match byte for byte:

```bash
git checkout plan/m7 -- \
  test/host/golden/dash_mqtt_grid.pbm \
  test/host/golden/dash_mqtt_home_cs.pbm
```


```bash
for f in mqtt_grid mqtt_home_cs; do build-host/render_dashboard $f /tmp/$f.pbm && cmp /tmp/$f.pbm test/host/golden/dash_$f.pbm && echo "$f same"; done
python3 tools/render.py && open captures/render/dash_mqtt_grid.png captures/render/dash_mqtt_home_cs.png
```

Expected: `mqtt_grid same`, `mqtt_home_cs same`. `dash_mqtt_grid` (the Grid layout's six slots): Outside 21.5 °C, CO2 612 ppm, Front door Closed, Power 1.24 kW stale with its age (its own `ttl_s` of an hour, three hours old), Washer missing, and `window`, which no mapping names, an empty slot; `dash_mqtt_home_cs`: Home's four small slots with each label where an icon would be, in Czech (21,5 °C).

- [ ] **Step 4: The app: the presets' keys in every context, and the snapshot's cap.**

`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -225,6 +225,7 @@ void app_ui_context(ui_context_t *ctx)
     localtime_r(&now, &ctx->local);
     ctx->local_day = local_day(&ctx->local);
     ctx->radar = app_radar_ui(); /* M6 */
+    ctx->mqtt_keys = &s.presets.mqtt; /* M7: the store comes with its task */
 }
 
 void app_ui_render(void)
```


`main/app_web.c`:

```diff
--- a/main/app_web.c
+++ b/main/app_web.c
@@ -294,6 +294,7 @@ static void preview(const char *method, const char *query, const char *body, uin
         app_radar_prepare(&doc.presets[index]); /* its map, and the frame from its file if PSRAM has none */
         ui_context_t ctx;
         app_ui_context(&ctx);
+        ctx.mqtt_keys = &doc.mqtt; /* the posted document's own keys (spec §12.5) */
         ui_draw_dashboard(fb, &ctx, &doc.presets[index]);
     }
     reply_bmp(fb, out, size, reply);
```


`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -96,7 +96,7 @@ typedef struct {
     app_ui_state_t ui;
     time_t next_alarm;
 } app_snapshot_t;
-_Static_assert(sizeof(app_snapshot_t) <= 4096, "the RTC-RAM snapshot is at most 4 KB (spec §6)");
+_Static_assert(sizeof(app_snapshot_t) <= 5120, "the RTC-RAM snapshot is at most 5 KB (spec §6)");
 
 static RTC_DATA_ATTR app_snapshot_t s_snap;
 static QueueHandle_t s_queue;
```


- [ ] **Step 5: Run the tests, and build.**

Run: `cmake --build build-host && ctest --test-dir build-host | tail -3; tools/idf.sh build 2>&1 | grep -c 'warning:'`
Expected: `100% tests passed, 0 tests failed out of 64`; `0` (the `_Static_assert` holds the snapshot under 5 KB).

- [ ] **Step 6: Commit.**

```bash
git add components/ui main/app.c main/app_ui.c main/app_web.c test/host
git commit -m "feat(ui): MQTT fields in presets and on the dashboard (spec §5.4, §12.5)"
```

### Task 6: The message and the MQTT mark (`ui`, `gfx`, `locale`)

**Files:**
- Modify: `assets/icons/icons.txt`, `components/gfx/icons/gfx_icons.c`, `components/gfx/include/gfx_icons.h` (both generated), `components/locale/include/lang.h`, `components/locale/lang_en.c`, `components/locale/lang_cs.c`, `components/ui/include/ui_fields.h`, `components/ui/include/ui_screens.h`, `components/ui/ui_fields.c`, `components/ui/ui_widget.c`, `components/ui/ui_screens.c`, `components/ui/ui_status.c`, `main/app_menu.c`, `test/host/context_fixtures.h`, `test/host/dashboard_fixtures.h`, `test/host/screen_fixtures.h`, `test/host/test_ui_fields.c`, `test/host/test_ui_widget_fit.c`
- Create (binary, from `plan/m7`): `test/host/golden/dash_home_mqtt_failed.pbm`, `dash_message_fields.pbm`, `screen_message_short_en.pbm`, `screen_message_long_en.pbm`, `screen_message_long_cs.pbm`, `screen_message_inverted_en.pbm`

**Interfaces:**
- Consumes: `ha_store_banner()`, `ha_store_message_freshness()`, `HA_MESSAGE_LEN` (Tasks 3, 4); `ui_context_t.mqtt` (Task 5); `tools/gen_icons.sh`.
- Produces:
  - `UI_FIELD_HA_MESSAGE` ("ha.message", a text field labelled `LS_MESSAGE`, "Message" / "Zpráva");
  - `ui_value_t.text[HA_MESSAGE_LEN]` (was 48 bytes);
  - `ui_context_t.mqtt_failed`: the status bar's MQTT mark (`gfx_icon_mqtt_off_16`);
  - `void ui_draw_message_banner(gfx_fb_t *fb, const ui_context_t *ctx)`;
  - `gfx_icon_message_24`, `gfx_icon_message_48`.

Home Assistant's message (spec §12.7) shows two ways. The banner is a black bar across the bottom of the dashboard, one line in the bold 16 px face cut with an ellipsis, under a 3 px white rim that keeps it apart from black content such as an inverted preset; one line, so the slots above stay readable for the hours it may show. The `ha.message` field has a message icon, and it and any text too wide for an M or larger slot (an MQTT text too) wrap at spaces over as many lines as the slot holds, up to 8, centred, the last one cut with an ellipsis, as is a word wider than a line; the small face keeps one cut line. The wrap runs twice, counting the lines and then drawing them, so no line is kept: about 200 bytes of the app task's stack rather than 900. The status bar's MQTT mark, a broken link, stands beside the sync's marks while the last MQTT session failed (spec §5.2). The longer `ui_value_t.text` makes GCC see that the menu's battery line could overflow (gotcha 33); it prints at most 8 bytes of the level.

- [ ] **Step 1: The icons.** Material Icons' `link_off` and `chat`:

`assets/icons/icons.txt`:

```diff
--- a/assets/icons/icons.txt
+++ b/assets/icons/icons.txt
@@ -15,10 +15,12 @@ sync           sync                  16
 sync_failed    cloud_off             16
 wifi           wifi                  16
 wifi_off       wifi_off              16
+mqtt_off       link_off              16
 air            air                   24 48
 particles      blur_on               24 48
 uv             light_mode            24 48
 pollen         local_florist         24 48
+message        chat                  24 48
 # Weather codes (spec §11), day and night where they differ, and the sun's times (M5).
 wx_clear_day        wi:day-sunny                24 48
 wx_clear_night      wi:night-clear              24 48
```


Run: `tools/gen_icons.sh && git diff --stat components/gfx`
Expected: `gfx_icons.c` and `gfx_icons.h` change, as below:

`components/gfx/include/gfx_icons.h`:

```diff
--- a/components/gfx/include/gfx_icons.h
+++ b/components/gfx/include/gfx_icons.h
@@ -30,6 +30,7 @@ extern const gfx_bitmap_t gfx_icon_sync_16;
 extern const gfx_bitmap_t gfx_icon_sync_failed_16;
 extern const gfx_bitmap_t gfx_icon_wifi_16;
 extern const gfx_bitmap_t gfx_icon_wifi_off_16;
+extern const gfx_bitmap_t gfx_icon_mqtt_off_16;
 extern const gfx_bitmap_t gfx_icon_air_24;
 extern const gfx_bitmap_t gfx_icon_air_48;
 extern const gfx_bitmap_t gfx_icon_particles_24;
@@ -38,6 +39,8 @@ extern const gfx_bitmap_t gfx_icon_uv_24;
 extern const gfx_bitmap_t gfx_icon_uv_48;
 extern const gfx_bitmap_t gfx_icon_pollen_24;
 extern const gfx_bitmap_t gfx_icon_pollen_48;
+extern const gfx_bitmap_t gfx_icon_message_24;
+extern const gfx_bitmap_t gfx_icon_message_48;
 extern const gfx_bitmap_t gfx_icon_wx_clear_day_24;
 extern const gfx_bitmap_t gfx_icon_wx_clear_day_48;
 extern const gfx_bitmap_t gfx_icon_wx_clear_night_24;
```


`components/gfx/icons/gfx_icons.c`:

```diff
--- a/components/gfx/icons/gfx_icons.c
+++ b/components/gfx/icons/gfx_icons.c
@@ -322,6 +322,12 @@ static const uint8_t s_wifi_off_16[] = {
 };
 const gfx_bitmap_t gfx_icon_wifi_off_16 = { s_wifi_off_16, 16, 16 };
 
+static const uint8_t s_mqtt_off_16[] = {
+    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x60, 0x00, 0x30, 0x00, 0x38, 0xF8, 0x64, 0x0C, 0x46, 0x44,
+    0x47, 0x04, 0x60, 0x8C, 0x3E, 0xC8, 0x00, 0x30, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
+};
+const gfx_bitmap_t gfx_icon_mqtt_off_16 = { s_mqtt_off_16, 16, 16 };
+
 static const uint8_t s_air_24[] = {
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x0F, 0x80, 0x00,
     0x0C, 0xC0, 0x00, 0x18, 0xC0, 0x00, 0x00, 0xC0, 0x3F, 0xFF, 0x80, 0x3F, 0xFF, 0x00, 0x00, 0x00,
@@ -446,6 +452,37 @@ static const uint8_t s_pollen_48[] = {
 };
 const gfx_bitmap_t gfx_icon_pollen_48 = { s_pollen_48, 48, 48 };
 
+static const uint8_t s_message_24[] = {
+    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0xF8, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x3F,
+    0xFF, 0xFC, 0x3C, 0x00, 0x3C, 0x3C, 0x00, 0x3C, 0x3F, 0xFF, 0xFC, 0x3C, 0x00, 0x3C, 0x3C, 0x00,
+    0x3C, 0x3F, 0xFF, 0xFC, 0x3C, 0x03, 0xFC, 0x3C, 0x03, 0xFC, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC,
+    0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xF8, 0x3C, 0x00, 0x00, 0x38, 0x00, 0x00, 0x30, 0x00, 0x00, 0x20,
+    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
+};
+const gfx_bitmap_t gfx_icon_message_24 = { s_message_24, 24, 24 };
+
+static const uint8_t s_message_48[] = {
+    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
+    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x07, 0xFF,
+    0xFF, 0xFF, 0xFF, 0xE0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
+    0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF,
+    0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x0F, 0xF0,
+    0x00, 0x00, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF0,
+    0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xF0, 0x00, 0x00,
+    0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x0F, 0xF0,
+    0x00, 0x00, 0x0F, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
+    0x0F, 0xF0, 0x00, 0x0F, 0xFF, 0xF0, 0x0F, 0xF0, 0x00, 0x0F, 0xFF, 0xF0, 0x0F, 0xF0, 0x00, 0x0F,
+    0xFF, 0xF0, 0x0F, 0xF0, 0x00, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF,
+    0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
+    0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF,
+    0xFF, 0xE0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x0F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xE0,
+    0x00, 0x00, 0x00, 0x00, 0x0F, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00,
+    0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
+    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
+    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
+};
+const gfx_bitmap_t gfx_icon_message_48 = { s_message_48, 48, 48 };
+
 static const uint8_t s_wx_clear_day_24[] = {
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x18, 0x00, 0x04, 0x18, 0x20, 0x0E,
     0x00, 0x60, 0x07, 0x00, 0xC0, 0x02, 0x3C, 0x80, 0x00, 0xFE, 0x00, 0x01, 0xC3, 0x00, 0x39, 0x81,
```


- [ ] **Step 2: Write the failing tests and the fixtures.**

`test/host/context_fixtures.h`:

```diff
--- a/test/host/context_fixtures.h
+++ b/test/host/context_fixtures.h
@@ -209,3 +209,8 @@ static inline void fixture_mqtt(ui_context_t *ctx)
     ctx->mqtt = &s_fix_mqtt;
     ctx->mqtt_keys = &s_fix_keys;
 }
+
+/* Home Assistant's longest message (spec §12.7): 96 bytes, in the language of the fixture. */
+#define FIX_MESSAGE_EN "Washing machine done. The dryer is free until 21:30, and the laundry room window is still open!!"
+#define FIX_MESSAGE_CS "Pračka dokončila praní. Sušička je volná do 21:30 a okno v prádelně je stále otevřené"
+
```


`test/host/dashboard_fixtures.h`:

```diff
--- a/test/host/dashboard_fixtures.h
+++ b/test/host/dashboard_fixtures.h
@@ -291,6 +291,16 @@ static inline bool fixture_dashboard(const char *name, ui_context_t *ctx, ui_pre
         }
         fixture_mqtt(ctx);
         ctx->lang = lang_get("cs");
+    } else if (strcmp(name, "home_mqtt_failed") == 0) { /* M7: the last MQTT session failed (spec §5.2) */
+        *preset = fixture_preset("home");
+        ctx->mqtt_failed = true;
+        ctx->wifi = UI_WIFI_ON;
+    } else if (strcmp(name, "message_fields") == 0) { /* ha.message, wrapped, in an L and an M slot */
+        *preset = fixture_preset("weather");
+        preset->slots[0] = UI_FIELD_HA_MESSAGE;
+        preset->slots[1] = UI_FIELD_HA_MESSAGE;
+        fixture_mqtt(ctx);
+        ha_store_set_message(&s_fix_mqtt, FIX_MESSAGE_EN, FIX_NOW - 300);
     } else if (strcmp(name, "grid_clock_12h") == 0) { /* a clock in a grid cell, 12-hour */
         *preset = fixture_preset("indoor");
         preset->slots[0] = UI_FIELD_TIME_CLOCK;
@@ -319,4 +329,4 @@ static const char *const k_dashboard_fixtures[] = { "home", "indoor", "weather",
                                                     "flights_failed", "flights_off", "split_weather",
                                                     "split_eight", "home_temp_main", "home_temp_main_cs",
                                                     "weather_frost_cs", "weather_hot_f", "mqtt_grid",
-                                                    "mqtt_home_cs" };
+                                                    "mqtt_home_cs", "home_mqtt_failed", "message_fields" };
```


`test/host/screen_fixtures.h`:

```diff
--- a/test/host/screen_fixtures.h
+++ b/test/host/screen_fixtures.h
@@ -185,6 +185,18 @@ static inline bool fixture_screen(const char *name, gfx_fb_t *fb)
         snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_PRESET), "Indoor");
         ui_draw_toast(fb, text);
         return true;
+    } else if (strncmp(name, "message", 7) == 0) { /* M7: Home Assistant's message over the dashboard */
+        ui_context_t ctx;
+        ui_preset_t preset;
+        bool inverted = strstr(name, "inverted") != NULL;
+        fixture_dashboard(inverted ? "home_inverted" : lang == lang_get("cs") ? "home_cs" : "home", &ctx, &preset);
+        fixture_mqtt(&ctx);
+        ctx.lang = lang;
+        ha_store_set_message(&s_fix_mqtt, strstr(name, "short") ? "Door open" :
+                             lang == lang_get("cs") ? FIX_MESSAGE_CS : FIX_MESSAGE_EN, FIX_NOW - 60);
+        ui_draw_dashboard(fb, &ctx, &preset);
+        ui_draw_message_banner(fb, &ctx);
+        return true;
     } else if (strncmp(name, "critical", 8) == 0) {
         ui_context_t ctx = fixture_context();
         ctx.lang = lang;
@@ -204,4 +216,5 @@ static const char *const k_screen_fixtures[] = { "menu_root_en", "menu_root_cs",
                                                  "menu_confirm_password_cs", "config_ap_en", "config_ap_url_cs",
                                                  "config_starting_en", "config_joining_en", "config_station_en",
                                                  "config_station_ap_cs", "config_station_back_en", "config_station_back_cs",
-                                                 "first_run_en", "first_run_invalid_cs" };
+                                                 "first_run_en", "first_run_invalid_cs", "message_short_en",
+                                                 "message_long_en", "message_long_cs", "message_inverted_en" };
```


`test/host/test_ui_fields.c`:

```diff
--- a/test/host/test_ui_fields.c
+++ b/test/host/test_ui_fields.c
@@ -361,6 +361,27 @@ static void test_unmapped_keys_are_empty_slots(void)
     TEST_ASSERT_EQUAL(UI_FIELD_NONE, resolve(FIX_MQTT(0)).field);
 }
 
+/* ha.message (spec §5.1, §12.7): the latest message, stale after 24 h whatever KEY did, gone once cleared. */
+static void test_the_message_field(void)
+{
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_HA_MESSAGE).state); /* no store */
+    fixture_mqtt(&s_ctx);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_HA_MESSAGE).state); /* no message */
+    ha_store_set_message(&s_fix_mqtt, "Washing machine done", FIX_NOW - 600);
+    ha_store_dismiss(&s_fix_mqtt); /* the banner's; the field still shows it */
+    ui_value_t v = resolve(UI_FIELD_HA_MESSAGE);
+    TEST_ASSERT_EQUAL(UI_FK_TEXT, v.kind);
+    TEST_ASSERT_EQUAL(UI_VALUE_FRESH, v.state);
+    TEST_ASSERT_EQUAL_STRING("Message", v.label);
+    TEST_ASSERT_EQUAL_STRING("Washing machine done", v.text);
+    s_ctx.now = FIX_NOW + 86400;
+    v = resolve(UI_FIELD_HA_MESSAGE);
+    TEST_ASSERT_EQUAL(UI_VALUE_STALE, v.state);
+    TEST_ASSERT_EQUAL_UINT32(86400 + 600, v.age_s);
+    ha_store_set_message(&s_fix_mqtt, "", FIX_NOW);
+    TEST_ASSERT_EQUAL(UI_VALUE_MISSING, resolve(UI_FIELD_HA_MESSAGE).state);
+}
+
 static void test_none_resolves_to_missing(void)
 {
     ui_value_t v = resolve(UI_FIELD_NONE);
@@ -393,5 +414,6 @@ int main(void)
     RUN_TEST(test_mqtt_fields_come_from_the_store);
     RUN_TEST(test_mqtt_values_go_stale_with_their_age);
     RUN_TEST(test_unmapped_keys_are_empty_slots);
+    RUN_TEST(test_the_message_field);
     return UNITY_END();
 }
```


`test/host/test_ui_widget_fit.c`:

```diff
--- a/test/host/test_ui_widget_fit.c
+++ b/test/host/test_ui_widget_fit.c
@@ -200,6 +200,11 @@ static ui_context_t split_context(int variant)
     if (variant == 5 || variant == 6) {
         fixture_forecast_shift(&s_fix_ds, variant == 5 ? 388 : -125);
     }
+    if (variant != 3) { /* the longest message, stale in the stale set */
+        fixture_mqtt(&ctx);
+        ha_store_set_message(&s_fix_mqtt, ctx.lang == lang_get("cs") ? FIX_MESSAGE_CS : FIX_MESSAGE_EN,
+                             variant == 2 ? FIX_NOW - 2 * 86400 : FIX_NOW - 60);
+    }
     return ctx;
 }
 
```


Run: `cmake --build build-host 2>&1 | grep -o "error: [^;]*" | sort | uniq -c`
Expected: the build stops on `use of undeclared identifier 'UI_FIELD_HA_MESSAGE'`, `no member named 'mqtt_failed' in 'ui_context_t'` and `call to undeclared function 'ui_draw_message_banner'`.

- [ ] **Step 3: The field, the banner and the mark.**

`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -199,6 +199,7 @@ typedef enum {
     LS_DIR_SW,
     LS_DIR_W,
     LS_DIR_NW,
+    LS_MESSAGE, /* ha.message: Home Assistant's message (M7, spec §12.7) */
     LS_COUNT,
 } lang_str_t;
 
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -207,6 +207,7 @@ const lang_t lang_en = {
         [LS_DIR_SW] = "SW",
         [LS_DIR_W] = "W",
         [LS_DIR_NW] = "NW",
+        [LS_MESSAGE] = "Message",
     },
     .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
     .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -247,6 +247,7 @@ const lang_t lang_cs = {
         [LS_DIR_SW] = "JZ",
         [LS_DIR_W] = "Z",
         [LS_DIR_NW] = "SZ",
+        [LS_MESSAGE] = "Zpráva",
     },
     .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
     .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
```


`components/ui/include/ui_fields.h`:

```diff
--- a/components/ui/include/ui_fields.h
+++ b/components/ui/include/ui_fields.h
@@ -68,6 +68,7 @@ typedef enum {
     UI_FIELD_POLLEN_RAGWEED,
     UI_FIELD_WX_RAIN2H, /* M6 (D27) */
     UI_FIELD_RAIN_MAP,  /* M6 (D23) */
+    UI_FIELD_HA_MESSAGE, /* M7 (D32): Home Assistant's message */
     UI_FIELD_COUNT,     /* the built-in fields; the MQTT fields follow */
 } ui_field_id_t;
 
@@ -142,6 +143,7 @@ typedef struct {
     const ui_radar_t *radar; /* M6: the radars' map, frames and settings; NULL for none */
     const ha_store_t *mqtt;  /* M7: the MQTT fields' values and the message; NULL for none */
     const ui_mqtt_keys_t *mqtt_keys; /* the keys of the presets drawn; NULL for none */
+    bool mqtt_failed;        /* the last MQTT session failed: the status bar's mark (spec §5.2) */
 } ui_context_t;
 
 typedef enum {
@@ -156,7 +158,7 @@ typedef struct {
     ui_value_state_t state;
     uint32_t age_s;    /* how old a stale value is */
     const char *label; /* from the language pack */
-    char text[48];     /* the value: "23.4", "20:48", "Friday 25 September", a name */
+    char text[HA_MESSAGE_LEN]; /* the value: "23.4", "20:48", "Friday 25 September", a name, the message */
     char unit[8];      /* "°C", "%", "d", or the AM/PM suffix of a time */
     char extra[24];    /* secondary text: the seconds, the medium date, the illumination, the voltage */
     char short_text[16]; /* a shorter form: the short phase name, or a number without its decimals */
```


`components/ui/include/ui_screens.h`:

```diff
--- a/components/ui/include/ui_screens.h
+++ b/components/ui/include/ui_screens.h
@@ -10,6 +10,9 @@
 
 /* A short message over whatever is on screen, for about 3 s: "Preset: Indoor". */
 void ui_draw_toast(gfx_fb_t *fb, const char *text);
+/* Home Assistant's message across the bottom of the dashboard (spec §12.7), in one line cut with an
+ * ellipsis, while the store says its banner shows; nothing otherwise. */
+void ui_draw_message_banner(gfx_fb_t *fb, const ui_context_t *ctx);
 /* The last screen before the battery gives out (spec §8): nothing else updates after it. */
 void ui_draw_critical(gfx_fb_t *fb, const ui_context_t *ctx);
 /* The first run (spec §5.5): what the buttons do, and the clock if the time is valid. */
```


`components/ui/ui_fields.c`:

```diff
--- a/components/ui/ui_fields.c
+++ b/components/ui/ui_fields.c
@@ -40,6 +40,7 @@ static const ui_field_info_t k_fields[UI_FIELD_COUNT] = {
     [UI_FIELD_POLLEN_RAGWEED] = { "pollen.ragweed", UI_FK_POLLEN, LS_POLLEN_RAGWEED, -1 },
     [UI_FIELD_WX_RAIN2H] = { "wx.rain2h", UI_FK_SERIES, LS_RAIN_2H, -1 },
     [UI_FIELD_RAIN_MAP] = { "rain.map", UI_FK_RAIN_MAP, LS_RAIN_MAP, -1 },
+    [UI_FIELD_HA_MESSAGE] = { "ha.message", UI_FK_TEXT, LS_MESSAGE, -1 },
 };
 
 const ui_field_info_t *ui_field_info(ui_field_id_t field)
@@ -226,6 +227,18 @@ void ui_mqtt_value(const ui_context_t *ctx, int i, ui_value_t *out)
     snprintf(out->unit, sizeof(out->unit), "%s", e->unit);
 }
 
+/* ha.message (spec §12.7): the latest message until it is replaced or cleared, stale after 24 h. */
+static void resolve_message(const ui_context_t *ctx, ui_value_t *out)
+{
+    ha_freshness_t fresh = ctx->mqtt != NULL ? ha_store_message_freshness(ctx->mqtt, ctx->now) : HA_MISSING;
+    if (fresh == HA_MISSING) {
+        return;
+    }
+    out->state = fresh == HA_STALE ? UI_VALUE_STALE : UI_VALUE_FRESH;
+    out->age_s = (uint32_t)ctx->now > ctx->mqtt->message_at ? (uint32_t)ctx->now - ctx->mqtt->message_at : 0;
+    snprintf(out->text, sizeof(out->text), "%s", ctx->mqtt->message);
+}
+
 /* mqtt.<key> (spec §12.5): the mapping that names the key; a key no mapping names, or no store, is no
  * field at all, so its slot stays empty. */
 static void resolve_mqtt(const ui_context_t *ctx, int k, ui_value_t *out)
@@ -255,6 +268,8 @@ void ui_resolve(const ui_context_t *ctx, ui_field_id_t field, ui_value_t *out)
     out->label = lang_str(ctx->lang, info->label);
     if (info->ds_field >= 0) {
         resolve_store(ctx, field, out);
+    } else if (field == UI_FIELD_HA_MESSAGE) {
+        resolve_message(ctx, out);
     } else if (!ui_resolve_forecast(ctx, field, out) && !ui_resolve_radar(ctx, field, out)) {
         resolve_clock(ctx, field, out);
     }
```


`components/ui/ui_widget.c`:

```diff
--- a/components/ui/ui_widget.c
+++ b/components/ui/ui_widget.c
@@ -69,6 +69,9 @@ static const gfx_bitmap_t *field_icon(ui_field_id_t field, int size)
     case UI_FIELD_DATE_HOLIDAY:
         s24 = &gfx_icon_celebration_24, s48 = &gfx_icon_celebration_48;
         break;
+    case UI_FIELD_HA_MESSAGE:
+        s24 = &gfx_icon_message_24, s48 = &gfx_icon_message_48;
+        break;
     case UI_FIELD_WX_NOW:
     case UI_FIELD_WX_TODAY:
     case UI_FIELD_WX_HOURLY:
@@ -189,7 +192,7 @@ void ui_split_two_lines(const gfx_font_t *font, const char *text, int max_w, cha
         gfx_text_ellipsize(font, text, max_w, line1, size);
         return;
     }
-    char first[96];
+    char first[128]; /* the message's 96 bytes too */
     snprintf(first, sizeof(first), "%s", text ? text : "");
     for (char *space = strrchr(first, ' '); space != NULL; space = strrchr(first, ' ')) {
         *space = '\0';
@@ -368,6 +371,59 @@ static void draw_small(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v)
     draw_group(fb, f, vf, &shown, value, x, r.y + (r.h + digit_height(vf)) / 2);
 }
 
+#define WRAP_LINES 8
+
+/* How much of `p` its next line takes: the longest run of whole words that fits `max_w`, a word wider than a
+ * line alone, or the rest on the last line. */
+static size_t wrap_take(const gfx_font_t *f, const char *p, int max_w, bool last, char *part, size_t part_size)
+{
+    if (last || gfx_text_width(f, p) <= max_w) {
+        return strlen(p);
+    }
+    size_t fit = 0;
+    for (size_t i = 1; p[i - 1] != '\0'; i++) {
+        if (p[i] == ' ' || p[i] == '\0') {
+            snprintf(part, part_size, "%.*s", (int)i, p);
+            if (gfx_text_width(f, part) > max_w) {
+                break;
+            }
+            fit = i;
+        }
+    }
+    return fit > 0 ? fit : strcspn(p, " ");
+}
+
+/* A text wider than the body over as many lines as its height holds, broken at spaces, centred; the last
+ * line takes the rest, cut with an ellipsis, as does a word wider than a line. Two passes, counting the lines
+ * and then drawing them, so no line is kept: the app task's stack is small. */
+static void draw_wrapped(gfx_fb_t *fb, const gfx_font_t *f, gfx_rect_t body, const char *text)
+{
+    int max_w = body.w - 12, max_lines = (body.h - 4) / f->line_height;
+    max_lines = max_lines < 1 ? 1 : max_lines > WRAP_LINES ? WRAP_LINES : max_lines;
+    char part[HA_MESSAGE_LEN], line[HA_MESSAGE_LEN + 4];
+    int top = body.y;
+    for (int pass = 0; pass < 2; pass++) {
+        const char *p = text;
+        int n = 0;
+        while (*p != '\0' && n < max_lines) {
+            while (*p == ' ') {
+                p++;
+            }
+            size_t take = wrap_take(f, p, max_w, n == max_lines - 1, part, sizeof(part));
+            if (pass == 1) {
+                snprintf(part, sizeof(part), "%.*s", (int)take, p);
+                gfx_text_ellipsize(f, part, max_w, line, sizeof(line));
+                gfx_text_in_rect(fb, f, (gfx_rect_t){ body.x, (int16_t)(top + n * f->line_height), body.w,
+                                                      (int16_t)f->line_height },
+                                 GFX_ALIGN_CENTER, line, GFX_BLACK);
+            }
+            p += take;
+            n++;
+        }
+        top = body.y + (body.h - n * f->line_height) / 2;
+    }
+}
+
 static void draw_labelled(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_value_t *v)
 {
     const ui_fonts_t *f = &k_fonts[size];
@@ -404,6 +460,10 @@ static void draw_labelled(gfx_fb_t *fb, gfx_rect_t r, ui_size_t size, const ui_v
                 value = v->extra; /* the medium form: "Fri 25 Sep" */
             }
         }
+        if (v->kind == UI_FK_TEXT && v->state != UI_VALUE_MISSING && gfx_text_width(tf, value) > body.w - 12) {
+            draw_wrapped(fb, tf, body, value); /* a message, or an MQTT text (M7) */
+            return;
+        }
         gfx_text_ellipsize(tf, value, body.w - 12, fit, sizeof(fit));
         gfx_text_in_rect(fb, tf, body, GFX_ALIGN_CENTER, fit, GFX_BLACK);
         return;
```


`components/ui/ui_screens.c`:

```diff
--- a/components/ui/ui_screens.c
+++ b/components/ui/ui_screens.c
@@ -20,6 +20,23 @@ void ui_draw_toast(gfx_fb_t *fb, const char *text)
     gfx_text_in_rect(fb, f, box, GFX_ALIGN_CENTER, fit, GFX_WHITE);
 }
 
+void ui_draw_message_banner(gfx_fb_t *fb, const ui_context_t *ctx)
+{
+    if (ctx->mqtt == NULL || !ha_store_banner(ctx->mqtt, ctx->now)) {
+        return;
+    }
+    const gfx_font_t *f = &gfx_font_sans_bold_16;
+    char line[HA_MESSAGE_LEN + 4];
+    gfx_text_ellipsize(f, ctx->mqtt->message, fb->width - 24, line, sizeof(line));
+    int h = 10 + f->line_height; /* one line: the slots above stay readable for the hours it may show */
+    gfx_rect_t box = { 0, (int16_t)(fb->height - h), fb->width, (int16_t)h };
+    gfx_reset_clip(fb);
+    /* a white rim keeps it apart from black content, such as an inverted preset */
+    gfx_fill_rect(fb, (gfx_rect_t){ 0, (int16_t)(box.y - 3), fb->width, 3 }, GFX_WHITE);
+    gfx_fill_rect(fb, box, GFX_BLACK);
+    gfx_text_in_rect(fb, f, box, GFX_ALIGN_CENTER, line, GFX_WHITE);
+}
+
 void ui_draw_critical(gfx_fb_t *fb, const ui_context_t *ctx)
 {
     gfx_reset_clip(fb);
```


`components/ui/ui_status.c`:

```diff
--- a/components/ui/ui_status.c
+++ b/components/ui/ui_status.c
@@ -6,7 +6,8 @@
 #include "ui_internal.h"
 
 /* Status bar (spec §5.2): "Set time" or a stale warning on the left, then a globe while a phone is
- * logged in to the web UI (D20), the sync state and in sync mode `always` the Wi-Fi state; an
+ * logged in to the web UI (D20), the sync state, in sync mode `always` the Wi-Fi state, and a failed MQTT
+ * session (M7); an
  * optional clock in the middle, the battery on the right with its level, voltage or days left as
  * the preset asks. */
 void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *preset, bool any_stale)
@@ -38,6 +39,10 @@ void ui_status_draw(gfx_fb_t *fb, const ui_context_t *ctx, const ui_preset_t *pr
     }
     if (ctx->wifi != UI_WIFI_NONE) { /* sync mode `always` (D19) */
         gfx_bitmap(fb, left, 2, ctx->wifi == UI_WIFI_ON ? &gfx_icon_wifi_16 : &gfx_icon_wifi_off_16, GFX_BLACK);
+        left += 20;
+    }
+    if (ctx->mqtt_failed) { /* M7 (D32): until an MQTT session succeeds */
+        gfx_bitmap(fb, left, 2, &gfx_icon_mqtt_off_16, GFX_BLACK);
     }
 
     if (preset->status_clock && ctx->time_valid) {
```


`main/app_menu.c`:

```diff
--- a/main/app_menu.c
+++ b/main/app_menu.c
@@ -200,7 +200,7 @@ static void build_model(void)
     if (bat.state == UI_VALUE_MISSING) {
         snprintf(s_info[0], sizeof(s_info[0]), "\xE2\x80\x94");
     } else {
-        snprintf(s_info[0], sizeof(s_info[0]), "%s %% \xC2\xB7 %s", bat.text, bat.extra);
+        snprintf(s_info[0], sizeof(s_info[0]), "%.8s %% \xC2\xB7 %s", bat.text, bat.extra); /* "87": 3 digits */
     }
     char sha[8];
     esp_app_get_elf_sha256(sha, sizeof(sha));
```


Run: `cmake --build build-host && for t in test_ui_fields test_ui_widget_fit test_lang_glyphs; do ./build-host/$t | tail -1; done`
Expected: `OK` three times (23 and 10 tests, then the glyphs).

- [ ] **Step 4: The goldens.** Copy them from the branch and draw them again; they must match byte for byte:

```bash
git checkout plan/m7 -- \
  test/host/golden/dash_home_mqtt_failed.pbm \
  test/host/golden/dash_message_fields.pbm \
  test/host/golden/screen_message_inverted_en.pbm \
  test/host/golden/screen_message_long_cs.pbm \
  test/host/golden/screen_message_long_en.pbm \
  test/host/golden/screen_message_short_en.pbm
```


```bash
build-host/render_dashboard home_mqtt_failed /tmp/a.pbm && cmp /tmp/a.pbm test/host/golden/dash_home_mqtt_failed.pbm
build-host/render_dashboard message_fields /tmp/b.pbm && cmp /tmp/b.pbm test/host/golden/dash_message_fields.pbm
for f in message_short_en message_long_en message_long_cs message_inverted_en; do
  build-host/render_screen $f /tmp/$f.pbm && cmp /tmp/$f.pbm test/host/golden/screen_$f.pbm && echo "$f same"
done
python3 tools/render.py
```

Expected: no `cmp` output, and four `same` lines. In `captures/render/`: `dash_home_mqtt_failed` has the broken link beside the Wi-Fi mark; `dash_message_fields` the 96-byte message wrapped in Weather's two large slots with the message icon; the banners one line at the bottom, the long ones cut with "…", the inverted preset's set apart by its white rim.

- [ ] **Step 5: Run the tests, and build.**

Run: `cmake --build build-host && ctest --test-dir build-host | tail -3; tools/idf.sh build 2>&1 | grep -c 'warning:'`
Expected: `100% tests passed, 0 tests failed out of 64`; `0`.

- [ ] **Step 6: Commit.**

```bash
git add assets/icons components/gfx components/locale components/ui main/app_menu.c test/host
git commit -m "feat(ui): Home Assistant's message, its banner and the MQTT mark (spec §5.2, §12.7)"
```

### Task 7: The MQTT client (`ha_mqtt`)

**Files:**
- Create: `components/ha_mqtt/include/ha_session.h`, `components/ha_mqtt/ha_session.c`, `components/ha_mqtt/include/ha_mqtt.h`, `components/ha_mqtt/ha_mqtt.c`, `test/host/test_ha_session.c`
- Modify: `components/ha_mqtt/CMakeLists.txt`, `test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `ha_fields_t` (Task 2); `ha_topic()`, `ha_cmd_parse()`, `ha_value_parse()`, `ha_value_t`, `HA_DISC_COUNT`, `HA_TOPIC_MAX`, `HA_PAYLOAD_MAX` (Task 3); ESP-IDF's `mqtt` component (esp-mqtt), NVS, `esp_timer`.
- Produces:
  - `ha_session.h` (pure C): `HA_QUIET_MS 1000`; `uint32_t ha_backoff_ms(int failures)`; `int ha_topics(const ha_fields_t *f, const char *out[HA_FIELDS_MAX])`; `int ha_topic_index(const char *const *topics, int count, const char *topic, size_t len)`; `bool ha_collect_over(int topics, int seen, int64_t now_ms, int64_t quiet_since_ms)`; `uint32_t ha_expected_s(bool always, uint32_t sync_expected_s, uint32_t longest_off_s)`;
  - `ha_mqtt.h` (device): `HA_DETAIL_LEN 24`, `HA_PASS_LEN 64`, `HA_STATE_MAX 512`; `ha_conn_t { char id[16]; char host[64]; uint16_t port; char user[64]; char password[HA_PASS_LEN]; bool discovery; }`; `ha_payloads_t { char state[HA_STATE_MAX]; uint32_t disc_hash; char topic[HA_DISC_COUNT][HA_TOPIC_MAX]; char payload[HA_DISC_COUNT][HA_PAYLOAD_MAX]; }`; `ha_hooks_t { void (*command)(ha_cmd_t, const char *, size_t); void (*value)(const char *key, const ha_value_t *v); void (*status)(void); bool (*payloads)(ha_payloads_t *out); }`; `ha_mqtt_status_t { bool keep, connected; char detail[HA_DETAIL_LEN]; bool test_running, test_done, test_ok; char test_detail[HA_DETAIL_LEN]; }`;
  - `esp_err_t ha_mqtt_init(const ha_hooks_t *hooks)` (once; after a failure, a later call makes what is missing; until it succeeds the other calls do nothing), `void ha_mqtt_set_fields(const ha_fields_t *f)`, `esp_err_t ha_mqtt_session(const ha_conn_t *c, int budget_ms, char *detail, size_t size)`, `void ha_mqtt_keep(const ha_conn_t *c)`, `void ha_mqtt_drop(void)`, `void ha_mqtt_publish_state(const char *json)`, `void ha_mqtt_publish_action(const char *payload)`, `void ha_mqtt_test(const ha_conn_t *c)`, `void ha_mqtt_forget_discovery(void)`, `void ha_mqtt_status(ha_mqtt_status_t *out)`;
  - NVS `sys/mqtt_disc`: the hash of the discovery configs that went out.

One esp-mqtt client, driven by a task of its own (stack 4 KB, priority 3, core 0), so that nothing the client does blocks the app task: esp-mqtt holds its API lock through a whole connect, up to the network timeout, so only this task calls the client's API. A sync's session (spec §9.3 step 6) connects with a persistent session (`disable_clean_session`: the broker keeps the commands queued for the sleeping device), subscribes to `cmd/#` at QoS 1 and to each mapped topic once at QoS 0, in chunks of eight, collects until every topic brought its retained value or a second passes without one, has the app build the state and the discovery configs on its own task, publishes the configs whose hash differs from NVS's and then the state, retained at QoS 1 with four in flight, and disconnects, all within its budget. A retained command is ignored (it would come back at every session), and so is a message longer than the client's 4 KB buffer, logged once. The configs go out again whenever the broker had no session of ours (CONNACK's session-present flag): a new broker, or one that lost its state, may have lost the retained configs too. Sync mode `always` keeps the client: esp-mqtt's own reconnect is off, and the task tries again after 10 s, the wait doubling to 5 min; after each connection it subscribes and publishes. A failure says why in a few words ("refused: login", "host not found", "timeout", "no broker", "socket error 113"). The esp-mqtt task parses values on a 6 KB stack, a payload nested past 16 levels unparsed (Task 3). Test connection connects with the given settings and leaves; with the client kept, it waits for that connection.

Two locks meet here. esp-mqtt's task holds the client's API lock while it hands over events, and `on_data()` takes `s_lock` (the mappings, the topics seen, the details); so nothing calls esp-mqtt, or a hook, while holding `s_lock`. `subscribe()` copies the filters under it and subscribes after, and a message's values are parsed under it into a buffer of esp-mqtt's task and handed to the hooks after; otherwise a subscription in sync mode `always` (a reconnect, new mappings) meeting a live value would stop both tasks, and the app's next status read with them. Each client has a generation, bumped when it opens and when it closes, so a closed client's late events (a `CONNECTED` during the stop, a `DISCONNECTED` queued behind it) change nothing.

- [ ] **Step 1: Write the failing tests.**

`test/host/test_ha_session.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/test/host/test_ha_session.c
@@ -0,0 +1,76 @@
+#include <string.h>
+
+#include "ha_session.h"
+#include "unity.h"
+
+void setUp(void) {}
+void tearDown(void) {}
+
+/* spec §12.9: a lost connection is tried again after 10 s, the wait doubling to at most 5 min. */
+static void test_reconnects_back_off_to_five_minutes(void)
+{
+    static const uint32_t k_waits[] = { 10000, 20000, 40000, 80000, 160000, 300000, 300000 };
+    for (int i = 0; i < (int)(sizeof(k_waits) / sizeof(k_waits[0])); i++) {
+        TEST_ASSERT_EQUAL_UINT32_MESSAGE(k_waits[i], ha_backoff_ms(i), "failures");
+    }
+    TEST_ASSERT_EQUAL_UINT32(300000, ha_backoff_ms(1000));
+}
+
+static ha_field_t mapping(const char *key, const char *topic)
+{
+    ha_field_t f = { .kind = HA_KIND_NUMBER };
+    strncpy(f.key, key, sizeof(f.key) - 1);
+    strncpy(f.topic, topic, sizeof(f.topic) - 1);
+    return f;
+}
+
+/* One subscription a topic, however many values it brings (Zigbee2MQTT's devices). */
+static void test_topics_are_subscribed_once(void)
+{
+    static ha_fields_t f;
+    f.count = 4;
+    f.field[0] = mapping("co2", "z2m/living_room");
+    f.field[1] = mapping("outdoor", "ha/statestream/sensor/outdoor/state");
+    f.field[2] = mapping("voc", "z2m/living_room");
+    f.field[3] = mapping("door", "z2m/door");
+    const char *topics[HA_FIELDS_MAX];
+    TEST_ASSERT_EQUAL_INT(3, ha_topics(&f, topics));
+    TEST_ASSERT_EQUAL_STRING("z2m/living_room", topics[0]);
+    TEST_ASSERT_EQUAL_STRING("ha/statestream/sensor/outdoor/state", topics[1]);
+    TEST_ASSERT_EQUAL_STRING("z2m/door", topics[2]);
+    TEST_ASSERT_EQUAL_INT(2, ha_topic_index(topics, 3, "z2m/door", 8));
+    TEST_ASSERT_EQUAL_INT(-1, ha_topic_index(topics, 3, "z2m/doors", 9));
+    TEST_ASSERT_EQUAL_INT(-1, ha_topic_index(topics, 3, "z2m/do", 6));
+    f.count = 0;
+    TEST_ASSERT_EQUAL_INT(0, ha_topics(&f, topics));
+}
+
+/* spec §9.3 step 6.3: collect until every mapped topic has arrived, or 1 s passes with none. */
+static void test_collecting_ends_with_every_topic_or_a_quiet_second(void)
+{
+    TEST_ASSERT_TRUE(ha_collect_over(3, 3, 5000, 4990));   /* all came */
+    TEST_ASSERT_FALSE(ha_collect_over(3, 2, 5000, 4500));  /* one missing, half a second quiet */
+    TEST_ASSERT_TRUE(ha_collect_over(3, 2, 5000, 4000));   /* a quiet second: retained messages came by now */
+    TEST_ASSERT_TRUE(ha_collect_over(0, 0, 5000, 4000));   /* no mappings: a quiet second for queued commands */
+    TEST_ASSERT_FALSE(ha_collect_over(0, 0, 5000, 4001));
+}
+
+/* spec §12.3, §9.3: HA's sensors expire by the sync's interval; in sync mode `always` by 10 min, and the
+ * longest span Wi-Fi is off besides, so a night or quiet hours don't mark them unavailable. */
+static void test_the_interval_sensors_expire_by(void)
+{
+    TEST_ASSERT_EQUAL_UINT32(86400, ha_expected_s(false, 86400, 0));
+    TEST_ASSERT_EQUAL_UINT32(0, ha_expected_s(false, 0, 7 * 3600)); /* manual: none */
+    TEST_ASSERT_EQUAL_UINT32(600, ha_expected_s(true, 3600, 0));
+    TEST_ASSERT_EQUAL_UINT32(600 + 7 * 3600, ha_expected_s(true, 7 * 3600, 7 * 3600));
+}
+
+int main(void)
+{
+    UNITY_BEGIN();
+    RUN_TEST(test_reconnects_back_off_to_five_minutes);
+    RUN_TEST(test_topics_are_subscribed_once);
+    RUN_TEST(test_collecting_ends_with_every_topic_or_a_quiet_second);
+    RUN_TEST(test_the_interval_sensors_expire_by);
+    return UNITY_END();
+}
```


`test/host/CMakeLists.txt`:

```diff
--- a/test/host/CMakeLists.txt
+++ b/test/host/CMakeLists.txt
@@ -176,7 +176,8 @@ target_link_libraries(storage_logic PRIVATE cjson util m)
 
 # ha_mqtt: MQTT and Home Assistant's mappings, payloads and store build on the host; the client does not.
 add_library(ha_mqtt_logic STATIC ${REPO_ROOT}/components/ha_mqtt/ha_fields.c
-            ${REPO_ROOT}/components/ha_mqtt/ha_payload.c ${REPO_ROOT}/components/ha_mqtt/ha_store.c)
+            ${REPO_ROOT}/components/ha_mqtt/ha_payload.c ${REPO_ROOT}/components/ha_mqtt/ha_store.c
+            ${REPO_ROOT}/components/ha_mqtt/ha_session.c)
 target_include_directories(ha_mqtt_logic PUBLIC ${REPO_ROOT}/components/ha_mqtt/include)
 target_compile_options(ha_mqtt_logic PRIVATE ${REFLBO_WARNINGS})
 target_link_libraries(ha_mqtt_logic PUBLIC util PRIVATE cjson m)
@@ -270,6 +271,7 @@ reflbo_host_test(test_storage_backup storage_logic)
 reflbo_host_test(test_ha_fields ha_mqtt_logic)
 reflbo_host_test(test_ha_payload ha_mqtt_logic cjson)
 reflbo_host_test(test_ha_store ha_mqtt_logic)
+reflbo_host_test(test_ha_session ha_mqtt_logic)
 target_compile_definitions(test_ha_payload PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ha")
 
 reflbo_host_test(test_test_pattern_golden gfx)
```


Run: `cmake -S test/host -B build-host -G Ninja 2>&1 | grep -m1 -A1 'CMake Error'`
Expected: `CMake Error at CMakeLists.txt:178 (add_library):`, then `Cannot find source file:` and the path of `components/ha_mqtt/ha_session.c`.

- [ ] **Step 2: The session's rules.**

`components/ha_mqtt/include/ha_session.h`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/include/ha_session.h
@@ -0,0 +1,28 @@
+#pragma once
+
+#include <stdbool.h>
+#include <stddef.h>
+#include <stdint.h>
+
+#include "ha_fields.h"
+
+/*
+ * The rules of an MQTT session (spec §9.3 step 6, §12.9) that don't need a client: the reconnects' back-off,
+ * the topics to subscribe, when collecting ends, and the interval Home Assistant's sensors expire by.
+ * Pure C, host-buildable.
+ */
+
+#define HA_QUIET_MS 1000 /* collecting ends after a second without a message */
+
+/* After `failures` failed connections in a row: 10 s, then twice as long each time, at most 5 min. */
+uint32_t ha_backoff_ms(int failures);
+/* The distinct topics the mappings read, in their order (several values may share one); returns how many. */
+int ha_topics(const ha_fields_t *f, const char *out[HA_FIELDS_MAX]);
+/* `topic` (its first `len` bytes) in that list, or -1. */
+int ha_topic_index(const char *const *topics, int count, const char *topic, size_t len);
+/* The collect phase is over: every one of `topics` brought a message, or HA_QUIET_MS passed since
+ * `quiet_since_ms` (the later of the subscriptions' confirmation and the last message). */
+bool ha_collect_over(int topics, int seen, int64_t now_ms, int64_t quiet_since_ms);
+/* What HA's sensors expire by (spec §12.3): the sync's expected interval, or in sync mode `always` 10 min
+ * and the longest span Wi-Fi is off (quiet hours, a night); 0, never, in manual mode. */
+uint32_t ha_expected_s(bool always, uint32_t sync_expected_s, uint32_t longest_off_s);
```


`components/ha_mqtt/ha_session.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/ha_session.c
@@ -0,0 +1,48 @@
+#include "ha_session.h"
+
+#include <string.h>
+
+#define BACKOFF_FIRST_MS 10000
+#define BACKOFF_MAX_MS 300000
+#define ALWAYS_EXPECTED_S 600 /* state goes out every 5 min in sync mode `always` (spec §9.3) */
+
+uint32_t ha_backoff_ms(int failures)
+{
+    uint32_t ms = BACKOFF_FIRST_MS;
+    for (int i = 0; i < failures && ms < BACKOFF_MAX_MS; i++) {
+        ms *= 2;
+    }
+    return ms < BACKOFF_MAX_MS ? ms : BACKOFF_MAX_MS;
+}
+
+int ha_topic_index(const char *const *topics, int count, const char *topic, size_t len)
+{
+    for (int i = 0; i < count; i++) {
+        if (strlen(topics[i]) == len && memcmp(topics[i], topic, len) == 0) {
+            return i;
+        }
+    }
+    return -1;
+}
+
+int ha_topics(const ha_fields_t *f, const char *out[HA_FIELDS_MAX])
+{
+    int n = 0;
+    for (int i = 0; i < f->count && i < HA_FIELDS_MAX; i++) {
+        const char *t = f->field[i].topic;
+        if (ha_topic_index(out, n, t, strlen(t)) < 0) {
+            out[n++] = t;
+        }
+    }
+    return n;
+}
+
+bool ha_collect_over(int topics, int seen, int64_t now_ms, int64_t quiet_since_ms)
+{
+    return (topics > 0 && seen >= topics) || now_ms - quiet_since_ms >= HA_QUIET_MS;
+}
+
+uint32_t ha_expected_s(bool always, uint32_t sync_expected_s, uint32_t longest_off_s)
+{
+    return always ? ALWAYS_EXPECTED_S + longest_off_s : sync_expected_s;
+}
```


Run: `cmake -S test/host -B build-host -G Ninja >/dev/null && cmake --build build-host --target test_ha_session && ./build-host/test_ha_session | tail -1`
Expected: `OK` (4 tests).

- [ ] **Step 3: The client.** Device-only: the board checks it in Task 11, and a broker once the owner has one (spec §12.10).

`components/ha_mqtt/CMakeLists.txt`:

```diff
--- a/components/ha_mqtt/CMakeLists.txt
+++ b/components/ha_mqtt/CMakeLists.txt
@@ -1,6 +1,7 @@
 # MQTT and Home Assistant (spec §12, D32). ha_fields.c (the field mappings), ha_payload.c (topics and
-# payloads) and ha_store.c (what the dashboard shows) are pure C and also build on the host.
-idf_component_register(SRCS "ha_fields.c" "ha_payload.c" "ha_store.c"
+# payloads), ha_store.c (what the dashboard shows) and ha_session.c (a session's rules) are pure C and also
+# build on the host; ha_mqtt.c is the client.
+idf_component_register(SRCS "ha_fields.c" "ha_payload.c" "ha_store.c" "ha_session.c" "ha_mqtt.c"
                        INCLUDE_DIRS "include"
-                       REQUIRES util
-                       PRIV_REQUIRES json)
+                       REQUIRES util esp_common
+                       PRIV_REQUIRES json mqtt nvs_flash esp_timer esp-tls)
```


`components/ha_mqtt/include/ha_mqtt.h`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/include/ha_mqtt.h
@@ -0,0 +1,76 @@
+#pragma once
+
+#include <stdbool.h>
+#include <stddef.h>
+#include <stdint.h>
+
+#include "esp_err.h"
+#include "ha_fields.h"
+#include "ha_payload.h"
+
+/*
+ * The MQTT client (spec §12.9, D32): one esp-mqtt client, driven from a task of its own. A sync runs a
+ * session through it (ha_mqtt_session(), which blocks the sync's task); sync mode `always` keeps it
+ * connected (ha_mqtt_keep()). Commands and the mapped topics' values go to the app through hooks; the state
+ * and the discovery configs come from the app, which builds them on its own task. The commands' topic is
+ * subscribed at QoS 1, so the broker keeps them for the sleeping device in its persistent session; the mapped
+ * topics at QoS 0, so it keeps no backlog of readings, and each subscription brings their retained value.
+ * Device only.
+ */
+
+#define HA_DETAIL_LEN 24 /* why a session failed: "no broker", "refused: login" */
+#define HA_PASS_LEN 64
+#define HA_STATE_MAX 512
+
+typedef struct {
+    char id[16]; /* reflbo-XXXX: the client id, and the node of every topic */
+    char host[64];
+    uint16_t port;
+    char user[64];
+    char password[HA_PASS_LEN];
+    bool discovery;
+} ha_conn_t;
+
+/* What a session publishes; the app fills it on its task. */
+typedef struct {
+    char state[HA_STATE_MAX];
+    uint32_t disc_hash; /* ha_disc_hash() */
+    char topic[HA_DISC_COUNT][HA_TOPIC_MAX];
+    char payload[HA_DISC_COUNT][HA_PAYLOAD_MAX];
+} ha_payloads_t;
+
+typedef struct {
+    /* On the client's tasks: they hand over to the app task and return at once. */
+    void (*command)(ha_cmd_t cmd, const char *payload, size_t len);
+    void (*value)(const char *key, const ha_value_t *v);
+    void (*status)(void); /* the connection came or went */
+    /* Fills `out`; the client's task waits while the app task builds it. False if it couldn't. */
+    bool (*payloads)(ha_payloads_t *out);
+} ha_hooks_t;
+
+typedef struct {
+    bool keep;      /* sync mode `always` keeps the client */
+    bool connected;
+    char detail[HA_DETAIL_LEN]; /* why the last connection failed */
+    bool test_running, test_done, test_ok;
+    char test_detail[HA_DETAIL_LEN];
+} ha_mqtt_status_t;
+
+/* Starts the client's task; once. */
+esp_err_t ha_mqtt_init(const ha_hooks_t *hooks);
+/* The mappings changed (a cold boot, the MQTT page, a restore): a kept connection subscribes again. */
+void ha_mqtt_set_fields(const ha_fields_t *f);
+/* A sync's step (spec §9.3 step 6): connect, subscribe, collect, publish the state and any discovery
+ * configs whose hash changed, and disconnect, within `budget_ms`; with the client kept, publish on it. ESP_OK,
+ * or ESP_FAIL with why in `detail`. */
+esp_err_t ha_mqtt_session(const ha_conn_t *c, int budget_ms, char *detail, size_t size);
+/* Sync mode `always` (spec §12.9): connected with `c`, again after 10 s doubling to 5 min when it drops. */
+void ha_mqtt_keep(const ha_conn_t *c);
+void ha_mqtt_drop(void);
+void ha_mqtt_publish_state(const char *json);     /* while connected: retained, QoS 1 */
+void ha_mqtt_publish_action(const char *payload); /* while connected: reflbo/<id>/action, QoS 0 (spec §12.8) */
+/* Test connection (spec §10.3): connects with `c` and leaves; ha_mqtt_status() reports how it went. */
+void ha_mqtt_test(const ha_conn_t *c);
+/* Discovery was turned off: the next time it is on, the configs go out again (NVS sys/mqtt_disc). */
+void ha_mqtt_forget_discovery(void);
+void ha_mqtt_status(ha_mqtt_status_t *out);
```


`components/ha_mqtt/ha_mqtt.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/components/ha_mqtt/ha_mqtt.c
@@ -0,0 +1,673 @@
+#include "ha_mqtt.h"
+
+#include <errno.h>
+#include <stdatomic.h>
+#include <stdint.h>
+#include <stdio.h>
+#include <stdlib.h>
+#include <string.h>
+
+#include "esp_attr.h"
+#include "esp_log.h"
+#include "esp_timer.h"
+#include "esp_tls_errors.h"
+#include "freertos/FreeRTOS.h"
+#include "freertos/event_groups.h"
+#include "freertos/queue.h"
+#include "freertos/semphr.h"
+#include "freertos/task.h"
+#include "ha_session.h"
+#include "mqtt_client.h"
+#include "nvs.h"
+
+static const char *TAG = "ha_mqtt";
+
+#define TASK_STACK 4096
+#define TASK_PRIORITY 3 /* below netmgr (4) and the app (5), as the sync's task */
+#define QUEUE_DEPTH 8
+#define MQTT_TASK_STACK 6144 /* the values' JSON parses on it */
+#define MQTT_TASK_PRIORITY 3
+#define MQTT_BUFFER 4096 /* a mapped payload longer than this is ignored */
+#define KEEPALIVE_S 60
+#define CONNECT_MAX_MS 10000
+#define STEP_MS 50
+#define ACKS_AHEAD 4 /* QoS 1 messages in flight at once */
+#define SUBSCRIBE_CHUNK 8
+#define NVS_KEY_DISC "mqtt_disc" /* in `sys`: the hash of the discovery configs that went out (spec §12.3) */
+
+#define BIT_CONNECTED BIT0
+#define BIT_DOWN BIT1
+
+typedef enum {
+    REQ_SESSION, REQ_KEEP, REQ_DROP, REQ_STATE, REQ_ACTION, REQ_TEST, REQ_FIELDS, REQ_UP, REQ_DOWN
+} req_type_t;
+
+typedef struct {
+    req_type_t type;
+    uint32_t gen; /* REQ_UP, REQ_DOWN: the generation of the client whose event it is */
+    ha_conn_t *conn; /* a copy, freed by the task */
+    char *text;
+    int budget_ms;
+} req_t;
+
+static ha_hooks_t s_hooks;
+static bool s_started;
+static QueueHandle_t s_queue;
+/* The mappings, the topics seen and the details. esp-mqtt's task holds the client's API lock while it hands
+ * us events, and on_data() takes s_lock: so nothing calls esp-mqtt, or a hook, with s_lock held. */
+static SemaphoreHandle_t s_lock;
+static EventGroupHandle_t s_bits;
+static esp_mqtt_client_handle_t s_client; /* the task's */
+static volatile uint32_t s_gen;            /* the client's generation: a closed client's late events are ignored */
+static ha_conn_t s_conn;                   /* what s_client connects with */
+static volatile bool s_connected, s_keep;
+static volatile bool s_session_present;    /* the broker kept our session: it has seen the discovery configs */
+static atomic_int s_subacks, s_pubacks;    /* awaited */
+static int s_failures;
+static int64_t s_retry_at_ms;
+static char s_detail[HA_DETAIL_LEN];
+EXT_RAM_BSS_ATTR static ha_fields_t s_fields;
+static const char *s_topics[HA_FIELDS_MAX];
+static int s_topic_count;
+EXT_RAM_BSS_ATTR static char s_filters[HA_FIELDS_MAX + 1][HA_TOPIC_LEN]; /* the task's copy, to subscribe */
+EXT_RAM_BSS_ATTR static struct {
+    char key[HA_KEY_LEN];
+    ha_value_t v;
+} s_got[HA_FIELDS_MAX]; /* esp-mqtt's task: a message's values, handed over once s_lock is free */
+static uint32_t s_seen;          /* a bit per topic: a message came this session */
+static int64_t s_last_msg_ms;
+EXT_RAM_BSS_ATTR static ha_payloads_t s_payloads;
+static struct {
+    SemaphoreHandle_t done;
+    esp_err_t result;
+    char detail[HA_DETAIL_LEN];
+} s_session;
+static struct {
+    volatile bool running, done, ok;
+    char detail[HA_DETAIL_LEN];
+} s_test;
+
+static int64_t now_ms(void)
+{
+    return esp_timer_get_time() / 1000;
+}
+
+static void set_detail(char *to, const char *text)
+{
+    xSemaphoreTake(s_lock, portMAX_DELAY);
+    snprintf(to, HA_DETAIL_LEN, "%s", text);
+    xSemaphoreGive(s_lock);
+}
+
+static bool post(const req_t *r)
+{
+    if (!s_started || xQueueSend(s_queue, r, pdMS_TO_TICKS(100)) != pdTRUE) {
+        ESP_LOGW(TAG, "%s: request %d dropped", s_started ? "queue full" : "not started", r->type);
+        free(r->conn);
+        free(r->text);
+        return false;
+    }
+    return true;
+}
+
+/* Why a connection failed, in a few words (spec §12.9). */
+static void error_detail(const esp_mqtt_error_codes_t *e, char *out, size_t size)
+{
+    if (e->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
+        esp_mqtt_connect_return_code_t c = e->connect_return_code;
+        snprintf(out, size, "%s", c == MQTT_CONNECTION_REFUSE_BAD_USERNAME || c == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED
+                                      ? "refused: login"
+                                  : c == MQTT_CONNECTION_REFUSE_ID_REJECTED ? "refused: client id"
+                                                                            : "refused");
+    } else if (e->esp_tls_last_esp_err == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) {
+        snprintf(out, size, "host not found");
+    } else if (e->esp_tls_last_esp_err == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) {
+        snprintf(out, size, "timeout");
+    } else if (e->esp_transport_sock_errno == ECONNREFUSED) {
+        snprintf(out, size, "no broker");
+    } else if (e->esp_transport_sock_errno != 0) {
+        snprintf(out, size, "socket error %d", e->esp_transport_sock_errno);
+    } else {
+        snprintf(out, size, "no connection");
+    }
+}
+
+/* A message: a command, or a mapped topic's value for each field that reads it. */
+static void on_data(esp_mqtt_event_handle_t e)
+{
+    if (e->current_data_offset != 0 || e->data_len != e->total_data_len) {
+        if (e->current_data_offset == 0) { /* its other pieces come without the topic */
+            ESP_LOGW(TAG, "%.*s: %d bytes is too long; ignored", e->topic_len, e->topic, e->total_data_len);
+        }
+        return;
+    }
+    ha_cmd_t cmd = ha_cmd_parse(s_conn.id, e->topic, (size_t)e->topic_len);
+    if (cmd != HA_CMD_NONE) {
+        if (e->retain) { /* commands aren't retained (spec §12.2): one would come back at every session */
+            ESP_LOGW(TAG, "a retained command on %.*s: ignored", e->topic_len, e->topic);
+        } else {
+            s_hooks.command(cmd, e->data, (size_t)e->data_len);
+        }
+        return;
+    }
+    int got = 0;
+    xSemaphoreTake(s_lock, portMAX_DELAY);
+    int t = ha_topic_index(s_topics, s_topic_count, e->topic, (size_t)e->topic_len);
+    if (t >= 0) {
+        s_seen |= 1u << t;
+        s_last_msg_ms = now_ms();
+        for (int i = 0; i < s_fields.count && got < HA_FIELDS_MAX; i++) {
+            if (strcmp(s_fields.field[i].topic, s_topics[t]) == 0 &&
+                ha_value_parse(&s_fields.field[i], e->data, (size_t)e->data_len, &s_got[got].v)) {
+                snprintf(s_got[got++].key, HA_KEY_LEN, "%s", s_fields.field[i].key);
+            }
+        }
+    }
+    xSemaphoreGive(s_lock);
+    for (int i = 0; i < got; i++) {
+        s_hooks.value(s_got[i].key, &s_got[i].v);
+    }
+}
+
+/* On esp-mqtt's task. */
+static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
+{
+    (void)base;
+    esp_mqtt_event_handle_t e = data;
+    uint32_t gen = (uint32_t)(uintptr_t)arg;
+    if (gen != s_gen) {
+        return; /* a client being closed: its news is no longer ours */
+    }
+    switch ((esp_mqtt_event_id_t)id) {
+    case MQTT_EVENT_CONNECTED:
+        s_session_present = e->session_present;
+        s_connected = true;
+        xEventGroupSetBits(s_bits, BIT_CONNECTED);
+        post(&(req_t){ .type = REQ_UP, .gen = gen });
+        break;
+    case MQTT_EVENT_DISCONNECTED:
+        s_connected = false;
+        xEventGroupSetBits(s_bits, BIT_DOWN);
+        post(&(req_t){ .type = REQ_DOWN, .gen = gen });
+        break;
+    case MQTT_EVENT_ERROR:
+        if (e->error_handle != NULL && e->error_handle->error_type != MQTT_ERROR_TYPE_NONE) {
+            char detail[HA_DETAIL_LEN];
+            error_detail(e->error_handle, detail, sizeof(detail));
+            set_detail(s_detail, detail);
+            xEventGroupSetBits(s_bits, BIT_DOWN);
+        }
+        break;
+    case MQTT_EVENT_SUBSCRIBED:
+        atomic_fetch_sub(&s_subacks, 1);
+        break;
+    case MQTT_EVENT_PUBLISHED:
+        atomic_fetch_sub(&s_pubacks, 1);
+        break;
+    case MQTT_EVENT_DATA:
+        on_data(e);
+        break;
+    default:
+        break;
+    }
+}
+
+static esp_mqtt_client_handle_t open_client(const ha_conn_t *c, int timeout_ms)
+{
+    const esp_mqtt_client_config_t cfg = {
+        .broker.address = { .hostname = c->host, .port = c->port, .transport = MQTT_TRANSPORT_OVER_TCP },
+        .credentials = { .client_id = c->id, .username = c->user[0] ? c->user : NULL,
+                         .authentication.password = c->password[0] ? c->password : NULL },
+        .session = { .disable_clean_session = true, .keepalive = KEEPALIVE_S }, /* commands wait (spec §12.4) */
+        .network = { .disable_auto_reconnect = true, .timeout_ms = timeout_ms },
+        .task = { .priority = MQTT_TASK_PRIORITY, .stack_size = MQTT_TASK_STACK },
+        .buffer = { .size = MQTT_BUFFER },
+    };
+    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&cfg);
+    if (client == NULL) {
+        set_detail(s_detail, "no memory");
+        return NULL;
+    }
+    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, on_event, (void *)(uintptr_t)++s_gen);
+    xEventGroupClearBits(s_bits, BIT_CONNECTED | BIT_DOWN);
+    set_detail(s_detail, "");
+    s_connected = false;
+    if (esp_mqtt_client_start(client) != ESP_OK) {
+        esp_mqtt_client_destroy(client);
+        set_detail(s_detail, "no client");
+        return NULL;
+    }
+    return client;
+}
+
+static void close_client(void)
+{
+    s_gen++; /* its events from now on are ignored, the CONNECTED of a connect that ends during the stop too */
+    if (s_client != NULL) {
+        esp_mqtt_client_stop(s_client); /* a DISCONNECT first: the broker keeps the session (spec §12.4) */
+        esp_mqtt_client_destroy(s_client);
+        s_client = NULL;
+    }
+    s_connected = false;
+}
+
+/* Waits for the connection; false with the reason in s_detail. */
+static bool wait_connected(int64_t end_ms)
+{
+    int64_t left = end_ms - now_ms();
+    EventBits_t bits = xEventGroupWaitBits(s_bits, BIT_CONNECTED | BIT_DOWN, pdFALSE, pdFALSE,
+                                           left > 0 ? pdMS_TO_TICKS(left) : 0);
+    if (bits & BIT_CONNECTED && s_connected) {
+        return true;
+    }
+    xSemaphoreTake(s_lock, portMAX_DELAY);
+    if (s_detail[0] == '\0') {
+        snprintf(s_detail, sizeof(s_detail), "%s", bits & BIT_DOWN ? "no connection" : "timeout");
+    }
+    xSemaphoreGive(s_lock);
+    return false;
+}
+
+static void wait_acks(atomic_int *acks, int at_most, int64_t end_ms)
+{
+    while (atomic_load(acks) > at_most && s_connected && now_ms() < end_ms) {
+        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
+    }
+}
+
+/* cmd/# at QoS 1 and the mapped topics at QoS 0, in chunks, then their acknowledgements. The filters are
+ * copied under s_lock and subscribed without it (see s_lock). */
+static bool subscribe(int64_t end_ms)
+{
+    ha_topic(s_filters[0], HA_TOPIC_LEN, s_conn.id, "cmd/#");
+    xSemaphoreTake(s_lock, portMAX_DELAY);
+    s_topic_count = ha_topics(&s_fields, s_topics);
+    s_seen = 0;
+    for (int i = 0; i < s_topic_count; i++) {
+        snprintf(s_filters[i + 1], HA_TOPIC_LEN, "%s", s_topics[i]);
+    }
+    int total = s_topic_count + 1;
+    xSemaphoreGive(s_lock);
+    esp_mqtt_topic_t list[SUBSCRIBE_CHUNK];
+    bool ok = true;
+    for (int at = 0; ok && at < total; at += SUBSCRIBE_CHUNK) {
+        int n = 0;
+        for (int i = at; i < total && n < SUBSCRIBE_CHUNK; i++, n++) {
+            list[n] = (esp_mqtt_topic_t){ .filter = s_filters[i], .qos = i == 0 ? 1 : 0 };
+        }
+        atomic_fetch_add(&s_subacks, 1);
+        ok = esp_mqtt_client_subscribe_multiple(s_client, list, n) >= 0;
+        if (!ok) {
+            atomic_fetch_sub(&s_subacks, 1);
+        }
+    }
+    wait_acks(&s_subacks, 0, end_ms);
+    ok = ok && atomic_load(&s_subacks) <= 0;
+    atomic_store(&s_subacks, 0);
+    if (!ok) {
+        set_detail(s_detail, "subscribe failed");
+    }
+    return ok;
+}
+
+/* spec §9.3 step 6.3: until every mapped topic brought its retained value, or a quiet second. */
+static void collect(int64_t end_ms)
+{
+    xSemaphoreTake(s_lock, portMAX_DELAY);
+    s_last_msg_ms = now_ms();
+    xSemaphoreGive(s_lock);
+    for (;;) {
+        xSemaphoreTake(s_lock, portMAX_DELAY);
+        int seen = __builtin_popcount(s_seen);
+        bool over = ha_collect_over(s_topic_count, seen, now_ms(), s_last_msg_ms);
+        xSemaphoreGive(s_lock);
+        if (over || now_ms() >= end_ms) {
+            ESP_LOGI(TAG, "%d of %d topics came", seen, s_topic_count);
+            return;
+        }
+        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
+    }
+}
+
+static uint32_t disc_hash_saved(void)
+{
+    nvs_handle_t nvs;
+    uint32_t hash = 0;
+    if (nvs_open("sys", NVS_READONLY, &nvs) == ESP_OK) {
+        nvs_get_u32(nvs, NVS_KEY_DISC, &hash);
+        nvs_close(nvs);
+    }
+    return hash;
+}
+
+static void disc_hash_save(uint32_t hash)
+{
+    nvs_handle_t nvs;
+    if (nvs_open("sys", NVS_READWRITE, &nvs) != ESP_OK) {
+        return;
+    }
+    if ((hash != 0 ? nvs_set_u32(nvs, NVS_KEY_DISC, hash) : nvs_erase_key(nvs, NVS_KEY_DISC)) == ESP_OK) {
+        nvs_commit(nvs);
+    }
+    nvs_close(nvs);
+}
+
+static bool publish_qos1(const char *topic, const char *payload, int64_t end_ms)
+{
+    wait_acks(&s_pubacks, ACKS_AHEAD - 1, end_ms);
+    atomic_fetch_add(&s_pubacks, 1);
+    if (esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1) < 0) {
+        atomic_fetch_sub(&s_pubacks, 1);
+        return false;
+    }
+    return true;
+}
+
+/* The state, and the discovery configs whose hash changed (spec §12.3), retained at QoS 1. */
+static bool publish_payloads(int64_t end_ms)
+{
+    if (!s_hooks.payloads(&s_payloads)) {
+        set_detail(s_detail, "app busy");
+        return false;
+    }
+    atomic_store(&s_pubacks, 0);
+    /* again when they changed, or when the broker has no session of ours: a new broker, or one that lost its
+     * state, may have lost the retained configs too */
+    bool disc = s_conn.discovery && (!s_session_present || s_payloads.disc_hash != disc_hash_saved());
+    bool ok = true;
+    for (int i = 0; disc && ok && i < HA_DISC_COUNT; i++) {
+        if (s_payloads.topic[i][0] != '\0') { /* the app leaves out a config that didn't fit */
+            ok = publish_qos1(s_payloads.topic[i], s_payloads.payload[i], end_ms);
+        }
+    }
+    char topic[HA_TOPIC_MAX];
+    ha_topic(topic, sizeof(topic), s_conn.id, "state");
+    ok = ok && publish_qos1(topic, s_payloads.state, end_ms);
+    wait_acks(&s_pubacks, 0, end_ms);
+    ok = ok && atomic_load(&s_pubacks) <= 0;
+    atomic_store(&s_pubacks, 0);
+    if (!ok) {
+        set_detail(s_detail, s_connected ? "publish timed out" : "connection lost");
+    } else if (disc) {
+        disc_hash_save(s_payloads.disc_hash);
+        ESP_LOGI(TAG, "discovery published");
+    }
+    return ok;
+}
+
+static void run_session(const ha_conn_t *c, int budget_ms)
+{
+    int64_t end = now_ms() + budget_ms;
+    bool ok;
+    if (s_keep) { /* sync mode `always`: the kept client (spec §12.9) */
+        ok = s_connected && publish_payloads(end);
+        xSemaphoreTake(s_lock, portMAX_DELAY);
+        if (!s_connected && s_detail[0] == '\0') {
+            snprintf(s_detail, sizeof(s_detail), "not connected");
+        }
+        xSemaphoreGive(s_lock);
+    } else {
+        s_conn = *c;
+        s_client = open_client(c, budget_ms < CONNECT_MAX_MS ? budget_ms : CONNECT_MAX_MS);
+        ok = s_client != NULL && wait_connected(end) && subscribe(end);
+        if (ok) {
+            collect(end);
+            ok = publish_payloads(end);
+        }
+        close_client();
+    }
+    s_session.result = ok ? ESP_OK : ESP_FAIL;
+    xSemaphoreTake(s_lock, portMAX_DELAY);
+    snprintf(s_session.detail, sizeof(s_session.detail), "%s", ok ? "" : s_detail);
+    xSemaphoreGive(s_lock);
+    ESP_LOGI(TAG, "session %s%s", ok ? "done" : "failed: ", ok ? "" : s_session.detail);
+}
+
+static void keep(const ha_conn_t *c)
+{
+    close_client();
+    s_keep = true;
+    s_failures = 0;
+    s_retry_at_ms = 0;
+    s_conn = *c;
+    s_client = open_client(c, CONNECT_MAX_MS);
+    if (s_client == NULL) {
+        s_retry_at_ms = now_ms() + ha_backoff_ms(s_failures++);
+    }
+}
+
+static void test(const ha_conn_t *c)
+{
+    bool ok;
+    if (s_keep) {
+        ok = wait_connected(now_ms() + CONNECT_MAX_MS); /* the kept client is the test, maybe still connecting */
+    } else {
+        ha_conn_t saved = s_conn;
+        s_conn = *c;
+        s_client = open_client(c, CONNECT_MAX_MS);
+        ok = s_client != NULL && wait_connected(now_ms() + CONNECT_MAX_MS);
+        close_client();
+        s_conn = saved;
+    }
+    xSemaphoreTake(s_lock, portMAX_DELAY);
+    snprintf(s_test.detail, sizeof(s_test.detail), "%s", ok ? "" : s_detail[0] ? s_detail : "no connection");
+    xSemaphoreGive(s_lock);
+    s_test.ok = ok;
+    s_test.done = true;
+    s_test.running = false;
+    ESP_LOGI(TAG, "test %s%s", ok ? "connected" : "failed: ", ok ? "" : s_test.detail);
+}
+
+static void handle(req_t *r)
+{
+    switch (r->type) {
+    case REQ_SESSION:
+        run_session(r->conn, r->budget_ms);
+        xSemaphoreGive(s_session.done);
+        break;
+    case REQ_KEEP:
+        keep(r->conn);
+        break;
+    case REQ_DROP:
+        s_keep = false;
+        s_retry_at_ms = 0;
+        close_client();
+        s_hooks.status();
+        break;
+    case REQ_STATE:
+    case REQ_ACTION:
+        if (s_connected && s_client != NULL) {
+            char topic[HA_TOPIC_MAX];
+            ha_topic(topic, sizeof(topic), s_conn.id, r->type == REQ_STATE ? "state" : "action");
+            esp_mqtt_client_publish(s_client, topic, r->text, 0, r->type == REQ_STATE ? 1 : 0, r->type == REQ_STATE);
+        }
+        break;
+    case REQ_TEST:
+        test(r->conn);
+        break;
+    case REQ_FIELDS:
+        if (s_keep && s_connected) {
+            subscribe(now_ms() + CONNECT_MAX_MS);
+        }
+        break;
+    case REQ_UP:
+        if (r->gen == s_gen && s_keep) { /* spec §12.9: subscriptions, then the state and any discovery */
+            s_failures = 0;
+            int64_t end = now_ms() + CONNECT_MAX_MS;
+            if (subscribe(end)) {
+                publish_payloads(end);
+            }
+            s_hooks.status();
+        }
+        break;
+    case REQ_DOWN:
+        if (r->gen == s_gen && s_keep) {
+            s_retry_at_ms = now_ms() + ha_backoff_ms(s_failures);
+            char why[HA_DETAIL_LEN];
+            xSemaphoreTake(s_lock, portMAX_DELAY);
+            snprintf(why, sizeof(why), "%s", s_detail);
+            xSemaphoreGive(s_lock);
+            ESP_LOGW(TAG, "connection lost (%s); again in %u s", why, (unsigned)(ha_backoff_ms(s_failures) / 1000));
+            s_failures++;
+            s_hooks.status();
+        }
+        break;
+    }
+    free(r->conn);
+    free(r->text);
+}
+
+static void task(void *arg)
+{
+    (void)arg;
+    for (;;) {
+        TickType_t wait = portMAX_DELAY;
+        if (s_keep && !s_connected && s_retry_at_ms != 0) {
+            int64_t left = s_retry_at_ms - now_ms();
+            wait = left > 0 ? pdMS_TO_TICKS(left) + 1 : 0;
+        }
+        req_t r;
+        if (xQueueReceive(s_queue, &r, wait) == pdTRUE) {
+            handle(&r);
+        } else if (s_keep && !s_connected && s_retry_at_ms != 0 && now_ms() >= s_retry_at_ms) {
+            s_retry_at_ms = 0;
+            if (s_client == NULL) {
+                keep(&s_conn);
+            } else if (esp_mqtt_client_reconnect(s_client) != ESP_OK) {
+                s_retry_at_ms = now_ms() + ha_backoff_ms(s_failures++);
+            }
+        }
+    }
+}
+
+esp_err_t ha_mqtt_init(const ha_hooks_t *hooks)
+{
+    if (s_started) {
+        return ESP_OK;
+    }
+    s_hooks = *hooks;
+    /* each made once: after a failure, a later call makes what is still missing */
+    s_lock = s_lock != NULL ? s_lock : xSemaphoreCreateMutex();
+    s_bits = s_bits != NULL ? s_bits : xEventGroupCreate();
+    s_session.done = s_session.done != NULL ? s_session.done : xSemaphoreCreateBinary();
+    s_queue = s_queue != NULL ? s_queue : xQueueCreate(QUEUE_DEPTH, sizeof(req_t));
+    if (s_lock == NULL || s_bits == NULL || s_session.done == NULL || s_queue == NULL ||
+        xTaskCreatePinnedToCore(task, "ha_mqtt", TASK_STACK, NULL, TASK_PRIORITY, NULL, 0) != pdPASS) {
+        return ESP_ERR_NO_MEM;
+    }
+    s_started = true;
+    return ESP_OK;
+}
+
+void ha_mqtt_set_fields(const ha_fields_t *f)
+{
+    if (!s_started) {
+        return;
+    }
+    xSemaphoreTake(s_lock, portMAX_DELAY);
+    s_fields = *f;
+    xSemaphoreGive(s_lock);
+    post(&(req_t){ .type = REQ_FIELDS });
+}
+
+static ha_conn_t *copy(const ha_conn_t *c)
+{
+    ha_conn_t *out = malloc(sizeof(*out));
+    if (out != NULL) {
+        *out = *c;
+    }
+    return out;
+}
+
+esp_err_t ha_mqtt_session(const ha_conn_t *c, int budget_ms, char *detail, size_t size)
+{
+    if (!s_started) {
+        snprintf(detail, size, "no client");
+        return ESP_ERR_INVALID_STATE;
+    }
+    ha_conn_t *conn = copy(c);
+    if (conn == NULL) {
+        snprintf(detail, size, "no memory");
+        return ESP_ERR_NO_MEM;
+    }
+    xSemaphoreTake(s_session.done, 0); /* a late answer to an earlier session */
+    if (!post(&(req_t){ .type = REQ_SESSION, .conn = conn, .budget_ms = budget_ms })) {
+        snprintf(detail, size, "busy");
+        return ESP_FAIL;
+    }
+    if (xSemaphoreTake(s_session.done, pdMS_TO_TICKS(budget_ms + 5000)) != pdTRUE) {
+        snprintf(detail, size, "timeout");
+        return ESP_ERR_TIMEOUT;
+    }
+    snprintf(detail, size, "%s", s_session.detail);
+    return s_session.result;
+}
+
+void ha_mqtt_keep(const ha_conn_t *c)
+{
+    ha_conn_t *conn = copy(c);
+    if (conn != NULL) {
+        post(&(req_t){ .type = REQ_KEEP, .conn = conn });
+    }
+}
+
+void ha_mqtt_drop(void)
+{
+    post(&(req_t){ .type = REQ_DROP });
+}
+
+static void publish_text(req_type_t type, const char *text)
+{
+    if (!s_connected) {
+        return;
+    }
+    char *copy_text = strdup(text);
+    if (copy_text != NULL) {
+        post(&(req_t){ .type = type, .text = copy_text });
+    }
+}
+
+void ha_mqtt_publish_state(const char *json)
+{
+    publish_text(REQ_STATE, json);
+}
+
+void ha_mqtt_publish_action(const char *payload)
+{
+    publish_text(REQ_ACTION, payload);
+}
+
+void ha_mqtt_test(const ha_conn_t *c)
+{
+    ha_conn_t *conn = copy(c);
+    if (conn == NULL) {
+        return;
+    }
+    s_test.running = true;
+    s_test.done = false;
+    if (!post(&(req_t){ .type = REQ_TEST, .conn = conn })) {
+        s_test.running = false;
+    }
+}
+
+void ha_mqtt_forget_discovery(void)
+{
+    disc_hash_save(0);
+}
+
+void ha_mqtt_status(ha_mqtt_status_t *out)
+{
+    memset(out, 0, sizeof(*out));
+    if (!s_started) {
+        return;
+    }
+    out->keep = s_keep;
+    out->connected = s_connected;
+    out->test_running = s_test.running;
+    out->test_done = s_test.done;
+    out->test_ok = s_test.ok;
+    xSemaphoreTake(s_lock, portMAX_DELAY);
+    snprintf(out->detail, sizeof(out->detail), "%s", s_detail);
+    snprintf(out->test_detail, sizeof(out->test_detail), "%s", s_test.detail);
+    xSemaphoreGive(s_lock);
+}
```


- [ ] **Step 4: Run the tests, and build.**

Run: `cmake --build build-host && ctest --test-dir build-host | tail -3; tools/idf.sh build 2>&1 | grep -c 'warning:'`
Expected: `100% tests passed, 0 tests failed out of 65`; `0`.

- [ ] **Step 5: Commit.**

```bash
git add components/ha_mqtt test/host/CMakeLists.txt test/host/test_ha_session.c
git commit -m "feat(ha_mqtt): the MQTT client, its sessions and its kept connection (spec §12.9)"
```

### Task 8: MQTT in the sync and in sync mode `always` (`sync`, `ui`, `fetch`, `main`)

**Files:**
- Create: `main/app_mqtt.c`
- Modify: `components/sync/include/sync.h`, `components/sync/sync.c`, `components/sync/include/sync_plan.h`, `components/sync/sync_plan.c`, `components/ha_mqtt/include/ha_session.h`, `components/ha_mqtt/ha_session.c`, `components/ui/include/ui_menu.h`, `components/ui/ui_menu.c`, `components/ui/include/ui_preset.h`, `components/ui/ui_schedule.c`, `components/locale/include/lang.h`, `components/locale/lang_en.c`, `components/locale/lang_cs.c`, `components/fetch/CMakeLists.txt`, `components/fetch/fetch.c`, `main/CMakeLists.txt`, `main/app_internal.h`, `main/app_sync.c`, `main/app.c`, `main/app_ui.c`, `main/app_menu.c`, `test/host/test_ha_session.c`, `test/host/test_sync_plan.c`, `test/host/test_ui_schedule.c`, `test/host/test_ui_menu.c`, `test/host/screen_fixtures.h`

**Interfaces:**
- Consumes: Task 7's client (`ha_mqtt_init()`, `ha_mqtt_session()`, `ha_mqtt_keep()`, `ha_mqtt_drop()`, `ha_mqtt_status()`, `ha_mqtt_publish_state()`, `ha_mqtt_forget_discovery()`, `ha_hooks_t`, `ha_payloads_t`), `ha_expected_s()`; Task 3's `ha_state_t`, `ha_state_json()`, `ha_disc_message()`, `ha_disc_hash()`, `ha_expire_after_s()`; Task 1's `mqtt_*` settings; the sync's `sync_budget_ms()`; `app_sync_expected_s()`, `app_sync_lan_ui()`, `app_execute()`, `app_post()`.
- Produces:
  - `SYNC_STEP_MQTT` (named `"mqtt"`) and `sync_request_t.mqtt` (`esp_err_t (*)(int budget_ms, char *detail, size_t size)`, NULL while MQTT is off);
  - `sync_step_t` and `sync_step_result_t` move from `sync.h` to the pure `sync_plan.h` (`sync.h` includes it), and `int sync_first_failed(const uint8_t result[SYNC_STEP_COUNT])`: the step that fails the sync, never MQTT's;
  - `uint32_t sync_quiet_span_s(const sync_schedule_t *s)`, `uint32_t ui_schedule_longest_night_s(const ui_schedule_t *schedule)`, `bool ha_state_due(bool changed, int64_t since_s)`;
  - `UI_MI_INFO_MQTT`; `LS_M_MQTT` ("MQTT"), `LS_MQTT_CONNECTED` ("Connected" / "Připojeno"), `LS_SYNC_STEP_MQTT` ("MQTT");
  - `app_sync_state_t.mqtt_detail`, `.last_ok_at`, `.sched_failed`; `bool app_sync_mark_failed(void)`;
  - `bool app_mqtt_on(void)`, `void app_mqtt_prepare(void)`, `esp_err_t app_mqtt_sync_step(int budget_ms, char *detail, size_t size)`, `void app_mqtt_tick(void)`, `int64_t app_mqtt_deadline_ms(void)`, `void app_mqtt_settings_changed(const settings_t *before)`, `void app_mqtt_password_changed(void)`, `bool app_mqtt_failed(void)`, `void app_mqtt_summary(char *out, size_t size)`.

Step 6 of the sync (spec §9.3) runs the session within up to 15 s of the sync's 45, while MQTT is on and has a broker. Its failure is shown, on the Sync page, in Info ▸ MQTT ("05:30 no broker") and as the status bar's MQTT mark, but doesn't fail the sync: the sync isn't retried for it, and the next one tries again (D32). One host-tested function says which step fails a sync, for the status bar, the retries and the summary alike. The state's Last sync is the sync running, so HA sees the one that just reached it. Sync mode `always` keeps the client while it keeps Wi-Fi on the network: the app looks at the state every 30 s and publishes it on a change at most every 30 s, and every 5 min; the Wi-Fi signal, which jitters, goes out with the 5-min state rather than counting as a change. HA's sensors expire after twice the expected interval plus 10 min: in sync mode `always` the interval is 10 min plus the longest span Wi-Fi is off, quiet hours or the schedule's longest night, so the sensors outlast them. Settings that move the broker drop the kept client, which the next tick connects again; turning discovery off forgets its hash, so turning it on again publishes it, and the hash covers the broker, so a new one gets the configs (Task 3). A config that doesn't fit is left out, not sent half-written. The state and its copies sit in PSRAM, as does the settings' `before` (AGENTS §8).

Review minors M7 touches (D32): `fetch`'s transmit buffer is 1 KB, as the forecast's request line is 451–454 bytes (M6 review: 512 left no room for a header); a reply's headers that don't come in time are a timeout rather than `ESP_FAIL`, and the retry on a closed kept connection gets what is left of the budget, at least 1 s (M6 review); the crossed-out cloud shows only after a scheduled sync failed, not one on demand, and the log says "sync failed at weather: HTTP 503" (M5 review).

- [ ] **Step 1: Write the failing tests.**

`test/host/test_ha_session.c`:

```diff
--- a/test/host/test_ha_session.c
+++ b/test/host/test_ha_session.c
@@ -65,6 +65,16 @@ static void test_the_interval_sensors_expire_by(void)
     TEST_ASSERT_EQUAL_UINT32(600 + 7 * 3600, ha_expected_s(true, 7 * 3600, 7 * 3600));
 }
 
+/* spec §12.9, sync mode `always`: the state goes out on a change, at most every 30 s, and every 5 min. */
+static void test_the_state_goes_out_on_change_or_every_five_minutes(void)
+{
+    TEST_ASSERT_TRUE(ha_state_due(false, -1));   /* none went out yet */
+    TEST_ASSERT_FALSE(ha_state_due(true, 29));
+    TEST_ASSERT_TRUE(ha_state_due(true, 30));
+    TEST_ASSERT_FALSE(ha_state_due(false, 299));
+    TEST_ASSERT_TRUE(ha_state_due(false, 300));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -72,5 +82,6 @@ int main(void)
     RUN_TEST(test_topics_are_subscribed_once);
     RUN_TEST(test_collecting_ends_with_every_topic_or_a_quiet_second);
     RUN_TEST(test_the_interval_sensors_expire_by);
+    RUN_TEST(test_the_state_goes_out_on_change_or_every_five_minutes);
     return UNITY_END();
 }
```


`test/host/test_sync_plan.c`:

```diff
--- a/test/host/test_sync_plan.c
+++ b/test/host/test_sync_plan.c
@@ -1,3 +1,4 @@
+#include <string.h>
 #define _POSIX_C_SOURCE 200809L /* setenv */
 
 #include <stdlib.h>
@@ -439,6 +440,36 @@ static void test_interval_slots_on_the_fall_back_day(void)
     TEST_ASSERT_EQUAL_UINT32(2 * 3600, sync_expected_interval_s(&s, utc(2026, 10, 25, 10, 0, 0)));
 }
 
+/* M7: how long quiet hours keep Wi-Fi off in sync mode `always` (HA's sensors outlast it, spec §12.3). */
+static void test_the_quiet_hours_span(void)
+{
+    sync_schedule_t s = { .mode = SYNC_MODE_ALWAYS, .quiet = true, .quiet_from = 23 * 60, .quiet_to = 6 * 60 };
+    TEST_ASSERT_EQUAL_UINT32(7 * 3600, sync_quiet_span_s(&s));
+    s.quiet_from = 60;
+    TEST_ASSERT_EQUAL_UINT32(5 * 3600, sync_quiet_span_s(&s));
+    s.quiet_from = s.quiet_to;
+    TEST_ASSERT_EQUAL_UINT32(0, sync_quiet_span_s(&s)); /* no minutes: off */
+    s.quiet_from = 23 * 60;
+    s.quiet = false;
+    TEST_ASSERT_EQUAL_UINT32(0, sync_quiet_span_s(&s));
+}
+
+/* D32: a failed MQTT session never fails a sync; any other step that didn't pass does (the first one names it). */
+static void test_mqtt_alone_never_fails_a_sync(void)
+{
+    uint8_t r[SYNC_STEP_COUNT];
+    memset(r, SYNC_STEP_OK, sizeof(r));
+    TEST_ASSERT_EQUAL_INT(SYNC_STEP_COUNT, sync_first_failed(r));
+    r[SYNC_STEP_MQTT] = SYNC_STEP_FAILED;
+    TEST_ASSERT_EQUAL_INT(SYNC_STEP_COUNT, sync_first_failed(r));
+    r[SYNC_STEP_MQTT] = SYNC_STEP_NOT_RUN; /* MQTT off */
+    TEST_ASSERT_EQUAL_INT(SYNC_STEP_COUNT, sync_first_failed(r));
+    r[SYNC_STEP_RADAR] = SYNC_STEP_NOT_RUN;
+    TEST_ASSERT_EQUAL_INT(SYNC_STEP_RADAR, sync_first_failed(r));
+    r[SYNC_STEP_WEATHER] = SYNC_STEP_FAILED;
+    TEST_ASSERT_EQUAL_INT(SYNC_STEP_WEATHER, sync_first_failed(r));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -477,5 +508,7 @@ int main(void)
     RUN_TEST(test_a_time_in_the_spring_forward_gap_runs_after_it);
     RUN_TEST(test_a_time_in_the_repeated_hour_runs_once);
     RUN_TEST(test_interval_slots_on_the_fall_back_day);
+    RUN_TEST(test_the_quiet_hours_span);
+    RUN_TEST(test_mqtt_alone_never_fails_a_sync);
     return UNITY_END();
 }
```


`test/host/test_ui_schedule.c`:

```diff
--- a/test/host/test_ui_schedule.c
+++ b/test/host/test_ui_schedule.c
@@ -148,6 +148,18 @@ static void test_a_step_runs_nothing_late(void)
     TEST_ASSERT_EQUAL_INT(0, order[0]);
 }
 
+/* M7: the longest night the schedule starts (HA's sensors outlast it, spec §12.3); none while it is off. */
+static void test_the_longest_night(void)
+{
+    ui_schedule_t s = { .enabled = true, .count = 3 };
+    s.entries[0] = (ui_schedule_entry_t){ .at_min = 22 * 60, .days = 0x7F, .action = UI_SCHED_NIGHT, .until_min = 22 * 60 + 30 };
+    s.entries[1] = (ui_schedule_entry_t){ .at_min = 23 * 60, .days = 0x1F, .action = UI_SCHED_NIGHT, .until_min = 6 * 60 + 30 };
+    s.entries[2] = (ui_schedule_entry_t){ .at_min = 6 * 60, .days = 0x7F, .action = UI_SCHED_PRESET, .preset = 0 };
+    TEST_ASSERT_EQUAL_UINT32(7 * 3600 + 1800, ui_schedule_longest_night_s(&s));
+    s.enabled = false;
+    TEST_ASSERT_EQUAL_UINT32(0, ui_schedule_longest_night_s(&s));
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -157,5 +169,6 @@ int main(void)
     RUN_TEST(test_dst_changes_run_an_entry_once);
     RUN_TEST(test_an_entry_at_the_nights_end_minute_runs);
     RUN_TEST(test_a_step_runs_nothing_late);
+    RUN_TEST(test_the_longest_night);
     return UNITY_END();
 }
```


`test/host/test_ui_menu.c`:

```diff
--- a/test/host/test_ui_menu.c
+++ b/test/host/test_ui_menu.c
@@ -226,16 +226,17 @@ static void test_the_wifi_section_asks_before_it_forgets_anything(void)
     TEST_ASSERT_NULL(ui_menu_question(UI_MI_REBOOT, en));
 }
 
-/* Spec §5.7: Info gains the IP address and the MAC with M4, the last sync with M5. */
+/* Spec §5.7: Info gains the IP address and the MAC with M4, the last sync with M5, MQTT with M7 (last, so
+ * the first screen stays as it was). */
 static void test_info_shows_the_network_addresses(void)
 {
     open_item(UI_MI_INFO);
     ui_menu_item_t items[UI_MI_COUNT];
     const ui_menu_item_t expected[] = { UI_MI_INFO_BATTERY, UI_MI_INFO_FIRMWARE, UI_MI_INFO_DEVICE,
                                         UI_MI_INFO_IP,      UI_MI_INFO_MAC,      UI_MI_INFO_SYNC,
-                                        UI_MI_INFO_UPTIME,  UI_MI_INFO_MEMORY };
-    TEST_ASSERT_EQUAL_INT(8, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
-    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 8);
+                                        UI_MI_INFO_UPTIME,  UI_MI_INFO_MEMORY,   UI_MI_INFO_MQTT };
+    TEST_ASSERT_EQUAL_INT(9, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
+    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 9);
     TEST_ASSERT_TRUE(ui_menu_is_section(UI_MI_WIFI));
     TEST_ASSERT_FALSE(ui_menu_is_section(UI_MI_INFO_IP));
 }
```


`test/host/screen_fixtures.h`:

```diff
--- a/test/host/screen_fixtures.h
+++ b/test/host/screen_fixtures.h
@@ -70,6 +70,7 @@ static inline void fixture_menu_model(ui_menu_model_t *m, const lang_t *lang)
     m->info[UI_MI_INFO_MAC] = "14:c1:9f:54:bb:94";
     m->info[UI_MI_INFO_UPTIME] = "2 d 3 h";
     m->info[UI_MI_INFO_MEMORY] = "7.9 MB";
+    m->info[UI_MI_INFO_MQTT] = "Off";
     m->local = fixture_local(20, 48, 0);
 }
 
```


Run: `cmake --build build-host 2>&1 | grep -o "error: [^;]*" | sort | uniq -c`
Expected: `5 error: call to undeclared function 'ha_state_due'`, `1 error: call to undeclared function 'sync_quiet_span_s'`, `1 error: call to undeclared function 'sync_first_failed'` with the `SYNC_STEP_*` names undeclared in `test_sync_plan.c` (they are in `sync.h` so far), `1 error: call to undeclared function 'ui_schedule_longest_night_s'` and `1 error: use of undeclared identifier 'UI_MI_INFO_MQTT'`.

- [ ] **Step 2: The step, the spans and Info ▸ MQTT.**

`components/sync/include/sync.h`:

```diff
--- a/components/sync/include/sync.h
+++ b/components/sync/include/sync.h
@@ -7,6 +7,7 @@
 #include "esp_err.h"
 #include "radar_fetch.h"
 #include "settings.h"
+#include "sync_plan.h"
 
 /*
  * The sync (spec §9.3): Wi-Fi, the time, the weather, the air quality and the weather radar, on a
@@ -16,21 +17,6 @@
  * keeps it.
  */
 
-typedef enum {
-    SYNC_STEP_WIFI,
-    SYNC_STEP_TIME,
-    SYNC_STEP_WEATHER,
-    SYNC_STEP_AIR,
-    SYNC_STEP_RADAR, /* M6 (spec §11.2) */
-    SYNC_STEP_COUNT,
-} sync_step_t;
-
-typedef enum {
-    SYNC_STEP_NOT_RUN, /* skipped: an earlier step failed, or the sync never got there */
-    SYNC_STEP_OK,
-    SYNC_STEP_FAILED,
-} sync_step_result_t;
-
 #define SYNC_DETAIL_LEN 24
 
 typedef struct {
@@ -38,6 +24,9 @@ typedef struct {
     char ntp[SETTINGS_NTP_MAX][SETTINGS_HOST_LEN];
     radar_fetch_req_t radar; /* its deadline is the sync's to set */
     bool radar_only;         /* sync mode `always`'s radar refresh: Wi-Fi up already, the radar alone */
+    /* M7: the MQTT session on the sync's task within budget_ms, ESP_OK or why not in `detail`; NULL while
+     * MQTT is off, which skips the step */
+    esp_err_t (*mqtt)(int budget_ms, char *detail, size_t size);
 } sync_request_t;
 
 typedef struct {
@@ -59,4 +48,4 @@ esp_err_t sync_start(const sync_request_t *req, void (*done)(sync_report_t *repo
 bool sync_running(void);
 /* The step running now, for the progress the web UI shows; SYNC_STEP_COUNT when none runs. */
 sync_step_t sync_step(void);
-const char *sync_step_name(sync_step_t step); /* "wifi", "time", "weather", "air", "radar" */
+const char *sync_step_name(sync_step_t step); /* "wifi", "time", "weather", "air", "radar", "mqtt" */
```


`components/sync/sync.c`:

```diff
--- a/components/sync/sync.c
+++ b/components/sync/sync.c
@@ -28,6 +28,7 @@ static const char *TAG = "sync";
 #define BODY_MAX (12 * 1024) /* the largest reply, the forecast, is about 5 KB */
 #define RADAR_STEP_MS 10000 /* spec §9.3 */
 #define REFRESH_MAX_MS 30000 /* a radar-only refresh, the last hour's 12 frames at most (D28) */
+#define MQTT_STEP_MS 15000   /* spec §9.3 step 6 */
 
 static volatile bool s_running;
 static volatile uint8_t s_step = SYNC_STEP_COUNT;
@@ -173,6 +174,25 @@ static void step_radar(int max_ms)
     }
 }
 
+/* M7 (spec §9.3 step 6, D32): the MQTT session, while MQTT is on. */
+static void step_mqtt(void)
+{
+    if (s_req.mqtt == NULL) {
+        return; /* off: not run */
+    }
+    int budget = sync_budget_ms(esp_timer_get_time(), s_deadline_us, MQTT_STEP_MS);
+    if (budget == 0) {
+        failed(SYNC_STEP_MQTT, "timeout");
+        return;
+    }
+    char detail[SYNC_DETAIL_LEN];
+    if (s_req.mqtt(budget, detail, sizeof(detail)) == ESP_OK) {
+        s_report.result[SYNC_STEP_MQTT] = SYNC_STEP_OK;
+    } else {
+        failed(SYNC_STEP_MQTT, detail[0] ? detail : "failed");
+    }
+}
+
 /* Sync mode `always`: the radar alone, on the network Wi-Fi is on already (D23: never joining). */
 static void refresh_task(int64_t start)
 {
@@ -207,6 +227,8 @@ static void sync_task(void *arg)
         step_air();
         s_step = SYNC_STEP_RADAR;
         step_radar(RADAR_STEP_MS);
+        s_step = SYNC_STEP_MQTT;
+        step_mqtt();
     } else {
         failed(SYNC_STEP_WIFI, err == ESP_ERR_NOT_FOUND       ? "no network saved"
                                : err == ESP_ERR_INVALID_STATE ? "Wi-Fi busy"
@@ -249,6 +271,6 @@ sync_step_t sync_step(void)
 
 const char *sync_step_name(sync_step_t step)
 {
-    static const char *const k_names[SYNC_STEP_COUNT] = { "wifi", "time", "weather", "air", "radar" };
+    static const char *const k_names[SYNC_STEP_COUNT] = { "wifi", "time", "weather", "air", "radar", "mqtt" };
     return (unsigned)step < SYNC_STEP_COUNT ? k_names[step] : "";
 }
```


`components/sync/include/sync_plan.h`:

```diff
--- a/components/sync/include/sync_plan.h
+++ b/components/sync/include/sync_plan.h
@@ -13,6 +13,26 @@
 
 typedef enum { SYNC_MODE_TIMES, SYNC_MODE_INTERVAL, SYNC_MODE_ALWAYS, SYNC_MODE_MANUAL } sync_mode_t;
 
+/* A sync's steps, in their order (spec §9.3), and how each went. */
+typedef enum {
+    SYNC_STEP_WIFI,
+    SYNC_STEP_TIME,
+    SYNC_STEP_WEATHER,
+    SYNC_STEP_AIR,
+    SYNC_STEP_RADAR, /* M6 (spec §11.2) */
+    SYNC_STEP_MQTT,  /* M7 (spec §9.3 step 6, D32): its failure doesn't fail the sync */
+    SYNC_STEP_COUNT,
+} sync_step_t;
+
+typedef enum {
+    SYNC_STEP_NOT_RUN, /* skipped: an earlier step failed, or the sync never got there */
+    SYNC_STEP_OK,
+    SYNC_STEP_FAILED,
+} sync_step_result_t;
+
+/* The first step that didn't pass, which fails the sync, or SYNC_STEP_COUNT: MQTT's never does (D32). */
+int sync_first_failed(const uint8_t result[SYNC_STEP_COUNT]);
+
 #define SYNC_TIMES_MAX 8
 #define SYNC_ALWAYS_REFRESH_MIN 60 /* weather and air quality in `always` mode */
 #define SYNC_RETRY_COUNT 3         /* retries 15, 30 and 60 min after each failure */
@@ -37,6 +57,8 @@ typedef struct {
 } sync_due_t;
 
 bool sync_quiet_at(const sync_schedule_t *s, time_t t);
+/* How long quiet hours last, 0 when they are off or have no minutes (M7: what HA's sensors must outlast). */
+uint32_t sync_quiet_span_s(const sync_schedule_t *s);
 /* The next scheduled sync strictly after `after`: a time, an interval slot, or in `always` mode the
  * hourly refresh (aligned like interval 60); one inside quiet hours moves to their end. 0 in `manual`. */
 time_t sync_next_scheduled(const sync_schedule_t *s, time_t after);
```


`components/sync/sync_plan.c`:

```diff
--- a/components/sync/sync_plan.c
+++ b/components/sync/sync_plan.c
@@ -32,6 +32,24 @@ static int local_minute(time_t t)
     return localtime_r(&t, &local) != NULL ? local.tm_hour * 60 + local.tm_min : -1;
 }
 
+int sync_first_failed(const uint8_t result[SYNC_STEP_COUNT])
+{
+    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
+        if (i != SYNC_STEP_MQTT && result[i] != SYNC_STEP_OK) {
+            return i;
+        }
+    }
+    return SYNC_STEP_COUNT;
+}
+
+uint32_t sync_quiet_span_s(const sync_schedule_t *s)
+{
+    if (!s->quiet || s->quiet_from == s->quiet_to || s->quiet_from >= DAY_MIN || s->quiet_to >= DAY_MIN) {
+        return 0;
+    }
+    return (uint32_t)((s->quiet_to - s->quiet_from + DAY_MIN) % DAY_MIN) * 60;
+}
+
 bool sync_quiet_at(const sync_schedule_t *s, time_t t)
 {
     if (!s->quiet || s->quiet_from == s->quiet_to || s->quiet_from >= DAY_MIN || s->quiet_to >= DAY_MIN) {
```


`components/ha_mqtt/include/ha_session.h`:

```diff
--- a/components/ha_mqtt/include/ha_session.h
+++ b/components/ha_mqtt/include/ha_session.h
@@ -23,6 +23,9 @@ int ha_topic_index(const char *const *topics, int count, const char *topic, size
 /* The collect phase is over: every one of `topics` brought a message, or HA_QUIET_MS passed since
  * `quiet_since_ms` (the later of the subscriptions' confirmation and the last message). */
 bool ha_collect_over(int topics, int seen, int64_t now_ms, int64_t quiet_since_ms);
+/* Sync mode `always` (spec §12.9): the state goes out on a change, at most every 30 s, and every 5 min;
+ * `since_s` is the time since it last went out, negative for never. */
+bool ha_state_due(bool changed, int64_t since_s);
 /* What HA's sensors expire by (spec §12.3): the sync's expected interval, or in sync mode `always` 10 min
  * and the longest span Wi-Fi is off (quiet hours, a night); 0, never, in manual mode. */
 uint32_t ha_expected_s(bool always, uint32_t sync_expected_s, uint32_t longest_off_s);
```


`components/ha_mqtt/ha_session.c`:

```diff
--- a/components/ha_mqtt/ha_session.c
+++ b/components/ha_mqtt/ha_session.c
@@ -42,6 +42,11 @@ bool ha_collect_over(int topics, int seen, int64_t now_ms, int64_t quiet_since_m
     return (topics > 0 && seen >= topics) || now_ms - quiet_since_ms >= HA_QUIET_MS;
 }
 
+bool ha_state_due(bool changed, int64_t since_s)
+{
+    return since_s < 0 || since_s >= 300 || (changed && since_s >= 30);
+}
+
 uint32_t ha_expected_s(bool always, uint32_t sync_expected_s, uint32_t longest_off_s)
 {
     return always ? ALWAYS_EXPECTED_S + longest_off_s : sync_expected_s;
```


`components/ui/include/ui_preset.h`:

```diff
--- a/components/ui/include/ui_preset.h
+++ b/components/ui/include/ui_preset.h
@@ -88,6 +88,8 @@ int ui_schedule_due(const ui_schedule_t *schedule, time_t after, time_t now, int
  * back, and a gap longer than max_gap_s, a sleep no entry could end, only move the mark. */
 int ui_schedule_step(const ui_schedule_t *schedule, time_t *checked, time_t now, time_t max_gap_s,
                      int order[UI_SCHEDULE_MAX]);
+/* The longest night the schedule starts, 0 while it is off (M7: what HA's sensors must outlast). */
+uint32_t ui_schedule_longest_night_s(const ui_schedule_t *schedule);
 /* Where the checks resume after a night that ended at `until`: a night covers [start, until), so
  * the entries inside it don't run, and one at the end minute does. */
 time_t ui_schedule_after_night(time_t until);
```


`components/ui/ui_schedule.c`:

```diff
--- a/components/ui/ui_schedule.c
+++ b/components/ui/ui_schedule.c
@@ -50,6 +50,19 @@ int ui_schedule_step(const ui_schedule_t *schedule, time_t *checked, time_t now,
     return ui_schedule_due(schedule, after, now, order);
 }
 
+uint32_t ui_schedule_longest_night_s(const ui_schedule_t *schedule)
+{
+    uint32_t longest = 0;
+    for (int i = 0; schedule->enabled && i < schedule->count && i < UI_SCHEDULE_MAX; i++) {
+        const ui_schedule_entry_t *e = &schedule->entries[i];
+        uint32_t span = (uint32_t)((e->until_min - e->at_min + 24 * 60) % (24 * 60)) * 60;
+        if (e->action == UI_SCHED_NIGHT && span > longest) {
+            longest = span;
+        }
+    }
+    return longest;
+}
+
 time_t ui_schedule_after_night(time_t until)
 {
     return until - 1;
```


`components/ui/include/ui_menu.h`:

```diff
--- a/components/ui/include/ui_menu.h
+++ b/components/ui/include/ui_menu.h
@@ -50,6 +50,7 @@ typedef enum {
     UI_MI_INFO_SYNC, /* the last sync's result (spec §5.7) */
     UI_MI_INFO_UPTIME,
     UI_MI_INFO_MEMORY,
+    UI_MI_INFO_MQTT, /* M7: the last MQTT session, or the kept connection (D32) */
     UI_MI_SYSTEM,
     UI_MI_LANGUAGE,      /* choice: the language packs */
     UI_MI_REBOOT,        /* action */
```


`components/ui/ui_menu.c`:

```diff
--- a/components/ui/ui_menu.c
+++ b/components/ui/ui_menu.c
@@ -67,6 +67,7 @@ static const node_t k_nodes[UI_MI_COUNT] = {
     [UI_MI_INFO_SYNC] = { .label = LS_M_LAST_SYNC, .kind = K_INFO, .parent = UI_MI_INFO },
     [UI_MI_INFO_UPTIME] = { .label = LS_M_UPTIME, .kind = K_INFO, .parent = UI_MI_INFO },
     [UI_MI_INFO_MEMORY] = { .label = LS_M_FREE_MEMORY, .kind = K_INFO, .parent = UI_MI_INFO },
+    [UI_MI_INFO_MQTT] = { .label = LS_M_MQTT, .kind = K_INFO, .parent = UI_MI_INFO },
     [UI_MI_SYSTEM] = { .label = LS_M_SYSTEM, .kind = K_SECTION, .parent = UI_MI_ROOT },
     [UI_MI_LANGUAGE] = { .label = LS_M_LANGUAGE, .kind = K_CHOICE, .parent = UI_MI_SYSTEM },
     [UI_MI_REBOOT] = { .label = LS_M_REBOOT, .kind = K_ACTION, .parent = UI_MI_SYSTEM },
```


`components/locale/include/lang.h`:

```diff
--- a/components/locale/include/lang.h
+++ b/components/locale/include/lang.h
@@ -200,6 +200,9 @@ typedef enum {
     LS_DIR_W,
     LS_DIR_NW,
     LS_MESSAGE, /* ha.message: Home Assistant's message (M7, spec §12.7) */
+    LS_M_MQTT,         /* Info ▸ MQTT (spec §5.7): the last session, or the kept connection */
+    LS_MQTT_CONNECTED,
+    LS_SYNC_STEP_MQTT, /* the sync's step 6 (app_sync.c maps the steps) */
     LS_COUNT,
 } lang_str_t;
 
```


`components/locale/lang_en.c`:

```diff
--- a/components/locale/lang_en.c
+++ b/components/locale/lang_en.c
@@ -208,6 +208,9 @@ const lang_t lang_en = {
         [LS_DIR_W] = "W",
         [LS_DIR_NW] = "NW",
         [LS_MESSAGE] = "Message",
+        [LS_M_MQTT] = "MQTT",
+        [LS_MQTT_CONNECTED] = "Connected",
+        [LS_SYNC_STEP_MQTT] = "MQTT",
     },
     .weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
     .weekdays_short = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
```


`components/locale/lang_cs.c`:

```diff
--- a/components/locale/lang_cs.c
+++ b/components/locale/lang_cs.c
@@ -248,6 +248,9 @@ const lang_t lang_cs = {
         [LS_DIR_W] = "Z",
         [LS_DIR_NW] = "SZ",
         [LS_MESSAGE] = "Zpráva",
+        [LS_M_MQTT] = "MQTT",
+        [LS_MQTT_CONNECTED] = "Připojeno",
+        [LS_SYNC_STEP_MQTT] = "MQTT",
     },
     .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
     .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
```


Run: `cmake --build build-host && for t in test_ha_session test_sync_plan test_ui_schedule test_ui_menu; do ./build-host/$t | tail -1; done`
Expected: `OK` four times (5, 37, 7 and 14 tests).

- [ ] **Step 3: `fetch`'s minors.**

`components/fetch/CMakeLists.txt`:

```diff
--- a/components/fetch/CMakeLists.txt
+++ b/components/fetch/CMakeLists.txt
@@ -2,4 +2,4 @@
 idf_component_register(SRCS "fetch.c"
                        INCLUDE_DIRS "include"
                        REQUIRES esp_common
-                       PRIV_REQUIRES esp_http_client mbedtls esp_app_format log)
+                       PRIV_REQUIRES esp_http_client mbedtls esp_app_format esp_timer log)
```


`components/fetch/fetch.c`:

```diff
--- a/components/fetch/fetch.c
+++ b/components/fetch/fetch.c
@@ -8,6 +8,7 @@
 #include "esp_crt_bundle.h"
 #include "esp_http_client.h"
 #include "esp_log.h"
+#include "esp_timer.h"
 
 static const char *TAG = "fetch";
 
@@ -36,6 +37,7 @@ static esp_err_t get_once(fetch_session_t *s, const char *url, char *buf, size_t
             .crt_bundle_attach = esp_crt_bundle_attach,
             .user_agent = agent,
             .buffer_size = 2048,
+            .buffer_size_tx = 1024, /* the forecast's request line is 451-454 B: 512 left no room (M6 review) */
         };
         client = esp_http_client_init(&cfg);
         ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_NO_MEM, TAG, "client");
@@ -49,7 +51,9 @@ static esp_err_t get_once(fetch_session_t *s, const char *url, char *buf, size_t
     }
     if (err == ESP_OK) {
         int64_t length = esp_http_client_fetch_headers(client);
-        if (length < 0) {
+        if (length == -ESP_ERR_HTTP_EAGAIN) {
+            err = ESP_ERR_TIMEOUT; /* the reply's headers didn't come in time: "timeout", not ESP_FAIL */
+        } else if (length < 0) {
             err = ESP_FAIL; /* no reply: a connection the server closed meanwhile reads as status -1 */
         } else {
             *status = esp_http_client_get_status_code(client);
@@ -89,9 +93,11 @@ esp_err_t fetch_get(fetch_session_t *s, const char *url, void *buf, size_t size,
                     int *status)
 {
     bool reused = s->client != NULL;
+    int64_t start_ms = esp_timer_get_time() / 1000;
     esp_err_t err = get_once(s, url, buf, size, len, timeout_ms, status);
-    if (err != ESP_OK && reused && *status <= 0) { /* the server closed the kept connection: once more */
-        err = get_once(s, url, buf, size, len, timeout_ms, status);
+    int left_ms = timeout_ms - (int)(esp_timer_get_time() / 1000 - start_ms); /* the retry gets what is left */
+    if (err != ESP_OK && reused && *status <= 0 && left_ms >= 1000) { /* the kept connection was closed: once more */
+        err = get_once(s, url, buf, size, len, left_ms, status);
     }
     if (err != ESP_OK) {
         ESP_LOGW(TAG, "GET %.48s...: %s, HTTP %d, %u bytes", url, esp_err_to_name(err), *status, (unsigned)*len);
```


- [ ] **Step 4: The app: the step, sync mode `always`, the state, Info and the mark.**

`main/CMakeLists.txt`:

```diff
--- a/main/CMakeLists.txt
+++ b/main/CMakeLists.txt
@@ -1,7 +1,7 @@
 idf_component_register(SRCS "main.c" "app.c" "app_ui.c" "app_menu.c" "app_cmds.c" "app_config.c" "app_web.c" "app_sync.c"
-                            "app_radar.c" "app_flights.c"
+                            "app_radar.c" "app_flights.c" "app_mqtt.c"
                        INCLUDE_DIRS "."
-                       PRIV_REQUIRES adsb app_update board console datastore diag display esp_app_format
+                       PRIV_REQUIRES adsb app_update board console datastore diag display esp_app_format ha_mqtt
                                      esp_driver_gpio esp_timer espcoredump gfx heap json locale map netmgr nvs_flash
                                      power radar rtc scheduler sensors st7305 storage sync timekeeping ui util weather
                                      webui)
```


`main/app_internal.h`:

```diff
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -28,8 +28,11 @@ typedef struct {
     sync_due_t due;             /* the next automatic sync; at 0 = none */
     uint32_t last_at;           /* when the last sync started (UTC); 0 = none since the cold boot */
     uint8_t last_result[SYNC_STEP_COUNT]; /* sync_step_result_t */
-    uint8_t last_failed_step;   /* the first step that failed; SYNC_STEP_COUNT if none */
+    uint8_t last_failed_step;   /* the first step that failed, but MQTT's; SYNC_STEP_COUNT if none */
     char last_detail[SYNC_DETAIL_LEN];
+    char mqtt_detail[SYNC_DETAIL_LEN]; /* M7: why the MQTT step failed (spec §9.3 step 6) */
+    uint32_t last_ok_at;        /* when the last sync that worked started (UTC); 0 = none: HA's Last sync */
+    bool sched_failed;          /* a scheduled sync failed, and none worked since: the crossed-out cloud */
 } app_sync_state_t;
 
 typedef struct {
@@ -137,7 +140,8 @@ void app_sync_toggle_always(void);
 bool app_sync_active(void);      /* a sync or a radar-only refresh runs */
 bool app_sync_refreshing(void);  /* what runs is a radar-only refresh (spec §9.3): not shown as a sync */
 bool app_sync_running(void);     /* a sync runs, or waits for a refresh to end: what the screens show */
-bool app_sync_failed(void);      /* the last sync failed a step */
+bool app_sync_failed(void);      /* the last sync failed a step, MQTT's aside (D32) */
+bool app_sync_mark_failed(void); /* the status bar's crossed-out cloud: a scheduled sync failed (spec §5.2) */
 bool app_sync_holds_wifi(void);  /* sync mode `always` keeps Wi-Fi now */
 bool app_sync_wifi_pending(void); /* Wi-Fi is on, but nothing needs it: awake until it is off */
 void app_sync_wifi_check(void);   /* turns that Wi-Fi off, once a web reply has gone out */
@@ -219,5 +223,16 @@ int64_t app_uptime_ms(void); /* milliseconds since boot, unmoved by clock change
 /* Logs, NVS and the console, as a board that stays awake has them (spec §3.3): a sync needs NVS. */
 void app_alive(void);
 
+/* MQTT and Home Assistant (main/app_mqtt.c, spec §12, D32). */
+bool app_mqtt_on(void);    /* on, with a broker */
+void app_mqtt_prepare(void); /* as a sync starts: the client, and the session's settings */
+esp_err_t app_mqtt_sync_step(int budget_ms, char *detail, size_t size); /* sync_request_t.mqtt */
+void app_mqtt_tick(void);  /* sync mode `always`'s connection and state; call from the app loop */
+int64_t app_mqtt_deadline_ms(void); /* app_uptime_ms() of its next look at the state; 0 when it keeps none */
+void app_mqtt_settings_changed(const settings_t *before); /* a kept connection starts again with them */
+void app_mqtt_password_changed(void);
+bool app_mqtt_failed(void); /* the status bar's MQTT mark (spec §5.2) */
+void app_mqtt_summary(char *out, size_t size); /* Info ▸ MQTT: "Off", "12:05 OK", "Connected" */
+
 /* The `field`, `preset` and `night` console commands (main/app_cmds.c); call after diag_start(). */
 void app_register_commands(void);
```


`main/app_mqtt.c`:

```diff
new file mode 100644
--- /dev/null
+++ b/main/app_mqtt.c
@@ -0,0 +1,319 @@
+#include <stdio.h>
+#include <string.h>
+#include <time.h>
+
+#include "app.h"
+#include "app_internal.h"
+#include "esp_app_desc.h"
+#include "esp_log.h"
+#include "esp_mac.h"
+#include "ha_mqtt.h"
+#include "ha_session.h"
+#include "lang.h"
+#include "netmgr.h"
+#include "nvs.h"
+#include "sync_plan.h"
+#include "timekeeping.h"
+
+/* MQTT and Home Assistant on the app's side (spec §12, D32): the client's hooks, the sync's step, sync mode
+ * `always`'s connection and its state, and what the status bar and Info say about them. It belongs to the
+ * app task, but for the hooks, which run on the client's and the sync's tasks and hand over to it. */
+
+static const char *TAG = "app_mqtt";
+
+#define NVS_KEY_PASS "mqtt_pass" /* in `secrets`: write-only (spec §12.1) */
+#define CHECK_MS 30000           /* sync mode `always`: how often the state is looked at (spec §12.9) */
+
+static bool s_started;           /* the client's task runs */
+static ha_conn_t s_conn;         /* the next session's: set on the app task as a sync starts */
+static bool s_keeping;           /* sync mode `always` keeps the client */
+static int64_t s_published_ms = -1;
+static int64_t s_check_ms;
+EXT_RAM_BSS_ATTR static char s_published[HA_STATE_MAX]; /* the last state that went out, its uptime left out */
+
+static const char *bat_state_name(uint8_t state)
+{
+    static const char *const k_names[] = { "unknown", "discharging", "charging", "full" };
+    return state < sizeof(k_names) / sizeof(k_names[0]) ? k_names[state] : "unknown";
+}
+
+static void device_id(char *out, size_t size)
+{
+    uint8_t mac[6] = { 0 };
+    esp_read_mac(mac, ESP_MAC_WIFI_STA); /* spec §5.5 */
+    snprintf(out, size, "reflbo-%02x%02x", mac[4], mac[5]);
+}
+
+bool app_mqtt_on(void)
+{
+    const settings_t *s = app_settings();
+    return s->mqtt_enabled && s->mqtt_host[0] != '\0';
+}
+
+/* The connection's settings, with the password from NVS (`secrets`, spec §12.1): NVS is up, as Wi-Fi is. */
+static void make_conn(ha_conn_t *c)
+{
+    const settings_t *s = app_settings();
+    memset(c, 0, sizeof(*c));
+    device_id(c->id, sizeof(c->id));
+    snprintf(c->host, sizeof(c->host), "%s", s->mqtt_host);
+    c->port = s->mqtt_port;
+    snprintf(c->user, sizeof(c->user), "%s", s->mqtt_user);
+    c->discovery = s->mqtt_discovery;
+    nvs_handle_t nvs;
+    if (nvs_open("secrets", NVS_READONLY, &nvs) == ESP_OK) {
+        size_t len = sizeof(c->password);
+        if (nvs_get_str(nvs, NVS_KEY_PASS, c->password, &len) != ESP_OK) {
+            c->password[0] = '\0';
+        }
+        nvs_close(nvs);
+    }
+}
+
+/* What HA's sensors expire by (spec §12.3): the sync's interval, or in sync mode `always` 10 min and the
+ * longest span Wi-Fi is off, quiet hours or a night of the schedule. */
+static uint32_t expected_s(void)
+{
+    const settings_t *s = app_settings();
+    sync_schedule_t q = { .quiet = s->quiet, .quiet_from = s->quiet_from, .quiet_to = s->quiet_to };
+    uint32_t quiet = sync_quiet_span_s(&q), night = ui_schedule_longest_night_s(&app_presets()->schedule);
+    return ha_expected_s(s->sync_mode == SETTINGS_SYNC_ALWAYS, app_sync_expected_s(), quiet > night ? quiet : night);
+}
+
+/* The state (spec §12.2) as it is now; the strings point into the app's state. */
+static void build_state(ha_state_t *out)
+{
+    const app_ui_state_t *st = app_state();
+    *out = (ha_state_t){ .charging = "unknown", .fw = esp_app_get_description()->version,
+                         .uptime_s = (uint32_t)(app_uptime_ms() / 1000) };
+    ds_entry_t e;
+    if (ds_get(app_ds(), DS_ENV_TEMP, &e) && e.updated != 0) {
+        out->has_temp = true;
+        out->temp_c100 = e.value;
+    }
+    if (ds_get(app_ds(), DS_ENV_HUM, &e) && e.updated != 0) {
+        out->has_hum = true;
+        out->hum_pct100 = e.value;
+    }
+    if (ds_get(app_ds(), DS_BAT_LEVEL, &e) && e.updated != 0) {
+        out->has_battery = true;
+        out->bat_pct = e.value;
+        out->bat_mv = e.mv;
+        out->charging = bat_state_name(e.bat_state);
+    }
+    if (app_net_ready()) {
+        netmgr_status_t ns;
+        netmgr_status(&ns);
+        if (ns.state == NETMGR_STATION) {
+            out->has_rssi = true;
+            out->rssi = ns.rssi;
+        }
+    }
+    const ui_preset_t *p = &st->presets.presets[st->presets.active];
+    out->preset_id = p->id;
+    out->preset_name = p->name;
+    if (timekeeping_valid()) { /* a sync that reached its MQTT step is the last one (spec §9.3) */
+        out->last_sync = app_sync_running() ? (uint32_t)time(NULL) : st->sync.last_ok_at;
+    }
+}
+
+/* On the app task, for the client's task (ha_hooks_t.payloads): the state and every discovery config. */
+static void fill_payloads(void *arg)
+{
+    ha_payloads_t *out = arg;
+    ha_state_t state;
+    build_state(&state);
+    ha_state_json(&state, out->state, sizeof(out->state));
+    const ui_presets_t *p = app_presets();
+    const char *names[UI_PRESET_MAX];
+    for (int i = 0; i < p->count; i++) {
+        names[i] = p->presets[i].name;
+    }
+    char id[16], broker[SETTINGS_HOST_LEN + 8];
+    device_id(id, sizeof(id));
+    snprintf(broker, sizeof(broker), "%s:%u", app_settings()->mqtt_host, app_settings()->mqtt_port);
+    ha_disc_t d = { .id = id, .prefix = app_settings()->mqtt_prefix, .fw = esp_app_get_description()->version,
+                    .expire_after_s = ha_expire_after_s(expected_s()), .presets = names, .preset_count = p->count,
+                    .broker = broker };
+    for (int i = 0; i < HA_DISC_COUNT; i++) {
+        if (!ha_disc_message(&d, i, out->topic[i], sizeof(out->topic[i]), out->payload[i], sizeof(out->payload[i]))) {
+            ESP_LOGE(TAG, "discovery config %d doesn't fit: left out", i);
+            out->topic[i][0] = '\0';
+        }
+    }
+    out->disc_hash = ha_disc_hash(&d);
+}
+
+static bool on_payloads(ha_payloads_t *out)
+{
+    return app_execute(fill_payloads, out) == ESP_OK;
+}
+
+static void on_command(ha_cmd_t cmd, const char *payload, size_t len)
+{
+    ESP_LOGI(TAG, "command %d (%.*s): applied from M7's next step", cmd, (int)len, payload);
+}
+
+static void on_value(const char *key, const ha_value_t *v)
+{
+    (void)v;
+    ESP_LOGD(TAG, "value for %s", key);
+}
+
+static void status_changed(void *arg)
+{
+    (void)arg;
+    app_ui_render(); /* the status bar's mark (spec §5.2) */
+}
+
+static void on_status(void)
+{
+    app_post(status_changed, NULL);
+}
+
+static void start(void)
+{
+    if (s_started) {
+        return;
+    }
+    static const ha_hooks_t k_hooks = { .command = on_command, .value = on_value, .status = on_status,
+                                        .payloads = on_payloads };
+    esp_err_t err = ha_mqtt_init(&k_hooks);
+    if (err != ESP_OK) {
+        ESP_LOGE(TAG, "the client didn't start: %s", esp_err_to_name(err));
+        return;
+    }
+    s_started = true;
+}
+
+void app_mqtt_prepare(void)
+{
+    start();
+    make_conn(&s_conn);
+}
+
+esp_err_t app_mqtt_sync_step(int budget_ms, char *detail, size_t size) /* on the sync's task */
+{
+    if (!s_started) {
+        snprintf(detail, size, "no client");
+        return ESP_FAIL;
+    }
+    return ha_mqtt_session(&s_conn, budget_ms, detail, size);
+}
+
+/* Sync mode `always` (spec §12.9): connected while it keeps Wi-Fi on the network; the state on a change, at
+ * most every 30 s, and every 5 min. */
+void app_mqtt_tick(void)
+{
+    bool want = app_mqtt_on() && app_sync_lan_ui();
+    if (want && !s_keeping) {
+        start();
+        if (!s_started) {
+            return; /* no memory for the client: the next tick tries again */
+        }
+        ha_conn_t c;
+        make_conn(&c);
+        ha_mqtt_keep(&c);
+        s_keeping = true;
+        s_published_ms = -1;
+        ESP_LOGI(TAG, "sync mode always: connecting to %s", c.host);
+    } else if (!want && s_keeping) {
+        ha_mqtt_drop();
+        s_keeping = false;
+        ESP_LOGI(TAG, "sync mode always: disconnected");
+    }
+    int64_t now = app_uptime_ms();
+    if (!s_keeping || now < s_check_ms) {
+        return;
+    }
+    s_check_ms = now + CHECK_MS;
+    ha_mqtt_status_t hs;
+    ha_mqtt_status(&hs);
+    if (!hs.connected) {
+        return;
+    }
+    ha_state_t state;
+    build_state(&state);
+    EXT_RAM_BSS_ATTR static char json[HA_STATE_MAX], same[HA_STATE_MAX];
+    ha_state_t still = state;
+    still.uptime_s = 0;
+    still.has_rssi = false; /* the signal jitters: it goes out with the 5-min state, not as a change */
+    ha_state_json(&still, same, sizeof(same));
+    bool changed = strcmp(same, s_published) != 0;
+    if (ha_state_due(changed, s_published_ms < 0 ? -1 : (now - s_published_ms) / 1000)) {
+        ha_state_json(&state, json, sizeof(json));
+        ha_mqtt_publish_state(json);
+        memcpy(s_published, same, sizeof(s_published));
+        s_published_ms = now;
+    }
+}
+
+int64_t app_mqtt_deadline_ms(void)
+{
+    return s_keeping ? s_check_ms : 0;
+}
+
+void app_mqtt_settings_changed(const settings_t *before)
+{
+    const settings_t *s = app_settings();
+    if (before->mqtt_discovery && !s->mqtt_discovery) {
+        ha_mqtt_forget_discovery(); /* on again, it goes out again (spec §12.3) */
+    }
+    bool moved = before->mqtt_enabled != s->mqtt_enabled || strcmp(before->mqtt_host, s->mqtt_host) != 0 ||
+                 before->mqtt_port != s->mqtt_port || strcmp(before->mqtt_user, s->mqtt_user) != 0 ||
+                 before->mqtt_discovery != s->mqtt_discovery || strcmp(before->mqtt_prefix, s->mqtt_prefix) != 0;
+    if (moved && s_keeping) {
+        ha_mqtt_drop(); /* the next tick connects with the new settings */
+        s_keeping = false;
+    }
+}
+
+void app_mqtt_password_changed(void)
+{
+    if (s_keeping) {
+        ha_mqtt_drop();
+        s_keeping = false;
+    }
+}
+
+bool app_mqtt_failed(void)
+{
+    if (!app_mqtt_on()) {
+        return false;
+    }
+    if (s_keeping) {
+        ha_mqtt_status_t hs;
+        ha_mqtt_status(&hs);
+        return !hs.connected && hs.detail[0] != '\0'; /* down after a failure, not while connecting */
+    }
+    return app_state()->sync.last_at != 0 && app_state()->sync.last_result[SYNC_STEP_MQTT] == SYNC_STEP_FAILED;
+}
+
+void app_mqtt_summary(char *out, size_t size)
+{
+    const lang_t *lang = lang_get(app_settings()->language);
+    const app_sync_state_t *st = &app_state()->sync;
+    if (!app_mqtt_on()) {
+        snprintf(out, size, "%s", lang_str(lang, LS_OFF));
+        return;
+    }
+    if (s_keeping) {
+        ha_mqtt_status_t hs;
+        ha_mqtt_status(&hs);
+        snprintf(out, size, "%s",
+                 hs.connected ? lang_str(lang, LS_MQTT_CONNECTED) : hs.detail[0] ? hs.detail : "\xE2\x80\xA6");
+        return;
+    }
+    uint8_t r = st->last_at != 0 ? st->last_result[SYNC_STEP_MQTT] : SYNC_STEP_NOT_RUN;
+    if (r == SYNC_STEP_NOT_RUN) {
+        snprintf(out, size, "%s", lang_str(lang, LS_SYNC_NEVER));
+        return;
+    }
+    time_t at = st->last_at;
+    struct tm local;
+    localtime_r(&at, &local);
+    char when[12];
+    const char *suffix;
+    lang_format_time(local.tm_hour, local.tm_min, 0, app_settings()->clock_24h, false, when, sizeof(when), &suffix);
+    snprintf(out, size, "%s%s%s %s", when, suffix[0] ? " " : "", suffix, r == SYNC_STEP_OK ? "OK" : st->mqtt_detail);
+}
```


`main/app_sync.c`:

```diff
--- a/main/app_sync.c
+++ b/main/app_sync.c
@@ -120,15 +120,12 @@ bool app_sync_running(void)
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
+    return s->last_at != 0 && sync_first_failed(s->last_result) < SYNC_STEP_COUNT; /* MQTT's never (D32) */
+}
+
+bool app_sync_mark_failed(void)
+{
+    return st()->sched_failed;
 }
 
 /* Wi-Fi is off: netmgr isn't up yet (a routine wake), or it says so. */
@@ -188,14 +185,12 @@ static void save_summary(const sync_report_t *r, time_t started)
     app_sync_state_t *s = st();
     s->last_at = (uint32_t)started;
     s->last_detail[0] = '\0';
-    s->last_failed_step = SYNC_STEP_COUNT;
-    for (int i = 0; i < SYNC_STEP_COUNT; i++) {
-        s->last_result[i] = r->result[i];
-        if (r->result[i] != SYNC_STEP_OK && s->last_failed_step == SYNC_STEP_COUNT) {
-            s->last_failed_step = (uint8_t)i;
-            snprintf(s->last_detail, sizeof(s->last_detail), "%s", r->detail[i]);
-        }
+    memcpy(s->last_result, r->result, sizeof(s->last_result));
+    s->last_failed_step = (uint8_t)sync_first_failed(r->result); /* MQTT's never fails it (D32) */
+    if (s->last_failed_step < SYNC_STEP_COUNT) {
+        snprintf(s->last_detail, sizeof(s->last_detail), "%s", r->detail[s->last_failed_step]);
     }
+    snprintf(s->mqtt_detail, sizeof(s->mqtt_detail), "%s", r->detail[SYNC_STEP_MQTT]);
 }
 
 static int64_t s_started_mono; /* esp_timer µs at the start, to date it once the clock is right */
@@ -254,8 +249,17 @@ static void apply(void *arg)
     save_summary(r, started);
     bool ok = !app_sync_failed();
     sync_history_record(&st()->history, s_started_by, ok, now);
-    ESP_LOGI(TAG, "sync %s%s%s", ok ? "done" : "failed at ", ok ? "" : sync_step_name(st()->last_failed_step),
-             ok ? "" : st()->last_detail);
+    if (ok) {
+        st()->last_ok_at = (uint32_t)started;
+        st()->sched_failed = false;
+    } else if (s_started_by.at != 0) {
+        st()->sched_failed = true; /* spec §5.2: a scheduled sync's, not one on demand (M5 review) */
+    }
+    ESP_LOGI(TAG, "sync %s%s%s%s", ok ? "done" : "failed at ", ok ? "" : sync_step_name(st()->last_failed_step),
+             ok ? "" : ": ", ok ? "" : st()->last_detail); /* "failed at weather: HTTP 503" (M5 review) */
+    if (r->result[SYNC_STEP_MQTT] == SYNC_STEP_FAILED) {
+        ESP_LOGW(TAG, "MQTT: %s", r->detail[SYNC_STEP_MQTT]);
+    }
     s_active = false; /* the report is applied: the next sync may overwrite it */
     release_wifi();
     app_sync_schedule();
@@ -289,6 +293,10 @@ static esp_err_t start(bool manual, sync_due_t due)
     req = (sync_request_t){ .lat_e4 = set->lat_e4, .lon_e4 = set->lon_e4 };
     memcpy(req.ntp, set->ntp, sizeof(req.ntp));
     app_radar_request(&req.radar);
+    if (app_mqtt_on()) { /* M7 (spec §9.3 step 6) */
+        app_mqtt_prepare();
+        req.mqtt = app_mqtt_sync_step;
+    }
     esp_err_t err = sync_start(&req, done);
     if (err == ESP_OK) {
         s_active = true;
@@ -465,7 +473,7 @@ void app_sync_summary(char *out, size_t size)
     const char *suffix;
     lang_format_time(local.tm_hour, local.tm_min, 0, app_settings()->clock_24h, false, when, sizeof(when), &suffix);
     static const lang_str_t k_steps[SYNC_STEP_COUNT] = { LS_SYNC_STEP_WIFI, LS_SYNC_STEP_TIME, LS_SYNC_STEP_WEATHER,
-                                                         LS_SYNC_STEP_AIR, LS_SYNC_STEP_RADAR };
+                                                         LS_SYNC_STEP_AIR, LS_SYNC_STEP_RADAR, LS_SYNC_STEP_MQTT };
     if (s->last_failed_step >= SYNC_STEP_COUNT) {
         snprintf(out, size, "%s%s%s OK", when, suffix[0] ? " " : "", suffix);
     } else {
```


`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -703,6 +703,7 @@ static void app_task(void *arg)
             }
             app_config_tick();
             app_sync_wifi_check();
+            app_mqtt_tick();
             app_ui_toast_expire();
             app_radar_loop_tick();
             bool busy = pending || app_menu_is_open() || app_ui_toast_active();
@@ -744,7 +745,8 @@ static void app_task(void *arg)
             int64_t mono = app_uptime_ms();
             const int64_t deadlines[] = { app_menu_deadline_ms(), app_ui_toast_until_ms(),
                                           app_ui_night() ? s_peek_until_ms : 0, app_config_redraw_ms(),
-                                          s_ota_pending ? OTA_VERIFY_MS : 0, app_radar_loop_deadline_ms() };
+                                          s_ota_pending ? OTA_VERIFY_MS : 0, app_radar_loop_deadline_ms(),
+                                          app_mqtt_deadline_ms() };
             for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++) {
                 if (deadlines[i] != 0 && deadlines[i] - mono < wait_ms) {
                     wait_ms = deadlines[i] - mono;
```


`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -214,9 +214,10 @@ void app_ui_context(ui_context_t *ctx)
                            .fahrenheit = s.settings.fahrenheit,
                            .web_session = (app_config_active() || app_sync_lan_ui()) && webui_session_active(),
                            .lat_e4 = s.settings.lat_e4, .lon_e4 = s.settings.lon_e4,
-                           .sync = app_sync_running()  ? UI_SYNC_RUNNING
-                                   : app_sync_failed() ? UI_SYNC_FAILED
-                                                       : UI_SYNC_IDLE };
+                           .sync = app_sync_running()       ? UI_SYNC_RUNNING
+                                   : app_sync_mark_failed() ? UI_SYNC_FAILED
+                                                            : UI_SYNC_IDLE,
+                           .mqtt_failed = app_mqtt_failed() };
     if (app_sync_holds_wifi() && !app_config_active()) { /* spec §5.2: sync mode `always` */
         netmgr_status_t ns;
         netmgr_status(&ns);
@@ -677,7 +678,7 @@ static void settings_changed(void)
 
 esp_err_t app_ui_replace_settings(const char *json, char *err, size_t err_size)
 {
-    static settings_t parsed;
+    EXT_RAM_BSS_ATTR static settings_t parsed, before;
     if (!settings_from_json(json, &s.settings, &parsed, err, err_size)) {
         return ESP_ERR_INVALID_ARG;
     }
@@ -687,7 +688,9 @@ esp_err_t app_ui_replace_settings(const char *json, char *err, size_t err_size)
         return ESP_ERR_INVALID_SIZE;
     }
     settings_replaced(&parsed, &s.settings); /* a page that turns `always` on: BOOT double returns */
+    before = s.settings;
     s.settings = parsed;
+    app_mqtt_settings_changed(&before);
     memcpy(s_settings_base, json, n + 1); /* keys this firmware doesn't know stay, as in the file */
     esp_err_t e = app_ui_save_settings();
     settings_changed();
```


`main/app_menu.c`:

```diff
--- a/main/app_menu.c
+++ b/main/app_menu.c
@@ -61,7 +61,7 @@ static const char *s_zone_names[ZONES_MAX];
 static const char *s_language_names[LANGUAGE_COUNT];
 static char s_rate_text[RATE_COUNT][12];
 static const char *s_rates[RATE_COUNT];
-static char s_info[8][80];
+static char s_info[9][80];
 static const char *s_sync_modes[4];
 
 static const lang_t *lang(void)
@@ -225,10 +225,11 @@ static void build_model(void)
     snprintf(s_info[6], sizeof(s_info[6]), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
              mac[5]);
     app_sync_summary(s_info[7], sizeof(s_info[7]));
+    app_mqtt_summary(s_info[8], sizeof(s_info[8])); /* M7 */
     const ui_menu_item_t info_items[] = { UI_MI_INFO_BATTERY, UI_MI_INFO_FIRMWARE, UI_MI_INFO_DEVICE,
                                           UI_MI_INFO_UPTIME,  UI_MI_INFO_MEMORY,   UI_MI_INFO_IP,
-                                          UI_MI_INFO_MAC,     UI_MI_INFO_SYNC };
-    for (int i = 0; i < 8; i++) {
+                                          UI_MI_INFO_MAC,     UI_MI_INFO_SYNC,     UI_MI_INFO_MQTT };
+    for (int i = 0; i < 9; i++) {
         m->info[info_items[i]] = s_info[i];
     }
 }
```


- [ ] **Step 5: Run the tests, and build.**

Run: `cmake --build build-host && ctest --test-dir build-host | tail -3; tools/idf.sh build 2>&1 | grep -c 'warning:'; tools/idf.sh exec xtensa-esp32s3-elf-nm -S build/reflbo.elf | grep ' s_snap$'`
Expected: `100% tests passed, 0 tests failed out of 65`; `0`; `50000000 000012f8 d s_snap` (4 856 bytes: the MQTT settings, the presets' key table and the syncs' new state).

- [ ] **Step 6: Commit.**

```bash
git add components main test/host
git commit -m "feat(app): MQTT in every sync and in sync mode always (spec §9.3, §12.9)"
```

### Task 9: Commands, values, the message and key presses (`ha_mqtt`, `ui`, `storage`, `main`)

**Files:**
- Modify: `components/ha_mqtt/include/ha_payload.h`, `components/ha_mqtt/ha_payload.c`, `components/ha_mqtt/include/ha_store.h`, `components/ha_mqtt/ha_store.c`, `components/ha_mqtt/include/ha_mqtt.h`, `components/ha_mqtt/ha_mqtt.c`, `components/ui/include/ui_preset.h`, `components/ui/ui_preset.c`, `components/storage/include/storage.h`, `main/app_internal.h`, `main/app_mqtt.c`, `main/app.c`, `main/app_ui.c`, `main/app_cmds.c`, `AGENTS.md`, `test/host/test_ha_payload.c`, `test/host/test_ha_store.c`, `test/host/test_ui_preset.c`

**Interfaces:**
- Consumes: Tasks 2–8; `storage_init()`, `storage_load()`; `ui_draw_message_banner()` (Task 6); `app_ui_select()`, `ui_presets_next()`, `app_sync_now()`, `app_sync_active()`; `diag_on_owner()`.
- Produces:
  - `typedef enum { HA_PRESS_SHORT, HA_PRESS_DOUBLE, HA_PRESS_LONG } ha_press_t;` and `const char *ha_action_payload(bool boot, ha_press_t press)`;
  - `bool ha_store_clear(ha_store_t *s, int i)`;
  - `int ui_presets_lookup(const ui_presets_t *p, const char *text)`: a preset by its name, else by its id;
  - `void ha_mqtt_resubscribe(void)`, `uint32_t ha_mqtt_discovery_hash(void)`;
  - `#define STORAGE_MQTT_FIELDS_PATH "/fs/cfg/mqtt_fields.json"`;
  - `void app_mqtt_boot(bool warm)`, `void app_mqtt_seal(void)`, `void app_mqtt_clock_moved(int64_t delta_s)`, `const ha_store_t *app_mqtt_store(void)`, `void app_mqtt_key(board_button_t button, gesture_t gesture)`, `bool app_mqtt_banner(void)`, `void app_mqtt_dismiss(void)`, `void app_mqtt_set_message(const char *text)`, `bool app_mqtt_set_value(const char *key, const char *text, char *err, size_t size)`, `bool app_mqtt_clear_value(const char *key)`, `void app_mqtt_print_status(void)`;
  - the console's `mqtt status`, and `field` for `ha.message` and `mqtt.<key>`.

The values and the message live in `ha_store_t` in RTC FAST memory (3 968 bytes of its 8 KB). RTC SLOW holds the snapshot (5 008 bytes used of 8 192 with the rest), and RTC FAST stays powered in deep sleep, because `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP` makes `sleep_modes.c` keep it on; the heap leaves `.rtc.force_fast` alone, and on the S3 both cores reach it. The block is sealed before a deep sleep and checked at a routine wake, which then reads no file; a cold boot, or a block that doesn't check, starts it again from `/fs/cfg/mqtt_fields.json`. The mappings themselves (topics and paths) are read once a boot when something needs them: the client, the console or the web page.

Commands (spec §12.4) come on the esp-mqtt task, are copied and posted to the app task, and applied there in the order they came: `cmd/preset` by name, as HA's select sends it, else by id, saved like a manual switch; `cmd/next` as KEY short; `cmd/sync` only in sync mode `always` and not while a sync runs (one that waited while the board slept comes during a sync, D32); `cmd/message` sets or clears the message and draws it. An unknown preset or payload is logged and ignored. A sync's session publishes the new state at its end; in sync mode `always` it goes out at once. Values are staged under a mutex and drained by one posted call, so a burst of retained values draws once; the staging and its copy sit in PSRAM. When the clock moves (a sync sets it after its session), the values and the message move with it, keeping their age. In sync mode `always` the kept session subscribes again every 5 min, so the broker sends the retained values again and a value that doesn't change stays fresh.

The banner is drawn over the dashboard only: not over the menu, config mode (even its dashboard view while a phone is logged in, D20), the first-run screen or the critical-battery screen. KEY short while it shows only dismisses it. While MQTT is connected, in sync mode `always` or a sync's own session, each other dashboard gesture is published to `reflbo/<id>/action` too (spec §12.8). That publish happens at once on the app task under a small lock on the client handle: a sync's session keeps the client's task busy until it ends, and a key press queued behind it would find the client gone. Once connected, esp-mqtt holds its lock only briefly, and a QoS 0 publish is a write. `handle_button()`'s dashboard bindings move into `dashboard_button()` so the two can come first.

- [ ] **Step 1: Write the failing tests.**

`test/host/test_ha_payload.c`:

```diff
--- a/test/host/test_ha_payload.c
+++ b/test/host/test_ha_payload.c
@@ -282,6 +282,32 @@ static void test_the_largest_discovery_message_fits(void)
     printf("the largest discovery payload: %d bytes of %d\n", largest, HA_PAYLOAD_MAX);
 }
 
+/* spec §12.8: each dashboard gesture's payload on reflbo/<id>/action, which a device trigger of discovery
+ * waits for. */
+static void test_key_presses_have_their_payloads(void)
+{
+    TEST_ASSERT_EQUAL_STRING("key_short", ha_action_payload(false, HA_PRESS_SHORT));
+    TEST_ASSERT_EQUAL_STRING("key_double", ha_action_payload(false, HA_PRESS_DOUBLE));
+    TEST_ASSERT_EQUAL_STRING("key_long", ha_action_payload(false, HA_PRESS_LONG));
+    TEST_ASSERT_EQUAL_STRING("boot_short", ha_action_payload(true, HA_PRESS_SHORT));
+    TEST_ASSERT_EQUAL_STRING("boot_double", ha_action_payload(true, HA_PRESS_DOUBLE));
+    TEST_ASSERT_EQUAL_STRING("boot_long", ha_action_payload(true, HA_PRESS_LONG));
+    TEST_ASSERT_NULL(ha_action_payload(false, (ha_press_t)3));
+    ha_disc_t d = discovery();
+    for (int boot = 0; boot < 2; boot++) {
+        for (int p = HA_PRESS_SHORT; p <= HA_PRESS_LONG; p++) {
+            char want[48];
+            snprintf(want, sizeof(want), "\"payload\":\"%s\"", ha_action_payload(boot != 0, (ha_press_t)p));
+            bool found = false;
+            for (int i = 0; i < HA_DISC_COUNT && !found; i++) {
+                TEST_ASSERT_TRUE(ha_disc_message(&d, i, s_topic, sizeof(s_topic), s_payload, sizeof(s_payload)));
+                found = strstr(s_payload, want) != NULL;
+            }
+            TEST_ASSERT_TRUE_MESSAGE(found, want);
+        }
+    }
+}
+
 int main(void)
 {
     UNITY_BEGIN();
@@ -297,5 +323,6 @@ int main(void)
     RUN_TEST(test_discovery_matches_its_golden);
     RUN_TEST(test_the_hash_follows_what_discovery_says);
     RUN_TEST(test_the_largest_discovery_message_fits);
+    RUN_TEST(test_key_presses_have_their_payloads);
     return UNITY_END();
 }
```


`test/host/test_ha_store.c`:

```diff
--- a/test/host/test_ha_store.c
+++ b/test/host/test_ha_store.c
@@ -92,6 +92,21 @@ static void test_a_value_says_whether_the_screen_changes(void)
 }
 
 /* Editing the mappings keeps the values of the keys that stay with their kind. */
+/* The console's `field clear mqtt.<key>` (spec §15): the entry has no value again; other entries keep theirs. */
+static void test_a_value_can_be_cleared(void)
+{
+    ha_value_t n = number(123, 1), t = text("open");
+    ha_store_set(&s_store, 0, &n, NOW);
+    ha_store_set(&s_store, 1, &t, NOW);
+    TEST_ASSERT_TRUE(ha_store_clear(&s_store, 0));
+    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_freshness(&s_store, 0, NOW));
+    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_freshness(&s_store, 1, NOW));
+    TEST_ASSERT_FALSE(ha_store_clear(&s_store, 0)); /* nothing left to clear */
+    TEST_ASSERT_FALSE(ha_store_clear(&s_store, 5));
+    TEST_ASSERT_FALSE(ha_store_clear(&s_store, -1));
+    TEST_ASSERT_TRUE(ha_store_set(&s_store, 0, &n, NOW)); /* the same value again shows again */
+}
+
 static void test_a_rebuild_keeps_what_still_maps(void)
 {
     ha_value_t v = number(215, 1);
@@ -219,6 +234,7 @@ int main(void)
     RUN_TEST(test_entries_follow_the_mappings);
     RUN_TEST(test_values_go_stale_after_their_time_to_live);
     RUN_TEST(test_a_value_says_whether_the_screen_changes);
+    RUN_TEST(test_a_value_can_be_cleared);
     RUN_TEST(test_a_rebuild_keeps_what_still_maps);
     RUN_TEST(test_a_rebuild_follows_the_keys_through_any_order);
     RUN_TEST(test_a_clock_move_keeps_the_ages);
```


`test/host/test_ui_preset.c`:

```diff
--- a/test/host/test_ui_preset.c
+++ b/test/host/test_ui_preset.c
@@ -38,6 +38,19 @@ static void test_defaults_are_the_six_built_ins(void)
     TEST_ASSERT_EQUAL_INT(-1, ui_presets_find(&s_p, "nope"));
 }
 
+/* cmd/preset (spec §12.4): HA's select sends a preset's name, as its options are the names; an automation or
+ * the console may send an id. Names first, so the select always gets the preset it shows. */
+static void test_a_preset_is_found_by_its_name_or_its_id(void)
+{
+    TEST_ASSERT_EQUAL_INT(4, ui_presets_lookup(&s_p, "Rain radar"));
+    TEST_ASSERT_EQUAL_INT(4, ui_presets_lookup(&s_p, "rain"));
+    TEST_ASSERT_EQUAL_INT(-1, ui_presets_lookup(&s_p, "rain radar")); /* names match exactly */
+    TEST_ASSERT_EQUAL_INT(-1, ui_presets_lookup(&s_p, ""));
+    TEST_ASSERT_EQUAL_INT(-1, ui_presets_lookup(&s_p, NULL));
+    snprintf(s_p.presets[1].name, sizeof(s_p.presets[1].name), "%s", "rain"); /* a name that is another's id */
+    TEST_ASSERT_EQUAL_INT(1, ui_presets_lookup(&s_p, "rain"));
+}
+
 static void test_next_follows_cycle_order_and_skips_presets_out_of_it(void)
 {
     TEST_ASSERT_EQUAL_INT(1, ui_presets_next(&s_p, true)); /* home -> indoor */
@@ -655,6 +668,7 @@ int main(void)
 {
     UNITY_BEGIN();
     RUN_TEST(test_defaults_are_the_six_built_ins);
+    RUN_TEST(test_a_preset_is_found_by_its_name_or_its_id);
     RUN_TEST(test_next_follows_cycle_order_and_skips_presets_out_of_it);
     RUN_TEST(test_the_cycle_visits_flights_only_in_sync_mode_always);
     RUN_TEST(test_a_file_from_before_m6_gains_the_radars_once);
```


Run: `cmake --build build-host 2>&1 | grep -o "error: [^;]*" | sort | uniq -c`
Expected: `1 error: call to undeclared function 'ha_action_payload'`, the `ha_press_t` names undeclared (`ha_press_t`, `HA_PRESS_SHORT`, `HA_PRESS_DOUBLE`, `HA_PRESS_LONG`), `4 error: call to undeclared function 'ha_store_clear'` and `1 error: call to undeclared function 'ui_presets_lookup'`.

- [ ] **Step 2: The key presses' payloads, clearing a value, and finding a preset by name.**

`components/ha_mqtt/include/ha_payload.h`:

```diff
--- a/components/ha_mqtt/include/ha_payload.h
+++ b/components/ha_mqtt/include/ha_payload.h
@@ -35,6 +35,16 @@ bool ha_cmd_press(const char *payload, size_t len);
 /* The message's text: control characters as spaces, cut at a character to 96 bytes. */
 void ha_message_text(const char *payload, size_t len, char *out, size_t size);
 
+typedef enum {
+    HA_PRESS_SHORT,
+    HA_PRESS_DOUBLE,
+    HA_PRESS_LONG,
+} ha_press_t;
+
+/* A dashboard gesture's payload on reflbo/<id>/action (spec §12.8): "key_short" ... "boot_long"; NULL for
+ * no such press. */
+const char *ha_action_payload(bool boot, ha_press_t press);
+
 typedef struct {
     uint8_t kind;     /* ha_kind_t */
     uint8_t decimals; /* a number's: `number` is the value times 10^decimals */
```


`components/ha_mqtt/ha_payload.c`:

```diff
--- a/components/ha_mqtt/ha_payload.c
+++ b/components/ha_mqtt/ha_payload.c
@@ -70,6 +70,15 @@ void ha_message_text(const char *payload, size_t len, char *out, size_t size)
     copy_cut(payload, len, out, size < HA_MESSAGE_LEN ? size : HA_MESSAGE_LEN);
 }
 
+const char *ha_action_payload(bool boot, ha_press_t press)
+{
+    static const char *const k_payloads[2][3] = {
+        { "key_short", "key_double", "key_long" },
+        { "boot_short", "boot_double", "boot_long" },
+    };
+    return (unsigned)press < 3 ? k_payloads[boot ? 1 : 0][press] : NULL;
+}
+
 /* A number as field `f` keeps it: rounded to its precision, or to its own decimals up to 3; fewer when
  * the value with them doesn't fit an int32. */
 static bool scale_number(const ha_field_t *f, double v, ha_value_t *out)
```


`components/ha_mqtt/include/ha_store.h`:

```diff
--- a/components/ha_mqtt/include/ha_store.h
+++ b/components/ha_mqtt/include/ha_store.h
@@ -57,6 +57,8 @@ void ha_store_set_default_ttl(ha_store_t *s, uint32_t ttl_s);
  * A value of the wrong kind, or for no entry, is ignored. */
 bool ha_store_set(ha_store_t *s, int i, const ha_value_t *v, time_t now);
 ha_freshness_t ha_store_freshness(const ha_store_t *s, int i, time_t now);
+/* Entry `i` has no value again (the console's `field clear`); false if it had none. */
+bool ha_store_clear(ha_store_t *s, int i);
 /* The clock moved by `delta_s` (a sync set it): every value and the message keep their age. */
 void ha_store_shift_time(ha_store_t *s, int64_t delta_s);
 /* The message (spec §12.7): a new one shows its banner again; "" clears it. */
```


`components/ha_mqtt/ha_store.c`:

```diff
--- a/components/ha_mqtt/ha_store.c
+++ b/components/ha_mqtt/ha_store.c
@@ -103,6 +103,15 @@ bool ha_store_set(ha_store_t *s, int i, const ha_value_t *v, time_t now)
     return !(shown && same);
 }
 
+bool ha_store_clear(ha_store_t *s, int i)
+{
+    if (i < 0 || i >= s->count || s->entry[i].updated == 0) {
+        return false;
+    }
+    s->entry[i].updated = 0;
+    return true;
+}
+
 /* A time moved by `delta_s`, kept at 1 or later: 0 means none. */
 static uint32_t shifted(uint32_t t, int64_t delta_s)
 {
```


`components/ui/include/ui_preset.h`:

```diff
--- a/components/ui/include/ui_preset.h
+++ b/components/ui/include/ui_preset.h
@@ -120,6 +120,8 @@ int ui_preset_slots(const ui_preset_t *p);
 /* The built-in presets (spec §5.4): used when presets.json is missing or invalid. */
 void ui_presets_defaults(ui_presets_t *p);
 int ui_presets_find(const ui_presets_t *p, const char *id); /* index, or -1 */
+/* A preset by its name, as HA's select sends it (spec §12.4), else by its id; -1 if neither. */
+int ui_presets_lookup(const ui_presets_t *p, const char *text);
 /* The next preset in cycle order after the active one, wrapping; the active one if no other
  * preset is in the cycle (spec §5.4, KEY short). Presets on the Flights layout are in it only in
  * sync mode `always` (D28). */
```


`components/ui/ui_preset.c`:

```diff
--- a/components/ui/ui_preset.c
+++ b/components/ui/ui_preset.c
@@ -92,6 +92,16 @@ int ui_presets_find(const ui_presets_t *p, const char *id)
     return -1;
 }
 
+int ui_presets_lookup(const ui_presets_t *p, const char *text)
+{
+    for (int i = 0; text != NULL && text[0] != '\0' && i < p->count; i++) {
+        if (strcmp(p->presets[i].name, text) == 0) {
+            return i;
+        }
+    }
+    return ui_presets_find(p, text);
+}
+
 int ui_presets_next(const ui_presets_t *p, bool always)
 {
     for (int step = 1; step < p->count; step++) {
```


Run: `cmake --build build-host && for t in test_ha_payload test_ha_store test_ui_preset; do ./build-host/$t | tail -1; done`
Expected: `OK` three times (13, 9 and 33 tests).

- [ ] **Step 3: The client: key presses at once, and subscribing again.**

`components/ha_mqtt/include/ha_mqtt.h`:

```diff
--- a/components/ha_mqtt/include/ha_mqtt.h
+++ b/components/ha_mqtt/include/ha_mqtt.h
@@ -60,6 +60,9 @@ typedef struct {
 esp_err_t ha_mqtt_init(const ha_hooks_t *hooks);
 /* The mappings changed (a cold boot, the MQTT page, a restore): a kept connection subscribes again. */
 void ha_mqtt_set_fields(const ha_fields_t *f);
+/* A kept connection subscribes again, so the broker sends each mapped topic's retained value again: a value
+ * that doesn't change stays fresh in sync mode `always` (spec §12.9). */
+void ha_mqtt_resubscribe(void);
 /* A sync's step (spec §9.3 step 6): connect, subscribe, collect, publish the state and any discovery
  * configs whose hash changed, and disconnect, within `budget_ms`; with the client kept, publish on it. ESP_OK,
  * or ESP_FAIL with why in `detail`. */
@@ -68,9 +71,12 @@ esp_err_t ha_mqtt_session(const ha_conn_t *c, int budget_ms, char *detail, size_
 void ha_mqtt_keep(const ha_conn_t *c);
 void ha_mqtt_drop(void);
 void ha_mqtt_publish_state(const char *json);     /* while connected: retained, QoS 1 */
-void ha_mqtt_publish_action(const char *payload); /* while connected: reflbo/<id>/action, QoS 0 (spec §12.8) */
+/* While connected: reflbo/<id>/action at QoS 0 (spec §12.8), at once from the caller's task, even in a sync's
+ * session; dropped otherwise. */
+void ha_mqtt_publish_action(const char *payload);
 /* Test connection (spec §10.3): connects with `c` and leaves; ha_mqtt_status() reports how it went. */
 void ha_mqtt_test(const ha_conn_t *c);
 /* Discovery was turned off: the next time it is on, the configs go out again (NVS sys/mqtt_disc). */
 void ha_mqtt_forget_discovery(void);
+uint32_t ha_mqtt_discovery_hash(void); /* what NVS sys/mqtt_disc holds: 0 if no configs went out */
 void ha_mqtt_status(ha_mqtt_status_t *out);
```


`components/ha_mqtt/ha_mqtt.c`:

```diff
--- a/components/ha_mqtt/ha_mqtt.c
+++ b/components/ha_mqtt/ha_mqtt.c
@@ -38,9 +38,7 @@ static const char *TAG = "ha_mqtt";
 #define BIT_CONNECTED BIT0
 #define BIT_DOWN BIT1
 
-typedef enum {
-    REQ_SESSION, REQ_KEEP, REQ_DROP, REQ_STATE, REQ_ACTION, REQ_TEST, REQ_FIELDS, REQ_UP, REQ_DOWN
-} req_type_t;
+typedef enum { REQ_SESSION, REQ_KEEP, REQ_DROP, REQ_STATE, REQ_TEST, REQ_FIELDS, REQ_UP, REQ_DOWN } req_type_t;
 
 typedef struct {
     req_type_t type;
@@ -57,7 +55,8 @@ static QueueHandle_t s_queue;
  * us events, and on_data() takes s_lock: so nothing calls esp-mqtt, or a hook, with s_lock held. */
 static SemaphoreHandle_t s_lock;
 static EventGroupHandle_t s_bits;
-static esp_mqtt_client_handle_t s_client; /* the task's */
+static esp_mqtt_client_handle_t s_client; /* the task's; set under s_client_lock, which the app's key presses take */
+static SemaphoreHandle_t s_client_lock;
 static volatile uint32_t s_gen;            /* the client's generation: a closed client's late events are ignored */
 static ha_conn_t s_conn;                   /* what s_client connects with */
 static volatile bool s_connected, s_keep;
@@ -240,15 +239,23 @@ static esp_mqtt_client_handle_t open_client(const ha_conn_t *c, int timeout_ms)
     return client;
 }
 
+static void set_client(esp_mqtt_client_handle_t client)
+{
+    xSemaphoreTake(s_client_lock, portMAX_DELAY);
+    s_client = client;
+    xSemaphoreGive(s_client_lock);
+}
+
 static void close_client(void)
 {
     s_gen++; /* its events from now on are ignored, the CONNECTED of a connect that ends during the stop too */
-    if (s_client != NULL) {
-        esp_mqtt_client_stop(s_client); /* a DISCONNECT first: the broker keeps the session (spec §12.4) */
-        esp_mqtt_client_destroy(s_client);
-        s_client = NULL;
-    }
+    esp_mqtt_client_handle_t client = s_client;
+    set_client(NULL);
     s_connected = false;
+    if (client != NULL) {
+        esp_mqtt_client_stop(client); /* a DISCONNECT first: the broker keeps the session (spec §12.4) */
+        esp_mqtt_client_destroy(client);
+    }
 }
 
 /* Waits for the connection; false with the reason in s_detail. */
@@ -408,7 +415,7 @@ static void run_session(const ha_conn_t *c, int budget_ms)
         xSemaphoreGive(s_lock);
     } else {
         s_conn = *c;
-        s_client = open_client(c, budget_ms < CONNECT_MAX_MS ? budget_ms : CONNECT_MAX_MS);
+        set_client(open_client(c, budget_ms < CONNECT_MAX_MS ? budget_ms : CONNECT_MAX_MS));
         ok = s_client != NULL && wait_connected(end) && subscribe(end);
         if (ok) {
             collect(end);
@@ -430,7 +437,7 @@ static void keep(const ha_conn_t *c)
     s_failures = 0;
     s_retry_at_ms = 0;
     s_conn = *c;
-    s_client = open_client(c, CONNECT_MAX_MS);
+    set_client(open_client(c, CONNECT_MAX_MS));
     if (s_client == NULL) {
         s_retry_at_ms = now_ms() + ha_backoff_ms(s_failures++);
     }
@@ -444,7 +451,7 @@ static void test(const ha_conn_t *c)
     } else {
         ha_conn_t saved = s_conn;
         s_conn = *c;
-        s_client = open_client(c, CONNECT_MAX_MS);
+        set_client(open_client(c, CONNECT_MAX_MS));
         ok = s_client != NULL && wait_connected(now_ms() + CONNECT_MAX_MS);
         close_client();
         s_conn = saved;
@@ -475,17 +482,16 @@ static void handle(req_t *r)
         s_hooks.status();
         break;
     case REQ_STATE:
-    case REQ_ACTION:
         if (s_connected && s_client != NULL) {
             char topic[HA_TOPIC_MAX];
-            ha_topic(topic, sizeof(topic), s_conn.id, r->type == REQ_STATE ? "state" : "action");
-            esp_mqtt_client_publish(s_client, topic, r->text, 0, r->type == REQ_STATE ? 1 : 0, r->type == REQ_STATE);
+            ha_topic(topic, sizeof(topic), s_conn.id, "state");
+            esp_mqtt_client_publish(s_client, topic, r->text, 0, 1, 1);
         }
         break;
     case REQ_TEST:
         test(r->conn);
         break;
-    case REQ_FIELDS:
+    case REQ_FIELDS: /* new mappings, or the retained values again */
         if (s_keep && s_connected) {
             subscribe(now_ms() + CONNECT_MAX_MS);
         }
@@ -548,10 +554,11 @@ esp_err_t ha_mqtt_init(const ha_hooks_t *hooks)
     s_hooks = *hooks;
     /* each made once: after a failure, a later call makes what is still missing */
     s_lock = s_lock != NULL ? s_lock : xSemaphoreCreateMutex();
+    s_client_lock = s_client_lock != NULL ? s_client_lock : xSemaphoreCreateMutex();
     s_bits = s_bits != NULL ? s_bits : xEventGroupCreate();
     s_session.done = s_session.done != NULL ? s_session.done : xSemaphoreCreateBinary();
     s_queue = s_queue != NULL ? s_queue : xQueueCreate(QUEUE_DEPTH, sizeof(req_t));
-    if (s_lock == NULL || s_bits == NULL || s_session.done == NULL || s_queue == NULL ||
+    if (s_lock == NULL || s_client_lock == NULL || s_bits == NULL || s_session.done == NULL || s_queue == NULL ||
         xTaskCreatePinnedToCore(task, "ha_mqtt", TASK_STACK, NULL, TASK_PRIORITY, NULL, 0) != pdPASS) {
         return ESP_ERR_NO_MEM;
     }
@@ -570,6 +577,11 @@ void ha_mqtt_set_fields(const ha_fields_t *f)
     post(&(req_t){ .type = REQ_FIELDS });
 }
 
+void ha_mqtt_resubscribe(void)
+{
+    post(&(req_t){ .type = REQ_FIELDS });
+}
+
 static ha_conn_t *copy(const ha_conn_t *c)
 {
     ha_conn_t *out = malloc(sizeof(*out));
@@ -616,25 +628,32 @@ void ha_mqtt_drop(void)
     post(&(req_t){ .type = REQ_DROP });
 }
 
-static void publish_text(req_type_t type, const char *text)
+void ha_mqtt_publish_state(const char *json)
 {
     if (!s_connected) {
         return;
     }
-    char *copy_text = strdup(text);
-    if (copy_text != NULL) {
-        post(&(req_t){ .type = type, .text = copy_text });
+    char *text = strdup(json);
+    if (text != NULL) {
+        post(&(req_t){ .type = REQ_STATE, .text = text });
     }
 }
 
-void ha_mqtt_publish_state(const char *json)
-{
-    publish_text(REQ_STATE, json);
-}
-
+/* On the app task, at once: a sync's session keeps the client's task busy until it ends (spec §12.8). Once
+ * connected, esp-mqtt holds its lock only briefly, and a QoS 0 publish is a write. */
 void ha_mqtt_publish_action(const char *payload)
 {
-    publish_text(REQ_ACTION, payload);
+    if (s_client_lock == NULL || payload == NULL) {
+        return;
+    }
+    xSemaphoreTake(s_client_lock, portMAX_DELAY);
+    if (s_connected && s_client != NULL) {
+        char topic[HA_TOPIC_MAX];
+        ha_topic(topic, sizeof(topic), s_conn.id, "action");
+        esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
+        ESP_LOGI(TAG, "key press: %s", payload);
+    }
+    xSemaphoreGive(s_client_lock);
 }
 
 void ha_mqtt_test(const ha_conn_t *c)
@@ -655,6 +674,11 @@ void ha_mqtt_forget_discovery(void)
     disc_hash_save(0);
 }
 
+uint32_t ha_mqtt_discovery_hash(void)
+{
+    return disc_hash_saved();
+}
+
 void ha_mqtt_status(ha_mqtt_status_t *out)
 {
     memset(out, 0, sizeof(*out));
```


`mqtt status` prints what spec §15 lists: the settings without the password, the client, the last session, each mapping with its value and age, the discovery hash, and the message.

- [ ] **Step 4: The app: the store, commands, values, the banner, KEY and the console.**

`components/storage/include/storage.h`:

```diff
--- a/components/storage/include/storage.h
+++ b/components/storage/include/storage.h
@@ -13,6 +13,7 @@
 
 #define STORAGE_SETTINGS_PATH "/fs/cfg/settings.json"
 #define STORAGE_PRESETS_PATH "/fs/cfg/presets.json"
+#define STORAGE_MQTT_FIELDS_PATH "/fs/cfg/mqtt_fields.json" /* M7 (spec §12.5) */
 
 esp_err_t storage_init(void);
 bool storage_ready(void);
```


`main/app_internal.h`:

```diff
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -233,6 +233,20 @@ void app_mqtt_settings_changed(const settings_t *before); /* a kept connection s
 void app_mqtt_password_changed(void);
 bool app_mqtt_failed(void); /* the status bar's MQTT mark (spec §5.2) */
 void app_mqtt_summary(char *out, size_t size); /* Info ▸ MQTT: "Off", "12:05 OK", "Connected" */
+/* The mapped fields' values and the message (spec §12.5, §12.7), kept in RTC memory through deep sleep: a
+ * routine wake keeps them, anything else reads the mappings again. */
+void app_mqtt_boot(bool warm);
+void app_mqtt_seal(void); /* before a deep sleep */
+void app_mqtt_clock_moved(int64_t delta_s); /* the clock moved: values keep their age */
+const ha_store_t *app_mqtt_store(void); /* as a render draws them */
+void app_mqtt_key(board_button_t button, gesture_t gesture); /* a dashboard gesture, for HA (spec §12.8) */
+bool app_mqtt_banner(void);  /* the message's banner shows (spec §12.7) */
+void app_mqtt_dismiss(void); /* KEY short took it away */
+/* The console (spec §15): `field set ha.message`, `field set|clear mqtt.<key>` and `mqtt status`. */
+void app_mqtt_set_message(const char *text);
+bool app_mqtt_set_value(const char *key, const char *text, char *err, size_t size);
+bool app_mqtt_clear_value(const char *key);
+void app_mqtt_print_status(void);
 
 /* The `field`, `preset` and `night` console commands (main/app_cmds.c); call after diag_start(). */
 void app_register_commands(void);
```


`main/app_mqtt.c`:

```diff
--- a/main/app_mqtt.c
+++ b/main/app_mqtt.c
@@ -1,36 +1,64 @@
 #include <stdio.h>
+#include <stdlib.h>
 #include <string.h>
 #include <time.h>
 
 #include "app.h"
 #include "app_internal.h"
 #include "esp_app_desc.h"
+#include "esp_attr.h"
 #include "esp_log.h"
 #include "esp_mac.h"
+#include "freertos/FreeRTOS.h"
+#include "freertos/semphr.h"
 #include "ha_mqtt.h"
 #include "ha_session.h"
+#include "ha_store.h"
 #include "lang.h"
 #include "netmgr.h"
 #include "nvs.h"
+#include "storage.h"
 #include "sync_plan.h"
 #include "timekeeping.h"
 
 /* MQTT and Home Assistant on the app's side (spec §12, D32): the client's hooks, the sync's step, sync mode
- * `always`'s connection and its state, and what the status bar and Info say about them. It belongs to the
- * app task, but for the hooks, which run on the client's and the sync's tasks and hand over to it. */
+ * `always`'s connection and its state, the mapped fields' values and the message, the key presses HA hears,
+ * and what the status bar and Info say about them. It belongs to the app task, but for the hooks, which run
+ * on the client's and the sync's tasks and hand over to it. */
 
 static const char *TAG = "app_mqtt";
 
 #define NVS_KEY_PASS "mqtt_pass" /* in `secrets`: write-only (spec §12.1) */
 #define CHECK_MS 30000           /* sync mode `always`: how often the state is looked at (spec §12.9) */
+#define RESUBSCRIBE_MS 300000    /* sync mode `always`: the retained values again, with the 5-min state */
 
 static bool s_started;           /* the client's task runs */
 static ha_conn_t s_conn;         /* the next session's: set on the app task as a sync starts */
 static bool s_keeping;           /* sync mode `always` keeps the client */
 static int64_t s_published_ms = -1;
 static int64_t s_check_ms;
+static int64_t s_resubscribe_ms;
+static bool s_state_now;         /* a command changed the state: it goes out at once (spec §12.4) */
 EXT_RAM_BSS_ATTR static char s_published[HA_STATE_MAX]; /* the last state that went out, its uptime left out */
 
+/* The values and the message through deep sleep (spec §12.5): RTC FAST memory, kept powered as the heap
+ * may use it (CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP), since the snapshot fills RTC SLOW. */
+static RTC_FAST_ATTR ha_store_t s_store;
+_Static_assert(sizeof(ha_store_t) <= 4096, "the MQTT values' RTC block is at most 4 KB");
+EXT_RAM_BSS_ATTR static ha_fields_t s_fields; /* the mappings, read once a boot when something needs them */
+EXT_RAM_BSS_ATTR static char s_fields_json[HA_FIELDS_JSON_MAX];
+static bool s_fields_loaded, s_fields_sent;
+
+/* Values from the client's task wait here for the app task, so a burst of retained values draws once. */
+typedef struct {
+    char key[HA_KEY_LEN];
+    ha_value_t v;
+} staged_t;
+static SemaphoreHandle_t s_stage_lock;
+EXT_RAM_BSS_ATTR static staged_t s_staged[HA_FIELDS_MAX];
+static int s_staged_count;
+static bool s_drain_posted;
+
 static const char *bat_state_name(uint8_t state)
 {
     static const char *const k_names[] = { "unknown", "discharging", "charging", "full" };
@@ -80,6 +108,69 @@ static uint32_t expected_s(void)
     return ha_expected_s(s->sync_mode == SETTINGS_SYNC_ALWAYS, app_sync_expected_s(), quiet > night ? quiet : night);
 }
 
+static bool parse_fields(const char *text, void *ctx)
+{
+    char err[96];
+    if (!ha_fields_from_json(text, ctx, err, sizeof(err))) {
+        ESP_LOGW(TAG, "%s: %s", STORAGE_MQTT_FIELDS_PATH, err);
+        return false;
+    }
+    return true;
+}
+
+/* The mappings in /fs/cfg/mqtt_fields.json (spec §12.5), once a boot; none if it is missing or invalid. */
+static void load_fields(void)
+{
+    if (s_fields_loaded) {
+        return;
+    }
+    s_fields_loaded = true;
+    bool from_backup = false;
+    esp_err_t err = storage_init(); /* not mounted yet after a routine deep-sleep wake */
+    if (err == ESP_OK) {
+        err = storage_load(STORAGE_MQTT_FIELDS_PATH, s_fields_json, sizeof(s_fields_json), parse_fields, &s_fields,
+                           &from_backup);
+    }
+    if (err == ESP_OK) {
+        ESP_LOGI(TAG, "%d MQTT fields%s", s_fields.count, from_backup ? ", from the backup" : "");
+    } else {
+        memset(&s_fields, 0, sizeof(s_fields)); /* a failed parse may have left it half-written */
+        if (err != ESP_ERR_NOT_FOUND) {
+            ESP_LOGW(TAG, "MQTT fields: %s; none", esp_err_to_name(err));
+        }
+    }
+}
+
+void app_mqtt_boot(bool warm)
+{
+    if (warm && ha_store_valid(&s_store)) {
+        return; /* a routine wake reads no file: the mappings load when a sync or the console needs them */
+    }
+    if (warm) {
+        ESP_LOGW(TAG, "the MQTT values' RTC block is invalid; starting it again");
+    }
+    ha_store_init(&s_store);
+    load_fields();
+    ha_store_rebuild(&s_store, &s_fields);
+}
+
+void app_mqtt_seal(void)
+{
+    ha_store_seal(&s_store);
+}
+
+void app_mqtt_clock_moved(int64_t delta_s)
+{
+    ha_store_shift_time(&s_store, delta_s); /* values that came before a sync set the clock keep their age */
+}
+
+const ha_store_t *app_mqtt_store(void)
+{
+    uint32_t expected = expected_s(); /* spec §12.5: twice the expected interval; never stale in manual mode */
+    ha_store_set_default_ttl(&s_store, expected <= UINT32_MAX / 2 ? expected * 2 : UINT32_MAX);
+    return &s_store;
+}
+
 /* The state (spec §12.2) as it is now; the strings point into the app's state. */
 static void build_state(ha_state_t *out)
 {
@@ -149,15 +240,120 @@ static bool on_payloads(ha_payloads_t *out)
     return app_execute(fill_payloads, out) == ESP_OK;
 }
 
+typedef struct {
+    ha_cmd_t cmd;
+    bool press;                /* the payload is a button's PRESS */
+    bool fits;                 /* the whole payload is in `text` */
+    char text[HA_MESSAGE_LEN]; /* a preset's id or name, or the message */
+} command_t;
+
+/* spec §12.4: on the app task, in the order the commands came. */
+static void apply_command(void *arg)
+{
+    command_t *c = arg;
+    const ui_presets_t *p = app_presets();
+    bool always = app_settings()->sync_mode == SETTINGS_SYNC_ALWAYS;
+    if ((c->cmd == HA_CMD_NEXT || c->cmd == HA_CMD_SYNC) && !c->press) {
+        ESP_LOGW(TAG, "command %d: \"%s\" isn't PRESS; ignored", c->cmd, c->text);
+    } else if (c->cmd == HA_CMD_PRESET) {
+        int i = c->fits ? ui_presets_lookup(p, c->text) : -1;
+        if (i < 0) {
+            ESP_LOGW(TAG, "cmd/preset: no preset \"%s\"; ignored", c->text);
+        } else {
+            ESP_LOGI(TAG, "cmd/preset: %s", p->presets[i].id);
+            app_ui_select(i, true); /* saved like a manual switch */
+            s_state_now = true;
+        }
+    } else if (c->cmd == HA_CMD_NEXT) {
+        app_ui_select(ui_presets_next(p, always), true); /* as KEY short (D32) */
+        ESP_LOGI(TAG, "cmd/next: %s", p->presets[p->active].id);
+        s_state_now = true;
+    } else if (c->cmd == HA_CMD_SYNC) {
+        if (app_sync_active()) { /* one that waited while the board slept comes in a sync (D32) */
+            ESP_LOGI(TAG, "cmd/sync: a sync runs; ignored");
+        } else if (!always) {
+            ESP_LOGI(TAG, "cmd/sync: only in sync mode always; ignored");
+        } else {
+            esp_err_t err = app_sync_now();
+            ESP_LOGI(TAG, "cmd/sync: %s", err == ESP_OK ? "a sync starts" : esp_err_to_name(err));
+        }
+    } else if (c->cmd == HA_CMD_MESSAGE) {
+        ha_store_set_message(&s_store, c->text, time(NULL));
+        ESP_LOGI(TAG, "cmd/message: %s", c->text[0] != '\0' ? c->text : "cleared");
+        app_ui_render();
+    }
+    if (s_state_now && s_keeping) {
+        s_check_ms = 0; /* sync mode `always`: the next tick publishes it; a sync's session does at its end */
+    }
+    free(c);
+}
+
 static void on_command(ha_cmd_t cmd, const char *payload, size_t len)
 {
-    ESP_LOGI(TAG, "command %d (%.*s): applied from M7's next step", cmd, (int)len, payload);
+    command_t *c = calloc(1, sizeof(*c));
+    if (c == NULL) {
+        ESP_LOGW(TAG, "no memory: command %d dropped", cmd);
+        return;
+    }
+    c->cmd = cmd;
+    c->press = ha_cmd_press(payload, len);
+    if (cmd == HA_CMD_MESSAGE) {
+        ha_message_text(payload, len, c->text, sizeof(c->text)); /* cut at a character (spec §12.7) */
+        c->fits = true;
+    } else {
+        c->fits = len < sizeof(c->text);
+        snprintf(c->text, sizeof(c->text), "%.*s", (int)(c->fits ? len : sizeof(c->text) - 1), payload);
+    }
+    if (app_post(apply_command, c) != ESP_OK) {
+        ESP_LOGW(TAG, "the app is busy: command %d dropped", cmd);
+        free(c);
+    }
+}
+
+/* On the app task: the values that came since the last time, then one render if any shows differently. */
+static void drain_values(void *arg)
+{
+    (void)arg;
+    EXT_RAM_BSS_ATTR static staged_t batch[HA_FIELDS_MAX];
+    xSemaphoreTake(s_stage_lock, portMAX_DELAY);
+    int n = s_staged_count;
+    memcpy(batch, s_staged, (size_t)n * sizeof(batch[0]));
+    s_staged_count = 0;
+    s_drain_posted = false;
+    xSemaphoreGive(s_stage_lock);
+    time_t now = time(NULL);
+    bool changed = false;
+    for (int i = 0; i < n; i++) {
+        changed |= ha_store_set(&s_store, ha_store_find(&s_store, batch[i].key), &batch[i].v, now);
+    }
+    if (changed) {
+        app_ui_render();
+    }
 }
 
 static void on_value(const char *key, const ha_value_t *v)
 {
-    (void)v;
-    ESP_LOGD(TAG, "value for %s", key);
+    xSemaphoreTake(s_stage_lock, portMAX_DELAY);
+    int i = 0;
+    while (i < s_staged_count && strcmp(s_staged[i].key, key) != 0) {
+        i++;
+    }
+    if (i == s_staged_count && i < HA_FIELDS_MAX) {
+        snprintf(s_staged[i].key, sizeof(s_staged[i].key), "%s", key);
+        s_staged_count++;
+    }
+    if (i < HA_FIELDS_MAX) {
+        s_staged[i].v = *v; /* the latest one wins */
+    }
+    bool post = !s_drain_posted;
+    s_drain_posted = true;
+    xSemaphoreGive(s_stage_lock);
+    if (post && app_post(drain_values, NULL) != ESP_OK) {
+        xSemaphoreTake(s_stage_lock, portMAX_DELAY);
+        s_drain_posted = false; /* the next value tries again */
+        xSemaphoreGive(s_stage_lock);
+        ESP_LOGW(TAG, "the app is busy: the values wait");
+    }
 }
 
 static void status_changed(void *arg)
@@ -171,19 +367,25 @@ static void on_status(void)
     app_post(status_changed, NULL);
 }
 
+/* The client's task, once, and the mappings it subscribes to, once a boot. */
 static void start(void)
 {
-    if (s_started) {
-        return;
+    if (!s_started) {
+        static const ha_hooks_t k_hooks = { .command = on_command, .value = on_value, .status = on_status,
+                                            .payloads = on_payloads };
+        s_stage_lock = s_stage_lock != NULL ? s_stage_lock : xSemaphoreCreateMutex();
+        esp_err_t err = s_stage_lock != NULL ? ha_mqtt_init(&k_hooks) : ESP_ERR_NO_MEM;
+        if (err != ESP_OK) {
+            ESP_LOGE(TAG, "the client didn't start: %s", esp_err_to_name(err));
+            return;
+        }
+        s_started = true;
     }
-    static const ha_hooks_t k_hooks = { .command = on_command, .value = on_value, .status = on_status,
-                                        .payloads = on_payloads };
-    esp_err_t err = ha_mqtt_init(&k_hooks);
-    if (err != ESP_OK) {
-        ESP_LOGE(TAG, "the client didn't start: %s", esp_err_to_name(err));
-        return;
+    if (!s_fields_sent) {
+        load_fields();
+        ha_mqtt_set_fields(&s_fields);
+        s_fields_sent = true;
     }
-    s_started = true;
 }
 
 void app_mqtt_prepare(void)
@@ -202,7 +404,7 @@ esp_err_t app_mqtt_sync_step(int budget_ms, char *detail, size_t size) /* on the
 }
 
 /* Sync mode `always` (spec §12.9): connected while it keeps Wi-Fi on the network; the state on a change, at
- * most every 30 s, and every 5 min. */
+ * most every 30 s, at once after a command, and every 5 min, with the retained values again. */
 void app_mqtt_tick(void)
 {
     bool want = app_mqtt_on() && app_sync_lan_ui();
@@ -216,6 +418,8 @@ void app_mqtt_tick(void)
         ha_mqtt_keep(&c);
         s_keeping = true;
         s_published_ms = -1;
+        s_state_now = false;
+        s_resubscribe_ms = app_uptime_ms() + RESUBSCRIBE_MS; /* connecting subscribes */
         ESP_LOGI(TAG, "sync mode always: connecting to %s", c.host);
     } else if (!want && s_keeping) {
         ha_mqtt_drop();
@@ -232,6 +436,10 @@ void app_mqtt_tick(void)
     if (!hs.connected) {
         return;
     }
+    if (now >= s_resubscribe_ms) {
+        ha_mqtt_resubscribe(); /* a value that doesn't change stays fresh */
+        s_resubscribe_ms = now + RESUBSCRIBE_MS;
+    }
     ha_state_t state;
     build_state(&state);
     EXT_RAM_BSS_ATTR static char json[HA_STATE_MAX], same[HA_STATE_MAX];
@@ -240,11 +448,12 @@ void app_mqtt_tick(void)
     still.has_rssi = false; /* the signal jitters: it goes out with the 5-min state, not as a change */
     ha_state_json(&still, same, sizeof(same));
     bool changed = strcmp(same, s_published) != 0;
-    if (ha_state_due(changed, s_published_ms < 0 ? -1 : (now - s_published_ms) / 1000)) {
+    if (s_state_now || ha_state_due(changed, s_published_ms < 0 ? -1 : (now - s_published_ms) / 1000)) {
         ha_state_json(&state, json, sizeof(json));
         ha_mqtt_publish_state(json);
         memcpy(s_published, same, sizeof(s_published));
         s_published_ms = now;
+        s_state_now = false;
     }
 }
 
@@ -276,6 +485,60 @@ void app_mqtt_password_changed(void)
     }
 }
 
+/* spec §12.8: while connected, a dashboard gesture goes to HA too; at other times nothing is sent. */
+void app_mqtt_key(board_button_t button, gesture_t gesture)
+{
+    if (!s_started || gesture < GESTURE_SHORT || gesture > GESTURE_LONG) {
+        return;
+    }
+    ha_press_t press = gesture == GESTURE_SHORT ? HA_PRESS_SHORT : gesture == GESTURE_DOUBLE ? HA_PRESS_DOUBLE
+                                                                                          : HA_PRESS_LONG;
+    ha_mqtt_publish_action(ha_action_payload(button == BOARD_BUTTON_BOOT, press));
+}
+
+bool app_mqtt_banner(void)
+{
+    return ha_store_banner(&s_store, time(NULL));
+}
+
+void app_mqtt_dismiss(void)
+{
+    ha_store_dismiss(&s_store);
+    ESP_LOGI(TAG, "KEY short: the message's banner dismissed");
+}
+
+void app_mqtt_set_message(const char *text)
+{
+    char clean[HA_MESSAGE_LEN];
+    ha_message_text(text, strlen(text), clean, sizeof(clean));
+    ha_store_set_message(&s_store, clean, time(NULL));
+}
+
+/* As a payload would bring it at the mapping's path (spec §15): the console's `field set mqtt.<key>`. */
+bool app_mqtt_set_value(const char *key, const char *text, char *err, size_t size)
+{
+    load_fields();
+    int m = ha_fields_find(&s_fields, key), i = ha_store_find(&s_store, key);
+    if (m < 0 || i < 0) {
+        snprintf(err, size, "no MQTT field \"%s\" (see `mqtt status`)", key);
+        return false;
+    }
+    ha_field_t f = s_fields.field[m];
+    f.json_path[0] = '\0';
+    ha_value_t v;
+    if (!ha_value_parse(&f, text, strlen(text), &v)) {
+        snprintf(err, size, "mqtt.%s takes a %s", key, f.kind == HA_KIND_NUMBER ? "number" : "text");
+        return false;
+    }
+    ha_store_set(&s_store, i, &v, time(NULL));
+    return true;
+}
+
+bool app_mqtt_clear_value(const char *key)
+{
+    return ha_store_clear(&s_store, ha_store_find(&s_store, key));
+}
+
 bool app_mqtt_failed(void)
 {
     if (!app_mqtt_on()) {
@@ -317,3 +580,74 @@ void app_mqtt_summary(char *out, size_t size)
     lang_format_time(local.tm_hour, local.tm_min, 0, app_settings()->clock_24h, false, when, sizeof(when), &suffix);
     snprintf(out, size, "%s%s%s %s", when, suffix[0] ? " " : "", suffix, r == SYNC_STEP_OK ? "OK" : st->mqtt_detail);
 }
+
+static bool password_set(void)
+{
+    nvs_handle_t nvs;
+    size_t len = 0;
+    bool set = false;
+    if (nvs_open("secrets", NVS_READONLY, &nvs) == ESP_OK) {
+        set = nvs_get_str(nvs, NVS_KEY_PASS, NULL, &len) == ESP_OK && len > 1;
+        nvs_close(nvs);
+    }
+    return set;
+}
+
+/* `mqtt status` (spec §15): the settings, the client, the last session, the mappings and the message. */
+void app_mqtt_print_status(void)
+{
+    const settings_t *s = app_settings();
+    printf("mqtt %s, broker %s:%u, user \"%s\", password %s, discovery %s (prefix %s)\n",
+           s->mqtt_enabled ? "on" : "off", s->mqtt_host[0] ? s->mqtt_host : "-", s->mqtt_port, s->mqtt_user,
+           password_set() ? "set" : "none", s->mqtt_discovery ? "on" : "off", s->mqtt_prefix);
+    ha_mqtt_status_t hs;
+    ha_mqtt_status(&hs);
+    char summary[48];
+    app_mqtt_summary(summary, sizeof(summary));
+    printf("client: %s, %s%s%s; last: %s\n",
+           !s_started  ? "not started"
+           : s_keeping ? "kept connected (sync mode always)"
+                       : "a session in each sync",
+           hs.connected ? "connected" : "not connected", hs.detail[0] ? ": " : "", hs.detail, summary);
+    if (hs.test_running || hs.test_done) {
+        printf("test: %s%s\n", hs.test_running ? "running" : hs.test_ok ? "connected" : "failed: ",
+               hs.test_running || hs.test_ok ? "" : hs.test_detail);
+    }
+    load_fields();
+    const ha_store_t *st = app_mqtt_store();
+    printf("%d of %d fields mapped; values stale by default after %lu s%s\n", s_fields.count, HA_FIELDS_MAX,
+           (unsigned long)st->default_ttl_s, st->default_ttl_s == 0 ? " (never)" : "");
+    time_t now = time(NULL);
+    for (int i = 0; i < s_fields.count; i++) {
+        const ha_field_t *f = &s_fields.field[i];
+        printf("  mqtt.%-23s %-6s %s%s%s: ", f->key, f->kind == HA_KIND_NUMBER ? "number" : "text", f->topic,
+               f->json_path[0] ? " at " : "", f->json_path);
+        int k = ha_store_find(st, f->key);
+        if (k < 0 || st->entry[k].updated == 0) {
+            printf("no value\n");
+            continue;
+        }
+        const ha_entry_t *e = &st->entry[k];
+        char value[HA_TEXT_LEN];
+        if (e->kind == HA_KIND_NUMBER) {
+            lang_format_decimal(lang_get("en"), e->number, e->decimals, value, sizeof(value));
+        } else {
+            snprintf(value, sizeof(value), "%s", e->text);
+        }
+        printf("%s%s%s, %lu s old%s\n", value, e->unit[0] ? " " : "", e->unit,
+               (unsigned long)(now > (time_t)e->updated ? now - (time_t)e->updated : 0),
+               ha_store_freshness(st, k, now) == HA_STALE ? ", stale" : "");
+    }
+    uint32_t disc = ha_mqtt_discovery_hash();
+    printf("discovery: %s", disc != 0 ? "sent, hash " : "not sent yet\n");
+    if (disc != 0) {
+        printf("%08lx\n", (unsigned long)disc);
+    }
+    if (st->message_at == 0) {
+        printf("message: none\n");
+    } else {
+        printf("message: \"%s\", %lu s old, banner %s\n", st->message,
+               (unsigned long)(now > (time_t)st->message_at ? now - (time_t)st->message_at : 0),
+               ha_store_banner(st, now) ? "shown" : st->message_dismissed ? "dismissed" : "over");
+    }
+}
```


`main/app.c`:

```diff
--- a/main/app.c
+++ b/main/app.c
@@ -256,6 +256,7 @@ static void on_tick(bool force, bool rtc_edge)
 void app_clock_moved(int64_t delta_s)
 {
     sensors_shift_time(delta_s); /* the battery history keeps its spacing on the new clock */
+    app_mqtt_clock_moved(delta_s); /* and the MQTT values their age (a sync sets the clock after its session) */
     app_state()->sched_checked = time(NULL); /* entries the jump skipped don't run late */
     app_state()->cycle_at = 0; /* the next tick starts the cycle interval again, rather than switching at once */
     app_sync_schedule(); /* the next sync by the new clock */
@@ -268,7 +269,44 @@ static const char *preset_name(void)
     return p->presets[p->active].name;
 }
 
-/* Dashboard bindings (spec §5.6); the menu has its own while it is open. */
+/* The dashboard's bindings (spec §5.6). */
+static void dashboard_button(board_button_t button, gesture_t gesture)
+{
+    const lang_t *lang = lang_get(app_settings()->language);
+    char text[64];
+    if (button == BOARD_BUTTON_KEY && gesture == GESTURE_SHORT) {
+        app_ui_select(ui_presets_next(app_presets(), app_state()->settings.sync_mode == SETTINGS_SYNC_ALWAYS), true);
+        snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_PRESET), preset_name());
+        app_ui_toast(text);
+    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_DOUBLE) {
+        app_ui_toggle_cycle();
+        app_ui_toast(lang_str(lang, app_presets()->cycle_enabled ? LS_T_CYCLE_ON : LS_T_CYCLE_OFF));
+    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_LONG) {
+        app_menu_open();
+    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT &&
+               app_presets()->presets[app_presets()->active].layout == UI_LAYOUT_RADAR) {
+        if (app_radar_loop_start()) {
+            ESP_LOGI(TAG, "BOOT short: the radar's loop"); /* D28 */
+        } else {
+            ESP_LOGI(TAG, "BOOT short: a sync for a fresh frame"); /* D30: no hour to play */
+            app_sync_now_toast();
+        }
+    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT) {
+        app_ui_sample(time(NULL));
+        app_ui_render();
+        ESP_LOGI(TAG, "BOOT short: sensors refreshed");
+    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_DOUBLE) { /* spec §5.6, D31 */
+        if (app_menu_closed_within(MENU_SETTLE_MS)) {
+            ESP_LOGI(TAG, "BOOT double just after the menu closed: ignored"); /* the presses that backed out */
+        } else {
+            app_sync_toggle_always();
+        }
+    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_LONG) {
+        app_config_enter(); /* 3 s on the dashboard (spec §5.6) */
+    }
+}
+
+/* Button gestures: the menu's, config mode's and the special screens' bindings, or the dashboard's. */
 static void handle_button(board_button_t button, gesture_t gesture)
 {
     power_hold_awake_ms(GRACE_MS);
@@ -276,8 +314,6 @@ static void handle_button(board_button_t button, gesture_t gesture)
         s_peek_until_ms = app_uptime_ms() + PEEK_MS;
         power_hold_awake_ms(PEEK_MS);
     }
-    const lang_t *lang = lang_get(app_settings()->language);
-    char text[64];
     if (button != BOARD_BUTTON_BOOT || gesture != GESTURE_SHORT) {
         app_radar_loop_stop(); /* spec §11.2: KEY short still switches the preset */
     }
@@ -303,35 +339,12 @@ static void handle_button(board_button_t button, gesture_t gesture)
                 app_menu_open();
             }
         }
-    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_SHORT) {
-        app_ui_select(ui_presets_next(app_presets(), app_state()->settings.sync_mode == SETTINGS_SYNC_ALWAYS), true);
-        snprintf(text, sizeof(text), "%s: %s", lang_str(lang, LS_T_PRESET), preset_name());
-        app_ui_toast(text);
-    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_DOUBLE) {
-        app_ui_toggle_cycle();
-        app_ui_toast(lang_str(lang, app_presets()->cycle_enabled ? LS_T_CYCLE_ON : LS_T_CYCLE_OFF));
-    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_LONG) {
-        app_menu_open();
-    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT &&
-               app_presets()->presets[app_presets()->active].layout == UI_LAYOUT_RADAR) {
-        if (app_radar_loop_start()) {
-            ESP_LOGI(TAG, "BOOT short: the radar's loop"); /* D28 */
-        } else {
-            ESP_LOGI(TAG, "BOOT short: a sync for a fresh frame"); /* D30: no hour to play */
-            app_sync_now_toast();
-        }
-    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_SHORT) {
-        app_ui_sample(time(NULL));
+    } else if (button == BOARD_BUTTON_KEY && gesture == GESTURE_SHORT && app_mqtt_banner()) {
+        app_mqtt_dismiss(); /* spec §12.7: that press does nothing else */
         app_ui_render();
-        ESP_LOGI(TAG, "BOOT short: sensors refreshed");
-    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_DOUBLE) { /* spec §5.6, D31 */
-        if (app_menu_closed_within(MENU_SETTLE_MS)) {
-            ESP_LOGI(TAG, "BOOT double just after the menu closed: ignored"); /* the presses that backed out */
-        } else {
-            app_sync_toggle_always();
-        }
-    } else if (button == BOARD_BUTTON_BOOT && gesture == GESTURE_LONG) {
-        app_config_enter(); /* 3 s on the dashboard (spec §5.6) */
+    } else {
+        app_mqtt_key(button, gesture); /* spec §12.8: HA hears it too while MQTT is connected */
+        dashboard_button(button, gesture);
     }
     schedule_next();
 }
@@ -443,6 +456,7 @@ static bool prepare_deep_sleep(void)
     display_export(&s_snap.display);
     app_ui_export(&s_snap.ui);
     s_snap.next_alarm = s_next_alarm;
+    app_mqtt_seal();
     util_snapshot_seal(&s_snap, sizeof(s_snap), SNAP_MAGIC, SNAP_VERSION);
     esp_err_t err = display_prepare_deep_sleep();
     if (err != ESP_OK) {
@@ -589,6 +603,7 @@ static esp_err_t boot(void)
         app_ui_load();
         app_ui_restore_forecast(); /* spec §6: shown as stale by its age */
     }
+    app_mqtt_boot(warm); /* the MQTT fields' values and the message (spec §12.5) */
 
     ESP_RETURN_ON_ERROR(board_init(wake == POWER_WAKE_COLD), TAG, "board");
     ESP_RETURN_ON_ERROR(pcf85063_init(board_i2c()), TAG, "RTC");
```


`main/app_ui.c`:

```diff
--- a/main/app_ui.c
+++ b/main/app_ui.c
@@ -226,7 +226,8 @@ void app_ui_context(ui_context_t *ctx)
     localtime_r(&now, &ctx->local);
     ctx->local_day = local_day(&ctx->local);
     ctx->radar = app_radar_ui(); /* M6 */
-    ctx->mqtt_keys = &s.presets.mqtt; /* M7: the store comes with its task */
+    ctx->mqtt = app_mqtt_store(); /* M7 */
+    ctx->mqtt_keys = &s.presets.mqtt;
 }
 
 void app_ui_render(void)
@@ -254,6 +255,9 @@ void app_ui_render(void)
         ui_draw_first_run(fb, &ctx);
     } else {
         ui_draw_dashboard(fb, &ctx, &s.presets.presets[s.presets.active]);
+        if (!app_config_active()) { /* spec §12.7: not in config mode, even on its dashboard (D20) */
+            ui_draw_message_banner(fb, &ctx);
+        }
     }
     if (app_ui_toast_active()) {
         ui_draw_toast(fb, s_toast);
```


`main/app_cmds.c`:

```diff
--- a/main/app_cmds.c
+++ b/main/app_cmds.c
@@ -12,7 +12,7 @@
 #include "ui_fields.h"
 #include "ui_layout.h"
 
-/* `field` and `preset` (spec §15): inspect the dashboard and inject test data on the device. */
+/* `field`, `preset` and the others (spec §15): inspect the dashboard and inject test data on the device. */
 
 static const char *TAG = "app_cmds";
 
@@ -22,22 +22,78 @@ static int usage(const char *text)
     return 1;
 }
 
+static void print_value(const char *id, const ui_value_t *v)
+{
+    const char *state = v->state == UI_VALUE_FRESH ? "fresh" : v->state == UI_VALUE_STALE ? "stale" : "missing";
+    printf("%-13s %-7s %s%s%s", id, state, v->text, v->unit[0] ? " " : "", v->unit);
+    if (v->extra[0]) {
+        printf(" (%s)", v->extra);
+    }
+    if (v->state == UI_VALUE_STALE) {
+        printf(", %lu s old", (unsigned long)v->age_s);
+    }
+    if (v->trend) {
+        printf(", %s", v->trend > 0 ? "rising" : "falling");
+    }
+    printf("\n");
+}
+
 static void print_field(const ui_context_t *ctx, ui_field_id_t field)
 {
     ui_value_t v;
     ui_resolve(ctx, field, &v);
-    const char *state = v.state == UI_VALUE_FRESH ? "fresh" : v.state == UI_VALUE_STALE ? "stale" : "missing";
-    printf("%-13s %-7s %s%s%s", ui_field_info(field)->id, state, v.text, v.unit[0] ? " " : "", v.unit);
-    if (v.extra[0]) {
-        printf(" (%s)", v.extra);
+    print_value(ui_field_info(field)->id, &v);
+}
+
+/* mqtt.<key> (spec §12.5): store entry `i`, whether a preset shows it or not. */
+static void print_mqtt(const ui_context_t *ctx, int i)
+{
+    ui_value_t v = { 0 };
+    ui_mqtt_value(ctx, i, &v);
+    char id[8 + HA_KEY_LEN];
+    snprintf(id, sizeof(id), "mqtt.%s", ctx->mqtt->entry[i].key);
+    print_value(id, &v);
+}
+
+/* The words from argv[from] on, one space between them: a message or a text value needs no quotes. */
+static void join_args(int argc, char **argv, int from, char *out, size_t size)
+{
+    size_t at = 0;
+    out[0] = '\0';
+    for (int i = from; i < argc && at < size; i++) {
+        at += (size_t)snprintf(out + at, size - at, "%s%s", i > from ? " " : "", argv[i]);
     }
-    if (v.state == UI_VALUE_STALE) {
-        printf(", %lu s old", (unsigned long)v.age_s);
+}
+
+/* `field get|set|clear mqtt.<key>`: a value as its topic would bring it (spec §15). */
+static int mqtt_field(ui_context_t *ctx, int argc, char **argv, const char *usage_text)
+{
+    const char *key = argv[2] + 5;
+    int i = ha_store_find(ctx->mqtt, key);
+    if (i < 0) {
+        printf("field: no MQTT field \"%s\" (see `mqtt status`)\n", key);
+        return 1;
     }
-    if (v.trend) {
-        printf(", %s", v.trend > 0 ? "rising" : "falling");
+    if (argc == 3 && strcmp(argv[1], "get") == 0) {
+        print_mqtt(ctx, i);
+        return 0;
     }
-    printf("\n");
+    if (argc >= 4 && strcmp(argv[1], "set") == 0) {
+        char text[256], err[80];
+        join_args(argc, argv, 3, text, sizeof(text));
+        if (!app_mqtt_set_value(key, text, err, sizeof(err))) {
+            printf("field: %s\n", err);
+            return 1;
+        }
+    } else if (argc == 3 && strcmp(argv[1], "clear") == 0) {
+        app_mqtt_clear_value(key);
+    } else {
+        return usage(usage_text);
+    }
+    app_ui_render();
+    app_ui_context(ctx);
+    print_mqtt(ctx, i);
+    return 0;
 }
 
 /* Datastore units per console unit: 0.01 °C, 0.01 %, %, 0.1 d. */
@@ -55,11 +111,17 @@ static int field_body(int argc, char **argv)
         for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
             print_field(&ctx, (ui_field_id_t)f);
         }
+        for (int i = 0; i < ctx.mqtt->count; i++) {
+            print_mqtt(&ctx, i);
+        }
         return 0;
     }
     if (argc < 3) {
         return usage(k_usage);
     }
+    if (strncmp(argv[2], "mqtt.", 5) == 0) {
+        return mqtt_field(&ctx, argc, argv, k_usage);
+    }
     ui_field_id_t field = ui_field_by_name(argv[2]);
     if (field == UI_FIELD_NONE) {
         printf("field: no field \"%s\" (see `field list`)\n", argv[2]);
@@ -69,6 +131,17 @@ static int field_body(int argc, char **argv)
         print_field(&ctx, field);
         return 0;
     }
+    if (field == UI_FIELD_HA_MESSAGE && argc >= 3 && (strcmp(argv[1], "set") == 0 || strcmp(argv[1], "clear") == 0)) {
+        char text[256] = ""; /* spec §12.7: as Home Assistant's would come, a banner too */
+        if (strcmp(argv[1], "set") == 0) {
+            join_args(argc, argv, 3, text, sizeof(text));
+        }
+        app_mqtt_set_message(text);
+        app_ui_render();
+        app_ui_context(&ctx);
+        print_field(&ctx, field);
+        return 0;
+    }
     int ds_field = ui_field_info(field)->ds_field;
     bool set = argc == 4 && strcmp(argv[1], "set") == 0;
     bool clear = argc == 3 && strcmp(argv[1], "clear") == 0;
@@ -122,6 +195,21 @@ static int preset_body(int argc, char **argv)
     return usage(k_usage);
 }
 
+/* `mqtt status` (spec §15). */
+static int mqtt_body(int argc, char **argv)
+{
+    if (argc == 2 && strcmp(argv[1], "status") == 0) {
+        app_mqtt_print_status();
+        return 0;
+    }
+    return usage("mqtt status");
+}
+
+static int cmd_mqtt(int argc, char **argv)
+{
+    return diag_on_owner(mqtt_body, argc, argv);
+}
+
 static int cmd_field(int argc, char **argv)
 {
     return diag_on_owner(field_body, argc, argv);
@@ -409,6 +497,7 @@ void app_register_commands(void)
         { .command = "wifi", .help = "wifi status | scan", .func = &cmd_wifi },
         { .command = "sync", .help = "sync now | status (spec §9.3)", .func = &cmd_sync },
         { .command = "radar", .help = "radar status | loop (spec §11.2, §11.3)", .func = &cmd_radar },
+        { .command = "mqtt", .help = "mqtt status (spec §12)", .func = &cmd_mqtt },
     };
     for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
         esp_err_t err = esp_console_cmd_register(&cmds[i]);
```


`AGENTS.md`:

```diff
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -354,7 +354,7 @@ Use the cheapest level that proves the change. Any UI change needs at least leve
 
 **Screenshots**: the `screenshot` console command prints the canonical framebuffer as base64 PBM between `-----BEGIN RLCD PBM-----` and `-----END RLCD PBM-----`. `tools/screenshot.py` turns that into a PNG using only pyserial and the standard library. In config mode the web UI serves `/api/screenshot.bmp`, and `/api/preview.bmp` renders any preset with live data. A screenshot shows what the firmware drew, not what the panel shows, because the ST7305 is write-only. After any display-driver change, have the owner confirm the test pattern.
 
-**Diagnostics console** (`diag`; full list in spec §15). Available now: `help`, `version`, `heap`, `reboot`, `screenshot`, `panel status|test|clear|mode <hpm|lpm>|rate <0.25|0.5|1|2|4|8>|fps [s]|sleep|wake|init <factory|xiaozhi>`, `btn <key|boot> <short|double|long>` (simulated presses), `sensors`, `battery [learn start|stop]`, `rtc get|set <ISO 8601>`, `tasks`, `power idle [deep|light]`, `sleep stats [reset]|test <deep|light> <n>`, `field list|get <id>|set <id> <value>|clear <id>`, `preset list|set <id>`, `night <minutes>`, `schedule list|on|off|clear|add <HH:MM> preset <id> [days]|add <HH:MM> night <HH:MM> [days]`, `wifi status|scan`, `sync now|status`, `radar status|loop`; `rtc get` also prints the trim and the last drift. Planned: `audio tone`. Drive the UI, the menu included, with `btn` and `screenshot` instead of asking the owner to press buttons. A toast lasts 3 s, less than two port sessions take, so send `--cmd "btn key short" --cmd screenshot` in one devlog call and decode the log with `screenshot.extract_pbm()`. A `btn` gesture reaches the app through the buttons task, so a command in the same devlog call can run before it has taken effect: check its result in a separate call, and end a call that sends one with a slower command (`sync status`, `screenshot`), as a devlog call ends when its last command answers, before the app logs what the gesture did. Inject test data with `field set`. Run commands with `tools/idf.sh exec python tools/devlog.py --cmd <command>`. The console runs in plain line mode on purpose: no history, arrow keys or tab completion, even in a terminal. It never sends escape-code queries that a script can't answer (spec §15, `components/diag/diag.c`).
+**Diagnostics console** (`diag`; full list in spec §15). Available now: `help`, `version`, `heap`, `reboot`, `screenshot`, `panel status|test|clear|mode <hpm|lpm>|rate <0.25|0.5|1|2|4|8>|fps [s]|sleep|wake|init <factory|xiaozhi>`, `btn <key|boot> <short|double|long>` (simulated presses), `sensors`, `battery [learn start|stop]`, `rtc get|set <ISO 8601>`, `tasks`, `power idle [deep|light]`, `sleep stats [reset]|test <deep|light> <n>`, `field list|get <id>|set <id> <value>|clear <id>`, `preset list|set <id>`, `night <minutes>`, `schedule list|on|off|clear|add <HH:MM> preset <id> [days]|add <HH:MM> night <HH:MM> [days]`, `wifi status|scan`, `sync now|status`, `radar status|loop`, `mqtt status`; `rtc get` also prints the trim and the last drift. `field` also takes `ha.message` (`field set ha.message <text>` raises the banner as Home Assistant's message would; `clear` takes it away) and `mqtt.<key>` for every mapping on the MQTT page, set as its topic's payload would bring it. Planned: `audio tone`. Drive the UI, the menu included, with `btn` and `screenshot` instead of asking the owner to press buttons. A toast lasts 3 s, less than two port sessions take, so send `--cmd "btn key short" --cmd screenshot` in one devlog call and decode the log with `screenshot.extract_pbm()`. A `btn` gesture reaches the app through the buttons task, so a command in the same devlog call can run before it has taken effect: check its result in a separate call, and end a call that sends one with a slower command (`sync status`, `screenshot`), as a devlog call ends when its last command answers, before the app logs what the gesture did. Inject test data with `field set`. Run commands with `tools/idf.sh exec python tools/devlog.py --cmd <command>`. The console runs in plain line mode on purpose: no history, arrow keys or tab completion, even in a terminal. It never sends escape-code queries that a script can't answer (spec §15, `components/diag/diag.c`).
 
 **Done** means: the acceptance criteria pass at the right level, new logic has tests, power-affecting changes have measurements in `docs/power.md`, and this file and `docs/` are updated, the user guide (`docs/guide.md`, its images and `README.md`) included.
 
```


- [ ] **Step 5: Run the tests, and build.** The ASan/UBSan build too: it refuses `sprintf`.

Run: `cmake --build build-host && ctest --test-dir build-host | tail -3; cmake --build build-host-asan >/dev/null && ctest --test-dir build-host-asan | tail -3; tools/idf.sh build 2>&1 | grep -c 'warning:'; tools/idf.sh size | grep -E 'RTC (SLOW|FAST)'`
Expected: `100% tests passed, 0 tests failed out of 65` twice; `0`; `RTC SLOW` 5 008 bytes used of 8 192 (the snapshot, 4 856), `RTC FAST` 4 148 (the values' block, 3 968).

- [ ] **Step 6: Commit.**

```bash
git add components main AGENTS.md test/host
git commit -m "feat(app): MQTT commands, values, the message banner and key presses (spec §12.4-§12.8)"
```

### Task 10: The MQTT page and its API (`main`, `webui`, `web`)

**Files:**
- Modify: `components/webui/include/webui.h`, `components/webui/webui.c`, `main/app_internal.h`, `main/app_mqtt.c`, `main/app_web.c`, `web/app.js`, `web/index.html`, `test/web/test_app.mjs`

**Interfaces:**
- Consumes: `settings_patch_secret()` (Task 1); `ha_fields_from_json()`, `ha_fields_to_json()`, `HA_FIELDS_JSON_MAX` (Task 2); `ha_mqtt_test()`, `ha_mqtt_status_t` (Task 7); `app_sync_state_t.mqtt_detail` (Task 8); Task 9's store and mappings; the page's `api()`, `busy()`, `field()`, `card()`, `facts()`, `when()`, `duration()`.
- Produces:
  - `GET /api/mqtt_fields`, `PUT /api/mqtt_fields` (validated as the file is), `POST /api/mqtt/test` (202; 409 without a host, or while the board has only its own network);
  - `mqtt` in `GET /api/status`: `enabled`, `keep`, `connected`, `password_set`, `detail` (a kept connection's failure), `last` (`at`, `result` "ok" or "failed", `detail`), `test` (`running`, `ok`, `detail`);
  - `PATCH /api/settings` with `mqtt.password` (a text, or `null` or `""` to forget it) into NVS `secrets/mqtt_pass`;
  - `mqtt_fields.json` in the backup, validated with the rest before a restore replaces anything;
  - `app_mqtt_status_t`, `void app_mqtt_status(app_mqtt_status_t *out)`, `esp_err_t app_mqtt_set_password(const char *password)`, `size_t app_mqtt_fields_json(char *out, size_t size)`, `esp_err_t app_mqtt_replace_fields(const ha_fields_t *f)`, `esp_err_t app_mqtt_test(void)`;
  - `WEBUI_BODY_MAX` and `WEBUI_REPLY_MAX` of 40 KB;
  - the page's `mqttPage()` at `#mqtt`, and the Sync page's MQTT step.

The MQTT page (spec §10.3) has three cards. Broker: on or off, host, port, user, a write-only password (left empty it stays; "Forget the saved password" clears it), discovery and its prefix, each checked as the device checks it before anything is sent: the device keeps the old value of one it can't take, so a page that let `mqtt://ha.local` through would say "Saved" over a broker it never set. Connection: the last session, the kept connection in sync mode `always`, and Test connection, which follows the test in `GET /api/status` until it ends, about 10 s at most. Fields: each mapping with its key, label, kind, unit, decimals, topic, JSON path and how long its value stays fresh, its last value from `GET /api/fields`, Remove and Add a field, and the note that publishers must retain their messages or go through HA's statestream. The page checks keys, topics and paths as the device does (Task 2), so a save the page allows is one the device takes. The Sync page lists the MQTT step and says when it failed without failing the sync ("05:30 today, but MQTT failed (no broker)"). The preset editor shows a slot's `mqtt.<key>` that no mapping names as "mqtt.<key> (no mapping)" rather than an empty slot, and keeps it.

The password goes to NVS and is never returned or logged: a settings reply has no `mqtt.password`, and `GET /api/status` says only whether one is set; the server zeroes a changing request's body once it is answered, as it does for the web password. A backup carries the mappings (a bundle at its largest is 2 KB of settings, up to 20 KB of presets and 12 KB of mappings), so requests and replies grow to 40 KB, in PSRAM (the `_Static_assert` in `app_web.c` checks the sum). The M6b review's minor about `webui.h`'s stale size comment goes with it.

- [ ] **Step 1: Write the failing page tests.**

`test/web/test_app.mjs`:

```diff
--- a/test/web/test_app.mjs
+++ b/test/web/test_app.mjs
@@ -202,6 +202,27 @@ test('Undo changes asks before it drops the edits', async () => {
   assert.match(text(main), /Weather copy/);
 });
 
+/* spec §12.5: a slot keeps an mqtt.<key> no mapping names; the editor shows it rather than an empty slot. */
+test('a slot whose MQTT key has no mapping shows it, and keeps it', async () => {
+  const saved = [];
+  const routes = presetDevice(saved);
+  const doc = JSON.parse(JSON.stringify(await (await routes['GET /api/presets']()).json()));
+  doc.presets[1].slots.s1 = 'mqtt.gone';
+  routes['GET /api/presets'] = () => reply(200, JSON.parse(JSON.stringify(doc)));
+  routes['GET /api/layouts'] = () => reply(200, { ...CATALOGUE, layouts: [{ id: 'classic', slots: [
+    CATALOGUE.layouts[0].slots[0], { ...CATALOGUE.layouts[0].slots[1], kinds: ['number', 'text'] }] }] });
+  const { ctx, main } = await load(routes);
+  await ctx.presetsPage(); /* Weather, the active one */
+  const opts = below(main).filter((e) => e.tag === 'option' && e.value === 'mqtt.gone');
+  assert.equal(opts.length, 1);
+  assert.equal(text(opts[0]), 'mqtt.gone (no mapping)');
+  assert.ok(opts[0].selected);
+  await buttonNamed(main, 'New preset').click();
+  await buttonNamed(main, 'Save').click();
+  await settle();
+  assert.equal(saved.at(-1).presets.find((p) => p.id === 'weather').slots.s1, 'mqtt.gone');
+});
+
 test('the preview names each slot where the layout puts it', async () => {
   const { ctx, main } = await load(presetDevice([]));
   await ctx.presetsPage();
@@ -644,3 +665,170 @@ test('a cell says when its field draws at a smaller size than the cell\'s', asyn
   await ctx.presetsPage();
   assert.deepEqual(cellLabels(main), ['1 · 400×209 · XL (Next hours at M)', '2 · 400×69 · S']);
 });
+
+/* ---- MQTT and Home Assistant (spec §12, D32) ---- */
+
+const MQTT_SETTINGS = { schema: 1, sync: { mode: 'times' },
+                        mqtt: { enabled: true, host: 'ha.local', port: 1883, user: 'reflbo', discovery: true,
+                                discovery_prefix: 'homeassistant' } };
+const OUTSIDE = { key: 'outdoor_temp', label: 'Outside', kind: 'number', unit: '°C', precision: 1,
+                  topic: 'ha/statestream/sensor/outdoor_temperature/state', json_path: null, ttl_s: 0 };
+const mqttStatus = (mqtt) => ({ device: {}, time: { valid: true }, battery: {}, sensors: {}, preset: {},
+                                sync: { mode: 'times', running: false }, wifi: { state: 'station' },
+                                mqtt: { enabled: true, connected: false, keep: false, password_set: true, ...mqtt } });
+
+function mqttDevice({ patches = [], puts = [], tests = [], status = () => mqttStatus({}), fields = [OUTSIDE] } = {}) {
+  return {
+    'GET /api/settings': () => reply(200, MQTT_SETTINGS),
+    'GET /api/status': () => reply(200, status()),
+    'GET /api/mqtt_fields': () => reply(200, { schema: 1, fields: JSON.parse(JSON.stringify(fields)) }),
+    'GET /api/fields': () => reply(200, { fields: [{ id: 'mqtt.outdoor_temp', kind: 'number', label: 'Outside',
+                                                     value: '12.5 °C', state: 'fresh' }] }),
+    'PATCH /api/settings': (init) => { patches.push(JSON.parse(init.body)); return reply(200, MQTT_SETTINGS); },
+    'PUT /api/mqtt_fields': (init) => { puts.push(JSON.parse(init.body)); return reply(200, JSON.parse(init.body)); },
+    'POST /api/mqtt/test': () => { tests.push(true); return reply(202, { started: true }); },
+  };
+}
+
+const mappings = (main) => below(main).filter((e) => e.className === 'mapping');
+
+test('the MQTT page keeps the saved password unless one is typed', async () => {
+  const patches = [];
+  const { ctx, main } = await load(mqttDevice({ patches }));
+  await ctx.mqttPage();
+  control(main, 'Host').value = 'mqtt.lan';
+  await buttonNamed(main, 'Save broker').click();
+  assert.deepEqual(patches.at(-1), { mqtt: { enabled: true, host: 'mqtt.lan', port: 1883, user: 'reflbo',
+                                             discovery: true, discovery_prefix: 'homeassistant' } });
+  control(main, 'Password').value = 'n3w-secret';
+  await buttonNamed(main, 'Save broker').click();
+  assert.equal(patches.at(-1).mqtt.password, 'n3w-secret');
+  assert.equal(control(main, 'Password').value, '', 'the field empties once saved');
+});
+
+test('the MQTT page can forget the saved password', async () => {
+  const patches = [];
+  const { ctx, main } = await load(mqttDevice({ patches }));
+  await ctx.mqttPage();
+  below(main).find((e) => e.tag === 'input' && e.attrs.name === 'forget').checked = true;
+  await buttonNamed(main, 'Save broker').click();
+  assert.equal(patches.at(-1).mqtt.password, null);
+});
+
+test('the MQTT page checks the broker before it saves', async () => {
+  const patches = [];
+  const { ctx, main } = await load(mqttDevice({ patches }));
+  await ctx.mqttPage();
+  for (const [label, value, error] of [['Host', '', /needs the broker's host/], ['Host', 'mqtt://ha.local', /Host: /],
+                                       ['Host', 'my_broker', /Host: /], ['Host', '192.168.1.10:1883', /Host: /],
+                                       ['User', 'u'.repeat(64), /User: /], ['User', 'tab\there', /User: /],
+                                       ['Port', '70000', /Port: 1 to 65535/], ['Discovery prefix', 'ha/', /Discovery prefix/]]) {
+    const el = control(main, label), before = el.value;
+    el.value = value;
+    await buttonNamed(main, 'Save broker').click();
+    assert.match(text(main), error);
+    el.value = before;
+  }
+  assert.equal(patches.length, 0);
+});
+
+test('Test connection follows the test to its result', async () => {
+  let polls = 0;
+  const tests = [];
+  const status = () => mqttStatus({ test: ++polls < 3 ? { running: true }
+    : { running: false, ok: false, detail: 'no broker' } });
+  const { ctx, main } = await load(mqttDevice({ tests, status }));
+  ctx.setTimeout = (fn) => { fn(); return 0; }; /* sleep() returns at once */
+  await ctx.mqttPage();
+  await buttonNamed(main, 'Test connection').click();
+  await settle();
+  assert.equal(tests.length, 1);
+  assert.match(text(main), /It couldn't connect: no broker/);
+});
+
+test('Test connection says why the device refused it', async () => {
+  const routes = mqttDevice();
+  routes['POST /api/mqtt/test'] = () => reply(409, { error: 'the device is on its own network only' });
+  const { ctx, main } = await load(routes);
+  await ctx.mqttPage();
+  await buttonNamed(main, 'Test connection').click();
+  await settle();
+  assert.match(text(main), /The device is on its own network only\./);
+});
+
+test('the MQTT page shows each field with its last value, and what publishers must do', async () => {
+  const { ctx, main } = await load(mqttDevice());
+  await ctx.mqttPage();
+  assert.equal(mappings(main).length, 1);
+  assert.match(text(mappings(main)[0]), /12\.5 °C/);
+  assert.match(text(main), /retain/);
+});
+
+test('a new field is saved with the others', async () => {
+  const puts = [];
+  const { ctx, main } = await load(mqttDevice({ puts }));
+  await ctx.mqttPage();
+  await buttonNamed(main, 'Add a field').click();
+  const row = mappings(main)[1];
+  control(row, 'Key').value = 'co2';
+  control(row, 'Label').value = 'CO2';
+  control(row, 'Unit').value = 'ppm';
+  control(row, 'Decimals').value = '0';
+  control(row, 'Topic').value = 'zigbee2mqtt/living_room';
+  control(row, 'JSON path').value = 'co2';
+  await buttonNamed(main, 'Save fields').click();
+  assert.deepEqual(puts.at(-1), { schema: 1, fields: [OUTSIDE, { key: 'co2', label: 'CO2', kind: 'number', unit: 'ppm',
+                                                                precision: 0, topic: 'zigbee2mqtt/living_room',
+                                                                json_path: 'co2', ttl_s: 0 }] });
+});
+
+test('the MQTT page refuses a key that is taken or malformed, and a topic with a wildcard', async () => {
+  const puts = [];
+  const { ctx, main } = await load(mqttDevice({ puts }));
+  await ctx.mqttPage();
+  await buttonNamed(main, 'Add a field').click();
+  const row = mappings(main)[1];
+  control(row, 'Topic').value = 't';
+  for (const [key, error] of [['outdoor_temp', /Two fields have the key outdoor_temp/], ['CO2', /a–z, 0–9 and _/],
+                              ['', /a–z, 0–9 and _/]]) {
+    control(row, 'Key').value = key;
+    await buttonNamed(main, 'Save fields').click();
+    assert.match(text(main), error);
+  }
+  control(row, 'Key').value = 'ok';
+  control(row, 'Topic').value = 'zigbee2mqtt/#';
+  await buttonNamed(main, 'Save fields').click();
+  assert.match(text(main), /without \+ or #/);
+  assert.equal(puts.length, 0);
+});
+
+test('Remove takes a field out of the mappings', async () => {
+  const puts = [];
+  const { ctx, main } = await load(mqttDevice({ puts }));
+  await ctx.mqttPage();
+  await buttonNamed(mappings(main)[0], 'Remove').click();
+  await buttonNamed(main, 'Save fields').click();
+  assert.deepEqual(puts.at(-1), { schema: 1, fields: [] });
+});
+
+test('a time to live the page has no option for is kept', async () => {
+  const puts = [];
+  const { ctx, main } = await load(mqttDevice({ puts, fields: [{ ...OUTSIDE, ttl_s: 5400 }] }));
+  await ctx.mqttPage();
+  assert.ok(options(control(mappings(main)[0], 'Stale after')).includes('5400'));
+  await buttonNamed(main, 'Save fields').click();
+  assert.equal(puts.at(-1).fields[0].ttl_s, 5400);
+});
+
+test('the Sync page shows the MQTT step and why it failed', async () => {
+  const last = { at: 1790880000, steps: { wifi: 'ok', time: 'ok', weather: 'ok', air: 'ok', radar: 'ok', mqtt: 'failed' } };
+  const status = syncStatus({ mode: 'times', running: false, last });
+  status.mqtt = { enabled: true, connected: false, last: { at: 1790880000, result: 'failed', detail: 'no broker' } };
+  const { ctx, main } = await load({
+    'GET /api/settings': () => reply(200, SYNC_SETTINGS),
+    'GET /api/status': () => reply(200, status),
+  });
+  await ctx.syncPage();
+  assert.match(text(main), /MQTTfailed: no broker/); /* the steps' list: its name, then its result */
+  assert.match(text(main), /but MQTT failed \(no broker\)/);
+});
```


Run: `node --test test/web/test_app.mjs 2>&1 | grep -E '^# (pass|fail)'`
Expected: `# pass 34`, `# fail 12`: ten with `ctx.mqttPage is not a function`, the Sync page's MQTT step missing, and the unmapped slot shown as empty.

- [ ] **Step 2: The page.**

`web/index.html`:

```diff
--- a/web/index.html
+++ b/web/index.html
@@ -18,6 +18,7 @@
   <a href="#place">Location &amp; time</a>
   <a href="#sync">Sync</a>
   <a href="#radar">Radar</a>
+  <a href="#mqtt">MQTT</a>
   <a href="#device">Device</a>
   <a href="#presets">Presets</a>
   <a href="#firmware">Firmware</a>
```


`web/app.js`:

```diff
--- a/web/app.js
+++ b/web/app.js
@@ -224,7 +224,7 @@ document.getElementById('done').onclick = async () => {
 /* ---- pages ---- */
 
 const pages = { status: statusPage, wifi: wifiPage, place: placePage, sync: syncPage, radar: radarPage,
-                device: devicePage, presets: presetsPage, firmware: firmwarePage, backup: backupPage };
+                mqtt: mqttPage, device: devicePage, presets: presetsPage, firmware: firmwarePage, backup: backupPage };
 
 function route() {
   const name = location.hash.slice(1) || 'status';
@@ -287,7 +287,7 @@ async function statusPage() {
       env.age_s !== undefined ? ['Measured', `${duration(env.age_s)} ago`] : null,
     ])),
     card('Wi-Fi', facts([['Now', wifiText(s.wifi)], s.wifi.ap_on ? ['On its network', `${s.wifi.ap_clients} device(s)`] : null])),
-    card('Sync', facts(syncFacts(s.sync)), actions(h('a', { class: 'btn', href: '#sync' }, 'Sync settings'))),
+    card('Sync', facts(syncFacts(s.sync, s.mqtt)), actions(h('a', { class: 'btn', href: '#sync' }, 'Sync settings'))),
     card('This page', passwordForm('Change password', null, async (password, old, note, form) => {
       await api('POST', '/api/auth/password', { old, password });
       form.reset();
@@ -307,7 +307,7 @@ async function statusPage() {
 /* ---- Sync (spec §9.3, D25) ---- */
 
 const SYNC_STEPS = [['wifi', 'Wi-Fi'], ['time', 'Time'], ['weather', 'Weather'], ['air', 'Air quality'],
-                    ['radar', 'Radar']];
+                    ['radar', 'Radar'], ['mqtt', 'MQTT']];
 const SYNC_INTERVALS = [15, 30, 60, 120, 180, 360, 720, 1440];
 const intervalLabel = (m) => (m < 60 ? `${m} min` : `${m / 60} h`);
 
@@ -320,14 +320,19 @@ function when(epoch) {
   return `${hm} ${day}`;
 }
 
-function syncFacts(sync) {
+/* A failed MQTT session doesn't fail the sync (D32): it is said apart. */
+const mqttFailed = (last, mqtt) => (last && last.steps && last.steps.mqtt === 'failed'
+  ? `MQTT failed (${((mqtt || {}).last || {}).detail || 'no reason given'})` : null);
+
+function syncFacts(sync, mqtt) {
   if (!sync) return [];
-  const last = sync.last;
+  const last = sync.last, m = mqttFailed(last, mqtt);
   return [
     ['Last sync', sync.running ? `running: ${(SYNC_STEPS.find(([k]) => k === sync.step) || [0, '…'])[1]}`
       : !last ? 'not since the device started'
-        : last.failed ? `${when(last.at)}: ${(SYNC_STEPS.find(([k]) => k === last.failed) || [0, last.failed])[1]} failed (${last.detail})`
-          : `${when(last.at)}, all well`],
+        : last.failed ? `${when(last.at)}: ${(SYNC_STEPS.find(([k]) => k === last.failed) || [0, last.failed])[1]} failed ` +
+            `(${last.detail})${m ? `; ${m}` : ''}`
+          : `${when(last.at)}, ${m ? `but ${m}` : 'all well'}`],
     ['Next', sync.next ? `${when(sync.next)}${sync.next_retry ? ', a retry' : ''}` : sync.mode === 'manual' ? 'when you ask' : '—'],
     sync.weather_at ? ['Weather from', when(sync.weather_at)] : null,
   ];
@@ -339,14 +344,16 @@ async function syncPage() {
   const steps = h('dl', { class: 'facts' });
   const showSteps = (status) => {
     const last = status.sync.last;
+    const why = (k) => (k === 'mqtt' ? ((status.mqtt || {}).last || {}).detail : last.failed === k ? last.detail : null);
     steps.replaceChildren(...SYNC_STEPS.flatMap(([k, name]) => [h('dt', { text: name }),
       h('dd', { class: !last ? '' : last.steps[k] === 'ok' ? 'good' : last.steps[k] === 'failed' ? 'bad' : 'muted',
-                text: !last ? '—' : last.steps[k] === 'failed' && last.failed === k ? `failed: ${last.detail}` : last.steps[k] })]));
+                text: !last ? '—' : last.steps[k] === 'failed' && why(k) ? `failed: ${why(k)}`
+                  : last.steps[k] || 'skipped' })]));
   };
   const summary = h('div');
   const rtc = h('p', { class: 'muted small' });
   const showStatus = (status) => {
-    summary.replaceChildren(facts(syncFacts(status.sync)));
+    summary.replaceChildren(facts(syncFacts(status.sync, status.mqtt)));
     showSteps(status);
     const r = status.time.rtc || {};
     rtc.textContent = `The clock chip's trim: ${r.trim_steps ?? 0} steps` +
@@ -762,6 +769,185 @@ async function radarPage() {
   await Promise.all([radarPreview(wxImg, 'radar'), radarPreview(flImg, 'flights')]).catch(() => {});
 }
 
+/* ---- MQTT and Home Assistant (spec §12, D32) ---- */
+
+const STALE_AFTER = [[0, 'Twice the sync interval'], [3600, '1 h'], [7200, '2 h'], [21600, '6 h'], [43200, '12 h'],
+                     [86400, '1 day'], [172800, '2 days'], [604800, '7 days'], [2592000, '30 days']];
+const DECIMALS = [['auto', 'As the value comes, up to 3'], ['0', 'None'], ['1', '1'], ['2', '2'], ['3', '3']];
+const MQTT_FIELDS_MAX = 32;
+
+/* The last session (a sync's), or the kept connection in sync mode Always on. */
+function mqttFacts(st) {
+  const m = st.mqtt || {}, last = m.last;
+  if (!m.enabled) return [['Now', 'off']];
+  return [
+    m.keep ? ['Now', m.connected ? 'connected' : `not connected${m.detail ? ` (${m.detail})` : ''}; it tries again`] : null,
+    ['Last session', !last ? 'none yet: one runs with each sync'
+      : last.result === 'ok' ? `${when(last.at)}, all well` : `${when(last.at)}: failed (${last.detail || 'no reason given'})`],
+  ];
+}
+
+/* One mapping (spec §12.5): its controls, and the value it brought last. */
+function mappingBox(f, value, onRemove) {
+  const select = (pairs, current) => {
+    const el = h('select', {}, pairs.map(([v, t]) => h('option', { value: v, selected: v === current }, t)));
+    el.value = current;
+    return el;
+  };
+  const key = h('input', { type: 'text', value: f.key || '', autocapitalize: 'off', spellcheck: 'false' });
+  const label = h('input', { type: 'text', value: f.label || '' });
+  const kind = select([['number', 'Number'], ['text', 'Text']], f.kind === 'text' ? 'text' : 'number');
+  const unit = h('input', { type: 'text', value: f.unit || '' });
+  const decimals = select(DECIMALS, typeof f.precision === 'number' ? String(f.precision) : 'auto');
+  const topic = h('input', { type: 'text', value: f.topic || '', autocapitalize: 'off', spellcheck: 'false' });
+  const path = h('input', { type: 'text', value: f.json_path || '', autocapitalize: 'off', spellcheck: 'false' });
+  const ttl = f.ttl_s || 0;
+  const stale = select([...STALE_AFTER, ...(STALE_AFTER.some(([s]) => s === ttl) ? [] : [[ttl, duration(ttl)]])]
+    .map(([s, t]) => [String(s), t]), String(ttl));
+  const box = h('div', { class: 'mapping' },
+    h('div', { class: 'row' }, h('div', {}, field('Key', key)), h('div', {}, field('Label', label))),
+    h('div', { class: 'row' }, h('div', {}, field('Kind', kind)), h('div', {}, field('Unit', unit)),
+      h('div', {}, field('Decimals', decimals))),
+    field('Topic', topic), field('JSON path', path, 'Keys joined by dots, such as co2 or state.temperature; empty ' +
+      'for the whole payload.'),
+    field('Stale after', stale),
+    h('p', { class: 'muted small', text: value ? `Last value: ${value.value || '—'}${value.state === 'stale' ? ', stale' : ''}`
+      : 'No value yet: it comes with the next sync.' }),
+    actions(button('Remove', onRemove)));
+  /* The mapping as mqtt_fields.json has it, checked as the device would (spec §12.5); `n` names it. */
+  box.read = (n) => {
+    const k = key.value.trim(), name = k || `number ${n}`;
+    if (!/^[a-z0-9_]{1,23}$/.test(k)) throw new ApiError(`Field ${name}: a key is 1 to 23 characters of a–z, 0–9 and _.`);
+    const t = topic.value.trim();
+    if (!t || bytes(t) > 127 || !/^[\x20-\x7e]+$/.test(t) || /["\\]/.test(t)) {
+      throw new ApiError(`Field ${k}: a topic of 1 to 127 plain characters.`);
+    }
+    if (/[+#]/.test(t)) throw new ApiError(`Field ${k}: a topic without + or #.`);
+    const pth = path.value.trim();
+    if (pth && (bytes(pth) > 47 || !/^[\x20-\x7e]+$/.test(pth) || /["\\]|^\.|\.$|\.\./.test(pth))) {
+      throw new ApiError(`Field ${k}: the JSON path is keys joined by dots, up to 47 characters.`);
+    }
+    if (bytes(label.value) > 23) throw new ApiError(`Field ${k}: a label of up to 23 bytes.`);
+    if (bytes(unit.value) > 7) throw new ApiError(`Field ${k}: a unit of up to 7 bytes.`);
+    return { key: k, label: label.value.trim() || k, kind: kind.value, unit: unit.value.trim(),
+             precision: decimals.value === 'auto' ? null : Number(decimals.value), topic: t, json_path: pth || null,
+             ttl_s: Number(stale.value) };
+  };
+  return box;
+}
+
+async function mqttPage() {
+  const [s, st, doc, cat] = await Promise.all([api('GET', '/api/settings'), api('GET', '/api/status'),
+    api('GET', '/api/mqtt_fields'), api('GET', '/api/fields')]);
+  const m = s.mqtt || {}, now = st.mqtt || {};
+
+  const enabled = h('input', { type: 'checkbox', checked: !!m.enabled });
+  const host = h('input', { type: 'text', value: m.host || '', autocapitalize: 'off', spellcheck: 'false' });
+  const port = h('input', { type: 'number', min: 1, max: 65535, value: m.port ?? 1883 });
+  const user = h('input', { type: 'text', value: m.user || '', autocapitalize: 'off', autocomplete: 'off' });
+  const password = h('input', { type: 'password', autocomplete: 'new-password' });
+  const forget = now.password_set ? h('input', { type: 'checkbox', name: 'forget' }) : null;
+  const discovery = h('input', { type: 'checkbox', checked: m.discovery !== false });
+  const prefix = h('input', { type: 'text', value: m.discovery_prefix || 'homeassistant', autocapitalize: 'off' });
+  const brokerNote = h('p');
+  const brokerCard = card('Broker',
+    h('label', { class: 'check' }, enabled, 'Connect to an MQTT broker'),
+    field('Host', host, 'Its name or address, such as homeassistant.local or 192.168.1.10.'), field('Port', port),
+    field('User', user), field('Password', password, now.password_set ? 'Saved on the device, which never shows it. Leave ' +
+      'it empty to keep it.' : 'None saved.'),
+    forget ? h('label', { class: 'check' }, forget, 'Forget the saved password') : null,
+    h('label', { class: 'check' }, discovery, 'Home Assistant discovery: the device appears in Home Assistant by itself'),
+    field('Discovery prefix', prefix, 'Home Assistant\'s, "homeassistant" unless you changed it there.'),
+    brokerNote,
+    actions(button('Save broker', () => busy(brokerCard, brokerNote, async () => {
+      const h_ = host.value.trim(), pr = prefix.value.trim(), po = Number(port.value);
+      if (enabled.checked && !h_) throw new ApiError('MQTT needs the broker\'s host.');
+      if (h_ && (bytes(h_) > 63 || !/^[A-Za-z0-9.-]+$/.test(h_))) { /* settings.c's host_name() */
+        throw new ApiError('Host: a name or an address, of letters, digits, dots and dashes; no mqtt:// or port.');
+      }
+      if (bytes(user.value.trim()) > 63 || /[\x00-\x1f\x7f]/.test(user.value)) {
+        throw new ApiError('User: up to 63 bytes, without control characters.');
+      }
+      if (!Number.isInteger(po) || po < 1 || po > 65535) throw new ApiError('Port: 1 to 65535.');
+      if (!pr || bytes(pr) > 31 || /^\/|\/$|[+#\s]/.test(pr) || !/^[\x21-\x7e]+$/.test(pr)) {
+        throw new ApiError('Discovery prefix: up to 31 characters, without spaces, + or #, and not starting or ' +
+          'ending with /.');
+      }
+      if (bytes(password.value) > 63) throw new ApiError('Password: up to 63 bytes.');
+      const patch = { enabled: enabled.checked, host: h_, port: po, user: user.value.trim(), discovery: discovery.checked,
+                      discovery_prefix: pr };
+      if (password.value) patch.password = password.value;
+      else if (forget && forget.checked) patch.password = null;
+      await api('PATCH', '/api/settings', { mqtt: patch });
+      password.value = '';
+      brokerNote.className = 'good';
+      brokerNote.textContent = 'Saved. The next sync connects with it.';
+      toast('Broker saved');
+    }), 'primary')));
+
+  const status = h('div', {}, facts(mqttFacts(st)));
+  const testNote = h('p');
+  const testCard = card('Connection', status,
+    h('p', { class: 'muted small', text: 'A session runs with each sync: it sends the device\'s state and takes ' +
+      'Home Assistant\'s commands and the fields\' values. In sync mode Always on the device stays connected. Test ' +
+      'connection tries the saved broker, which needs the device on your network.' }),
+    testNote,
+    actions(button('Test connection', () => busy(testCard, testNote, async () => {
+      await api('POST', '/api/mqtt/test');
+      testNote.className = 'muted';
+      testNote.textContent = 'Connecting…';
+      for (let i = 0; i < 30; i++) { /* it gives up after 10 s */
+        await sleep(1000);
+        const t = await api('GET', '/api/status');
+        status.replaceChildren(facts(mqttFacts(t)));
+        const r = (t.mqtt || {}).test || {};
+        if (!r.running) {
+          testNote.className = r.ok ? 'good' : 'bad';
+          testNote.textContent = r.ok ? 'Connected to the broker.' : `It couldn't connect: ${r.detail || 'no reason given'}.`;
+          return;
+        }
+      }
+    }))));
+
+  const values = Object.fromEntries((cat.fields || []).map((f) => [f.id, f]));
+  const list = h('div');
+  let boxes = [];
+  const fieldsNote = h('p');
+  const add = (f) => {
+    const box = mappingBox(f, values[`mqtt.${f.key}`], () => { boxes = boxes.filter((b) => b !== box); show(); });
+    boxes.push(box);
+  };
+  const show = () => list.replaceChildren(...boxes, ...(boxes.length < MQTT_FIELDS_MAX ? []
+    : [h('p', { class: 'muted small', text: `${MQTT_FIELDS_MAX} fields at most.` })]));
+  (doc.fields || []).forEach(add);
+  show();
+  const fieldsCard = card('Fields',
+    h('p', { class: 'muted small', text: 'Values from Home Assistant and other devices, shown on the dashboard as ' +
+      'mqtt.<key> fields; choose them for a preset\'s slots on the Presets page.' }),
+    h('p', { class: 'muted small', text: 'The device sleeps between syncs, so it reads only retained messages: ' +
+      'publishers must retain theirs, or go through Home Assistant\'s MQTT statestream. Zigbee2MQTT needs retain: true ' +
+      'for each device.' }),
+    list, fieldsNote,
+    actions(button('Add a field', () => {
+      if (boxes.length >= MQTT_FIELDS_MAX) return;
+      add({ kind: 'number', precision: null, ttl_s: 0 });
+      show();
+    }), button('Save fields', () => busy(fieldsCard, fieldsNote, async () => {
+      const fields = boxes.map((b, i) => b.read(i + 1));
+      const keys = new Set();
+      for (const f of fields) {
+        if (keys.has(f.key)) throw new ApiError(`Two fields have the key ${f.key}.`);
+        keys.add(f.key);
+      }
+      await api('PUT', '/api/mqtt_fields', { schema: 1, fields });
+      fieldsNote.className = 'good';
+      fieldsNote.textContent = 'Saved. Their values come with the next sync.';
+      toast('Fields saved');
+    }), 'primary')));
+
+  main.replaceChildren(h('h1', { text: 'MQTT and Home Assistant' }), brokerCard, testCard, fieldsCard);
+}
+
 /* ---- Device: the settings the menu also has (spec §5.7, D19) ---- */
 
 const LANGUAGES = [['en', 'English'], ['cs', 'Čeština']];
@@ -874,6 +1060,11 @@ async function presetsPage() {
 
 /* The preview, with each slot's name at its top right corner, as the slot fields below call them
  * (the renderer puts captions top left); a split preset's cells by their numbers. */
+/* A slot's or a cell's field the catalogue doesn't list, such as an mqtt.<key> no mapping names (spec §12.5): an
+ * option of its own, so the slot shows it and keeps it. */
+const unlisted = (ed, id) => (id && !ed.fields.some((f) => f.id === id)
+  ? h('option', { value: id, selected: true }, id.startsWith('mqtt.') ? `${id} (no mapping)` : id) : null);
+
 function previewBox(ed, slots) {
   const pct = (v, of) => `${+(100 * v / of).toFixed(3)}%`;
   return h('div', { class: 'preview' }, ed.img, slots.map((slot) => {
@@ -952,7 +1143,8 @@ function splitEditor(ed, p) {
         else delete node.field;
         changed(ed, false);
       } }, h('option', { value: '' }, '(empty)'), fits.map((f) => h('option',
-        { value: f.id, selected: node.field === f.id }, `${f.label} — ${f.value || 'no data yet'}`)));
+        { value: f.id, selected: node.field === f.id }, `${f.label} — ${f.value || 'no data yet'}`)),
+      unlisted(ed, node.field));
       const splitButton = (dir, text) => {
         const half = { split: dir, ratio: '1/2', line: true, a: {}, b: {} }; /* the field goes to the first part */
         const b = button(text, () => {
@@ -1078,7 +1270,8 @@ function renderPresets(ed) {
       changed(ed, false);
     } }, h('option', { value: '' }, '(empty)'),
     ed.fields.filter((f) => slot.kinds.includes(f.kind)).map((f) => h('option',
-      { value: f.id, selected: p.slots[slot.id] === f.id }, `${f.label} — ${f.value || 'no data yet'}`))))));
+      { value: f.id, selected: p.slots[slot.id] === f.id }, `${f.label} — ${f.value || 'no data yet'}`)),
+    unlisted(ed, p.slots[slot.id])))));
 
   const o = p.options;
   const check = (key, text) => h('label', { class: 'check' }, h('input', { type: 'checkbox', checked: !!o[key],
```


Run: `node --test test/web/test_app.mjs 2>&1 | grep -E '^# (pass|fail)'`
Expected: `# pass 46`, `# fail 0`.

- [ ] **Step 3: The API.** Device-only; Task 11 checks it on the board.

`components/webui/include/webui.h`:

```diff
--- a/components/webui/include/webui.h
+++ b/components/webui/include/webui.h
@@ -41,8 +41,8 @@ typedef struct {
     void (*event)(webui_event_t event);
 } webui_config_t;
 
-#define WEBUI_BODY_MAX  (24 * 1024) /* the largest request: a backup to restore, its presets up to 16 KB */
-#define WEBUI_REPLY_MAX (24 * 1024) /* the largest reply: a backup, or a BMP (15 662 bytes) */
+#define WEBUI_BODY_MAX  (40 * 1024) /* the largest request: a backup to restore with presets and MQTT fields */
+#define WEBUI_REPLY_MAX (40 * 1024) /* the largest reply: a backup, or a BMP (15 662 bytes) */
 /* The deepest request: a backup bundle, a file's own 16 levels inside the bundle's two
  * (storage_backup.c). Deeper ones are refused before anything parses them. */
 #define WEBUI_JSON_MAX_DEPTH 18
```


`components/webui/webui.c`:

```diff
--- a/components/webui/webui.c
+++ b/components/webui/webui.c
@@ -740,7 +740,11 @@ static esp_err_t api_handler(httpd_req_t *req)
     }
     api_call_t call = { .method = method_name(req->method), .path = path, .query = has_query ? query : "",
                         .body = mutating ? s_body : "", .reply = { .status = 404, .type = "application/json" } };
-    if (s_cfg.run(api_on_app, &call) != ESP_OK) {
+    esp_err_t ran = s_cfg.run(api_on_app, &call);
+    if (mutating) {
+        memset(s_body, 0, req->content_len); /* a PATCH may carry the MQTT password: it doesn't linger */
+    }
+    if (ran != ESP_OK) {
         return send_error(req, 503, "the device is busy");
     }
     httpd_resp_set_status(req, webui_status_line(call.reply.status));
```


`main/app_internal.h`:

```diff
--- a/main/app_internal.h
+++ b/main/app_internal.h
@@ -8,6 +8,7 @@
 #include "board_buttons.h"
 #include "datastore.h"
 #include "esp_err.h"
+#include "ha_mqtt.h"
 #include "radar_fetch.h"
 #include "scheduler.h"
 #include "settings.h"
@@ -247,6 +248,20 @@ void app_mqtt_set_message(const char *text);
 bool app_mqtt_set_value(const char *key, const char *text, char *err, size_t size);
 bool app_mqtt_clear_value(const char *key);
 void app_mqtt_print_status(void);
+/* The web UI (spec §10.3). */
+typedef struct {
+    bool on;       /* app_mqtt_on() */
+    bool keeping;  /* sync mode `always` keeps the client */
+    bool password_set;
+    ha_mqtt_status_t client;
+} app_mqtt_status_t;
+void app_mqtt_status(app_mqtt_status_t *out);
+esp_err_t app_mqtt_set_password(const char *password); /* "" forgets it */
+size_t app_mqtt_fields_json(char *out, size_t size);   /* mqtt_fields.json as saved; 0 if it doesn't fit */
+esp_err_t app_mqtt_replace_fields(const ha_fields_t *f);
+/* Starts a test connection with the saved settings: ESP_ERR_INVALID_ARG without a host, ESP_ERR_INVALID_STATE
+ * while the board has only its own network; app_mqtt_status() reports how it went. */
+esp_err_t app_mqtt_test(void);
 
 /* The `field`, `preset` and `night` console commands (main/app_cmds.c); call after diag_start(). */
 void app_register_commands(void);
```


`main/app_mqtt.c`:

```diff
--- a/main/app_mqtt.c
+++ b/main/app_mqtt.c
@@ -539,6 +539,101 @@ bool app_mqtt_clear_value(const char *key)
     return ha_store_clear(&s_store, ha_store_find(&s_store, key));
 }
 
+static bool password_set(void)
+{
+    nvs_handle_t nvs;
+    size_t len = 0;
+    bool set = false;
+    if (nvs_open("secrets", NVS_READONLY, &nvs) == ESP_OK) {
+        set = nvs_get_str(nvs, NVS_KEY_PASS, NULL, &len) == ESP_OK && len > 1;
+        nvs_close(nvs);
+    }
+    return set;
+}
+
+/* PATCH /api/settings's mqtt.password (spec §12.1): into NVS `secrets`, never logged; "" forgets it. */
+esp_err_t app_mqtt_set_password(const char *password)
+{
+    nvs_handle_t nvs;
+    esp_err_t err = nvs_open("secrets", NVS_READWRITE, &nvs);
+    if (err != ESP_OK) {
+        return err;
+    }
+    err = password[0] != '\0' ? nvs_set_str(nvs, NVS_KEY_PASS, password) : nvs_erase_key(nvs, NVS_KEY_PASS);
+    err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err; /* nothing to forget */
+    if (err == ESP_OK) {
+        err = nvs_commit(nvs);
+    }
+    nvs_close(nvs);
+    if (err == ESP_OK) {
+        ESP_LOGI(TAG, "MQTT password %s", password[0] != '\0' ? "saved" : "forgotten");
+        app_mqtt_password_changed();
+    }
+    return err;
+}
+
+void app_mqtt_status(app_mqtt_status_t *out)
+{
+    memset(out, 0, sizeof(*out));
+    out->on = app_mqtt_on();
+    out->keeping = s_keeping;
+    out->password_set = password_set();
+    ha_mqtt_status(&out->client);
+}
+
+size_t app_mqtt_fields_json(char *out, size_t size)
+{
+    load_fields();
+    return ha_fields_to_json(&s_fields, out, size);
+}
+
+/* PUT /api/mqtt_fields, a restore (spec §12.5): saved, then the values, the client and the screen follow. */
+esp_err_t app_mqtt_replace_fields(const ha_fields_t *f)
+{
+    size_t n = ha_fields_to_json(f, s_fields_json, sizeof(s_fields_json));
+    esp_err_t err = n == 0 ? ESP_ERR_INVALID_SIZE : storage_init();
+    if (err == ESP_OK) {
+        err = storage_write_atomic(STORAGE_MQTT_FIELDS_PATH, s_fields_json, n);
+    }
+    if (err != ESP_OK) {
+        ESP_LOGE(TAG, "saving the MQTT fields: %s", esp_err_to_name(err));
+        return err;
+    }
+    if (f != &s_fields) {
+        s_fields = *f;
+    }
+    s_fields_loaded = true;
+    ha_store_rebuild(&s_store, &s_fields); /* a key with the same kind keeps its value */
+    s_fields_sent = s_started;
+    if (s_started) {
+        ha_mqtt_set_fields(&s_fields); /* a kept connection subscribes to them at once */
+    }
+    ESP_LOGI(TAG, "%d MQTT fields saved", s_fields.count);
+    app_ui_render();
+    return ESP_OK;
+}
+
+/* POST /api/mqtt/test (spec §10.3): the saved broker, from the owner's network. */
+esp_err_t app_mqtt_test(void)
+{
+    if (app_settings()->mqtt_host[0] == '\0') {
+        return ESP_ERR_INVALID_ARG;
+    }
+    netmgr_status_t ns;
+    netmgr_status(&ns);
+    if (ns.state != NETMGR_STATION || ns.ip[0] == '\0') {
+        return ESP_ERR_INVALID_STATE;
+    }
+    start();
+    if (!s_started) {
+        return ESP_FAIL;
+    }
+    ha_conn_t c;
+    make_conn(&c);
+    ha_mqtt_test(&c);
+    return ESP_OK;
+}
+
 bool app_mqtt_failed(void)
 {
     if (!app_mqtt_on()) {
@@ -581,18 +676,6 @@ void app_mqtt_summary(char *out, size_t size)
     snprintf(out, size, "%s%s%s %s", when, suffix[0] ? " " : "", suffix, r == SYNC_STEP_OK ? "OK" : st->mqtt_detail);
 }
 
-static bool password_set(void)
-{
-    nvs_handle_t nvs;
-    size_t len = 0;
-    bool set = false;
-    if (nvs_open("secrets", NVS_READONLY, &nvs) == ESP_OK) {
-        set = nvs_get_str(nvs, NVS_KEY_PASS, NULL, &len) == ESP_OK && len > 1;
-        nvs_close(nvs);
-    }
-    return set;
-}
-
 /* `mqtt status` (spec §15): the settings, the client, the last session, the mappings and the message. */
 void app_mqtt_print_status(void)
 {
```


`main/app_web.c`:

```diff
--- a/main/app_web.c
+++ b/main/app_web.c
@@ -230,6 +230,37 @@ static void get_status(uint8_t *out, size_t size, webui_reply_t *reply)
         cJSON_AddNumberToObject(flights, "routes_paused_until", (double)fs.routes_paused_until);
     }
 
+    /* spec §10.3, M7: MQTT, the last sync's session, sync mode `always`'s connection and a test's result */
+    app_mqtt_status_t ms;
+    app_mqtt_status(&ms);
+    cJSON *mqtt = cJSON_AddObjectToObject(o, "mqtt");
+    cJSON_AddBoolToObject(mqtt, "enabled", ms.on);
+    cJSON_AddBoolToObject(mqtt, "keep", ms.keeping);
+    cJSON_AddBoolToObject(mqtt, "connected", ms.client.connected);
+    cJSON_AddBoolToObject(mqtt, "password_set", ms.password_set);
+    if (ms.keeping && !ms.client.connected && ms.client.detail[0] != '\0') {
+        cJSON_AddStringToObject(mqtt, "detail", ms.client.detail);
+    }
+    uint8_t mr = st->sync.last_at != 0 ? st->sync.last_result[SYNC_STEP_MQTT] : SYNC_STEP_NOT_RUN;
+    if (mr != SYNC_STEP_NOT_RUN) {
+        cJSON *last = cJSON_AddObjectToObject(mqtt, "last");
+        cJSON_AddNumberToObject(last, "at", st->sync.last_at);
+        cJSON_AddStringToObject(last, "result", mr == SYNC_STEP_OK ? "ok" : "failed");
+        if (mr == SYNC_STEP_FAILED) {
+            cJSON_AddStringToObject(last, "detail", st->sync.mqtt_detail);
+        }
+    }
+    if (ms.client.test_running || ms.client.test_done) {
+        cJSON *test = cJSON_AddObjectToObject(mqtt, "test");
+        cJSON_AddBoolToObject(test, "running", ms.client.test_running);
+        if (!ms.client.test_running) {
+            cJSON_AddBoolToObject(test, "ok", ms.client.test_ok);
+            if (!ms.client.test_ok) {
+                cJSON_AddStringToObject(test, "detail", ms.client.test_detail);
+            }
+        }
+    }
+
     const ui_preset_t *active = &st->presets.presets[st->presets.active];
     cJSON *preset = cJSON_AddObjectToObject(o, "preset");
     cJSON_AddStringToObject(preset, "active", active->id);
@@ -360,9 +391,9 @@ static void learn(const char *body, uint8_t *out, size_t size, webui_reply_t *re
     reply_cjson(reply, out, size, o);
 }
 
-/* A backup of the largest files restores: settings.json up to the 2 KB app_ui.c keeps, presets.json up to
- * its own limit, and the bundle around them. */
-_Static_assert(SETTINGS_JSON_MAX + UI_PRESETS_JSON_MAX + 512 <= WEBUI_BODY_MAX,
+/* A backup of the largest files restores: settings.json up to the 2 KB app_ui.c keeps, presets.json and
+ * mqtt_fields.json up to their own limits, and the bundle around them. */
+_Static_assert(SETTINGS_JSON_MAX + UI_PRESETS_JSON_MAX + HA_FIELDS_JSON_MAX + 512 <= WEBUI_BODY_MAX,
                "a backup of the largest files fits a request");
 
 /* GET /api/backup (spec §14.4): the /cfg files as the firmware would save them now. */
@@ -370,14 +401,16 @@ static void backup(uint8_t *out, size_t size, webui_reply_t *reply)
 {
     EXT_RAM_BSS_ATTR static char settings[SETTINGS_JSON_MAX];
     EXT_RAM_BSS_ATTR static char presets[UI_PRESETS_JSON_MAX];
+    EXT_RAM_BSS_ATTR static char fields[HA_FIELDS_JSON_MAX];
     netmgr_status_t net;
     netmgr_status(&net);
     backup_file_t files[] = {
         { "settings.json", app_ui_settings_json(settings, sizeof(settings)) ? settings : NULL },
         { "presets.json", ui_presets_to_json(app_presets(), presets, sizeof(presets)) ? presets : NULL },
+        { "mqtt_fields.json", app_mqtt_fields_json(fields, sizeof(fields)) ? fields : NULL }, /* M7 */
     };
-    reply_text(reply, out, size,
-               backup_build(files, 2, net.host, esp_app_get_description()->version, (char *)out, size));
+    reply_text(reply, out, size, backup_build(files, sizeof(files) / sizeof(files[0]), net.host,
+                                              esp_app_get_description()->version, (char *)out, size));
 }
 
 /* POST /api/restore: every file is checked before any is replaced. */
@@ -385,20 +418,23 @@ static void restore(const char *body, uint8_t *out, size_t size, webui_reply_t *
 {
     EXT_RAM_BSS_ATTR static char buf[WEBUI_BODY_MAX];
     EXT_RAM_BSS_ATTR static ui_presets_t presets;
+    EXT_RAM_BSS_ATTR static ha_fields_t fields;
     backup_file_t files[8];
-    char err[112];
+    char err[128]; /* "mqtt_fields.json: " and a reason of up to 95 bytes */
     int n = backup_split(body, files, 8, buf, sizeof(buf), err, sizeof(err));
     if (n < 0) {
         reply_error(reply, out, size, 400, err);
         return;
     }
-    const char *settings = NULL, *presets_text = NULL;
+    const char *settings = NULL, *presets_text = NULL, *fields_text = NULL;
     cJSON *skipped = cJSON_CreateArray();
     for (int i = 0; i < n; i++) {
         if (strcmp(files[i].name, "settings.json") == 0) {
             settings = files[i].text;
         } else if (strcmp(files[i].name, "presets.json") == 0) {
             presets_text = files[i].text;
+        } else if (strcmp(files[i].name, "mqtt_fields.json") == 0) {
+            fields_text = files[i].text;
         } else {
             cJSON_AddItemToArray(skipped, cJSON_CreateString(files[i].name)); /* from a later firmware */
         }
@@ -408,6 +444,8 @@ static void restore(const char *body, uint8_t *out, size_t size, webui_reply_t *
         snprintf(err, sizeof(err), "settings.json: %s", why);
     } else if (presets_text != NULL && !ui_presets_from_json(presets_text, &presets, why, sizeof(why))) {
         snprintf(err, sizeof(err), "presets.json: %s", why);
+    } else if (fields_text != NULL && !ha_fields_from_json(fields_text, &fields, why, sizeof(why))) {
+        snprintf(err, sizeof(err), "mqtt_fields.json: %s", why);
     } else {
         err[0] = '\0';
     }
@@ -420,6 +458,9 @@ static void restore(const char *body, uint8_t *out, size_t size, webui_reply_t *
     if (e == ESP_OK && presets_text != NULL) {
         e = app_ui_replace_presets(&presets);
     }
+    if (e == ESP_OK && fields_text != NULL) {
+        e = app_mqtt_replace_fields(&fields);
+    }
     if (e != ESP_OK) {
         cJSON_Delete(skipped);
         reply_error(reply, out, size, 500, "saving failed; some files may be restored");
@@ -432,10 +473,29 @@ static void restore(const char *body, uint8_t *out, size_t size, webui_reply_t *
     reply_cjson(reply, out, size, o);
 }
 
+/* PATCH /api/settings (spec §12.1): mqtt.password goes to NVS, the rest into settings.json. */
+static void patch_settings(const char *body, uint8_t *out, size_t size, webui_reply_t *reply)
+{
+    char err[112], password[SETTINGS_MQTT_PASS_LEN];
+    settings_secret_t secret = settings_patch_secret(body, password, sizeof(password));
+    if (secret == SETTINGS_SECRET_BAD) {
+        reply_error(reply, out, size, 400, "mqtt.password: a text of up to 63 bytes, or null");
+    } else if (app_ui_patch_settings(body, err, sizeof(err)) != ESP_OK) {
+        reply_error(reply, out, size, 400, err);
+    } else if (secret != SETTINGS_SECRET_NONE &&
+               app_mqtt_set_password(secret == SETTINGS_SECRET_SET ? password : "") != ESP_OK) {
+        reply_error(reply, out, size, 500, "the MQTT password wasn't saved");
+    } else {
+        reply_text(reply, out, size, app_ui_settings_json((char *)out, size));
+    }
+    memset(password, 0, sizeof(password));
+}
+
 void app_web_api(const char *method, const char *path, const char *query, const char *body, uint8_t *out,
                  size_t size, webui_reply_t *reply)
 {
     EXT_RAM_BSS_ATTR static ui_presets_t presets;
+    EXT_RAM_BSS_ATTR static ha_fields_t mqtt_fields;
     char err[112];
     bool get = strcmp(method, "GET") == 0;
     if (strcmp(path, "/api/status") == 0 && get) {
@@ -443,11 +503,7 @@ void app_web_api(const char *method, const char *path, const char *query, const
     } else if (strcmp(path, "/api/settings") == 0 && get) {
         reply_text(reply, out, size, app_ui_settings_json((char *)out, size));
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
@@ -484,6 +540,28 @@ void app_web_api(const char *method, const char *path, const char *query, const
                                                        "is on its own network only"
                                                      : "the sync didn't start");
         }
+    } else if (strcmp(path, "/api/mqtt_fields") == 0 && get) { /* spec §10.3, M7 */
+        reply_text(reply, out, size, app_mqtt_fields_json((char *)out, size));
+    } else if (strcmp(path, "/api/mqtt_fields") == 0 && strcmp(method, "PUT") == 0) {
+        if (!ha_fields_from_json(body, &mqtt_fields, err, sizeof(err))) {
+            reply_error(reply, out, size, 400, err);
+        } else if (app_mqtt_replace_fields(&mqtt_fields) != ESP_OK) {
+            reply_error(reply, out, size, 500, "mqtt_fields.json wasn't saved");
+        } else {
+            reply_text(reply, out, size, app_mqtt_fields_json((char *)out, size));
+        }
+    } else if (strcmp(path, "/api/mqtt/test") == 0 && strcmp(method, "POST") == 0) {
+        esp_err_t e = app_mqtt_test();
+        if (e == ESP_OK) {
+            reply_text(reply, out, size, (size_t)snprintf((char *)out, size, "{\"started\":true}"));
+            reply->status = 202; /* the page follows it in GET /api/status */
+        } else {
+            reply_error(reply, out, size, 409,
+                        e == ESP_ERR_INVALID_ARG     ? "save the broker's host first"
+                        : e == ESP_ERR_INVALID_STATE ? "the device is on its own network only: it reaches the broker "
+                                                       "from yours"
+                                                     : "the test didn't start");
+        }
     } else if (strcmp(path, "/api/backup") == 0 && get) {
         backup(out, size, reply);
     } else if (strcmp(path, "/api/restore") == 0 && strcmp(method, "POST") == 0) {
```


- [ ] **Step 4: Run the tests, and build.**

Run: `cmake --build build-host && ctest --test-dir build-host | tail -3; tools/idf.sh build 2>&1 | grep -c 'warning:'; ls -l build/reflbo.bin | awk '{print $5}'`
Expected: `100% tests passed, 0 tests failed out of 65`; `0`; `2543552`.

- [ ] **Step 5: Commit.**

```bash
git add components/webui main web test/web
git commit -m "feat(web): the MQTT page and its API (spec §10.3)"
```

### Task 11: On the board, and the docs as built

**Files:**
- Modify (only if the checks find something): whatever they point at, each fix with its own test where one can fail first.
- Modify: `docs/specs/2026-09-25-firmware-design.md` (r35, as built), `AGENTS.md`, `README.md`, `docs/guide.md`
- Create: `docs/images/web/mqtt.png`

**Needs the owner first:** the board plugged into this Mac, and a choice of time. M5's RTC-trim run takes its syncs at 08:00 on 2026-10-03, 10-04 and 10-05 (memory `m5-deferred-owner-checks`), and Step 6's sync on demand restarts its count, so the checks run after the 2026-10-05 08:00 sync, or now if the owner lets the count restart. These checks follow spec §12.10: with no broker yet, they cover what needs none (MQTT off, the MQTT page and its API, `mqtt status`, the message banner through `field set`, an unreachable broker); the board starts and ends on the stable firmware (`stable-m6b`), its configuration is backed up first and restored last, and nothing is erased. Step 2 resets the owner's web password, as the owner allows (memory `web-password-reset-ok`), and Step 9 clears the temporary one, so the owner's next visit over the device's own network chooses theirs again. Ask, and wait.

Before anything else, confirm the port is this board (`ioreg -p IOUSB -l -w0 | grep 'USB Serial Number'` shows `14:C1:9F:54:BB:94`), and note what it has now:

```bash
tools/idf.sh exec python tools/devlog.py --cmd version --cmd "sync status" --cmd "preset list" -o captures/m7-before.log
grep -E 'elf|mode' captures/m7-before.log
```

Expected: `elf dcb35a7e3` (the stable firmware), and the sync mode and presets to restore in Step 9.

- [ ] **Step 1: Config mode and a temporary web password.** Enter config mode and read the device's network and its password from the screen:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "btn boot long" -t 5
tools/idf.sh exec python tools/screenshot.py -o captures/m7-config.png
```

If the page asks for a password the owner set, reset it first: Menu ▸ Wi-Fi ▸ Reset web password (`btn key long` opens the menu, `btn key short` steps, `btn key long` enters; the confirmation asks for KEY held; a screenshot after each press shows where the menu is), then config mode again. Join the device's network and set a throwaway password (AGENTS §6: without the USB Ethernet, joining cuts this Mac's internet; do the join, the calls and the way back in one script that restores the home Wi-Fi whatever happens):

```bash
networksetup -setairportnetwork en0 reflbo-bb94 '<the password on the screen>'
PW=$(openssl rand -hex 8) # never written down; Step 9 clears it
curl -s -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/setup
curl -s -c captures/m7-jar -H 'Content-Type: application/json' -d "{\"password\":\"$PW\"}" http://192.168.4.1/api/auth/login
```

Expected: both answer 200. In config mode the board also joins the home network (gotcha 43); `wifi status` prints its address there, where the same session works without the device's network.

- [ ] **Step 2: Back up, on the stable firmware.**

```bash
curl -s -b captures/m7-jar http://192.168.4.1/api/backup > captures/m7-backup.json
python3 -c 'import json; b=json.load(open("captures/m7-backup.json")); print(sorted(b["files"]), b["firmware"])'
```

Expected: `['presets.json', 'settings.json']` and the stable firmware's version.

- [ ] **Step 3: Flash M7 and boot.**

```bash
tools/idf.sh build && tools/idf.sh -p /dev/cu.usbmodemXXXX flash 2>&1 | grep -E 'MAC:|Hash of data verified'
tools/idf.sh exec python tools/devlog.py --cmd reboot --until "reflbo ready" -t 30 -o captures/m7-boot.log
grep -E 'presets.json|settings.json|MQTT|E \(' captures/m7-boot.log
tools/idf.sh exec python tools/devlog.py --cmd version --cmd "mqtt status" --cmd "preset list"
```

Expected: `MAC: 14:c1:9f:54:bb:94`; no `E (` line, nothing about invalid files (the owner's files from M6b parse as they are); `mqtt off, broker -:1883, user "", password none, discovery on (prefix homeassistant)`, `client: not started, not connected; last: Off`, `0 of 32 fields mapped`, `discovery: not sent yet`, `message: none`; the presets as in `captures/m7-before.log`. (If the board sits in download mode after flashing, leave it as gotcha 22 says.)

- [ ] **Step 4: The message's banner, and KEY.** A banner and its screenshot go in one call (AGENTS §7):

```bash
tools/idf.sh exec python tools/devlog.py --cmd "field set ha.message Washing machine done" --cmd screenshot -t 8 -o captures/m7-banner.log
tools/idf.sh exec python tools/devlog.py --cmd "btn key short" --cmd "preset list" -t 5 -o captures/m7-dismiss.log
tools/idf.sh exec python tools/devlog.py --cmd screenshot --cmd "field get ha.message" -t 8 -o captures/m7-after.log
```

Expected: the first screenshot (decoded with `screenshot.extract_pbm()`) has the black bar at the bottom with "Washing machine done"; `field set` prints the field's line: `ha.message`, `fresh`, the text. KEY short logs `KEY short: the message's banner dismissed`, and `preset list` names the same active preset as before (that press does nothing else). The last screenshot has no banner, and `ha.message` is still fresh with the text.

- [ ] **Step 5: Deep sleep keeps the values and the message.**

```bash
tools/idf.sh exec python tools/devlog.py --cmd "sleep stats reset" --cmd "field set ha.message Door open" --cmd "sleep test deep 1" -t 10
```

The console drops for the cycle, up to a minute, and its port comes back after it. Then:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "sleep stats" --cmd "field get ha.message" --cmd "mqtt status" -o captures/m7-warm.log
```

Expected: `sleep: 0 light, 1 deep`; `ha.message` still fresh with "Door open", read from RTC FAST memory after the warm wake; `mqtt status` ends `banner shown`. Then `--cmd "field clear ha.message"`: `ha.message` missing.

- [ ] **Step 6: The MQTT page's API, an unreachable broker, and a sync.** Config mode again (`btn boot long`), log in again as in Step 1 (the session ended with config mode). The spec's example mappings (§12.5), then two the device refuses:

```bash
J='-b captures/m7-jar -H Content-Type:application/json'
curl -s $J -X PUT -d '{"schema":1,"fields":[{"key":"outdoor_temp","label":"Outside","kind":"number","unit":"°C","precision":1,"topic":"ha/statestream/sensor/outdoor_temperature/state","json_path":null,"ttl_s":172800},{"key":"co2","label":"CO2","kind":"number","unit":"ppm","precision":0,"topic":"zigbee2mqtt/living_room","json_path":"co2"}]}' http://192.168.4.1/api/mqtt_fields | head -c 120; echo
curl -s -w ' %{http_code}\n' $J -X PUT -d '{"schema":1,"fields":[{"key":"CO2","topic":"t"}]}' http://192.168.4.1/api/mqtt_fields
curl -s -w ' %{http_code}\n' $J -X PUT -d '{"schema":1,"fields":[{"key":"co2","topic":"zigbee2mqtt/#"}]}' http://192.168.4.1/api/mqtt_fields
```

Expected: the first echoes the file (`{"schema":1,"fields":[{"key":"outdoor_temp",…`); then `{"error":"field 1: a key is 1-23 characters of a-z, 0-9 or _"} 400` and `{"error":"field co2: the topic has a wildcard"} 400`. On the console, `field set mqtt.outdoor_temp 12.34` prints `mqtt.outdoor_temp` fresh at `12.3 °C`, `field set mqtt.co2 abc` prints `field: mqtt.co2 takes a number`, and `mqtt status` lists both mappings, Outside's with `12.3 °C, 0 s old` and CO2's with `no value`.

A broker that isn't there (TEST-NET-1, which nothing answers), with a password:

```bash
curl -s $J -X PATCH -d '{"mqtt":{"enabled":true,"host":"192.0.2.1","password":"not-a-secret"}}' http://192.168.4.1/api/settings | grep -c password
curl -s -b captures/m7-jar http://192.168.4.1/api/status | python3 -c 'import json,sys; print(json.load(sys.stdin)["mqtt"])'
curl -s -w ' %{http_code}\n' $J -X POST http://192.168.4.1/api/mqtt/test
```

Expected: `0` (the reply never carries the password); `{'enabled': True, 'keep': False, 'connected': False, 'password_set': True}`; the test: `{"started":true} 202` if config mode joined the home network, else `{"error":"the device is on its own network only: it reaches the broker from yours"} 409`. After a 202, `GET /api/status` within 12 s shows `'test': {'running': False, 'ok': False, 'detail': 'timeout'}` (or `no connection`).

Headless Chrome on `http://192.168.4.1/#mqtt` (AGENTS §6), with the jar's session: the three cards; Broker with host 192.0.2.1 and "Saved on the device, which never shows it"; the two fields, "No value yet" under CO2 and "Last value: 12.3 °C" under Outside. Save its screenshot for the guide as `docs/images/web/mqtt.png` (the docs kit's 500 px window, number inputs as text, AGENTS §6).

Leave config mode (`btn boot long`) and sync with the broker unreachable:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "sync now" -t 60 --until "app_sync: sync (done|failed)" -o captures/m7-sync.log
grep -E 'app_sync|app_mqtt|ha_mqtt' captures/m7-sync.log
tools/idf.sh exec python tools/devlog.py --cmd "sync status" --cmd "mqtt status" --cmd screenshot -t 8 -o captures/m7-mark.log
```

Expected: `ha_mqtt: session failed: timeout` (or `no connection`), `app_sync: MQTT: timeout`, and `app_sync: sync done`: the sync's other steps ran and it isn't failed (D32). `mqtt status`'s last line names the failure, and the screenshot's status bar has the MQTT mark (the broken link) and no crossed-out cloud. Info ▸ MQTT (`btn key long`, then KEY short to Info and KEY held, a screenshot) reads `HH:MM timeout`.

- [ ] **Step 7: Sync mode `always`'s connection.** `btn boot double` turns `always` on; the client keeps trying:

```bash
tools/idf.sh exec python tools/devlog.py --cmd "btn boot double" --cmd "mqtt status" -t 30 -o captures/m7-always.log
grep -E 'app_mqtt|ha_mqtt' captures/m7-always.log
tools/idf.sh exec python tools/devlog.py --cmd "btn boot double" -t 5
```

Expected: `app_mqtt: sync mode always: connecting to 192.0.2.1`, then `ha_mqtt: connection lost (timeout); again in 10 s`; the status bar's MQTT mark stays.

Before the second press, the memory M7 adds, with the Flights view up as well (`preset set flights`, then back to the preset before): `--cmd heap --cmd tasks`, twice a minute apart. Expected: `internal` `min` above 40 KB (M6's lowest with a sync, a radar refresh and the flight radar's session was 74 KB; the client's buffers and two task stacks come out of it), and in `tasks` the stack column of `ha_mqtt`, `mqtt_task` and `app` above 512 bytes. Write both into the spec's §6 as built. The second press: `sync mode always: disconnected`, and the mode before.

- [ ] **Step 8: MQTT off again.** Config mode, log in, then turn MQTT off and forget the password, so no MQTT secret stays in NVS:

```bash
curl -s $J -X PATCH -d '{"mqtt":{"enabled":false,"host":"","password":null}}' http://192.168.4.1/api/settings >/dev/null
curl -s -b captures/m7-jar http://192.168.4.1/api/status | python3 -c 'import json,sys; print(json.load(sys.stdin)["mqtt"]["password_set"])'
```

Expected: `False`.

- [ ] **Step 9: Back to the stable firmware, and the configuration restored.**

```bash
captures/stable/stable-m6b/flash.sh /dev/cu.usbmodemXXXX
tools/idf.sh exec python tools/devlog.py --cmd reboot --until "reflbo ready" -t 30
tools/idf.sh exec python tools/devlog.py --cmd version
```

Expected: `elf dcb35a7e3`. Then config mode, log in (Step 1), restore and compare:

```bash
curl -s -b captures/m7-jar -H 'Content-Type: application/json' --data-binary @captures/m7-backup.json http://192.168.4.1/api/restore
curl -s -b captures/m7-jar http://192.168.4.1/api/backup > captures/m7-after.json
python3 -c 'import json; a,b=(json.load(open(f))["files"] for f in ("captures/m7-backup.json","captures/m7-after.json")); print("same" if a==b else "differs")'
```

Expected: `{"ok":true,"skipped":[]}` and `same`: `settings.json` without the `mqtt` section the M7 firmware wrote into it, and the owner's presets with their active one. `/fs/cfg/mqtt_fields.json` stays on the storage partition, where the stable firmware never reads it, and `sys/mqtt_disc` was never written (no discovery went out). Then clear the temporary web password (Menu ▸ Wi-Fi ▸ Reset web password, as in Step 1), leave config mode, `networksetup -removepreferredwirelessnetwork en0 reflbo-bb94`, and `rm captures/m7-jar captures/m7-*.json`. `sync status` and `preset list` as in `captures/m7-before.log`.

- [ ] **Step 10: Write down what was built.**
  - Spec r35: §12 as built (mapped topics at QoS 0, spec §12.2's table, as the broker would otherwise keep a backlog of readings for the sleeping device; the session's steps and its failure details; key presses published at once from the app task; commands in sync mode `always` publishing the state at once; the kept session subscribing again every 5 min; `cmd/preset` by name, then id; discovery again for a broker without our session, and the broker in its hash; the signal no change of state; values that move with the clock; the banner one line and not in config mode, and the KEY short that dismisses it unreported), §5.1 (`ha.message`'s wrapping), §5.2 (the MQTT mark beside the sync's marks; the crossed-out cloud after a scheduled sync's failure only), §5.4 (the presets' own key table), §6 (the snapshot: 4 856 bytes of a 5 KB cap, version 9; the values' block in RTC FAST, 3 968 bytes), §9.3 (step 6's budget; `fetch`'s transmit buffer and its timeout), §10.3 (the MQTT page as built; requests and replies up to 40 KB; the `mqtt` block of `GET /api/status`), §14 (`secrets/mqtt_pass`), §15 (`mqtt status`, `field` with `ha.message` and `mqtt.<key>`), §17 (the tests as built), §20 (anything the checks found, and the review's four left for later: the quiet second with no mapped topics, a command lost behind a full queue after its PUBACK, the values after a restart that keeps the clock, a key press on a dead link), §21.
  - `AGENTS.md`: the status (M7 built; its acceptance waits for the owner's broker and HA), §5.2 (`ha_mqtt` without its *(planned)* marker), and the gotchas this milestone taught: esp-mqtt holds its API lock through a whole connect; RTC FAST stays powered in deep sleep only because the heap may use it; the M7 firmware writes an `mqtt` section into `settings.json` that a restore on older firmware drops.
  - `README.md` and `docs/guide.md`: MQTT and Home Assistant (what goes to HA, the commands, the fields and the retain rule, the message and its banner, key presses), with the MQTT page's screenshot; `python3 tools/docs_images.py` again if a golden changed.

- [ ] **Step 11: Commit and push.**

```bash
git add docs AGENTS.md README.md
git commit -m "docs: record M7 as built (spec r35)"
git push origin main
```

## Owner acceptance

M7 is done (spec §18) once the owner, with a broker and Home Assistant set up, has checked, with the expected results:

1. **Discovery:** the device appears in HA (Settings ▸ Devices) as reflbo-XXXX, Waveshare ESP32-S3-RLCD-4.2, with temperature, humidity, battery and charging, three diagnostics, the Preset select, the Sync now and Next preset buttons, the Message notify entity and six device triggers, after the first sync with the broker set on the MQTT page.
2. **Commands at the next sync:** choosing a preset in HA, or pressing Next preset, switches the panel at the next sync, and the select then shows it; in sync mode Always on, at once, and Sync now starts a sync.
3. **A mapped value and a message:** a mapping on the MQTT page to a retained topic (HA's statestream, or Zigbee2MQTT with `retain: true`) shows its value in a preset's slot; `notify.send_message` to Message shows the banner, which KEY short dismisses without switching the preset.
4. **Key presses in sync mode Always on:** an automation on "button_1 short press" fires when KEY is pressed on the dashboard.
5. **Power:** a sync's session against the broker, as spec §9.4 measures, in `docs/power.md`.

Until HA exists, the broker side can be tried on this Mac with Mosquitto (`brew install mosquitto`, only with the owner's agreement): a broker on the LAN, `mosquitto_pub -r` for retained values, and `mosquitto_sub -v -t 'reflbo/#' -t 'homeassistant/#'` to watch the state, the discovery configs and the key presses.

Still open from earlier milestones, offered when the owner is at the board: M6b's acceptance (memory `m6b-design`), M6's rainy-day ČHMÚ frame and power (memory `m6-acceptance-open`), M5's trim over days, sync on battery and router-off checks (memory `m5-deferred-owner-checks`), M3's night-sleep current and night peek, M4's update from the page and first run.
