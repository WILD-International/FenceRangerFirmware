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
#include "debug.h"
#include "display.h"
#include "esp32-hal-gpio.h"
#include "isr_button.h"
#include "measure_program.h"
#include "sleep.h"
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

static void runComms(Comms& comms, State& state, Config& config, Display& display) {
  if (comms.begin(&display) != RADIOLIB_ERR_NONE) {
    display.message("RADIO ERR", 2000);
    return;
  }

  uint16_t battMv = max(state.batt_min_mv, state.batt_max_mv);
  uint16_t fenceNeg = max(state.fence_neg_min_v, state.fence_neg_max_v);
  uint16_t fencePos = max(state.fence_pos_min_v, state.fence_pos_max_v);
  uint16_t fenceV = max(fenceNeg, fencePos);
  display.message("SEND MSR");
  Downlink downlink;
  comms.sendMeasurement(battMv, fenceV, &display, &downlink);
  display.message("SENT", 1000);

  while (downlink.type == DownlinkType::QueryConfig) {
    delay(3000);
    display.message("SEND CFG");
    uint8_t fport = downlink.fport;
    downlink = {};
    comms.sendConfigReport(fport, &display, &downlink);
    display.message("SENT", 1000);
  }

  switch (downlink.type) {
    case DownlinkType::EnableOTA:
      state.setMode(ModeConfig);
      break;
    case DownlinkType::SetAlertThresholds:
      config.setAlertThresholds(downlink.alertThresholds.alertV, downlink.alertThresholds.recoverV);
      break;
    case DownlinkType::SetAlertCycles:
      config.setAlertCycles(downlink.alertCycles.burstSends, downlink.alertCycles.idleHbCycles,
                            downlink.alertCycles.alertHbCycles,
                            downlink.alertCycles.recoveryCycles);
      break;
    case DownlinkType::SetSleepConfig:
      config.setSleepIntervalS(downlink.sleepConfig.sleepIntervalS);
      config.setBattAdaptiveSleep(config.getBattFullMv(), config.getBattLowMv(),
                                  downlink.sleepConfig.sleepMult, downlink.sleepConfig.recoveryN);
      break;
    case DownlinkType::SetSleepAdjRange:
      config.setBattAdaptiveSleep(downlink.sleepAdjRange.battFullMv,
                                  downlink.sleepAdjRange.battLowMv, config.getBattSleepMult(),
                                  config.getBattRecoveryN());
      break;
    default:
      break;
  }
}

void setup() {
  int64_t millis_slept = getTimeSleptMs();
  int64_t now_us = rtcTimeNowUs();

  gpioWakeInit();
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
    runConfigMode(config, state, display, webUi);
    comms.resetBootCountForJoining();
  }

  digitalWrite(LED, LOW);
  buttonSetup(Boot_Key);

  display.message("MEASURE");
  measureProgram.run(config, state);
  state.updateSlowBatt();
  display.showMeasurement(state, 2000);

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

  uint16_t fenceVoltage = max(state.fence_pos_max_v, state.fence_neg_max_v);
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
    runComms(comms, state, config, display);
    comms.saveSession();
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
