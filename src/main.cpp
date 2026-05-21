#include <Arduino.h>
#include <Preferences.h>
#include <driver/adc.h>
#include <driver/rtc_io.h>
#include <esp_adc_cal.h>

#include "alert.h"
#include "at_commands.h"
#include "board.h"
#include "comms.h"
#include "config.h"
#include "debug.h"
#include "display.h"
#include "esp32-hal-gpio.h"
#include "hal/gpio_types.h"
#include "helpers.h"
#include "isr_button.h"
#include "measure_program.h"
#include "state.h"
#include "web_ui.h"

// Global objects
Config config;
State state(&config);
Comms comms(&config, &state);
Display display(&config);
WebUi webUi(&config);

// Programs
MeasureProgram measureProgram;

void runConfigMode();

void setup() {
  int64_t millis_slept = getTimeSleptMs();
  int64_t now_us = rtcTimeNowUs();

  gpio_deep_sleep_hold_dis();
  rtc_gpio_hold_dis(gpio_num_t(RADIO_NSS));
  gpio_hold_dis(gpio_num_t(LED));  // release hold on LED so we can change state after sleep
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  pinMode(Boot_Key, INPUT);

  rtc_gpio_hold_dis((gpio_num_t)LORA_PA_POWER);
  pinMode(LORA_PA_POWER, OUTPUT);
  digitalWrite(LORA_PA_POWER, LOW);
#if defined(USE_GC1109_PA)
  rtc_gpio_hold_dis((gpio_num_t)LORA_PA_EN);
  pinMode(LORA_PA_EN, OUTPUT);
  digitalWrite(LORA_PA_EN, LOW);
#endif

#if defined(USE_KCT8103L_PA)
  rtc_gpio_hold_dis((gpio_num_t)LORA_PA_CSD);
  pinMode(LORA_PA_CSD, OUTPUT);
  digitalWrite(LORA_PA_CSD, LOW);
#endif

  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || wasButtonPressed()) {
    digitalWrite(LED, HIGH);
    state.setMode(ModeConfig);
  }

  config.load();

  if (!config.hasAppKey()) {
    state.setMode(ModeConfig);
  }

  if (state.useDisplay()) {
    display.turnOn();
  }

  Serial.begin(115200);

  if (state.getMode() == ModeConfig) {
    digitalWrite(LED, LOW);
    runConfigMode();
    comms.resetBootCountForJoining();
  }

  digitalWrite(LED, LOW);
  buttonSetup(Boot_Key);

  display.message("MEASURE");
  measureProgram.run(config, state);
  display.showMeasurement(state, 2000);

  LOG("Firmware version: %s\n", GIT_TAG);
  LOG("Awake after sleeping for %lld ms\n", millis_slept);
  LOG("RTC time now: %lld ms\n", now_us / 1000);

  // Allow interrupting before trying to join
  if (wasButtonPressed()) {
    gotoSleep(100);
  }

  if (!comms.hasJoined()) {
    if (comms.begin(&display) != RADIOLIB_ERR_NONE) {
      display.message("RADIO ERR", 2000);
      gotoSleep(config.getSleepIntervalMs());
    }
  }

  if (comms.hasJoined()) {
    // test the logic for first 1000 calls
    // state.fence_neg_max_mv = 1000;
    // state.fence_pos_max_mv = 2000;
    // for (int i = 0; i < 1000; i++) {
    //   if (i > 200) {
    //     state.fence_neg_max_mv = 4000;
    //     state.fence_pos_max_mv = 4000;
    //   }
    //   if (i > 231) {
    //     state.fence_neg_max_mv = 2000;
    //     state.fence_pos_max_mv = 2000;
    //   }

    // Test values
    // state.batt_min_mv = 3500;
    // state.batt_max_mv = 3500;
    // state.fence_neg_min_v = 1000;
    // state.fence_neg_max_v = 1000;
    // state.fence_pos_min_v = 2000;
    // state.fence_pos_max_v = 2000;
    uint16_t fenceVoltage = max(state.fence_pos_max_v, state.fence_neg_max_v);
    AlertConfig alertCfg = {
        .alertThresholdV = config.getAlertThresholdV(),
        .recoverThresholdV = config.getRecoverThresholdV(),
        .burstSends = config.getAlertBurstSends(),
        .idleHeartbeatCycles = config.getIdleHeartbeatCycles(),
        .alertHeartbeatCycles = config.getAlertHeartbeatCycles(),
        .recoveryConfirmCycles = config.getRecoveryConfirmCycles(),
    };
    bool shouldSend = processMeasurement(fenceVoltage, alertCfg);

    if (shouldSend) {
      // No need to initialize the radio if there's nothing to send
      // This will restore the LoRaWAN session from RTC memory
      if (comms.begin(&display) != RADIOLIB_ERR_NONE) {
        display.message("RADIO ERR", 2000);
        gotoSleep(config.getSleepIntervalMs());
      } else {
        uint16_t batt_voltage = max(state.batt_min_mv, state.batt_max_mv);
        uint16_t fence_neg_voltage = max(state.fence_neg_min_v, state.fence_neg_max_v);
        uint16_t fence_pos_voltage = max(state.fence_pos_min_v, state.fence_pos_max_v);
        uint16_t fence_voltage = max(fence_neg_voltage, fence_pos_voltage);

        if (batt_voltage < 2300) {
          batt_voltage = 2300;
        }
        if (batt_voltage > 4340) {
          batt_voltage = 4340;
        }
        uint8_t batt_voltage_compressed = ((batt_voltage - 2300) >> 3);

        uint8_t uplinkPayload[3];
        uplinkPayload[0] = batt_voltage_compressed;
        uplinkPayload[1] = (fence_voltage >> 8) & 0xFF;
        uplinkPayload[2] = fence_voltage & 0xFF;
        display.message("SENDING");
        Downlink downlink;
        comms.send(uplinkPayload, sizeof(uplinkPayload), &display, &downlink);
        display.message("SENT", 1000);

        switch (downlink.type) {
          case DownlinkType::EnableOTA:
            state.setMode(ModeConfig);
            break;
          case DownlinkType::UpdateConfig:
            config.setSleepIntervalS(downlink.config.sleepIntervalS);
            config.setAlertThresholds(downlink.config.alertV, downlink.config.recoverV);
            break;
          default:
            break;
        }
      }
    }
  }

  // now save session to RTC memory
  comms.saveSession();
  // Put radio to sleep
  comms.sleep();

  if (state.getMode() == ModeConfig) {
    gotoSleep(100);
  }

  if (wasButtonPressed()) {
    gotoSleep(100);
  } else {
    display.message("SLEEP", 500);
  }

  gotoSleep(config.getSleepIntervalMs());
}

void runConfigMode() {
  uint64_t configModeStartTime = millis();
  state.at_command_mode = true;
  display.message("CONFIG", 1000);

  // Predefined values for each cycle parameter
  const uint8_t BURST_OPTIONS[] = {1, 2, 3, 5, 10};
  const uint8_t IDLE_HB_OPTIONS[] = {5, 10, 15, 30, 45, 60, 90, 120};
  const uint8_t ALERT_HB_OPTIONS[] = {5, 10, 15, 20, 30, 45, 60};
  const uint8_t RECOVER_OPTIONS[] = {5, 10, 15, 20, 30, 45, 60};

  struct ValueItem {
    const char* label;
    const uint8_t* options;
    uint8_t count;
  };
  const ValueItem VALUE_ITEMS[] = {
      {"Burst Sends", BURST_OPTIONS, sizeof(BURST_OPTIONS)},
      {"Idle Heartbeat", IDLE_HB_OPTIONS, sizeof(IDLE_HB_OPTIONS)},
      {"Alert Heartbeat", ALERT_HB_OPTIONS, sizeof(ALERT_HB_OPTIONS)},
      {"Recovery Confirm", RECOVER_OPTIONS, sizeof(RECOVER_OPTIONS)},
  };

  const int MENU_DEBUG = 4;
  const int MENU_BACK = 5;
  const int NUM_MENU_ITEMS = 6;
  const uint32_t LONG_PRESS_MS = 500;

  enum MenuState { INFO, BROWSE, EDIT };
  MenuState menuState = INFO;
  int menuIndex = 0;
  int editOptionIndex = 0;

  auto findOptionIndex = [](const uint8_t* opts, uint8_t count, uint8_t val) -> int {
    for (int i = 0; i < count; i++) {
      if (opts[i] == val) {
        return i;
      }
    }
    return 0;
  };

  auto getCurrentValue = [&](int idx) -> uint8_t {
    switch (idx) {
      case 0: return config.getAlertBurstSends();
      case 1: return config.getIdleHeartbeatCycles();
      case 2: return config.getAlertHeartbeatCycles();
      case 3: return config.getRecoveryConfirmCycles();
      default: return 0;
    }
  };

  auto saveValue = [&](int idx, uint8_t val) {
    switch (idx) {
      case 0:
        config.setAlertCycles(val, config.getIdleHeartbeatCycles(),
                              config.getAlertHeartbeatCycles(), config.getRecoveryConfirmCycles());
        break;
      case 1:
        config.setAlertCycles(config.getAlertBurstSends(), val,
                              config.getAlertHeartbeatCycles(), config.getRecoveryConfirmCycles());
        break;
      case 2:
        config.setAlertCycles(config.getAlertBurstSends(), config.getIdleHeartbeatCycles(), val,
                              config.getRecoveryConfirmCycles());
        break;
      case 3:
        config.setAlertCycles(config.getAlertBurstSends(), config.getIdleHeartbeatCycles(),
                              config.getAlertHeartbeatCycles(), val);
        break;
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
      display.showMenu(menuIndex, Debug::enabled());
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
        display.showMenu(menuIndex, Debug::enabled());
      } else {
        menuState = EDIT;
        uint8_t currentVal = getCurrentValue(menuIndex);
        editOptionIndex = findOptionIndex(VALUE_ITEMS[menuIndex].options,
                                         VALUE_ITEMS[menuIndex].count, currentVal);
        display.showMenuEdit(VALUE_ITEMS[menuIndex].label,
                             VALUE_ITEMS[menuIndex].options[editOptionIndex],
                             getCurrentValue(menuIndex));
      }
    } else {
      if (editOptionIndex == VALUE_ITEMS[menuIndex].count) {
        // Cancel slot: discard, return to browse
        menuState = BROWSE;
        display.showMenu(menuIndex, Debug::enabled());
      } else {
        saveValue(menuIndex, VALUE_ITEMS[menuIndex].options[editOptionIndex]);
        menuState = BROWSE;
        display.showMenu(menuIndex, Debug::enabled());
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
          display.showMenu(menuIndex, Debug::enabled());
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

void loop() {
  Serial.println("Should not have ended up in loop, going to sleep");
  gotoSleep(3600 * 1000);
}
