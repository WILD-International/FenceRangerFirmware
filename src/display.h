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
  void turnOn();
  void turnOff();
  void message(const char* msg, uint32_t delayMs = 0);
  void showConfig(State& state);
  void showMeasurement(State& state, uint32_t delayMs = 0);
  void showMenu(int selectedIndex, int scrollOffset, bool debugEnabled);
  void showMenuEdit(const char* name, uint16_t value, uint16_t currentValue);
  void showMenuCancel(const char* name, uint16_t currentValue);

 private:
  void displayTitle();
  bool enabled;
  Config* config;
  Adafruit_SSD1306 display;
};

#endif  // DISPLAY_H
