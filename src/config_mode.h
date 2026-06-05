#ifndef CONFIG_MODE_H
#define CONFIG_MODE_H

#include "comms.h"
#include "config.h"
#include "display.h"
#include "state.h"
#include "web_ui.h"

// Returns true if the user requested an immediate deep sleep ("Sleep now").
// The one-shot OTA-activation request staged in RTC by an EnableOTA downlink (open directly on
// the WiFi AP screen, with a per-activation password and AP timeout) is consumed inside.
bool runConfigMode(Config& config, State& state, Display& display, WebUi& webUi, Comms& comms);

#endif  // CONFIG_MODE_H
