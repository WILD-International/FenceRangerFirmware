#ifndef CARRIER_BOARD_H
#define CARRIER_BOARD_H

#include <stdint.h>

#include "hal/gpio_types.h"

// Runtime carrier-board revision → pin mapping. One firmware image runs on every revision; the
// stored carrierRev config (see Config) selects the right pins at runtime. Only the opamp-enable
// pin differs today; add a future revision by adding a case below.
//
//   rev 0 = v3.0 (modded): opamp on GPIO 2 (shared with the mis-routed LoRa PA pin). The board
//           mod removes the P-MOSFET that gated the opamp supply and wires the pin straight to
//           the opamp power input, so the pin is active-HIGH and powering the opamp also enables
//           the PA during a measurement (accepted). Driven LOW + held during deep sleep.
//   rev 1 = v3.1: opamp on GPIO 6 (GPIO 2 becomes PA-only). The MOSFET is present, so the pin is
//           active-LOW (drive LOW to power on) and floats during deep sleep.
struct CarrierBoardConfig {
  gpio_num_t opampEnablePin;
  bool opampActiveHigh;
};

inline CarrierBoardConfig carrierBoardConfig(uint8_t rev) {
  switch (rev) {
    case 1:
      return {GPIO_NUM_6, false};  // v3.1
    default:
      return {GPIO_NUM_2, true};  // v3.0 modded
  }
}

#endif  // CARRIER_BOARD_H
