#ifndef CONFIG_MODE_H
#define CONFIG_MODE_H

#include "comms.h"
#include "config.h"
#include "display.h"
#include "state.h"
#include "web_ui.h"

// Returns true if the user requested an immediate deep sleep ("Sleep now").
bool runConfigMode(Config& config, State& state, Display& display, WebUi& webUi, Comms& comms);

#endif  // CONFIG_MODE_H
