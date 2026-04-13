// #include "config.h"

#define SOMETHING_TO_PREVENT_REORDERING_DEPENDENCIES_BY_FORMATTER
#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#include "at_commands.h"
#include "board.h"
#include "button.h"
#include "config.h"
#include "debug.h"
#include "display_menu.h"
#include "measure_program.h"
#include "state.h"

// now include RadioBoards
// this must be included AFTER RadioLib!
#include <RadioBoards.h>

// create a new RadioLib module
// "Radio" will be set to the correct module type,
// and "RadioModule" will have the correct pins
Radio radio = new RadioModule();

// regional choices: EU868, US915, AU915, AS923, AS923_2, AS923_3, AS923_4, IN865, KR920, CN470
const LoRaWANBand_t Region = EU868;

// subband choice: for US915/AU915 set to 2, for CN470 set to 1, otherwise leave on 0
const uint8_t subBand = 0;

// Global objects

Config config;
State state;
Button button;
DisplayMenu displayMenu(&config);

// Programs
MeasureProgram measureProgram;

uint64_t joinEUI = 0x0000000000000000;

uint8_t dataUp[] = {0x42};
uint8_t dataDown[RADIOLIB_LORAWAN_MAX_DOWNLINK_SIZE + 1];
size_t downlinkLen = sizeof(dataDown);
LoRaWANEvent_t uplinkDetails;
LoRaWANEvent_t downlinkDetails;

LoRaWANNode node(&radio, &Region, subBand);
Preferences store;  // for radiolib persistent data

// utilities & vars to support ESP32 deep-sleep. The RTC_DATA_ATTR attribute
// puts these in to the RTC memory which is preserved during deep-sleep
RTC_DATA_ATTR uint16_t bootCount = 0;
RTC_DATA_ATTR uint16_t bootCountSinceUnsuccessfulJoin = 0;
RTC_DATA_ATTR uint8_t LWsession[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];

// put device in to lowest power deep-sleep mode
void gotoSleep(uint32_t seconds) {
  esp_sleep_enable_timer_wakeup(seconds * 1000UL * 1000UL);  // function uses uS
  LOGLN(F("Sleeping\n"));
  Serial.flush();

  esp_deep_sleep_start();

  // if this appears in the serial debug, we didn't go to sleep!
  // so take defensive action so we don't continually uplink
  Serial.println(F("\n\n### Sleep failed, delay of 5 minutes & then restart ###\n"));
  delay(5UL * 60UL * 1000UL);
  ESP.restart();
}

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

// "Inspired" from
// https://github.com/radiolib-org/radiolib-persistence/blob/main/examples/LoRaWAN_ESP32/LoRaWAN_ESP32.ino
int16_t lwActivate() {
  int16_t radiolib_state = RADIOLIB_ERR_UNKNOWN;

  // setup the OTAA session information
  node.beginOTAA(joinEUI, config.getDevEUIAsUint64(), NULL, config.getAppKey());

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
      return (radiolib_state);
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

      gotoSleep(sleepForSeconds);

    }  // if activateOTAA state
  }  // while join

  LOGLN(F("Joined"));

  // reset the failed join count
  bootCountSinceUnsuccessfulJoin = 0;

  delay(1000);  // hold off off hitting the airwaves again too soon - an issue in the US

  // ##### close the store
  store.end();
  return (radiolib_state);
}

void setup() {
  bootCount++;
  Serial.begin(115200);
  delay(10000);
  config.load();
  button.begin(Boot_Key);
  displayMenu.begin();
  LOG("Firmware version: %s\n", GIT_TAG);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  // store.begin("radiolib");
  // store.remove("nonces");  // for testing - remove any saved nonces to force a fresh join each
  // // time
  // store.end();

  Serial.println("[SX1262] Initializing ... ");
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

  if (config.hasAppKey()) {
    digitalWrite(LORA_PA_POWER, HIGH);
    digitalWrite(LORA_PA_EN, HIGH);
    digitalWrite(LORA_PA_TX_EN, HIGH);
    lwActivate();
    digitalWrite(LORA_PA_TX_EN, LOW);
    digitalWrite(LORA_PA_EN, LOW);
    digitalWrite(LORA_PA_POWER, LOW);
  }

  // now save session to RTC memory
  uint8_t* persist = node.getBufferSession();
  memcpy(LWsession, persist, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);

  // gotoSleep(30);
}

void loop() {
  static uint32_t last_measurement_finish_time = 0;
  if (measureProgram.isFinished() && state.measurement_running) {
    last_measurement_finish_time = millis();
    state.measurement_running = false;
    LOGLN("done");
  }
  if (millis() - last_measurement_finish_time > 1000 && !state.measurement_running) {
    state.measurement_running = true;
    LOGLN("starting (again)");
    measureProgram.begin();
  }

  measureProgram.update(state);
  ButtonEvent event = button.update();
  if (event != ButtonEvent::None) {
    LOG("Button event: %d\n", (int)event);
    displayMenu.buttonEvent(event);
  }

  while (Serial.available()) {
    char c = Serial.read();
    handleATSerial(c, config, state);
  }

  displayMenu.update(state);
  delay(1);
}

// Serial.println("Program finished, going to sleep");
// while (true) {
//   delay(1000);
// }
//
// // put your setup code here, to run once:
// // Serial.begin(115200);
// pinMode(Boot_Key, INPUT_PULLUP);
// pinMode(Led, OUTPUT);
// pinMode(LORA_PA_POWER, OUTPUT);
// digitalWrite(LORA_PA_POWER, LOW);
// pinMode(LORA_PA_EN, OUTPUT);
// digitalWrite(LORA_PA_EN, LOW);
// pinMode(LORA_PA_TX_EN, OUTPUT);
// digitalWrite(LORA_PA_TX_EN, LOW);
//
// // if (preferences.isKey("app_key")) {
// //   size_t len = preferences.getBytes("app_key", (void*)appKey, 16);
// //   for (int i = 0; i < sizeof(appKey); i++) {
// //     printHex(appKey[i]);
// //   }
// //   Serial.println();
// // } else {
// //   preferences.putBytes("app_key", appKey, 16);
// // }
//
// // Start I2C on the correct pins
// Wire.begin(OLED_SDA, OLED_SCL);
//
// // Initialize display (0x3C is the typical I2C address)
// if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
//   Serial.println("SSD1306 init failed");
//   while (true);
// }
//
// while (true) {
//   display.clearDisplay();
//   display.setTextSize(1);
//   display.setTextColor(SSD1306_WHITE);
//   display.setCursor(0, 0);
//   if (Serial.isConnected()) {
//     display.println("HAS SERIAL");
//   } else {
//     display.println("NO SERIAL");
//   }
//   display.display();
// }
//
// while (true) {
//   console.update();
// }

// Serial.println("[SX1262] Initializing ... ");
//
// // Heltec library uses 1.8V for TCXO, instead of RadioLib default of 1.6V
// int state = radio.begin(868.1, 125.0, 12, 7, RADIOLIB_SX126X_SYNC_WORD_PUBLIC, 10, 8, 1.8,
// false); if (state == RADIOLIB_ERR_NONE) {
//   Serial.println(F("success!"));
// } else {
//   Serial.print(F("failed, code "));
//   Serial.println(state);
//   while (true) {
//     delay(10);
//   }
// }
//
// // #if defined(ARDUINO_heltec_wifi_lora_32_V3)
// if (radio.setDio2AsRfSwitch() != RADIOLIB_ERR_NONE) {
//   Serial.println(F("Failed to set DIO2 as RF switch!"));
//   while (true) {
//     delay(10);
//   }
// }
// // #endif
//
// // Start I2C on the correct pins
// Wire.begin(OLED_SDA, OLED_SCL);
//
// // Initialize display (0x3C is the typical I2C address)
// if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
//   Serial.println("SSD1306 init failed");
//   while (true);
// }
//
// display.clearDisplay();
// display.setTextSize(1);
// display.setTextColor(SSD1306_WHITE);
// display.setCursor(0, 0);
// display.println(arrayToUint64(devEUI), HEX);
// display.display();
//
// while (true) {
//   delay(5000);
//   Serial.println("Sleeping");
// }
//
// Serial.println(F("Beginning OTAA"));
// node.beginOTAA(joinEUI, arrayToUint64(devEUI), NULL, appKey);
//
// Serial.println(F("Activate OTAA"));
// node.activateOTAA();
// Serial.println(F("Done"));
//
// // Enable the ADR algorithm (on by default which is preferable)
// node.setADR(true);
// }

// // helper function to display a byte array
// void arrayDump(uint8_t* buffer, uint16_t len) {
//   for (uint16_t c = 0; c < len; c++) {
//     char b = buffer[c];
//     if (b < 0x10) {
//       Serial.print('0');
//     }
//     Serial.print(b, HEX);
//   }
//   Serial.println();
// }

// void loop() {
//   digitalWrite(LORA_PA_EN, HIGH);
//   digitalWrite(LORA_PA_TX_EN, HIGH);
//
//   int16_t state = node.sendReceive(dataUp, sizeof(dataUp), 1, dataDown, &downlinkLen, false,
//                                    &uplinkDetails, &downlinkDetails);
//
//   digitalWrite(LORA_PA_EN, LOW);
//   digitalWrite(LORA_PA_TX_EN, LOW);
//
//   // Check if a downlink was received
//   // (state 0 = no downlink, state 1/2 = downlink in window Rx1/Rx2)
//   if (state > 0) {
//     Serial.println(F("Received a downlink"));
//     // Did we get a downlink with data for us
//     if (downlinkLen > 0) {
//       Serial.println(F("Downlink data: "));
//       arrayDump(dataDown, downlinkLen);
//     } else {
//       Serial.println(F("<MAC commands only>"));
//     }
//
//     // print RSSI (Received Signal Strength Indicator)
//     Serial.print(F("[LoRaWAN] RSSI:\t\t"));
//     Serial.print(radio.getRSSI());
//     Serial.println(F(" dBm"));
//
//     // print SNR (Signal-to-Noise Ratio)
//     Serial.print(F("[LoRaWAN] SNR:\t\t"));
//     Serial.print(radio.getSNR());
//     Serial.println(F(" dB"));
//
//     // print extra information about the event
//     Serial.println(F("[LoRaWAN] Event information:"));
//     Serial.print(F("[LoRaWAN] Confirmed:\t"));
//     Serial.println(downlinkDetails.confirmed);
//     Serial.print(F("[LoRaWAN] Confirming:\t"));
//     Serial.println(downlinkDetails.confirming);
//     Serial.print(F("[LoRaWAN] Datarate:\t"));
//     Serial.println(downlinkDetails.datarate);
//     Serial.print(F("[LoRaWAN] Frequency:\t"));
//     Serial.print(downlinkDetails.freq, 3);
//     Serial.println(F(" MHz"));
//     Serial.print(F("[LoRaWAN] Frame count:\t"));
//     Serial.println(downlinkDetails.fCnt);
//     Serial.print(F("[LoRaWAN] Port:\t\t"));
//     Serial.println(downlinkDetails.fPort);
//     Serial.print(F("[LoRaWAN] Time-on-air: \t"));
//     Serial.print(node.getLastToA());
//     Serial.println(F(" ms"));
//     Serial.print(F("[LoRaWAN] Rx window: \t"));
//     Serial.println(state);
//
//     uint8_t margin = 0;
//     uint8_t gwCnt = 0;
//     if (node.getMacLinkCheckAns(&margin, &gwCnt) == RADIOLIB_ERR_NONE) {
//       Serial.print(F("[LoRaWAN] LinkCheck margin:\t"));
//       Serial.println(margin);
//       Serial.print(F("[LoRaWAN] LinkCheck count:\t"));
//       Serial.println(gwCnt);
//     }
//
//     uint32_t timestamp = 0;
//     uint16_t milliseconds = 0;
//     if (node.getMacDeviceTimeAns(&timestamp, &milliseconds, true) == RADIOLIB_ERR_NONE) {
//       Serial.print(F("[LoRaWAN] DeviceTime Unix:\t"));
//       Serial.println(timestamp);
//       Serial.print(F("[LoRaWAN] DeviceTime frac:\t"));
//       Serial.print(milliseconds);
//       Serial.println(F(" ms"));
//     }
//
//   } else {
//     Serial.println(F("[LoRaWAN] No downlink received"));
//   }
//
//   // // Serial.println("Loop");
//   // if (digitalRead(Boot_Key) == 0) {
//   //   digitalWrite(Led, HIGH);
//   //   Serial.print("hello world !!! \n");
//   //   delay(1000);
//   //   digitalWrite(Led, LOW);
//   // }
//   delay(30000);
// }
