#ifndef AT_COMMANDS_H
#define AT_COMMANDS_H

#include <Arduino.h>

#include "config.h"

// Action the config-mode loop should take in response to a completed AT command. Most input
// (partial lines, queries, value sets) yields None; commands that steer the loop return a verb.
enum class AtAction { None, Exit, ShowUpdateOk };

// Feeds one serial character to the AT parser; returns an action when a command line completes.
AtAction handleATSerial(char c, Config& config);

#endif  // AT_COMMANDS_H
