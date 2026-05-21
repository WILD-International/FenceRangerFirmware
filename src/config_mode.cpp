#include "config_mode.h"

#include <Arduino.h>

#include "at_commands.h"
#include "board.h"
#include "debug.h"
#include "esp32-hal-gpio.h"

void runConfigMode(Config& config, State& state, Display& display, WebUi& webUi) {
  uint64_t configModeStartTime = millis();
  state.at_command_mode = true;
  display.message("CONFIG", 1000);

  const uint16_t SLEEP_OPTIONS[] = {10, 15, 20, 30, 45, 60, 90, 120, 180, 300, 600, 900};
  const uint16_t BURST_OPTIONS[] = {1, 2, 3, 5, 10};
  const uint16_t IDLE_HB_OPTIONS[] = {5, 10, 15, 30, 45, 60, 90, 120};
  const uint16_t ALERT_HB_OPTIONS[] = {5, 10, 15, 20, 30, 45, 60};
  const uint16_t RECOVER_OPTIONS[] = {5, 10, 15, 20, 30, 45, 60};

  struct ValueItem {
    const char* label;
    const uint16_t* options;
    uint8_t count;
  };
  const ValueItem VALUE_ITEMS[] = {
      {"Cycle Sleep", SLEEP_OPTIONS, sizeof(SLEEP_OPTIONS) / sizeof(SLEEP_OPTIONS[0])},
      {"Burst Sends", BURST_OPTIONS, sizeof(BURST_OPTIONS) / sizeof(BURST_OPTIONS[0])},
      {"Idle Heartbeat", IDLE_HB_OPTIONS, sizeof(IDLE_HB_OPTIONS) / sizeof(IDLE_HB_OPTIONS[0])},
      {"Alert Heartbeat", ALERT_HB_OPTIONS, sizeof(ALERT_HB_OPTIONS) / sizeof(ALERT_HB_OPTIONS[0])},
      {"Recovery Confirm", RECOVER_OPTIONS, sizeof(RECOVER_OPTIONS) / sizeof(RECOVER_OPTIONS[0])},
  };

  const int MENU_DEBUG = 5;
  const int MENU_BACK = 6;
  const int NUM_MENU_ITEMS = 7;
  const int VISIBLE_ITEMS = 6;
  const uint32_t LONG_PRESS_MS = 500;

  enum MenuState { INFO, BROWSE, EDIT };
  MenuState menuState = INFO;
  int menuIndex = 0;
  int scrollOffset = 0;
  int editOptionIndex = 0;

  auto findOptionIndex = [](const uint16_t* opts, uint8_t count, uint16_t val) -> int {
    for (int i = 0; i < count; i++) {
      if (opts[i] == val) {
        return i;
      }
    }
    return 0;
  };

  auto getCurrentValue = [&](int idx) -> uint16_t {
    switch (idx) {
      case 0:
        return (uint16_t)config.getSleepIntervalS();
      case 1:
        return config.getAlertBurstSends();
      case 2:
        return config.getIdleHeartbeatCycles();
      case 3:
        return config.getAlertHeartbeatCycles();
      case 4:
        return config.getRecoveryConfirmCycles();
      default:
        return 0;
    }
  };

  auto saveValue = [&](int idx, uint16_t val) {
    switch (idx) {
      case 0:
        config.setSleepIntervalS(val);
        break;
      case 1:
        config.setAlertCycles(val, config.getIdleHeartbeatCycles(),
                              config.getAlertHeartbeatCycles(), config.getRecoveryConfirmCycles());
        break;
      case 2:
        config.setAlertCycles(config.getAlertBurstSends(), val, config.getAlertHeartbeatCycles(),
                              config.getRecoveryConfirmCycles());
        break;
      case 3:
        config.setAlertCycles(config.getAlertBurstSends(), config.getIdleHeartbeatCycles(), val,
                              config.getRecoveryConfirmCycles());
        break;
      case 4:
        config.setAlertCycles(config.getAlertBurstSends(), config.getIdleHeartbeatCycles(),
                              config.getAlertHeartbeatCycles(), val);
        break;
    }
  };

  auto updateScrollOffset = [&]() {
    if (menuIndex < scrollOffset) {
      scrollOffset = menuIndex;
    }
    if (menuIndex >= scrollOffset + VISIBLE_ITEMS) {
      scrollOffset = menuIndex - VISIBLE_ITEMS + 1;
    }
  };

  webUi.start();
  display.showConfig(state);

  uint64_t buttonPressStartTime = 0;
  bool buttonPressedState = false;
  bool longPressTriggered = false;

  auto handleLongPress = [&]() {
    if (menuState == INFO) {
      menuState = BROWSE;
      display.showMenu(menuIndex, scrollOffset, Debug::enabled());
    } else if (menuState == BROWSE) {
      if (menuIndex == MENU_BACK) {
        menuState = INFO;
        display.showConfig(state);
      } else if (menuIndex == MENU_DEBUG) {
        if (Debug::enabled()) {
          Debug::disable();
        } else {
          Debug::enable();
        }
        display.showMenu(menuIndex, scrollOffset, Debug::enabled());
      } else {
        menuState = EDIT;
        uint16_t currentVal = getCurrentValue(menuIndex);
        editOptionIndex = findOptionIndex(VALUE_ITEMS[menuIndex].options,
                                          VALUE_ITEMS[menuIndex].count, currentVal);
        display.showMenuEdit(VALUE_ITEMS[menuIndex].label,
                             VALUE_ITEMS[menuIndex].options[editOptionIndex], currentVal);
      }
    } else {
      if (editOptionIndex == VALUE_ITEMS[menuIndex].count) {
        menuState = BROWSE;
        display.showMenu(menuIndex, scrollOffset, Debug::enabled());
      } else {
        saveValue(menuIndex, VALUE_ITEMS[menuIndex].options[editOptionIndex]);
        menuState = BROWSE;
        display.showMenu(menuIndex, scrollOffset, Debug::enabled());
      }
    }
  };

  while (state.at_command_mode && millis() - configModeStartTime < 60 * 60 * 1000) {
    webUi.update();
    while (Serial.available()) {
      char c = Serial.read();
      handleATSerial(c, config, state);
    }

    bool buttonDown = digitalRead(Boot_Key) == 0;

    if (buttonDown && !buttonPressedState) {
      buttonPressedState = true;
      buttonPressStartTime = millis();
      longPressTriggered = false;
    } else if (buttonDown && !longPressTriggered) {
      if (millis() - buttonPressStartTime >= LONG_PRESS_MS) {
        longPressTriggered = true;
        handleLongPress();
      }
    } else if (!buttonDown && buttonPressedState) {
      buttonPressedState = false;
      if (!longPressTriggered) {
        if (menuState == INFO) {
          state.at_command_mode = false;
        } else if (menuState == BROWSE) {
          menuIndex = (menuIndex + 1) % NUM_MENU_ITEMS;
          updateScrollOffset();
          display.showMenu(menuIndex, scrollOffset, Debug::enabled());
        } else {
          uint8_t slots = VALUE_ITEMS[menuIndex].count + 1;
          editOptionIndex = (editOptionIndex + 1) % slots;
          if (editOptionIndex == VALUE_ITEMS[menuIndex].count) {
            display.showMenuCancel(VALUE_ITEMS[menuIndex].label, getCurrentValue(menuIndex));
          } else {
            display.showMenuEdit(VALUE_ITEMS[menuIndex].label,
                                 VALUE_ITEMS[menuIndex].options[editOptionIndex],
                                 getCurrentValue(menuIndex));
          }
        }
      }
    }

    delay(1);
  }

  webUi.stop();
  state.setMode(ModeNormal);
  state.at_command_mode = false;
  Serial.println("EXIT");
}
