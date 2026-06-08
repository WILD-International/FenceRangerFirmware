#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "config.h"
#include "measure_program.h"
#include "signal_info.h"
#include "state.h"

// Buffer size for each submenu label string: "%-14s%5d" = 19 chars + null + margin
constexpr int SUBMENU_LABEL_LEN = 22;

class Display {
 public:
  Display(Config* config);
  ~Display();
  void turnOn();
  void turnOff();
  void message(const char* msg, uint32_t delayMs = 0);
  void messageWithCode(const char* msg, int code, uint32_t delayMs = 0);
  void showUpdateDone(const char* version);
  void showConfig(State& state);
  void showMeasurement(const Measurement& measurement, uint32_t delayMs = 0);
  void showMenu(int selectedIndex);
  void showSubMenu(const char* header, const char labels[][SUBMENU_LABEL_LEN], int totalItems,
                   int selectedIndex, int scrollOffset);
  void showMenuEdit(const char* name, uint16_t value, uint16_t currentValue,
                    const char* valueLabel = nullptr, const char* currentLabel = nullptr);
  void showMenuCancel(const char* name, uint16_t currentValue, const char* currentLabel = nullptr);
  void showVoltageTest(const Measurement& measurement);
  void showLoraTest(bool joined, LoraTestState state = LoraTestState::Idle,
                    const SignalInfo* signalInfo = nullptr);
  void showWifiAp(const char* ssid, const char* password);
  void showWifiStation(const char* ssid, const char* status, const char* ip);

 private:
  void displayTitle();
  void displayHeader(const char* text);
  bool enabled;
  Config* config;
  Adafruit_SSD1306 display;
};

#endif  // DISPLAY_H
