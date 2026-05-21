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

// On a new alert: send immediately, then send again for the next ALERT_BURST_SENDS-1 cycles.
// 3 total = 1 immediate + 2 follow-ups within the next ~2 minutes (at default 60s sleep interval).
static const uint32_t ALERT_BURST_SENDS = 3;
// Send a heartbeat every N cycles so we know the device is alive.
static const uint32_t IDLE_HEARTBEAT_CYCLES = 60;
static const uint32_t ALERT_HEARTBEAT_CYCLES = 15;
// Require this many consecutive above-threshold cycles before declaring recovery.
// Filters out brief voltage spikes during fence repairs.
static const uint32_t RECOVERY_CONFIRM_CYCLES = 30;

bool processMeasurement(uint16_t fenceVoltage, uint16_t alertThresholdV,
                        uint16_t recoverThresholdV) {
  bool fenceDown = fenceVoltage < alertThresholdV;
  bool fenceUp = fenceVoltage >= recoverThresholdV;
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
        if (++idleCycles >= IDLE_HEARTBEAT_CYCLES)
          idleCycles = 0;
      }
      break;

    case NotifyStateAlerting:
      if (fenceUp) {
        notifyState = NotifyStateRecovering;
        recoveredCycles = 1;
      } else {
        alertCycles++;
        bool inBurst = alertCycles < ALERT_BURST_SENDS;
        bool heartbeatDue = (alertCycles % ALERT_HEARTBEAT_CYCLES) == 0;
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
        if (recoveredCycles >= RECOVERY_CONFIRM_CYCLES) {
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
