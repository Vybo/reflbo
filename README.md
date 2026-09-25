# reflbo

Firmware for the Waveshare [ESP32-S3-RLCD-4.2](https://docs.waveshare.com/ESP32-S3-RLCD-4.2): a battery-powered desk display with watch-face-like dashboards (time, indoor climate, weather, sun times, Home Assistant values), alarms and internet radio.

**Status:** early development. See the roadmap in [AGENTS.md](AGENTS.md) and the design in [docs/specs](docs/specs/2026-09-25-firmware-design.md).

## Build and flash

Requires ESP-IDF v5.5.5. Setup is described in [AGENTS.md §6](AGENTS.md).

```sh
tools/idf.sh set-target esp32s3   # once per clone
tools/idf.sh build
tools/idf.sh -p /dev/cu.usbmodemXXXX flash
tools/idf.sh exec python tools/devlog.py --cmd version
```

Host tests:

```sh
cmake -S test/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host
```

## Licence

Apache-2.0. See [LICENSE](LICENSE), [NOTICE](NOTICE) and [THIRD_PARTY.md](THIRD_PARTY.md).
