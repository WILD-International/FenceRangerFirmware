#include "state.h"

#include "helpers.h"

// Persistent data that lives in RTC memory
RTC_DATA_ATTR Mode currentMode = ModeNormal;
RTC_DATA_ATTR int64_t useDisplayInNormalModeStartTime = 0;

State::State() {}

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
