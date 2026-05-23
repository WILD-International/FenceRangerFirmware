#include "state.h"

State::State(Config* config) : config(config) {}

void State::setMode(Mode mode) {
  rtc.mode = mode;
}

Mode State::getMode() {
  return rtc.mode;
}

bool State::useDisplayInNormalMode() {
  return rtc.displayCyclesRemaining > 0;
}

bool State::useDisplay() {
  return rtc.mode == ModeConfig || useDisplayInNormalMode();
}

void State::startDisplayInNormalMode() {
  rtc.displayCyclesRemaining = 10;
}

void State::stopDisplayInNormalMode() {
  rtc.displayCyclesRemaining = 0;
}

void State::tickDisplayCycles() {
  if (rtc.displayCyclesRemaining > 0) {
    rtc.displayCyclesRemaining--;
  }
}

void State::incrementBootCount() {
  rtc.bootCount++;
}

uint16_t State::getBootCount() {
  return rtc.bootCount;
}

void State::updateSlowBatt(uint16_t battMaxMv) {
  if (rtc.slowBattMv == 0 || battMaxMv < rtc.slowBattMv) {
    rtc.slowBattMv = battMaxMv;
  } else {
    rtc.slowBattMv =
        (uint16_t)(rtc.slowBattMv + (battMaxMv - rtc.slowBattMv) / config->getBattRecoveryN());
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
