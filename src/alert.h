#ifndef ALERT_H
#define ALERT_H

#include <cstdint>

// Two states only: Idle (healthy, slow heartbeat) and Outage (fence problem). Within
// Outage, recoveredCycles == 0 means actively alerting and > 0 means we are counting
// consecutive healthy readings toward a confirmed recovery.
//
// An outage opens in either of two ways: the reading crosses below the alert threshold,
// or — while still above it — the reading falls at least dropPercent below the previous
// one (a rapid relative drop, the early warning for a failing fence). Both share the same
// burst / alert-heartbeat / recovery machinery; only the opening uplink differs.
enum NotifyState { NotifyStateIdle, NotifyStateOutage };

struct AlertState {
  NotifyState notifyState = NotifyStateIdle;
  uint32_t alertCycles = 0;
  uint32_t idleCycles = 0;
  uint32_t recoveredCycles = 0;
  uint16_t previousFenceVoltage = 0;  // 0 = no previous reading yet (nothing can drop below it)
};

struct AlertConfig {
  uint16_t alertThresholdV;
  uint16_t recoverThresholdV;
  uint8_t burstSends;
  uint8_t idleHeartbeatCycles;
  uint8_t alertHeartbeatCycles;
  uint8_t recoveryConfirmCycles;
  uint8_t dropPercent;  // a still-healthy reading this many percent below the last opens an outage
};

// Per-cycle send decision. When dropAlert is set the uplink is the rapid-drop payload
// (previous + current voltage) instead of a plain measurement.
struct AlertDecision {
  bool send = false;
  bool dropAlert = false;
  uint16_t previousFenceVoltage = 0;  // the pre-drop reading, for the rapid-drop payload
};

AlertDecision processMeasurement(uint16_t fenceVoltage, const AlertConfig& cfg, AlertState& state);

#endif  // ALERT_H
