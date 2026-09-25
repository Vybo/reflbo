# reflbo firmware: design spec

- **Date:** 2026-09-25
- **Status:** Approved by the owner on 2026-09-25 (r3; changes listed in §21)
- **Covers:** firmware v1, milestones M0–M8
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
| R9 | Use the microSD slot (feature set agreed at M8) |
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
| D3 | Choose the idle strategy (deep or light sleep) from USB power-meter measurements at M2 | Both are supported until then (§3.4) |
| D4 | English by default, structured as language packs | No other pack in v1 |
| D5 | Home Assistant over MQTT with HA MQTT discovery | |
| D6 | Power is best effort; an average below 2 mA is the stretch goal | |
| D7 | Other local devices publish to MQTT, and the device maps topics to fields | Same mechanism as HA values (§12.5) |
| D8 | Licence Apache-2.0, plus `NOTICE` and `THIRD_PARTY.md` (confirmed 2026-09-25) | Same licence as the Waveshare code we adapt. `NOTICE` carries attribution into forks |
| D9 | No RTC backup cell is fitted now; one can be fitted later. Firmware must work without it (§7) | The owner may fit an ML1220 |
| D10 | Features beyond R1–R11 and N1–N5 are proposals, discussed at the relevant milestone (§19) | |
| D11 | The sync schedule and the display update interval are both configurable (§9.2, §9.3) | Owner adjustment in r2 |
| D12 | Panel: the factory init sequence, with the LPM refresh rate set separately (default 1 Hz; `panel rate` changes it from 0.25 to 8 Hz) (§4.2) | Owner check at M1: factory contrast is visibly better than XiaoZhi's, and it looks the same at 1 Hz as at 8 Hz |
| D13 | Landscape only | Portrait orientation declined at M1 |

### 1.3 Out of scope for v1

- Everything in §19.
- BLE and ESP-NOW sources.
- Voice features.
- Network services other than Open-Meteo, NTP and the owner's MQTT broker.
- Portrait orientation.

### 1.4 Terms

| Term | Meaning | Frequency | Radio |
|---|---|---|---|
| **Sync** | A network session: Wi-Fi on → time (NTP) → weather → MQTT → Wi-Fi off | Configurable schedule (§9.3); default once a day at 05:30 | Yes |
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
| `board` | Pin map, I²C bus, GPIO setup, buttons → gestures, wake-cause decoding | IDF | gesture logic |
| `st7305` | Panel init, frame push, LPM/HPM, deep-sleep retention | board | frame conversion |
| `display` | Canonical framebuffer; pushes it to the panel when its CRC changes | gfx, st7305, util | — |
| `gfx` | Framebuffer, primitives, text, fonts, bitmaps, QR, PBM/BMP encoders | — | ✓ |
| `util` | Small pure-C helpers: CRC-32, base64, delay ticks | — | ✓ |
| `locale` | Language packs: strings, date and number formats | — | ✓ |
| `astro` | Sunrise, sunset, day length | — | ✓ |
| `datastore` | Field registry, values, freshness, change events, snapshot | — | ✓ |
| `ui` | Layouts, widgets, presets, cycler, screens, menu, input handling | gfx, locale, datastore | ✓ |
| `scheduler` | Next-wake computation for display, sensors, alarms, sync, timeouts | — | ✓ |
| `sensors` | SHTC3, battery gauge | board | curve and filter logic |
| `rtc` | PCF85063: time, oscillator-stop flag, alarm → INT | board | — |
| `timekeeping` | System time from the RTC, time zone, SNTP, manual set | rtc | TZ logic |
| `power` | Power states, idle strategy, wake sources, sleep entry | board, rtc, st7305 | — |
| `netmgr` | Wi-Fi STA/AP state machine, captive DNS, mDNS | IDF | — |
| `webui` | HTTP server, REST API, embedded web assets | netmgr, storage, ui | — |
| `weather` | Open-Meteo client and parser | netmgr | parser |
| `ha_mqtt` | MQTT session, discovery, state, commands, field mappings | netmgr, datastore | payload builders |
| `sync` | Runs the sync sequence, handles backoff | timekeeping, weather, ha_mqtt, netmgr | — |
| `audio` | Codec control, tone/WAV/stream players, alarm ringing | board, storage | — |
| `storage` | NVS (identity, secrets), LittleFS config files, microSD mount | IDF | JSON schemas |
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
3. When the queue is empty and the power state allows it, the app task calls `power_idle()` (§3.4).

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
- **Button presses after a deep-sleep wake.** The press that woke the chip may be over by the time the app runs, roughly 100–300 ms later.
  - If the pin is already high, treat it as a short press. When the current context binds a double press, wait out the double-press window first.
  - If the pin is still low, keep timing it for a long press.

### 3.4 Idle strategies (D3)

Both strategies live behind `power_idle()` until the M2 measurements pick the default.

| | Deep sleep | Automatic light sleep (tickless) |
|---|---|---|
| RAM state | Lost; RTC-RAM snapshot of at most 4 KB | Kept |
| Wake latency | Full boot, about 100–300 ms (measure) | Under 1 ms |
| ESP32-S3 floor current | µA range | Hundreds of µA (measure, PSRAM included) |
| USB console | Disconnects | Stays up |
| Extra complexity | Snapshot and fast-boot path | GPIO wake interrupts |

- **Tethered mode.** If `usb_serial_jtag_is_connected()` is true at boot, the device uses light sleep whatever the setting is. The console and flashing then keep working.
- **Decision rule at M2.** Choose deep sleep if the measured average current is clearly lower (guideline: at least 20 % lower at the one-minute cadence). Otherwise choose light sleep.

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
- **LPM frame rate.** The LFRA field of `B2h` (0.25–8 Hz), written after the vendor sequence, so it is independent of that sequence. Default 1 Hz. A new rate applies at once, even in LPM. `panel rate` changes it at runtime; offering it as a user setting is an M3 proposal (§19). Its power cost is measured at M2/M5.
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
- **Sizes.** Initial: text 12, 16, 20, 28 px; numeric 48, 72, 110 px. Tuned in M1/M3.
- **Choice.** Specific fonts are picked in M1, for example a pixel font for small sizes and a clean sans for large digits. Every font needs a licence that allows redistribution in this repository.

### 4.5 Icons

- A 1-bpp icon set generated into C bitmaps by `tools/imggen.py`, at sizes 16, 24, 48 and 64 px.
- Contents: weather codes (day and night variants), battery levels, charging, Wi-Fi, sync/stale, alarm bell, thermometer, humidity, sunrise, sunset.
- Sources and licences are recorded in `THIRD_PARTY.md`.

## 5. UI

### 5.1 Field catalogue (v1)

| Field id | Kind | Source | Freshness |
|---|---|---|---|
| `time.clock` | time | system time | Valid whenever the time is valid |
| `date.day` | date | system time | Same |
| `env.temp` | number, °C | SHTC3 | Stale after 15 min without a reading |
| `env.hum` | number, % | SHTC3 | Same |
| `bat.level` | battery (%, V, charging state) | gauge | Stale after 15 min |
| `wx.now` | weather (code, temperature) | Weather data (see below) | Normal until the expected sync interval (§9.3) plus 2 h has passed since the last fetch; stale after that; missing past the forecast range |
| `wx.today` | weather day (code, min, max, precipitation %) | forecast | Same |
| `wx.hourly` | series: next 12 h (temperature, code) | forecast | Same |
| `wx.daily` | series: 3 days | forecast | Same |
| `sun.times` | pair (sunrise, sunset) | `astro`, computed locally | Always, given a valid date and location |
| `mqtt.<key>` | number or text, with unit and label | MQTT mapping (§12.5) | Configured TTL; default twice the expected sync interval (§9.3) |

`wx.now` uses the `current` block while it is at most 60 minutes old. After that it uses the hourly forecast entry for the current local hour. This keeps "now" meaningful between syncs, even with a once-a-day schedule.

### 5.2 Layouts (v1)

Every layout can show a status bar (top 20 px). It carries the battery icon and %, charging state, Wi-Fi/sync state or a stale warning, and the next alarm (M7).

| Layout | Slots |
|---|---|
| Classic | `main` XL (time), `sub` M (date), bottom row `s1`–`s4` S |
| Weather | `now` L, `today` M, `hourly` M (series strip), `s1`–`s2` S |
| Grid | `g1`–`g6` M, in 3×2 |
| Focus | `main` XL, `s1`–`s2` M |

- Slot rectangles are fixed per layout and defined in code. They are finalised in M3 from host renders the owner reviews.
- Each slot declares which field kinds it accepts. The preset editor offers only compatible fields.

### 5.3 Widgets

- There is one renderer per field kind and size class (XL/L/M/S). For example, a number widget shows label, value and unit, while the S size shows only an icon and the value.
- Each slot sets a policy for missing or stale data (a preset option):
  - `hide`: leave the slot empty.
  - `placeholder`: show `—`.
  - `stale`: show the value with an age marker, e.g. "⟲ 2 d".

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
        "s1": "env.temp", "s2": "env.hum", "s3": "wx.now", "s4": "bat.level"
      },
      "options": { "clock_24h": true, "seconds": false, "invert": false, "stale_policy": "stale" }
    }
  ]
}
```

- **Built-in defaults.** Home (Classic), Weather and Focus clock are compiled in. They are used when the file is missing or invalid. There can be at most 16 presets.
- **Switching.** Presets change by:
  - KEY short: next preset in cycle order.
  - The auto-cycle timer: interval at least 10 s; each switch costs a wake-up.
  - The HA `select` entity (§12.3).
  - The web UI or the menu.
- **Seconds.** `seconds: true` needs a wake-up every second. It is allowed, and the web UI shows the power cost.

### 5.5 Screens

| Screen | Content |
|---|---|
| Dashboard | The active preset |
| Menu | §5.7 |
| Config mode | Wi-Fi state; URL (`http://reflbo-XXXX.local`) and IP; a QR code to join the AP or open the URL; time left before timeout |
| First run | "Hold BOOT 3 s to set up Wi-Fi · Hold KEY for menu", plus the clock if the time is valid |
| Alarm ringing | Large time, the alarm label, button hints |
| Radio | Station, ICY title, volume, a battery warning when on battery |
| Critical battery | "Battery empty — charge me" and the last known time. Nothing else updates |
| Toast | A short overlay (e.g. "Preset: Weather", "Sync failed") shown for about 3 s |

`XXXX` is the last two bytes of the Wi-Fi STA MAC in lowercase hex. The same id is used for the hostname, AP SSID, MQTT client id and device id.

### 5.6 Controls

- **Gesture timings.**
  - Debounce: 30 ms.
  - Double press: 300 ms window, waited for only when the context binds a double press.
  - Long press: fires at 1 s while the button is still held.
  - BOOT on the dashboard: long press fires at 3 s instead, so Wi-Fi doesn't switch on by accident.
- A context binds at most one long gesture per button.

| Context | KEY short | KEY double | KEY long | BOOT short | BOOT long |
|---|---|---|---|---|---|
| Dashboard | Next preset | Auto-cycle on/off | Open menu | Refresh sensors | Config mode (3 s) |
| Menu: browsing | Next item | — | Select / enter | Back | Exit menu |
| Menu: editing a value | + | — | Confirm | − | Cancel |
| Config mode | Toggle QR (join AP / open URL) | — | — | — | Exit config mode |
| Alarm ringing | Snooze | Snooze | Stop | Snooze | Stop |
| Radio | Volume + | Next station | Open menu | Volume − | Stop radio |

The `diag` console command `btn` injects the same gestures. Holding BOOT at power-on still enters download mode; holding it at runtime is safe.

### 5.7 Menu (v1)

```
Presets    ▸ Active preset · Auto-cycle on/off · Interval (10 s … 1 h)
Alarms     ▸ Alarm 1–8: on/off · Time · Days · Sound · Volume        (M7)
Radio      ▸ Play/stop · Station · Volume                            (M7)
Wi-Fi      ▸ Config mode · Forget networks
Sync       ▸ Sync now · Schedule (times / interval / always / manual) · Times or interval
Time       ▸ Set date and time · 24-hour clock · Time zone (short list)
Display    ▸ Contrast · Update interval (1–15 min)
Sensors    ▸ Temperature offset · Humidity offset · Units (°C/°F)
Info       ▸ Battery V/% · Firmware · IP/MAC · Last sync result · Uptime · Free heap
System     ▸ Reboot · Factory reset (with confirmation)
```

- The menu closes after 60 s without input.
- The full time zone picker is in the web UI.

### 5.8 Language packs

- **What a pack holds.** Each pack in `locale` is a set of C tables:
  - String ID → UTF-8 text.
  - Weekday and month names, including short forms.
  - Date and time patterns.
  - Decimal separator.
  - First day of the week.
  - The character set the pack needs, so `fontgen` can check coverage.
- **v1.** Ships only `en`. `settings.language` selects the pack.
- **Web UI.** English only in v1.

## 6. Datastore

- **Table.** An entry for each built-in field, plus up to 32 dynamic `mqtt.<key>` entries.
- **Entry contents.**
  - Id and kind.
  - Value: a number (float plus precision), short text (at most 48 bytes of UTF-8), a time, or a weather struct/series.
  - Unit and label.
  - `updated_at` (UTC) and `ttl_s`.
  - Flags.
- **Weather storage.** Kept compact: 72 hourly entries (int16 temperature ×10, uint8 code, uint8 precipitation %) and 3 daily entries.
- **API.** Typed setters and getters, a freshness check, and a change mask posted as `DATA_CHANGED`. A mutex protects the table; getters copy values out.
- **Snapshot.** Binary format: magic, version, CRC32, entries, at most 4 KB in total. It is written:
  - To RTC RAM before every idle.
  - To `/state/datastore.bin` after every sync, so data survives a power-off and reappears marked as stale.

## 7. Timekeeping

- The PCF85063 stores UTC. At boot and on every wake the firmware reads the RTC and calls `settimeofday()`. If the oscillator-stop flag is set, the time counts as invalid until SNTP or a manual set.
- The time zone is a POSIX TZ string in the settings (default `CET-1CEST,M3.5.0,M10.5.0/3`), stored alongside its IANA name for display. The web UI maps IANA names to POSIX strings.
- SNTP runs during each sync (servers `cz.pool.ntp.org` and `pool.ntp.org`, 5 s timeout) and writes the result to the RTC.
- Manual set: the date/time editor in the menu, or "set time from phone" in the web UI (sends the epoch and time zone).
- RTC configuration: CLKOUT is disabled (`COF = 111`) to save current. The RTC alarm serves the wake scheduler (§9.2). User alarms are evaluated in firmware.
- **Without a backup cell (the current state, D9).**
  - The RTC keeps time as long as the board has power: while running on battery, and through deep and light sleep.
  - It loses the time when PWR switches the board off, or when the battery is removed or runs flat with no USB attached.
  - On the next boot the oscillator-stop flag is set. With Wi-Fi configured, the firmware syncs at once to fetch the time. Without Wi-Fi it shows "Set time" and waits for a manual set.
  - Alarms stay suspended until the time is valid.
- **Fitting a cell later.**
  - Use a rechargeable ML1220 with leads and a 2-pin 1.0 mm plug for connector J7. Never use a CR1220: the board charges the cell whenever it is powered.
  - Per the schematic, J7 pin 1 is + (RTC_BAT) and pin 2 is GND. Pre-wired cells don't all share one pinout, so check the polarity with a multimeter before plugging one in.
  - No firmware change is needed.

## 8. Sensors and battery

**SHTC3**

- Sampled every 5 min (configurable 1–30 min) and on BOOT short. The sensor is sent to sleep after each reading.
- Low-power measurement mode (about 0.8 ms instead of about 12 ms) is used if M2 shows its accuracy is good enough.
- Temperature and humidity offsets are settings. Defaults come from an M2/M5 comparison with a reference thermometer.
- Readings are taken right after wake, before Wi-Fi or the CPU warm the board.

**Battery gauge**

- **Reading.** ADC1_CH3 one-shot at 12 dB with curve-fitting calibration. Average 16 samples, then multiply by 3 (the divider) and by a per-device factor (default 1.000).
- **When.** Only while idle (no Wi-Fi, no audio): every 5 min and before each sync.
- **Level.** Voltage maps to % through a Li-ion open-circuit-voltage table (NCR18650B-like). The result is smoothed with an EMA and never jumps up unless charging is inferred.
- **Charging state (inferred; there is no hardware signal).**

  | State | Rule |
  |---|---|
  | `charging` | VBAT rose at least 30 mV over the last 30 min while idle |
  | `full` | VBAT at or above 4.15 V and steady |
  | `discharging` | Otherwise |
  | `unknown` | Not enough history yet |

  `usb_serial_jtag_is_connected()` additionally marks external power when a PC is attached.
- **Thresholds.**
  - Low, 15 %: status icon; sync retries are skipped.
  - Critical, 3.3 V or below: critical screen. Only KEY wakes the device.

## 9. Power management and scheduling

### 9.1 Power states

| State | When | Panel | Leaves by |
|---|---|---|---|
| ACTIVE | Menu, config mode, alarm ringing, radio | HPM for menu and config mode; otherwise LPM | Timeout (menu 60 s, config 10 min, alarm auto-stop 10 min) or user action |
| IDLE | Dashboard | LPM | Any event |
| SYNC | Sync running | LPM, with a sync indicator | Sync done |
| CRITICAL | Battery at or below 3.3 V | Final screen | KEY press, if the voltage has recovered |

### 9.2 Wake scheduler

- `scheduler_next_wake(now, state)` returns the earliest of these:
  - The next display update: every `display.update_min` minutes (1–15, default 1, aligned to the minute), every second when a preset shows seconds, and the next cycle switch.
  - The next sensor sample.
  - The next user alarm, including snoozes.
  - The next sync.
  - Any running timeout.
- It is a pure function. Host tests cover DST transitions.
- **Minute-aligned wakes use the PCF85063 alarm.** When the alarm fires, the chip latches the AF flag and holds INT low until firmware clears it, which triggers the ext1 wake reliably. The alarm is programmed before each idle. Other wakes use the ESP timer. A backup ESP timer set about 5 s after the RTC alarm covers a missed INT.
- **User alarms** are checked at minute ticks in local time.
  - On spring-forward, a time that doesn't exist fires at the first valid minute after it.
  - On fall-back, a repeated time fires once.

### 9.3 Sync schedule and sequence

The sync schedule (`settings.sync`) is fully configurable from the menu and the web UI:

| Mode | Behaviour | Parameters |
|---|---|---|
| `times` (default) | Sync at fixed local times | 1–8 times of day; default `05:30` |
| `interval` | Sync every N minutes, aligned to the clock | N = 15–1440 |
| `always` | Wi-Fi and MQTT stay up. Weather refreshes every 60 min. State is published on change (at most every 30 s) and every 5 min. Commands and MQTT fields apply at once. Meant for USB power; not auto-detected | — |
| `manual` | Sync only on demand | — |

- The UI offers shortcuts: *Battery saver* = `times ["05:30"]`, *Balanced* = `interval 60`, *Always connected* = `always`.
- In every mode a sync can also be started on demand: from the menu, the web UI or `sync now`.
- **Expected sync interval.** Data freshness and MQTT `expire_after` depend on it:

  | Mode | Expected interval |
  |---|---|
  | `times` | Largest gap between consecutive times (24 h for a single time) |
  | `interval` | N |
  | `always` | 10 min |
  | `manual` | None: data never expires and only shows its age |

Sequence. The steps are independent and each has a timeout. The radio may be on for at most 45 s in total.

1. **Wi-Fi connect.** Saved networks, trying the cached BSSID and channel first.
2. **SNTP.** 5 s.
3. **Weather.** 10 s.
4. **MQTT.** 15 s.
   1. Connect with a persistent session.
   2. Subscribe to field and command topics.
   3. Collect retained and queued messages until 1 s passes with none, or until every mapped topic has arrived.
   4. Publish state, plus discovery if the config or firmware changed.
   5. Disconnect.
5. **Finish.** Wi-Fi off. Persist the datastore snapshot. Record each step's result (shown in Info and the web UI).

On failure, retry after 15, 30 and 60 min, then wait for the next scheduled sync. At low battery there are no retries.

### 9.4 Power budget and measurement

**Measurement method** (owner, USB power meter):

- Power the board from a USB charger, not a PC. The console then stays off, and the firmware uses its normal sleep policy.
- Remove the 18650, or make sure it is fully charged, so charging current doesn't distort the readings.
- For each idle scenario, use the meter's mAh counter over at least 1 h. For events (sync, alarm), record the mAh over N repetitions.
- Estimate battery-side current as `I_bat ≈ I_5V × 5 V / V_bat`. This ignores converter losses; say so when reporting.
- Scenarios:
  - M2: deep sleep vs light sleep while idle.
  - M5: energy per sync.
  - Config mode.
  - M7: radio.
- Record results in `docs/power.md` with the date, commit, settings and meter model. Many USB meters are inaccurate below 1 mA, so long accumulation windows matter.

**Rough budget for the stretch goal** (to be replaced by measurements):

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
- **Fast connect.** BSSID and channel are cached in NVS and RTC RAM.
- **Hostname.** mDNS `reflbo-XXXX.local`, advertising `_http._tcp`.
- **AP.**
  - SSID `reflbo-XXXX`, WPA2-PSK with a random 10-character password generated at first boot and kept in NVS.
  - IP 192.168.4.1; at most 2 clients.
  - Captive DNS answers every name with the AP IP, and OS connectivity-probe URLs redirect to `/`.

### 10.2 Config mode

- **Entry.** BOOT long (3 s) on the dashboard, the menu, or first run.
- **Connecting.** With saved networks, it joins as a station and shows the LAN URL and IP. If that fails or no network is saved, it starts the AP and captive portal. While the owner tests a new network from the web UI it runs AP and STA together.
- **Exit.** BOOT long, "Done" in the web UI, or 10 min without HTTP requests. Wi-Fi then switches off.

### 10.3 Web UI and REST API

- **Tech.** Plain HTML/CSS/JS, mobile-first. The files are gzipped and **embedded in the app image**, so OTA updates them and flashing never touches user data.
- **Pages.** Status, Wi-Fi, Location & time, Presets (editor with live preview), Sync, MQTT/HA, Alarms, Radio, Firmware, Backup.
- **API.** JSON. Mutating requests must send `Content-Type: application/json`.

| Method and path | Purpose |
|---|---|
| `GET /api/status` | Device, battery, sensors, time, Wi-Fi, last sync steps, firmware |
| `GET/PATCH /api/settings` | Non-secret settings; secrets are accepted on write and never returned |
| `GET /api/wifi/scan` · `GET/POST/DELETE /api/wifi/networks` | Wi-Fi setup |
| `GET /api/layouts` · `GET /api/fields` | Slot definitions; the field catalogue with current values |
| `GET/PUT /api/presets` | The preset document (§5.4) |
| `GET /api/preview.bmp?preset=<id>` · `POST /api/preview.bmp` | Render a saved or unsaved preset with live data, using the real renderer |
| `GET /api/screenshot.bmp` | The current frame |
| `POST /api/time` | Set time from the phone (epoch, IANA zone, POSIX TZ) |
| `GET /api/geocode?q=` | Proxy for the Open-Meteo geocoding search (station mode only; manual lat/lon always works) |
| `POST /api/sync` | Sync now |
| `GET/PUT /api/alarms` · `GET/PUT /api/stations` · `POST /api/radio/play` · `POST /api/radio/stop` | Audio (M7) |
| `POST /api/ota` · `GET /api/ota/status` | Firmware upload |
| `GET /api/backup` · `POST /api/restore` | Settings bundle without secrets |
| `POST /api/reboot` · `POST /api/factory-reset` | System |

### 10.4 Security

- The configurator is reachable only in config mode, or on the LAN in `always` sync mode.
- The AP uses WPA2.
- Secrets are write-only and never logged.
- The JSON content-type check blocks cross-site form posts.
- OTA images are checked for project name, chip and version before the device switches to them.
- An admin PIN is a deferred proposal (§19).

### 10.5 OTA

- Two app slots with rollback. A new image marks itself valid only after panel init, a first render, and 60 s without a panic. Otherwise the bootloader rolls back at the next reset.
- Sources: upload through the web UI (M4). A file on microSD is an M8 candidate.

## 11. Weather and astro

**Request** (once per sync, about 2.6 KB of JSON for 3 days; checked against the live API on 2026-09-25):

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
- **Astro.** Sunrise, sunset and day length use the NOAA solar algorithm at the configured location. They work offline. Host tests compare them with the sunrise/sunset values in the Open-Meteo fixtures (±2 min).

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

### 13.2 Alarms (M7)

- **Storage.** Up to 8 alarms in `/cfg/alarms.json`: `{id, enabled, time "HH:MM", days (Mon–Sun bitmask; 0 = once), label, sound, volume, snooze_min (default 9), ramp_s (default 30)}`.
- **Sounds.** Built-in generated patterns, and 16-bit PCM WAV files from LittleFS or microSD. MP3 files are added once the radio decoder exists.
- **Ringing.** The ACTIVE state shows the alarm screen and ramps the volume up. It stops automatically after 10 min. A short press snoozes; a long press stops.
- **Conditions.** Alarms work offline and from deep sleep, but need a valid time.
- **Display.** The next alarm is shown in the status bar.

### 13.3 Internet radio (M7)

- **Start.** From the menu or the web UI. It needs Wi-Fi, which stays on while the radio plays.
- **Pipeline.**
  1. HTTP(S) stream via `esp_http_client`, sending `Icy-MetaData: 1`.
  2. PSRAM ring buffer of about 256 KB.
  3. Decoder task using Espressif `esp_audio_codec` (MP3, AAC).
  4. PCM out to the ES8311.
- **Stations.** The list lives in `/cfg/stations.json` (name, URL). Defaults are chosen at M7.
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
| `sys` | Device id, AP password, schema version |
| `wifi` | Saved networks (SSIDs and passwords), fast-connect cache |
| `secrets` | MQTT password; future tokens |
| `ctr` | Counters: boots, sync statistics |

### 14.3 LittleFS layout

```
/cfg/settings.json      non-secret settings
/cfg/presets.json       §5.4
/cfg/mqtt_fields.json   §12.5
/cfg/alarms.json        §13.2 (M7)
/cfg/stations.json      §13.3 (M7)
/state/datastore.bin    last datastore snapshot (written after each sync)
/sounds/                user sound files (M7)
```

- Every file carries a schema version, and migrations run at boot.
- Writes are atomic: write `*.tmp`, fsync, rename, keeping the previous file as `*.bak`.
- If a file is invalid, the firmware uses `*.bak`; if that is invalid too, it uses defaults and shows a toast.

`settings.json` sketch:

```json
{
  "schema": 1,
  "language": "en",
  "location": { "name": "Brno", "lat": 49.1951, "lon": 16.6068 },
  "time": { "tz_iana": "Europe/Prague", "tz_posix": "CET-1CEST,M3.5.0,M10.5.0/3",
            "clock_24h": true, "ntp": ["cz.pool.ntp.org", "pool.ntp.org"] },
  "units": { "temp": "C" },
  "sync": { "mode": "times", "times": ["05:30"], "interval_min": 60 },
  "sensors": { "interval_min": 5, "temp_offset_c": 0.0, "hum_offset_pct": 0.0 },
  "display": { "contrast": "default", "update_min": 1 },
  "mqtt": { "enabled": false, "host": "", "port": 1883, "user": "",
            "discovery_prefix": "homeassistant", "discovery": true }
}
```

### 14.4 Backup, restore, factory reset

- **Backup.** A JSON bundle of every `/cfg/*` file, without secrets. Restore validates the schemas before replacing anything.
- **Factory reset.** From the menu (with confirmation) or the web UI. It erases `storage` and the NVS namespaces `wifi`, `secrets` and `ctr`. It keeps `sys`, the device identity.

### 14.5 microSD (M8)

- FAT32 on SDMMC 1-bit. The card is mounted on demand and unmounted after use; a mount attempt detects whether one is present.
- The feature set is agreed at the start of M8, from the candidates in §19.

## 15. Diagnostics and tooling

**Console** (`esp_console` over USB-Serial-JTAG):

| Command | Purpose |
|---|---|
| `version` · `reboot` · `heap` · `tasks` | Basics |
| `screenshot` | Framebuffer as base64 PBM between markers |
| `panel status` · `panel test` · `panel clear` · `panel mode <hpm\|lpm>` · `panel rate <Hz>` · `panel fps [s]` · `panel init <factory\|xiaozhi>` | Panel diagnostics: test pattern, power mode, LPM rate, measured frame rate, init sequence |
| `btn <key\|boot> <short\|double\|long>` | Inject button gestures |
| `sensors` · `battery` | Readings |
| `rtc get` · `rtc set <ISO 8601>` | RTC |
| `field list` · `field get <id>` · `field set <id> <value>` | Inspect and inject data, e.g. fixtures on the device |
| `preset list` · `preset set <id>` | Presets |
| `wifi status` · `wifi scan` | Wi-Fi |
| `sync now` | Run a sync |
| `sleep stats` · `power idle <deep\|light>` | Power debugging |
| `audio tone <Hz> <ms>` | Audio check (M7) |

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
| `tools/render.py` | Run the host renderer on presets and fixtures, writing PNGs | Host build |
| `tools/fontgen.py` · `tools/imggen.py` | Turn fonts and icons into C sources | Pillow (tools venv) |

pyserial comes from the ESP-IDF Python environment. Pillow lives in a separate tools venv (`tools/requirements.txt`).

## 16. Error handling and robustness

- **Crashes.** The task watchdog is on. A panic writes a core dump to flash. The next boot reports the dump in the log and makes it available through the console and the web UI, then erases it.
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
  - Weather parser: against fixtures.
  - Preset and settings JSON: validation and migrations.
  - MQTT payload builders: golden JSON.
  - locale formatting.
- **Golden renders.** Each built-in preset, the menu and each special screen are rendered with fixture data at fixed times. Each render is compared with `test/host/golden/*.pbm`. After the owner reviews the PNGs, `--update` rewrites the goldens.
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
| M3 | `datastore`, `locale` (en), 4 layouts, widgets, presets JSON and defaults, cycling, status bar, menu v1, special screens | Owner reviews golden renders (2); presets switch with KEY/`btn` and survive a reboot (3) |
| M4 | `netmgr` (STA/AP, captive portal, mDNS), config screen with QR, `webui` and REST API, preset editor with preview, set time from phone, OTA with rollback | Owner sets up Wi-Fi from a phone in AP mode (4); the preview matches a device screenshot (3); OTA upload and rollback work (3) |
| M5 | SNTP → RTC, `weather`, `astro`, `sync` with the configurable schedule and backoff, weather widgets, power tuning | A sync on battery reports its results in Info (3); astro tests pass (2); sync energy and the daily average are measured and `docs/power.md` is updated (4) |
| M6 | `ha_mqtt`: session, discovery, state, preset command, MQTT field mappings, `always` sync mode | Entities appear in HA; the preset select works at the next sync; a mapped HA value renders (3/4) |
| M7 | `audio`: codec path, offline alarms (also from deep sleep), tones and WAV, radio (MP3/AAC, ICY) | An alarm fires from idle, and snooze and stop work (3/4); a radio stream plays (4) |
| M8 | microSD features agreed at the start of M8 | Per the agreed list |

## 19. Deferred proposals (discuss at the milestone)

| Milestone | Proposal |
|---|---|
| M0 | GitHub Actions CI (firmware build and host tests) |
| M1 | ~~Portrait orientation~~ (declined 2026-09-25, D13) |
| M3 | The LPM refresh rate as a display setting (the driver supports 0.25–8 Hz, D12); preset time-of-day schedule; Night layout; extra fields (dew point, today's min/max, trends, week number, change in day length, moon phase, estimated battery days left); Czech language pack with name days and CZ public holidays |
| M4 | Web UI admin PIN; web UI translations |
| M5 | Quiet hours; air quality and pollen (Open-Meteo); RTC offset calibration; static IP |
| M6 | MQTT over TLS; HA buttons (sync now, next preset) and device triggers for key presses; HA message entity; HA REST pull as an alternative source |
| M7 | Radio sleep timer; ESP-SR (echo cancellation, noise suppression, wake word) |
| M8 | Config backup/provisioning file; sensor history CSV with graphs; sounds and station lists; 1-bit images; firmware file; logs and screenshots |
| Later | IDS JMK departures; a remote 1-bit image slot; the VBUS-sense hardware mod; external I²C sensors on the header; BLE or ESP-NOW sources |

## 20. Risks and open items

| Risk / item | Mitigation |
|---|---|
| The panel keeping its image through deep sleep is unverified | Test at M1/M2; fall back to light sleep |
| Waking through BOOT (GPIO0, a strapping pin) might enter download mode | Verify at M2. If it does, wake only on KEY and the RTC |
| Real currents are unknown (panel LPM, board quiescent, PSRAM in light sleep) | Measure at M2 before building on either strategy |
| USB meters are inaccurate below 1 mA | Long mAh windows; note the limits in `docs/power.md` |
| SHTC3 self-heating | Offset calibration; sample right after wake |
| `esp_audio_codec` is distributed as prebuilt binaries, which may not suit an open-source repo | Check at M7; pick another decoder if needed |
| Open-Meteo's free tier is for non-commercial use | Low request rate (daily sync); the provider can be swapped |
| No RTC backup cell (D9): the time is lost at every PWR-off | Sync at boot when Wi-Fi is configured, otherwise a "Set time" prompt; the owner may fit an ML1220 (§7) |
| Homebrew Python 3.14 on this Mac (3.14.6 and 3.14.7 checked) can't load `pyexpat` (it expects a newer libexpat than macOS 26.2 has), which breaks pip and the ESP-IDF installer | ESP-IDF uses uv's Python 3.13 through `~/esp/python-shim` (`AGENTS.md` §6) |

## 21. Revision history

| Rev | Date | Changes |
|---|---|---|
| r1 | 2026-09-25 | First draft |
| r2 | 2026-09-25 | Configurable sync schedule (`times` / `interval` / `always` / `manual`) and display update interval (D11, §9.2, §9.3); terms defined (§1.4); no RTC cell fitted, so a boot with invalid time syncs at once (§3.3, §7); ST7305 partial-update findings: RAM windows are supported but don't lower panel power, so v1 pushes full frames (§4.2) |
| r3 | 2026-09-25 | Approved by the owner; licence confirmed (D8); Python toolchain risk recorded (§20) |
| r4 | 2026-09-25 | M1 panel check: factory init sequence with a separate LPM rate, default 1 Hz, configurable at runtime (D12, §4.2); a 120 ms wait after a panel reset (datasheet §12.1.4); HPM/LPM switching delays from datasheet §7.11; `panel rate` and `panel fps` (§15); landscape only (D13); `display` and `util` components (§3.1) |
