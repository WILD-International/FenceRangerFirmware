#include <Arduino.h>
#include <Preferences.h>
#include <driver/adc.h>
#include <driver/rtc_io.h>
#include <esp_adc_cal.h>

#include "alert.h"
#include "board.h"
#include "comms.h"
#include "config.h"
#include "config_mode.h"
#include "display.h"
#include "esp32-hal-gpio.h"
#include "isr_button.h"
#include "logging.h"
#include "measure_program.h"
#include "sleep.h"
#include "state.h"
#include "version.h"
#include "web_ui.h"

// Global objects
Config config;
State state(&config);
Comms comms(&config, &state);
Display display(&config);
WebUi webUi(&config);

// Programs
MeasureProgram measureProgram;

static void runComms(Comms& comms, State& state, Config& config, Display& display,
                     const Measurement& measurement) {
  if (comms.begin(&display) != RADIOLIB_ERR_NONE) {
    display.message("RADIO ERR", 2000);
    return;
  }

  display.message("SEND DATA");
  Downlink downlink;
  int16_t sendState = comms.sendMeasurement(measurement.battMaxMv,
                                            max(measurement.fenceNegMaxV, measurement.fencePosMaxV),
                                            &display, &downlink);
  if (sendState < 0) {
    return;
  }
  display.message("DATA SENT", 1000);

  for (int i = 0; downlink.type != DownlinkType::None && i < kMaxDownlinkChain; i++) {
    downlink = comms.processDownlink(downlink, &display);
  }
  if (downlink.type != DownlinkType::None) {
    LOG("Downlink chain limit reached, remaining downlinks lost\n");
  }
}

void setup() {
  int64_t millis_slept = getTimeSleptMs();
  int64_t now_us = rtcTimeNowUs();

  gpioWakeInit();
  pinMode(USER_BUTTON, INPUT);

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
    runConfigMode(config, state, display, webUi, comms);
    buttonResetState();
    gotoSleep(100);
  }

  digitalWrite(LED, LOW);
  buttonSetup(USER_BUTTON);

  display.message("MEASURE");
  Measurement measurement = measureProgram.run(config);
  state.updateSlowBatt(measurement.battMaxMv);
  state.tickDisplayCycles();
  display.showMeasurement(measurement, 2000);

  uint32_t adaptedSleepMs = state.computeAdaptedSleepMs();

  LOG("Firmware version: %s\n", GIT_TAG);
  LOG("Awake after sleeping for %lld ms\n", millis_slept);
  LOG("RTC time now: %lld ms\n", now_us / 1000);

  if (wasButtonPressed()) {
    gotoSleep(100);
  }

  // Try joining if not joined yet, even if there's nothing to send
  if (!comms.hasJoined()) {
    if (comms.begin(&display) != RADIOLIB_ERR_NONE) {
      display.message("RADIO ERR", 2000);
    } else {
      comms.saveSession();
    }
  }
  if (!comms.hasJoined()) {
    gotoSleep(adaptedSleepMs);
  }

  if (wasButtonPressed()) {
    gotoSleep(100);
  }

  uint16_t fenceVoltage = max(measurement.fencePosMaxV, measurement.fenceNegMaxV);
  AlertConfig alertCfg = {
      .alertThresholdV = config.getAlertThresholdV(),
      .recoverThresholdV = config.getRecoverThresholdV(),
      .burstSends = config.getAlertBurstSends(),
      .idleHeartbeatCycles = config.getIdleHeartbeatCycles(),
      .alertHeartbeatCycles = config.getAlertHeartbeatCycles(),
      .recoveryConfirmCycles = config.getRecoveryConfirmCycles(),
  };
  bool shouldSend = processMeasurement(fenceVoltage, alertCfg, rtc.alertState);
  if (shouldSend) {
    runComms(comms, state, config, display, measurement);
  }
  comms.sleep();
  if (wasButtonPressed() || state.getMode() == ModeConfig) {
    gotoSleep(100);
  } else {
    display.message("SLEEP", 500);
    gotoSleep(adaptedSleepMs);
  }
}

void loop() {
  Serial.println("Should not have ended up in loop, going to sleep");
  gotoSleep(3600 * 1000);
}
