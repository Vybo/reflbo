# AGENTS.md — reflbo

Guide for coding agents (and humans) working in this repository. Read it fully before changing anything, and keep it current (§8).

> **Scope.** This is a standalone embedded firmware project. Instructions inherited from parent directories about iOS, WeConnect-iOS, the CAT monorepo, Jira ticket keys or PR templates do not apply here.

**Quick rules**

1. Read §3.4 (hardware gotchas) before touching power, sleep, pins or the display.
2. Verify at the right level (§7). Prefer screenshots and logs you capture yourself over asking the owner to look.
3. Never erase flash/NVS, flash a port you haven't confirmed is this board, or commit secrets without asking.
4. Items marked *(planned)* do not exist yet. Never describe them as done.
5. Build only what the design spec covers. Anything extra is a proposal to raise with the owner at the relevant milestone (§2).
6. Update this file in the same commit as any new command, component, decision or gotcha.

---

## 1. What we are building

**reflbo** is firmware for the Waveshare **ESP32-S3-RLCD-4.2** board. The device is a battery-powered, always-visible desk display (4.2″ reflective LCD, 400×300, 1-bit) that shows configurable, watch-face-like dashboards: time and date, indoor climate, weather, sun times, Home Assistant values and data from other local devices. It also does alarms and internet radio.

Guiding principles:

1. **Standalone first.** Works with no network: RTC time, local sensors and computed sun times. Wi-Fi, weather and Home Assistant only add to that.
2. **Battery first.** The radio is off by default. A *sync* is a Wi-Fi session for time, weather and MQTT; it runs on a configurable schedule (default once a day), and Wi-Fi also turns on when the owner asks. A *display update* redraws from local data, every minute by default (configurable), with no Wi-Fi. Every feature states its power cost.
3. **Universal, with Brno defaults.** Location, time zone, language and units are all configurable. Defaults: Brno, CZ (49.1951 N, 16.6068 E), `Europe/Prague` (`CET-1CEST,M3.5.0,M10.5.0/3`), metric, `cz.pool.ntp.org`. The UI is in English, organised as language packs so more languages can be added.
4. **Agent-verifiable.** Anything that renders can be screenshotted over USB and rendered on the host. Changes can be checked without the owner's eyes.
5. **Small, testable modules.** Pure logic (layout, formatting, astro, parsing, scheduling) builds and runs tests on the host.

## 2. Status and roadmap

- **There is no firmware code yet.**
- **Design spec:** [`docs/specs/2026-09-25-firmware-design.md`](docs/specs/2026-09-25-firmware-design.md) is the authoritative design. The owner approved it on 2026-09-25. §5 below summarises it. If the two disagree, the spec wins; fix this file.
- **Plans:** each milestone gets its own implementation plan in `docs/plans/`, written just before that milestone starts. Current plan: [`docs/plans/2026-09-25-m0-toolchain-and-skeleton.md`](docs/plans/2026-09-25-m0-toolchain-and-skeleton.md).
- **Extra features:** anything beyond the requirements (spec §1.1) is a proposal. Raise it at the relevant milestone (spec §19) and build it only after the owner agrees.
- **Repository:** the owner is in Brno, CZ. Remote `origin` is `git@github.com:Vybo/reflbo.git`.

| # | Milestone | Done when |
|---|---|---|
| M0 | Toolchain and skeleton: ESP-IDF, project builds and flashes, USB console, `tools/` helpers, licence files | `idf.py build` is clean and the console answers |
| M1 | ST7305 driver, `gfx`, fonts, screenshot path (serial → PNG), host renderer | The test pattern on the panel (owner confirms orientation) matches the screenshot |
| M2 | Board services: PCF85063, SHTC3, battery gauge, buttons; clock screen; both idle strategies | Values on screen match the console. The owner measures deep vs light sleep, and one idle strategy is chosen |
| M3 | Data store, layouts, presets, cycling, status bar, on-device menu | KEY switches presets, and the choice survives a reboot |
| M4 | Wi-Fi manager (STA/AP, captive portal), web configurator, mDNS, OTA | A phone sets up Wi-Fi from AP mode; OTA works |
| M5 | Time sync, weather, astro, sync scheduler, power tuning | Daily sync works on battery; the measured average current is in `docs/power.md` |
| M6 | MQTT and Home Assistant, including data from other local devices over MQTT | Entities appear in HA; a mapped MQTT value renders on the device |
| M7 | Audio: offline alarms, then internet radio | An alarm fires from idle; a radio stream plays |
| M8 | microSD features (list agreed at the start of M8) | The agreed features are verified |

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
4. **Keeping the image through deep sleep.** The ST7305 keeps showing its image while powered. RESET (GPIO41) and CS (GPIO40) are digital-only pads that lose state in deep sleep, so hold them with `gpio_hold_en()` plus `gpio_deep_sleep_hold_en()`. On wake from deep sleep, skip the panel reset and init: re-attach SPI and push the frame. Between updates use LPM (`0x39`, 0.25–8 Hz). Use HPM (`0x38`, 16–51 Hz) only for fast interaction. A community driver reports about 10 µA in sleep-in (image hidden), about 1 mA in LPM and about 5 mA in HPM. Measure these.
5. **Framebuffer format.** In landscape the panel packs 2×4-pixel blocks into each byte (vendor `InitLandscapeLUT`), and in the panel buffer bit 1 means white. Our canonical buffer is row-major 1 bpp, MSB first, 1 = black (the PBM P4 layout). Convert it when flushing (spec §4.1).
6. The two vendor init sequences differ: the factory firmware runs SPI at 10 MHz, XiaoZhi at 40 MHz, and they use different VSHP/VSLP voltages (contrast) and frame-rate settings. Pick one, tune contrast, and record the choice here.
7. **Use the PCF85063 for timing.** The ESP32 has no 32 kHz crystal; the board's crystal belongs to the PCF85063. The ESP sleep timer therefore drifts, so the PCF85063 is the time source. Wake scheduling uses its alarm: the AF flag latches and INT stays low until cleared, which ext1 catches reliably. Don't rely on its minute interrupt, which is only a 1/64 s pulse.
8. **The audio analog rail is always on** (RT9193). Put the ES8311 and ES7210 into standby over I²C and keep PA_CTRL low when nothing is playing.
9. **The microSD slot is always powered and has no card detect.** An inserted card adds idle current. Mount it on demand and detect a card by probing.
10. The SHTC3 reads high because the board heats it; vendor code subtracts a constant 4 °C. Provide a calibration offset, and sample right after wake, before Wi-Fi and the CPU warm the board.
11. USB-Serial-JTAG disappears during deep sleep, which breaks flashing and the console. Dev builds should use light sleep instead whenever a USB host is detected ("tethered mode"). Otherwise press KEY to wake the board, or ask the owner to enter download mode (hold BOOT while powering on).
12. The vendor factory firmware draws about 90 mA at 5.3 V, roughly 24 h on a battery. That is the baseline to beat by a wide margin.
13. **RTC backup cell: none is fitted now; one can be added later.** It plugs into a small 2-pin 1.0 mm connector (J7 in the schematic), not a coin holder, and must be a rechargeable cell with leads (ML1220), because the board charges it. Per the schematic, J7 pin 1 is + and pin 2 is GND; check the polarity before plugging a cell in. Without a cell, the time is lost at PWR-off and the RTC's oscillator-stop flag reports it. The firmware then syncs at boot if Wi-Fi is configured, and otherwise asks for the time to be set (spec §7).
14. **Partial updates exist but don't save panel power.** CASET/RASET/RAMWR can write a RAM window in 12×2 px cells (landscape *y* × *x*), and RAM may be written in LPM; new content shows at the next panel frame. The panel still re-drives every line each frame, so a partial write saves only SPI time (about 0.03–0.1 mAh/day). v1 pushes full frames and skips the push when nothing changed (spec §4.2).

Datasheets: [ST7305](https://files.waveshare.com/wiki/common/ST_7305_V0_2.pdf) · [ES8311](https://files.waveshare.com/wiki/common/ES8311.DS.pdf) · [PCF85063](https://files.waveshare.com/wiki/common/Pcf85063atl1118-NdPQpTGE-loeW7GbZ7.pdf) · [SHTC3](https://files.waveshare.com/wiki/common/SHTC3_Datasheet.pdf) · [ESP32-S3](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf)

### 3.5 Reference code

Clone reference repos into the gitignored `ref/` directory. Do not vendor them.

```sh
git clone --depth 1 https://github.com/waveshareteam/ESP32-S3-RLCD-4.2 ref/waveshare
git clone --depth 1 https://github.com/JasonHEngineering/waveshare_RLCD_400x300_monochrome ref/jasonh
```

- **Waveshare** (Apache-2.0). Worth reusing, under `02_Example/ESP-IDF/10_FactoryProgram/components/`: the ST7305 init and pixel LUT (`port_bsp/display_bsp.cpp`), SHTC3 and PCF85063 access (`port_bsp/i2c_equipment.cpp`), the battery ADC (`port_bsp/adc_bsp.cpp`), buttons (`port_bsp/button_bsp.c`) and codec pins (`ExternLib/codec_board/board_cfg.txt`, board `S3_RLCD_4_2`). Also see the XiaoZhi board (`02_Example/XiaoZhi/XiaoZhiCode_V2.1.0/main/boards/waveshare-s3-rlcd-4.2/`) and the ESPHome YAMLs (`02_Example/ESPHome/`).
- **JasonH smart clock** (MIT). An Arduino monolith with Singapore-specific APIs and no real low-power design. **We do not fork it.** Borrow ideas only: screen set, 1-bpp canvas, SD config, image converter script, and 3D-printable case STEP files.
- Code copied from either keeps its licence header and gets listed in `THIRD_PARTY.md` *(planned)*.

## 4. Requirements (from the owner)

The spec (§1.1) lists these with IDs.

- Monitor the battery and charging.
- Offer a few configurable layouts, watch-face style: each layout is fixed, and its data fields are optional. Presets can be stored and cycled automatically or by hand. Fields: time and date; SHTC3 temperature and humidity; weather from the internet; sunrise and sunset; data from other local devices (arriving over MQTT).
- Keep time with the PCF85063 RTC.
- Play audio for alarms and internet radio (ES8311 + ES7210).
- Provide settings on the device and through a website the device hosts, opened from a phone: in AP mode (to set up the client Wi-Fi) and over the local network.
- Save power: sync over Wi-Fi on a configurable schedule (default once a day), then switch Wi-Fi off. The owner can switch Wi-Fi on to reach the configurator.
- Control everything with the board's buttons.
- Suggest uses for the microSD slot (list agreed at M8).
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

Partition table: spec §14.1. Key `sdkconfig.defaults`: 16 MB QIO flash; octal PSRAM at 80 MHz with `CONFIG_SPIRAM_MEMTEST=n`; console on USB-Serial-JTAG; custom partition table; `CONFIG_PM_ENABLE`; tickless idle; app rollback; `CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`; quiet bootloader logs.

### 5.2 Components *(planned layout)*

```
main/            app_main: init order, wiring, app event loop
components/
  board/         pins, I²C bus, GPIO setup, buttons → gestures, wake cause
  st7305/        panel init, LPM/HPM, frame push, deep-sleep retention
  gfx/           framebuffer, primitives, text, fonts, bitmaps, QR    [host]
  locale/        language packs (en in v1)                            [host]
  astro/         sunrise/sunset, day length                           [host]
  datastore/     fields, freshness, change events, snapshot           [host]
  ui/            layouts, widgets, presets, screens, menu             [host]
  scheduler/     next-wake computation (display, alarms, sync)        [host]
  sensors/       SHTC3, battery gauge
  rtc/           PCF85063 driver
  timekeeping/   system time from RTC, TZ, SNTP, manual set
  power/         power states, idle strategy, sleep entry
  netmgr/        Wi-Fi STA/AP, captive DNS, mDNS
  webui/         HTTP server, REST API, embedded web assets
  weather/       Open-Meteo client and parser
  ha_mqtt/       MQTT session, discovery, state, commands, field mappings
  sync/          sync sequence and backoff
  audio/         codec control, tone/WAV/stream players, alarm ringing
  storage/       NVS, LittleFS config files, microSD mount
  diag/          console commands, screenshot export
web/             web UI sources, embedded into the app image
tools/           host helpers: idf wrapper, log capture, screenshot, render, font/image generators
test/host/       host unit tests, fixtures, golden images
docs/            specs, plans, power measurements
```

### 5.3 Design rules

- **Rendering is a pure function of (data, preset, time).** The web preview and the host golden tests use the same renderer.
- **Data flows one way:** drivers and services → `datastore` → `ui`. The UI emits intents, and `main` wires everything together.
- **`[host]` components include no ESP-IDF headers.** Other components keep their pure logic free of IDF headers too, so it can be host-tested (spec §3.1).
- **Web assets are embedded in the app image,** so OTA updates them. User data lives in the `storage` LittleFS partition, which `idf.py flash` never writes.
- **The device sleeps most of the time.** Every feature must work with that. For MQTT this means retained state, `expire_after`, and QoS 1 commands on a persistent session (spec §12).
- **The idle strategy (deep or light sleep) stays switchable** until the M2 measurement (spec §3.4). Tethered mode, with a USB host connected, always uses light sleep.
- For details, see spec §5.6 (controls), §9 (power and scheduling) and §5 (UI).

## 6. Environment and commands

One-time setup on macOS:

```sh
brew install cmake ninja dfu-util ccache   # already present on the owner's Mac
# Homebrew's Python 3.14 (3.14.6 and 3.14.7 checked) can't load pyexpat on macOS 26
# (it expects a newer libexpat than the system's), so pip, and the ESP-IDF installer
# with it, fail. ESP-IDF uses uv's Python 3.13 through a shim directory instead:
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
tools/idf.sh set-target esp32s3            # once per clone (planned, M0 Task 5)
tools/idf.sh build                         # (planned, M0 Task 5)
ls /dev/cu.usbmodem*                       # the board's USB-Serial-JTAG port
tools/idf.sh -p /dev/cu.usbmodemXXXX flash # (planned, M0 Task 5)
tools/idf.sh exec python tools/devlog.py --reset --until "reflbo ready" -t 20 -o captures/boot.log   # (planned, M0 Task 4)
tools/idf.sh exec python tools/devlog.py --cmd version --cmd heap                                   # (planned, M0 Task 4)
tools/idf.sh exec python tools/screenshot.py -o captures/screen.png                                 # (planned, M1)
cmake -S test/host -B build-host -G Ninja && cmake --build build-host \
  && ctest --test-dir build-host --output-on-failure                                                # (planned, M0 Task 3)
```

- `devlog.py` picks the port itself when exactly one `/dev/cu.usbmodem*` exists; otherwise pass `-p`. Exit codes: 0 ok, 2 port problem, 3 console prompt never appeared, 4 `--until` not seen in time.
- Do not run `idf.py monitor` from an agent shell; it needs an interactive TTY. Use `devlog.py`.
- Do not run `idf.py erase-flash` or erase NVS without asking. Either wipes the owner's Wi-Fi credentials and presets.
- If the port is missing, the board is probably in deep sleep. Press KEY. If it is still missing, ask the owner to enter download mode (hold BOOT while powering on).
- Power measurements follow the USB power-meter method in spec §9.4. Record them in `docs/power.md`.

## 7. Verification

Use the cheapest level that proves the change. Any UI change needs at least level 3.

1. **Build**: `idf.py build` passes with no new warnings.
2. **Host**: unit tests and golden render tests in `test/host/`. Look at the rendered PNGs.
3. **Device**: flash, capture the boot log, exercise the change through the console, then take a screenshot and look at it.
4. **Owner**: only for physical facts, such as panel orientation and contrast, audio, how the buttons behave, and current draw. Give an exact checklist with expected results.

**Screenshots** *(planned, M1)*: the `screenshot` console command prints the canonical framebuffer as base64 PBM between `-----BEGIN RLCD PBM-----` and `-----END RLCD PBM-----`. `tools/screenshot.py` turns that into a PNG using only pyserial and the standard library. The web UI serves `/api/screenshot.bmp`. A screenshot shows what the firmware drew, not what the panel shows, because the ST7305 is write-only. After any display-driver change, have the owner confirm the test pattern.

**Diagnostics console** *(planned, `diag`; full list in spec §15)*: `screenshot`, `btn <key|boot> <short|double|long>` (simulated presses), `sensors`, `battery`, `rtc get|set`, `field list|get|set`, `preset list|set`, `wifi status|scan`, `sync now`, `sleep stats`, `power idle <deep|light>`, `audio tone`. Drive the UI with `btn` and `screenshot` instead of asking the owner to press buttons. Inject test data with `field set`.

**Done** means: the acceptance criteria pass at the right level, new logic has tests, power-affecting changes have measurements in `docs/power.md`, and this file and `docs/` are updated.

## 8. Conventions

**Code**

- ESP-IDF style: 4-space indent and `snake_case`. Public APIs carry a component prefix (`st7305_…`, `ds_…`) and live in `include/`.
- Return `esp_err_t` and use `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`. No `ESP_ERROR_CHECK` on recoverable paths such as network, SD or sensors.
- Large buffers go in PSRAM (`MALLOC_CAP_SPIRAM`); DMA buffers go in internal RAM. No allocation inside render or audio hot loops.
- One `TAG` per module. `ESP_LOGI` for state changes, `ESP_LOGD` for detail. Never log secrets.
- Every task gets an explicit stack size, priority and core. Document who owns each shared resource (I²C bus, SPI).
- Compile-time defaults come from Kconfig (`REFLBO_*`); runtime settings in NVS or LittleFS override them.
- Dependencies come from the ESP Component Registry through `idf_component.yml`, with pinned versions. Commit `dependencies.lock`. Never edit `managed_components/`.
- Change configuration through `sdkconfig.defaults`, then run `idf.py reconfigure` or delete `sdkconfig`. `sdkconfig` is generated and gitignored. Personal overrides, such as dev Wi-Fi credentials, go in the gitignored `sdkconfig.defaults.local`.
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
| D3 | The idle strategy (deep or light sleep) is chosen from USB power-meter measurements at M2 |
| D4 | English UI, structured as language packs |
| D5 | Home Assistant over MQTT |
| D6 | Power is best effort; an average below 2 mA is the stretch goal |
| D7 | Data from other local devices arrives over MQTT |
| D8 | Open source with credit: Apache-2.0 (confirmed) |
| D9 | No RTC backup cell is fitted now (one can be added later); firmware must work without it |
| D10 | Extra features are discussed at the relevant milestone before they are built |
| D11 | The sync schedule (`times` / `interval` / `always` / `manual`) and the display update interval are both configurable |
