#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "Config.h"

// HC-SR04 berfungsi sebagai monitor kapasitas rak LIFO, BUKAN referensi posisi lift.
enum RackCapacityState : uint8_t {
  RACK_CAPACITY_UNKNOWN = 0, // belum ada sampel yang dapat diklasifikasikan
  RACK_CAPACITY_SPACE_OK = 1,
  RACK_CAPACITY_NEAR_FULL = 2,
  RACK_CAPACITY_NO_TARGET = 3, // tidak ada echo; bukan bukti sensor sehat atau rak kosong
  RACK_CAPACITY_STALE = 4     // task/sampel tidak diperbarui
};

static portMUX_TYPE sensorMux = portMUX_INITIALIZER_UNLOCKED;
static float distCmRaw[2] = { -1.0f, -1.0f };
static unsigned long sensorLastAttempt[2] = { 0, 0 };
static RackCapacityState rackState[2] = { RACK_CAPACITY_UNKNOWN, RACK_CAPACITY_UNKNOWN };
static uint8_t rackNearCount[2] = { 0, 0 };
static uint8_t rackClearCount[2] = { 0, 0 };
static uint8_t rackNoEchoCount[2] = { 0, 0 };
static float sensHist[2][3] = { { -1, -1, -1 }, { -1, -1, -1 } };
static uint8_t sensIdx[2] = { 0, 0 };

void sensorsInit() {
  pinMode(PIN_TRIG1, OUTPUT);
  pinMode(PIN_ECHO1, INPUT);
  pinMode(PIN_TRIG2, OUTPUT);
  pinMode(PIN_ECHO2, INPUT);
  digitalWrite(PIN_TRIG1, LOW);
  digitalWrite(PIN_TRIG2, LOW);
}

float readCm(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long pulse = pulseIn(echo, HIGH, 25000UL);
  if (pulse == 0) return -1.0f;
  float cm = pulse * 0.0343f / 2.0f;
  return (cm < SENSOR_MIN_VALID_CM || cm > SENSOR_MAX_VALID_CM) ? -1.0f : cm;
}

static float median3(float a, float b, float c) {
  float t;
  if (a > b) { t = a; a = b; b = t; }
  if (b > c) { t = b; b = c; c = t; }
  if (a > b) { t = a; a = b; b = t; }
  return b;
}

static float rackEnterThreshold(int i) {
  return (i == 0) ? RACK1_NEAR_FULL_ENTER_CM : RACK2_NEAR_FULL_ENTER_CM;
}

static float rackExitThreshold(int i) {
  return (i == 0) ? RACK1_NEAR_FULL_EXIT_CM : RACK2_NEAR_FULL_EXIT_CM;
}

// Hysteresis + sampel berurutan mencegah status berkedip di sekitar ambang.
// Function ini dipanggil hanya oleh sensorTask ketika sensorMux sedang di-lock.
static void updateRackCapacityLocked(int i, float distanceCm) {
  if (distanceCm < 0.0f) {
    if (rackNoEchoCount[i] < RACK_CAPACITY_STABLE_SAMPLES) rackNoEchoCount[i]++;
    rackNearCount[i] = 0;
    rackClearCount[i] = 0;
    if (rackNoEchoCount[i] >= RACK_CAPACITY_STABLE_SAMPLES) {
      rackState[i] = RACK_CAPACITY_NO_TARGET;
    }
    return;
  }
  rackNoEchoCount[i] = 0;

  const float enterCm = rackEnterThreshold(i);
  const float exitCm = rackExitThreshold(i);

  if (rackState[i] == RACK_CAPACITY_NEAR_FULL) {
    rackNearCount[i] = 0;
    if (distanceCm >= exitCm) {
      if (rackClearCount[i] < RACK_CAPACITY_STABLE_SAMPLES) rackClearCount[i]++;
      if (rackClearCount[i] >= RACK_CAPACITY_STABLE_SAMPLES) {
        rackState[i] = RACK_CAPACITY_SPACE_OK;
        rackClearCount[i] = 0;
      }
    } else {
      rackClearCount[i] = 0;
    }
    return;
  }

  if (distanceCm <= enterCm) {
    if (rackNearCount[i] < RACK_CAPACITY_STABLE_SAMPLES) rackNearCount[i]++;
    rackClearCount[i] = 0;
    if (rackNearCount[i] >= RACK_CAPACITY_STABLE_SAMPLES) {
      rackState[i] = RACK_CAPACITY_NEAR_FULL;
      rackNearCount[i] = 0;
    }
    return;
  }

  if (distanceCm >= exitCm) {
    if (rackClearCount[i] < RACK_CAPACITY_STABLE_SAMPLES) rackClearCount[i]++;
    rackNearCount[i] = 0;
    if (rackClearCount[i] >= RACK_CAPACITY_STABLE_SAMPLES) {
      rackState[i] = RACK_CAPACITY_SPACE_OK;
      rackClearCount[i] = 0;
    }
    return;
  }

  // Zona mati antara ENTER dan EXIT: pertahankan status stabil terakhir (UNKNOWN saat baru menyala).
  rackNearCount[i] = 0;
  rackClearCount[i] = 0;
}

float sensorCm(int i) {
  if (i < 0 || i > 1) return -1.0f;
  float d;
  unsigned long lastAttempt;
  portENTER_CRITICAL(&sensorMux);
  d = distCmRaw[i];
  lastAttempt = sensorLastAttempt[i];
  portEXIT_CRITICAL(&sensorMux);
  if (lastAttempt == 0 || millis() - lastAttempt > SENSOR_MAX_AGE_MS) return -1.0f;
  return d;
}

bool sensorValid(int i) { return sensorCm(i) >= 0.0f; }

RackCapacityState rackCapacityStatus(int i) {
  if (i < 0 || i > 1) return RACK_CAPACITY_UNKNOWN;
  RackCapacityState state;
  unsigned long lastAttempt;
  portENTER_CRITICAL(&sensorMux);
  state = rackState[i];
  lastAttempt = sensorLastAttempt[i];
  portEXIT_CRITICAL(&sensorMux);
  if (lastAttempt == 0 || millis() - lastAttempt > SENSOR_MAX_AGE_MS) return RACK_CAPACITY_STALE;
  return state;
}

const char* rackCapacityStatusName(RackCapacityState state) {
  switch (state) {
    case RACK_CAPACITY_SPACE_OK: return "SPACE_OK";
    case RACK_CAPACITY_NEAR_FULL: return "NEAR_FULL";
    case RACK_CAPACITY_NO_TARGET: return "NO_TARGET";
    case RACK_CAPACITY_STALE: return "STALE";
    default: return "UNKNOWN";
  }
}

bool rackNearFull(int i) {
  return rackCapacityStatus(i) == RACK_CAPACITY_NEAR_FULL;
}

void sensorTask(void* arg) {
  (void)arg;
  bool turn = false;
  for (;;) {
    int i = turn ? 1 : 0;
    float d = (i == 0) ? readCm(PIN_TRIG1, PIN_ECHO1) : readCm(PIN_TRIG2, PIN_ECHO2);
    unsigned long now = millis();

    float filtered = -1.0f;
    if (d >= 0.0f) {
      sensHist[i][sensIdx[i]] = d;
      sensIdx[i] = (uint8_t)((sensIdx[i] + 1U) % 3U);
      float valid[3];
      uint8_t n = 0;
      for (uint8_t k = 0; k < 3; ++k) {
        if (sensHist[i][k] >= 0.0f) valid[n++] = sensHist[i][k];
      }
      if (n == 1) filtered = valid[0];
      else if (n == 2) filtered = (valid[0] + valid[1]) * 0.5f;
      else if (n == 3) filtered = median3(valid[0], valid[1], valid[2]);
    } else {
      // Sampel no-echo tidak dimasukkan ke median agar tidak menciptakan jarak palsu.
      sensHist[i][sensIdx[i]] = -1.0f;
      sensIdx[i] = (uint8_t)((sensIdx[i] + 1U) % 3U);
    }

    portENTER_CRITICAL(&sensorMux);
    sensorLastAttempt[i] = now;
    if (d < 0.0f) {
      distCmRaw[i] = -1.0f;
      updateRackCapacityLocked(i, -1.0f);
    } else {
      distCmRaw[i] = filtered;
      updateRackCapacityLocked(i, filtered);
    }
    portEXIT_CRITICAL(&sensorMux);

    turn = !turn; // trigger bergantian untuk mengurangi crosstalk
    vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));
  }
}

#endif
