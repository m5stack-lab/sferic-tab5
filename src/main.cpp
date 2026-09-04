// sferic-tab5 bring-up spike (ADR-0013 §2) — v3: the C6 radio.
//
// v1/v2 established: P4 toolchain + M5Unified work; display/touch/IMU/PSRAM
// pass; touching Wi-Fi crashed because esp_hosted used the Espressif-devkit
// SDIO pins, not the Tab5's.
//
// The fix (arduino-esp32 3.2.1, PR #11513): WiFi.setPins(clk,cmd,d0..d3,rst)
// with the Tab5's SDIO2 -> C6 wiring, called before any Wi-Fi use.
//   CLK 12  CMD 13  D0 11  D1 10  D2 9  D3 8  RST 15
//
// This spike: bring the C6 up, scan, then the real ADR §2 question —
// does *promiscuous / monitor mode* work over esp_hosted?

#include <M5Unified.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Tab5 SDIO2 -> ESP32-C6
static constexpr int C6_CLK = 12, C6_CMD = 13, C6_D0 = 11, C6_D1 = 10, C6_D2 = 9,
                     C6_D3 = 8, C6_RST = 15;

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

volatile uint32_t g_pkts = 0;
void promiscCb(void*, wifi_promiscuous_pkt_type_t) {
  g_pkts = g_pkts + 1;
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

  Serial.println("\n\n=== sferic-tab5 bring-up spike v3 — C6 radio (ADR-0013) ===");
  Serial.printf("chip %s rev %d, %d cores @ %d MHz, arduino %s\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                getCpuFrequencyMhz(), ESP_ARDUINO_VERSION_STR);
  M5.Display.printf("sferic-tab5 spike v3 (C6 radio)\narduino %s\n\n",
                   ESP_ARDUINO_VERSION_STR);

  line("psram", ESP.getPsramSize() ? "PASS" : "FAIL", "%u MB",
       (unsigned)(ESP.getPsramSize() / (1024 * 1024)));
  line("display", "PASS", "%dx%d", M5.Display.width(), M5.Display.height());

  // --- C6 over esp_hosted / SDIO2 ---
  const bool pinsOk = WiFi.setPins(C6_CLK, C6_CMD, C6_D0, C6_D1, C6_D2, C6_D3, C6_RST);
  line("c6-pins", pinsOk ? "PASS" : "FAIL", "clk12 cmd13 d0-3=11/10/9/8 rst15");

  WiFi.mode(WIFI_STA);
  delay(300);
  const uint8_t* mac = WiFi.macAddress().length() ? nullptr : nullptr;
  (void)mac;
  line("c6-link", WiFi.getMode() == WIFI_STA ? "PASS" : "FAIL", "sta mode, mac %s",
       WiFi.macAddress().c_str());

  const int n = WiFi.scanNetworks(false, true);
  if (n >= 0) {
    char top[64] = "-";
    if (n > 0)
      snprintf(top, sizeof(top), "'%s' %ddBm ch%d", WiFi.SSID(0).c_str(),
               WiFi.RSSI(0), WiFi.channel(0));
    line("wifi-scan", "PASS", "%d APs, top %s", n, top);
  } else {
    line("wifi-scan", "FAIL", "scanNetworks -> %d", n);
  }

  // --- THE GATE: monitor mode over esp_hosted ---
  esp_err_t e1 = esp_wifi_set_promiscuous(true);
  esp_err_t e2 = esp_wifi_set_promiscuous_rx_cb(&promiscCb);
  if (e1 != ESP_OK || e2 != ESP_OK) {
    line("monitor", "FAIL", "set_promiscuous=0x%x set_rx_cb=0x%x", e1, e2);
  } else {
    g_pkts = 0;
    const uint8_t chans[] = {1, 6, 11};
    for (uint8_t c : chans) {
      esp_wifi_set_channel(c, WIFI_SECOND_CHAN_NONE);
      delay(1200);
    }
    esp_wifi_set_promiscuous(false);
    line("monitor", g_pkts > 0 ? "PASS" : "FAIL", "%lu frames in 3.6s on ch 1/6/11",
         (unsigned long)g_pkts);
  }

  Serial.println("=== spike v3 done ===");
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
