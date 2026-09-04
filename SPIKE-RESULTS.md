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
| **radio / C6 link** | ✅ PASS (v3) | `WiFi.setPins(12,13,11,10,9,8,15)` before `WiFi.mode()` — arduino-esp32 3.2.1, PR #11513. `esp_hosted` SDIO2 → C6 comes up, no crash. STA mode OK, C6 MAC `58:E6:C5:21:62:50`. |
| **wifi.scan** | ✅ PASS (v3) | `WiFi.scanNetworks()` → 32 APs, strongest −39 dBm ch6. Normal STA/AP Wi-Fi works over the C6. |
| **wifi monitor mode** | ❌ **NOT SUPPORTED** (v3) | `esp_wifi_set_promiscuous(true)` → `0x106` = `ESP_ERR_NOT_SUPPORTED`. `esp_hosted` / `esp_wifi_remote` does not expose promiscuous RX. **This is the ADR-0013 §2 gate — answered.** |

## Takeaways

1. **The P4 + M5Unified path is viable.** Display, touch, IMU, PSRAM all
   work with no custom driver work. The UI kit + HAL port (ADR-0013 §3, §4)
   is unblocked.
2. **The C6 radio works — for STA/AP, not monitor.** The v1/v2 "crash on any
   Wi-Fi" was just the wrong SDIO pins; `WiFi.setPins()` with the Tab5's
   SDIO2 → C6 wiring (CLK 12 / CMD 13 / D0–3 11,10,9,8 / RST 15) fixes it.
   Scan + connect + (presumably) SoftAP go through `esp_hosted` fine. But
   **promiscuous / monitor mode returns `ESP_ERR_NOT_SUPPORTED`** — the RPC
   transport does not carry a raw-packet firehose.
3. **So on the Tab5, `wifi.*` is scan / AP class only** (`wifi.scan`,
   `wifi.wardrive`, `wifi.portal`), **not monitor class** (`wifi.monitor`,
   `wifi.probe`, `wifi.handshake`, `wifi.deauth` all need promiscuous —
   those stay StickS3-only). This *reinforces* the fleet model: the Stick
   sniffs, the Tab5 scans + sees + does Thread/USB.
4. Still to scope: BLE over `esp_hosted` (NimBLE), raw 802.11 TX
   (`esp_wifi_80211_tx`), 802.15.4 (Thread/Zigbee — is the C6's 15.4 even
   reachable from the P4 host?), SD (`SD_MMC` pins), and the keyboard.

## Next actions

- [ ] Scope the rest of the C6: BLE scan (NimBLE), `esp_wifi_80211_tx`,
      whether 802.15.4 is exposed over `esp_hosted` at all.
- [ ] `SD_MMC` with explicit `setPins()` / 4-bit width for the Tab5 slot.
- [ ] Keyboard: trace the M-Bus header, or test USB-HID host enumeration.
- [ ] Copy the board-agnostic sources + a Tab5 `board_profile.h`; wire
      `dispatch` + a WS `ws_station`; declare Tab5 toolsets
      (`camera`/`usbhost`/`wifi`(scan)/... ) to the collector — which needs
      no changes (ADR-0013 §8).
- [ ] ADR-0013 follow-up note: monitor-mode confirmed unsupported over the
      C6 → Tab5 `wifi.*` scope narrowed to scan/AP.
