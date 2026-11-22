#include <Arduino.h>
#include <ctype.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "Config.h"
#include "SystemState.h"
#include "Button.h"
#include "PIDControl.h"
#include "MQTTManager.h"
#include "Monitor.h"

// -----------------------------------------------------------------------------
// Global state — motor/control state hanya dimodifikasi dari loop(),
// kecuali estopFlag dan encoderTicks yang ditulis oleh ISR.
// -----------------------------------------------------------------------------
static SystemState currentState = STATE_BOOT;
static volatile int32_t encoderTicks = 0;
static volatile bool estopFlag = false;
static portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;

static bool positionValid = false; // tidak pernah diasumsikan valid saat boot
static long targetTicks = FLOOR_1_TICKS;
static const char* lastInfo = "BOOT";
static int motorPwm = 0;
static int8_t motorDirection = 0; // 1=UP, -1=DOWN, 0=STOP
static bool pwmReady = false;
static unsigned long lastMotorOffMs = 0;

static unsigned long moveStartMs = 0;
static unsigned long manualStartMs = 0;
static unsigned long lastEncoderProgressMs = 0;
static unsigned long lastTelemetryMs = 0;
static unsigned long lastUiSnapshotMs = 0;
static unsigned long arrivedAtMs = 0;
static unsigned long zeroArmDeadlineMs = 0;
static bool manualJogLockedUntilRelease = false;
static long lastProgressTicks = 0;

static long speedTicksPerSecond = 0;
static long lastSpeedSampleTicks = 0;
static unsigned long lastSpeedSampleMs = 0;

static int8_t uiPage = -1; // -1=screensaver; 0..3=detail pages
static unsigned long lastUiActivityMs = 0;

static DebouncedButton btnUp{};
static DebouncedButton btnDown{};
static DebouncedButton btnMode{};

static LiftPID pid(PID_KP, PID_KI, PID_KD, PID_MIN_PWM, PID_MAX_PWM);
static WarehouseMonitor monitor;

static QueueHandle_t uiQueue = nullptr;
static QueueHandle_t sensorQueue = nullptr;
static SensorSnapshot sensorCache = {-1.0f, -1.0f, 0, 0, 0, 0, false, false};

static char serialLine[48];
static size_t serialLineLength = 0;

// -----------------------------------------------------------------------------
// Encoder synchronization
// -----------------------------------------------------------------------------
static long readEncoderTicks() {
  long value;
  portENTER_CRITICAL(&encoderMux);
  value = static_cast<long>(encoderTicks);
  portEXIT_CRITICAL(&encoderMux);
  return value;
}

static void writeEncoderTicks(long value) {
  portENTER_CRITICAL(&encoderMux);
  encoderTicks = static_cast<int32_t>(value);
  portEXIT_CRITICAL(&encoderMux);
}

void IRAM_ATTR countEncoder() {
  const int channelB = gpio_get_level(static_cast<gpio_num_t>(PIN_ENC_B));
  portENTER_CRITICAL_ISR(&encoderMux);
  if ((channelB == HIGH) != ENCODER_INVERT) {
    ++encoderTicks;
  } else {
    --encoderTicks;
  }
  portEXIT_CRITICAL_ISR(&encoderMux);
}

// E-STOP software ISR hanya tindakan tambahan. Jalur pemutus suplai motor
// secara hardware tetap wajib dipasang dan diuji.
void IRAM_ATTR estopISR() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  estopFlag = true;
}

// -----------------------------------------------------------------------------
// PWM compatibility: Arduino-ESP32 Core 2.x and 3.x
// -----------------------------------------------------------------------------
static bool setupPwm() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  return ledcAttach(PIN_PWM, PWM_FREQ, PWM_RES);
#else
  const double configuredFrequency = ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  if (configuredFrequency <= 0.0) return false;
  ledcAttachPin(PIN_PWM, PWM_CHANNEL);
  return true;
#endif
}

static void writePwm(int duty) {
  duty = constrain(duty, 0, 255);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_PWM, duty);
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
}

// -----------------------------------------------------------------------------
// Motor abstraction. Direction reversal always passes through a stop/dead-time.
// -----------------------------------------------------------------------------
static void stopMotor() {
  const bool wasActive = (motorDirection != 0 || motorPwm != 0);
  if (!wasActive) return;

  lastMotorOffMs = millis();
  if (pwmReady) writePwm(0);
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  motorPwm = 0;
  motorDirection = 0;
}

static void driveMotor(int8_t direction, int pwm) {
  if (!pwmReady || direction == 0 || pwm <= 0 || estopFlag ||
      digitalRead(PIN_ESTOP) == ESTOP_ACTIVE_LEVEL) {
    stopMotor();
    return;
  }

  if (motorDirection != 0 && motorDirection != direction) {
    stopMotor();
    return;
  }

  if (motorDirection == 0 &&
      (millis() - lastMotorOffMs) < MOTOR_DIRECTION_DEADTIME_MS) {
    return;
  }

  if (direction > 0) {
    digitalWrite(PIN_IN1, HIGH);
    digitalWrite(PIN_IN2, LOW);
  } else {
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, HIGH);
  }

  motorDirection = direction;
  motorPwm = constrain(pwm, 0, PID_MAX_PWM);
  writePwm(motorPwm);
}

static bool motorIsActive() {
  return motorDirection != 0 && motorPwm > 0;
}

static bool estopActive() {
  return digitalRead(PIN_ESTOP) == ESTOP_ACTIVE_LEVEL;
}

// -----------------------------------------------------------------------------
// State and telemetry
// -----------------------------------------------------------------------------
static const char* stateName(SystemState state) {
  switch (state) {
    case STATE_BOOT:                  return "BOOT";
    case STATE_POSITION_UNREFERENCED:return "POSITION_UNREFERENCED";
    case STATE_IDLE:                  return "IDLE";
    case STATE_MOVING:                return "MOVING";
    case STATE_ARRIVED:               return "ARRIVED";
    case STATE_MANUAL:                return "MANUAL";
    case STATE_ESTOP:                 return "ESTOP";
    case STATE_ERROR:                 return "ERROR";
  }
  return "UNKNOWN";
}

static int getFloor(long ticks) {
  if (!positionValid) return 0;
  if (labs(ticks - FLOOR_1_TICKS) <= FLOOR_TOL_TICKS) return 1;
  if (labs(ticks - FLOOR_2_TICKS) <= FLOOR_TOL_TICKS) return 2;
  return 0;
}

static void publishTelemetry() {
  const long ticks = readEncoderTicks();
  char payload[320];
  snprintf(payload, sizeof(payload),
           "{\"state\":\"%s\",\"floor\":%d,\"ticks\":%ld,\"target\":%ld,"
           "\"dist1\":%.1f,\"dist2\":%.1f,\"sensor1_valid\":%s,\"sensor2_valid\":%s,"
           "\"estop\":%s,\"position_valid\":%s,\"homed\":%s,\"pwm\":%d,"
           "\"direction\":%d,\"info\":\"%s\"}",
           stateName(currentState), getFloor(ticks), ticks, targetTicks,
           sensorCache.valid1 ? sensorCache.dist1 : -1.0f,
           sensorCache.valid2 ? sensorCache.dist2 : -1.0f,
           sensorCache.valid1 ? "true" : "false",
           sensorCache.valid2 ? "true" : "false",
           (currentState == STATE_ESTOP || estopActive()) ? "true" : "false",
           positionValid ? "true" : "false",
           positionValid ? "true" : "false", // legacy alias; bukan bukti auto-homing
           motorPwm, static_cast<int>(motorDirection), lastInfo ? lastInfo : "");
  mqttPublishTelemetry(payload);
  lastTelemetryMs = millis();
}

static void setState(SystemState nextState) {
  if (currentState == nextState) return;
  currentState = nextState;
  publishTelemetry();
}

static void failSystem(const char* reason) {
  stopMotor();
  positionValid = false;
  targetTicks = readEncoderTicks();
  lastInfo = reason;
  Serial.print(F("[ERROR] "));
  Serial.println(reason);
  setState(STATE_ERROR);
}

static void enterEstop() {
  stopMotor();
  positionValid = false;
  targetTicks = readEncoderTicks();
  estopFlag = false;
  lastInfo = "ESTOP_ACTIVE_POSITION_INVALID";
  Serial.println(F("[SAFETY] E-STOP active; position invalidated."));
  setState(STATE_ESTOP);
}

// -----------------------------------------------------------------------------
// Position calibration
// Dengan bottom limit switch dihapus, ZERO hanya boleh dilakukan setelah
// operator secara fisik menempatkan lift di titik referensi FLOOR 1.
// ZERO tidak menggerakkan motor.
// -----------------------------------------------------------------------------
static bool canSetZero() {
  return !estopActive() && currentState != STATE_ESTOP &&
         !motorIsActive() && motorPwm == 0 &&
         !btnUp.stablePressed && !btnDown.stablePressed;
}

static void setManualPositionReference() {
  if (!canSetZero()) {
    Serial.println(F("[ZERO] rejected: E-STOP, motor, atau tombol manual aktif."));
    lastInfo = "ZERO_REJECTED_CHECK_INPUTS";
    publishTelemetry();
    return;
  }

  stopMotor();
  writeEncoderTicks(FLOOR_1_TICKS);
  targetTicks = FLOOR_1_TICKS;
  positionValid = true;
  pid.reset();
  zeroArmDeadlineMs = 0;
  lastInfo = "MANUAL_REFERENCE_SET";
  Serial.println(F("[ZERO] position reference set to FLOOR 1."));
  setState(STATE_IDLE);
}

static void handleSerialLine(char* line) {
  // Normalize command in-place.
  size_t begin = 0;
  while (line[begin] && isspace(static_cast<unsigned char>(line[begin]))) ++begin;
  if (begin > 0) memmove(line, line + begin, strlen(line + begin) + 1);
  size_t length = strlen(line);
  while (length > 0 && isspace(static_cast<unsigned char>(line[length - 1]))) {
    line[--length] = '\0';
  }
  for (size_t i = 0; line[i]; ++i) {
    line[i] = static_cast<char>(toupper(static_cast<unsigned char>(line[i])));
  }

  if (strcmp(line, "ZERO") == 0) {
    if (!canSetZero() || (currentState != STATE_POSITION_UNREFERENCED && currentState != STATE_ERROR)) {
      Serial.println(F("[ZERO] unavailable. Stop motor, release buttons/E-STOP, then check state."));
      return;
    }
    zeroArmDeadlineMs = millis() + 10000UL;
    Serial.println(F("[ZERO] WARNING: ensure carriage is physically aligned at FLOOR 1."));
    Serial.println(F("[ZERO] Type CONFIRM ZERO within 10 seconds. This command does not move the motor."));
    return;
  }

  if (strcmp(line, "CONFIRM ZERO") == 0) {
    if (zeroArmDeadlineMs == 0 || static_cast<long>(millis() - zeroArmDeadlineMs) > 0) {
      zeroArmDeadlineMs = 0;
      Serial.println(F("[ZERO] confirmation expired. Type ZERO again."));
      return;
    }
    setManualPositionReference();
    return;
  }

  if (strcmp(line, "STOP") == 0) {
    stopMotor();
    if (currentState == STATE_MOVING || currentState == STATE_MANUAL) {
      lastInfo = "STOPPED_BY_SERIAL";
      setState(positionValid ? STATE_IDLE : STATE_POSITION_UNREFERENCED);
    }
    return;
  }

  if (strcmp(line, "STATUS") == 0) {
    Serial.printf("state=%s ticks=%ld position_valid=%s estop=%s\n",
                  stateName(currentState), readEncoderTicks(),
                  positionValid ? "true" : "false",
                  estopActive() ? "true" : "false");
    return;
  }

  if (line[0] != '\0') {
    Serial.println(F("Commands: ZERO, CONFIRM ZERO, STOP, STATUS"));
  }
}

static void serviceSerialConsole() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (serialLineLength > 0) {
        serialLine[serialLineLength] = '\0';
        handleSerialLine(serialLine);
        serialLineLength = 0;
      }
    } else if (serialLineLength < sizeof(serialLine) - 1) {
      serialLine[serialLineLength++] = c;
    } else {
      serialLineLength = 0;
      Serial.println(F("[SERIAL] command too long; discarded."));
    }
  }

  if (zeroArmDeadlineMs != 0 &&
      static_cast<long>(millis() - zeroArmDeadlineMs) > 0) {
    zeroArmDeadlineMs = 0;
    Serial.println(F("[ZERO] confirmation window expired."));
  }
}

// -----------------------------------------------------------------------------
// Motion commands
// -----------------------------------------------------------------------------
static void startMove(long target) {
  if (!positionValid || currentState != STATE_IDLE) return;
  if (target < SOFT_LIMIT_BOTTOM_TICKS || target > SOFT_LIMIT_TOP_TICKS) {
    lastInfo = "TARGET_OUT_OF_RANGE";
    publishTelemetry();
    return;
  }

  targetTicks = target;
  pid.reset();
  moveStartMs = millis();
  lastEncoderProgressMs = moveStartMs;
  lastProgressTicks = readEncoderTicks();
  lastInfo = "AUTO_MOVE_STARTED";
  setState(STATE_MOVING);
}

static void handleMqttCommand(const char* command) {
  if (!command || command[0] == '\0') return;
  Serial.printf("[MQTT] command: %s\n", command);

  if (currentState == STATE_ESTOP || estopActive()) {
    lastInfo = "COMMAND_BLOCKED_ESTOP";
    publishTelemetry();
    return;
  }

  if (strcmp(command, "STOP") == 0) {
    stopMotor();
    if (currentState == STATE_MOVING || currentState == STATE_MANUAL) {
      lastInfo = "STOPPED_BY_MQTT";
      setState(positionValid ? STATE_IDLE : STATE_POSITION_UNREFERENCED);
    }
    return;
  }

  // HOME/RESET tidak valid lagi karena bottom limit switch sudah dilepas.
  if (strcmp(command, "HOME") == 0 || strcmp(command, "RESET") == 0 ||
      strcmp(command, "ZERO") == 0) {
    lastInfo = "NO_BOTTOM_LIMIT_USE_LOCAL_ZERO";
    publishTelemetry();
    return;
  }

  if (!positionValid) {
    lastInfo = "POSITION_UNREFERENCED";
    publishTelemetry();
    return;
  }

  if (currentState != STATE_IDLE) {
    lastInfo = "BUSY";
    publishTelemetry();
    return;
  }

  if (strcmp(command, "0") == 0 || strcmp(command, "GROUND") == 0 ||
      strcmp(command, "1") == 0) {
    startMove(FLOOR_1_TICKS);
  } else if (strcmp(command, "2") == 0) {
    startMove(FLOOR_2_TICKS);
  } else {
    lastInfo = "UNKNOWN_COMMAND";
    publishTelemetry();
  }
}

static void updateManualControls() {
  const bool up = btnUp.stablePressed;
  const bool down = btnDown.stablePressed;

  if (currentState == STATE_ESTOP || currentState == STATE_ERROR ||
      currentState == STATE_POSITION_UNREFERENCED || currentState == STATE_BOOT ||
      currentState == STATE_ARRIVED) {
    return;
  }

  // Tombol UP/DOWN saat otomatis bergerak membatalkan gerakan dan mengambil alih.
  if (currentState == STATE_MOVING && (up || down)) {
    stopMotor();
    lastInfo = "AUTO_MOVE_MANUAL_OVERRIDE";
    setState(STATE_IDLE);
  }

  if (up && down) {
    if (currentState == STATE_MANUAL) stopMotor();
    manualJogLockedUntilRelease = true;
    lastInfo = "UP_DOWN_CONFLICT";
    if (currentState == STATE_MANUAL) setState(STATE_IDLE);
    return;
  }

  if (!up && !down) {
    manualJogLockedUntilRelease = false;
    if (currentState == STATE_MANUAL) {
      stopMotor();
      lastInfo = "MANUAL_RELEASED";
      setState(STATE_IDLE);
    }
    return;
  }

  if (manualJogLockedUntilRelease || !positionValid || (currentState != STATE_IDLE && currentState != STATE_MANUAL)) {
    return;
  }

  const int8_t requestedDirection = up ? 1 : -1;
  const long ticks = readEncoderTicks();

  if ((requestedDirection > 0 && ticks >= SOFT_LIMIT_TOP_TICKS) ||
      (requestedDirection < 0 && ticks <= SOFT_LIMIT_BOTTOM_TICKS)) {
    stopMotor();
    if (currentState == STATE_MANUAL) setState(STATE_IDLE);
    lastInfo = "SOFTWARE_TRAVEL_LIMIT";
    return;
  }

  if (currentState != STATE_MANUAL) {
    manualStartMs = millis();
    lastEncoderProgressMs = manualStartMs;
    lastProgressTicks = ticks;
    lastInfo = "MANUAL_JOG";
    setState(STATE_MANUAL);
  }

  if (millis() - manualStartMs >= MAX_MANUAL_JOG_MS) {
    stopMotor();
    manualJogLockedUntilRelease = true;
    lastInfo = "MANUAL_JOG_TIMEOUT_RELEASE_BUTTON";
    setState(STATE_IDLE);
    Serial.println(F("[MANUAL] 3-second jog limit reached; release button to re-arm."));
    return;
  }

  if (labs(ticks - lastProgressTicks) >= ENCODER_PROGRESS_MIN_TICKS) {
    lastProgressTicks = ticks;
    lastEncoderProgressMs = millis();
  } else if (motorIsActive() &&
             millis() - lastEncoderProgressMs >= ENCODER_STALL_TIMEOUT_MS) {
    failSystem("MANUAL_ENCODER_STALL");
    return;
  }

  driveMotor(requestedDirection, PWM_MANUAL);
}

static void updateAutomaticMotion() {
  const long ticks = readEncoderTicks();
  const long error = targetTicks - ticks;

  if (!positionValid) {
    failSystem("POSITION_INVALID_DURING_MOVE");
    return;
  }

  if (labs(error) <= TOLERANCE_TICKS) {
    stopMotor();
    arrivedAtMs = millis();
    lastInfo = "TARGET_REACHED";
    setState(STATE_ARRIVED);
    return;
  }

  if (ticks < SOFT_LIMIT_BOTTOM_TICKS || ticks > SOFT_LIMIT_TOP_TICKS) {
    failSystem("SOFTWARE_TRAVEL_LIMIT" );
    return;
  }

  if (millis() - moveStartMs >= MOVE_TIMEOUT_MS) {
    failSystem("MOTION_TIMEOUT");
    return;
  }

  if (labs(ticks - lastProgressTicks) >= ENCODER_PROGRESS_MIN_TICKS) {
    lastProgressTicks = ticks;
    lastEncoderProgressMs = millis();
  } else if (motorIsActive() &&
             millis() - lastEncoderProgressMs >= ENCODER_STALL_TIMEOUT_MS) {
    failSystem("AUTO_ENCODER_STALL");
    return;
  }

  const int pwm = pid.compute(targetTicks, ticks);
  driveMotor(error > 0 ? 1 : -1, pwm);
}

static void updateSpeedEstimate(unsigned long now) {
  if (lastSpeedSampleMs == 0) {
    lastSpeedSampleMs = now;
    lastSpeedSampleTicks = readEncoderTicks();
    return;
  }

  const unsigned long elapsed = now - lastSpeedSampleMs;
  if (elapsed < 250) return;

  const long currentTicks = readEncoderTicks();
  speedTicksPerSecond = (currentTicks - lastSpeedSampleTicks) * 1000L /
                        static_cast<long>(elapsed);
  lastSpeedSampleTicks = currentTicks;
  lastSpeedSampleMs = now;
}

// -----------------------------------------------------------------------------
// Monitor task: only this task owns OLED/I2C and HC-SR04 reads.
// -----------------------------------------------------------------------------
static void monitorTask(void* parameter) {
  (void)parameter;
  const bool oledReady = monitor.begin();
  Serial.println(oledReady ? F("[OLED] initialized") : F("[OLED] not detected; control remains available"));

  UiSnapshot currentSnapshot{};
  currentSnapshot.state = STATE_BOOT;
  currentSnapshot.page = -1;
  currentSnapshot.positionValid = false;
  currentSnapshot.estopActive = false;
  currentSnapshot.info[0] = '\0';

  unsigned long lastDrawMs = 0;
  unsigned long lastSensorPublishMs = 0;

  for (;;) {
    if (uiQueue) {
      UiSnapshot incoming;
      while (xQueueReceive(uiQueue, &incoming, 0) == pdTRUE) {
        currentSnapshot = incoming;
      }
    }

    const unsigned long now = millis();
    monitor.serviceSensors(now);

    if (sensorQueue && now - lastSensorPublishMs >= 100) {
      const SensorSnapshot sensors = monitor.sensorSnapshot(now);
      xQueueOverwrite(sensorQueue, &sensors);
      lastSensorPublishMs = now;
    }

    if (now - lastDrawMs >= OLED_REFRESH_MS) {
      monitor.draw(currentSnapshot, now);
      lastDrawMs = now;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void publishUiSnapshot(unsigned long now) {
  (void)now;
  if (!uiQueue) return;

  UiSnapshot snapshot{};
  snapshot.state = static_cast<uint8_t>(currentState);
  snapshot.encoderTicks = readEncoderTicks();
  snapshot.targetTicks = targetTicks;
  snapshot.speedTicksPerSecond = speedTicksPerSecond;
  snapshot.motorPwm = motorPwm;
  snapshot.motorDirection = motorDirection;
  snapshot.page = uiPage;
  snapshot.positionValid = positionValid;
  snapshot.estopActive = estopActive() || currentState == STATE_ESTOP;
  snapshot.buttonUp = btnUp.stablePressed;
  snapshot.buttonDown = btnDown.stablePressed;
  snapshot.buttonMode = btnMode.stablePressed;
  snapshot.buttonEstop = estopActive();
  snapshot.lastUiActivityMs = lastUiActivityMs;
  snprintf(snapshot.info, sizeof(snapshot.info), "%s", lastInfo ? lastInfo : "");
  xQueueOverwrite(uiQueue, &snapshot);
}

static void updateUiControls(unsigned long now) {
  updateButton(btnMode, DEBOUNCE_MS, now);
  if (btnMode.pressEvent) {
    uiPage = static_cast<int8_t>((uiPage + 1) % 4);
    lastUiActivityMs = now;
    Serial.printf("[OLED] page %d/4\n", uiPage + 1);
  }

  if (uiPage >= 0 && now - lastUiActivityMs >= UI_PAGE_TIMEOUT_MS) {
    uiPage = -1;
    Serial.println(F("[OLED] return to idle animation"));
  }
}

// -----------------------------------------------------------------------------
// Arduino setup / loop
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("=== SMART WAREHOUSE firmware ==="));
  Serial.println(F("Hardware profile: MODE GPIO33; bottom limit switch removed."));

  // Put motor direction and enable into safe state before initializing services.
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  pinMode(PIN_PWM, OUTPUT);
  digitalWrite(PIN_PWM, LOW);

  pinMode(PIN_ENC_A, INPUT);
  pinMode(PIN_ENC_B, INPUT);
  pinMode(PIN_ESTOP, INPUT_PULLUP);
  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);

  beginButton(btnUp, PIN_BTN_UP, LOW);
  beginButton(btnDown, PIN_BTN_DOWN, LOW);
  beginButton(btnMode, PIN_BTN_MODE, MODE_ACTIVE_LEVEL);

  pwmReady = setupPwm();
  if (pwmReady) writePwm(0);
  stopMotor();
  lastMotorOffMs = millis() - MOTOR_DIRECTION_DEADTIME_MS;

  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), countEncoder, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_ESTOP), estopISR,
                  ESTOP_ACTIVE_LEVEL == HIGH ? RISING : FALLING);

  if (!pwmReady) {
    lastInfo = "PWM_INIT_FAILED";
    setState(STATE_ERROR);
  } else if (estopActive()) {
    currentState = STATE_ESTOP;
    positionValid = false;
    lastInfo = "ESTOP_ACTIVE_AT_BOOT";
  } else {
    currentState = STATE_POSITION_UNREFERENCED;
    positionValid = false;
    lastInfo = "POSITION_NEEDS_MANUAL_REFERENCE";
  }

  uiQueue = xQueueCreate(1, sizeof(UiSnapshot));
  sensorQueue = xQueueCreate(1, sizeof(SensorSnapshot));
  if (!uiQueue || !sensorQueue) {
    Serial.println(F("[ERROR] failed to allocate monitor queues; OLED/sensor task not started."));
    if (uiQueue) vQueueDelete(uiQueue);
    if (sensorQueue) vQueueDelete(sensorQueue);
    uiQueue = nullptr;
    sensorQueue = nullptr;
  } else {
    const BaseType_t taskResult = xTaskCreatePinnedToCore(
        monitorTask, "warehouse_monitor", 5120, nullptr, 1, nullptr, 0);
    if (taskResult != pdPASS) {
      Serial.println(F("[ERROR] monitor task creation failed."));
      vQueueDelete(uiQueue);
      vQueueDelete(sensorQueue);
      uiQueue = nullptr;
      sensorQueue = nullptr;
    }
  }

  mqttSetup(handleMqttCommand);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "Nama_WiFi_Kamu") != 0) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    Serial.println(F("[WiFi] set WIFI_SSID/WIFI_PASSWORD in Config.h"));
  }

  lastUiActivityMs = millis();
  lastTelemetryMs = millis();
  lastSpeedSampleMs = 0;
  publishTelemetry();
  Serial.println(F("[POSITION] UNKNOWN after boot. Motor commands are locked."));
  Serial.println(F("[CALIBRATION] With motor power isolated, align carriage to FLOOR 1."));
  Serial.println(F("[CALIBRATION] Then send ZERO and CONFIRM ZERO in Serial Monitor."));
}

void loop() {
  const unsigned long now = millis();

  updateButton(btnUp, DEBOUNCE_MS, now);
  updateButton(btnDown, DEBOUNCE_MS, now);
  updateUiControls(now);
  serviceSerialConsole();

  // E-STOP has highest software priority. Hardware E-STOP must independently
  // interrupt motor power; this firmware logic is supplementary only.
  if ((estopFlag || estopActive()) && currentState != STATE_ESTOP) {
    enterEstop();
  }

  if (currentState == STATE_ESTOP && !estopActive()) {
    stopMotor();
    positionValid = false;
    lastInfo = "ESTOP_RELEASED_REQUIRES_REFERENCE";
    setState(STATE_ERROR);
  }

  updateManualControls();

  switch (currentState) {
    case STATE_BOOT:
      stopMotor();
      break;

    case STATE_POSITION_UNREFERENCED:
      stopMotor();
      break;

    case STATE_IDLE:
      stopMotor();
      break;

    case STATE_MOVING:
      updateAutomaticMotion();
      break;

    case STATE_ARRIVED:
      stopMotor();
      if (now - arrivedAtMs >= 700) {
        lastInfo = "READY";
        setState(STATE_IDLE);
      }
      break;

    case STATE_MANUAL:
      // Motor is refreshed by updateManualControls().
      break;

    case STATE_ESTOP:
    case STATE_ERROR:
      stopMotor();
      break;
  }

  updateSpeedEstimate(now);

  const bool motionActive = (currentState == STATE_MOVING || currentState == STATE_MANUAL);
  mqttMaintain(!motionActive);

  const unsigned long telemetryInterval = motionActive
      ? TELEMETRY_MOVING_MS : TELEMETRY_IDLE_MS;
  if (now - lastTelemetryMs >= telemetryInterval) {
    publishTelemetry();
  }

  if (now - lastUiSnapshotMs >= UI_SNAPSHOT_INTERVAL_MS) {
    publishUiSnapshot(now);
    lastUiSnapshotMs = now;
  }

  if (sensorQueue) {
    SensorSnapshot incoming;
    if (xQueueReceive(sensorQueue, &incoming, 0) == pdTRUE) {
      sensorCache = incoming;
    }
  }

  // Yield without a fixed delay; control loop remains responsive.
  taskYIELD();
}
