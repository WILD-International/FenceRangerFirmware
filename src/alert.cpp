#include "alert.h"

#include "logging.h"

bool processMeasurement(uint16_t fenceVoltage, const AlertConfig& cfg, AlertState& state) {
  bool fenceDown = fenceVoltage < cfg.alertThresholdV;
  bool fenceHealthy = fenceVoltage >= cfg.recoverThresholdV;
  bool shouldSend = false;

  if (state.notifyState == NotifyStateIdle) {
    if (fenceDown) {
      // New outage: switch to the alert cadence and fire the first burst message now.
      state.notifyState = NotifyStateOutage;
      state.alertCycles = 0;
      state.recoveredCycles = 0;
      shouldSend = true;
    } else {
      // Healthy, or in the hysteresis band: just the slow idle heartbeat.
      shouldSend = (state.idleCycles == 0);
      if (++state.idleCycles >= cfg.idleHeartbeatCycles) {
        state.idleCycles = 0;
      }
    }
  } else {
    // Outage spans alerting and recovery-confirmation under one cadence: alertCycles
    // drives the initial burst, then a heartbeat every [alertHeartbeatCycles] cycles. A
    // single outage counter (never reset until idle) keeps that cadence continuous even
    // as the fence bounces between down and healthy.
    state.alertCycles++;
    // Burst of [burstSends] messages at the very start of the outage, then a heartbeat
    // every [alertHeartbeatCycles] cycles measured from the end of that burst (so the
    // first heartbeat is a full interval after the last burst message, not sooner).
    bool inBurst = state.alertCycles < cfg.burstSends;
    bool heartbeatDue =
        !inBurst && ((state.alertCycles - cfg.burstSends + 1) % cfg.alertHeartbeatCycles) == 0;
    shouldSend = inBurst || heartbeatDue;

    if (fenceHealthy) {
      // Count consecutive healthy readings; confirm recovery once we have enough.
      if (++state.recoveredCycles >= cfg.recoveryConfirmCycles) {
        // Send one explicit all-clear and return to idle, with the next idle heartbeat
        // a full [idleHeartbeatCycles] away (idleCycles = 1, as if we just sent at 0).
        state.notifyState = NotifyStateIdle;
        state.recoveredCycles = 0;
        state.idleCycles = 1;
        shouldSend = true;
      }
    } else {
      // Below the recover threshold (fence down or in the hysteresis band): any such
      // reading restarts the recovery count.
      state.recoveredCycles = 0;
    }
  }

  LOG("fenceVoltage: %d, state: %d, alertCycles: %d, idleCycles: %d, recoveredCycles: %d, "
      "shouldSend: %d\n",
      fenceVoltage, state.notifyState, state.alertCycles, state.idleCycles, state.recoveredCycles,
      shouldSend);

  return shouldSend;
}

bool detectRapidDrop(uint16_t currentVoltage, bool healthy, const RapidDropConfig& config,
                     RapidDropState& state, RapidDropInfo* outInfo) {
  if (state.rearmCyclesLeft > 0) {
    state.rearmCyclesLeft--;
  }

  // previousFenceVoltage defaults to 0, so the first-ever call can't satisfy
  // currentVoltage < previousFenceVoltage and is skipped.
  bool fire = false;
  if (healthy && state.rearmCyclesLeft == 0 && currentVoltage < state.previousFenceVoltage) {
    double percentDrop =
        100.0 * (state.previousFenceVoltage - currentVoltage) / state.previousFenceVoltage;
    if (percentDrop >= config.dropPercent) {
      fire = true;
      state.rearmCyclesLeft = config.rearmCycles;
      if (outInfo != nullptr) {
        *outInfo = {state.previousFenceVoltage, currentVoltage};
      }
    }
  }

  // Always advance the baseline so the next call compares against this reading.
  state.previousFenceVoltage = currentVoltage;

  LOG("rapidDrop: currentVoltage: %d, healthy: %d, fire: %d\n", currentVoltage, healthy, fire);
  return fire;
}
