#include "alert.h"

#include "debug.h"

enum NotifyState {
  NotifyStateIdle,
  NotifyStateAlerting,
  NotifyStateRecovering,
};

// All counters must survive deep sleep, so they live in RTC memory.
RTC_DATA_ATTR static NotifyState notifyState = NotifyStateIdle;
RTC_DATA_ATTR static uint32_t alertCycles = 0;      // cycles spent in alerting state
RTC_DATA_ATTR static uint32_t idleCycles = 0;       // cycles spent in idle state
RTC_DATA_ATTR static uint32_t recoveredCycles = 0;  // consecutive cycles above recover threshold

bool processMeasurement(uint16_t fenceVoltage, const AlertConfig& cfg) {
  bool fenceDown = fenceVoltage < cfg.alertThresholdV;
  bool fenceUp = fenceVoltage >= cfg.recoverThresholdV;
  bool shouldSend = false;

  switch (notifyState) {
    case NotifyStateIdle:
      if (fenceDown) {
        notifyState = NotifyStateAlerting;
        alertCycles = 0;
        idleCycles = 0;
        shouldSend = true;  // send immediately when alert is first detected
      } else {
        shouldSend = (idleCycles == 0);
        if (++idleCycles >= cfg.idleHeartbeatCycles)
          idleCycles = 0;
      }
      break;

    case NotifyStateAlerting:
      if (fenceUp) {
        notifyState = NotifyStateRecovering;
        recoveredCycles = 1;
      } else {
        alertCycles++;
        bool inBurst = alertCycles < cfg.burstSends;
        bool heartbeatDue = (alertCycles % cfg.alertHeartbeatCycles) == 0;
        shouldSend = inBurst || heartbeatDue;
      }
      break;

    case NotifyStateRecovering:
      if (fenceDown) {
        // Voltage dipped again — likely a repair in progress. Go back to alerting
        // without restarting the burst, since the recipients already know about this outage.
        notifyState = NotifyStateAlerting;
        recoveredCycles = 0;
      } else if (fenceUp) {
        recoveredCycles++;
        if (recoveredCycles >= cfg.recoveryConfirmCycles) {
          notifyState = NotifyStateIdle;
          idleCycles = 0;
          recoveredCycles = 0;
        }
      }
      break;
  }

  LOG("fenceVoltage: %d, state: %d, alertCycles: %d, idleCycles: %d, recoveredCycles: %d, "
      "shouldSend: %d\n",
      fenceVoltage, notifyState, alertCycles, idleCycles, recoveredCycles, shouldSend);

  return shouldSend;
}
