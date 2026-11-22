#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Wi-Fi & MQTT
// Isi kredensial sebelum digunakan. Broker publik hanya untuk pengujian.
// Jangan gunakan broker publik tanpa autentikasi untuk kontrol lift produksi.
// -----------------------------------------------------------------------------
static const char* const WIFI_SSID     = "Nama_WiFi_Kamu";
static const char* const WIFI_PASSWORD = "Password_WiFi_Kamu";
static const char* const MQTT_BROKER   = "broker.hivemq.com";
static constexpr uint16_t MQTT_PORT    = 1883;
static const char* const MQTT_USERNAME = "";
static const char* const MQTT_PASSWORD = "";

static const char* const MQTT_TOPIC_COMMAND   = "topic/lift/command";
static const char* const MQTT_TOPIC_TELEMETRY = "topic/lift/telemetry";

// -----------------------------------------------------------------------------
// ESP32 DevKit WROOM pin mapping — mengikuti TestAll.ino terbaru.
// GPIO33 adalah MODE BUTTON, BUKAN bottom limit switch.
// -----------------------------------------------------------------------------
static constexpr uint8_t PIN_PWM       = 25; // L298N ENB, jumper ENB dilepas
static constexpr uint8_t PIN_IN1       = 26; // L298N IN3
static constexpr uint8_t PIN_IN2       = 27; // L298N IN4
static constexpr uint8_t PIN_ENC_A     = 34; // input-only; perlu pull-up eksternal bila diperlukan
static constexpr uint8_t PIN_ENC_B     = 35; // input-only; perlu pull-up eksternal bila diperlukan
static constexpr uint8_t PIN_ESTOP     = 32;
static constexpr uint8_t PIN_BTN_MODE  = 33; // navigasi OLED
static constexpr uint8_t PIN_BTN_UP    = 14;
static constexpr uint8_t PIN_BTN_DOWN  = 13;
static constexpr uint8_t PIN_TRIG1     = 17;
static constexpr uint8_t PIN_ECHO1     = 16;
static constexpr uint8_t PIN_TRIG2     = 18;
static constexpr uint8_t PIN_ECHO2     = 19;
static constexpr uint8_t PIN_OLED_SDA  = 21;
static constexpr uint8_t PIN_OLED_SCL  = 22;

// TestAll.ino menggunakan push button NO ke GND (aktif LOW).
// Verifikasi wiring fisik E-STOP sebelum mengaktifkan driver motor.
static constexpr uint8_t MODE_ACTIVE_LEVEL  = LOW;
static constexpr uint8_t ESTOP_ACTIVE_LEVEL = LOW;
static constexpr bool    ENCODER_INVERT     = true; // mengikuti TestAll.ino

// -----------------------------------------------------------------------------
// OLED
// -----------------------------------------------------------------------------
static constexpr uint8_t OLED_WIDTH  = 128;
static constexpr uint8_t OLED_HEIGHT = 64;
static constexpr uint8_t OLED_ADDR   = 0x3C; // beberapa modul memakai 0x3D

// -----------------------------------------------------------------------------
// Position model
// Nilai FLOOR_2_TICKS WAJIB dikalibrasi pada hardware sebenarnya.
// Tidak ada bottom limit switch pada konfigurasi hardware terbaru.
// -----------------------------------------------------------------------------
static constexpr long FLOOR_1_TICKS = 0;
static constexpr long FLOOR_2_TICKS = 750; // TODO: kalibrasi aktual
static constexpr long FLOOR_TOL_TICKS = 30;
static constexpr long TOLERANCE_TICKS = 5;
static constexpr long SOFT_LIMIT_BOTTOM_TICKS = FLOOR_1_TICKS;
static constexpr long SOFT_LIMIT_TOP_TICKS    = FLOOR_2_TICKS;

// -----------------------------------------------------------------------------
// PID — nilai awal, bukan hasil tuning final.
// -----------------------------------------------------------------------------
static constexpr float PID_KP = 0.6f;
static constexpr float PID_KI = 0.05f;
static constexpr float PID_KD = 0.2f;
static constexpr int PID_MIN_PWM = 55;
static constexpr int PID_MAX_PWM = 220;

// -----------------------------------------------------------------------------
// Motion & safety limits
// Tanpa limit switch fisik, software limit bukan pengganti limit hardware.
// -----------------------------------------------------------------------------
static constexpr int PWM_MANUAL = 120;
static constexpr unsigned long MOVE_TIMEOUT_MS = 8000;
static constexpr unsigned long MAX_MANUAL_JOG_MS = 3000; // mengikuti TestAll
static constexpr unsigned long ENCODER_STALL_TIMEOUT_MS = 1200;
static constexpr long ENCODER_PROGRESS_MIN_TICKS = 1;
static constexpr unsigned long MOTOR_DIRECTION_DEADTIME_MS = 80;

// -----------------------------------------------------------------------------
// Input, sensor, OLED, MQTT scheduling
// -----------------------------------------------------------------------------
static constexpr unsigned long DEBOUNCE_MS = 30;
static constexpr unsigned long SENSOR_INTERVAL_MS = 100; // sensor ditembak bergantian
static constexpr unsigned long SENSOR_ECHO_TIMEOUT_US = 25000;
static constexpr unsigned long SENSOR_MAX_AGE_MS = 700;
static constexpr unsigned long OLED_REFRESH_MS = 100;
static constexpr unsigned long ANIMATION_FRAME_MS = 180;
static constexpr unsigned long UI_PAGE_TIMEOUT_MS = 5000;
static constexpr unsigned long UI_SNAPSHOT_INTERVAL_MS = 100;
static constexpr unsigned long TELEMETRY_MOVING_MS = 1000;
static constexpr unsigned long TELEMETRY_IDLE_MS = 5000;
static constexpr unsigned long MQTT_RETRY_MS = 5000;

static constexpr uint32_t PWM_FREQ = 2000;
static constexpr uint8_t PWM_RES = 8;
static constexpr uint8_t PWM_CHANNEL = 0; // Arduino-ESP32 core 2.x

#endif // CONFIG_H
