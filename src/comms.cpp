#include "comms.h"

#include "TypeDef.h"
#include "logging.h"
#include "rtc_state.h"

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
      display->message("JOIN ERR", 1000);
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
  LOGLN("[SX1262] Initializing ... ");
  // Heltec library uses 1.8V for TCXO, instead of RadioLib default of 1.6V
  // TODO! I still put the power back to 10dBm because it's not clear how
  // TODO! this combines with the amplifiers
  int radiolib_state = radio.begin(868.1, 125.0, config->getSpreadingFactor(), 7,
                                   config->getSyncwordPublic() ? RADIOLIB_SX126X_SYNC_WORD_PUBLIC
                                                               : RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                                   config->getTxPower(), 8, 1.8, false);
  if (radiolib_state == RADIOLIB_ERR_NONE) {
    radioInitialized = true;
    LOGLN(F("radiolib begin success!"));
  } else {
    LOG("Failed to initialize radio, code: %d\n", radiolib_state);
    return radiolib_state;
  }
#if defined(USE_KCT8103L_PA)
  rfswitch_table[1].values[1] = config->getRxLnaEnabled() ? LOW : HIGH;
#endif
  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);

  pinMode(LORA_PA_POWER, OUTPUT);
  digitalWrite(LORA_PA_POWER, LOW);
  if (config->hasAppKey()) {
    digitalWrite(LORA_PA_POWER, HIGH);
    delay(1);
    rtc.joined = lwActivate(display);
    if (rtc.joined) {
      node.setADR(config->getAdrEnabled());
    }
    digitalWrite(LORA_PA_POWER, LOW);
  }
  return radiolib_state;
}

int16_t Comms::sendMeasurement(uint16_t battMv, uint16_t fenceV, Display* display,
                               Downlink* downlink) {
  battMv = constrain(battMv, (uint16_t)1950, (uint16_t)4500);
  uint8_t payload[3];
  payload[0] = (battMv - 1950) / 10;
  payload[1] = (fenceV >> 8) & 0xFF;
  payload[2] = fenceV & 0xFF;
  return send(1, payload, sizeof(payload), display, downlink);
}

int16_t Comms::send(uint8_t fport, uint8_t* data, size_t len, Display* display,
                    Downlink* downlink) {
  digitalWrite(LORA_PA_POWER, HIGH);
  delay(1);

  uint8_t downlinkPayload[32];
  size_t downlinkSize;

  LoRaWANEvent_t uplinkDetails;
  LoRaWANEvent_t downlinkDetails;

  uint32_t fCntUp = node.getFCntUp();

  int16_t radiolib_state = node.sendReceive(data, len, fport, downlinkPayload, &downlinkSize, false,
                                            &uplinkDetails, &downlinkDetails);
  digitalWrite(LORA_PA_POWER, LOW);

  if (radiolib_state > 0) {
    LOGLN("Received a message from server");
    if (downlink != nullptr) {
      switch (downlinkDetails.fPort) {
        case 10:
          LOGLN("Downlink: EnableOTA");
          downlink->type = DownlinkType::EnableOTA;
          break;
        case 11:
          if (downlinkSize == 0) {
            LOGLN("Downlink: QueryConfig fPort 11");
            downlink->type = DownlinkType::QueryConfig;
            downlink->fport = 11;
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
            LOGLN("Downlink: QueryConfig fPort 12");
            downlink->type = DownlinkType::QueryConfig;
            downlink->fport = 12;
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
            LOGLN("Downlink: QueryConfig fPort 13");
            downlink->type = DownlinkType::QueryConfig;
            downlink->fport = 13;
          } else if (downlinkSize == 5) {
            LOGLN("Downlink: SetSleepConfig");
            downlink->type = DownlinkType::SetSleepConfig;
            downlink->sleepConfig.sleepIntervalS = (downlinkPayload[0] << 8) | downlinkPayload[1];
            downlink->sleepConfig.sleepMult = downlinkPayload[2];
            downlink->sleepConfig.recoveryN = (downlinkPayload[3] << 8) | downlinkPayload[4];
          } else {
            LOG("Downlink: fPort 13 ignored (unexpected size %d)\n", downlinkSize);
          }
          break;
        case 14:
          if (downlinkSize == 0) {
            LOGLN("Downlink: QueryConfig fPort 14");
            downlink->type = DownlinkType::QueryConfig;
            downlink->fport = 14;
          } else if (downlinkSize == 2) {
            LOGLN("Downlink: SetSleepAdjRange");
            downlink->type = DownlinkType::SetSleepAdjRange;
            downlink->sleepAdjRange.battFullMv = downlinkPayload[0] * 10 + 1950;
            downlink->sleepAdjRange.battLowMv = downlinkPayload[1] * 10 + 1950;
          } else {
            LOG("Downlink: fPort 14 ignored (unexpected size %d)\n", downlinkSize);
          }
          break;
        default:
          LOG("Downlink: unknown fPort %d\n", downlinkDetails.fPort);
          break;
      }
    }
  } else if (radiolib_state != RADIOLIB_ERR_NONE) {
    LOGLN(radiolib_state);
    display->message("SEND ERR");
  }
  return radiolib_state;
}

int16_t Comms::sendConfigReport(uint8_t fport, Display* display, Downlink* downlink) {
  uint8_t payload[5];
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
      len = 5;
      break;
    case 14:
      payload[0] = (uint8_t)((config->getBattFullMv() - 1950) / 10);
      payload[1] = (uint8_t)((config->getBattLowMv() - 1950) / 10);
      len = 2;
      break;
    default:
      return RADIOLIB_ERR_NONE;
  }
  return send(fport, payload, len, display, downlink);
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
}

