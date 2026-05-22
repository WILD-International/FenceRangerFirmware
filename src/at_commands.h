#ifndef AT_COMMANDS_H
#define AT_COMMANDS_H

#include <Arduino.h>

#include "config.h"

void handleATSerial(char c, Config& config, bool& exitConfigMode);

#endif  // AT_COMMANDS_H
