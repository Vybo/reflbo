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

## The hardware floor

The TPS63020 that makes the 3V3 rail has PS/SYNC tied high, which forces PWM and disables its power-save mode (AGENTS.md gotcha 23). TI's efficiency curve for that mode (datasheet Figure 9) is about 1–2 % at 0.1 mA and about 10 % at 1 mA, so the converter burns tens of mW even when the 3V3 load is almost nothing. That loss is there in every mode, deep sleep included, and on battery as well as on USB. Two deep-sleep runs, with and without the battery, both drew about 60 mW (11.4–11.5 mA at 5.2 V), while the chip was awake 0.1 % of the time. Deep and light sleep differ by less than that floor, so compare them in absolute mA.

## Results

| Date | Commit | Scenario | Settings | Meter | Window | mAh | Mean at 5 V | Est. battery current | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 2026-09-28 | 4704a19 | Deep sleep, idle | `power idle deep`; display every 1 min, sensors every 5 min; LPM 1 Hz; 18650 removed; wall charger | Fnirsi FNB58 | 1 h 55 min | 21.78 (0.114 Wh) | 11.4 mA at 5.23 V (meter: a fairly steady 11.18 mA) | — | The clock was invalid throughout. With no battery, moving the cable cut the power, which stopped the RTC, so the screen showed "Set time" and most wakes pushed no frame. Also includes the charger running with no battery. The battery estimate waits for the USB-path overhead (PWR-off reading) |
| 2026-09-28 | ffbf55c | Deep sleep, idle | `power idle deep`; display every 1 min, sensors every 5 min; LPM 1 Hz; 18650 in and charged (CHG LED off throughout); wall charger | Fnirsi FNB58 | 58 min | 11.14 (0.0583 Wh) | 11.5 mA at 5.24 V | — | Valid clock. The board's stats over 88 min (including this window): 86 deep sleeps, wakes rtc 85 and key 1, mean awake 71 ms, mean sleep 59.4 s. So the chip was awake 0.12 % of the time, and the ~60 mW floor comes from the hardware. Most of it is likely the 3V3 converter running in forced PWM (AGENTS.md gotcha 23) |
