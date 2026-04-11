#ifndef AT_COMMANDS_H
#define AT_COMMANDS_H

#include <Arduino.h>

#include "config.h"
#include "state.h"

void handleATSerial(char c, Config& config, State& state);

#endif  // AT_COMMANDS_H
