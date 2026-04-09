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

 private:
  // Private methods

  // Private data
  Preferences prefs;
};

#endif  // STATE_H
