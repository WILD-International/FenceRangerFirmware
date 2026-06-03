#pragma once

#include <Arduino.h>
#include <RadioLib.h>

#include "alert.h"

enum Mode { ModeNormal, ModeConfig };

// Radio-config trial (commit-on-confirm). A SetRadioConfig downlink stages its
// candidate settings here (volatile RTC) instead of writing NVS; they are only
// committed to NVS once a confirmed-uplink ACK proves the link still works both
// ways. Timeout, crash, or power loss all fall back to the last good NVS config.
struct RadioTrial {
  bool active = false;
  // candidate radio settings under test (mirror of the Config radio fields)
  uint8_t rxLnaEnabled = 0;
  int8_t txPower = 0;
  uint8_t dataRate = 0;
  uint8_t adrEnabled = 0;
  uint8_t syncwordPublic = 0;
  uint16_t scanGuardMs = 0;
  // state machine
  uint8_t phase = 0;         // index into the DR-test sequence
  uint8_t testDr = 0;        // DR forced for this phase's confirmed-uplink probe
  uint8_t attemptsLeft = 0;  // confirmed-uplink attempts remaining for this phase
};

struct RtcState {
  // Sleep tracking
  int64_t sleepEnterUs = 0;
  int64_t sleepTimeMs = 0;

  // Alert state machine
  AlertState alertState = {};

  // Battery slow filter
  uint16_t slowBattMv = 0;

  // Display: cycles remaining to show display in normal mode (0 = disabled)
  uint8_t displayCyclesRemaining = 10;

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

  // Radio-config trial-and-rollback
  RadioTrial radioTrial = {};
  bool trialResultPending = false;  // a trial just concluded; send one fPort-16 report, then clear
};

extern RTC_DATA_ATTR RtcState rtc;
