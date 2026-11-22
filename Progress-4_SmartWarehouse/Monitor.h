#ifndef MONITOR_H
#define MONITOR_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Config.h"
#include "SystemState.h"

struct UiSnapshot {
  uint8_t state;
  long encoderTicks;
  long targetTicks;
  long speedTicksPerSecond;
  int motorPwm;
  int8_t motorDirection; // 1=UP, -1=DOWN, 0=STOP
  int8_t page;            // -1=idle animation, 0..3=halaman detail
  bool positionValid;
  bool estopActive;
  bool buttonUp;
  bool buttonDown;
  bool buttonMode;
  bool buttonEstop;
  unsigned long lastUiActivityMs;
  char info[28];
};

struct SensorSnapshot {
  float dist1;
  float dist2;
  uint32_t ok1;
  uint32_t fail1;
  uint32_t ok2;
  uint32_t fail2;
  bool valid1;
  bool valid2;
};

class WarehouseMonitor {
 public:
  WarehouseMonitor()
      : oled_(OLED_WIDTH, OLED_HEIGHT, &Wire, -1) {}

  bool begin() {
    pinMode(PIN_TRIG1, OUTPUT);
    pinMode(PIN_ECHO1, INPUT);
    pinMode(PIN_TRIG2, OUTPUT);
    pinMode(PIN_ECHO2, INPUT);
    digitalWrite(PIN_TRIG1, LOW);
    digitalWrite(PIN_TRIG2, LOW);

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    Wire.setClock(400000);

    oledReady_ = oled_.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    if (!oledReady_ && OLED_ADDR == 0x3C) {
      // Beberapa modul SSD1306 menggunakan alamat alternatif 0x3D.
      oledReady_ = oled_.begin(SSD1306_SWITCHCAPVCC, 0x3D);
    }

    if (oledReady_) {
      oled_.clearDisplay();
      oled_.setTextColor(SSD1306_WHITE);
      oled_.setTextSize(1);
      oled_.setCursor(0, 0);
      oled_.println(F("SMART WAREHOUSE"));
      oled_.println(F("Monitor starting..."));
      oled_.display();
    }

    lastSensorMs_ = millis();
    return oledReady_;
  }

  // Dipanggil hanya dari monitorTask. pulseIn boleh menunggu, karena task ini
  // terpisah dari loop kontrol motor.
  void serviceSensors(unsigned long now) {
    if (now - lastSensorMs_ < SENSOR_INTERVAL_MS) return;
    lastSensorMs_ = now;

    const uint8_t index = nextSensor_;
    nextSensor_ ^= 1U;

    const uint8_t trig = (index == 0) ? PIN_TRIG1 : PIN_TRIG2;
    const uint8_t echo = (index == 0) ? PIN_ECHO1 : PIN_ECHO2;
    const float cm = readCm_(trig, echo);

    if (index == 0) {
      dist1_ = cm;
      valid1_ = (cm >= 0.0f);
      lastRead1Ms_ = now;
      if (valid1_) ++ok1_;
      else ++fail1_;
    } else {
      dist2_ = cm;
      valid2_ = (cm >= 0.0f);
      lastRead2Ms_ = now;
      if (valid2_) ++ok2_;
      else ++fail2_;
    }
  }

  SensorSnapshot sensorSnapshot(unsigned long now = millis()) const {
    SensorSnapshot snapshot;
    snapshot.dist1 = dist1_;
    snapshot.dist2 = dist2_;
    snapshot.ok1 = ok1_;
    snapshot.fail1 = fail1_;
    snapshot.ok2 = ok2_;
    snapshot.fail2 = fail2_;
    snapshot.valid1 = valid1_ && (now - lastRead1Ms_ <= SENSOR_MAX_AGE_MS);
    snapshot.valid2 = valid2_ && (now - lastRead2Ms_ <= SENSOR_MAX_AGE_MS);
    if (!snapshot.valid1) snapshot.dist1 = -1.0f;
    if (!snapshot.valid2) snapshot.dist2 = -1.0f;
    return snapshot;
  }

  void draw(const UiSnapshot& snapshot, unsigned long now) {
    if (!oledReady_) return;

    // Safety/operator state always takes precedence over the screensaver.
    if (snapshot.estopActive || snapshot.state == STATE_ESTOP) {
      drawAlert_(F("!! E-STOP !!"), F("Motor command disabled"), F("Release + recalibrate"));
      return;
    }

    if (snapshot.state == STATE_ERROR) {
      drawAlert_(F("SYSTEM ERROR"), snapshot.info, F("Check system / re-zero"));
      return;
    }

    if (snapshot.state == STATE_POSITION_UNREFERENCED || !snapshot.positionValid) {
      if (snapshot.page >= 0 && snapshot.page < 4) {
        drawPage_(snapshot, now);
      } else {
        drawAlert_(F("POSITION UNKNOWN"), F("Reference not valid"), F("Align floor 1, then ZERO"));
      }
      return;
    }

    if (snapshot.state == STATE_BOOT) {
      drawAlert_(F("SYSTEM STARTING"), F("Initializing modules"), F("Please wait"));
      return;
    }

    if (snapshot.state == STATE_MOVING || snapshot.state == STATE_MANUAL) {
      if (snapshot.page >= 0 && snapshot.page < 4) {
        drawPage_(snapshot, now);
      } else {
        drawMotion_(snapshot);
      }
      return;
    }

    if (snapshot.state == STATE_ARRIVED) {
      drawAlert_(F("TARGET REACHED"), F("Lift stopped"), F("System ready"));
      return;
    }

    if (snapshot.state == STATE_IDLE && snapshot.page < 0) {
      drawIdleAnimation_(now);
      return;
    }

    drawPage_(snapshot, now);
  }

  bool oledReady() const { return oledReady_; }

 private:
  Adafruit_SSD1306 oled_;
  bool oledReady_ = false;

  float dist1_ = -1.0f;
  float dist2_ = -1.0f;
  bool valid1_ = false;
  bool valid2_ = false;
  uint32_t ok1_ = 0;
  uint32_t fail1_ = 0;
  uint32_t ok2_ = 0;
  uint32_t fail2_ = 0;
  unsigned long lastRead1Ms_ = 0;
  unsigned long lastRead2Ms_ = 0;
  unsigned long lastSensorMs_ = 0;
  uint8_t nextSensor_ = 0;

  static float readCm_(uint8_t trig, uint8_t echo) {
    digitalWrite(trig, LOW);
    delayMicroseconds(2);
    digitalWrite(trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(trig, LOW);

    const unsigned long pulse = pulseIn(echo, HIGH, SENSOR_ECHO_TIMEOUT_US);
    if (pulse == 0) return -1.0f;

    const float cm = static_cast<float>(pulse) * 0.0343f * 0.5f;
    if (cm < 2.0f || cm > 400.0f) return -1.0f;
    return cm;
  }

  void header_(const __FlashStringHelper* title) {
    oled_.clearDisplay();
    oled_.setTextSize(1);
    oled_.setTextColor(SSD1306_WHITE);
    oled_.setCursor(0, 0);
    oled_.print(title);
    oled_.drawFastHLine(0, 9, OLED_WIDTH, SSD1306_WHITE);
  }

  void header_(const char* title) {
    oled_.clearDisplay();
    oled_.setTextSize(1);
    oled_.setTextColor(SSD1306_WHITE);
    oled_.setCursor(0, 0);
    oled_.print(title ? title : "MONITOR");
    oled_.drawFastHLine(0, 9, OLED_WIDTH, SSD1306_WHITE);
  }

  void footer_(const UiSnapshot& snapshot, unsigned long now) {
    oled_.drawFastHLine(0, 54, OLED_WIDTH, SSD1306_WHITE);
    oled_.setCursor(0, 56);
    if (snapshot.state == STATE_MOVING) {
      oled_.print(F("AUTO MOVING"));
    } else if (snapshot.state == STATE_MANUAL) {
      oled_.print(F("MANUAL JOG"));
    } else if (!snapshot.positionValid) {
      oled_.print(F("WARNING: POS UNKNOWN"));
    } else if (snapshot.page >= 0) {
      const unsigned long elapsed = now - snapshot.lastUiActivityMs;
      const long remaining = (elapsed >= UI_PAGE_TIMEOUT_MS)
                                 ? 0
                                 : static_cast<long>((UI_PAGE_TIMEOUT_MS - elapsed + 999) / 1000);
      oled_.printf("Idle screen in %lds", remaining);
    } else {
      oled_.print(F("MODE: change page"));
    }
  }

  void drawAlert_(const __FlashStringHelper* title,
                  const __FlashStringHelper* line1,
                  const __FlashStringHelper* line2) {
    oled_.clearDisplay();
    oled_.setTextColor(SSD1306_WHITE);
    oled_.setTextSize(1);
    oled_.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SSD1306_WHITE);
    oled_.setCursor(7, 8);
    oled_.println(title);
    oled_.drawFastHLine(7, 20, 114, SSD1306_WHITE);
    oled_.setCursor(7, 29);
    oled_.println(line1);
    oled_.setCursor(7, 44);
    oled_.println(line2);
    oled_.display();
  }

  void drawAlert_(const __FlashStringHelper* title,
                  const char* line1,
                  const __FlashStringHelper* line2) {
    oled_.clearDisplay();
    oled_.setTextColor(SSD1306_WHITE);
    oled_.setTextSize(1);
    oled_.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SSD1306_WHITE);
    oled_.setCursor(7, 8);
    oled_.println(title);
    oled_.drawFastHLine(7, 20, 114, SSD1306_WHITE);
    oled_.setCursor(7, 29);
    oled_.println((line1 && line1[0]) ? line1 : "Check system");
    oled_.setCursor(7, 44);
    oled_.println(line2);
    oled_.display();
  }

  void drawIdleAnimation_(unsigned long now) {
    const uint8_t frame = static_cast<uint8_t>((now / ANIMATION_FRAME_MS) % 16U);
    const int liftOffset = (frame < 8U) ? frame * 2 : (15U - frame) * 2;
    const int packageX = 48 + static_cast<int>((frame * 5U) % 68U);

    oled_.clearDisplay();
    oled_.setTextColor(SSD1306_WHITE);
    oled_.setTextSize(1);
    oled_.setCursor(0, 0);
    oled_.print(F("SMART WAREHOUSE"));
    oled_.fillCircle(121, 4, (frame % 4U == 0U) ? 3 : 2, SSD1306_WHITE);

    // Lift shaft and animated cabin.
    oled_.drawRoundRect(9, 15, 30, 39, 3, SSD1306_WHITE);
    oled_.drawFastVLine(14, 18, 33, SSD1306_WHITE);
    oled_.drawFastVLine(33, 18, 33, SSD1306_WHITE);
    const int cabinY = 19 + liftOffset;
    oled_.drawRect(17, cabinY, 14, 12, SSD1306_WHITE);
    oled_.drawFastHLine(20, cabinY + 4, 8, SSD1306_WHITE);
    oled_.drawFastHLine(20, cabinY + 7, 8, SSD1306_WHITE);

    // Conveyor and moving package.
    oled_.drawFastHLine(43, 49, 79, SSD1306_WHITE);
    for (int x = 46; x < 123; x += 13) {
      oled_.drawPixel(x, 52, SSD1306_WHITE);
    }
    oled_.drawRect(packageX, 37, 11, 11, SSD1306_WHITE);
    oled_.drawLine(packageX, 37, packageX + 5, 33, SSD1306_WHITE);
    oled_.drawLine(packageX + 5, 33, packageX + 11, 37, SSD1306_WHITE);

    oled_.setCursor(47, 17);
    oled_.print(F("SYSTEM READY"));
    oled_.setCursor(47, 27);
    oled_.print(F("Waiting for cmd"));
    oled_.setCursor(47, 57);
    for (uint8_t i = 0; i < 3; ++i) {
      oled_.print((i <= (frame % 4U)) ? '.' : ' ');
    }
    oled_.display();
  }

  void drawMotion_(const UiSnapshot& snapshot) {
    header_(snapshot.state == STATE_MANUAL ? F("MANUAL CONTROL") : F("LIFT MOVEMENT"));
    oled_.setCursor(0, 13);
    oled_.printf("Direction: %s", snapshot.motorDirection > 0 ? "UP" :
                                      snapshot.motorDirection < 0 ? "DOWN" : "STOP");
    oled_.setCursor(0, 25);
    oled_.printf("Position : %ld", snapshot.encoderTicks);
    oled_.setCursor(0, 36);
    oled_.printf("Target   : %ld", snapshot.targetTicks);
    oled_.setCursor(0, 47);
    oled_.printf("PWM      : %d", snapshot.motorPwm);
    oled_.display();
  }

  void drawPage_(const UiSnapshot& snapshot, unsigned long now) {
    static const char* const titles[4] = {
      "NORMAL", "INPUT", "ENCODER + MOTOR", "SENSOR"
    };
    const uint8_t page = (snapshot.page >= 0 && snapshot.page < 4)
                             ? static_cast<uint8_t>(snapshot.page) : 0U;

    header_(titles[page]);

    const SensorSnapshot sensors = sensorSnapshot(now);
    char d1[12];
    char d2[12];
    formatDistance_(d1, sizeof(d1), sensors.dist1, sensors.valid1);
    formatDistance_(d2, sizeof(d2), sensors.dist2, sensors.valid2);

    if (page == 0) {
      oled_.setCursor(0, 12);
      if (!snapshot.positionValid) oled_.print(F("Position: UNKNOWN"));
      else oled_.printf("Floor: %d", getFloor_(snapshot.encoderTicks));
      oled_.setCursor(0, 22);
      oled_.printf("Pos:%ld Tgt:%ld", snapshot.encoderTicks, snapshot.targetTicks);
      oled_.setCursor(0, 32);
      oled_.printf("Motor:%s PWM:%d", motorName_(snapshot.motorDirection), snapshot.motorPwm);
      oled_.setCursor(0, 42);
      oled_.printf("L1:%s L2:%s", d1, d2);
    } else if (page == 1) {
      oled_.setCursor(0, 12);
      oled_.printf("UP   : %s", snapshot.buttonUp ? "PRESSED" : "released");
      oled_.setCursor(0, 22);
      oled_.printf("DOWN : %s", snapshot.buttonDown ? "PRESSED" : "released");
      oled_.setCursor(0, 32);
      oled_.printf("MODE : %s", snapshot.buttonMode ? "PRESSED" : "released");
      oled_.setCursor(0, 42);
      oled_.printf("ESTOP: %s", snapshot.buttonEstop ? "ACTIVE" : "normal");
    } else if (page == 2) {
      oled_.setCursor(0, 12);
      oled_.printf("Ticks: %ld", snapshot.encoderTicks);
      oled_.setCursor(0, 22);
      oled_.printf("Speed: %ld t/s", snapshot.speedTicksPerSecond);
      oled_.setCursor(0, 32);
      oled_.printf("Target: %ld", snapshot.targetTicks);
      oled_.setCursor(0, 42);
      oled_.printf("Dir:%s PWM:%d", motorName_(snapshot.motorDirection), snapshot.motorPwm);
    } else {
      oled_.setCursor(0, 12);
      oled_.printf("L1: %s cm", d1);
      oled_.setCursor(0, 22);
      oled_.printf("ok:%lu fail:%lu", static_cast<unsigned long>(sensors.ok1),
                                      static_cast<unsigned long>(sensors.fail1));
      oled_.setCursor(0, 32);
      oled_.printf("L2: %s cm", d2);
      oled_.setCursor(0, 42);
      oled_.printf("ok:%lu fail:%lu", static_cast<unsigned long>(sensors.ok2),
                                      static_cast<unsigned long>(sensors.fail2));
    }

    footer_(snapshot, now);
    oled_.display();
  }

  static void formatDistance_(char* output, size_t size, float cm, bool valid) {
    if (!valid || cm < 0.0f) {
      snprintf(output, size, "N/A");
    } else {
      snprintf(output, size, "%.1f", static_cast<double>(cm));
    }
  }

  static const char* motorName_(int8_t direction) {
    if (direction > 0) return "UP";
    if (direction < 0) return "DOWN";
    return "STOP";
  }

  static int getFloor_(long ticks) {
    if (labs(ticks - FLOOR_1_TICKS) <= FLOOR_TOL_TICKS) return 1;
    if (labs(ticks - FLOOR_2_TICKS) <= FLOOR_TOL_TICKS) return 2;
    return 0;
  }
};

#endif // MONITOR_H
