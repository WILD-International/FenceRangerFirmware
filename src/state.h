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

  void setMeasurementRunning(bool running) {
    measurement_running = running;
  }
  bool isMeasurementRunning() const {
    return measurement_running;
  }

  // Setters
  void setBatteryMv(uint32_t min_mv, uint32_t max_mv) {
    batt_min_mv = min_mv;
    batt_max_mv = max_mv;
  }
  void setFenceNegMv(uint32_t min_mv, uint32_t max_mv) {
    fence_neg_min_mv = min_mv;
    fence_neg_max_mv = max_mv;
  }
  void setFencePosMv(uint32_t min_mv, uint32_t max_mv) {
    fence_pos_min_mv = min_mv;
    fence_pos_max_mv = max_mv;
  }
  void setAdcErrorCount(uint32_t count) {
    adc_error_count = count;
  }

  // Getters
  uint32_t getBattMinMv() const {
    return batt_min_mv;
  }
  uint32_t getBattMaxMv() const {
    return batt_max_mv;
  }
  uint32_t getFenceNegMinMv() const {
    return fence_neg_min_mv;
  }
  uint32_t getFenceNegMaxMv() const {
    return fence_neg_max_mv;
  }
  uint32_t getFencePosMinMv() const {
    return fence_pos_min_mv;
  }
  uint32_t getFencePosMaxMv() const {
    return fence_pos_max_mv;
  }
  uint32_t getAdcErrorCount() const {
    return adc_error_count;
  }

 private:
  // Private methods

  // Private data
  Preferences prefs;
  bool measurement_running = false;
  uint32_t batt_min_mv = 0;
  uint32_t batt_max_mv = 0;
  uint32_t fence_neg_min_mv = 0;
  uint32_t fence_neg_max_mv = 0;
  uint32_t fence_pos_min_mv = 0;
  uint32_t fence_pos_max_mv = 0;
  uint32_t adc_error_count = 0;
};

#endif  // STATE_H
