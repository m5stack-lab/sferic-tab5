# Tab5 bring-up spike — results (ADR-0013 §2)

Run 2026-09-03 on the physical Tab5. `src/main.cpp` (v2). PlatformIO 6.2.0b2
/ Python 3.12, pioarduino platform `54.03.21`, M5Unified (latest),
`click<8.2` pinned for esptool v5 compat.

| Check | Verdict | Detail |
|---|---|---|
| **toolchain** | ✅ PASS | Arduino + M5Unified compiles, links, and flashes for `board = esp32-p4-evboard` / `mcu = esp32p4`. This was the biggest unknown. |
| chip | — | ESP32-P4 rev 103, 2× RISC-V @ 360 MHz |
| **PSRAM** | ✅ PASS | 32 MB, ~29 MB free; heap ~545 KB |
| **display** | ✅ PASS | M5GFX autodetect → `board_M5Tab5`, ST7121 driver, 1280×720 landscape (native 720×1280 portrait — needs `setRotation(1)`) |
| **touch** | ✅ PASS | "M5Tab5 ST touch FW version 01" detected at `M5.begin()`; `M5.Touch` reports points |
| **IMU** | ✅ PASS | BMI270 (I2C 0x68), accel `0.03 0.00 -1.00` flat |
| **I2C bus 1** (SDA 31 / SCL 32) | ℹ️ | 10 devices: `10 28 32 36 40 41 43 44 55 68` — IMU, touch, PMU, IO expander, audio codec, RTC, … the internal bus |
| **I2C bus 0** (SDA 53 / SCL 54) | ℹ️ | empty — the external Grove / M-Bus bus, nothing attached |
| **keyboard** | ⚠️ TODO | not on either I2C bus. The Tab5 keyboard attaches some other way (M-Bus GPIO matrix, or USB-HID with the Tab5 as USB host). Needs investigation. |
| **SD** | ⚠️ FAIL | `SD.begin()` (SPI) and `SD_MMC.begin()` both fail — `send_op_cond … 0x107` timeout, "lines need pull-ups". Either no card inserted, or the slot needs explicit SDMMC pin/width config. Minor — an API/config issue, not a hardware gap. |
| **radio / C6** | ❌ **BLOCKED** | Any Wi-Fi call brings up `esp_hosted` over SDIO to the ESP32-C6 and it **fails to init and panics** (`H_SDIO_DRV: sdio card init failed`, `sdmmc_common: send_op_cond returned 0x107`, then `FreeRTOS Task "sdio_read" should not return, Aborting now!` → reboot loop). No STA, no scan, no monitor-mode test possible. |

## Takeaways

1. **The P4 + M5Unified path is viable.** Display, touch, IMU, PSRAM all
   work with no custom driver work. The UI kit + HAL port (ADR-0013 §3, §4)
   is unblocked.
2. **The C6 radio link is the gate — and it's a real one.** A fresh
   pioarduino build does not bring up the C6 over `esp_hosted`. This needs
   its own focused pass: confirm/flash the C6 slave firmware, match the
   `esp_hosted` host/slave versions, and get the SDIO transport config
   right (M5's factory firmware does this; a from-scratch build must
   replicate it). Until then: no `wifi.*` / `ble.*` / `thread.*`, and the
   ADR §2 monitor-mode question stays open.
3. **SD + keyboard are small, separable unknowns** — chase them after (or
   alongside) the C6.

## Next actions

- [ ] C6 / `esp_hosted` bring-up (own branch) → then re-run the
      promiscuous / monitor-mode probe from spike v1.
- [ ] `SD_MMC` with explicit `setPins()` / 4-bit width for the Tab5 slot.
- [ ] Keyboard: trace the M-Bus header, or test USB-HID host enumeration.
- [ ] Once radio is sane: copy the board-agnostic sources + `board_profile.h`,
      wire `dispatch` + a WS `ws_station`, declare Tab5 toolsets to the
      collector (which needs no changes — ADR-0013 §8 consequences).
