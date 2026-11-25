#ifndef PID_CONTROL_H
#define PID_CONTROL_H

#include <Arduino.h>

class LiftPID {
  private:
    static const unsigned long SAMPLE_MS = 10;   // hitung PID tiap >= 10 ms

    float Kp, Ki, Kd;
    int outMin, outMax;
    float previousError;
    float integral;
    unsigned long lastTime;
    int lastOutput;
    bool firstRun;

  public:
    LiftPID(float p, float i, float d, int minOut, int maxOut) {
      Kp = p; Ki = i; Kd = d;
      outMin = minOut; outMax = maxOut;
      reset();
    }

    // Output PWM (magnitudo) dari error posisi (tick). Arah ditentukan pemanggil dari tanda error.
    int compute(long target, long current) {
      unsigned long now = millis();
      unsigned long dtMs = now - lastTime;

      if (dtMs < SAMPLE_MS) return lastOutput;   // terlalu cepat: pakai output sebelumnya

      float deltaTime = dtMs / 1000.0f;
      float error = (float)(target - current);

      if (firstRun) {                            // tanpa derivative kick
        previousError = error;
        firstRun = false;
      }

      float pOut = Kp * error;

      float dOut = Kd * (error - previousError) / deltaTime;

      // Conditional integration: integral hanya bertambah selama output belum jenuh (anti-windup)
      float candidate = integral + error * deltaTime;
      if (candidate > 400) candidate = 400;
      if (candidate < -400) candidate = -400;
      if (fabsf(pOut + Ki * candidate + dOut) <= (float)outMax) integral = candidate;
      float iOut = Ki * integral;

      float output = pOut + iOut + dOut;
      previousError = error;
      lastTime = now;

      lastOutput = constrain((int)fabsf(output), outMin, outMax);
      return lastOutput;
    }

    void reset() {
      previousError = 0;
      integral = 0;
      lastTime = millis() - SAMPLE_MS;
      lastOutput = outMin;
      firstRun = true;
    }
};

#endif
