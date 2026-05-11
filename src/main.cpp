#include <Arduino.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <driver/adc.h>
#include <driver/rtc_io.h>
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

WebServer* server = nullptr;

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
  display.showMeasurement(state);

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

    state.fence_neg_max_v = 1000;
    state.fence_pos_max_v = 2000;
    bool shouldSend = state.processMeasurement();
    // }

    if (comms.begin(&display) != RADIOLIB_ERR_NONE) {
      display.message("RADIO ERR", 2000);
      gotoSleep(config.getSleepIntervalMs());
    }

    if (shouldSend) {
      // This will restore the LoRaWAN session from RTC memory, or join the network on first boot
      display.message("SEND", 1000);
    }
  }

  // now save session to RTC memory
  comms.saveSession();
  // Put radio to sleep
  comms.sleep();

  delay(2000);

  if (wasButtonPressed()) {
    gotoSleep(100);
  } else {
    display.message("SLEEP", 500);
  }

  gotoSleep(config.getSleepIntervalMs());
}

const char* uploadPage = R"(
<!DOCTYPE html><html><body>
<form method='POST' action='/update' enctype='multipart/form-data'>
  <input type='file' name='firmware'>
  <input type='submit' value='Update'>
</form></body></html>
)";

void startOTA() {
  char ssid[32] = "FenceRanger-";
  bytesToHex(config.getDevEUI(), DEVEUI_LEN, ssid + 12);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, "wildinternational");

  server = new WebServer(80);

  server->on("/", HTTP_GET, []() { server->send(200, "text/html", uploadPage); });
  server->on(
      "/update", HTTP_POST,
      []() {
        server->send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
        delay(500);
        ESP.restart();
      },
      []() {
        HTTPUpload& upload = server->upload();
        if (upload.status == UPLOAD_FILE_START) {
          Update.begin(UPDATE_SIZE_UNKNOWN);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          Update.write(upload.buf, upload.currentSize);
        } else if (upload.status == UPLOAD_FILE_END) {
          Update.end(true);
        }
      });

  server->begin();
}

void stopOTA() {
  server->stop();
  delete server;
  server = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void runConfigMode() {
  // run config mode for max 1 hour
  uint64_t configModeStartTime = millis();

  state.at_command_mode = true;
  display.message("CONFIG", 1000);
  display.showConfig(state);

  uint64_t buttonPressStartTime = 0;
  bool buttonPressedState = false;

  startOTA();

  while (state.at_command_mode && millis() - configModeStartTime < 60 * 60 * 1000) {
    server->handleClient();  // For OTA
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
          if (state.useDisplayInNormalMode()) {
            state.stopDisplayInNormalMode();
            display.message("DISABLED", 1000);
          } else {
            state.startDisplayInNormalMode();
            display.message("ENABLED", 1000);
          }
        } else if (pressedTime > 1000) {
          display.debugModeProgressBar((pressedTime - 1000) * 100 / 1500.0,
                                       !state.useDisplayInNormalMode());
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

  stopOTA();
  state.setMode(ModeNormal);
  state.at_command_mode = false;
  Serial.println("EXIT");
}

void loop() {
  Serial.println("Should not have ended up in loop, going to sleep");
  gotoSleep(3600 * 1000);
}
