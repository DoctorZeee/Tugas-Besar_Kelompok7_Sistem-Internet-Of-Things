#ifndef PID_CONTROL_H
#define PID_CONTROL_H

#include <Arduino.h>
#include <math.h>

class LiftPID {
 public:
  LiftPID(float kp, float ki, float kd, int minOutput, int maxOutput)
      : kp_(kp), ki_(ki), kd_(kd), minOutput_(minOutput), maxOutput_(maxOutput) {
    reset();
  }

  // Return magnitudo PWM. Arah motor ditentukan oleh tanda error di pemanggil.
  // Panggil hanya ketika |error| lebih besar dari toleransi berhenti.
  int compute(long target, long current) {
    const unsigned long now = millis();
    const unsigned long dtMs = now - lastTimeMs_;
    if (dtMs < SAMPLE_MS) return lastOutput_;

    float dt = static_cast<float>(dtMs) / 1000.0f;
    if (dt <= 0.0f) dt = static_cast<float>(SAMPLE_MS) / 1000.0f;

    // Direction dikendalikan pemanggil, jadi PID bekerja pada besar error.
    const float error = fabsf(static_cast<float>(target - current));
    if (firstRun_) {
      previousError_ = error;
      firstRun_ = false;
    }

    const float derivative = (error - previousError_) / dt;
    const float pTerm = kp_ * error;
    const float dTerm = kd_ * derivative;

    // Conditional integration: jangan menambah integral ketika output sudah
    // tersaturasi ke arah yang sama dengan error.
    float candidateIntegral = integral_ + error * dt;
    candidateIntegral = constrain(candidateIntegral, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);
    float candidateOutput = pTerm + (ki_ * candidateIntegral) + dTerm;

    const bool saturatingHigh = candidateOutput > static_cast<float>(maxOutput_);
    // Error adalah magnitudo non-negatif. Saat output berada di luar rentang
    // atas, integral tidak ditambah lagi; saat derivative menurunkan output
    // di bawah nol, integral tetap boleh membantu mengembalikannya.
    if (!saturatingHigh) {
      integral_ = candidateIntegral;
    }

    const float output = pTerm + (ki_ * integral_) + dTerm;
    previousError_ = error;
    lastTimeMs_ = now;

    int magnitude = static_cast<int>(output);
    if (magnitude <= 0) {
      lastOutput_ = 0;
      return lastOutput_;
    }
    magnitude = constrain(magnitude, minOutput_, maxOutput_);
    lastOutput_ = magnitude;
    return lastOutput_;
  }

  void reset() {
    previousError_ = 0.0f;
    integral_ = 0.0f;
    lastTimeMs_ = millis() - SAMPLE_MS;
    lastOutput_ = minOutput_;
    firstRun_ = true;
  }

 private:
  static constexpr unsigned long SAMPLE_MS = 10;
  static constexpr float INTEGRAL_LIMIT = 400.0f;

  float kp_;
  float ki_;
  float kd_;
  float previousError_ = 0.0f;
  float integral_ = 0.0f;
  unsigned long lastTimeMs_ = 0;
  int minOutput_;
  int maxOutput_;
  int lastOutput_ = 0;
  bool firstRun_ = true;
};

#endif // PID_CONTROL_H
