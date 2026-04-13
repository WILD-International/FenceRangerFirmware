#ifndef COMMS_H
#define COMMS_H

#include <Preferences.h>
#include <RadioLib.h>

#include "board.h"
#include "config.h"

// now include RadioBoards
// this must be included AFTER RadioLib!
#include <RadioBoards.h>

#include <cstdint>

// regional choices: EU868, US915, AU915, AS923, AS923_2, AS923_3, AS923_4, IN865, KR920, CN470
const LoRaWANBand_t Region = EU868;

// subband choice: for US915/AU915 set to 2, for CN470 set to 1, otherwise leave on 0
const uint8_t subBand = 0;

const uint64_t joinEUI = 0x0000000000000000;

class Comms {
 public:
  Comms(Config* config);

  void begin();
  void saveSession();

 private:
  int16_t lwActivate();

  Config* config;
  Preferences store;  // for radiolib persistent data
  Radio radio;
  LoRaWANNode node;
};

#endif  // COMMS_H
