#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "config.h"
#include "state.h"

class Display {
 public:
  Display(Config* config);
  ~Display();
  void begin();
  void trigger();
  void message(const char* msg, uint32_t delayMs = 0);
  void showConfig(State& state);
  void showMeasurement(State& state);

 private:
  void turnOn();
  void turnOff();
  void displayTitle();
  bool enabled;
  Config* config;
  Adafruit_SSD1306 display;
};

#endif  // DISPLAY_H
