#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include <Preferences.h>

enum ProgramState {
  PROGRAM_CONFIGURATION,
  PROGRAM_JOIN,
  PROGRAM_MEASURE,
};

class State {
 public:
  State();
  ~State();

  // Public methods
  ProgramState getProgramState();
  void setProgramState(ProgramState newState);

  // Public data
  bool measurement_running = false;
  uint32_t batt_min_mv = 0;
  uint32_t batt_max_mv = 0;
  uint32_t fence_neg_min_mv = 0;
  uint32_t fence_neg_max_mv = 0;
  uint32_t fence_pos_min_mv = 0;
  uint32_t fence_pos_max_mv = 0;
  uint32_t adc_error_count = 0;

 private:
  // Private methods

  // Private data
  Preferences prefs;
};

#endif  // STATE_H
