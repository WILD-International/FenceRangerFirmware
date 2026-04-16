#include "comms.h"

#include "debug.h"
#include "helpers.h"

// utilities & vars to support ESP32 deep-sleep. The RTC_DATA_ATTR attribute
// puts these in to the RTC memory which is preserved during deep-sleep
RTC_DATA_ATTR uint16_t bootCount = 0;
RTC_DATA_ATTR uint16_t bootCountSinceUnsuccessfulJoin = 0;
RTC_DATA_ATTR uint8_t LWsession[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];

uint8_t dataUp[] = {0x42};
uint8_t dataDown[RADIOLIB_LORAWAN_MAX_DOWNLINK_SIZE + 1];
size_t downlinkLen = sizeof(dataDown);
LoRaWANEvent_t uplinkDetails;
LoRaWANEvent_t downlinkDetails;

// Helper function to display any issues
void radioDebug(bool isFail, const __FlashStringHelper* message, int state, bool Freeze) {
  if (isFail) {
    LOGPRINT(message);
    LOGPRINT("(");
    LOGPRINT(state);
    LOGLN(")");
    while (Freeze);
  }
}

Comms::Comms(Config* config)
    : config(config), radio(new RadioModule()), node(&radio, &Region, subBand) {
  bootCount++;
  LOGPRINT(F("Boot number: "));
  LOGLN(bootCount);
}

// "Inspired" from
// https://github.com/radiolib-org/radiolib-persistence/blob/main/examples/LoRaWAN_ESP32/LoRaWAN_ESP32.ino
bool Comms::lwActivate() {
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
    radiolib_state = node.setBufferSession(LWsession);  // send them to LoRaWAN stack

    // if we have booted more than once we should have a session to restore, so report any failure
    // otherwise no point saying there's been a failure when it was bound to fail with an empty
    // LWsession var.
    radioDebug((radiolib_state != RADIOLIB_ERR_NONE) && (bootCount > 1),
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
      return true;  //(radiolib_state);
    }

  } else {  // store has no key "nonces"
    LOGLN(F("No Nonces saved - starting fresh."));
  }

  // if we got here, there was no session to restore, so start trying to join
  radiolib_state = RADIOLIB_ERR_NETWORK_NOT_JOINED;
  while (radiolib_state != RADIOLIB_LORAWAN_NEW_SESSION) {
    LOGLN(F("Join ('login') to the LoRaWAN Network"));
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
      LOGLN(radiolib_state);

      // how long to wait before join attempts. This is an interim solution pending
      // implementation of TS001 LoRaWAN Specification section #7 - this doc applies to v1.0.4 &
      // v1.1 it sleeps for longer & longer durations to give time for any gateway issues to resolve
      // or whatever is interfering with the device <-> gateway airwaves.
      uint32_t sleepForSeconds = min((bootCountSinceUnsuccessfulJoin++ + 1UL) * 60UL, 3UL * 60UL);
      LOGPRINT(F("Boots since unsuccessful join: "));
      LOGLN(bootCountSinceUnsuccessfulJoin);
      LOGPRINT(F("Retrying join in "));
      LOGPRINT(sleepForSeconds);
      LOGLN(F(" seconds"));

      return false;  // gotoSleep(sleepForSeconds);

    }  // if activateOTAA state
  }  // while join

  LOGLN(F("Joined"));

  // reset the failed join count
  bootCountSinceUnsuccessfulJoin = 0;

  delay(1000);  // hold off off hitting the airwaves again too soon - an issue in the US

  // ##### close the store
  store.end();
  return true;  //(radiolib_state);
}

void Comms::begin() {
  LOGLN("[SX1262] Initializing ... ");
  // TODO make 20dBm instead of 10
  // TODO
  // TODO
  // TODO
  // Heltec library uses 1.8V for TCXO, instead of RadioLib default of 1.6V
  int radiolib_state =
      radio.begin(868.1, 125.0, 12, 7, RADIOLIB_SX126X_SYNC_WORD_PUBLIC, 10, 8, 1.8, false);
  if (radiolib_state == RADIOLIB_ERR_NONE) {
    LOGLN(F("radiolib begin success!"));
  } else {
    LOG("Failed to initialize radio, code: %d\n", radiolib_state);
    while (true) {
      delay(10);
    }
  }

  pinMode(LORA_PA_POWER, OUTPUT);
  digitalWrite(LORA_PA_POWER, LOW);
  pinMode(LORA_PA_EN, OUTPUT);
  digitalWrite(LORA_PA_EN, LOW);
  pinMode(LORA_PA_TX_EN, OUTPUT);
  digitalWrite(LORA_PA_TX_EN, LOW);

  if (config->hasAppKey()) {
    digitalWrite(LORA_PA_POWER, HIGH);
    digitalWrite(LORA_PA_EN, HIGH);
    digitalWrite(LORA_PA_TX_EN, HIGH);
    joined = lwActivate();
    digitalWrite(LORA_PA_TX_EN, LOW);
    digitalWrite(LORA_PA_EN, LOW);
    digitalWrite(LORA_PA_POWER, LOW);
  }
}

void Comms::saveSession() {
  // now save session to RTC memory
  uint8_t* persist = node.getBufferSession();
  memcpy(LWsession, persist, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
}
