// =====================================================================
// SMART WAREHOUSE - ESP32 / L298N / JGA25 / encoder / 2x HC-SR04 / OLED
//
// MODE : klik singkat = ganti halaman OLED; klik 3x = wizard kalibrasi posisi.
// UP/DOWN : di dalam wizard = jog terawasi; di luar wizard = satu tap perintah lantai (PID).
// Kalibrasi selalu butuh operator untuk memastikan posisi fisik lift.
// HOME/homing berbasis motor-stall sengaja dinonaktifkan karena tidak ada limit switch.
// =====================================================================

#include <Preferences.h>
#include "Config.h"
#include "State.h"
#include "PIDControl.h"
#include "CommandUtil.h"
#if FEATURE_NETWORK
#include "MQTTManager.h"
#endif
#include "Button.h"
#include "Sensors.h"
#include "Display.h"
#if FEATURE_NETWORK
// Mulai baris ini semua Serial.xxx juga masuk ke serial monitor jarak jauh (aktif hanya bila admin LOG_ON).
#define Serial LogSerial
#endif

SystemState currentState = STATE_BOOT;
static const char* FIRMWARE_BUILD_ID = "SW-R21-20261124";   // R21 = perilaku lift R20 + integrasi penuh

enum CalibrationStage : uint8_t {
  CAL_STAGE_NONE = 0,
  CAL_STAGE_SET_FLOOR1,
  CAL_STAGE_MEASURE_FLOOR2,
  CAL_STAGE_REREFERENCE
};
CalibrationStage calibrationStage = CAL_STAGE_NONE;

// Travel lantai hasil pengukuran wizard dua titik, disimpan persisten di NVS.
long calibratedFloor2Ticks = 0;
bool floorTravelCalibrated = false;
static const char* POSITION_NVS_NAMESPACE = "sw_position";
static const char* POSITION_NVS_KEY_TICKS = "floor2_ticks";
static const char* POSITION_NVS_KEY_VERSION = "schema";
static const uint8_t POSITION_NVS_SCHEMA = 1;

bool loadFloorTravelCalibration();
bool saveFloorTravelCalibration(long floor2Ticks);
void printFloorTravelStatus();

// Encoder ditulis oleh ISR dan dibaca loop/task lain.
volatile long encoderSub = 0;   // hitungan kuadratur 4x; tick = encoderSub >> 2 (skala tick sama seperti sebelumnya)
static uint8_t encPrev = 0;     // status terakhir (A<<1)|B
static portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;

// ---- Status sistem dan gerak ----
volatile bool estopFlag = false;
bool positionValid = false;
bool homed = false; // compatibility dengan kontrak MQTT lama; selalu sama dengan positionValid
long targetTicks = 0;
int moveDir = 0;
int motorDir = 0;
int lastPwm = 0;
bool pwmReady = false;
unsigned long lastMotorOffMs = 0;
unsigned long rampStart = 0;
unsigned long moveStartTime = 0;
unsigned long jogStart = 0;
bool jogLocked = false;
unsigned long lastTelemetry = 0;
long lastTickSeen = 0;
unsigned long lastTickChange = 0;
const char* lastInfo = "";
const char* positionSource = "NONE";
unsigned long infoSince = 0;

// ---- Diagnostik (ditampilkan di OLED, tidak memengaruhi kontrol) ----
const char* lastFault = "";          // fault terakhir, tetap terlihat setelah ACK_ERROR
int diagLastDriveDir = 0;            // perintah motor terakhir; tidak dihapus motorStop()
int diagLastDrivePwm = 0;
unsigned long diagLastDriveAt = 0;
long diagLastGoal = 0;               // target lantai terakhir yang diterima
bool diagGoalValid = false;
unsigned long diagNoTickMs = 0;      // lama encoder tidak berubah (dibekukan saat MOVING berakhir)
unsigned long diagMoveMs = 0;        // lama gerakan berjalan (dibekukan saat MOVING berakhir)
unsigned long moveTraceAt = 0;
long moveStartTicks = 0;             // posisi awal gerakan, untuk deteksi arah salah
long moveOriginTicks = 0;            // posisi saat perintah diterima (tidak di-rebase; untuk bar progres OLED)
bool moveRebased = false;            // patokan gerak sudah dipindah ke posisi setelah masa awal
long moveProgressBest = 0;           // kemajuan terbaik menuju target (tick)
unsigned long moveProgressAt = 0;    // kapan kemajuan terakhir bertambah
long moveSpeedTps = 0;               // kecepatan tick/s selama gerakan (bertanda)
long speedSampleTicks = 0;
unsigned long speedSampleAt = 0;
unsigned long settleReportAt = 0;    // jadwal laporan posisi akhir (0 = tidak ada)
long settleGoal = 0;

LiftPID pid(PID_KP, PID_KI, PID_KD, PWM_MOVE_MIN, PWM_MOVE_MAX);

bool loadFloorTravelCalibration() {
  Preferences prefs;
  if (!prefs.begin(POSITION_NVS_NAMESPACE, true)) {
    floorTravelCalibrated = false;
    calibratedFloor2Ticks = 0;
    Serial.println("[CAL] NVS read failed; floor travel remains uncalibrated.");
    return false;
  }
  const uint8_t schema = prefs.getUChar(POSITION_NVS_KEY_VERSION, 0);
  const long storedTicks = prefs.getLong(POSITION_NVS_KEY_TICKS, 0);
  prefs.end();

  if (schema != POSITION_NVS_SCHEMA || storedTicks < FLOOR_TRAVEL_MIN_TICKS ||
      storedTicks > FLOOR_TRAVEL_MAX_TICKS) {
    floorTravelCalibrated = false;
    calibratedFloor2Ticks = 0;
    Serial.println("[CAL] No valid two-floor travel record in NVS.");
    return false;
  }

  calibratedFloor2Ticks = storedTicks;
  floorTravelCalibrated = true;
  Serial.printf("[CAL] Loaded calibratedFloor2Ticks=%ld from NVS. Position after boot still requires physical referencing.\n",
                calibratedFloor2Ticks);
  return true;
}

bool saveFloorTravelCalibration(long floor2Ticks) {
  if (floor2Ticks < FLOOR_TRAVEL_MIN_TICKS || floor2Ticks > FLOOR_TRAVEL_MAX_TICKS) {
    Serial.printf("[CAL][SAVE-REJECT] travel ticks out of range: %ld (allowed %ld..%ld)\n",
                  floor2Ticks, FLOOR_TRAVEL_MIN_TICKS, FLOOR_TRAVEL_MAX_TICKS);
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(POSITION_NVS_NAMESPACE, false)) {
    Serial.println("[CAL][SAVE-FAIL] NVS namespace open failed.");
    return false;
  }
  const size_t ticksWritten = prefs.putLong(POSITION_NVS_KEY_TICKS, floor2Ticks);
  const size_t schemaWritten = prefs.putUChar(POSITION_NVS_KEY_VERSION, POSITION_NVS_SCHEMA);
  prefs.end();

  if (ticksWritten != sizeof(long) || schemaWritten != sizeof(uint8_t)) {
    Serial.printf("[CAL][SAVE-FAIL] incomplete NVS write: ticksBytes=%u schemaBytes=%u\n",
                  (unsigned)ticksWritten, (unsigned)schemaWritten);
    return false;
  }

  // Baca ulang dari NVS sebelum perintah lantai diaktifkan.
  Preferences verify;
  if (!verify.begin(POSITION_NVS_NAMESPACE, true)) {
    Serial.println("[CAL][SAVE-FAIL] NVS read-back open failed.");
    return false;
  }
  const long verifyTicks = verify.getLong(POSITION_NVS_KEY_TICKS, 0);
  const uint8_t verifySchema = verify.getUChar(POSITION_NVS_KEY_VERSION, 0);
  verify.end();

  if (verifyTicks != floor2Ticks || verifySchema != POSITION_NVS_SCHEMA) {
    Serial.printf("[CAL][SAVE-FAIL] NVS read-back mismatch: requested=%ld stored=%ld schema=%u\n",
                  floor2Ticks, verifyTicks, (unsigned)verifySchema);
    return false;
  }

  calibratedFloor2Ticks = verifyTicks;
  floorTravelCalibrated = true;
  Serial.printf("[CAL][SAVE-OK] NVS verified: FLOOR_1=0, FLOOR_2=%ld ticks.\n",
                calibratedFloor2Ticks);
  return true;
}

void printFloorTravelStatus() {
  Serial.printf("[CAL STATUS] travel_calibrated=%d floor2_ticks=%ld position_valid=%d state=%s info=%s\n",
                floorTravelCalibrated ? 1 : 0, calibratedFloor2Ticks,
                positionValid ? 1 : 0, stateName(currentState), lastInfo);
}

// ---- Tombol dan gesture MODE ----
Button btnUp   = { (uint8_t)PIN_BTN_UP,   false, false, 0 };
Button btnDown = { (uint8_t)PIN_BTN_DOWN, false, false, 0 };
Button btnMode = { (uint8_t)PIN_BTN_MODE, false, false, 0 };

bool modePrev = false;
bool modeLongHandled = false;
unsigned long modePressAt = 0;
uint8_t modeClickCount = 0;
unsigned long modeLastReleaseAt = 0;
unsigned long calibrationStartedAt = 0;

// Jog saat kalibrasi adalah pengecualian terkontrol dari position/travel lock.
// Tidak pernah aktif di luar wizard MODE dan selalu hold-to-run.
bool calibrationJogStarted = false;
bool calibrationJogLocked = false;
int calibrationJogDirection = 0;
unsigned long calibrationJogStartedAt = 0;
unsigned long calibrationJogLastTickAt = 0;
unsigned long calibrationJogLastLogAt = 0;
long calibrationJogLastTicks = 0;
int calibrationOriginFloor = 0;
long calibrationOriginTicks = 0;
bool calibrationOriginValid = false;
bool calibrationUpJogSeen = false;
bool calibrationEncoderTrusted = false;   // posisi sudah valid saat wizard dibuka

// Edge tracking untuk tombol tujuan lantai.
// Di luar wizard: satu tekan UP = target FLOOR 2, satu tekan DOWN = target FLOOR 1.
// Tombol tidak perlu ditahan; pergerakan dijalankan oleh state machine PID.
bool upButtonPrev = false;
bool downButtonPrev = false;

// ---- Penjadwalan statistik dan log periodik ----
unsigned long tStat = 0;
unsigned long tSerialStat = 0;
unsigned long tNet = 0;
long lastStatTicks = 0;

// ---------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------
long readEncoderTicks() {
  long value;
  portENTER_CRITICAL(&encoderMux);
  value = encoderSub >> 2;
  portEXIT_CRITICAL(&encoderMux);
  return value;
}

void setEncoderTicks(long value) {
  portENTER_CRITICAL(&encoderMux);
  encoderSub = value * 4;
  portEXIT_CRITICAL(&encoderMux);
}

void IRAM_ATTR countEncoder() {
  // Tabel transisi kuadratur: getaran maju-mundur di satu tepi sinyal saling meniadakan.
  static const int8_t T[16] = {0,1,-1,0, -1,0,0,1, 1,0,0,-1, 0,-1,1,0};
  portENTER_CRITICAL_ISR(&encoderMux);
  uint8_t cur = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  int8_t d = T[(encPrev << 2) | cur];
  encPrev = cur;
  encoderSub += (ENC_INVERT != 0) ? -d : d;
  portEXIT_CRITICAL_ISR(&encoderMux);
}

// ISR hanya menandai E-STOP. Pemutusan tenaga motor wajib dilakukan hardware.
void IRAM_ATTR estopISR() {
  estopFlag = true;
}

bool estopActive() {
  return digitalRead(PIN_ESTOP) == ESTOP_ACTIVE_LEVEL;
}

// ---------------------------------------------------------------------
// PWM: kompatibel Arduino-ESP32 Core 2.x dan 3.x
// ---------------------------------------------------------------------
bool setupPwm() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return ledcAttach(PIN_PWM, PWM_FREQ, PWM_RES);
#else
  if (ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES) <= 0) return false;
  ledcAttachPin(PIN_PWM, PWM_CHANNEL);
  return true;
#endif
}

void writePwm(int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_PWM, duty);
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
}

// ---------------------------------------------------------------------
// Motor
// ---------------------------------------------------------------------
void motorStop() {
  if (motorDir != 0) lastMotorOffMs = millis();
  digitalWrite(PIN_IN1, STOP_BRAKE ? HIGH : LOW);
  digitalWrite(PIN_IN2, STOP_BRAKE ? HIGH : LOW);
  if (pwmReady) writePwm(STOP_BRAKE ? 255 : 0);
  motorDir = 0;
  lastPwm = 0;
}

// Atur arah, soft-start, dan PWM. Guard keselamatan ada di pemanggil.
// Return false bila dead-time belum lewat (motor belum boleh dinyalakan).
static bool applyMotorOutput(int dir, int pwm) {
  if (motorDir != 0 && motorDir != dir) {
    motorStop();
    return false;
  }
  if (motorDir == 0 && millis() - lastMotorOffMs < MOTOR_DEADTIME_MS) return false;

  const bool naik = (dir > 0) != (MOTOR_DIR_INVERT != 0);
  digitalWrite(PIN_IN1, naik ? HIGH : LOW);
  digitalWrite(PIN_IN2, naik ? LOW : HIGH);

  if (motorDir != dir) rampStart = millis();
  motorDir = dir;

  const int target = constrain(pwm * ((dir > 0) ? PWM_UP_PCT : PWM_DOWN_PCT) / 100, 0, 255);
  int effective = target;
  const unsigned long elapsed = millis() - rampStart;
  if (elapsed < KICK_MS && target < PWM_START_BOOST) {
    effective = PWM_START_BOOST;   // dorongan awal, bukan soft-start (lihat Config.h)
  }
  writePwm(effective);
  lastPwm = effective;
  diagLastDriveDir = dir;
  diagLastDrivePwm = effective;
  diagLastDriveAt = millis();
  return true;
}

// Gerak normal (PID / perintah lantai): wajib posisi valid dan travel terkalibrasi.
void motorDrive(int dir, int pwm) {
  if (!pwmReady || dir == 0 || estopFlag || estopActive() || !positionValid || !floorTravelCalibrated) {
    motorStop();
    return;
  }
  applyMotorOutput(dir, pwm);
}

// Jog kalibrasi terawasi: melewati positionValid/floorTravelCalibrated,
// hanya boleh dipanggil saat wizard MODE aktif. Jangan dipakai untuk MQTT / operasi normal.
void motorDriveCalibration(int dir, int pwm) {
  if (!ui.calibrationActive || !pwmReady || dir == 0 || estopFlag || estopActive() ||
      currentState == STATE_ESTOP || currentState == STATE_ERROR) {
    motorStop();
    return;
  }
  applyMotorOutput(dir, pwm);
}

// ---------------------------------------------------------------------
// Status, UI, telemetri
// ---------------------------------------------------------------------
void touch() {
  ui.lastActivity = millis();
}

void setInfo(const char* info) {
  lastInfo = info ? info : "";
  infoSince = millis();
}

void publishTelemetry() {
#if FEATURE_NETWORK
  long ticks = readEncoderTicks();
  float d1 = sensorCm(0);
  float d2 = sensorCm(1);
  int floor = positionValid ? getFloor(ticks) : 0;
  RackCapacityState rack1 = rackCapacityStatus(0);
  RackCapacityState rack2 = rackCapacityStatus(1);
  char buf[MQTT_PAYLOAD_MAX];
  snprintf(buf, sizeof(buf),
           "{\"state\":\"%s\",\"floor\":%d,\"ticks\":%ld,\"target\":%ld,"
           "\"dist1\":%.1f,\"dist2\":%.1f,\"sensor1_valid\":%s,\"sensor2_valid\":%s,"
           "\"rack1_status\":\"%s\",\"rack2_status\":\"%s\","
           "\"rack1_near_full\":%s,\"rack2_near_full\":%s,"
           "\"estop\":%s,\"homed\":%s,\"position_valid\":%s,\"travel_config_valid\":%s,\"ready_to_move\":%s,\"position_source\":\"%s\","
           "\"pwm\":%d,\"direction\":%d,\"speed\":%ld,\"floor2_ticks\":%ld,\"fault\":\"%s\",\"info\":\"%s\"}",
           stateName(currentState), floor, ticks, targetTicks,
           d1, d2, d1 >= 0.0f ? "true" : "false", d2 >= 0.0f ? "true" : "false",
           rackCapacityStatusName(rack1), rackCapacityStatusName(rack2),
           rack1 == RACK_CAPACITY_NEAR_FULL ? "true" : "false",
           rack2 == RACK_CAPACITY_NEAR_FULL ? "true" : "false",
           (currentState == STATE_ESTOP || estopActive()) ? "true" : "false",
           positionValid ? "true" : "false", positionValid ? "true" : "false",
           floorTravelCalibrated ? "true" : "false",
           (positionValid && floorTravelCalibrated) ? "true" : "false",
           positionSource, lastPwm, motorDir, (long)ui.speedTps, calibratedFloor2Ticks,
           (lastFault && lastFault[0]) ? lastFault : "", lastInfo);
  mqttPublishTelemetry(buf);
#endif
  lastTelemetry = millis();
}

void setState(SystemState state) {
  currentState = state;
  if (state == STATE_ERROR) lastFault = (lastInfo && lastInfo[0]) ? lastInfo : "UNSPECIFIED_ERROR";
  else if (state == STATE_ESTOP) lastFault = "ESTOP_ACTIVE";
  publishTelemetry();
}

void invalidatePosition(const char* reason) {
  positionValid = false;
  homed = false;
  positionSource = "NONE";
  targetTicks = 0;
  setInfo(reason);
}

void fail(const char* info) {
  Serial.printf("[FAIL] %s start=%ld ticks=%ld goal=%ld floor2=%ld dir=%d lastPwm=%d noTick=%lums move=%lums\n",
                info, moveStartTicks, readEncoderTicks(), diagLastGoal, calibratedFloor2Ticks,
                moveDir, diagLastDrivePwm, diagNoTickMs, diagMoveMs);
  motorStop();
  invalidatePosition(info);
  Serial.print("ERROR: ");
  Serial.println(info);
  setState(STATE_ERROR);
  touch();
}

void enterEstop() {
  motorStop();
  estopFlag = false;
  invalidatePosition("ESTOP_POSITION_INVALID");
  Serial.println("E-STOP: motor stopped; position must be revalidated.");
  ui.calibrationActive = false;
  setState(STATE_ESTOP);
  touch();
}

// ---------------------------------------------------------------------
// Kalibrasi posisi: jog manual terawasi hanya di dalam wizard MODE; tanpa homing sensorless.
// ---------------------------------------------------------------------
void beginCalibration() {
  if (estopActive() || currentState == STATE_ESTOP || currentState == STATE_ERROR ||
      motorDir != 0 || (currentState != STATE_IDLE && currentState != STATE_POSITION_UNREFERENCED)) {
    setInfo("CALIBRATION_NOT_ALLOWED");
    Serial.printf("[CAL][BEGIN-REJECT] state=%s motorDir=%d estop=%d\n",
                  stateName(currentState), motorDir, estopActive() ? 1 : 0);
    publishTelemetry();
    return;
  }

  motorStop();
  ui.calibrationActive = true;
  ui.page = 0;
  calibrationJogStarted = false;
  calibrationJogLocked = false;
  calibrationJogDirection = 0;
  calibrationJogLastLogAt = 0;
  calibrationStartedAt = millis();
  calibrationEncoderTrusted = positionValid;
  calibrationOriginTicks = readEncoderTicks();
  calibrationOriginFloor = positionValid ? getFloor(calibrationOriginTicks) : 0;
  calibrationOriginValid = positionValid &&
      (calibrationOriginFloor == 1 || calibrationOriginFloor == 2);
  calibrationUpJogSeen = false;

  // Selama travel belum terukur, wizard mempunyai dua tahap yang FIXED:
  // 1) referensikan FLOOR 1; 2) ukur perjalanan ke FLOOR 2.
  // Klik singkat MODE tidak boleh mengubah target tahap ini.
  if (!floorTravelCalibrated) {
    if (positionValid && calibrationOriginFloor == 1) {
      calibrationStage = CAL_STAGE_MEASURE_FLOOR2;
      ui.calibrationFloor = 2;
      setInfo("STAGE2_JOG_UP_TO_FLOOR2_HOLD_MODE_TO_SAVE");
      Serial.printf("[CAL][BEGIN] stage=MEASURE_FLOOR2 originTicks=%ld originFloor=%d positionValid=1\n",
                    calibrationOriginTicks, calibrationOriginFloor);
    } else {
      calibrationStage = CAL_STAGE_SET_FLOOR1;
      ui.calibrationFloor = 1;
      calibrationOriginValid = false;
      setInfo("STAGE1_ALIGN_FLOOR1_HOLD_MODE_TO_SET");
      Serial.printf("[CAL][BEGIN] stage=SET_FLOOR1 rawTicks=%ld positionValid=%d\n",
                    calibrationOriginTicks, positionValid ? 1 : 0);
    }
  } else {
    // Travel sudah tersimpan: wizard hanya melakukan re-reference posisi setelah boot.
    calibrationStage = CAL_STAGE_REREFERENCE;
    ui.calibrationFloor = calibrationOriginValid ? calibrationOriginFloor : 1;
    setInfo("REPOSITION_TO_SELECTED_FLOOR_HOLD_MODE");
    Serial.printf("[CAL][BEGIN] stage=REREFERENCE rawTicks=%ld suggestedFloor=%d\n",
                  calibrationOriginTicks, ui.calibrationFloor);
  }

  touch();
  displayPublish();
}

void cancelCalibration() {
  motorStop();
  ui.calibrationActive = false;
  ui.calibrationFloor = 0;
  calibrationStage = CAL_STAGE_NONE;
  calibrationJogStarted = false;
  calibrationJogLocked = false;
  calibrationJogDirection = 0;
  calibrationOriginFloor = 0;
  calibrationOriginTicks = 0;
  calibrationOriginValid = false;
  calibrationUpJogSeen = false;
  setInfo("CALIBRATION_CANCELLED");
  touch();
  displayPublish();
}

bool confirmCalibration() {
  if (!ui.calibrationActive || calibrationStage == CAL_STAGE_NONE) {
    setInfo("CALIBRATION_STAGE_INVALID");
    Serial.println("[CAL][CONFIRM-REJECT] wizard has no active calibration stage.");
    return false;
  }

  if (estopActive() || estopFlag || currentState == STATE_ESTOP || currentState == STATE_ERROR ||
      motorDir != 0 || btnUp.stable || btnDown.stable) {
    setInfo("CALIBRATION_INTERLOCK_RELEASE_BUTTONS");
    Serial.println("[CAL][CONFIRM-REJECT] release UP/DOWN and ensure motor is stopped before holding MODE.");
    displayPublish();
    return false;
  }

  const long observedTicks = readEncoderTicks();
  long referenceTicks = FLOOR_1_TICKS;
  const int confirmedFloor = ui.calibrationFloor;

  Serial.printf("[CAL][CONFIRM] stage=%u selectedFloor=%d originFloor=%d originValid=%d originTicks=%ld observedTicks=%ld upJogSeen=%d delta=%ld\n",
                (unsigned)calibrationStage, confirmedFloor, calibrationOriginFloor,
                calibrationOriginValid ? 1 : 0, calibrationOriginTicks, observedTicks,
                calibrationUpJogSeen ? 1 : 0, observedTicks - calibrationOriginTicks);

  if (calibrationStage == CAL_STAGE_SET_FLOOR1) {
    if (confirmedFloor != 1) {
      setInfo("STAGE1_REQUIRES_FLOOR1");
      Serial.println("[CAL][CONFIRM-REJECT] stage 1 must set FLOOR 1. Do not select FLOOR 2 here.");
      displayPublish();
      return false;
    }
    referenceTicks = FLOOR_1_TICKS;
    Serial.printf("[CAL][STEP1-OK] FLOOR 1 physical reference accepted. rawTicks=%ld; logical ticks will be zeroed.\n",
                  observedTicks);

  } else if (calibrationStage == CAL_STAGE_MEASURE_FLOOR2) {
    if (confirmedFloor != 2) {
      setInfo("STAGE2_REQUIRES_FLOOR2");
      Serial.println("[CAL][CONFIRM-REJECT] stage 2 is fixed to FLOOR 2. Hold MODE without short-clicking it first.");
      displayPublish();
      return false;
    }

    const long measuredDelta = observedTicks - calibrationOriginTicks;
    if (!calibrationOriginValid || calibrationOriginFloor != 1) {
      setInfo("CALIBRATE_FLOOR1_FIRST");
      Serial.printf("[CAL][TRAVEL-REJECT] invalid origin: valid=%d floor=%d. Re-reference FLOOR 1 first.\n",
                    calibrationOriginValid ? 1 : 0, calibrationOriginFloor);
      displayPublish();
      return false;
    }
    if (!calibrationUpJogSeen) {
      setInfo("UP_JOG_REQUIRED_FOR_FLOOR2");
      Serial.println("[CAL][TRAVEL-REJECT] no UP jog event was recorded. Use UP to reach the FLOOR 2 mark.");
      displayPublish();
      return false;
    }
    if (measuredDelta <= 0) {
      setInfo("ENCODER_UP_DIRECTION_INVALID");
      Serial.printf("[CAL][TRAVEL-REJECT] UP jog produced non-positive delta=%ld. UP must increase ticks. Check encoder polarity/A-B phase and ENC_INVERT; MOTOR_DIR_INVERT controls physical motor direction and must not be changed just to fix encoder sign. Do not save.\n",
                    measuredDelta);
      displayPublish();
      return false;
    }
    if (measuredDelta < FLOOR_TRAVEL_MIN_TICKS) {
      setInfo("TRAVEL_DELTA_TOO_SMALL");
      Serial.printf("[CAL][TRAVEL-REJECT] delta=%ld is below minimum=%ld ticks.\n",
                    measuredDelta, FLOOR_TRAVEL_MIN_TICKS);
      displayPublish();
      return false;
    }
    if (measuredDelta > FLOOR_TRAVEL_MAX_TICKS) {
      setInfo("TRAVEL_DELTA_TOO_LARGE");
      Serial.printf("[CAL][TRAVEL-REJECT] delta=%ld exceeds maximum=%ld ticks.\n",
                    measuredDelta, FLOOR_TRAVEL_MAX_TICKS);
      displayPublish();
      return false;
    }

    Serial.printf("[CAL][TRAVEL-CANDIDATE] origin=%ld observed=%ld delta=%ld ticks. Saving to NVS...\n",
                  calibrationOriginTicks, observedTicks, measuredDelta);
    if (!saveFloorTravelCalibration(measuredDelta)) {
      setInfo("TRAVEL_NVS_SAVE_FAILED");
      displayPublish();
      return false;
    }
    referenceTicks = calibratedFloor2Ticks;

  } else if (calibrationStage == CAL_STAGE_REREFERENCE) {
    if (confirmedFloor == 3) {
      // UKUR ULANG: hapus travel lama di NVS, lalu mulai lagi dari Tahap 1.
      // Tanpa opsi ini, F2 lama (mis. 485) dimuat dari NVS saat boot dan tidak pernah berubah.
      Preferences prefs;
      if (!prefs.begin(POSITION_NVS_NAMESPACE, false)) {
        setInfo("TRAVEL_NVS_OPEN_FAILED");
        displayPublish();
        return false;
      }
      prefs.clear();
      prefs.end();
      calibratedFloor2Ticks = 0;
      floorTravelCalibrated = false;
      ui.travelCalibrated = false;
      ui.floor2Ticks = 0;
      positionValid = false;
      homed = false;
      positionSource = "NONE";
      targetTicks = 0;
      calibrationStage = CAL_STAGE_SET_FLOOR1;
      ui.calibrationFloor = 1;
      calibrationOriginValid = false;
      calibrationOriginFloor = 0;
      calibrationOriginTicks = readEncoderTicks();
      calibrationUpJogSeen = false;
      calibrationJogStarted = false;
      calibrationJogLocked = false;
      calibrationJogDirection = 0;
      calibrationStartedAt = millis();
      setInfo("STAGE1_ALIGN_FLOOR1_HOLD_MODE_TO_SET");
      Serial.println("[CAL][REMEASURE] Travel lama dihapus dari NVS. TAHAP 1: jog ke Floor 1, lepas tombol, tahan MODE 1,5 detik.");
      setState(STATE_POSITION_UNREFERENCED);
      touch();
      displayPublish();
      return true;  // wizard tetap terbuka di Tahap 1
    }
    if (calibrationEncoderTrusted && (confirmedFloor == 1 || confirmedFloor == 2)) {
      const long expectedTicks = (confirmedFloor == 1) ? FLOOR_1_TICKS : calibratedFloor2Ticks;
      if (labs(observedTicks - expectedTicks) > REREF_MISMATCH_TICKS) {
        setInfo("TRAVEL_MISMATCH_REMEASURE");
        Serial.printf("[CAL][REREF-REJECT] encoder=%ld tetapi FLOOR %d tersimpan=%ld (selisih %ld > %ld). Travel tersimpan tidak cocok dengan jarak fisik. Klik MODE sampai UKUR ULANG F1+F2, lalu tahan MODE.\n",
                      observedTicks, confirmedFloor, expectedTicks, observedTicks - expectedTicks, REREF_MISMATCH_TICKS);
        displayPublish();
        return false;
      }
    }
    if (confirmedFloor == 1) {
      referenceTicks = FLOOR_1_TICKS;
    } else if (confirmedFloor == 2) {
      referenceTicks = calibratedFloor2Ticks;
    } else {
      setInfo("SELECT_VALID_FLOOR");
      return false;
    }
    Serial.printf("[CAL][REREFERENCE-OK] physical FLOOR %d selected using stored travel=%ld ticks.\n",
                  confirmedFloor, calibratedFloor2Ticks);
  }

  motorStop();
  setEncoderTicks(referenceTicks);
  targetTicks = referenceTicks;
  positionValid = true;
  homed = true;
  positionSource = "MODE_MANUAL_CONFIRM";
  ui.calibrationActive = false;
  ui.calibrationFloor = 0;
  calibrationStage = CAL_STAGE_NONE;
  calibrationJogStarted = false;
  calibrationJogLocked = false;
  calibrationJogDirection = 0;
  calibrationOriginFloor = 0;
  calibrationOriginTicks = 0;
  calibrationOriginValid = false;
  calibrationUpJogSeen = false;
  ui.calibrationFlashUntil = millis() + 1800;
  lastTickSeen = referenceTicks;
  lastTickChange = millis();
  pid.reset();
  jogLocked = false;
  ui.travelCalibrated = floorTravelCalibrated;
  ui.floor2Ticks = calibratedFloor2Ticks;

  if (floorTravelCalibrated) setInfo("POSITION_CALIBRATED_READY");
  else if (confirmedFloor == 1) setInfo("FLOOR1_SET_NOW_MEASURE_FLOOR2");
  else setInfo("CALIBRATION_INCOMPLETE");

  Serial.printf("[CAL][COMPLETE] physicalFloor=%d logicalTicks=%ld travelCalibrated=%d floor2Ticks=%ld\n",
                confirmedFloor, referenceTicks, floorTravelCalibrated ? 1 : 0,
                calibratedFloor2Ticks);
  if (floorTravelCalibrated) {
    Serial.println("[CAL][READY] PID floor buttons enabled: tap UP=FLOOR 2, DOWN=FLOOR 1.");
  } else if (confirmedFloor == 1) {
    Serial.println("[CAL][NEXT] Triple-click MODE again. Wizard is fixed to FLOOR 2; jog UP to the FLOOR 2 mark, then HOLD MODE 1.5 s to save travel. Do NOT short-click MODE in stage 2.");
  }
  setState(STATE_IDLE);
  touch();
  displayPublish();
  return true;
}

// ---------------------------------------------------------------------
// Supervised jog khusus wizard kalibrasi posisi.
// Aktif hanya saat MODE triple-click wizard terbuka; bukan pengganti limit fisik.
// Hold-to-run, maksimal 1.5 s per tekanan, lepas UP+DOWN untuk re-arm.
// Jika encoder tidak berubah atau kedua tombol ditekan, motor berhenti dan terkunci.
// ---------------------------------------------------------------------
void handleCalibrationJog(bool up, bool down, unsigned long now) {
  if (!ui.calibrationActive) {
    if (motorDir != 0) motorStop();
    calibrationJogStarted = false;
    calibrationJogDirection = 0;
    return;
  }

  if (estopFlag || estopActive() || currentState == STATE_ESTOP || currentState == STATE_ERROR || !pwmReady) {
    motorStop();
    calibrationJogStarted = false;
    calibrationJogLocked = true;
    calibrationJogDirection = 0;
    return;
  }

  // MODE digunakan untuk pilih/konfirmasi; ketika ditekan, jog harus berhenti.
  if (btnMode.stable) {
    if (motorDir != 0) motorStop();
    calibrationJogStarted = false;
    calibrationJogDirection = 0;
    if (up || down) calibrationJogLocked = true;
    return;
  }

  if (up && down) {
    motorStop();
    calibrationJogStarted = false;
    calibrationJogLocked = true;
    calibrationJogDirection = 0;
    calibrationStartedAt = now;
    setInfo("CALIBRATION_BOTH_BUTTONS_RELEASE");
    touch();
    return;
  }

  if (!up && !down) {
    if (motorDir != 0) motorStop();
    if (calibrationJogStarted || calibrationJogLocked) {
      calibrationJogStarted = false;
      calibrationJogLocked = false;
      calibrationJogDirection = 0;
      calibrationStartedAt = now;
      setInfo("CALIBRATION_READY");
      touch();
    }
    return;
  }

  if (calibrationJogLocked) {
    motorStop();
    return;
  }

  const int requestedDirection = up ? 1 : -1;
  if (!calibrationJogStarted) {
    calibrationJogStarted = true;
    calibrationJogDirection = requestedDirection;
    if (requestedDirection > 0) calibrationUpJogSeen = true;
    calibrationJogStartedAt = now;
    calibrationJogLastTicks = readEncoderTicks();
    calibrationJogLastTickAt = now;
    calibrationJogLastLogAt = now;
    calibrationStartedAt = now;
    setInfo(up ? "CALIBRATION_JOG_UP" : "CALIBRATION_JOG_DOWN");
    Serial.printf("[CAL][JOG-START] direction=%s ticks=%ld origin=%ld stage=%u\n",
                  up ? "UP" : "DOWN", calibrationJogLastTicks,
                  calibrationOriginTicks, (unsigned)calibrationStage);
    touch();
  } else if (requestedDirection != calibrationJogDirection) {
    motorStop();
    calibrationJogLocked = true;
    setInfo("RELEASE_BEFORE_DIRECTION_CHANGE");
    touch();
    return;
  }

  const long ticks = readEncoderTicks();
  if (ticks != calibrationJogLastTicks) {
    calibrationJogLastTicks = ticks;
    calibrationJogLastTickAt = now;
    if (now - calibrationJogLastLogAt >= 250) {
      Serial.printf("[CAL][ENCODER] direction=%s ticks=%ld deltaFromOrigin=%ld\n",
                    requestedDirection > 0 ? "UP" : "DOWN", ticks,
                    ticks - calibrationOriginTicks);
      calibrationJogLastLogAt = now;
    }
  }

  if (now - calibrationJogStartedAt >= CALIBRATION_JOG_MAX_MS) {
    motorStop();
    calibrationJogLocked = true;
    setInfo("CALIBRATION_JOG_LIMIT_RELEASE");
    touch();
    return;
  }

  if (calibrationStage == CAL_STAGE_MEASURE_FLOOR2 && requestedDirection > 0 &&
      ticks - calibrationOriginTicks >= FLOOR_TRAVEL_MAX_TICKS) {
    motorStop();
    calibrationJogLocked = true;
    setInfo("TRAVEL_MAX_REACHED");
    Serial.printf("[CAL][JOG-LIMIT] UP stopped at delta=%ld (FLOOR_TRAVEL_MAX_TICKS=%ld).\n",
                  ticks - calibrationOriginTicks, FLOOR_TRAVEL_MAX_TICKS);
    touch();
    return;
  }

  if (now - calibrationJogStartedAt >= RAMP_MS &&
      now - calibrationJogLastTickAt >= CALIBRATION_JOG_STALL_MS) {
    // Motor mungkin bergerak tanpa feedback encoder. Referensi sebelumnya tidak lagi tepercaya.
    ui.calibrationActive = false;
    calibrationJogStarted = false;
    calibrationJogLocked = true;
    fail("CALIBRATION_ENCODER_NO_MOTION");
    return;
  }

  motorDriveCalibration(requestedDirection, PWM_CALIBRATION_JOG);
}

// ---------------------------------------------------------------------
// Tombol fisik: satu tap = satu permintaan lantai yang dikendalikan PID.
// UP meminta FLOOR 2; DOWN meminta FLOOR 1. Bukan hold-to-run.
// Jog hold-to-run hanya ada di dalam wizard kalibrasi MODE.
// ---------------------------------------------------------------------
bool requestFloorMove(long target, const char* sourceInfo) {
  Serial.printf("[FLOOR REQUEST] source=%s target=%ld state=%s pos=%d travel=%d pwmReady=%d estop=%d\n",
                sourceInfo ? sourceInfo : "UNKNOWN", target, stateName(currentState),
                positionValid ? 1 : 0, floorTravelCalibrated ? 1 : 0,
                pwmReady ? 1 : 0, (estopFlag || estopActive()) ? 1 : 0);
  if (!pwmReady) {
    setInfo("PWM_NOT_READY");
    publishTelemetry();
    return false;
  }
  if (estopFlag || estopActive() || currentState == STATE_ESTOP) {
    setInfo("ESTOP_ACTIVE");
    publishTelemetry();
    return false;
  }
  if (currentState == STATE_ERROR) {
    setInfo("ERROR_ACK_REQUIRED");
    publishTelemetry();
    return false;
  }
  if (ui.calibrationActive) {
    setInfo("CALIBRATION_BUSY");
    publishTelemetry();
    return false;
  }
  if (!positionValid) {
    setInfo("POSITION_UNREFERENCED");
    publishTelemetry();
    return false;
  }
  if (!floorTravelCalibrated) {
    setInfo("CALIBRATE_FLOOR1_THEN_FLOOR2");
    Serial.println("[FLOOR REQUEST REJECTED] Perform two-point calibration: FLOOR 1 reference, then measure FLOOR 1 -> FLOOR 2.");
    displayPublish();
    publishTelemetry();
    return false;
  }
  if (currentState != STATE_IDLE) {
    setInfo("BUSY_WAIT_FOR_ARRIVAL");
    Serial.printf("[FLOOR REQUEST REJECTED] state=%s is not IDLE.\n", stateName(currentState));
    publishTelemetry();
    return false;
  }
  if (target != FLOOR_1_TICKS && target != calibratedFloor2Ticks) {
    setInfo("TARGET_NOT_CALIBRATED");
    publishTelemetry();
    return false;
  }
  if (target < (FLOOR_1_TICKS - FLOOR_SOFT_OVERRUN_TICKS) ||
      target > (calibratedFloor2Ticks + FLOOR_SOFT_OVERRUN_TICKS)) {
    setInfo("TARGET_OUT_OF_RANGE");
    publishTelemetry();
    return false;
  }

  const long currentTicks = readEncoderTicks();
  const long positionError = target - currentTicks;
  diagLastGoal = target;
  diagGoalValid = true;
  if (labs(positionError) <= TOLERANCE_TICKS) {
    targetTicks = target;
    motorStop();
    setInfo("ALREADY_AT_REQUESTED_FLOOR");
    publishTelemetry();
    return true;
  }

  targetTicks = target;
  moveDir = (positionError > 0) ? 1 : -1;
  pid.reset();
  moveStartTime = millis();
  moveStartTicks = currentTicks;
  moveOriginTicks = currentTicks;
  moveTraceAt = moveStartTime;
  Serial.printf("[MOVE-START] from=%ld goal=%ld dir=%d floor2=%ld\n", currentTicks, target, moveDir, calibratedFloor2Ticks);
  moveProgressBest = 0;
  moveProgressAt = moveStartTime;
  moveRebased = false;
  moveSpeedTps = 0;
  speedSampleTicks = currentTicks;
  speedSampleAt = moveStartTime;
  lastTickSeen = currentTicks;
  lastTickChange = moveStartTime;
  setInfo(sourceInfo);
  Serial.printf("[FLOOR REQUEST ACCEPTED] current=%ld target=%ld direction=%d\n",
                currentTicks, targetTicks, moveDir);
  setState(STATE_MOVING);
  return true;
}

void handleFloorButtonPress(bool upPressedEdge, bool downPressedEdge,
                            bool upStable, bool downStable) {
  if (!upPressedEdge && !downPressedEdge) return;
  if (ui.calibrationActive) return; // wizard handler owns hold-to-run jog

  // Tekan bersamaan itu ambigu; jangan menebak tujuan.
  if ((upPressedEdge && downPressedEdge) || (upStable && downStable)) {
    setInfo("BOTH_FLOOR_BUTTONS_PRESSED");
    publishTelemetry();
    return;
  }

  if (upPressedEdge) {
    requestFloorMove(calibratedFloor2Ticks, "BUTTON_UP_REQUEST_FLOOR_2");
  } else if (downPressedEdge) {
    requestFloorMove(FLOOR_1_TICKS, "BUTTON_DOWN_REQUEST_FLOOR_1");
  }
}

// ---------------------------------------------------------------------
// Perintah
// ---------------------------------------------------------------------
// Satu pintu semua perintah (Serial, MQTT, admin).
// trusted = true bila berasal dari Serial USB atau jalur admin MQTT yang sudah lolos allow-list;
// perintah servis/admin (ACK_ERROR, STATUS, CANCEL_CAL, ZERO, CLEAR_TRAVEL_CAL) hanya diproses bila trusted.
void handleCommand(const char* cmd, bool trusted) {
  if (!cmd || !cmd[0]) return;
  Serial.print("CMD: ");
  Serial.println(cmd);
  touch();

  // STOP selalu boleh diproses, termasuk ketika calibration sedang terbuka.
  if (strcmp(cmd, "STOP") == 0) {
    motorStop();
    if (ui.calibrationActive) {
      calibrationJogStarted = false;
      calibrationJogLocked = true;
      calibrationJogDirection = 0;
    }
    if (currentState == STATE_MOVING || currentState == STATE_MANUAL) {
      setInfo("STOPPED");
      setState(positionValid ? STATE_IDLE : STATE_POSITION_UNREFERENCED);
    } else {
      setInfo("STOPPED");
      publishTelemetry();
    }
    return;
  }

  if (estopActive() || currentState == STATE_ESTOP) {
    setInfo("ESTOP_ACTIVE");
    publishTelemetry();
    return;
  }
  if (strcmp(cmd, "CANCEL_CAL") == 0 && trusted) {
    if (ui.calibrationActive && motorDir == 0) {
      cancelCalibration();
      Serial.println("Calibration wizard cancelled locally.");
    } else {
      setInfo("CANCEL_CAL_NOT_ALLOWED");
      publishTelemetry();
    }
    return;
  }

  if (ui.calibrationActive) {
    setInfo("CALIBRATION_BUSY");
    publishTelemetry();
    return;
  }

  if (strcmp(cmd, "HOME") == 0 || strcmp(cmd, "RESET") == 0) {
    setInfo("HOME_UNAVAILABLE_NO_REFERENCE");
    publishTelemetry();
    if (trusted) Serial.println("Automatic HOME disabled: no independent physical reference sensor. Use MODE triple-click calibration after physically checking the floor.");
    return;
  }

  if (strcmp(cmd, "ACK_ERROR") == 0 && trusted) {
    if (currentState == STATE_ERROR && !estopActive() && motorDir == 0 && pwmReady) {
      invalidatePosition("FAULT_ACK_POSITION_UNREFERENCED");
      setState(STATE_POSITION_UNREFERENCED);
      Serial.println("Fault acknowledged. Position remains unreferenced; perform MODE calibration after physical inspection.");
    } else {
      setInfo("ACK_ERROR_NOT_ALLOWED");
      publishTelemetry();
    }
    return;
  }

  if (strcmp(cmd, "ZERO") == 0 && trusted) {
    setInfo("USE_MODE_TRIPLE_CLICK");
    publishTelemetry();
    Serial.println("ZERO command disabled. Triple-click MODE, select the physically verified floor, then hold MODE to confirm.");
    return;
  }

  if (strcmp(cmd, "CLEAR_TRAVEL_CAL") == 0 && trusted) {
    if (motorDir != 0 || ui.calibrationActive || currentState == STATE_MOVING ||
        currentState == STATE_MANUAL || estopActive()) {
      setInfo("CLEAR_TRAVEL_NOT_ALLOWED");
      publishTelemetry();
      Serial.println("CLEAR_TRAVEL_CAL rejected: motor must be stopped, wizard closed, E-STOP released.");
      return;
    }
    Preferences prefs;
    if (!prefs.begin(POSITION_NVS_NAMESPACE, false)) {
      setInfo("TRAVEL_NVS_OPEN_FAILED");
      publishTelemetry();
      return;
    }
    prefs.clear();
    prefs.end();
    calibratedFloor2Ticks = 0;
    floorTravelCalibrated = false;
    ui.travelCalibrated = false;
    ui.floor2Ticks = 0;
    invalidatePosition("TRAVEL_CAL_CLEARED_RECALIBRATE");
    setState(STATE_POSITION_UNREFERENCED);
    Serial.println("Stored floor travel cleared. Repeat two-point calibration: FLOOR 1, then FLOOR 2.");
    return;
  }

  if (strcmp(cmd, "STATUS") == 0 && trusted) {
    Serial.printf("state=%s ticks=%ld position_valid=%d estop=%d source=%s info=%s\n",
                  stateName(currentState), readEncoderTicks(), positionValid ? 1 : 0,
                  estopActive() ? 1 : 0, positionSource, lastInfo);
    printFloorTravelStatus();
    return;
  }

  long target;
  if (strcmp(cmd, "0") == 0 || strcmp(cmd, "GROUND") == 0 || strcmp(cmd, "1") == 0) target = FLOOR_1_TICKS;
  else if (strcmp(cmd, "2") == 0) target = calibratedFloor2Ticks;
  else {
    setInfo("UNKNOWN_CMD");
    publishTelemetry();
    return;
  }

  requestFloorMove(target, trusted ? "SERIAL_FLOOR_COMMAND" : "MQTT_FLOOR_COMMAND");
}

// ---------------------------------------------------------------------
// Parser perintah Serial
// ---------------------------------------------------------------------
char serialLine[32];
size_t serialLen = 0;

void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLen > 0) {
        serialLine[serialLen] = '\0';
        normalizeCommand(serialLine);
        if (serialLine[0]) handleCommand(serialLine, true);
        serialLen = 0;
      }
    } else if (serialLen < sizeof(serialLine) - 1) {
      serialLine[serialLen++] = c;
    } else {
      serialLen = 0;
      setInfo("SERIAL_COMMAND_TOO_LONG");
    }
  }
}

// ---------------------------------------------------------------------
// Penanganan tombol MODE
// Di luar wizard: satu/dua klik navigasi OLED, tiga klik membuka kalibrasi.
// Di wizard dua titik, FLOOR 1/FLOOR 2 fixed; klik singkat hanya info. Re-reference boleh memilih lantai.
// ---------------------------------------------------------------------
void toggleCalibrationFloor() {
  // Pilihan floor hanya boleh diganti saat travel geometry sudah tersimpan
  // (mode re-reference setelah boot). Tahap ukur dua titik harus fixed F1 -> F2.
  if (calibrationStage != CAL_STAGE_REREFERENCE) {
    setInfo(calibrationStage == CAL_STAGE_SET_FLOOR1
              ? "STAGE1_FIXED_FLOOR1_HOLD_MODE_TO_SET"
              : "STAGE2_FIXED_FLOOR2_HOLD_MODE_TO_SAVE");
    Serial.printf("[CAL][MODE-CLICK] stage=%u floor fixed at %d; hold MODE to confirm.\n",
                  (unsigned)calibrationStage, ui.calibrationFloor);
    touch();
    displayPublish();
    return;
  }
  ui.calibrationFloor = (ui.calibrationFloor >= 3) ? 1 : (ui.calibrationFloor + 1);
  calibrationStartedAt = millis();
  setInfo(ui.calibrationFloor == 3 ? "SELECT_REMEASURE_HOLD_MODE" : "REFERENCE_FLOOR_SELECTION_CHANGED");
  if (ui.calibrationFloor == 3) Serial.println("[CAL][MODE-CLICK] dipilih UKUR ULANG F1+F2. Tahan MODE untuk menghapus travel lama dan mulai Tahap 1.");
  else Serial.printf("[CAL][MODE-CLICK] re-reference selected FLOOR %d.\n", ui.calibrationFloor);
  touch();
  displayPublish();
}

void handleModeButton(bool mode, unsigned long now) {
  if (mode && !modePrev) {
    modePressAt = now;
    modeLongHandled = false;
    // MODE menjadi stop tambahan selama wizard: menekan MODE saat jog berjalan
    // langsung menghentikan motor dan mewajibkan UP/DOWN dilepas sebelum jog ulang.
    if (ui.calibrationActive) {
      if (motorDir != 0) motorStop();
      calibrationJogStarted = false;
      calibrationJogDirection = 0;
      if (btnUp.stable || btnDown.stable) calibrationJogLocked = true;
    }
    touch();
  }

  if (mode && !modeLongHandled && ui.calibrationActive &&
      now - modePressAt >= CALIBRATION_CONFIRM_HOLD_MS) {
    modeLongHandled = true;
    confirmCalibration();
  } else if (mode && !modeLongHandled && !ui.calibrationActive &&
             now - modePressAt >= CALIBRATION_CONFIRM_HOLD_MS) {
    // Long-press di luar wizard tidak mengubah posisi atau menjalankan motor.
    modeLongHandled = true;
  }

  if (!mode && modePrev) {
    if (!modeLongHandled) {
      if (ui.calibrationActive) {
        // Satu klik langsung mengganti pilihan lantai; tahan MODE untuk konfirmasi.
        toggleCalibrationFloor();
      } else {
        modeClickCount++;
        modeLastReleaseAt = now;
        if (modeClickCount >= MODE_CALIBRATION_CLICKS) {
          modeClickCount = 0;
          beginCalibration();
        }
      }
    }
  }
  modePrev = mode;

  if (!mode && !ui.calibrationActive && modeClickCount > 0 &&
      now - modeLastReleaseAt > MODE_MULTI_CLICK_WINDOW_MS) {
    ui.page = (ui.page + modeClickCount) % UI_PAGE_COUNT;
    modeClickCount = 0;
    touch();
    displayPublish();
  }

  if (ui.calibrationActive && now - calibrationStartedAt > CALIBRATION_TIMEOUT_MS) {
    cancelCalibration();
  }
}

// ---------------------------------------------------------------------
// Pembuat snapshot UI. displayTask hanya membaca salinan di antrean.
// ---------------------------------------------------------------------
void syncUi(unsigned long now) {
  ui.state = (int)currentState;
  ui.ticks = readEncoderTicks();
  ui.target = targetTicks;
  ui.motorDir = motorDir;
  ui.pwm = lastPwm;
  ui.lastGoal = diagLastGoal;
  ui.lastGoalValid = diagGoalValid;
  ui.moveOrigin = moveOriginTicks;
  ui.modeHeldMs = (btnMode.stable && !modeLongHandled) ? (now - modePressAt) : 0;
  ui.build = FIRMWARE_BUILD_ID;
  ui.pwmReady = pwmReady;
  ui.lastFault = lastFault;
  ui.homed = positionValid;
  ui.positionValid = positionValid;
  ui.travelCalibrated = floorTravelCalibrated;
  ui.floor2Ticks = calibratedFloor2Ticks;
  ui.info = lastInfo;
  ui.sensorDist[0] = sensorCm(0);
  ui.sensorDist[1] = sensorCm(1);
  ui.sensorValid[0] = ui.sensorDist[0] >= 0.0f;
  ui.sensorValid[1] = ui.sensorDist[1] >= 0.0f;
  ui.rackStatus[0] = (int)rackCapacityStatus(0);
  ui.rackStatus[1] = (int)rackCapacityStatus(1);
  ui.upPressed = btnUp.stable;
  ui.downPressed = btnDown.stable;
  ui.modePressed = btnMode.stable;
  ui.estopPressed = estopActive();
  ui.networkEnabled = (FEATURE_NETWORK != 0);
  ui.rackEnabled = (FEATURE_RACK != 0);
  ui.calStage = (int)calibrationStage;
  ui.calDelta = ui.ticks - calibrationOriginTicks;
#if FEATURE_NETWORK
  ui.wifi = (WiFi.status() == WL_CONNECTED);
  ui.mqtt = mqttLinkUp();
  ui.admin = netAdminState();
#else
  ui.wifi = false;
  ui.mqtt = false;
#endif

  if (now - tNet > 2000) {
    tNet = now;
#if FEATURE_NETWORK
    if (ui.wifi) {
      ui.rssi = WiFi.RSSI();
      IPAddress address = WiFi.localIP();
      snprintf(ui.ip, sizeof(ui.ip), "%d.%d.%d.%d", address[0], address[1], address[2], address[3]);
    } else
#endif
    {
      ui.rssi = -127;
      snprintf(ui.ip, sizeof(ui.ip), "-");
    }
  }

  if (now - tStat >= 500) {
    long ticks = ui.ticks;
    unsigned long dt = now - tStat;
    ui.speedTps = (dt > 0) ? (ticks - lastStatTicks) * 1000L / (long)dt : 0;
    lastStatTicks = ticks;
    tStat = now;
  }
  displayPublish();
}

// ---------------------------------------------------------------------
// Pindai OLED (I2C)
// ---------------------------------------------------------------------
uint8_t scanOled() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  for (uint8_t address = 0x3C; address <= 0x3D; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("OLED ditemukan di 0x%02X\n", address);
      return address;
    }
  }
  Serial.println("OLED tidak ditemukan. Periksa VCC, GND, SDA=21, SCL=22.");
  return 0;
}

// ---------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------
void setup() {
  // Keluaran motor dalam kondisi aman sebelum jaringan/layar diinisialisasi.
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  pwmReady = setupPwm();
  if (pwmReady) writePwm(0);

  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.printf("=== SMART WAREHOUSE / %s ===\n", FIRMWARE_BUILD_ID);
  Serial.println("GPIO33 = MODE only. HC-SR04 = LIFO rack capacity monitoring only.");
  Serial.printf("[CONFIG] ENC_INVERT=%d MOTOR_DIR_INVERT=%d; UP should produce positive encoder ticks.\n", ENC_INVERT, MOTOR_DIR_INVERT);
  Serial.printf("[CONFIG] FEATURE_NETWORK=%d FEATURE_RACK=%d\n", FEATURE_NETWORK, FEATURE_RACK);
  Serial.println("HC-SR04 is NOT used as lift position reference or homing sensor.");
  loadFloorTravelCalibration();

  pinMode(PIN_ENC_A, INPUT);
  pinMode(PIN_ENC_B, INPUT);
  pinMode(PIN_ESTOP, INPUT_PULLUP);
  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  buttonInit(btnUp, LOW);
  buttonInit(btnDown, LOW);
  buttonInit(btnMode, MODE_ACTIVE_LEVEL);
  upButtonPrev = btnUp.stable;
  downButtonPrev = btnDown.stable;

  ui.info = "BOOTING";
  ui.bootText = "OLED";
  ui.bootTarget = 8;
  ui.lastActivity = millis();
  ui.page = 0;
  ui.positionValid = false;
  ui.homed = false;
  ui.travelCalibrated = floorTravelCalibrated;
  ui.floor2Ticks = calibratedFloor2Ticks;
  ui.calibrationActive = false;
  ui.calibrationFloor = 0;
  ui.rackStatus[0] = (int)RACK_CAPACITY_UNKNOWN;
  ui.rackStatus[1] = (int)RACK_CAPACITY_UNKNOWN;
  snprintf(ui.ip, sizeof(ui.ip), "-");

  if (!displayQueueInit()) {
    Serial.println("ERROR: UI queue allocation failed; OLED task not started.");
  }
  uint8_t oledAddress = scanOled();
  if (oledAddress) displayInit(oledAddress);
  displayPublish();
  if (oledOk) {
    BaseType_t displayCreated = xTaskCreatePinnedToCore(displayTask, "oled", 10240, NULL, 1, NULL, 0);
    if (displayCreated != pdPASS) Serial.println("ERROR: OLED task creation failed.");
  }

  ui.bootText = "Sensor"; ui.bootTarget = 25; displayPublish();
#if FEATURE_RACK
  sensorsInit();
  BaseType_t sensorCreated = xTaskCreatePinnedToCore(sensorTask, "sensor", 3072, NULL, 1, NULL, 0);
  if (sensorCreated != pdPASS) Serial.println("ERROR: sensor task creation failed.");
  delay(250);
#endif

  ui.bootText = "Encoder"; ui.bootTarget = 40; displayPublish();
  encPrev = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), countEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), countEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ESTOP), estopISR,
                  (ESTOP_ACTIVE_LEVEL == HIGH) ? RISING : FALLING);
  delay(100);

#if FEATURE_NETWORK
  ui.bootText = "WiFi"; ui.bootTarget = 60; displayPublish();
  // netBegin() hanya menyalakan task jaringan; MQTT tersambung di belakang layar.
  if (!netBegin()) Serial.println("ERROR: network task creation failed.");
  if (wifiConfigured()) {
    // Tunggu singkat hanya demi tampilan boot; motor belum aktif dan posisi belum valid.
    for (unsigned long start = millis(); WiFi.status() != WL_CONNECTED && millis() - start < WIFI_BOOT_WAIT_MS; ) {
      delay(100);
    }
  }
  ui.bootText = "MQTT"; ui.bootTarget = 85; displayPublish();
  delay(150);
#endif

  ui.bootText = "Siap"; ui.bootTarget = 100; displayPublish();
  delay(500);
  ui.bootDone = true;

  motorStop();
  if (!pwmReady) {
    invalidatePosition("PWM_INIT_FAILED");
    setState(STATE_ERROR);
  } else if (estopActive()) {
    enterEstop();
  } else {
    invalidatePosition("POSITION_UNREFERENCED");
    setState(STATE_POSITION_UNREFERENCED);
  }
  touch();
  syncUi(millis());

  Serial.println("BOOT complete. Position is UNREFERENCED.");
  Serial.println("MODE triple-click opens position calibration wizard.");
  Serial.println("Inside calibration wizard: hold UP/DOWN for supervised alignment jog.");
  Serial.println("Two-point flow: stage 1 is fixed FLOOR 1; stage 2 is fixed FLOOR 2. No MODE short-click is needed inside these stages.");
  Serial.println("After travel is saved and position is referenced: tap UP once = PID FLOOR 2; tap DOWN once = PID FLOOR 1.");
  Serial.println("After restart, position must still be physically referenced; stored travel geometry is retained.");
  Serial.println("HC-SR04 reports rack capacity/near-full only; it never suggests FLOOR 1/2.");
  Serial.println("HOME/RESET disabled. Serial ACK_ERROR only acknowledges a cleared fault; it does not validate position.");
  if (!floorTravelCalibrated) {
    Serial.println("PID floor buttons locked until two-point calibration has measured FLOOR 1 -> FLOOR 2 and saved it to NVS.");
  }

#if FEATURE_LOOP_WATCHDOG
  // Diaktifkan paling akhir: setup() punya delay panjang yang tidak boleh memicu reset.
  // Setelah ini loop() wajib memanggil feedLoopWDT(); bila macet, ESP32 reset.
  enableLoopWDT();
  Serial.println("[WDT] loop watchdog aktif.");
#endif
}

// ---------------------------------------------------------------------
// Loop utama
// ---------------------------------------------------------------------
void loop() {
#if FEATURE_LOOP_WATCHDOG
  feedLoopWDT();
#endif
  unsigned long now = millis();
  bool busy = (currentState == STATE_MOVING || currentState == STATE_MANUAL ||
               (ui.calibrationActive && motorDir != 0));

#if FEATURE_NETWORK
  {
    // Jaringan berjalan di netTask. Di sini hanya mengambil hasilnya, tanpa menunggu apa pun.
    char netBuf[NET_EVENT_MAX];
    while (mqttTakeEvent(netBuf, sizeof(netBuf))) Serial.println(netBuf);
    bool fromAdmin = false;
    while (mqttTakeCommand(netBuf, sizeof(netBuf), &fromAdmin)) handleCommand(netBuf, fromAdmin);
  }
#endif
  handleSerial();

  if (now - lastTelemetry >= (busy ? TELEMETRY_BUSY_MS : TELEMETRY_IDLE_MS)) publishTelemetry();

  if (lastInfo[0] && currentState == STATE_IDLE && !ui.calibrationActive &&
      now - infoSince > 6000UL) lastInfo = "";

  // E-STOP gets priority over buttons, MQTT and state-machine motion.
  if ((estopFlag || estopActive()) && currentState != STATE_ESTOP) enterEstop();

  bool up = updateButton(btnUp, LOW);
  bool down = updateButton(btnDown, LOW);
  bool mode = updateButton(btnMode, MODE_ACTIVE_LEVEL);
  const bool upPressedEdge = up && !upButtonPrev;
  const bool downPressedEdge = down && !downButtonPrev;
  if (upPressedEdge) Serial.println("[BUTTON] UP press edge detected on GPIO13.");
  if (downPressedEdge) Serial.println("[BUTTON] DOWN press edge detected on GPIO14.");
  upButtonPrev = up;
  downButtonPrev = down;

  if (upPressedEdge || downPressedEdge || busy || currentState == STATE_ESTOP) touch();
  handleModeButton(mode, now);

  if (ui.calibrationActive) handleCalibrationJog(up, down, now);
  else handleFloorButtonPress(upPressedEdge, downPressedEdge, up, down);

  // Handler di atas bisa menulis timestamp millis() yang lebih baru dari `now`
  // (mis. moveStartTime). Tanpa refresh, `now - timestamp` underflow (unsigned)
  // dan memicu MOTOR_STALL / MOTION_TIMEOUT seketika.
  now = millis();

  if (settleReportAt && now >= settleReportAt) {
    const long settled = readEncoderTicks();
    Serial.printf("[SETTLED] ticks=%ld goal=%ld error=%ld (negatif = kebablasan ke bawah, positif = berhenti kurang)\n",
                  settled, settleGoal, settleGoal - settled);
    settleReportAt = 0;
  }

  switch (currentState) {
    case STATE_BOOT:
    case STATE_POSITION_UNREFERENCED:
    case STATE_IDLE:
    case STATE_MANUAL:
      break;

    case STATE_MOVING: {
      long ticks = readEncoderTicks();
      long remaining = (targetTicks - ticks) * moveDir;

      if (now - speedSampleAt >= SPEED_SAMPLE_MS) {
        moveSpeedTps = (ticks - speedSampleTicks) * 1000L / (long)(now - speedSampleAt);
        speedSampleTicks = ticks;
        speedSampleAt = now;
      }
      if (now - moveTraceAt >= 100) {
        moveTraceAt = now;
        Serial.printf("[MOVE] ms=%lu ticks=%ld rem=%ld spd=%ldt/s dir=%d pwm=%d\n",
                      now - moveStartTime, ticks, remaining, moveSpeedTps, motorDir, lastPwm);
      }
      long brakeLead = (moveSpeedTps * moveDir) * (long)BRAKE_LEAD_MS / 1000L;  // hanya kecepatan menuju target
      brakeLead = constrain(brakeLead, 0L, BRAKE_LEAD_MAX_TICKS);

      if (remaining <= TOLERANCE_TICKS + brakeLead - BRAKE_RECOIL_TICKS) {
        Serial.printf("[ARRIVED] ticks=%ld goal=%ld remaining=%ld speed=%ldt/s lead=%ld\n",
                      ticks, targetTicks, remaining, moveSpeedTps, brakeLead);
        settleGoal = targetTicks;
        settleReportAt = now + SETTLE_REPORT_MS;
        motorStop();
        setState(STATE_ARRIVED);
        break;
      }
      // Soft limit hanya berlaku bila bergerak MENJAUH dari rentang lantai.
      // Gerakan kembali ke dalam rentang (pemulihan setelah kebablasan) tetap diizinkan.
      const bool belowRange = ticks < (FLOOR_1_TICKS - FLOOR_SOFT_OVERRUN_TICKS);
      const bool aboveRange = ticks > (calibratedFloor2Ticks + FLOOR_SOFT_OVERRUN_TICKS);
      if ((belowRange && moveDir < 0) || (aboveRange && moveDir > 0)) {
        fail(aboveRange ? "TOP_SOFT_LIMIT" : "BOTTOM_SOFT_LIMIT");
        break;
      }
      // Masa awal: geseran awal berlawanan arah ditoleransi sampai WRONG_DIRECTION_HARD_TICKS.
      // Setelah masa awal, patokan dipindah ke posisi saat itu: yang dinilai hanya gerak
      // berikutnya (arah salah yang terus berlanjut tetap memicu fault).
      if (!moveRebased && now - moveStartTime >= WRONG_DIRECTION_GRACE_MS) {
        moveRebased = true;
        Serial.printf("[MOVE-REBASE] start=%ld -> %ld (geseran awal %ld tick)\n",
                      moveStartTicks, ticks, (ticks - moveStartTicks) * moveDir);
        moveStartTicks = ticks;
        moveProgressBest = 0;
        moveProgressAt = now;
      }
      const long wrongLimit = moveRebased ? WRONG_DIRECTION_TICKS : WRONG_DIRECTION_HARD_TICKS;
      if ((ticks - moveStartTicks) * moveDir < -wrongLimit) {
        fail("WRONG_DIRECTION");
        break;
      }

      if (ticks != lastTickSeen) {
        lastTickSeen = ticks;
        lastTickChange = now;
      }
      diagNoTickMs = now - lastTickChange;
      diagMoveMs = now - moveStartTime;
      if (now - lastTickChange > STALL_FAIL_MS + RAMP_MS) {
        fail("MOTOR_STALL");
        break;
      }
      if (now - moveStartTime > MOVE_TIMEOUT_MS) {
        fail("MOTION_TIMEOUT");
        break;
      }
      {
        const long progress = (ticks - moveStartTicks) * moveDir;
        if (!moveRebased) {
          moveProgressAt = now;   // jam NO_PROGRESS baru jalan setelah masa awal
        } else if (progress >= moveProgressBest + PROGRESS_MIN_TICKS) {
          moveProgressBest = progress;
          moveProgressAt = now;
        }
        if (now - moveProgressAt > PROGRESS_FAIL_MS + RAMP_MS) {
          Serial.printf("[NO_PROGRESS] ticks=%ld goal=%ld best=%ld rem=%ld\n", ticks, targetTicks, moveProgressBest, remaining);
          if (remaining <= FLOOR_TOL_TICKS) {
            // Mentok sangat dekat target (dalam toleransi lantai): anggap sampai, bukan fault.
            // Terjadi saat lift menyentuh dudukan/ujung rel sebelum tick mencapai toleransi ARRIVED.
            Serial.printf("[ARRIVED-STALL] ticks=%ld goal=%ld remaining=%ld (dalam toleransi lantai %ld)\n",
                          ticks, targetTicks, remaining, FLOOR_TOL_TICKS);
            settleGoal = targetTicks;
            settleReportAt = now + SETTLE_REPORT_MS;
            motorStop();
            setState(STATE_ARRIVED);
            break;
          }
          fail("NO_PROGRESS");
          break;
        }
      }
      int drivePwm = pid.compute(targetTicks, ticks);
      if (moveDir > 0 && drivePwm < PWM_MOVE_MIN_UP) drivePwm = PWM_MOVE_MIN_UP;
      motorDrive(moveDir, drivePwm);
      break;
    }

    case STATE_ARRIVED:
      motorStop();
      setInfo("ARRIVED");
      setState(STATE_IDLE);
      break;

    case STATE_ESTOP:
      motorStop();
      if (!estopActive()) {
        invalidatePosition("ESTOP_RELEASED_RECALIBRATE");
        setState(STATE_POSITION_UNREFERENCED);
      }
      break;

    case STATE_ERROR:
      motorStop();
      break;
  }

  syncUi(now);

  if (now - tSerialStat >= 2000) {
    tSerialStat = now;
    Serial.printf("[%s] ticks=%ld t/s=%ld dir=%d pwm=%d pos_valid=%d floor2=%ld",
                  stateName(currentState), readEncoderTicks(), (long)ui.speedTps,
                  motorDir, lastPwm, positionValid ? 1 : 0, calibratedFloor2Ticks);
#if FEATURE_RACK
    Serial.printf(" R1=%.1fcm/%s R2=%.1fcm/%s",
                  sensorCm(0), rackCapacityStatusName(rackCapacityStatus(0)),
                  sensorCm(1), rackCapacityStatusName(rackCapacityStatus(1)));
#endif
#if FEATURE_NETWORK
    Serial.printf(" wifi=%d mqtt=%d", ui.wifi ? 1 : 0, ui.mqtt ? 1 : 0);
#endif
    Serial.println();
  }
}
