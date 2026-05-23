#pragma once

#include <cstdint>

enum class LoraTestState { Idle, NoDownlink, Received };

struct SignalInfo {
  float rssi = 0.0f;
  float snr = 0.0f;
  float freq = 0.0f;
  uint8_t datarate = 0;
  int16_t rxWindow = 0;
};
