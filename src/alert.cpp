#include "alert.h"

#include "logging.h"

bool processMeasurement(uint16_t fenceVoltage, const AlertConfig& cfg, AlertState& state) {
  bool fenceDown = fenceVoltage < cfg.alertThresholdV;
  bool fenceUp = fenceVoltage >= cfg.recoverThresholdV;
  bool shouldSend = false;

  switch (state.notifyState) {
    case NotifyStateIdle:
      if (fenceDown) {
        state.notifyState = NotifyStateAlerting;
        state.alertCycles = 0;
        state.idleCycles = 0;
        shouldSend = true;
      } else {
        shouldSend = (state.idleCycles == 0);
        if (++state.idleCycles >= cfg.idleHeartbeatCycles)
          state.idleCycles = 0;
      }
      break;

    case NotifyStateAlerting:
      if (fenceUp) {
        state.notifyState = NotifyStateRecovering;
        state.recoveredCycles = 1;
      } else {
        state.alertCycles++;
        bool inBurst = state.alertCycles < cfg.burstSends;
        bool heartbeatDue = (state.alertCycles % cfg.alertHeartbeatCycles) == 0;
        shouldSend = inBurst || heartbeatDue;
      }
      break;

    case NotifyStateRecovering:
      if (fenceDown) {
        // Voltage dipped again — likely a repair in progress. Go back to alerting
        // without restarting the burst, since the recipients already know about this outage.
        state.notifyState = NotifyStateAlerting;
        state.recoveredCycles = 0;
      } else if (fenceUp) {
        state.recoveredCycles++;
        if (state.recoveredCycles >= cfg.recoveryConfirmCycles) {
          state.notifyState = NotifyStateIdle;
          state.idleCycles = 0;
          state.recoveredCycles = 0;
        }
      }
      break;
  }

  LOG("fenceVoltage: %d, state: %d, alertCycles: %d, idleCycles: %d, recoveredCycles: %d, "
      "shouldSend: %d\n",
      fenceVoltage, state.notifyState, state.alertCycles, state.idleCycles, state.recoveredCycles,
      shouldSend);

  return shouldSend;
}
