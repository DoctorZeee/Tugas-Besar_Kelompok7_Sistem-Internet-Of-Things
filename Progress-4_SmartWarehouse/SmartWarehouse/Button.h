#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>
#include "Config.h"

struct Button {
  uint8_t pin;
  bool stable;
  bool lastRaw;
  unsigned long changedAt;
};

inline void buttonInit(Button& b, int activeLevel = LOW) {
  bool pressed = (digitalRead(b.pin) == activeLevel);
  b.stable = pressed;
  b.lastRaw = pressed;
  b.changedAt = millis();
}

// Return true saat tombol stabil ditekan. Debounce menggunakan active level pin.
inline bool updateButton(Button& b, int activeLevel = LOW) {
  bool rawPressed = (digitalRead(b.pin) == activeLevel);
  unsigned long now = millis();
  if (rawPressed != b.lastRaw) {
    b.lastRaw = rawPressed;
    b.changedAt = now;
  }
  if (now - b.changedAt >= DEBOUNCE_MS) b.stable = rawPressed;
  return b.stable;
}

#endif
