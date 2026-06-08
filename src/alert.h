#ifndef ALERT_H
#define ALERT_H

#include <cstdint>

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

// Secondary "rapid relative-drop" early warning. Independent of the Idle/Outage FSM above:
// it flags a fence that is falling fast while still healthy, before it crosses Alert V — it
// fires when a reading is at least dropPercent below the previous one.
struct RapidDropConfig {
  uint16_t dropPercent;  // fire when the reading is at least this many percent below the last
  uint16_t rearmCycles;  // after firing, suppress further alerts for this many cycles
};

struct RapidDropState {
  uint16_t previousFenceVoltage = 0;  // 0 = no previous reading yet (nothing can drop below it)
  uint16_t rearmCyclesLeft = 0;       // cycles remaining before another alert can fire
};

// Filled in (and meaningful) only when detectRapidDrop returns true — the values to report.
struct RapidDropInfo {
  uint16_t previousVoltage;
  uint16_t currentVoltage;
};

// Returns true when a rapid drop should be alerted this cycle. Always updates `state` (the
// previous reading and cooldown) so it tracks every cycle; `healthy` (the FSM being Idle) only
// gates whether a qualifying drop actually fires.
bool detectRapidDrop(uint16_t currentVoltage, bool healthy, const RapidDropConfig& config,
                     RapidDropState& state, RapidDropInfo* outInfo = nullptr);

#endif  // ALERT_H
