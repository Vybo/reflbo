# Power measurements

Measured by the owner with a USB power meter, following spec §9.4:

- Power the board from a USB charger, not a PC. With no USB host, the firmware follows its sleep policy.
- Use a fully charged 18650, or remove it, so charging current doesn't distort the reading.
- Read the meter's mAh counter over at least 1 h per scenario.
- Battery-side current ≈ 5 V current × 5 V / V_bat. This ignores converter losses.

Many USB meters are inaccurate below 1 mA, so the long windows matter.

Baseline to beat: the vendor factory firmware draws about 90 mA at 5.3 V (AGENTS.md gotcha 12).

## Firmware facts that shape the numbers

Firmware at M2, measured with `sleep stats` on 2026-09-28:
- 168 ms of app time per deep-sleep wake, plus ROM and bootloader time.
- 88 ms per light-sleep wake.
- One wake per minute: the display updates every minute, and the sensors are sampled on every fifth.
- The panel stays in LPM at 1 Hz throughout.

## Results

| Date | Commit | Scenario | Settings | Meter | Window | mAh | Mean at 5 V | Est. battery current | Notes |
|---|---|---|---|---|---|---|---|---|---|
