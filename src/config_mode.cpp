#include "config_mode.h"

#include <Arduino.h>

#include "at_commands.h"
#include "board.h"
#include "comms.h"
#include "esp32-hal-gpio.h"
#include "measure_program.h"

bool runConfigMode(Config& config, State& state, Display& display, WebUi& webUi, Comms& comms) {
  uint64_t configModeStartTime = millis();
  bool exitConfigMode = false;
  bool longSleepRequested = false;
  display.message("CONFIG", 1000);

  // Consume the one-shot OTA-activation request staged in RTC by an EnableOTA downlink (set in
  // Comms::processDownlink): whether to open straight onto the WiFi AP screen, the per-activation
  // AP password, and how long to keep the AP up. Cleared here so it fires exactly once.
  bool openWifiAp = rtc.otaConfigRequested;
  char apPassword[WIFI_AP_PASS_MAX];
  strlcpy(apPassword, rtc.otaApPassword, sizeof(apPassword));
  uint8_t apTimeoutMin = rtc.otaApTimeoutMin;
  rtc.otaConfigRequested = false;
  rtc.otaApPassword[0] = '\0';
  rtc.otaApTimeoutMin = 0;

  // Password the AP comes up with: the downlink-carried one if it meets the WPA2 minimum,
  // otherwise the fixed default. Tracked separately so the AP screen shows the live value.
  const char* downlinkApPassword = (strlen(apPassword) >= 8) ? apPassword : WebUi::AP_PASSWORD;
  const char* activeApPassword = WebUi::AP_PASSWORD;
  // Auto-sleep timer for a remotely-activated AP (0 = rely on the global config-mode timeout).
  uint64_t apStartTime = 0;
  bool apTimeoutActive = false;

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
  const uint16_t RX_LNA_OPTIONS[] = {0, 1};
  const uint16_t MEASURE_DURATION_OPTIONS[] = {1200, 2200, 3000, 6000, 12000};
  const uint16_t FENCE_CAL_OPTIONS[] = {86,  88,  90,  92,  94,  96,  98, 100,
                                        102, 104, 106, 108, 110, 112, 114};
  const uint16_t TX_POWER_OPTIONS[] = {0, 2, 5, 8, 10, 12, 14, 17, 20, 22};
  const uint16_t DATA_RATE_OPTIONS[] = {0, 1, 2, 3, 4, 5};
  const uint16_t PA_SLEEP_OPTIONS[] = {0, 1, 2};  // 0=LOW 1=HIGH 2=FLOAT
  const uint16_t ADR_OPTIONS[] = {0, 1};
  const uint16_t SYNCWORD_PUBLIC_OPTIONS[] = {0, 1};
  const uint16_t SCAN_GUARD_OPTIONS[] = {10, 15, 20, 30, 50, 75, 100, 150, 200};

  // To add a parameter: add one entry to the relevant group below.
  // An item with optCount == 0 is an "action" item: it shows no value, and a long
  // press runs its set() callback (the argument is unused) instead of editing.
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
      {"PA Sleep Mode", PA_SLEEP_OPTIONS, sizeof(PA_SLEEP_OPTIONS) / sizeof(PA_SLEEP_OPTIONS[0]),
       [&] { return (uint16_t)config.getPaSleepMode(); },
       [&](uint16_t v) { config.setPaSleepMode((uint8_t)v); }},
      {"Sleep now", nullptr, 0, nullptr,
       [&](uint16_t) {
         longSleepRequested = true;
         exitConfigMode = true;
       }},
  };

  struct MenuGroup {
    const char* label;
    const MenuItem* items;
    int count;
  };

  const MenuItem measureItems[] = {
      {"Measure ms", MEASURE_DURATION_OPTIONS,
       sizeof(MEASURE_DURATION_OPTIONS) / sizeof(MEASURE_DURATION_OPTIONS[0]),
       [&] { return config.getMeasureDurationMs(); },
       [&](uint16_t ms) { config.setMeasureDurationMs(ms); }},
      {"Fence Cal %", FENCE_CAL_OPTIONS, sizeof(FENCE_CAL_OPTIONS) / sizeof(FENCE_CAL_OPTIONS[0]),
       [&] { return config.getFenceCalPercent(); },
       [&](uint16_t percent) { config.setFenceCalPercent(percent); }},
  };

  const MenuItem radioItems[] = {
      {"RX LNA", RX_LNA_OPTIONS, sizeof(RX_LNA_OPTIONS) / sizeof(RX_LNA_OPTIONS[0]),
       [&] { return (uint16_t)config.getRxLnaEnabled(); },
       [&](uint16_t value) { config.setRxLnaEnabled(value != 0); }},
      {"TX Power dBm", TX_POWER_OPTIONS, sizeof(TX_POWER_OPTIONS) / sizeof(TX_POWER_OPTIONS[0]),
       [&] { return (uint16_t)config.getTxPower(); },
       [&](uint16_t power) { config.setTxPower((int8_t)power); }},
      {"Data Rate", DATA_RATE_OPTIONS, sizeof(DATA_RATE_OPTIONS) / sizeof(DATA_RATE_OPTIONS[0]),
       [&] { return (uint16_t)config.getDataRate(); },
       [&](uint16_t dr) { config.setDataRate((uint8_t)dr); }},
      {"ADR", ADR_OPTIONS, sizeof(ADR_OPTIONS) / sizeof(ADR_OPTIONS[0]),
       [&] { return (uint16_t)config.getAdrEnabled(); },
       [&](uint16_t value) { config.setAdrEnabled(value != 0); }},
      {"Syncword Pub", SYNCWORD_PUBLIC_OPTIONS,
       sizeof(SYNCWORD_PUBLIC_OPTIONS) / sizeof(SYNCWORD_PUBLIC_OPTIONS[0]),
       [&] { return (uint16_t)config.getSyncwordPublic(); },
       [&](uint16_t value) { config.setSyncwordPublic(value != 0); }},
      {"Scan Guard ms", SCAN_GUARD_OPTIONS,
       sizeof(SCAN_GUARD_OPTIONS) / sizeof(SCAN_GUARD_OPTIONS[0]),
       [&] { return config.getScanGuardMs(); }, [&](uint16_t ms) { config.setScanGuardMs(ms); }},
  };

  const MenuGroup GROUPS[] = {
      {"Alert config", alertItems, (int)(sizeof(alertItems) / sizeof(alertItems[0]))},
      {"Sleep config", sleepItems, (int)(sizeof(sleepItems) / sizeof(sleepItems[0]))},
      {"Measure config", measureItems, (int)(sizeof(measureItems) / sizeof(measureItems[0]))},
      {"Radio config", radioItems, (int)(sizeof(radioItems) / sizeof(radioItems[0]))},
  };

  const int BROWSE_VOLT_TEST = 4;
  const int BROWSE_LORA_TEST = 5;
  const int BROWSE_WIFI_AP = 6;
  const int BROWSE_BACK = 7;
  const int NUM_BROWSE_ITEMS = 8;
  const int VISIBLE_ITEMS = 6;
  const uint32_t LONG_PRESS_MS = 500;

  enum MenuState { INFO, BROWSE, SUBMENU, EDIT, VOLT_TEST, LORA_TEST, WIFI_AP_SCREEN };
  MenuState menuState = INFO;
  int browseIndex = 0;
  int activeGroup = 0;
  int submenuIndex = 0;
  int submenuScrollOffset = 0;
  const MenuItem* editItem = nullptr;
  int editOptionIndex = 0;
  MeasureProgram measureProgram;
  Measurement liveMeasurement;
  SignalInfo lastSignalInfo = {};
  LoraTestState loraTestState = LoraTestState::Idle;

  auto findOptionIndex = [](const uint16_t* opts, uint8_t count, uint16_t val) -> int {
    for (int i = 0; i < count; i++) {
      if (opts[i] == val) {
        return i;
      }
    }
    return 0;
  };

  // Items with exactly the options {0, 1} are treated as toggles: shown as ON/OFF and
  // long press flips the value directly without entering the edit screen.
  auto isToggleItem = [](const MenuItem& item) {
    return item.optCount == 2 && item.options[0] == 0 && item.options[1] == 1;
  };

  // Action items (optCount == 0) show no value; a long press runs their set() callback.
  auto isActionItem = [](const MenuItem& item) { return item.optCount == 0; };

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
      if (isActionItem(group.items[i])) {
        snprintf(submenuLabels[i], SUBMENU_LABEL_LEN, "%s", group.items[i].label);
      } else if (isToggleItem(group.items[i])) {
        snprintf(submenuLabels[i], SUBMENU_LABEL_LEN, "%-14.14s%5s", group.items[i].label,
                 group.items[i].get() ? "ON" : "OFF");
      } else {
        snprintf(submenuLabels[i], SUBMENU_LABEL_LEN, "%-14.14s%5d", group.items[i].label,
                 (int)group.items[i].get());
      }
    }
    snprintf(submenuLabels[group.count], SUBMENU_LABEL_LEN, "Back");
    display.showSubMenu(group.label, submenuLabels, group.count + 1, submenuIndex,
                        submenuScrollOffset);
  };

  auto showWifiApScreen = [&]() {
    char ssid[32];
    webUi.apSsid(ssid, sizeof(ssid));
    // A password sent over LoRa is a per-activation secret — mask it so a bystander can't
    // read it off the screen. The fixed default is public, so it's shown as-is.
    bool secret = strcmp(activeApPassword, WebUi::AP_PASSWORD) != 0;
    display.showWifiAp(ssid, secret ? "********" : activeApPassword);
  };

  auto handleLongPress = [&]() {
    if (menuState == INFO) {
      browseIndex = 0;
      menuState = BROWSE;
      display.showMenu(browseIndex);
    } else if (menuState == BROWSE) {
      if (browseIndex == BROWSE_BACK) {
        menuState = INFO;
        display.showConfig(state);
      } else if (browseIndex == BROWSE_VOLT_TEST) {
        menuState = VOLT_TEST;
        liveMeasurement = Measurement{};
        display.showVoltageTest(liveMeasurement);
        measureProgram.begin(config.getMeasureDurationMs());
      } else if (browseIndex == BROWSE_LORA_TEST) {
        comms.sleep();
        menuState = LORA_TEST;
        loraTestState = LoraTestState::Idle;
        display.showLoraTest(comms.hasJoined());
      } else if (browseIndex == BROWSE_WIFI_AP) {
        // Operator-initiated from the menu: fixed password, no AP-specific timeout.
        activeApPassword = WebUi::AP_PASSWORD;
        webUi.start();
        menuState = WIFI_AP_SCREEN;
        showWifiApScreen();
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
        const MenuItem& selectedItem = GROUPS[activeGroup].items[submenuIndex];
        if (isActionItem(selectedItem)) {
          selectedItem.set(0);
        } else if (isToggleItem(selectedItem)) {
          selectedItem.set(selectedItem.get() ? 0 : 1);
          refreshSubMenu();
        } else {
          editItem = &selectedItem;
          uint16_t currentVal = editItem->get();
          editOptionIndex = findOptionIndex(editItem->options, editItem->optCount, currentVal);
          menuState = EDIT;
          display.showMenuEdit(editItem->label, editItem->options[editOptionIndex], currentVal);
        }
      }
    } else if (menuState == VOLT_TEST) {
      // long press ignored in volt test — tap exits
    } else if (menuState == LORA_TEST) {
      if (measureProgram.isFinished()) {
        menuState = BROWSE;
        display.showMenu(browseIndex);
      }
    } else if (menuState == WIFI_AP_SCREEN) {
      webUi.stop();
      menuState = BROWSE;
      display.showMenu(browseIndex);
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

  // WiFi is off by default; it runs only while the WiFi AP screen is active. An EnableOTA
  // downlink (openWifiAp) jumps straight to that screen with the AP started.
  if (openWifiAp) {
    activeApPassword = downlinkApPassword;
    webUi.start(downlinkApPassword);
    apStartTime = millis();
    apTimeoutActive = (apTimeoutMin > 0);
    menuState = WIFI_AP_SCREEN;
    browseIndex = BROWSE_WIFI_AP;
    showWifiApScreen();
  } else {
    display.showConfig(state);
  }

  uint64_t buttonPressStartTime = 0;
  bool buttonPressedState = false;
  bool longPressTriggered = false;

  while (!exitConfigMode && millis() - configModeStartTime < 60 * 60 * 1000) {
    // A remotely-activated AP sleeps the device once its requested duration elapses, so it
    // doesn't idle with WiFi up waiting for a crew that never came.
    if (apTimeoutActive && webUi.isRunning() &&
        millis() - apStartTime >= (uint64_t)apTimeoutMin * 60 * 1000) {
      webUi.stop();
      exitConfigMode = true;
      break;
    }
    // The web UI's "resume monitoring" button stops the AP and returns to normal operation.
    if (webUi.isRunning() && webUi.resumeRequested()) {
      webUi.stop();
      exitConfigMode = true;
      break;
    }
    // The AP is only up on the WiFi AP screen, where no measurement runs, so there's no
    // contention with the measurement DMA pool here.
    if (webUi.isRunning()) {
      webUi.update();
    }
    while (Serial.available()) {
      char c = Serial.read();
      handleATSerial(c, config, exitConfigMode);
    }

    if (!measureProgram.isFinished()) {
      measureProgram.update(config, liveMeasurement);
      if (measureProgram.isFinished()) {
        if (menuState == VOLT_TEST) {
          display.showVoltageTest(liveMeasurement);
          measureProgram.begin(config.getMeasureDurationMs());
        } else if (menuState == LORA_TEST) {
          uint16_t fenceVoltage = max(liveMeasurement.fencePosMaxV, liveMeasurement.fenceNegMaxV);
          Downlink downlink = {};
          SignalInfo signalInfo = {};
          display.message("SEND DATA");
          int16_t sendResult = comms.sendMeasurement(liveMeasurement.battMaxMv, fenceVoltage,
                                                     &display, &downlink, true, &signalInfo);
          if (sendResult > 0) {
            lastSignalInfo = signalInfo;
            loraTestState = LoraTestState::Received;
            for (int i = 0; downlink.type != DownlinkType::None && i < kMaxDownlinkChain; i++) {
              downlink = comms.processDownlink(downlink, &display);
            }
          } else {
            loraTestState = LoraTestState::NoDownlink;
          }
          display.showLoraTest(
              comms.hasJoined(), loraTestState,
              loraTestState == LoraTestState::Received ? &lastSignalInfo : nullptr);
        }
      }
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
        } else if (menuState == VOLT_TEST) {
          measureProgram.stop();
          menuState = BROWSE;
          display.showMenu(browseIndex);
        } else if (menuState == LORA_TEST) {
          if (!measureProgram.isFinished()) {
            // ignore tap during ongoing measurement
          } else if (!comms.hasJoined()) {
            comms.resetJoinSchedule();
            comms.begin(&display);
            comms.saveSession();
            display.showLoraTest(
                comms.hasJoined(), loraTestState,
                loraTestState == LoraTestState::Received ? &lastSignalInfo : nullptr);
          } else if (comms.begin(&display) != RADIOLIB_ERR_NONE) {
            display.message("RADIO ERR", 2000);
            display.showLoraTest(
                comms.hasJoined(), loraTestState,
                loraTestState == LoraTestState::Received ? &lastSignalInfo : nullptr);
          } else {
            liveMeasurement = Measurement{};
            display.message("MEASURE");
            measureProgram.begin(config.getMeasureDurationMs());
          }
        } else if (menuState == WIFI_AP_SCREEN) {
          // tap ignored — long press exits so a stray tap can't drop the AP mid-OTA
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
  state.startDisplayInNormalMode();
  if (longSleepRequested) {
    display.message("SLEEP");
  }

  // Wait for the button to be released before returning. Otherwise a long-press
  // exit (e.g. "Sleep now") leaves GPIO0 held LOW when the following deep sleep
  // arms the EXT0 button wake, which fires immediately and bounces straight back
  // into config mode.
  while (digitalRead(USER_BUTTON) == 0) {
    delay(10);
  }
  delay(50);  // debounce

  Serial.println("EXIT");
  return longSleepRequested;
}
