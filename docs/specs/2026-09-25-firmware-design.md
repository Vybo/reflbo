# reflbo firmware: design spec

- **Date:** 2026-09-25
- **Status:** Approved by the owner on 2026-09-25 (r3; changes listed in §21)
- **Covers:** firmware v1, milestones M0–M9
- **Related:** `AGENTS.md` (hardware reference §3, workflow §6–§8)

## 1. Purpose and scope

reflbo turns the Waveshare ESP32-S3-RLCD-4.2 into a battery-powered desk display. It shows watch-face-like dashboards, rings alarms and plays internet radio. It can be configured on the device and from a phone, and optionally linked to Home Assistant over MQTT.

### 1.1 Requirements

| ID | Requirement |
|---|---|
| R1 | Monitor battery voltage, level and charging state |
| R2 | A few layouts whose data fields are optional; presets can be stored and cycled automatically or by hand |
| R3 | Fields: time, date, SHTC3 temperature and humidity, internet weather, sunrise/sunset, data from other local devices |
| R4 | Keep time with the PCF85063 RTC |
| R5 | Audio: alarms and internet radio |
| R6 | Settings on the device and on a website the device hosts, reachable from a phone in AP mode and over the LAN |
| R7 | Save power: sync on a configurable schedule (default once a day), keep Wi-Fi off otherwise, turn Wi-Fi on when asked so the configurator is reachable |
| R8 | Control everything with the board buttons |
| R9 | Use the microSD slot (feature set agreed at M9) |
| R10 | Two-way Home Assistant integration: publish status and sensors, and show chosen HA entities |
| R11 | Agent-verifiable: logs and screenshots over USB, rendering on the host; the owner confirms physical output |

| ID | Non-functional requirement |
|---|---|
| N1 | Works fully offline; network features only add data |
| N2 | Battery life as long as practical. Stretch goal: average current below 2 mA with the default sync schedule |
| N3 | English UI, with strings and formats organised as language packs |
| N4 | Location, time zone and units are configurable. Defaults: Brno, Europe/Prague, metric |
| N5 | Open source, with third-party code credited |

### 1.2 Decisions

| ID | Decision | Notes |
|---|---|---|
| D1 | Our own immediate-mode 1-bpp renderer | LVGL is not used |
| D2 | ESP-IDF v5.5.x (v5.5.5 at the time of writing) | Revisit v6.x after M5 |
| D3 | Light sleep is the default idle strategy (M2 measurement, 2026-09-28) | Both drew 11.5 mA at 5.24 V; the 3V3 converter's forced PWM dominates (§9.4). Deep sleep stays selectable |
| D4 | English by default, structured as language packs | A Czech pack joins in M3 (D15) |
| D5 | Home Assistant over MQTT with HA MQTT discovery | |
| D6 | Power is best effort; an average below 2 mA is the stretch goal | |
| D7 | Other local devices publish to MQTT, and the device maps topics to fields | Same mechanism as HA values (§12.5) |
| D8 | Licence Apache-2.0, plus `NOTICE` and `THIRD_PARTY.md` (confirmed 2026-09-25) | Same licence as the Waveshare code we adapt. `NOTICE` carries attribution into forks |
| D9 | No RTC backup cell is fitted now; one can be fitted later. Firmware must work without it (§7) | The owner may fit an ML1220 |
| D10 | Features beyond R1–R11 and N1–N5 are proposals, discussed at the relevant milestone (§19) | |
| D11 | The sync schedule and the display update interval are both configurable (§9.2, §9.3) | Owner adjustment in r2 |
| D12 | Panel: the factory init sequence, with the LPM refresh rate set separately (default 1 Hz; `panel rate` changes it from 0.25 to 8 Hz) (§4.2) | Owner check at M1: factory contrast is visibly better than XiaoZhi's, and it looks the same at 1 Hz as at 8 Hz |
| D13 | Landscape only | Portrait orientation declined at M1 |
| D14 | Light sleep is entered explicitly by the app (`power_sleep_light()`); esp_pm automatic light sleep is not used. A tethered board stays awake (§3.4) | M2: the USB console drops in any light sleep, and automatic light sleep would need level-type interrupts on the button and RTC pins all the time |
| D15 | Accepted M3 proposals (owner, 2026-09-28): the LPM refresh rate as a display setting (§4.2); a preset schedule whose entries can also start a timed night sleep (§5.4, §9.1); extra local fields (§5.1); a Czech language pack with name days and public holidays (§5.8) | The Night layout stays deferred (§19). The owner asked to measure what night sleep saves |
| D16 | Owner, 2026-09-29: the `cs` pack ships the public holidays but no name-day calendar until a source with a clean licence turns up (§5.8, §20). A button still held when the board goes to sleep is left out of that sleep's wake sources (§9.2) | The best name-day list found (`namedays-cs`, MIT) traces its data to Czech Wikipedia (CC BY-SA). A stuck KEY or BOOT would otherwise wake the board again and again |
| D17 | Owner, 2026-09-29 (M3b render review): the menu leaves out Display ▸ Contrast for now, and the temperature offset steps by 0.1 °C (§5.7) | No contrast levels besides the factory sequence's (D12) have been checked on the panel |
| D18 | Owner, 2026-09-30 (M4 planning): the web UI gets a password instead of the admin PIN, chosen on the first visit over the device's own AP and needed on every visit after (§10.4); web UI translations stay deferred; M4 runs as one plan; the snapshot header gets no ELF hash | A PIN guards little that a password doesn't; every firmware change resets the chip, and only deep-sleep wakes read the snapshot |
| D19 | Owner, 2026-09-30 (M4 spike review): the first-run screen appears when `settings.json` is missing at boot (§5.5); the status bar's Wi-Fi state moves to M5 and M6 (§5.2); a restart from the web UI returns in config mode (§10.2); the web UI sets a lost clock from the phone, can change the password and log out, and gets a Device page (§10.3, §10.4); the temperature offset defaults to −2.0 °C (§8) | In M4 Wi-Fi runs only in config mode, which has its own screen. The board warms the SHTC3 by about 2 °C at all times; the user trims the rest |
| D20 | Owner, 2026-09-30 (M4 acceptance): config mode watches the battery (§8, §10.2); while a phone is logged in, config mode shows the dashboard with a globe in the status bar (§5.2, §10.2); the preset preview names its slots, and a new preset joins the cycle (§10.3); seconds keep the refresh rate the user picked (§7); the battery level can follow the owner's own full and empty voltages (§8) | A battery that sags under the radio would brown out anyway. The owner wants to watch settings change on the screen itself. The refresh rate is the user's to tune |
| D21 | Owner, 2026-09-30: the battery curve is learned from one full discharge, not a charge (§8) | The firmware can't see charging, and the charger's current lifts VBAT, so a charge maps poorly to the resting level; this board's steady load makes time a fair measure of charge |
| D22 | Owner, 2026-10-01 (ADS-B radar proposal, §19.1): adsb.fi is the only source; the radar runs only in sync mode `always` (D11), with no time-boxed sessions and no separate switch; the map is centred on a point and zoomed from the web UI, with towns and airports built in | Still a proposal for after M5 (D10). One always-on Wi-Fi mode serves both the radar and the LAN configurator (§10.4) |
| D23 | Owner, 2026-10-01 (weather radar, §19.2): ČHMÚ by default, with RainViewer outside its coverage; both a full-screen radar layout and a slot widget; aircraft stay a separate view, never over the rain; the radar never turns Wi-Fi on itself: it takes a frame with each normal sync, and every 5 minutes in sync mode `always`; both radars join the roadmap | M7 follows M6, which brings sync mode `always`; Audio and microSD move to M8 and M9 |
| D24 | Owner, 2026-10-01: the radars come sooner. Sync mode `always` moves from the MQTT milestone into M5, the radars follow as M6, and MQTT and Home Assistant become M7 | Nothing in the radars needs MQTT; `always` belongs with the sync framework anyway |
| D25 | Owner, 2026-10-01 (M5 planning): M5 also brings quiet hours (§9.3), air quality and pollen from Open-Meteo (§5.1, §11) and the RTC trim against NTP (§7); a static IP stays deferred; M5 runs as one plan; review minors are fixed where M5 touches their code | The RTC loses about 3.4 s a day (`AGENTS.md` gotcha 7); one trim step is 4.34 ppm, 0.37 s a day |
| D26 | Owner, 2026-10-01 (M5 spike review): the weather icons come from the Weather Icons font (SIL OFL 1.1) beside Material Icons (§4.5); the sun widget shows the day length's change since yesterday, M3's deferred proposal (§5.1); a UV index field joins the air quality (§5.1, §11); pollen stays as built: a top field and one per type | The day-length change needed `astro`; the UV index comes in the same request |
| D27 | Owner, 2026-10-01 (M6 planning): M6 runs as one plan, written straight from the spec without a spike; review minors are fixed where M6 touches their code; three extras join the radars: rain in the next 2 hours (§11.4), a loop of the last hour's radar frames (§11.2), and the nearest aircraft's route from adsb.lol (§11.3) | The radars are about the size of M3a and M3b together (§19) |
| D28 | Owner, 2026-10-01 (M6 design): the flight radar is a built-in preset that the cycle visits only in sync mode `always` (§5.4); each radar has its own centre and zoom (§11.1); BOOT short on the radar layout plays the last hour, 12 frames (§5.6, §11.2); the map draws borders, towns and airports (§11.1); no microSD in M6, as its working set fits PSRAM and LittleFS, while radar and flight history over days, a detailed map pack and an aircraft registration database join M9's candidates (§19) | A frame's grid is 78 KB of PSRAM; 12 frames take about 1 MB of the 8 MB |
| D29 | Owner, 2026-10-02 (M6 plan review): GeoNames' places join the map's towns inside ČHMÚ's radar area, and GeoNames is credited (§11.1); the RTC trim keeps measuring only across syncs at least 20 h apart (§7); aircraft altitudes stay as flight levels from 10 000 ft and feet below (§11.3); M6 runs inline, one context with a review of the whole branch at the end | GeoNames is CC BY 4.0; its places of 1000 inhabitants or more add about 4 000 towns and 110 KB to the map |

### 1.3 Out of scope for v1

- Everything in §19.
- BLE and ESP-NOW sources.
- Voice features.
- Network services other than Open-Meteo, NTP and the owner's MQTT broker.
- Portrait orientation.

### 1.4 Terms

| Term | Meaning | Frequency | Radio |
|---|---|---|---|
| **Sync** | A network session: Wi-Fi on → time (NTP) → weather and air quality → MQTT (M7) → Wi-Fi off | Configurable schedule (§9.3); default once a day at 05:30 | Yes |
| **Display update** | Redraw from local data (RTC time, the latest readings, stored weather and MQTT values) and push to the panel | Configurable (§9.2); default every minute | No |
| **Sensor sample** | A reading from the SHTC3 and the battery gauge | Every 5 min (configurable) | No |
| **Wake** | The CPU leaving sleep for any of the above, or for a button, an alarm or a timeout | — | — |

## 2. Hardware constraints that shape the design

`AGENTS.md` §3 has the details.

- Firmware can read only two buttons, KEY (GPIO18) and BOOT (GPIO0). PWR is a hardware power switch.
- No GPIO reports charging or USB presence, so charging state is inferred (§8).
- The panel is write-only. It keeps its image while powered. Its low-power mode is reported to draw about 1 mA.
- The PCF85063 INT line on GPIO15 gives exact wake-ups. It has no external pull-up.
- The audio analog rail and the microSD slot are always powered.
- The USB console disappears during deep sleep.
- Baseline to beat: the vendor factory firmware draws about 90 mA.

## 3. System architecture

### 3.1 Components

| Component | Responsibility | Depends on | Host build |
|---|---|---|---|
| `main` | Init order, wiring, app event loop | all | — |
| `board` | Pin map, I²C bus, GPIO setup and ISR service, buttons → gestures, codec standby | IDF | gesture logic |
| `st7305` | Panel init, frame push, LPM/HPM, deep-sleep retention | board | frame conversion |
| `display` | Canonical framebuffer; pushes it to the panel when its CRC changes | gfx, st7305, util | — |
| `gfx` | Framebuffer, primitives, text, fonts, bitmaps, QR, PBM/BMP encoders | — | ✓ |
| `util` | Small pure-C helpers: CRC-32, base64, delay ticks, SHA-256, HMAC and PBKDF2 | — | ✓ |
| `locale` | Language packs (API prefix `lang_`, because libc owns `locale_t`): strings, date and number formats, name days and holidays | — | ✓ |
| `astro` | Sunrise, sunset, day length | — | ✓ |
| `datastore` | Measured and fetched values, freshness, derived values and trends, change mask (§6) | — | ✓ |
| `ui` | Field catalogue, layouts, widgets, status bar, presets and their JSON codec, cycle order, screens, menu, input handling | gfx, locale, datastore, util | ✓ |
| `scheduler` | Next-wake computation for display, sensors, alarms, sync, timeouts | — | ✓ |
| `sensors` | SHTC3, battery gauge | board | curve and filter logic |
| `rtc` | PCF85063 (API prefix `pcf85063_`, because ESP-IDF owns `rtc_*`): time, oscillator-stop flag, alarm → INT, the Offset register; the time set to the millisecond and its error timed (§7) | board | register codec, the offset's |
| `timekeeping` | System time from the RTC, time zone, the true time from a sync, the RTC trim, manual set | rtc | TZ logic, the trim's arithmetic |
| `power` | Power states, idle strategy, wake sources and wake cause, sleep entry, sleep statistics | board, rtc, st7305 | sleep policy |
| `netmgr` | Wi-Fi STA/AP state machine, a sync's station-only join, captive DNS, mDNS | IDF | captive DNS reply, saved-network list, scan choices |
| `webui` | HTTP server, the password and sessions, Wi-Fi, OTA and place-search routes, embedded web assets; every other API route goes to `main` | netmgr, util, weather | password record, sessions, HTTP helpers, host names, status lines |
| `weather` | Open-Meteo's forecast (with `minutely_15` from M6), air quality and place search: URLs, parsers, skies, bands and levels; the HTTPS fetch | datastore, fetch | all but the fetch |
| `fetch` | HTTPS GETs with the certificate bundle and a `reflbo/<version>` User-Agent; a session keeps its connection for the next GET to the same host and closes one the server won't keep (M6) | IDF | — |
| `png` | PNG reader for the radar images: 8-bit palette and RGBA, row by row; the ROM's `tinfl` on the device, zlib on the host (M6) | util | ✓ |
| `map` | Web-Mercator views; the built-in map (`assets/map/map.bin`: borders, coasts, towns, airports) and its drawing, with labels that keep clear (M6) | gfx, util | ✓ |
| `radar` | ČHMÚ's and RainViewer's frames: decoding into three rain levels, the store, the frame file, which views a frame covers, drawing; their HTTPS fetch on the sync task (M6) | png, map, fetch | all but the fetch |
| `adsb` | adsb.fi's aircraft on the Flights map, adsb.lol's routes and their cache; the polling task (M6) | map, fetch | all but the task |
| `ha_mqtt` | MQTT session, discovery, state, commands, field mappings | netmgr, datastore | payload builders |
| `sync` | When syncs run (the modes, quiet hours, retries, the radio's 45 s), SNTP packets, and the sync task, which fetches and reports to `main` (§9.3); from M6 the radar step and sync mode `always`'s radar-only refresh; MQTT joins in M7 | netmgr, weather, scheduler, datastore, storage | the plan and the SNTP packets |
| `audio` | Codec control, tone/WAV/stream players, alarm ringing | board, storage | — |
| `storage` | NVS (identity, secrets), LittleFS config files, microSD mount | IDF | settings codec with its defaults, config-file backup logic |
| `diag` | Console commands, screenshot export | most | — |

Rules:

- Components with ✓ in the host column must not include ESP-IDF headers. They build and run their tests on macOS. The partial entries ("gesture logic", "parser" and so on) name the pure-logic part that must be kept free of IDF headers.
- Data flows one way: drivers and services write to `datastore`, and `ui` reads from it. The UI never calls network code or drivers. It emits intents, such as "start radio" or "enter config mode", which `main` handles.
- Only `main` knows every component.

### 3.2 Runtime model

One app task owns an event queue. Event types:

| Event | Posted by |
|---|---|
| `MINUTE_TICK`, `TIMER` (cycle, menu or config timeout, seconds display) | power / scheduler |
| `BUTTON` (button, gesture) | board, `diag btn` |
| `ALARM_DUE` | scheduler |
| `SYNC_DUE`, `SYNC_DONE` | scheduler, sync |
| `DATA_CHANGED` (field mask) | datastore |
| `NET` (Wi-Fi or MQTT state) | netmgr, ha_mqtt |
| `COMMAND` (from MQTT or the web UI) | ha_mqtt, webui |
| `POWER` (battery thresholds) | sensors |

How events are handled:

1. The app task takes an event and updates its state (UI state machine, scheduler inputs). If the output changed, it renders into the framebuffer. It pushes the frame to the panel only if the frame's CRC32 changed.
2. Long-running work runs in its own task (sync, HTTP server, audio) and reports back with events.
3. When the queue is empty, the app task asks `power_plan()` whether and how to sleep, then calls `power_sleep_light()` or `power_sleep_deep()` (§3.4).

Initial task plan (finalised in the implementation plan): app (core 1), sync (core 0, created per sync), httpd (IDF default), audio stream and decode (core 1, only while playing), console REPL (low priority).

### 3.3 Boot and wake paths

- **Cold boot** (power-on, reset, OTA, panic):
  1. Init the board.
  2. Reset and init the panel, then clear it.
  3. Mount storage and load settings and presets.
  4. Read the RTC and check its oscillator-stop flag, then set system time.
  5. Restore the last persisted datastore snapshot. Its data is shown as stale according to its age.
  6. Read the sensors and render.
  7. If the time is invalid and Wi-Fi is configured, start a sync at once to fetch the time (§7). Then go idle.
- **Wake from deep sleep**:
  1. Decode the wake cause.
  2. Restore the RTC-RAM snapshot (magic, version, CRC). If it is invalid, take the cold-boot path but skip the panel reset.
  3. Re-attach SPI without resetting the panel.
  4. Re-read the RTC.
  5. Post the event (`MINUTE_TICK`, `BUTTON` or `ALARM_DUE`), handle it normally, then go idle.
- **Routine wakes** (the RTC alarm or its backup timer): skip the console, NVS and info-level logs. Each costs time on every wake, and no PC can enumerate the board before it sleeps again. The app starts all three once it decides to stay awake: after a button, while tethered, or when a `sleep test` ends. Settings needed on routine wakes (the idle strategy) are mirrored in RTC RAM.
- **Boot failure** (a driver fails to start): while a PC is attached, stay awake with the console up. Otherwise deep-sleep for 5 min, or until KEY or BOOT, then boot from scratch with a reset, so every driver and the panel start over. The RTC alarm is not a wake source then, because whatever broke the boot may hold INT low.
- **Button presses after a deep-sleep wake.** The press that woke the chip may be over by the time the app runs, roughly 100–300 ms later.
  - If the pin is already high, treat it as a short press. When the current context binds a double press, wait out the double-press window first.
  - If the pin is still low, keep timing it for a long press.

### 3.4 Idle strategies (D3)

Both strategies live in `power` (`power_sleep_deep()`, `power_sleep_light()`) until the M2 measurements pick the default. The app calls one of them when it has nothing to do (D14).

| | Deep sleep | Light sleep (`power_sleep_light()`) |
|---|---|---|
| RAM state | Lost; RTC-RAM snapshot of at most 4 KB | Kept |
| Wake latency | Full boot; 63 ms of app time per routine wake with a frame push (M2), plus ROM and bootloader | Under 1 ms; 57 ms of app time per wake with a frame push (M2) |
| ESP32-S3 floor current | µA range | Hundreds of µA (measure, PSRAM included) |
| USB console | Disconnects: the USB PHY is off | Unusable while asleep: the pad is disabled; the Mac keeps the port |
| Extra complexity | Snapshot, pin holds, a panel attach without reset | GPIO wake with level interrupts, restored to edges after wake |

- **Tethered mode.** While a USB host is connected (`usb_serial_jtag_is_connected()`, checked before every sleep), the board stays awake, because either sleep interrupts the console and flashing. After a cold boot or a button wake it stays awake 2 s so a PC can find it. `sleep test <deep|light> <n>` forces sleep cycles while tethered, for testing.
- **Decision rule at M2.** Choose deep sleep if the measured average current is clearly lower (guideline: at least 20 % lower at the one-minute cadence). Otherwise choose light sleep.
- **Result (2026-09-28): light sleep.** Deep and light sleep both drew 11.5 mA at 5.24 V (60.35 and 60.29 mW over 58 min and 1 h 55 min), because the 3V3 converter runs in forced PWM (§9.4). Deep sleep stays selectable with `power idle deep`. Measure again if the board gets the PS/SYNC rework.

## 4. Display and rendering

### 4.1 Framebuffer

- **Canonical frame.** 400×300 at 1 bpp: row-major, MSB first, 1 = black, stride 50 bytes, 15 000 bytes in total. This is exactly the PBM P4 payload.
- **Panel buffer.** 15 000 bytes in the ST7305 landscape layout, kept in DMA-capable internal RAM. **In the panel buffer, bit 1 means white.**
- **Conversion from canonical to panel.** Pixel (x, y) goes to byte `(x/2)·75 + (299−y)/4`, bit `7 − (((299−y) mod 4)·2 + (x mod 2))`. The bit value is inverted: a black canonical pixel (1) becomes panel bit 0.
  - Done as a direct loop. We don't copy the vendor's 360 KB PSRAM lookup table.
  - Host-tested against the vendor formula.

### 4.2 ST7305 driver

- **SPI.** Mode 0, 8-bit commands and parameters through esp_lcd panel IO, as in the vendor code. D/C GPIO5, CS GPIO40, RESET GPIO41, TE GPIO6. Clock 10 MHz, as in the factory sequence (D12).
- **`st7305_init(cold)`.**
  - Cold: hardware reset and a wait of at least 120 ms (datasheet §12.1.4), the factory init sequence (D12; XiaoZhi's stays selectable for diagnostics), the configured LPM rate, then clear.
  - Warm (deep-sleep wake): attach the IO only.
- **`st7305_push()`.** Set the column window (`2Ah` 0x12–0x2A) and the page window (`2Bh` 0x00–0xC7), send `2Ch`, then write 15 000 bytes.
- **`st7305_set_mode(HPM | LPM)`.** Sends `38h`/`39h` using the switching sequence in datasheet §7.11, including its delays: about 120 ms into LPM and 320 ms into HPM. The sequence's per-mode voltage step reselects voltage set 1 (`C9h`), because both vendor sequences load the same values into all four sets.
  - Idle uses LPM.
  - Menu and config mode use HPM, so new frames appear without lag. Entering HPM takes about 320 ms, which the M3 menu design must absorb, for example by switching before rendering or asynchronously.
- **LPM frame rate.** The LFRA field of `B2h` (0.25–8 Hz), written after the vendor sequence, so it is independent of that sequence. Default 1 Hz. A new rate applies at once, even in LPM. `panel rate` changes it at runtime, and the setting `display.lpm_hz` (menu Display ▸ Refresh rate) keeps it (D15). Its power cost is measured at M5.
- **Frame-rate check.** The TE output (GPIO6, enabled by `35h`) pulses once per panel frame. `panel fps` counts the pulses, which gives the real frame rate. M1 measured 1.00 Hz in LPM and about 16–17 Hz in HPM with the factory sequence.
- **RAM writes in LPM.** The panel RAM can be written in any power mode. The datasheet (§7.3) guarantees no visible artefacts when the interface writes while the panel reads. New content appears at the next panel frame, so at 1 Hz LPM a minute update shows within 1 s. Idle updates therefore stay in LPM and need no mode switch.
- **Partial updates (verified in datasheet §7.2.3, §7.4, §8.1.14–16).**
  - CASET/RASET define a RAM window, and RAMWR fills only that window. One window cell is 3 bytes: 12 source pixels × 2 gate lines. In our landscape orientation that is 12 px of *y* by 2 px of *x*.
  - Partial writes shorten only the SPI transfer. They do **not** lower panel power: every frame, the controller re-drives the whole active-matrix panel from its RAM, whether or not anything changed. Panel power depends on the power mode and frame rate.
  - What they save is CPU-awake time, and not much of it.
    - At 40 MHz a full frame takes about 3 ms. A typical clock-digit window saves about 2 ms per update, roughly 0.03 mAh/day at one update a minute.
    - At 10 MHz the saving is about 0.1 mAh/day.
    - For scale, the panel in LPM costs about 24 mAh/day.
  - The controller's Partial Display Mode (`PTLON`, 12h) is something else. It drives fewer gate lines and leaves the rest of the panel undriven, so it can't show a full-screen dashboard.
  - **v1 decision:** push full frames, and skip the push when the frame CRC is unchanged. Windowed pushes stay an optimisation candidate in case M2 measurements show they matter (§9.4).
- **Sleep-in and wake** (night sleep, §9.1).
  - Sleep-in follows datasheet §7.10: from LPM through HPM (`38h`, 300 ms), then `SLPIN` (`10h`, 100 ms). The panel stops scanning, and its image fades.
  - Both init sequences set NRDSLP (`D6h`), so `SLPOUT` would reload the NVM defaults; at M3b the panel came back at 2 Hz that way. Waking therefore resets the panel and runs the init sequence again, then pushes the frame and returns to LPM.
  - A deep-sleep wake keeps a sleeping panel asleep: the snapshot records it.
- **Before deep sleep.** Drive CS high and RESET high, then call `gpio_hold_en()` on both and `gpio_deep_sleep_hold_en()`. Release the holds on wake.
- **Test pattern (M1).** 1-px border, corner labels TL/TR/BL/BR, a 10-px grid, a checkerboard patch and a glyph sample. The owner confirms orientation and contrast, and the screenshot must match the panel.

### 4.3 gfx

- **Framebuffer type.** `gfx_fb_t { uint8_t *buf; int16_t width, height, stride; gfx_rect_t clip; }`. The fields are signed so that clip arithmetic needs no casts.
- **Primitives.** Pixel, horizontal/vertical line, line, rectangle, filled rectangle, rounded rectangle, circle, filled circle. Draw modes: black, white, invert (XOR).
- **Bitmaps.** 1-bpp images, drawn transparent, opaque or inverted.
- **Text.**
  - UTF-8 decoding and glyph lookup by binary search.
  - Measure, then draw with alignment.
  - Single-line ellipsis, or word wrap inside a box.
  - A missing glyph draws a fallback hollow box.
- **QR codes.** Nayuki qrcodegen (MIT), vendored so it also builds on the host.
- **Encoders.** PBM P4 and 1-bit BMP, used by screenshots and the web preview.

### 4.4 Fonts

- **Pipeline.** Source fonts live in `assets/fonts/` with their licences. `tools/fontgen.py` (Pillow) renders glyphs at fixed pixel sizes into C sources under `components/gfx/fonts/`. These sources are committed, so building needs no Python or Pillow.
- **Format.** Header (ascent, line height, glyph count; descent = line height − ascent), a sorted codepoint table, per-glyph metrics (w, h, x/y offset, advance), and 1-bpp packed bitmaps. Each generated file names the font's licence file.
- **Glyph coverage for text fonts.** ASCII, Latin-1 Supplement, Latin Extended-A, `° µ ² ³ € – — …` and the arrows used by widgets.
  - This covers Czech and most European languages.
  - MQTT/HA text with diacritics therefore renders correctly even with the English UI.
- **Numeric display fonts.** Large sizes can carry only digits, `: . - °` and a few letters.
- **Sizes (M3a).** Text: DejaVu Sans 12, 16 and 20 px, and Sans Bold 16, 20 and 28 px. Numeric: DejaVu Sans Condensed Bold 48, 72, 110 and 130 px. `fontgen` trims blank glyph rows and columns, and grows the ascent and line height to fit every glyph's ink.
- **Choice.** DejaVu 2.37, picked at M1 (`THIRD_PARTY.md`). Every font needs a licence that allows redistribution in this repository.

### 4.5 Icons

- A 1-bpp icon set generated into C bitmaps by `tools/imggen.py` (`tools/gen_icons.sh`) from Google's Material Icons font (Apache-2.0, pinned to one upstream commit). `assets/icons/icons.txt` lists each icon's name and sizes.
- M3a: thermometer, drop, dew, bolt (charging), stale, clock, calendar, person, celebration and cloud, at 16, 24 or 48 px as the list says. The battery and the moon are drawn with primitives. Weather codes (day and night), Wi-Fi, sync, the alarm bell, sunrise and sunset join with their milestones.
- M5 (D26): the weather codes, sunrise and sunset come from Erik Flowers' Weather Icons (SIL OFL 1.1), as Material Icons has no rain or showers. The generator draws a second font with `--font PREFIX=TTF,CODEPOINTS,LICENCE`, its glyphs fitted to the square inside Material's padding. Sync, a crossed-out cloud, Wi-Fi, a crossed-out Wi-Fi, air, particles, UV and a flower come from Material Icons.
- Sources and licences are recorded in `THIRD_PARTY.md`.

## 5. UI

### 5.1 Field catalogue (v1)

| Field id | Kind | Source | Freshness |
|---|---|---|---|
| `time.clock` | time | system time | Valid whenever the time is valid |
| `date.day` | date | system time | Same |
| `env.temp` | number, °C | SHTC3 | Stale after 15 min without a reading, or after twice the sample interval if that is longer |
| `env.hum` | number, % | SHTC3 | Same |
| `bat.level` | battery (%, V, charging state) | gauge | Stale after 15 min |
| `wx.now` | weather (code, temperature) | Weather data (see below) | Normal until the expected sync interval (§9.3) plus 2 h has passed since the last fetch; stale after that; missing past the forecast range |
| `wx.today` | weather day (code, min, max, precipitation %) | forecast | Same |
| `wx.hourly` | series: next 12 h (temperature, code) | forecast | Same |
| `wx.daily` | series: 3 days | forecast | Same |
| `sun.times` | pair (sunrise, sunset) | `astro`, computed locally | Always, given a valid date and location |
| `env.dew` | number, °C | Dew point from `env.temp` and `env.hum` (Magnus formula) | Same as `env.temp` |
| `env.temp_min`, `env.temp_max` | number, °C | Lowest and highest `env.temp` since local midnight | From the first reading of the day; reset at midnight |
| `date.week` | number | ISO 8601 week of the local date | Valid whenever the time is valid |
| `moon.phase` | moon (phase index 0–7, illumination %) | Computed from the date | Always, given a valid date |
| `bat.days` | number, days | Level ÷ the discharge rate over up to the last 24 h | Missing unless discharging, with at least 6 h of history |
| `date.nameday` | text | The language pack's name-day calendar | Valid whenever the time is valid; empty for packs without one |
| `date.holiday` | text | The language pack's public holidays | Same; empty on ordinary days |
| `aq.index` | level: the European air quality index and its band | Air quality data (§11), the hour now | As `wx.now` |
| `aq.pm25`, `aq.pm10` | number, µg/m³ | Air quality data, the hour now | Same |
| `aq.uv` | level: the UV index, rounded, and its WHO band (D26) | Air quality data, the hour now | Same |
| `pollen.top` | pollen: the type with the highest level today, and that level | Air quality data, today's peaks | Same; missing where CAMS has no pollen (outside Europe); "None" when nothing is in the air |
| `pollen.alder`, `pollen.birch`, `pollen.grass`, `pollen.mugwort`, `pollen.olive`, `pollen.ragweed` | pollen: today's peak in grains/m³, and its level | Same | Same |
| `wx.rain2h` | series: 8 × 15 min from now (precipitation, its probability) | forecast, Open-Meteo's `minutely_15` (§11.4) | As `wx.now`; missing past the 24 h stored |
| `rain.map` | rain map: the weather radar's latest frame around its centre | the weather radar (§11.2) | Its frame time always shows; after 30 min the time shows inverted with its age |
| `mqtt.<key>` | number or text, with unit and label | MQTT mapping (§12.5) | Configured TTL; default twice the expected sync interval (§9.3) |

`env.temp` and `env.hum` carry a trend: the change over the last hour. Widgets show ↑ or ↓ when it exceeds 0.5 °C or 3 %. The extra fields (from `env.dew` to `date.holiday`) are the accepted M3 proposals (D15); the air quality and pollen fields are an accepted M5 proposal (D25); `wx.rain2h` and `rain.map` come with M6 (D23, D27).

As built (M6): `wx.rain2h` is missing unless its 8 quarter hours lie within the stored day, and when none of them has an amount (Open-Meteo's nulls: no data, not a dry spell). Its bars are a third, two thirds or the whole height for light, moderate or heavy rain (below 2.5 mm/h, below 7.6 mm/h, above), solid from 50 % probability and an outline below. `rain.map` before the first frame shows "Rain map —", not the map (§20).

`wx.now` uses the `current` block while it is at most 60 minutes old. After that it uses the hourly forecast entry for the current local hour. This keeps "now" meaningful between syncs, even with a once-a-day schedule. `aq.*` take the hourly entry for the current hour in the same way. Pollen forecasts are read by the day, so `pollen.*` show today's peak.

- **Air quality bands** (Open-Meteo's `european_aqi`, the EEA's bands): 0–20 good, 20–40 fair, 40–60 moderate, 60–80 poor, 80–100 very poor, above 100 extremely poor.
- **UV bands** (WHO, D26), of the index rounded: 0–2 low, 3–5 moderate, 6–7 high, 8–10 very high, 11 and above extreme.
- **Sun times** show the day length and, in medium and large slots, its change since yesterday in whole minutes (D26).
- **Pollen levels** from the clinical thresholds CAMS uses (EAACI, checked 2026-10-01): below 1 grain/m³ none; then low; moderate from the season threshold; high from the peak threshold. Alder, birch, olive and mugwort: 10 and 100. Grass and ragweed: 3 and 50. `pollen.top` compares the types by level, then by concentration as a share of the type's peak threshold.

### 5.2 Layouts (v1)

Every layout has a status bar (top 20 px):

- Left: "Set time" while the time is invalid (§5.3); otherwise a stale warning when a shown value is stale. After either, a globe while a phone is logged in to the web UI (D20). Then the sync state (M5): a sync mark while a sync runs, or a crossed-out cloud while the last scheduled sync failed, until one succeeds. In sync mode `always`, the Wi-Fi state follows: a Wi-Fi mark while on the network, a crossed-out one while rejoining it (D19).
- Middle: a small clock, if the preset sets `status_clock`. It is meant for data-first presets (owner request, 2026-09-28).
- Right: the charging bolt and the battery icon, with the parts `status_battery` lists: level %, voltage, days left. The default is the level.
- Later: the next alarm (M8).

| Layout | Slots |
|---|---|
| Classic | `main` XL (time), `sub` M (date), bottom row `s1`–`s4` S |
| Weather | `now` L, `today` M, `hourly` M (series strip), `s1`–`s2` S |
| Grid | `g1`–`g6` M, in 3×2 |
| Focus | `main` XL, `s1`–`s2` M |
| Radar (M6) | the weather radar map, full width under the status bar (400×279 as built), with the frame's time and source and a legend (§11.2) |
| Flights (M6) | the flight radar map (400×238 as built) over a line and a 40 px panel for the nearest aircraft (§11.3) |

- Slot rectangles are fixed per layout and defined in code (`components/ui/ui_layout.c`). The owner approved them from the M3a host renders on 2026-09-28.
- Each slot declares which field kinds it accepts. The preset editor offers only compatible fields.

### 5.3 Widgets

- There is one renderer per field kind and size class (XL/L/M/S). For example, a number widget in M shows label, value and unit; in S it shows an icon and the value with its unit. A number that doesn't fit its slot first drops its decimals ("101 °F" for 100.8 °F), then steps down to smaller fonts; only if it still doesn't fit is it cut with an ellipsis. Text that doesn't fit ends in an ellipsis, and a widget never draws outside its slot.
- Each slot sets a policy for missing or stale data (a preset option):
  - `hide`: leave the slot empty.
  - `placeholder`: show `—`.
  - `stale`: show the value with an age marker: the stale icon and the age, e.g. "2 d", at the slot's bottom right.

  Default: `stale` for data that has a value, `placeholder` for data that doesn't.
- If the time is invalid (RTC oscillator-stop flag set, and the time was never synced or set), time fields show `--:--` and the status bar shows "Set time".

### 5.4 Presets

A preset is a layout, a slot → field binding and a set of options. Presets are stored in `/cfg/presets.json`:

```json
{
  "schema": 1,
  "active": "home",
  "cycle": { "enabled": false, "interval_s": 60 },
  "presets": [
    {
      "id": "home",
      "name": "Home",
      "layout": "classic",
      "in_cycle": true,
      "slots": {
        "main": "time.clock", "sub": "date.day",
        "s1": "env.temp", "s2": "env.hum", "s3": "moon.phase", "s4": "bat.level"
      },
      "options": { "clock_24h": true, "seconds": false, "invert": false, "stale_policy": "stale",
                   "status_clock": false, "status_battery": ["percent"] }
    }
  ]
}
```

- **Built-in defaults.** Home (Classic), Indoor (Grid, with the status clock), Weather and Focus clock are compiled in. They are used when the file is missing or invalid. The built-in Weather preset joins the cycle from M5, which brings its data; a `presets.json` saved earlier keeps its own choice. M6 adds Rain radar (the Radar layout, in the cycle) and Flights (the Flights layout, D28): the cycle visits Flights only in sync mode `always`, and outside it a preset on that layout says "Flights need sync mode Always on". A `presets.json` saved before M6 gains both once, at the first boot that knows them, if there is room; the file then carries a marker, so a preset deleted later stays deleted. There can be at most 16 presets.
- **Options.** `clock_24h` overrides the time setting when present. `status_clock` and `status_battery` shape the status bar (§5.2).
- **Validation.** A file with a structural error is rejected as a whole, and the error names it. Structural errors:
  - not JSON, or another schema;
  - no presets, or more than 16;
  - a bad or duplicate id;
  - an unknown layout, slot or field, or a field the slot can't show;
  - an unknown `stale_policy` or `status_battery` value;
  - nesting deeper than 16 levels, or `slots` that isn't an object;
  - a bad schedule: more than 8 entries, or an entry with a bad time, action or preset, or a night that ends at the minute it starts.

  Everything else is lenient:
  - an unknown `active` selects the first preset;
  - the cycle interval is clamped to 10 s–1 h;
  - a missing name takes the id, and a name longer than 23 bytes is cut at a character boundary;
  - missing or `null` options take their defaults;
  - `null` or `""` leaves a slot empty.
- **Switching.** Presets change by:
  - KEY short: next preset in cycle order.
  - The auto-cycle timer: interval at least 10 s; each switch costs a wake-up.
  - The HA `select` entity (§12.3).
  - The web UI or the menu.

  A manual switch is saved in `presets.json`, so it survives a reboot. An auto-cycle switch is not saved.
- **Seconds.** `seconds: true` needs a wake-up every second. It is allowed, and the web UI shows the power cost.
- **Schedule** (D15). `presets.json` holds `"schedule": { "enabled": true, "entries": [...] }` with up to 8 entries: `{ "at": "22:30", "days": 127, "action": "preset", "preset": "focus" }` or `{ "at": "23:00", "days": 127, "action": "night", "until": "06:00" }`. `days` is a Mon–Sun bitmask (bit 0 = Monday, default all). An entry runs at its local minute, with the same DST rules as user alarms (§9.2).
  - `preset` makes that preset active. Manual switching and auto-cycling carry on from there.
  - `night` starts night sleep (§9.1) until `until`, which may be on the next day.
  - Entries of the same minute run in list order, presets before a night, because nothing runs once a night has started.
  - Entries run only while the time is valid, and not on the critical-battery screen. They don't run late: not those a night covers or a clock change skips, not those missed while the board was off, and not after a gap longer than the display interval plus 5 minutes, such as the critical sleep.
  - A night covers the minutes from its start up to its end, so an entry at the end minute runs: night 23:00–06:00 with a preset at 06:00 switches the preset in the morning.
  - A preset entry's switch is not saved, like an auto-cycle switch.
  - Until the web UI (M4), the `schedule` console command edits the entries (§15).

### 5.5 Screens

| Screen | Content |
|---|---|
| Dashboard | The active preset |
| Menu | §5.7 |
| Config mode | Wi-Fi state; URL (`http://reflbo-XXXX.local`) and IP; a QR code to join the AP or open the URL; time left before timeout |
| First run | "Hold BOOT 3 s to set up Wi-Fi · Hold KEY for menu · Press KEY to continue", plus the clock if the time is valid. Shown at boot when `settings.json` doesn't exist: a new board, or after a factory reset (D19). Whichever button leads on saves the settings, so it doesn't come back |
| Alarm ringing | Large time, the alarm label, button hints |
| Radio | Station, ICY title, volume, a battery warning when on battery |
| Critical battery | A large empty battery, "Battery empty" and "Please charge me", with the time and date it was drawn. Nothing else updates |
| Toast | A short overlay near the bottom, in a black box (e.g. "Preset: Weather", "Sync failed"), shown for 3 s. The board stays awake meanwhile |

`XXXX` is the last two bytes of the Wi-Fi STA MAC in lowercase hex. The same id is used for the hostname, AP SSID, MQTT client id and device id.

### 5.6 Controls

- **Gesture timings.**
  - Debounce: 30 ms.
  - Double press: 300 ms window, waited for only when the context binds a double press.
  - Long press: fires at 1 s while the button is still held.
  - BOOT on the dashboard: long press fires at 3 s instead, so Wi-Fi doesn't switch on by accident.
  - In the menu KEY has no double press, so a short press answers at once, and BOOT's long press fires at 1 s.
- A context binds at most one long gesture per button.

| Context | KEY short | KEY double | KEY long | BOOT short | BOOT long |
|---|---|---|---|---|---|
| Dashboard | Next preset | Auto-cycle on/off | Open menu | Refresh sensors | Config mode (3 s) |
| Radar layout (M6) | Next preset | Auto-cycle on/off | Open menu | Play the last hour in sync mode `always`; otherwise refresh sensors (§11.2) | Config mode (3 s) |
| Menu: browsing | Next item | — | Select / enter | Back | Exit menu |
| Menu: editing a value | + | — | Confirm | − | Cancel |
| First run | Dashboard | — | Open menu | — | Config mode (3 s) |
| Config mode | Toggle QR (join AP / open URL) | — | — | — | Exit config mode (1 s) |
| Alarm ringing | Snooze | Snooze | Stop | Snooze | Stop |
| Radio | Volume + | Next station | Open menu | Volume − | Stop radio |

The `diag` console command `btn` injects the same gestures. Holding BOOT at power-on still enters download mode; holding it at runtime is safe.

### 5.7 Menu (v1)

```
Presets    ▸ Active preset · Auto-cycle on/off · Interval (10 s … 1 h) · Schedule on/off
Alarms     ▸ Alarm 1–8: on/off · Time · Days · Sound · Volume        (M8)
Radio      ▸ Play/stop · Station · Volume                            (M8)
Wi-Fi      ▸ Config mode · Forget networks · Reset web password
Sync       ▸ Sync now · Schedule (times / interval / always / manual) · Interval · Quiet hours on/off
Time       ▸ Set date and time · 24-hour clock · Time zone (short list)
Display    ▸ Contrast · Update interval (1–15 min) · Refresh rate (0.25–8 Hz)
Sensors    ▸ Temperature offset · Humidity offset · Units (°C/°F)
Info       ▸ Battery V/% · Firmware · IP/MAC · Last sync result · Uptime · Free heap
System     ▸ Language (English, Čeština) · Reboot · Factory reset (with confirmation)
```

- The menu closes after 60 s without input.
- Items for features that don't exist yet (Wi-Fi before M4, Sync before M5, Alarms and Radio before M8) are hidden, not shown disabled.
- Schedule entries are edited in the web UI (M4); until then `presets.json` or the console sets them.
- Display ▸ Contrast is hidden for now (D17). Last sync result joins Info with M5.
- Sync (M5): Interval shows in `interval` mode only. The times of the `times` mode and the span of the quiet hours are set in the web UI; the menu's Quiet hours switches them on and off.
- As built (M3b):
  - A toggle flips at once. A choice or a number is edited in place and saved with KEY long.
  - The temperature offset steps by 0.1 °C (±10 °C, D17), the humidity offset by 0.5 % (±20 %).
  - The time zone list holds 14 zones. A zone set elsewhere stays selectable.
  - Info shows the device id `reflbo-XXXX`, the firmware version with the first bytes of its ELF hash, and the uptime since the first valid clock after a cold boot.
  - Reboot doesn't ask. Factory reset asks, and only KEY held confirms it.
  - The date-time editor starts from 2026 when the clock reads an earlier year, as after a power-off without the backup cell (D9).
  - The panel is in HPM while the menu is open.
- As built (M4):
  - Wi-Fi ▸ Config mode starts config mode at once. Forget networks and Reset web password ask first, like Factory reset, and end with a toast.
  - Info shows IP and MAC as two rows. The IP shows only while Wi-Fi is on, otherwise a dash.
  - A value too long for its row, such as a zone the web UI set, is cut with "…". A short label keeps its width; a long one gets half the row.
- As built (M5):
  - Sync ▸ Sync now starts a sync at once, without asking, and ends with a toast; on the device's own network alone it is refused, as nothing can be reached. Schedule is a choice of the four modes, Interval shows in `interval` mode, Quiet hours toggles.
  - Info ▸ Last sync reads "HH:MM OK", or the time, the first step that failed and why. The IP row shows whenever Wi-Fi runs, in config mode or not.
- The full time zone picker is in the web UI.

### 5.8 Language packs

- **What a pack holds.** Each pack in `locale` is a set of C tables:
  - String ID → UTF-8 text.
  - Weekday and month names, including short forms.
  - Date and time patterns.
  - Decimal separator.
  - First day of the week.
  - The character set the pack needs, so `fontgen` can check coverage.
- **v1.** Ships `en` and `cs` (D15). `settings.language` selects the pack.
- **Czech extras.** The `cs` pack also holds the public holidays: 1 January, Good Friday, Easter Monday, 1 and 8 May, 5 and 6 July, 28 September, 28 October, 17 November and 24–26 December (zákon č. 245/2000 Sb.). Easter is computed. The Czech name-day calendar (one entry per day of a leap year) waits for a source whose licence allows redistribution (D16, §20), so `date.nameday` stays empty with `cs` for now. `en` has neither table, so `date.nameday` and `date.holiday` stay empty with it.
- **Web UI.** English only in v1.

## 6. Datastore

- **Table.** An entry for each measured or fetched field: `env.*` and `bat.*` since M3a, weather from M5, and up to 32 dynamic `mqtt.<key>` entries from M7. Fields that follow from the clock (`time.*`, `date.*`, `moon.phase`) are computed by `ui` at render time and never stored.
- **Entry contents.**
  - A fixed-point value: 0.01 °C, 0.01 %, whole %, or 0.1 days. Short text (at most 48 bytes of UTF-8), times and weather structs arrive with the fields that need them.
  - The trend and `updated` (UTC); a `ttl_s` per field. The battery entry also holds the voltage and the charging state.
  - Units and labels live in the `ui` field catalogue.
- **Derived values.** Each SHTC3 reading also sets:
  - `env.dew`, from the Magnus formula (17.62 and 243.12 °C);
  - today's `env.temp_min` and `env.temp_max`, restarted at local midnight;
  - the trends: the change since the newest reading that is 60–90 min old, from a 16-point history spaced at least 5 min apart.
- **Weather storage.** Kept compact: 72 hourly entries (int16 temperature ×10, uint8 code, uint8 precipitation %) and 3 daily entries.
- **Air quality storage** (D25): 72 hourly entries (uint8 index, PM2.5 and PM10 in whole µg/m³, the UV index in tenths, each capped at 254), and each pollen type's peak for 3 days (uint16, 0.1 grains/m³). Weather and air quality together add about 700 bytes to the snapshot.
- **Rain in the next 2 h** (M6, D27): 96 entries of 15 min (uint8 precipitation in 0.1 mm, capped at 25.4 mm, and uint8 probability %), 24 h from the forecast's fetch: 192 bytes, which keeps the snapshot under its 4 KB. The radar's frames are kept outside the datastore (§11.2).
- **API.** Setters and getters, a freshness check (missing, fresh, stale) and a change mask. The app task owns the datastore (`AGENTS.md` §5.3). Other tasks reach it through events, and console commands through the app's executor, so it needs no mutex.
- **Snapshot.** The datastore is plain data inside the app's RTC-RAM snapshot (magic, version, CRC32, at most 4 KB in total), sealed before every deep sleep. From M5 the forecast and the air quality are also written to `/fs/state/datastore.bin` after every sync that brought one (magic, version, CRC32), so they survive a power-off and come back at the next cold boot, marked as stale by their age.
  - As built (M5): the snapshot is 3392 bytes (version 6), with the syncs' state (§9.3) beside the datastore.
  - As built (M6): 3616 bytes (version 7) with the rain; `/fs/state/datastore.bin` is version 2. An older one is left out at the first M6 boot, and the missing forecast makes the board sync at once (outside sync mode `manual`).

## 7. Timekeeping

- The PCF85063 stores UTC. At boot and on every wake the firmware reads the RTC and sets the system time from it. If the oscillator-stop flag is set, the time counts as invalid until SNTP or a manual set.
  - The running clock keeps its fraction of a second while it agrees with the RTC within a second; setting it to the RTC's whole second at every read lost each read's latency, and a seconds display skipped a second now and then. The RTC's minute alarm fires as the RTC's second begins, so that wake takes the phase.
  - Seconds keep the panel refresh rate the user picked (D20); a user who sees them stutter raises it.
- The time zone is a POSIX TZ string in the settings (default `CET-1CEST,M3.5.0,M10.5.0/3`), stored alongside its IANA name for display. The web UI maps IANA names to POSIX strings.
- SNTP runs during each sync (servers `time.ntp`, default `cz.pool.ntp.org` and `pool.ntp.org`, 5 s timeout) and writes the result to the RTC.
  - As built (M5): our own client (`sync_ntp`) instead of lwIP's, which would set the clock itself on its own task without taking out the round trip. One request per server; the reply must answer that request (its originate timestamp) and is refused for leap indicator 3, stratum 0 (a kiss-o'-death) or above 15. The sync task only reports the true time at a monotonic instant; the app task times the RTC's error, sets the system clock and sets the RTC to the millisecond. Measured from Brno: round trips of 13–27 ms to `cz.pool.ntp.org`.
  - **To the millisecond.** The STOP bit holds the RTC's prescaler while the time is written, and is released 0.5078 s before the next whole second: the first tick comes 0.507813–0.507935 s after the release (datasheet §8.2.1.2), so the RTC's seconds begin with the true ones.
  - Any other clock set goes through `app_clock_moved()`, as the menu's does: it shifts the battery history, restarts the schedule checks and the cycle interval, and renders.
- **RTC trim** (D25). The crystal runs slow: about 3.4 s a day (40 ppm) at M2, as the board loads it with more than its rated capacitance (`AGENTS.md` gotcha 7).
  - The Offset register (02h) corrects it. MODE 0 corrects once every two hours, 4.34 ppm a step, from −64 to +63; a positive value slows the clock. The correction comes in 1⁄32 s pulses, so the RTC's error swings by up to the step count × 1⁄32 s within each two hours.
  - At each SNTP sync the RTC's error is read to the millisecond, by watching for its next second. Divided by the time since the last set to the millisecond, it is the drift; the crystal's own error is the drift plus 4.34 ppm × the offset in effect, and the new offset is that error ÷ 4.34 ppm, rounded.
  - Only after at least 20 h, so the swing above weighs little: ±0.14 s over a day is ±1.6 ppm, against a step of 4.34 ppm.
  - A manual set (menu, `rtc set`, phone) is only good to a second, so the next SNTP sync sets the RTC without measuring; so does a sync after the oscillator stopped.
  - Kept in NVS `sys` (`rtc_trim`: the offset, when the RTC was last set to the millisecond, the last drift), as it belongs to the board: a factory reset keeps it. It is written to the chip at every cold boot, as the chip loses it with its power (D9); a deep-sleep wake keeps the chip's offset and loads the record when it is first used, once NVS is up (M5 review).
  - As built (M5): every sync sets the RTC to the millisecond and starts a new measurement, so only syncs at least 20 h apart trim it: the default schedule does, `interval`, `always` and several daily times don't (§20).
  - Cost: nothing while idle (the correction runs inside the RTC); up to 1 s of I²C reads per sync.
- Manual set: the date/time editor in the menu, or "set time from phone" in the web UI (sends the epoch and time zone).
- RTC configuration: CLKOUT is disabled (`COF = 111`) to save current. The RTC alarm serves the wake scheduler (§9.2). User alarms are evaluated in firmware.
- **Without a backup cell (the current state, D9).**
  - The RTC keeps time as long as the board has power: while running on battery, and through deep and light sleep.
  - It loses the time when PWR switches the board off, or when the battery is removed or runs flat with no USB attached.
  - On the next boot the oscillator-stop flag is set. With Wi-Fi configured, the firmware syncs at once to fetch the time, and while that fails, again after 15, 30 and 60 min and then every 60 min (M5 review). Without Wi-Fi it shows "Set time" and waits for a manual set.
  - Alarms stay suspended until the time is valid.
- **Fitting a cell later.**
  - Use a rechargeable ML1220 with leads and a 2-pin 1.0 mm plug for connector J7. Never use a CR1220: the board charges the cell whenever it is powered.
  - Per the schematic, J7 pin 1 is + (RTC_BAT) and pin 2 is GND. Pre-wired cells don't all share one pinout, so check the polarity with a multimeter before plugging one in.
  - No firmware change is needed.

## 8. Sensors and battery

**SHTC3**

- Sampled every 5 min (configurable 1–30 min) and on BOOT short. The sensor is sent to sleep after each reading.
- Low-power measurement mode (about 0.8 ms instead of about 12 ms) is used if M2 shows its accuracy is good enough.
- Temperature and humidity offsets are settings. The humidity with its offset stays within 0–100 %. The temperature offset defaults to −2.0 °C: the board warms the sensor by about 2 °C at all times (owner, 2026-09-30, D19). The user trims it against a reference thermometer in the menu or the web UI.
- Readings are taken right after wake, before Wi-Fi or the CPU warm the board.

**Battery gauge**

- **Reading.** ADC1_CH3 one-shot at 12 dB with curve-fitting calibration. Average 16 samples, then multiply by 3 (the divider) and by a per-device factor (default 1.000).
- **When.** Every 5 min and before each sync, in config mode too (D20): the radio's load pulls VBAT down, which errs on the safe side, as a battery that sags under it would brown out anyway. Not during audio.
- **Level.** Voltage maps to % through a Li-ion open-circuit-voltage table (NCR18650B-like). The result is smoothed with an EMA and never jumps up unless charging is inferred.
- **Calibration** (owner request, 2026-09-30; settings `battery.*`, the web UI's Device page).
  - `curve`: the built-in table, 3.27 V for 0 % to 4.20 V for 100 %. The default.
  - `manual`: the same table stretched between the owner's empty and full voltages (empty 3.0–4.0 V, full 3.6–4.4 V, at least 0.3 V apart), so the shape stays.
  - A change applies at once: the level follows the new mapping, and the days-left estimate starts over, as its history is in the old levels.
  - `learned`: a curve learned from one full discharge (D21), below. The critical thresholds stay in volts whatever the calibration.
- **Learning the curve** (D21; the Device page, or `battery learn start|stop`).
  - Started, it waits for a charge: readings while charging, or at the charger's plateau, set the start. The discharge begins when the smoothed voltage falls 10 mV below that plateau.
  - It records the smoothed voltage once an hour, 256 readings at most; when they run out they thin to every other one, twice as far apart, so any length fits.
  - It ends at the critical level (3.3 V). This board's load is steady, so the share of the time gone is the share of the charge used: the voltage at every 5 % of the time becomes a 21-point curve, saved as `battery.learned_mv` with `battery.learned_at`, and the level switches to it.
  - A discharge shorter than 12 h is no curve: something drew far more than this board does. A charge on the way, or a gap of more than 2 h between readings, starts the wait again. A clock set moves the session with it; a clock that was never set (before 2020) records nothing.
  - The session lives in the sensors' RTC-RAM state through deep sleep, and in `/fs/state/battery_learn.txt` (base64 with a CRC) whenever it gains a point, so a restart keeps it. Its cost: one LittleFS write an hour.
- **Charging state (inferred; there is no hardware signal).**

  | State | Rule |
  |---|---|
  | `charging` | VBAT rose at least 30 mV over the last 30 min while idle |
  | `full` | VBAT at or above 4.15 V and steady |
  | `discharging` | Otherwise |
  | `unknown` | Not enough history yet |

  `usb_serial_jtag_is_connected()` additionally marks external power when a PC is attached.
- **Thresholds.**
  - Low, 15 %: a "!" beside the status bar's battery; sync retries are skipped.
  - Critical, 3.3 V or below while not charging: the critical screen. Only KEY wakes the device. If KEY is still held, usually the press that asked for the check, its release wakes the chip, with a 10-minute timer in case it is stuck. While a PC is attached the board stays awake on the critical screen, as a tethered board does (D14). The screen stays until the battery reads 3.4 V, or until charging is seen, so it doesn't flicker at the threshold.

## 9. Power management and scheduling

### 9.1 Power states

| State | When | Panel | Leaves by |
|---|---|---|---|
| ACTIVE | Menu, config mode, alarm ringing, radio | HPM for menu and config mode; otherwise LPM | Timeout (menu 60 s, config 10 min, alarm auto-stop 10 min) or user action |
| IDLE | Dashboard | LPM | Any event |
| SYNC | Sync running | LPM, with a sync indicator | Sync done |
| CRITICAL | Battery at or below 3.3 V | Final screen | KEY press, if the voltage has recovered |
| NIGHT | A `night` schedule entry (§5.4), or `night <minutes>` on the console for measuring | Sleep-in: the image is blanked (datasheet §7.10: via HPM, then `SLPIN`) | The entry's end time, or KEY/BOOT |

**Night sleep** (D15):

- The chip deep-sleeps whatever the idle strategy, woken only by KEY, BOOT or the end time (the RTC alarm, with the usual backup timer). Nothing is sampled or drawn. It sleeps even while a PC is attached, so the console drops.
- A button press wakes the panel (with the init sequence again, §4.2) and shows the dashboard.
  - The press that woke it does nothing else.
  - The board stays awake until 60 s after the last press, then night sleep resumes.
  - KEY long still opens the menu. A night that comes due while the menu is open waits until it closes.
  - A night that starts with a button held checks again every minute, so the button can wake it once released (D16).
- At the end time the device renders the dashboard and carries on as usual.
- The owner measures its current against the idle strategy, to see whether it saves enough to keep (M3 acceptance).

### 9.2 Wake scheduler

- `scheduler_next_wake(now, state)` returns the earliest of these:
  - The next display update: every `display.update_min` minutes (1–15, default 1, aligned to the minute), every second when a preset shows seconds, and the next cycle switch.
  - The next sensor sample.
  - The next user alarm, including snoozes.
  - The next sync.
  - The next schedule entry (§5.4).
  - Any running timeout.
- It is a pure function. Host tests cover DST transitions.
- **Minute-aligned wakes use the PCF85063 alarm.** When the alarm fires, the chip latches the AF flag and holds INT low until firmware clears it, which triggers the ext1 wake reliably. The alarm is programmed before each idle. Other wakes use the ESP timer. A backup ESP timer set about 5 s after the RTC alarm covers a missed INT.
- **Held buttons** (D16). If KEY or BOOT is still low when the board goes to sleep, that sleep leaves it out of the wake sources and relies on the RTC alarm and its backup timer. The button becomes a wake source again at the first sleep after it is released, so a stuck button, or the board lying on one, can't keep it awake. A button still held after its gesture has fired doesn't keep the board awake either, and its release gives nothing.
- **User alarms** are checked at minute ticks in local time.
  - On spring-forward, a time that doesn't exist fires at the first valid minute after it.
  - On fall-back, a repeated time fires once.

### 9.3 Sync schedule and sequence

The sync schedule (`settings.sync`) is fully configurable from the menu and the web UI:

| Mode | Behaviour | Parameters |
|---|---|---|
| `times` (default) | Sync at fixed local times | 1–8 times of day; default `05:30` |
| `interval` | Sync every N minutes, aligned to the clock | N = 15–1440 |
| `always` | Wi-Fi stays up (from M5, D24), and MQTT with it (from M7). Weather and air quality refresh every 60 min, the weather radar every 5 min (M6). State is published on change (at most every 30 s) and every 5 min. Commands and MQTT fields apply at once. Meant for USB power; not auto-detected | — |
| `manual` | Sync only on demand | — |

- The UI offers shortcuts: *Battery saver* = `times ["05:30"]`, *Balanced* = `interval 60`, *Always connected* = `always`.
- In every mode a sync can also be started on demand: from the menu, the web UI or `sync now`.
- **Expected sync interval.** Data freshness and MQTT `expire_after` depend on it:

  | Mode | Expected interval |
  |---|---|
  | `times` | Largest gap between consecutive times (24 h for a single time) |
  | `interval` | N |
  | `always` | 60 min for weather and air quality; 10 min for MQTT (M7) |
  | `manual` | None: data never expires and only shows its age |

- **Quiet hours** (D25). `sync.quiet` sets a span of local time, such as 23:00–06:00, in which no sync starts by itself. Off by default.
  - A scheduled time, an interval slot or a retry that falls inside moves to the span's end, so one sync runs as it ends and the morning starts with fresh data.
  - In `always` mode Wi-Fi goes off at the span's start, so the board sleeps as usual, and comes back at its end.
  - A sync on demand still runs, and so does the sync at boot that fetches a lost time, as the hour isn't known then.
  - The span may cross midnight; a span of no minutes counts as off. The web UI says when a sync time falls inside it.
- **In config mode** a sync that comes due runs over config mode's station, if it has one, and leaves Wi-Fi on; on the AP alone it waits until config mode ends. Leaving config mode in `always` mode keeps Wi-Fi and the web UI on.
- **Radar refresh and flights** (M6): in `always` mode a radar-only refresh comes every 5 min (RainViewer: 10), without SNTP or the forecast and outside the sync's history and retries; the flight radar polls adsb.fi from its own task while its view is on screen (§11.3).
  - As built (M6): the refresh comes a minute after each 5-minute step (`SYNC_RADAR_DELAY_S`), at once when `always` takes Wi-Fi, so the past hour arrives in one go (12 frames in 9.2 s on the board); one new frame took 0.6–0.8 s. It shows neither the status bar's sync mark nor the Sync page's "running". A sync asked for during a refresh starts when the refresh ends, 30 s at most, and shows as running from the moment it is asked.
- **In `always` mode** the board stays awake, as neither sleep keeps Wi-Fi (D14), and netmgr keeps rejoining a network it lost. The web UI is reachable on the LAN (§10.4). A critical battery turns Wi-Fi off, as it ends config mode (§8).
- **Results.** Each step's result and the sync's time are kept until the next sync, also through deep sleep. Info ▸ Last sync result shows the time and the first step that failed; the web UI's Sync page shows every step.
- **Power** (measured at M5, §9.4): a sync wakes the radio for some seconds at about 100 mA; `always` keeps the chip awake with Wi-Fi in modem sleep, tens of mA.

Sequence. The steps are independent and each has a timeout. The radio may be on for at most 45 s in total. The sync runs in its own task; the app task applies its results (§3.2).

1. **Wi-Fi connect.** Saved networks, trying the cached BSSID and channel first. A sync never starts the AP.
2. **SNTP.** 5 s. Sets the RTC to the millisecond and trims it (§7).
3. **Weather.** 10 s.
4. **Air quality** (D25). 10 s.
5. **Radar** (M6, §11.2). 10 s: one ČHMÚ frame, or RainViewer's index and the view's tiles. As built: ČHMÚ's file is named from this sync's NTP time when its time step worked, as the app sets the clock only after the sync; else from the clock if it is valid; with neither, the step fails with "no time". A step that never ran, as Wi-Fi didn't come up, leaves the radar's status as it was.
6. **MQTT** (M7). 15 s.
   1. Connect with a persistent session.
   2. Subscribe to field and command topics.
   3. Collect retained and queued messages until 1 s passes with none, or until every mapped topic has arrived.
   4. Publish state, plus discovery if the config or firmware changed.
   5. Disconnect.
7. **Finish.** Wi-Fi off, unless config mode or `always` mode keeps it. Persist the datastore snapshot. Record each step's result (shown in Info and the web UI).

A sync fails when any step fails. On failure, retry after 15, 30 and 60 min, then wait for the next scheduled sync. At low battery there are no retries.

As built (M5):

- **The 45 s.** The join starts no attempt that would end after 25 s (an attempt takes up to 8 s, two a saved network); each later step gets the time that is left, and one with less than 1 s left fails as "timeout".
- **Syncs the device needs.** The lost time: at once, and while it fails after 15, 30, 60 and then every 60 min (§7). No forecast yet, as after saving the first network or a first boot: at once, unless a sync failed since. Sync mode `always` with Wi-Fi off, as after a night or quiet hours: at once, unless a sync failed since, so a router that is off follows the retries.
- **A night** (§9.1) turns `always` mode's Wi-Fi off, like quiet hours, so the board sleeps; Wi-Fi comes back as the night ends.
- **Leaving `always` mode** from the web UI: Wi-Fi goes off 3 s after the last request, so the page gets its reply; the board stays awake until it is off.
- **Measured** on 2026-10-01, the board on the home network at −87 to −88 dBm: 3.2–4.4 s for a whole sync (the join about 2 s, SNTP 0.1–0.2 s, the forecast and the air quality about 1 s each); 13–27 s when the server or the weak link was slow; one forecast that timed out at 10 s. The lowest free internal heap with config mode and a sync together: 80 KB.

### 9.4 Power budget and measurement

**Measurement method** (owner, USB power meter):

- Power the board from a USB charger, not a PC. The console then stays off, and the firmware uses its normal sleep policy.
- Remove the 18650, or make sure it is fully charged, so charging current doesn't distort the readings.
- For each idle scenario, use the meter's mAh counter over at least 1 h. For events (sync, alarm), record the mAh over N repetitions.
- Estimate battery-side current as `I_bat ≈ I_5V × 5 V / V_bat`. This ignores converter losses; say so when reporting.
- Scenarios:
  - M2: deep sleep vs light sleep while idle.
  - M5: energy per sync, and sync mode `always`.
  - Config mode.
  - M6: a radar frame per sync, and the flight radar in sync mode `always`.
  - M8: radio.
- Record results in `docs/power.md` with the date, commit, settings and meter model. Many USB meters are inaccurate below 1 mA, so long accumulation windows matter.

**Measured at M2:** a floor of about 60 mW at 5.24 V (11.5 mA) in either sleep, with the chip awake 0.1 % of the time. 0.87 mA of it is the USB side (charger and power latch, read with the board switched off). Most of the rest is the TPS63020 3V3 converter: its PS/SYNC pin is tied high, which forces PWM, and TI gives about 10 % efficiency at 1 mA in that mode. The converter loss stays on battery too, so the stretch goal (D6) needs a board rework: PS/SYNC to GND enables power-save mode (25–50 µA quiescent).

**Rough budget for the stretch goal** (assumes the converter in power-save mode; to be replaced by measurements):

| Consumer | Estimate | Basis |
|---|---|---|
| Panel in LPM | ~1 mA | Community driver report |
| Board quiescent (converters, dividers, codecs in standby, RTC) | 0.1–0.3 mA | Estimate |
| ESP32-S3 while idle | Deep sleep ~0.01 mA; light sleep ~0.3–1 mA | Estimate |
| Once-a-minute wakes | ~0.17 mA | 1440 × ~0.2 s × ~50 mA ≈ 4 mAh/day |
| Daily sync | ~0.01 mA | ~10 s × ~100 mA per day |

Optimisation candidates (evaluated at M2/M5, not features): LPM frame rate, CPU frequency, SHTC3 mode and interval, Wi-Fi fast connect and TX power, fast-boot options, windowed panel pushes (§4.2), and skipping unchanged frames (already designed in).

## 10. Networking and web configurator

### 10.1 Wi-Fi manager

- **Saved networks.** Up to 5 SSID/password pairs in NVS. They are tried in order of last success, 8 s per attempt.
- **Fast connect.** BSSID and channel are cached in NVS, written only when they change, so frequent syncs don't wear the flash. A try with the cached BSSID that fails is repeated with a scan, as the router may have moved.
- **Hostname.** mDNS `reflbo-XXXX.local`, advertising `_http._tcp`.
- **AP.**
  - SSID `reflbo-XXXX`, WPA2-PSK with a random 10-character password generated at first boot and kept in NVS.
  - IP 192.168.4.1; at most 2 clients.
  - Captive DNS answers every name with the AP IP, and OS connectivity-probe URLs redirect to `/`. DHCP offers `http://192.168.4.1/` as the captive portal (RFC 8910).
  - The station shares the radio, so the AP must follow a network the station joins onto its channel, which drops the AP's clients. The AP therefore starts on the channel of the strongest saved network in sight, else of the strongest network, else 1: trying the likely network doesn't move it.
- **Testing a network** (web UI). A scan comes first: a network out of reach is reported at once and never takes the AP off its channel. The device then joins by BSSID and channel, beside the AP. On success the network is saved first in the list and the device stays on it; on failure it returns to the network it was on. The test runs in the background, and the page polls for the result, as the phone may drop off the AP for a moment. Leaving the old network for the test never counts as the new one failing, however late its event comes.

- As built (M5): a link that drops rejoins any AP of its network, not only the one it joined, as a mesh node or a new router may carry it. A station without an address counts as not joined: config mode then offers its own network, and a sync joins again. Saving the networks tells the app, which plans the syncs by them.

### 10.2 Config mode

- **Entry.** BOOT long (3 s) on the dashboard, the menu, or first run.
- **Connecting.** With saved networks, it joins as a station and shows the LAN URL and IP. If that fails or no network is saved, it starts the AP and captive portal. While the owner tests a new network from the web UI it runs AP and STA together. While no web password is set, it runs the AP as well, because only a phone on the AP may choose the password (§10.4).
- **Exit.** BOOT long, "Done" in the web UI, or 10 min without HTTP requests. Wi-Fi then switches off, unless sync mode `always` keeps it on the network (§9.3).
  - Only requests for the device's own pages and API count: a joined phone's connectivity probes would otherwise keep config mode on for good.
  - A critical battery (§8) ends it too; battery samples go on while it runs (D20).
- **While it runs.** The board stays awake, as neither sleep keeps Wi-Fi, and the panel is in HPM. The screen shows the state, a QR code and the minutes left; KEY switches the QR code (§5.6).
  - While a phone is logged in to the web UI, the screen shows the dashboard instead, with a globe in the status bar, so settings can be watched as they change (D20). KEY short brings the setup screen back for another phone ("KEY back"), and KEY again returns to the dashboard. Logging out, or the last session ending, brings the setup screen back.
- **Restarts.** A restart the web UI asks for (a firmware update, Restart) comes back in config mode, so the page finds the device again; so does the image a rollback returns to (§10.5, D19). The flag is in NVS (`sys/resume_cfg`): another image lays out RTC RAM differently.

### 10.3 Web UI and REST API

- **Tech.** Plain HTML/CSS/JS, mobile-first. The files are gzipped and **embedded in the app image**, so OTA updates them and flashing never touches user data.
- **Pages.** Status, Wi-Fi, Location & time, Device, Presets (editor with live preview), Firmware and Backup in M4; Sync (M5), Radar (M6), MQTT/HA (M7), Alarms and Radio (M8) join later.
  - Status sets the device's clock from the phone at once when the device has lost the time (D9, D19).
  - Device holds the menu's language, units, sensor offsets, sensor interval, update interval and refresh rate (D19), and the battery calibration (§8).
  - Presets: the preview names each slot at its corner as the slot fields call it; a new preset joins the cycle; "Undo changes" asks first, as the save bar can float over other buttons (D20).
  - Sync (M5): the mode with its shortcuts, the times or the interval, the quiet hours (D25), "Sync now" with its progress, the last sync's steps, the next sync, and the RTC's trim and drift (§7).
  - Location & time (M5): a place search through `/api/geocode`, which fills in the name, latitude and longitude.
  - Radar (M6): a card for each radar: its centre (the location, the place search or coordinates) and zoom or range, the flight radar's filters, a live preview through `/api/preview.bmp`, and the credits (§11.2, §11.3). The flight radar's card says it runs only in sync mode Always on.
    - As built (M6): the previews draw the saved settings and refresh after each save; the zoom is a list with each step's width in km; the flight radar's card shows the last poll's failure ("Last poll: failed: too big") and adsb.lol's pause; below both cards, the map's credits: Natural Earth, OurAirports and GeoNames (CC BY 4.0, D29).
- **API.** JSON. Mutating requests must send `Content-Type: application/json`, those without a body too.
  - The server reads each body once, before any route and any login: at most 16 KB (413), nested at most 18 levels, a backup bundle's depth (400).
  - A client that sends nothing for 15 s gets 408, and the connection closes, so one phone that vanishes mid-request can't stop the server.

| Method and path | Purpose |
|---|---|
| `GET /api/auth` · `POST /api/auth/setup` · `POST /api/auth/login` · `POST /api/auth/logout` · `POST /api/auth/password` | The web password (§10.4): whether one is set and the session is valid; choosing it (over the AP only); logging in and out; changing it. The only routes open without a session |
| `GET /api/status` | Device, battery, sensors, time, Wi-Fi, firmware; from M5 `sync` (mode, running, the last one's time and steps, the next one and whether it is a retry, when the forecast and the air quality came) and `time.rtc` (the trim's steps, and the last drift once measured); from M6 `radar` (as built: `weather` with `source` "chmu" or "rainviewer", `frames`, `frame_at`, `fetched_at` and `error`; `flights` with `on`, `aircraft`, `updated`, `failed`, `error` and `routes_paused_until`) |
| `GET/PATCH /api/settings` | Non-secret settings; secrets are accepted on write and never returned |
| `GET /api/wifi/scan` · `GET/POST/DELETE /api/wifi/networks` | Wi-Fi setup. A POST starts a test and answers 202 at once; GET reports its result with the saved names, never their passwords. `"test": false` saves without trying |
| `GET /api/layouts` · `GET /api/fields` | Slot definitions; the field catalogue with current values |
| `GET/PUT /api/presets` | The preset document (§5.4) |
| `GET /api/preview.bmp?preset=<id>` · `POST /api/preview.bmp?preset=<id>` | Render a saved preset, or one from a presets document the editor posts (validated like `presets.json`), with live data, using the real renderer; a 1-bit BMP |
| `GET /api/screenshot.bmp` | The current frame |
| `POST /api/time` | Set time from the phone (epoch, IANA zone, POSIX TZ) |
| `POST /api/battery/learn` | `{"start": true}` learns the battery curve from the next full discharge, `{"stop": true}` ends it (D21); `GET /api/status` reports its state and hours |
| `GET /api/geocode?q=&lang=` | Proxy for the Open-Meteo geocoding search (M5): up to 5 places with name, region, country, latitude, longitude and time zone. Needs a session and the device on a network (503 otherwise); 400 for a name that is empty or doesn't decode, 502 when the search fails; manual latitude and longitude always work |
| `POST /api/sync` | Sync now (M5): 202 once it starts; 409 while one runs, or with no saved network |
| `GET/PUT /api/alarms` · `GET/PUT /api/stations` · `POST /api/radio/play` · `POST /api/radio/stop` | Audio (M8) |
| `POST /api/ota` · `GET /api/ota/status` | Firmware upload, as `application/octet-stream`; the running version, its slot, whether it is still pending, and the slot of an update that was rolled back |
| `GET /api/backup` · `POST /api/restore` | Settings bundle without secrets |
| `POST /api/done` · `POST /api/reboot` · `POST /api/factory-reset` | End config mode; system. The reply goes out before the device acts |

### 10.4 Security

- The configurator is reachable only in config mode, or on the LAN in `always` sync mode.
  - On the LAN (M5) it answers only requests that name the device: its address, `reflbo-XXXX`, `reflbo-XXXX.local`, or `reflbo-XXXX` under a router's domain (`reflbo-XXXX.fritz.box`). Any other name gets 421, so a page from elsewhere can't reach the API by pointing its own domain at the device (DNS rebinding).
  - A client counts as on the AP when it reached the AP's own address, 192.168.4.1, not when its own address merely looks like the AP's subnet, which a LAN may share.
- The AP uses WPA2.
- Secrets are write-only and never logged.
- The JSON content-type check blocks cross-site form posts.
- OTA images are checked for project name, chip and version before the device switches to them.
- **Web UI password** (D18).
  - The first visit asks for a new password, and only a client on the device's AP may set it; every visit after needs it.
  - It is kept as a salted hash in NVS `secrets`, is never returned or logged, and a factory reset erases it. The record is `pbkdf2-sha256$10000$<salt>$<hash>` with a 16-byte random salt; a login takes about 2 s on the chip. 8–64 characters.
  - A login starts a session that ends with config mode: at most 4, each a random 128-bit token in a cookie that is `HttpOnly` and `SameSite=Strict`. Phones also send cookies other gadgets set for 192.168.4.1, so the server takes 2 KB of request headers.
  - In sync mode `always` (M5), where the web UI stays up, a session also ends after 1 h without a request.
  - After 5 wrong passwords, logins wait 60 s.
  - Menu ▸ Wi-Fi ▸ Reset web password clears it, for when it is forgotten.
  - The page can also change it, given the current one, and log out (D19).

### 10.5 OTA

- Two app slots with rollback. A new image marks itself valid only after panel init, a first render, and 60 s without a panic. Otherwise the bootloader rolls back at the next reset. An image that fails to start rolls back at once.
  - Until then the board doesn't deep-sleep: a deep-sleep wake is a reset, which would roll the image back.
  - The upload streams into the other slot. Its header must name this project and chip, and the image's own checksum and hash must pass.
  - Measured at M4: 1.3 MB in 11–15 s over the AP. A test build that crashed after 25 s rolled back to the previous image, which returned in config mode (§10.2) and reported it. After the M4 review, a test build that failed to start was back on the previous image within 6 s, in config mode.
- Sources: upload through the web UI (M4). A file on microSD is an M9 candidate.

## 11. Weather, air quality and astro

**Weather request** (once per sync, about 2.6 KB of JSON for 3 days; checked against the live API on 2026-09-25 and 2026-10-01):

```
GET https://api.open-meteo.com/v1/forecast?latitude=<lat>&longitude=<lon>
  &current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m
  &hourly=temperature_2m,weather_code,precipitation_probability
  &daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset
  &timezone=auto&timeformat=unixtime&forecast_days=3
```

- Always fetch metric units; convert for display according to the units setting.
- WMO weather codes map to an icon and a language-pack string: clear, mainly clear, partly cloudy, overcast, fog, drizzle, rain, freezing rain, snow, showers, snow showers, thunderstorm.
- Parser tests on the host use recorded fixtures (`test/host/fixtures/open-meteo/*.json`).
- A provider interface allows other weather services later.

**Air quality request** (D25, D26; once per sync, about 5 KB of JSON; checked against the live API on 2026-10-01):

```
GET https://air-quality-api.open-meteo.com/v1/air-quality?latitude=<lat>&longitude=<lon>
  &hourly=european_aqi,pm2_5,pm10,uv_index,alder_pollen,birch_pollen,grass_pollen,mugwort_pollen,olive_pollen,ragweed_pollen
  &timezone=auto&timeformat=unixtime&forecast_days=3
```

- CAMS data: the index and particles worldwide; pollen only in Europe and in its season. Values the API sends as `null` stay missing.
- A second HTTPS request per sync: one more TLS handshake, under a second of radio (measured at M5).
- The bands and levels are in §5.1; Open-Meteo's terms are the same as for the forecast (§20).

- **Astro.** Sunrise, sunset and day length use the NOAA solar algorithm at the configured location. They work offline. Host tests compare them with the sunrise/sunset values in the Open-Meteo fixtures (±2 min). On a polar day or night the field shows that instead of times.

### 11.1 Map (M6)

- **Projection.** Web Mercator (EPSG:3857), the projection of both radar sources. A view is a centre and a zoom: at zoom z one ground pixel is 156 543 m × cos(latitude) ÷ 2^z, about 1.1 km at Brno at zoom 6.5. Each radar has its own view (D28): `radar.weather` and `radar.flights` (§14.3), whose centres default to `location.*`.
- **Data, built in** (`map`, host-tested). `tools/gen_map.py` packs them at build time, as `gen_fonts.sh` and `gen_icons.sh` do (§4.4, §4.5):
  - country borders from Natural Earth's 1:10 m boundary lines, simplified to about 200 m, delta-coded in a coarse grid of cells;
  - towns from Natural Earth's populated places, with name and population rank; inside ČHMÚ's radar area (§11.2) also GeoNames' places of 1000 inhabitants or more, less those Natural Earth has and the districts of larger towns (D29);
  - large and medium airports from OurAirports, with their IATA and ICAO codes.

  Natural Earth and OurAirports are public domain. GeoNames is CC BY 4.0: it is credited in `THIRD_PARTY.md`, the README and the Radar page (D29). The whole world takes about 0.5–1 MB of the app image (859 KB as built), and any centre works offline.
- As built (M6):
  - **The pack** (`assets/map/map.bin`: 2324 line pieces, 11 426 towns, 5281 airports; the sources and their SHA-256 in `assets/map/sources.txt`). Lines are pieces of at most 128 points, each with its bounding box, delta-coded from an absolute first point. The coastline from Natural Earth's 1:50 m set joins the borders, as a view near a sea otherwise ends in blank paper. OurAirports' file of 2026-10-01 and GeoNames' of 2026-10-02 are pinned, as both change daily. The reader checks the header and the sections' bounds; the CRC-32 is the tests' (`map_data_verify()`), as the app image's own SHA-256 covers the blob on the device.
  - **D29's towns.** GeoNames' `cities1000` inside ČHMÚ's data area, without sections of towns (PPLX and the like). A Natural Earth town's own entry is the nearest place within 10 km that goes by its name, so Natural Earth keeps its local names ("Praha", not "Prague"). A district is a place inside a larger one's administrative unit (the same country and the admin codes it has) within 0.0142 km × √(its inhabitants): 9 km of Brno, 15 km of Praha; neighbours stay, Sosnowiec beside Katowice, Zgorzelec across from Görlitz. Natural Earth's towns come first in the pack, so they lead the wide views and GeoNames fills in the closer ones. Four of Natural Earth's spellings are corrected: Plzeň, Ústí nad Labem, Wrocław, München.
- **Drawing.** Borders as 1 px lines, clipped to the view; towns as dots with labels, the larger first and thinned by zoom; on the flight radar, airports as a short runway bar with the IATA code; home as ⊙. Labels keep clear of each other and of home, a label with no room is left out, and every label has a 1 px white halo, so it stays legible over rain.
  - As built (M6): labels sit on white boxes a pixel larger than their text, as a glyph halo read poorly over dithered rain; lines get a 4-neighbour white halo where there is rain; lines and places project in single precision (the S3's FPU has no doubles), so where float and double round differently a line moves by a pixel. The flight radar reserves a box for each aircraft first (192 boxes at most).

### 11.2 Weather radar (M6)

- **Source** (D23): ČHMÚ when the view's centre lies inside its data area, otherwise RainViewer. Parts of a view outside ČHMÚ's coverage show its edge, not rain.
  - **ČHMÚ** (CC BY 4.0, credited "Data: ČHMÚ, opendata.chmi.cz, CC BY 4.0"; checked 2026-10-01): `https://opendata.chmi.cz/meteorology/weather/radar/composite/maxz/png/pacz2gmaps3.z_max3d.YYYYMMDD.hhmm.0.png` (UTC), a 680×460 8-bit palette PNG every 5 min, about 3 KB on a dry day and tens of KB in rain, kept for a week. The image spans 11.267–20.770 E and 48.047–52.167 N; its data end at 19.624 E and 51.458 N, and the title and colour scale beyond are masked (ČHMÚ's documentation as MeteoPlaneRadar, MIT, reads it; checked on the board against ČHMÚ's own viewer). The file name comes from the clock, so the sync's time step comes first: the newest 5-min step at least 5 min old, then up to two steps back on a 404 (as built: from the sync's own NTP time, §9.3). ČHMÚ answers every request with `Connection: close`, so each file has its own TLS connection. The directory's index is 327 KB, too much to read.
  - **RainViewer** (free for personal and educational use, with a link to rainviewer.com; checked 2026-10-01): `https://api.rainviewer.com/public/weather-maps.json` (about 2 KB) names the last 2 h of frames at 10-min steps; a frame's tiles are `{host}{path}/256/{z}/{x}/{y}/2/1_1.png` (as built: `2/0_0.png`, without smoothing or snow, as smoothing blends colours that no level of the table names), RGBA, at zoom 7 at most (a higher zoom answers a placeholder), so a closer view enlarges them. 100 requests a minute per IP. A view's tiles come over one kept-alive connection.
- **Decoding** (`png`, host-tested): a reader for 8-bit palette and RGBA PNGs, non-interlaced, row by row, inflating with the ROM's `tinfl` on the device (ESP32-S3 ROM, `esp_rom/include/miniz.h`) and with miniz's `tinfl.c` (MIT) on the host (as built: the system zlib, which ships with macOS and every Linux, so nothing is vendored). It refuses a truncated file, a bad CRC, interlacing, other bit depths, more than 1024×1024 pixels, and more than 256 KB.
- **Levels.** Each pixel becomes none, light (from about 20 dBZ), moderate (35) or heavy (45): ČHMÚ's palette through its colour scale (`scl/scl-dbz-mmh.png`), RainViewer's colours through its scheme. The levels are kept as a 2-bit grid in the source's own pixels (78 KB for ČHMÚ) in PSRAM, and every render reprojects the grid into its view: the frame is decoded once and drawn many times.
  - As built (M6): ČHMÚ's frames keep only its data area (598 × 378 pixels, 56 KB); RainViewer's the view's tiles, 3 × 3 at most (147 KB). A render maps each screen column and row to the grid once, through two tables, so a pixel costs two lookups and no floating point.
- **Dithering.** Light is a sparse dot pattern, moderate a checkerboard, heavy solid; the map and its labels go on top.
- **Frames.** The latest frame's PNG is kept in `/fs/state/radar.png`, so a power-off or a deep sleep keeps it. In sync mode `always` the last 12 frames (1 h, D28) stay in PSRAM for the loop; when `always` begins, the past hour comes in one go, 12 small files from ČHMÚ (RainViewer: 6).
  - As built (M6): the newest frame is kept as `/fs/state/radar.bin`: its levels behind a 48-byte header with a CRC-32 (56 KB for ČHMÚ, up to 147 KB for RainViewer), as RainViewer's frame is several tiles and a deep-sleep wake reads it back without decoding. It is written at once outside `always`, at most every 30 min in it, and read back the first time a preset draws the weather radar after a cold boot or a deep sleep (346 ms awake on such a wake, measured). A frame stays while it covers the view, the Radar layout's 400 × 279 (`radar_frame_covers()`): ČHMÚ's any view centred in its data, RainViewer's while it holds the tiles the view needs at its zoom.
- **When frames come** (§9.3): a radar step in each sync, after air quality; in sync mode `always`, a radar-only refresh every 5 min (RainViewer: 10). It never turns Wi-Fi on by itself (D23).
- **Where it shows** (D23): the Radar layout and the `rain.map` slot widget (§5.1, §5.2).
  - The layout: the map with the rain; the frame's time and source at the bottom left, inverted with its age once older than 30 min ("21:05 · 3 h ago"); a legend of the three levels at the bottom right; "No radar frame yet" before the first.
  - The widget, in M and L slots: the same, cropped to the slot around the weather view's centre at its zoom, with the frame's time.
  - As built (M6): a refusal or any other missing frame shows "No radar frame yet" (not "No radar here"), the reason in `radar status` and on the Radar page.
- **The loop** (D27, D28): BOOT short on the Radar layout switches the panel to HPM and plays the kept frames, oldest first, at about 3 a second with each frame's time and a row of progress dots, then stops on the newest and returns to LPM. Missing frames are skipped; KEY short still switches the preset and ends it. Outside sync mode `always` there is one frame, and BOOT short refreshes the sensors as elsewhere. As built (M6): a frame every 333 ms; the panel reads HPM while it plays and LPM after.

### 11.3 Flight radar (M6)

- **Source** (D22): adsb.fi's open data, `https://opendata.adsb.fi/api/v3/lat/{lat}/lon/{lon}/dist/{nm}`, readsb JSON (`ac[]`: `hex`, `flight`, `lat`, `lon`, `alt_baro`, `gs`, `track`, `t`); for non-commercial use, at most 1 request a second, credited with a link to adsb.fi (checked 2026-10-01).
- **When it runs** (D22): only in sync mode `always`, while Wi-Fi is up and the Flights view is on screen, and not in quiet hours or a night. Its own task polls every 5 s up to 25 km of range, 10 s up to 50 km and 15 s beyond, doubling the interval after a failure up to 60 s, over one kept-alive connection with a `reflbo/<version>` User-Agent.
- **Bounded** (`adsb`, host-tested): a reply over 128 KB is refused, and its depth is checked first (`AGENTS.md` gotcha 30). At most `radar.flights.max` aircraft are kept (100 at most), the nearest first, from `min_alt_ft` up, and without those on the ground unless `ground` is set.
- **Routes** (D27): when the nearest aircraft changes, one lookup at adsb.lol, `https://api.adsb.lol/api/0/route/{callsign}/{lat}/{lon}` (origin and destination with their codes and names; checked 2026-10-01), kept for 24 h in a 64-entry cache. A failed lookup counts as unknown for 1 h; a 403 or 429 backs off. adsb.lol is credited beside adsb.fi.
- **The view** (§5.2): the map at the flight radar's centre and range, with rings at half the range and at the range; aircraft as 12 px arrows in 16 headings, labelled with the callsign and the flight level ("FL338", or feet below 10 000 ft), the nearest first and none overlapping; a panel for the nearest aircraft with its callsign, type, altitude, speed in km/h, distance and direction, its route once known, and the adsb.fi credit. "No aircraft within 50 km" when there are none, "No aircraft data (HH:MM)" after a failure.
- As built (M6):
  - The query reaches the map's farthest corner, in NM, and the parser keeps the aircraft that fall on the map. Before the first poll after the view comes up, the panel says "No aircraft data". The polling stops while the menu is open.
  - A failed poll's reason shows in `radar status`, in `/api/status` (`error`) and on the Radar page; adsb.lol's pause after a 403 or 429 shows as `routes_paused_until`.
  - On the board (2026-10-02, −85 dBm): around Brno at 50 km 8–19 aircraft, the polls on one kept TLS session; the first poll took 25–50 s on the weak link; Frankfurt at 100 km fit (100 aircraft kept), London at 100 km passed 128 KB ("too big"); the lowest internal heap with a sync and the polling together was 74 KB.

### 11.4 Rain in the next 2 hours (M6, D27)

- The forecast request also asks for `&minutely_15=precipitation,precipitation_probability&forecast_minutely_15=96`: 24 h of 15-minute values (checked 2026-10-01), as a daily sync's next 2 h are long past by evening.
- `wx.rain2h` (§5.1) shows the 2 h from now as 8 bars, and a line: "Dry for 2 h", "Rain from 21:45" or "Rain now · 1.2 mm/h". It is missing past the 24 h stored.
- As built (M6): each `minutely_15` amount is the sum of the 15 minutes before its time (Open-Meteo's docs), so the quarter hour now is the entry that ends at the next quarter-hour mark, and "Rain from 21:45" names where the first wet quarter hour starts.

## 12. MQTT and Home Assistant

### 12.1 Settings

- Broker host and port (default 1883), username, password (secret).
- Client id and device id: `reflbo-XXXX`.
- Base topic `reflbo/<device id>`, discovery prefix `homeassistant`, discovery on/off.
- TLS is deferred (§19).

### 12.2 Topics

| Topic | Direction | QoS / retain | Payload |
|---|---|---|---|
| `reflbo/<id>/state` | out | 1, retained | State JSON |
| `homeassistant/<component>/<id>/<object>/config` | out | 1, retained | Discovery config |
| `reflbo/<id>/cmd/<name>` | in | 1, not retained, persistent session | Command value |
| Mapped field topics (§12.5) | in | subscribed at QoS 1 | Value or JSON |

State example:

```json
{"temp":21.4,"hum":45.2,"bat_pct":78,"bat_v":3.92,"charging":"discharging","rssi":-61,
 "preset":"home","last_sync":"2026-09-25T03:30:12Z","fw":"0.1.0","uptime_s":86400}
```

### 12.3 Discovery entities (v1)

| Entity | Details |
|---|---|
| Sensors | Temperature (°C), humidity (%), battery (%), battery voltage (V, diagnostic), Wi-Fi RSSI (dBm, diagnostic), last sync (timestamp, diagnostic), charging state (enum) |
| `select` | Active preset. Options are the preset names; `command_topic` is `.../cmd/preset`; `qos: 1` |

- All entities share one `device` block: identifiers `reflbo-XXXX`, model "ESP32-S3-RLCD-4.2", manufacturer "Waveshare", software version.
- **Sleepy-device settings.** Sensors set `expire_after` to twice the expected sync interval (§9.3) plus 10 min, and omit it in `manual` mode. There is no availability topic. Entities therefore keep their last value while the device sleeps.
- Discovery is published at the first sync, and again whenever preset names, relevant settings or the firmware change. A hash of it is stored in NVS.

### 12.4 Commands (v1)

- `cmd/preset` activates a preset by id or name.
- Other commands arrive together with their features, if agreed.
- A command is applied, and the new state is published in the same session.
- Commands depend on QoS 1 and a persistent session (`disable_clean_session`). The broker then queues them while the device sleeps. Mosquitto keeps sessions by default. Discovery sets `qos: 1` so HA publishes commands at QoS 1.

### 12.5 MQTT fields (HA entities and other devices, D7)

Mappings live in `/cfg/mqtt_fields.json` (edited in the web UI), up to 32:

```json
{
  "schema": 1,
  "fields": [
    { "key": "outdoor_temp", "label": "Outside", "kind": "number", "unit": "°C", "precision": 1,
      "topic": "ha/statestream/sensor/outdoor_temperature/state", "json_path": null, "ttl_s": 172800 },
    { "key": "co2", "label": "CO2", "kind": "number", "unit": "ppm", "precision": 0,
      "topic": "zigbee2mqtt/living_room", "json_path": "co2" }
  ]
}
```

- **Field id.** `mqtt.<key>`. The payload is parsed as a number or text. `json_path` takes dotted keys (`a.b.c`); arrays are not supported in v1.
- **Delivery.** Values arrive during a sync as retained messages, or live in `always` sync mode.
- **Publisher requirement.** Publishers must retain their messages, or go through HA statestream, for a sleeping device to see them. Zigbee2MQTT needs `retain: true` per device; document this in the web UI.

### 12.6 Home Assistant side (example)

```yaml
# configuration.yaml
mqtt_statestream:
  base_topic: ha/statestream
  include:
    entities:
      - sensor.outdoor_temperature
```

HA publishes `ha/statestream/<domain>/<object_id>/state` at QoS 1, retained. With `publish_attributes: true`, it also publishes attributes as `.../<attribute>` in JSON.

## 13. Audio

### 13.1 Codec path

- **Playback.** `esp_codec_dev` drives the ES8311 (I²C `0x18`) over I²S standard mode: MCLK GPIO16 (256 × fs), BCLK 9, WS 45, DOUT 8.
- **Mics.** The ES7210 stays in standby, because v1 doesn't use the mics.
- **Power.** PA_CTRL (GPIO46) is high only while playing, with a settle delay and a volume ramp to avoid pops. After playback the codecs go to standby and the I²S driver is removed.

### 13.2 Alarms (M8)

- **Storage.** Up to 8 alarms in `/cfg/alarms.json`: `{id, enabled, time "HH:MM", days (Mon–Sun bitmask; 0 = once), label, sound, volume, snooze_min (default 9), ramp_s (default 30)}`.
- **Sounds.** Built-in generated patterns, and 16-bit PCM WAV files from LittleFS or microSD. MP3 files are added once the radio decoder exists.
- **Ringing.** The ACTIVE state shows the alarm screen and ramps the volume up. It stops automatically after 10 min. A short press snoozes; a long press stops.
- **Conditions.** Alarms work offline and from deep sleep, but need a valid time.
- **Display.** The next alarm is shown in the status bar.

### 13.3 Internet radio (M8)

- **Start.** From the menu or the web UI. It needs Wi-Fi, which stays on while the radio plays.
- **Pipeline.**
  1. HTTP(S) stream via `esp_http_client`, sending `Icy-MetaData: 1`.
  2. PSRAM ring buffer of about 256 KB.
  3. Decoder task using Espressif `esp_audio_codec` (MP3, AAC).
  4. PCM out to the ES8311.
- **Stations.** The list lives in `/cfg/stations.json` (name, URL). Defaults are chosen at M8.
- **Screen and controls.** The screen shows the station and the ICY title, with a battery warning when on battery. BOOT long, the menu or the web UI stops playback. A dropped stream reconnects with backoff.

## 14. Storage

### 14.1 Partition table (16 MB flash)

| Name | Type | Offset | Size |
|---|---|---|---|
| nvs | data / nvs | 0x9000 | 64 KB |
| otadata | data / ota | 0x19000 | 8 KB |
| phy_init | data / phy | 0x1B000 | 4 KB |
| ota_0 | app | 0x20000 | 4 MB |
| ota_1 | app | 0x420000 | 4 MB |
| coredump | data / coredump | 0x820000 | 64 KB |
| storage | data / littlefs | 0x830000 | 7.8 MB (to end of flash) |

`storage` has no partition image, so `idf.py flash` never writes it. It is formatted at first boot. The web assets live in the app image.

### 14.2 NVS

| Namespace | Contents |
|---|---|
| `sys` | Device id, AP password (`ap_pass`), schema version, idle strategy override (`idle`), come back in config mode after a web restart (`resume_cfg`, §10.2), the RTC trim (`rtc_trim`, §7) |
| `wifi` | Saved networks with their passwords and fast-connect cache (`nets`, one versioned blob) |
| `secrets` | MQTT password; the web UI password's salted hash (`web_pass`, D18); future tokens |
| `ctr` | Counters: boots, sync statistics |

### 14.3 LittleFS layout

```
/cfg/settings.json      non-secret settings
/cfg/presets.json       §5.4
/cfg/mqtt_fields.json   §12.5
/cfg/alarms.json        §13.2 (M8)
/cfg/stations.json      §13.3 (M8)
/state/datastore.bin    last datastore snapshot (written after each sync)
/state/radar.bin        the newest weather radar frame: its levels, a header and CRC-32 (M6, as built)
/sounds/                user sound files (M8)
```

- Every file carries a schema version, and migrations run at boot.
- Writes are atomic: write `*.tmp`, fsync, rename, keeping the previous file as `*.bak`.
- If a file is invalid, the firmware uses `*.bak`; if that is invalid too, it uses defaults and shows the toast "Using default settings".
  - A file rejected in favour of its `.bak` is removed, so the next save keeps the good backup.
  - A file nested deeper than 16 levels is rejected before it is parsed.
- The partition is mounted at `/fs`, so the files are `/fs/cfg/settings.json` and so on.

`settings.json` sketch:

```json
{
  "schema": 1,
  "language": "en",
  "location": { "name": "Brno", "lat": 49.1951, "lon": 16.6068 },
  "time": { "tz_iana": "Europe/Prague", "tz_posix": "CET-1CEST,M3.5.0,M10.5.0/3",
            "clock_24h": true, "ntp": ["cz.pool.ntp.org", "pool.ntp.org"] },
  "units": { "temp": "C" },
  "sync": { "mode": "times", "times": ["05:30"], "interval_min": 60,
            "quiet": { "enabled": false, "from": "23:00", "to": "06:00" } },
  "sensors": { "interval_min": 5, "temp_offset_c": 0.0, "hum_offset_pct": 0.0 },
  "display": { "contrast": "default", "update_min": 1, "lpm_hz": 1 },
  "battery": { "level_from": "curve", "empty_v": 3.27, "full_v": 4.2 },
  "radar": { "weather": { "lat": 49.1951, "lon": 16.6068, "zoom": 6.5 },
             "flights": { "lat": 49.1951, "lon": 16.6068, "range_km": 50, "min_alt_ft": 0,
                          "ground": false, "max": 100 } },
  "mqtt": { "enabled": false, "host": "", "port": 1883, "user": "",
            "discovery_prefix": "homeassistant", "discovery": true }
}
```

Once a discharge is learned, `battery` also holds `learned_mv` (21 voltages, 0 % to 100 %) and `learned_at` (UTC seconds), and `level_from` can be `learned` (D21).

M3a reads `language`, `time.tz_iana`, `time.tz_posix`, `time.clock_24h`, `units.temp`, `sensors.*`, `display.update_min` and `display.lpm_hz`; M4 adds `location.*`, with latitude and longitude clamped to the globe, and its acceptance `battery.*` (§8; a manual pair without 0.3 V between them, or a learned curve that doesn't rise, falls back to the built-in curve). M5 adds `time.ntp` (1–2 host names) and `sync.*`: `times` keeps 1–8 valid `HH:MM` times, sorted and without repeats, falling back to `["05:30"]`; `interval_min` is clamped to 15–1440. M6 adds `radar.*` (§11.1–§11.3): each centre defaults to `location.*`; `weather.zoom` is clamped to 4–9 in steps of 0.25; `flights.range_km` to 10–100 (from the centre to the map's top edge), `max` to 1–100, `min_alt_ft` to 0–60000. `PATCH /api/settings` merges into the file as an RFC 7396 merge patch, which must keep `"schema": 1`. The file must be a JSON object with `"schema": 1`; beyond that, a missing or mistyped key takes its default and an out-of-range number is clamped, so one bad value never resets the rest. Saving keeps the keys the firmware doesn't know.

### 14.4 Backup, restore, factory reset

- **Backup.** A JSON bundle of every `/cfg/*` file, without secrets: `{"reflbo_backup": 1, "device": …, "firmware": …, "files": {"settings.json": {…}, "presets.json": {…}}}`. Restore validates every file it knows before replacing anything, applies them at once, and leaves out files of a later firmware.
- **Factory reset.** From the menu (System ▸ Factory reset, confirmed by holding KEY), or from M4 the web UI. The board restarts afterwards. It erases `storage` and the NVS namespaces `wifi`, `secrets` and `ctr`. It keeps `sys`, the device identity.

### 14.5 microSD (M9)

- FAT32 on SDMMC 1-bit. The card is mounted on demand and unmounted after use; a mount attempt detects whether one is present.
- The feature set is agreed at the start of M9, from the candidates in §19.

## 15. Diagnostics and tooling

**Console** (`esp_console` over USB-Serial-JTAG):

| Command | Purpose |
|---|---|
| `version` · `reboot` · `heap` · `tasks` | Basics |
| `screenshot` | Framebuffer as base64 PBM between markers |
| `panel status` · `panel test` · `panel clear` · `panel mode <hpm\|lpm>` · `panel rate <Hz>` · `panel fps [s]` · `panel sleep` · `panel wake` · `panel init <factory\|xiaozhi>` | Panel diagnostics: test pattern, power mode, LPM rate, measured frame rate, sleep-in and wake, init sequence |
| `btn <key\|boot> <short\|double\|long>` | Inject button gestures |
| `sensors` · `battery` · `battery learn start\|stop` | Readings; learning the battery curve from the next full discharge (D21) |
| `rtc get` · `rtc set <ISO 8601>` | RTC |
| `field list` · `field get <id>` · `field set <id> <value>` · `field clear <id>` | Inspect and inject data, e.g. fixtures on the device |
| `preset list` · `preset set <id>` | Presets |
| `schedule list` · `schedule on\|off\|clear` · `schedule add <HH:MM> preset <id> [days]` · `schedule add <HH:MM> night <HH:MM> [days]` | The preset schedule (§5.4); `days` is the Mon–Sun mask, default 127 |
| `night <minutes>` | Night sleep now (§9.1), for measuring; the console drops until it ends |
| `wifi status` · `wifi scan` | Wi-Fi: the state, network, address, AP clients and saved names; the networks in sight while Wi-Fi is on (config mode) |
| `sync now` · `sync status` | Run a sync; the running or last sync's steps and the next one (M5). `rtc get` also prints the trim and the last drift (§7) |
| `radar status` · `radar loop` | The weather radar's source, frame time, frames kept and last error, and the flight radar's state with its last failure and adsb.lol's pause (M6); the loop as BOOT short plays it |
| `sleep stats [reset]` · `sleep test <deep\|light> <n>` · `power idle [deep\|light]` | Power debugging: sleeps, wake causes, and per-cycle awake and slept times; `sleep test` forces sleep cycles while tethered |
| `audio tone <Hz> <ms>` | Audio check (M8) |

Screenshot framing:

```
-----BEGIN RLCD PBM-----
<base64 of "P4\n400 300\n" + 15 000 bytes, in 76-character lines>
-----END RLCD PBM-----
```

**Host tools:**

| Tool | Purpose | Needs |
|---|---|---|
| `tools/idf.sh` | Source the IDF environment, then run `idf.py` | ESP-IDF |
| `tools/devlog.py` | Capture the serial log for N seconds, reconnect when USB re-enumerates, optionally reset first | pyserial |
| `tools/screenshot.py` | Request a screenshot and write a PNG (and the PBM) | pyserial, stdlib |
| `tools/render.py` | Run the host renderers on every fixture, writing PNGs; `render_dashboard --list` and `render_screen --list` name them | Host build |
| `tools/fontgen.py` · `tools/imggen.py`, run by `tools/gen_fonts.sh` · `tools/gen_icons.sh` | Turn fonts and icons into C sources | uv, Pillow |

pyserial comes from the ESP-IDF Python environment. The generators run through `uv` with the pinned versions in `tools/requirements.txt`.

## 16. Error handling and robustness

- **Crashes.** The task watchdog is on. A panic writes a core dump to flash. The next boot that comes alive (not a routine wake, §3.3) reports the dump in the log; `idf.py coredump-info` reads it. Later the console and the web UI make it available, then erase it.
- **Power.** The brownout detector is on. Critical battery is handled before a brownout happens (§8).
- **Network.** Every network call has a timeout. Failures are recorded per sync step and shown in Info and the web UI. Network work never blocks the UI.
- **Config files.** Atomic writes, `.bak` copies, schema validation, falling back to defaults (§14.3).
- **Firmware updates.** OTA rollback (§10.5).
- **Invalid time.** Alarms and time-dependent features are suspended, with a clear prompt to set the time.
- **I²C.** A failed transfer is retried once, then the field is marked stale. A stuck SDA line is recovered with clock pulses.

## 17. Testing strategy

**Host.** CMake + Ninja + Apple clang, with Unity (MIT) vendored in `test/host/third_party/`.

- **Unit tests.**
  - gfx: primitives, clipping, UTF-8, text measure and wrap.
  - Panel conversion, checked against the vendor formula.
  - datastore: freshness, snapshot round trip, CRC.
  - scheduler: next wake, DST, alarms, snooze.
  - Gesture recogniser: timelines of button events.
  - Battery curve and charging inference.
  - astro: against fixture sunrise/sunset values.
  - Weather and air quality parsers: against fixtures.
  - Sync planning: the next sync in each mode, the quiet hours, the retries, the expected interval, the syncs the device needs (a lost clock, a first forecast, `always` mode's Wi-Fi) and each step's share of the 45 s; the RTC trim's arithmetic and the Offset register's codec; SNTP packets and their refusals.
  - The forecast fields and widgets: the words, rounding and units, a two-digit high and low that must show whole, and the goldens of each new widget.
  - M6: the PNG reader against real ČHMÚ frames and a RainViewer tile, and its refusals; the map's projection round trips through known points, clipping and label placement; `tools/gen_map.py` against small fixtures; ČHMÚ's file names from the time, its palette's levels, a known pixel landing on its place in a view, the coverage test; RainViewer's index and tile choice; the adsb.fi parser against a real reply, its caps, filters, distance and bearing; the route parser and cache; the 15-minute rain and its window; `radar.*` in the settings; the preset migration; goldens of the Radar layout (rain, stale, a loop frame), the rain map in M and L slots, the rain strip, and the Flights view (aircraft and a route, none, outside sync mode `always`).
  - As built (M6): 60 host targets (53 at M5), with 17 new goldens (8 of the Radar layout and `rain.map`, 6 of the Flights view, 3 of the rain strip); the generator's 14 tests among the 67 tool tests; 25 page tests; the ASan/UBSan build passes them all.
  - Preset and settings JSON: validation and migrations.
  - Config files: the atomic write and the `.bak` fallback, in a scratch directory.
  - MQTT payload builders: golden JSON.
  - locale formatting.
  - The web configurator: SHA-256, HMAC and PBKDF2 against published vectors; the password record, sessions and login throttle; the captive DNS reply; the saved-network list and the scan choices; the settings merge patch; the backup bundle; the preset editor's catalogue; the config screen's QR codes; `tools/gen_zones.py`; from M5 the device's name under a router's domain, the idle hour of a session, every status line the server sends, and the settings' sync defaults.
- **Golden renders.** Each built-in preset, the menu and each special screen are rendered with fixture data at fixed times. Each render is compared with `test/host/golden/*.pbm`. After an intentional change, the renderer rewrites the golden (`build-host/render_dashboard <fixture> <file>`, or `render_screen` for the menu and the special screens), and the owner reviews the PNGs from `tools/render.py`.
- **Sanitizers.** `-DREFLBO_SANITIZE=ON` builds the host tests with AddressSanitizer and UndefinedBehaviorSanitizer.
- **JSON on the host.** cJSON is built from the ESP-IDF tree (`$IDF_PATH/components/json/cJSON`).

**Device.**

- Boot smoke test through `devlog.py`: no errors, and a "ready" log line.
- Console checks, and `btn` + `screenshot` flows for each milestone.
- The owner checks listed per milestone (§18).

## 18. Milestones and acceptance

Verification levels (1–4) are defined in `AGENTS.md` §7.

| M | Scope | Acceptance (level) |
|---|---|---|
| M0 | ESP-IDF v5.5.5 installed; project skeleton (CMake, `main`, `sdkconfig.defaults`, partition table); console (`version`, `heap`, `reboot`); `tools/idf.sh`, `tools/devlog.py`; host test skeleton with Unity; `LICENSE`, `NOTICE`, `THIRD_PARTY.md` | Clean build (1); host tests pass (2); boot log captured and the console answers (3) |
| M1 | `st7305` (cold init, push, LPM/HPM), frame conversion, `gfx` core, `fontgen` and the first fonts, screenshot command and tool, host render tool, test pattern | Host tests (2); the test-pattern screenshot matches the host render (3); owner confirms orientation, contrast and the init sequence (4) |
| M2 | `st7305` warm init for deep-sleep wakes (moved from M1), `board` (I²C, buttons, gestures), `rtc`, `sensors` (SHTC3, battery), `timekeeping` (RTC → system time; manual set through the console), Classic clock screen, `power` with both idle strategies, `scheduler` minute wakes | Screen values match the console (3); owner measures deep vs light sleep with the USB meter; the idle strategy is chosen and recorded (4) |
| M3 | Two plans. **M3a:** `storage` (LittleFS config files), `datastore` with the extra fields, `locale` (en), fonts and icons, 4 layouts, widgets, status bar, presets JSON and defaults, cycling. **M3b:** menu v1, special screens, settings, schedule and night sleep, the LPM rate setting, the `cs` pack | Owner reviews golden renders (2); presets switch with KEY/`btn` and survive a reboot (3); owner measures night sleep (4); owner reviews the Czech renders (2) |
| M4 | `netmgr` (STA/AP, captive portal, mDNS), config screen with QR, `webui` and REST API, preset editor with preview, set time from phone, OTA with rollback | Owner sets up Wi-Fi from a phone in AP mode (4); the preview matches a device screenshot (3); OTA upload and rollback work (3) |
| M5 | One plan (D25): SNTP → RTC with the trim (D25), `weather` with air quality and pollen (D25), `astro`, `sync` with the configurable schedule, quiet hours (D25) and backoff, sync mode `always` with the web UI on the LAN (D24), the weather and air quality widgets, the status bar's sync and Wi-Fi state, the Sync page and the place search, power tuning | A sync on battery reports its results in Info (3); astro tests pass (2); the owner reviews the new widgets' goldens (2); the RTC trim brings the drift under 1 s a day (3); sync energy, `always`'s cost and the daily average are measured and `docs/power.md` is updated (4) |
| M6 | One plan (D27): the radar views (D22, D23, D24, D28; §11.1–§11.4): `png`, the web-Mercator `map` with built-in borders, towns and airports; the weather radar (ČHMÚ, RainViewer outside its coverage) as the Radar layout and the `rain.map` widget, with a frame each sync, every 5 min in sync mode `always`, and the last hour's loop; the flight radar (adsb.fi, sync mode `always` only) as the Flights preset, with the nearest aircraft's route (adsb.lol); rain in the next 2 hours; the Radar page | Goldens of both radars and the new widgets (2); a ČHMÚ frame renders after a sync (3); the loop plays in sync mode `always` (3); aircraft from adsb.fi show in sync mode `always` (3); the owner checks both on the panel (4) |
| M7 | `ha_mqtt`: session, discovery, state, preset command, MQTT field mappings | Entities appear in HA; the preset select works at the next sync; a mapped HA value renders (3/4) |
| M8 | `audio`: codec path, offline alarms (also from deep sleep), tones and WAV, radio (MP3/AAC, ICY) | An alarm fires from idle, and snooze and stop work (3/4); a radio stream plays (4) |
| M9 | microSD features agreed at the start of M9 | Per the agreed list |

## 19. Deferred proposals (discuss at the milestone)

| Milestone | Proposal |
|---|---|
| M0 | GitHub Actions CI (firmware build and host tests) |
| M1 | ~~Portrait orientation~~ (declined 2026-09-25, D13) |
| M3 | Accepted (D15): the LPM refresh rate as a display setting; the preset schedule, with timed night sleep; extra fields (dew point, today's min/max, trends, week number, moon phase, battery days left); the Czech pack with name days and public holidays (name days deferred, D16). Still deferred: the Night layout; the change in day length (needs `astro`, M5) |
| M4 | Accepted (D18): a web UI password, instead of the admin PIN. Accepted (D19): config mode after a web restart, the clock set from the phone when lost, changing the password and logging out, a Device page. Still deferred: web UI translations |
| M5 | Accepted (D25): quiet hours; air quality and pollen (Open-Meteo); RTC offset calibration. Still deferred: static IP |
| M6 | Accepted (D22, D23, D24): the ADS-B flight radar (§19.1) and the weather radar (§19.2), on one map renderer; together about the size of M3a and M3b. Designed in §11.1–§11.4 (D27, D28), with three extras: rain in the next 2 hours, the last hour's loop, the nearest aircraft's route |
| M7 | MQTT over TLS; HA buttons (sync now, next preset) and device triggers for key presses; HA message entity; HA REST pull as an alternative source |
| M8 | Radio sleep timer; ESP-SR (echo cancellation, noise suppression, wake word) |
| M9 | Config backup/provisioning file; sensor history CSV with graphs; sounds and station lists; 1-bit images; firmware file; logs and screenshots; from M6 (D28): radar and flight history over days, a detailed map pack, an aircraft registration database |
| Later | IDS JMK departures; a remote 1-bit image slot; the VBUS-sense hardware mod; external I²C sensors on the header; BLE or ESP-NOW sources |

### 19.1 ADS-B flight radar (M6)

Designed in §11.1 and §11.3 (r28); the findings and answers below are its background.

Owner request, 2026-10-01, naming viz1090 and MeteoPlaneRadar. Accepted for M6 with the weather radar (D23, D24); the owner answered the design questions the same day (D22, below). The milestone's plan settles the details.

**Findings** (checked 2026-10-01)

- **No reception on the board.** The ESP32-S3 has no 1090 MHz radio. A UART receiver module on the header would draw about 100 mA all the time, which a battery-first device can't afford. The data comes over Wi-Fi.
- **viz1090** (github.com/nmatsuda/viz1090) is a desktop SDL2 app for a Raspberry Pi with an RTL-SDR running dump1090; it can't run on the ESP32, and its licence isn't stated. Worth borrowing as an idea: its map pipeline (`mapconverter.py`: Natural Earth and FAA shapefiles, simplified).
- **MeteoPlaneRadar** (github.com/petus/MeteoPlaneRadar, MIT; Arduino on ESP32 core 3; a round 480×480 colour ST7701; USB power only):
  - polls adsb.fi every 5, 10 or 15 s for ranges up to 25 km, up to 50 km and beyond (10–100 km selectable), doubling the interval after a failure;
  - keeps at most 100 airborne aircraft, parsed through an ArduinoJson filter to bound memory;
  - fetches a flight's route from adsb.lol (`https://api.adsb.lol/api/0/route`) only when its detail opens; adsb.lol answers 403 without a descriptive User-Agent;
  - draws EU borders (about 31 000 points) and 1100 cities from a 518 KB embedded header.
- **adsb.fi open data**: `https://opendata.adsb.fi/api/v3/lat/{lat}/lon/{lon}/dist/{nm}`, up to 250 NM, readsb JSON compatible with ADS-B Exchange v2. At most 1 request a second; non-commercial use only; adsb.fi must be credited with a link to its site.
- **A local receiver** (an RTL-SDR with readsb, dump1090-fa or tar1090 on a Pi) serves the same JSON as `aircraft.json` over plain HTTP: no rate limit, no internet.

**Sketch**

- **Data.** One parser for readsb / ADS-B Exchange v2 JSON (`ac[]`: `hex`, `flight`, `lat`, `lon`, `alt_baro`, `gs`, `track`), fed by adsb.fi by default or by a local `aircraft.json` URL. Bounded: a size cap and `util_json_depth()` before parsing (hardware gotcha 30), into a fixed aircraft table in PSRAM. Pure and host-tested: the parser, the distance and bearing projection, label placement.
- **View.** A full-screen 1-bit radar: range rings around home; aircraft as arrows turned to their track (pre-rendered rotations) with callsign and altitude; a nearest-aircraft panel; an optional underlay of borders and towns, generated at build time by a `tools/` script like the fonts and icons (Natural Earth is public domain). Goldens and device screenshots verify it. LPM at 1–2 Hz suits updates every 5–15 s.
- **Power, the main constraint.** The radar needs Wi-Fi on the whole time it shows, which is what sync mode `always` gives (D22). Estimate, to be measured: about 1 % of the battery per hour; nonstop it would cut the battery life from about a week to 2–3 days, so `always` suits USB power best (the firmware can only tell USB power reliably with the VBUS-sense mod, hardware gotcha 3).
- **Settings.** `adsb.source` (adsb.fi or a local URL), range, maximum aircraft, filters (minimum altitude, aircraft on the ground). The adsb.fi credit goes in the web UI and the README.
- **From MeteoPlaneRadar:** range-dependent polling with back-off, the User-Agent, routes on demand, the borders and cities. Rewritten in C for ESP-IDF rather than ported; adapted code would be credited in `THIRD_PARTY.md`.
- **Fit.** It needs M5's HTTPS client, the certificate bundle and the sync framework, and sync mode `always`, which moves into M5 (D24), so it comes in M6.

**Owner's answers** (2026-10-01, D22)

1. **Source:** adsb.fi only; no local receiver.
2. **When it runs:** only in sync mode `always` (D11, §9.3), which the user picks in the settings; not behind a separate switch, and no time-boxed radar sessions. The power estimate above is the price of that mode.
3. **Map:** set in the web UI, centred on a point (default `location.*`) with a zoom level, and with towns and airports built in.
   - Sources, both public domain, with no attribution required (terms checked 2026-10-01): OurAirports (`ourairports.com/data`; its `airports.csv` is 12.7 MB for every airport, so only the large and medium ones) and Natural Earth's populated places. GeoNames (CC BY) and OpenStreetMap (ODbL, share-alike) would need attribution, so they are left out.
   - Proposed: a build-time `tools/` generator, like `gen_fonts.sh` and `gen_icons.sh`, packs the towns and the large and medium airports of the world into a compact table: about 12 000 entries of about 24 bytes, some 300 KB, which fits the 4 MB app slot beside the 1.3 MB image. The device picks what falls inside the configured map at render time, so it works offline for any centre. The alternative, the page fetching the area's data and uploading it to LittleFS, has more moving parts and depends on CORS.
   - The radar's web settings could show a live preview through `/api/preview.bmp`, as the preset editor does.
   - Settings this implies: the radar's centre (latitude and longitude) and zoom or range, and the filters above.


### 19.2 Weather radar (M6)

Designed in §11.1 and §11.2 (r28); the findings and answers below are its background.

Owner question, 2026-10-01, after the flight radar; MeteoPlaneRadar shows one too. Accepted for M6 (D23, D24, below).

**Sources** (checked 2026-10-01)

- **ČHMÚ open data**, as in MeteoPlaneRadar: the composite of maximum reflectivity from the Brdy and Skalky radars, a new image every 5 minutes. `https://opendata.chmi.cz/meteorology/weather/radar/composite/maxz/png/pacz2gmaps3.z_max3d.YYYYMMDD.hhmm.0.png` (UTC): a 680×460 palette PNG, a few KB on a dry day, with the map at 1×1 km in web Mercator (EPSG:3857) beside side projections that get cropped away. `png_masked/` marks the echoes that reach the ground. Licence CC BY 4.0, commercial use included; the credit reads "Data: ČHMÚ, opendata.chmi.cz, CC BY 4.0". It covers Czechia and the land around it, so Brno sits well inside. A forecast for +10 to +60 min exists only as HDF5 in TAR files, too heavy for the device.
- **RainViewer**, MeteoPlaneRadar's source outside Czechia: free for personal or educational use, with a link to rainviewer.com. Since 2026-01-01: zoom 7 at most (about 0.8 km a pixel at Brno's latitude), one colour scheme, no nowcast, 100 requests per IP per minute; 2 h of past frames at 10-minute steps, as 256 or 512 px tiles.

**Sketch**

- **Fetch and decode.** HTTPS through M5's client; the PNG decoded in PSRAM by a small streaming decoder (pngle, MIT, is a candidate; adapted code goes in `THIRD_PARTY.md`). Palette index to dBZ through ČHMÚ's colour scale (`scl/scl-dbz-mmh.png`), then two or three dither densities for light, moderate and heavy rain on the 1-bit panel.
- **Map.** Both sources are web Mercator, the projection §19.1's map uses, so one map renderer with the same centre, zoom, borders and towns draws both radars. The aircraft stay on their own view, never over the rain (D23). The web UI previews it through `/api/preview.bmp`.
- **Power: it never turns Wi-Fi on itself** (D23). Each normal sync takes one frame along, a few KB more and well under a second; in sync mode `always`, Wi-Fi is on anyway and a frame comes every 5 minutes, ČHMÚ's own step (RainViewer's is 10). So on battery it costs next to nothing beyond the sync; the frame is as old as the last sync.
- **Where it shows** (D23): a full-screen radar layout and a smaller rain-map widget for a slot, both from the latest frame. A short loop of past frames on KEY would cost one download per frame, so it is left for the milestone's plan.
- **A cheaper alternative**, inside M5's weather work: Open-Meteo's 15-minute precipitation forecast for the location, drawn as "rain in the next 2 hours". It needs no map and no images.

**Owner's answers** (2026-10-01, D23)

1. **Source:** ČHMÚ by default, and RainViewer where the map lies outside ČHMÚ's coverage.
2. **Where:** both a full-screen layout and a slot widget. Aircraft are a separate view, not drawn on the weather radar.
3. **Refresh:** by the board's mode at the time. The radar never starts Wi-Fi by itself: it fetches with the normal sync, and every 5 minutes while sync mode `always` is on.

## 20. Risks and open items

| Risk / item | Mitigation |
|---|---|
| The panel keeping its image through deep sleep is unverified | Test at M1/M2; fall back to light sleep |
| Waking through BOOT (GPIO0, a strapping pin) might enter download mode | Verify at M2. If it does, wake only on KEY and the RTC |
| Real currents are unknown (panel LPM, board quiescent, PSRAM in light sleep) | Measured at M2: the forced-PWM 3V3 converter dominates at about 60 mW. The panel and other 3V3 loads can only be split out after the PS/SYNC rework (M5) |
| The stretch goal (D6) is out of reach on the board as built | Owner decision on the PS/SYNC rework (a VSON package with pins underneath); until then about 15 mA from the battery is expected (estimate) |
| USB meters are inaccurate below 1 mA | Long mAh windows; note the limits in `docs/power.md` |
| SHTC3 self-heating | Offset calibration; sample right after wake |
| `esp_audio_codec` is distributed as prebuilt binaries, which may not suit an open-source repo | Check at M8; pick another decoder if needed |
| Open-Meteo's free tier is for non-commercial use; it covers the forecast, air quality and geocoding APIs alike | Low request rate (two requests per sync); the provider can be swapped |
| The RTC trim's measurement rides on the RTC's own correction swing, up to ±0.14 s within two hours at today's offset | It waits for at least 20 h between sets (±1.6 ppm against a step of 4.34 ppm); a trim one step off still leaves under 0.4 s a day |
| The Czech name-day calendar needs a source whose licence allows redistribution in this repository | Checked 2026-09-29: the best list (`namedays-cs`, MIT) traces its data to Czech Wikipedia (CC BY-SA); others were incomplete, broken or unlicensed. Deferred (D16): `cs` ships the holidays only until a clean source turns up |
| The critical-battery path has not met a really low battery: its thresholds are host-tested and its screen is a golden, but its KEY-only sleep has not run on the board | Watch the first time the board runs flat on battery (M5 power work) |
| No RTC backup cell (D9): the time is lost at every PWR-off | Sync at boot when Wi-Fi is configured, otherwise a "Set time" prompt; the owner may fit an ML1220 (§7) |
| The board's spot gets the home network at −87 to −88 dBm (M5): syncs of up to 27 s, and a step that times out now and then | The 45 s and the retries bound it; moving the board or the router helps |
| The RTC trim measures only across syncs at least 20 h apart (M5 review) | The default schedule trims; `interval`, `always` and several daily times keep the RTC untrimmed, which matters once syncs stop. Owner decision: a measurement that adds up each set's error instead (a §7 change) |
| The deep-sleep idle strategy's syncs (a routine wake has no NVS) are fixed but unchecked on the board, which never deep-sleeps while tethered | Check with the power measurements on battery (§9.4) |
| The ČHMÚ image's bounds come from MeteoPlaneRadar's reading of ČHMÚ's documentation (§11.2) | Constants, checked on the board with a rainy frame against ČHMÚ's own viewer; on 2026-10-02 a nearly dry frame's light rain at the view's corner drew where ČHMÚ's file has it, and a rainy frame is still to compare |
| RainViewer's free use is personal and educational, 100 requests a minute per IP and zoom 7 at most, and its terms changed in 2026-01 | Used only outside ČHMÚ's coverage; a refusal shows "No radar here" |
| adsb.fi's open data are for non-commercial use, at most 1 request a second, with credit; adsb.lol's rate limits move and may need an API key later | Caps, back-off and credits; routes are optional, and the flight radar works without them |
| The map's data (0.5–1 MB) bring the image to about 2.5 of the app slot's 4 MB | The generator thins the data; OTA still fits. As built: 859 KB of map, an image of 2.47 MB, 41 % of the slot free |
| The forecast's request line (451–454 B) sits about 10 B short of esp_http_client's 512 B transmit buffer, past which the request goes out empty and the step times out (M6 review) | Open: `fetch` would set `.buffer_size_tx = 1024` before the URL grows |
| Open after M6's review: the radar refresh isn't a wake input (a display interval above 5 min slows it, a clock set back stops it until it catches up); a retry on a hung kept connection gets the full timeout again; "png:" details cut at 24 bytes and timeouts reading "ESP_FAIL"; `rain.map` shows "—" before the first frame; an older RainViewer frame from a smaller tile set can survive a second pan into the loop; `radar.bin`'s CRC covers only the levels; about 12 KB of new internal static buffers; no wrap at the antimeridian | Deferred minors, for the owner to choose |
| GeoNames' towns end at ČHMÚ's area: views beyond it show small towns crowding its edge (Teutschenthal, Boxberg) | Cosmetic, outside the home region |
| A Rain radar wake is awake 287 ms on light sleep (346 ms on deep, with the file), against about 60 ms for the dashboards | The owner's power measurements (§9.4) decide; the town loop's doubles are the first saving |
| Rain dithered on 1 bit over borders and labels | Label halos; the owner checks the panel |
| The web UI moving focus to a text box on a phone (owner, 2026-09-30) | Not reproduced in iOS 26 Safari; waiting for the phone and browser |
| Homebrew Python 3.14 on this Mac (3.14.6 and 3.14.7 checked) can't load `pyexpat` (it expects a newer libexpat than macOS 26.2 has), which breaks pip and the ESP-IDF installer | ESP-IDF uses uv's Python 3.13 through `~/esp/python-shim` (`AGENTS.md` §6) |

## 21. Revision history

| Rev | Date | Changes |
|---|---|---|
| r1 | 2026-09-25 | First draft |
| r2 | 2026-09-25 | Configurable sync schedule (`times` / `interval` / `always` / `manual`) and display update interval (D11, §9.2, §9.3); terms defined (§1.4); no RTC cell fitted, so a boot with invalid time syncs at once (§3.3, §7); ST7305 partial-update findings: RAM windows are supported but don't lower panel power, so v1 pushes full frames (§4.2) |
| r3 | 2026-09-25 | Approved by the owner; licence confirmed (D8); Python toolchain risk recorded (§20) |
| r4 | 2026-09-25 | M1 panel check: factory init sequence with a separate LPM rate, default 1 Hz, configurable at runtime (D12, §4.2); a 120 ms wait after a panel reset (datasheet §12.1.4); HPM/LPM switching delays from datasheet §7.11; `panel rate` and `panel fps` (§15); landscape only (D13); `display` and `util` components (§3.1) |
| r5 | 2026-09-28 | M2: a tethered board stays awake and light sleep is entered explicitly (D14, §3.4), with measured wake costs; board, rtc and power rows updated (§3.1); NVS `sys/idle` (§14.2); `sleep test` (§15) |
| r6 | 2026-09-28 | M2 review: routine wakes skip the console, NVS and info logs (§3.3), which cuts a deep wake from 168 to 63 ms of app time (§3.4); a failed boot keeps the console up while tethered, otherwise it sleeps 5 min and boots again (§3.3); `sleep stats` reports per-cycle slept times (§15); a boot that comes alive reports a stored core dump (§16) |
| r7 | 2026-09-28 | M2 measurement: D3 is light sleep (§1.2, §3.4); the forced-PWM 3V3 converter sets a ~60 mW floor and puts the stretch goal out of reach without a board rework (§9.4, §20) |
| r8 | 2026-09-28 | M3 scope: accepted proposals (D15): the LPM rate setting (§4.2), extra local fields and trends (§5.1), the schedule (§5.4), night sleep (§9.1, §9.2), the `cs` pack (§5.8); hidden menu items for later features (§5.7); M3 split into M3a and M3b (§18) |
| r9 | 2026-09-29 | M3a as built: the status bar's `status_clock` and `status_battery` options and the built-in Indoor preset, from the owner's render review (§5.2, §5.4); preset validation (§5.4); widget sizing and ellipsis (§5.3); the datastore's scope, ownership and snapshot (§6); fonts and Material icons (§4.4, §4.5); the `lang_` prefix and storage's host-tested parts (§3.1); `power_plan()` in the runtime model (§3.2); the settings keys read and the `/fs` mount (§14.3); `field clear` (§15); golden updates and the sanitizer build (§17) |
| r10 | 2026-09-29 | Owner decisions (D16): the `cs` pack ships the public holidays only for now (§5.8, §20); a held button is left out of the wake sources (§9.2) |
| r11 | 2026-09-29 | M3b as built. The menu without Contrast and with the offset steps (D17, §5.7), and the menu's gesture timings (§5.6). Toasts and the critical screen (§5.5), the critical hysteresis and the humidity clamp (§8). The schedule's order and validation (§5.4), and night sleep (§9.1). Panel sleep and wake with NRDSLP (§4.2). The fallback toast, the kept backup and the nesting limit (§14.3, §5.4), and factory reset (§14.4). The `night`, `schedule` and `panel sleep\|wake` commands, and `--list` for the renderers (§15, §17). The sensor TTL (§5.1) and a critical-battery risk (§20) |
| r12 | 2026-09-29 | M3b review: a held button no longer keeps the board awake, and an ignored press's release is debounced (§9.2); the critical sleep wakes on a held KEY's release and stays awake while a PC is attached (§8); schedule entries at a night's end run, and none run after a long gap or on the critical screen (§5.4); the date-time editor starts in 2026 (§5.7) |
| r13 | 2026-09-30 | M4 scope (D18): a web UI password instead of the admin PIN (§10.2, §10.4, §14.2, §19), with Menu ▸ Wi-Fi ▸ Reset web password (§5.7); one M4 plan; no ELF hash in the snapshot |
| r14 | 2026-09-30 | M4 spike review (D19): when the first-run screen appears, and its buttons (§5.5, §5.6); the status bar's Wi-Fi state moves to M5 and M6 (§5.2); web restarts return in config mode (§10.2); the M4 pages, with Device, and the clock set from the phone (§10.3); changing the web password and logging out (§10.4); the −2.0 °C default temperature offset (§8); accepted extras (§19) |
| r15 | 2026-09-30 | M4 as built: the components (§3.1); the menu's Wi-Fi section and Info rows (§5.7); the AP's channel, testing a network, and the portal address (§10.1); what ends config mode and what runs meanwhile (§10.2); the API (§10.3); the password record, sessions and login throttle (§10.4); OTA checks and measurements (§10.5); NVS keys (§14.2); `location.*` and the merge patch (§14.3); the backup bundle (§14.4); `wifi` (§15); the new host tests (§17) |
| r16 | 2026-09-30 | M4 review: long menu values are cut (§5.7); leaving a network for a test is never its failure (§10.1); config mode doesn't watch the battery, and the resume flag is in NVS (§10.2, §14.2); bodies are read once, nested at most 18 levels, and a stalled client gets 408 (§10.3); 2 KB of headers for other gadgets' cookies (§10.4); an image that fails to start rolls back at once (§10.5) |
| r17 | 2026-09-30 | M4 acceptance (D20): the globe in the status bar and the dashboard in config mode while a phone is logged in (§5.2, §10.2); the running clock's phase and the seconds' refresh rate (§7); battery samples in config mode and the battery calibration (§8, §14.3); the Device page's battery card and the preset editor's slot names, new presets in the cycle and Undo (§10.3); two open items (§20) |
| r18 | 2026-09-30 | D21: learning the battery curve from a full discharge (§8), `POST /api/battery/learn` (§10.3), `battery.learned_mv` (§14.3); the open item on a learned curve is closed (§20) |
| r19 | 2026-10-01 | ADS-B flight radar proposal, for after M5, with three owner questions (§19, §19.1) |
| r20 | 2026-10-01 | The owner's answers on the ADS-B radar (D22, §19.1): adsb.fi only, the always-on client mode, a centred and zoomed map with built-in towns and airports; one question left |
| r21 | 2026-10-01 | The ADS-B radar runs in sync mode `always` (D22); its last question is closed (§19.1) |
| r22 | 2026-10-01 | Weather radar proposal: ČHMÚ and RainViewer, a map shared with the ADS-B radar, three owner questions (§19, §19.2) |
| r23 | 2026-10-01 | Both radars join the roadmap as M7 (D23): the milestone table (§18), the weather radar's answers and power (§19.2), the flight radar's slot (§19.1); Audio becomes M8 and microSD M9 (§5.2, §5.7, §9.4, §10.3, §10.5, §13, §14, §15, §19, §20) |
| r24 | 2026-10-01 | D24: sync mode `always` moves into M5, the radars become M6 and MQTT/HA M7 (§5.1, §5.2, §9.4, §10.3, §18, §19) |
| r25 | 2026-10-01 | M5 scope (D25): air quality and pollen fields with their bands and levels (§5.1, §6, §11); the status bar's sync and Wi-Fi state (§5.2); the Weather preset in the cycle (§5.4); the menu's Sync section (§5.7); SNTP to the millisecond and the RTC trim (§7, §14.2, §20); quiet hours, config mode and `always` mode in the sync, its steps and results (§9.3, §10.2); fast connect without RTC RAM (§10.1); the Sync page, the place search and their API (§10.3); the web UI on the LAN (§10.4); `time.ntp` and `sync.*` (§14.3); `sync status` (§15); the tests (§17); the M5 row (§18, §19) |
| r26 | 2026-10-01 | M5 spike review (D26): Weather Icons beside Material Icons (§4.5); `aq.uv` and the UV bands, the day length's change, "None" for pollen (§5.1, §6, §11) |
| r27 | 2026-10-01 | M5 as built: the components (§3.1); the menu's Sync section and Info row (§5.7); the snapshot's size and the forecast file (§6); our own SNTP client, the lost clock's retries and the trim's loading and reach (§7); the 45 s, the syncs the device needs, nights and leaving `always` mode, and the measured syncs (§9.3); rejoining by name and a station without an address (§10.1); the place search's and the status's final shape (§10.3); the new tests (§17); three open items (§20) |
| r28 | 2026-10-01 | M6 design (D27, D28): the map, the weather radar with its loop, the flight radar with routes, and rain in the next 2 hours (§11.1–§11.4); their fields, layouts, presets and control (§5.1, §5.2, §5.4, §5.6); the 15-minute rain's storage (§6); the radar step, refresh and flights in the sync (§9.3, §9.4); the Radar page and status (§10.3); `radar.*` and the frame file (§14.3); `radar` commands (§15); the tests (§17); the M6 row (§18); M6's place in §19 and M9's new candidates; five risks (§20) |
| r29 | 2026-10-02 | D29, the owner's answers to the M6 plan: GeoNames' towns in ČHMÚ's radar area, with their credit (§1.2, §11.1) |
| r30 | 2026-10-02 | M6 as built: the components (§3.1); `wx.rain2h`, `rain.map` and the two layouts (§5.1, §5.2); the snapshot (§6); the refresh, a sync on demand behind it and ČHMÚ's names from NTP (§9.3); the Radar page and the status API (§10.3); the map's pack and D29's towns, labels on boxes (§11.1); the frame file, coverage, `2/0_0`, zlib on the host (§11.2); the flight radar's failures and board figures (§11.3); the 15-minute convention (§11.4); `radar.bin` (§14.3); `radar status` (§15); the tests (§17); open items (§20) |
