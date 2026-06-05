#pragma once

#include <Arduino.h>
#include <RadioLib.h>

#include "alert.h"

enum Mode { ModeNormal, ModeConfig };

// Size of the AP-password buffer carried by an EnableOTA downlink (and the RTC field that
// holds it across the wake). The downlink payload buffer is 32 bytes with byte 0 used for the
// timeout, so the password is at most 31 chars + NUL — well within WPA2's 8..63 range.
constexpr size_t WIFI_AP_PASS_MAX = 32;

// Buffers for the WiFi-station credentials carried by a JoinWifiSta downlink (fPort 9):
// SSID up to 32 chars + NUL, WPA2 password up to 63 chars + NUL.
constexpr size_t WIFI_SSID_MAX = 33;
constexpr size_t WIFI_STA_PASS_MAX = 64;

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
  uint8_t confirmsLeft = 0;  // successful ACKs still needed before this phase passes
  uint8_t attemptsLeft = 0;  // consecutive missed ACKs left before this phase reverts
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

  // Set when an EnableOTA downlink requested config mode; consumed once on the next wake to
  // open config mode directly on the WiFi AP screen (with the AP started).
  bool otaConfigRequested = false;
  // Per-activation AP password carried by that downlink ("" = use the fixed AP_PASSWORD) and
  // how long the AP should stay up (minutes; 0 = the default 1-hour config-mode timeout).
  char otaApPassword[WIFI_AP_PASS_MAX] = {};
  uint8_t otaApTimeoutMin = 0;

  // Set when a JoinWifiSta downlink (fPort 9) requested station mode; consumed once on the next
  // wake to connect to the gateway AP and run the web server there.
  bool staConnectRequested = false;
  char staSsid[WIFI_SSID_MAX] = {};          // "" = use fixed STA_GATEWAY_SSID
  char staPassword[WIFI_STA_PASS_MAX] = {};  // per-activation gateway WPA2 password
  uint8_t staTimeoutMin = 0;                 // 0 = default config-mode timeout

  // Radio-config trial-and-rollback
  RadioTrial radioTrial = {};
  bool trialResultPending = false;  // a trial just concluded; send one fPort-16 report, then clear
};

extern RTC_DATA_ATTR RtcState rtc;
