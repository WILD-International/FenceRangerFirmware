#ifndef ALERT_H
#define ALERT_H

#include <Arduino.h>

// Two states only: Idle (healthy, slow heartbeat) and Outage (fence problem). Within
// Outage, recoveredCycles == 0 means actively alerting and > 0 means we are counting
// consecutive healthy readings toward a confirmed recovery.
enum NotifyState { NotifyStateIdle, NotifyStateOutage };

struct AlertState {
  NotifyState notifyState = NotifyStateIdle;
  uint32_t alertCycles = 0;
  uint32_t idleCycles = 0;
  uint32_t recoveredCycles = 0;
};

struct AlertConfig {
  uint16_t alertThresholdV;
  uint16_t recoverThresholdV;
  uint8_t burstSends;
  uint8_t idleHeartbeatCycles;
  uint8_t alertHeartbeatCycles;
  uint8_t recoveryConfirmCycles;
};

bool processMeasurement(uint16_t fenceVoltage, const AlertConfig& cfg, AlertState& state);

#endif  // ALERT_H
