// #include "config.h"

#define SOMETHING_TO_PREVENT_REORDERING_DEPENDENCIES_BY_FORMATTER
#include <Arduino.h>
#include <Preferences.h>
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

Preferences preferences;

uint64_t joinEUI = 0x0000000000000000;
uint64_t devEUI = 0x70B3D57ED0075374;  // random
// uint64_t devEUI = 0x7B1C000064E83369; // the v3 module that i have
uint8_t appKey[] = {0x90, 0xB8, 0x3E, 0x98, 0xA9, 0x37, 0x54, 0x53,
                    0xDC, 0xDC, 0xEB, 0x72, 0x96, 0xEF, 0x46, 0xB3};
uint8_t nwkKey[] = {0x25, 0xF5, 0x66, 0x70, 0xE0, 0x25, 0x46, 0x5B,
                    0x0B, 0x36, 0x6F, 0x77, 0x01, 0xBC, 0x7C, 0x06};

LoRaWANNode node(&radio, &Region, subBand);

#define Boot_Key 0
#define Led 35

void printHex(uint8_t num) {
  char hexCar[2];

  sprintf(hexCar, "%02X", num);
  Serial.print(hexCar);
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(Boot_Key, INPUT_PULLUP);
  pinMode(Led, OUTPUT);

  preferences.begin("fence");

  if (preferences.isKey("app_key")) {
    size_t len = preferences.getBytes("app_key", (void*)appKey, 16);
    for (int i = 0; i < sizeof(appKey); i++) {
      printHex(appKey[i]);
    }
    Serial.println();
  } else {
    preferences.putBytes("app_key", appKey, 16);
  }

  if (preferences.isKey("nwk_key")) {
    size_t len = preferences.getBytes("nwk_key", (void*)nwkKey, 16);
    for (int i = 0; i < sizeof(nwkKey); i++) {
      printHex(nwkKey[i]);
    }
    Serial.println();
  } else {
    preferences.putBytes("nwk_key", nwkKey, 16);
  }

  Serial.print(F("[SX1262] Initializing ... "));

  // Heltec library uses 1.8V for TCXO, instead of RadioLib default of 1.6V
  int state = radio.begin(868.1, 125.0, 12, 7, RADIOLIB_SX126X_SYNC_WORD_PUBLIC, 10, 8, 1.8, false);
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

  // node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);

  // node.activateOTAA();

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
