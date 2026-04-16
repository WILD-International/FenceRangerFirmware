#include <Arduino.h>
#include <Preferences.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

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

// Global objects
Config config;
State state;
Comms comms(&config);
Display display(&config);

// Programs
MeasureProgram measureProgram;

enum Mode { Normal, Config };
RTC_DATA_ATTR Mode currentMode = Normal;

RTC_DATA_ATTR int64_t useDisplayInNormalModeStartTime = 0;

bool useDisplayInNormalMode();
bool useDisplayInNormalMode() {
  int64_t now = rtcTimeNowUs();
  if (useDisplayInNormalModeStartTime == 0) {
    return false;
  }

  // use display for debugging for max of 60 minutes
  if (now - useDisplayInNormalModeStartTime < 60LL * 60 * 1000 * 1000) {
    return true;
  }
  return false;
}

bool useDisplay();
bool useDisplay() {
  if (currentMode == Config) {
    return true;
  }
  return useDisplayInNormalMode();
}

void runConfigMode();

void setup() {
  int64_t millis_slept = getTimeSleptMs();
  int64_t now_us = rtcTimeNowUs();

  gpio_hold_dis(gpio_num_t(LED));  // release hold on LED so we can change state after sleep
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  pinMode(Boot_Key, INPUT);

  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || wasButtonPressed()) {
    digitalWrite(LED, HIGH);
    currentMode = Config;
  }

  config.load();

  if (!config.hasAppKey()) {
    currentMode = Config;
  }

  if (useDisplay()) {
    display.turnOn();
  }

  Serial.begin(115200);

  if (currentMode == Config) {
    digitalWrite(LED, LOW);
    runConfigMode();
  }
  digitalWrite(LED, LOW);

  buttonSetup(Boot_Key);
  display.message("MEASURE");
  measureProgram.begin();
  while (!measureProgram.isFinished()) {
    measureProgram.update(state);
    delay(1);
  }
  display.showMeasurement(state);

  delay(2000);
  digitalWrite(LED, LOW);
  LOG("Firmware version: %s\n", GIT_TAG);
  LOG("Awake after sleeping for %lld ms\n", millis_slept);
  LOG("RTC time now: %lld ms\n", now_us / 1000);

  // This will restore the LoRaWAN session from RTC memory, or join the network on first boot
  // comms.begin();

  // now save session to RTC memory
  // comms.saveSession();

  delay(2000);

  if (wasButtonPressed()) {
    gotoSleep(100);
  } else {
    display.message("SLEEP", 500);
  }

  gotoSleep(30 * 1000);
}

void runConfigMode() {
  // run config mode for max 1 hour
  uint64_t configModeStartTime = millis();

  state.at_command_mode = true;
  display.message("CONFIG", 1000);
  display.showConfig(state);

  uint64_t buttonPressStartTime = 0;
  bool buttonPressedState = false;

  while (state.at_command_mode && millis() - configModeStartTime < 60 * 60 * 1000) {
    while (Serial.available()) {
      char c = Serial.read();
      handleATSerial(c, config, state);
    }

    if (digitalRead(Boot_Key) == 0) {
      if (!buttonPressedState) {
        buttonPressedState = true;
        buttonPressStartTime = millis();
      } else {
        uint64_t pressedTime = millis() - buttonPressStartTime;
        if (pressedTime > 2500) {
          if (useDisplayInNormalMode()) {
            useDisplayInNormalModeStartTime = 0;
            display.message("DISABLED", 1000);
          } else {
            useDisplayInNormalModeStartTime = rtcTimeNowUs();
            display.message("ENABLED", 1000);
          }
        } else if (pressedTime > 1000) {
          display.debugModeProgressBar((pressedTime - 1000) * 100 / 1500.0,
                                       !useDisplayInNormalMode());
          continue;
        }
      }
    } else {
      if (buttonPressedState && millis() - buttonPressStartTime < 600) {
        display.message("RESUME", 1000);
        break;
      }
      buttonPressedState = false;
    }
    display.showConfig(state);
    delay(1);
  }
  currentMode = Normal;
  state.at_command_mode = false;
  Serial.println("EXIT");
}

void loop() {
  Serial.println("Should not have ended up in loop, going to sleep");
  gotoSleep(3600 * 1000);
}
