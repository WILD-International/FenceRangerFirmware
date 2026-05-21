#include "state.h"

#include "helpers.h"

// Persistent data that lives in RTC memory
RTC_DATA_ATTR Mode currentMode = ModeNormal;
RTC_DATA_ATTR uint16_t slowBattMv = 0;
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

void State::updateSlowBatt() {
  if (slowBattMv == 0 || batt_max_mv < slowBattMv) {
    slowBattMv = batt_max_mv;
  } else {
    slowBattMv = (uint16_t)(slowBattMv + (batt_max_mv - slowBattMv) / config->getBattRecoveryN());
  }
}

uint32_t State::computeAdaptedSleepMs() const {
  float battT = constrain(((float)config->getBattFullMv() - slowBattMv) /
                              (config->getBattFullMv() - config->getBattLowMv()),
                          0.0f, 1.0f);
  uint32_t minSleep = config->getSleepIntervalMs();
  uint32_t maxSleep = (uint32_t)(minSleep * config->getBattSleepMult());
  return minSleep + (uint32_t)(battT * (maxSleep - minSleep));
}
