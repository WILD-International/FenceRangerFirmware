#include "state.h"

#include "sleep.h"

State::State(Config* config) : config(config) {}

void State::setMode(Mode mode) {
  rtc.mode = mode;
}

Mode State::getMode() {
  return rtc.mode;
}

bool State::useDisplayInNormalMode() {
  if (rtc.displayStartTime == 0) {
    return false;
  }
  return rtcTimeNowUs() - rtc.displayStartTime < 60LL * 60 * 1000 * 1000;
}

bool State::useDisplay() {
  return rtc.mode == ModeConfig || useDisplayInNormalMode();
}

void State::startDisplayInNormalMode() {
  rtc.displayStartTime = rtcTimeNowUs();
}

void State::stopDisplayInNormalMode() {
  rtc.displayStartTime = 0;
}

void State::incrementBootCount() {
  rtc.bootCount++;
}

uint16_t State::getBootCount() {
  return rtc.bootCount;
}

void State::updateSlowBatt() {
  if (rtc.slowBattMv == 0 || batt_max_mv < rtc.slowBattMv) {
    rtc.slowBattMv = batt_max_mv;
  } else {
    rtc.slowBattMv =
        (uint16_t)(rtc.slowBattMv + (batt_max_mv - rtc.slowBattMv) / config->getBattRecoveryN());
  }
}

uint32_t State::computeAdaptedSleepMs() const {
  float battT = constrain(((float)config->getBattFullMv() - rtc.slowBattMv) /
                              (config->getBattFullMv() - config->getBattLowMv()),
                          0.0f, 1.0f);
  uint32_t minSleep = config->getSleepIntervalMs();
  uint32_t maxSleep = (uint32_t)(minSleep * config->getBattSleepMult());
  return minSleep + (uint32_t)(battT * (maxSleep - minSleep));
}
