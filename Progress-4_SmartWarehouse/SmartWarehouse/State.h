#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include "Config.h"

enum SystemState {
  STATE_BOOT,
  STATE_POSITION_UNREFERENCED,
  STATE_IDLE,
  STATE_MOVING,
  STATE_ARRIVED,
  STATE_MANUAL,
  STATE_ESTOP,
  STATE_ERROR
};

inline const char* stateName(int s) {
  switch (s) {
    case STATE_BOOT:                    return "BOOT";
    case STATE_POSITION_UNREFERENCED:   return "POSITION_UNREFERENCED";
    case STATE_IDLE:                    return "IDLE";
    case STATE_MOVING:                  return "MOVING";
    case STATE_ARRIVED:                 return "ARRIVED";
    case STATE_MANUAL:                  return "MANUAL";
    case STATE_ESTOP:                   return "ESTOP";
    case STATE_ERROR:                   return "ERROR";
  }
  return "UNKNOWN";
}

// Kalibrasi runtime; dimuat dari NVS di SmartWarehouse.ino.
extern long calibratedFloor2Ticks;
extern bool floorTravelCalibrated;

inline int getFloor(long ticks) {
  if (labs(ticks - FLOOR_1_TICKS) <= FLOOR_TOL_TICKS) return 1;
  if (floorTravelCalibrated &&
      labs(ticks - calibratedFloor2Ticks) <= FLOOR_TOL_TICKS) return 2;
  return 0;
}

#endif
