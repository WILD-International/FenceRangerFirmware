#include "alert.h"

#include "logging.h"

AlertDecision processMeasurement(uint16_t fenceVoltage, const AlertConfig& cfg, AlertState& state) {
  bool fenceDown = fenceVoltage < cfg.alertThresholdV;
  bool fenceHealthy = fenceVoltage >= cfg.recoverThresholdV;

  // Rapid relative-drop early warning: a reading at least dropPercent below the previous one
  // while the fence is still above the alert threshold. Only evaluated from Idle — once in
  // Outage the absolute thresholds govern — and never when this same reading already crossed
  // the alert threshold (the absolute alert wins and opens the outage as a plain measurement).
  // previousFenceVoltage defaults to 0, so the first-ever call can't satisfy
  // fenceVoltage < previousFenceVoltage and is skipped.
  bool rapidDrop = false;
  if (state.notifyState == NotifyStateIdle && !fenceDown &&
      fenceVoltage < state.previousFenceVoltage) {
    double percentDrop =
        100.0 * (state.previousFenceVoltage - fenceVoltage) / state.previousFenceVoltage;
    rapidDrop = percentDrop >= cfg.dropPercent;
  }

  AlertDecision decision;
  decision.dropAlert = rapidDrop;
  decision.previousFenceVoltage = state.previousFenceVoltage;

  if (state.notifyState == NotifyStateIdle) {
    if (fenceDown || rapidDrop) {
      // New outage: switch to the alert cadence and fire the first burst message now.
      state.notifyState = NotifyStateOutage;
      state.alertCycles = 0;
      state.recoveredCycles = 0;
      decision.send = true;
    } else {
      // Healthy, or in the hysteresis band: just the slow idle heartbeat.
      decision.send = (state.idleCycles == 0);
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
    decision.send = inBurst || heartbeatDue;

    if (fenceHealthy) {
      // Count consecutive healthy readings; confirm recovery once we have enough.
      if (++state.recoveredCycles >= cfg.recoveryConfirmCycles) {
        // Send one explicit all-clear and return to idle, with the next idle heartbeat
        // a full [idleHeartbeatCycles] away (idleCycles = 1, as if we just sent at 0).
        state.notifyState = NotifyStateIdle;
        state.recoveredCycles = 0;
        state.idleCycles = 1;
        decision.send = true;
      }
    } else {
      // Below the recover threshold (fence down or in the hysteresis band): any such
      // reading restarts the recovery count.
      state.recoveredCycles = 0;
    }
  }

  // Always advance the baseline so the next cycle compares against this reading.
  state.previousFenceVoltage = fenceVoltage;

  LOG("fenceVoltage: %d, state: %d, alertCycles: %d, idleCycles: %d, recoveredCycles: %d, "
      "rapidDrop: %d, send: %d\n",
      fenceVoltage, state.notifyState, state.alertCycles, state.idleCycles, state.recoveredCycles,
      rapidDrop, decision.send);

  return decision;
}
