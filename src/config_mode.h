#ifndef CONFIG_MODE_H
#define CONFIG_MODE_H

#include "comms.h"
#include "config.h"
#include "display.h"
#include "state.h"
#include "web_ui.h"

// Returns true if the user requested an immediate deep sleep ("Sleep now").
// When openWifiAp is true (config mode was requested by an EnableOTA downlink), config mode
// opens directly on the WiFi AP screen with the AP already started.
bool runConfigMode(Config& config, State& state, Display& display, WebUi& webUi, Comms& comms,
                   bool openWifiAp = false);

#endif  // CONFIG_MODE_H
