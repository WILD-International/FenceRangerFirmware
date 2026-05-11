#ifndef COMMS_H
#define COMMS_H

#include <Preferences.h>
#include <RadioLib.h>

#include <cstdint>

#include "board.h"
#include "config.h"
#include "display.h"

// regional choices: EU868, US915, AU915, AS923, AS923_2, AS923_3, AS923_4, IN865, KR920, CN470
const LoRaWANBand_t Region = EU868;

// subband choice: for US915/AU915 set to 2, for CN470 set to 1, otherwise leave on 0
const uint8_t subBand = 0;

const uint64_t joinEUI = 0x0000000000000000;

class Comms {
 public:
  Comms(Config* config);

  int16_t begin(Display* display);
  int16_t send(uint8_t* data, size_t len, Display* display);
  void saveSession();
  bool hasJoined();
  void sleep();
  void resetBootCountForJoining();

 private:
  bool lwActivate(Display* display);

  bool beginCalled = false;
  Config* config;
  Preferences store;  // for radiolib persistent data
  Radio radio;
  LoRaWANNode node;
};

#endif  // COMMS_H
