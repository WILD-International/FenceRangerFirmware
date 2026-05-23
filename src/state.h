#ifndef STATE_H
#define STATE_H

#include "config.h"
#include "rtc_state.h"

class State {
 public:
  State(Config* config);

  void setMode(Mode mode);
  Mode getMode();

  bool useDisplayInNormalMode();
  bool useDisplay();
  void startDisplayInNormalMode();
  void stopDisplayInNormalMode();
  void tickDisplayCycles();

  void incrementBootCount();
  uint16_t getBootCount();

  void updateSlowBatt(uint16_t battMaxMv);
  uint32_t computeAdaptedSleepMs() const;

 private:
  Config* config;
};

#endif  // STATE_H
