#pragma once

#include <Arduino.h>
#include <RadioLib.h>

#include "alert.h"

enum Mode { ModeNormal, ModeConfig };

struct RtcState {
  // Sleep tracking
  int64_t sleepEnterUs = 0;
  int64_t sleepTimeMs = 0;

  // Alert state machine
  AlertState alertState = {};

  // Battery slow filter
  uint16_t slowBattMv = 0;

  // Display: 0 = disabled, any other value = start time in us (1 = show from first boot)
  int64_t displayStartTime = 1;

  // Boot tracking
  uint16_t bootCount = 0;
  uint16_t bootCountForJoining = 0;

  // LoRaWAN session
  bool joined = false;
  uint8_t lwSession[RADIOLIB_LORAWAN_SESSION_BUF_SIZE] = {};

  // Button
  bool buttonPressed = false;

  // Device mode
  Mode mode = ModeNormal;
};

extern RTC_DATA_ATTR RtcState rtc;
