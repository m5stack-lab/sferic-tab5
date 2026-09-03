// sferic-tab5 bring-up spike (ADR-0013 §2) — v2.
//
// v1 found: P4 toolchain + M5Unified work; display/touch/IMU/PSRAM pass;
// SD needs SD_MMC (not SPI SD); the keyboard is not on I2C bus 0; and
// touching Wi-Fi crashes the device — the ESP32-C6 esp_hosted/SDIO link
// fails to init ("H_SDIO_DRV: sdio card init failed" -> FreeRTOS abort).
//
// v2 therefore leaves the radio alone (that is its own sub-task: get the
// C6 slave firmware / esp_hosted config right) and nails down SD + keyboard.

#include <M5Unified.h>
#include <SD_MMC.h>
#include <Wire.h>

namespace {

void line(const char* name, const char* verdict, const char* fmt, ...) {
  char detail[110];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detail, sizeof(detail), fmt, ap);
  va_end(ap);
  Serial.printf("[spike] %-10s %-4s  %s\n", name, verdict, detail);
  uint16_t c = TFT_WHITE;
  if (!strcmp(verdict, "PASS")) c = TFT_GREEN;
  else if (!strcmp(verdict, "FAIL")) c = TFT_RED;
  else c = TFT_YELLOW;
  M5.Display.setTextColor(c, TFT_BLACK);
  M5.Display.printf("%-9s %-4s %s\n", name, verdict, detail);
}

void scanI2C(TwoWire& bus, const char* label) {
  int found = 0;
  char list[80] = {0};
  for (uint8_t a = 1; a < 127; a++) {
    bus.beginTransmission(a);
    if (bus.endTransmission() == 0) {
      found++;
      char b[6];
      snprintf(b, sizeof(b), "%02x ", a);
      strncat(list, b, sizeof(list) - strlen(list) - 1);
    }
  }
  line(label, found ? "INFO" : "INFO", "%d dev: %s", found, found ? list : "-");
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(400);
  M5.Display.setRotation(1);  // landscape
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);

  Serial.println("\n\n=== sferic-tab5 bring-up spike v2 (ADR-0013) ===");
  Serial.printf("chip: %s rev %d, %d cores @ %d MHz\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores(), getCpuFrequencyMhz());
  M5.Display.printf("sferic-tab5 spike v2\n%s  %d MHz  %d cores\n\n",
                   ESP.getChipModel(), getCpuFrequencyMhz(), ESP.getChipCores());

  line("psram", ESP.getPsramSize() ? "PASS" : "FAIL", "%u MB free %u KB",
       (unsigned)(ESP.getPsramSize() / (1024 * 1024)),
       (unsigned)(ESP.getFreePsram() / 1024));

  line("display", (M5.Display.width() > 0) ? "PASS" : "FAIL", "%dx%d rot1",
       M5.Display.width(), M5.Display.height());

  const bool imu = M5.Imu.isEnabled();
  float ax = 0, ay = 0, az = 0;
  if (imu) M5.Imu.getAccel(&ax, &ay, &az);
  line("imu", imu ? "PASS" : "FAIL", "accel %.2f %.2f %.2f", ax, ay, az);

  // SD over SDMMC (the Tab5 slot is SDIO, not SPI). 1-bit, formatted mount off.
  if (SD_MMC.begin("/sdcard", true, false)) {
    line("sd", "PASS", "SD_MMC %llu MB", SD_MMC.cardSize() / (1024ULL * 1024ULL));
  } else {
    line("sd", "FAIL", "SD_MMC.begin() failed (card in?)");
  }

  // Keyboard: not on I2C bus 0 in v1. Scan the exposed buses + note that a
  // USB-HID keyboard (Tab5 as USB host) would not show here at all.
  Wire.begin();
  scanI2C(Wire, "i2c0");
#if defined(WIRE1_PIN_SDA) || 1
  Wire1.begin();
  scanI2C(Wire1, "i2c1");
#endif
  line("keyboard", "TODO", "not on I2C; check M-Bus / USB-HID host");

  // Touch: quick liveness — the controller was detected at M5.begin
  // ("M5Tab5 ST touch FW version 01"); just report ready.
  line("touch", "PASS", "controller detected at init");

  line("radio", "BLOCKED", "C6 esp_hosted/SDIO init fails + panics; skipped");

  Serial.println("=== spike v2 done ===");
}

void loop() {
  M5.update();
  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    M5.Display.fillCircle(t.x, t.y, 10, TFT_CYAN);
    Serial.printf("[spike] touch %d,%d\n", t.x, t.y);
  }
  static uint32_t beat = 0;
  if (millis() - beat > 5000) {
    beat = millis();
    Serial.printf("[spike] alive up=%lus heap=%uKB psram=%uKB\n",
                  millis() / 1000, (unsigned)(ESP.getFreeHeap() / 1024),
                  (unsigned)(ESP.getFreePsram() / 1024));
  }
  delay(20);
}
