// #include "config.h"

#define SOMETHING_TO_PREVENT_REORDERING_DEPENDENCIES_BY_FORMATTER
#include <Arduino.h>
#include <RadioLib.h>

// Based on selected board, automatically pick the correct radio config
#define RADIO_BOARD_AUTO

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

uint64_t joinEUI = 0x0000000000000000;
uint64_t devEUI = RADIOLIB_LORAWAN_DEV_EUI;
uint8_t appKey[] = {RADIOLIB_LORAWAN_APP_KEY};
uint8_t nwkKey[] = {RADIOLIB_LORAWAN_NWK_KEY};

LoRaWANNode node(&radio, &Region, subBand);

#define Boot_Key 0

#define Led 35
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(Boot_Key, INPUT_PULLUP);
  pinMode(Led, OUTPUT);

  Serial.print(F("[SX1262] Initializing ... "));

  // Heltec library uses 1.8V for TCXO, instead of RadioLib default of 1.6V
  int state =
      radio.begin(868.1, 125.0, 12, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 8, 1.8, false);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true) {
      delay(10);
    }
  }

#if defined(ARDUINO_heltec_wifi_lora_32_V3)
  if (radio.setDio2AsRfSwitch() != RADIOLIB_ERR_NONE) {
    Serial.println(F("Failed to set DIO2 as RF switch!"));
    while (true) {
      delay(10);
    }
  }
#endif

  node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);

  node.activateOTAA();

  // Enable the ADR algorithm (on by default which is preferable)
  node.setADR(true);
}

void loop() {
  if (digitalRead(Boot_Key) == 0) {
    digitalWrite(Led, HIGH);
    Serial.print("hello world !!! \n");
    delay(1000);
    digitalWrite(Led, LOW);
  }
  delay(100);
}
