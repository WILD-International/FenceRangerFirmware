#include "state.h"

// Persistent data that lives in RTC memory
RTC_DATA_ATTR ProgramState programState;

State::State() {}

State::~State() {}

ProgramState State::getProgramState() {
  return programState;
}

void State::setProgramState(ProgramState newState) {
  programState = newState;
}
