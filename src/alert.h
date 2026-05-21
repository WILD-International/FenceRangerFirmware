#ifndef ALERT_H
#define ALERT_H

#include <Arduino.h>

struct AlertConfig {
  uint16_t alertThresholdV;
  uint16_t recoverThresholdV;
  uint8_t burstSends;
  uint8_t idleHeartbeatCycles;
  uint8_t alertHeartbeatCycles;
  uint8_t recoveryConfirmCycles;
};

bool processMeasurement(uint16_t fenceVoltage, const AlertConfig& cfg);

#endif  // ALERT_H
