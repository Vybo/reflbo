# Power measurements

Measured by the owner with a USB power meter, following spec §9.4:

- Power the board from a USB charger, not a PC. With no USB host, the firmware follows its sleep policy.
- Use a fully charged 18650, or remove it, so charging current doesn't distort the reading.
- Read the meter's mAh counter over at least 1 h per scenario.
- Battery-side current ≈ 5 V current × 5 V / V_bat. This ignores converter losses.

Many USB meters are inaccurate below 1 mA, so the long windows matter.

Baseline to beat: the vendor factory firmware draws about 90 mA at 5.3 V (AGENTS.md gotcha 12).

## Firmware facts that shape the numbers

Firmware at M2 (4704a19), measured with `sleep test` and `sleep stats` on 2026-09-28. These are tethered samples, from two awake phases per test, on wakes that push a new minute but sample no sensors:
- **Deep sleep:** 63 ms of app time per routine wake, plus ROM and bootloader time, which esp_timer doesn't see. Before the review fixes it was 168 ms: every wake started the USB console, initialised NVS and printed about 45 start-up log lines. Routine wakes (the RTC alarm or its backup timer) now skip all three.
- **Light sleep:** 57–58 ms per wake. The console runs from the cold boot; while no PC is attached its REPL polls every 10 ms during awake phases, which costs little.
- About 38 ms of either figure is the full-frame push (M1: per-pixel conversion from PSRAM plus SPI at 10 MHz). Every fifth wake also samples the SHTC3 and the battery, which adds about 40 ms (103 ms for such a deep wake).
- One wake per minute: the display updates every minute, and the sensors are sampled on every fifth.
- The panel stays in LPM at 1 Hz throughout.
- Full cycles slept 60.0 s, so nothing woke the board early.

## Results

| Date | Commit | Scenario | Settings | Meter | Window | mAh | Mean at 5 V | Est. battery current | Notes |
|---|---|---|---|---|---|---|---|---|---|
