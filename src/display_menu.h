#ifndef DISPLAY_MENU_H
#define DISPLAY_MENU_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "button.h"
#include "config.h"
#include "state.h"

enum DisplayMenuState {
  DISPLAY_MENU_OFF,
  DISPLAY_MENU_LIVE_INFO,
  DISPLAY_MENU_DEVICE_INFO,
};

class DisplayMenu {
 public:
  DisplayMenu(Config* config);
  ~DisplayMenu();

  // Public methods
  void begin();
  void update(State& state);
  void buttonEvent(ButtonEvent event);

 private:
  // Private methods
  void changeState(DisplayMenuState newState);

  // Private data
  Config* config;
  uint32_t last_button_event;
  Adafruit_SSD1306 display;
  DisplayMenuState currentMenu = DISPLAY_MENU_OFF;
};

#endif
