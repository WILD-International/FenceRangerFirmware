#include "state.h"

#include "debug.h"
#include "helpers.h"

// Persistent data that lives in RTC memory
RTC_DATA_ATTR Mode currentMode = ModeNormal;
RTC_DATA_ATTR int64_t useDisplayInNormalModeStartTime = 1;  // set to 0 to disable by default
RTC_DATA_ATTR uint16_t bootCount = 0;

State::State(Config* config) : config(config) {}

State::~State() {}

void State::setMode(Mode mode) {
  currentMode = mode;
}

Mode State::getMode() {
  return currentMode;
}

bool State::useDisplayInNormalMode() {
  int64_t now = rtcTimeNowUs();
  if (useDisplayInNormalModeStartTime == 0) {
    return false;
  }

  // use display for debugging for max of 60 minutes
  if (now - useDisplayInNormalModeStartTime < 60LL * 60 * 1000 * 1000) {
    return true;
  }
  return false;
}

bool State::useDisplay() {
  if (currentMode == ModeConfig) {
    return true;
  }
  return useDisplayInNormalMode();
}

void State::startDisplayInNormalMode() {
  useDisplayInNormalModeStartTime = rtcTimeNowUs();
}

void State::stopDisplayInNormalMode() {
  useDisplayInNormalModeStartTime = 0;
}

void State::incrementBootCount() {
  bootCount++;
}

uint16_t State::getBootCount() {
  return bootCount;
}

bool State::processMeasurement() {
  static uint32_t idleCount = 0;
  static uint32_t backoffCount = 0;
  static uint32_t recoverCount = 0;

  uint16_t fenceVoltage = max(fence_pos_max_v, fence_neg_max_v);

  uint16_t alertThreshold = config->getAlertThresholdV();
  uint16_t recoverThreshold = config->getRecoverThresholdV();

  bool shouldSend = false;

  switch (notifyState) {
    case NotifyStateIdle:
      if (fenceVoltage < alertThreshold) {
        notifyState = NotifyStateAlerting;
        backoffCount = 1;
        shouldSend = true;
      } else if (idleCount == 0) {
        shouldSend = true;
      }
      idleCount++;
      if (idleCount >= 10) {
        idleCount = 0;
      }
      break;
    case NotifyStateAlerting:
      if (fenceVoltage > recoverThreshold) {
        recoverCount = 0;
        notifyState = NotifyStateRecovering;
      } else {
        backoffCount++;
        // Only send with some backoff, so like
        // call 1, 2, 4, 8, 16... so that we alert more frequently at the beginning of a fence down
        // event, but less frequently if it has been down for a while But also send every 64 calls
        // regardless of backoff, so that we don't have super long gaps between notifications for
        // long fence down events
        if ((backoffCount & (backoffCount - 1)) == 0 || backoffCount % 64 == 0) {
          shouldSend = true;
        }
      }
      break;
    case NotifyStateRecovering:
      if (fenceVoltage < alertThreshold) {
        // same logic as above
        backoffCount++;
        if ((backoffCount & (backoffCount - 1)) == 0 || backoffCount % 64 == 0) {
          shouldSend = true;
        }
        notifyState = NotifyStateAlerting;
      } else if (fenceVoltage > recoverThreshold) {
        recoverCount++;
        if (recoverCount >= 30) {
          recoverCount = 0;
          idleCount = 0;
          notifyState = NotifyStateIdle;
        }
      }
      break;
  }

  if (shouldSend) {
    LOG("fenceVoltage: %d, notifyState: %d, backoffCount: %d, recoverCount: %d, shouldSend: %d\n",
        fenceVoltage, notifyState, backoffCount, recoverCount, shouldSend);
  }

  return shouldSend;
}
