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

  // Allow interrupting before trying to join
  if (wasButtonPressed()) {
    gotoSleep(100);
  }

  if (!comms.hasJoined()) {
    if (comms.begin(&display) != RADIOLIB_ERR_NONE) {
      display.message("RADIO ERR", 2000);
      gotoSleep(adaptedSleepMs);
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

    uint8_t pendingReport = state.getPendingConfigReport();
    if (shouldSend || pendingReport) {
      // No need to initialize the radio if there's nothing to send
      // This will restore the LoRaWAN session from RTC memory
      if (comms.begin(&display) != RADIOLIB_ERR_NONE) {
        display.message("RADIO ERR", 2000);
        gotoSleep(adaptedSleepMs);
      }

      // Clear before any uplink so QueryConfig downlinks received this cycle go to next cycle
      state.clearPendingConfigReport();

      auto handleDownlink = [&](Downlink& downlink) {
        switch (downlink.type) {
          case DownlinkType::EnableOTA:
            state.setMode(ModeConfig);
            break;
          case DownlinkType::SetAlertThresholds:
            config.setAlertThresholds(downlink.alertThresholds.alertV,
                                      downlink.alertThresholds.recoverV);
            break;
          case DownlinkType::SetAlertCycles:
            config.setAlertCycles(
                downlink.alertCycles.burstSends, downlink.alertCycles.idleHbCycles,
                downlink.alertCycles.alertHbCycles, downlink.alertCycles.recoveryCycles);
            break;
          case DownlinkType::SetSleepConfig:
            config.setSleepIntervalS(downlink.sleepConfig.sleepIntervalS);
            config.setBattAdaptiveSleep(config.getBattFullMv(), config.getBattLowMv(),
                                        downlink.sleepConfig.sleepMult,
                                        downlink.sleepConfig.recoveryN);
            break;
          case DownlinkType::SetSleepAdjRange:
            config.setBattAdaptiveSleep(downlink.sleepAdjRange.battFullMv,
                                        downlink.sleepAdjRange.battLowMv, config.getBattSleepMult(),
                                        config.getBattRecoveryN());
            break;
          case DownlinkType::QueryConfig:
            state.schedulePendingConfigReport(downlink.fport);
            break;
          default:
            break;
        }
      };

      if (shouldSend) {
        uint16_t battMv = max(state.batt_min_mv, state.batt_max_mv);
        uint16_t fenceNeg = max(state.fence_neg_min_v, state.fence_neg_max_v);
        uint16_t fencePos = max(state.fence_pos_min_v, state.fence_pos_max_v);
        uint16_t fenceV = max(fenceNeg, fencePos);
        display.message("SEND MSR");
        Downlink downlink;
        comms.sendMeasurement(battMv, fenceV, &display, &downlink);
        display.message("SENT", 1000);
        handleDownlink(downlink);
      }

      for (uint8_t fport = 11; fport <= 14; fport++) {
        if (pendingReport & (1 << (fport - 11))) {
          delay(3000);
          display.message("SEND CFG");
          Downlink downlink;
          comms.sendConfigReport(fport, &display, &downlink);
          display.message("SENT", 1000);
          handleDownlink(downlink);
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

  gotoSleep(adaptedSleepMs);
}

void loop() {
  Serial.println("Should not have ended up in loop, going to sleep");
  gotoSleep(3600 * 1000);
}
