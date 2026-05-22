#ifndef ALERT_H
#define ALERT_H

#include <Arduino.h>

enum NotifyState { NotifyStateIdle, NotifyStateAlerting, NotifyStateRecovering };

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
