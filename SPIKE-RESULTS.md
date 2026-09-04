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
| **wifi raw TX** | ❌ NOT SUPPORTED (v4) | `esp_wifi_80211_tx()` → `0x106` `ESP_ERR_NOT_SUPPORTED`. No frame injection over `esp_hosted` either. |
| **BLE** | ❌ off (v4) | `CONFIG_BT_ENABLED` is **not set** in the stock pioarduino P4 libs — BLE isn't compiled in. Needs a custom Arduino-as-IDF-component build. |
| **802.15.4 / Thread / Zigbee** | ❌ off (v4) | `CONFIG_OPENTHREAD_ENABLED` not set; 15.4-over-`esp_hosted` isn't a supported transport regardless. Custom firmware, and possibly not feasible at all. |
| **microSD** | ❌ FAIL (v4) | SDMMC pins CLK43/CMD44/D0-3=39-42 are right, but the card slot power is an **on-chip LDO** (not GPIO 45), and `esp_ldo_acquire_channel(109)` reports "already in use" → rail never comes up → `send_op_cond 0x107`. Fixable with proper `sd_pwr_ctrl` LDO config; a P4 power-domain detail. |

## Radio capability — final (stock pioarduino P4 tooling)

| Capability | Works? |
|---|---|
| Wi-Fi STA connect / scan | ✅ |
| Wi-Fi SoftAP (captive portal) | ✅ (same API path; untested but expected) |
| Wi-Fi monitor / promiscuous | ❌ `ESP_ERR_NOT_SUPPORTED` |
| Wi-Fi raw 802.11 TX | ❌ `ESP_ERR_NOT_SUPPORTED` |
| BLE | ❌ compiled out (`CONFIG_BT_ENABLED`) |
| 802.15.4 / Thread / Zigbee | ❌ compiled out; not an `esp_hosted` transport |

Everything in the ❌ rows needs a **custom Arduino-as-IDF-component build**
with `esp_hosted` extended config — and even then, monitor / raw-TX are
`esp_hosted` *protocol* limits, not just config, so they likely stay
unavailable.

## Takeaways

1. **The P4 + M5Unified path is viable.** Display, touch, IMU, PSRAM all
   work with no custom driver work — the UI kit + HAL port (ADR-0013 §3/§4)
   is unblocked.
2. **The Tab5 is not a radio station.** With stock tooling the C6 gives you
   `wifi.scan` and `wifi.portal` — nothing else. It is a *worse* radio node
   than a StickS3 + RF Pack in every dimension.
3. **The Tab5's fleet value is the camera, USB host, big touch screen +
   keyboard, and compute** (dual RISC-V, 32 MB PSRAM) — not radios. Its
   toolsets are `camera`, `usbhost`, `canbus`/`rs485`, plus `wifi.scan` /
   `wifi.portal`. The operator-console role (ADR-0013 §8) matters more given
   the thin radio story.
4. **This needs an ADR-0013 amendment** — §6's toolset table changes
   materially (drop `thread`/`ieee802154` and `ble` from the initial scope;
   narrow `wifi` to scan/AP).

## Camera — PASS (spike `[env:cam]`, 2026-09-03)

Framework decision (ADR-0013 §2): the camera needs `esp_video`, an ESP-IDF
component → `[env:cam]` is **`framework = espidf`**, pioarduino platform
`55.03.38-1` (IDF 5.5.4), board `m5stack-tab5-p4` (vendored). We replicate
esp-bsp's `bsp_camera_start()` by hand (I2C bus 1 + PI4IOE5V6408 `0x43` pin 6
to power the camera + `esp_video_init` MIPI-CSI) to avoid dragging in the
BSP's LVGL/display stack, which currently wants a newer IDF than the
platform ships.

| Step | Result |
|---|---|
| toolchain (espidf + esp_video) | ✅ builds + flashes |
| camera power (IO expander `0x43` pin 6) | ✅ `ESP_OK` |
| SC202CS sensor detect | ✅ `PID=0xeb52` on I2C |
| `esp_video_init` + MIPI-CSI | ✅ `ESP_OK` |
| V4L2 stream | ✅ **1280×720 RGB565**, 1800 KB/buf |
| frame rate | ✅ steady **30.0 fps** |
| ISP / auto-exposure | ✅ after the `fix_esp_ipa.py` workaround — `lum avg` 7 → ~51, real dynamic range |
| CCM (colour matrix) | ⚠️ one SC202CS IPA coefficient (4.011) exceeds the ISP's ±4.0 → CCM rejected once at init; capture unaffected, colours a bit flat. Fixable in a custom IPA JSON. |

Gotchas hit (all solved, documented in `platformio.ini` / this repo):
- PlatformIO Core **6.2.0b2** breaks the ESP-IDF builder (`SCons.Tool.
  FortranCommon` gone in SCons 4.9) → use **stable 6.1.19** on Python 3.12
  for `[env:cam]`. (6.2 was only needed for the 54.x pioarduino postinstall,
  which is a one-time thing.)
- `<M5Unified.h>` in the same TU as the BSP clashes (`i2c_config_t` redef:
  IDF i2c driver vs `espressif__i2c_bus`) → the spike is pure IDF, no
  M5Unified. The display path (M5GFX vs BSP LCD) is an ADR-0013 §4 item.
- `espressif/m5stack_tab5` (any version) pulls `esp_lvgl_port ^2` which
  needs IDF > 5.5.4 (`on_frame_buf_complete`) → don't use the full BSP;
  depend on `esp_video` + `esp_io_expander_pi4ioe5v6408` directly.
- `esp_ipa`'s prebuilt lib ships a placeholder `esp_ipa_pipeline_get_config`
  that shadows the generated SC202CS one → `extra_scripts/fix_esp_ipa.py`
  strips it pre-link (from amcchord/M5Tab5-VideoLink).

## Next actions

- [x] ADR-0013 amendment: Tab5 radio scoping.
- [x] Camera bring-up → PASS at 720p30.
- [ ] CCM clamp: a custom SC202CS IPA JSON with `[2][2] ≤ 4.0`.
- [ ] microSD LDO power config (`sd_pwr_ctrl` on-chip LDO) — store-and-forward (§7).
- [ ] Keyboard: trace the M-Bus header / test USB-HID host enumeration.
- [ ] Resolve the hybrid question: can Arduino/M5Unified + `esp_video`
      coexist (own TUs / drop `espressif__i2c_bus`), or does the Tab5
      firmware go pure-IDF and render `ui_kit` through the BSP LCD path?
      (ADR-0013 §4.)
- [ ] Copy the board-agnostic sources + a Tab5 `board_profile.h`; wire
      `dispatch` + WS `ws_station`; declare `wifi`(scan) + `camera` toolsets
      to the collector (no collector changes needed — ADR-0013 §8).
