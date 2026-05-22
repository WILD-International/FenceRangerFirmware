#include "config_mode.h"

#include <Arduino.h>

#include "at_commands.h"
#include "board.h"
#include "esp32-hal-gpio.h"

void runConfigMode(Config& config, State& state, Display& display, WebUi& webUi) {
  uint64_t configModeStartTime = millis();
  bool exitConfigMode = false;
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

  // To add a parameter: add one entry to the relevant group below.
  struct MenuItem {
    const char* label;
    const uint16_t* options;
    uint8_t optCount;
    std::function<uint16_t()> get;
    std::function<void(uint16_t)> set;
  };

  const MenuItem alertItems[] = {
      {"Alert V", VOLTAGE_OPTIONS, sizeof(VOLTAGE_OPTIONS) / sizeof(VOLTAGE_OPTIONS[0]),
       [&] { return config.getAlertThresholdV(); },
       [&](uint16_t v) { config.setAlertThresholdV(v); }},
      {"Recover V", VOLTAGE_OPTIONS, sizeof(VOLTAGE_OPTIONS) / sizeof(VOLTAGE_OPTIONS[0]),
       [&] { return config.getRecoverThresholdV(); },
       [&](uint16_t v) { config.setRecoverThresholdV(v); }},
      {"Burst Sends", BURST_OPTIONS, sizeof(BURST_OPTIONS) / sizeof(BURST_OPTIONS[0]),
       [&] { return (uint16_t)config.getAlertBurstSends(); },
       [&](uint16_t v) { config.setAlertBurstSends(v); }},
      {"Idle Heartbeat", IDLE_HB_OPTIONS, sizeof(IDLE_HB_OPTIONS) / sizeof(IDLE_HB_OPTIONS[0]),
       [&] { return (uint16_t)config.getIdleHeartbeatCycles(); },
       [&](uint16_t v) { config.setIdleHeartbeatCycles(v); }},
      {"Alert Heartb", ALERT_HB_OPTIONS, sizeof(ALERT_HB_OPTIONS) / sizeof(ALERT_HB_OPTIONS[0]),
       [&] { return (uint16_t)config.getAlertHeartbeatCycles(); },
       [&](uint16_t v) { config.setAlertHeartbeatCycles(v); }},
      {"Recovery Count", RECOVER_OPTIONS, sizeof(RECOVER_OPTIONS) / sizeof(RECOVER_OPTIONS[0]),
       [&] { return (uint16_t)config.getRecoveryConfirmCycles(); },
       [&](uint16_t v) { config.setRecoveryConfirmCycles(v); }},
  };

  const MenuItem sleepItems[] = {
      {"Cycle Sleep s", SLEEP_OPTIONS, sizeof(SLEEP_OPTIONS) / sizeof(SLEEP_OPTIONS[0]),
       [&] { return (uint16_t)config.getSleepIntervalS(); },
       [&](uint16_t v) { config.setSleepIntervalS(v); }},
      {"Batt Full mV", BATT_FULL_OPTIONS, sizeof(BATT_FULL_OPTIONS) / sizeof(BATT_FULL_OPTIONS[0]),
       [&] { return config.getBattFullMv(); }, [&](uint16_t v) { config.setBattFullMv(v); }},
      {"Batt Low mV", BATT_LOW_OPTIONS, sizeof(BATT_LOW_OPTIONS) / sizeof(BATT_LOW_OPTIONS[0]),
       [&] { return config.getBattLowMv(); }, [&](uint16_t v) { config.setBattLowMv(v); }},
      {"Sleep Mult", SLEEP_MULT_OPTIONS, sizeof(SLEEP_MULT_OPTIONS) / sizeof(SLEEP_MULT_OPTIONS[0]),
       [&] { return (uint16_t)config.getBattSleepMult(); },
       [&](uint16_t v) { config.setBattSleepMult(v); }},
      {"Recovery N", RECOVERY_N_OPTIONS, sizeof(RECOVERY_N_OPTIONS) / sizeof(RECOVERY_N_OPTIONS[0]),
       [&] { return config.getBattRecoveryN(); }, [&](uint16_t v) { config.setBattRecoveryN(v); }},
  };

  struct MenuGroup {
    const char* label;
    const MenuItem* items;
    int count;
  };

  const MenuGroup GROUPS[] = {
      {"Alert", alertItems, (int)(sizeof(alertItems) / sizeof(alertItems[0]))},
      {"Sleep", sleepItems, (int)(sizeof(sleepItems) / sizeof(sleepItems[0]))},
  };

  const int BROWSE_BACK = 2;
  const int NUM_BROWSE_ITEMS = 3;
  const int VISIBLE_ITEMS = 6;
  const uint32_t LONG_PRESS_MS = 500;

  enum MenuState { INFO, BROWSE, SUBMENU, EDIT };
  MenuState menuState = INFO;
  int browseIndex = 0;
  int activeGroup = 0;
  int submenuIndex = 0;
  int submenuScrollOffset = 0;
  const MenuItem* editItem = nullptr;
  int editOptionIndex = 0;

  auto findOptionIndex = [](const uint16_t* opts, uint8_t count, uint16_t val) -> int {
    for (int i = 0; i < count; i++) {
      if (opts[i] == val) {
        return i;
      }
    }
    return 0;
  };

  auto updateSubmenuScrollOffset = [&]() {
    if (submenuIndex < submenuScrollOffset) {
      submenuScrollOffset = submenuIndex;
    }
    if (submenuIndex >= submenuScrollOffset + VISIBLE_ITEMS) {
      submenuScrollOffset = submenuIndex - VISIBLE_ITEMS + 1;
    }
  };

  int maxGroupItems = 0;
  for (const MenuGroup& g : GROUPS) {
    if (g.count > maxGroupItems)
      maxGroupItems = g.count;
  }
  // +1 row for the Back item at the end of every submenu
  char submenuLabels[maxGroupItems + 1][SUBMENU_LABEL_LEN];

  auto refreshSubMenu = [&]() {
    const MenuGroup& group = GROUPS[activeGroup];
    for (int i = 0; i < group.count; i++) {
      snprintf(submenuLabels[i], SUBMENU_LABEL_LEN, "%-14.14s%5d", group.items[i].label,
               (int)group.items[i].get());
    }
    snprintf(submenuLabels[group.count], SUBMENU_LABEL_LEN, "Back");
    display.showSubMenu(submenuLabels, group.count + 1, submenuIndex, submenuScrollOffset);
  };

  auto handleLongPress = [&]() {
    if (menuState == INFO) {
      menuState = BROWSE;
      display.showMenu(browseIndex);
    } else if (menuState == BROWSE) {
      if (browseIndex == BROWSE_BACK) {
        menuState = INFO;
        display.showConfig(state);
      } else {
        activeGroup = browseIndex;
        submenuIndex = 0;
        submenuScrollOffset = 0;
        menuState = SUBMENU;
        refreshSubMenu();
      }
    } else if (menuState == SUBMENU) {
      int backSlot = GROUPS[activeGroup].count;
      if (submenuIndex == backSlot) {
        menuState = BROWSE;
        display.showMenu(browseIndex);
      } else {
        editItem = &GROUPS[activeGroup].items[submenuIndex];
        uint16_t currentVal = editItem->get();
        editOptionIndex = findOptionIndex(editItem->options, editItem->optCount, currentVal);
        menuState = EDIT;
        display.showMenuEdit(editItem->label, editItem->options[editOptionIndex], currentVal);
      }
    } else {
      // EDIT state
      if (editOptionIndex == editItem->optCount) {
        // CANCEL slot
        menuState = SUBMENU;
        refreshSubMenu();
      } else {
        editItem->set(editItem->options[editOptionIndex]);
        menuState = SUBMENU;
        refreshSubMenu();
      }
    }
  };

  webUi.start();
  display.showConfig(state);

  uint64_t buttonPressStartTime = 0;
  bool buttonPressedState = false;
  bool longPressTriggered = false;

  while (!exitConfigMode && millis() - configModeStartTime < 60 * 60 * 1000) {
    webUi.update();
    while (Serial.available()) {
      char c = Serial.read();
      handleATSerial(c, config, exitConfigMode);
    }

    bool buttonDown = digitalRead(USER_BUTTON) == 0;

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
          exitConfigMode = true;
        } else if (menuState == BROWSE) {
          browseIndex = (browseIndex + 1) % NUM_BROWSE_ITEMS;
          display.showMenu(browseIndex);
        } else if (menuState == SUBMENU) {
          int slots = GROUPS[activeGroup].count + 1;
          submenuIndex = (submenuIndex + 1) % slots;
          updateSubmenuScrollOffset();
          refreshSubMenu();
        } else {
          // EDIT
          uint8_t slots = editItem->optCount + 1;
          editOptionIndex = (editOptionIndex + 1) % slots;
          if (editOptionIndex == editItem->optCount) {
            display.showMenuCancel(editItem->label, editItem->get());
          } else {
            display.showMenuEdit(editItem->label, editItem->options[editOptionIndex],
                                 editItem->get());
          }
        }
      }
    }

    delay(1);
  }

  webUi.stop();
  state.setMode(ModeNormal);
  Serial.println("EXIT");
}
