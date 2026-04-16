#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include <Preferences.h>

enum Mode { ModeNormal, ModeConfig };

class State {
 public:
  State();
  ~State();

  // Public methods
  void setMode(Mode mode);
  Mode getMode();

  bool useDisplayInNormalMode();
  bool useDisplay();
  void startDisplayInNormalMode();
  void stopDisplayInNormalMode();

  // Public data
  bool at_command_mode = false;
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
