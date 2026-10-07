# AGENTS.md — reflbo

Guide for coding agents (and humans) working in this repository. Read it fully before changing anything, and keep it current (§8).

> **Scope.** This is a standalone embedded firmware project. Instructions inherited from parent directories about iOS, WeConnect-iOS, the CAT monorepo, Jira ticket keys or PR templates do not apply here.

**Quick rules**

1. Read §3.4 (hardware gotchas) before touching power, sleep, pins or the display.
2. Verify at the right level (§7). Prefer screenshots and logs you capture yourself over asking the owner to look.
3. Never erase anything on the board (flash, NVS, the storage partition), flash a port you haven't confirmed is this board, or commit secrets without asking. Back up the board's configuration before a test that changes it and restore it after; after every MQTT/HA test, flash the stable firmware back (§6, spec §12.10).
4. Items marked *(planned)* do not exist yet. Never describe them as done.
5. Build only what the design spec covers. Anything extra is a proposal to raise with the owner at the relevant milestone (§2).
6. Update this file in the same commit as any new command, component, decision or gotcha.

---

## 1. What we are building

**reflbo** is firmware for the Waveshare **ESP32-S3-RLCD-4.2** board. The device is a battery-powered, always-visible desk display (4.2″ reflective LCD, 400×300, 1-bit) that shows configurable, watch-face-like dashboards: time and date, indoor climate, weather, sun times, Home Assistant values and data from other local devices, a solar forecast and the house's energy. It also does alarms and internet radio.

Guiding principles:

1. **Standalone first.** Works with no network: RTC time, local sensors and computed sun times. Wi-Fi, weather and Home Assistant only add to that.
2. **Battery first.** The radio is off by default. A *sync* is a Wi-Fi session for time, weather and MQTT; it runs on a configurable schedule (default once a day), and Wi-Fi also turns on when the owner asks. A *display update* redraws from local data, every minute by default (configurable), with no Wi-Fi. Every feature states its power cost.
3. **Universal, with Brno defaults.** Location, time zone, language and units are all configurable. Defaults: Brno, CZ (49.1951 N, 16.6068 E), `Europe/Prague` (`CET-1CEST,M3.5.0,M10.5.0/3`), metric, `cz.pool.ntp.org`. The UI is in English, organised as language packs so more languages can be added.
4. **Agent-verifiable.** Anything that renders can be screenshotted over USB and rendered on the host. Changes can be checked without the owner's eyes.
5. **Small, testable modules.** Pure logic (layout, formatting, astro, parsing, scheduling) builds and runs tests on the host.

## 2. Status and roadmap

- **Status:** M0 and M1 are done. M0: toolchain, skeleton, USB console, host tests and `devlog.py`. M1: ST7305 driver, `gfx` with fonts, the `display` service, screenshots over USB, host rendering with a golden test pattern; the owner checked the physical panel. M2 is done: board services, the clock screen and both idle strategies; the owner's measurements picked light sleep (D3). M3 runs as two plans. M3a is done: LittleFS config files, the datastore with the extra fields, the English pack, four layouts with widgets and a status bar, and presets that KEY switches and auto-cycles and that survive a reboot. M3b is done: the on-device menu with settings editing, toasts and the critical-battery screen, the preset schedule with timed night sleep, and the Czech pack with its public holidays. On 2026-09-30 the owner checked preset switching, a held KEY and BOOT, the menu buttons and timeout, and the Czech panel. Two owner checks wait for a later session at the owner's request: the night-sleep current (D15) and the night peek, which needs a night started from the console (`night <minutes>`) or from a schedule, which the web UI now edits. M4 is built: the Wi-Fi manager with the device's own network and captive portal, config mode with its QR screen, the web configurator with its API and a live preset preview, setting the time from a phone, and firmware updates with rollback. M4 is done once the owner has set up Wi-Fi from a phone and updated the firmware from the page (Owner acceptance in the M4 plan). The M4 review's fixes are in (spec r16). Owner acceptance, 2026-09-30: Wi-Fi from a phone works; the page's feedback is handled (D20, spec r17); the update from the page and the first run wait for a later session at the owner's request. M5 is built (plan below): our own SNTP client sets the RTC to the millisecond and trims it (D25); Open-Meteo brings the forecast, air quality, pollen and UV; sunrise and sunset are computed on the device; syncs follow the schedule with quiet hours and retries, or keep Wi-Fi up in sync mode `always` with the web UI on the LAN; the forecast widgets draw Weather Icons (D26); the menu has a Sync section, the console `sync now|status`, the web UI a Sync page and a place search. Its board checks (the plan's Task 15) passed on 2026-10-01, after the fixes they and the final review found (spec r27). The owner's acceptance (M5 plan) waits for a later session at the owner's request (2026-10-01): a sync on battery, the new widgets on the panel, the RTC trim over three daily syncs (2026-10-02 to 10-04, with no Sync now, manual set or power-off between them), the power measurements, sync mode `always` on the LAN, and the Wi-Fi mark with the router off. The web password was reset during the checks, so the owner chooses it again over the device's own network. M6 is built (spec r31): the map with GeoNames' towns (D29), the weather radar with its loop, the flight radar with routes, rain in the next 2 hours and the Radar page; a review of the whole run and its fixes are in, and its board checks passed on 2026-10-02 (the plan's Task 15). The owner's acceptance (Owner acceptance in the M6 plan) waits: the goldens, the panel's legibility, the loop and the aircraft at the board, and the power measurements. The board checks restarted M5's RTC-trim count. M6b is built (spec r33, plan below): the split layout with its web editor, and BOOT double for sync mode `always`; a review of the whole run and its fixes are in (numbers fit their slot's height, the weather widgets fit frost and °F heat), and its board checks passed on 2026-10-02 (the plan's Task 6). The owner's acceptance (Owner acceptance in the M6b plan) waits: the goldens, a split preset built in the editor on the panel, BOOT double with the buttons. The home network is now "Kremikove_nebe" (2026-10-02; "TriDva" forgotten). M7 is designed (D32, spec r34): MQTT and Home Assistant, with HA buttons, key-press triggers and a message; its plan is written (below) and waits for the owner's review. There is no broker or HA yet, so its checks with HA wait for the owner's setup, and a stable build is flashed back after every MQTT/HA test (§6; `stable-m6b` then, `stable-m6d` from 2026-10-06). On 2026-10-04 the owner put M7 on hold (D33): M6c, split cells down to 40×20 (D34), and M6d, a solar forecast and the house's energy from SolaX Cloud, with switches for the sync's steps (D35, D36), come first. Both are designed (spec r35); the owner approved their designs from a throwaway spike's panel renders (local branch `spike/m6cd`). M6c is built (spec r36, plan below): split cells down to 40×20 and up to 24, the XS size and S in short cells; a review of the whole run and its fixes are in, and its board checks passed on 2026-10-04 (the plan's Task 5). The owner's acceptance (Owner acceptance in the M6c plan) waits: the goldens, a small-cell preset built in the editor on the phone, XS cells read at the panel. The checks reset the web password, so the owner chooses theirs again over the device's own network. M6d is built (spec r38, plan below): the PV forecast from Open-Meteo through our model, Forecast.Solar or Solcast, with up to 2 roof planes; the house's energy from SolaX Cloud, by its Developer API (D37) or a Token ID; the `pv.*` and `energy.*` fields with the chart and the flow, the Solar and Energy layouts and presets; switches for the sync's data steps (Sync ▸ Steps and the Sync page); the Solar page with Check now; `solar.bin`; `solar status|demo`, `energy raw`. A review of the whole run and its fixes are in, and its board checks passed on 2026-10-05 (the plan's Task 12, Steps 0–11). There the owner's SolaX account turned out to have no Token ID, so the Developer API joined as a second source (D37), with today's totals from SolaX's statistics; the owner's own keys gave a reading the same day. A fresh review of D37 found no Critical issue and two Important ones, both fixed (spec r39): a retry only on SolaX's refusals, and a reply whose `dataTime` can't be read refused. The owner saw the layouts on the panel (2026-10-06) and asked for watts under 1 kW (D38). The owner's checks on 2026-10-06: the panel and the owner's roof look good; the SolaX reading matches the app since the house's use takes the inverter's phases and solar the app's figure (D39). The power measurement and Solcast with the owner's key wait for the project's end (D39); this build is `stable-m6d` (§6). M7's plan is refreshed against M6c and M6d, with D40's additions (plan below); it waits for the owner's broker and HA, and runs Native.
- **Design spec:** [`docs/specs/2026-09-25-firmware-design.md`](docs/specs/2026-09-25-firmware-design.md) is the authoritative design. The owner approved it on 2026-09-25. §5 below summarises it. If the two disagree, the spec wins; fix this file.
- **User guide:** [`README.md`](README.md) gives the overview and [`docs/guide.md`](docs/guide.md) describes every feature for users, with images in `docs/images/`: the panel's from the goldens (`tools/docs_images.py`), the web pages' as phone screenshots against a demo device. Planned features are marked as planned there too.
- **Plans:** each milestone gets its own implementation plan in `docs/plans/`, written just before that milestone starts. Latest plan: [`docs/plans/2026-10-06-m7-mqtt-home-assistant.md`](docs/plans/2026-10-06-m7-mqtt-home-assistant.md), M7's refresh (D40). Its code was built on the branch `plan/m7r`, one commit per task, from which the plan's tasks copy their goldens (M6d's: `plan/m6d`, M6c's: `plan/m6c`, M6b's: `plan/m6b`, M6's: `plan/m6`; M7's first plan and `plan/m7` gave way to the refresh).
- **Extra features:** anything beyond the requirements (spec §1.1) is a proposal. Raise it at the relevant milestone (spec §19) and build it only after the owner agrees. Accepted for M6 (owner, 2026-10-01): the ADS-B flight radar (spec §19.1, D22) and the weather radar (spec §19.2, D23), with sync mode `always` moved into M5 (D24). Accepted for M5 (D25): quiet hours, air quality and pollen, and the RTC trim.
- **Repository:** the owner is in Brno, CZ. Remote `origin` is `git@github.com:Vybo/reflbo.git`.

| # | Milestone | Done when |
|---|---|---|
| M0 | Toolchain and skeleton: ESP-IDF, project builds and flashes, USB console, `tools/` helpers, licence files | `idf.py build` is clean and the console answers |
| M1 | ST7305 driver, `gfx`, fonts, screenshot path (serial → PNG), host renderer | The test pattern on the panel (owner confirms orientation) matches the screenshot |
| M2 | Board services: PCF85063, SHTC3, battery gauge, buttons; clock screen; both idle strategies | Values on screen match the console. The owner measures deep vs light sleep, and one idle strategy is chosen |
| M3 | Data store, layouts, presets, cycling, status bar, on-device menu, schedule and night sleep, Czech pack | KEY switches presets, and the choice survives a reboot. The owner measures night sleep |
| M4 | Wi-Fi manager (STA/AP, captive portal), web configurator, mDNS, OTA | A phone sets up Wi-Fi from AP mode; OTA works |
| M5 | Time sync with the RTC trim, weather with air quality and pollen, astro, the sync scheduler with quiet hours and sync mode `always` (D24, D25), power tuning | Daily sync works on battery; the RTC drifts under 1 s a day; the measured average current is in `docs/power.md` |
| M6 | Radar views (D22–D24, D27, D28): a shared map, the weather radar (ČHMÚ, RainViewer outside its coverage) as a layout and a slot widget with the last hour's loop, and the ADS-B flight radar (adsb.fi) as its own preset with the nearest aircraft's route; rain in the next 2 hours | Both radars render on the panel: rain after a sync, aircraft in sync mode `always` |
| M6b | The split layout and BOOT double for sync mode `always` (D31) | A split preset built in the web editor shows on the panel; BOOT double turns `always` on and back |
| M6c | Small split cells (D34): down to 40×20 and up to 24 cells, the XS size, S in short cells | A 24-cell preset built in the web editor shows on the panel; the owner checks the XS cells' legibility |
| M6d | Solar (D35, D36): the PV forecast (Open-Meteo with our model, Forecast.Solar, Solcast), the house's energy from SolaX Cloud, the Solar and Energy layouts and presets, switches for the sync's steps | Forecasts render after a sync; a SolaX reading matches the SolaX app; a switched-off step makes no request; a sync's energy is measured |
| M7 | Next; its plan is written (D40) and waits for the owner's broker and HA. MQTT and Home Assistant (D32): state, discovery, the preset select, HA buttons, key-press triggers, a message, data from HA and other local devices over MQTT (numbers, labelled states, times), and the house's energy from MQTT | Entities appear in HA; the select and buttons work at the next sync; a mapped MQTT value and a message render on the device (with the owner's HA, once set up) |
| M8 | Audio: offline alarms, then internet radio | An alarm fires from idle; a radio stream plays |
| M9 | microSD features (list agreed at the start of M9) | The agreed features are verified |

## 3. Hardware reference: Waveshare ESP32-S3-RLCD-4.2

Sources: [wiki](https://docs.waveshare.com/ESP32-S3-RLCD-4.2) · [schematic](https://files.waveshare.com/wiki/ESP32-S3-RLCD-4.2/ESP32-S3-RLCD-4.2-schematic.pdf) · [vendor examples](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2) (the most complete is `02_Example/ESP-IDF/10_FactoryProgram`, built on ESP-IDF 5.5.x). The pins below were cross-checked against the schematic and the vendor code on 2026-09-25.

### 3.1 Chips

| Part | Role | Bus / address |
|---|---|---|
| ESP32-S3-WROOM-1-N16R8 | 2× Xtensa LX7 up to 240 MHz, 16 MB flash (QIO), 8 MB octal PSRAM, Wi-Fi 2.4 GHz, BLE 5 | — |
| ST7305 | Reflective LCD controller, 400×300, 1 bpp. Write-only: no MISO wired | SPI (vendor uses `SPI3_HOST`) |
| PCF85063ATL | RTC with alarm, timer and minute interrupt. Own backup cell connector | I²C `0x51` |
| SHTC3 | Temperature and humidity | I²C `0x70` |
| ES8311 | Audio codec. Its DAC drives the speaker amp; its ADC is unused | I²C `0x18` + I²S |
| ES7210 | 4-channel ADC: 2 mics plus the speaker output looped back for echo cancellation | I²C `0x40` + I²S |
| NS4150B | Class-D speaker amp, 2-pin speaker header | enable = GPIO46 |
| ETA6098 | Li-ion charger for the 18650. STAT only drives the CHG LED | — |
| TPS63020 / RT9193-33 | 3V3 buck-boost for the system / 3V3 LDO for audio analog (always on) | — |
| U3 power-latch IC + P-MOSFET | PWR push button: short press = on, long press = off | hardware only |

### 3.2 GPIO map

| GPIO | Function | Notes |
|---|---|---|
| 0 | BOOT button, active low, external 10k pull-up | Strapping pin: held low at reset → download mode. RTC GPIO, can wake the chip |
| 4 | BAT_ADC, ADC1_CH3 = VBAT × 1/3 (200k/100k) | The divider always draws about 14 µA |
| 5 | LCD D/C | |
| 6 | LCD TE (tearing-effect output) | Optional |
| 8 | I²S DOUT → ES8311 | |
| 9 | I²S BCLK | |
| 10 | I²S DIN ← ES7210 | |
| 11 / 12 | LCD SCK / MOSI | |
| 13 / 14 | I²C SDA / SCL, external 2.2k pull-ups | Shared by RTC, SHTC3 and both codecs. Also on the header |
| 15 | RTC_INT (PCF85063 INT, open-drain, active low) | **No external pull-up**: enable the RTC-domain pull-up. RTC GPIO, can wake the chip |
| 16 | I²S MCLK | |
| 18 | KEY button, active low, external 10k pull-up | RTC GPIO, can wake the chip. Also on the header |
| 19 / 20 | USB D− / D+ (USB-Serial-JTAG: console and flashing) | Also on the header |
| 21 / 38 / 39 | SD CMD / CLK / D0 (SDMMC 1-bit) | D3 is pulled up. There is no card-detect line |
| 40 | LCD CS | Digital-only pad (see gotcha 4) |
| 41 | LCD RESET | Digital-only pad. Must stay high in deep sleep or the panel resets |
| 45 | I²S LRCK/WS | Strapping pin |
| 46 | PA_CTRL (amp enable, external 10k pull-down) | Strapping pin. Keep low while silent |
| 1, 2, 3, 17 | Free, on the header only | 1–3 are ADC1_CH0–2. 3 is a strapping pin (JTAG select) |
| 43 / 44 | UART0 TX / RX, header only | Logs that survive deep sleep, via a USB-UART adapter |
| 7, 42, 47, 48 | Not routed | Cannot be used without rework |
| 35–37 | Taken by octal PSRAM | Never use |

### 3.3 2×8 expansion header (P1, 2.54 mm)

Numbering follows the schematic. Check the silkscreen before wiring.

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | 3V3 | 2 | VBUS (5 V only while USB is connected) |
| 3 | GND | 4 | GND |
| 5 | GPIO0 (BOOT) | 6 | USB D− (GPIO19) |
| 7 | GPIO1 | 8 | USB D+ (GPIO20) |
| 9 | GPIO2 | 10 | U0TXD (GPIO43) |
| 11 | GPIO3 | 12 | U0RXD (GPIO44) |
| 13 | GPIO17 | 14 | I²C SDA (GPIO13) |
| 15 | GPIO18 (KEY) | 16 | I²C SCL (GPIO14) |

### 3.4 Hardware gotchas

1. **The PWR button cannot be read.** It toggles a hardware latch that cuts all power except the RTC backup cell. Firmware can neither see presses nor switch itself off; it can only sleep. The only buttons firmware can use are **KEY (GPIO18)** and **BOOT (GPIO0)**.
2. The first power-up with a freshly inserted 18650 needs USB connected, to release the battery protection. After that the battery runs the board.
3. **Charging and USB presence are not wired to any GPIO.** Infer them from the VBAT trend, or from `usb_serial_jtag_is_connected()`, which only works with a PC host. Optional mod: VBUS (header pin 2) → 100k/100k divider → GPIO1, 2 or 3.
4. **Keeping the image through deep sleep.** The ST7305 keeps showing its image while powered. RESET (GPIO41) and CS (GPIO40) are digital-only pads that lose state in deep sleep, so hold them with `gpio_hold_en()` plus `gpio_deep_sleep_hold_en()`. On wake from deep sleep, skip the panel reset and init: re-attach SPI and push the frame. The hold latches at once, so `st7305_prepare_deep_sleep()` is the last panel access before `esp_deep_sleep_start()`. On wake, `st7305_init_warm()` drives the same levels, configures the pins, then calls `gpio_hold_dis()`. Checked at M2: the owner watched six deep-sleep cycles and the image stayed, updating every minute; afterwards `panel fps` still measured 1.00 Hz. Between updates use LPM (`0x39`, 0.25–8 Hz). Use HPM (`0x38`, 16–51 Hz) only for fast interaction. A community driver reports about 10 µA in sleep-in (image hidden), about 1 mA in LPM and about 5 mA in HPM. Measure these.
5. **Framebuffer format.** In landscape the panel packs 2×4-pixel blocks into each byte (vendor `InitLandscapeLUT`), and in the panel buffer bit 1 means white. Our canonical buffer is row-major 1 bpp, MSB first, 1 = black (the PBM P4 layout). Convert it when flushing (spec §4.1).
6. The two vendor init sequences differ: the factory firmware runs SPI at 10 MHz, XiaoZhi at 40 MHz, and they use different source voltages (contrast: VSHP/VSHN 0x41 vs 0x69/0x4B), oscillator settings (HPM 16 vs 25.5 Hz) and LPM frame rates (8 vs 1 Hz). **Chosen at M1 (owner check 2026-09-25): the factory sequence**, which has visibly better contrast. The driver replaces its LPM rate with its own setting (default 1 Hz, `panel rate`), because at 1 Hz the owner saw the same contrast and no flicker. The XiaoZhi sequence stays available as `panel init xiaozhi`.
   - The LPM rate (FRCTRL `B2h`) can be changed at any time and applies at once, even in LPM. `panel fps` measures the real frame rate by counting TE pulses on GPIO6. M1 measured 1.00 Hz in LPM and about 16–17 Hz in HPM with the factory sequence.
   - Switching between HPM and LPM follows datasheet §7.11 (figure on page 53): about 120 ms into LPM and 320 ms into HPM, because of mandatory delays.
7. **Use the PCF85063 for timing.** The ESP32 has no 32 kHz crystal, so its sleep timer drifts; the PCF85063 is the time source. Wake scheduling uses its alarm: the alarm flag (AF) latches and INT stays low until firmware clears it, which ext1 catches reliably. Don't use the minute interrupt. Depending on TI_TP it is a 1/64 s pulse or a level held until TF is cleared (datasheet §8.2.2.3, unverified here), and it shares INT with the alarm. **CLKOUT is on after power-up, and CLKOE (pin 3) is not connected** on the schematic, so firmware writes COF = 111 at every boot. The board puts 22 pF on each crystal pin, above the usual load rating, so the RTC runs slow: about 3.4 s/day at M2 (9 s in 2.6 days, against an estimate of 2–9 s/day). Trim the Offset register against NTP at M5. The vendor firmware overwrote the time on every boot and never checked the oscillator-stop flag. A 5 s PWR-off can set that flag while keeping the time (seen at M2: VDD sags through D2 and C11); `rtc set` clears it.
8. **The audio analog rail is always on** (RT9193). Put the ES8311 and ES7210 into standby over I²C and keep PA_CTRL low when nothing is playing. The vendor firmware never puts the codecs into standby and keeps the amp enabled; `board_init()` writes the esp_codec_dev standby sequences at every cold boot (they keep that state through deep sleep).
9. **The microSD slot is always powered and has no card detect.** An inserted card adds idle current. Mount it on demand and detect a card by probing.
10. The SHTC3 reads high because the board heats it; vendor code subtracts a constant 4 °C. On this board it reads about 2 °C high at all times (owner, 2026-09-30), so the offset defaults to −2.0 °C (D19) and the user trims the rest. Sample right after wake, before Wi-Fi and the CPU warm the board. The SHTC3 idles at 45 µA unless sent to sleep, which the vendor code never did; `sensors.c` sleeps it after every read.
11. **The USB console works only while the chip is awake.** Deep sleep powers the USB PHY off, so the host sees a detach. Light sleep disables the USB pad; on the owner's Mac the port stays present, but nothing gets through until the chip wakes. So a *tethered* board, one with a USB host sending frames, never sleeps. After a cold boot or a button wake the board stays awake 2 s so a PC can find it. To catch a deep-sleeping board, press KEY; if that fails, ask the owner to enter download mode (hold BOOT while powering on). `sleep test <deep|light> <n>` runs sleep cycles while tethered, for testing. Routine deep-sleep wakes (the RTC alarm or its backup timer) don't start the console at all, nor load NVS or print info logs; the app does all three once it decides to stay awake.
12. The vendor factory firmware draws about 90 mA at 5.3 V, roughly 24 h on a battery. That is the baseline to beat by a wide margin.
13. **RTC backup cell: none is fitted now; one can be added later.** It plugs into a small 2-pin 1.0 mm connector (J7 in the schematic), not a coin holder, and must be a rechargeable cell with leads (ML1220), because the board charges it. Per the schematic, J7 pin 1 is + and pin 2 is GND; check the polarity before plugging a cell in. Without a cell, the time is lost at PWR-off and the RTC's oscillator-stop flag reports it. The firmware then syncs at boot if Wi-Fi is configured, and otherwise asks for the time to be set (spec §7).
14. **Partial updates exist but don't save panel power.** CASET/RASET/RAMWR can write a RAM window in 12×2 px cells (landscape *y* × *x*), and RAM may be written in LPM; new content shows at the next panel frame. The panel still re-drives every line each frame, so a partial write saves only SPI time (about 0.03–0.1 mAh/day). v1 pushes full frames and skips the push when nothing changed (spec §4.2).
15. **Opening the USB port can reset the chip.** The OS asserts DTR and RTS when the port opens, and releasing DTR while RTS is still asserted resets a USB-Serial-JTAG chip. pyserial's default order does exactly that. Open ports through `devlog.open_port()`: it asserts both lines before `open()`, then releases RTS before DTR, the order esp-idf-monitor uses for `--no-reset`.
16. **KEY (GPIO18) has no debounce capacitor** (BOOT has 100 nF). Buttons are debounced in software (30 ms, spec §5.6).
17. **Battery drain the firmware cannot remove** (schematic estimate): the BAT_ADC divider, 10–14 µA, and the ideal-diode bias (Q6 into R16), about 30–36 µA, both straight from VBAT, even with PWR off.
18. **ESP-IDF timeouts in ms become ticks rounded down.** At the 100 Hz tick, an I²C timeout below 10 ms is 0 ticks and fails at once; use 20 ms or more (the drivers use 50). Datasheet minimum delays go through `util_ticks_at_least()`.
19. **Light sleep floats every pin** unless told otherwise (`ESP_SLEEP_GPIO_RESET_WORKAROUND`): panel CS and RESET call `gpio_sleep_sel_dis()` so they keep driving; the three wake pins keep theirs through `gpio_wakeup_enable()`, which also switches them to level interrupts until `power_sleep_light()` restores their edge type.
20. `usb_serial_jtag_is_connected()` watches for SOF frames on every tick while awake. It starts true and turns false after one 10 ms tick without an SOF. A PC needs roughly 0.1–1 s to enumerate the board again after a boot or wake. Log lines printed right after a light-sleep wake can be lost the same way. While it reads false, console reads and writes return -1 at once, so the REPL retries every 10 ms. Before the first tick, a write to a full USB FIFO with no host reading waits up to 50 ms (`TX_FLUSH_TIMEOUT_US`).
21. **GPIO15 (RTC INT) needs the RTC-domain pull-up in deep sleep** (`rtc_gpio_pullup_en`); `gpio_pullup_en` does not apply there. Clear the alarm flag before sleeping, or ext1 wakes at once.
22. **Download mode sticks until a power-on or watchdog reset.** Powering on with BOOT held latches download mode in the strapping register (`boot:0x21`). Resets over USB don't sample the pins again: esptool's hard reset after `flash`, and `devlog --reset`, land in "waiting for download" every time, so a freshly flashed app never runs. Leave with a watchdog reset, which samples them again: `tools/idf.sh exec python -m esptool --chip esp32s3 -p <port> --after watchdog_reset read_mac`. Seen at M2, 2026-09-28.
23. **The 3V3 buck-boost runs in forced PWM.** The TPS63020's PS/SYNC pin (13) is tied to EN and VINA, so it is high, and "logic high forces PWM mode" (datasheet §7.4.4). TI's Figure 9 (power save disabled) shows about 1–2 % efficiency at 0.1 mA and about 10 % at 1 mA, so the converter burns tens of mW at almost no load. It does so in every mode and on battery too. At M2 the board drew a steady ~11.5 mA at 5.2 V (~60 mW) in deep sleep while the chip was awake 0.1 % of the time. Firmware can't change it; only a board rework can (PS/SYNC to GND enables power-save mode, 25–50 µA quiescent). The DSJ package is a 3×4 mm VSON with the pins underneath.
24. **Sleep-out needs the init sequence again.** Both vendor init sequences set NRDSLP (`D6h`, second parameter `0x02`), which makes `SLPOUT` reload the NVM defaults: at M3b a plain SLPOUT brought the panel back at 2 Hz. So `st7305_sleep_out()` resets the panel and runs the init sequence, and `display_wake()` then pushes the frame and returns to LPM. `panel fps` reads high for a few seconds while the panel settles, then 1.00 Hz. Sleep-in from LPM goes through HPM first (datasheet §7.10), about 400 ms in all. Night sleep uses both (spec §9.1); `panel sleep` and `panel wake` try them by hand.
25. **lwIP has 10 sockets by default: too few for a browser.** The web server takes 7 plus 3 of its own, the captive DNS 1, and a browser opens several connections at once. At M4 `accept()` then failed with errno 23 (ENFILE) and Chrome showed an empty response. `CONFIG_LWIP_MAX_SOCKETS=16`.
26. **The AP shares the radio with the station, and their channel.** Joining a network on another channel moves the AP there and drops the phones on it; calling `esp_wifi_set_config(WIFI_IF_AP, …)` on a running AP drops them too (seen at M4). So netmgr starts the AP on the channel of the network the owner will likely pick (spec §10.1), leaves a running AP alone, and scans before a test, so a network out of reach never moves it.
27. **A phone on the AP keeps probing for the internet** (`captive.apple.com`, `connectivitycheck.gstatic.com`), and the captive DNS sends those probes to the device. Counting them as use kept config mode on for good; only the device's own pages and API count.
28. **A firmware image uploaded over OTA stays pending until it marks itself valid**, and any reset while it is pending rolls it back. A deep-sleep wake is a reset, so the board doesn't deep-sleep until the image is valid (spec §10.5). A pending image that fails to start rolls back at once (`POWER_PLAN_ROLLBACK`): it holds the board awake itself, so the reset would never come.
29. **RTC RAM addresses move between images.** `.rtc_noinit` follows `.rtc.data` and `.rtc.bss`, so an image with 32 more bytes of RTC data read the old image's resume flag 32 bytes off (0x500007b0 against 0x500007d0, seen at M4) and lost it. RTC RAM carries state within one image, through deep sleep; whatever must survive an update or a rollback goes in NVS, such as `sys/resume_cfg`.
30. **A stack overflow corrupts memory without a clean crash.** No end-of-stack watchpoint is set, and FreeRTOS checks a task's stack only at a context switch. At M4 a JSON body nested 1000 levels deep sent cJSON's recursion past the web server's 8 KB stack, and the next `xSemaphoreTake` asserted on the corrupted auth mutex (`queue.c:1713`). Check `util_json_depth()` before parsing anything from outside.
31. **A routine deep-sleep wake has no NVS** (gotcha 11). Code that reads NVS on such a wake brings it up first, as `app_net_init()` does for netmgr's networks and AP password, or loads later, as the RTC trim does. Syncs are planned at a cold boot, after each sync and when the clock, the settings or the saved networks change; a deep-sleeping board carries the next one in its snapshot.
32. **`netmgr_status()` before `netmgr_init()` crashes**: its mutex doesn't exist yet. Code that can run first asks `app_net_ready()`.
33. **GCC sees `snprintf` truncation that clang doesn't** (`-Wformat-truncation`, an error under `-Werror`). The host tests build with this Mac's clang, so a buffer that passes there can fail the firmware build: size it for the widest value its format can print.
34. **Stopping Wi-Fi inside a web request loses its reply.** A settings change runs on the app task while the server waits for it; Wi-Fi turned off there takes the reply with it, and the page reports an error for a change that worked (M5, leaving sync mode `always` from the LAN). Wi-Fi that nothing needs goes off from the app loop, 3 s after the last request (`app_sync_wifi_check()`).
35. **The linker leaves an embedded blob out until code reads it.** `assets/map/map.bin` (859 KB) joined the image only when `app_radar.c` first referenced `_binary_map_bin_start` (M6 Task 11); the image grew by the map's size then, not when the component embedded it.
36. **`util_snapshot` blocks are at most 64 KB** (a `uint16_t` size). The radar's frame file (56–147 KB) has its own header and CRC (`RADAR_FILE_*`).
37. **`cJSON_ArrayForEach(element, array)` takes a plain pointer.** It expands `array` inside a ternary without parentheses, so an expression there (`a ? b : c`) parses wrongly; put it in a local first.
38. **TLS allocates in PSRAM** (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`), so a sync, a radar refresh and the flight radar's kept session fit together: the lowest internal heap with all three was 74 KB at M6. Handshakes are slower, about 0.6–0.7 s each.
39. **ČHMÚ closes every connection** (`Connection: close`). A GET on a kept socket the server has closed fails in `esp_http_client_fetch_headers()`, which leaves the status at -1. `fetch` closes connections the server won't keep and retries once, on a new one, a kept connection that brought no reply.
40. **Each Open-Meteo `minutely_15` amount is the 15 minutes before its time**, so the quarter hour now is the entry that ends at the next quarter-hour mark (`ds_rain_index()`).
41. **esp_http_client's 512 B transmit buffer:** when the request line leaves no room for the first header, nothing is sent and the GET times out silently. The forecast's request line is 451–454 B (M6); `fetch` sets a 2 KB buffer since D37, which a bearer token's header needs too.
42. **Web sessions end when the web server stops**, as in quiet hours in sync mode `always`: log in again. In a check script, read the session from curl's cookie jar by hand: curl writes HttpOnly cookies as `#HttpOnly_<host>` lines, which Python's `http.cookiejar` skips as comments.
43. **This Mac may not find the board's AP while its Wi-Fi is on another network** (`Could not find network reflbo-XXXX`, M6b): power Wi-Fi off and on (`networksetup -setairportpower en0 off`, then `on`), then join. In config mode with a saved network in reach, the board joins that network too and the AP follows its channel (gotcha 26), so the web UI is also on the LAN, at the address `wifi status` prints.
44. **`version`'s build date can be stale.** It is the app description's `__DATE__`/`__TIME__`, which ESP-IDF compiles once per clean build: at M6d both the old image and the new said "built Oct 2 10:14". Only the ELF hash in the same line tells builds apart.

Datasheets: [ST7305](https://files.waveshare.com/wiki/common/ST_7305_V0_2.pdf) · [ES8311](https://files.waveshare.com/wiki/common/ES8311.DS.pdf) · [PCF85063](https://files.waveshare.com/wiki/common/Pcf85063atl1118-NdPQpTGE-loeW7GbZ7.pdf) · [SHTC3](https://files.waveshare.com/wiki/common/SHTC3_Datasheet.pdf) · [ESP32-S3](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf)

### 3.5 Reference code

Clone reference repos into the gitignored `ref/` directory. Do not vendor them.

```sh
git clone --depth 1 https://github.com/waveshareteam/ESP32-S3-RLCD-4.2 ref/waveshare
git clone --depth 1 https://github.com/JasonHEngineering/waveshare_RLCD_400x300_monochrome ref/jasonh
```

- **Waveshare** (Apache-2.0). Worth reusing, under `02_Example/ESP-IDF/10_FactoryProgram/components/`: the ST7305 init and pixel LUT (`port_bsp/display_bsp.cpp`), SHTC3 and PCF85063 access (`port_bsp/i2c_equipment.cpp`), the battery ADC (`port_bsp/adc_bsp.cpp`), buttons (`port_bsp/button_bsp.c`) and codec pins (`ExternLib/codec_board/board_cfg.txt`, board `S3_RLCD_4_2`). Also see the XiaoZhi board (`02_Example/XiaoZhi/XiaoZhiCode_V2.1.0/main/boards/waveshare-s3-rlcd-4.2/`) and the ESPHome YAMLs (`02_Example/ESPHome/`).
- **JasonH smart clock** (MIT). An Arduino monolith with Singapore-specific APIs and no real low-power design. **We do not fork it.** Borrow ideas only: screen set, 1-bpp canvas, SD config, image converter script, and 3D-printable case STEP files.
- Code copied from either keeps its licence header and gets listed in `THIRD_PARTY.md`.

## 4. Requirements (from the owner)

The spec (§1.1) lists these with IDs.

- Monitor the battery and charging.
- Offer a few configurable layouts, watch-face style: each layout is fixed, and its data fields are optional. Presets can be stored and cycled automatically or by hand. Fields: time and date; SHTC3 temperature and humidity; weather from the internet; sunrise and sunset; data from other local devices (arriving over MQTT).
- Keep time with the PCF85063 RTC.
- Play audio for alarms and internet radio (ES8311 + ES7210).
- Provide settings on the device and through a website the device hosts, opened from a phone: in AP mode (to set up the client Wi-Fi) and over the local network.
- Save power: sync over Wi-Fi on a configurable schedule (default once a day), then switch Wi-Fi off. The owner can switch Wi-Fi on to reach the configurator.
- Control everything with the board's buttons.
- Suggest uses for the microSD slot (list agreed at M9).
- Later, integrate two-way with Home Assistant over MQTT: report device status and sensor data, and read chosen HA entities to show on the dashboard.
- Verification: flash from this Mac; the agent reads logs and screenshots over USB; the owner confirms what the panel physically shows and measures current with a USB power meter.

## 5. Architecture summary

The spec has the full design. This section keeps the essentials at hand.

### 5.1 Stack (decided 2026-09-25)

| Area | Choice |
|---|---|
| Framework | ESP-IDF **v5.5.x** (v5.5.5), target `esp32s3`. Revisit v6.x after M5 |
| Languages | C17 firmware, Python 3 host tools, plain HTML/CSS/JS web UI (no build step) |
| Graphics | Our own immediate-mode 1-bpp renderer (`gfx`). `tools/fontgen.py` generates fonts from TTF/BDF and they are committed as C sources. Text fonts cover Latin-1 + Latin Extended-A |
| Storage | NVS for identity and secrets. LittleFS `storage` partition for config JSON, state and sounds. FAT on microSD |
| Network | esp_wifi STA/AP, captive portal, mDNS `reflbo-XXXX.local`, esp_http_server, HTTPS through the cert bundle |
| Weather | Open-Meteo (no API key). Sun times are computed locally |
| Home Assistant and other devices | MQTT (esp-mqtt) with HA MQTT discovery. MQTT topics map to fields |
| Audio | `esp_codec_dev` (ES8311/ES7210) and the Espressif audio decoder |
| OTA | Two app slots with rollback |

Partition table: spec §14.1. Key `sdkconfig.defaults`: 16 MB QIO flash; octal PSRAM at 80 MHz with `CONFIG_SPIRAM_MEMTEST=n`; console on USB-Serial-JTAG; custom partition table; `CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`; bootloader and app logs at warning level (the app raises its logs to info once the board stays awake); core dumps without the boot-time check and logs; `tasks` statistics; app rollback; 16 lwIP sockets and 2 KB request headers for the web server (gotcha 25; other gadgets' cookies for 192.168.4.1 come along); large static buffers in PSRAM (`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` with `EXT_RAM_BSS_ATTR`); TLS allocations in PSRAM too (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`), so the flight radar's polls and a sync each hold a connection without exhausting internal RAM. Automatic light sleep (`CONFIG_PM_ENABLE`) is not used (D14).

### 5.2 Components

```
main/            app_main: init order, wiring, app event loop
components/
  util/          small pure-C helpers (CRC-32, base64, delay ticks)   [host]
  board/         pins, I²C bus, GPIO setup, buttons → gestures, wake cause
  st7305/        panel init, LPM/HPM, frame push, deep-sleep retention
  display/       canonical framebuffer, CRC-skipped pushes to the panel
  gfx/           framebuffer, primitives, text, fonts, bitmaps, QR    [host]
  locale/        language packs: en, cs; API prefix lang_             [host]
  astro/         sunrise/sunset, day length                           [host]
  datastore/     fields, freshness, change events, snapshot           [host]
  ui/            layouts, split trees, widgets, presets, screens, menu [host]
  scheduler/     next-wake computation (display, alarms, sync)        [host]
  sensors/       SHTC3, battery gauge
  rtc/           PCF85063 driver
  timekeeping/   system time from RTC, TZ and its short list, the RTC trim, manual set
  power/         power states, idle strategy, sleep entry
  netmgr/        Wi-Fi STA/AP, captive DNS, mDNS
  webui/         HTTP server, REST API, embedded web assets
  fetch/         HTTPS GETs with the certificate bundle, one connection kept per host
  weather/       Open-Meteo URLs, parsers, bands and levels; the HTTPS fetch
  solar/         PV forecast: Open-Meteo with our model, Forecast.Solar, Solcast; parsers, quarter hours [host]
  energy/        the house's energy from SolaX Cloud, by its Token ID or its Developer API (D37): requests, parsers, signs, today's totals [host]
  png/           PNG reader for the radar images: palette and RGBA, row by row   [host]
  map/           web-Mercator views; the built-in map (assets/map/map.bin) and its drawing   [host]
  radar/         ČHMÚ's and RainViewer's frames: their decoding, store and drawing [host]; their fetch
  adsb/          adsb.fi's aircraft on the Flights map, adsb.lol's routes and their cache [host]; the polling task
  ha_mqtt/       MQTT session, discovery, state, commands, field mappings   (planned)
  sync/          when syncs run and their retries, SNTP packets, the sync task
  audio/         codec control, tone/WAV/stream players, alarm ringing      (planned)
  storage/       NVS, LittleFS config files, microSD mount (planned)
  diag/          console commands, screenshot export
web/             web UI sources, embedded into the app image
tools/           host helpers: idf wrapper, log capture, screenshot, render, font/image generators
tools/tests/     unit tests for the host tools (run by ctest)
test/host/       host unit tests, fixtures, golden images
test/web/        tests of the page script against a fake device (Node, no packages; run by ctest)
docs/            the user guide and its images, specs, plans, power measurements
```

### 5.3 Design rules

- **Rendering is a pure function of (data, preset, time).** The web preview and the host golden tests use the same renderer.
- **Data flows one way:** drivers and services → `datastore` → `ui`. The UI emits intents, and `main` wires everything together.
- **`[host]` components include no ESP-IDF headers.** Other components keep their pure logic free of IDF headers too, so it can be host-tested (spec §3.1).
- **Web assets are embedded in the app image,** so OTA updates them. User data lives in the `storage` LittleFS partition, which `idf.py flash` never writes.
- **Shared resources have one owner.** `board` owns the I²C bus (GPIO 13/14) and the GPIO ISR service. `st7305` owns SPI3_HOST and GPIO 5, 6, 11, 12, 40 and 41. `display`, `st7305`, the I²C device drivers and sleep are not thread-safe; they belong to the app task (`main/app.c`, spec §3.2). Console commands that touch them run on it through `diag_set_executor(app_execute)`.
- **The device sleeps most of the time.** Every feature must work with that. For MQTT this means retained state, `expire_after`, and QoS 1 commands on a persistent session (spec §12).
- **Light sleep is the default idle strategy** (D3). Deep sleep stays available through `power idle deep`, so a board with the PS/SYNC rework can be compared again (gotcha 23). Tethered mode, with a USB host connected, stays awake (gotcha 11).
- For details, see spec §5.6 (controls), §9 (power and scheduling) and §5 (UI).

## 6. Environment and commands

One-time setup on macOS:

```sh
brew install cmake ninja dfu-util ccache   # already present on the owner's Mac
# Homebrew's Python 3.14 (3.14.6 and 3.14.7 checked) can't load pyexpat on macOS 26
# (it expects a newer libexpat than the system's), so pip, and the ESP-IDF installer
# with it, fail. ESP-IDF uses uv's Python 3.13 through a shim directory instead:
~/.local/bin/uv python install 3.13        # a fresh Mac has none yet
mkdir -p ~/esp/python-shim
ln -sf "$(~/.local/bin/uv python find 3.13)" ~/esp/python-shim/python3
ln -sf "$(~/.local/bin/uv python find 3.13)" ~/esp/python-shim/python
git clone -b v5.5.5 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.5
PATH="$HOME/esp/python-shim:$PATH" ~/esp/esp-idf-v5.5.5/install.sh esp32s3
```

`tools/idf.sh` runs `idf.py`, or any command after `exec`, inside the IDF environment. Before that it puts the shim on `PATH`, loads `export.sh` (showing its banner only if it fails) and changes to the repo root. It therefore works from any fresh shell, which matters for agents because every Bash call is one. Paths passed to it are relative to the repo root.

It takes ESP-IDF from `REFLBO_IDF_PATH` (default `~/esp/esp-idf-v5.5.5`) and deliberately ignores an inherited `IDF_PATH`. That keeps an older install from being picked up by accident; an ESP-IDF 5.2 environment already exists in `~/.espressif` on the owner's Mac.

Build, flash and observe:

```sh
tools/idf.sh set-target esp32s3            # once per clone
tools/idf.sh build
ls /dev/cu.usbmodem*                       # the board's USB-Serial-JTAG port
tools/idf.sh -p /dev/cu.usbmodemXXXX flash
tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/boot.log
tools/idf.sh exec python tools/devlog.py --cmd version --cmd heap
tools/idf.sh exec python tools/devlog.py --cmd "rtc set $(date -u +%Y-%m-%dT%H:%M:%SZ)"   # set the RTC from this Mac
tools/idf.sh exec python tools/devlog.py --cmd "power idle deep"   # or light; kept in NVS (sys/idle)
tools/idf.sh exec python tools/devlog.py --cmd "sleep test deep 2" # sleep cycles while tethered; then: sleep stats
tools/idf.sh exec python tools/devlog.py --cmd "preset list" --cmd "field set env.temp -5.5"   # redraws at once
tools/idf.sh exec python tools/devlog.py --cmd "btn key long"    # the menu: then KEY and BOOT, short or long
tools/idf.sh exec python tools/devlog.py --cmd "schedule add 23:00 night 06:00" --cmd "schedule on"
tools/idf.sh exec python tools/devlog.py --cmd "night 60"        # night sleep now; the console drops
tools/idf.sh exec python tools/devlog.py --cmd "btn boot long"   # config mode: Wi-Fi on, the AP password on screen
tools/idf.sh exec python tools/devlog.py --cmd "wifi status" --cmd "wifi scan"
tools/idf.sh exec python tools/devlog.py --cmd "battery learn start"   # learn the curve from the next full discharge (D21)
tools/idf.sh exec python tools/devlog.py --cmd "sync now" -t 40 --until "app_sync: sync (done|failed)"   # then: sync status, rtc get
tools/idf.sh exec python tools/devlog.py --cmd "radar status"   # the weather radar's frames and the flight radar's polls
tools/idf.sh exec python tools/devlog.py --cmd "solar status"   # the PV forecast and the house's reading, never a key
tools/idf.sh exec python tools/devlog.py --cmd "solar demo on" --cmd screenshot   # the goldens' sample day, without a provider
tools/idf.sh exec python tools/devlog.py --cmd "energy raw"     # SolaX Developer API's last replies, to check fields and signs (D37)
python3 tools/gen_zones.py                 # regenerate web/zones.js from this Mac's tz database
tools/idf.sh exec python tools/screenshot.py -o captures/screen.png --compare test/host/golden/test_pattern.pbm
cmake -S test/host -B build-host -G Ninja && cmake --build build-host \
  && ctest --test-dir build-host --output-on-failure
cmake -S test/host -B build-host-asan -G Ninja -DREFLBO_SANITIZE=ON && cmake --build build-host-asan \
  && ctest --test-dir build-host-asan --output-on-failure   # the same tests with ASan and UBSan
tools/gen_fonts.sh                          # regenerate components/gfx/fonts (needs uv; versions in tools/requirements.txt)
tools/gen_icons.sh                          # regenerate components/gfx/icons from assets/icons (needs uv)
tools/gen_map.sh                            # regenerate assets/map/map.bin (plain Python 3; sources cached in ref/mapdata/)
python3 tools/render.py                     # host renderings to captures/render/*.png (after the host build)
python3 tools/docs_images.py                # the panel images in docs/images/panel, from the goldens
node --test test/web/test_app.mjs           # the page script against a fake device; ctest runs it when node is found
```

- `tools/idf.sh` refuses commands that talk to the board (`flash`, `erase-*`, `monitor`, …) unless the port is given with `-p` or `ESPPORT`. Otherwise idf.py would probe every serial port and use the first ESP chip that answers.
- `devlog.py` picks the port itself when exactly one `/dev/cu.usbmodem*` exists; otherwise pass `-p`. Exit codes: 0 ok, 2 port problem, 3 console prompt never appeared, 4 `--until` not seen in time. `screenshot.py` picks the port the same way and adds 5 (the image differs from `--compare`) and 6 (no complete, valid image arrived). It writes the PNG and the raw PBM next to it.
- After adding a component directory, run `tools/idf.sh reconfigure` once. ESP-IDF finds components when CMake configures, so a plain `build` in an existing build directory silently leaves the new component out.
- Config files live on LittleFS, mounted at `/fs`: `/fs/cfg/settings.json` and `/fs/cfg/presets.json`, each with a `.bak` of the previous version (spec §14.3). A boot formats the `storage` partition whenever it doesn't mount: when it is blank, but also when it is corrupted. The menu's factory reset formats it too. `idf.py flash` never writes it.
- Golden renders: after an intentional UI change, rewrite a golden with `build-host/render_dashboard <fixture> test/host/golden/dash_<fixture>.pbm` or `build-host/render_screen <fixture> test/host/golden/screen_<fixture>.pbm` (fixtures in `test/host/dashboard_fixtures.h` and `screen_fixtures.h`; `--list` prints them), look at the PNGs from `tools/render.py`, then commit. If the user guide shows that golden (`tools/docs_images.py` lists them), run it too.
- **Night sleep deep-sleeps even while a PC is attached** (spec §9.1). After `night <minutes>` or a schedule's night entry, the port is gone until the end time or a KEY or BOOT press.
- Do not run `idf.py monitor` from an agent shell; it needs an interactive TTY. Use `devlog.py`.
- Do not run `idf.py erase-flash` or erase NVS without asking. Either wipes the owner's Wi-Fi credentials and presets.
- If the port is missing, the board is probably in deep sleep. Press KEY. If it is still missing, ask the owner to enter download mode (hold BOOT while powering on).
- **One process per port.** A capture or console left open while `flash` runs makes esptool fail ("multiple access on port"), and its DTR/RTS changes can leave the chip in download mode. Close every other reader first.
- **A mute board after flashing:** USB stays attached, but nothing comes out, commands get no answer, and esptool cannot connect. Seen twice at M2. On 2026-09-25 the cause is unknown. On 2026-09-28 the build crashed at boot on purpose (a failure test), and the core dump showed the crash. Seen again at M3b (2026-09-29), right after `flash` and an immediate `devlog --reset`: no core dump was stored, and 15 minutes of esptool retries never reached the chip. Since then the checks capture boots with `devlog --cmd reboot --until "reflbo ready"` instead, which also failed once at M5 (2026-10-01, the fourth flash of the day): the bootloader's banner came, then nothing, and esptool got "No serial data received". A power cycle (hold PWR 3 s, then press it) recovers the board. For download mode, hold BOOT while switching on, flash, then leave download mode as gotcha 22 says. A power cycle can set the RTC's oscillator-stop flag (gotcha 7), so check `rtc get` afterwards.
- Power measurements follow the USB power-meter method in spec §9.4. Record them in `docs/power.md`.
- **Stable firmware and board tests** (owner, 2026-10-03, D32, spec §12.10).
  - The stable firmware is tag `stable-m6d` (2026-10-06, D36, D39; the code of 5074f1a); its build is kept in `captures/stable/stable-m6d/` (local and gitignored; build the tag again if it's lost). `captures/stable/stable-m6d/flash.sh <port>` writes the bootloader, partition table, OTA data and app, never the storage partition or NVS.
  - The board runs it between tests: flash it back after every MQTT/HA test, and check that `version` shows `elf 112aa30d3`.
  - Before a test that changes the board's data, save `GET /api/backup`; after the test, and after the stable firmware is back, restore it with `POST /api/restore` and compare.
  - The one before, `stable-m6b` (`captures/stable/stable-m6b/`, `elf dcb35a7e3`), refuses presets of more than 8 cells (M6c's) and presets with the Solar or Energy layout (M6d's): it loads `presets.json.bak` if that parses, else its built-ins, and its next save (a KEY switch, a cycle toggle) overwrites the file. Going back to it needs `captures/stable/before-m6d-backup.json` restored after it (taken under the M6c image before M6d's first flash).
- **Web UI checks from this Mac** (owner, 2026-09-30).
  - Join the board's AP: `networksetup -setairportnetwork en0 reflbo-XXXX <password from the screen>`. Wi-Fi comes after the USB Ethernet in the service order, so the Mac's internet stays up. Without the USB Ethernet (2026-10-01) joining the AP cuts this Mac's internet, and the agent's session with it: join, call and come back in one script that restores the home Wi-Fi whatever happens (`networksetup -removepreferredwirelessnetwork en0 reflbo-XXXX`, then Wi-Fi power off and on).
  - Only a phone or Mac on the AP may choose the first web password (D18). A check that needs the API on a board with the owner's password resets it (the owner allows that at any time, 2026-10-02: Menu ▸ Wi-Fi ▸ Reset web password), chooses a temporary one over the AP, logs in over the AP or the LAN, and resets it again at the end. With a password set, config mode keeps the AP off: after a restart of config mode the calls go over the LAN, at the address `wifi status` prints (M6c checks).
  - Log in with `curl -c jar -H 'Content-Type: application/json' -d '{"password":"…"}' http://192.168.4.1/api/auth/login` and pass `-b jar` to the other calls. `--data-binary @build/reflbo.bin` with `Content-Type: application/octet-stream` on `/api/ota` updates the firmware.
  - Afterwards run `networksetup -removepreferredwirelessnetwork en0 reflbo-XXXX`, and clear any web password a check set (Menu ▸ Wi-Fi ▸ Reset web password), so the owner's first visit chooses theirs.
  - Headless Chrome with `--remote-debugging-port=9222` is an argent Chromium target. Its `--screenshot` renders at least 500 px wide, Chrome's smallest window. A key press of Enter sent over CDP doesn't submit a form: tap the button.
  - Buttons that ask first (Forget, Factory reset) open a native `confirm()`, which blocks a page driven over CDP. Replace `window.confirm` with `debugger-evaluate` before tapping one, and never tap Factory reset without the owner's agreement.
  - A web restart or update sets NVS `sys/resume_cfg`, and the next image comes back in config mode. After a USB flash in the middle of such a check, the board can come up in config mode by itself; `btn boot long` would then switch it off.
- The Bash tool runs zsh: a variable holding several flags isn't split into words, and a word that starts with `=` is looked up as a command. Write flags out. `path` is zsh's array twin of `PATH`: a loop variable named `path` breaks every command after it.

## 7. Verification

Use the cheapest level that proves the change. Any UI change needs at least level 3.

1. **Build**: `idf.py build` passes with no new warnings.
2. **Host**: unit tests and golden render tests in `test/host/`, and the page script's tests in `test/web/`. Look at the rendered PNGs.
3. **Device**: flash, capture the boot log, exercise the change through the console, then take a screenshot and look at it.
4. **Owner**: only for physical facts, such as panel orientation and contrast, audio, how the buttons behave, and current draw. Give an exact checklist with expected results.

**Screenshots**: the `screenshot` console command prints the canonical framebuffer as base64 PBM between `-----BEGIN RLCD PBM-----` and `-----END RLCD PBM-----`. `tools/screenshot.py` turns that into a PNG using only pyserial and the standard library. In config mode the web UI serves `/api/screenshot.bmp`, and `/api/preview.bmp` renders any preset with live data. A screenshot shows what the firmware drew, not what the panel shows, because the ST7305 is write-only. After any display-driver change, have the owner confirm the test pattern.

**Diagnostics console** (`diag`; full list in spec §15). Available now: `help`, `version`, `heap`, `reboot`, `screenshot`, `panel status|test|clear|mode <hpm|lpm>|rate <0.25|0.5|1|2|4|8>|fps [s]|sleep|wake|init <factory|xiaozhi>`, `btn <key|boot> <short|double|long>` (simulated presses), `sensors`, `battery [learn start|stop]`, `rtc get|set <ISO 8601>`, `tasks`, `power idle [deep|light]`, `sleep stats [reset]|test <deep|light> <n>`, `field list|get <id>|set <id> <value>|clear <id>`, `preset list|set <id>`, `night <minutes>`, `schedule list|on|off|clear|add <HH:MM> preset <id> [days]|add <HH:MM> night <HH:MM> [days]`, `wifi status|scan`, `sync now|status`, `radar status|loop`, `solar status|demo on|demo off`, `energy raw`; `rtc get` also prints the trim and the last drift. Planned: `audio tone`. Drive the UI, the menu included, with `btn` and `screenshot` instead of asking the owner to press buttons. A toast lasts 3 s, less than two port sessions take, so send `--cmd "btn key short" --cmd screenshot` in one devlog call and decode the log with `screenshot.extract_pbm()`. A `btn` gesture reaches the app through the buttons task, so a command in the same devlog call can run before it has taken effect: check its result in a separate call, and end a call that sends one with a slower command (`sync status`, `screenshot`), as a devlog call ends when its last command answers, before the app logs what the gesture did. Inject test data with `field set`. Run commands with `tools/idf.sh exec python tools/devlog.py --cmd <command>`. The console runs in plain line mode on purpose: no history, arrow keys or tab completion, even in a terminal. It never sends escape-code queries that a script can't answer (spec §15, `components/diag/diag.c`).

**Done** means: the acceptance criteria pass at the right level, new logic has tests, power-affecting changes have measurements in `docs/power.md`, and this file and `docs/` are updated, the user guide (`docs/guide.md`, its images and `README.md`) included.

## 8. Conventions

**Code**

- ESP-IDF style: 4-space indent and `snake_case`. Public APIs carry a component or module prefix (`st7305_…`, `ds_…`, `battery_…` in `sensors`) and live in `include/`.
- Return `esp_err_t` and use `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`. No `ESP_ERROR_CHECK` on recoverable paths such as network, SD or sensors.
- Large buffers go in PSRAM (`MALLOC_CAP_SPIRAM`); DMA buffers go in internal RAM. No allocation inside render or audio hot loops.
- One `TAG` per module. `ESP_LOGI` for state changes, `ESP_LOGD` for detail. Never log secrets.
- Every task gets an explicit stack size, priority and core. Document who owns each shared resource (I²C bus, SPI).
- Compile-time defaults come from Kconfig (`REFLBO_*`, the `reflbo` menu in `main/Kconfig.projbuild`); runtime settings in NVS or LittleFS override them.
- List a component's sources explicitly in `SRCS`, not `SRC_DIRS`. ESP-IDF globs `SRC_DIRS` only when CMake configures, so a new file would be left out of the build without any error.
- Dependencies come from the ESP Component Registry through `idf_component.yml`, with pinned versions. Commit `dependencies.lock`. Never edit `managed_components/`.
- Change configuration through `sdkconfig.defaults`, then delete `sdkconfig` and build. An existing `sdkconfig` keeps the values it has, so `reconfigure` only adds options that are new. `sdkconfig` is generated and gitignored. Personal overrides, such as dev Wi-Fi credentials, go in the gitignored `sdkconfig.defaults.local`.
- A setting's default lives in `components/storage`, where the host tests see it (`settings_sync_defaults()`), not only in `main/app_ui.c`: at M5 the sync times and NTP servers had defaults in the tests alone, and the board synced never.
- Never commit secrets: Wi-Fi passwords, MQTT credentials, tokens or API keys.

**Licence**

- Open source with credit: Apache-2.0 (confirmed by the owner, 2026-09-25). `LICENSE`, `NOTICE` and `THIRD_PARTY.md` land in M0.
- Adapted third-party code keeps its licence header. Credit it in `THIRD_PARTY.md`, and in `NOTICE` where its licence requires it.
- Fonts, icons and other assets need licences that allow redistribution in this repository.

**Git**

- Branch `main`; remote `origin` is `git@github.com:Vybo/reflbo.git`. The owner allows pushing to `origin` without asking (2026-09-25). Never force-push `main`.
- Make small, focused commits in Conventional Commits style (`feat(st7305): …`, `fix: …`, `docs: …`), imperative mood. Commit only states that build.
- No AI or assistant attribution anywhere: commits, PRs, code comments or docs.

**Keep this file current.** When you add a command, component, decision or gotcha, update AGENTS.md in the same commit, and remove *(planned)* markers as things land.

## 9. Decisions

Recorded 2026-09-25. Rationale is in spec §1.2.

| # | Decision |
|---|---|
| D1 | Our own immediate-mode renderer; no LVGL |
| D2 | ESP-IDF v5.5.x |
| D3 | Light sleep is the default idle strategy. At M2 both strategies drew 11.5 mA at 5.24 V, because the 3V3 converter's forced PWM dominates (gotcha 23); deep sleep stays available (`power idle deep`) |
| D4 | English UI, structured as language packs; a Czech pack joins in M3 (D15) |
| D5 | Home Assistant over MQTT |
| D6 | Power is best effort; an average below 2 mA is the stretch goal |
| D7 | Data from other local devices arrives over MQTT |
| D8 | Open source with credit: Apache-2.0 (confirmed) |
| D9 | No RTC backup cell is fitted now (one can be added later); firmware must work without it |
| D10 | Extra features are discussed at the relevant milestone before they are built |
| D11 | The sync schedule (`times` / `interval` / `always` / `manual`) and the display update interval are both configurable |
| D12 | Panel: the factory init sequence (better contrast), with the LPM refresh rate set separately: 1 Hz by default, changeable at runtime with `panel rate` from 0.25 to 8 Hz (owner check at M1) |
| D13 | Landscape only; portrait orientation was declined at M1 |
| D14 | Light sleep is entered explicitly by the app (`power_sleep_light()`), not by esp_pm automatic light sleep; a tethered board stays awake |
| D15 | Accepted M3 proposals (owner, 2026-09-28): the LPM refresh rate setting, a preset schedule that can also start a timed night sleep (screen off, woken only by buttons or the end time, its saving measured), extra local fields, and a Czech pack with name days and holidays. The Night layout stays deferred |
| D16 | Owner, 2026-09-29: the Czech pack ships the public holidays but no name days until a source with a clean licence turns up (the best one found traces to CC BY-SA Wikipedia). A button still held at sleep time is left out of that sleep's wake sources |
| D17 | Owner, 2026-09-29 (M3b render review): the menu leaves out Display ▸ Contrast for now, and the temperature offset steps by 0.1 °C |
| D18 | Owner, 2026-09-30 (M4 planning): the web UI gets a password instead of the admin PIN, chosen on the first visit over the device's own AP and needed on every visit after (§10.4); web UI translations stay deferred; M4 runs as one plan; the snapshot header gets no ELF hash |
| D19 | Owner, 2026-09-30 (M4 spike review): the first-run screen appears when `settings.json` is missing at boot; the status bar's Wi-Fi state moves to M5 and M6; a restart from the web UI returns in config mode; the web UI sets a lost clock from the phone, can change the password and log out, and gets a Device page; the temperature offset defaults to −2.0 °C, as the board warms the SHTC3 by about 2 °C at all times |
| D20 | Owner, 2026-09-30 (M4 acceptance): config mode watches the battery, as a battery that sags under the radio would brown out anyway; while a phone is logged in, config mode shows the dashboard with a globe in the status bar, and KEY brings the setup screen back; the preset preview names its slots and a new preset joins the cycle; seconds keep the refresh rate the user picked; the battery level follows the built-in curve or the owner's own full and empty voltages (a learned curve is still open) |
| D21 | Owner, 2026-09-30: the battery curve is learned from one full discharge, not a charge: charging is invisible to the firmware (gotcha 3), and this board's steady load makes time a fair measure of charge |
| D22 | Owner, 2026-10-01 (ADS-B radar proposal, spec §19.1): adsb.fi only; it runs only in sync mode `always` (D11), not behind a separate switch; the map is centred and zoomed from the web UI, with towns and airports from OurAirports and Natural Earth built in. Still a proposal for after M5 |
| D23 | Owner, 2026-10-01 (weather radar, spec §19.2): ČHMÚ by default with RainViewer outside its coverage; a full-screen layout and a slot widget; aircraft stay a separate view; the radar never turns Wi-Fi on itself, taking a frame with each normal sync and every 5 minutes in sync mode `always`; both radars join the roadmap as M7, after M6 brings `always`, so Audio is M8 and microSD M9 |
| D24 | Owner, 2026-10-01: the radars come sooner: sync mode `always` moves into M5, the radars become M6, and MQTT and Home Assistant M7 |
| D25 | Owner, 2026-10-01 (M5 planning): M5 also brings quiet hours, air quality and pollen from Open-Meteo, and the RTC trim against NTP; a static IP stays deferred; M5 runs as one plan; review minors are fixed where M5 touches their code |
| D26 | Owner, 2026-10-01 (M5 spike review): weather icons from the Weather Icons font (SIL OFL 1.1) beside Material Icons; the sun widget shows the day length's change; a UV index field joins the air quality; pollen keeps a top field and one per type |
| D27 | Owner, 2026-10-01 (M6 planning): M6 runs as one plan, straight from the spec without a spike; review minors are fixed where M6 touches their code; three extras join: rain in the next 2 hours, the last hour's radar loop, the nearest aircraft's route (adsb.lol) |
| D28 | Owner, 2026-10-01 (M6 design): the flight radar is a built-in preset that the cycle visits only in sync mode `always`; each radar has its own centre and zoom; BOOT short on the radar layout plays the last hour (12 frames); the map draws borders, towns and airports; no microSD in M6 (radar and flight history, a detailed map pack and an aircraft database join M9's candidates) |
| D29 | Owner, 2026-10-02 (M6 plan review): GeoNames' places join the map's towns inside ČHMÚ's radar area, with GeoNames credited (CC BY 4.0); the RTC trim keeps measuring only across syncs at least 20 h apart (M5 review's I9); aircraft altitudes stay as flight levels from 10 000 ft and feet below; M6 runs inline (Native) |
| D30 | Owner, 2026-10-02 (M6 board checks): BOOT short on the Radar layout starts a sync on demand for a fresh frame whenever the loop can't play (outside sync mode `always`, or with fewer than two frames), with the menu's Sync now toasts |
| D31 | Owner, 2026-10-02 (M6b design): a split layout beside the fixed ones: rows or columns at 1/4, 1/3, 1/2, 2/3 or 3/4, split again, at most 8 cells of at least 90×40, each cell a field at the size it allows, each split's separator shown or hidden, edited on the web page; BOOT double on the dashboard toggles sync mode `always`; both are M6b, one plan, before M7 |
| D32 | Owner, 2026-10-03 (M7 design): one `ha_mqtt` client, a session in every sync and kept connected in sync mode `always`; a failed MQTT session is shown (Sync page, Info, a status-bar mark) but doesn't fail the sync; HA buttons (sync now, next preset), key-press triggers (only while connected) and a message (a banner until KEY dismisses it, and the `ha.message` field); TLS and HA REST pull deferred; review minors where M7 touches their code, plus `fetch`'s transmit buffer; no broker or HA yet, so the checks with HA wait; the M6b build is the stable firmware (tag `stable-m6b`), flashed back after every MQTT/HA test, with the board's configuration backed up and restored around tests and nothing erased without asking |
| D33 | Owner, 2026-10-04: M7 waits; M6c (small split cells) and M6d (solar and the house's energy) come first, one plan each, their designs approved from a throwaway spike's panel renders (`spike/m6cd`); M7 keeps its number and its plan is refreshed before it runs |
| D34 | Owner, 2026-10-04 (M6c): split cells down to 40×20 px and up to 24; an XS size drawn like the status bar (one line, or the symbol over the value in a narrow, tall cell); S in cells under 150×80 puts the symbol beside the value; a cell that grows never draws a field smaller; the fixed layouts stay as they are |
| D35 | Owner, 2026-10-04 (M6d forecast): Open-Meteo's tilted irradiance through our PV model by default, or Forecast.Solar, or Solcast (within its 10 calls a day); up to 2 roof planes; the `pv.*` fields and chart, the Solar layout and preset; every data step of the sync can be switched off, the time step excepted |
| D36 | Owner, 2026-10-04 (M6d energy): SolaX Cloud's real-time API (token and registration number in NVS), a reading each sync and every 5 min in sync mode `always`, failures shown but not failing the sync; the home battery on auto, on or off; the `energy.*` fields and flow, the Energy layout and preset; MQTT as a source with M7; after M6d's acceptance `stable-m6d` replaces `stable-m6b` |
| D37 | Owner, 2026-10-05 (M6d board checks): SolaX Cloud's Developer API joins as an energy source (`solax-dev`, listed first on the Solar page) beside the Token ID one, as solaxcloud.com issues no new Token IDs: an application's Client ID and Client Secret in NVS and SolaX's region; a bearer token kept in RAM only; the plant and its devices found once; today's totals from the plant's statistics, without a midnight reading; `energy raw` to check fields and signs on the owner's plant |
| D38 | Owner, 2026-10-06 (M6d acceptance): powers under 1 kW show in whole watts, as the SolaX app does, everywhere; the flow shows W while all its powers are under 1 kW; Solcast's check with the owner's key waits |
| D39 | Owner, 2026-10-06 (M6d acceptance): the Developer API's solar is the app's figure, what the panels give through the inverter (its output with a battery's charge, at most the panels' DC); past bars from SolaX's 5-minute history are a later option (spec §19); roof checked; the power measurement and Solcast wait for the project's end; this build is `stable-m6d` |
| D40 | Owner, 2026-10-06 (M7 refresh, D33): M7 also brings an MQTT source for the house's energy beside SolaX's two (spec §12.11); state labels for text fields and a time kind for timestamps, so any HA entity reads well (spec §12.5); HA's `unknown`/`unavailable` are no value; no `pv.*`/`energy.*` published to HA; HA checks still wait for the owner's broker; the plan runs Native |
