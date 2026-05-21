#ifndef CONFIG_MODE_H
#define CONFIG_MODE_H

#include "config.h"
#include "display.h"
#include "state.h"
#include "web_ui.h"

void runConfigMode(Config& config, State& state, Display& display, WebUi& webUi);

#endif  // CONFIG_MODE_H
