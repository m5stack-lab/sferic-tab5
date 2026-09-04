// sferic-tab5 bring-up spike (ADR-0013 §2) — v4: finish scoping the radio + SD.
//
// Settled so far: toolchain OK; display/touch/IMU/PSRAM OK; C6 Wi-Fi link OK
// via WiFi.setPins(12,13,11,10,9,8,15); STA + scan OK; monitor mode =
// ESP_ERR_NOT_SUPPORTED over esp_hosted.
//
// v4 checks: raw 802.11 TX (esp_wifi_80211_tx), and the microSD slot on
// SDMMC pins CLK43/CMD44/D0-3=39/40/41/42 (power pin 45, active LOW).
// BLE and 802.15.4/Thread are compile-time OFF in the stock pioarduino P4
// libs (CONFIG_BT_ENABLED / CONFIG_OPENTHREAD_ENABLED not set) — noted, not
// probed here.

#include <M5Unified.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <esp_wifi.h>

static constexpr int C6_CLK = 12, C6_CMD = 13, C6_D0 = 11, C6_D1 = 10, C6_D2 = 9,
                     C6_D3 = 8, C6_RST = 15;
static constexpr int SD_CLK = 43, SD_CMD = 44, SD_D0 = 39, SD_D1 = 40,
                     SD_D2 = 41, SD_D3 = 42, SD_PWR = 45;

namespace {
void line(const char* name, const char* verdict, const char* fmt, ...) {
  char d[120];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(d, sizeof(d), fmt, ap);
  va_end(ap);
  Serial.printf("[spike] %-10s %-4s  %s\n", name, verdict, d);
  uint16_t c = !strcmp(verdict, "PASS")   ? TFT_GREEN
               : !strcmp(verdict, "FAIL") ? TFT_RED
                                          : TFT_YELLOW;
  M5.Display.setTextColor(c, TFT_BLACK);
  M5.Display.printf("%-9s %-4s %s\n", name, verdict, d);
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(400);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  Serial.println("\n\n=== sferic-tab5 spike v4 — radio TX + SD (ADR-0013) ===");
  M5.Display.printf("sferic-tab5 spike v4\n\n");

  // --- microSD on SDMMC ---
  pinMode(SD_PWR, OUTPUT);
  digitalWrite(SD_PWR, LOW);  // power on (active LOW per variant)
  delay(50);
  bool sd = SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  if (sd && SD_MMC.begin("/sdcard", false /*1bit? no, 4-bit*/, false)) {
    line("sd-4bit", "PASS", "%llu MB, type %d",
         SD_MMC.cardSize() / (1024ULL * 1024ULL), (int)SD_MMC.cardType());
  } else {
    SD_MMC.end();
    sd = SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0) &&
         SD_MMC.begin("/sdcard", true /*1-bit*/, false);
    line("sd", sd ? "PASS" : "FAIL",
         sd ? "1-bit %llu MB" : "both 4-bit and 1-bit failed",
         sd ? SD_MMC.cardSize() / (1024ULL * 1024ULL) : 0ULL);
  }

  // --- C6 Wi-Fi (regression) ---
  WiFi.setPins(C6_CLK, C6_CMD, C6_D0, C6_D1, C6_D2, C6_D3, C6_RST);
  WiFi.mode(WIFI_STA);
  delay(300);
  const int n = WiFi.scanNetworks(false, true);
  line("wifi-scan", n >= 0 ? "PASS" : "FAIL", "%d APs", n);

  // --- raw 802.11 TX over esp_hosted ---
  // A minimal well-formed 802.11 probe-request frame.
  uint8_t frame[] = {
      0x40, 0x00, 0x00, 0x00,                          // FC (probe req), dur
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff,              // addr1 broadcast
      0x58, 0xe6, 0xc5, 0x21, 0x62, 0x50,              // addr2 (this C6)
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff,              // addr3
      0x00, 0x00,                                      // seq
      0x00, 0x00,                                      // SSID tag, len 0
  };
  esp_err_t tx = esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
  line("raw-tx", tx == ESP_OK ? "PASS" : "FAIL",
      "esp_wifi_80211_tx -> 0x%x (%s)", tx,
      tx == ESP_OK ? "sent" : esp_err_to_name(tx));

  line("ble", "N/A", "CONFIG_BT_ENABLED off in stock P4 libs");
  line("thread", "N/A", "CONFIG_OPENTHREAD_ENABLED off; 15.4 not over hosted");

  Serial.println("=== spike v4 done ===");
}

void loop() {
  M5.update();
  static uint32_t beat = 0;
  if (millis() - beat > 5000) {
    beat = millis();
    Serial.printf("[spike] alive up=%lus heap=%uKB\n", millis() / 1000,
                  (unsigned)(ESP.getFreeHeap() / 1024));
  }
  delay(20);
}
