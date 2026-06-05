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

// Low-power "acquire" for a JoinWifiSta downlink: each wake, scan once for the updater AP (display
// off, no server). If it's in range, hand off to the config-mode station path (connect + serve);
// otherwise deep-sleep STA_RETRY_MS and let the next wake retry, until the deadline. Never returns
// unless the deadline has passed (then it clears the request and falls through to monitoring).
static void runStaAcquire() {
  const uint32_t STA_RETRY_MS = 20000;
  const uint32_t DEFAULT_DEADLINE_MIN = 60;

  if (rtc.staDeadlineUs == 0) {
    uint32_t mins = rtc.staTimeoutMin ? rtc.staTimeoutMin : DEFAULT_DEADLINE_MIN;
    rtc.staDeadlineUs = rtcTimeNowUs() + (int64_t)mins * 60 * 1000000;
  }
  if (rtcTimeNowUs() > rtc.staDeadlineUs) {
    LOGLN("STA acquire: deadline reached, giving up");
    rtc.staConnectRequested = false;
    rtc.staDeadlineUs = 0;
    return;  // resume normal monitoring
  }

  // Brief blink so it's visibly alive and in STA acquire each retry, without powering the display.
  digitalWrite(LED, HIGH);
  delay(30);
  digitalWrite(LED, LOW);

  const char* ssid = strlen(rtc.staSsid) ? rtc.staSsid : WebUi::STA_GATEWAY_SSID;
  bool inRange = webUi.isStationInRange(ssid);
  LOG("STA acquire: scan for %s -> %s\n", ssid, inRange ? "in range" : "absent");

  if (inRange) {
    // In range — hand off to the (awake) config-mode station serve path. It still sees
    // staConnectRequested set and connects + serves; the deadline no longer applies.
    rtc.staDeadlineUs = 0;
    state.setMode(ModeConfig);
    if (state.useDisplay()) {
      display.turnOn();
    }
    bool longSleep = runConfigMode(config, state, display, webUi, comms);
    buttonResetState();
    configurePaSleepPin(config.getPaSleepMode());
    gotoSleep(longSleep ? config.getSleepIntervalMs() : 100);
  }

  // Not in range yet — sleep and retry. gotoSleep arms EXT0, so a button press still interrupts.
  gotoSleep(STA_RETRY_MS);
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
  configurePaSleepPin(config.getPaSleepMode());

  if (!config.hasAppKey()) {
    state.setMode(ModeConfig);
  }

  Serial.begin(115200);

  // A JoinWifiSta downlink: keep WiFi off and the screen dark while we scan-and-sleep for the
  // updater AP. Skipped when the operator forced config mode (button / missing AppKey).
  if (rtc.staConnectRequested && state.getMode() != ModeConfig) {
    runStaAcquire();  // no return unless the deadline passed (then falls through to monitoring)
  }

  if (state.useDisplay()) {
    display.turnOn();
  }

  if (state.getMode() == ModeConfig) {
    // The operator is physically present and asserting control — abandon any radio trial in
    // flight, and cancel a pending STA acquire, rather than letting them run under the operator.
    rtc.radioTrial.active = false;
    rtc.trialResultPending = false;
    rtc.staConnectRequested = false;
    rtc.staDeadlineUs = 0;
    digitalWrite(LED, LOW);
    bool longSleep = runConfigMode(config, state, display, webUi, comms);
    buttonResetState();
    configurePaSleepPin(config.getPaSleepMode());  // pick up a change made in the submenu
    gotoSleep(longSleep ? config.getSleepIntervalMs() : 100);
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

  // A radio-config trial is in progress: this wake is a confirmed-uplink probe at the
  // phase's forced DR, not a normal send. runRadioTrialCycle commits or reverts the
  // candidate based on whether an ACK arrives; either way we just sleep afterwards.
  if (rtc.radioTrial.active) {
    comms.runRadioTrialCycle(measurement.battMaxMv,
                             max(measurement.fenceNegMaxV, measurement.fencePosMaxV), &display);
    comms.sleep();
    gotoSleep(adaptedSleepMs);
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
  // After a radio trial concludes, report the now-active radio config once (fPort 16)
  // so the server can tell commit (new settings) from revert (old settings).
  if (rtc.trialResultPending) {
    if (comms.begin(&display) == RADIOLIB_ERR_NONE && comms.hasJoined()) {
      Downlink next;
      comms.sendConfigReport(16, &display, &next);
      rtc.trialResultPending = false;
    }
  }
  comms.sleep();
  if (wasButtonPressed() || state.getMode() == ModeConfig || rtc.staConnectRequested) {
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
