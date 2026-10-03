# sferic-tab5

**sferic** fleet-station firmware for the **M5Stack Tab5** (ESP32-P4).
Plan of record: [`sferic-sticks3` ADR-0013](https://github.com/m5stack-lab/sferic-sticks3/blob/main/docs/adr/0013-tab5-station.md).
Pairs with [`sferic-collector`](https://github.com/m5stack-lab/sferic-collector)
(the C2 server) and [`sferic-sticks3`](https://github.com/m5stack-lab/sferic-sticks3)
(the StickS3 firmware — where the board-agnostic code lives).

## Status: bring-up spike

Only `src/main.cpp` — the ADR-0013 §2 bring-up spike. It answers "what
works on this board" before any tool or UI code is ported. Results:
[`SPIKE-RESULTS.md`](SPIKE-RESULTS.md).

- ✅ toolchain (pioarduino + M5Unified compiles + flashes for ESP32-P4)
- ✅ display / touch / IMU / 32 MB PSRAM
- ⚠️ SD — `SD_MMC` times out (card? pin config?)
- ⚠️ keyboard — not on either I2C bus; connection method TBD
- ⚠️ **radio (ESP32-C6)** — link works with `WiFi.setPins()`; STA + scan OK,
  but **monitor mode is `ESP_ERR_NOT_SUPPORTED`** over esp_hosted (ADR §2
  gate — answered). Tab5 wifi.* = scan/AP class only.

## Toolchain

The ESP32-P4 needs the **pioarduino** platform fork (the official
PlatformIO `espressif32` has no P4 support), and pioarduino's build scripts
+ bundled esptool v5 need **PlatformIO Core ≥ 6.2** on **Python ≥ 3.10**.

```sh
# isolated PlatformIO on Python 3.12 (keeps the StickS3's 3.9 pio untouched)
uv tool install --python 3.12 platformio
~/.local/share/uv/tools/platformio/bin/python -m pip install -U \
  https://github.com/platformio/platformio-core/archive/develop.zip   # 6.2.0b2
~/.local/share/uv/tools/platformio/bin/python -m pip install 'click<8.2'  # esptool compat

~/.local/bin/pio run -e spike            # build
~/.local/bin/pio run -e spike -t upload  # flash
~/.local/bin/pio device monitor          # serial
```

## Next

1. **Unblock the C6** — get `esp_hosted` / the C6 slave firmware talking
   over SDIO, then re-run the monitor-mode probe (ADR-0013 §2).
2. Sort SD (`SD_MMC` pins / peripheral) and the keyboard connection.
3. Copy the board-agnostic sources from `sferic-sticks3` (ui_kit, theme,
   registry, dispatch, link, transports) + a Tab5 `board_profile.h`
   (1280×720, touch + keyboard) — ADR-0013 §1, §4.
4. New toolset specs: `docs/mcp/{camera,thread,usbhost,canbus}.md`.

## License

MIT © 2026 ry-ops
