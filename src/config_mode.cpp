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
  const uint16_t IDLE_HB_OPTIONS[] = {1, 5, 10, 15, 30, 45, 60, 90, 120};
  const uint16_t ALERT_HB_OPTIONS[] = {1, 5, 10, 15, 20, 30, 45, 60};
  const uint16_t RECOVER_OPTIONS[] = {5, 10, 15, 20, 30, 45, 60};
  const uint16_t VOLTAGE_OPTIONS[] = {500,  1000, 1500, 2000, 2500, 3000, 3500, 4000,
                                      4500, 5000, 6000, 7000, 8000, 9000, 10000};
  const uint16_t BATT_FULL_OPTIONS[] = {3800, 3900, 4000, 4100, 4200};
  const uint16_t BATT_LOW_OPTIONS[] = {2800, 2900, 3000, 3100, 3200, 3300, 3400, 3500, 3600};
  const uint16_t SLEEP_MULT_OPTIONS[] = {2, 3, 4, 5, 6, 8, 10, 15, 20, 25, 30};
  const uint16_t RECOVERY_N_OPTIONS[] = {5, 10, 15, 20, 25, 30, 40, 50, 75, 100, 150, 200};

  struct ValueItem {
    const char* label;
    const uint16_t* options;
    uint8_t count;
  };
  // Indices: 0=Cycle Sleep, 1=Burst Sends, 2=Idle Hb, 3=Alert Hb, 4=Recovery Confirm,
  //          5=Alert V, 6=Recover V, 7=Batt Full, 8=Batt Low, 9=Sleep Mult, 10=Recovery N
  const ValueItem VALUE_ITEMS[] = {
      {"Cycle Sleep Sec", SLEEP_OPTIONS, sizeof(SLEEP_OPTIONS) / sizeof(SLEEP_OPTIONS[0])},
      {"Burst Sends", BURST_OPTIONS, sizeof(BURST_OPTIONS) / sizeof(BURST_OPTIONS[0])},
      {"Idle Heartbeat", IDLE_HB_OPTIONS, sizeof(IDLE_HB_OPTIONS) / sizeof(IDLE_HB_OPTIONS[0])},
      {"Alert Heartbeat", ALERT_HB_OPTIONS, sizeof(ALERT_HB_OPTIONS) / sizeof(ALERT_HB_OPTIONS[0])},
      {"Recovery Confirm", RECOVER_OPTIONS, sizeof(RECOVER_OPTIONS) / sizeof(RECOVER_OPTIONS[0])},
      {"Alert V", VOLTAGE_OPTIONS, sizeof(VOLTAGE_OPTIONS) / sizeof(VOLTAGE_OPTIONS[0])},
      {"Recover V", VOLTAGE_OPTIONS, sizeof(VOLTAGE_OPTIONS) / sizeof(VOLTAGE_OPTIONS[0])},
      {"Batt Full mV", BATT_FULL_OPTIONS, sizeof(BATT_FULL_OPTIONS) / sizeof(BATT_FULL_OPTIONS[0])},
      {"Batt Low mV", BATT_LOW_OPTIONS, sizeof(BATT_LOW_OPTIONS) / sizeof(BATT_LOW_OPTIONS[0])},
      {"Sleep Mult", SLEEP_MULT_OPTIONS,
       sizeof(SLEEP_MULT_OPTIONS) / sizeof(SLEEP_MULT_OPTIONS[0])},
      {"Recovery N", RECOVERY_N_OPTIONS,
       sizeof(RECOVERY_N_OPTIONS) / sizeof(RECOVERY_N_OPTIONS[0])},
  };

  struct MenuGroup {
    const char* label;
    const int* itemIndices;
    int count;
  };
  const int ALERT_INDICES[] = {5, 6, 1, 2, 3, 4};
  const int SLEEP_INDICES[] = {0, 7, 8, 9, 10};
  const MenuGroup GROUPS[] = {
      {"Alert", ALERT_INDICES, 6},
      {"Sleep", SLEEP_INDICES, 5},
  };

  const int BROWSE_DEBUG = 2;
  const int BROWSE_BACK = 3;
  const int NUM_BROWSE_ITEMS = 4;
  const int VISIBLE_ITEMS = 6;
  const uint32_t LONG_PRESS_MS = 500;

  enum MenuState { INFO, BROWSE, SUBMENU, EDIT };
  MenuState menuState = INFO;
  int browseIndex = 0;
  int activeGroup = 0;
  int submenuIndex = 0;
  int submenuScrollOffset = 0;
  int editItemIndex = 0;
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
      case 5:
        return config.getAlertThresholdV();
      case 6:
        return config.getRecoverThresholdV();
      case 7:
        return config.getBattFullMv();
      case 8:
        return config.getBattLowMv();
      case 9:
        return config.getBattSleepMult();
      case 10:
        return config.getBattRecoveryN();
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
      case 5:
        config.setAlertThresholds(val, config.getRecoverThresholdV());
        break;
      case 6:
        config.setAlertThresholds(config.getAlertThresholdV(), val);
        break;
      case 7:
        config.setBattAdaptiveSleep(val, config.getBattLowMv(), config.getBattSleepMult(),
                                    config.getBattRecoveryN());
        break;
      case 8:
        config.setBattAdaptiveSleep(config.getBattFullMv(), val, config.getBattSleepMult(),
                                    config.getBattRecoveryN());
        break;
      case 9:
        config.setBattAdaptiveSleep(config.getBattFullMv(), config.getBattLowMv(), (uint8_t)val,
                                    config.getBattRecoveryN());
        break;
      case 10:
        config.setBattAdaptiveSleep(config.getBattFullMv(), config.getBattLowMv(),
                                    config.getBattSleepMult(), val);
        break;
    }
  };

  auto updateSubmenuScrollOffset = [&]() {
    int total = GROUPS[activeGroup].count + 1;
    if (submenuIndex < submenuScrollOffset) {
      submenuScrollOffset = submenuIndex;
    }
    if (submenuIndex >= submenuScrollOffset + VISIBLE_ITEMS) {
      submenuScrollOffset = submenuIndex - VISIBLE_ITEMS + 1;
    }
    (void)total;
  };

  auto handleLongPress = [&]() {
    if (menuState == INFO) {
      menuState = BROWSE;
      display.showMenu(browseIndex, Debug::enabled());
    } else if (menuState == BROWSE) {
      if (browseIndex == BROWSE_BACK) {
        menuState = INFO;
        display.showConfig(state);
      } else if (browseIndex == BROWSE_DEBUG) {
        if (Debug::enabled()) {
          Debug::disable();
        } else {
          Debug::enable();
        }
        display.showMenu(browseIndex, Debug::enabled());
      } else {
        activeGroup = browseIndex;
        submenuIndex = 0;
        submenuScrollOffset = 0;
        menuState = SUBMENU;
        display.showSubMenu(activeGroup, submenuIndex, submenuScrollOffset);
      }
    } else if (menuState == SUBMENU) {
      int backSlot = GROUPS[activeGroup].count;
      if (submenuIndex == backSlot) {
        menuState = BROWSE;
        display.showMenu(browseIndex, Debug::enabled());
      } else {
        editItemIndex = GROUPS[activeGroup].itemIndices[submenuIndex];
        uint16_t currentVal = getCurrentValue(editItemIndex);
        editOptionIndex = findOptionIndex(VALUE_ITEMS[editItemIndex].options,
                                          VALUE_ITEMS[editItemIndex].count, currentVal);
        menuState = EDIT;
        display.showMenuEdit(VALUE_ITEMS[editItemIndex].label,
                             VALUE_ITEMS[editItemIndex].options[editOptionIndex], currentVal);
      }
    } else {
      // EDIT state
      if (editOptionIndex == VALUE_ITEMS[editItemIndex].count) {
        // CANCEL slot
        menuState = SUBMENU;
        display.showSubMenu(activeGroup, submenuIndex, submenuScrollOffset);
      } else {
        saveValue(editItemIndex, VALUE_ITEMS[editItemIndex].options[editOptionIndex]);
        menuState = SUBMENU;
        display.showSubMenu(activeGroup, submenuIndex, submenuScrollOffset);
      }
    }
  };

  webUi.start();
  display.showConfig(state);

  uint64_t buttonPressStartTime = 0;
  bool buttonPressedState = false;
  bool longPressTriggered = false;

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
          browseIndex = (browseIndex + 1) % NUM_BROWSE_ITEMS;
          display.showMenu(browseIndex, Debug::enabled());
        } else if (menuState == SUBMENU) {
          int slots = GROUPS[activeGroup].count + 1;
          submenuIndex = (submenuIndex + 1) % slots;
          updateSubmenuScrollOffset();
          display.showSubMenu(activeGroup, submenuIndex, submenuScrollOffset);
        } else {
          // EDIT
          uint8_t slots = VALUE_ITEMS[editItemIndex].count + 1;
          editOptionIndex = (editOptionIndex + 1) % slots;
          if (editOptionIndex == VALUE_ITEMS[editItemIndex].count) {
            display.showMenuCancel(VALUE_ITEMS[editItemIndex].label,
                                   getCurrentValue(editItemIndex));
          } else {
            display.showMenuEdit(VALUE_ITEMS[editItemIndex].label,
                                 VALUE_ITEMS[editItemIndex].options[editOptionIndex],
                                 getCurrentValue(editItemIndex));
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
