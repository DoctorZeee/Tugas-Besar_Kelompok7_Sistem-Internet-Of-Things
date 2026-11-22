#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

// Debouncer non-blocking dengan event press satu kali per transisi.
struct DebouncedButton {
  uint8_t pin;
  uint8_t activeLevel;
  bool stablePressed;
  bool lastRawPressed;
  bool pressEvent;
  bool releaseEvent;
  unsigned long lastChangeMs;
};

inline void beginButton(DebouncedButton& button,
                        uint8_t pin,
                        uint8_t activeLevel,
                        unsigned long now = millis()) {
  button.pin = pin;
  button.activeLevel = activeLevel;
  const bool rawPressed = (digitalRead(pin) == activeLevel);
  button.stablePressed = rawPressed;
  button.lastRawPressed = rawPressed;
  button.pressEvent = false;
  button.releaseEvent = false;
  button.lastChangeMs = now;
}

inline void updateButton(DebouncedButton& button,
                         unsigned long debounceMs = 30,
                         unsigned long now = millis()) {
  button.pressEvent = false;
  button.releaseEvent = false;

  const bool rawPressed = (digitalRead(button.pin) == button.activeLevel);
  if (rawPressed != button.lastRawPressed) {
    button.lastRawPressed = rawPressed;
    button.lastChangeMs = now;
  }

  if ((now - button.lastChangeMs) >= debounceMs &&
      button.stablePressed != rawPressed) {
    button.stablePressed = rawPressed;
    button.pressEvent = rawPressed;
    button.releaseEvent = !rawPressed;
  }
}

#endif // BUTTON_H
