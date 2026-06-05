#include "comms.h"

#include "TypeDef.h"
#include "logging.h"
#include "rtc_state.h"
#include "version.h"

#if defined(USE_GC1109_PA)
static const uint32_t rfswitch_pins[] = {LORA_PA_EN, LORA_PA_TX_EN, RADIOLIB_NC, RADIOLIB_NC,
                                         RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    {Module::MODE_IDLE, {LOW, LOW}},
    {Module::MODE_RX, {HIGH, LOW}},
    {Module::MODE_TX, {HIGH, HIGH}},
    END_OF_MODE_TABLE,
};
#endif

#if defined(USE_KCT8103L_PA)
static const uint32_t rfswitch_pins[] = {LORA_PA_CSD, LORA_PA_CTX, RADIOLIB_NC, RADIOLIB_NC,
                                         RADIOLIB_NC};

static Module::RfSwitchMode_t rfswitch_table[] = {
    {Module::MODE_IDLE, {LOW, LOW}},
    {Module::MODE_RX, {HIGH, LOW}},
    {Module::MODE_TX, {HIGH, HIGH}},
    END_OF_MODE_TABLE,
};
#endif

// --- Radio-config trial helpers ------------------------------------------
// Consecutive missed ACKs allowed within a phase before it reverts (a success refills
// this budget, so only a sustained run of misses fails the phase).
static constexpr uint8_t RADIO_TRIAL_ATTEMPTS = 3;

// Successful confirmed round-trips the ADR phase must collect before it passes. More
// than one guards against a fluke ACK on a marginal link (it does not exercise ADR's
// own rate loop — that needs far more frames than a trial can run).
static constexpr uint8_t ADR_PHASE_CONFIRMS = 3;

// Number of probe phases for the staged candidate. An ADR-on candidate runs two:
// first at DR0, then with ADR actually enabled (the real configured mode). DR0 is
// singled out because in testing it needed a larger scanGuard than the higher data
// rates (DR2–DR5) — a scanGuard that's fine at those can silently drop DR0 downlinks.
// Since ADR
// can push the device down to DR0 (and the climb-back commands are themselves
// downlinks we'd then miss), we confirm DR0 works under the candidate before
// committing. An ADR-off candidate runs a single probe at its fixed data rate.
static uint8_t trialPhaseCount() {
  return rtc.radioTrial.adrEnabled ? 2 : 1;
}

// True when the given phase should probe with ADR enabled rather than a forced DR.
// Only the final phase of an ADR candidate does this.
static bool trialPhaseIsAdr(uint8_t phase) {
  return rtc.radioTrial.adrEnabled && phase == 1;
}

// Successful confirmed round-trips a phase must collect before it passes. The ADR
// phase wants several; a forced-DR phase passes on the first success.
static uint8_t trialPhaseConfirms(uint8_t phase) {
  return trialPhaseIsAdr(phase) ? ADR_PHASE_CONFIRMS : 1;
}

// DR for a given trial phase. An ADR candidate's first phase forces DR0 (the rate
// found to need the most scanGuard); every other phase uses the candidate's own data
// rate (in the ADR phase that is just the ADR start rate).
static uint8_t trialDrForPhase(uint8_t phase) {
  if (rtc.radioTrial.adrEnabled && phase == 0) {
    return 0;  // DR0: the finicky low rate ADR can drop to
  }
  return rtc.radioTrial.dataRate;
}

static uint8_t responseFPort(DownlinkType t) {
  switch (t) {
    case DownlinkType::QueryAlertThresholds:
      return 11;
    case DownlinkType::QueryAlertCycles:
      return 12;
    case DownlinkType::QuerySleepConfig:
      return 13;
    case DownlinkType::QuerySleepAdjRange:
      return 14;
    case DownlinkType::QueryMeasureConfig:
      return 15;
    case DownlinkType::QueryRadioConfig:
      return 16;
    case DownlinkType::QueryDevName:
      return 21;
    case DownlinkType::QueryVersion:
      return 20;
    default:
      return 0;
  }
}

static bool isQuery(DownlinkType t) {
  return responseFPort(t) != 0;
}

// Helper function to display any issues
void radioDebug(bool isFail, const __FlashStringHelper* message, int state, bool Freeze) {
  if (isFail) {
    LOGPRINT(message);
    LOGPRINT("(");
    LOGPRINT(state);
    LOGLN(")");
    // while (Freeze);
  }
}

Comms::Comms(Config* config, State* state)
    : config(config), state(state), radio(new RadioModule()), node(&radio, &Region, subBand) {
  state->incrementBootCount();
  LOGPRINT(F("Boot number: "));
  LOGLN(state->getBootCount());
}

// "Inspired" from
// https://github.com/radiolib-org/radiolib-persistence/blob/main/examples/LoRaWAN_ESP32/LoRaWAN_ESP32.ino
bool Comms::lwActivate(Display* display) {
  rtc.bootCountForJoining++;

  int16_t radiolib_state = RADIOLIB_ERR_UNKNOWN;

  // setup the OTAA session information
  node.beginOTAA(joinEUI, config->getDevEUIAsUint64(), NULL, config->getAppKey());

  // pad the RX windows to absorb timing inaccuracy (covers join and all uplink RX)
  node.scanGuard = effectiveRadioConfig().scanGuardMs;

  LOGLN(F("Recalling LoRaWAN nonces & session"));
  // ##### setup the flash storage
  store.begin("radiolib");
  // ##### if we have previously saved nonces, restore them and try to restore session as well
  if (store.isKey("nonces")) {
    uint8_t buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];  // create somewhere to store nonces
    store.getBytes("nonces", buffer, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);  // get them from the store
    radiolib_state = node.setBufferNonces(buffer);                       // send them to LoRaWAN
    radioDebug(radiolib_state != RADIOLIB_ERR_NONE, F("Restoring nonces buffer failed"),
               radiolib_state, false);

    // recall session from RTC deep-sleep preserved variable
    radiolib_state = node.setBufferSession(rtc.lwSession);  // send them to LoRaWAN stack

    // if we have booted more than once we should have a session to restore, so report any failure
    // otherwise no point saying there's been a failure when it was bound to fail with an empty
    // rtc.lwSession var.
    radioDebug((radiolib_state != RADIOLIB_ERR_NONE) && (state->getBootCount() > 1),
               F("Restoring session buffer failed"), radiolib_state, false);

    // if Nonces and Session restored successfully, activation is just a formality
    // moreover, Nonces didn't change so no need to re-save them
    if (radiolib_state == RADIOLIB_ERR_NONE) {
      LOGLN(F("Succesfully restored session - now activating"));
      radiolib_state = node.activateOTAA();
      radioDebug((radiolib_state != RADIOLIB_LORAWAN_SESSION_RESTORED),
                 F("Failed to activate restored session"), radiolib_state, true);

      // ##### close the store before returning
      store.end();
      return radiolib_state == RADIOLIB_LORAWAN_SESSION_RESTORED;
    }

  } else {  // store has no key "nonces"
    LOGLN(F("No Nonces saved - starting fresh."));
  }

  if ((rtc.bootCountForJoining - 1) > 30) {
    // From now on, only try joining every 10th time
    if ((rtc.bootCountForJoining - 1) % 10 != 0) {
      LOG("Next join attempt in %d boots\n", 10 - ((rtc.bootCountForJoining - 1) % 10));
      store.end();
      return false;
    }
  } else {
    // From now on, only try joining every 5th time
    if ((rtc.bootCountForJoining - 1) % 5 != 0) {
      LOG("Next join attempt in %d boots\n", 5 - ((rtc.bootCountForJoining - 1) % 5));
      store.end();
      return false;
    }
  }

  // if we got here, there was no session to restore, so start trying to join
  node.setDatarate(effectiveRadioConfig().dataRate);
  radiolib_state = RADIOLIB_ERR_NETWORK_NOT_JOINED;
  while (radiolib_state != RADIOLIB_LORAWAN_NEW_SESSION) {
    LOGLN(F("Join ('login') to the LoRaWAN Network"));
    display->message("JOINING");
    radiolib_state = node.activateOTAA();

    // ##### save the join counters (nonces) to permanent store
    LOGLN(F("Saving nonces to flash"));
    uint8_t buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];           // create somewhere to store nonces
    uint8_t* persist = node.getBufferNonces();                  // get pointer to nonces
    memcpy(buffer, persist, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);  // copy in to buffer
    store.putBytes("nonces", buffer, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);  // send them to the store

    // print the nonces as hex
    LOG("Nonces saved: ");
    for (size_t i = 0; i < RADIOLIB_LORAWAN_NONCES_BUF_SIZE; i++) {
      LOG("%02X", buffer[i]);
    }
    LOG("\n");

    // we'll save the session after an uplink

    if (radiolib_state != RADIOLIB_LORAWAN_NEW_SESSION) {
      LOGPRINT(F("Join failed: "));
      display->messageWithCode("JOIN ERR", radiolib_state, 2000);
      LOGLN(radiolib_state);

      // how long to wait before join attempts. This is an interim solution pending
      // implementation of TS001 LoRaWAN Specification section #7 - this doc applies to v1.0.4 &
      // v1.1 it sleeps for longer & longer durations to give time for any gateway issues to resolve
      // or whatever is interfering with the device <-> gateway airwaves.
      // uint32_t sleepForSeconds = min((bootCountSinceUnsuccessfulJoin++ + 1UL) * 60UL, 3UL *
      // 60UL);
      // LOGPRINT(F("Boots since unsuccessful join: "));
      // LOGLN(bootCountSinceUnsuccessfulJoin);
      // LOGPRINT(F("Retrying join in "));
      // LOGPRINT(sleepForSeconds);
      // LOGLN(F(" seconds"));

      store.end();
      return false;  // gotoSleep(sleepForSeconds);

    }  // if activateOTAA state
  }  // while join

  display->message("JOINED", 1000);
  LOGLN(F("Joined"));

  // reset the failed join count
  // bootCountSinceUnsuccessfulJoin = 0;

  delay(1000);  // hold off off hitting the airwaves again too soon - an issue in the US

  // ##### close the store
  store.end();
  return true;  //(radiolib_state);
}

int16_t Comms::begin(Display* display) {
  if (radioInitialized) {
    return RADIOLIB_ERR_NONE;
  }
  EffectiveRadioConfig er = effectiveRadioConfig();
  LOGLN("[SX1262] Initializing ... ");
  // Heltec library uses 1.8V for TCXO, instead of RadioLib default of 1.6V
  // TODO! I still put the power back to 10dBm because it's not clear how
  // TODO! this combines with the amplifiers
  int radiolib_state = radio.begin(
      868.1, 125.0, 9, 7,
      er.syncwordPublic ? RADIOLIB_SX126X_SYNC_WORD_PUBLIC : RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
      er.txPower, 8, 1.8, false);
  if (radiolib_state == RADIOLIB_ERR_NONE) {
    radioInitialized = true;
    LOGLN(F("radiolib begin success!"));
  } else {
    LOG("Failed to initialize radio, code: %d\n", radiolib_state);
    return radiolib_state;
  }
#if defined(USE_KCT8103L_PA)
  rfswitch_table[1].values[1] = er.rxLnaEnabled ? LOW : HIGH;
#endif
  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);

  pinMode(LORA_PA_POWER, OUTPUT);
  digitalWrite(LORA_PA_POWER, LOW);
  if (config->hasAppKey()) {
    digitalWrite(LORA_PA_POWER, HIGH);
    delay(1);
    rtc.joined = lwActivate(display);
    if (rtc.joined) {
      node.setADR(er.adrEnabled);
      // Normally we only pin the DR when ADR is off; during a trial we also pin it in the
      // ADR phase to give that probe a known start rate before ADR takes over.
      if (!er.adrEnabled || rtc.radioTrial.active) {
        node.setDatarate(er.dataRate);
      }
    }
    digitalWrite(LORA_PA_POWER, LOW);
  }
  return radiolib_state;
}

Comms::EffectiveRadioConfig Comms::effectiveRadioConfig() {
  if (rtc.radioTrial.active) {
    // During a trial probe, use the candidate settings. Forced-DR phases pin the DR with
    // ADR off so we control which rate the downlink is tested at; the final ADR phase
    // enables ADR (testDr is then the ADR start DR) to prove the real configured mode.
    bool adrPhase = trialPhaseIsAdr(rtc.radioTrial.phase);
    return {
        rtc.radioTrial.rxLnaEnabled != 0,
        rtc.radioTrial.txPower,
        rtc.radioTrial.syncwordPublic != 0,
        rtc.radioTrial.scanGuardMs,
        adrPhase,
        rtc.radioTrial.testDr,
    };
  }
  return {
      config->getRxLnaEnabled(), config->getTxPower(),    config->getSyncwordPublic(),
      config->getScanGuardMs(),  config->getAdrEnabled(), config->getDataRate(),
  };
}

void Comms::stageRadioTrial(const Downlink& downlink) {
  rtc.radioTrial.rxLnaEnabled = downlink.radioConfig.rxLnaEnabled;
  rtc.radioTrial.txPower = downlink.radioConfig.txPower;
  rtc.radioTrial.dataRate = downlink.radioConfig.dataRate;
  rtc.radioTrial.adrEnabled = downlink.radioConfig.adrEnabled;
  rtc.radioTrial.syncwordPublic = downlink.radioConfig.syncwordPublic;
  rtc.radioTrial.scanGuardMs = downlink.radioConfig.scanGuardMs;
  rtc.radioTrial.phase = 0;
  rtc.radioTrial.testDr = trialDrForPhase(0);
  rtc.radioTrial.confirmsLeft = trialPhaseConfirms(0);
  rtc.radioTrial.attemptsLeft = RADIO_TRIAL_ATTEMPTS;
  rtc.radioTrial.active = true;
  LOG("Radio trial staged: %s, phases=%d, first probe DR%d\n",
      rtc.radioTrial.adrEnabled ? "ADR on (test DR0 then ADR)" : "ADR off", trialPhaseCount(),
      rtc.radioTrial.testDr);
}

void Comms::commitRadioTrial() {
  config->setRxLnaEnabled(rtc.radioTrial.rxLnaEnabled != 0);
  config->setTxPower(rtc.radioTrial.txPower);
  config->setDataRate(rtc.radioTrial.dataRate);
  config->setAdrEnabled(rtc.radioTrial.adrEnabled != 0);
  config->setSyncwordPublic(rtc.radioTrial.syncwordPublic != 0);
  config->setScanGuardMs(rtc.radioTrial.scanGuardMs);
  rtc.radioTrial.active = false;
  rtc.trialResultPending = true;
  LOGLN("Radio trial committed to NVS");
}

void Comms::revertRadioTrial() {
  rtc.radioTrial.active = false;
  rtc.trialResultPending = true;
  LOGLN("Radio trial reverted (candidate discarded; NVS config unchanged)");
}

void Comms::runRadioTrialCycle(uint16_t battMv, uint16_t fenceV, Display* display) {
  if (begin(display) != RADIOLIB_ERR_NONE || !hasJoined()) {
    // The radio failing to init / not being joined while a config trial is pending
    // should never happen — the trial only exists because a downlink got through.
    // If it does, something is badly wrong: revert to the known-good config rather
    // than risk lingering on the unproven candidate. The operator can re-send.
    LOGLN("Radio trial: radio/join not ready — reverting to known-good config");
    revertRadioTrial();
    return;
  }

  if (trialPhaseIsAdr(rtc.radioTrial.phase)) {
    LOG("Radio trial probe: ADR enabled (start DR%d), %d confirm(s) left, %d attempts\n",
        rtc.radioTrial.testDr, rtc.radioTrial.confirmsLeft, rtc.radioTrial.attemptsLeft);
  } else {
    LOG("Radio trial probe: forced DR%d, %d attempts left\n", rtc.radioTrial.testDr,
        rtc.radioTrial.attemptsLeft);
  }
  display->message("RADIO TEST");

  Downlink downlink;
  int16_t result = sendMeasurement(battMv, fenceV, display, &downlink, /*confirmed=*/true);
  bool ackReceived = (result > 0);

  if (ackReceived) {
    // A success refills the miss budget and counts toward this phase's required confirms.
    rtc.radioTrial.attemptsLeft = RADIO_TRIAL_ATTEMPTS;
    if (rtc.radioTrial.confirmsLeft > 0) {
      rtc.radioTrial.confirmsLeft--;
    }
    if (rtc.radioTrial.confirmsLeft > 0) {
      LOG("Radio trial: ACK ok, %d more confirm(s) needed this phase\n",
          rtc.radioTrial.confirmsLeft);
    } else {
      rtc.radioTrial.phase++;
      if (rtc.radioTrial.phase >= trialPhaseCount()) {
        commitRadioTrial();
      } else {
        rtc.radioTrial.testDr = trialDrForPhase(rtc.radioTrial.phase);
        rtc.radioTrial.confirmsLeft = trialPhaseConfirms(rtc.radioTrial.phase);
        rtc.radioTrial.attemptsLeft = RADIO_TRIAL_ATTEMPTS;
        if (trialPhaseIsAdr(rtc.radioTrial.phase)) {
          LOG("Radio trial: DR0 passed, next probe with ADR enabled (%d confirms)\n",
              rtc.radioTrial.confirmsLeft);
        } else {
          LOG("Radio trial: phase passed, next probe DR%d\n", rtc.radioTrial.testDr);
        }
      }
    }
  } else {
    if (rtc.radioTrial.attemptsLeft > 0) {
      rtc.radioTrial.attemptsLeft--;
    }
    if (rtc.radioTrial.attemptsLeft == 0) {
      revertRadioTrial();
    } else {
      LOG("Radio trial: no ACK, %d attempts left for this DR\n", rtc.radioTrial.attemptsLeft);
    }
  }

  // Handle anything the server piggybacked on the ACK frame. This runs *after* the ACK
  // evaluation above so that a new SetRadioConfig restages the trial (latest wins),
  // cleanly superseding this probe's result, and other commands (EnableOTA, alert/sleep
  // config, queries) still take effect during a trial. A no-ACK probe carries no
  // downlink, so this loop is a no-op there.
  for (int i = 0; downlink.type != DownlinkType::None && i < kMaxDownlinkChain; i++) {
    downlink = processDownlink(downlink, display);
  }
}

int16_t Comms::sendMeasurement(uint16_t battMv, uint16_t fenceV, Display* display,
                               Downlink* downlink, bool confirmed, SignalInfo* signalInfo) {
  battMv = constrain(battMv, (uint16_t)1950, (uint16_t)4500);
  uint8_t payload[3];
  payload[0] = (battMv - 1950) / 10;
  payload[1] = (fenceV >> 8) & 0xFF;
  payload[2] = fenceV & 0xFF;
  return send(1, payload, sizeof(payload), display, downlink, confirmed, signalInfo);
}

int16_t Comms::send(uint8_t fport, uint8_t* data, size_t len, Display* display, Downlink* downlink,
                    bool confirmed, SignalInfo* signalInfo) {
  digitalWrite(LORA_PA_POWER, HIGH);
  delay(1);

  // Sized to hold the largest downlink we parse — a JoinWifiSta payload (fPort 9) carrying an
  // SSID plus a WPA2 password can exceed 32 bytes.
  uint8_t downlinkPayload[128];
  size_t downlinkSize;

  LoRaWANEvent_t uplinkDetails;
  LoRaWANEvent_t downlinkDetails;

  uint32_t fCntUp = node.getFCntUp();

  int16_t radiolib_state = node.sendReceive(data, len, fport, downlinkPayload, &downlinkSize,
                                            confirmed, &uplinkDetails, &downlinkDetails);
  digitalWrite(LORA_PA_POWER, LOW);

  if (radiolib_state > 0) {
    if (signalInfo != nullptr) {
      signalInfo->rssi = radio.getRSSI();
      signalInfo->snr = radio.getSNR();
      signalInfo->freq = downlinkDetails.freq;
      signalInfo->datarate = downlinkDetails.datarate;
      signalInfo->rxWindow = radiolib_state;
    }
    LOGLN("Received a message from server");
    if (downlink != nullptr) {
      switch (downlinkDetails.fPort) {
        case 9: {
          LOGLN("Downlink: JoinWifiSta");
          downlink->type = DownlinkType::JoinWifiSta;
          // Payload: [timeout][ssidLen L][ssid (L bytes)][password]. SSID optional (L=0 ->
          // fixed STA_GATEWAY_SSID); password required (>=8) or the join is refused on consume.
          downlink->joinWifiSta.timeoutMin = (downlinkSize >= 1) ? downlinkPayload[0] : 0;
          size_t ssidLen = (downlinkSize >= 2) ? downlinkPayload[1] : 0;
          size_t avail = (downlinkSize >= 2) ? downlinkSize - 2 : 0;
          if (ssidLen > avail) {
            ssidLen = avail;
          }
          if (ssidLen >= WIFI_SSID_MAX) {
            ssidLen = WIFI_SSID_MAX - 1;
          }
          memcpy(downlink->joinWifiSta.ssid, downlinkPayload + 2, ssidLen);
          downlink->joinWifiSta.ssid[ssidLen] = '\0';
          size_t passOff = 2 + ssidLen;
          size_t passLen = (downlinkSize > passOff) ? downlinkSize - passOff : 0;
          if (passLen >= WIFI_STA_PASS_MAX) {
            passLen = WIFI_STA_PASS_MAX - 1;
          }
          memcpy(downlink->joinWifiSta.password, downlinkPayload + passOff, passLen);
          downlink->joinWifiSta.password[passLen] = '\0';
          break;
        }
        case 10: {
          LOGLN("Downlink: EnableOTA");
          downlink->type = DownlinkType::EnableOTA;
          // Optional payload: byte 0 = AP active minutes (0 = default timeout),
          // bytes 1.. = per-activation AP password (empty/short -> fixed AP_PASSWORD).
          downlink->enableOta.timeoutMin = (downlinkSize >= 1) ? downlinkPayload[0] : 0;
          size_t passLen = (downlinkSize >= 2) ? downlinkSize - 1 : 0;
          if (passLen >= WIFI_AP_PASS_MAX) {
            passLen = WIFI_AP_PASS_MAX - 1;
          }
          memcpy(downlink->enableOta.password, downlinkPayload + 1, passLen);
          downlink->enableOta.password[passLen] = '\0';
          break;
        }
        case 11:
          if (downlinkSize == 0) {
            LOGLN("Downlink: QueryAlertThresholds");
            downlink->type = DownlinkType::QueryAlertThresholds;
          } else if (downlinkSize == 4) {
            LOGLN("Downlink: SetAlertThresholds");
            downlink->type = DownlinkType::SetAlertThresholds;
            downlink->alertThresholds.alertV = (downlinkPayload[0] << 8) | downlinkPayload[1];
            downlink->alertThresholds.recoverV = (downlinkPayload[2] << 8) | downlinkPayload[3];
          } else {
            LOG("Downlink: fPort 11 ignored (unexpected size %d)\n", downlinkSize);
          }
          break;
        case 12:
          if (downlinkSize == 0) {
            LOGLN("Downlink: QueryAlertCycles");
            downlink->type = DownlinkType::QueryAlertCycles;
          } else if (downlinkSize == 4) {
            LOGLN("Downlink: SetAlertCycles");
            downlink->type = DownlinkType::SetAlertCycles;
            downlink->alertCycles.burstSends = downlinkPayload[0];
            downlink->alertCycles.idleHbCycles = downlinkPayload[1];
            downlink->alertCycles.alertHbCycles = downlinkPayload[2];
            downlink->alertCycles.recoveryCycles = downlinkPayload[3];
          } else {
            LOG("Downlink: fPort 12 ignored (unexpected size %d)\n", downlinkSize);
          }
          break;
        case 13:
          if (downlinkSize == 0) {
            LOGLN("Downlink: QuerySleepConfig");
            downlink->type = DownlinkType::QuerySleepConfig;
          } else if (downlinkSize == 6) {
            LOGLN("Downlink: SetSleepConfig");
            downlink->type = DownlinkType::SetSleepConfig;
            downlink->sleepConfig.sleepIntervalS = (downlinkPayload[0] << 8) | downlinkPayload[1];
            downlink->sleepConfig.sleepMult = downlinkPayload[2];
            downlink->sleepConfig.recoveryN = (downlinkPayload[3] << 8) | downlinkPayload[4];
            downlink->sleepConfig.paSleepMode = downlinkPayload[5];
          } else {
            LOG("Downlink: fPort 13 ignored (unexpected size %d)\n", downlinkSize);
          }
          break;
        case 14:
          if (downlinkSize == 0) {
            LOGLN("Downlink: QuerySleepAdjRange");
            downlink->type = DownlinkType::QuerySleepAdjRange;
          } else if (downlinkSize == 2) {
            LOGLN("Downlink: SetSleepAdjRange");
            downlink->type = DownlinkType::SetSleepAdjRange;
            downlink->sleepAdjRange.battFullMv = downlinkPayload[0] * 10 + 1950;
            downlink->sleepAdjRange.battLowMv = downlinkPayload[1] * 10 + 1950;
          } else {
            LOG("Downlink: fPort 14 ignored (unexpected size %d)\n", downlinkSize);
          }
          break;
        case 15:
          if (downlinkSize == 0) {
            LOGLN("Downlink: QueryMeasureConfig");
            downlink->type = DownlinkType::QueryMeasureConfig;
          } else if (downlinkSize == 4) {
            LOGLN("Downlink: SetMeasureConfig");
            downlink->type = DownlinkType::SetMeasureConfig;
            downlink->measureConfig.measureDurationMs =
                (downlinkPayload[0] << 8) | downlinkPayload[1];
            downlink->measureConfig.fenceCalPercent =
                (downlinkPayload[2] << 8) | downlinkPayload[3];
          } else {
            LOG("Downlink: fPort 15 ignored (unexpected size %d)\n", downlinkSize);
          }
          break;
        case 16:
          if (downlinkSize == 0) {
            LOGLN("Downlink: QueryRadioConfig");
            downlink->type = DownlinkType::QueryRadioConfig;
          } else if (downlinkSize == 7) {
            LOGLN("Downlink: SetRadioConfig");
            downlink->type = DownlinkType::SetRadioConfig;
            downlink->radioConfig.rxLnaEnabled = downlinkPayload[0];
            downlink->radioConfig.txPower = downlinkPayload[1];
            downlink->radioConfig.dataRate = downlinkPayload[2];
            downlink->radioConfig.adrEnabled = downlinkPayload[3];
            downlink->radioConfig.syncwordPublic = downlinkPayload[4];
            downlink->radioConfig.scanGuardMs = (downlinkPayload[5] << 8) | downlinkPayload[6];
          } else {
            LOG("Downlink: fPort 16 ignored (unexpected size %d)\n", downlinkSize);
          }
          break;
        case 20:
          if (downlinkSize == 0) {
            LOGLN("Downlink: QueryVersion");
            downlink->type = DownlinkType::QueryVersion;
          } else {
            LOG("Downlink: fPort 20 ignored (unexpected size %d)\n", downlinkSize);
          }
          break;
        case 21:
          if (downlinkSize == 0) {
            LOGLN("Downlink: QueryDevName");
            downlink->type = DownlinkType::QueryDevName;
          } else if (downlinkSize < DEVNAME_MAX) {
            LOGLN("Downlink: SetDevName");
            memcpy(downlink->devNameData.name, downlinkPayload, downlinkSize);
            downlink->devNameData.name[downlinkSize] = '\0';
            downlink->type = DownlinkType::SetDevName;
          } else {
            LOG("Downlink: fPort 21 ignored (unexpected size %d)\n", downlinkSize);
          }
          break;
        default:
          LOG("Downlink: unknown fPort %d\n", downlinkDetails.fPort);
          break;
      }
    }
  } else if (radiolib_state != RADIOLIB_ERR_NONE) {
    LOGLN(radiolib_state);
    display->messageWithCode("SEND ERR", radiolib_state, 2000);
  }
  saveSession();
  return radiolib_state;
}

int16_t Comms::sendConfigReport(uint8_t fport, Display* display, Downlink* downlink) {
  if (fport == 20) {
    const char* tag = GIT_TAG;
    size_t len = strlen(tag);
    if (len > 51) {
      len = 51;
    }
    uint8_t buf[52];
    memcpy(buf, tag, len);
    return send(fport, buf, len, display, downlink);
  }
  if (fport == 21) {
    const char* name = config->getDevName();
    size_t len = strlen(name);
    if (len > 51) {
      len = 51;
    }
    uint8_t buf[52];
    memcpy(buf, name, len);
    return send(fport, buf, len, display, downlink);
  }

  uint8_t payload[7];
  size_t len = 0;
  switch (fport) {
    case 11:
      payload[0] = (config->getAlertThresholdV() >> 8) & 0xFF;
      payload[1] = config->getAlertThresholdV() & 0xFF;
      payload[2] = (config->getRecoverThresholdV() >> 8) & 0xFF;
      payload[3] = config->getRecoverThresholdV() & 0xFF;
      len = 4;
      break;
    case 12:
      payload[0] = config->getAlertBurstSends();
      payload[1] = config->getIdleHeartbeatCycles();
      payload[2] = config->getAlertHeartbeatCycles();
      payload[3] = config->getRecoveryConfirmCycles();
      len = 4;
      break;
    case 13:
      payload[0] = (config->getSleepIntervalS() >> 8) & 0xFF;
      payload[1] = config->getSleepIntervalS() & 0xFF;
      payload[2] = config->getBattSleepMult();
      payload[3] = (config->getBattRecoveryN() >> 8) & 0xFF;
      payload[4] = config->getBattRecoveryN() & 0xFF;
      payload[5] = config->getPaSleepMode();
      len = 6;
      break;
    case 14:
      payload[0] = (uint8_t)((config->getBattFullMv() - 1950) / 10);
      payload[1] = (uint8_t)((config->getBattLowMv() - 1950) / 10);
      len = 2;
      break;
    case 15:
      payload[0] = (config->getMeasureDurationMs() >> 8) & 0xFF;
      payload[1] = config->getMeasureDurationMs() & 0xFF;
      payload[2] = (config->getFenceCalPercent() >> 8) & 0xFF;
      payload[3] = config->getFenceCalPercent() & 0xFF;
      len = 4;
      break;
    case 16:
      payload[0] = config->getRxLnaEnabled() ? 1 : 0;
      payload[1] = (uint8_t)config->getTxPower();
      payload[2] = config->getDataRate();
      payload[3] = config->getAdrEnabled() ? 1 : 0;
      payload[4] = config->getSyncwordPublic() ? 1 : 0;
      payload[5] = (config->getScanGuardMs() >> 8) & 0xFF;
      payload[6] = config->getScanGuardMs() & 0xFF;
      len = 7;
      break;
    default:
      return RADIOLIB_ERR_NONE;
  }
  return send(fport, payload, len, display, downlink);
}

Downlink Comms::processDownlink(Downlink downlink, Display* display) {
  switch (downlink.type) {
    case DownlinkType::EnableOTA:
      state->setMode(ModeConfig);
      rtc.otaConfigRequested = true;
      rtc.otaApTimeoutMin = downlink.enableOta.timeoutMin;
      strlcpy(rtc.otaApPassword, downlink.enableOta.password, sizeof(rtc.otaApPassword));
      return {};
    case DownlinkType::JoinWifiSta:
      state->setMode(ModeConfig);
      rtc.staConnectRequested = true;
      rtc.staTimeoutMin = downlink.joinWifiSta.timeoutMin;
      strlcpy(rtc.staSsid, downlink.joinWifiSta.ssid, sizeof(rtc.staSsid));
      strlcpy(rtc.staPassword, downlink.joinWifiSta.password, sizeof(rtc.staPassword));
      return {};
    case DownlinkType::SetAlertThresholds:
      config->setAlertThresholds(downlink.alertThresholds.alertV,
                                 downlink.alertThresholds.recoverV);
      return {};
    case DownlinkType::SetAlertCycles:
      config->setAlertCycles(downlink.alertCycles.burstSends, downlink.alertCycles.idleHbCycles,
                             downlink.alertCycles.alertHbCycles,
                             downlink.alertCycles.recoveryCycles);
      return {};
    case DownlinkType::SetSleepConfig:
      config->setSleepIntervalS(downlink.sleepConfig.sleepIntervalS);
      config->setBattAdaptiveSleep(config->getBattFullMv(), config->getBattLowMv(),
                                   downlink.sleepConfig.sleepMult, downlink.sleepConfig.recoveryN);
      config->setPaSleepMode(downlink.sleepConfig.paSleepMode);
      return {};
    case DownlinkType::SetSleepAdjRange:
      config->setBattAdaptiveSleep(downlink.sleepAdjRange.battFullMv,
                                   downlink.sleepAdjRange.battLowMv, config->getBattSleepMult(),
                                   config->getBattRecoveryN());
      return {};
    case DownlinkType::SetMeasureConfig:
      config->setMeasureDurationMs(downlink.measureConfig.measureDurationMs);
      config->setFenceCalPercent(downlink.measureConfig.fenceCalPercent);
      return {};
    case DownlinkType::SetRadioConfig:
      // Don't persist remote radio changes directly — stage a trial that auto-reverts
      // unless a confirmed-uplink ACK proves the link still works (see runRadioTrialCycle).
      stageRadioTrial(downlink);
      return {};
    case DownlinkType::SetDevName:
      config->setDevName(downlink.devNameData.name);
      return {};
    default:
      break;
  }
  if (isQuery(downlink.type)) {
    delay(3000);
    display->message("SEND CFG");
    uint8_t fport = responseFPort(downlink.type);
    Downlink next{};
    sendConfigReport(fport, display, &next);
    display->message("SENT", 1000);
    return next;
  }
  return {};
}

void Comms::saveSession() {
  if (!radioInitialized) {
    return;
  }
  // now save session to RTC memory
  uint8_t* persist = node.getBufferSession();
  memcpy(rtc.lwSession, persist, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
}

bool Comms::hasJoined() {
  return rtc.joined;
}

void Comms::sleep() {
  if (!radioInitialized) {
    return;
  }
  LOGLN("Put radio to sleep");
  radio.sleep(false);  // don't retain config
  radioInitialized = false;
}

void Comms::resetJoinSchedule() {
  rtc.bootCountForJoining = 0;
}
